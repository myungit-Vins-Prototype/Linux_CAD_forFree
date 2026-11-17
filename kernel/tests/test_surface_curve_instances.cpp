#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include "fk_mapping_fit.h"
#include "fk_primitives.h"
#include "fk_pcurve.h"
#include "fk_quilt_mapping.h"
#include "fk_surface_curve_bounds.h"
#include "fk_surface_mapping.h"
#include "fk_sheet.h"
#include "fk_test.h"
#include "fk_transform.h"
#include <limits>
#include <algorithm>
#include <atomic>
#include <sstream>

using namespace ForgeCad::Kernel;
namespace {
std::string historyStamp(const BodyHistory &history) {
    std::ostringstream out;
    out << history.operandCount();
    for (auto kind : {EntityKind::Vertex, EntityKind::Edge, EntityKind::Face}) {
        out << ':' << history.resultSize(kind);
        for (int i = 0; i < history.resultSize(kind); ++i) {
            out << '/' << history.isDeclared(kind, i);
            for (const auto &origin : history.origins(kind, i))
                out << ',' << origin.operand << ',' << int(origin.kind) << ',' << origin.index << ','
                    << int(origin.relation);
        }
    }
    return out.str();
}
int sourceIndex(const BodyHistory &history, EntityKind kind, int index) {
    const auto &origins = history.origins(kind, index);
    if (origins.size() != 1 || origins.front().operand != 0 || origins.front().kind != kind ||
        origins.front().relation != HistoryRelation::Modified || !history.isDeclared(kind, index))
        throw std::runtime_error("mapping test: missing explicit source genealogy");
    return origins.front().index;
}
void verifyFinalFins(const Body &source, const Body &result, const BodyHistory &history,
                     double tolerance, int expectedCount) {
    const double cap = .8 * std::min(tolerance, linearResolution());
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(history.operandCount() == 1);
    int count = 0;
    for (auto face : result.faces()) {
        const FaceId parent(sourceIndex(history, EntityKind::Face, face.index));
        FK_CHECK(source.contains(parent));
        const auto &surface = result.face(face).surface;
        FK_CHECK(surface->type() == SurfaceType::BSpline);
        for (auto loop : result.face(face).loops) for (auto id : result.loopFins(loop)) {
            const auto &fin = result.fin(id);
            const auto &edge = result.edge(fin.edge);
            const EdgeId parentEdge(sourceIndex(history, EntityKind::Edge, fin.edge.index));
            const auto &original = source.edge(parentEdge);
            FK_CHECK(edge.range.lo == original.range.lo && edge.range.hi == original.range.hi);
            int incidence = 0;
            for (auto oldLoop : source.face(parent).loops) for (auto oldId : source.loopFins(oldLoop)) {
                const auto &old = source.fin(oldId);
                if (old.edge == parentEdge && old.sense == fin.sense)
                    ++incidence;
            }
            FK_CHECK(incidence == 1);
            const auto proof = detail::surfaceCurveResidualBound(edge.curve, surface, fin.pcurve, edge.range, cap);
            FK_CHECK(proof.status == detail::SurfaceCurveBoundStatus::Certified);
            FK_CHECK(std::isfinite(proof.upperBound) && proof.upperBound <= cap);
            FK_CHECK(fin.pcurveTolerance == cap);
            ++count;
        }
    }
    FK_CHECK(count == expectedCount);
}
Body unitSheet() {
    const std::vector<Vec3> points{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    std::vector<Body::BuildEdge> edges;
    for (int i = 0; i < 4; ++i)
        edges.push_back({i, (i + 1) % 4,
            std::make_shared<Line<3>>(points[i], points[(i + 1) % 4] - points[i]), {0, 1}, 0});
    Body::BuildFace face;
    face.surface = std::make_shared<Plane>(Frame3());
    face.loops = {{{0, true, nullptr, 0}, {1, true, nullptr, 0},
                   {2, true, nullptr, 0}, {3, true, nullptr, 0}}};
    auto result = Body::buildSheet(points, edges, {face});
    if (computePCurves(result) != 0)
        throw std::runtime_error("mapping test: source pcurves");
    return result;
}
SurfacePtr narrowSupport(bool spike) {
    const std::vector<double> ku{0, 0, 0, 0, .1003, .1003, .1003, .1013, .1013, .1013, 1, 1, 1, 1};
    const std::vector<double> kv{0, 0, 0, 0, 1, 1, 1, 1};
    std::vector<Vec3> poles;
    for (int i = 0; i < 10; ++i) for (int j = 0; j < 4; ++j)
        poles.emplace_back((ku[i + 1] + ku[i + 2] + ku[i + 3]) / 3., j / 3.,
                           spike && (i == 4 || i == 5) ? 1e-3 : 0.);
    return std::make_shared<BSplineSurface>(3, 3, ku, kv, 10, 4, poles);
}
Body identityImage(const Body &source, SurfacePtr support, Vec2 uvShift,
                   BodyHistory *history, double *deviation) {
    return detail::mapBodyByFunction(source, [](const Vec3 &p) { return p; }, {}, 1e-6,
        history, deviation, {}, .001, 3, [&](FaceId) {
            return detail::MappingFaceImage{support,
                [uvShift](const Vec3 &p) { return Vec2(p.x(), p.y()) + uvShift; }, true};
        });
}
Body rationalTarget() {
    const std::vector<double> knots{0, 0, 0, 1, 1, 1};
    const double x[]{0, .25, 1}, w[]{1, 2, 3};
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        poles.push_back({x[i], .5 * j, 0});
        weights.push_back(w[i]);
    }
    auto surface = std::make_shared<BSplineSurface>(2, 2, knots, knots, 3, 3, poles, weights);
    const std::vector<Vec3> points{surface->point(0, 0), surface->point(1, 0),
                                    surface->point(1, 1), surface->point(0, 1)};
    const std::vector<Body::BuildEdge> edges{{0, 1, surface->vIso(0), {0, 1}, 0},
        {1, 2, surface->uIso(1), {0, 1}, 0}, {3, 2, surface->vIso(1), {0, 1}, 0},
        {0, 3, surface->uIso(0), {0, 1}, 0}};
    const auto pc = [](Vec2 p, Vec2 d) { return std::make_shared<Line<2>>(p, d); };
    Body::BuildFace face;
    face.surface = surface;
    face.loops = {{{0, true, pc({0, 0}, {1, 0}), 0}, {1, true, pc({1, 0}, {0, 1}), 0},
                   {2, false, pc({0, 1}, {1, 0}), 0}, {3, false, pc({0, 0}, {0, 1}), 0}}};
    return Body::buildSheet(points, edges, {face});
}
}


namespace {
std::shared_ptr<BSplineSurface> planarCarrier(double delta = 0, double translatedX = 0) {
    const std::vector<double> knots{0, 0, 0, 1, 1, 1};
    std::vector<Vec3> poles;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        poles.push_back({translatedX + .5 * i + delta, .5 * j, 0});
    return std::make_shared<BSplineSurface>(2, 2, knots, knots, 3, 3, poles);
}
CurvePtr<3> verticalEdge(double x, const Transform3 &placement = Transform3(), double gap = 0) {
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i) {
        auto p = placement.applyToPoint({x, i / 3., 0});
        p[2] += gap;
        poles.push_back(p);
    }
    return std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1}, poles);
}
CurvePtr<2> verticalUV(double u) {
    return std::make_shared<BSplineCurve<2>>(1, std::vector<double>{0, 0, 1, 1},
                                           std::vector<Vec2>{{u, 0}, {u, 1}});
}
}

FK_TEST(SurfaceInstanceIdentityNestedPoseMirrorAndScale) {
    const auto target = rationalTarget();
    const auto base = std::dynamic_pointer_cast<const BSplineSurface>(target.face(target.faces().front()).surface);
    const auto uv = verticalUV(.5);
    const auto rotation = Transform3::rotation({0, 0, 0}, {1, 2, .3}, .42);
    const std::vector<Transform3> placements{Transform3(),
        Transform3::translation({3, -4, 2}) * rotation,
        Transform3::reflection({.1, -.2, .3}, {1, 2, 3}),
        Transform3::scaling({0, 0, 0}, .125), Transform3::scaling({0, 0, 0}, 100)};
    for (const auto &placement : placements) {
        const auto first = std::make_shared<BSplineSurface>(base, rotation);
        const auto instance = std::make_shared<BSplineSurface>(first, placement);
        // The actual constructor composes the IEEE matrices and canonicalizes
        // the chain. The provider must use that stored placement and base.
        FK_CHECK(instance->basis() == base && !instance->basis()->basis());
        const auto edge = verticalEdge(.5, instance->placement());
        const auto proof = detail::surfaceCurveResidualBound(edge, instance, uv, {0, 1}, 8e-8);
        FK_CHECK(proof.status == detail::SurfaceCurveBoundStatus::Certified);
        FK_CHECK(std::isfinite(proof.upperBound) && proof.upperBound <= 8e-8);
        const auto gap = detail::surfaceCurveResidualBound(verticalEdge(.5, instance->placement(), 1e-3),
                                                          instance, uv, {0, 1}, 8e-8);
        FK_CHECK(gap.status == detail::SurfaceCurveBoundStatus::Failed);
    }
    FK_CHECK(base->basis() == nullptr);
    FK_CHECK_NEAR(base->weight(1, 1), 2, 0);
}

FK_TEST(SurfaceInstanceRoundedPolesCannotHideOriginalCoefficientGap) {
    const double translation = std::ldexp(1., 18), delta = std::ldexp(1., -36), cap = std::ldexp(1., -40);
    const auto base = planarCarrier(delta);
    const auto instance = std::make_shared<BSplineSurface>(base, Transform3::translation({translation, 0, 0}));
    const auto edge = verticalEdge(translation);
    const auto uv = verticalUV(0);
    // Rounded world poles lose delta. They would produce a false certificate
    // for a different carrier; the original base+matrix have exact gap16*cap.
    FK_CHECK(instance->pole(0, 0).x() == translation);
    FK_CHECK(instance->point(0, .5).x() == translation);
    FK_CHECK(delta == 16 * cap && base->pole(0, 0).x() == delta);
    const auto roundedControl = detail::surfaceCurveResidualBound(edge, planarCarrier(0, translation), uv,
                                                                 {0, 1}, cap);
    FK_CHECK(roundedControl.status == detail::SurfaceCurveBoundStatus::Certified);
    FK_CHECK(detail::surfaceCurveResidualBound(edge, instance, uv, {0, 1}, cap).status ==
             detail::SurfaceCurveBoundStatus::Failed);
}

FK_TEST(SurfaceInstancePublicRationalQuiltPreservesFinalFinContracts) {
    const auto original = rationalTarget();
    const auto pose = Transform3::translation({.2, -.3, .4}) *
        Transform3::rotation({0, 0, 0}, {.2, 1, .3}, .25) * Transform3::scaling({0, 0, 0}, 1.25);
    const auto target = transformBody(original, pose);
    const auto support = std::dynamic_pointer_cast<const BSplineSurface>(target.face(target.faces().front()).surface);
    FK_CHECK(bool(support->basis()) && !support->basis()->basis());
    const auto source = makePlaneSheet(Frame3(), .1);
    const auto before = writeBodyBinary(source), targetBefore = writeBodyBinary(target), originalBefore = writeBodyBinary(original);
    SurfaceWrapOptions options;
    options.anchor = {.5, .5};
    SurfaceQuiltMap quilt({{&target, target.faces().front()}}, options.anchor);
    BodyHistory history;
    double deviation = -1;
    const auto result = quilt.bend(source, options, &history, &deviation);
    verifyFinalFins(source, result, history, options.tolerance, 4);
    FK_CHECK(result.face(result.faces().front()).surface == support);
    FK_CHECK(deviation <= options.tolerance);
    FK_CHECK(writeBodyBinary(result) == writeBodyBinary(quilt.bend(source, options, nullptr)));
    const auto decoded = readBodyBinary(writeBodyBinary(result));
    FK_CHECK(checkBody(decoded, {true, true}).empty());
    const auto restored = std::dynamic_pointer_cast<const BSplineSurface>(decoded.face(decoded.faces().front()).surface);
    FK_CHECK(bool(restored->basis()));
    for (auto id : decoded.fins()) {
        const auto &fin = decoded.fin(id); const auto &edge = decoded.edge(fin.edge);
        FK_CHECK(detail::surfaceCurveResidualBound(edge.curve, restored, fin.pcurve, edge.range, 8e-8).status ==
                 detail::SurfaceCurveBoundStatus::Certified);
    }
    FK_CHECK(writeBodyBinary(source) == before && writeBodyBinary(target) == targetBefore &&
             writeBodyBinary(original) == originalBefore);
}

FK_TEST(SurfaceInstanceHiddenCarrierPeakRejectedWithoutLegacyDowngrade) {
    const auto source = unitSheet();
    const auto before = writeBodyBinary(source);
    const auto base = std::dynamic_pointer_cast<const BSplineSurface>(narrowSupport(true));
    const auto instance = std::make_shared<BSplineSurface>(base, Transform3());
    FK_CHECK_NEAR(base->point(.1008, 0).z(), .00075, 1e-16);
    auto history = sameIndexHistory(source, source);
    const auto oldHistory = historyStamp(history);
    double deviation = -123;
    bool rejected = false;
    try { (void)identityImage(source, instance, {}, &history, &deviation); }
    catch (const std::domain_error &) { rejected = true; }
    FK_CHECK(rejected);
    FK_CHECK(writeBodyBinary(source) == before);
    FK_CHECK(historyStamp(history) == oldHistory && deviation == -123);
    FK_CHECK(instance->basis() == base);
}

FK_TEST(SurfaceInstanceCurveWeightsAndWrapperFamiliesRemainExplicit) {
    const auto surface = std::make_shared<BSplineSurface>(planarCarrier(), Transform3());
    const auto edge = std::dynamic_pointer_cast<const BSplineCurve<3>>(verticalEdge(.5));
    const auto uv = std::dynamic_pointer_cast<const BSplineCurve<2>>(verticalUV(.5));
    FK_CHECK(detail::surfaceCurveResidualBound(std::make_shared<TransformedCurve>(edge, Transform3()),
        surface, uv, {0, 1}, 8e-8).status == detail::SurfaceCurveBoundStatus::Certified);
    FK_CHECK(detail::surfaceCurveResidualBound(edge, surface, std::make_shared<TrimmedCurve<2>>(uv, 0, 1),
        {0, 1}, 8e-8).status == detail::SurfaceCurveBoundStatus::Certified);
    FK_CHECK(detail::surfaceCurveResidualBound(std::make_shared<BSplineCurve<3>>(3, edge->knots(), edge->poles(),
        std::vector<double>{1, .25, 1, 1}), surface, uv, {0, 1}, 8e-8).status ==
        detail::SurfaceCurveBoundStatus::Failed);
    FK_CHECK(detail::surfaceCurveResidualBound(edge, surface, std::make_shared<BSplineCurve<2>>(1, uv->knots(),
        uv->poles(), std::vector<double>{1, .25}), {0, 1}, 8e-8).status ==
        detail::SurfaceCurveBoundStatus::Failed);
}

FK_TEST(SurfaceInstanceCancellationRemainsDistinct) {
    const auto surface = std::make_shared<BSplineSurface>(planarCarrier(), Transform3());
    const auto edge = verticalEdge(.5);
    const auto uv = verticalUV(.5);
    const auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool cancelled = false;
    try { (void)detail::surfaceCurveResidualBound(edge, surface, uv, {0, 1}, 8e-8); }
    catch (const OperationCancelled &) { cancelled = true; }
    FK_CHECK(cancelled);
}
