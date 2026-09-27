#include "cad_sketch_refs.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "cad_constraints.h"
#include "cad_curve_solver.h"
#include "cad_kernel.h"
#include "fk_boolean.h"
#include "fk_bspline.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_helix.h"
#include "fk_intersect.h"
#include "fk_nurbs.h"
#include "fk_sheet.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

void appendSegment(SketchObject &sketch, const QPointF &a, const QPointF &b, bool construction) {
    sketch.segments.append({a, b});
    sketch.constraints.append(-1);
    sketch.segmentLengths.append(0.0);
    sketch.segmentAngles.append(-1.0);
    if (construction) sketch.constructionSegments.append(sketch.segments.size() - 1);
}

// Toglie i livelli di TrimmedCurve (il tratto resta quello dato).
const Curve<3> *basisOf(const Curve<3> *curve) {
    while (curve && curve->type() == CurveType::Trimmed) curve = static_cast<const TrimmedCurve<3> *>(curve)->basis().get();
    return curve;
}

}

bool nearestBodyEdge(const Body &body, const Vec3 &point, CurvePtr<3> &curve, Interval &range) {
    double best = std::numeric_limits<double>::max();
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        if (!edge.curve) continue;
        const double d = projectPoint(*edge.curve, point, edge.range).distance;
        if (d < best) best = d, curve = edge.curve, range = edge.range;
    }
    return best < std::numeric_limits<double>::max();
}

QString appendProjectedCurve(SketchObject &sketch, const CurvePtr<3> &curve, const Interval &range, bool construction, bool fixed,
                             QVector<SketchEntity> *created) {
    if (!curve || !(range.hi > range.lo)) return QStringLiteral("Curva non valida.");
    const Frame3 frame = sketchAxes(sketch);
    const auto local = [&](const Vec3 &p) {
        const Vec3 q = frame.toLocal(p);
        return QPointF(q.x(), q.y());
    };
    const int segmentsBefore = sketch.segments.size(), curvesBefore = sketch.curves.size();
    try {
        const Curve<3> *basis = basisOf(curve.get());
        if (basis->type() == CurveType::Line) {
            const QPointF a = local(curve->point(range.lo)), b = local(curve->point(range.hi));
            if (std::hypot(b.x() - a.x(), b.y() - a.y()) <= kSketchConnectionTolerance)
                return QStringLiteral("Lo spigolo e' perpendicolare al piano dello schizzo: la sua proiezione e' un punto.");
            appendSegment(sketch, a, b, construction);
        } else if (basis->type() == CurveType::Circle
                   && std::fabs(std::fabs(dot(normalized(cross(static_cast<const Circle<3> *>(basis)->xAxis(), static_cast<const Circle<3> *>(basis)->yAxis())),
                                              frame.zDir())) - 1.0) <= 1e-12) {
            // Cerchio in un piano parallelo: cerchio o arco (antiorario attorno alla normale dello schizzo).
            const auto *circle = static_cast<const Circle<3> *>(basis);
            const Vec3 axis = normalized(cross(circle->xAxis(), circle->yAxis()));
            CurveObject object;
            object.construction = construction;
            const QPointF center = local(circle->center());
            if (range.hi - range.lo >= kTwoPi - 1e-12) {
                object.tool = DrawingTool::Circle;
                object.controlPoints = {center, local(curve->point(range.lo))};
            } else {
                object.tool = DrawingTool::Arc;
                const QPointF start = local(curve->point(range.lo)), end = local(curve->point(range.hi));
                const bool counterClockwise = dot(axis, frame.zDir()) > 0.0;
                object.controlPoints = {center, counterClockwise ? start : end, counterClockwise ? end : start};
            }
            recalculateCurve(object);
            sketch.curves.append(object);
        } else {
            // B-spline razionale esatta (le eliche e le altre curve "Other" dalla loro B-spline).
            BSplineCurve<3> spline(1, {0.0, 0.0, 1.0, 1.0}, {Vec3(), Vec3(1, 0, 0)});
            if (const auto *helix = dynamic_cast<const HelixCurve *>(basis)) {
                const BSplineCurve<3> full = helixBSpline(*helix);
                std::vector<BSplineCurve<3>> pieces = standardBezierPieces(full, range);
                int degree = 1;
                for (const BSplineCurve<3> &piece : pieces) degree = std::max(degree, piece.degree());
                spline = joinBezierPieces(standardBezierPieces(full, range, degree));
            } else {
                std::vector<BSplineCurve<3>> pieces = standardBezierPieces(*curve, range);
                int degree = 1;
                for (const BSplineCurve<3> &piece : pieces) degree = std::max(degree, piece.degree());
                spline = joinBezierPieces(standardBezierPieces(*curve, range, degree));
            }
            // Curva in un piano perpendicolare allo schizzo (i poli proiettati stanno
            // su una retta): la proiezione e' il segmento tra i punti estremi della curva lungo la retta.
            std::vector<QPointF> projected;
            for (const Vec3 &p : spline.poles()) projected.push_back(local(p));
            QPointF a = projected.front(), b = projected.front();
            double span = 0.0;
            for (const QPointF &p : projected)
                for (const QPointF &q : projected)
                    if (std::hypot(p.x() - q.x(), p.y() - q.y()) > span) span = std::hypot(p.x() - q.x(), p.y() - q.y()), a = p, b = q;
            bool collinear = span > kSketchConnectionTolerance;
            for (const QPointF &p : projected) {
                const QPointF d = b - a, r = p - a;
                collinear = collinear && std::fabs(d.x() * r.y() - d.y() * r.x()) / span <= 1e-9 * std::max(1.0, span);
            }
            if (collinear) {
                // Estremi di e . C(t) (e = la retta nel piano, in 3D): campioni e Newton sulle derivate esatte.
                const Vec3 e = normalized(frame.xDir() * ((b - a).x() / span) + frame.yDir() * ((b - a).y() / span));
                double lowest = std::numeric_limits<double>::max(), highest = -lowest;
                Vec3 low, high;
                std::vector<double> breaks = spline.breakpoints(spline.domain());
                for (std::size_t k = 0; k + 1 < breaks.size(); ++k)
                    for (int i = 0; i <= 32; ++i)
                        for (const double sign : {1.0, -1.0}) {
                            double t = breaks[k] + (breaks[k + 1] - breaks[k]) * i / 32.0;
                            for (int iteration = 0; iteration < 30; ++iteration) {
                                Vec3 d[3];
                                spline.evaluate(t, 2, d);
                                const double g = sign * dot(d[1], e), h = sign * dot(d[2], e);
                                if (!(h < 0.0)) break;  // cerca il massimo di sign * e . C
                                const double next = std::clamp(t - g / h, breaks[k], breaks[k + 1]);
                                if (std::fabs(next - t) <= 1e-15 * std::max(1.0, std::fabs(t))) break;
                                t = next;
                            }
                            const Vec3 p = spline.point(t);
                            const double v = dot(p, e);
                            if (v < lowest) lowest = v, low = p;
                            if (v > highest) highest = v, high = p;
                        }
                appendSegment(sketch, local(low), local(high), construction);
                goto done;
            }
            {
            CurveObject object;
            object.tool = DrawingTool::Converted;
            object.construction = construction;
            object.degree = spline.degree();
            for (const Vec3 &p : spline.poles()) object.controlPoints.append(local(p));
            for (double w : spline.weights()) object.weights.append(w);
            for (double k : spline.knots()) object.knots.append(k);
            recalculateCurve(object);
            if (!object.numericallyValid) return QStringLiteral("La proiezione della curva sul piano dello schizzo e' degenere.");
            sketch.curves.append(object);
            }
        }
    done:;
    } catch (const std::exception &failure) {
        return QStringLiteral("Proiezione non riuscita: %1").arg(QString::fromUtf8(failure.what()));
    }
    for (int i = segmentsBefore; i < sketch.segments.size(); ++i) {
        if (created) created->append({0, i});
        if (fixed) sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Fix, {{0, i, -1}}));
    }
    for (int i = curvesBefore; i < sketch.curves.size(); ++i) {
        if (created) created->append({1, i});
        if (fixed) sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Fix, {{1, i, -1}}));
    }
    return {};
}

QString appendSectionCurves(SketchObject &sketch, const Body &body, bool construction, bool fixed, QVector<SketchEntity> *created) {
    if (body.isSheet()) return QStringLiteral("La sezione vale per i solidi.");
    try {
        const Frame3 frame = sketchAxes(sketch);
        Box box;
        for (FaceId f : body.faces()) box.add(faceBox(body, f));
        const Vec3 middle = 0.5 * (box.lo + box.hi);
        const double half = 2.0 * std::max(1.0, box.diagonal()) + distance(frame.origin(), middle);
        // La parte del piano dentro il solido: i suoi bordi sono la sezione.
        const Body sheet = makePlaneSheet(frame, half);
        const Body inside = booleanOperation(sheet, body, Kernel::BooleanOperation::Intersect);
        int count = 0;
        for (EdgeId e : inside.edges()) {
            if (!inside.isLaminar(e)) continue;
            const Edge &edge = inside.edge(e);
            const QString error = appendProjectedCurve(sketch, edge.curve, edge.range, construction, fixed, created);
            if (!error.isEmpty()) return error;
            ++count;
        }
        if (count == 0) return QStringLiteral("Il piano dello schizzo non taglia il solido.");
    } catch (const std::exception &failure) {
        return QStringLiteral("Sezione non riuscita: %1").arg(QString::fromUtf8(failure.what()));
    }
    return {};
}

}
