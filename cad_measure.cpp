#include "cad_measure.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "cad_datum.h"
#include "cad_expression.h"
#include "cad_topology_ref.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_mass.h"
#include "fk_surface.h"
#include "fk_surface_algo.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr double kDegrees = 180.0 / kPi;

void setError(QString *error, const QString &message) {
    if (error) *error = message;
}

QString number(double value, int decimals = 6) {
    QString text = QString::number(value, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) text.chop(1);
        if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    }
    return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}
QString length(double value) { return formatLength(value, 6); }
QString angle(double radians) { return number(radians * kDegrees, 6) + QStringLiteral("°"); }
QString point(const Vec3 &p) {
    return QStringLiteral("(%1; %2; %3)").arg(formatLength(p.x(), 6, false), formatLength(p.y(), 6, false), formatLength(p.z(), 6));
}
QString vector(const Vec3 &d) { return QStringLiteral("(%1; %2; %3)").arg(number(d.x()), number(d.y()), number(d.z())); }

// Corpo del riferimento (per featureId, poi per indice).
int bodyOf(const GeometryRef &ref, const QVector<ExtrusionObject> &bodies) {
    if (ref.featureId)
        for (int index = 0; index < bodies.size(); ++index)
            if (bodies.at(index).featureId == ref.featureId) return index;
    return ref.index >= 0 && ref.index < bodies.size() ? ref.index : -1;
}

// Cerchio o ellisse sotto le restrizioni e le trasformazioni.
struct Conic {
    bool ok = false;
    bool circle = false;
    Vec3 center, normal;
    double r1 = 0.0, r2 = 0.0;
};
Conic conicOf(const Curve<3> &curve) {
    Conic c;
    if (curve.type() == CurveType::Trimmed) return conicOf(*static_cast<const TrimmedCurve<3> &>(curve).basis());
    if (curve.type() == CurveType::Transformed) {
        const auto &t = static_cast<const TransformedCurve &>(curve);
        Conic base = conicOf(*t.basis());
        if (!base.ok) return base;
        // Due direzioni del piano della conica, trasformate.
        Vec3 x = cross(base.normal, Vec3(1, 0, 0));
        if (norm(x) < 0.5) x = cross(base.normal, Vec3(0, 1, 0));
        x = normalized(x);
        const Vec3 y = cross(base.normal, x);
        const Vec3 tx = t.transform().applyToVector(x), ty = t.transform().applyToVector(y);
        const double scale = norm(tx);
        base.center = t.transform().applyToPoint(base.center);
        base.normal = normalized(cross(tx, ty));
        base.r1 *= scale;
        base.r2 *= scale;
        return base;
    }
    if (curve.type() == CurveType::Circle) {
        const auto &circle = static_cast<const Circle<3> &>(curve);
        c = {true, true, circle.center(), normalized(cross(circle.xAxis(), circle.yAxis())), circle.radius(), circle.radius()};
    } else if (curve.type() == CurveType::Ellipse) {
        const auto &ellipse = static_cast<const Ellipse<3> &>(curve);
        c = {true, false, ellipse.center(), normalized(cross(ellipse.xAxis(), ellipse.yAxis())), ellipse.xRadius(), ellipse.yRadius()};
    }
    return c;
}

// Direzione di una retta sotto le restrizioni e le trasformazioni (falso se non e' una retta).
bool lineDirection(const Curve<3> &curve, Vec3 &direction) {
    if (curve.type() == CurveType::Trimmed) return lineDirection(*static_cast<const TrimmedCurve<3> &>(curve).basis(), direction);
    if (curve.type() == CurveType::Transformed) {
        const auto &t = static_cast<const TransformedCurve &>(curve);
        if (!lineDirection(*t.basis(), direction)) return false;
        direction = normalized(t.transform().applyToVector(direction));
        return true;
    }
    if (curve.type() != CurveType::Line) return false;
    direction = normalized(static_cast<const Line<3> &>(curve).direction());
    return true;
}

double curveLength(const std::vector<PathSegment> &segments) {
    double total = 0.0;
    for (const PathSegment &s : segments) total += arcLength(*s.curve, s.range, 1e-10);
    return total;
}

// --- Entita' pronte per la distanza ------------------------------------------

struct Prepared {
    const MeasureEntity *entity = nullptr;
    std::vector<Vec3> seeds;
    // Faccia: finestra (u, v) dei suoi loop e spigoli del bordo.
    SurfacePtr surface;
    Interval u, v;
    std::vector<PathSegment> boundary;
    double scale = 1.0;
    bool finite() const {
        return entity->kind != MeasureEntity::Kind::Line && entity->kind != MeasureEntity::Kind::Plane;
    }
};

void prepareFace(Prepared &p) {
    const Body &body = *p.entity->body;
    const Face &face = body.face(p.entity->face);
    p.surface = face.surface;
    for (EdgeId e : faceBoundaryEdges(body, p.entity->face)) p.boundary.push_back({body.edge(e).curve, body.edge(e).range});
    double u0 = kInfinity, u1 = -kInfinity, v0 = kInfinity, v1 = -kInfinity;
    const auto add = [&](const Vec2 &uv) {
        u0 = std::min(u0, uv.x()), u1 = std::max(u1, uv.x());
        v0 = std::min(v0, uv.y()), v1 = std::max(v1, uv.y());
    };
    for (LoopId loop : face.loops)
        for (FinId f : body.loopFins(loop)) {
            const Fin &fin = body.fin(f);
            const Edge &edge = body.edge(fin.edge);
            for (int k = 0; k <= 16; ++k) {
                const double t = edge.range.lo + edge.range.length() * k / 16.0;
                if (fin.pcurve) {
                    add(fin.pcurve->point(t));
                } else {
                    try {
                        const SurfaceProjection q = projectPoint(*p.surface, edge.curve->point(t));
                        add(Vec2(q.u, q.v));
                    } catch (const std::exception &) {
                    }
                }
            }
        }
    const Interval ud = p.surface->uDomain(), vd = p.surface->vDomain();
    if (!(u0 <= u1)) u0 = ud.lo, u1 = ud.hi;
    if (!(v0 <= v1)) v0 = vd.lo, v1 = vd.hi;
    // Un periodo intero al piu'. Fuori dalla finestra (SP-curve approssimate)
    // restano comunque gli spigoli del bordo.
    if (p.surface->isUPeriodic() && u1 - u0 > p.surface->uPeriod()) u1 = u0 + p.surface->uPeriod();
    if (p.surface->isVPeriodic() && v1 - v0 > p.surface->vPeriod()) v1 = v0 + p.surface->vPeriod();
    if (!p.surface->isUPeriodic()) u0 = std::max(u0, ud.lo), u1 = std::min(u1, ud.hi);
    if (!p.surface->isVPeriodic()) v0 = std::max(v0, vd.lo), v1 = std::min(v1, vd.hi);
    p.u = {u0, u1};
    p.v = {v0, v1};
}

double faceTolerance(const Prepared &p) { return std::max(1e-7, 1e-9 * p.scale); }

// Dentro la faccia, non sul bordo: i punti del bordo vengono dalla proiezione
// esatta sugli spigoli (una proiezione sulla superficie appena fuori dal
// bordo, entro la tolleranza, darebbe una distanza un poco piu' corta).
bool insideFace(const Prepared &p, const Vec3 &q) {
    try {
        return classifyPointOnFace(*p.entity->body, p.entity->face, q, faceTolerance(p)) == PointLocation::Inside;
    } catch (const std::exception &) {
        return false;
    }
}

// Proiezione locale (Gauss-Newton) sulla superficie dal punto `hint` che vi sta.
bool localFaceProjection(const Prepared &p, const Vec3 &q, const Vec3 &hint, Vec3 &result) {
    Vec2 uv;
    try {
        if (!invertPoint(*p.surface, hint, uv, 1e-6 * std::max(1.0, p.scale), p.scale)) return false;
    } catch (const std::exception &) {
        return false;
    }
    double u = uv.x(), v = uv.y();
    for (int iteration = 0; iteration < 30; ++iteration) {
        Vec3 d[4];  // (order + 1)^2 valori, come vuole Surface::evaluate
        p.surface->evaluate(u, v, 1, d);
        const Vec3 r = d[0] - q, su = d[Surface::derivativeIndex(1, 0, 1)], sv = d[Surface::derivativeIndex(0, 1, 1)];
        const double a = dot(su, su), b = dot(su, sv), c = dot(sv, sv);
        const double g0 = dot(r, su), g1 = dot(r, sv);
        const double det = a * c - b * b;
        if (!(det > 1e-300)) break;
        const double stepU = -(c * g0 - b * g1) / det, stepV = -(a * g1 - b * g0) / det;
        u = p.u.clamp(u + stepU);
        v = p.v.clamp(v + stepV);
        if (std::fabs(stepU) * std::sqrt(a) + std::fabs(stepV) * std::sqrt(c) < 1e-15 * std::max(1.0, p.scale)) break;
    }
    result = p.surface->point(u, v);
    return std::isfinite(result.x());
}

// Punto dell'entita' piu' vicino a q (hint: l'ultimo punto trovato su questa entita').
Vec3 closest(const Prepared &p, const Vec3 &q, const Vec3 *hint = nullptr) {
    const MeasureEntity &e = *p.entity;
    switch (e.kind) {
    case MeasureEntity::Kind::Point: return e.point;
    case MeasureEntity::Kind::Line: return e.point + dot(q - e.point, e.direction) * e.direction;
    case MeasureEntity::Kind::Plane: return q - dot(q - e.point, e.direction) * e.direction;
    case MeasureEntity::Kind::Curve: {
        Vec3 best = q;
        double nearest = kInfinity;
        for (const PathSegment &s : e.segments) {
            const CurveProjection<3> c = projectPoint(*s.curve, q, s.range);
            if (c.distance < nearest) nearest = c.distance, best = c.point;
        }
        return best;
    }
    case MeasureEntity::Kind::Face: {
        Vec3 best = q;
        double nearest = kInfinity;
        const auto consider = [&](const Vec3 &candidate) {
            const double d = distance(candidate, q);
            if (d < nearest) nearest = d, best = candidate;
        };
        // Prima il bordo (esatto, senza classificazione); i punti della
        // superficie si classificano solo se sono piu' vicini.
        for (const PathSegment &s : p.boundary) consider(projectPoint(*s.curve, q, s.range).point);
        try {
            const SurfaceProjection s = projectPoint(*p.surface, q, p.u, p.v);
            if (s.distance < nearest && insideFace(p, s.point)) consider(s.point);
        } catch (const std::exception &) {
        }
        Vec3 local;
        if (hint && localFaceProjection(p, q, *hint, local) && distance(local, q) < nearest && insideFace(p, local)) consider(local);
        return best;
    }
    case MeasureEntity::Kind::None: break;
    }
    return q;
}

Prepared prepare(const MeasureEntity &entity) {
    Prepared p;
    p.entity = &entity;
    std::vector<Vec3> &seeds = p.seeds;
    switch (entity.kind) {
    case MeasureEntity::Kind::Point:
    case MeasureEntity::Kind::Line:
    case MeasureEntity::Kind::Plane: seeds.push_back(entity.point); break;
    case MeasureEntity::Kind::Curve: {
        // Circa 256 semi in tutto (almeno 2 per tratto).
        const int perSegment = std::max(2, 256 / std::max<int>(1, int(entity.segments.size())));
        for (const PathSegment &s : entity.segments) {
            if (!s.range.isFinite()) continue;
            for (int k = 0; k <= perSegment; ++k) seeds.push_back(s.curve->point(s.range.lo + s.range.length() * k / perSegment));
        }
        break;
    }
    case MeasureEntity::Kind::Face: {
        prepareFace(p);
        const int perEdge = std::max(2, 192 / std::max<int>(1, int(p.boundary.size())));
        for (const PathSegment &s : p.boundary)
            for (int k = 0; k <= perEdge; ++k) seeds.push_back(s.curve->point(s.range.lo + s.range.length() * k / perEdge));
        if (p.u.isFinite() && p.v.isFinite()) {
            constexpr int grid = 8;
            for (int i = 0; i <= grid; ++i)
                for (int j = 0; j <= grid; ++j) {
                    const Vec3 q = p.surface->point(p.u.lo + p.u.length() * (i + 0.5) / (grid + 1), p.v.lo + p.v.length() * (j + 0.5) / (grid + 1));
                    if (std::isfinite(q.x()) && insideFace(p, q)) seeds.push_back(q);
                }
        }
        break;
    }
    case MeasureEntity::Kind::None: break;
    }
    double extent = 1.0;
    for (const Vec3 &s : seeds) extent = std::max({extent, std::fabs(s.x()), std::fabs(s.y()), std::fabs(s.z())});
    p.scale = extent;
    return p;
}

// Distanza minima tra due entita': forme chiuse per punti, rette e piani,
// altrimenti proiezioni alternate dai semi piu' promettenti dei due lati.
bool minimumDistance(const Prepared &a, const Prepared &b, Vec3 &pa, Vec3 &pb) {
    using K = MeasureEntity::Kind;
    const MeasureEntity &ea = *a.entity, &eb = *b.entity;
    if (ea.kind == K::Point) {
        pa = ea.point;
        pb = closest(b, pa);
        return true;
    }
    if (eb.kind == K::Point) {
        pb = eb.point;
        pa = closest(a, pb);
        return true;
    }
    if (!a.finite() && !b.finite()) {
        if (ea.kind == K::Line && eb.kind == K::Line) {
            const Vec3 w = ea.point - eb.point, n = cross(ea.direction, eb.direction);
            if (norm(n) < 1e-12) {
                pa = ea.point;
                pb = closest(b, pa);
                return true;
            }
            const double d1 = dot(ea.direction, eb.direction);
            const double p = dot(ea.direction, w), q = dot(eb.direction, w), denominator = 1.0 - d1 * d1;
            const double s = (d1 * q - p) / denominator, t = (q - d1 * p) / denominator;
            pa = ea.point + s * ea.direction;
            pb = eb.point + t * eb.direction;
            return true;
        }
        const MeasureEntity &plane = ea.kind == K::Plane ? ea : eb, &other = ea.kind == K::Plane ? eb : ea;
        const Prepared &planePrepared = ea.kind == K::Plane ? a : b;
        Vec3 onOther, onPlane;
        if (other.kind == K::Line) {
            const double along = dot(other.direction, plane.direction);
            if (std::fabs(along) < 1e-12) {
                onOther = other.point;
                onPlane = closest(planePrepared, onOther);
            } else {
                onOther = other.point + (dot(plane.point - other.point, plane.direction) / along) * other.direction;
                onPlane = onOther;
            }
        } else {
            const Vec3 line = cross(plane.direction, other.direction);
            if (norm(line) < 1e-12) {
                onOther = other.point;
                onPlane = closest(planePrepared, onOther);
            } else {
                // Punto della retta comune dei due piani (il piu' vicino all'origine).
                const double h1 = dot(plane.direction, plane.point), h2 = dot(other.direction, other.point);
                const Vec3 common = (h1 * cross(other.direction, line) + h2 * cross(line, plane.direction)) / dot(line, line);
                onOther = onPlane = common;
            }
        }
        pa = ea.kind == K::Plane ? onPlane : onOther;
        pb = ea.kind == K::Plane ? onOther : onPlane;
        return true;
    }
    struct Candidate {
        Vec3 a, b;
        double d;
    };
    std::vector<Candidate> candidates;
    if (a.finite() && b.finite()) {
        // Per ogni seme il seme piu' vicino dell'altra entita' (punti esatti):
        // le proiezioni, care sulle facce, solo per le coppie che si raffinano.
        const auto nearestSeed = [](const Vec3 &s, const std::vector<Vec3> &others) {
            const Vec3 *best = &others.front();
            for (const Vec3 &t : others)
                if (distance(s, t) < distance(s, *best)) best = &t;
            return *best;
        };
        if (!a.seeds.empty() && !b.seeds.empty()) {
            for (const Vec3 &s : a.seeds) {
                const Vec3 t = nearestSeed(s, b.seeds);
                candidates.push_back({s, t, distance(s, t)});
            }
            for (const Vec3 &t : b.seeds) {
                const Vec3 s = nearestSeed(t, a.seeds);
                candidates.push_back({s, t, distance(s, t)});
            }
        }
    } else if (a.finite()) {
        for (const Vec3 &s : a.seeds) {
            const Vec3 q = closest(b, s);
            candidates.push_back({s, q, distance(s, q)});
        }
    } else {
        for (const Vec3 &s : b.seeds) {
            const Vec3 q = closest(a, s);
            candidates.push_back({q, s, distance(s, q)});
        }
    }
    if (candidates.empty()) return false;
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &x, const Candidate &y) { return x.d < y.d; });
    const double scale = std::max(a.scale, b.scale);
    const double epsilon = 1e-14 * scale;
    const double touching = 1e-12 * scale;  // a contatto: distanza nulla
    double best = kInfinity;
    std::vector<Vec3> started;
    int refined = 0;
    for (const Candidate &start : candidates) {
        if (refined >= 12 || best <= touching) break;
        // Semi quasi uguali portano allo stesso minimo.
        bool seen = false;
        for (const Vec3 &s : started) seen = seen || distance(s, start.a) < 1e-6 * scale;
        if (seen) continue;
        started.push_back(start.a);
        ++refined;
        Vec3 p = start.a, q = closest(b, p);
        double d = distance(p, q);
        Vec3 anchor = p;  // punto di qualche passo fa, per l'estrapolazione
        for (int iteration = 0; iteration < 200; ++iteration) {
            const Vec3 nextQ = closest(b, p, &q);
            const Vec3 nextP = closest(a, nextQ, &p);
            const double nextD = distance(nextP, nextQ);
            const double moved = distance(nextP, p) + distance(nextQ, q);
            if (nextD > d) break;
            double gain = d - nextD;
            p = nextP, q = nextQ, d = nextD;
            // Le proiezioni alternate convergono in modo lineare, lentissimo dove
            // le due entita' sono quasi parallele: ogni tre passi si prova a
            // proseguire nella direzione in cui i punti si stanno spostando, con
            // passi doppi finche' la distanza scende.
            if (iteration % 3 == 2 && distance(p, anchor) > epsilon) {
                const Vec3 drift = p - anchor;
                for (double factor = 2.0; factor < 1e6; factor *= 2.0) {
                    const Vec3 tryP = closest(a, p + factor * drift, &p);
                    const Vec3 tryQ = closest(b, tryP, &q);
                    const double tryD = distance(tryP, tryQ);
                    if (!(tryD < d)) break;
                    gain += d - tryD;
                    p = tryP, q = tryQ, d = tryD;
                }
                anchor = p;
            }
            // Fermo, a contatto o senza piu' progressi apprezzabili (quando il
            // minimo e' un insieme continuo, come tra facce coassiali, i punti
            // scivolano lungo di esso con la distanza gia' minima).
            if (moved < epsilon || d <= touching || (gain <= 1e-12 * scale && iteration > 2)) break;
        }
        if (d < best) best = d, pa = p, pb = q;
    }
    return std::isfinite(best);
}

// --- Proprieta' delle entita' ------------------------------------------------

bool axisOf(const MeasureEntity &e, Vec3 &point, Vec3 &direction) {
    using K = MeasureEntity::Kind;
    if (e.kind == K::Line) {
        point = e.point, direction = e.direction;
        return true;
    }
    if (e.kind == K::Curve && e.segments.size() == 1) {
        const Conic c = conicOf(*e.segments.front().curve);
        if (c.ok && c.circle) {
            point = c.center, direction = c.normal;
            return true;
        }
    }
    if (e.kind == K::Face) {
        const Surface &s = *e.body->face(e.face).surface;
        const Frame3 *frame = nullptr;
        if (s.type() == SurfaceType::Cylinder) frame = &static_cast<const CylindricalSurface &>(s).frame();
        else if (s.type() == SurfaceType::Cone) frame = &static_cast<const ConicalSurface &>(s).frame();
        else if (s.type() == SurfaceType::Torus) frame = &static_cast<const ToroidalSurface &>(s).frame();
        if (frame) {
            point = frame->origin(), direction = frame->zDir();
            return true;
        }
    }
    return false;
}

// Direzione di una retta (rette, spigoli rettilinei, assi di cilindri e coni).
bool lineOf(const MeasureEntity &e, Vec3 &direction) {
    using K = MeasureEntity::Kind;
    if (e.kind == K::Line) {
        direction = e.direction;
        return true;
    }
    if (e.kind == K::Curve && e.segments.size() == 1) return lineDirection(*e.segments.front().curve, direction);
    if (e.kind == K::Face) {
        const SurfaceType type = e.body->face(e.face).surface->type();
        Vec3 point;
        if (type == SurfaceType::Cylinder || type == SurfaceType::Cone) return axisOf(e, point, direction);
    }
    return false;
}

// Normale di un piano (piani, facce piane: uscente).
bool planeOf(const MeasureEntity &e, Vec3 &normal) {
    using K = MeasureEntity::Kind;
    if (e.kind == K::Plane) {
        normal = e.direction;
        return true;
    }
    if (e.kind == K::Face) {
        const Face &face = e.body->face(e.face);
        if (face.surface->type() != SurfaceType::Plane) return false;
        const Vec3 z = static_cast<const Plane &>(*face.surface).frame().zDir();
        normal = face.sense ? z : -z;
        return true;
    }
    return false;
}

bool centerOf(const MeasureEntity &e, Vec3 &center) {
    using K = MeasureEntity::Kind;
    if (e.kind == K::Point) {
        center = e.point;
        return true;
    }
    if (e.kind == K::Curve && e.segments.size() == 1) {
        const Conic c = conicOf(*e.segments.front().curve);
        if (c.ok) center = c.center;
        return c.ok;
    }
    if (e.kind == K::Face) {
        const Surface &s = *e.body->face(e.face).surface;
        if (s.type() == SurfaceType::Sphere) {
            center = static_cast<const SphericalSurface &>(s).frame().origin();
            return true;
        }
    }
    return false;
}

double lineAngle(const Vec3 &a, const Vec3 &b) {
    return std::atan2(norm(cross(a, b)), std::fabs(dot(a, b)));  // [0, 90 gradi]
}

void describe(const MeasureEntity &e, QStringList &lines, QString &label) {
    using K = MeasureEntity::Kind;
    switch (e.kind) {
    case K::Point:
        lines << QStringLiteral("X: %1").arg(length(e.point.x())) << QStringLiteral("Y: %1").arg(length(e.point.y()))
              << QStringLiteral("Z: %1").arg(length(e.point.z()));
        label = point(e.point);
        return;
    case K::Line:
        lines << QStringLiteral("Retta per %1").arg(point(e.point)) << QStringLiteral("Direzione: %1").arg(vector(e.direction));
        return;
    case K::Plane:
        lines << QStringLiteral("Piano per %1").arg(point(e.point)) << QStringLiteral("Normale: %1").arg(vector(e.direction));
        return;
    case K::Curve: {
        const double total = curveLength(e.segments);
        lines << QStringLiteral("Lunghezza: %1").arg(length(total));
        label = QStringLiteral("L %1").arg(length(total));
        if (e.segments.size() == 1) {
            const PathSegment &s = e.segments.front();
            const Vec3 a = s.curve->point(s.range.lo), b = s.curve->point(s.range.hi);
            Vec3 direction;
            const Conic c = conicOf(*s.curve);
            if (lineDirection(*s.curve, direction)) {
                const Vec3 d = b - a;
                lines << QStringLiteral("Segmento, direzione %1").arg(vector(direction))
                      << QStringLiteral("ΔX: %1   ΔY: %2   ΔZ: %3").arg(length(std::fabs(d.x())), length(std::fabs(d.y())), length(std::fabs(d.z())));
            } else if (c.ok && c.circle) {
                const bool full = s.range.length() >= kTwoPi - 1e-9;
                lines << (full ? QStringLiteral("Cerchio") : QStringLiteral("Arco di %1").arg(angle(s.range.length())))
                      << QStringLiteral("Raggio: %1").arg(length(c.r1)) << QStringLiteral("Diametro: %1").arg(length(2.0 * c.r1))
                      << QStringLiteral("Centro: %1").arg(point(c.center)) << QStringLiteral("Normale: %1").arg(vector(c.normal));
                label = full ? QStringLiteral("⌀ %1").arg(length(2.0 * c.r1)) : QStringLiteral("R %1").arg(length(c.r1));
            } else if (c.ok) {
                lines << QStringLiteral("Ellisse") << QStringLiteral("Semiassi: %1, %2").arg(length(c.r1), length(c.r2))
                      << QStringLiteral("Centro: %1").arg(point(c.center));
            } else {
                lines << QStringLiteral("Curva libera");
            }
            if (distance(a, b) > 1e-12) lines << QStringLiteral("Estremi: %1 → %2").arg(point(a), point(b));
        }
        return;
    }
    case K::Face: {
        const Body &body = *e.body;
        const Face &face = body.face(e.face);
        const double area = faceArea(body, e.face, 1e-10);
        double perimeter = 0.0;
        for (EdgeId edge : faceBoundaryEdges(body, e.face)) perimeter += arcLength(*body.edge(edge).curve, body.edge(edge).range, 1e-10);
        lines << QStringLiteral("Area: %1").arg(formatArea(area, 6)) << QStringLiteral("Perimetro: %1").arg(length(perimeter));
        label = QStringLiteral("A %1").arg(formatArea(area, 4));
        const Surface &s = *face.surface;
        switch (s.type()) {
        case SurfaceType::Plane: {
            Vec3 n;
            planeOf(e, n);
            lines << QStringLiteral("Faccia piana, normale %1").arg(vector(n));
            break;
        }
        case SurfaceType::Cylinder: {
            const auto &c = static_cast<const CylindricalSurface &>(s);
            lines << QStringLiteral("Faccia cilindrica") << QStringLiteral("Raggio: %1").arg(length(c.radius()))
                  << QStringLiteral("Diametro: %1").arg(length(2.0 * c.radius()))
                  << QStringLiteral("Asse: per %1, direzione %2").arg(point(c.frame().origin()), vector(c.frame().zDir()));
            label = QStringLiteral("⌀ %1").arg(length(2.0 * c.radius()));
            break;
        }
        case SurfaceType::Cone: {
            const auto &c = static_cast<const ConicalSurface &>(s);
            lines << QStringLiteral("Faccia conica") << QStringLiteral("Semiangolo: %1").arg(angle(std::fabs(c.semiAngle())))
                  << QStringLiteral("Angolo al vertice: %1").arg(angle(2.0 * std::fabs(c.semiAngle())))
                  << QStringLiteral("Asse: per %1, direzione %2").arg(point(c.frame().origin()), vector(c.frame().zDir()));
            break;
        }
        case SurfaceType::Sphere: {
            const auto &c = static_cast<const SphericalSurface &>(s);
            lines << QStringLiteral("Faccia sferica") << QStringLiteral("Raggio: %1").arg(length(c.radius()))
                  << QStringLiteral("Centro: %1").arg(point(c.frame().origin()));
            label = QStringLiteral("SR %1").arg(length(c.radius()));
            break;
        }
        case SurfaceType::Torus: {
            const auto &c = static_cast<const ToroidalSurface &>(s);
            lines << QStringLiteral("Faccia toroidale") << QStringLiteral("Raggio maggiore: %1").arg(length(c.majorRadius()))
                  << QStringLiteral("Raggio minore: %1").arg(length(c.minorRadius()));
            break;
        }
        default: lines << QStringLiteral("Superficie libera"); break;
        }
        return;
    }
    case K::None: break;
    }
}

}  // namespace

Vec3 edgeMidpoint(const Body &body, EdgeId edge) {
    const Edge &e = body.edge(edge);
    Vec3 direction;
    const Conic c = conicOf(*e.curve);
    // Rette e cerchi: il parametro e' proporzionale alla lunghezza.
    if (lineDirection(*e.curve, direction) || (c.ok && c.circle)) return e.curve->point(0.5 * (e.range.lo + e.range.hi));
    const double half = 0.5 * arcLength(*e.curve, e.range, 1e-12);
    double lo = e.range.lo, hi = e.range.hi;
    for (int iteration = 0; iteration < 60 && hi - lo > 1e-15 * std::max(1.0, std::fabs(hi)); ++iteration) {
        const double mid = 0.5 * (lo + hi);
        if (arcLength(*e.curve, Interval{e.range.lo, mid}, 1e-12) < half) lo = mid;
        else hi = mid;
    }
    return e.curve->point(0.5 * (lo + hi));
}

bool edgeCenter(const Body &body, EdgeId edge, Vec3 &center) {
    const Conic c = conicOf(*body.edge(edge).curve);
    if (c.ok) center = c.center;
    return c.ok;
}

bool measureEntity(const GeometryRef &ref, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                   MeasureEntity &entity, QString *error) {
    using K = MeasureEntity::Kind;
    entity = MeasureEntity();
    const int owner = int(bodies.size());
    try {
        if (ref.kind == kGeometryRefEdgeMidpoint || ref.kind == kGeometryRefEdgeCenter || ref.kind == 5) {
            const int index = bodyOf(ref, bodies);
            if (index < 0 || !bodies.at(index).forgeBody) return setError(error, QStringLiteral("il corpo non esiste piu'")), false;
            const Body &body = *bodies.at(index).forgeBody;
            if (ref.kind == 5) {
                const FaceId face = resolveFaceReference(body, ref.point, std::numeric_limits<double>::max());
                if (!face.valid()) return setError(error, QStringLiteral("la faccia non esiste piu'")), false;
                entity.kind = K::Face;
                entity.body = bodies.at(index).forgeBody;
                entity.face = face;
                entity.name = QStringLiteral("Faccia di %1").arg(bodies.at(index).name);
                return true;
            }
            const EdgeId edge = resolveEdgeReference(body, ref.point, std::numeric_limits<double>::max());
            if (!edge.valid()) return setError(error, QStringLiteral("lo spigolo non esiste piu'")), false;
            entity.kind = K::Point;
            if (ref.kind == kGeometryRefEdgeMidpoint) {
                entity.point = edgeMidpoint(body, edge);
                entity.name = QStringLiteral("Punto medio di uno spigolo di %1").arg(bodies.at(index).name);
            } else {
                if (!edgeCenter(body, edge, entity.point)) return setError(error, QStringLiteral("lo spigolo non e' circolare")), false;
                entity.name = QStringLiteral("Centro di uno spigolo di %1").arg(bodies.at(index).name);
            }
            return true;
        }
        entity.name = geometryRefText(ref, sketches, bodies);
        if (ref.kind == 4 || ref.kind == 7 || ref.kind == 9) {
            entity.kind = K::Curve;
            return geometryRefPath(ref, owner, sketches, bodies, entity.segments, error);
        }
        ResolvedRef resolved;
        if (!resolveGeometryRef(ref, owner, sketches, bodies, resolved, error)) return false;
        entity.point = resolved.point;
        entity.direction = resolved.direction;
        if (ref.kind == 2 && resolved.hasLine) entity.kind = K::Line;
        else if ((ref.kind == 1 || ref.kind == 8) && resolved.hasPlane) entity.kind = K::Plane;
        else if (resolved.hasPoint) entity.kind = K::Point;
        else return setError(error, QStringLiteral("riferimento non misurabile")), false;
        return true;
    } catch (const std::exception &failure) {
        return setError(error, QString::fromUtf8(failure.what())), false;
    }
}

MeasureReport measureEntities(const QVector<MeasureEntity> &entities) {
    MeasureReport report;
    try {
        if (entities.size() == 1) {
            describe(entities.first(), report.lines, report.label);
            report.ok = true;
            return report;
        }
        if (entities.size() != 2) {
            report.error = QStringLiteral("Scegli una o due entita'.");
            return report;
        }
        const MeasureEntity &a = entities.at(0), &b = entities.at(1);
        const Prepared pa = prepare(a), pb = prepare(b);
        Vec3 from, to;
        if (!minimumDistance(pa, pb, from, to)) {
            report.error = QStringLiteral("Distanza non calcolabile.");
            return report;
        }
        report.hasDistance = true;
        report.from = from;
        report.to = to;
        report.distance = distance(from, to);
        const Vec3 d = to - from;
        report.lines << QStringLiteral("Distanza minima: %1").arg(length(report.distance))
                     << QStringLiteral("ΔX: %1   ΔY: %2   ΔZ: %3").arg(length(std::fabs(d.x())), length(std::fabs(d.y())), length(std::fabs(d.z())))
                     << QStringLiteral("Punto su A: %1").arg(point(from)) << QStringLiteral("Punto su B: %1").arg(point(to));
        report.label = length(report.distance);
        // Angoli.
        Vec3 la, lb, na, nb;
        const bool lineA = lineOf(a, la), lineB = lineOf(b, lb), planeA = planeOf(a, na), planeB = planeOf(b, nb);
        if (lineA && lineB) {
            const double t = lineAngle(la, lb);
            report.lines << QStringLiteral("Angolo tra le rette: %1 (supplementare %2)").arg(angle(t), angle(kPi - t));
        } else if (planeA && planeB) {
            const double between = std::atan2(norm(cross(na, nb)), dot(na, nb));
            report.lines << QStringLiteral("Angolo tra i piani: %1").arg(angle(lineAngle(na, nb)))
                         << QStringLiteral("Angolo tra le normali: %1").arg(angle(between));
            if (norm(cross(na, nb)) < 1e-12)  // from e to stanno sui due piani
                report.lines << QStringLiteral("Piani paralleli, distanza: %1").arg(length(std::fabs(dot(to - from, na))));
        } else if ((lineA && planeB) || (planeA && lineB)) {
            const Vec3 line = lineA ? la : lb, normal = planeA ? na : nb;
            report.lines << QStringLiteral("Angolo retta-piano: %1").arg(angle(0.5 * kPi - lineAngle(line, normal)));
        }
        // Assi e centri: interassi dei fori, centro dall'asse.
        Vec3 ap, ad, bp, bd, ca, cb;
        const bool axisA = axisOf(a, ap, ad), axisB = axisOf(b, bp, bd);
        const bool centerA = centerOf(a, ca), centerB = centerOf(b, cb);
        if (axisA && axisB && a.kind != MeasureEntity::Kind::Line && b.kind != MeasureEntity::Kind::Line) {
            const Vec3 n = cross(ad, bd);
            if (norm(n) < 1e-12) {
                const Vec3 w = bp - ap;
                report.lines << QStringLiteral("Assi paralleli, interasse: %1").arg(length(norm(w - dot(w, ad) * ad)));
            } else {
                report.lines << QStringLiteral("Distanza tra gli assi: %1").arg(length(std::fabs(dot(bp - ap, normalized(n)))));
            }
        } else if (axisA != axisB && (axisA ? centerB : centerA)) {
            const Vec3 axisPoint = axisA ? ap : bp, axisDirection = axisA ? ad : bd, c = axisA ? cb : ca;
            const Vec3 w = c - axisPoint;
            report.lines << QStringLiteral("Distanza del centro dall'asse: %1").arg(length(norm(w - dot(w, axisDirection) * axisDirection)));
        }
        if (centerA && centerB && !(a.kind == MeasureEntity::Kind::Point && b.kind == MeasureEntity::Kind::Point)) {
            const Vec3 c = cb - ca;
            report.lines << QStringLiteral("Distanza tra i centri: %1").arg(length(norm(c)))
                         << QStringLiteral("  ΔX: %1   ΔY: %2   ΔZ: %3").arg(length(std::fabs(c.x())), length(std::fabs(c.y())), length(std::fabs(c.z())));
        }
        report.ok = true;
    } catch (const std::exception &failure) {
        report.error = QString::fromUtf8(failure.what());
    }
    return report;
}

}
