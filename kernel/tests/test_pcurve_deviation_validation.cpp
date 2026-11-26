#include "fk_pcurve.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_test.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
void expectInfinity(double value) { FK_CHECK(std::isinf(value) && value > 0); }
std::shared_ptr<Circle<3>> overflowCircle() {
    const double largest = std::numeric_limits<double>::max();
    return std::make_shared<Circle<3>>(Vec3(largest, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), .5 * largest);
}
}

FK_TEST(PCurveDeviationFiniteControlsAndSampledKnotPeaks) {
    const Plane plane{Frame3()}; const Line<2> uv(Vec2(), Vec2(1, 0));
    const Line<3> exact(Vec3(), Vec3(1, 0, 0)), offset(Vec3(0, 0, 1), Vec3(1, 0, 0));
    for (int samples : {1, 4, 16}) {
        FK_CHECK(pcurveDeviation(plane, exact, uv, {0, 1}, samples) == 0);
        FK_CHECK(pcurveDeviation(plane, offset, uv, {0, 1}, samples) == 1);
    }
    // The maximum is at an ORIGINAL smooth-span boundary, which must remain
    // in the sampling union. This is a sampled metric, not a continuous bound.
    const BSplineCurve<3> peaked(1, {0, 0, .25, 1, 1}, {{0, 0, 0}, {.25, 0, 2}, {1, 0, 0}});
    const BSplineCurve<2> peakedUV(1, {0, 0, .25, 1, 1}, {{0, 0}, {.25, 2}, {1, 0}});
    for (int samples : {1, 3, 16}) {
        FK_CHECK(pcurveDeviation(plane, peaked, uv, {0, 1}, samples) == 2);
        FK_CHECK(pcurveDeviation(plane, exact, peakedUV, {0, 1}, samples) == 2);
    }
}

FK_TEST(PCurveDeviationInvalidSamplesAndRangesReturnInfinity) {
    const Plane plane{Frame3()}; const Line<2> uv(Vec2(), Vec2(1, 0));
    const Line<3> offset(Vec3(0, 0, 1), Vec3(1, 0, 0));
    for (int samples : {0, -1}) expectInfinity(pcurveDeviation(plane, offset, uv, {0, 1}, samples));
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    const double largest = std::numeric_limits<double>::max();
    for (Interval range : {Interval{0, 0}, Interval{1, 0}, Interval{nan, 1}, Interval{0, nan},
            Interval{0, inf}, Interval{-inf, 1}, Interval{-largest, largest}})
        expectInfinity(pcurveDeviation(plane, offset, uv, range));
}

FK_TEST(PCurveDeviationNonFiniteNativeEvaluationsNeverLookExact) {
    const Plane plane{Frame3()}; const auto overflow = overflowCircle();
    const Line<3> finiteCurve(Vec3(), Vec3(1, 0, 0));
    const Line<2> finiteUV(Vec2(), Vec2(1, 0));
    const double largest = std::numeric_limits<double>::max();
    const Circle<2> overflowUV(Vec2(largest, 0), Vec2(1, 0), Vec2(0, 1), .5 * largest);
    FK_CHECK(!isFinite(overflow->point(0)) && !isFinite(overflowUV.point(0)));
    expectInfinity(pcurveDeviation(plane, *overflow, finiteUV, {0, .1}));
    expectInfinity(pcurveDeviation(plane, finiteCurve, overflowUV, {0, .1}));
    expectInfinity(pcurveDeviation(plane, *overflow, overflowUV, {0, .1}));
    const Plane translated(Frame3({largest, 0, 0}, {0, 0, 1}, {1, 0, 0}));
    const Line<2> largeButFiniteUV(Vec2(.5 * largest, 0), Vec2(1, 0));
    FK_CHECK(isFinite(largeButFiniteUV.point(0)));
    FK_CHECK(!isFinite(translated.point(largeButFiniteUV.point(0).x(), 0)));
    expectInfinity(pcurveDeviation(translated, finiteCurve, largeButFiniteUV, {0, .1}));
}

FK_TEST(PCurveDeviationOverflowGapFromFinitePointsIsInfinity) {
    const Plane plane{Frame3()}; const Line<2> uv(Vec2(), Vec2(1, 0));
    const Line<3> largeGap(Vec3(0, 0, 1e200), Vec3(1, 0, 0));
    FK_CHECK(isFinite(largeGap.point(0)) && isFinite(plane.point(0, 0)));
    expectInfinity(distance(largeGap.point(0), plane.point(0, 0)));
    expectInfinity(pcurveDeviation(plane, largeGap, uv, {0, 1}));
    const double large = .75 * std::numeric_limits<double>::max();
    const Line<3> negative(Vec3(-large, 0, 0), Vec3(0, 1, 0));
    const Line<2> positive(Vec2(large, 0), Vec2(0, 1));
    FK_CHECK(isFinite(negative.point(0)) && isFinite(plane.point(positive.point(0).x(), 0)));
    expectInfinity(pcurveDeviation(plane, negative, positive, {0, 1}));
}

FK_TEST(PCurveAcceptanceNeedsFiniteDeviationEvenWithInfiniteTolerance) {
    const Plane plane{Frame3()};
    const double inf = std::numeric_limits<double>::infinity();
    const auto finite = std::make_shared<Line<3>>(Vec3(), Vec3(1, 0, 0));
    const auto overflow = overflowCircle();
    const auto finitePointsOverflowGap = std::make_shared<Line<3>>(Vec3(0, 0, 1e200), Vec3(1, 0, 0));
    // Valid public NURBS, queried deliberately OUTSIDE its natural domain.
    // Its extrapolated homogeneous denominator W=24t-3 vanishes at t=.125;
    // positive original weights do not make that extrapolation finite.
    const auto pole = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{1, 1, 1, 1, 2, 2, 2, 2},
        std::vector<Vec3>(4, Vec3()), std::vector<double>{21, 29, 37, 45});
    FK_CHECK(!isFinite(pole->point(.125)));
    for (double budget : {1e-8, inf}) {
        const auto validExact = exactPCurve(plane, finite, {0, 1}, budget);
        FK_CHECK(validExact && pcurveDeviation(plane, *finite, *validExact, {0, 1}) == 0);
        FK_CHECK(!exactPCurve(plane, overflow, {0, .1}, budget));
        FK_CHECK(!exactPCurve(plane, pole, {0, 1}, budget));
        double deviation = -123;
        FK_CHECK(!fitPCurve(plane, pole, {0, 1}, budget, &deviation));
        FK_CHECK(deviation == -123);
        const auto naturalExact = exactPCurve(plane, pole, {1, 2}, budget);
        FK_CHECK(naturalExact && pcurveDeviation(plane, *pole, *naturalExact, {1, 2}) == 0);
        deviation = -123;
        const auto naturalFit = fitPCurve(plane, pole, {1, 2}, budget, &deviation);
        FK_CHECK(naturalFit && std::isfinite(deviation) && deviation == 0);
        deviation = -123;
        const auto finiteFit = fitPCurve(plane, finite, {0, 1}, budget, &deviation);
        FK_CHECK(finiteFit && std::isfinite(deviation) && deviation >= 0);
    }
    FK_CHECK(!exactPCurve(plane, finitePointsOverflowGap, {0, 1}, inf));
}

FK_TEST(PCurveDeviationAndAcceptancePropagateCancellation) {
    const Plane plane{Frame3()}; const auto curve = std::make_shared<Line<3>>(Vec3(), Vec3(1, 0, 0));
    const Line<2> uv(Vec2(), Vec2(1, 0));
    const auto token = std::make_shared<std::atomic<bool>>(true); CancellationScope scope(token);
    bool metricCancelled = false, exactCancelled = false, fitCancelled = false;
    try { (void)pcurveDeviation(plane, *curve, uv, {0, 1}); } catch (const OperationCancelled &) { metricCancelled = true; }
    try { (void)exactPCurve(plane, curve, {0, 1}); } catch (const OperationCancelled &) { exactCancelled = true; }
    double deviation = -123;
    try { (void)fitPCurve(plane, curve, {0, 1}, 1e-8, &deviation); } catch (const OperationCancelled &) { fitCancelled = true; }
    FK_CHECK(metricCancelled && exactCancelled && fitCancelled && deviation == -123);
}

namespace {
class PoleArc final : public Curve<3> {
public:
    PoleArc(const SphericalSurface &surface, std::vector<Vec2> poles)
        : surface_(surface), uv_(2, {0., 0., 0., 1., 1., 1.}, std::move(poles)) {}

    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return uv_.domain(); }
    void evaluate(double t, int order, Vec3 *out) const override {
        if (order > 2) throw std::invalid_argument("PoleArc: derivative order");
        Vec2 uv[3];
        uv_.evaluate(t, 2, uv);
        Vec3 d[9];
        surface_.evaluate(uv[0].x(), uv[0].y(), 2, d);
        const auto at = [&](int u, int v) { return d[Surface::derivativeIndex(u, v, 2)]; };
        out[0] = at(0, 0);
        if (order >= 1) out[1] = uv[1].x() * at(1, 0) + uv[1].y() * at(0, 1);
        if (order >= 2)
            out[2] = uv[1].x() * uv[1].x() * at(2, 0)
                   + 2. * uv[1].x() * uv[1].y() * at(1, 1)
                   + uv[1].y() * uv[1].y() * at(0, 2)
                   + uv[2].x() * at(1, 0) + uv[2].y() * at(0, 1);
    }

private:
    const SphericalSurface &surface_;
    BSplineCurve<2> uv_;
};
}

FK_TEST(PCurveFitDistinguishesPolarEndpointAndNearbyRegularPoints) {
    const SphericalSurface sphere(Frame3(), 1.);
    constexpr double cap = 1e-7, startAngle = 6.1, departure = .1;
    for (bool reversed : {false, true}) for (bool slow : {false, true}) {
        for (double poleOffset : {0., .5 * cap}) {
            const double v0 = kHalfPi - poleOffset;
            std::vector<Vec2> poles{{startAngle, v0},
                {startAngle, v0 - (slow ? 0. : .5 * departure)}, {startAngle + 1., v0 - departure}};
            if (reversed) std::reverse(poles.begin(), poles.end());
            const auto edge = std::make_shared<PoleArc>(sphere, std::move(poles));
            double deviation = -1.;
            const auto result = fitPCurve(sphere, edge, {0., 1.}, cap, &deviation);
            FK_CHECK(bool(result));
            if (!result) continue;
            FK_CHECK(deviation >= 0. && deviation <= cap);
            FK_CHECK(pcurveDeviation(sphere, *edge, *result, {0., 1.}, 128) <= cap);
            const double endpoint = reversed ? 1. : 0.;
            const Vec2 polarUv = result->point(endpoint);
            FK_CHECK_NEAR(std::remainder(polarUv.x() - startAngle, kTwoPi), 0., 1e-6);
            FK_CHECK_NEAR(polarUv.y(), v0, 1e-9);
            const double near = reversed ? 1. - .0008 : .0008;
            if (slow) FK_CHECK(result->point(near).y() < kHalfPi - 1e-8);
        }
    }

    const SphericalSurface otherRadius(Frame3(), 1.0002);
    const auto outside = std::make_shared<PoleArc>(otherRadius, std::vector<Vec2>{
        {startAngle, kHalfPi}, {startAngle, kHalfPi}, {startAngle + 1., kHalfPi - departure}});
    double unchanged = -1.;
    FK_CHECK(!fitPCurve(sphere, outside, {0., 1.}, cap, &unchanged));
    FK_CHECK(unchanged == -1.);
}

FK_TEST(PCurveFitPreservesManyTurnsNearPole) {
    const SphericalSurface sphere(Frame3(), 1.);
    const auto edge = sphere.vIso(kHalfPi - .0005);
    constexpr double cap = 1e-10;
    const double span = 256 * kTwoPi;
    for (const Interval range : {Interval{.25, .25 + span}, Interval{-.25 - span, -.25}}) {
        double deviation = -1.;
        const auto result = fitPCurve(sphere, edge, range, cap, &deviation);
        FK_CHECK(bool(result));
        if (!result) continue;
        FK_CHECK(deviation >= 0. && deviation <= cap);
        FK_CHECK_NEAR(result->point(range.hi).x() - result->point(range.lo).x(), span, 1e-8);
        for (int i = 0; i < 256; ++i) {
            const double lo = range.lo + i * kTwoPi, hi = range.lo + (i + 1) * kTwoPi;
            FK_CHECK(pcurveDeviation(sphere, *edge, *result, {lo, hi}, 16) <= cap);
        }
    }
}
