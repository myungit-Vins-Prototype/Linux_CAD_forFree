#include "cad_snap.h"

#include <cmath>
#include "cad_curve_solver.h"
#include "cad_kernel.h"
#include "cad_sketch_refs.h"
#include "fk_curve_algo.h"
#include "fk_intersect.h"

namespace ForgeCad {

namespace {
double distance(const QPointF &a, const QPointF &b) { return std::hypot(b.x() - a.x(), b.y() - a.y()); }
}

PointAttachment pointAttachment(const QPointF &point, const SketchObject &local,
                                const SketchObject &external, double tolerance, bool origin,
                                const QVector<ConstraintRef> &excluded) {
    if (origin && distance(point, {}) <= tolerance)
        return {{2,0,-1}, ConstraintType::Coincident, 0.0, -1, false};
    PointAttachment result;
    double bestDistance=tolerance;
    const auto append = [&](const SketchObject &sketch, bool isExternal) {
        if (result.priority==0) return;
        const auto skip = [&](int kind, int element) {
            if (isExternal) return false;
            for (const auto &ref : excluded)
                if (ref.kind == kind && ref.element == element) return true;
            return false;
        };
        const auto consider = [&](ConstraintRef ref, ConstraintType type, double d, int priority, double value = 0.0) {
            if (d > tolerance) return;
            // A parita' di tipo vince il punto piu vicino; a pari distanza resta il primo.
            if (priority > result.priority || (priority==result.priority && d>=bestDistance)) return;
            bestDistance=d;
            result = {ref, type, value, priority, isExternal};
        };
        for (int i = 0; i < sketch.segments.size(); ++i) {
            if (skip(0,i)) continue;
            const auto &s = sketch.segments[i];
            consider({0,i,0},ConstraintType::Coincident,distance(point,s.first),0);
            consider({0,i,1},ConstraintType::Coincident,distance(point,s.second),0);
            consider({0,i,-1},ConstraintType::Midpoint,distance(point,(s.first+s.second)*0.5),2);
            const QPointF d=s.second-s.first;
            const double d2=QPointF::dotProduct(d,d);
            if (d2>0.0) {
                const double t=qBound(0.0,QPointF::dotProduct(point-s.first,d)/d2,1.0);
                consider({0,i,-1},ConstraintType::PointOnCurve,distance(point,s.first+t*d),3);
            }
        }
        for (int i = 0; i < sketch.curves.size(); ++i) {
            if (skip(1,i)) continue;
            const auto &curve=sketch.curves[i];
            for (int k=0;k<curve.controlPoints.size();++k) {
                if ((curve.tool==DrawingTool::Circle || curve.tool==DrawingTool::Polygon) && k>0) continue;
                if (curve.tool==DrawingTool::Converted && k!=0 && k!=curve.controlPoints.size()-1) continue;
                consider({1,i,k},ConstraintType::Coincident,distance(point,curve.controlPoints[k]),0);
            }
            for (const auto &q:curveQuadrants(curve))
                consider({1,i,-1},ConstraintType::Quadrant,distance(point,q.second),1,q.first);
            if (curve.tool==DrawingTool::Polygon || result.priority<3) continue;
            try {
                for (const auto &span:curveGeometry(curve))
                    consider({1,i,-1},ConstraintType::PointOnCurve,
                        Kernel::projectPoint(*span.curve,{point.x(),point.y()},span.range).distance,3);
            } catch (const std::exception &) { /* geometria ancora incompleta */ }
        }
    };
    append(local,false); append(external,true);
    return result;
}

ConstraintRef resolvePointAttachment(SketchObject &sketch, const PointAttachment &attachment,
                                     const SketchObject &external, const QVector<SketchObject> &sketches,
                                     const QVector<ExtrusionObject> &bodies) {
    const ConstraintRef target=attachment.target;
    if (!attachment.external) return target;
    for (const auto &link:external.linkedReferences) if (link)
        for (int k=0;k<link->entities.size();++k) {
            const auto &entity=link->entities[k];
            if (entity.kind!=target.kind || entity.element!=target.element) continue;
            QVector<SketchEntity> created;
            if (appendLinkedSketchReference(sketch,link->source,sketches,bodies,link->contact,
                link->construction,link->halfLength,&created).isEmpty() && k<created.size())
                return {target.kind,created[k].index,target.point};
            return {};
        }
    return {};
}

IntersectionSnap snapIntersection(const QPointF &point, const SketchObject &local,
                                  const SketchObject &external, double tolerance,
                                  const QVector<ConstraintRef> &excluded) {
    struct Candidate {
        Kernel::ProfileSegment span;
        ConstraintRef entity;
        bool external;
    };
    std::vector<Candidate> candidates;
    const Kernel::Vec2 query(point.x(), point.y());
    const auto append = [&](const SketchObject &sketch, bool isExternal) {
        const auto add = [&](const Kernel::ProfileSegment &span, ConstraintRef entity) {
            if (!isExternal) for (const auto &skip : excluded)
                if (skip.kind == entity.kind && skip.element == entity.element) return;
            // Culling sulla geometria esatta: una curva piu' lontana dal cursore
            // non puo' avere un'intersezione nella sfera di selezione.
            if (Kernel::projectPoint(*span.curve, query, span.range).distance <= tolerance)
                candidates.push_back({span, entity, isExternal});
        };
        for (int i = 0; i < sketch.segments.size(); ++i) {
            const auto &line = sketch.segments[i];
            const double length = distance(line.first, line.second);
            if (length <= sketchConnectionTolerance()) continue;
            add({std::make_shared<Kernel::Line<2>>(Kernel::Vec2(line.first.x(), line.first.y()),
                 Kernel::Vec2(line.second.x()-line.first.x(), line.second.y()-line.first.y())), {0., length}}, {0,i,-1});
        }
        for (int i = 0; i < sketch.curves.size(); ++i) {
            // Il solver non identifica ancora i singoli lati di un poligono.
            if (sketch.curves[i].tool == DrawingTool::Polygon) continue;
            try {
                for (const auto &span : curveGeometry(sketch.curves[i])) add(span, {1,i,-1});
            } catch (const std::exception &) { /* geometria incompleta durante il disegno */ }
        }
    };
    append(local, false); append(external, true);
    IntersectionSnap result;
    double best = tolerance;
    for (size_t i = 0; i < candidates.size(); ++i) for (size_t j = i+1; j < candidates.size(); ++j) {
        const auto &a = candidates[i], &b = candidates[j];
        if (a.external == b.external && a.entity == b.entity) continue;
        try {
            const auto hits = Kernel::intersectCurves(*a.span.curve, a.span.range, *b.span.curve, b.span.range, 1e-9);
            // Un tratto sovrapposto non individua un punto d'intersezione unico.
            if (hits.overlap) continue;
            for (const auto &hit : hits.points) {
                // Un vertice comune mantiene lo snap Coincidente esistente,
                // con il legame agli estremi invece dello scorrimento su due rette.
                const auto atEnd = [&](const Candidate &c) {
                    return Kernel::norm(hit.point-c.span.start()) <= 1e-9
                        || Kernel::norm(hit.point-c.span.end()) <= 1e-9;
                };
                if (atEnd(a) && atEnd(b)) continue;
                const QPointF p(hit.point.x(), hit.point.y());
                const double d = distance(point, p);
                if (d >= best) continue;
                // Verifica anche il residuo sulle due curve, non sui loro campioni.
                if (Kernel::projectPoint(*a.span.curve, hit.point, a.span.range).distance > 1e-9
                    || Kernel::projectPoint(*b.span.curve, hit.point, b.span.range).distance > 1e-9) continue;
                best = d;
                result = {true, p, a.entity, b.entity, a.external, b.external};
            }
        } catch (const std::exception &) { /* coppia degenere o non supportata */ }
    }
    return result;
}

bool constrainIntersection(SketchObject &sketch, const ConstraintRef &point,
                           const IntersectionSnap &snap, const SketchObject &external,
                           const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies) {
    if (!snap.found) return false;
    SketchObject work = sketch;
    const auto resolve = [&](ConstraintRef entity, bool isExternal, ConstraintRef &out) {
        out = entity;
        if (!isExternal) return true;
        for (const auto &link : external.linkedReferences) if (link) {
            for (int i = 0; i < link->entities.size(); ++i) {
                if (link->entities[i].kind != entity.kind || link->entities[i].element != entity.element) continue;
                QVector<SketchEntity> created;
                if (!appendLinkedSketchReference(work, link->source, sketches, bodies, link->contact,
                                                 link->construction, link->halfLength, &created).isEmpty()
                    || i >= created.size()) return false;
                out = {created[i].kind, created[i].index, -1};
                return true;
            }
        }
        return false;
    };
    ConstraintRef first, second;
    if (!resolve(snap.first, snap.firstExternal, first) || !resolve(snap.second, snap.secondExternal, second)
        || first == second) return false;
    for (const auto &entity : {first, second}) {
        if (entity.kind == point.kind && entity.element == point.element) return false;
        bool exists = false;
        for (const auto &c : work.geometricConstraints)
            if (c.type == ConstraintType::PointOnCurve && c.first == point && c.second == entity) exists = true;
        if (!exists) {
            SketchConstraint c; c.type = ConstraintType::PointOnCurve; c.first = point; c.second = entity;
            work.geometricConstraints.append(c);
        }
    }
    sketch = std::move(work);
    return true;
}

SnapResult snapSegments(const QPointF &point, const QVector<SketchSegment> &segments,
                        const QVector<QPointF> &points, bool snapToGeometry, bool snapToGrid,
                        double gridSpacing, double tolerance) {
    SnapResult result;
    result.point = point;
    if (!snapToGeometry) {
        if (snapToGrid) {
            result.point.setX(std::round(point.x() / gridSpacing) * gridSpacing);
            result.point.setY(std::round(point.y() / gridSpacing) * gridSpacing);
        }
        return result;
    }

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
