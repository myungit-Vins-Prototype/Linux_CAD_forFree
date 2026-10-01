#include "cad_datum.h"

#include <cmath>
#include <limits>

#include "cad_curve_solver.h"
#include "cad_kernel.h"
#include "cad_topology_ref.h"
#include "fk_classify.h"
#include "fk_curve_ops.h"
#include "fk_sweep.h"
#include "fk_intersect.h"
#include "fk_curve_algo.h"
#include "fk_surface_algo.h"
#include "fk_topology.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

void setError(QString *error, const QString &message) {
    if (error) *error = message;
}

// Curva del kernel proprio sull'intervallo.
bool nearestOnForgeCurve(const Curve<3> &curve, const Interval &range, const Vec3 &q, Vec3 &foot, Vec3 &tangent) {
    const CurveProjection<3> p = projectPoint(curve, q, range);
    const Vec3 d = curve.derivative(p.parameter);
    if (norm(d) < 1e-14) return false;
    foot = p.point;
    tangent = normalized(d);
    return true;
}

// Una retta come riferimento: anche una curva (con la tangente costante).
void setLine(ResolvedRef &r, const Vec3 &point, const Vec3 &direction) {
    r.hasLine = true;
    r.point = point;
    r.direction = normalized(direction);
}

bool resolveForgeBody(const Body &body, int kind, const EdgePoint &reference, ResolvedRef &r, QString *error) {
    const Vec3 p(reference.x, reference.y, reference.z);
    if (kind == 3) {
        const VertexId vertex = resolveVertexReference(body, reference, std::numeric_limits<double>::max());
        if (!vertex.valid()) return setError(error, QStringLiteral("il corpo non ha vertici")), false;
        r.point = body.vertex(vertex).point;
        r.hasPoint = true;
        return true;
    }
    if (kind == 4) {
        const EdgeId best = resolveEdgeReference(body, reference, std::numeric_limits<double>::max());
        if (!best.valid()) return setError(error, QStringLiteral("il corpo non ha spigoli")), false;
        const Edge &edge = body.edge(best);
        const CurvePtr<3> curve = edge.curve;
        const Interval range = edge.range;
        const Curve<3> *basis = curve.get();
        while (basis->type() == CurveType::Trimmed) basis = static_cast<const TrimmedCurve<3> *>(basis)->basis().get();
        if (basis->type() == CurveType::Line) setLine(r, curve->point(range.lo), curve->derivative(range.lo));
        r.hasCurve = true;
        r.nearest = [curve, range](const Vec3 &q, Vec3 &foot, Vec3 &tangent) { return nearestOnForgeCurve(*curve, range, q, foot, tangent); };
        return true;
    }
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    const double tolerance = 1e-6 * std::max(1.0, box.diagonal());
    const FaceId best = resolveFaceReference(body, reference, 1e3 * tolerance);
    if (!best.valid()) return setError(error, QStringLiteral("il punto non sta piu' su una faccia del corpo")), false;
    const Face &face = body.face(best);
    const Surface &surface = *face.surface;
    if (surface.type() == SurfaceType::Plane) {
        const Frame3 &frame = static_cast<const Plane &>(surface).frame();
        const Vec3 n = face.sense ? frame.zDir() : -frame.zDir();
        r.hasPlane = true;
        r.point = p - dot(p - frame.origin(), n) * n;
        r.direction = n;
        return true;
    }
    if (surface.type() == SurfaceType::Cylinder) {
        const Frame3 &f = static_cast<const CylindricalSurface &>(surface).frame();
        setLine(r, f.origin(), f.zDir());
        return true;
    }
    if (surface.type() == SurfaceType::Cone) {
        const Frame3 &f = static_cast<const ConicalSurface &>(surface).frame();
        setLine(r, f.origin(), f.zDir());
        return true;
    }
    return setError(error, QStringLiteral("la faccia scelta non e' piana ne' cilindrica o conica")), false;
}

// Punto dello schizzo (coordinate del piano) indicato da un ConstraintRef.
bool sketchPoint(const SketchObject &sketch, const ConstraintRef &element, QPointF &point) {
    if (element.kind == 2 && element.element == 0) {
        point = QPointF(0, 0);
        return true;
    }
    if (element.kind == 0 && element.element >= 0 && element.element < sketch.segments.size() && (element.point == 0 || element.point == 1)) {
        const SketchSegment &s = sketch.segments.at(element.element);
        point = element.point == 0 ? s.first : s.second;
        return true;
    }
    if (element.kind == 1 && element.element >= 0 && element.element < sketch.curves.size() && element.point >= 0
        && element.point < sketch.curves.at(element.element).controlPoints.size()) {
        point = sketch.curves.at(element.element).controlPoints.at(element.point);
        return true;
    }
    return false;
}

Vec3 planeNormal(int plane) {
    // Come extrusionVector: la distanza positiva va lungo +Z, +Y, +X.
    return plane == 0 ? Vec3(0, 0, 1) : plane == 1 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
}

}  // namespace

const QVector<DatumMode> &datumModes() {
    static const QVector<DatumMode> modes = [] {
        QVector<DatumMode> m(7);
        m[0] = {QStringLiteral("Parallelo a distanza"), {DatumRolePlane}, {QStringLiteral("Piano:")}, true, false, false};
        m[1] = {QStringLiteral("Per tre punti"), {DatumRolePoint, DatumRolePoint, DatumRolePoint},
                {QStringLiteral("Primo punto:"), QStringLiteral("Secondo punto:"), QStringLiteral("Terzo punto:")}, false, false, false};
        m[2] = {QStringLiteral("Normale a una curva in un punto"), {DatumRoleCurve, DatumRolePoint}, {QStringLiteral("Curva:"), QStringLiteral("Punto:")},
                false, false, true};
        m[3] = {QStringLiteral("Per una retta e un punto"), {DatumRoleLine, DatumRolePoint}, {QStringLiteral("Retta:"), QStringLiteral("Punto:")}, false,
                false, false};
        m[4] = {QStringLiteral("Parallelo a un piano per un punto"), {DatumRolePlane, DatumRolePoint}, {QStringLiteral("Piano:"), QStringLiteral("Punto:")},
                false, false, false};
        m[5] = {QStringLiteral("Per una retta, ad angolo da un piano"), {DatumRolePlane, DatumRoleLine},
                {QStringLiteral("Piano:"), QStringLiteral("Retta (asse di rotazione):")}, false, true, false};
        m[6] = {QStringLiteral("Piano medio tra due piani"), {DatumRolePlane, DatumRolePlane}, {QStringLiteral("Primo piano:"), QStringLiteral("Secondo piano:")},
                false, false, false};
        return m;
    }();
    return modes;
}

int geometryRefRoles(const GeometryRef &ref, const QVector<SketchObject> &sketches) {
    switch (ref.kind) {
    case 0: return DatumRolePoint;
    case 1: return DatumRolePlane | DatumRoleFace;
    case 2: return DatumRoleLine | DatumRoleCurve;
    case 3: return DatumRolePoint;
    case 4: return DatumRoleLine | DatumRoleCurve;
    case 5: return DatumRolePlane | DatumRoleLine | DatumRoleFace;
    case 6: return DatumRolePoint;
    case 7: {
        const bool segment = ref.element.kind == 0;
        const bool valid = ref.index >= 0 && ref.index < sketches.size();
        return valid && segment ? DatumRoleLine | DatumRoleCurve : DatumRoleCurve;
    }
    case 8: return DatumRolePlane | DatumRoleFace;
    case 9: return DatumRoleCurve;
    case 10: return DatumRolePoint;
    default: return 0;
    }
}

bool resolveGeometryRef(const GeometryRef &ref, int owner, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                        ResolvedRef &r, QString *error) {
    r = ResolvedRef();
    const Vec3 p(ref.point.x, ref.point.y, ref.point.z);
    try {
        switch (ref.kind) {
        case 0:
            r.hasPoint = true;
            r.point = Vec3(0, 0, 0);
            return true;
        case 1:
            if (ref.index < 0 || ref.index > 2) break;
            r.hasPlane = true;
            r.point = Vec3(0, 0, 0);
            r.direction = planeNormal(ref.index);
            return true;
        case 2: {
            if (ref.index < 0 || ref.index > 2) break;
            const Vec3 d = ref.index == 0 ? Vec3(1, 0, 0) : ref.index == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
            setLine(r, Vec3(0, 0, 0), d);
            r.hasCurve = true;
            r.nearest = [d](const Vec3 &q, Vec3 &foot, Vec3 &tangent) {
                foot = dot(q, d) * d;
                tangent = d;
                return true;
            };
            return true;
        }
        case 3:
        case 4:
        case 5: {
            int bodyIndex = ref.index;
            if (ref.featureId) {
                bodyIndex = -1;
                for (int candidate = 0; candidate < owner && candidate < bodies.size(); ++candidate)
                    if (bodies.at(candidate).featureId == ref.featureId) { bodyIndex = candidate; break; }
            }
            if (bodyIndex < 0 || bodyIndex >= owner || bodyIndex >= bodies.size()) {
                setError(error, QStringLiteral("il corpo del riferimento non esiste piu'"));
                return false;
            }
            const ExtrusionObject &body = bodies.at(bodyIndex);
            if (body.forgeBody) return resolveForgeBody(*body.forgeBody, ref.kind, ref.point, r, error);
            setError(error, QStringLiteral("il corpo \"%1\" non ha geometria").arg(body.name));
            return false;
        }
        case 6:
        case 7: {
            if (ref.index < 0 || ref.index >= sketches.size()) {
                setError(error, QStringLiteral("lo schizzo del riferimento non esiste piu'"));
                return false;
            }
            const SketchObject &sketch = sketches.at(ref.index);
            if (sketch.datumPlane >= owner && owner >= 0) {
                setError(error, QStringLiteral("lo schizzo \"%1\" sta su questo piano o su uno successivo").arg(sketch.name));
                return false;
            }
            if (ref.kind == 6) {
                QPointF q;
                if (!sketchPoint(sketch, ref.element, q)) {
                    setError(error, QStringLiteral("il punto dello schizzo non esiste piu'"));
                    return false;
                }
                r.hasPoint = true;
                r.point = sketchToWorld(q, sketch);
                return true;
            }
            const Frame3 plane = sketchAxes(sketch);
            if (ref.element.kind == 0) {
                if (ref.element.element < 0 || ref.element.element >= sketch.segments.size()) break;
                const SketchSegment &s = sketch.segments.at(ref.element.element);
                const Vec3 a = sketchToWorld(s.first, sketch), b = sketchToWorld(s.second, sketch);
                if (distance(a, b) < 1e-12) break;
                setLine(r, a, b - a);
                r.hasCurve = true;
                const Vec3 d = normalized(b - a);
                const double length = distance(a, b);
                r.nearest = [a, d, length](const Vec3 &q, Vec3 &foot, Vec3 &tangent) {
                    foot = a + std::clamp(dot(q - a, d), 0.0, length) * d;
                    tangent = d;
                    return true;
                };
                return true;
            }
            if (ref.element.kind != 1 || ref.element.element < 0 || ref.element.element >= sketch.curves.size()) break;
            std::vector<PathSegment> curves;
            for (const ProfileSegment &piece : curveGeometry(sketch.curves.at(ref.element.element))) curves.push_back({embedCurve(piece.curve, plane), piece.range});
            if (curves.empty()) break;
            r.hasCurve = true;
            r.nearest = [curves](const Vec3 &q, Vec3 &foot, Vec3 &tangent) {
                double best = std::numeric_limits<double>::max();
                bool found = false;
                for (const PathSegment &c : curves) {
                    Vec3 f, t;
                    if (!nearestOnForgeCurve(*c.curve, c.range, q, f, t)) continue;
                    if (distance(f, q) < best) best = distance(f, q), foot = f, tangent = t, found = true;
                }
                return found;
            };
            return true;
        }
        case 8: {
            int bodyIndex = ref.index;
            if (ref.featureId) {
                bodyIndex = -1;
                for (int candidate = 0; candidate < owner && candidate < bodies.size(); ++candidate)
                    if (bodies.at(candidate).featureId == ref.featureId) { bodyIndex = candidate; break; }
            }
            if (bodyIndex < 0 || bodyIndex >= owner || bodyIndex >= bodies.size() || !bodies.at(bodyIndex).datumValid) {
                setError(error, QStringLiteral("il piano di costruzione del riferimento non esiste piu' o non e' valido"));
                return false;
            }
            const SketchFrame &f = bodies.at(bodyIndex).datumFrame;
            r.hasPlane = true;
            r.point = Vec3(f.origin[0], f.origin[1], f.origin[2]);
            r.direction = normalized(Vec3(f.normal[0], f.normal[1], f.normal[2]));
            return true;
        }
        case 9: {
            int bodyIndex = ref.index;
            if (ref.featureId) {
                bodyIndex = -1;
                for (int candidate = 0; candidate < owner && candidate < bodies.size(); ++candidate)
                    if (bodies.at(candidate).featureId == ref.featureId) { bodyIndex = candidate; break; }
            }
            if (bodyIndex < 0 || bodyIndex >= owner || bodyIndex >= bodies.size() || !bodies.at(bodyIndex).curve) {
                setError(error, QStringLiteral("la curva del riferimento non esiste piu'"));
                return false;
            }
            const ForgeCurve curve = bodies.at(bodyIndex).curve;
            r.hasCurve = true;
            r.nearest = [curve](const Vec3 &q, Vec3 &foot, Vec3 &tangent) { return nearestOnForgeCurve(*curve, curve->domain(), q, foot, tangent); };
            return true;
        }
        case 10: {
            int bodyIndex = ref.index;
            if (ref.featureId) {
                bodyIndex = -1;
                for (int candidate = 0; candidate < owner && candidate < bodies.size(); ++candidate)
                    if (bodies.at(candidate).featureId == ref.featureId) { bodyIndex = candidate; break; }
            }
            if (bodyIndex < 0 || bodyIndex >= owner || bodyIndex >= bodies.size() || !bodies.at(bodyIndex).curve) {
                setError(error, QStringLiteral("la curva del punto di riferimento non esiste piu'"));
                return false;
            }
            const ForgeCurve curve = bodies.at(bodyIndex).curve;
            const Interval domain = curve->domain();
            const double parameter = ref.element.point == 1 ? domain.hi : domain.lo;
            r.hasPoint = true;
            r.point = curve->point(parameter);
            return true;
        }
        default: break;
        }
    } catch (const std::exception &failure) {
        setError(error, QString::fromUtf8(failure.what()));
        return false;
    }
    setError(error, QStringLiteral("riferimento non valido"));
    return false;
}

bool computeDatum(const DatumParameters &parameters, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                  SketchFrame &frame, QString *error) {
    const QVector<DatumMode> &modes = datumModes();
    if (parameters.mode < 0 || parameters.mode >= modes.size()) return setError(error, QStringLiteral("Modo del piano non valido.")), false;
    const DatumMode &mode = modes.at(parameters.mode);
    if (parameters.refs.size() < mode.roles.size()) return setError(error, QStringLiteral("Mancano dei riferimenti.")), false;
    QVector<ResolvedRef> refs(mode.roles.size());
    for (int k = 0; k < mode.roles.size(); ++k) {
        QString reason;
        if (!resolveGeometryRef(parameters.refs.at(k), index, sketches, bodies, refs[k], &reason))
            return setError(error, QStringLiteral("%1 %2.").arg(mode.labels.at(k), reason)), false;
        const int role = mode.roles.at(k);
        const bool ok = (role == DatumRolePlane && refs[k].hasPlane) || (role == DatumRolePoint && refs[k].hasPoint)
                     || (role == DatumRoleLine && refs[k].hasLine) || (role == DatumRoleCurve && refs[k].hasCurve);
        if (!ok) {
            const QString what = role == DatumRolePlane ? QStringLiteral("un piano") : role == DatumRolePoint ? QStringLiteral("un punto")
                               : role == DatumRoleLine  ? QStringLiteral("una retta (spigolo rettilineo, segmento, asse o faccia cilindrica)")
                                                        : QStringLiteral("una curva");
            return setError(error, QStringLiteral("%1 serve %2.").arg(mode.labels.at(k), what)), false;
        }
    }
    // Scala per le tolleranze: la distanza dei punti in gioco dall'origine.
    double scale = 1.0;
    for (const ResolvedRef &r : refs) scale = std::max(scale, norm(r.point));
    Vec3 n, c;
    switch (parameters.mode) {
    case 0:
        n = refs[0].direction;
        c = refs[0].point + parameters.distance * n;
        break;
    case 1: {
        const Vec3 a = refs[0].point, b = refs[1].point, d = refs[2].point;
        const Vec3 normal = cross(b - a, d - a);
        if (norm(normal) <= 1e-12 * scale * scale) return setError(error, QStringLiteral("I tre punti sono allineati (o coincidenti).")), false;
        n = normalized(normal);
        c = (a + b + d) / 3.0;
        break;
    }
    case 2: {
        Vec3 foot, tangent;
        if (!refs[0].nearest || !refs[0].nearest(refs[1].point, foot, tangent))
            return setError(error, QStringLiteral("Tangente della curva non definita in quel punto.")), false;
        n = tangent;
        c = parameters.onCurve ? foot : refs[1].point;
        break;
    }
    case 3: {
        const Vec3 d = refs[0].direction, v = refs[1].point - refs[0].point;
        const Vec3 w = v - dot(v, d) * d;
        if (norm(w) <= 1e-9 * scale) return setError(error, QStringLiteral("Il punto sta sulla retta: il piano non e' definito.")), false;
        n = normalized(cross(d, w));
        c = refs[0].point + dot(v, d) * d + 0.5 * w;
        break;
    }
    case 4:
        n = refs[0].direction;
        c = refs[1].point;
        break;
    case 5: {
        const Vec3 d = refs[1].direction, n0 = refs[0].direction;
        const Vec3 p = n0 - dot(n0, d) * d;
        if (norm(p) <= 1e-9) return setError(error, QStringLiteral("La retta e' perpendicolare al piano: l'angolo non e' definito.")), false;
        const Vec3 m = normalized(p);
        const double a = parameters.angle * M_PI / 180.0;
        n = std::cos(a) * m + std::sin(a) * cross(d, m);
        // Il centro: il punto della retta piu' vicino al punto del piano.
        c = refs[1].point + dot(refs[0].point - refs[1].point, d) * d;
        break;
    }
    case 6: {
        Vec3 n1 = refs[0].direction, n2 = refs[1].direction;
        if (dot(n1, n2) < 0.0) n2 = -n2;
        const double d1 = dot(n1, refs[0].point), d2 = dot(n2, refs[1].point);
        const Vec3 sum = n1 + n2;
        // Paralleli: il piano a meta'; altrimenti il bisettore (n1 + n2) . x = d1 + d2, che per piani quasi paralleli e' lo stesso.
        n = normalized(sum);
        const double offset = (d1 + d2) / norm(sum);
        const Vec3 middle = 0.5 * (refs[0].point + refs[1].point);
        c = middle - (dot(n, middle) - offset) * n;
        break;
    }
    default: return setError(error, QStringLiteral("Modo del piano non valido.")), false;
    }
    if (parameters.flip) n = -n;
    if (!(norm(n) > 0.5) || !std::isfinite(c.x()) || !std::isfinite(c.y()) || !std::isfinite(c.z()))
        return setError(error, QStringLiteral("Piano non definito.")), false;
    frame = faceSketchFrame(c, normalized(n));
    for (int k = 0; k < 3; ++k) frame.origin[k] = c[k];
    return true;
}

QString geometryRefText(const GeometryRef &ref, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies) {
    static const char *planes[] = {"XY", "XZ", "YZ"}, *axes[] = {"X", "Y", "Z"};
    const auto bodyName = [&](int i) {
        if (ref.featureId)
            for (const ExtrusionObject &body : bodies)
                if (body.featureId == ref.featureId) return body.name;
        return i >= 0 && i < bodies.size() ? bodies.at(i).name : QStringLiteral("?");
    };
    const auto sketchName = [&](int i) { return i >= 0 && i < sketches.size() ? sketches.at(i).name : QStringLiteral("?"); };
    switch (ref.kind) {
    case 0: return QStringLiteral("Origine");
    case 1: return ref.index >= 0 && ref.index < 3 ? QStringLiteral("Piano %1").arg(QLatin1String(planes[ref.index])) : QStringLiteral("Piano");
    case 2: return ref.index >= 0 && ref.index < 3 ? QStringLiteral("Asse %1").arg(QLatin1String(axes[ref.index])) : QStringLiteral("Asse");
    case 3: return QStringLiteral("Vertice di %1").arg(bodyName(ref.index));
    case 4: return QStringLiteral("Spigolo di %1").arg(bodyName(ref.index));
    case 5: return QStringLiteral("Faccia di %1").arg(bodyName(ref.index));
    case 6:
        if (ref.element.kind == 2) return QStringLiteral("Origine di %1").arg(sketchName(ref.index));
        return QStringLiteral("Punto di %1").arg(sketchName(ref.index));
    case 7:
        return (ref.element.kind == 0 ? QStringLiteral("Segmento %1 di %2") : QStringLiteral("Curva %1 di %2")).arg(ref.element.element + 1).arg(sketchName(ref.index));
    case 8: return bodyName(ref.index);
    case 9: return bodyName(ref.index);
    case 10: return QStringLiteral("%1 di %2").arg(ref.element.point == 1 ? QStringLiteral("Fine") : QStringLiteral("Inizio"), bodyName(ref.index));
    default: return QStringLiteral("(da scegliere)");
    }
}

}
