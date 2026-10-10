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
    if (!curve.knots.isEmpty()) {
        if (curve.degree < 1 || curve.knots.size() != count + curve.degree + 1) return {};
        std::vector<Vec2> poles;
        for (const auto &p : curve.controlPoints) poles.push_back(toVec(p));
        if (!curve.weights.isEmpty() && curve.weights.size() != count) return {};
        for (double weight : curve.weights) if (!(weight > 0.0)) return {};
        return std::make_shared<BSplineCurve<2>>(curve.degree,
            std::vector<double>(curve.knots.begin(), curve.knots.end()), std::move(poles),
            std::vector<double>(curve.weights.begin(), curve.weights.end()));
    }
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

QVector<QPair<int, QPointF>> curveQuadrants(const CurveObject &curve) {
    QVector<QPair<int, QPointF>> result;
    const auto &p = curve.controlPoints;
    if ((curve.tool != DrawingTool::Circle && curve.tool != DrawingTool::Arc) || p.size() < 2
        || (curve.tool == DrawingTool::Arc && p.size() < 3)) return result;
    const double pi = 3.14159265358979323846;
    const double r = std::hypot(p[1].x()-p[0].x(), p[1].y()-p[0].y());
    if (!(r > 0.0)) return result;
    const QPointF offsets[] = {{r,0},{0,r},{-r,0},{0,-r}};
    for (int k = 0; k < 4; ++k) {
        if (curve.tool == DrawingTool::Arc) {
            const double start = std::atan2(p[1].y()-p[0].y(), p[1].x()-p[0].x());
            double sweep = std::atan2(p[2].y()-p[0].y(), p[2].x()-p[0].x()) - start;
            while (sweep <= 0.0) sweep += 2*pi;
            double offset = std::fmod(k*pi/2-start+2*pi, 2*pi);
            if (offset > sweep+1e-12) continue;
        }
        result.append({k, p[0]+offsets[k]});
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
    curve.tangentLinked.fill(true, curve.controlPoints.size());
    for (int index = 0; index < qMin(linked.size(), curve.tangentLinked.size()); ++index)
        curve.tangentLinked[index] = linked.at(index);
}

void shapeSpline(CurveObject &curve, const SplineShapeOptions &options) {
    if (curve.tool != DrawingTool::Spline || curve.controlPoints.size() < 2
        || !(options.uniform || options.relaxed || options.limitOvershoot)) return;
    const auto &p = curve.controlPoints;
    // Solo estremi esattamente coincidenti: non spostare punti o vincoli.
    const bool closed = p.size() > 3 && p.first() == p.last();
    const int n = int(p.size()) - (closed ? 1 : 0);
    QVector<QPointF> d(n), rhs(n);
    for (int i = 0; i < n; ++i) {
        const QPointF before = p.at(closed ? (i + n - 1) % n : qMax(0, i - 1));
        const QPointF after = p.at(closed ? (i + 1) % n : qMin(n - 1, i + 1));
        d[i] = after - before;
        rhs[i] = 3.0 * d[i];
    }
    if (options.uniform) {
        // Sistema strettamente diagonalmente dominante: 64 iterazioni di
        // Gauss-Seidel portano l'errore sotto la precisione double. O(n),
        // anche per la giunzione periodica delle spline chiuse.
        for (int pass = 0; pass < 64; ++pass)
            for (int i = 0; i < n; ++i) {
                QPointF value = rhs.at(i);
                if (closed || i > 0) value -= d.at((i + n - 1) % n);
                if (closed || i + 1 < n) value -= d.at((i + 1) % n);
                d[i] = value / (!closed && (i == 0 || i == n - 1) ? 2.0 : 4.0);
            }
    }
    if (options.relaxed) for (QPointF &tangent : d) tangent *= 0.5;
    if (options.limitOvershoot) {
        // Ogni coordinata dei due poli interni resta ordinata tra gli estremi:
        // niente inversioni/overshoot di X o Y all'interno del singolo tratto.
        const auto limit = [](double value, double delta) {
            if (delta == 0.0 || value * delta <= 0.0) return 0.0;
            return std::copysign(std::min(std::abs(value), 1.5 * std::abs(delta)), delta);
        };
        for (int i = 0; i < (closed ? n : n - 1); ++i) {
            const int j = (i + 1) % n;
            const QPointF delta = p.at(j) - p.at(i);
            for (int k : {i, j}) d[k] = QPointF(limit(d.at(k).x(), delta.x()), limit(d.at(k).y(), delta.y()));
        }
    }
    curve.tangentHandles.clear();
    for (int i = 0; i < p.size(); ++i) {
        const QPointF handle = d.at(i % n) / 3.0;
        curve.tangentHandles.append(qMakePair(p.at(i) - handle, p.at(i) + handle));
    }
    curve.tangentLinked.fill(true, p.size());
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
