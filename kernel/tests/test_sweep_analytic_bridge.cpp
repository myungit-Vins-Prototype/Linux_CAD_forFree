#include "fk_body_check.h"
#include "fk_mass.h"
#include "fk_sweep.h"
#include "fk_sweep_analytic_bridge.h"
#include "fk_sweep_atlas.h"
#include "fk_test.h"
using namespace ForgeCad::Kernel;
using namespace ForgeCad::Kernel::detail;
namespace {
ProfileRegion bridgeDisk(double r) {
    ProfileRegion p;
    p.outer = {{{std::make_shared<Circle<2>>(Vec2(), Vec2(1, 0), Vec2(0, 1), r), {0, kTwoPi}}}};
    return p;
}
} // namespace
FK_TEST(SweepAnalyticBridgeBoundsTiltedCylinder) {
    const Vec3 d = normalized(Vec3(0, 1, 1));
    const Frame3 frame(Vec3(1, 2, 3), d, Vec3(.3, .7, .2));
    const Circle<3> section(frame.origin(), frame.xDir(), frame.yDir(), .4);
    const auto chart = translationChart(section, {0, kTwoPi}, 5 * d);
    const CylindricalSurface actual(frame, .4);
    const auto bridge = boundSweepAnalyticBridge(section, {0, kTwoPi}, *chart, 5 * d, false, {}, {}, 0, &actual);
    FK_CHECK(bridge.bounded);
    FK_CHECK(bridge.poleError > 0 && bridge.poleError < 1e-8);
    FK_CHECK(bridge.weightError > 0 && bridge.weightError < 1e-8);
}
FK_TEST(SweepAnalyticBridgeBoundsRotationCarriers) {
    const Line<3> line(Vec3(2, 0, 0), Vec3(0, 0, 1));
    const auto chart = rotationChart(line, {0, 1}, Vec3(), Vec3(0, 0, 1), 2);
    const CylindricalSurface cylinder(Frame3(), 2);
    const auto a = boundSweepAnalyticBridge(line, {0, 1}, *chart, {}, true, {}, Vec3(0, 0, 1), 2, &cylinder);
    FK_CHECK(a.bounded && a.poleError < 1e-8);
    const Circle<3> circle(Vec3(3, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1), .4);
    const auto torusChart = rotationChart(circle, {0, kTwoPi}, {}, Vec3(0, 0, 1), 2);
    const ToroidalSurface torus(Frame3(), 3, .4);
    const auto b = boundSweepAnalyticBridge(circle, {0, kTwoPi}, *torusChart, {}, true, {}, Vec3(0, 0, 1), 2, &torus);
    FK_CHECK(b.bounded && b.poleError < 1e-8 && b.weightError < 1e-8);
    const Line<3> coneLine(Vec3(2, 0, 0), Vec3(1, 0, 1));
    const auto coneChart = rotationChart(coneLine, {0, std::sqrt(2.)}, {}, Vec3(0, 0, 1), 2);
    const ConicalSurface cone(Frame3(), kPi / 4, 2);
    const auto c =
        boundSweepAnalyticBridge(coneLine, {0, std::sqrt(2.)}, *coneChart, {}, true, {}, Vec3(0, 0, 1), 2, &cone);
    FK_CHECK(c.bounded && c.poleError < 1e-8);
}
FK_TEST(SweepAnalyticBridgeFallbackIsExplicit) {
    const BSplineCurve<3> generic(1, {0, 0, 1, 1}, {Vec3(), Vec3(1, 0, 0)});
    const auto chart = translationChart(generic, {0, 1}, Vec3(0, 0, 1));
    FK_CHECK(!boundSweepAnalyticBridge(generic, {0, 1}, *chart, Vec3(0, 0, 1)).bounded);
    const Circle<3> circle({}, Vec3(1, 0, 0), Vec3(0, 1, 0), 1);
    FK_CHECK(!boundSweepAnalyticBridge(circle, {-10, -9}, *chart, Vec3(0, 0, 1)).bounded);
}
FK_TEST(SweepAnalyticBridgePreservesLegacyCylinder) {
    const Vec3 d = normalized(Vec3(0, 1, 1));
    const Frame3 frame(Vec3(1, 2, 3), d, Vec3(.3, .7, .2));
    const Body body = sweepRegions(frame, {bridgeDisk(.4)}, {{std::make_shared<Line<3>>(frame.origin(), d), {0, 5}}});
    FK_CHECK(checkBody(body).empty());
    int cylinders = 0;
    for (FaceId face : body.faces()) {
        const auto type = body.face(face).surface->type();
        FK_CHECK(type == SurfaceType::Cylinder || type == SurfaceType::Plane);
        cylinders += type == SurfaceType::Cylinder;
    }
    FK_CHECK(cylinders == 2);
    FK_CHECK_NEAR(massProperties(body).volume, kPi * .16 * 5, 1e-9);
}
FK_TEST(SweepAnalyticBridgePreservesLegacyTorus) {
    const auto path = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 3);
    const Frame3 frame(path->point(0), path->derivative(0), Vec3(0, 0, 1));
    const Body body = sweepRegions(frame, {bridgeDisk(.4)}, {{path, {0, 2}}});
    FK_CHECK(checkBody(body).empty());
    int tori = 0;
    for (FaceId face : body.faces())
        tori += body.face(face).surface->type() == SurfaceType::Torus;
    FK_CHECK(tori == 2);
    FK_CHECK_NEAR(massProperties(body).volume, kPi * .16 * 6, 1e-9);
}
FK_TEST(SweepAnalyticBridgeFallbackPreservesLongitudinalParameters) {
    const auto arc = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 3);
    const Frame3 oblique(arc->point(0), normalized(Vec3(0, 1, .2)), Vec3(1, 0, 0));
    ProfileRegion square;
    const Vec2 corners[] = {{-.2, -.2}, {.2, -.2}, {.2, .2}, {-.2, .2}};
    for (int i = 0; i < 4; ++i) {
        const Vec2 a = corners[i], d = corners[(i + 1) % 4] - a;
        square.outer.segments.push_back({std::make_shared<Line<2>>(a, d), {0, norm(d)}});
    }
    const auto solid = sweepRegions(oblique, {square}, {{arc, {0, .8}}});
    FK_CHECK(checkBody(solid).empty());
    FK_CHECK(massProperties(solid).volume > 0);
    ProfileLoop profile;
    profile.segments = {{std::make_shared<BSplineCurve<2>>(2, std::vector<double>{0, 0, 0, 1, 1, 1},
                                                           std::vector<Vec2>{{-1, 0}, {0, .2}, {1, 0}}),
                         {0, 1}}};
    const auto sheet = sweepChains(Frame3(Vec3(), Vec3(0, 1, 0), Vec3(1, 0, 0)), {profile},
                                   {{std::make_shared<Line<3>>(Vec3(), Vec3(0, 0, 1)), {0, 2}}});
    FK_CHECK(sheet.isSheet());
    FK_CHECK(checkBody(sheet).empty());
    double area = 0;
    for (auto face : sheet.faces())
        area += faceArea(sheet, face);
    FK_CHECK_NEAR(area, 4, 1e-10);
}
