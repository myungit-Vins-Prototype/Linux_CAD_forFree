#include "fk_curve.h"
#include "fk_surface.h"
#include "fk_cancel.h"
#include "fk_test.h"
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
struct Consumer {
    const char *name;
    std::function<std::vector<double>(const Interval &)> breaks;
};
std::vector<Consumer> consumers() {
    const auto circle2 = std::make_shared<Circle<2>>(Vec2(), Vec2(1, 0), Vec2(0, 1), 3);
    const auto circle3 = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 3);
    const auto ellipse2 = std::make_shared<Ellipse<2>>(Vec2(), Vec2(1, 0), Vec2(0, 1), 4, 2);
    const auto ellipse3 = std::make_shared<Ellipse<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 4, 2);
    const auto cylinder = std::make_shared<CylindricalSurface>(Frame3(), 3);
    const auto cone = std::make_shared<ConicalSurface>(Frame3(), .3, 3);
    const auto sphere = std::make_shared<SphericalSurface>(Frame3(), 3);
    const auto torus = std::make_shared<ToroidalSurface>(Frame3(), 5, 1);
    const auto extrusion = std::make_shared<ExtrusionSurface>(circle3, Vec3(0, 0, 1));
    const auto meridian = std::make_shared<Circle<3>>(Vec3(5, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1), 1);
    const auto revolution = std::make_shared<RevolutionSurface>(meridian, Vec3(), Vec3(0, 0, 1));
    return {
        {"detail", [](const Interval &r) { return detail::quadrantBreakpoints(r); }},
        {"Circle2", [circle2](const Interval &r) { return circle2->breakpoints(r); }},
        {"Circle3", [circle3](const Interval &r) { return circle3->breakpoints(r); }},
        {"Ellipse2", [ellipse2](const Interval &r) { return ellipse2->breakpoints(r); }},
        {"Ellipse3", [ellipse3](const Interval &r) { return ellipse3->breakpoints(r); }},
        {"CylinderU", [cylinder](const Interval &r) { return cylinder->uBreakpoints(r); }},
        {"ConeU", [cone](const Interval &r) { return cone->uBreakpoints(r); }},
        {"SphereU", [sphere](const Interval &r) { return sphere->uBreakpoints(r); }},
        {"SphereV", [sphere](const Interval &r) { return sphere->vBreakpoints(r); }},
        {"TorusU", [torus](const Interval &r) { return torus->uBreakpoints(r); }},
        {"TorusV", [torus](const Interval &r) { return torus->vBreakpoints(r); }},
        {"ExtrusionCircleU", [extrusion](const Interval &r) { return extrusion->uBreakpoints(r); }},
        {"RevolutionU", [revolution](const Interval &r) { return revolution->uBreakpoints(r); }},
        {"RevolutionCircleV", [revolution](const Interval &r) { return revolution->vBreakpoints(r); }}
    };
}
void expectBits(const Consumer &consumer, const Interval &range, const std::vector<double> &expected) {
    const auto actual = consumer.breaks(range);
    const bool same = actual.size() == expected.size()
        && std::memcmp(actual.data(), expected.data(), actual.size() * sizeof(double)) == 0;
    if (!same) fktest::reportFailure(__FILE__, __LINE__, std::string(consumer.name) + ": breakpoint bytes changed");
}
}

// IMPORTANT: the unchanged baseline must run ONLY filter "SafeControls".
// Other groups include inputs that can hang its original floating-index loop.
FK_TEST(QuadrantBreakpointsSafeControls) {
    const double q = kHalfPi;
    struct Fixture { Interval range; std::vector<double> expected; };
    // Explicit legacy golden values, without a copied index-generation loop.
    // Signed zero is intentional: ceil(negative fractional index) yields -0.
    const std::vector<Fixture> fixtures{
        {{0, kTwoPi}, {0, q, 2 * q, 3 * q, kTwoPi}},
        {{-kPi, kPi}, {-kPi, -q, 0, q, kPi}},
        {{-.25, .25}, {-.25, -0.0, .25}},
        {{.25, .5}, {.25, .5}},
        {{-4 * q, 5 * q}, {-4 * q, -3 * q, -2 * q, -q, 0, q, 2 * q, 3 * q, 4 * q, 5 * q}},
        {{2 * kTwoPi + .2, 3 * kTwoPi - .2}, {2 * kTwoPi + .2, 9 * q, 10 * q, 11 * q, 3 * kTwoPi - .2}},
        {{q - 1e-13, q + 1e-13}, {q - 1e-13, q + 1e-13}},
        {{.25, .25}, {.25, .25}},
        {{-0.0, 0.0}, {-0.0, 0.0}}
    };
    for (const auto &consumer : consumers()) for (const auto &fixture : fixtures)
        expectBits(consumer, fixture.range, fixture.expected);
}

FK_TEST(QuadrantBreakpointsInvalidRanges) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    for (const auto &consumer : consumers())
        for (Interval range : {Interval{nan, 1}, Interval{0, nan}, Interval{0, inf}, Interval{-inf, 0},
                Interval{inf, inf}, Interval{1, 0}}) {
            bool rejected = false;
            try { (void)consumer.breaks(range); } catch (const std::invalid_argument &) { rejected = true; }
            if (!rejected) fktest::reportFailure(__FILE__, __LINE__, std::string(consumer.name) + ": invalid range accepted");
        }
}

FK_TEST(QuadrantBreakpointsNonProgressRange) {
    // Candidate only, or baseline under an EXTERNAL watchdog. No numerical
    // sample cap is prescribed: failure is the unrepresentable next index.
    const Interval range{1e20, 1e20 + 1e9};
    for (const auto &consumer : consumers()) {
        bool rejected = false;
        try { (void)consumer.breaks(range); } catch (const std::domain_error &) { rejected = true; }
        if (!rejected) fktest::reportFailure(__FILE__, __LINE__, std::string(consumer.name) + ": nonprogress range accepted");
    }
}

FK_TEST(QuadrantBreakpointsCancellationPreset) {
    // Construct valid geometry BEFORE activating cancellation. A short finite
    // range suffices for entry propagation, without a timed thread or memory race.
    const auto calls = consumers();
    const auto token = std::make_shared<std::atomic<bool>>(true); CancellationScope scope(token);
    for (const auto &consumer : calls) {
        bool cancelled = false;
        try { (void)consumer.breaks({0, kTwoPi}); } catch (const OperationCancelled &) { cancelled = true; }
        if (!cancelled) fktest::reportFailure(__FILE__, __LINE__, std::string(consumer.name) + ": cancellation swallowed");
    }
}
