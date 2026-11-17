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

FK_TEST(MappingNonPlaneCylinderFinalFinsAndNullHistory) {
    const auto source = makeBox(Frame3({-2, -1, 0}, {0, 0, 1}, {1, 0, 0}), 4, 2, 1);
    const auto before = writeBodyBinary(source);
    const CylindricalSurface target(Frame3(), 20);
    SurfaceWrapOptions options;
    options.anchor = {kHalfPi, 10};
    options.tolerance = 1e-6;
    BodyHistory history;
    double deviation = -1;
    const auto result = wrapBodyOnSurface(source, target, options, &history, &deviation);
    verifyFinalFins(source, result, history, options.tolerance, 24);
    FK_CHECK(!result.isSheet() && deviation <= options.tolerance);
    FK_CHECK(writeBodyBinary(result) == writeBodyBinary(wrapBodyOnSurface(source, target, options, nullptr)));
    FK_CHECK(writeBodyBinary(source) == before);
}

FK_TEST(MappingNonPlaneQuadraticTargetFinalFinsAndNullHistory) {
    const std::vector<double> knots{0, 0, 0, 1, 1, 1};
    const double xy[]{-5, 0, 5}, square[]{25, -25, 25};
    std::vector<Vec3> poles;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        poles.emplace_back(xy[i], xy[j], .02 * (square[i] + square[j]));
    const BSplineSurface target(2, 2, knots, knots, 3, 3, poles);
    const auto originalPoles = poles;
    const auto source = makeBox(Frame3({-.1, -.1, 0}, {0, 0, 1}, {1, 0, 0}), .2, .2, .02);
    const auto before = writeBodyBinary(source);
    SurfaceWrapOptions options;
    options.anchor = {.5, .5};
    BodyHistory history;
    double deviation = -1;
    const auto result = wrapBodyOnSurface(source, target, options, &history, &deviation);
    verifyFinalFins(source, result, history, options.tolerance, 24);
    FK_CHECK(deviation <= options.tolerance);
    FK_CHECK(writeBodyBinary(result) == writeBodyBinary(wrapBodyOnSurface(source, target, options, nullptr)));
    FK_CHECK(writeBodyBinary(source) == before);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        FK_CHECK(distance(target.pole(i, j), originalPoles[3 * i + j]) == 0);
}

FK_TEST(MappingNonPlaneRationalCarrierFinalFinsAndNullHistory) {
    const auto source = makePlaneSheet(Frame3(), .1), target = rationalTarget();
    const auto before = writeBodyBinary(source), original = writeBodyBinary(target);
    SurfaceWrapOptions options;
    options.anchor = {.5, .5};
    SurfaceQuiltMap quilt({{&target, target.faces().front()}}, options.anchor);
    BodyHistory history;
    double deviation = -1;
    const auto result = quilt.bend(source, options, &history, &deviation);
    verifyFinalFins(source, result, history, options.tolerance, 4);
    FK_CHECK(result.face(result.faces().front()).surface == target.face(target.faces().front()).surface);
    FK_CHECK(writeBodyBinary(result) == writeBodyBinary(quilt.bend(source, options, nullptr)));
    FK_CHECK(writeBodyBinary(source) == before && writeBodyBinary(target) == original);
}

FK_TEST(MappingNonPlaneHiddenCarrierPeakRejectedAtomically) {
    const auto source = unitSheet();
    const auto before = writeBodyBinary(source);
    BodyHistory history;
    double deviation = -1;
    const auto control = identityImage(source, narrowSupport(false), {}, &history, &deviation);
    verifyFinalFins(source, control, history, 1e-6, 4);
    const auto spiked = narrowSupport(true);
    // The real geometric mismatch is between the old 33-point checks.
    // This witness is a regression observation, not the continuous certificate.
    FK_CHECK_NEAR(spiked->point(.1008, 0).z(), .00075, 1e-16);
    for (int i = 0; i <= 32; ++i)
        FK_CHECK(std::fabs(spiked->point(i / 32., 0).z()) < 1e-16);
    history = sameIndexHistory(source, source);
    const auto oldHistory = historyStamp(history);
    deviation = -123;
    bool rejected = false;
    try {
        (void)identityImage(source, spiked, {}, &history, &deviation);
    } catch (const std::domain_error &) {
        rejected = true;
    }
    FK_CHECK(rejected);
    FK_CHECK(writeBodyBinary(source) == before);
    FK_CHECK(historyStamp(history) == oldHistory && deviation == -123);
}

FK_TEST(MappingNonPlaneFarOutsideCannotBeRepairedByClamping) {
    const auto source = unitSheet();
    const auto before = writeBodyBinary(source);
    auto history = sameIndexHistory(source, source);
    const auto oldHistory = historyStamp(history);
    double deviation = -123;
    bool rejected = false;
    try {
        (void)identityImage(source, narrowSupport(false), {10, 0}, &history, &deviation);
    } catch (const std::domain_error &) {
        rejected = true;
    }
    FK_CHECK(rejected);
    FK_CHECK(writeBodyBinary(source) == before);
    FK_CHECK(historyStamp(history) == oldHistory && deviation == -123);
}

FK_TEST(MappingNonPlaneCancellationPreservesInputsAndOutputs) {
    const auto source = unitSheet();
    const auto before = writeBodyBinary(source);
    auto history = sameIndexHistory(source, source);
    const auto oldHistory = historyStamp(history);
    double deviation = -123;
    const auto cancel = std::make_shared<std::atomic<bool>>(true);
    bool cancelled = false;
    {
        CancellationScope scope(cancel);
        try {
            (void)identityImage(source, narrowSupport(false), {}, &history, &deviation);
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
    }
    FK_CHECK(cancelled);
    FK_CHECK(writeBodyBinary(source) == before);
    FK_CHECK(historyStamp(history) == oldHistory && deviation == -123);
}
