#include "fk_blend.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_extrude.h"
#include "fk_intersect.h"
#include "fk_precision.h"
#include "fk_profile.h"
#include "fk_revolve.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

Vec2 perp(const Vec2 &v) { return Vec2(-v.y(), v.x()); }
double cross2(const Vec2 &a, const Vec2 &b) { return a.x() * b.y() - a.y() * b.x(); }
double angleOf(const Vec2 &v) { return std::atan2(v.y(), v.x()); }

// Sezione di una faccia nel piano (o semipiano) dello spigolo: la retta per
// lo spigolo o un cerchio, con la direzione `t` in cui la faccia si
// allontana dallo spigolo e la normale `m` verso il materiale.
struct SectionFace {
    bool circle = false;
    Vec2 center;  // cerchio
    double radius = 0.0;
    Vec2 t, m;
};

// Curva spostata di `offset` dalla parte `side` (vettore unitario nello spigolo).
SectionFace shifted(const SectionFace &face, const Vec2 &corner, const Vec2 &side, double offset) {
    SectionFace result = face;
    if (!face.circle) {
        result.center = corner + offset * side;  // punto della retta
        return result;
    }
    result.radius = dot(side, face.center - corner) > 0.0 ? face.radius - offset : face.radius + offset;
    if (!(result.radius > 0.0)) throw std::domain_error("blendEdges: raggio maggiore della curvatura di una faccia");
    return result;
}

// Punti comuni di due curve della sezione (una retta passa per `center` con direzione `t`).
std::vector<Vec2> meet(const SectionFace &a, const SectionFace &b) {
    std::vector<Vec2> result;
    if (!a.circle && !b.circle) {
        const double det = cross2(a.t, b.t);
        if (std::fabs(det) < 1e-14) return result;
        const double s = cross2(b.center - a.center, b.t) / det;
        result.push_back(a.center + s * a.t);
        return result;
    }
    if (a.circle && !b.circle) return meet(b, a);
    if (!a.circle && b.circle) {
        const Vec2 w = a.center - b.center;
        const double p = dot(a.t, w), q = squaredNorm(w) - b.radius * b.radius, disc = p * p - q;
        if (disc < 0.0) return result;
        for (double sign : {-1.0, 1.0}) result.push_back(a.center + (-p + sign * std::sqrt(disc)) * a.t);
        return result;
    }
    const Vec2 d = b.center - a.center;
    const double l = norm(d);
    if (!(l > 0.0)) return result;
    const double x = (l * l + a.radius * a.radius - b.radius * b.radius) / (2.0 * l), h2 = a.radius * a.radius - x * x;
    if (h2 < 0.0) return result;
    const Vec2 base = a.center + (x / l) * d, n = perp(d / l);
    for (double sign : {-1.0, 1.0}) result.push_back(base + sign * std::sqrt(h2) * n);
    return result;
}

Vec2 foot(const SectionFace &face, const Vec2 &corner, const Vec2 &p) {
    if (!face.circle) return corner + dot(p - corner, face.t) * face.t;
    return face.center + face.radius * normalized(p - face.center);
}

// Tratto della curva della faccia dallo spigolo `corner` al punto `to`.
ProfileSegment along(const SectionFace &face, const Vec2 &corner, const Vec2 &to) {
    if (!face.circle) {
        if (!(dot(to - corner, face.t) > 0.0)) throw std::domain_error("blendEdges: raggio troppo grande per le facce dello spigolo");
        return {std::make_shared<Line<2>>(corner, to - corner), {0.0, distance(corner, to)}};
    }
    const bool ccw = cross2(corner - face.center, face.t) > 0.0;
    const double a0 = angleOf(corner - face.center);
    double a1 = angleOf(to - face.center);
    if (ccw) {
        while (a1 <= a0) a1 += kTwoPi;
    } else {
        while (a1 >= a0) a1 -= kTwoPi;
    }
    if (std::fabs(a1 - a0) >= kPi) throw std::domain_error("blendEdges: raggio troppo grande per le facce dello spigolo");
    auto circle = std::make_shared<Circle<2>>(makeCircle(face.center, face.radius));
    return {circle, {std::min(a0, a1), std::max(a0, a1)}};
}

// Zona tra lo spigolo e il raccordo (o lo smusso) nella sezione.
// Con `outward` > 0 la zona non segue le facce ma se ne scosta di `outward`
// (fuori dal solido per gli spigoli convessi, dentro il materiale per quelli
// concavi): niente facce coincidenti con quelle del solido nella booleana (le superfici di rivoluzione coassiali
// che coincidono in parte e toccano il toro del raccordo sono il caso difficile).
ProfileRegion blendRegion(const SectionFace &f1, const SectionFace &f2, const Vec2 &corner, double size, bool chamfer, bool convex,
                          double tolerance, double outward = 0.0) {
    Vec2 t1, t2;
    std::vector<ProfileSegment> segments;
    if (chamfer) {
        auto at = [&](const SectionFace &face) {
            if (!face.circle) return corner + size * face.t;
            SectionFace ring;
            ring.circle = true;
            ring.center = corner;
            ring.radius = size;
            for (const Vec2 &p : meet(face, ring))
                if (dot(p - corner, face.t) > 0.0) return p;
            throw std::domain_error("blendEdges: distanza dello smusso troppo grande");
        };
        t1 = at(f1);
        t2 = at(f2);
        segments.push_back({std::make_shared<Line<2>>(t1, t2 - t1), {0.0, distance(t1, t2)}});
    } else {
        // Il cerchio sta nell'angolo minore tra le facce: dalla parte del
        // materiale se lo spigolo e' convesso, dall'altra se e' concavo.
        const double sign = convex ? 1.0 : -1.0;
        const std::vector<Vec2> centers = meet(shifted(f1, corner, sign * f1.m, size), shifted(f2, corner, sign * f2.m, size));
        if (centers.empty()) throw std::domain_error("blendEdges: raccordo impossibile tra le facce dello spigolo");
        Vec2 center = centers.front();
        for (const Vec2 &c : centers)
            if (distance(c, corner) < distance(center, corner)) center = c;
        t1 = foot(f1, corner, center);
        t2 = foot(f2, corner, center);
        const double a1 = angleOf(t1 - center);
        double delta = std::remainder(angleOf(t2 - center) - a1, kTwoPi);
        const Vec2 middle(std::cos(a1 + 0.5 * delta), std::sin(a1 + 0.5 * delta));
        if (dot(middle, corner - center) < 0.0) delta += delta > 0.0 ? -kTwoPi : kTwoPi;
        auto arc = std::make_shared<Circle<2>>(makeCircle(center, size));
        segments.push_back({arc, {std::min(a1, a1 + delta), std::max(a1, a1 + delta)}});
    }
    if (outward > 0.0) {
        // Normale uscente della faccia in un suo punto della sezione.
        auto normalAt = [&](const SectionFace &face, const Vec2 &p) {
            if (!face.circle) return -face.m;
            const Vec2 radial = normalized(p - face.center);
            return dot(radial, normalized(corner - face.center)) * dot(-face.m, normalized(corner - face.center)) >= 0.0 ? radial : -radial;
        };
        (void)along(f1, corner, t1);  // controlla che i punti stiano sulle facce
        (void)along(f2, corner, t2);
        // Spigolo convesso: fuori dal solido; concavo (la zona si aggiunge):
        // dentro il materiale, dove l'unione non cambia nulla.
        const double side = convex ? outward : -outward;
        const Vec2 p1 = t1 + side * normalAt(f1, t1), p2 = t2 + side * normalAt(f2, t2);
        const Vec2 p12 = corner - side * (f1.m + f2.m);
        for (const auto &[a, b] : {std::pair<Vec2, Vec2>{t1, p1}, {p1, p12}, {p12, p2}, {p2, t2}})
            segments.push_back({std::make_shared<Line<2>>(a, b - a), {0.0, distance(a, b)}});
    } else {
        segments.push_back(along(f1, corner, t1));
        segments.push_back(along(f2, corner, t2));
    }
    const Profile profile = buildProfile(segments, tolerance);
    if (profile.regions.size() != 1) throw std::domain_error("blendEdges: zona del raccordo non valida");
    return profile.regions.front();
}

Vec3 outwardNormal(const Body &body, FaceId f, const Vec3 &x) {
    const Face &face = body.face(f);
    const SurfaceProjection p = projectPoint(*face.surface, x);
    const Vec3 n = normalAt(*face.surface, p.u, p.v);
    return face.sense ? n : -n;
}

bool parallel(const Vec3 &a, const Vec3 &b) { return norm(cross(normalized(a), normalized(b))) <= 1e-9; }

}

EdgeId nearestEdge(const Body &body, const Vec3 &point, double tolerance) {
    EdgeId best;
    double closest = tolerance;
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        if (!edge.curve) continue;
        const double d = projectPoint(*edge.curve, point, edge.range).distance;
        if (d <= closest) {
            closest = d;
            best = e;
        }
    }
    return best;
}

Body blendEdges(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer) {
    if (body.isSheet()) throw std::domain_error("blendEdges: solo solidi");
    if (!(size > kLinearResolution)) throw std::domain_error("blendEdges: raggio o distanza non validi");
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    const double scale = std::max(box.diagonal(), 1.0), tolerance = 1e-9 * scale, probe = 1e-4 * std::min(scale, size);

    // Tre spigoli scelti in un vertice chiederebbero la pezza d'angolo.
    std::map<int, int> perVertex;
    for (EdgeId e : edges)
        if (!body.isLaminar(e)) {
            ++perVertex[body.edgeStart(e).index];
            if (body.edgeEnd(e) != body.edgeStart(e)) ++perVertex[body.edgeEnd(e).index];
        }
    for (const auto &[vertex, count] : perVertex)
        if (count >= 3) throw std::domain_error("blendEdges: tre o piu' spigoli in un vertice (pezza d'angolo) non gestiti");

    struct Tool {
        Body body;
        bool add;
    };
    std::vector<Tool> tools;
    for (EdgeId e : edges) {
        const Edge &edge = body.edge(e);
        if (!edge.curve || body.isLaminar(e)) throw std::domain_error("blendEdges: spigolo non valido");
        const FaceId faces[2] = {body.finFace(edge.forward), body.finFace(edge.backward)};
        if (faces[0] == faces[1]) throw std::domain_error("blendEdges: spigolo interno a una faccia");
        const Vec3 start = edge.curve->point(edge.range.lo), end = edge.curve->point(edge.range.hi);
        const Vec3 middle = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
        const bool straight = distance(start, end) > tolerance
                           && norm(cross(middle - start, normalized(end - start))) <= tolerance;
        const bool round = edge.curve->type() == CurveType::Circle && edge.range.length() >= kTwoPi - 1e-9;
        if (!straight && !round) throw std::domain_error("blendEdges: solo spigoli rettilinei o circolari chiusi");

        // Sistema della sezione: (e1, e2) nel piano normale allo spigolo
        // rettilineo, o (radiale, asse) nel semipiano di quello circolare.
        Vec3 origin, e1, e2, axis;
        Vec3 at;  // punto dello spigolo nella sezione
        if (straight) {
            // La sezione a meta' dello spigolo (lontano dai vertici, dove le
            // prove di appartenenza alle facce cadrebbero sui loro bordi).
            axis = normalized(end - start);
            origin = start;
            at = middle;
            e1 = normalized(std::fabs(axis.x()) < 0.9 ? cross(axis, Vec3(1, 0, 0)) : cross(axis, Vec3(0, 1, 0)));
            e2 = cross(axis, e1);
        } else {
            const auto &circle = static_cast<const Circle<3> &>(*edge.curve);
            origin = circle.center();
            axis = normalized(cross(circle.xAxis(), circle.yAxis()));
            e1 = normalized(middle - origin);
            e2 = axis;
            at = middle;
        }
        auto map = [&](const Vec3 &p) { return Vec2(dot(p - origin, e1), dot(p - origin, e2)); };
        const Vec2 corner = map(at);

        SectionFace section[2];
        for (int k = 0; k < 2; ++k) {
            const Face &face = body.face(faces[k]);
            const Surface &surface = *face.surface;
            SectionFace &s = section[k];
            const Vec3 n = outwardNormal(body, faces[k], at);
            s.m = -normalized(Vec2(dot(n, e1), dot(n, e2)));
            switch (surface.type()) {
            case SurfaceType::Plane: {
                const Vec3 normal = static_cast<const Plane &>(surface).frame().zDir();
                if (straight ? std::fabs(dot(normal, axis)) > 1e-9 : !parallel(normal, axis))
                    throw std::domain_error("blendEdges: faccia piana non perpendicolare alla sezione dello spigolo");
                break;
            }
            case SurfaceType::Cylinder: {
                const auto &cylinder = static_cast<const CylindricalSurface &>(surface);
                const Frame3 &f = cylinder.frame();
                if (!parallel(f.zDir(), axis)) throw std::domain_error("blendEdges: cilindro non parallelo allo spigolo");
                if (straight) {
                    s.circle = true;
                    s.center = map(f.origin());
                    s.radius = cylinder.radius();
                } else if (norm(cross(origin - f.origin(), f.zDir())) > 1e-9 * scale) {
                    throw std::domain_error("blendEdges: cilindro non coassiale allo spigolo circolare");
                }
                break;
            }
            case SurfaceType::Cone: {
                const auto &cone = static_cast<const ConicalSurface &>(surface);
                if (straight || !parallel(cone.frame().zDir(), axis) || norm(cross(origin - cone.frame().origin(), cone.frame().zDir())) > 1e-9 * scale)
                    throw std::domain_error("blendEdges: cono non coassiale allo spigolo circolare");
                break;
            }
            case SurfaceType::Sphere: {
                const auto &sphere = static_cast<const SphericalSurface &>(surface);
                if (straight || norm(cross(sphere.frame().origin() - origin, axis)) > 1e-9 * scale)
                    throw std::domain_error("blendEdges: sfera non centrata sull'asse dello spigolo circolare");
                s.circle = true;
                s.center = map(sphere.frame().origin());
                s.radius = sphere.radius();
                break;
            }
            case SurfaceType::Torus: {
                const auto &torus = static_cast<const ToroidalSurface &>(surface);
                const Frame3 &f = torus.frame();
                if (straight || !parallel(f.zDir(), axis) || norm(cross(f.origin() - origin, axis)) > 1e-9 * scale)
                    throw std::domain_error("blendEdges: toro non coassiale allo spigolo circolare");
                s.circle = true;
                s.center = Vec2(torus.majorRadius(), dot(f.origin() - origin, axis));
                s.radius = torus.minorRadius();
                break;
            }
            default:
                throw std::domain_error("blendEdges: tipo di faccia non gestito");
            }
            // La faccia si allontana dallo spigolo lungo la tangente alla sua
            // sezione (perpendicolare alla normale), nel verso in cui c'e' la faccia.
            bool found = false;
            for (double sign : {1.0, -1.0}) {
                const Vec2 t = sign * perp(s.m);
                const Vec3 test = projectPoint(surface, at + probe * (t.x() * e1 + t.y() * e2)).point;
                if (classifyPointOnFace(body, faces[k], test, tolerance) == PointLocation::Inside) {
                    s.t = t;
                    found = true;
                    break;
                }
            }
            if (!found) throw std::domain_error("blendEdges: faccia dello spigolo troppo stretta");
        }
        const double turn = dot(section[1].t, section[0].m);
        if (std::fabs(turn) <= 1e-9) throw std::domain_error("blendEdges: spigolo tra facce tangenti");
        const bool convex = turn > 0.0;

        if (straight) {
            // Le facce agli estremi devono essere piani perpendicolari allo
            // spigolo: la zona estrusa finisce li'.
            for (VertexId v : {body.edgeStart(e), body.edgeEnd(e)})
                for (FinId fin : body.fins()) {
                    if (body.finStart(fin) != v) continue;
                    const FaceId g = body.finFace(fin);
                    if (g == faces[0] || g == faces[1]) continue;
                    const Surface &surface = *body.face(g).surface;
                    if (surface.type() != SurfaceType::Plane || !parallel(static_cast<const Plane &>(surface).frame().zDir(), axis))
                        throw std::domain_error("blendEdges: lo spigolo finisce contro una faccia non perpendicolare");
                }
        }
        const ProfileRegion region = blendRegion(section[0], section[1], corner, size, chamfer, convex, tolerance, straight ? 0.0 : 0.05 * size);
        Tool tool;
        tool.add = !convex;
        if (straight) tool.body = makeExtrusion(Frame3(origin, axis, e1), region, distance(start, end));
        else tool.body = makeRevolution(Frame3(origin, axis, e1), region);
        tools.push_back(std::move(tool));
    }
    Body result = body;
    for (const Tool &tool : tools) result = booleanOperation(result, tool.body, tool.add ? BooleanOperation::Unite : BooleanOperation::Subtract);
    return result;
}

}
