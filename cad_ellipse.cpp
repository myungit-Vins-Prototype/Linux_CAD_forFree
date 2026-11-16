#include "cad_ellipse.h"

#include "fk_precision.h"
#include <cmath>

namespace ForgeCad {
namespace {
bool finite(const QPointF &p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
double length(const QPointF &p) { return std::hypot(p.x(), p.y()); }
QPointF midpoint(const QPointF &a, const QPointF &b) { return 0.5 * a + 0.5 * b; }
double dot(const QPointF &a, const QPointF &b) { return a.x() * b.x() + a.y() * b.y(); }
}

bool ellipseAxes(const CurveObject &curve, QPointF &center, QPointF &axisA, QPointF &axisB) {
    const auto &p = curve.controlPoints;
    const bool extremes = curve.tool == DrawingTool::EllipseExtremes;
    if ((!extremes && curve.tool != DrawingTool::Ellipse) || p.size() != (extremes ? 4 : 3)) return false;
    for (const auto &point : p) if (!finite(point)) return false;
    const QPointF c = extremes ? midpoint(p[0], p[1]) : p[0];
    const QPointF a = (extremes ? p[0] : p[1]) - c;
    const QPointF b = p[2] - c;
    const double la = length(a), lb = length(b);
    if (!finite(c) || !finite(a) || !finite(b) || !std::isfinite(la) || !std::isfinite(lb)
        || la <= Kernel::linearResolution() || lb <= Kernel::linearResolution()) return false;
    if (extremes) {
        // Scarti spaziali dei punti veri rispetto all'ellisse analitica:
        // stessa risoluzione del documento, nessun aumento della tolleranza.
        if (length(midpoint(p[2], p[3]) - c) > Kernel::linearResolution()
            || std::abs(dot(a / la, b)) > Kernel::linearResolution()) return false;
    }
    center = c;
    axisA = a;
    axisB = b;
    return true;
}

CurveObject ellipseFromExtremes(const QPointF &a0, const QPointF &a1, const QPointF &through) {
    CurveObject curve;
    curve.tool = DrawingTool::EllipseExtremes;
    if (!finite(a0) || !finite(a1) || !finite(through)) return curve;
    const QPointF c = midpoint(a0, a1), a = a0 - c;
    const double la = length(a);
    if (!finite(c) || !finite(a) || !std::isfinite(la) || la <= Kernel::linearResolution()) return curve;
    const QPointF normal(-a.y() / la, a.x() / la);
    const double signedRadius = dot(through - c, normal);
    if (!std::isfinite(signedRadius) || std::abs(signedRadius) <= Kernel::linearResolution()) return curve;
    const QPointF b = signedRadius * normal;
    if (!finite(c + b) || !finite(c - b)) return curve;
    curve.controlPoints = {a0, a1, c + b, c - b};
    QPointF checkedCenter, checkedA, checkedB;
    if (!ellipseAxes(curve, checkedCenter, checkedA, checkedB)) curve.controlPoints.clear();
    return curve;
}

void normalizeEllipseExtremes(CurveObject &curve, int moved, const QPointF &previous) {
    if (curve.tool != DrawingTool::EllipseExtremes || curve.controlPoints.size() != 4 || moved < 0 || moved >= 4) return;
    CurveObject before = curve;
    before.controlPoints[moved] = previous;
    QPointF oldCenter, oldA, oldB;
    if (!ellipseAxes(before, oldCenter, oldA, oldB)) { curve = before; return; }
    const bool firstAxis = moved < 2;
    const int start = firstAxis ? 0 : 2, otherStart = firstAxis ? 2 : 0;
    const QPointF c = midpoint(curve.controlPoints[start], curve.controlPoints[start + 1]);
    const QPointF a = curve.controlPoints[start] - c;
    const double la = length(a), radius = length(firstAxis ? oldB : oldA);
    if (!finite(c) || !finite(a) || !std::isfinite(la) || la <= Kernel::linearResolution()) { curve = before; return; }
    const QPointF normal(-a.y() / la, a.x() / la);
    const QPointF oldFirst = firstAxis ? oldA : oldB, oldOther = firstAxis ? oldB : oldA;
    const double orientation = dot(QPointF(-oldFirst.y(), oldFirst.x()), oldOther);
    const QPointF offset = (orientation < 0.0 ? -radius : radius) * normal;
    curve.controlPoints[otherStart] = c + offset;
    curve.controlPoints[otherStart + 1] = c - offset;
    QPointF checkedCenter, checkedA, checkedB;
    if (!ellipseAxes(curve, checkedCenter, checkedA, checkedB)) curve = before;
}
}
