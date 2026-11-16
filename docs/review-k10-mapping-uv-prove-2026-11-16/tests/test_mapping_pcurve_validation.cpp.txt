#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_curve_fit_bounds.h"
#include "fk_curve_ops.h"
#include "fk_mapping_fit.h"
#include "fk_quilt_mapping.h"
#include "fk_sheet.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ForgeCad::Kernel;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Options {
    bool rational = false, trim = false, reverse = false, pose = false,
         transformed = false;
    double scale = 1, height = 0, normalOffset = 0, farUV = 0;
    Interval range{0, 1};
};
Frame3 sourceFrame(const Options &o) {
    return o.pose ? Frame3({3, -4, 2}, {1, 2, 3}, {2, -1, .4}) : Frame3();
}
Frame3 targetFrame(const Options &o) {
    return o.pose ? Frame3({-2, 1, 4}, {-1, 3, 2}, {1, 2, -.2}) : Frame3();
}
Body source(const Options &o) {
    const auto frame = sourceFrame(o);
    const auto raised =
        Frame3(frame.toGlobal({0, 0, o.height}), frame.zDir(), frame.xDir());
    std::vector<Vec3> local{{0, 0, o.height},
                            {o.scale, 0, o.height},
                            {o.scale, o.scale, o.height},
                            {0, o.scale, o.height}};
    std::vector<Vec3> points;
    for (auto p : local)
        points.push_back(frame.toGlobal(p));
    std::vector<Body::BuildEdge> edges;
    Body::BuildFace face;
    face.surface = std::make_shared<Plane>(raised);
    std::vector<Body::BuildFin> fins;
    const double x = .1003, y = .1013;
    std::vector<double> knots{0, 0, 0, 0, x, x, x, y, y, y, 1, 1, 1, 1};
    for (auto &k : knots)
        k = o.range.lo + k * o.range.length();
    std::vector<double> q{0,
                          x / 3,
                          2 * x / 3,
                          x,
                          x + (y - x) / 3 + 1e-5,
                          y - (y - x) / 3 + 1e-5,
                          y,
                          y + (1 - y) / 3,
                          y + 2 * (1 - y) / 3,
                          1};
    std::vector<double> w(q.size(), 1);
    if (o.rational) {
        w[4] = 1.2;
        w[5] = .8;
    }
    for (int i = 0; i < 4; ++i) {
        auto a = local[i], b = local[(i + 1) % 4];
        std::vector<Vec3> p;
        std::vector<Vec2> uv;
        for (double t : q) {
            auto v = a + t * (b - a);
            p.push_back(o.transformed ? v : frame.toGlobal(v));
            uv.push_back({v.x(), v.y()});
        }
        if (o.reverse) {
            std::reverse(p.begin(), p.end());
            std::reverse(uv.begin(), uv.end());
        }
        auto weights = w;
        auto ks = knots;
        if (o.reverse) {
            std::reverse(weights.begin(), weights.end());
            std::reverse(ks.begin(), ks.end());
            for (auto &k : ks)
                k = o.range.lo + o.range.hi - k;
        }
        CurvePtr<3> c = std::make_shared<BSplineCurve<3>>(3, ks, p, weights);
        CurvePtr<2> pc = std::make_shared<BSplineCurve<2>>(3, ks, uv, weights);
        if (o.transformed)
            c = std::make_shared<TransformedCurve>(
                c, Transform3::fromFrame(frame));
        if (o.trim) {
            c = std::make_shared<TrimmedCurve<3>>(c, o.range.lo, o.range.hi);
            pc = std::make_shared<TrimmedCurve<2>>(pc, o.range.lo, o.range.hi);
        }
        edges.push_back({o.reverse ? (i + 1) % 4 : i,
                         o.reverse ? i : (i + 1) % 4, c, o.range, 0});
        fins.push_back({i, !o.reverse, pc, 0});
    }
    face.loops = {fins};
    return Body::buildSheet(points, edges, {face});
}
void hex(std::ostream &s, double x) {
    s << '"' << std::hexfloat << x << std::defaultfloat << '"';
}
template <int N> void vec(std::ostream &s, const Vec<N> &v) {
    s << '[';
    for (int i = 0; i < N; ++i) {
        if (i)
            s << ',';
        hex(s, v[i]);
    }
    s << ']';
}
void numbers(std::ostream &s, const std::vector<double> &a) {
    s << '[';
    bool first = true;
    for (double v : a) {
        if (!first)
            s << ',';
        first = false;
        hex(s, v);
    }
    s << ']';
}
template <int N> void curve(std::ostream &s, const CurvePtr<N> &c) {
    s << '{';
    if (c->type() == CurveType::Line) {
        auto &l = static_cast<const Line<N> &>(*c);
        s << "\"type\":\"line\",\"origin\":";
        vec(s, l.origin());
        s << ",\"direction\":";
        vec(s, l.direction());
    } else if (c->type() == CurveType::BSpline) {
        auto &b = static_cast<const BSplineCurve<N> &>(*c);
        s << "\"type\":\"bspline\",\"degree\":" << b.degree() << ",\"knots\":";
        numbers(s, b.knots());
        s << ",\"weights\":";
        numbers(s, b.weights());
        s << ",\"poles\":[";
        bool first = true;
        for (auto p : b.poles()) {
            if (!first)
                s << ',';
            first = false;
            vec(s, p);
        }
        s << ']';
    } else if (c->type() == CurveType::Trimmed) {
        auto &t = static_cast<const TrimmedCurve<N> &>(*c);
        s << "\"type\":\"trimmed\",\"range\":[";
        hex(s, t.domain().lo);
        s << ',';
        hex(s, t.domain().hi);
        s << "],\"basis\":";
        curve(s, t.basis());
    } else if constexpr (N == 3) {
        if (c->type() == CurveType::Transformed) {
            auto &t = static_cast<const TransformedCurve &>(*c);
            s << "\"type\":\"transformed\",\"matrix\":[";
            for (int i = 0; i < 3; ++i) {
                if (i)
                    s << ',';
                s << '[';
                for (int j = 0; j < 3; ++j) {
                    if (j)
                        s << ',';
                    hex(s, t.transform().matrix(i, j));
                }
                s << ']';
            }
            s << "],\"translation\":";
            vec(s, t.transform().translationPart());
            s << ",\"basis\":";
            curve(s, t.basis());
        } else
            throw std::runtime_error("unsupported exporter curve");
    } else
        throw std::runtime_error("unsupported exporter curve");
    s << '}';
}
void fins(std::ostream &s, const Body &body) {
    s << '[';
    bool first = true;
    for (auto id : body.fins()) {
        auto &fin = body.fin(id);
        auto &edge = body.edge(fin.edge);
        auto faceId = body.finFace(id);
        auto &face = body.face(faceId);
        require(face.surface->type() == SurfaceType::Plane,
                "exporter requires final Plane");
        auto &f = static_cast<const Plane &>(*face.surface).frame();
        if (!first)
            s << ',';
        first = false;
        s << "{\"fin\":" << id.index << ",\"edge\":" << fin.edge.index
          << ",\"face\":" << faceId.index
          << ",\"sense\":" << (fin.sense ? "true" : "false") << ",\"range\":[";
        hex(s, edge.range.lo);
        s << ',';
        hex(s, edge.range.hi);
        s << "],\"budget\":";
        hex(s, fin.pcurveTolerance);
        s << ",\"plane\":{\"origin\":";
        vec(s, f.origin());
        s << ",\"x\":";
        vec(s, f.xDir());
        s << ",\"y\":";
        vec(s, f.yDir());
        s << ",\"z\":";
        vec(s, f.zDir());
        s << "},\"curve\":";
        curve(s, edge.curve);
        s << ",\"pcurve\":";
        curve(s, fin.pcurve);
        s << '}';
    }
    s << ']';
}

void exportCase(const std::string &directory, const std::string &name,
                const Body &src, const Body &out, const BodyHistory &history,
                double tolerance, double deviation) {
    if (directory.empty())
        return;
    std::ofstream s(directory + "/" + name + ".json");
    s << "{\"case\":\"" << name << "\",\"tolerance\":";
    hex(s, tolerance);
    s << ",\"deviation\":";
    hex(s, deviation);
    s << ",\"source\":";
    fins(s, src);
    s << ",\"actual\":";
    fins(s, out);
    s << ",\"face_origins\":[";
    bool first = true;
    for (auto id : out.faces()) {
        if (!first)
            s << ',';
        first = false;
        s << "{\"actual\":" << id.index << ",\"source\":[";
        bool f = true;
        for (auto x : history.origins(EntityKind::Face, id.index)) {
            if (!f)
                s << ',';
            f = false;
            s << x.index;
        }
        s << "]}";
    }
    s << "]}\n";
    std::ofstream(directory + "/" + name + ".source.body", std::ios::binary)
        << writeBodyBinary(src);
    std::ofstream(directory + "/" + name + ".actual.body", std::ios::binary)
        << writeBodyBinary(out);
}
Body targetBody(const Options &o) {
    auto to = targetFrame(o);
    auto target = makePlaneSheet(to, 3 * o.scale);
    if (o.farUV) {
        target.face(target.faces()[0]).surface = std::make_shared<Plane>(
            Frame3(to.toGlobal({-o.farUV, 0, 0}), to.zDir(), to.xDir()));
        for (auto id : target.fins()) {
            auto &f = target.fin(id);
            f.pcurve = translatedCurve<2>(f.pcurve, {o.farUV, 0});
        }
        require(checkBody(target, {true, true}).empty(),
                "invalid far-UV original target");
    }
    return target;
}
Body mapped(const Body &b, const Options &o, bool quilt, BodyHistory *h,
            double *d) {
    auto from = sourceFrame(o), to = targetFrame(o);
    SurfaceWrapOptions opt;
    opt.sourcePlane = from;
    opt.normalOffset = o.normalOffset;
    opt.tolerance = 1e-6;
    if (quilt) {
        auto target = targetBody(o);
        SurfaceQuiltMap m({{&target, target.faces()[0]}}, {o.farUV, 0});
        return m.bend(b, opt, h, d);
    }
    const auto map = [=](const Vec3 &p) {
        auto q = from.toLocal(p);
        q[2] += o.normalOffset;
        return to.toGlobal(q);
    };
    const auto image = [=](FaceId) {
        detail::MappingFaceImage im;
        im.surface = std::make_shared<Plane>(
            Frame3(to.toGlobal({0, 0, o.height + o.normalOffset}), to.zDir(),
                   to.xDir()));
        im.parameters = [=](const Vec3 &p) {
            auto q = from.toLocal(p);
            return Vec2(q.x(), q.y());
        };
        return im;
    };
    return detail::mapBodyByFunction(b, map, {}, opt.tolerance, h, d, {}, .001,
                                     3, image);
}
void run(const Options &o, bool quilt, const std::string &name,
         const std::string &dir) {
    const auto b = source(o);
    const auto before = writeBodyBinary(b);
    require(checkBody(b, {true, true}).empty(), "source checker failure");
    BodyHistory h;
    double d = -1;
    const auto out = mapped(b, o, quilt, &h, &d);
    require(writeBodyBinary(b) == before, "source mutated");
    require(checkBody(out, {true, true}).empty(), "output checker failure");
    require(out.faces().size() == b.faces().size() &&
                out.edges().size() == b.edges().size() &&
                out.vertices().size() == b.vertices().size(),
            "topology changed");
    exportCase(dir, name, b, out, h, 1e-6, d);
    // Dense witnesses check the actual mapping at the SAME source parameter.
    // They do not certify the sampled upstream 3D edge fitter globally.
    double worst = 0, ratio = 0, mapError = 0, sourceResidual = 0;
    const auto from = sourceFrame(o), to = targetFrame(o);
    const auto expectedTarget = targetBody(o);
    std::unique_ptr<SurfaceQuiltMap> expectedQuilt;
    if (quilt)
        expectedQuilt = std::make_unique<SurfaceQuiltMap>(
            std::vector<SurfaceTargetFace>{
                {&expectedTarget, expectedTarget.faces()[0]}},
            Vec2(o.farUV, 0));
    for (auto id : out.fins()) {
        const auto &f = out.fin(id);
        const auto &e = out.edge(f.edge);
        const auto &s = *out.face(out.finFace(id)).surface;
        require(e.range.lo == o.range.lo && e.range.hi == o.range.hi,
                "original parameter range lost");
        require(f.pcurveTolerance >= 0 &&
                    f.pcurveTolerance <= .8 * linearResolution(),
                "fin exceeds preassigned operation budget");
        require(h.isDeclared(EntityKind::Edge, f.edge.index) &&
                    h.isDeclared(EntityKind::Face, out.finFace(id).index),
                "missing declared genealogy");
        auto origins = h.origins(EntityKind::Edge, f.edge.index);
        require(origins.size() == 1, "ambiguous edge genealogy");
        const auto &se = b.edge(EdgeId{origins.front().index});
        auto cuts = e.curve->breakpoints(e.range);
        auto more = f.pcurve->breakpoints(e.range);
        cuts.insert(cuts.end(), more.begin(), more.end());
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        for (size_t k = 1; k < cuts.size(); ++k)
            for (int j = 0; j <= 100; ++j) {
                double t = cuts[k - 1] + (cuts[k] - cuts[k - 1]) * j / 100.;
                auto uv = f.pcurve->point(t);
                double r = distance(s.point(uv.x(), uv.y()), e.curve->point(t));
                worst = std::max(worst, r);
                ratio =
                    std::max(ratio, r / std::max(1e-300, f.pcurveTolerance));
                auto q = from.toLocal(se.curve->point(t));
                q[2] += o.normalOffset;
                mapError =
                    std::max(mapError, distance(e.curve->point(t),
                                                quilt ? expectedQuilt->fold(q)
                                                      : to.toGlobal(q)));
            }
    }
    for (auto id : out.fins()) {
        const auto &f = out.fin(id);
        const auto &e = out.edge(f.edge);
        const double bound = detail::requirePCurveOnPlane(
            e.curve, out.face(out.finFace(id)).surface, f.pcurve, e.range,
            f.pcurveTolerance);
        require(std::isfinite(bound) && bound <= f.pcurveTolerance,
                "continuous final fin check failed");
    }
    for (auto id : b.fins()) {
        const auto &f = b.fin(id);
        const auto &e = b.edge(f.edge);
        const auto &s = *b.face(b.finFace(id)).surface;
        for (int j = 0; j <= 1000; ++j) {
            double t = e.range.lo + e.range.length() * j / 1000.;
            auto uv = f.pcurve->point(t);
            sourceResidual =
                std::max(sourceResidual,
                         distance(s.point(uv.x(), uv.y()), e.curve->point(t)));
        }
    }
    std::cout << name << " source=" << sourceResidual << " residual=" << worst
              << " ratio=" << ratio << " map_error=" << mapError
              << " deviation=" << d;
    require(sourceResidual < 1e-10 * std::max(1., o.scale),
            "source construction inconsistent");
    require(ratio <= 1., "final fin exceeds its published physical tolerance");
    require(mapError <= .2 * linearResolution(),
            "mapped edge witness exceeds fit budget");
    require(std::isfinite(d) && d <= .8 * linearResolution(),
            "deviation not finite/in budget");
    auto round = readBodyBinary(writeBodyBinary(out));
    require(checkBody(round, {true, true}).empty(), "roundtrip invalid");
}
void failures() {
    Options o;
    o.rational = true;
    o.trim = true;
    auto b = source(o);
    auto before = writeBodyBinary(b);
    BodyHistory h = makeHistory({&b}, b);
    h.declareOrigins(
        EntityKind::Face, b.faces()[0].index,
        {{0, EntityKind::Face, b.faces()[0].index, HistoryRelation::Same}});
    const auto initial = h.origins(EntityKind::Face, b.faces()[0].index);
    double d = 123;
    auto token = std::make_shared<std::atomic<bool>>(true);
    for (bool quilt : {false, true}) {
        bool cancelled = false;
        try {
            CancellationScope scope(token);
            (void)mapped(b, o, quilt, &h, &d);
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
        require(cancelled && d == 123 &&
                    h.origins(EntityKind::Face, b.faces()[0].index)
                            .front()
                            .relation == initial.front().relation &&
                    writeBodyBinary(b) == before,
                "cancellation published outputs or mutated source");
    }
    for (double far : {1e5, 1e6})
        for (bool pose : {false, true}) {
            o.height = .2;
            o.normalOffset = .3;
            o.farUV = far;
            o.pose = pose;
            b = source(o);
            before = writeBodyBinary(b);
            bool rejected = false;
            try {
                (void)mapped(b, o, true, &h, &d);
            } catch (const std::domain_error &) {
                rejected = true;
            }
            require(rejected && d == 123 &&
                        h.origins(EntityKind::Face, b.faces()[0].index)
                                .front()
                                .relation == initial.front().relation &&
                        writeBodyBinary(b) == before,
                    "unresolved large UV accepted or changed outputs");
        }
}
} // namespace
int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : "";
    int failed = 0, passed = 0;
    const auto one = [&](Options o, bool q, const std::string &name) {
        try {
            run(o, q, name, dir);
            std::cout << " PASS\n";
            ++passed;
        } catch (const std::exception &e) {
            std::cout << name << " FAIL " << e.what() << '\n';
            ++failed;
        }
    };
    for (bool quilt : {false, true}) {
        const std::string p = quilt ? "quilt_" : "mapping_";
        Options o;
        one(o, quilt, p + "polynomial");
        o.rational = true;
        one(o, quilt, p + "rational");
        o.trim = true;
        o.range = {-2, 5};
        one(o, quilt, p + "trim_range");
        o.reverse = true;
        one(o, quilt, p + "reverse");
        o.pose = true;
        o.transformed = true;
        one(o, quilt, p + "pose_wrapper");
        o.scale = 1000;
        one(o, quilt, p + "scale1000");
        o.scale = .01;
        one(o, quilt, p + "scale001");
        o = Options{};
        o.rational = true;
        o.height = .2;
        one(o, quilt, p + "height");
        o.normalOffset = .3;
        one(o, quilt, p + "normal_offset");
    }
    for (double far : {1e4})
        for (bool pose : {false, true}) {
            Options o;
            o.rational = true;
            o.trim = true;
            o.height = .2;
            o.normalOffset = .3;
            o.farUV = far;
            o.pose = pose;
            one(o, true,
                "quilt_faruv" + std::to_string(int(far)) +
                    (pose ? "_pose" : "_identity"));
        }
    try {
        failures();
        std::cout << "atomic_failures PASS\n";
        ++passed;
    } catch (const std::exception &e) {
        std::cout << "atomic_failures FAIL " << e.what() << '\n';
        ++failed;
    }
    std::cout << "passed=" << passed << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}
