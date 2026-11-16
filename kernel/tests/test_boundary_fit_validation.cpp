#include "fk_body_check.h"
#include "fk_boundary.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_curve_algo.h"
#include "fk_curve_fit_bounds.h"
#include "fk_curve_ops.h"
#include "fk_nurbs.h"
#include "fk_test.h"
#include <algorithm>
#include <limits>
using namespace ForgeCad::Kernel;
namespace {
// Real K10 source: an internal 0.001-wide span contains a finite hidden lobe.
// Rational homogeneous coordinates keep x(t)=t, so the first side maps to C0(u).
CurvePtr<3> hiddenSpan(bool rational) {
    const int degree = 6;
    std::vector<double> knots(7, 0);
    knots.insert(knots.end(), 6, .1);
    knots.insert(knots.end(), 6, .101);
    knots.insert(knots.end(), 7, 1);
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < 19; ++i) {
        double x = 0, x2 = 0;
        for (int j = 1; j <= degree; ++j) {
            x += knots[i + j] / degree;
            for (int k = j + 1; k <= degree; ++k)
                x2 += knots[i + j] * knots[i + k] / 15.;
        }
        const double w = rational ? 1 + .5 * x : 1;
        poles.emplace_back((rational ? x + .5 * x2 : x) / w, 0, (i == 9 ? .0032 : 0) / w);
        if (rational)
            weights.push_back(w);
    }
    return std::make_shared<BSplineCurve<3>>(degree, knots, poles, weights);
}
CurvePtr<3> hiddenDegree27() {
    const std::vector<double> z = {0,
                                   0,
                                   0,
                                   .0023926594471666389,
                                   -.03158506535957388,
                                   .22274247758415186,
                                   -1.0588644597919092,
                                   3.7068614333740251,
                                   -10.150702528319338,
                                   22.626599248232061,
                                   -42.105538437516088,
                                   66.431608164324672,
                                   -89.673893632054558,
                                   104.07526656293437,
                                   -104.07526656293437,
                                   89.673893632054558,
                                   -66.431608164324672,
                                   42.105538437516088,
                                   -22.626599248232061,
                                   10.150702528319338,
                                   -3.7068614333740251,
                                   1.0588644597919092,
                                   -.22274247758415186,
                                   .03158506535957388,
                                   -.0023926594471666389,
                                   0,
                                   0,
                                   0};
    std::vector<Vec3> poles;
    for (int i = 0; i < 28; ++i)
        poles.emplace_back(i / 27., 0, z[i]);
    std::vector<double> knots(28, 0);
    knots.insert(knots.end(), 28, 1);
    return std::make_shared<BSplineCurve<3>>(27, knots, poles);
}
PathSegment segment(const Vec3 &a, const Vec3 &b) {
    return {std::make_shared<Line<3>>(a, b - a), {0, distance(a, b)}};
}
std::vector<PathSegment> rectangle(const CurvePtr<3> &curve, Interval range, Vec3 up, bool compound = false) {
    const Vec3 a = curve->point(range.lo), b = curve->point(range.hi);
    std::vector<PathSegment> pieces;
    if (compound) {
        pieces.push_back({curve, {range.lo, .1005}});
        pieces.push_back({curve, {.1005, range.hi}});
    } else
        pieces.push_back({curve, range});
    pieces.push_back(segment(b, b + up));
    pieces.push_back(segment(b + up, a + up));
    pieces.push_back(segment(a + up, a));
    return pieces;
}
void checkC0(const Body &body, const CurvePtr<3> &curve, Interval range, double tolerance,
             bool compound = false) {
    FK_CHECK(body.isSheet() && body.faces().size() == 1);
    const auto surface = body.face(body.faces().front()).surface;
    // Independent same-parameter checks on the final carrier, not its fitted
    // pcurves. Compound-side weights follow the documented piece-length rule.
    const double split = .1005;
    const double width =
        compound ? arcLength(*curve, {range.lo, split}) /
                       (arcLength(*curve, {range.lo, split}) + arcLength(*curve, {split, range.hi}))
                 : 0;
    double worst = 0;
    const auto sample = [&](double t) {
        double u = (t - range.lo) / range.length();
        if (compound)
            u = t <= split ? width * (t - range.lo) / (split - range.lo)
                           : width + (1 - width) * (t - split) / (range.hi - split);
        worst = std::max(worst, distance(surface->point(u, 0), curve->point(t)));
    };
    for (int i = 0; i <= 10000; ++i)
        sample(range.lo + range.length() * i / 10000.);
    if (range.lo <= split && split <= range.hi)
        sample(split);
    FK_CHECK_NEAR(worst, 0., tolerance);
}
void checkCircularRows(const Body &body, double tolerance) {
    FK_CHECK(body.isSheet() && body.faces().size() == 1);
    const auto surface = body.face(body.faces().front()).surface;
    double worst = 0;
    for (int edge = 0; edge < 4; ++edge)
        for (int i = 0; i <= 1000; ++i) {
            const double t = i / 1000.;
            const Vec3 p = edge == 0   ? surface->point(t, 0)
                           : edge == 1 ? surface->point(1, t)
                           : edge == 2 ? surface->point(t, 1)
                                       : surface->point(0, t);
            worst = std::max(worst, std::hypot(std::hypot(p.x(), p.y()) - 1., p.z()));
        }
    FK_CHECK_NEAR(worst, 0., tolerance);
}
} // namespace
FK_TEST(BoundaryFitHiddenSpansAndDegree27FinalSurface) {
    for (const auto &curve : {hiddenSpan(false), hiddenSpan(true), hiddenDegree27()}) {
        const double tolerance = 1e-7;
        const Body body = boundarySheet(rectangle(curve, {0, 1}, Vec3(0, 1, 0)), tolerance);
        checkC0(body, curve, {0, 1}, tolerance);
        const auto surface = body.face(body.faces().front()).surface;
        double gap = 0;
        for (int i = 0; i <= 10000; ++i) {
            const double t = i / 10000.;
            gap = std::max(gap, distance(curve->point(t), surface->point(t, 0)));
        }
        FK_CHECK_NEAR(gap, 0., tolerance);
    }
}
FK_TEST(BoundaryFitCompoundSidesPoseScaleAndReversals) {
    for (double scale : {.01, 1., 1000.})
        for (bool rational : {false, true}) {
            const double shear[3][3] = {{1, .2, .1}, {0, 1, .2}, {.1, 0, 1}};
            const auto transform = Transform3::translation(scale * Vec3(2, -1, .5)) *
                                   Transform3::rotation({}, Vec3(1, 2, 3), .4) *
                                   Transform3::scaling({}, scale) * Transform3::fromParts(shear, {});
            const CurvePtr<3> curve = std::make_shared<TransformedCurve>(hiddenSpan(rational), transform);
            auto pieces = rectangle(curve, {0, 1}, transform.applyToVector(Vec3(0, 1, 0)), true);
            // Keep the first side as C0, but reverse connecting input curves and
            // shuffle them; chaining must retain the actual topological use.
            for (std::size_t i = 2; i < pieces.size(); ++i) {
                pieces[i].curve = reversedCurve(pieces[i].curve);
                pieces[i].range = {-pieces[i].range.hi, -pieces[i].range.lo};
            }
            std::swap(pieces[2], pieces[4]);
            checkC0(boundarySheet(pieces, 1e-7 * scale), curve, {0, 1}, 1e-7 * scale, true);
        }
}
FK_TEST(BoundaryFitPartialNestedTrimAndAutomaticSubpieces) {
    CurvePtr<3> curve = std::make_shared<TrimmedCurve<3>>(hiddenSpan(true), .01, .99);
    curve = std::make_shared<TrimmedCurve<3>>(curve, .03, .97);
    checkC0(boundarySheet(rectangle(curve, {.05, .95}, Vec3(0, 1, 0)), 1e-7), curve, {.05, .95}, 1e-7);
    const Circle<3> circle({}, Vec3(1, 0, 0), Vec3(0, 1, 0), 1);
    const auto nurbs = std::make_shared<BSplineCurve<3>>(
        joinBezierPieces(rationalBezierPieces(circle, {0, kTwoPi}), {0, .5 * kPi, kPi, 1.5 * kPi, kTwoPi}));
    checkCircularRows(boundarySheet({{nurbs, {0, kTwoPi}}}, 1e-7), 1e-7);
}
FK_TEST(BoundaryFitConicCompatibilityAndCancellation) {
    const auto circle = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 1);
    checkCircularRows(boundarySheet({{circle, {0, kTwoPi}}}, 1e-7), 1e-7);
    const auto pieces = rectangle(hiddenSpan(false), {0, 1}, Vec3(0, 1, 0));
    const auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool cancelled = false;
    try {
        boundarySheet(pieces, 1e-7);
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
}

FK_TEST(BoundaryFitReversedKnownSide) {
    for (bool rational : {false, true}) {
        const auto curve = reversedCurve(hiddenSpan(rational));
        checkC0(boundarySheet(rectangle(curve, {-1, 0}, Vec3(0, 1, 0)), 1e-7), curve, {-1, 0}, 1e-7);
    }
}

FK_TEST(BoundaryFitKnownSideAllRowsAndTraversalDirections) {
    for (bool rational : {false, true}) {
        const auto curve = hiddenSpan(rational);
        for (int first = 1; first <= 3; ++first)
            for (bool backwards : {false, true}) {
                auto pieces = rectangle(curve, {0, 1}, Vec3(0, 1, 0));
                std::rotate(pieces.begin(), pieces.begin() + first, pieces.end());
                if (backwards) {
                    // Reverse only the starting LINE. The native NURBS source is
                    // unchanged; chaining must choose Piece.forward=false for it.
                    const auto &edge = pieces.front();
                    pieces.front() =
                        segment(edge.curve->point(edge.range.hi), edge.curve->point(edge.range.lo));
                }
                const Body body = boundarySheet(pieces, 1e-7);
                const auto surface = body.face(body.faces().front()).surface;
                const int slot = backwards ? first : 4 - first;
                double worst = 0;
                for (int i = 0; i <= 10000; ++i) {
                    const double t = i / 10000.;
                    const double traversal = backwards ? 1 - t : t;
                    const Vec3 p = slot == 1   ? surface->point(1, traversal)
                                   : slot == 2 ? surface->point(1 - traversal, 1)
                                               : surface->point(0, 1 - traversal);
                    worst = std::max(worst, distance(p, curve->point(t)));
                }
                FK_CHECK_NEAR(worst, 0., 1e-7);
            }
    }
}

FK_TEST(BoundaryFitInvalidTolerance) {
    const auto pieces = rectangle(hiddenSpan(false), {0, 1}, Vec3(0, 1, 0));
    for (double tolerance :
         {0., -1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        FK_CHECK_THROWS(boundarySheet(pieces, tolerance));
    }
}

FK_TEST(BoundaryFitFinalCoonsEndpointMismatchBudget) {
    const double tolerance = 1e-7;
    for (double factor : {0., .25, 2., 5.}) {
        const double dz = factor * tolerance;
        const std::vector<PathSegment> pieces{
            segment(Vec3(0, 0, 0), Vec3(1, 0, 0)), segment(Vec3(1, 0, dz), Vec3(1, 1, 0)),
            segment(Vec3(1, 1, 0), Vec3(0, 1, 0)), segment(Vec3(0, 1, 0), Vec3(0, 0, 0))};
        FaceAliases labels{{"sentinel"}};
        if (factor <= .25) {
            const Body body = boundarySheet(pieces, tolerance, &labels);
            const auto surface = body.face(body.faces().front()).surface;
            double worst = 0;
            for (int i = 0; i <= 1000; ++i) {
                const double t = i / 1000.;
                worst = std::max(worst, distance(surface->point(t, 0), Vec3(t, 0, 0)));
            }
            FK_CHECK_NEAR(worst, 0., tolerance);
            FK_CHECK(!labels.empty());
        } else {
            FK_CHECK_THROWS(boundarySheet(pieces, tolerance, &labels));
            FK_CHECK(labels.empty());
        }
    }
}

FK_TEST(BoundaryFitOracleClosedJunctionEndpoints) {
    for (bool rawLine : {false, true})
        for (bool reversed : {false, true}) {
            CurvePtr<3> first, second;
            if (rawLine) {
                first = std::make_shared<Line<3>>(Vec3(0, 0, 0), Vec3(1, 0, 0));
                second = std::make_shared<Line<3>>(Vec3(3, 0, 0), Vec3(1, 0, 0));
            } else {
                first = std::make_shared<BSplineCurve<3>>(1, std::vector<double>{0, 0, 1, 1},
                                                          std::vector<Vec3>{{0, 0, 0}, {1, 0, 0}});
                second = std::make_shared<BSplineCurve<3>>(1, std::vector<double>{0, 0, 1, 1},
                                                           std::vector<Vec3>{{3, 0, 0}, {4, 0, 0}});
            }
            const auto bound = detail::curvePositionErrorBound(
                {{first, {0, 1}, {0, 1}, reversed}, {second, {0, 1}, {1, 2}, reversed}});
            FK_CHECK(bool(bound));
            const double gap = distance(first->point(reversed ? 0 : 1), second->point(reversed ? 1 : 0));
            for (bool queryRight : {false, true}) {
                // Each candidate exactly follows its own linear span. Its CLOSED
                // query also contains the other span's value at their junction.
                const auto &source = queryRight ? second : first;
                std::array<Vec3, 4> candidate;
                for (int i = 0; i < 4; ++i)
                    candidate[i] = source->point(reversed ? 1 - i / 3. : i / 3.);
                const double error = bound(queryRight ? Interval{1, 2} : Interval{0, 1}, candidate);
                FK_CHECK(error >= gap);
            }
        }
}
