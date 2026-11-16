#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_cancel.h"
#include "fk_curve_fit_bounds.h"
#include "fk_surface_tools.h"
#include "fk_test.h"
#include "fk_transform.h"
#include <algorithm>
#include <limits>
using namespace ForgeCad::Kernel;
namespace {
namespace composition_fixture {
using namespace ForgeCad::Kernel;
inline BSplineSurfacePtr support(bool rational, double ySign = 1.) {
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
        const double px = (rational ? x + .5 * x2 : x) / w;
        poles.emplace_back(px, 0, (i == 9 ? .0016 : 0) / w);
        poles.emplace_back(px, ySign, 0);
        weights.push_back(w);
        weights.push_back(w);
    }
    return std::make_shared<BSplineSurface>(6, 1, knots, std::vector<double>{0, 0, 1, 1}, 19, 2, poles,
                                            weights);
}
inline CurvePtr<2> affineUV(Vec2 a, Vec2 b, Interval range) {
    return std::make_shared<BSplineCurve<2>>(1, std::vector<double>{range.lo, range.lo, range.hi, range.hi},
                                             std::vector<Vec2>{a, b});
}
inline Body single(bool rational = false, double vLo = 0., double vHi = 1., bool nonunit = false,
                   bool reverse = false) {
    const auto s = support(rational);
    std::vector<Vec3> vertices{s->point(0, vLo), s->point(1, vLo), s->point(1, vHi), s->point(0, vHi)};
    CurvePtr<3> bottom = std::make_shared<BSplineCurve<3>>(s->vIsoCurve(vLo));
    Interval bottomRange{0, 1};
    if (nonunit) {
        const auto &c = static_cast<const BSplineCurve<3> &>(*bottom);
        auto knots = c.knots();
        for (double &k : knots)
            k = -2 + 5 * k;
        bottom = std::make_shared<BSplineCurve<3>>(c.degree(), knots, c.poles(), c.weights());
        bottomRange = {-2, 3};
    }
    const std::vector<Body::BuildEdge> edges{{0, 1, bottom, bottomRange, 1e-9},
                                             {1, 2, s->uIso(1), {vLo, vHi}, 1e-9},
                                             {3, 2, s->vIso(vHi), {0, 1}, 1e-9},
                                             {0, 3, s->uIso(0), {vLo, vHi}, 1e-9}};
    Body::BuildFace face;
    face.surface = s;
    face.loops = {{{0, true, affineUV({0, vLo}, {1, vLo}, bottomRange), 1e-9},
                   {1, true, affineUV({1, vLo}, {1, vHi}, {vLo, vHi}), 1e-9},
                   {2, false, affineUV({0, vHi}, {1, vHi}, {0, 1}), 1e-9},
                   {3, false, affineUV({0, vLo}, {0, vHi}, {vLo, vHi}), 1e-9}}};
    Body body = Body::buildSheet(vertices, edges, {face});
    if (reverse)
        body.reverseEdge(body.edges().front());
    return body;
}
inline Body shared(bool rational = false) {
    const auto upper = support(rational), lower = support(rational, -1);
    std::vector<Vec3> vertices{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, -1, 0}, {1, -1, 0}};
    std::vector<Body::BuildEdge> edges{{0, 1, upper->vIso(0), {0, 1}, 1e-9}};
    for (auto pair : std::vector<std::pair<int, int>>{{1, 2}, {2, 3}, {3, 0}, {0, 4}, {4, 5}, {5, 1}})
        edges.push_back(
            {pair.first,
             pair.second,
             std::make_shared<Line<3>>(vertices[pair.first], vertices[pair.second] - vertices[pair.first]),
             {0, 1},
             1e-9});
    Body::BuildFace a, b;
    a.surface = upper;
    b.surface = lower;
    b.sense = false;
    a.loops = {{{0, true, affineUV({0, 0}, {1, 0}, {0, 1}), 1e-9},
                {1, true, affineUV({1, 0}, {1, 1}, {0, 1}), 1e-9},
                {2, true, affineUV({1, 1}, {0, 1}, {0, 1}), 1e-9},
                {3, true, affineUV({0, 1}, {0, 0}, {0, 1}), 1e-9}}};
    b.loops = {{{0, false, affineUV({0, 0}, {1, 0}, {0, 1}), 1e-9},
                {4, true, affineUV({0, 0}, {0, 1}, {0, 1}), 1e-9},
                {5, true, affineUV({0, 1}, {1, 1}, {0, 1}), 1e-9},
                {6, true, affineUV({1, 1}, {1, 0}, {0, 1}), 1e-9}}};
    return Body::buildSheet(vertices, edges, {a, b});
}
inline double gap(const Body &body, int samples = 10000) {
    double worst = 0;
    for (auto id : body.fins()) {
        const auto &fin = body.fin(id);
        const auto &edge = body.edge(fin.edge);
        const auto &s = body.face(body.finFace(id)).surface;
        for (int i = 0; i <= samples; ++i) {
            const double t = edge.range.lo + edge.range.length() * i / samples;
            const auto q = fin.pcurve->point(t);
            worst = std::max(worst, distance(s->point(q.x(), q.y()), edge.curve->point(t)));
        }
    }
    return worst;
}
} // namespace composition_fixture

} // namespace

FK_TEST(SurfaceCurveFitPolesHiddenSupportPoseScaleAndParameters) {
    for (double scale : {.01, 1., 1000.})
        for (bool rational : {false, true})
            for (int mode : {0, 1, 2}) {
                Body source = composition_fixture::single(rational, mode == 1 ? .2 : 0., mode == 1 ? .8 : 1.,
                                                          mode == 2, mode == 2);
                const auto transform = Transform3::translation(scale * Vec3(2, -1, .5)) *
                                       Transform3::rotation({}, Vec3(1, 2, 3), .4) *
                                       Transform3::scaling({}, scale);
                source = transformBody(source, transform);
                const double tolerance = 1e-7 * scale;
                FK_CHECK(checkBody(source, {true, true}).empty());
                FK_CHECK(composition_fixture::gap(source) <= tolerance);
                const auto frozen = writeBodyBinary(source);
                const Vec3 delta = transform.applyToVector(Vec3(0, 0, .0016));
                const auto result =
                    editBodyFacesPoles(source, {{source.faces().front(), {{9, 0, delta, 1}}, {}}}, tolerance);
                // A valid input MUST succeed; no conservative rejection is accepted here.
                FK_CHECK(checkBody(result, {true, true}).empty());
                FK_CHECK_NEAR(composition_fixture::gap(result), 0., tolerance);
                FK_CHECK(writeBodyBinary(source) == frozen);
            }
}

FK_TEST(SurfaceCurveFitSharedHiddenNeighborRejectedAndCoherentAccepted) {
    for (bool rational : {false, true}) {
        const Body source = composition_fixture::shared(rational);
        FK_CHECK(checkBody(source, {true, true}).empty());
        FK_CHECK_NEAR(composition_fixture::gap(source), 0., 1e-9);
        const auto frozen = writeBodyBinary(source);
        const SurfaceFacePoleChange upper{source.faces()[0], {{9, 0, {0, 0, .0016}, 1}}, {}};
        FK_CHECK_THROWS(editBodyFacesPoles(source, {upper}, 1e-7));
        FK_CHECK(writeBodyBinary(source) == frozen);
        const SurfaceFacePoleChange lower{source.faces()[1], {{9, 0, {0, 0, .0016}, 1}}, {}};
        const Body result = editBodyFacesPoles(source, {upper, lower}, 1e-7);
        FK_CHECK(checkBody(result, {true, true}).empty());
        FK_CHECK_NEAR(composition_fixture::gap(result), 0., 1e-7);
        FK_CHECK(writeBodyBinary(source) == frozen);
    }
}

FK_TEST(SurfaceCurveFitDirectOracleKnotCrossingsAndUnsupportedUV) {
    for (bool rational : {false, true})
        for (bool trimmed : {false, true}) {
            const auto surface = composition_fixture::support(rational);
            CurvePtr<2> uv = composition_fixture::affineUV({0, 0}, {1, 0}, {-2, 3});
            Interval range{-2, 3};
            if (trimmed) {
                uv = std::make_shared<TrimmedCurve<2>>(uv, -1.9, 2.9);
                uv = std::make_shared<TrimmedCurve<2>>(uv, -1.8, 2.8);
                range = {-1.8, 2.8};
            }
            const auto data = detail::surfaceCompositionErrorBound(surface, uv, range);
            FK_CHECK(bool(data.bound));
            for (double knot : {-1.5, -1.495})
                FK_CHECK(std::any_of(data.breaks.begin(), data.breaks.end(),
                                     [&](double t) { return std::abs(t - knot) < 1e-12; }));
            const double peak = surface->point(.1005, 0).z();
            std::array<Vec3, 4> candidate;
            for (int i = 0; i < 4; ++i) {
                const double t = range.lo + range.length() * i / 3.;
                candidate[i] = Vec3(uv->point(t).x(), 0, 0);
            }
            const double error = data.bound(range, candidate);
            FK_CHECK(std::isfinite(error) && error >= peak);
            const auto diagonal = composition_fixture::affineUV({0, 0}, {1, 1}, {0, 1});
            FK_CHECK(!detail::surfaceCompositionErrorBound(surface, diagonal, {0, 1}).bound);
        }
}

FK_TEST(SurfaceCurveFitInstancedSurfaceAndMaterializedAffine) {
    const double shear[3][3] = {{2, .3, .2}, {-.1, 1, .4}, {.2, 0, 1.2}};
    const auto affine = Transform3::fromParts(shear, {2, -1, .5});
    const auto similarity = Transform3::translation({.5, -.2, .3}) * Transform3::scaling({}, 1.3);
    const auto outer = Transform3::translation({-1, 2, 3}) * Transform3::rotation({}, {1, 2, 3}, .7);
    for (bool rational : {false, true})
        for (bool materialized : {false, true}) {
            const auto original = composition_fixture::support(rational);
            BSplineSurfacePtr basis = original;
            const auto transform = materialized ? affine : similarity;
            if (materialized) {
                // Instance placement intentionally supports similarities only.
                // A general shear is valid NURBS geometry through its own poles.
                std::vector<Vec3> poles;
                std::vector<double> weights;
                for (int u = 0; u < original->uPoleCount(); ++u)
                    for (int v = 0; v < original->vPoleCount(); ++v) {
                        poles.push_back(affine.applyToPoint(original->pole(u, v)));
                        weights.push_back(original->weight(u, v));
                    }
                basis = std::make_shared<BSplineSurface>(
                    original->uDegree(), original->vDegree(), original->uKnots(), original->vKnots(),
                    original->uPoleCount(), original->vPoleCount(), poles, weights);
            } else
                basis = std::make_shared<BSplineSurface>(original, similarity);
            const auto instance = std::make_shared<BSplineSurface>(basis, outer);
            const auto uv = composition_fixture::affineUV({0, 0}, {1, 0}, {0, 1});
            const auto data = detail::surfaceCompositionErrorBound(instance, uv, {0, 1});
            FK_CHECK(bool(data.bound));
            std::array<Vec3, 4> candidate;
            for (int i = 0; i < 4; ++i)
                candidate[i] = outer.applyToPoint(transform.applyToPoint(Vec3(i / 3., 0, 0)));
            const double peak = distance(instance->point(.1005, 0),
                                         outer.applyToPoint(transform.applyToPoint(Vec3(.1005, 0, 0))));
            const double error = data.bound({0, 1}, candidate);
            FK_CHECK(std::isfinite(error) && error >= peak);
        }
}

FK_TEST(SurfaceCurveFitInvalidInputAndCancellation) {
    const auto surface = composition_fixture::support(false);
    const auto uv = composition_fixture::affineUV({0, 0}, {1, 0}, {0, 1});
    FK_CHECK_THROWS(detail::surfaceCompositionErrorBound({}, uv, {0, 1}));
    FK_CHECK_THROWS(detail::surfaceCompositionErrorBound(surface, {}, {0, 1}));
    for (Interval range : std::vector<Interval>{{0, 0},
                                                {1, 0},
                                                {0, std::numeric_limits<double>::infinity()},
                                                {std::numeric_limits<double>::quiet_NaN(), 1}})
        FK_CHECK_THROWS(detail::surfaceCompositionErrorBound(surface, uv, range));
    const auto data = detail::surfaceCompositionErrorBound(surface, uv, {0, 1});
    const std::array<Vec3, 4> candidate{};
    FK_CHECK(!std::isfinite(data.bound({-1, 2}, candidate)));
    const auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool factoryCancelled = false, queryCancelled = false;
    try {
        detail::surfaceCompositionErrorBound(surface, uv, {0, 1});
    } catch (const OperationCancelled &) {
        factoryCancelled = true;
    }
    try {
        data.bound({0, 1}, candidate);
    } catch (const OperationCancelled &) {
        queryCancelled = true;
    }
    FK_CHECK(factoryCancelled && queryCancelled);
}
