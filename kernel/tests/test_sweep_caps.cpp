#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_curve_ops.h"
#include "fk_sweep.h"
#include "fk_sweep_caps.h"
#include "fk_test.h"
#include <limits>
using namespace ForgeCad::Kernel;
using namespace ForgeCad::Kernel::detail;
ProfileLoop loop(Vec2 c, double r) {
    return {{{std::make_shared<Circle<2>>(c, Vec2(1, 0), Vec2(0, 1), r), {0, kTwoPi}}}};
}
FK_TEST(CapTrimAnnulusHoleAndIsland) {
    const CapDomainIndex annulus({loop(Vec2(), 2), reversed(loop(Vec2(), 1))}, Frame3(), 1e-7);
    FK_CHECK(annulus.classifyPoint(Vec2()) == CapBoxLocation::Outside);
    FK_CHECK(annulus.classifyPoint(Vec2(1.5, 0)) == CapBoxLocation::Inside);
    FK_CHECK(annulus.classifyBox(Vec2(-.2, -.2), Vec2(.2, .2)) == CapBoxLocation::Outside);
    FK_CHECK(annulus.classifyPoint(Vec2(3, 0)) == CapBoxLocation::Outside);
    FK_CHECK(annulus.classifyPoint(Vec2(2, 0)) == CapBoxLocation::Unresolved);
    FK_CHECK(annulus.classifyPoint(Vec2(), 0) == CapBoxLocation::Inside);
    FK_CHECK(annulus.classifyPoint(Vec2(), 1) == CapBoxLocation::Inside);
    const CapDomainIndex island({loop(Vec2(), .3)}, Frame3(), 1e-7);
    FK_CHECK(capDomainsDisjoint(annulus, island, 1e-7));
    FK_CHECK(!capDomainsDisjoint(annulus, annulus, 1e-7));
}
FK_TEST(CapTrimPoseDifferentPlanesAndBody) {
    const Frame3 pose(Vec3(13, -7, 5), normalized(Vec3(1, 2, 3)), Vec3(0, 1, 0));
    const CapDomainIndex a({loop(Vec2(), 1)}, pose, 1e-7);
    Frame3 shifted(pose.origin() + 3 * pose.zDir(), pose.zDir(), pose.xDir());
    const CapDomainIndex b({loop(Vec2(), 1)}, shifted, 1e-7);
    FK_CHECK(capDomainsDisjoint(a, b, 1e-7));
    ProfileRegion disk;
    disk.outer = loop(Vec2(), 1);
    const auto body = sweepRegions(pose, {disk}, {{std::make_shared<Line<3>>(pose.origin(), pose.zDir()), {0, 3}}});
    int count = 0;
    for (auto f : body.faces())
        if (body.face(f).surface->type() == SurfaceType::Plane) {
            CapDomainIndex cap(body, f, 1e-7);
            const auto uv = cap.frame().toLocal(pose.origin());
            (void)uv;
            FK_CHECK(cap.classifyPoint(Vec2()) == CapBoxLocation::Inside);
            ++count;
        }
    FK_CHECK(count == 2);
}
FK_TEST(CapTrimBudgetAndCancel) {
    const CapDomainIndex a({loop(Vec2(), 1)}, Frame3(), 1e-7);
    CapDomainOptions zero;
    zero.maxVisits = 0;
    FK_CHECK(a.classifyPoint(Vec2(), -1, zero) == CapBoxLocation::Unresolved);
    FK_CHECK(!capDomainsDisjoint(a, a, 1e-7, zero));
    auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool cancelled = false;
    try {
        a.classifyPoint(Vec2());
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
}
FK_TEST(CapTrimRationalSplineAndHoleOrientation) {
    const auto square = std::make_shared<BSplineCurve<2>>(
        1, std::vector<double>{0, 0, 1, 2, 3, 4, 4}, std::vector<Vec2>{{-2, -2}, {2, -2}, {2, 2}, {-2, 2}, {-2, -2}},
        std::vector<double>{1, 2, 3, 2, 1});
    ProfileLoop outer;
    outer.segments = {{square, {0, 4}}};
    const CapDomainIndex cap({outer, loop(Vec2(), 1)}, Frame3(), 1e-7);
    FK_CHECK(cap.classifyPoint(Vec2()) == CapBoxLocation::Outside);
    FK_CHECK(cap.classifyPoint(Vec2(1.5, .123)) == CapBoxLocation::Inside);
    FK_CHECK(cap.classifyPoint(Vec2(-3, 0)) == CapBoxLocation::Outside);
    std::size_t budget = 3;
    CapDomainOptions options;
    const auto value = cap.classifyBoxBudget(Vec2(), Vec2(), -1, options, budget);
    FK_CHECK(value == CapBoxLocation::Unresolved && budget == 0);
    const auto nonclamped =
        std::make_shared<BSplineCurve<2>>(1, std::vector<double>{-1, 0, 1, 2}, std::vector<Vec2>{{-1, 0}, {1, 0}});
    ProfileLoop unsupported;
    unsupported.segments = {{nonclamped, {0, 1}}};
    FK_CHECK(CapDomainIndex({unsupported}, Frame3(), 0).classifyPoint(Vec2(0, 3)) == CapBoxLocation::Unresolved);
}
FK_TEST(CapTrimNearPlanesAndCrossing) {
    const CapDomainIndex a({loop(Vec2(), 1)}, Frame3(), 1e-7);
    const Frame3 crossing(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0));
    const CapDomainIndex b({loop(Vec2(), 1)}, crossing, 1e-7);
    FK_CHECK(!capDomainsDisjoint(a, b, 1e-7));
    const CapDomainIndex distant({loop(Vec2(), 1)}, Frame3(Vec3(0, 0, 2e-6), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1e-7);
    FK_CHECK(capDomainsDisjoint(a, distant, 1e-7));
    const CapDomainIndex near({loop(Vec2(), 1)}, Frame3(Vec3(0, 0, 1e-8), Vec3(0, 0, 1), Vec3(1, 0, 0)), 1e-7);
    FK_CHECK(!capDomainsDisjoint(a, near, 1e-7));
}
FK_TEST(CapTrimOutwardKnotInsertionAndSubrange) {
    auto arch = std::make_shared<BSplineCurve<2>>(2, std::vector<double>{0, 0, 0, .5, 1, 1, 1},
                                                  std::vector<Vec2>{{-2, -2}, {-2, 2}, {2, 2}, {2, -2}});
    ProfileLoop profile;
    profile.segments = {
        {arch, {0, .25}}, {arch, {.25, 1}}, {std::make_shared<Line<2>>(Vec2(2, -2), Vec2(-1, 0)), {0, 4}}};
    const CapDomainIndex cap({profile}, Frame3(), 1e-7);
    FK_CHECK(cap.classifyPoint(Vec2()) == CapBoxLocation::Inside);
    FK_CHECK(cap.classifyPoint(Vec2(0, 3)) == CapBoxLocation::Outside);
    FK_CHECK(cap.classifyPoint(Vec2(-3, 0)) == CapBoxLocation::Outside);
}
SweepSurfaceChart sidePlane(double y, double z0, double z1) {
    SweepSurfaceChart c;
    c.family = 7;
    c.u = {0, 1};
    c.v = {0, 2};
    c.surface =
        std::make_shared<BSplineSurface>(1, 1, std::vector<double>{0, 0, 1, 1}, std::vector<double>{0, 0, 1, 1}, 2, 2,
                                         std::vector<Vec3>{{-.5, y, z0}, {-.5, y, z1}, {.5, y, z0}, {.5, y, z1}});
    return c;
}
FK_TEST(CapSideGenealogyAndOtherFamilies) {
    CapDomainIndex cap({loop(Vec2(), 1)}, Frame3(), 1e-7);
    const auto edge = sidePlane(0, 0, 2);
    FK_CHECK(validateSweepSurfaceCap({edge}, cap, {7}, 0, 1e-7).status == SweepSurfaceValidationStatus::Certified);
    SweepSurfaceValidationOptions limited;
    limited.maxVisits = 2000;
    limited.maxDepth = 8;
    FK_CHECK(validateSweepSurfaceCap({edge}, cap, {}, 0, 1e-7, limited).status ==
             SweepSurfaceValidationStatus::Unresolved);
    auto crossing = sidePlane(0, -1, 1);
    crossing.family = 9;
    FK_CHECK(validateSweepSurfaceCap({edge, crossing}, cap, {7}, 0, 1e-7, limited).status ==
             SweepSurfaceValidationStatus::Unresolved);
    auto outside = sidePlane(3, -1, 1);
    outside.family = 9;
    FK_CHECK(validateSweepSurfaceCap({edge, outside}, cap, {7}, 0, 1e-7).status ==
             SweepSurfaceValidationStatus::Certified);
}
FK_TEST(CapSideHoleBudgetAndCancellation) {
    CapDomainIndex annulus({loop(Vec2(), 2), loop(Vec2(), 1)}, Frame3(), 1e-7);
    const auto crossingHole = sidePlane(0, -1, 1);
    FK_CHECK(validateSweepSurfaceCap({crossingHole}, annulus, {}, 0, 1e-7).status ==
             SweepSurfaceValidationStatus::Certified);
    SweepSurfaceValidationOptions zero;
    zero.maxVisits = 0;
    FK_CHECK(validateSweepSurfaceCap({crossingHole}, annulus, {}, 0, 1e-7, zero).status ==
             SweepSurfaceValidationStatus::Unresolved);
    auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool cancelled = false;
    try {
        validateSweepSurfaceCap({crossingHole}, annulus, {}, 0, 1e-7);
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
}
