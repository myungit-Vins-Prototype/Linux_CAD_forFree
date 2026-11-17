#include "fk_surface_curve_bounds.h"
#include "fk_sheet_join_validation.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_body_io.h"
#include "fk_cancel.h"
#include "fk_test.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace ForgeCad::Kernel;
namespace {
using detail::SurfaceCurveBoundStatus;
constexpr double cap = 8e-8;
const std::vector<double> cubicKnots{0, 0, 0, 0, 1, 1, 1, 1};
const std::vector<double> peakWeights{1, 1, 1e8, 1e8};
std::vector<double> knots(int degree, Interval domain = {0, 1}) {
    std::vector<double> result(degree + 1, domain.lo);
    result.insert(result.end(), degree + 1, domain.hi);
    return result;
}
// Degree elevation of the fixture's homogeneous Bernstein controls, before
// conversion back to original Euclidean IEEE controls. No production extractor.
std::shared_ptr<BSplineCurve<3>> cubicEdge(std::vector<Vec3> poles, std::vector<double> weights,
    Interval domain = {0, 1}, Transform3 pose = Transform3()) {
    for (size_t i = 0; i < poles.size(); ++i) poles[i] *= weights[i];
    while (poles.size() < 4) {
        std::vector<Vec3> elevated(poles.size() + 1);
        std::vector<double> w(weights.size() + 1);
        elevated.front() = poles.front(); elevated.back() = poles.back();
        w.front() = weights.front(); w.back() = weights.back();
        for (size_t i = 1; i < poles.size(); ++i) {
            const double a = double(i) / poles.size();
            elevated[i] = a * poles[i - 1] + (1 - a) * poles[i];
            w[i] = a * weights[i - 1] + (1 - a) * weights[i];
        }
        poles = std::move(elevated); weights = std::move(w);
    }
    for (size_t i = 0; i < poles.size(); ++i) poles[i] = pose.applyToPoint(poles[i] / weights[i]);
    return std::make_shared<BSplineCurve<3>>(3, knots(3, domain), poles, weights);
}
std::shared_ptr<BSplineCurve<3>> embedded(const BSplineCurve<2> &q, Transform3 pose = Transform3()) {
    std::vector<Vec3> poles; std::vector<double> weights;
    for (int i = 0; i < q.poleCount(); ++i) {
        poles.emplace_back(q.poles()[i].x(), q.poles()[i].y(), 0);
        weights.push_back(q.weight(i));
    }
    return cubicEdge(poles, weights, q.domain(), pose);
}
std::shared_ptr<BSplineSurface> carrier(bool nonlinear = false, bool rational = false,
    bool split = false, int degreeV = 2, double weightGauge = 1) {
    const std::vector<double> ku = split ? std::vector<double>{0, 0, 0, .5, .5, 1, 1, 1} : knots(2);
    const int nu = split ? 5 : 3;
    const double x[]{0, 1. / 3, 1}, w[]{2, 1.5, 1};
    std::vector<Vec3> poles; std::vector<double> weights;
    for (int i = 0; i < nu; ++i) for (int j = 0; j <= degreeV; ++j) {
        poles.emplace_back(rational ? x[i] : double(i) / (nu - 1), double(j) / degreeV,
            nonlinear && i == 2 ? 1 : 0);
        weights.push_back(weightGauge * (rational ? w[i] : 1));
    }
    return std::make_shared<BSplineSurface>(2, degreeV, ku, knots(degreeV), nu, degreeV + 1, poles, weights);
}
std::shared_ptr<BSplineCurve<2>> cubicUV(bool reverse = false, Interval domain = {0, 1}) {
    std::vector<Vec2> poles{{0, .1}, {.2, .6}, {.8, .3}, {1, .9}};
    std::vector<double> weights{1, 2, 1, 4};
    if (reverse) { std::reverse(poles.begin(), poles.end()); std::reverse(weights.begin(), weights.end()); }
    return std::make_shared<BSplineCurve<2>>(3, knots(3, domain), poles, weights);
}
std::shared_ptr<BSplineCurve<2>> peakUV(bool peak) {
    return std::make_shared<BSplineCurve<2>>(3, cubicKnots,
        std::vector<Vec2>{{0, 0}, {1. / 3, peak ? .01 : 0}, {2. / 3, 0}, {1, 0}}, peakWeights);
}
void expect(const CurvePtr<3> &e, const SurfacePtr &s, const CurvePtr<2> &q,
    SurfaceCurveBoundStatus status, Interval range = {0, 1}) {
    const auto result = detail::surfaceCurveResidualBound(e, s, q, range, cap);
    FK_CHECK(result.status == status);
    if (status == SurfaceCurveBoundStatus::Certified)
        FK_CHECK(std::isfinite(result.upperBound) && result.upperBound >= 0 && result.upperBound <= cap);
}
Body sheet(bool peak) {
    const std::vector<Vec3> points{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const auto bottom = embedded(*peakUV(false));
    const std::vector<Body::BuildEdge> edges{{0, 1, bottom, {0, 1}, cap},
        {1, 2, std::make_shared<Line<3>>(points[1], Vec3(0, 1, 0)), {0, 1}, cap},
        {3, 2, std::make_shared<Line<3>>(points[3], Vec3(1, 0, 0)), {0, 1}, cap},
        {0, 3, std::make_shared<Line<3>>(points[0], Vec3(0, 1, 0)), {0, 1}, cap}};
    Body::BuildFace face; face.surface = carrier();
    face.loops = {{{0, true, peakUV(peak), 3e-10},
                   {1, true, std::make_shared<Line<2>>(Vec2(1, 0), Vec2(0, 1)), 0},
                   {2, false, std::make_shared<Line<2>>(Vec2(0, 1), Vec2(1, 0)), 0},
                   {3, false, std::make_shared<Line<2>>(Vec2(0, 0), Vec2(0, 1)), 0}}};
    return Body::buildSheet(points, edges, {face});
}
}

FK_TEST(RationalPCurveLinearAndCubicMatchOriginalParameter) {
    const auto linear = std::make_shared<BSplineCurve<2>>(1, knots(1),
        std::vector<Vec2>{{.1, .2}, {.9, .8}}, std::vector<double>{1, 4});
    const auto cubic = cubicUV();
    for (const auto &q : {linear, cubic}) {
        const auto polynomial = std::make_shared<BSplineCurve<2>>(q->degree(), q->knots(), q->poles());
        FK_CHECK(norm(q->point(.5) - polynomial->point(.5)) > .01);
        expect(embedded(*q), carrier(), q, SurfaceCurveBoundStatus::Certified);
    }
}

FK_TEST(RationalPCurvePolynomialCarrierNeedsDenominatorPower) {
    const auto q = std::make_shared<BSplineCurve<2>>(1, knots(1),
        std::vector<Vec2>{{0, .25}, {1, .25}}, std::vector<double>{2, 1});
    // Q_u=t/(2-t); E=(Q_u,.25,Q_u^2) has degree-two homogeneous controls.
    expect(cubicEdge({{0, .25, 0}, {.5, .25, 0}, {1, .25, 1}}, {4, 2, 1}),
        carrier(true), q, SurfaceCurveBoundStatus::Certified);
    const auto reciprocal = std::make_shared<BSplineCurve<2>>(1, knots(1),
        std::vector<Vec2>{{.5, 0}, {1, 0}}, std::vector<double>{2, 1});
    expect(embedded(*reciprocal), carrier(), reciprocal, SurfaceCurveBoundStatus::Certified);
    // Here U=1,D=2-t. The homogeneous tensor numerator is D^3, while
    // the denominator is D^4 even though the ORIGINAL carrier is polynomial.
    // Omitting that denominator could incorrectly certify this wrong E=D^3.
    const auto wrong = std::make_shared<BSplineCurve<3>>(3, cubicKnots,
        std::vector<Vec3>{{8, 0, 0}, {4, 0, 0}, {2, 0, 0}, {1, 0, 0}});
    FK_CHECK(norm(wrong->point(0) - Vec3(reciprocal->point(0).x(), 0, 0)) > 7);
    expect(wrong, carrier(), reciprocal, SurfaceCurveBoundStatus::Failed);
}

FK_TEST(RationalPCurveRationalCarrierAndPlacementKeepAllDenominators) {
    const auto pose = Transform3::translation(Vec3(2, -3, 4)) * Transform3::scaling(Vec3(), -1.25);
    for (int power : {-100, 0, 100}) {
        const double gauge = std::ldexp(1., power);
        const auto q = std::make_shared<BSplineCurve<2>>(1, knots(1),
            std::vector<Vec2>{{0, .25}, {1, .25}}, std::vector<double>{2 * gauge, gauge});
        // S_u=u/(2-u) and Q_u=t/(2-t), hence E_x=t/(4-3t).
        expect(cubicEdge({{0, .25, 0}, {1, .25, 0}}, {4, 1}, {0, 1}, pose),
            std::make_shared<BSplineSurface>(carrier(false, true, false, 3, 1 / gauge), pose), q,
            SurfaceCurveBoundStatus::Certified);
    }
}

FK_TEST(RationalPCurveTrimReverseAndCarrierCellCrossings) {
    const auto q = cubicUV(false, {-2, 3}), reverse = cubicUV(true, {-2, 3});
    expect(embedded(*q), carrier(false, false, true), q, SurfaceCurveBoundStatus::Certified, {-2, 3});
    expect(embedded(*reverse), carrier(false, false, true), reverse, SurfaceCurveBoundStatus::Certified, {-1.75, 2.125});
    expect(embedded(*q), carrier(false, false, true), reverse, SurfaceCurveBoundStatus::Failed, {-1.75, 2.125});
}

FK_TEST(RationalPCurveHiddenPeakIsNotAContinuousSampleProof) {
    const auto s = carrier(); const auto flat = peakUV(false), peak = peakUV(true);
    const auto e = embedded(*flat);
    expect(e, s, flat, SurfaceCurveBoundStatus::Certified);
    // The grid documents aliasing only; rejection is the production bound.
    for (int i = 0; i <= 32; ++i) {
        const double t = i / 32.; const auto p = peak->point(t);
        FK_CHECK(norm(e->point(t) - s->point(p.x(), p.y())) < cap);
    }
    const double t = 1 / std::sqrt(3e8); const auto p = peak->point(t);
    FK_CHECK(norm(e->point(t) - s->point(p.x(), p.y())) > 10 * cap);
    expect(e, s, peak, SurfaceCurveBoundStatus::Failed);
}

FK_TEST(RationalPCurveLedgerSinglePlannedFinRejectsBeforeAssign) {
    auto good = sheet(false); const auto goodEdge = good.edges().front(); const auto &e = good.edge(goodEdge);
    const auto records = detail::certifySheetJoins(good, {{0, 0, cap, e.curve, e.range}}, {goodEdge});
    FK_CHECK(records.size() == 1);
    const auto fin = good.loopFins(good.face(good.faces().front()).loops.front()).front();
    FK_CHECK(good.fin(fin).pcurveTolerance == cap);
    auto bad = sheet(true); const auto id = bad.edges().front(); const auto before = writeBodyBinary(bad);
    const auto curve = bad.edge(id).curve;
    const auto support = bad.face(bad.faces().front()).surface;
    const auto range = bad.edge(id).range;
    bool rejected = false;
    try { detail::certifySheetJoins(bad, {{0, 0, cap, curve, range}}, {id}); }
    catch (const std::domain_error &) { rejected = true; }
    FK_CHECK(rejected);
    // Only the first planned incidence is tested, not multi-plan transactions.
    FK_CHECK(writeBodyBinary(bad) == before);
    FK_CHECK(bad.edge(id).curve == curve && bad.face(bad.faces().front()).surface == support);
}

FK_TEST(RationalPCurveDomainWrappersAndCancellationStayExplicit) {
    const auto q = std::make_shared<BSplineCurve<2>>(1, knots(1),
        std::vector<Vec2>{{0, .25}, {1.01, .25}}, std::vector<double>{1, 2});
    expect(embedded(*q), carrier(), q, SurfaceCurveBoundStatus::Failed);
    const auto inside = cubicUV(); const auto e = embedded(*inside);
    expect(e, carrier(), std::make_shared<TrimmedCurve<2>>(inside, 0, 1), SurfaceCurveBoundStatus::Certified);
    const auto token = std::make_shared<std::atomic<bool>>(true); CancellationScope scope(token);
    bool cancelled = false;
    try { (void)detail::surfaceCurveResidualBound(e, carrier(), inside, {0, 1}, cap); }
    catch (const OperationCancelled &) { cancelled = true; }
    FK_CHECK(cancelled);
}
