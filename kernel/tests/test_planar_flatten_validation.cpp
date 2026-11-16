#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_mass.h"
#include "fk_quilt_mapping.h"
#include "fk_test.h"
#include <algorithm>
#include <limits>
#include <sstream>
using namespace ForgeCad::Kernel;
namespace {
const std::vector<double> z27 = {0,
                                 0,
                                 0,
                                 0.0023926594471666389,
                                 -0.03158506535957388,
                                 0.22274247758415186,
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
                                 -0.22274247758415186,
                                 0.03158506535957388,
                                 -0.0023926594471666389,
                                 0,
                                 0,
                                 0};

CurvePtr<2> straight(Vec2 a, Vec2 b) {
    return std::make_shared<BSplineCurve<2>>(1, std::vector<double>{0, 0, 1, 1}, std::vector<Vec2>{a, b});
}
// A single degree-27 span whose visible lobes fall between the legacy fit
// samples. The rational homogeneous numerator preserves the same hidden
// zeros, with positive nonconstant weights and X(t) equal to affine t.
Body fixture(double scale, bool rational, const Frame3 &frame, bool sense, bool trimmed = false,
             bool reverse = false, bool hole = false) {
    std::vector<Vec2> poles;
    std::vector<double> weights;
    for (int i = 0; i < 28; ++i) {
        const double x = i / 27., w = rational ? 1 + .5 * x : 1;
        const double numerator = x + (rational ? .5 * i * (i - 1.) / (27 * 26.) : 0);
        poles.emplace_back(scale * numerator / w, scale * z27[i] / w);
        if (rational)
            weights.push_back(w);
    }
    const Interval natural = trimmed ? Interval{2, 5} : Interval{0, 1};
    std::vector<double> knots(28, natural.lo);
    knots.insert(knots.end(), 28, natural.hi);
    CurvePtr<2> bottom = std::make_shared<BSplineCurve<2>>(27, knots, poles, weights);
    Interval range = natural;
    if (trimmed) {
        bottom = std::make_shared<TrimmedCurve<2>>(bottom, 2, 5);
        bottom = std::make_shared<TrimmedCurve<2>>(bottom, 2.3, 4.7);
        range = {2.3, 4.7};
    }
    const auto a = bottom->point(range.lo), b = bottom->point(range.hi);
    const std::vector<Vec2> corners{a, b, {b.x(), scale}, {a.x(), scale}};
    std::vector<Vec3> vertices;
    for (auto q : corners)
        vertices.push_back(frame.toGlobal({q.x(), q.y(), 0}));
    std::vector<Body::BuildEdge> edges;
    Body::BuildFace face;
    face.surface = std::make_shared<Plane>(frame);
    face.sense = sense;
    std::vector<Body::BuildFin> outer;
    if (reverse) {
        bottom = reversedCurve(bottom);
        range = {-range.hi, -range.lo};
    }
    edges.push_back({reverse ? 1 : 0, reverse ? 0 : 1, embedCurve(bottom, frame), range, 1e-9});
    outer.push_back({0, !reverse, bottom, 1e-9});
    for (int i = 1; i < 4; ++i) {
        auto uv = straight(corners[i], corners[(i + 1) % 4]);
        edges.push_back({i, (i + 1) % 4, embedCurve(uv, frame), {0, 1}, 1e-9});
        outer.push_back({i, true, uv, 1e-9});
    }
    face.loops = {outer};
    if (hole) {
        const std::vector<Vec2> points{{.35 * scale, .35 * scale},
                                       {.35 * scale, .65 * scale},
                                       {.65 * scale, .65 * scale},
                                       {.65 * scale, .35 * scale}};
        std::vector<Body::BuildFin> inner;
        for (auto q : points)
            vertices.push_back(frame.toGlobal({q.x(), q.y(), 0}));
        for (int i = 0; i < 4; ++i) {
            auto uv = straight(points[i], points[(i + 1) % 4]);
            inner.push_back({int(edges.size()), true, uv, 1e-9});
            edges.push_back({4 + i, 4 + (i + 1) % 4, embedCurve(uv, frame), {0, 1}, 1e-9});
        }
        face.loops.push_back(inner);
    }
    if (!sense) {
        for (auto &loop : face.loops) {
            std::reverse(loop.begin(), loop.end());
            for (auto &use : loop)
                use.sense = !use.sense;
        }
    }
    return Body::buildSheet(vertices, edges, {face});
}
std::string historyStamp(const BodyHistory &h) {
    std::ostringstream out;
    out << h.operandCount();
    for (EntityKind kind : {EntityKind::Vertex, EntityKind::Edge, EntityKind::Face}) {
        out << '/' << h.resultSize(kind);
        for (int i = 0; i < h.resultSize(kind); ++i) {
            out << ':' << h.isDeclared(kind, i);
            for (const auto &o : h.origins(kind, i))
                out << ',' << o.operand << ',' << int(o.kind) << ',' << o.index << ',' << int(o.relation);
        }
    }
    return out.str();
}
void verify(const Body &source, const Frame3 &output, const Vec2 &anchor, double tolerance, bool roundtrip) {
    FK_CHECK(checkBody(source, {true, true}).empty());
    FK_CHECK(faceArea(source, source.faces().front()) > 0);
    const auto before = writeBodyBinary(source);
    SurfaceQuiltMap map({{&source, source.faces().front()}}, anchor);
    SurfaceFlattenStats stats;
    BodyHistory history;
    const auto flat = map.flattenSheet(output, tolerance, &stats, &history);
    FK_CHECK(stats.samples > 0 && stats.boundaryDeviation <= .2 * std::min(tolerance, linearResolution()));
    for (double value :
         {stats.minimumStretch, stats.maximumStretch, stats.minimumAreaRatio, stats.maximumAreaRatio})
        FK_CHECK_NEAR(value, 1., 1e-10);
    for (EntityKind kind : {EntityKind::Vertex, EntityKind::Edge, EntityKind::Face})
        for (int i = 0; i < history.resultSize(kind); ++i)
            FK_CHECK(history.isDeclared(kind, i));
    const double sign = source.face(source.faces().front()).sense ? 1 : -1;
    const auto reference = [&](const Vec2 &uv) {
        return output.toGlobal({uv.x() - anchor.x(), sign * (uv.y() - anchor.y()), 0});
    };
    const auto check = [&](const Body &body) {
        FK_CHECK(checkBody(body, {true, true}).empty());
        FK_CHECK(body.faces().size() == source.faces().size() &&
                 body.edges().size() == source.edges().size());
        FK_CHECK(body.face(body.faces().front()).loops.size() ==
                 source.face(source.faces().front()).loops.size());
        FK_CHECK(faceArea(body, body.faces().front()) > 0);
        double error = 0;
        Vec3 witness;
        for (auto id : source.edges()) {
            const auto descendants = history.descendants(EntityKind::Edge, 0, id.index);
            FK_CHECK(descendants.size() == 1);
            if (descendants.size() != 1)
                throw std::runtime_error("missing unique edge history");
            const auto &original = source.edge(id);
            const auto &edge = body.edge(EdgeId{descendants.front()});
            FK_CHECK(edge.range.lo == original.range.lo && edge.range.hi == original.range.hi);
            auto outputBasis = edge.curve;
            while (outputBasis->type() == CurveType::Trimmed)
                outputBasis = static_cast<const TrimmedCurve<3> &>(*outputBasis).basis();
            auto finId = source.fins().front();
            for (auto candidate : source.fins())
                if (source.fin(candidate).edge == id)
                    finId = candidate;
            const auto uv = source.fin(finId).pcurve;
            auto basis = uv;
            while (basis->type() == CurveType::Trimmed)
                basis = static_cast<const TrimmedCurve<2> &>(*basis).basis();
            if (basis->type() == CurveType::BSpline) {
                const auto &input = static_cast<const BSplineCurve<2> &>(*basis);
                const auto *outputCurve = dynamic_cast<const BSplineCurve<3> *>(outputBasis.get());
                FK_CHECK(outputCurve != nullptr);
                if (!outputCurve)
                    throw std::runtime_error("known UV not emitted as explicit NURBS");
                FK_CHECK(outputCurve->degree() == input.degree());
                FK_CHECK(outputCurve->knots() == input.knots());
                FK_CHECK(outputCurve->poleCount() == input.poleCount());
                for (int pole = 0; pole < input.poleCount(); ++pole)
                    FK_CHECK(outputCurve->weight(pole) == input.weight(pole));
            }
            if (basis->type() == CurveType::Line) {
                const auto *outputCurve = dynamic_cast<const BSplineCurve<3> *>(outputBasis.get());
                FK_CHECK(outputCurve && outputCurve->degree() == 1 && outputCurve->poleCount() == 2);
                if (outputCurve)
                    FK_CHECK(outputCurve->knots() ==
                             std::vector<double>({original.range.lo, original.range.lo, original.range.hi,
                                                  original.range.hi}));
            }
            double local = 0;
            // Dense checks are independent regressions, not the certificate.
            for (int i = 0; i <= 10000; ++i) {
                const double t = original.range.lo + original.range.length() * i / 10000.;
                const auto p = reference(uv->point(t));
                const double gap = distance(p, edge.curve->point(t));
                if (gap > error) {
                    error = gap;
                    witness = p;
                }
                local = std::max(local, gap);
            }
            FK_CHECK(local <= .2 * std::min(tolerance, linearResolution()));
        }
        if (error > 0) {
            double geometric = INFINITY;
            for (auto id : body.edges()) {
                const auto &edge = body.edge(id);
                geometric = std::min(geometric, projectPoint(*edge.curve, witness, edge.range).distance);
            }
            FK_CHECK(geometric <= .2 * std::min(tolerance, linearResolution()));
        }
    };
    check(flat);
    if (roundtrip)
        check(readBodyBinary(writeBodyBinary(flat)));
    const auto &plane = static_cast<const Plane &>(*source.face(source.faces().front()).surface).frame();
    Vec3 interior;
    Vec2 uv;
    const bool found = faceInteriorPoint(source, source.faces().front(), interior, &uv);
    FK_CHECK(found);
    if (!found)
        throw std::runtime_error("missing interior point for round-trip check");
    const auto q = map.flatten(0, uv);
    FK_CHECK(distance(q, Vec2(uv.x() - anchor.x(), sign * (uv.y() - anchor.y()))) <= 1e-10);
    const double height = .002;
    FK_CHECK(distance(map.fold({q.x(), q.y(), height}), plane.toGlobal({uv.x(), uv.y(), sign * height})) <=
             1e-9);
    FK_CHECK(writeBodyBinary(source) == before);
}
} // namespace

Body lineFixture(bool sense = true) {
    const Frame3 frame({2, -3, 1}, Vec3(.2, .3, 1), Vec3(1, 0, 0));
    const std::vector<Vec2> corners{{2, 0}, {5, 0}, {5, 1}, {2, 1}};
    const std::vector<Interval> ranges{{2, 5}, {0, 1}, {0, 3}, {0, 1}};
    const std::vector<CurvePtr<2>> curves{std::make_shared<Line<2>>(Vec2(0, 0), Vec2(1, 0)),
                                          std::make_shared<Line<2>>(Vec2(5, 0), Vec2(0, 1)),
                                          std::make_shared<Line<2>>(Vec2(5, 1), Vec2(-1, 0)),
                                          std::make_shared<Line<2>>(Vec2(2, 1), Vec2(0, -1))};
    std::vector<Vec3> vertices;
    for (auto p : corners)
        vertices.push_back(frame.toGlobal({p.x(), p.y(), 0}));
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFin> loop;
    for (int i = 0; i < 4; ++i) {
        edges.push_back({i, (i + 1) % 4, embedCurve(curves[i], frame), ranges[i], 1e-9});
        loop.push_back({i, true, curves[i], 1e-9});
    }
    Body::BuildFace face;
    face.surface = std::make_shared<Plane>(frame);
    face.loops = {loop};
    face.sense = sense;
    if (!sense) {
        std::reverse(face.loops.front().begin(), face.loops.front().end());
        for (auto &use : face.loops.front())
            use.sense = !use.sense;
    }
    return Body::buildSheet(vertices, edges, {face});
}

FK_TEST(PlanarFlattenHiddenPolynomialRationalPoseScaleSense) {
    for (double scale : {.01, 1., 1000.})
        for (bool rational : {false, true})
            for (bool sense : {false, true}) {
                const Frame3 sourceFrame(scale * Vec3(2, -3, 1), Vec3(.2, .3, 1), Vec3(1, 0, 0));
                const Frame3 output(scale * Vec3(-4, 2, 3), Vec3(1, 2, 3), Vec3(1, 0, 0));
                const auto source = fixture(scale, rational, sourceFrame, sense);
                try {
                    verify(source, output, {.37 * scale, .43 * scale}, 1e-6, false);
                } catch (const std::exception &e) {
                    throw std::runtime_error(std::string(e.what()) + " scale=" + std::to_string(scale) +
                                             " rational=" + std::to_string(rational) +
                                             " sense=" + std::to_string(sense));
                }
            }
}

FK_TEST(PlanarFlattenNestedPartialTrimReverseHolesAndRoundtrip) {
    for (bool rational : {false, true})
        for (bool reverse : {false, true}) {
            const Frame3 sourceFrame({2, -3, 1}, Vec3(.2, .3, 1), Vec3(1, 0, 0));
            const Frame3 output({-4, 2, 3}, Vec3(1, 2, 3), Vec3(1, 0, 0));
            const auto source = fixture(1, rational, sourceFrame, !reverse, true, reverse, true);
            verify(source, output, {.5, .5}, 1e-6, true);
        }
}

FK_TEST(PlanarFlattenInvalidAndCancellationPreserveStatsHistorySource) {
    const auto source = fixture(1, false, Frame3(), true);
    const auto bytes = writeBodyBinary(source);
    SurfaceQuiltMap map({{&source, source.faces().front()}}, {.37, .43});
    for (double tolerance : {0., -1., 1e-10, .1, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), 1e-6}) {
        const bool cancel = tolerance == 1e-6;
        SurfaceFlattenStats stats;
        stats.minimumStretch = 2;
        stats.maximumStretch = 3;
        stats.minimumAreaRatio = 4;
        stats.maximumAreaRatio = 5;
        stats.boundaryDeviation = 6;
        stats.samples = 7;
        BodyHistory history(1, {1}, {4}, {4}, 2, 3, 4);
        const auto stamp = historyStamp(history);
        auto token = std::make_shared<std::atomic<bool>>(cancel);
        CancellationScope scope(token);
        bool rejected = false;
        try {
            map.flattenSheet(Frame3(), tolerance, &stats, &history);
        } catch (const OperationCancelled &) {
            rejected = cancel;
        } catch (const std::exception &) {
            rejected = !cancel;
        }
        FK_CHECK(rejected && historyStamp(history) == stamp);
        FK_CHECK(stats.minimumStretch == 2 && stats.maximumStretch == 3 && stats.minimumAreaRatio == 4 &&
                 stats.maximumAreaRatio == 5 && stats.boundaryDeviation == 6 && stats.samples == 7);
    }
    FK_CHECK(writeBodyBinary(source) == bytes);
}

FK_TEST(PlanarFlattenLineUVFiniteNonunitRangePreservesParameter) {
    const auto source = lineFixture();
    verify(source, Frame3({-4, 2, 3}, Vec3(1, 2, 3), Vec3(1, 0, 0)), {3, .5}, 1e-6, true);
}

FK_TEST(PlanarFlattenAngularConicLegacyCompatibility) {
    const Frame3 frame({2, -3, 1}, Vec3(.2, .3, 1), Vec3(1, 0, 0));
    const auto uv = std::make_shared<Circle<2>>(Vec2(), Vec2(1, 0), Vec2(0, 1), 1);
    const auto point = frame.toGlobal({1, 0, 0});
    Body::BuildFace face;
    face.surface = std::make_shared<Plane>(frame);
    face.loops = {{{0, true, uv, 1e-9}}};
    const auto source = Body::buildSheet({point}, {{0, 0, embedCurve(uv, frame), {0, kTwoPi}, 1e-9}}, {face});
    // Angular conics intentionally keep the existing sampled fit route.
    verify(source, Frame3({-4, 2, 3}, Vec3(1, 2, 3), Vec3(1, 0, 0)), {0, 0}, 1e-6, true);
}

FK_TEST(PlanarFlattenFoldPreservesFiniteCarrierWindowAndBoundaryRoundtrip) {
    for (bool sense : {false, true}) {
        const auto source = lineFixture(sense);
        const auto &frame = static_cast<const Plane &>(*source.face(source.faces().front()).surface).frame();
        const Vec2 anchor(3, .5);
        const double sign = sense ? 1 : -1;
        SurfaceQuiltMap map({{&source, source.faces().front()}}, anchor);
        const auto flat = [&](Vec2 uv) { return Vec3(uv.x() - anchor.x(), sign * (uv.y() - anchor.y()), 0); };
        for (Vec2 uv : {Vec2(2, 0), Vec2(5, 0), Vec2(5, 1), Vec2(2, 1)})
            FK_CHECK(distance(map.fold(flat(uv), true), frame.toGlobal({uv.x(), uv.y(), 0})) <= 1e-10);
        for (Vec2 uv : {Vec2(3, 1.01), Vec2(3, -.01), Vec2(5.01, .5), Vec2(1.99, .5)})
            FK_CHECK_THROWS(map.fold(flat(uv), true));
        const double margin = .5 * linearResolution();
        FK_CHECK(distance(map.fold(flat({5 + margin, .5}), true), frame.toGlobal({5, .5, 0})) <= 1e-9);
        FK_CHECK(distance(map.fold(flat({3, 1 + margin}), true), frame.toGlobal({3, 1, 0})) <= 1e-9);
    }
}
