#include "cad_snap.h"

#include <cmath>

namespace ForgeCad {

namespace {
double distance(const QPointF &a, const QPointF &b) { return std::hypot(b.x() - a.x(), b.y() - a.y()); }
}

SnapResult snapSegments(const QPointF &point, const QVector<SketchSegment> &segments,
                        const QVector<QPointF> &points, bool enabled, bool snapToGrid,
                        double gridSpacing, double tolerance) {
    SnapResult result;
    result.point = point;
    if (!enabled) return result;

    double bestDistance = tolerance;
    for (const QPointF &candidate : points) {
        const double candidateDistance = distance(point, candidate);
        if (candidateDistance < bestDistance) {
            bestDistance = candidateDistance;
            result.point = candidate;
            result.kind = SnapKind::Endpoint;
        }
    }
    for (const auto &segment : segments) {
        for (const QPointF &endpoint : {segment.first, segment.second}) {
            const double endpointDistance = distance(point, endpoint);
            if (endpointDistance < bestDistance) {
                bestDistance = endpointDistance;
                result.point = endpoint;
                result.kind = SnapKind::Endpoint;
            }
        }
        const QPointF midpoint = (segment.first + segment.second) * 0.5;
        const double midpointDistance = distance(point, midpoint);
        if (midpointDistance < bestDistance) {
            bestDistance = midpointDistance;
            result.point = midpoint;
            result.kind = SnapKind::Midpoint;
        }
    }
    // I punti (estremi, punti medi, punti notevoli, origine) vincono sul punto
    // piu' vicino di un segmento, che e' sempre piu' vicino al cursore.
    if (result.kind != SnapKind::None) return result;
    for (const auto &segment : segments) {
        const QPointF delta = segment.second - segment.first;
        const double lengthSquared = QPointF::dotProduct(delta, delta);
        if (lengthSquared > 1.0e-12) {
            const double t = qBound(0.0, QPointF::dotProduct(point - segment.first, delta) / lengthSquared, 1.0);
            const QPointF nearest = segment.first + delta * t;
            const double nearestDistance = distance(point, nearest);
            if (nearestDistance < bestDistance) {
                bestDistance = nearestDistance;
                result.point = nearest;
                result.kind = SnapKind::Nearest;
            }
        }
    }
    if (result.kind == SnapKind::None && snapToGrid) {
        result.point.setX(std::round(point.x() / gridSpacing) * gridSpacing);
        result.point.setY(std::round(point.y() / gridSpacing) * gridSpacing);
    }
    return result;
}

}
