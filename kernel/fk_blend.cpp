#include "fk_blend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

#include "fk_blend_loop.h"
#include "fk_blend_surface.h"
#include "fk_blend_model.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_extrude.h"
#include "fk_intersect.h"
#include "fk_pcurve.h"
#include "fk_parallel.h"
#include "fk_precision.h"
#include "fk_primitives.h"
#include "fk_profile.h"
#include "fk_revolve.h"
#include "fk_surface_algo.h"
#include "fk_unify.h"

namespace ForgeCad::Kernel {
namespace {

Vec2 perp(const Vec2 &v) { return Vec2(-v.y(), v.x()); }
double cross2(const Vec2 &a, const Vec2 &b) { return a.x() * b.y() - a.y() * b.x(); }
double angleOf(const Vec2 &v) { return std::atan2(v.y(), v.x()); }

// Sezione di una faccia nel piano (o semipiano) dello spigolo: la retta per
// lo spigolo o un cerchio, con la direzione `t` in cui la faccia si
// allontana da `base` e la normale `m` verso il materiale (in `base`).
// `base` e' lo spigolo per le due facce dello spigolo; per le facce che
// continuano un appoggio (sotto) e' il bordo comune con la faccia precedente.
struct SectionFace {
    bool circle = false;
    Vec2 center;  // cerchio
    double radius = 0.0;
    Vec2 t, m;
    Vec2 base;
};

// Curva spostata di `offset` dalla parte `side` (vettore unitario in `base`).
SectionFace shifted(const SectionFace &face, const Vec2 &side, double offset) {
    SectionFace result = face;
    if (!face.circle) {
        result.center = face.base + offset * side;  // punto della retta
        return result;
    }
    result.radius = dot(side, face.center - face.base) > 0.0 ? face.radius - offset : face.radius + offset;
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

Vec2 foot(const SectionFace &face, const Vec2 &p) {
    if (!face.circle) return face.base + dot(p - face.base, face.t) * face.t;
    return face.center + face.radius * normalized(p - face.center);
}

// Cammino con segno lungo la curva della faccia da `base` al piede di `p`
// (positivo nel verso di `t`; sul cerchio entro mezzo giro).
double travel(const SectionFace &face, const Vec2 &p) {
    if (!face.circle) return dot(p - face.base, face.t);
    const bool ccw = cross2(face.base - face.center, face.t) > 0.0;
    const double delta = std::remainder(angleOf(p - face.center) - angleOf(face.base - face.center), kTwoPi);
    return (ccw ? delta : -delta) * face.radius;
}

// Tratto della curva della faccia da `base` al punto `to`.
ProfileSegment along(const SectionFace &face, const Vec2 &to) {
    const Vec2 &corner = face.base;
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

// Normale uscente della faccia in un suo punto della sezione.
Vec2 sectionNormal(const SectionFace &face, const Vec2 &p) {
    if (!face.circle) return -face.m;
    const Vec2 radial = normalized(p - face.center), atBase = normalized(face.base - face.center);
    return dot(-face.m, atBase) >= 0.0 ? radial : -radial;
}

// Appoggio di un lato del raccordo nella sezione: la faccia dello spigolo e,
// se e' piu' corta del raccordo, le facce che la continuano tangenti (ognuna
// parte dal bordo comune con la precedente, `base`). La palla tocca l'ultima.
using SectionSupport = std::vector<SectionFace>;

// Centro del raccordo di raggio `size` tra gli ultimi tratti dei due appoggi:
// il piu' vicino allo spigolo; se un appoggio ha piu' facce, tra i centri
// con i punti di contatto davanti alle basi.
bool filletCenter(const SectionSupport &s1, const SectionSupport &s2, const Vec2 &corner, double size, bool convex, double tolerance, Vec2 &center) {
    const SectionFace &f1 = s1.back(), &f2 = s2.back();
    const double sign = convex ? 1.0 : -1.0;
    const std::vector<Vec2> centers = meet(shifted(f1, sign * f1.m, size), shifted(f2, sign * f2.m, size));
    const bool composite = s1.size() > 1 || s2.size() > 1;
    bool found = false;
    for (const Vec2 &c : centers) {
        if (composite && (travel(f1, foot(f1, c)) < -tolerance || travel(f2, foot(f2, c)) < -tolerance)) continue;
        if (!found || distance(c, corner) < distance(center, corner)) center = c;
        found = true;
    }
    return found;
}

// Punti di un tratto dell'appoggio da `from` a `to` (compresi): gli estremi
// sulle rette; sui cerchi abbastanza fitti perche' le corde tra i punti
// scostati di `offset` restino dalla stessa parte della faccia.
std::vector<Vec2> supportSamples(const SectionFace &face, const Vec2 &from, const Vec2 &to, double offset, bool dense) {
    if (!face.circle || !dense) return {from, to};
    const bool ccw = cross2(face.base - face.center, face.t) > 0.0;
    const double a0 = angleOf(from - face.center);
    double delta = std::remainder(angleOf(to - face.center) - a0, kTwoPi);
    if (ccw && delta < 0.0) delta += kTwoPi;
    if (!ccw && delta > 0.0) delta -= kTwoPi;
    const double step = std::acos(face.radius / (face.radius + offset));  // meta' dell'angolo con freccia = offset
    const int n = std::max(1, int(std::ceil(std::fabs(delta) / std::max(step, 1e-3))));
    std::vector<Vec2> points{from};
    for (int i = 1; i < n; ++i) {
        const double a = a0 + delta * double(i) / double(n);
        points.push_back(face.center + face.radius * Vec2(std::cos(a), std::sin(a)));
    }
    points.push_back(to);
    return points;
}

// Zona tra lo spigolo e il raccordo (o lo smusso) nella sezione.
// Con `outward` > 0 la zona non segue le facce ma se ne scosta di `outward`
// (fuori dal solido per gli spigoli convessi, dentro il materiale per quelli
// concavi): niente facce coincidenti con quelle del solido nella booleana (le superfici di rivoluzione coassiali
// che coincidono in parte e toccano il toro del raccordo sono il caso difficile).
// Smusso: `size` dallo spigolo sulla prima faccia, `size2` sulla seconda (< 0: `size`).
// Gli appoggi con piu' facce valgono solo per il raccordo.
ProfileRegion blendRegion(const SectionSupport &s1, const SectionSupport &s2, const Vec2 &corner, double size, bool chamfer, bool convex,
                          double tolerance, double outward = 0.0, std::vector<Vec2> *extent = nullptr, double size2 = -1.0) {
    const SectionFace &f1 = s1.back(), &f2 = s2.back();
    const bool composite = s1.size() > 1 || s2.size() > 1;
    Vec2 t1, t2;
    std::vector<ProfileSegment> segments;
    if (chamfer) {
        if (composite) throw std::logic_error("blendEdges: smusso con un appoggio di piu' facce");
        auto at = [&](const SectionFace &face, double d) {
            if (!face.circle) return corner + d * face.t;
            SectionFace ring;
            ring.circle = true;
            ring.center = corner;
            ring.radius = d;
            for (const Vec2 &p : meet(face, ring))
                if (dot(p - corner, face.t) > 0.0) return p;
            throw std::domain_error("blendEdges: distanza dello smusso troppo grande");
        };
        t1 = at(f1, size);
        t2 = at(f2, size2 < 0.0 ? size : size2);
        segments.push_back({std::make_shared<Line<2>>(t1, t2 - t1), {0.0, distance(t1, t2)}});
    } else {
        // Il cerchio sta nell'angolo minore tra le facce: dalla parte del
        // materiale se lo spigolo e' convesso, dall'altra se e' concavo.
        Vec2 center;
        if (!filletCenter(s1, s2, corner, size, convex, tolerance, center)) throw std::domain_error("blendEdges: raccordo impossibile tra le facce dello spigolo");
        t1 = foot(f1, center);
        t2 = foot(f2, center);
        const double a1 = angleOf(t1 - center);
        double delta = std::remainder(angleOf(t2 - center) - a1, kTwoPi);
        const Vec2 middle(std::cos(a1 + 0.5 * delta), std::sin(a1 + 0.5 * delta));
        if (!composite && dot(middle, corner - center) < 0.0) delta += delta > 0.0 ? -kTwoPi : kTwoPi;
        auto arc = std::make_shared<Circle<2>>(makeCircle(center, size));
        segments.push_back({arc, {std::min(a1, a1 + delta), std::max(a1, a1 + delta)}});
    }
    // Tratti di un appoggio dallo spigolo al punto di contatto (controlla che i punti stiano sulle facce).
    auto path = [&](const SectionSupport &support, const Vec2 &contact) {
        std::vector<ProfileSegment> pieces;
        for (std::size_t i = 0; i < support.size(); ++i) pieces.push_back(along(support[i], i + 1 < support.size() ? support[i + 1].base : contact));
        return pieces;
    };
    const std::vector<ProfileSegment> path1 = path(s1, t1), path2 = path(s2, t2);
    if (outward > 0.0) {
        // Spigolo convesso: fuori dal solido; concavo (la zona si aggiunge):
        // dentro il materiale, dove l'unione non cambia nulla.
        const double side = convex ? outward : -outward;
        // Punti scostati lungo un appoggio, dallo spigolo (escluso) al contatto.
        auto offsets = [&](const SectionSupport &support, const Vec2 &contact) {
            std::vector<Vec2> points;
            for (std::size_t i = 0; i < support.size(); ++i) {
                const Vec2 to = i + 1 < support.size() ? support[i + 1].base : contact;
                const std::vector<Vec2> samples = supportSamples(support[i], support[i].base, to, outward, composite);
                for (std::size_t j = 1; j < samples.size(); ++j) points.push_back(samples[j] + side * sectionNormal(support[i], samples[j]));
            }
            return points;
        };
        std::vector<Vec2> polyline{t1};
        const std::vector<Vec2> o1 = offsets(s1, t1), o2 = offsets(s2, t2);
        polyline.insert(polyline.end(), o1.rbegin(), o1.rend());
        polyline.push_back(corner - side * (f1.m + f2.m));
        if (composite) polyline.back() = corner - side * (s1.front().m + s2.front().m);
        polyline.insert(polyline.end(), o2.begin(), o2.end());
        polyline.push_back(t2);
        for (std::size_t i = 0; i + 1 < polyline.size(); ++i)
            segments.push_back({std::make_shared<Line<2>>(polyline[i], polyline[i + 1] - polyline[i]), {0.0, distance(polyline[i], polyline[i + 1])}});
    } else {
        segments.insert(segments.end(), path1.begin(), path1.end());
        segments.insert(segments.end(), path2.begin(), path2.end());
    }
    if (extent) {
        // Punti che racchiudono la zona (con un margine di chi li usa: gli archi sporgono poco).
        extent->assign({corner, t1, t2});
        for (const SectionSupport *support : {&s1, &s2})
            for (std::size_t i = 1; i < support->size(); ++i) extent->push_back((*support)[i].base);
        for (const std::vector<ProfileSegment> *pieces : {static_cast<const std::vector<ProfileSegment> *>(&segments), &path1, &path2}) {
            if (pieces != &segments && !composite) continue;
            for (const ProfileSegment &segment : *pieces) extent->push_back(segment.curve->point(0.5 * (segment.range.lo + segment.range.hi)));
        }
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

Vec3 anyPerpendicular(const Vec3 &n) { return normalized(cross(n, std::fabs(n.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0))); }

// Facce che toccano il vertice.
std::vector<FaceId> facesAround(const Body &body, VertexId v) {
    std::vector<FaceId> result;
    for (FinId fin : body.fins()) {
        if (body.finStart(fin) != v) continue;
        const FaceId f = body.finFace(fin);
        if (std::find(result.begin(), result.end(), f) == result.end()) result.push_back(f);
    }
    return result;
}

// Solido delimitato da facce date come cicli di vertici (le pezze d'angolo).
// Ogni coppia di vertici consecutivi deve avere un edge; il verso dei loop si
// sceglie in modo che la normale `outward` stia dalla parte giusta (vettore
// di Newell dei punti del contorno).
struct PieceEdge {
    int a = -1, b = -1;  // curve(range.lo) = punto a
    CurvePtr<3> curve;
    Interval range;
};
struct PieceFace {
    SurfacePtr surface;
    bool sense = true;
    std::vector<int> cycle;
    Vec3 outward;
};

Body buildPiece(const std::vector<Vec3> &points, const std::vector<PieceEdge> &edges, const std::vector<PieceFace> &faces) {
    std::vector<Body::BuildEdge> buildEdges;
    for (const PieceEdge &e : edges) buildEdges.push_back({e.a, e.b, e.curve, e.range, 0.0});
    std::vector<Body::BuildFace> buildFaces;
    for (const PieceFace &face : faces) {
        std::vector<Body::BuildFin> fins;
        std::vector<Vec3> samples;
        const std::size_t n = face.cycle.size();
        for (std::size_t k = 0; k < n; ++k) {
            const int a = face.cycle[k], b = face.cycle[(k + 1) % n];
            int index = -1;
            bool sense = true;
            for (std::size_t i = 0; i < edges.size() && index < 0; ++i) {
                if (edges[i].a == a && edges[i].b == b) index = int(i);
                else if (edges[i].a == b && edges[i].b == a) index = int(i), sense = false;
            }
            if (index < 0) throw std::logic_error("blendEdges: pezza d'angolo senza edge");
            fins.push_back({index, sense, nullptr, 0.0});
            const PieceEdge &e = edges[std::size_t(index)];
            for (int j = 0; j < 8; ++j) {
                const double s = e.range.length() * j / 8.0;
                samples.push_back(e.curve->point(sense ? e.range.lo + s : e.range.hi - s));
            }
        }
        Vec3 newell(0, 0, 0);
        for (std::size_t k = 0; k < samples.size(); ++k) newell = newell + cross(samples[k], samples[(k + 1) % samples.size()]);
        if (dot(newell, face.outward) < 0.0) {
            std::reverse(fins.begin(), fins.end());
            for (Body::BuildFin &fin : fins) fin.sense = !fin.sense;
        }
        Body::BuildFace buildFace;
        buildFace.surface = face.surface;
        buildFace.sense = face.sense;
        buildFace.loops = {fins};
        buildFaces.push_back(std::move(buildFace));
    }
    Body body = Body::build(points, buildEdges, buildFaces);
    computePCurves(body);
    return body;
}

PieceEdge lineEdge(const std::vector<Vec3> &points, int a, int b) {
    const Vec3 &p = points[std::size_t(a)], &q = points[std::size_t(b)];
    return {a, b, std::make_shared<Line<3>>(p, q - p), {0.0, distance(p, q)}};
}

// Vertice d'angolo con tre spigoli rettilinei scelti tra tre facce piane:
// normali uscenti n[i], direzioni degli spigoli dal vertice d[k], facce di
// ogni spigolo edgeFaces[k].
struct Corner {
    Vec3 vertex;
    Vec3 n[3], d[3];
    int edgeFaces[3][2];
    // Per ogni spigolo, la faccia che non lo contiene.
    int otherFace(int k) const { return 3 - edgeFaces[k][0] - edgeFaces[k][1]; }
    // I due spigoli della faccia i.
    void edgesOf(int i, int &k, int &l) const {
        k = l = -1;
        for (int e = 0; e < 3; ++e)
            if (edgeFaces[e][0] == i || edgeFaces[e][1] == i) (k < 0 ? k : l) = e;
    }
};

// Soluzione di n[i] . x = b[i] (regola di Cramer con i prodotti vettoriali).
Vec3 solvePlanes(const Vec3 n[3], const double b[3]) {
    const double det = dot(n[0], cross(n[1], n[2]));
    if (std::fabs(det) < 1e-9) throw std::domain_error("blendEdges: facce d'angolo quasi parallele");
    return (b[0] * cross(n[1], n[2]) + b[1] * cross(n[2], n[0]) + b[2] * cross(n[0], n[1])) / det;
}

// Raccordo d'angolo (come OCCT): la sfera di raggio r tangente ai tre piani.
// La pezza e' la cella tra il vertice e il centro C (facce sui tre piani e sui
// piani per C normali agli spigoli) meno la palla. `inset[k]`: dove il
// raccordo di ciascuno spigolo lascia il posto alla sfera (distanza dal vertice).
Body filletCorner(const Corner &c, double r, double inset[3]) {
    const double b[3] = {-r, -r, -r};
    const Vec3 x = solvePlanes(c.n, b), center = c.vertex + x;
    std::vector<Vec3> points{c.vertex};
    for (int k = 0; k < 3; ++k) {
        inset[k] = dot(x, c.d[k]);
        if (!(inset[k] > 0.0)) throw std::domain_error("blendEdges: angolo non convesso");
        points.push_back(c.vertex + inset[k] * c.d[k]);  // 1 + k: E_k
    }
    for (int i = 0; i < 3; ++i) points.push_back(center + r * c.n[i]);  // 4 + i: T_i
    std::vector<PieceEdge> edges;
    for (int k = 0; k < 3; ++k) {
        edges.push_back(lineEdge(points, 0, 1 + k));
        for (int i : c.edgeFaces[k]) edges.push_back(lineEdge(points, 1 + k, 4 + i));
        // Arco della sfera nel piano per C normale allo spigolo.
        const int i = c.edgeFaces[k][0], j = c.edgeFaces[k][1];
        const Vec3 xa = c.n[i], ya = normalized(c.n[j] - dot(c.n[j], xa) * xa);
        const double angle = std::atan2(dot(c.n[j], ya), dot(c.n[j], xa));
        edges.push_back({4 + i, 4 + j, std::make_shared<Circle<3>>(center, xa, ya, r), {0.0, angle}});
    }
    std::vector<PieceFace> faces;
    for (int i = 0; i < 3; ++i) {
        int k, l;
        c.edgesOf(i, k, l);
        faces.push_back({std::make_shared<Plane>(Frame3(c.vertex, c.n[i], c.d[k])), true, {0, 1 + k, 4 + i, 1 + l}, c.n[i]});
    }
    for (int k = 0; k < 3; ++k)
        faces.push_back({std::make_shared<Plane>(Frame3(points[std::size_t(1 + k)], c.d[k], c.n[c.edgeFaces[k][0]])), true,
                         {1 + k, 4 + c.edgeFaces[k][0], 4 + c.edgeFaces[k][1]}, c.d[k]});
    // I poli della sfera fuori dalla pezza (che sta tra le direzioni n[i]).
    const Vec3 middle = normalized(c.n[0] + c.n[1] + c.n[2]);
    faces.push_back({std::make_shared<SphericalSurface>(Frame3(center, normalized(c.n[0] - c.n[1]), c.n[2]), r), false, {4, 5, 6}, -middle});
    return buildPiece(points, edges, faces);
}

// Smusso d'angolo (come OCCT): il triangolo per i tre punti F_i in cui si
// incontrano i bordi degli smussi sulle facce. La pezza e' il tetraedro tra il
// triangolo e il punto Q comune ai tre piani degli smussi, che gli smussi dei
// singoli spigoli non tolgono.
// dist[k][s]: distanza dello smusso dello spigolo k sulla faccia edgeFaces[k][s].
Body chamferCorner(const Corner &c, const double (&dist)[3][2]) {
    // u[k][s]: direzione nella faccia edgeFaces[k][s], normale allo spigolo k, verso l'interno della faccia.
    Vec3 u[3][2];
    for (int k = 0; k < 3; ++k)
        for (int s = 0; s < 2; ++s) {
            const int i = c.edgeFaces[k][s];
            int a, b;
            c.edgesOf(i, a, b);
            const int other = a == k ? b : a;
            u[k][s] = normalized(cross(c.n[i], c.d[k]));
            if (dot(u[k][s], c.d[other]) < 0.0) u[k][s] = -u[k][s];
        }
    auto inward = [&](int k, int i) { return c.edgeFaces[k][0] == i ? u[k][0] : u[k][1]; };
    auto distanceOn = [&](int k, int i) { return c.edgeFaces[k][0] == i ? dist[k][0] : dist[k][1]; };
    std::vector<Vec3> points;
    for (int i = 0; i < 3; ++i) {
        int k, l;
        c.edgesOf(i, k, l);
        // Punto della faccia i in cui si incontrano i bordi degli smussi di k e di l.
        const double beta = distanceOn(k, i) / dot(c.d[l], inward(k, i)), alpha = distanceOn(l, i) / dot(c.d[k], inward(l, i));
        points.push_back(c.vertex + alpha * c.d[k] + beta * c.d[l]);  // F_i
    }
    Vec3 m[3];
    double offsets[3];
    for (int k = 0; k < 3; ++k) {
        // Piano dello smusso di k: per i suoi due bordi, paralleli allo spigolo.
        const Vec3 a = dist[k][0] * u[k][0], b = dist[k][1] * u[k][1];
        m[k] = normalized(cross(c.d[k], b - a));
        offsets[k] = dot(m[k], a);
    }
    points.push_back(c.vertex + solvePlanes(m, offsets));  // Q
    const Vec3 centroid = 0.25 * (points[0] + points[1] + points[2] + points[3]);
    std::vector<PieceEdge> edges;
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b) edges.push_back(lineEdge(points, a, b));
    std::vector<PieceFace> faces;
    for (const auto &[a, b, e] : {std::array<int, 3>{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}}) {
        const Vec3 &p = points[std::size_t(a)];
        Vec3 normal = normalized(cross(points[std::size_t(b)] - p, points[std::size_t(e)] - p));
        if (dot(normal, p - centroid) < 0.0) normal = -normal;
        faces.push_back({std::make_shared<Plane>(Frame3(p, normal, points[std::size_t(b)] - p)), true, {a, b, e}, normal});
    }
    return buildPiece(points, edges, faces);
}

// Cuce piu' solidi che si toccano solo lungo facce piane uguali e opposte
// (le sezioni comuni degli utensili di una catena): le coppie di facce con
// gli stessi edge spariscono, vertici ed edge coincidenti si fondono.
Body sewBodies(const std::vector<const Body *> &bodies, double tolerance) {
    std::vector<Vec3> points;
    auto vertexIndex = [&](const Vec3 &p) {
        for (std::size_t i = 0; i < points.size(); ++i)
            if (distance(points[i], p) <= tolerance) return int(i);
        points.push_back(p);
        return int(points.size()) - 1;
    };
    struct SewnEdge {
        Body::BuildEdge edge;
        Vec3 middle;
    };
    std::vector<SewnEdge> edges;
    struct SewnFace {
        Body::BuildFace face;
        std::vector<int> edgeSet;
        int body;
    };
    std::vector<SewnFace> faces;
    for (std::size_t b = 0; b < bodies.size(); ++b) {
        const Body &body = *bodies[b];
        std::map<int, std::pair<int, bool>> edgeMap;  // edge del body -> (edge cucito, invertito)
        for (EdgeId e : body.edges()) {
            const Edge &edge = body.edge(e);
            const int start = vertexIndex(body.vertex(body.edgeStart(e)).point), end = vertexIndex(body.vertex(body.edgeEnd(e)).point);
            const Vec3 middle = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
            int found = -1;
            bool reversed = false;
            for (std::size_t i = 0; i < edges.size() && found < 0; ++i) {
                const Body::BuildEdge &other = edges[i].edge;
                if (distance(edges[i].middle, middle) > tolerance) continue;
                if (other.start == start && other.end == end) found = int(i);
                else if (other.start == end && other.end == start) found = int(i), reversed = true;
            }
            if (found < 0) {
                edges.push_back({{start, end, edge.curve, edge.range, edge.tolerance}, middle});
                found = int(edges.size()) - 1;
            } else {
                // Resta una delle due curve: lo scarto dall'altra diventa
                // tolleranza dell'edge (sta sulla superficie dell'altro pezzo solo
                // entro quello).
                Body::BuildEdge &kept = edges[std::size_t(found)].edge;
                double gap = 0.0;
                for (int k = 0; k <= 8; ++k) {
                    const Vec3 q = edge.curve->point(edge.range.lo + edge.range.length() * k / 8.0);
                    gap = std::max(gap, distance(q, projectPoint(*kept.curve, q, kept.range).point));
                }
                if (gap > kLinearResolution) {
                    // Se questa arriva nel polo di una sua faccia (il meridiano di
                    // una sfera, quando il raggio del raccordo e' uguale a quello
                    // dell'arco) e l'altra gli passa solo accanto, resta questa: un
                    // edge accanto al polo vi gira in u di mezzo giro e la sua
                    // SP-curve sulla sfera non si calcola.
                    bool atPole = false, keptAtPole = false;
                    for (FinId fin : {edge.forward, edge.backward}) {
                        if (!fin.valid()) continue;
                        const std::vector<SurfacePole> poles = surfacePoles(*body.face(body.finFace(fin)).surface);
                        for (const Vec3 &end : {edge.curve->point(edge.range.lo), edge.curve->point(edge.range.hi)})
                            atPole = atPole || poleIndex(poles, end, kLinearResolution) >= 0;
                        for (const Vec3 &end : {kept.curve->point(kept.range.lo), kept.curve->point(kept.range.hi)})
                            keptAtPole = keptAtPole || poleIndex(poles, end, kLinearResolution) >= 0;
                    }
                    if (atPole && !keptAtPole) {
                        const bool reverse = kept.start != start;
                        kept.curve = reverse ? reversedCurve(edge.curve) : edge.curve;
                        kept.range = reverse ? Interval{-edge.range.hi, -edge.range.lo} : edge.range;
                    }
                    kept.tolerance = std::max({kept.tolerance, edge.tolerance, 1.01 * gap});
                }
            }
            edgeMap[e.index] = {found, reversed};
        }
        for (FaceId f : body.faces()) {
            const Face &face = body.face(f);
            SewnFace sewn;
            sewn.body = int(b);
            sewn.face.surface = face.surface;
            sewn.face.sense = face.sense;
            for (LoopId l : face.loops) {
                std::vector<Body::BuildFin> loop;
                for (FinId fin : body.loopFins(l)) {
                    const auto [index, reversed] = edgeMap.at(body.fin(fin).edge.index);
                    loop.push_back({index, body.fin(fin).sense != reversed, nullptr, 0.0});
                    sewn.edgeSet.push_back(index);
                }
                if (loop.empty()) throw std::domain_error("blendEdges: loop senza edge in un utensile");
                sewn.face.loops.push_back(std::move(loop));
            }
            std::sort(sewn.edgeSet.begin(), sewn.edgeSet.end());
            faces.push_back(std::move(sewn));
        }
    }
    std::vector<bool> removed(faces.size(), false);
    for (std::size_t a = 0; a < faces.size(); ++a)
        for (std::size_t b = a + 1; b < faces.size() && !removed[a]; ++b)
            if (!removed[b] && faces[a].body != faces[b].body && faces[a].edgeSet == faces[b].edgeSet
                && faces[a].face.surface->type() == SurfaceType::Plane && faces[b].face.surface->type() == SurfaceType::Plane)
                removed[a] = removed[b] = true;
    // Solo gli edge ancora usati, rinumerati.
    std::vector<int> used(edges.size(), -1);
    std::vector<Body::BuildEdge> buildEdges;
    std::vector<Body::BuildFace> buildFaces;
    for (std::size_t f = 0; f < faces.size(); ++f) {
        if (removed[f]) continue;
        Body::BuildFace face = faces[f].face;
        for (auto &loop : face.loops)
            for (Body::BuildFin &fin : loop) {
                if (used[std::size_t(fin.edge)] < 0) {
                    used[std::size_t(fin.edge)] = int(buildEdges.size());
                    buildEdges.push_back(edges[std::size_t(fin.edge)].edge);
                }
                fin.edge = used[std::size_t(fin.edge)];
            }
        buildFaces.push_back(std::move(face));
    }
    Body sewn = Body::build(points, buildEdges, buildFaces);
    // Vertici fusi entro la tolleranza della cucitura: le curve che vi arrivano
    // possono fermarsi poco lontano (pezzi costruiti da spigoli della base che
    // si toccano solo entro la loro precisione). Edge e vertici diventano
    // tolleranti per lo scarto, come nelle booleane: senza, l'SP-curve di un
    // meridiano che arriva accanto al polo di una sfera (raggio del raccordo
    // uguale a quello dell'arco) non si calcola.
    for (EdgeId e : sewn.edges()) {
        Edge &edge = sewn.edge(e);
        for (const auto &[v, t] : {std::pair<VertexId, double>{sewn.edgeStart(e), edge.range.lo}, {sewn.edgeEnd(e), edge.range.hi}}) {
            const double gap = distance(sewn.vertex(v).point, edge.curve->point(t));
            if (gap <= kLinearResolution) continue;
            edge.tolerance = std::max(edge.tolerance, 1.01 * gap);
            sewn.vertex(v).tolerance = std::max(sewn.vertex(v).tolerance, 1.01 * gap);
        }
    }
    computePCurves(sewn);
    return unifySameDomain(sewn);
}

// Semispazio dietro il piano (punto, normale uscente), limitato a un cubo di lato 2 size.
Body halfSpace(const Vec3 &point, const Vec3 &normal, double size) {
    return makePrism(Frame3(point, -normal, anyPerpendicular(normal)),
                     {Vec2(-size, -size), Vec2(size, -size), Vec2(size, size), Vec2(-size, size)}, {}, size);
}

// Spigolo da raccordare, con la sua sezione.
struct BlendEdge {
    EdgeId id;
    FaceId faces[2];
    bool straight = false, full = false;  // retta; cerchio chiuso (altrimenti arco)
    bool convex = true;
    Vec3 origin, e1, e2, axis;
    Vec2 corner;
    SectionFace section[2];
    SectionSupport support[2];  // appoggi del raccordo (la faccia dello spigolo e quelle tangenti che la continuano)
    std::vector<FaceId> supportFaces[2];  // le facce aggiunte agli appoggi
    bool composite = false;     // un appoggio ha piu' facce
    int chain = -1;  // catena di spigoli tangenti (utensili cuciti in uno solo)
    ProfileRegion region;
    std::vector<Vec2> extent;  // punti che racchiudono la zona nella sezione
    double from = 0.0, to = 0.0;  // retta: tratto lungo l'asse dall'origine (il vertice iniziale)
    std::vector<std::pair<Vec3, Vec3>> trims;  // retta: piani (punto, normale uscente) che limitano la zona agli estremi
    double chamfer[2] = {0.0, 0.0};  // smusso: distanze sulle facce faces[0] e faces[1]
    double angleFrom = 0.0, angleTo = 0.0;  // arco: prolungamento (radianti) oltre l'inizio e la fine, poi tagliato dai trims
    // Estremi concavi: oltre il vertice lo spigolo proseguirebbe nel materiale
    // dietro la faccia B (lo spigolo tra le due pareti e' concavo). La zona si
    // allunga e si tiene solo dalla parte in cui guarda la normale uscente di B.
    struct ConcaveEnd {
        VertexId vertex;
        FaceId face;         // B
        Vec3 point, normal;  // vertice e normale uscente di B nel vertice
        double near = 0.0;   // mezzo lato del cubo attorno al vertice in cui vale il taglio
    };
    std::vector<ConcaveEnd> concaveEnds;
};

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

namespace {

Body blendEdgesWith(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer, const std::vector<ChamferSides> *sides);
Body analyticBlend(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer, const std::vector<ChamferSides> *sides);

}

Body blendEdges(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer) {
    return blendEdgesWith(body, edges, size, chamfer, nullptr);
}

Body chamferEdges(const Body &body, const std::vector<EdgeId> &edges, const std::vector<ChamferSides> &sides) {
    if (sides.size() != edges.size()) throw std::invalid_argument("chamferEdges: una coppia di distanze per spigolo");
    double size = 0.0;
    for (const ChamferSides &side : sides) {
        if (!(side.onReference > kLinearResolution) || !(side.onOther > kLinearResolution)) throw std::domain_error("blendEdges: distanze dello smusso non valide");
        size = std::max({size, side.onReference, side.onOther});
    }
    return blendEdgesWith(body, edges, size, true, &sides);
}

namespace {

// Propagazione per tangenza (come nei CAD: un raccordo non puo' finire in un
// vertice liscio): gli spigoli che continuano tangenti quelli scelti, tra le
// stesse facce o facce tangenti a quelle nel vertice. Le distanze degli smussi
// passano allo spigolo aggiunto.
void propagateTangent(const Body &body, std::vector<EdgeId> &edges, std::vector<ChamferSides> *sides) {
    const double tolerance = 1e-9;
    std::set<int> chosen;
    for (EdgeId e : edges) chosen.insert(e.index);
    const auto normalAtVertex = [&](FaceId f, const Vec3 &p) { return outwardNormal(body, f, p); };
    const auto smooth = [&](EdgeId e) {
        const Edge &edge = body.edge(e);
        const Vec3 p = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
        return dot(normalAtVertex(body.finFace(edge.forward), p), normalAtVertex(body.finFace(edge.backward), p)) >= 1.0 - tolerance;
    };
    // Direzione dell'edge nel vertice, verso l'interno dell'edge.
    const auto into = [&](EdgeId e, VertexId v) {
        const Edge &edge = body.edge(e);
        const bool atStart = body.edgeStart(e) == v;
        Vec3 d[2];
        if (atStart) edge.curve->evaluate(edge.range.lo, 1, d);
        else edge.curve->evaluateLeft(edge.range.hi, 1, d);
        const Vec3 t = normalized(d[1]);
        return atStart ? t : -t;
    };
    for (std::size_t k = 0; k < edges.size(); ++k) {
        const EdgeId e = edges[k];
        const Edge &edge = body.edge(e);
        if (!edge.curve || body.isLaminar(e)) continue;
        for (VertexId v : {body.edgeStart(e), body.edgeEnd(e)}) {
            if (body.edgeStart(e) == body.edgeEnd(e)) break;  // edge chiuso
            const Vec3 p = body.vertex(v).point;
            const Vec3 te = into(e, v);
            const FaceId fa = body.finFace(edge.forward), fb = body.finFace(edge.backward);
            const Vec3 na = normalAtVertex(fa, p), nb = normalAtVertex(fb, p);
            EdgeId found;
            int candidates = 0;
            for (EdgeId g : body.edges()) {
                if (g == e || (body.edgeStart(g) != v && body.edgeEnd(g) != v) || body.isLaminar(g) || !body.edge(g).curve) continue;
                if (dot(te, into(g, v)) > -(1.0 - tolerance)) continue;
                const Edge &ge = body.edge(g);
                const Vec3 ga = normalAtVertex(body.finFace(ge.forward), p), gb = normalAtVertex(body.finFace(ge.backward), p);
                const bool same = (dot(ga, na) >= 1.0 - 1e-6 && dot(gb, nb) >= 1.0 - 1e-6) || (dot(ga, nb) >= 1.0 - 1e-6 && dot(gb, na) >= 1.0 - 1e-6);
                if (!same || smooth(g)) continue;
                found = g;
                ++candidates;
            }
            if (candidates != 1 || chosen.count(found.index)) continue;
            chosen.insert(found.index);
            edges.push_back(found);
            if (sides) sides->push_back((*sides)[k]);
        }
    }
}

Body blendEdgesWith(const Body &body, const std::vector<EdgeId> &selected, double size, bool chamfer, const std::vector<ChamferSides> *selectedSides) {
    if (body.isSheet()) throw std::domain_error("blendEdges: solo solidi");
    if (!(size > kLinearResolution)) throw std::domain_error("blendEdges: raggio o distanza non validi");
    std::vector<EdgeId> edges = selected;
    std::vector<ChamferSides> propagatedSides;
    if (selectedSides) propagatedSides = *selectedSides;
    propagateTangent(body, edges, selectedSides ? &propagatedSides : nullptr);
    const std::vector<ChamferSides> *sides = selectedSides ? &propagatedSides : nullptr;
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    const double scale = std::max(box.diagonal(), 1.0), tolerance = 1e-9 * scale;
    const double normalTolerance = 1e-9;

    // Spigoli tra facce tangenti (lisci): non c'e' niente da raccordare e si
    // lasciano (cliccando una faccia se ne prendono anche questi).
    {
        auto smooth = [&](EdgeId e) {
            const Edge &edge = body.edge(e);
            if (!edge.curve || body.isLaminar(e)) return false;
            for (double f : {0.2, 0.5, 0.8}) {
                const Vec3 p = edge.curve->point(edge.range.lo + f * edge.range.length());
                if (dot(outwardNormal(body, body.finFace(edge.forward), p), outwardNormal(body, body.finFace(edge.backward), p)) < 1.0 - normalTolerance) return false;
            }
            return true;
        };
        std::vector<EdgeId> sharp;
        std::vector<ChamferSides> sharpSides;
        for (std::size_t k = 0; k < edges.size(); ++k)
            if (!smooth(edges[k])) {
                sharp.push_back(edges[k]);
                if (sides) sharpSides.push_back((*sides)[k]);
            }
        if (sharp.empty()) throw std::domain_error("blendEdges: gli spigoli scelti stanno tra facce tangenti (niente da raccordare)");
        if (sharp.size() < edges.size()) return blendEdgesWith(body, sharp, size, chamfer, sides ? &sharpSides : nullptr);
    }

    // Bordi di forma libera (ne' rette ne' cerchi): catene sulle facce piane
    // (fk_blend_loop), con i segmenti e gli archi che li continuano nel
    // contorno; gli altri spigoli poi, sul risultato.
    std::vector<EdgeId> freeform;
    for (EdgeId e : edges) {
        const Edge &edge = body.edge(e);
        if (!edge.curve) continue;
        const Vec3 start = edge.curve->point(edge.range.lo), end = edge.curve->point(edge.range.hi);
        const Vec3 middle = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
        const bool straight = distance(start, end) > tolerance && norm(cross(middle - start, normalized(end - start))) <= tolerance;
        if (!straight && edge.curve->type() != CurveType::Circle) freeform.push_back(e);
    }
    // Quelli che non stanno tra una faccia piana e un fianco normale ad essa
    // (superfici curve qualsiasi: innesti tra cilindri, cilindri obliqui,
    // superfici B-spline) vanno al raccordo generale a palla rotolante.
    std::vector<EdgeId> general;
    for (EdgeId e : freeform)
        if (!isPlanarChainEdge(body, e)) general.push_back(e);
    // Anche rette e cerchi tra facce che le sezioni analitiche non conoscono
    // (B-spline dei loft e degli sweep, estrusioni, rivoluzioni), se non sono
    // bordi di una faccia piana con i fianchi normali (catene piane).
    const auto analyticFace = [&](FinId fin) {
        const SurfaceType type = body.face(body.finFace(fin)).surface->type();
        return type == SurfaceType::Plane || type == SurfaceType::Cylinder || type == SurfaceType::Cone || type == SurfaceType::Sphere
            || type == SurfaceType::Torus;
    };
    for (EdgeId e : edges) {
        if (std::find(freeform.begin(), freeform.end(), e) != freeform.end() || body.isLaminar(e)) continue;
        if ((!analyticFace(body.edge(e).forward) || !analyticFace(body.edge(e).backward)) && !isPlanarChainEdge(body, e)) general.push_back(e);
    }
    // Le distanze di ogni spigolo (smussi asimmetrici) seguono gli spigoli nei
    // passaggi tra i moduli: si ritrovano dal punto medio.
    auto sidesOf = [&](EdgeId e) {
        for (std::size_t k = 0; k < edges.size(); ++k)
            if (edges[k] == e) return (*sides)[k];
        throw std::logic_error("blendEdges: spigolo senza distanze");
    };
    // Un gruppo di spigoli a un modulo, poi gli altri sul risultato.
    auto delegate = [&](const std::vector<EdgeId> &runs, const std::function<Body(const std::vector<ChamferSides> *)> &blend, const char *message) {
        std::vector<Vec3> others;
        std::vector<ChamferSides> otherSides, runSides;
        for (EdgeId e : edges) {
            if (std::find(runs.begin(), runs.end(), e) != runs.end()) continue;
            others.push_back(body.edge(e).curve->point(0.5 * (body.edge(e).range.lo + body.edge(e).range.hi)));
            if (sides) otherSides.push_back(sidesOf(e));
        }
        if (sides)
            for (EdgeId e : runs) runSides.push_back(sidesOf(e));
        Body result = blend(sides ? &runSides : nullptr);
        if (others.empty()) return result;
        std::vector<EdgeId> rest;
        for (const Vec3 &p : others) {
            const EdgeId e = nearestEdge(result, p, 1e-7 * scale);
            if (!e.valid()) throw std::domain_error(message);
            rest.push_back(e);
        }
        return blendEdgesWith(result, rest, size, chamfer, sides ? &otherSides : nullptr);
    };
    if (!general.empty()) {
        // Le catene possono proseguire per tangenza oltre gli spigoli scelti: al modulo vanno quelli scelti che vi stanno.
        const std::vector<EdgeId> runs = surfaceChainRuns(body, edges, general);
        std::vector<EdgeId> chosen;
        for (EdgeId e : edges)
            if (std::find(runs.begin(), runs.end(), e) != runs.end()) chosen.push_back(e);
        return delegate(chosen, [&](const std::vector<ChamferSides> *runSides) {
            bool touching = false;
            const std::vector<std::vector<EdgeId>> groups = surfaceChainGroups(body, chosen, touching);
            if (!touching) return blendSurfaceChains(body, chosen, size, chamfer, runSides);

            // Due catene su coppie di facce diverse possono incontrarsi nello
            // stesso vertice (per esempio elica e fine-filetto). Costruirle
            // insieme genera due pezze terminali sovrapposte; dopo il primo
            // raccordo, invece, il secondo termina naturalmente sulla nuova
            // faccia e chiude il raccordo senza collassare a raggio zero.
            Body result = body;
            for (const std::vector<EdgeId> &group : groups) {
                std::vector<EdgeId> mapped;
                std::vector<ChamferSides> mappedSides;
                for (EdgeId original : group) {
                    const Edge &edge = body.edge(original);
                    const Vec3 middle = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
                    const EdgeId current = nearestEdge(result, middle, 1e-7 * scale);
                    if (!current.valid()) throw std::domain_error("blendEdges: raccordo tangente non ritrovato dopo la catena precedente");
                    mapped.push_back(current);
                    if (runSides) {
                        const auto at = std::find(chosen.begin(), chosen.end(), original);
                        mappedSides.push_back((*runSides)[std::size_t(at - chosen.begin())]);
                    }
                }
                result = blendSurfaceChains(result, mapped, size, chamfer, runSides ? &mappedSides : nullptr);
            }
            return result;
        },
                        "blendEdges: spigoli che toccano i raccordi tra superfici curve");
    }
    if (!freeform.empty()) {
        const std::vector<EdgeId> runs = planarChainRuns(body, edges, freeform);
        return delegate(runs, [&](const std::vector<ChamferSides> *runSides) { return blendPlanarChains(body, runs, size, chamfer, runSides); },
                        "blendEdges: spigoli che toccano i raccordi dei bordi di forma libera");
    }

    // Solo rette e cerchi: le sezioni analitiche. Se non riescono (un arco che
    // finisce contro un fianco obliquo, angoli vivi tra archi e segmenti) e gli
    // spigoli sono tutti bordi tra una faccia piana e fianchi normali ad essa,
    // la palla rotolante delle catene piane (fk_blend_loop).
    try {
        try {
            return analyticBlend(body, edges, size, chamfer, sides);
        } catch (const std::invalid_argument &failure) {
            // Una costruzione intermedia non valida: come un caso non gestito dalle sezioni analitiche.
            throw std::domain_error(failure.what());
        }
    } catch (const std::domain_error &) {
        // Un raccordo locale che invade un altro contorno della faccia (i
        // contatti incrociano i bordi di un raccordo vicino): si spiega questo
        // invece dell'errore della booleana.
        std::string crossing;
        const auto note = [&](const std::domain_error &failure) {
            if (crossing.empty() && std::string(failure.what()).find(describe(CheckCode::LoopsCross)) != std::string::npos) crossing = failure.what();
        };
        bool planar = true;
        for (EdgeId e : edges) planar = planar && isPlanarChainEdge(body, e);
        std::vector<ChamferSides> allSides;
        if (sides)
            for (EdgeId e : edges) allSides.push_back(sidesOf(e));
        if (planar) {
            try {
                // Semi: gli spigoli curvi (hanno una sola faccia piana possibile); le catene proseguono sui segmenti.
                std::vector<EdgeId> seeds;
                for (EdgeId e : edges)
                    if (body.edge(e).curve->type() != CurveType::Line) seeds.push_back(e);
                if (!seeds.empty()) {
                    const std::vector<EdgeId> runs = planarChainRuns(body, edges, seeds);
                    std::vector<ChamferSides> runSides;
                    if (sides)
                        for (EdgeId e : runs) runSides.push_back(sidesOf(e));
                    if (runs.size() == edges.size()) return blendPlanarChains(body, runs, size, chamfer, sides ? &runSides : nullptr);
                } else {
                    // Solo segmenti (i bordi di una tasca poligonale): T e' la faccia piana comune a tutti.
                    FaceId common;
                    for (FinId fin : {body.edge(edges.front()).forward, body.edge(edges.front()).backward}) {
                        const FaceId f = body.finFace(fin);
                        bool everywhere = body.face(f).surface->type() == SurfaceType::Plane;
                        for (EdgeId e : edges) everywhere = everywhere && (body.finFace(body.edge(e).forward) == f || body.finFace(body.edge(e).backward) == f);
                        if (everywhere) common = f;
                    }
                    if (common.valid()) return blendPlanarChains(body, edges, size, chamfer, sides ? &allSides : nullptr, common);
                }
            } catch (const std::domain_error &failure) {
                note(failure);
            }
        }
        // Ultimo tentativo: la palla rotolante tra superfici qualsiasi (angoli
        // vivi a mitra, estremi contro facce qualsiasi).
        try {
            (void)surfaceChainRuns(body, edges, edges);  // eccezione se le catene non sono gestite
            return blendSurfaceChains(body, edges, size, chamfer, sides ? &allSides : nullptr);
        } catch (const std::domain_error &failure) {
            note(failure);
        }
        if (!crossing.empty())
            throw std::domain_error("blendEdges: il raccordo arriva su un altro contorno della faccia (per esempio un raccordo vicino) e i "
                                    "due andrebbero rifilati l'uno sull'altro: non ancora gestito. " + crossing);
        throw;
    }
}

Body analyticBlend(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer, const std::vector<ChamferSides> *sides) {
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    const double scale = std::max(box.diagonal(), 1.0), tolerance = 1e-9 * scale, probe = 1e-4 * std::min(scale, size);
    const double normalTolerance = 1e-9;
    auto sidesOf = [&](EdgeId e) {
        for (std::size_t k = 0; k < edges.size(); ++k)
            if (edges[k] == e) return (*sides)[k];
        throw std::logic_error("blendEdges: spigolo senza distanze");
    };
    std::set<int> uniqueEdges;
    for (EdgeId e : edges)
        if (!uniqueEdges.insert(e.index).second) throw std::domain_error("blendEdges: spigolo scelto due volte");
    std::vector<BlendEdge> infos(edges.size());
    std::vector<std::exception_ptr> infoErrors(edges.size());
    parallelFor(edges.size(), threadCount(0), [&](std::size_t infoIndex) {
      try {
        const EdgeId e = edges[infoIndex];
        const Edge &edge = body.edge(e);
        if (!edge.curve || body.isLaminar(e)) throw std::domain_error("blendEdges: spigolo non valido");
        BlendEdge &info = infos[infoIndex];
        info.id = e;
        info.faces[0] = body.finFace(edge.forward);
        info.faces[1] = body.finFace(edge.backward);
        if (sides) {
            const auto [onForward, onBackward] = detail::chamferDistances(body, e, sidesOf(e));
            info.chamfer[0] = onForward;
            info.chamfer[1] = onBackward;
        } else {
            info.chamfer[0] = info.chamfer[1] = size;
        }
        const FaceId *faces = info.faces;
        if (faces[0] == faces[1]) throw std::domain_error("blendEdges: spigolo interno a una faccia");
        const Vec3 start = edge.curve->point(edge.range.lo), end = edge.curve->point(edge.range.hi);
        const Vec3 middle = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
        const bool straight = distance(start, end) > tolerance
                           && norm(cross(middle - start, normalized(end - start))) <= tolerance;
        const bool circular = !straight && edge.curve->type() == CurveType::Circle;
        if (!straight && !circular) throw std::domain_error("blendEdges: solo spigoli rettilinei o circolari");
        info.straight = straight;
        info.full = circular && edge.range.length() >= kTwoPi - 1e-9;

        // Sistema della sezione: (e1, e2) nel piano normale allo spigolo
        // rettilineo, o (radiale, asse) nel semipiano di quello circolare.
        Vec3 &origin = info.origin, &e1 = info.e1, &e2 = info.e2, &axis = info.axis;
        const Vec3 at = middle;  // punto dello spigolo nella sezione
        if (straight) {
            // La sezione a meta' dello spigolo (lontano dai vertici, dove le
            // prove di appartenenza alle facce cadrebbero sui loro bordi).
            axis = normalized(end - start);
            origin = start;
            e1 = anyPerpendicular(axis);
            e2 = cross(axis, e1);
            info.to = distance(start, end);
        } else {
            const auto &circle = static_cast<const Circle<3> &>(*edge.curve);
            origin = circle.center();
            axis = normalized(cross(circle.xAxis(), circle.yAxis()));
            e1 = normalized(middle - origin);
            e2 = axis;
        }
        auto map = [&](const Vec3 &p) { return Vec2(dot(p - origin, e1), dot(p - origin, e2)); };
        const Vec2 corner = map(at);
        info.corner = corner;

        // Punto 3D della sezione dello spigolo per un punto (x, y) della sezione.
        auto unmap = [&](const Vec2 &q) { return at + (q.x() - corner.x()) * e1 + (q.y() - corner.y()) * e2; };
        // Sezione della faccia `f` nel punto `base` (3D `p`) della sua curva.
        auto sectionOf = [&](FaceId f, const Vec3 &p, const Vec2 &base) {
            const Face &face = body.face(f);
            const Surface &surface = *face.surface;
            SectionFace s;
            s.base = base;
            const Vec3 n = outwardNormal(body, f, p);
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
                const Frame3 &frame = cylinder.frame();
                if (!parallel(frame.zDir(), axis)) throw std::domain_error("blendEdges: cilindro non parallelo allo spigolo");
                if (straight) {
                    s.circle = true;
                    s.center = map(frame.origin());
                    s.radius = cylinder.radius();
                } else if (norm(cross(origin - frame.origin(), frame.zDir())) > 1e-9 * scale) {
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
                const Frame3 &frame = torus.frame();
                if (straight || !parallel(frame.zDir(), axis) || norm(cross(frame.origin() - origin, axis)) > 1e-9 * scale)
                    throw std::domain_error("blendEdges: toro non coassiale allo spigolo circolare");
                s.circle = true;
                s.center = Vec2(torus.majorRadius(), dot(frame.origin() - origin, axis));
                s.radius = torus.minorRadius();
                break;
            }
            default:
                throw std::domain_error("blendEdges: tipo di faccia non gestito");
            }
            // La faccia si allontana da `base` lungo la tangente alla sua
            // sezione (perpendicolare alla normale), nel verso in cui c'e' la faccia.
            bool found = false;
            for (double sign : {1.0, -1.0}) {
                const Vec2 t = sign * perp(s.m);
                const Vec3 test = projectPoint(surface, p + probe * (t.x() * e1 + t.y() * e2)).point;
                if (classifyPointOnFace(body, f, test, tolerance) == PointLocation::Inside) {
                    s.t = t;
                    found = true;
                    break;
                }
            }
            if (!found) throw std::domain_error("blendEdges: faccia dello spigolo troppo stretta");
            return s;
        };
        SectionFace section[2];
        for (int k = 0; k < 2; ++k) section[k] = sectionOf(faces[k], at, corner);
        const double turn = dot(section[1].t, section[0].m);
        if (std::fabs(turn) <= 1e-9) throw std::domain_error("blendEdges: spigolo tra facce tangenti");
        info.convex = turn > 0.0;
        info.section[0] = section[0];
        info.section[1] = section[1];
        info.support[0] = {section[0]};
        info.support[1] = {section[1]};

        // Facce piu' corte del raccordo: se la palla toccherebbe la faccia
        // oltre il suo bordo opposto (uno spigolo parallelo allo spigolo, o un
        // cerchio coassiale, lungo quanto lui), l'appoggio prosegue nella
        // faccia accanto, che deve continuarla tangente: la palla tocca
        // quella, e la faccia corta sparisce sotto il raccordo. Si ripete se
        // anche la faccia accanto e' corta.
        if (!chamfer) {
            const double limitTolerance = 1e-7 * scale;
            // Punti dello spigolo e i loro corrispondenti su una curva della sezione in q.
            auto shiftedPoint = [&](const Vec3 &p, const Vec2 &from, const Vec2 &to) {
                if (straight) return p + (to.x() - from.x()) * e1 + (to.y() - from.y()) * e2;
                const Vec3 radial = normalized(p - origin - dot(p - origin, axis) * axis);
                return origin + to.x() * radial + to.y() * axis;
            };
            auto onCurve = [&](const Edge &g, const Vec3 &p) { return projectPoint(*g.curve, p, g.range).distance <= limitTolerance; };
            // Bordo opposto della faccia `f` (appoggio `s`) nella sezione: il punto q e l'edge.
            auto farSide = [&](FaceId f, const SectionFace &s, Vec2 &q, EdgeId &joint) {
                bool found = false;
                double best = 0.0;
                std::vector<EdgeId> border;
                for (LoopId l : body.face(f).loops) {
                    const FinId first = body.loop(l).first;
                    if (!first.valid()) continue;
                    FinId fin = first;
                    do {
                        border.push_back(body.fin(fin).edge);
                        fin = body.fin(fin).next;
                    } while (fin != first);
                }
                for (EdgeId g : border) {
                    if (g == info.id) continue;
                    const Edge &ge = body.edge(g);
                    if (!ge.curve || body.isLaminar(g)) continue;
                    Vec2 image;
                    if (straight) {
                        const Vec3 a = ge.curve->point(ge.range.lo), b = ge.curve->point(ge.range.hi);
                        if (ge.curve->type() != CurveType::Line || distance(a, b) <= tolerance || std::fabs(dot(normalized(b - a), axis)) < 1.0 - 1e-9) continue;
                        image = map(a);
                    } else {
                        if (ge.curve->type() != CurveType::Circle) continue;
                        const auto &c = static_cast<const Circle<3> &>(*ge.curve);
                        if (!parallel(cross(c.xAxis(), c.yAxis()), axis) || norm(cross(c.center() - origin, axis)) > limitTolerance) continue;
                        image = Vec2(c.radius(), dot(c.center() - origin, axis));
                    }
                    // Sulla curva della faccia, davanti alla base.
                    const double off = s.circle ? std::fabs(distance(image, s.center) - s.radius) : std::fabs(cross2(image - s.base, s.t));
                    const double ahead = travel(s, image);
                    if (off > limitTolerance || !(ahead > limitTolerance)) continue;
                    // Lungo quanto lo spigolo: i punti dell'uno stanno sull'altro e viceversa.
                    bool same = true;
                    for (double fraction : {0.0, 0.5, 1.0}) {
                        same = same && onCurve(ge, shiftedPoint(edge.curve->point(edge.range.lo + fraction * edge.range.length()), corner, image));
                        same = same && onCurve(edge, shiftedPoint(ge.curve->point(ge.range.lo + fraction * ge.range.length()), image, corner));
                    }
                    if (!same || (found && ahead >= best)) continue;
                    found = true;
                    best = ahead;
                    q = image;
                    joint = g;
                }
                return found;
            };
            for (int iteration = 0; iteration < 8; ++iteration) {
                Vec2 center;
                if (!filletCenter(info.support[0], info.support[1], corner, size, info.convex, tolerance, center)) break;
                bool changed = false;
                for (int k = 0; k < 2; ++k) {
                    SectionSupport &support = info.support[k];
                    const SectionFace &s = support.back();
                    Vec2 q;
                    EdgeId joint;
                    const FaceId current = info.supportFaces[k].empty() ? faces[k] : info.supportFaces[k].back();
                    if (!farSide(current, s, q, joint)) continue;
                    if (travel(s, foot(s, center)) <= travel(s, q) + limitTolerance) continue;
                    const Edge &je = body.edge(joint);
                    const FaceId next = body.finFace(je.forward) == current ? body.finFace(je.backward) : body.finFace(je.forward);
                    const Vec3 p = unmap(q);
                    if (next == current || next == faces[1 - k] || dot(outwardNormal(body, current, p), outwardNormal(body, next, p)) < 1.0 - 1e-7)
                        throw std::domain_error(
                            "blendEdges: la faccia accanto allo spigolo e' piu' corta del raggio e non prosegue tangente in un'altra faccia");
                    support.push_back(sectionOf(next, p, q));
                    info.supportFaces[k].push_back(next);
                    changed = true;
                }
                if (!changed) break;
                if (iteration == 7) throw std::domain_error("blendEdges: troppe facce corte accanto allo spigolo");
            }
            info.composite = info.support[0].size() > 1 || info.support[1].size() > 1;
        }
      } catch (...) {
          infoErrors[infoIndex] = std::current_exception();
      }
    });
    for (const std::exception_ptr &error : infoErrors)
        if (error) std::rethrow_exception(error);

    // Catene di spigoli tangenti (un segmento che prosegue in un arco): gli
    // utensili si cuciono lungo la sezione comune, perche' le superfici dei
    // raccordi vi si toccano tangenti e una booleana tra loro non si potrebbe fare.
    auto tangentAt = [&](const BlendEdge &info, VertexId v) {
        const Edge &edge = body.edge(info.id);
        return normalized(edge.curve->derivative(body.edgeStart(info.id) == v ? edge.range.lo : edge.range.hi));
    };
    std::vector<int> parent(infos.size());
    for (std::size_t k = 0; k < infos.size(); ++k) parent[k] = int(k);
    std::function<int(int)> root = [&](int k) { return parent[std::size_t(k)] == k ? k : parent[std::size_t(k)] = root(parent[std::size_t(k)]); };
    for (std::size_t a = 0; a < infos.size(); ++a)
        for (std::size_t b = a + 1; b < infos.size(); ++b) {
            if (infos[a].full || infos[b].full || infos[a].convex != infos[b].convex) continue;
            for (VertexId v : {body.edgeStart(infos[a].id), body.edgeEnd(infos[a].id)})
                if (v == body.edgeStart(infos[b].id) || v == body.edgeEnd(infos[b].id))
                    if (std::fabs(dot(tangentAt(infos[a], v), tangentAt(infos[b], v))) >= 1.0 - normalTolerance) parent[std::size_t(root(int(a)))] = root(int(b));
        }
    for (std::size_t k = 0; k < infos.size(); ++k) {
        int members = 0;
        for (std::size_t j = 0; j < infos.size(); ++j) members += root(int(j)) == root(int(k));
        if (members > 1) infos[k].chain = root(int(k));
    }
    // Zone dei raccordi. Gli spigoli circolari e quelli delle catene si
    // scostano dalle facce (vedi blendRegion): le sezioni comuni delle catene coincidono.
    std::vector<std::exception_ptr> regionErrors(infos.size());
    parallelFor(infos.size(), threadCount(0), [&](std::size_t k) {
        try {
            BlendEdge &info = infos[k];
            const bool offset = !info.straight || info.chain >= 0;
            if (info.composite && info.chain >= 0)
                throw std::domain_error("blendEdges: faccia piu' corta del raggio accanto a una catena di spigoli tangenti (non gestita)");
            info.region = blendRegion(info.support[0], info.support[1], info.corner, chamfer ? info.chamfer[0] : size, chamfer, info.convex, tolerance,
                                      offset ? 0.05 * size : 0.0, &info.extent, chamfer ? info.chamfer[1] : -1.0);
        } catch (...) {
            regionErrors[k] = std::current_exception();
        }
    });
    for (const std::exception_ptr &error : regionErrors)
        if (error) std::rethrow_exception(error);

    // Vertici con tre spigoli scelti: la pezza d'angolo.
    std::map<int, std::vector<int>> perVertex;
    for (int k = 0; k < int(infos.size()); ++k) {
        if (infos[std::size_t(k)].full) continue;
        const VertexId a = body.edgeStart(infos[std::size_t(k)].id), b = body.edgeEnd(infos[std::size_t(k)].id);
        perVertex[a.index].push_back(k);
        if (b != a) perVertex[b.index].push_back(k);
    }
    std::vector<Body> corners;
    std::vector<int> filletCorners;  // vertici in cui i raccordi finiscono contro la sfera
    for (const auto &[vertexIndex, list] : perVertex) {
        if (list.size() < 3) continue;
        if (list.size() > 3) throw std::domain_error("blendEdges: piu' di tre spigoli in un vertice");
        const VertexId v(vertexIndex);
        const std::vector<FaceId> around = facesAround(body, v);
        if (around.size() != 3) throw std::domain_error("blendEdges: angolo con tre spigoli scelti ma non tre facce");
        Corner c;
        c.vertex = body.vertex(v).point;
        for (int i = 0; i < 3; ++i) {
            if (body.face(around[std::size_t(i)]).surface->type() != SurfaceType::Plane)
                throw std::domain_error("blendEdges: pezza d'angolo solo tra facce piane");
            c.n[i] = outwardNormal(body, around[std::size_t(i)], c.vertex);
        }
        for (int k = 0; k < 3; ++k) {
            const BlendEdge &info = infos[std::size_t(list[std::size_t(k)])];
            if (!info.straight) throw std::domain_error("blendEdges: pezza d'angolo solo tra spigoli rettilinei");
            if (!info.convex) throw std::domain_error("blendEdges: pezza d'angolo solo su angoli convessi");
            c.d[k] = body.edgeStart(info.id) == v ? info.axis : -info.axis;
            for (int s = 0; s < 2; ++s)
                c.edgeFaces[k][s] = int(std::find(around.begin(), around.end(), info.faces[s]) - around.begin());
        }
        if (chamfer) {
            double dist[3][2];
            for (int k = 0; k < 3; ++k)
                for (int s = 0; s < 2; ++s) dist[k][s] = infos[std::size_t(list[std::size_t(k)])].chamfer[s];
            corners.push_back(chamferCorner(c, dist));
            continue;
        }
        double inset[3];
        corners.push_back(filletCorner(c, size, inset));
        filletCorners.push_back(vertexIndex);
        for (int k = 0; k < 3; ++k) {
            BlendEdge &info = infos[std::size_t(list[std::size_t(k)])];
            if (body.edgeStart(info.id) == v) info.from += inset[k];
            else info.to -= inset[k];
        }
    }

    // Estremi degli spigoli: la zona finisce nel piano (o semipiano) normale
    // allo spigolo nel vertice se le altre facce del vertice vi sono normali
    // (un piano perpendicolare, il piano per l'asse di un arco) o continuano
    // in modo tangente una delle due facce (catene di spigoli tangenti, come
    // un rettangolo con gli angoli raccordati); su una faccia piana obliqua la
    // zona di uno spigolo rettilineo si allunga oltre il vertice e si taglia
    // con il semispazio della faccia.
    for (BlendEdge &info : infos) {
        if (info.full) continue;
        const Edge &edge = body.edge(info.id);
        for (int end = 0; end < 2; ++end) {
            const VertexId v = end == 0 ? body.edgeStart(info.id) : body.edgeEnd(info.id);
            if (std::find(filletCorners.begin(), filletCorners.end(), v.index) != filletCorners.end()) continue;
            const Vec3 p = body.vertex(v).point;
            const Vec3 tangent = normalized(edge.curve->derivative(end == 0 ? edge.range.lo : edge.range.hi));
            const Vec3 n0 = outwardNormal(body, info.faces[0], p), n1 = outwardNormal(body, info.faces[1], p);
            std::vector<FaceId> others;
            bool flush = true;
            for (FaceId g : facesAround(body, v)) {
                if (g == info.faces[0] || g == info.faces[1]) continue;
                others.push_back(g);
                const Vec3 ng = outwardNormal(body, g, p);
                const bool normalEnd = std::fabs(dot(ng, tangent)) >= 1.0 - normalTolerance;
                const bool smooth = dot(ng, n0) >= 1.0 - normalTolerance || dot(ng, n1) >= 1.0 - normalTolerance;
                flush = flush && (normalEnd || smooth);
            }
            // Estremo concavo (la parete dello spigolo e la faccia B accanto
            // fanno un angolo concavo): il raccordo arriva fino a B dalla parte
            // in cui B guarda (l'aria davanti a B); se anche il bordo di B e'
            // scelto, i due raccordi si incontrano a mitra (la parte comune
            // dei due utensili allungati, sotto).
            if (info.convex && others.size() == 1) {
                const FaceId g = others.front();
                const Vec3 ng = outwardNormal(body, g, p);
                const Vec3 onward = end == 0 ? -tangent : tangent;
                const bool smoothOther = dot(ng, n0) >= 1.0 - normalTolerance || dot(ng, n1) >= 1.0 - normalTolerance;
                const double sine = -dot(ng, onward);
                if (!smoothOther && sine > 1e-3) {
                    // Il bordo di B sulla faccia comune, se e' scelto anche lui.
                    bool partner = false;
                    for (const BlendEdge &other : infos) {
                        if (&other == &info || !other.convex) continue;
                        const bool touches = body.edgeStart(other.id) == v || body.edgeEnd(other.id) == v;
                        const bool hasB = other.faces[0] == g || other.faces[1] == g;
                        const bool shares = other.faces[0] == info.faces[0] || other.faces[0] == info.faces[1] || other.faces[1] == info.faces[0]
                                         || other.faces[1] == info.faces[1];
                        partner = partner || (touches && hasB && shares);
                    }
                    if (!flush || partner) {
                        if (info.composite)
                            throw std::domain_error("blendEdges: faccia piu' corta del raggio accanto a uno spigolo che finisce in un angolo concavo (non gestita)");
                        // Gli utensili degli archi e delle catene sono scostati
                        // oltre le facce (blendRegion): lungo un bordo libero lo
                        // scostamento cade nell'aria, ma presso un angolo concavo,
                        // oltre la parete dello spigolo, c'e' il materiale della
                        // faccia accanto e l'utensile vi scaverebbe un gradino
                        // (volume sbagliato, facce spurie). Ci pensano le catene
                        // piane o il raccordo generale.
                        if (!info.straight || info.chain >= 0)
                            throw std::domain_error("blendEdges: arco o catena tangente che finisce in un angolo concavo (sezioni analitiche non esatte)");
                        const SurfaceType type = body.face(g).surface->type();
                        if (type != SurfaceType::Plane && type != SurfaceType::Cylinder)
                            throw std::domain_error("blendEdges: lo spigolo finisce in un angolo concavo contro una faccia non piana ne' cilindrica (non gestita)");
                        if (sine < 1e-2) throw std::domain_error("blendEdges: lo spigolo finisce contro una faccia quasi parallela");
                        // Allungamento: abbastanza perche' la parte comune con l'utensile
                        // del bordo di B (la mitra) stia tutta dentro.
                        double spread = 0.0;
                        for (const Vec2 &q : info.extent) spread = std::max(spread, distance(q, info.corner));
                        const double length = 2.0 * (spread + size) / sine;
                        if (info.straight) {
                            if (end == 0) info.from -= length;
                            else info.to += length;
                        } else {
                            const auto &circle = static_cast<const Circle<3> &>(*edge.curve);
                            const double free = kTwoPi - 1e-2 - edge.range.length() - info.angleFrom - info.angleTo;
                            const double delta = std::min(length / circle.radius(), 0.5 * free);
                            if (!(delta > 0.0)) throw std::domain_error("blendEdges: arco troppo lungo per l'angolo concavo all'estremo");
                            (end == 0 ? info.angleFrom : info.angleTo) = delta;
                        }
                        info.concaveEnds.push_back({v, g, p, ng, length + 2.0 * spread + size});
                        continue;
                    }
                }
            }
            if (flush) continue;
            if (info.composite)
                throw std::domain_error("blendEdges: faccia piu' corta del raggio accanto a uno spigolo che finisce contro una faccia obliqua (non gestita)");
            if (others.size() != 1 || body.face(others.front()).surface->type() != SurfaceType::Plane)
                throw std::domain_error("blendEdges: lo spigolo finisce contro facce non gestite (piu' facce, o una faccia curva non normale allo spigolo)");
            const Vec3 ng = outwardNormal(body, others.front(), p);
            const double c = dot(ng, end == 0 ? -tangent : tangent);
            if (c < 1e-3) throw std::domain_error("blendEdges: lo spigolo finisce contro una faccia quasi parallela");
            if (!info.straight) {
                // Arco: la rivoluzione si prolunga oltre il vertice finche' la sezione
                // sta tutta oltre il piano della faccia, poi si taglia con il suo semispazio.
                const auto &circle = static_cast<const Circle<3> &>(*edge.curve);
                const double theta = end == 0 ? edge.range.lo : edge.range.hi, direction = end == 0 ? -1.0 : 1.0;
                auto sectionPoint = [&](double phi, const Vec2 &q) {
                    return info.origin + q.x() * (std::cos(phi) * circle.xAxis() + std::sin(phi) * circle.yAxis()) + q.y() * info.axis;
                };
                double reach = 0.0;
                for (const Vec2 &q : info.extent) reach = std::max(reach, -dot(ng, sectionPoint(theta, q) - p));
                double delta = std::max(1e-3, (reach + 0.25 * size) / (circle.radius() * c));
                for (int attempt = 0;; ++attempt) {
                    bool beyond = true;
                    for (const Vec2 &q : info.extent) beyond = beyond && dot(ng, sectionPoint(theta + direction * delta, q) - p) > 0.0;
                    if (beyond) break;
                    delta *= 1.5;
                    if (attempt > 40 || edge.range.length() + info.angleFrom + info.angleTo + delta >= kTwoPi - 1e-3)
                        throw std::domain_error("blendEdges: l'arco finisce contro una faccia che il raccordo non raggiunge");
                }
                (end == 0 ? info.angleFrom : info.angleTo) = delta;
                info.trims.push_back({info.convex ? p + 1e-3 * size * ng : p, ng});
                continue;
            }
            double reach = 0.0;
            for (const Vec2 &q : info.extent) reach = std::max(reach, -dot(ng, (q.x() - info.corner.x()) * info.e1 + (q.y() - info.corner.y()) * info.e2));
            const double extension = (reach + 0.25 * size) / c;
            if (end == 0) info.from -= extension;
            else info.to += extension;
            // Sugli spigoli convessi (materiale tolto) il taglio sta appena fuori dalla
            // faccia: oltre non c'e' materiale, e l'utensile non ha facce sulla faccia del
            // corpo (due curve uguali tracciate due volte dividerebbero male la faccia).
            info.trims.push_back({info.convex ? p + 1e-3 * size * ng : p, ng});
        }
    }

    struct Tool {
        Body body;
        bool add;
        int chain;
    };
    std::vector<Tool> tools(infos.size());
    std::vector<Body> extended(infos.size());  // utensili allungati, prima dei tagli degli estremi concavi
    std::vector<std::exception_ptr> toolErrors(infos.size());
    // Estrusione/rivoluzione e tagli terminali di ciascun utensile sono
    // indipendenti. Le booleane interne vedono parallelDepth e non generano
    // altri pool: un livello di worker distribuisce gli utensili sui core.
    parallelFor(infos.size(), threadCount(0), [&](std::size_t k) {
      try {
        const BlendEdge &info = infos[k];
        Tool &tool = tools[k];
        tool.add = !info.convex;
        tool.chain = info.chain;
        if (info.straight) {
            if (!(info.to - info.from > tolerance)) throw std::domain_error("blendEdges: raggio troppo grande per la lunghezza dello spigolo");
            tool.body = makeExtrusion(Frame3(info.origin + info.from * info.axis, info.axis, info.e1), info.region, info.to - info.from);
            const double reach = 2.0 * (std::fabs(info.from) + std::fabs(info.to) + 10.0 * size);
            for (const auto &[point, normal] : info.trims)
                tool.body = booleanOperation(tool.body, halfSpace(point, normal, reach), BooleanOperation::Intersect);
        } else if (info.full) {
            tool.body = makeRevolution(Frame3(info.origin, info.axis, info.e1), info.region);
        } else {
            const Edge &edge = body.edge(info.id);
            const auto &circle = static_cast<const Circle<3> &>(*edge.curve);
            const double start = edge.range.lo - info.angleFrom;
            const Vec3 startDirection = std::cos(start) * circle.xAxis() + std::sin(start) * circle.yAxis();
            tool.body = makeRevolution(Frame3(info.origin, info.axis, startDirection), info.region, edge.range.length() + info.angleFrom + info.angleTo);
            const double reach = 4.0 * (circle.radius() + 10.0 * size);
            for (const auto &[point, normal] : info.trims)
                tool.body = booleanOperation(tool.body, halfSpace(point, normal, reach), BooleanOperation::Intersect);
        }
        if (!info.concaveEnds.empty()) extended[k] = tool.body;
      } catch (...) {
          toolErrors[k] = std::current_exception();
      }
    });
    for (const std::exception_ptr &error : toolErrors)
        if (error) std::rethrow_exception(error);
    // Estremi concavi: dell'utensile allungato resta la parte davanti a B (il
    // semispazio del piano, o il cilindro pieno o il suo complemento, con la
    // faccia esattamente sulla superficie di B). Se anche il bordo di B e'
    // scelto (e finisce qui contro la parete di questo spigolo) resta anche la
    // parte comune con il suo utensile allungato: oltre le due pareti, nel
    // materiale, i due raccordi si incontrano a mitra lungo la loro curva comune.
    // Verificato con gli smussi e i raccordi delle tasche poligonali (OCCT e i
    // volumi esatti); tra superfici curve, o quando le booleane non riescono
    // (i raccordi tangenti alle pareti nello spigolo verticale), le catene
    // piane di fk_blend_loop fanno lo stesso senza booleane.
    for (std::size_t k = 0; k < infos.size(); ++k) {
        for (const BlendEdge::ConcaveEnd &ce : infos[k].concaveEnds) {
            Body &toolBody = tools[k].body;
            const Surface &surface = *body.face(ce.face).surface;
            // Il taglio vale solo vicino al vertice (la superficie di B puo'
            // tornare vicino all'utensile altrove): nel cubo attorno al vertice
            // si toglie cio' che sta dietro B (e fuori dall'utensile del bordo di B).
            const double h = ce.near;
            const Vec3 diagonal(h, h, h);
            // Solo la meta' dello spigolo dalla parte del vertice (l'altro estremo ha il suo taglio).
            const Edge &edge = body.edge(infos[k].id);
            Vec3 middle[2];
            edge.curve->evaluate(0.5 * (edge.range.lo + edge.range.hi), 1, middle);
            const Vec3 away = body.edgeStart(infos[k].id) == ce.vertex ? normalized(middle[1]) : -normalized(middle[1]);
            const Body cube = booleanOperation(makeBox(Frame3(ce.point - diagonal, Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0 * h, 2.0 * h, 2.0 * h),
                                               halfSpace(middle[0], away, 4.0 * h), BooleanOperation::Intersect);
            Body behind;
            if (surface.type() == SurfaceType::Plane) {
                behind = booleanOperation(cube, halfSpace(ce.point, ce.normal, 4.0 * h), BooleanOperation::Intersect);
            } else {
                const auto &cylinder = static_cast<const CylindricalSurface &>(surface);
                const Frame3 &frame = cylinder.frame();
                const double along = dot(ce.point - frame.origin(), frame.zDir());
                const Body solid = makeCylinder(Frame3(frame.origin() + (along - 4.0 * h) * frame.zDir(), frame.zDir(), frame.xDir()), cylinder.radius(), 8.0 * h);
                const Vec3 radial = ce.point - frame.origin() - along * frame.zDir();
                const bool inward = dot(ce.normal, radial) < 0.0;  // B guarda l'asse: dietro B c'e' l'esterno del cilindro
                behind = booleanOperation(cube, solid, inward ? BooleanOperation::Subtract : BooleanOperation::Intersect);
            }
            // Il bordo di B scelto anche lui (che finisce qui contro la parete di
            // questo spigolo): dietro B resta la parte comune con il suo utensile.
            std::size_t partner = infos.size();
            for (std::size_t j = 0; j < infos.size(); ++j) {
                if (j == k || !(infos[j].faces[0] == ce.face || infos[j].faces[1] == ce.face)) continue;
                for (const BlendEdge::ConcaveEnd &other : infos[j].concaveEnds)
                    if (other.vertex == ce.vertex && (other.face == infos[k].faces[0] || other.face == infos[k].faces[1])) partner = j;
            }
            if (partner < infos.size()) {
                // Nella stessa catena gli utensili si cuciono: la mitra comune ai due non si potrebbe.
                if (infos[k].chain >= 0 && infos[k].chain == infos[partner].chain)
                    throw std::domain_error("blendEdges: angolo concavo tra due spigoli della stessa catena (le sezioni analitiche non lo gestiscono)");
                behind = booleanOperation(behind, extended[partner], BooleanOperation::Subtract);
            }
            const Body cut = booleanOperation(toolBody, behind, BooleanOperation::Intersect);
            if (cut.faces().empty()) continue;
            // La parte tolta deve stare dentro il cubo e prima della meta' dello
            // spigolo: se ne toccasse il bordo avrebbe edge sulle facce del cubo o
            // sul piano di mezzo (B torna vicino all'utensile: il taglio non e'
            // locale e non si sa trattare). Si provano i punti degli edge.
            const double margin = 1e-6 * h;
            bool local = true;
            for (EdgeId e : cut.edges()) {
                const Edge &cutEdge = cut.edge(e);
                if (!cutEdge.curve) continue;
                for (int step = 0; step <= 8; ++step) {
                    const Vec3 q = cutEdge.curve->point(cutEdge.range.lo + cutEdge.range.length() * step / 8.0);
                    for (int axis = 0; axis < 3; ++axis) local = local && std::fabs(q[axis] - ce.point[axis]) < h - margin;
                    local = local && dot(q - middle[0], away) < -margin;
                }
            }
            if (!local) throw std::domain_error("blendEdges: angolo concavo con la faccia accanto che torna vicino al raccordo (non gestito)");
            toolBody = booleanOperation(toolBody, behind, BooleanOperation::Subtract);
        }
    }
    // Gli utensili di ogni catena diventano uno.
    for (std::size_t k = 0; k < tools.size(); ++k) {
        if (tools[k].chain < 0) continue;
        std::vector<const Body *> pieces{&tools[k].body};
        for (std::size_t j = k + 1; j < tools.size(); ++j)
            if (tools[j].chain == tools[k].chain) pieces.push_back(&tools[j].body);
        Body sewn = sewBodies(pieces, tolerance * 100.0);
        const int chain = tools[k].chain;
        tools[k].body = std::move(sewn);
        tools[k].chain = -1;
        for (std::size_t j = tools.size(); j-- > k + 1;)
            if (tools[j].chain == chain) tools.erase(tools.begin() + std::ptrdiff_t(j));
    }
    for (Body &piece : corners) tools.push_back({std::move(piece), false, -1});
    Body result = body;
    for (const Tool &tool : tools) result = booleanOperation(result, tool.body, tool.add ? BooleanOperation::Unite : BooleanOperation::Subtract);
    return result;
}

}  // namespace

}
