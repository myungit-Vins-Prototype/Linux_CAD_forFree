#include "fk_cancel.h"
#include "fk_sweep_surface_validation.h"
#include "fk_test.h"
#include <cmath>
using namespace ForgeCad::Kernel;
using namespace ForgeCad::Kernel::detail;
namespace {
SweepSurfaceChart rectangle(double x0, double x1, double y0, double y1) {
    SweepSurfaceChart c;
    c.surface =
        std::make_shared<BSplineSurface>(1, 1, std::vector<double>{0, 0, 1, 1}, std::vector<double>{0, 0, 1, 1}, 2, 2,
                                         std::vector<Vec3>{{x0, y0, 0}, {x0, y1, 0}, {x1, y0, 0}, {x1, y1, 0}});
    c.u = {x0, x1};
    c.v = {y0, y1};
    return c;
}
bool certified(const std::vector<SweepSurfaceChart> &charts) {
    SweepSurfaceValidationOptions options;
    options.maxVisits = 2000;
    options.maxDepth = 8;
    return validateSweepSurfaces(charts, options).status == SweepSurfaceValidationStatus::Certified;
}
} // namespace
FK_TEST(SweepAtlasAcceptsRegularAndCreasedSurfaces) {
    auto a = rectangle(0, 1, 0, 1), b = rectangle(1, 2, 0, 1);
    FK_CHECK(certified({a}));
    FK_CHECK(certified({a, b}));
    b.surface = std::make_shared<BSplineSurface>(1, 1, std::vector<double>{0, 0, 1, 1}, std::vector<double>{0, 0, 1, 1},
                                                 2, 2, std::vector<Vec3>{{1, 0, 0}, {1, 1, 0}, {1, 0, 1}, {1, 1, 1}});
    FK_CHECK(certified({a, b}));
    const auto contact = boundSweepSurfaceCapContact(a, Frame3({0, 0, 0}, {0, 1, 0}, {1, 0, 0}),
                                                     SweepSurfaceBoundary::VMin, {0, 1}, {0, 1}, 1e-7);
    FK_CHECK(contact.confined);
    FK_CHECK(contact.deviation <= 1e-7);
}
FK_TEST(SweepAtlasChecksDeclaredPeriodicSeams) {
    auto falsePeriodic = rectangle(0, 1, 0, 1);
    falsePeriodic.uPeriod = 1;
    FK_CHECK(!certified({falsePeriodic}));
    std::vector<SweepSurfaceChart> ring;
    for (int k = 0; k < 4; ++k) {
        const double q = std::sqrt(.5);
        const Vec3 r[4] = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}};
        const Vec3 x = r[k], z = r[(k + 1) % 4], m = x + z;
        std::vector<Vec3> poles;
        for (Vec3 v : {x, m, z}) {
            poles.push_back(v);
            poles.push_back(v + Vec3(0, 0, 1));
        }
        SweepSurfaceChart c;
        c.surface = std::make_shared<BSplineSurface>(2, 1, std::vector<double>{0, 0, 0, 1, 1, 1},
                                                     std::vector<double>{0, 0, 1, 1}, 3, 2, poles,
                                                     std::vector<double>{1, 1, q, q, 1, 1});
        c.u = {double(k), double(k + 1)};
        c.v = {0, 1};
        c.uPeriod = 4;
        ring.push_back(c);
    }
    FK_CHECK(certified(ring));
}
FK_TEST(SweepAtlasRejectsCoincidentAndOverlappingSheets) {
    const auto a = rectangle(0, 1, 0, 1);
    FK_CHECK(!certified({a, a})); // duplicate parameter rectangle
    auto b = a;
    b.family = 1;
    FK_CHECK(!certified({a, b})); // distinct topological sheets
    b = a;
    b.u = {1, 2};
    FK_CHECK(!certified({a, b})); // folded back, same family
    b = rectangle(.5, 1.5, 0, 1);
    b.u = {1, 2};
    FK_CHECK(!certified({a, b})); // overlap and discontinuous join
}
FK_TEST(SweepAtlasNumericalAndBudgetFailuresStayUnresolved) {
    for (double scale : {1e-200, 1e200}) {
        auto c = rectangle(0, scale, 0, scale);
        c.u = {0, 1};
        c.v = {0, 1};
        FK_CHECK(!certified({c}));
    }
    SweepSurfaceValidationOptions options;
    options.maxVisits = 0;
    FK_CHECK(validateSweepSurfaces({rectangle(0, 1, 0, 1)}, options).status ==
             SweepSurfaceValidationStatus::Unresolved);
    auto uncertain = rectangle(0, 1, 0, 1);
    uncertain.weightError = 1;
    FK_CHECK(!certified({uncertain}));
}
FK_TEST(SweepAtlasPropagatesCancellation) {
    auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool cancelled = false;
    try {
        validateSweepSurfaces({rectangle(0, 1, 0, 1)});
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
}
FK_TEST(SweepAtlasInternalDiscontinuityIsRejected) {
    bool rejected = false;
    try {
        auto c = rectangle(0, 1, 0, 1);
        c.surface = std::make_shared<BSplineSurface>(
            1, 1, std::vector<double>{0, 0, .5, .5, 1, 1}, std::vector<double>{0, 0, 1, 1}, 4, 2,
            std::vector<Vec3>{{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}, {0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}});
        rejected = !certified({c});
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    FK_CHECK(rejected);
}
FK_TEST(SweepAtlasDetectsFoldInsideOneCarrier) {
    auto c = rectangle(0, 1, 0, 1);
    c.surface = std::make_shared<BSplineSurface>(1, 1, std::vector<double>{0, 0, 1, 1}, std::vector<double>{0, 0, 1, 1},
                                                 2, 2, std::vector<Vec3>{{0, 0, 0}, {1, 1, 0}, {1, 0, 0}, {0, 1, 0}});
    FK_CHECK(!certified({c}));
    auto bounded = rectangle(0, 1, 0, 1);
    bounded.poleError = 1e-12;
    FK_CHECK(certified({bounded}));
}
FK_TEST(SweepAtlasQueryOwnsCarrierAndRemainsCancellable) {
    const auto makeQuery = [] { return SweepSurfaceQuery(rectangle(0, 1, 0, 1)); };
    const auto query = makeQuery();
    const auto copy = query;
    const auto projection = copy.projection(Frame3(), {.25, .75}, {.125, .875});
    FK_CHECK(projection.resolved);
    FK_CHECK(projection.lo.x() <= .25 && projection.hi.x() >= .75);
    FK_CHECK(projection.lo.y() <= .125 && projection.hi.y() >= .875);
    FK_CHECK(projection.height.lo <= 0 && projection.height.hi >= 0);
    const auto contact =
        query.capContact(Frame3({0, 0, 0}, {0, 1, 0}, {1, 0, 0}), SweepSurfaceBoundary::VMin, {0, 1}, {0, 1}, 1e-7);
    FK_CHECK(contact.confined);
    auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool cancelled = false;
    try {
        copy.projection(Frame3(), {0, 1}, {0, 1});
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
}
