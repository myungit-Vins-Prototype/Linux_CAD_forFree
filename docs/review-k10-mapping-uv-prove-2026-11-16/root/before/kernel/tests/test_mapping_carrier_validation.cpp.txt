#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include "fk_classify.h"
#include "fk_mapping_carrier.h"
#include "fk_mapping_fit.h"
#include "fk_surface_algo.h"
#include "fk_surface_mapping.h"
#include "fk_test.h"
#include <limits>
#include <sstream>
using namespace ForgeCad::Kernel;
namespace {
CurvePtr<2> affineUV(Vec2 a, Vec2 b, Interval range) {
    return std::make_shared<BSplineCurve<2>>(1, std::vector<double>{range.lo, range.lo, range.hi, range.hi},
                                             std::vector<Vec2>{a, b});
}
Body planeSource(const Frame3 &frame, double scale, bool hole = false, bool trimmedEdges = false) {
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    Body::BuildFace face;
    face.surface = std::make_shared<Plane>(frame);
    const auto loop = [&](const std::vector<Vec2> &points) {
        const int first = int(vertices.size());
        std::vector<Body::BuildFin> fins;
        for (const auto &p : points)
            vertices.push_back(frame.toGlobal(Vec3(p.x(), p.y(), 0)));
        for (int i = 0; i < int(points.size()); ++i) {
            const auto a = points[i], b = points[(i + 1) % points.size()];
            const double length = distance(a, b);
            CurvePtr<3> curve = std::make_shared<Line<3>>(
                vertices[first + i], vertices[first + (i + 1) % points.size()] - vertices[first + i]);
            if (trimmedEdges)
                curve = std::make_shared<TrimmedCurve<3>>(curve, 0, length);
            fins.push_back({int(edges.size()), true, affineUV(a, b, {0, length}), 1e-9});
            edges.push_back({first + i, first + (i + 1) % int(points.size()), curve, {0, length}, 1e-9});
        }
        face.loops.push_back(fins);
    };
    loop({{0, 0}, {scale, 0}, {scale, scale}, {0, scale}});
    if (hole)
        loop({{.55 * scale, .35 * scale},
              {.55 * scale, .65 * scale},
              {.8 * scale, .65 * scale},
              {.8 * scale, .35 * scale}});
    return Body::buildSheet(vertices, edges, {face});
}
CurvePtr<3> narrowProfile(double scale, bool rational, const Transform3 &placement) {
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
        poles.push_back(placement.applyToPoint(
            scale * Vec3((rational ? x + .5 * x2 : x) / w, 0, (i == 9 ? .0032 : 0) / w)));
        if (rational)
            weights.push_back(w);
    }
    return std::make_shared<BSplineCurve<3>>(degree, knots, poles, weights);
}
Body targetSheet(SurfacePtr surface, double scale) {
    const std::vector<Vec3> vertices{surface->point(0, 0), surface->point(1, 0), surface->point(1, scale),
                                     surface->point(0, scale)};
    const std::vector<Body::BuildEdge> edges{{0, 1, surface->vIso(0), {0, 1}, 1e-9},
                                             {1, 2, surface->uIso(1), {0, scale}, 1e-9},
                                             {3, 2, surface->vIso(scale), {0, 1}, 1e-9},
                                             {0, 3, surface->uIso(0), {0, scale}, 1e-9}};
    Body::BuildFace face;
    face.surface = surface;
    face.loops = {{{0, true, affineUV({0, 0}, {1, 0}, {0, 1}), 1e-9},
                   {1, true, affineUV({1, 0}, {1, scale}, {0, scale}), 1e-9},
                   {2, false, affineUV({0, scale}, {1, scale}, {0, 1}), 1e-9},
                   {3, false, affineUV({0, 0}, {0, scale}, {0, scale}), 1e-9}}};
    return Body::buildSheet(vertices, edges, {face});
}
Body targetFixture(double scale, bool rational, bool bspline, const Transform3 &placement) {
    const auto profile = narrowProfile(scale, rational, placement);
    const auto direction = placement.applyToVector(Vec3(0, 1, 0));
    SurfacePtr surface = std::make_shared<ExtrusionSurface>(profile, direction);
    if (bspline) {
        const auto &c = static_cast<const BSplineCurve<3> &>(*profile);
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < c.poleCount(); ++i) {
            poles.push_back(c.poles()[i]);
            poles.push_back(c.poles()[i] + scale * direction);
            weights.push_back(c.weight(i));
            weights.push_back(c.weight(i));
        }
        surface = std::make_shared<BSplineSurface>(6, 1, c.knots(), std::vector<double>{0, 0, scale, scale},
                                                   c.poleCount(), 2, poles, weights);
    }
    return targetSheet(surface, scale);
}
Vec3 expectedPoint(const Surface &target, const SurfaceWrapOptions &options, const Vec3 &source) {
    Vec3 jet[4];
    target.evaluate(options.anchor.x(), options.anchor.y(), 1, jet);
    const double su = norm(jet[2]), sv = norm(jet[1]);
    const auto p = options.sourcePlane.toLocal(source);
    const double c = std::cos(options.angle), s = std::sin(options.angle);
    const double u = options.anchor.x() + (c * p.x() - s * p.y() + options.shift.x()) / su,
                 v = options.anchor.y() + (s * p.x() + c * p.y() + options.shift.y()) / sv;
    return target.point(u, v) + (p.z() + options.normalOffset) * target.normal(u, v);
}
std::string historyStamp(const BodyHistory &history) {
    std::ostringstream out;
    out << history.operandCount();
    for (auto kind : {EntityKind::Vertex, EntityKind::Edge, EntityKind::Face}) {
        out << ':' << history.resultSize(kind);
        for (int i = 0; i < history.resultSize(kind); ++i) {
            out << '/' << history.isDeclared(kind, i);
            for (auto origin : history.origins(kind, i))
                out << ',' << origin.operand << ',' << int(origin.kind) << ',' << origin.index << ','
                    << int(origin.relation);
        }
    }
    return out.str();
}
BodyHistory sentinelHistory() {
    BodyHistory h(1, {2}, {3}, {4}, 2, 3, 4);
    h.declareOrigins(EntityKind::Face, 0, {{0, EntityKind::Face, 1, HistoryRelation::Generated}});
    return h;
}
void checkMapped(const Body &source, const Body &targetBody, const SurfaceWrapOptions &options,
                 bool roundtrip = false) {
    const auto &target = *targetBody.face(targetBody.faces().front()).surface;
    const auto sourceBytes = writeBodyBinary(source), targetBytes = writeBodyBinary(targetBody);
    Vec3 jet[4];
    target.evaluate(options.anchor.x(), options.anchor.y(), 1, jet);
    // The supported route must actually engage; a legacy sampled fit is not
    // sufficient evidence for the new carrier-preserving branch.
    const auto direct = detail::wrapPlanarBodyOnCarrier(source, target, options, norm(jet[2]), norm(jet[1]),
                                                        nullptr, nullptr);
    FK_CHECK(bool(direct));
    BodyHistory history;
    double deviation = -123;
    const auto mapped = wrapBodyOnSurface(source, target, options, &history, &deviation);
    FK_CHECK(checkBody(mapped, {true, true}).empty());
    FK_CHECK(std::isfinite(deviation) && deviation >= 0 && deviation <= options.tolerance);
    FK_CHECK(history.operandCount() == 1);
    for (auto face : mapped.faces())
        FK_CHECK(history.isDeclared(EntityKind::Face, face.index));
    const auto verify = [&](const Body &body) {
        FK_CHECK(body.faces().size() == 1 && body.face(body.faces().front()).loops.size() ==
                                                 source.face(source.faces().front()).loops.size());
        const auto carrier = body.face(body.faces().front()).surface;
        double targetError = 0, edgeError = 0;
        // Same-parameter agreement is required by ORIGINAL-carrier retention.
        // Dense checks are independent regressions, not the certificate.
        for (int i = 0; i <= 10000; ++i) {
            const double u = i / 10000.;
            targetError = std::max(targetError, distance(carrier->point(u, .5 * norm(jet[2])),
                                                         target.point(u, .5 * norm(jet[2]))));
        }
        FK_CHECK_NEAR(targetError, 0., options.tolerance);
        const auto peak = target.point(.1005, .5 * norm(jet[2]));
        FK_CHECK(projectPoint(*carrier, peak).distance <= options.tolerance);
        for (auto id : body.edges()) {
            const auto &edge = body.edge(id);
            const auto &original = source.edge(id);
            for (int i = 0; i <= 1000; ++i) {
                const double t = edge.range.lo + edge.range.length() * i / 1000.;
                edgeError =
                    std::max(edgeError, distance(edge.curve->point(t),
                                                 expectedPoint(target, options, original.curve->point(t))));
            }
        }
        FK_CHECK_NEAR(edgeError, 0., options.tolerance);
    };
    verify(mapped);
    if (roundtrip)
        verify(readBodyBinary(writeBodyBinary(mapped)));
    FK_CHECK(writeBodyBinary(source) == sourceBytes && writeBodyBinary(targetBody) == targetBytes);
}
} // namespace

FK_TEST(MappingCarrierHiddenTargetsPolyRationalPoseScale) {
    for (double scale : {.01, 1., 1000.})
        for (bool rational : {false, true})
            for (bool bspline : {false, true}) {
                const auto placement = Transform3::translation(scale * Vec3(3, 2, -1)) *
                                       Transform3::rotation({}, Vec3(1, 2, 3), .57);
                const Frame3 sourceFrame(scale * Vec3(-2, 1, .3), Vec3(.2, -.3, 1), Vec3(1, 0, 0));
                const Frame3 interiorFrame(sourceFrame.toGlobal({.05 * scale, .05 * scale, 0}),
                                           sourceFrame.zDir(), sourceFrame.xDir());
                const auto source = planeSource(interiorFrame, .9 * scale);
                const auto target = targetFixture(scale, rational, bspline, placement);
                SurfaceWrapOptions options;
                options.sourcePlane = sourceFrame;
                options.tolerance = 1e-6 * scale;
                try {
                    checkMapped(source, target, options);
                } catch (const std::exception &e) {
                    throw std::runtime_error(std::string(e.what()) + " scale=" + std::to_string(scale) +
                                             " rational=" + std::to_string(rational) +
                                             " bspline=" + std::to_string(bspline));
                }
            }
}
FK_TEST(MappingCarrierSourceFrameQuarterTurnShiftHolesAndRoundtrip) {
    const Frame3 frame(Vec3(2, -3, 1), Vec3(.2, .3, 1), Vec3(1, 0, 0));
    for (bool rational : {false, true})
        for (bool hole : {false, true}) {
            const auto source = planeSource(frame, .8, hole, true);
            const auto target = targetFixture(1, rational, false, Transform3());
            SurfaceWrapOptions options;
            options.sourcePlane = frame;
            options.angle = kHalfPi;
            options.shift = {.9, .1};
            options.tolerance = 1e-6;
            checkMapped(source, target, options, hole && !rational);
            if (hole) {
                const auto mapped =
                    wrapBodyOnSurface(source, *target.face(target.faces().front()).surface, options);
                const FaceClassifier classifier(mapped, mapped.faces().front(), 1e-7);
                const auto &carrier = *target.face(target.faces().front()).surface;
                FK_CHECK(classifier.classify(expectedPoint(carrier, options, frame.toGlobal({.56, .4, 0}))) ==
                         PointLocation::Outside);
                FK_CHECK(classifier.classify(expectedPoint(carrier, options, frame.toGlobal({.08, .4, 0}))) ==
                         PointLocation::Inside);
            }
        }
}

FK_TEST(MappingCarrierDeclaredLegacyDiagonalPeriodicAndOffset) {
    const auto source = planeSource(Frame3(), 1);
    std::vector<Vec3> poles;
    for (double u : {-2., 2.})
        for (double v : {-2., 2.})
            poles.emplace_back(u, v, 0);
    const BSplineSurface plane(1, 1, {-2, -2, 2, 2}, {-2, -2, 2, 2}, 2, 2, poles);
    SurfaceWrapOptions diagonal;
    diagonal.angle = .37;
    diagonal.tolerance = 1e-6;
    auto history = sentinelHistory();
    const auto stamp = historyStamp(history);
    double deviation = -123;
    FK_CHECK(!detail::wrapPlanarBodyOnCarrier(source, plane, diagonal, 1, 1, &history, &deviation));
    FK_CHECK(historyStamp(history) == stamp && deviation == -123);
    const auto mapped = wrapBodyOnSurface(source, plane, diagonal, &history, &deviation);
    FK_CHECK(checkBody(mapped, {true, true}).empty() && deviation <= diagonal.tolerance);
    for (auto edge : mapped.edges())
        for (int i = 0; i <= 100; ++i) {
            const auto &e = mapped.edge(edge);
            const double t = e.range.lo + e.range.length() * i / 100.;
            FK_CHECK(std::abs(e.curve->point(t).z()) <= diagonal.tolerance);
        }
    const CylindricalSurface cylinder(Frame3(), 2);
    SurfaceWrapOptions offset;
    offset.anchor = {kTwoPi - .2, 0};
    offset.normalOffset = .02;
    offset.tolerance = 1e-6;
    FK_CHECK(!detail::wrapPlanarBodyOnCarrier(source, cylinder, offset, 2, 1, nullptr, nullptr));
    const auto periodic = wrapBodyOnSurface(source, cylinder, offset, nullptr, &deviation);
    FK_CHECK(checkBody(periodic, {true, true}).empty() && deviation <= offset.tolerance);
    for (auto edge : periodic.edges())
        for (int i = 0; i <= 100; ++i) {
            const auto &e = periodic.edge(edge);
            const double t = e.range.lo + e.range.length() * i / 100.;
            const auto p = e.curve->point(t);
            FK_CHECK(std::abs(std::hypot(p.x(), p.y()) - 2.02) <= offset.tolerance);
        }
}

FK_TEST(MappingCarrierFailureAndCallbackCancellationAreAtomic) {
    const auto source = planeSource(Frame3(), 1);
    const auto targetBody = targetFixture(1, false, false, Transform3());
    const auto &target = *targetBody.face(targetBody.faces().front()).surface;
    const auto sourceBytes = writeBodyBinary(source), targetBytes = writeBodyBinary(targetBody);
    for (double tolerance :
         {0., -1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        SurfaceWrapOptions options;
        options.tolerance = tolerance;
        auto history = sentinelHistory();
        const auto stamp = historyStamp(history);
        double deviation = -123;
        FK_CHECK_THROWS(wrapBodyOnSurface(source, target, options, &history, &deviation));
        FK_CHECK(historyStamp(history) == stamp && deviation == -123);
    }
    {
        SurfaceWrapOptions options;
        options.shift = {2, 0};
        auto history = sentinelHistory();
        const auto stamp = historyStamp(history);
        double deviation = -123;
        FK_CHECK_THROWS(wrapBodyOnSurface(source, target, options, &history, &deviation));
        FK_CHECK(historyStamp(history) == stamp && deviation == -123);
    }
    {
        auto history = sentinelHistory();
        const auto stamp = historyStamp(history);
        double deviation = -123;
        FK_CHECK_THROWS(detail::mapBodyByFunction(
            source, [](const Vec3 &) -> Vec3 { throw std::domain_error("callback failure"); }, {}, 1e-6,
            &history, &deviation));
        FK_CHECK(historyStamp(history) == stamp && deviation == -123);
    }
    for (bool fromCallback : {false, true}) {
        auto history = sentinelHistory();
        const auto stamp = historyStamp(history);
        double deviation = -123;
        bool cancelled = false;
        int calls = 0;
        auto token = std::make_shared<std::atomic<bool>>(!fromCallback);
        CancellationScope scope(token);
        try {
            if (fromCallback)
                detail::mapBodyByFunction(
                    source,
                    [&](const Vec3 &p) {
                        ++calls;
                        token->store(true);
                        return p;
                    },
                    {}, 1e-6, &history, &deviation);
            else
                wrapBodyOnSurface(source, target, SurfaceWrapOptions(), &history, &deviation);
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
        FK_CHECK(cancelled && historyStamp(history) == stamp && deviation == -123);
        if (fromCallback)
            FK_CHECK(calls > 0);
    }
    FK_CHECK(writeBodyBinary(source) == sourceBytes && writeBodyBinary(targetBody) == targetBytes);
}

FK_TEST(MappingCarrierDegree27SingleSpanAndRoundtrip) {
    // No internal target knot reveals these lobes. A fixed sample increase
    // or propagation of target breakpoints alone cannot validate this case.
    const std::vector<double> z = {0,
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
    std::vector<Vec3> poles;
    for (int i = 0; i < 28; ++i)
        poles.emplace_back(i / 27., 0, z[i]);
    std::vector<double> knots(28, 0);
    knots.insert(knots.end(), 28, 1);
    const auto curve = std::make_shared<BSplineCurve<3>>(27, knots, poles);
    const auto target = targetSheet(std::make_shared<ExtrusionSurface>(curve, Vec3(0, 1, 0)), 1);
    const auto source = planeSource(Frame3(), 1);
    SurfaceWrapOptions options;
    options.tolerance = 1e-6;
    checkMapped(source, target, options, true);
}

FK_TEST(MappingCarrierTolerantSourceDiscrepanciesAreRejectedAtomically) {
    const auto targetBody = targetFixture(1, false, false, Transform3());
    const auto &target = *targetBody.face(targetBody.faces().front()).surface;
    for (bool shiftedUV : {false, true}) {
        auto source = planeSource(Frame3(), 1);
        if (shiftedUV) {
            for (auto id : source.fins()) {
                auto &fin = source.fin(id);
                const auto &edge = source.edge(fin.edge);
                const Vec2 offset(1e-5, 0);
                fin.pcurve = affineUV(fin.pcurve->point(edge.range.lo) + offset,
                                      fin.pcurve->point(edge.range.hi) + offset, edge.range);
                fin.pcurveTolerance = 2e-5;
            }
        } else {
            auto &vertex = source.vertex(source.vertices().front());
            vertex.point[0] += 1e-5;
            vertex.tolerance = 2e-5;
        }
        // These are valid tolerance-bearing B-reps, but the operation's
        // stricter fitting budget must not silently discard their discrepancy.
        FK_CHECK(checkBody(source, {true, true}).empty());
        const auto sourceBytes = writeBodyBinary(source);
        auto history = sentinelHistory();
        const auto stamp = historyStamp(history);
        double deviation = -123;
        SurfaceWrapOptions options;
        options.tolerance = 1e-6;
        FK_CHECK_THROWS(wrapBodyOnSurface(source, target, options, &history, &deviation));
        FK_CHECK(historyStamp(history) == stamp && deviation == -123);
        FK_CHECK(writeBodyBinary(source) == sourceBytes);
    }
}

FK_TEST(MappingCarrierSmallSourceVertexDiscrepancyIsReported) {
    auto source = planeSource(Frame3(), 1);
    const auto vertexId = source.vertices().front();
    source.vertex(vertexId).point[0] += 1.9e-8;
    source.vertex(vertexId).tolerance = 2e-5;
    FK_CHECK(checkBody(source, {true, true}).empty());
    const auto before = writeBodyBinary(source);
    const auto targetBody = targetFixture(1, false, false, Transform3());
    const auto &target = *targetBody.face(targetBody.faces().front()).surface;
    SurfaceWrapOptions options;
    options.tolerance = 1e-6;
    double deviation = -123;
    const auto mapped = wrapBodyOnSurface(source, target, options, nullptr, &deviation);
    const Vec3 original = source.vertex(vertexId).point;
    const double gap = distance(mapped.vertex(vertexId).point, expectedPoint(target, options, original));
    FK_CHECK(gap > 1.8e-8 && gap <= deviation && deviation <= options.tolerance);
    FK_CHECK(checkBody(mapped, {true, true}).empty());
    FK_CHECK(writeBodyBinary(source) == before);
}

FK_TEST(MappingCarrierUnresolvedNaturalBoundaryIsAtomic) {
    // Outward bounds may straddle a finite natural-domain endpoint after
    // floating-point pose/quarter-turn construction. This is an explicit
    // conservative failure, while the corresponding interior cases must pass.
    for (bool quarterTurn : {false, true}) {
        const double scale = quarterTurn ? 1 : .01;
        const Frame3 frame(scale * Vec3(-2, 1, .3), Vec3(.2, -.3, 1), Vec3(1, 0, 0));
        const auto source = planeSource(frame, scale);
        const auto placement = quarterTurn ? Transform3()
                                           : Transform3::translation(scale * Vec3(3, 2, -1)) *
                                                 Transform3::rotation({}, Vec3(1, 2, 3), .57);
        const auto target = targetFixture(scale, false, false, placement);
        SurfaceWrapOptions options;
        options.sourcePlane = frame;
        options.tolerance = 1e-6 * scale;
        if (quarterTurn) {
            options.angle = kHalfPi;
            options.shift = {1, 0};
        }
        auto history = sentinelHistory();
        const auto stamp = historyStamp(history);
        double deviation = -123;
        FK_CHECK_THROWS(wrapBodyOnSurface(source, *target.face(target.faces().front()).surface, options,
                                          &history, &deviation));
        FK_CHECK(historyStamp(history) == stamp && deviation == -123);
    }
}

FK_TEST(MappingCarrierRecognizedAffineFinBudgetFailsAtomically) {
    auto source = planeSource(Frame3(), 1);
    auto &fin = source.fin(source.fins().front());
    const auto range = source.edge(fin.edge).range;
    const Vec2 a = fin.pcurve->point(range.lo), b = fin.pcurve->point(range.hi);
    constexpr int count = 100001;
    std::vector<Vec2> poles;
    std::vector<double> knots{range.lo, range.lo};
    poles.reserve(count);
    knots.reserve(count + 2);
    for (int i = 0; i < count; ++i) {
        const double t = double(i) / (count - 1);
        poles.push_back(a + (b - a) * t);
        if (i > 0 && i + 1 < count)
            knots.push_back(range.lo + range.length() * t);
    }
    knots.push_back(range.hi);
    knots.push_back(range.hi);
    fin.pcurve = std::make_shared<BSplineCurve<2>>(1, std::move(knots), std::move(poles));
    FK_CHECK(checkBody(source, {true, true}).empty());
    const auto sourceBytes = writeBodyBinary(source);
    const auto targetBody = targetFixture(1, false, false, Transform3());
    const auto &target = *targetBody.face(targetBody.faces().front()).surface;
    auto history = sentinelHistory();
    const auto stamp = historyStamp(history);
    double deviation = -123;
    SurfaceWrapOptions options;
    options.tolerance = 1e-6;
    // Known affine geometry beyond the resource budget is unresolved, not an
    // unsupported family eligible for the legacy sampled fallback.
    FK_CHECK_THROWS(detail::wrapPlanarBodyOnCarrier(source, target, options, 1, 1, &history, &deviation));
    FK_CHECK(historyStamp(history) == stamp && deviation == -123);
    FK_CHECK(writeBodyBinary(source) == sourceBytes);
}
