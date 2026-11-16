#include "fk_affine_surface_fit.h"
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
Body planeSource(const Frame3 &frame, double scale, bool hole = false, bool trimmedEdges = false,
                 double height = -1, bool reversed = false) {
    if (height < 0)
        height = scale;
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
            const auto originalA = points[i], originalB = points[(i + 1) % points.size()];
            const auto a = reversed ? originalB : originalA, b = reversed ? originalA : originalB;
            const double length = distance(a, b);
            const Vec3 pa = frame.toGlobal({a.x(), a.y(), 0});
            const Vec3 pb = frame.toGlobal({b.x(), b.y(), 0});
            Interval range{0, length};
            CurvePtr<3> curve = std::make_shared<Line<3>>(pa, pb - pa);
            if (trimmedEdges) {
                range = {2, 5};
                curve = std::make_shared<BSplineCurve<3>>(1, std::vector<double>{2, 2, 5, 5},
                                                          std::vector<Vec3>{pa, pb});
                curve = std::make_shared<TrimmedCurve<3>>(curve, 2, 5);
                curve = std::make_shared<TrimmedCurve<3>>(curve, 2, 5);
            }
            fins.push_back({int(edges.size()), !reversed, affineUV(a, b, range), 1e-9});
            const int ia = first + i, ib = first + (i + 1) % int(points.size());
            edges.push_back({reversed ? ib : ia, reversed ? ia : ib, curve, range, 1e-9});
        }
        face.loops.push_back(fins);
    };
    loop({{0, 0}, {scale, 0}, {scale, height}, {0, height}});
    if (hole)
        loop({{.55 * scale, .35 * height},
              {.55 * scale, .65 * height},
              {.8 * scale, .65 * height},
              {.8 * scale, .35 * height}});
    return Body::buildSheet(vertices, edges, {face});
}
// A regular graph with a narrow degree-six bump between U=.1 and .101.
// Greville coefficients reproduce X=U; the rational numerator includes the
// matching quadratic term so positive nonconstant weights preserve X=U.
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
    FK_CHECK(checkBody(source, {true, true}).empty());
    FK_CHECK(checkBody(targetBody, {true, true}).empty());
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
        FK_CHECK(projectPoint(*carrier, peak, {0, 1}, {0, norm(jet[2])}).distance <= options.tolerance);
        for (auto id : body.edges()) {
            const auto &edge = body.edge(id);
            const auto &original = source.edge(id);
            std::vector<double> parameters;
            for (int i = 0; i <= 2000; ++i)
                parameters.push_back(edge.range.lo + edge.range.length() * i / 2000.);
            const auto originalU = [&](double t) {
                const auto p = options.sourcePlane.toLocal(original.curve->point(t));
                return options.anchor.x() + (std::cos(options.angle) * p.x() -
                                             std::sin(options.angle) * p.y() + options.shift.x()) /
                                                norm(jet[2]);
            };
            const double a = originalU(edge.range.lo), b = originalU(edge.range.hi);
            if (a != b)
                for (double u : {.1, .10025, .1005, .10075, .101}) {
                    const double t = edge.range.lo + edge.range.length() * (u - a) / (b - a);
                    if (edge.range.contains(t))
                        parameters.push_back(t);
                }
            // Pull back the hidden target span as well as dense independent samples.
            for (double t : parameters)
                edgeError =
                    std::max(edgeError, distance(edge.curve->point(t),
                                                 expectedPoint(target, options, original.curve->point(t))));
        }
        FK_CHECK_NEAR(edgeError, 0., options.tolerance);
    };
    verify(mapped);
    if (roundtrip)
        verify(readBodyBinary(writeBodyBinary(mapped)));
    FK_CHECK(writeBodyBinary(source) == sourceBytes && writeBodyBinary(targetBody) == targetBytes);
}
} // namespace

FK_TEST(MappingDiagonalExtrusionHiddenPolynomialRationalPoseScaleReverse) {
    for (double scale : {.01, 1., 1000.})
        for (bool rational : {false, true})
            for (bool reverse : {false, true}) {
                const auto placement = Transform3::translation(scale * Vec3(3, 2, -1)) *
                                       Transform3::rotation({}, Vec3(1, 2, 3), .57);
                const Frame3 frame(scale * Vec3(-2, 1, .3), Vec3(.2, -.3, 1), Vec3(1, 0, 0));
                const auto source = planeSource(frame, .6 * scale, false, reverse, .2 * scale, reverse);
                const auto target = targetFixture(scale, rational, false, placement);
                SurfaceWrapOptions options;
                options.sourcePlane = frame;
                options.angle = .29;
                options.shift = {.03 * scale, .02 * scale};
                options.anchor = {.05, .1 * scale};
                options.tolerance = 1e-6 * scale;
                try {
                    checkMapped(source, target, options);
                } catch (const std::exception &e) {
                    throw std::runtime_error(std::string(e.what()) + " scale=" + std::to_string(scale) +
                                             " rational=" + std::to_string(rational) +
                                             " reverse=" + std::to_string(reverse));
                }
            }
}

FK_TEST(MappingDiagonalExtrusionNestedGeneratorTrimHolesHistoryRoundtrip) {
    const Frame3 frame(Vec3(2, -3, 1), Vec3(.2, .3, 1), Vec3(1, 0, 0));
    for (bool rational : {false, true}) {
        CurvePtr<3> curve = narrowProfile(1, rational, Transform3());
        curve = std::make_shared<TrimmedCurve<3>>(curve, 0, 1);
        const auto placement =
            Transform3::translation({3, -2, 1}) * Transform3::rotation({}, Vec3(1, 2, 3), .43);
        curve = std::make_shared<TransformedCurve>(curve, placement);
        const auto target =
            targetSheet(std::make_shared<ExtrusionSurface>(curve, placement.applyToVector({0, 1, 0})), 1);
        const auto source = planeSource(frame, .6, true, true, .2, true);
        SurfaceWrapOptions options;
        options.sourcePlane = frame;
        options.angle = .29;
        options.shift = {.03, .02};
        options.anchor = {.05, .1};
        options.tolerance = 1e-6;
        checkMapped(source, target, options, true);
        const auto mapped = wrapBodyOnSurface(source, *target.face(target.faces().front()).surface, options);
        const FaceClassifier classifier(mapped, mapped.faces().front(), 1e-7);
        const auto &carrier = *target.face(target.faces().front()).surface;
        FK_CHECK(classifier.classify(expectedPoint(carrier, options, frame.toGlobal({.4, .1, 0}))) ==
                 PointLocation::Outside);
        FK_CHECK(classifier.classify(expectedPoint(carrier, options, frame.toGlobal({.05, .1, 0}))) ==
                 PointLocation::Inside);
    }
}

FK_TEST(MappingDiagonalOriginalLineExtrusionSuccess) {
    const auto line = std::make_shared<Line<3>>(Vec3(), Vec3(1, 0, .2));
    const auto target = targetSheet(std::make_shared<ExtrusionSurface>(line, Vec3(0, 1, 0)), 1);
    const auto source = planeSource(Frame3(), .6, false, true, .2, true);
    SurfaceWrapOptions options;
    options.angle = -.29;
    options.shift = {.03, .3};
    options.anchor = {.05, .1};
    options.tolerance = 1e-6;
    checkMapped(source, target, options, true);
}

FK_TEST(MappingDiagonalProviderPullbackKnotsAndWrongCandidate) {
    for (bool rational : {false, true}) {
        const auto target =
            std::make_shared<ExtrusionSurface>(narrowProfile(1, rational, Transform3()), Vec3(0, 1, 0));
        CurvePtr<2> uv = affineUV({.05, .1}, {.2, .6}, {2, 5});
        uv = std::make_shared<TrimmedCurve<2>>(uv, 2, 5);
        uv = std::make_shared<TrimmedCurve<2>>(uv, 2, 5);
        const auto data = detail::affineSurfaceCompositionErrorBound(target, uv, {2, 5});
        FK_CHECK(bool(data.bound));
        for (double expected : {3., 3.02}) {
            double gap = std::numeric_limits<double>::infinity();
            for (double value : data.breaks)
                gap = std::min(gap, std::abs(value - expected));
            FK_CHECK(gap < 1e-11);
        }
        std::array<Vec3, 4> wrong{};
        for (int i = 0; i < 4; ++i) {
            const auto p = uv->point(3 + .02 * i / 3.);
            wrong[i] = target->point(p.x(), p.y());
            wrong[i][2] = 0;
        }
        const auto midpoint = uv->point(3.01);
        const double hiddenHeight = std::abs(target->point(midpoint.x(), midpoint.y()).z());
        FK_CHECK(hiddenHeight > .0009);
        FK_CHECK(data.bound({3, 3.02}, wrong) >= hiddenHeight);
        double deviation = -123;
        const auto fitted = fitCurveCertified(
            [&](double t) {
                const auto p = uv->point(t);
                return target->point(p.x(), p.y());
            },
            {2, 5}, data.breaks, 2e-8, data.bound, &deviation);
        double error = 0;
        for (int i = 0; i <= 10000; ++i) {
            const double t = 2 + 3. * i / 10000;
            const auto p = uv->point(t);
            error = std::max(error, distance(fitted->point(t), target->point(p.x(), p.y())));
        }
        FK_CHECK(error <= 2e-8 && deviation <= 2e-8);
    }
}

FK_TEST(MappingDiagonalFailureCancellationAreAtomicAndTensorIsUnsupported) {
    const auto source = planeSource(Frame3(), .6, false, false, .2);
    const auto target = targetFixture(1, false, false, Transform3());
    const auto sourceBytes = writeBodyBinary(source), targetBytes = writeBodyBinary(target);
    const auto providerUV = affineUV({.05, .1}, {.2, .6}, {2, 5});
    for (bool cancel : {false, true}) {
        SurfaceWrapOptions options;
        options.angle = .29;
        options.shift = cancel ? Vec2(.03, .02) : Vec2(2, 0);
        options.anchor = {.05, .1};
        auto history = sentinelHistory();
        const auto stamp = historyStamp(history);
        double deviation = -123;
        const auto token = std::make_shared<std::atomic<bool>>(cancel);
        CancellationScope scope(token);
        if (cancel) {
            bool cancelled = false;
            try {
                wrapBodyOnSurface(source, *target.face(target.faces().front()).surface, options, &history,
                                  &deviation);
            } catch (const OperationCancelled &) {
                cancelled = true;
            }
            FK_CHECK(cancelled);
            cancelled = false;
            try {
                detail::affineSurfaceCompositionErrorBound(target.face(target.faces().front()).surface,
                                                           providerUV, {2, 5});
            } catch (const OperationCancelled &) {
                cancelled = true;
            }
            FK_CHECK(cancelled);
        } else {
            FK_CHECK_THROWS(wrapBodyOnSurface(source, *target.face(target.faces().front()).surface, options,
                                              &history, &deviation));
        }
        FK_CHECK(historyStamp(history) == stamp && deviation == -123);
    }
    FK_CHECK(writeBodyBinary(source) == sourceBytes && writeBodyBinary(target) == targetBytes);
    const auto tensor = targetFixture(1, false, true, Transform3());
    const auto surface = tensor.face(tensor.faces().front()).surface;
    const auto uv = affineUV({.05, .1}, {.2, .6}, {2, 5});
    FK_CHECK(!detail::affineSurfaceCompositionErrorBound(surface, uv, {2, 5}).bound);
    SurfaceWrapOptions offset;
    offset.angle = .29;
    offset.normalOffset = .01;
    auto history = sentinelHistory();
    const auto stamp = historyStamp(history);
    double deviation = -123;
    FK_CHECK(!detail::wrapPlanarBodyOnCarrier(source, *target.face(target.faces().front()).surface, offset, 1,
                                              1, &history, &deviation));
    FK_CHECK(historyStamp(history) == stamp && deviation == -123);
    // Material normal offsets deliberately remain on the legacy route.
}

FK_TEST(MappingDiagonalProviderTinyNonzeroUDirectionMustNotSnap) {
    const auto original = narrowProfile(1, false, Transform3());
    const auto &spline = static_cast<const BSplineCurve<3> &>(*original);
    auto knots = spline.knots();
    constexpr double parameterScale = 1e-16;
    for (double &k : knots)
        k *= parameterScale;
    const auto compressed = std::make_shared<BSplineCurve<3>>(6, knots, spline.poles());
    const auto target = std::make_shared<ExtrusionSurface>(compressed, Vec3(0, 1, 0));
    const auto uv = affineUV({.06 * parameterScale, -.5}, {.94 * parameterScale, .75}, {0, 1});
    const auto data = detail::affineSurfaceCompositionErrorBound(target, uv, {0, 1});
    FK_CHECK(bool(data.bound));
    // The tiny parameter direction still traverses most of the generator.
    // Treating it as zero discards an order-one geometric displacement.
    const double peak = (.1005 - .06) / (.94 - .06);
    const auto p = uv->point(peak);
    FK_CHECK(target->point(p.x(), p.y()).z() > .00099);
    std::array<Vec3, 4> planar{};
    for (int i = 0; i < 4; ++i) {
        const auto q = uv->point(i / 3.);
        planar[i] = target->point(q.x(), q.y());
        planar[i][2] = 0;
    }
    FK_CHECK(data.bound({0, 1}, planar) >= .00099);
    double deviation = -123;
    const auto fitted = fitCurveCertified(
        [&](double t) {
            const auto q = uv->point(t);
            return target->point(q.x(), q.y());
        },
        {0, 1}, data.breaks, 2e-8, data.bound, &deviation);
    double error = 0;
    for (int i = 0; i <= 10000; ++i) {
        const double t = i / 10000.;
        const auto q = uv->point(t);
        error = std::max(error, distance(fitted->point(t), target->point(q.x(), q.y())));
    }
    FK_CHECK(error <= 2e-8 && deviation <= 2e-8);
}

FK_TEST(MappingDiagonalProviderLineConstantUAndClosedJunctionQueries) {
    const auto target =
        std::make_shared<ExtrusionSurface>(narrowProfile(1, false, Transform3()), Vec3(0, 1, 0));
    const double length = norm(Vec2(.15, .5));
    const std::vector<std::pair<CurvePtr<2>, Interval>> cases{
        {std::make_shared<Line<2>>(Vec2(.1005, -.5), Vec2(0, 1)), {0, 1}},
        {std::make_shared<Line<2>>(Vec2(.05, .1), Vec2(.15, .5)), {0, length}},
        {std::make_shared<BSplineCurve<2>>(1, std::vector<double>{2, 2, 3, 5, 5},
                                           std::vector<Vec2>{{.05, .1}, {.1005, .2}, {.2, .6}}),
         {2, 5}}};
    for (const auto &entry : cases) {
        const auto &uv = entry.first;
        const auto range = entry.second;
        const auto data = detail::affineSurfaceCompositionErrorBound(target, uv, range);
        FK_CHECK(bool(data.bound));
        double deviation = -123;
        const auto fitted = fitCurveCertified(
            [&](double t) {
                const auto p = uv->point(t);
                return target->point(p.x(), p.y());
            },
            range, data.breaks, 2e-8, data.bound, &deviation);
        double error = 0;
        for (int i = 0; i <= 10000; ++i) {
            const double t = range.lo + range.length() * i / 10000.;
            const auto p = uv->point(t);
            error = std::max(error, distance(fitted->point(t), target->point(p.x(), p.y())));
        }
        FK_CHECK(error <= 2e-8 && deviation <= 2e-8);
    }
    const auto &uv = cases.back().first;
    const auto data = detail::affineSurfaceCompositionErrorBound(target, uv, {2, 5});
    // Both closed sides include the shared hidden peak at parameter 3.
    for (const Interval query : {Interval{2, 3}, Interval{3, 5}, Interval{2.9, 3.1}}) {
        std::array<Vec3, 4> planar{};
        for (int i = 0; i < 4; ++i) {
            const auto p = uv->point(query.lo + query.length() * i / 3.);
            planar[i] = target->point(p.x(), p.y());
            planar[i][2] = 0;
        }
        FK_CHECK(data.bound(query, planar) >= .00099);
    }
}
