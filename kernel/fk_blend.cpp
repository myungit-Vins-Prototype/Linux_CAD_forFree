#include "fk_blend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>

#include "fk_blend_loop.h"
#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_extrude.h"
#include "fk_intersect.h"
#include "fk_pcurve.h"
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
                          double tolerance, double outward = 0.0, std::vector<Vec2> *extent = nullptr) {
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
    if (extent) {
        // Punti che racchiudono la zona (con un margine di chi li usa: gli archi sporgono poco).
        extent->assign({corner, t1, t2});
        for (const ProfileSegment &segment : segments) extent->push_back(segment.curve->point(0.5 * (segment.range.lo + segment.range.hi)));
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
Body chamferCorner(const Corner &c, double distanceFromEdge) {
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
    std::vector<Vec3> points;
    for (int i = 0; i < 3; ++i) {
        int k, l;
        c.edgesOf(i, k, l);
        const double beta = distanceFromEdge / dot(c.d[l], inward(k, i)), alpha = distanceFromEdge / dot(c.d[k], inward(l, i));
        points.push_back(c.vertex + alpha * c.d[k] + beta * c.d[l]);  // F_i
    }
    Vec3 m[3];
    double offsets[3];
    for (int k = 0; k < 3; ++k) {
        m[k] = normalized(cross(c.d[k], u[k][1] - u[k][0]));
        offsets[k] = dot(m[k], distanceFromEdge * u[k][0]);
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
    int chain = -1;  // catena di spigoli tangenti (utensili cuciti in uno solo)
    ProfileRegion region;
    std::vector<Vec2> extent;  // punti che racchiudono la zona nella sezione
    double from = 0.0, to = 0.0;  // retta: tratto lungo l'asse dall'origine (il vertice iniziale)
    std::vector<std::pair<Vec3, Vec3>> trims;  // retta: piani (punto, normale uscente) che limitano la zona agli estremi
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

Body blendEdges(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer) {
    if (body.isSheet()) throw std::domain_error("blendEdges: solo solidi");
    if (!(size > kLinearResolution)) throw std::domain_error("blendEdges: raggio o distanza non validi");
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    const double scale = std::max(box.diagonal(), 1.0), tolerance = 1e-9 * scale, probe = 1e-4 * std::min(scale, size);
    const double normalTolerance = 1e-9;

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
    if (!freeform.empty()) {
        const std::vector<EdgeId> runs = planarChainRuns(body, edges, freeform);
        std::vector<Vec3> others;
        for (EdgeId e : edges)
            if (std::find(runs.begin(), runs.end(), e) == runs.end())
                others.push_back(body.edge(e).curve->point(0.5 * (body.edge(e).range.lo + body.edge(e).range.hi)));
        Body result = blendPlanarChains(body, runs, size, chamfer);
        if (others.empty()) return result;
        std::vector<EdgeId> rest;
        for (const Vec3 &p : others) {
            const EdgeId e = nearestEdge(result, p, 1e-7 * scale);
            if (!e.valid()) throw std::domain_error("blendEdges: spigoli che toccano i raccordi dei bordi di forma libera");
            rest.push_back(e);
        }
        return blendEdges(result, rest, size, chamfer);
    }

    std::vector<BlendEdge> infos;
    for (EdgeId e : edges) {
        const Edge &edge = body.edge(e);
        if (!edge.curve || body.isLaminar(e)) throw std::domain_error("blendEdges: spigolo non valido");
        for (const BlendEdge &other : infos)
            if (other.id == e) throw std::domain_error("blendEdges: spigolo scelto due volte");
        BlendEdge info;
        info.id = e;
        info.faces[0] = body.finFace(edge.forward);
        info.faces[1] = body.finFace(edge.backward);
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
        info.convex = turn > 0.0;
        info.section[0] = section[0];
        info.section[1] = section[1];
        infos.push_back(std::move(info));
    }

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
    for (BlendEdge &info : infos) {
        const bool offset = !info.straight || info.chain >= 0;
        info.region = blendRegion(info.section[0], info.section[1], info.corner, size, chamfer, info.convex, tolerance, offset ? 0.05 * size : 0.0, &info.extent);
    }

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
            corners.push_back(chamferCorner(c, size));
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
            if (flush) continue;
            if (!info.straight || others.size() != 1 || body.face(others.front()).surface->type() != SurfaceType::Plane)
                throw std::domain_error("blendEdges: lo spigolo finisce contro facce non gestite");
            const Vec3 ng = outwardNormal(body, others.front(), p);
            const double c = dot(ng, end == 0 ? -tangent : tangent);
            if (c < 1e-3) throw std::domain_error("blendEdges: lo spigolo finisce contro una faccia quasi parallela");
            double reach = 0.0;
            for (const Vec2 &q : info.extent) reach = std::max(reach, -dot(ng, (q.x() - info.corner.x()) * info.e1 + (q.y() - info.corner.y()) * info.e2));
            const double extension = (reach + 0.25 * size) / c;
            if (end == 0) info.from -= extension;
            else info.to += extension;
            info.trims.push_back({p, ng});
        }
    }

    struct Tool {
        Body body;
        bool add;
        int chain;
    };
    std::vector<Tool> tools;
    for (const BlendEdge &info : infos) {
        Tool tool;
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
            tool.body = makeRevolution(Frame3(info.origin, info.axis, edge.curve->point(edge.range.lo) - info.origin), info.region, edge.range.length());
        }
        tools.push_back(std::move(tool));
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

}
