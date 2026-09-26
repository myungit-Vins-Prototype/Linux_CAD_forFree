#include "cad_sketch_edit.h"

#include "cad_constraints.h"

#include <GCE2d_MakeSegment.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2dInt_GInter.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <IntRes2d_IntersectionPoint.hxx>
#include <IntRes2d_IntersectionSegment.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax2d.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "cad_curve_solver.h"
#include "cad_kernel.h"

namespace ForgeCad {
namespace {

constexpr double kTolerance = kSketchConnectionTolerance;
constexpr double kTwoPi = 2.0 * M_PI;

double length(const QPointF &p) { return std::hypot(p.x(), p.y()); }
double distance(const QPointF &a, const QPointF &b) { return length(b - a); }
double cross(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }
gp_Pnt2d toPnt(const QPointF &p) { return gp_Pnt2d(p.x(), p.y()); }
QPointF toPoint(const gp_Pnt2d &p) { return QPointF(p.X(), p.Y()); }

// Angolo riportato in [base, base + 2 pi).
double wrapAngle(double angle, double base) {
    double a = std::fmod(angle - base, kTwoPi);
    if (a < 0.0) a += kTwoPi;
    return base + a;
}

enum class Shape { Segment, Arc, Circle, Spline, Other };

// Geometria esatta di un'entita' nel suo parametro naturale: lunghezza
// d'arco sul segmento (da `start`), angolo su archi e cerchi, parametro
// della B-spline (un'unita' per tratto di Bezier) sulle spline.
struct Geometry {
    Shape shape = Shape::Other;
    Handle(Geom2d_Curve) basis;
    double first = 0.0, last = 0.0;
    QPointF start, end;
    QPointF center;
    double radius = 0.0;

    bool valid() const { return !basis.IsNull(); }
    bool closed() const { return shape == Shape::Circle; }
    QPointF value(double t) const { return toPoint(basis->Value(t)); }
    // Agli estremi i punti dell'entita', non ricalcolati.
    QPointF point(double t) const {
        if (!closed() && t == first) return start;
        if (!closed() && t == last) return end;
        return value(t);
    }
    Handle(Geom2d_Curve) bounded() const {
        if (closed()) return basis;
        return new Geom2d_TrimmedCurve(basis, first, last);
    }
};

Geometry geometryOf(const SketchObject &sketch, SketchEntity entity) {
    Geometry g;
    try {
        if (entity.kind == 0 && entity.index >= 0 && entity.index < sketch.segments.size()) {
            const SketchSegment &segment = sketch.segments.at(entity.index);
            const double l = distance(segment.first, segment.second);
            if (l <= kTolerance) return g;
            const QPointF u = (segment.second - segment.first) / l;
            g.shape = Shape::Segment;
            g.basis = new Geom2d_Line(toPnt(segment.first), gp_Dir2d(u.x(), u.y()));
            g.first = 0.0;
            g.last = l;
            g.start = segment.first;
            g.end = segment.second;
            return g;
        }
        if (entity.kind != 1 || entity.index < 0 || entity.index >= sketch.curves.size()) return g;
        const CurveObject &curve = sketch.curves.at(entity.index);
        if (curve.tool == DrawingTool::Arc && curve.controlPoints.size() >= 3) {
            // Come curveGeometry: raggio dal punto iniziale, angolo finale dal terzo punto, verso antiorario.
            const QPointF center = curve.controlPoints.at(0), start = curve.controlPoints.at(1), end = curve.controlPoints.at(2);
            const double r = distance(center, start);
            if (r <= kTolerance || distance(center, end) <= kTolerance) return g;
            const double a0 = std::atan2(start.y() - center.y(), start.x() - center.x());
            double a1 = std::atan2(end.y() - center.y(), end.x() - center.x());
            while (a1 <= a0 + 1e-12) a1 += kTwoPi;
            g.shape = Shape::Arc;
            g.basis = new Geom2d_Circle(gp_Ax2d(toPnt(center), gp_Dir2d(1.0, 0.0)), r);
            g.first = a0;
            g.last = a1;
            g.center = center;
            g.radius = r;
            g.start = start;
            g.end = g.value(a1);
            return g;
        }
        if (curve.tool == DrawingTool::Circle && curve.controlPoints.size() >= 2) {
            const QPointF center = curve.controlPoints.at(0);
            const double r = distance(center, curve.controlPoints.at(1));
            if (r <= kTolerance) return g;
            g.shape = Shape::Circle;
            g.basis = new Geom2d_Circle(gp_Ax2d(toPnt(center), gp_Dir2d(1.0, 0.0)), r);
            g.first = 0.0;
            g.last = kTwoPi;
            g.center = center;
            g.radius = r;
            return g;
        }
        if (curve.tool == DrawingTool::Spline) {
            const QVector<Handle(Geom2d_Curve)> pieces = curveGeometry(curve);
            if (pieces.size() != 1) return g;
            g.shape = Shape::Spline;
            g.basis = pieces.front();
            g.first = 0.0;
            g.last = double(curve.controlPoints.size() - 1);
            g.start = curve.controlPoints.front();
            g.end = curve.controlPoints.back();
            return g;
        }
    } catch (const Standard_Failure &) {
        return {};
    }
    return g;
}

// Bordi di taglio: tutte le altre entita' (anche di costruzione).
QVector<Handle(Geom2d_Curve)> cutters(const SketchObject &sketch, SketchEntity except) {
    QVector<Handle(Geom2d_Curve)> result;
    for (int index = 0; index < sketch.segments.size(); ++index) {
        if (except.kind == 0 && except.index == index) continue;
        const SketchSegment &segment = sketch.segments.at(index);
        if (distance(segment.first, segment.second) <= kTolerance) continue;
        try {
            result.append(GCE2d_MakeSegment(toPnt(segment.first), toPnt(segment.second)).Value());
        } catch (const Standard_Failure &) {
        }
    }
    for (int index = 0; index < sketch.curves.size(); ++index) {
        if (except.kind == 1 && except.index == index) continue;
        for (const Handle(Geom2d_Curve) &curve : curveGeometry(sketch.curves.at(index))) result.append(curve);
    }
    return result;
}

// Parametro di un punto dell'entita' (per rette e cerchi in forma chiusa).
double parameterOf(const Geometry &g, const QPointF &p, double occtParameter) {
    switch (g.shape) {
    case Shape::Segment: {
        const QPointF u = (g.end - g.start) / distance(g.start, g.end);
        return std::clamp(QPointF::dotProduct(p - g.start, u), g.first, g.last);
    }
    case Shape::Arc:
    case Shape::Circle:
        return wrapAngle(std::atan2(p.y() - g.center.y(), p.x() - g.center.x()), g.first);
    default:
        return std::clamp(occtParameter, g.first, g.last);
    }
}

// Parametri delle intersezioni con le altre entita', ordinati e senza doppi.
// Sulle entita' aperte solo quelli interni (non sugli estremi).
QVector<double> crossings(const Geometry &g, const QVector<Handle(Geom2d_Curve)> &others) {
    QVector<double> params;
    const Handle(Geom2d_Curve) curve = g.bounded();
    for (const Handle(Geom2d_Curve) &other : others) {
        try {
            Geom2dAPI_InterCurveCurve intersection(curve, other, 1e-9);
            const Geom2dInt_GInter &result = intersection.Intersector();
            if (!result.IsDone()) continue;
            for (int i = 1; i <= result.NbPoints(); ++i)
                params.append(parameterOf(g, toPoint(result.Point(i).Value()), result.Point(i).ParamOnFirst()));
            for (int i = 1; i <= result.NbSegments(); ++i) {
                const IntRes2d_IntersectionSegment &segment = result.Segment(i);
                if (segment.HasFirstPoint())
                    params.append(parameterOf(g, toPoint(segment.FirstPoint().Value()), segment.FirstPoint().ParamOnFirst()));
                if (segment.HasLastPoint())
                    params.append(parameterOf(g, toPoint(segment.LastPoint().Value()), segment.LastPoint().ParamOnFirst()));
            }
        } catch (const Standard_Failure &) {
        }
    }
    std::sort(params.begin(), params.end());
    QVector<double> unique;
    for (double t : params) {
        const QPointF p = g.value(t);
        if (!g.closed() && (distance(p, g.start) <= kTolerance || distance(p, g.end) <= kTolerance)) continue;
        if (!unique.isEmpty() && distance(g.value(unique.back()), p) <= kTolerance) continue;
        unique.append(t);
    }
    // Sul cerchio il primo e l'ultimo possono essere lo stesso punto.
    if (g.closed() && unique.size() > 1 && distance(g.value(unique.front()), g.value(unique.back())) <= kTolerance) unique.removeLast();
    return unique;
}

// Parametro del punto dell'entita' piu' vicino a `p`.
double project(const Geometry &g, const QPointF &p) {
    switch (g.shape) {
    case Shape::Segment:
        return parameterOf(g, p, 0.0);
    case Shape::Circle:
        return parameterOf(g, p, 0.0);
    case Shape::Arc: {
        const double t = parameterOf(g, p, 0.0);
        if (t <= g.last) return t;
        // Fuori dall'arco: l'estremo angolarmente piu' vicino.
        return t - g.last < g.first + kTwoPi - t ? g.last : g.first;
    }
    default: {
        double best = distance(p, g.start) <= distance(p, g.end) ? g.first : g.last;
        double bestDistance = std::min(distance(p, g.start), distance(p, g.end));
        try {
            Geom2dAPI_ProjectPointOnCurve projection(toPnt(p), g.basis, g.first, g.last);
            if (projection.NbPoints() > 0 && projection.LowerDistance() < bestDistance) best = projection.LowerDistanceParameter();
        } catch (const Standard_Failure &) {
        }
        return best;
    }
    }
}

// Tratto [a, b] di [0, 1] di una Bezier cubica, esatto (de Casteljau). Le
// combinazioni (1 - t) p + t q danno i punti estremi esatti per t = 0 e 1.
using Bezier = std::array<QPointF, 4>;

QPointF mix(const QPointF &p, const QPointF &q, double t) { return (1.0 - t) * p + t * q; }

void splitBezier(const Bezier &p, double t, Bezier &left, Bezier &right) {
    const QPointF p01 = mix(p[0], p[1], t), p12 = mix(p[1], p[2], t), p23 = mix(p[2], p[3], t);
    const QPointF p012 = mix(p01, p12, t), p123 = mix(p12, p23, t);
    const QPointF p0123 = mix(p012, p123, t);
    left = {p[0], p01, p012, p0123};
    right = {p0123, p123, p23, p[3]};
}

Bezier bezierPart(const Bezier &p, double a, double b) {
    Bezier left = p, right, unused;
    if (b < 1.0) splitBezier(p, b, left, unused);
    if (a <= 0.0) return left;
    splitBezier(left, a / b, unused, right);
    return right;
}

// Tratto [lo, hi] (parametri della B-spline) di una spline dello schizzo: i
// tratti di Bezier che lo coprono, ristretti. Stessa curva, stesso tipo.
CurveObject splineRange(const CurveObject &curve, double lo, double hi) {
    const int pieces = curve.controlPoints.size() - 1;
    QVector<Bezier> parts;
    for (int k = std::max(0, int(std::floor(lo))); k < pieces && double(k) < hi; ++k) {
        const double a = std::max(lo - k, 0.0), b = std::min(hi - k, 1.0);
        if (b - a <= 1e-12) continue;
        const Bezier bezier{curve.controlPoints.at(k), curve.tangentHandles.at(k).second,
                            curve.tangentHandles.at(k + 1).first, curve.controlPoints.at(k + 1)};
        parts.append(bezierPart(bezier, a, b));
    }
    CurveObject result;
    result.tool = DrawingTool::Spline;
    result.construction = curve.construction;
    for (int k = 0; k < parts.size(); ++k) {
        const Bezier &part = parts.at(k);
        const QPointF in = k == 0 ? 2.0 * part[0] - part[1] : parts.at(k - 1)[2];
        result.controlPoints.append(part[0]);
        result.tangentHandles.append({in, part[1]});
    }
    if (!parts.isEmpty()) {
        const Bezier &part = parts.back();
        result.controlPoints.append(part[3]);
        result.tangentHandles.append({part[2], 2.0 * part[3] - part[2]});
    }
    return result;
}

// Un tratto di un'entita' come entita' nuova dello stesso tipo (un cerchio
// o un arco danno un arco).
struct Piece {
    bool segment = true;
    SketchSegment line;
    CurveObject curve;
};

Piece pieceOf(const SketchObject &sketch, SketchEntity entity, const Geometry &g, double lo, double hi) {
    Piece piece;
    if (g.shape == Shape::Segment) {
        piece.line = {g.point(lo), g.point(hi)};
        return piece;
    }
    piece.segment = false;
    const CurveObject &source = sketch.curves.at(entity.index);
    if (g.shape == Shape::Spline) {
        piece.curve = splineRange(source, lo, hi);
        return piece;
    }
    piece.curve.tool = DrawingTool::Arc;
    piece.curve.construction = source.construction;
    piece.curve.controlPoints = {g.center, g.point(lo), g.point(hi)};
    return piece;
}

// Punto `point` di un'entita' come nei vincoli di coincidenza.
bool constraintPoint(const SketchObject &sketch, int kind, int element, int point, QPointF &out) {
    if (kind == 0) {
        if (element < 0 || element >= sketch.segments.size() || point < 0 || point > 1) return false;
        out = point == 0 ? sketch.segments.at(element).first : sketch.segments.at(element).second;
        return true;
    }
    if (element < 0 || element >= sketch.curves.size()) return false;
    const QVector<QPointF> &points = sketch.curves.at(element).controlPoints;
    if (point < 0 || point >= points.size()) return false;
    out = points.at(point);
    return true;
}

// Estremi di un'entita' (indici nei vincoli di coincidenza).
QVector<int> endPoints(const SketchObject &sketch, SketchEntity entity) {
    if (entity.kind == 0) return {0, 1};
    const CurveObject &curve = sketch.curves.at(entity.index);
    if (curve.tool == DrawingTool::Arc) return {1, 2};
    if ((curve.tool == DrawingTool::Spline || curve.tool == DrawingTool::Nurbs) && curve.controlPoints.size() >= 2)
        return {0, int(curve.controlPoints.size()) - 1};
    return {};
}

// Vincoli delle entita' toccate: restano quelli ancora soddisfatti (per
// esempio un segmento accorciato resta orizzontale); si aggiungono le
// coincidenze dei loro estremi con i punti delle altre entita'.
void refreshCoincidences(SketchObject &sketch, const QVector<SketchEntity> &touched) {
    auto isTouched = [&](const ConstraintRef &r) {
        for (const SketchEntity &e : touched)
            if (e.kind == r.kind && e.index == r.element) return true;
        return false;
    };
    QVector<SketchConstraint> kept;
    for (const SketchConstraint &c : sketch.geometricConstraints) {
        if ((isTouched(c.first) || isTouched(c.second)) && constraintError(sketch, c) > kTolerance) continue;
        kept.append(c);
    }
    auto exists = [&](const ConstraintRef &a, const ConstraintRef &b) {
        for (const SketchConstraint &k : kept)
            if (k.type == ConstraintType::Coincident && ((k.first == a && k.second == b) || (k.first == b && k.second == a))) return true;
        return false;
    };
    for (const SketchEntity &entity : touched) {
        for (int end : endPoints(sketch, entity)) {
            QPointF p;
            if (!constraintPoint(sketch, entity.kind, entity.index, end, p)) continue;
            const ConstraintRef here{entity.kind, entity.index, end};
            auto add = [&](const ConstraintRef &other) {
                if (exists(here, other)) return;
                SketchConstraint c;
                c.type = ConstraintType::Coincident;
                c.first = here;
                c.second = other;
                kept.append(c);
            };
            for (int other = 0; other < sketch.segments.size(); ++other) {
                if (entity.kind == 0 && other == entity.index) continue;
                for (int k = 0; k < 2; ++k) {
                    const QPointF q = k == 0 ? sketch.segments.at(other).first : sketch.segments.at(other).second;
                    if (distance(p, q) <= kTolerance) add({0, other, k});
                }
            }
            for (int other = 0; other < sketch.curves.size(); ++other) {
                if (entity.kind == 1 && other == entity.index) continue;
                const QVector<QPointF> &points = sketch.curves.at(other).controlPoints;
                for (int k = 0; k < points.size(); ++k)
                    if (distance(p, points.at(k)) <= kTolerance) add({1, other, k});
            }
        }
    }
    sketch.geometricConstraints = kept;
}

void appendSegment(SketchObject &sketch, const SketchSegment &segment, int constraint, double angle, bool construction) {
    sketch.segments.append(segment);
    sketch.constraints.append(constraint);
    sketch.segmentLengths.append(0.0);
    sketch.segmentAngles.append(angle);
    if (construction) sketch.constructionSegments.append(sketch.segments.size() - 1);
}

// L'entita' sostituita dai suoi tratti (il primo al suo posto, gli altri in
// fondo) o eliminata se non ce ne sono. Restituisce la mappa dei segmenti.
QVector<int> replaceWithPieces(SketchObject &sketch, SketchEntity entity, const QVector<Piece> &pieces, QVector<SketchEntity> &touched) {
    if (pieces.isEmpty()) {
        QSet<int> segments, curves;
        (entity.kind == 0 ? segments : curves).insert(entity.index);
        return removeSketchEntities(sketch, segments, curves);
    }
    for (int k = 0; k < pieces.size(); ++k) {
        const Piece &piece = pieces.at(k);
        if (piece.segment) {
            if (k == 0) {
                sketch.segments[entity.index] = piece.line;
                // La lunghezza e' cambiata: la quota non vale piu'.
                if (entity.index < sketch.segmentLengths.size()) sketch.segmentLengths[entity.index] = 0.0;
                touched.append(entity);
            } else {
                appendSegment(sketch, piece.line, sketch.constraints.value(entity.index, -1), sketch.segmentAngles.value(entity.index, -1.0),
                              sketch.isConstructionSegment(entity.index));
                const int created = int(sketch.segments.size()) - 1;
                // I vincoli di direzione del segmento valgono anche per i suoi pezzi.
                const ConstraintRef original{0, entity.index, -1}, piece{0, created, -1};
                const QVector<SketchConstraint> existing = sketch.geometricConstraints;
                for (SketchConstraint c : existing) {
                    const bool direction = c.type == ConstraintType::Horizontal || c.type == ConstraintType::Vertical || c.type == ConstraintType::Parallel
                                        || c.type == ConstraintType::Perpendicular || c.type == ConstraintType::Collinear;
                    if (!direction || (c.first != original && c.second != original)) continue;
                    if (c.first == original) c.first = piece;
                    else c.second = piece;
                    sketch.geometricConstraints.append(c);
                }
                touched.append({0, created});
            }
        } else if (k == 0) {
            sketch.curves[entity.index] = piece.curve;
            touched.append(entity);
        } else {
            sketch.curves.append(piece.curve);
            touched.append({1, int(sketch.curves.size()) - 1});
        }
    }
    return {};
}

// Un poligono si scompone nei suoi lati (segmenti uniti agli estremi);
// l'entita' diventa il lato piu' vicino a `pick`.
QString explodePolygon(SketchObject &sketch, SketchEntity &entity, const QPointF &pick) {
    if (entity.kind != 1 || entity.index < 0 || entity.index >= sketch.curves.size()) return {};
    const CurveObject curve = sketch.curves.at(entity.index);
    if (curve.tool != DrawingTool::Polygon) return {};
    const QVector<Handle(Geom2d_Curve)> sides = curveGeometry(curve);
    if (sides.isEmpty()) return QStringLiteral("Il poligono non e' valido.");
    const int firstNew = sketch.segments.size();
    QVector<SketchEntity> touched;
    for (const Handle(Geom2d_Curve) &side : sides) {
        const QPointF a = toPoint(side->Value(side->FirstParameter())), b = toPoint(side->Value(side->LastParameter()));
        appendSegment(sketch, {a, b}, -1, -1.0, curve.construction);
        touched.append({0, int(sketch.segments.size()) - 1});
    }
    // Estremi dei lati consecutivi esattamente uguali.
    for (int k = firstNew; k < sketch.segments.size(); ++k) {
        const int next = k + 1 < sketch.segments.size() ? k + 1 : firstNew;
        sketch.segments[next].first = sketch.segments.at(k).second;
    }
    removeSketchEntities(sketch, {}, {entity.index});
    refreshCoincidences(sketch, touched);
    double best = std::numeric_limits<double>::max();
    for (int k = firstNew; k < sketch.segments.size(); ++k) {
        const SketchSegment &s = sketch.segments.at(k);
        const QPointF d = s.second - s.first;
        const double t = std::clamp(QPointF::dotProduct(pick - s.first, d) / QPointF::dotProduct(d, d), 0.0, 1.0);
        const double dist = distance(pick, s.first + t * d);
        if (dist < best) {
            best = dist;
            entity = {0, k};
        }
    }
    return {};
}

QString unsupported(const SketchObject &sketch, SketchEntity entity, const QString &operation) {
    if (entity.kind == 1 && entity.index >= 0 && entity.index < sketch.curves.size() && sketch.curves.at(entity.index).tool == DrawingTool::Nurbs)
        return QStringLiteral("%1: le NURBS non si possono modificare (fanno solo da bordo).").arg(operation);
    if (entity.kind == 1 && entity.index >= 0 && entity.index < sketch.curves.size() && sketch.curves.at(entity.index).tool == DrawingTool::Ellipse)
        return QStringLiteral("%1: le ellissi non si possono tagliare (fanno solo da bordo).").arg(operation);
    return QStringLiteral("%1: entita' non valida.").arg(operation);
}

// Tratto che il taglio toglie: [lo, hi] (sul cerchio hi puo' superare 2 pi).
struct TrimSpan {
    double lo = 0.0, hi = 0.0;
    bool all = false;
};

TrimSpan trimSpan(const SketchObject &sketch, SketchEntity entity, const Geometry &g, const QPointF &pick) {
    const QVector<double> params = crossings(g, cutters(sketch, entity));
    const double t = project(g, pick);
    TrimSpan span;
    if (g.closed()) {
        if (params.size() < 2) {
            span = {g.first, g.last, true};
            return span;
        }
        span.lo = params.back() - kTwoPi;
        span.hi = params.front();
        for (double p : params) {
            if (p < t) span.lo = p;
            else if (p > t) {
                span.hi = p;
                break;
            }
        }
        if (span.hi < span.lo) span.hi += kTwoPi;
        return span;
    }
    span.lo = g.first;
    span.hi = g.last;
    for (double p : params) {
        if (p < t) span.lo = p;
        else if (p > t) {
            span.hi = p;
            break;
        }
    }
    span.all = span.lo == g.first && span.hi == g.last;
    return span;
}

}

QVector<int> removeSketchEntities(SketchObject &sketch, const QSet<int> &segments, const QSet<int> &curves) {
    auto remap = [](const QSet<int> &removed, int count) {
        QVector<int> map(count, -1);
        int next = 0;
        for (int index = 0; index < count; ++index)
            if (!removed.contains(index)) map[index] = next++;
        return map;
    };
    const QVector<int> segmentMap = remap(segments, sketch.segments.size()), curveMap = remap(curves, sketch.curves.size());
    auto compact = [](auto &array, const QVector<int> &map) {
        std::remove_reference_t<decltype(array)> kept;
        for (int index = 0; index < array.size(); ++index)
            if (index < map.size() && map.at(index) >= 0) kept.append(array.at(index));
        array = kept;
    };
    compact(sketch.segments, segmentMap);
    compact(sketch.constraints, segmentMap);
    compact(sketch.segmentLengths, segmentMap);
    compact(sketch.segmentAngles, segmentMap);
    compact(sketch.curves, curveMap);
    QVector<int> construction;
    for (int index : sketch.constructionSegments)
        if (index >= 0 && index < segmentMap.size() && segmentMap.at(index) >= 0) construction.append(segmentMap.at(index));
    sketch.constructionSegments = construction;
    remapConstraints(sketch, segmentMap, curveMap);
    return segmentMap;
}

SketchEditResult trimSketchEntity(SketchObject &sketch, SketchEntity entity, const QPointF &pick) {
    SketchObject work = sketch;
    SketchEditResult result;
    result.error = explodePolygon(work, entity, pick);
    if (!result.error.isEmpty()) return result;
    const Geometry g = geometryOf(work, entity);
    if (!g.valid()) return {unsupported(work, entity, QStringLiteral("Taglia")), {}};
    const TrimSpan span = trimSpan(work, entity, g, pick);
    QVector<Piece> pieces;
    if (!span.all) {
        if (g.closed()) {
            pieces.append(pieceOf(work, entity, g, span.hi, span.lo + kTwoPi));
        } else {
            if (span.lo > g.first) pieces.append(pieceOf(work, entity, g, g.first, span.lo));
            if (span.hi < g.last) pieces.append(pieceOf(work, entity, g, span.hi, g.last));
        }
    }
    QVector<SketchEntity> touched;
    result.segmentMap = replaceWithPieces(work, entity, pieces, touched);
    refreshCoincidences(work, touched);
    sketch = work;
    return result;
}

QVector<QPointF> trimPreview(const SketchObject &sketch, SketchEntity entity, const QPointF &pick) {
    SketchObject work = sketch;
    if (!explodePolygon(work, entity, pick).isEmpty()) return {};
    const Geometry g = geometryOf(work, entity);
    if (!g.valid()) return {};
    const TrimSpan span = trimSpan(work, entity, g, pick);
    QVector<QPointF> samples;
    const int count = g.shape == Shape::Segment ? 1 : 64;
    for (int k = 0; k <= count; ++k) samples.append(g.value(span.lo + (span.hi - span.lo) * double(k) / double(count)));
    return samples;
}

SketchEditResult extendSketchEntity(SketchObject &sketch, SketchEntity entity, const QPointF &pick) {
    SketchObject work = sketch;
    SketchEditResult result;
    result.error = explodePolygon(work, entity, pick);
    if (!result.error.isEmpty()) return result;
    const Geometry g = geometryOf(work, entity);
    if (!g.valid()) return {unsupported(work, entity, QStringLiteral("Estendi")), {}};
    if (g.shape == Shape::Circle || g.shape == Shape::Spline)
        return {QStringLiteral("Estendi: si estendono solo segmenti e archi."), {}};
    const bool atEnd = distance(pick, g.end) < distance(pick, g.start);
    const QVector<Handle(Geom2d_Curve)> others = cutters(work, entity);
    if (g.shape == Shape::Segment) {
        const QPointF origin = atEnd ? g.end : g.start;
        const QPointF direction = (atEnd ? g.end - g.start : g.start - g.end) / (g.last - g.first);
        Geometry ray;
        ray.shape = Shape::Segment;
        ray.basis = new Geom2d_Line(toPnt(origin), gp_Dir2d(direction.x(), direction.y()));
        ray.first = 0.0;
        ray.last = 2.0e6;  // oltre il size box (1 km)
        ray.start = origin;
        ray.end = ray.value(ray.last);
        const QVector<double> params = crossings(ray, others);
        if (params.isEmpty()) return {QStringLiteral("Estendi: nessuna entita' da raggiungere in quella direzione."), {}};
        const QPointF target = origin + params.front() * direction;
        SketchSegment &segment = work.segments[entity.index];
        (atEnd ? segment.second : segment.first) = target;
        if (entity.index < work.segmentLengths.size()) work.segmentLengths[entity.index] = 0.0;
    } else {
        Geometry circle = g;
        circle.shape = Shape::Circle;
        circle.first = 0.0;
        circle.last = kTwoPi;
        const QVector<double> params = crossings(circle, others);
        const double gap = kTwoPi - (g.last - g.first);
        const double tiny = kTolerance / g.radius;
        double best = gap - tiny;
        bool found = false;
        for (double angle : params) {
            const double delta = atEnd ? wrapAngle(angle - g.last, 0.0) : wrapAngle(g.first - angle, 0.0);
            if (delta > tiny && delta < best) {
                best = delta;
                found = true;
            }
        }
        if (!found) return {QStringLiteral("Estendi: nessuna entita' da raggiungere lungo l'arco."), {}};
        CurveObject &arc = work.curves[entity.index];
        if (atEnd) arc.controlPoints[2] = g.value(g.last + best);
        else arc.controlPoints[1] = g.value(g.first - best);
    }
    refreshCoincidences(work, {entity});
    sketch = work;
    return result;
}

SketchEditResult splitSketchEntity(SketchObject &sketch, SketchEntity entity, const QPointF &point, double snapTolerance) {
    SketchObject work = sketch;
    SketchEditResult result;
    result.error = explodePolygon(work, entity, point);
    if (!result.error.isEmpty()) return result;
    const Geometry g = geometryOf(work, entity);
    if (!g.valid()) return {unsupported(work, entity, QStringLiteral("Spezza")), {}};
    double t = project(g, point);
    // Aggancio alle intersezioni e al punto medio dei segmenti.
    QVector<double> snaps = crossings(g, cutters(work, entity));
    if (g.shape == Shape::Segment) snaps.append(0.5 * (g.first + g.last));
    double bestDistance = snapTolerance;
    const QPointF projected = g.value(t);
    for (double s : snaps) {
        const double d = distance(g.value(s), projected);
        if (d <= bestDistance) {
            bestDistance = d;
            t = s;
        }
    }
    QVector<Piece> pieces;
    if (g.closed()) {
        pieces.append(pieceOf(work, entity, g, t, t + kTwoPi));
    } else {
        const QPointF p = g.value(t);
        if (distance(p, g.start) <= kTolerance || distance(p, g.end) <= kTolerance)
            return {QStringLiteral("Spezza: il punto e' un estremo dell'entita'."), {}};
        pieces.append(pieceOf(work, entity, g, g.first, t));
        pieces.append(pieceOf(work, entity, g, t, g.last));
        // I due tratti si toccano esattamente nello stesso punto.
        if (pieces[0].segment) pieces[1].line.first = pieces[0].line.second;
        else if (g.shape == Shape::Arc) pieces[1].curve.controlPoints[1] = pieces[0].curve.controlPoints[2];
        else pieces[1].curve.controlPoints[0] = pieces[0].curve.controlPoints.back();
    }
    QVector<SketchEntity> touched;
    replaceWithPieces(work, entity, pieces, touched);
    refreshCoincidences(work, touched);
    sketch = work;
    return result;
}

SketchEditResult blendSketchSegments(SketchObject &sketch, int first, const QPointF &pickFirst, int second,
                                     const QPointF &pickSecond, double size, bool chamfer) {
    const QString operation = chamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo");
    if (first == second || first < 0 || second < 0 || first >= sketch.segments.size() || second >= sketch.segments.size())
        return {QStringLiteral("%1: scegli due segmenti diversi.").arg(operation), {}};
    if (!(size > 0.0)) return {QStringLiteral("%1: la misura deve essere positiva.").arg(operation), {}};
    SketchObject work = sketch;
    const SketchSegment a = work.segments.at(first), b = work.segments.at(second);
    const QPointF da = a.second - a.first, db = b.second - b.first;
    const double la = length(da), lb = length(db);
    if (la <= kTolerance || lb <= kTolerance) return {QStringLiteral("%1: segmento degenere.").arg(operation), {}};
    const double denominator = cross(da, db);
    if (std::abs(denominator) <= 1e-12 * la * lb) return {QStringLiteral("%1: i segmenti sono paralleli.").arg(operation), {}};
    // Spigolo: punto comune alle due rette (parametri ta, tb in [0, 1] sui segmenti).
    const QPointF r = b.first - a.first;
    const double ta = cross(r, db) / denominator, tb = cross(r, da) / denominator;
    QPointF corner = a.first + ta * da;
    // Se gli estremi coincidono gia' lo spigolo e' quel punto, esatto.
    for (const QPointF &p : {a.first, a.second})
        for (const QPointF &q : {b.first, b.second})
            if (distance(p, q) <= kTolerance && distance(p, corner) <= 10.0 * kTolerance) corner = p;
    // Parte da tenere: dalla parte del punto scelto rispetto allo spigolo.
    const double pa = QPointF::dotProduct(pickFirst - a.first, da) / (la * la);
    const double pb = QPointF::dotProduct(pickSecond - b.first, db) / (lb * lb);
    const bool keepEndA = pa > ta, keepEndB = pb > tb;
    const QPointF ua = (keepEndA ? da : -da) / la, ub = (keepEndB ? db : -db) / lb;
    const double availableA = (keepEndA ? 1.0 - ta : ta) * la, availableB = (keepEndB ? 1.0 - tb : tb) * lb;
    const double cosine = std::clamp(QPointF::dotProduct(ua, ub), -1.0, 1.0);
    const double half = 0.5 * std::acos(cosine);
    if (half <= 1e-9 || half >= M_PI_2 - 1e-9) return {QStringLiteral("%1: i segmenti sono allineati.").arg(operation), {}};
    const double d = chamfer ? size : size / std::tan(half);
    if (d >= availableA - kTolerance || d >= availableB - kTolerance)
        return {QStringLiteral("%1: la misura e' troppo grande per questi segmenti (tangenza a %2).").arg(operation).arg(d), {}};
    const QPointF tangentA = corner + d * ua, tangentB = corner + d * ub;
    (keepEndA ? work.segments[first].first : work.segments[first].second) = tangentA;
    (keepEndB ? work.segments[second].first : work.segments[second].second) = tangentB;
    if (first < work.segmentLengths.size()) work.segmentLengths[first] = 0.0;
    if (second < work.segmentLengths.size()) work.segmentLengths[second] = 0.0;
    const bool construction = work.isConstructionSegment(first) && work.isConstructionSegment(second);
    QVector<SketchEntity> touched{{0, first}, {0, second}};
    if (chamfer) {
        appendSegment(work, {tangentA, tangentB}, -1, -1.0, construction);
        touched.append({0, int(work.segments.size()) - 1});
    } else {
        const QPointF bisector = (ua + ub) / length(ua + ub);
        const QPointF center = corner + (size / std::sin(half)) * bisector;
        CurveObject arc;
        arc.tool = DrawingTool::Arc;
        arc.construction = construction;
        // Verso antiorario: l'arco (meno di mezzo giro) va da un punto di tangenza all'altro.
        const bool forward = cross(tangentA - center, tangentB - center) > 0.0;
        arc.controlPoints = {center, forward ? tangentA : tangentB, forward ? tangentB : tangentA};
        work.curves.append(arc);
        touched.append({1, int(work.curves.size()) - 1});
    }
    refreshCoincidences(work, touched);
    if (!chamfer) {
        // Il raccordo resta tangente ai due segmenti.
        for (int segment : {first, second}) {
            SketchConstraint tangent;
            tangent.type = ConstraintType::Tangent;
            tangent.first = {0, segment, -1};
            tangent.second = {1, int(work.curves.size()) - 1, -1};
            work.geometricConstraints.append(tangent);
        }
    }
    sketch = work;
    return {};
}

bool sketchCornerAt(const SketchObject &sketch, const QPointF &point, double tolerance, int &first, QPointF &pickFirst,
                    int &second, QPointF &pickSecond) {
    double best = tolerance;
    bool found = false;
    for (int i = 0; i < sketch.segments.size(); ++i) {
        for (int end = 0; end < 2; ++end) {
            const QPointF vertex = end == 0 ? sketch.segments.at(i).first : sketch.segments.at(i).second;
            const double d = distance(point, vertex);
            if (d > best) continue;
            // Segmenti con un estremo nel vertice: devono essere esattamente due.
            QVector<QPair<int, QPointF>> incident;
            for (int j = 0; j < sketch.segments.size(); ++j) {
                const SketchSegment &s = sketch.segments.at(j);
                if (distance(s.first, s.second) <= kTolerance) continue;
                if (distance(s.first, vertex) <= kTolerance) incident.append({j, s.second});
                else if (distance(s.second, vertex) <= kTolerance) incident.append({j, s.first});
            }
            if (incident.size() != 2) continue;
            best = d;
            found = true;
            first = incident.at(0).first;
            pickFirst = incident.at(0).second;
            second = incident.at(1).first;
            pickSecond = incident.at(1).second;
        }
    }
    return found;
}

}
