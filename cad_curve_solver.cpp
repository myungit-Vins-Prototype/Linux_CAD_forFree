#include "cad_curve_solver.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "fk_bspline.h"
#include "fk_curve.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

constexpr double kConfusion = 1.0e-7;

Vec2 toVec(const QPointF &point) { return Vec2(point.x(), point.y()); }

ProfileSegment lineSegment(const Vec2 &a, const Vec2 &b) { return {std::make_shared<Line<2>>(a, b - a), {0.0, distance(a, b)}}; }

// Spline: sequenza di Bezier cubiche (P_i, H_i+, H_{i+1}-, P_{i+1}) espressa
// esattamente come B-spline di grado 3 con nodi interni di molteplicita' 3.
CurvePtr<2> makeSpline(const CurveObject &curve) {
    const int count = curve.controlPoints.size();
    if (count < 2 || curve.tangentHandles.size() != count) return {};
    std::vector<Vec2> poles;
    for (int i = 0; i < count; ++i) {
        if (i > 0) poles.push_back(toVec(curve.tangentHandles.at(i).first));
        poles.push_back(toVec(curve.controlPoints.at(i)));
        if (i + 1 < count) poles.push_back(toVec(curve.tangentHandles.at(i).second));
    }
    std::vector<double> knots;
    std::vector<int> multiplicities;
    for (int i = 0; i < count; ++i) {
        knots.push_back(double(i));
        multiplicities.push_back(i == 0 || i == count - 1 ? 4 : 3);
    }
    return std::make_shared<BSplineCurve<2>>(3, expandKnots(knots, multiplicities), std::move(poles));
}

// NURBS: poli e pesi dell'utente, grado min(3, n-1), nodi uniformi "clamped".
CurvePtr<2> makeNurbs(const CurveObject &curve) {
    const int count = curve.controlPoints.size();
    if (count < 2) return {};
    const int degree = std::min(3, count - 1);
    std::vector<Vec2> poles;
    std::vector<double> weights;
    for (int i = 0; i < count; ++i) {
        poles.push_back(toVec(curve.controlPoints.at(i)));
        const double weight = curve.weights.size() == count ? curve.weights.at(i) : 1.0;
        if (!(weight > 0.0)) return {};
        weights.push_back(weight);
    }
    const int spans = count - degree;
    std::vector<double> knots;
    std::vector<int> multiplicities;
    for (int i = 0; i <= spans; ++i) {
        knots.push_back(double(i) / spans);
        multiplicities.push_back(i == 0 || i == spans ? degree + 1 : 1);
    }
    return std::make_shared<BSplineCurve<2>>(degree, expandKnots(knots, multiplicities), std::move(poles), std::move(weights));
}

}

std::vector<ProfileSegment> curveGeometry(const CurveObject &curve) {
    std::vector<ProfileSegment> result;
    const int count = curve.controlPoints.size();
    try {
        switch (curve.tool) {
        case DrawingTool::Spline:
        case DrawingTool::Nurbs: {
            const CurvePtr<2> c = curve.tool == DrawingTool::Spline ? makeSpline(curve) : makeNurbs(curve);
            if (c) result.push_back({c, c->domain()});
            break;
        }
        case DrawingTool::Converted: {
            // Riferimento esterno: la B-spline razionale cosi' com'e'.
            if (count < 2 || curve.degree < 1 || curve.knots.size() != count + curve.degree + 1) break;
            std::vector<Vec2> poles;
            for (const QPointF &p : curve.controlPoints) poles.push_back(toVec(p));
            std::vector<double> weights(curve.weights.begin(), curve.weights.end());
            if (int(weights.size()) != count) weights.clear();
            const auto c = std::make_shared<BSplineCurve<2>>(curve.degree, std::vector<double>(curve.knots.begin(), curve.knots.end()), std::move(poles), std::move(weights));
            result.push_back({c, c->domain()});
            break;
        }
        case DrawingTool::Circle: {
            if (count < 2) break;
            const double radius = distance(toVec(curve.controlPoints.at(0)), toVec(curve.controlPoints.at(1)));
            if (radius <= kConfusion) break;
            result.push_back({std::make_shared<Circle<2>>(makeCircle(toVec(curve.controlPoints.at(0)), radius)), {0.0, kTwoPi}});
            break;
        }
        case DrawingTool::Arc: {
            if (count < 3) break;
            const Vec2 center = toVec(curve.controlPoints.at(0)), start = toVec(curve.controlPoints.at(1)), end = toVec(curve.controlPoints.at(2));
            const double radius = distance(center, start);
            if (radius <= kConfusion || distance(center, end) <= kConfusion) break;
            const double startAngle = std::atan2(start.y() - center.y(), start.x() - center.x());
            double endAngle = std::atan2(end.y() - center.y(), end.x() - center.x());
            while (endAngle <= startAngle + 1.0e-12) endAngle += kTwoPi;
            result.push_back({std::make_shared<Circle<2>>(makeCircle(center, radius)), {startAngle, endAngle}});
            break;
        }
        case DrawingTool::Ellipse: {
            if (count < 3) break;
            const Vec2 center = toVec(curve.controlPoints.at(0));
            const double a = distance(center, toVec(curve.controlPoints.at(1))), b = distance(center, toVec(curve.controlPoints.at(2)));
            if (a <= kConfusion || b <= kConfusion) break;
            const Vec2 u = (toVec(curve.controlPoints.at(1)) - center) / a;
            // Semiasse maggiore lungo X del sistema dell'ellisse.
            const Vec2 x = a >= b ? u : Vec2(-u.y(), u.x()), y(-x.y(), x.x());
            result.push_back({std::make_shared<Ellipse<2>>(center, x, y, std::max(a, b), std::min(a, b)), {0.0, kTwoPi}});
            break;
        }
        case DrawingTool::Rectangle:
        case DrawingTool::CenterRectangle: {
            if (count < 2) break;
            const QPointF p = curve.controlPoints.at(0), q = curve.controlPoints.at(1);
            const QPointF a = curve.tool == DrawingTool::Rectangle ? p : 2.0 * p - q;
            if (std::abs(q.x() - a.x()) <= kConfusion || std::abs(q.y() - a.y()) <= kConfusion) break;
            const Vec2 corners[4] = {toVec(a), Vec2(q.x(), a.y()), toVec(q), Vec2(a.x(), q.y())};
            for (int side = 0; side < 4; ++side) result.push_back(lineSegment(corners[side], corners[(side + 1) % 4]));
            break;
        }
        case DrawingTool::Polygon: {
            if (count < 2 || curve.sides < 3) break;
            const Vec2 center = toVec(curve.controlPoints.at(0)), vertex = toVec(curve.controlPoints.at(1));
            const double radius = distance(center, vertex);
            if (radius <= kConfusion) break;
            const double startAngle = std::atan2(vertex.y() - center.y(), vertex.x() - center.x());
            std::vector<Vec2> corners;
            for (int side = 0; side < curve.sides; ++side) {
                const double angle = startAngle + kTwoPi * side / curve.sides;
                corners.push_back(side == 0 ? vertex : center + Vec2(radius * std::cos(angle), radius * std::sin(angle)));
            }
            for (int side = 0; side < curve.sides; ++side) result.push_back(lineSegment(corners[std::size_t(side)], corners[std::size_t((side + 1) % curve.sides)]));
            break;
        }
        default:
            break;
        }
    } catch (const std::exception &) {
        result.clear();
    }
    return result;
}

void initializeTangentHandles(CurveObject &curve) {
    const QVector<bool> linked = curve.tangentLinked;
    curve.tangentHandles.clear();
    if (curve.tool != DrawingTool::Spline) return;
    for (int index = 0; index < curve.controlPoints.size(); ++index) {
        const QPointF previous = curve.controlPoints.at(qMax(0, index - 1));
        const QPointF next = curve.controlPoints.at(qMin(int(curve.controlPoints.size()) - 1, index + 1));
        const QPointF tangent = (next - previous) / 3.0;
        curve.tangentHandles.append(qMakePair(curve.controlPoints.at(index) - tangent, curve.controlPoints.at(index) + tangent));
    }
    curve.tangentLinked.fill(false, curve.controlPoints.size());
    for (int index = 0; index < qMin(linked.size(), curve.tangentLinked.size()); ++index)
        curve.tangentLinked[index] = linked.at(index);
}

void sampleCurve(const Curve<2> &curve, const Interval &range, double angular, double deflection, QVector<QPointF> &out) {
    const auto push = [&](const Vec2 &p) { out.append(QPointF(p.x(), p.y())); };
    std::vector<double> breaks = curve.breakpoints(range);
    if (breaks.size() < 2) breaks = {range.lo, range.hi};
    if (out.isEmpty()) push(curve.point(breaks.front()));
    // Suddivisione ricorsiva: il punto medio del parametro deve stare vicino
    // alla corda e le tangenti agli estremi non devono girare troppo.
    const auto refine = [&](auto &&self, double a, double b, const Vec2 &pa, const Vec2 &pb, int depth) -> void {
        const double m = 0.5 * (a + b);
        const Vec2 pm = curve.point(m);
        const Vec2 chord = pb - pa;
        const double length = norm(chord);
        const double offset = length > 0.0 ? std::fabs(cross(chord, pm - pa)) / length : norm(pm - pa);
        const Vec2 ta = curve.derivative(a), tb = curve.derivative(b);
        const double na = norm(ta), nb = norm(tb);
        double turn = 0.0;
        if (na > 0.0 && nb > 0.0) turn = std::acos(std::clamp(dot(ta, tb) / (na * nb), -1.0, 1.0));
        if (depth < 16 && (offset > deflection || turn > angular || depth < 1)) {
            self(self, a, m, pa, pm, depth + 1);
            self(self, m, b, pm, pb, depth + 1);
            return;
        }
        push(pb);
    };
    for (std::size_t k = 0; k + 1 < breaks.size(); ++k) {
        const double a = breaks[k], b = breaks[k + 1];
        if (!(b > a)) continue;
        // Almeno quattro intervalli per tratto: una corda sola non vede un'ansa simmetrica.
        Vec2 pa = curve.point(a);
        for (int part = 0; part < 4; ++part) {
            const double s = a + (b - a) * part / 4.0, e = part == 3 ? b : a + (b - a) * (part + 1) / 4.0;
            const Vec2 pe = curve.point(e);
            refine(refine, s, e, pa, pe, 0);
            pa = pe;
        }
    }
}

void recalculateCurve(CurveObject &curve, int quality) {
    if (curve.tool == DrawingTool::Spline && curve.tangentHandles.size() != curve.controlPoints.size()) initializeTangentHandles(curve);
    curve.samples.clear();
    const std::vector<ProfileSegment> geometry = curveGeometry(curve);
    const double angular = quality <= 0 ? 0.2 : quality == 1 ? 0.08 : 0.03;
    const double deflection = quality <= 0 ? 1.0e-2 : quality == 1 ? 2.0e-3 : 4.0e-4;
    try {
        for (const ProfileSegment &piece : geometry) sampleCurve(*piece.curve, piece.range, angular, deflection, curve.samples);
    } catch (const std::exception &) {
        curve.samples.clear();
    }
    curve.numericallyValid = !geometry.empty() && curve.samples.size() >= 2;
    for (const QPointF &sample : curve.samples) curve.numericallyValid = curve.numericallyValid && std::isfinite(sample.x()) && std::isfinite(sample.y());
}

}
