#include "fk_offset_support.h"
#include "fk_classify_uv.h"
#include "fk_exchange.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>

namespace ForgeCad::Kernel::detail {
namespace {
BSplineCurve<3> continueCurve(const BSplineCurve<3> &input, double lo, double hi) {
    const auto curve = input.clamped();
    auto knots = curve.knots();
    const int degree = curve.degree();
    const auto domain = curve.domain();
    std::vector<Vec3> homogeneous, weights;
    for (int i = 0; i < curve.poleCount(); ++i) {
        homogeneous.push_back(curve.weight(i) * curve.poles()[i]);
        weights.emplace_back(curve.weight(i), 0, 0);
    }
    const BSplineCurve<3> numerator(degree, knots, homogeneous), denominator(degree, knots, weights);
    const auto section = [&](const BSplineCurve<3> &basis, double at, double length, bool left) {
        Vec3 jet[3];
        basis.evaluate(at, 2, jet);
        std::vector<Vec3> poles;
        for (int i = 0; i <= degree; ++i) {
            const double a = double(i) / degree;
            const double b = degree > 1 ? double(i) * (i - 1) / (degree * (degree - 1)) : 0.0;
            poles.push_back(left ? jet[0] + (a - 1) * length * jet[1] + .5 * (1 - 2 * a + b) * length * length * jet[2]
                                 : jet[0] + a * length * jet[1] + .5 * b * length * length * jet[2]);
        }
        return poles;
    };
    if (hi > domain.hi) {
        const auto p = section(numerator, domain.hi, hi - domain.hi, false);
        const auto w = section(denominator, domain.hi, hi - domain.hi, false);
        homogeneous.insert(homogeneous.end(), p.begin() + 1, p.end());
        weights.insert(weights.end(), w.begin() + 1, w.end());
        knots.pop_back();
        knots.insert(knots.end(), degree + 1, hi);
    }
    if (lo < domain.lo) {
        const auto p = section(numerator, domain.lo, domain.lo - lo, true);
        const auto w = section(denominator, domain.lo, domain.lo - lo, true);
        homogeneous.insert(homogeneous.begin(), p.begin(), p.end() - 1);
        weights.insert(weights.begin(), w.begin(), w.end() - 1);
        knots.erase(knots.begin());
        knots.insert(knots.begin(), degree + 1, lo);
    }
    std::vector<Vec3> poles;
    std::vector<double> resultWeights;
    for (std::size_t i = 0; i < homogeneous.size(); ++i) {
        if (!(weights[i].x() > 0))
            throw std::domain_error("offset: continuazione C2 con peso non positivo");
        poles.push_back(homogeneous[i] / weights[i].x());
        if (curve.isRational())
            resultWeights.push_back(weights[i].x());
    }
    return BSplineCurve<3>(degree, std::move(knots), std::move(poles), std::move(resultWeights));
}
Body trimBody(const RawModel &model, const RawFace &face) {
    std::vector<Vec3> points;
    std::vector<Body::BuildEdge> edges;
    Body::BuildFace result{face.surface, face.sense, {}};
    std::map<int, int> pointMap, edgeMap;
    const auto point = [&](int index) {
        auto entry = pointMap.emplace(index, int(points.size()));
        if (entry.second)
            points.push_back(model.points.at(std::size_t(index)));
        return entry.first->second;
    };
    for (const auto &loop : face.loops) {
        std::vector<Body::BuildFin> fins;
        for (const auto &fin : loop) {
            auto entry = edgeMap.emplace(fin.edge, int(edges.size()));
            if (entry.second) {
                const auto &edge = model.edges.at(std::size_t(fin.edge));
                edges.push_back({point(edge.start), point(edge.end), edge.curve, edge.range});
            }
            fins.push_back({entry.first->second, fin.sense, fin.pcurve, fin.pcurveTolerance});
        }
        result.loops.push_back(std::move(fins));
    }
    return Body::buildSheet(points, edges, {result});
}
} // namespace
BSplineSurface continueOffsetSupport(const BSplineSurface &input, const Interval &u, const Interval &v) {
    BSplineSurface surface = input;
    for (int axis = 0; axis < 2; ++axis) {
        const int count = axis == 0 ? surface.vPoleCount() : surface.uPoleCount();
        const int along = axis == 0 ? surface.uPoleCount() : surface.vPoleCount();
        std::vector<BSplineCurve<3>> curves;
        for (int j = 0; j < count; ++j) {
            std::vector<Vec3> poles;
            std::vector<double> weights;
            for (int i = 0; i < along; ++i) {
                poles.push_back(axis == 0 ? surface.pole(i, j) : surface.pole(j, i));
                if (surface.isRational())
                    weights.push_back(axis == 0 ? surface.weight(i, j) : surface.weight(j, i));
            }
            const BSplineCurve<3> curve(axis == 0 ? surface.uDegree() : surface.vDegree(), axis == 0 ? surface.uKnots() : surface.vKnots(),
                                        std::move(poles), std::move(weights));
            const auto range = axis == 0 ? u : v;
            curves.push_back(continueCurve(curve, range.lo, range.hi));
        }
        const int nu = axis == 0 ? curves.front().poleCount() : count;
        const int nv = axis == 0 ? count : curves.front().poleCount();
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < nu; ++i)
            for (int j = 0; j < nv; ++j) {
                const auto &curve = curves[std::size_t(axis == 0 ? j : i)];
                const int k = axis == 0 ? i : j;
                poles.push_back(curve.poles()[std::size_t(k)]);
                if (surface.isRational())
                    weights.push_back(curve.weight(k));
            }
        surface = BSplineSurface(surface.uDegree(), surface.vDegree(), axis == 0 ? curves.front().knots() : surface.uKnots(),
                                 axis == 1 ? curves.front().knots() : surface.vKnots(), nu, nv, std::move(poles), std::move(weights));
    }
    return surface;
}
bool regularOffset(const Surface &surface, double distance, const Vec2 &uv) {
    if (distance == 0)
        return true;
    Vec3 jet[9];
    surface.evaluate(uv.x(), uv.y(), 2, jet);
    const auto a = jet[3], b = jet[1], n = normalized(cross(a, b));
    const double E = dot(a, a), F = dot(a, b), G = dot(b, b), L = dot(n, jet[6]), M = dot(n, jet[4]), N = dot(n, jet[2]);
    const double det = E * G - F * F;
    if (!(det > 0))
        return false;
    const double H = (E * N - 2 * F * M + G * L) / (2 * det), K = (L * N - M * M) / det;
    const double root = std::sqrt(std::max(0., H * H - K));
    return 1 - distance * (H + root) > 0 && 1 - distance * (H - root) > 0;
}
void requireRegularOffsetTrim(const RawModel &model, const RawFace &face, double distance, const Interval &u, const Interval &v,
                              double tolerance) {
    if (distance == 0 || face.surface->type() != SurfaceType::BSpline)
        return;
    auto us = face.surface->uBreakpoints(u), vs = face.surface->vBreakpoints(v);
    for (int k = 0; k <= 64; ++k) {
        us.push_back(u.lo + u.length() * k / 64.);
        vs.push_back(v.lo + v.length() * k / 64.);
    }
    // Preparazione solo se il rettangolo contiene un tratto ripiegato.
    std::optional<Body> trim;
    std::optional<FaceParameterClassifier> classifier;
    for (double a : us)
        for (double b : vs)
            if (!regularOffset(*face.surface, distance, {a, b})) {
                if (!trim) {
                    trim = trimBody(model, face);
                    classifier.emplace(*trim, trim->faces().front(), tolerance);
                }
                if (classifier->classify({a, b}) != PointLocation::Outside)
                    throw std::domain_error("offset: la parete a distanza si ripiega dentro la faccia rifilata");
            }
}
} // namespace ForgeCad::Kernel::detail
