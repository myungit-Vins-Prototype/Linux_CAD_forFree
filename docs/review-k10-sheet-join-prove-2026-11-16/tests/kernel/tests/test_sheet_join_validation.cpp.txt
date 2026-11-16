#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_cancel.h"
#include "fk_classify.h"
#include "fk_curve_ops.h"
#include "fk_history.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_sheet.h"
#include "fk_test.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
using namespace ForgeCad::Kernel;
namespace {
Body fixture(bool tangent = false, double gap = 0) {
    std::vector<SurfacePtr> surfaces;
    for (int side = 0; side < 2; ++side) {
        std::vector<Vec3> poles;
        double lo = side == 0 ? -1 : 0;
        for (int i = 0; i <= 2; ++i)
            for (int j = 0; j <= 2; ++j) {
                double x = lo + i * .5,
                       x2 = lo * lo + lo * i + (i == 2 ? 1 : 0), v = j * .5,
                       v2 = j == 2 ? 1 : 0;
                double z = (tangent ? .2 : (side == 0 ? -.3 : .4)) * x +
                           .1 * x2 + .1 * v2 * (1 + (side == 0 ? .3 : .6) * x2);
                poles.emplace_back(x, v, z + gap * (side == 1));
            }
        surfaces.push_back(std::make_shared<BSplineSurface>(
            2, 2, std::vector<double>{0, 0, 0, 1, 1, 1},
            std::vector<double>{0, 0, 0, 1, 1, 1}, 3, 3, poles));
    }
    std::vector<Vec3> points;
    for (auto uv : {Vec2(0, 0), Vec2(1, 0), Vec2(1, 1), Vec2(0, 1)})
        points.push_back(surfaces[0]->point(uv.x(), uv.y()));
    points.push_back(surfaces[1]->point(1, 0));
    points.push_back(surfaces[1]->point(1, 1));
    std::vector<Body::BuildEdge> edges{{0, 1, surfaces[0]->vIso(0), {0, 1}, 0},
                                       {1, 2, surfaces[0]->uIso(1), {0, 1}, 0},
                                       {3, 2, surfaces[0]->vIso(1), {0, 1}, 0},
                                       {0, 3, surfaces[0]->uIso(0), {0, 1}, 0},
                                       {1, 4, surfaces[1]->vIso(0), {0, 1}, 0},
                                       {4, 5, surfaces[1]->uIso(1), {0, 1}, 0},
                                       {2, 5, surfaces[1]->vIso(1), {0, 1}, 0}};
    Body::BuildFace a, b;
    a.surface = surfaces[0];
    b.surface = surfaces[1];
    a.loops.push_back({{0, true, nullptr, 0},
                       {1, true, nullptr, 0},
                       {2, false, nullptr, 0},
                       {3, false, nullptr, 0}});
    b.loops.push_back({{4, true, nullptr, 0},
                       {5, true, nullptr, 0},
                       {6, false, nullptr, 0},
                       {1, false, nullptr, 0}});
    auto body = Body::buildSheet(points, edges, {a, b});
    computePCurves(body);
    return body;
}
Body aliasFixture(double amplitude, double center, bool growingGap = false) {
    auto b = fixture(true);
    const std::vector<double> sites{
        0, 1, center - .00005, center, center + .00005, 2};
    std::vector<double> knots(4, 0);
    for (size_t n = 1; n + 1 < sites.size(); ++n)
        knots.insert(knots.end(), 3, sites[n]);
    knots.insert(knots.end(), 4, 2);
    std::vector<SurfacePtr> surfaces;
    for (int side = 0; side < 2; ++side) {
        std::vector<Vec3> poles;
        for (int i = 0; i < 3; ++i) {
            double lo = side == 0 ? -1 : 0, x = lo + .5 * i,
                   x2 = lo * lo + lo * i + (i == 2 ? 1 : 0);
            for (size_t s = 0; s + 1 < sites.size(); ++s)
                for (int j = s == 0 ? 0 : 1; j <= 3; ++j) {
                    double v = sites[s] + (sites[s + 1] - sites[s]) * j / 3;
                    double z = side == 1 && s == 2   ? (j >= 2 ? amplitude : 0)
                               : side == 1 && s == 3 ? (j <= 1 ? amplitude : 0)
                                                     : 0;
                    if (growingGap) {
                        double aa = sites[s] - 1, bb = sites[s + 1] - 1,
                               h = sites[s + 1] - sites[s];
                        z = side == 1 && s > 0
                                ? amplitude *
                                      (j == 0   ? aa * aa
                                       : j == 1 ? aa * aa + 2 * aa * h / 3
                                       : j == 2 ? bb * bb - 2 * bb * h / 3
                                                : bb * bb)
                                : 0;
                    }
                    poles.emplace_back(x, v * (1 + (side == 0 ? .1 : .3) * x2),
                                       z);
                }
        }
        surfaces.push_back(std::make_shared<BSplineSurface>(
            2, 3, std::vector<double>{0, 0, 0, 1, 1, 1}, knots, 3,
            int((sites.size() - 1) * 3 + 1), poles));
    }
    std::vector<Vec3> pts;
    for (auto uv : {Vec2(0, 0), Vec2(1, 0), Vec2(1, 1), Vec2(0, 1)})
        pts.push_back(surfaces[0]->point(uv.x(), uv.y()));
    pts.push_back(surfaces[1]->point(1, 0));
    pts.push_back(surfaces[1]->point(1, 1));
    std::vector<Body::BuildEdge> edges{{0, 1, surfaces[0]->vIso(0), {0, 1}, 0},
                                       {1, 2, surfaces[0]->uIso(1), {0, 1}, 0},
                                       {3, 2, surfaces[0]->vIso(1), {0, 1}, 0},
                                       {0, 3, surfaces[0]->uIso(0), {0, 1}, 0},
                                       {1, 4, surfaces[1]->vIso(0), {0, 1}, 0},
                                       {4, 5, surfaces[1]->uIso(1), {0, 1}, 0},
                                       {2, 5, surfaces[1]->vIso(1), {0, 1}, 0}};
    Body::BuildFace a, bb;
    a.surface = surfaces[0];
    bb.surface = surfaces[1];
    a.loops.push_back({{0, true, nullptr, 0},
                       {1, true, nullptr, 0},
                       {2, false, nullptr, 0},
                       {3, false, nullptr, 0}});
    bb.loops.push_back({{4, true, nullptr, 0},
                        {5, true, nullptr, 0},
                        {6, false, nullptr, 0},
                        {1, false, nullptr, 0}});
    auto body = Body::buildSheet(pts, edges, {a, bb});
    computePCurves(body);
    return body;
}
std::string number(double d) {
    std::ostringstream o;
    o << '"' << std::hexfloat << d << '"';
    return o.str();
}
template <class T> void array(std::ostream &o, const T &v) {
    o << '[';
    bool sep = false;
    for (auto x : v) {
        if (sep)
            o << ',';
        sep = true;
        o << number(x);
    }
    o << ']';
}
template <int N> void vector(std::ostream &o, const Vec<N> &v) {
    o << '[';
    for (int i = 0; i < N; ++i) {
        if (i)
            o << ',';
        o << number(v[i]);
    }
    o << ']';
}
template <int N> void curveJson(std::ostream &o, const CurvePtr<N> &c) {
    if (auto s = std::dynamic_pointer_cast<const BSplineCurve<N>>(c)) {
        o << "{\"type\":\"bspline\",\"degree\":" << s->degree()
          << ",\"knots\":";
        array(o, s->knots());
        o << ",\"poles\":[";
        for (int i = 0; i < s->poleCount(); ++i) {
            if (i)
                o << ',';
            vector(o, s->poles()[i]);
        }
        o << "],\"weights\":[";
        for (int i = 0; i < s->poleCount(); ++i) {
            if (i)
                o << ',';
            o << number(s->weight(i));
        }
        o << "]}";
    } else if (auto l = std::dynamic_pointer_cast<const Line<N>>(c)) {
        o << "{\"type\":\"line\",\"origin\":";
        vector(o, l->origin());
        o << ",\"direction\":";
        vector(o, l->direction());
        o << '}';
    } else if (auto t = std::dynamic_pointer_cast<const TrimmedCurve<N>>(c)) {
        o << "{\"type\":\"trimmed\",\"basis\":";
        curveJson(o, t->basis());
        o << '}';
    } else
        throw std::runtime_error("test export: unsupported curve wrapper");
}
void surfaceJson(std::ostream &o, const BSplineSurface &s) {
    if (s.basis())
        throw std::runtime_error(
            "test export: instance would lose placement rounding");
    o << "{\"uDegree\":" << s.uDegree() << ",\"vDegree\":" << s.vDegree()
      << ",\"uKnots\":";
    array(o, s.uKnots());
    o << ",\"vKnots\":";
    array(o, s.vKnots());
    o << ",\"uPoleCount\":" << s.uPoleCount()
      << ",\"vPoleCount\":" << s.vPoleCount() << ",\"poles\":[";
    for (int i = 0; i < s.uPoleCount(); ++i)
        for (int j = 0; j < s.vPoleCount(); ++j) {
            if (i || j)
                o << ',';
            vector(o, s.pole(i, j));
        }
    o << "],\"weights\":[";
    for (int i = 0; i < s.uPoleCount(); ++i)
        for (int j = 0; j < s.vPoleCount(); ++j) {
            if (i || j)
                o << ',';
            o << number(s.weight(i, j));
        }
    o << "]}";
}
void recordsJson(std::ostream &o, const Body &body,
                 const BodyHistory *history) {
    o << '[';
    bool sep = false;
    for (auto f : body.faces())
        for (auto loop : body.face(f).loops)
            for (auto fid : body.loopFins(loop)) {
                const auto &fin = body.fin(fid);
                const auto &e = body.edge(fin.edge);
                auto s = std::dynamic_pointer_cast<const BSplineSurface>(
                    body.face(f).surface);
                if (!s || !fin.pcurve)
                    throw std::runtime_error(
                        "test export: missing tensor or pcurve");
                if (sep)
                    o << ',';
                sep = true;
                o << "{\"face\":" << f.index << ",\"fin\":" << fid.index
                  << ",\"edge\":" << fin.edge.index << ",\"range\":["
                  << number(e.range.lo) << ',' << number(e.range.hi)
                  << "],\"budget\":"
                  << number(e.tolerance + fin.pcurveTolerance +
                            linearResolution())
                  << ",\"edgeTolerance\":" << number(e.tolerance)
                  << ",\"finTolerance\":" << number(fin.pcurveTolerance)
                  << ",\"resolution\":" << number(linearResolution())
                  << ",\"curve\":";
                curveJson(o, e.curve);
                o << ",\"pcurve\":";
                curveJson(o, fin.pcurve);
                o << ",\"surface\":";
                surfaceJson(o, *s);
                o << ",\"faceOrigins\":[";
                bool hsep = false;
                if (history)
                    for (auto h : history->origins(EntityKind::Face, f.index)) {
                        if (hsep)
                            o << ',';
                        hsep = true;
                        o << "{\"operand\":" << h.operand
                          << ",\"kind\":" << int(h.kind)
                          << ",\"index\":" << h.index
                          << ",\"relation\":" << int(h.relation) << '}';
                    }
                o << "]}";
            }
    o << ']';
}
void exportCase(const std::string &name, const Body &source, const Body &result,
                const BodyHistory *history) {
    const char *dir = std::getenv("FK_SHEET_JOIN_EXPORT_DIR");
    if (!dir)
        return;
    std::ofstream o(std::string(dir) + "/" + name + ".json");
    o << "{\"case\":\"" << name << "\",\"actual\":";
    recordsJson(o, result, history);
    o << ",\"source\":";
    recordsJson(o, source, nullptr);
    o << "}\n";
    if (!o)
        throw std::runtime_error("test export write failed");
}
Body variant(const Body &source, double scale, bool pose, bool rational,
             bool sense, bool canonical) {
    // Rebuild from original tensor poles, retaining source knot ranges exactly.
    std::vector<Vec3> points;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    const Frame3 frame =
        pose ? Frame3(Vec3(2, -3, 5), Vec3(.2, .3, 1), Vec3(1, 0, 0))
             : Frame3();
    auto place = [&](Vec3 p) { return frame.toGlobal(scale * p); };
    std::vector<SurfacePtr> surfaces;
    for (auto f : source.faces()) {
        auto s = std::dynamic_pointer_cast<const BSplineSurface>(
            source.face(f).surface);
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < s->uPoleCount(); ++i)
            for (int j = 0; j < s->vPoleCount(); ++j) {
                poles.push_back(place(s->pole(i, j)));
                if (rational)
                    weights.push_back(1 + .1 * i);
            }
        surfaces.push_back(std::make_shared<BSplineSurface>(
            s->uDegree(), s->vDegree(), s->uKnots(), s->vKnots(),
            s->uPoleCount(), s->vPoleCount(), poles, weights));
    }
    auto a = surfaces[0], b = surfaces[1];
    points = {a->point(0, 0), a->point(1, 0), a->point(1, 1),
              a->point(0, 1), b->point(1, 0), b->point(1, 1)};
    edges = {{0, 1, a->vIso(0), {0, 1}, 0}, {1, 2, a->uIso(1), {0, 1}, 0},
             {3, 2, a->vIso(1), {0, 1}, 0}, {0, 3, a->uIso(0), {0, 1}, 0},
             {1, 4, b->vIso(0), {0, 1}, 0}, {4, 5, b->uIso(1), {0, 1}, 0},
             {2, 5, b->vIso(1), {0, 1}, 0}};
    Body::BuildFace fa, fb;
    fa.surface = a;
    fb.surface = b;
    fa.loops.push_back({{0, true, nullptr, 0},
                        {1, true, nullptr, 0},
                        {2, false, nullptr, 0},
                        {3, false, nullptr, 0}});
    fb.loops.push_back({{4, true, nullptr, 0},
                        {5, true, nullptr, 0},
                        {6, false, nullptr, 0},
                        {1, false, nullptr, 0}});
    faces = {fa, fb};
    if (canonical) {
        for (auto &e : edges) {
            std::swap(e.start, e.end);
            e.curve = reversedCurve<3>(e.curve);
            e.range = {-1, 0};
        }
        for (auto &f : faces)
            for (auto &l : f.loops)
                for (auto &fin : l)
                    fin.sense = !fin.sense;
    }
    if (!sense) {
        for (auto &f : faces) {
            f.sense = false;
            for (auto &l : f.loops) {
                std::reverse(l.begin(), l.end());
                for (auto &fin : l)
                    fin.sense = !fin.sense;
            }
        }
    }
    Body out = Body::buildSheet(points, edges, faces);
    computePCurves(out);
    return out;
}
void positive(std::string name, bool tangent = false, double scale = 1,
              bool pose = false, bool rational = false, bool sense = true,
              bool canonical = false, bool reverseOrder = false,
              bool lower = false, bool trimmed = false) {
    Body source =
        variant(trimmed ? aliasFixture(0., 1.12345) : fixture(tangent), scale,
                pose, rational, sense, canonical);
    FK_CHECK(checkBody(source).empty());
    const std::string before = writeBodyBinary(source);
    auto ids = source.edges();
    std::vector<EdgeId> selected = lower ? std::vector<EdgeId>{ids[0], ids[4]}
                                         : std::vector<EdgeId>{ids[2], ids[6]};
    if (reverseOrder)
        std::reverse(selected.begin(), selected.end());
    BodyHistory h;
    Body result = extendSheet(source, selected, .25 * scale, false, &h);
    FK_CHECK(result.isSheet());
    FK_CHECK(checkBody(result).empty());
    FK_CHECK(writeBodyBinary(source) == before);
    FK_CHECK(h.operandCount() == 1);
    for (auto f : result.faces())
        FK_CHECK(!h.origins(EntityKind::Face, f.index).empty());
    // Original parameter rectangle is preserved on each corresponding support.
    for (size_t i = 0; i < source.faces().size(); ++i) {
        const auto &s = *source.face(source.faces()[i]).surface;
        double best = 1e100;
        for (auto f : result.faces()) {
            double worst = 0;
            for (int u = 0; u <= 4; ++u)
                for (int v = 0; v <= 4; ++v)
                    worst =
                        std::max(worst, distance(s.point(.25 * u, .25 * v),
                                                 result.face(f).surface->point(
                                                     .25 * u, .25 * v)));
            best = std::min(best, worst);
        }
        FK_CHECK(best < 1e-10 * std::max(scale, 1.));
    }
    // The tangent path publishes its preassigned cap on the actual post-unify
    // seam.
    if (tangent) {
        Box box;
        for (auto v : source.vertices())
            box.add(source.vertex(v).point);
        for (auto f : source.faces())
            box.add(faceBox(source, f));
        const double cap = std::max(1e-7, 1e-9 * std::max(box.diagonal(), 1.));
        int verified = 0;
        for (auto e : result.edges()) {
            const auto &edge = result.edge(e);
            if (!edge.forward.valid() || !edge.backward.valid())
                continue;
            for (auto fid : {edge.forward, edge.backward}) {
                const auto &fin = result.fin(fid);
                Vec2 middle =
                    fin.pcurve->point(.5 * (edge.range.lo + edge.range.hi));
                if (middle.y() >= 0 && middle.y() <= 1)
                    continue;
                ++verified;
                auto q = std::dynamic_pointer_cast<const BSplineCurve<2>>(
                    fin.pcurve);
                FK_CHECK(q && q->degree() == 1);
                FK_CHECK_NEAR(edge.tolerance, cap, 0);
                FK_CHECK_NEAR(fin.pcurveTolerance, cap, 0);
                const auto &surface = *result.face(result.finFace(fid)).surface;
                for (int k = 0; k <= 100; ++k) {
                    double t = edge.range.lo + edge.range.length() * k / 100;
                    Vec2 uv = fin.pcurve->point(t);
                    FK_CHECK(distance(edge.curve->point(t),
                                      surface.point(uv.x(), uv.y())) <= cap);
                }
            }
        }
        FK_CHECK(verified >= 2);
    }
    const Body restored = readBodyBinary(writeBodyBinary(result));
    FK_CHECK(checkBody(restored).empty());
    FK_CHECK(restored.edges().size() == result.edges().size());
    FK_CHECK(restored.faces().size() == result.faces().size());
    for (auto e : result.edges()) {
        FK_CHECK_NEAR(restored.edge(e).range.lo, result.edge(e).range.lo, 0);
        FK_CHECK_NEAR(restored.edge(e).range.hi, result.edge(e).range.hi, 0);
        FK_CHECK_NEAR(restored.edge(e).tolerance, result.edge(e).tolerance, 0);
    }
    exportCase(name, source, result, &h);
}
void rejection(std::string name, double center, bool growingGap,
               double scale = 1., bool pose = false,
               bool reverseOrder = false) {
    Body source =
        variant(aliasFixture(growingGap ? 1e-4 : .001, center, growingGap),
                scale, pose, false, true, false);
    FK_CHECK(checkBody(source).empty());
    const std::string before = writeBodyBinary(source);
    auto ids = source.edges();
    Body sentinel = makePlaneSheet(Frame3(), .5);
    const std::string prior = writeBodyBinary(sentinel);
    BodyHistory h = makeHistory({&source}, source);
    bool rejected = false;
    std::vector<EdgeId> chosen = {ids[2], ids[6]};
    if (reverseOrder)
        std::reverse(chosen.begin(), chosen.end());
    try {
        sentinel = extendSheet(source, chosen, .25 * scale, false, &h);
        exportCase(name, source, sentinel, &h);
    } catch (const std::domain_error &) {
        rejected = true;
    }
    FK_CHECK(rejected);
    FK_CHECK(writeBodyBinary(source) == before);
    if (rejected) {
        FK_CHECK(writeBodyBinary(sentinel) == prior);
        FK_CHECK(h.operandCount() == 0);
    }
}
} // namespace
FK_TEST(SheetJoinImplicitPolynomial) { positive("implicit-polynomial"); }
FK_TEST(SheetJoinTangentPolynomial) { positive("tangent-polynomial", true); }
FK_TEST(SheetJoinRigidPose) { positive("rigid-pose", false, 1, true); }
FK_TEST(SheetJoinSmallScale) { positive("small-scale", false, .01); }
FK_TEST(SheetJoinLargeScale) { positive("large-scale", false, 1000); }
FK_TEST(SheetJoinRational) { positive("rational", false, 1, false, true); }
FK_TEST(SheetJoinReverseSelection) {
    positive("reverse-selection", false, 1, false, false, true, false, true);
}
FK_TEST(SheetJoinFaceSense) {
    positive("face-sense", false, 1, false, false, false);
}
FK_TEST(SheetJoinCanonicalReversal) {
    positive("canonical-reversal", false, 1, false, false, true, true);
}
FK_TEST(SheetJoinLowerBoundary) {
    positive("lower-boundary", false, 1, false, false, true, false, false,
             true);
}
FK_TEST(SheetJoinTangentRigidPose) {
    positive("tangent-rigid-pose", true, 1, true);
}
FK_TEST(SheetJoinTangentSmallScale) {
    positive("tangent-small-scale", true, .01);
}
FK_TEST(SheetJoinTangentLargeScale) {
    positive("tangent-large-scale", true, 1000);
}
FK_TEST(SheetJoinTangentRational) {
    positive("tangent-rational", true, 1, false, true);
}
FK_TEST(SheetJoinTangentReverseSelection) {
    positive("tangent-reverse-selection", true, 1, false, false, true, false,
             true);
}
FK_TEST(SheetJoinTangentFaceSense) {
    positive("tangent-face-sense", true, 1, false, false, false);
}
FK_TEST(SheetJoinTangentCanonicalReversal) {
    positive("tangent-canonical-reversal", true, 1, false, false, true, true);
}
FK_TEST(SheetJoinTangentLowerBoundary) {
    positive("tangent-lower-boundary", true, 1, false, false, true, false,
             false, true);
}
FK_TEST(SheetJoinHiddenNarrowKnots) {
    rejection("hidden-narrow-knots", 1.12345, false);
}
FK_TEST(SheetJoinTangentGapBudget) {
    rejection("tangent-gap-budget", 1.12345, true);
}
FK_TEST(SheetJoinTangentTrimmedSupport) {
    positive("tangent-trimmed-support", true, 1, false, false, true, false,
             false, false, true);
}
FK_TEST(SheetJoinHiddenNarrowPhases) {
    for (double center : {1.111, 1.137})
        rejection("hidden-phase-" + std::to_string(center), center, false);
}
FK_TEST(SheetJoinHiddenNarrowPose) {
    rejection("hidden-pose", 1.12345, false, 1, true);
}
FK_TEST(SheetJoinHiddenNarrowSmallScale) {
    rejection("hidden-small-scale", 1.12345, false, .01);
}
FK_TEST(SheetJoinHiddenNarrowLargeScale) {
    rejection("hidden-large-scale", 1.12345, false, 1000);
}
FK_TEST(SheetJoinHiddenNarrowReverseSelection) {
    rejection("hidden-reverse-selection", 1.12345, false, 1, false, true);
}
FK_TEST(SheetJoinTangentGapPose) {
    rejection("tangent-gap-pose", 1.12345, true, 1, true);
}
FK_TEST(SheetJoinCancellationAtomic) {
    Body source = fixture();
    const std::string before = writeBodyBinary(source);
    auto ids = source.edges();
    Body output = makePlaneSheet(Frame3(), .5);
    const auto sentinel = writeBodyBinary(output);
    BodyHistory h = makeHistory({&source}, source);
    auto token = std::make_shared<std::atomic<bool>>(true);
    bool cancelled = false;
    {
        CancellationScope scope(token);
        try {
            output = extendSheet(source, {ids[2], ids[6]}, .25, false, &h);
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
    }
    FK_CHECK(cancelled);
    FK_CHECK(h.operandCount() == 0);
    FK_CHECK(writeBodyBinary(source) == before);
    FK_CHECK(writeBodyBinary(output) == sentinel);
}

FK_TEST(SheetJoinInvalidLengthAtomic) {
    for (double length : {0., -1., std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()}) {
        Body source = fixture();
        const auto original = writeBodyBinary(source);
        auto ids = source.edges();
        Body out = makePlaneSheet(Frame3(), .5);
        auto old = writeBodyBinary(out);
        BodyHistory h = makeHistory({&source}, source);
        bool rejected = false;
        try {
            out = extendSheet(source, {ids[2], ids[6]}, length, false, &h);
        } catch (const std::exception &) {
            rejected = true;
        }
        FK_CHECK(rejected);
        FK_CHECK(h.operandCount() == 0);
        FK_CHECK(writeBodyBinary(out) == old);
        FK_CHECK(writeBodyBinary(source) == original);
    }
}
