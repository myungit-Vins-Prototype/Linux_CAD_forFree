#include "fk_curve_algo.h"
#include "fk_bspline.h"
#include "fk_curve_length_cache.h"
#include "fk_cancel.h"
#include "fk_test.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
bool bitEqual(double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; }
std::shared_ptr<BSplineCurve<2>> corner2() {
    // Two rational Bezier spans meet at a C0 right angle. Each stays on
    // its straight segment and is monotone; physical length is 2+3.
    return std::make_shared<BSplineCurve<2>>(2,
        std::vector<double>{0, 0, 0, .5, .5, 1, 1, 1},
        std::vector<Vec2>{{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 3}},
        std::vector<double>{1, 2, 1, 3, 1});
}
std::shared_ptr<BSplineCurve<3>> corner3() {
    // Cubic C0 counterpart: length3+4, with variable positive weights.
    return std::make_shared<BSplineCurve<3>>(3,
        std::vector<double>{0, 0, 0, 0, .5, .5, .5, 1, 1, 1, 1},
        std::vector<Vec3>{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0},
                          {3, 1, 0}, {3, 2, 0}, {3, 4, 0}},
        std::vector<double>{1, 2, 3, 1, 3, 2, 1});
}
template <int N>
void expectCancelled(const Curve<N> &curve, const Interval &range) {
    bool cancelled = false;
    try { (void)arcLength(curve, range); }
    catch (const OperationCancelled &) { cancelled = true; }
    FK_CHECK(cancelled);
}
template <int N>
void expectDomainFailure(const Curve<N> &curve, const Interval &range) {
    bool rejected = false;
    try { (void)arcLength(curve, range); }
    catch (const std::domain_error &) { rejected = true; }
    FK_CHECK(rejected);
}
}

// The unchanged baseline must run ONLY substring filter SafeControls.
// Nonfinite-speed fixtures below can force its full depth30 quadrature tree.
FK_TEST(ArcLengthSafeControls) {
    const Line<2> line2(Vec2(4, -2), Vec2(3, 4));
    const Line<3> line3(Vec3(2, 3, -1), Vec3(0, 0, -1));
    FK_CHECK_NEAR(arcLength(line2, {-2, 5}), 7, 1e-12);
    FK_CHECK_NEAR(arcLength(line3, {-3, 2}), 5, 1e-12);
    const Circle<3> circle(Vec3(1, 2, 3), Vec3(1, 0, 0), Vec3(0, 1, 0), 2);
    FK_CHECK_NEAR(arcLength(circle, {-.25, 3 * kPi}, 1e-11), 2 * (3 * kPi + .25), 1e-11);
    const auto c2 = corner2();
    const auto c3 = corner3();
    FK_CHECK_NEAR(arcLength(*c2, 1e-11), 5, 1e-11);
    FK_CHECK_NEAR(arcLength(*c3, 1e-11), 7, 1e-11);
    const TrimmedCurve<2> trim(c2, .25, .75);
    // Independent geometric oracle for the two straight portions, not
    // another quadrature call or a copied integrator.
    const double expected = distance(c2->point(.25), c2->point(.5)) + distance(c2->point(.5), c2->point(.75));
    FK_CHECK_NEAR(arcLength(trim, 1e-11), expected, 1e-11);
    const double matrix[3][3]{{2, 0, 0}, {0, 3, 0}, {0, 0, 1}};
    const TransformedCurve transformed(c3, Transform3::fromParts(matrix, Vec3(12, -23, 7)));
    FK_CHECK_NEAR(arcLength(transformed, 1e-11), 18, 1e-11);
    // Legacy finite point ranges return zero without sampling a derivative.
    FK_CHECK_NEAR(arcLength(line2, {.25, .25}), 0, 0);
    FK_CHECK_NEAR(arcLength(*c2, {.5, .5}), 0, 0);
    FK_CHECK_NEAR(arcLength(transformed, {.5, .5}), 0, 0);
    // Compatibility: zero and negative tolerance retain the legacy roundoff
    // floor; this change imposes no new tolerance contract.
    detail::CurveLengthCache cache;
    for (double tolerance : {1e-11, 0., -1.}) {
        const double length = arcLength(line3, {-3, 2}, tolerance);
        FK_CHECK_NEAR(length, 5, 1e-12);
        FK_CHECK(bitEqual(cache.get(line3, {-3, 2}, tolerance), length));
        FK_CHECK(bitEqual(cache.get(line3, {-3, 2}, tolerance), length));
        FK_CHECK(bitEqual(cache.get(line3, {.5, .5}, tolerance), 0.));
    }
    for (const Curve<3> *curve : {static_cast<const Curve<3> *>(c3.get()), static_cast<const Curve<3> *>(&transformed)})
        FK_CHECK(bitEqual(cache.get(*curve, {.25, .75}, 1e-11), arcLength(*curve, {.25, .75}, 1e-11)));
}
FK_TEST(ArcLengthCancellationPreset) {
    const Line<2> line2(Vec2(), Vec2(1, 0));
    const Line<3> line3(Vec3(), Vec3(0, 1, 0));
    const auto c2 = corner2();
    const auto c3 = corner3();
    detail::CurveLengthCache cache;
    (void)cache.get(*c3, c3->domain(), 1e-10); // Include an already cached entry.
    const auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    expectCancelled(line2, {0, 1});
    expectCancelled(line3, {0, 1});
    expectCancelled(*c2, c2->domain());
    expectCancelled(*c3, c3->domain());
    // Cache entry cancellation already existed; this preserves it and does
    // not attribute that behavior to the new direct arcLength guard.
    bool cancelled = false;
    try { (void)cache.get(*c3, c3->domain(), 1e-10); }
    catch (const OperationCancelled &) { cancelled = true; }
    FK_CHECK(cancelled);
}
FK_TEST(ArcLengthReparametrizedSpeedsKeepPhysicalLength) {
    // Identical unit segments with native parameters rescaled by powers of
    // two. Ordinary coordinates and exact dyadic knots isolate derivative
    // norm underflow/overflow from any change of represented geometry.
    for (int exponent : {-600, -535, 535, 600}) {
        const double width = std::ldexp(1., exponent);
        const BSplineCurve<2> c2(1, {0, 0, width, width}, {{0, 0}, {1, 0}});
        const BSplineCurve<3> c3(1, {0, 0, width, width}, {{0, 0, 0}, {0, 1, 0}});
        const BSplineCurve<2> diagonal2(1, {0, 0, width, width}, {{0, 0}, {3, 4}});
        const BSplineCurve<3> diagonal3(1, {0, 0, width, width}, {{0, 0, 0}, {1, 2, 2}});
        detail::CurveLengthCache cache; // Borrowed curves outlive this cache.
        const Vec2 d2 = c2.derivative(.5 * width);
        const Vec3 d3 = c3.derivative(.5 * width);
        FK_CHECK(isFinite(d2) && d2.x() > 0);
        FK_CHECK(isFinite(d3) && d3.y() > 0);
        FK_CHECK_NEAR(distance(c2.point(0), c2.point(width)), 1, 0);
        FK_CHECK_NEAR(distance(c3.point(0), c3.point(width)), 1, 0);
        FK_CHECK_NEAR(arcLength(c2, c2.domain()), 1, 1e-12);
        const double actual = arcLength(c3, c3.domain());
        FK_CHECK_NEAR(actual, 1, 1e-12);
        FK_CHECK(bitEqual(cache.get(c3, c3.domain(), 1e-10), actual));
        FK_CHECK(bitEqual(cache.get(c3, c3.domain(), 1e-10), actual));
        // Non-axis-aligned vectors exercise both multi-argument norms.
        FK_CHECK_NEAR(distance(diagonal2.point(0), diagonal2.point(width)), 5, 0);
        FK_CHECK_NEAR(distance(diagonal3.point(0), diagonal3.point(width)), 3, 0);
        FK_CHECK_NEAR(arcLength(diagonal2, diagonal2.domain()), 5, 1e-12);
        const double diagonalLength = arcLength(diagonal3, diagonal3.domain());
        FK_CHECK_NEAR(diagonalLength, 3, 1e-12);
        FK_CHECK(bitEqual(cache.get(diagonal3, diagonal3.domain(), 1e-10), diagonalLength));
    }
}
FK_TEST(ArcLengthNonfiniteDerivativeIsRejected) {
    // A still smaller, valid native knot span overflows derivative evaluation
    // itself. This is distinct from a finite derivative with an unsafe norm.
    // DO NOT run this group on baseline without an external watchdog.
    const double width = std::ldexp(1., -1030);
    const BSplineCurve<2> c2(1, {0, 0, width, width}, {{0, 0}, {1, 0}});
    const BSplineCurve<3> c3(1, {0, 0, width, width}, {{0, 0, 0}, {1, 0, 0}});
    FK_CHECK(!isFinite(c2.derivative(.5 * width)));
    FK_CHECK(!isFinite(c3.derivative(.5 * width)));
    expectDomainFailure(c2, c2.domain());
    expectDomainFailure(c3, c3.domain());
}
FK_TEST(ArcLengthInvalidRangesAreRejected) {
    const Line<2> line(Vec2(), Vec2(1, 0));
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    for (Interval range : {Interval{nan, 1}, Interval{0, nan}, Interval{-inf, 1}, Interval{0, inf}, Interval{1, 0}}) {
        bool rejected = false;
        try { (void)arcLength(line, range); }
        catch (const std::invalid_argument &) { rejected = true; }
        FK_CHECK(rejected);
        detail::CurveLengthCache cache;
        const Line<3> line3(Vec3(), Vec3(1, 0, 0));
        rejected = false;
        try { (void)cache.get(line3, range, 1e-10); }
        catch (const std::invalid_argument &) { rejected = true; }
        FK_CHECK(rejected);
    }
    const double max = std::numeric_limits<double>::max();
    const Interval overflowingWidth{-max, max};
    expectDomainFailure(line, overflowingWidth);
    const Line<3> line3(Vec3(), Vec3(1, 0, 0));
    detail::CurveLengthCache cache;
    bool rejected = false;
    try { (void)cache.get(line3, overflowingWidth, 1e-10); }
    catch (const std::domain_error &) { rejected = true; }
    FK_CHECK(rejected);
}
FK_TEST(ArcLengthCachedFailuresDoNotPoisonFiniteEntries) {
    const Line<3> good(Vec3(), Vec3(1, 0, 0));
    const double width = std::ldexp(1., -1030);
    const BSplineCurve<3> bad(1, {0, 0, width, width}, {{0, 0, 0}, {1, 0, 0}});
    detail::CurveLengthCache cache;
    const double expected = cache.get(good, {0, 2}, 1e-10);
    FK_CHECK_NEAR(expected, 2, 1e-12);
    for (int repeat = 0; repeat != 2; ++repeat) {
        bool rejected = false;
        try { (void)cache.get(bad, bad.domain(), 1e-10); }
        catch (const std::domain_error &) { rejected = true; }
        FK_CHECK(rejected);
        // Same pointer/range/key must reject again, never return a cached
        // nonfinite result. A previously finite entry remains a valid hit.
        const auto hits = cache.statistics().hits;
        FK_CHECK(bitEqual(cache.get(good, {0, 2}, 1e-10), expected));
        FK_CHECK(cache.statistics().hits > hits);
    }
}
