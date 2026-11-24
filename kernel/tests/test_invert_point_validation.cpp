#include "fk_surface_algo.h"
#include "fk_bspline_surface.h"
#include "fk_nurbs.h"
#include "fk_test.h"
#include <cmath>
#include <limits>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
constexpr double tolerance = 1e-8;
void checkFinalSuccess(const Surface &surface, const Vec3 &point, Vec2 guess, double scale) {
    const bool success = invertPoint(surface, point, guess, tolerance, scale);
    // The implementation may reject a phase that cannot be represented near
    // a huge seed. If it succeeds, its FINAL output must satisfy the contract.
    if (success) {
        FK_CHECK(isFinite(guess));
        if (isFinite(guess)) {
            const double residual = distance(point, surface.point(guess.x(), guess.y()));
            FK_CHECK(std::isfinite(residual) && residual <= tolerance);
        }
    }
}
void checkNearbyBranch(const Surface &surface, Vec2 target, Vec2 seed, double scale) {
    const Vec3 point = surface.point(target.x(), target.y());
    Vec2 result = seed;
    FK_CHECK(invertPoint(surface, point, result, tolerance, scale));
    FK_CHECK(isFinite(result));
    if (!isFinite(result)) return;
    FK_CHECK(distance(point, surface.point(result.x(), result.y())) <= tolerance);
    if (surface.isUPeriodic()) FK_CHECK(std::fabs(result.x() - seed.x()) <= .5 * surface.uPeriod());
    if (surface.isVPeriodic()) FK_CHECK(std::fabs(result.y() - seed.y()) <= .5 * surface.vPeriod());
}
}

FK_TEST(InvertPointCylinderValidatesFinalUnwrappedResidual) {
    const CylindricalSurface cylinder(Frame3(), 10);
    const Vec3 point = cylinder.point(.25, 2);
    for (double seed : {1e8, 1e12, 1e16})
        checkFinalSuccess(cylinder, point, {seed, 2}, 10);
}

FK_TEST(InvertPointConeSphereValidateFinalUnwrappedResidual) {
    const ConicalSurface cone(Frame3(), .4, 10);
    const SphericalSurface sphere(Frame3(), 10);
    for (double seed : {1e8, 1e12, 1e16}) {
        checkFinalSuccess(cone, cone.point(.25, 2), {seed, 2}, 10);
        checkFinalSuccess(sphere, sphere.point(.25, .3), {seed, .3}, 10);
    }
}

FK_TEST(InvertPointTorusValidatesBothFinalPeriodicCoordinates) {
    const ToroidalSurface torus(Frame3(), 10, 2);
    const Vec3 point = torus.point(.25, .4);
    for (double seed : {1e8, 1e12, 1e16}) {
        checkFinalSuccess(torus, point, {seed, .4}, 10);
        checkFinalSuccess(torus, point, {.25, seed}, 10);
    }
    checkFinalSuccess(torus, point, {1e12, -1e12}, 10);
}

FK_TEST(InvertPointModeratePeriodicSeedsKeepNearbyBranch) {
    const CylindricalSurface cylinder(Frame3(), 10);
    const ConicalSurface cone(Frame3(), .4, 10);
    const SphericalSurface sphere(Frame3(), 10);
    const ToroidalSurface torus(Frame3(), 10, 2);
    for (int turns : {-24, 0, 24}) {
        const double u = .25 + turns * kTwoPi + .1;
        checkNearbyBranch(cylinder, {.25, 2}, {u, 2.1}, 10);
        checkNearbyBranch(cone, {.25, 2}, {u, 2.1}, 10);
        checkNearbyBranch(sphere, {.25, .3}, {u, .4}, 10);
        checkNearbyBranch(torus, {.25, .4}, {u, .4 - turns * kTwoPi + .1}, 10);
    }
}

FK_TEST(InvertPointPlaneAndClosedNonPeriodicSplineControls) {
    {
        const Plane plane(Frame3({2, -3, 4}, {1, 2, 3}, {1, 0, 0}));
        const Vec2 target(1.25, -.75);
        const Vec3 point = plane.point(target.x(), target.y());
        Vec2 result(1000, -1000);
        FK_CHECK(invertPoint(plane, point, result, tolerance, 10));
        FK_CHECK(isFinite(result));
        if (isFinite(result)) FK_CHECK(distance(point, plane.point(result.x(), result.y())) <= tolerance);
    }
    // Kernel-only counterpart of InvertPointKeepsClosedBSplineBranch in
    // test_surface_algo.cpp; the original OCCT-comparison TU stays unchanged.
    const CylindricalSurface cylinder(Frame3(), 3);
    const BSplineSurface source = toBSplineSurface(cylinder, {0, kTwoPi}, {0, 2});
    std::vector<Vec3> poles; std::vector<double> weights;
    for (int j = 0; j < source.vPoleCount(); ++j) for (int i = 0; i < source.uPoleCount(); ++i) {
        poles.push_back(source.pole(i, j)); weights.push_back(source.weight(i, j));
    }
    const BSplineSurface transposed(source.vDegree(), source.uDegree(), source.vKnots(), source.uKnots(),
        source.vPoleCount(), source.uPoleCount(), poles, weights);
    for (int axis : {0, 1}) {
        const BSplineSurface &surface = axis == 0 ? source : transposed;
        FK_CHECK(!surface.isUPeriodic() && !surface.isVPeriodic());
        const Interval domain = axis == 0 ? surface.uDomain() : surface.vDomain();
        for (bool high : {false, true}) {
            Vec2 target(1, 1), guess(1, 1);
            target[axis] = high ? domain.lo + 1e-8 : domain.hi - 1e-8;
            guess[axis] = high ? domain.hi - .1 : domain.lo + .1;
            Vec3 point = surface.point(target.x(), target.y());
            point[0] *= 1.0001; point[1] *= 1.0001;
            FK_CHECK(invertPoint(surface, point, guess, 1e-3, 3));
            FK_CHECK(isFinite(guess));
            FK_CHECK(high ? guess[axis] > domain.hi - .01 : guess[axis] < domain.lo + .01);
            if (isFinite(guess)) FK_CHECK(distance(point, surface.point(guess.x(), guess.y())) <= 1e-3);
        }
    }
}

FK_TEST(InvertPointNonFinitePeriodicSeedsCannotReturnSuccess) {
    const CylindricalSurface cylinder(Frame3(), 10);
    const ToroidalSurface torus(Frame3(), 10, 2);
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    for (double bad : {nan, inf, -inf}) {
        Vec2 cylinderGuess(bad, 2), torusGuess(.25, bad);
        FK_CHECK(!invertPoint(cylinder, cylinder.point(.25, 2), cylinderGuess, tolerance, 10));
        FK_CHECK(!invertPoint(torus, torus.point(.25, .4), torusGuess, tolerance, 10));
    }
}

FK_TEST(ProjectAndInvertSpindleTorusOnNegativeRadialBranch) {
    const ToroidalSurface torus(Frame3(), 1, 2);
    // At (pi, pi), the signed radius is -1. The point is at x=+1,
    // although its meridian parameter faces the opposite direction.
    const Vec3 point(1, 0, 0);
    constexpr double cap = 1e-10;
    const auto whole = projectPoint(torus, point);
    FK_CHECK(whole.distance <= cap);
    FK_CHECK_NEAR(whole.u, kPi, cap);
    FK_CHECK_NEAR(whole.v, kPi, cap);

    for (int turns : {-1, 0, 1}) {
        const Interval u{3. + turns * kTwoPi, 3.3 + turns * kTwoPi};
        const Interval v{3. - turns * kTwoPi, 3.3 - turns * kTwoPi};
        const auto window = projectPoint(torus, point, u, v);
        FK_CHECK(window.distance <= cap);
        FK_CHECK(u.contains(window.u) && v.contains(window.v));
        FK_CHECK_NEAR(window.u, kPi + turns * kTwoPi, cap);
        FK_CHECK_NEAR(window.v, kPi - turns * kTwoPi, cap);
    }

    // Moving the window away from the stationary point must leave an
    // eligible boundary candidate and must not return the excluded branch.
    const auto boundary = projectPoint(torus, point, {2.9, 3.}, {3., 3.3});
    FK_CHECK_NEAR(boundary.u, 3., cap);
    FK_CHECK_NEAR(boundary.v, kPi, cap);
    FK_CHECK_NEAR(boundary.distance, 2 * std::sin((kPi - 3.) / 2.), cap);

    Vec2 seed(0, 0);
    FK_CHECK(invertPoint(torus, point, seed, cap, 3));
    FK_CHECK(distance(torus.point(seed.x(), seed.y()), point) <= cap);
}
