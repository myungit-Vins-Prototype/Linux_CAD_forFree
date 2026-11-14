#pragma once
#include "cad_drawing_dimension.h"
#include "fk_curve_algo.h"
#include "fk_intersect.h"
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace ForgeCad {
struct DrawingLocalSectionSnapPoint {
    QPointF paper;
    QString label;
    std::size_t curve = 0;
    bool center = false;
    double gap = 0;
};
struct DrawingLocalSectionSnapGeometry {
    struct Edge { DrawingCurve curve; QRectF bounds; bool linear; QPointF first, second; };
    std::vector<DrawingLocalSectionSnapPoint> points;
    std::vector<Edge> edges;
};
inline std::shared_ptr<const DrawingLocalSectionSnapGeometry> drawingLocalSectionSnapGeometry(
    const DrawingView &view, const std::vector<DrawingCurve> &curves) {
    using namespace Kernel;
    auto result = std::make_shared<DrawingLocalSectionSnapGeometry>();
    const auto paper = [&](Vec3 point) {
        const auto p = view.plane.toLocal(point);
        return QPointF(p.x() * view.scale, -p.y() * view.scale);
    };
    const auto localPaper = [&](Vec2 p) { return QPointF(p.x() * view.scale, -p.y() * view.scale); };
    for (std::size_t index = 0; index < curves.size(); ++index) {
        const auto &curve = curves[index];
        const auto primitive = drawingPrimitive(view, curve);
        const auto a = curve.curve->point(curve.range.lo), b = curve.curve->point(curve.range.hi);
        QPointF first = paper(a), second = paper(b);
        if (primitive.linearProjection) {
            first = localPaper(primitive.lineFirst);
            second = localPaper(primitive.lineSecond);
        }
        const auto append = [&](QPointF point, const char *label, bool center = false) {
            result->points.push_back({point, QString::fromUtf8(label), index, center});
        };
        if (curve.kind != DrawingCurveKind::ApparentArc && QLineF(first, second).length() > view.tolerance * view.scale) {
            append(first, "Estremo");
            append(second, "Estremo");
        }
        const bool linear = primitive.kind == DrawingPrimitive::Kind::Line;
        if (linear)
            append(.5 * (first + second), "Punto medio");
        if (primitive.kind == DrawingPrimitive::Kind::Conic) {
            append(localPaper(primitive.center), "Centro", true);
            const double planeZ = view.plane.toLocal(a).z();
            for (int axis = 0; axis < 2; ++axis) {
                const double phase = std::atan2(primitive.secondAxis[axis], primitive.firstAxis[axis]);
                for (double angle : {phase, phase + kPi}) {
                    const auto p = primitive.center + std::cos(angle) * primitive.firstAxis + std::sin(angle) * primitive.secondAxis;
                    const auto world = view.plane.toGlobal(Vec3(p.x(), p.y(), planeZ));
                    if (projectPoint(*curve.curve, world, curve.range).distance <= view.tolerance)
                        append(localPaper(p), "Quadrante");
                }
            }
        }
        const auto box = curveBox(*curve.curve, curve.range);
        double left = std::numeric_limits<double>::infinity(), right = -left, top = left, bottom = -left;
        for (int mask = 0; mask < 8; ++mask) {
            const auto p = paper(Vec3(mask & 1 ? box.hi.x() : box.lo.x(), mask & 2 ? box.hi.y() : box.lo.y(),
                                      mask & 4 ? box.hi.z() : box.lo.z()));
            left = std::min(left, p.x()); right = std::max(right, p.x());
            top = std::min(top, p.y()); bottom = std::max(bottom, p.y());
        }
        result->edges.push_back({curve, QRectF(QPointF(left, top), QPointF(right, bottom)), linear, first, second});
    }
    return result;
}
// I pixel decidono solo se agganciare; la coordinata restituita proviene dalla curva esatta.
inline std::vector<DrawingLocalSectionSnapPoint> drawingLocalSectionSnapChoices(
    const DrawingLocalSectionSnapGeometry &geometry, const DrawingView &view, QPointF cursor, double zoom,
    const std::function<bool(std::size_t, QPointF, bool)> &visible = {}) {
    using namespace Kernel;
    std::vector<DrawingLocalSectionSnapPoint> result;
    const double radius = 9 / std::max(.01, zoom);
    const auto append = [&](DrawingLocalSectionSnapPoint point) {
        point.gap = QLineF(point.paper, cursor).length();
        if (point.gap > radius || (visible && !visible(point.curve, point.paper, point.center)))
            return;
        if (std::none_of(result.begin(), result.end(), [&](const auto &old) {
                return old.label == point.label && QLineF(old.paper, point.paper).length() <= view.tolerance * view.scale;
            }))
            result.push_back(std::move(point));
    };
    for (const auto &point : geometry.points)
        append(point);
    if (result.empty()) {
        for (std::size_t index = 0; index < geometry.edges.size(); ++index) {
            const auto &edge = geometry.edges[index];
            if (!edge.bounds.adjusted(-radius, -radius, radius, radius).contains(cursor))
                continue;
            QPointF point;
            if (edge.linear) {
                const auto delta = edge.second - edge.first;
                const double length2 = QPointF::dotProduct(delta, delta);
                if (length2 <= 0) continue;
                const double t = std::clamp(QPointF::dotProduct(cursor - edge.first, delta) / length2, 0., 1.);
                point = edge.first + t * delta;
            } else {
                const auto query = view.plane.toGlobal(Vec3(cursor.x() / view.scale, -cursor.y() / view.scale, 0));
                const auto nearest = projectPoint(*edge.curve.curve, query, edge.curve.range);
                const auto p = view.plane.toLocal(nearest.point);
                point = {p.x() * view.scale, -p.y() * view.scale};
            }
            append({point, QStringLiteral("Bordo"), index});
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.gap < b.gap; });
    return result;
}
} // namespace ForgeCad
