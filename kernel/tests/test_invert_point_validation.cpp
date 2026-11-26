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

FK_TEST(ProjectSmallParallelsNearPolesKeepsAngularMinimum) {
    const Frame3 frame;
    const ConicalSurface cone(frame, .5, 0.);
    const SphericalSurface sphere(frame, 1.);
    const ToroidalSurface torus(frame, 1., 2.);
    const auto meridian = std::make_shared<Line<3>>(Vec3(), Vec3(1, 0, 1));
    const RevolutionSurface revolution(meridian, Vec3(), Vec3(0, 0, 1));
    constexpr double angle = .277, offset = 2e-8;
    const Vec3 radial(std::cos(angle), std::sin(angle), 0.);
    const auto check = [&](const Surface &surface, double v, double radialSign, double trimStep) {
        const Vec3 wanted = surface.point(angle, v), p = wanted + radialSign * offset * radial;
        FK_CHECK(!surface.vIso(v));
        const Interval range{std::min(v, v + trimStep), std::max(v, v + trimStep)};
        for (const Interval vRange : {Interval{v, v}, range}) {
            for (double phase : {0., -kTwoPi, kTwoPi}) {
                const Interval uRange{.2 + phase, 1.4 + phase};
                const auto result = projectPoint(surface, p, uRange, vRange);
                FK_CHECK(uRange.contains(result.u) && vRange.contains(result.v));
                FK_CHECK_NEAR(result.u, angle + phase, 1e-8);
                FK_CHECK_NEAR(result.v, v, 1e-14);
                FK_CHECK(distance(result.point, wanted) <= 1e-14);
                FK_CHECK_NEAR(result.distance, offset, 1e-14);
            }
        }
        const double endpoint = .5;
        const auto restricted = projectPoint(surface, p, {endpoint, 1.4}, {v, v});
        FK_CHECK_NEAR(restricted.u, endpoint, 1e-12);
        FK_CHECK_NEAR(restricted.distance, distance(surface.point(endpoint, v), p), 1e-14);
    };
    check(cone, 5e-8, 1., -1e-8);
    check(cone, -5e-8, -1., 1e-8);
    check(sphere, kHalfPi - 2e-8, 1., 1e-8);
    check(torus, std::acos(-.5) + 1e-8, -1., -1e-8);
    check(revolution, 4e-8, 1., -1e-8);

    const auto apex = projectPoint(cone, {1, 0, 0}, {.2, 1.4}, {0., 0.});
    FK_CHECK(apex.distance == 1. && distance(apex.point, Vec3()) == 0.);
}

FK_TEST(InvertPointKeepsExactSeedOnCollapsedParallel) {
    const Frame3 frame;
    const ConicalSurface cone(frame, .4, 0.);
    const ToroidalSurface horn(frame, 1., 1.);
    const double spindlePole = 2.2;
    const ToroidalSurface reference(frame, 1., 2.);
    const ToroidalSurface spindle(frame, 1. - reference.point(0., spindlePole).x(), 2.);
    const auto meridian = std::make_shared<TrimmedCurve<3>>(
        std::make_shared<Line<3>>(Vec3(), Vec3(1, 0, 1)), 0., 2.);
    const RevolutionSurface revolution(meridian, Vec3(), Vec3(0, 0, 1));
    const auto check = [&](const Surface &surface, double v) {
        constexpr double angle = 1.234;
        const Vec3 point = surface.point(angle, v);
        for (double shift : {0., -4. * kTwoPi, 9. * kTwoPi}) {
            const Vec2 seed(angle + shift, v);
            FK_CHECK(distance(surface.point(seed.x(), seed.y()), point) == 0.);
            Vec2 result = seed;
            FK_CHECK(invertPoint(surface, point, result, tolerance, 3.));
            FK_CHECK(result.x() == seed.x() && result.y() == seed.y());
        }
    };
    check(cone, 0.);
    check(horn, kPi);
    check(spindle, spindlePole);
    check(revolution, 0.);

    const BSplineSurface triangular(1, 1, {0., 0., 1., 1.}, {0., 0., 1., 1.}, 2, 2,
        {{0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}});
    Vec2 seed(0., .734);
    FK_CHECK(invertPoint(triangular, Vec3(), seed, tolerance, 3.));
    FK_CHECK(seed.x() == 0. && seed.y() == .734);

    Vec2 outside(0., 1.5);
    FK_CHECK(invertPoint(triangular, Vec3(), outside, tolerance, 3.));
    FK_CHECK(triangular.uDomain().contains(outside.x()) && triangular.vDomain().contains(outside.y()));
}

FK_TEST(InvertPointProjectsFromPoleWhenSeedIsOnlyWithinTolerance) {
    const ConicalSurface cone(Frame3(), .4, 0.);
    // The initial apex already meets the tolerance. The target still
    // has a determinate azimuth and the projection must recover it.
    const Vec3 point = cone.point(.625, 1e-9);
    Vec2 seed(1.234, 0.);
    FK_CHECK(distance(cone.point(seed.x(), seed.y()), point) < tolerance);
    FK_CHECK(invertPoint(cone, point, seed, tolerance, 3.));
    FK_CHECK(distance(cone.point(seed.x(), seed.y()), point) < 1e-14);
    FK_CHECK_NEAR(seed.x(), .625, 1e-10);
}

namespace {
BSplineSurface foldedSplineInU() {
    // X = u^3 - 3*u/4 folds at u = +/-1/2.
    std::vector<Vec3> poles;
    for (double x : {-.25, 1.25, -1.25, .25})
        for (double v : {0., 1.}) poles.emplace_back(x, v, 0.);
    return BSplineSurface(3, 1, {-1., -1., -1., -1., 1., 1., 1., 1.},
        {0., 0., 1., 1.}, 4, 2, poles);
}
}

FK_TEST(InvertPointRecoversNearbyBranchAfterSingularSplineSeed) {
    const auto folded = foldedSplineInU();
    constexpr double cap = 1e-9;
    for (double far : {-.875, .875}) {
        const Vec3 point = folded.point(far, .5);
        const double nearSide = far > 0. ? -1. : 1.;
        const Interval local = nearSide < 0. ? Interval{-1., 0.} : Interval{0., 1.};
        FK_CHECK(projectPoint(folded, point, local, folded.vDomain()).distance <= cap);
        Vec2 uv(.5 * nearSide, .5);
        FK_CHECK(invertPoint(folded, point, uv, cap, 1.));
        FK_CHECK(uv.x() * nearSide > 0.);
        FK_CHECK(distance(folded.point(uv.x(), uv.y()), point) <= cap);
    }
    Vec2 outside(.5, .5);
    FK_CHECK(!invertPoint(folded, Vec3(-.251, .5, 0.), outside, cap, 1.));
}

FK_TEST(InvertPointRecoversNearbyBranchAfterNewtonConvergesAcrossFold) {
    const auto folded = foldedSplineInU();
    constexpr double cap = 1e-9;
    // With v=.4, rounding leaves a small nonzero U derivative at the
    // fold. Newton can cross the domain and converge in the other half.
    for (double x : {-.249, -.24}) {
        const Vec3 point(x, .4, 0.);
        FK_CHECK(projectPoint(folded, point, {0., 1.}, folded.vDomain()).distance <= cap);
        Vec2 uv(.5, .4);
        FK_CHECK(invertPoint(folded, point, uv, cap, 1.));
        FK_CHECK(uv.x() > 0.);
        FK_CHECK(distance(folded.point(uv.x(), uv.y()), point) <= cap);
    }

    const BSplineSurface flat(1, 1, {0., 0., 1., 1.}, {0., 0., 1., 1.}, 2, 2,
        {{0., 0., 0.}, {0., 1., 0.}, {1., 0., 0.}, {1., 1., 0.}});
    const Vec3 uniquePoint(.875, .25, 0.);
    FK_CHECK(projectPoint(flat, uniquePoint, {0., .5}, flat.vDomain()).distance > cap);
    Vec2 seed(.125, .25);
    FK_CHECK(invertPoint(flat, uniquePoint, seed, cap, 1.));
    FK_CHECK_NEAR(seed.x(), .875, cap);
    FK_CHECK(distance(flat.point(seed.x(), seed.y()), uniquePoint) <= cap);
}
