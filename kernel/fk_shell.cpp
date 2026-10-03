#include "fk_shell.h"

#include <algorithm>
#include <functional>
#include <locale>
#include <sstream>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "fk_boolean.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_exchange.h"
#include "fk_extrude.h"
#include "fk_intersect.h"
#include "fk_offset.h"
#include "fk_precision.h"
#include "fk_primitives.h"
#include "fk_profile.h"
#include "fk_sew.h"
#include "fk_surface.h"
#include "fk_surface_algo.h"
#include "fk_sweep.h"

namespace ForgeCad::Kernel {
namespace {

// Normale uscente della faccia nel punto della sua superficie piu' vicino a p.
Vec3 outwardNormal(const Body &body, FaceId f, const Vec3 &p) {
    const Face &face = body.face(f);
    const SurfaceProjection at = projectPoint(*face.surface, p);
    const Vec3 n = normalAt(*face.surface, at.u, at.v);
    return face.sense ? n : -n;
}


// Lastra di una faccia piana: la regione della faccia estrusa verso l'interno.
Body planarSlab(const Body &body, FaceId f, const Plane &plane, double thickness, double tolerance) {
    const Face &face = body.face(f);
    // Un punto della faccia per il verso: il primo vertice del primo loop.
    const Vec3 sample = body.finPoint(body.loop(face.loops.front()).first, 0.5);
    const Vec3 outward = outwardNormal(body, f, sample);
    const Frame3 frame(plane.frame().origin(), -outward, plane.frame().xDir());
    std::vector<ProfileSegment> segments;
    double edgeTolerance = 0.0;
    for (LoopId l : face.loops)
        for (FinId fin : body.loopFins(l)) {
            const Edge &edge = body.edge(body.fin(fin).edge);
            edgeTolerance = std::max(edgeTolerance, edge.tolerance);
            const PlanarImage image = planarImage(*edge.curve, edge.range, frame);
            if (image.curve) segments.push_back({image.curve, image.range});
        }
    const Profile profile = buildProfile(segments, std::max(tolerance, 2.0 * edgeTolerance));
    if (profile.regions.size() != 1) throw std::domain_error("guscio: regione di una faccia piana non valida");
    return makeExtrusion(frame, profile.regions.front(), thickness);
}

// Coppia di curve resa compatibile: stessi nodi (stesso grado, stesso dominio).
void makeCompatible(BSplineCurve<3> &a, BSplineCurve<3> &b) {
    std::vector<double> values;
    for (const BSplineCurve<3> *c : {&a, &b}) {
        const std::vector<double> &k = c->knots();
        for (std::size_t i = std::size_t(c->degree()) + 1; i < std::size_t(c->poleCount()); ++i) values.push_back(k[i]);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    for (double u : values) {
        const int target = std::max(a.multiplicity(u), b.multiplicity(u));
        if (a.multiplicity(u) < target) a = a.insertKnot(u, target - a.multiplicity(u));
        if (b.multiplicity(u) < target) b = b.insertKnot(u, target - b.multiplicity(u));
    }
}

// Fascia lungo il tratto `range` di un bordo della faccia f: le rette lungo la
// normale tra il bordo C(t) e il bordo a distanza D(t) = C(t) - s n(C(t)),
// nello stesso parametro (rigata bicubica x lineare esatta delle due curve
// approssimate entro 1e-9; edge: il bordo esatto, D, le due rette).
Body rimSheet(const Body &body, FaceId f, FaceId neighbor, const Edge &edge, const Interval &range, double thickness) {
    const auto onEdge = [&](double t) { return edge.curve->point(t); };
    const auto offset = [&](double t) {
        const Vec3 p = edge.curve->point(t);
        return p - thickness * outwardNormal(body, f, p);
    };
    std::vector<double> breaks = edge.curve->breakpoints(range);
    breaks.erase(std::remove_if(breaks.begin(), breaks.end(), [&](double t) { return !(t > range.lo && t < range.hi); }), breaks.end());
    BSplineCurve<3> c = *fitCurve(onEdge, range, breaks, 1e-9), d = *fitCurve(offset, range, breaks, 1e-9);
    makeCompatible(c, d);
    const int n = c.poleCount();
    std::vector<Vec3> poles(std::size_t(2 * n));
    for (int i = 0; i < n; ++i) {
        poles[std::size_t(2 * i)] = c.poles()[std::size_t(i)];
        poles[std::size_t(2 * i + 1)] = d.poles()[std::size_t(i)];
    }
    detail::RawModel model;
    model.points = {onEdge(range.lo), onEdge(range.hi), offset(range.lo), offset(range.hi)};
    const auto add = [&](int start, int end, CurvePtr<3> curve, const Interval &interval) {
        detail::RawEdge e;
        e.start = start;
        e.end = end;
        e.curve = std::move(curve);
        e.hasRange = true;
        e.range = interval;
        model.edges.push_back(e);
    };
    add(0, 1, edge.curve, range);
    add(1, 3, std::make_shared<Line<3>>(model.points[1], model.points[3] - model.points[1]), {0.0, distance(model.points[1], model.points[3])});
    add(2, 3, std::make_shared<BSplineCurve<3>>(d), range);
    add(0, 2, std::make_shared<Line<3>>(model.points[0], model.points[2] - model.points[0]), {0.0, distance(model.points[0], model.points[2])});
    detail::RawFace face;
    face.surface = std::make_shared<BSplineSurface>(3, 1, c.knots(), std::vector<double>{0.0, 0.0, 1.0, 1.0}, n, 2, std::move(poles));
    face.sense = true;
    face.loops.push_back({{0, true}, {1, true}, {2, false}, {3, false}});
    // La fascia giace sulla superficie della faccia accanto (una parete a 90
    // gradi: piano, cilindro...): quella superficie, cosi' nelle booleane la
    // fascia e la parete sono la stessa superficie e non due coincidenti.
    if (neighbor.valid()) {
        const SurfacePtr &surface = body.face(neighbor).surface;
        bool on = true;
        for (int k = 0; k <= 4 && on; ++k) {
            const Vec3 p = onEdge(range.lo + range.length() * k / 4.0);
            const Vec3 n = outwardNormal(body, f, p);
            for (double s : {0.5, 1.0}) {
                const Vec3 q = p - s * thickness * n;
                const SurfaceProjection at = projectPoint(*surface, q);
                if (distance(surface->point(at.u, at.v), q) > 1e-9 * std::max(1.0, norm(q))) on = false;
            }
        }
        if (on) {
            face.surface = surface;
            // Su un cilindro la cucitura (u = 0) va dalla parte opposta alla
            // fascia: lo stesso cilindro per le booleane, ma la fascia non e'
            // tagliata in due nello spazio (u, v).
            if (surface->type() == SurfaceType::Cylinder) {
                const auto &cylinder = static_cast<const CylindricalSurface &>(*surface);
                const Frame3 &cf = cylinder.frame();
                const Vec3 p = onEdge(range.lo + 0.5 * range.length()) - cf.origin();
                const Vec3 radial = p - dot(p, cf.zDir()) * cf.zDir();
                if (norm(radial) > 0.0) face.surface = std::make_shared<CylindricalSurface>(Frame3(cf.origin(), cf.zDir(), -normalized(radial)), cylinder.radius());
            }
            // Verso della faccia: quello della rigata (u lungo il bordo, v dal
            // bordo al bordo a distanza); sulle superfici periodiche assembleBody
            // non lo ricava dai loop.
            const double t = range.lo + 0.5 * range.length();
            Vec3 d[2];
            edge.curve->evaluate(t, 1, d);
            const Vec3 q = d[0] - 0.5 * thickness * outwardNormal(body, f, d[0]);
            const Vec3 ruled = cross(d[1], offset(t) - d[0]);
            const SurfaceProjection at = projectPoint(*face.surface, q);
            face.sense = dot(face.surface->normal(at.u, at.v), ruled) > 0.0;
            model.faces.push_back(face);
            return detail::assembleBody(model, false);
        }
    }
    // Retta con la normale costante (generatrice di un cilindro, bordo di un
    // raccordo): il piano della retta e della normale, calcolato dagli assi
    // come i fianchi delle lastre piane vicine (complanari per le booleane).
    if (edge.curve->type() == CurveType::Line) {
        const Vec3 a = edge.curve->point(range.lo), b = edge.curve->point(range.hi);
        const Vec3 na = outwardNormal(body, f, a), nb = outwardNormal(body, f, b);
        if (norm(cross(na, nb)) <= 1e-12) {
            const Vec3 direction = normalized(b - a);
            face.surface = std::make_shared<Plane>(Frame3(a, normalized(cross(direction, -na)), direction));
            model.faces.push_back(face);
            return detail::assembleBody(model, false);
        }
    }
    // Fascia piana (il bordo di un cilindro su un piano normale all'asse...):
    // il piano, e se coincide con una faccia piana del solido proprio quello,
    // cosi' le booleane la riconoscono complanare.
    if (SurfacePtr plane = planarEquivalent(*face.surface, 1e-9)) {
        face.surface = plane;
        const Frame3 &pf = static_cast<const Plane &>(*plane).frame();
        for (FaceId g : body.faces()) {
            const SurfacePtr &other = body.face(g).surface;
            if (other->type() != SurfaceType::Plane) continue;
            const Frame3 &of = static_cast<const Plane &>(*other).frame();
            if (norm(cross(of.zDir(), pf.zDir())) <= 1e-9 && std::fabs(dot(of.zDir(), pf.origin() - of.origin())) <= 1e-9 * std::max(1.0, norm(pf.origin()))) {
                face.surface = other;
                break;
            }
        }
    }
    model.faces.push_back(face);
    return detail::assembleBody(model, false);
}

// Lastra di un gruppo di facce unite da spigoli tangenti (una faccia curva
// da sola, o piani e raccordi): le facce, la loro superficie a distanza
// (offsetFaces le tiene cucite lungo gli spigoli tangenti) e le fasce lungo il
// bordo del gruppo, cucite in un solido. Un gruppo senza bordo (tutto il corpo
// liscio) da' direttamente il solido interno: la lastra e' il corpo meno quello.
Body groupSlab(const Body &body, const std::vector<FaceId> &group, double thickness, double tolerance) {
    const OffsetResult offset = offsetFaces(body, group, -thickness);
    const Body original = facesAsSheet(body, group);
    const auto inGroup = [&](FaceId f) { return std::find(group.begin(), group.end(), f) != group.end(); };
    std::vector<Body> rims;
    for (FaceId f : group)
        for (LoopId l : body.face(f).loops)
            for (FinId fin : body.loopFins(l)) {
                const EdgeId e = body.fin(fin).edge;
                const FinId other = body.otherFin(fin);
                if (other.valid() && inGroup(body.finFace(other))) continue;  // interno al gruppo
                const Edge &edge = body.edge(e);
                const FaceId neighbor = other.valid() ? body.finFace(other) : FaceId();
                // Un edge chiuso in due meta' (la fascia ha quattro lati).
                if (distance(edge.curve->point(edge.range.lo), edge.curve->point(edge.range.hi)) <= tolerance) {
                    const double mid = 0.5 * (edge.range.lo + edge.range.hi);
                    rims.push_back(rimSheet(body, f, neighbor, edge, {edge.range.lo, mid}, thickness));
                    rims.push_back(rimSheet(body, f, neighbor, edge, {mid, edge.range.hi}, thickness));
                } else {
                    rims.push_back(rimSheet(body, f, neighbor, edge, edge.range, thickness));
                }
            }
    if (rims.empty()) {
        SewResult inner = sewSheets({&offset.body}, 10.0 * tolerance, true);
        if (!inner.solid) throw std::domain_error("guscio: la superficie interna non si chiude");
        return booleanOperation(body, inner.body, BooleanOperation::Subtract);
    }
    std::vector<const Body *> sheets{&original, &offset.body};
    for (const Body &rim : rims) sheets.push_back(&rim);
    SewResult sewn = sewSheets(sheets, 10.0 * tolerance, true);
    if (!sewn.solid) throw std::domain_error("guscio: la lastra di un gruppo di facce non si chiude");
    return std::move(sewn.body);
}

// Tubo di raggio r attorno a un edge.
std::string formatNumber(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << value;
    return out.str();
}

Body tube(const Edge &edge, double radius) {
    Vec3 d[2];
    edge.curve->evaluate(edge.range.lo, 1, d);
    const Vec3 start = d[0];
    if (edge.curve->type() == CurveType::Line) {
        const Vec3 end = edge.curve->point(edge.range.hi);
        return makeCylinder(normalFrame(normalized(end - start), start), radius, distance(start, end));
    }
    const Frame3 frame = normalFrame(normalized(d[1]), start);
    const auto circle = std::make_shared<Circle<2>>(makeCircle(Vec2(0.0, 0.0), radius));
    const Profile profile = buildProfile({{circle, {0.0, kTwoPi}}}, 1e-9);
    return sweepRegions(frame, profile.regions, {{edge.curve, edge.range}}, SweepOrientation::MinimalTwist);
}

}

Body shellBody(const Body &solid, const std::vector<FaceId> &removed, double thickness) {
    if (solid.isSheet()) throw std::domain_error("guscio: serve un solido");
    if (!(thickness > kLinearResolution)) throw std::domain_error("guscio: lo spessore deve essere positivo");
    double scale = 1.0;
    for (VertexId v : solid.vertices()) scale = std::max(scale, norm(solid.vertex(v).point));
    const double tolerance = 1e-7 * scale;
    std::vector<FaceId> kept;
    for (FaceId f : solid.faces())
        if (std::find(removed.begin(), removed.end(), f) == removed.end()) kept.push_back(f);
    if (kept.empty()) throw std::domain_error("guscio: non resta nessuna faccia");
    const auto isKept = [&](FaceId f) { return std::find(kept.begin(), kept.end(), f) != kept.end(); };

    // Facce convesse con un raggio non maggiore dello spessore: la superficie a
    // distanza degenera (come negli altri CAD: guscio prima dei raccordi, o
    // spessore minore).
    for (FaceId f : kept) {
        const Face &face = solid.face(f);
        if (face.loops.empty()) throw std::domain_error("guscio: faccia senza bordi (sfera o toro interi) non gestita");
        double radius = 0.0;
        const Surface &surface = *face.surface;
        if (surface.type() == SurfaceType::Cylinder) radius = static_cast<const CylindricalSurface &>(surface).radius();
        else if (surface.type() == SurfaceType::Sphere) radius = static_cast<const SphericalSurface &>(surface).radius();
        else if (surface.type() == SurfaceType::Torus) radius = static_cast<const ToroidalSurface &>(surface).minorRadius();
        if (face.sense && radius > 0.0 && radius <= thickness * (1.0 + 1e-9))
            throw std::domain_error("lo spessore " + formatNumber(thickness) + " non e' minore del raggio " + formatNumber(radius) +
                                    " di una faccia convessa (raccordo): usa uno spessore minore o fai il guscio prima dei raccordi");
    }
    // Gruppi di facce unite da spigoli tangenti (union-find).
    std::vector<int> parent(kept.size());
    for (std::size_t k = 0; k < kept.size(); ++k) parent[k] = int(k);
    const std::function<int(int)> root = [&](int k) { return parent[std::size_t(k)] == k ? k : parent[std::size_t(k)] = root(parent[std::size_t(k)]); };
    const auto indexOf = [&](FaceId f) { return int(std::find(kept.begin(), kept.end(), f) - kept.begin()); };
    for (EdgeId e : solid.edges()) {
        const Edge &edge = solid.edge(e);
        if (!edge.forward.valid() || !edge.backward.valid()) continue;
        const FaceId a = solid.finFace(edge.forward), b = solid.finFace(edge.backward);
        if (a == b || !isKept(a) || !isKept(b)) continue;
        const Vec3 p = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
        if (norm(cross(outwardNormal(solid, a, p), outwardNormal(solid, b, p))) < 1e-6) parent[std::size_t(root(indexOf(a)))] = root(indexOf(b));
    }
    std::vector<std::vector<FaceId>> groups;
    std::vector<int> groupOf(kept.size(), -1);
    for (std::size_t k = 0; k < kept.size(); ++k) {
        const int r = root(int(k));
        if (groupOf[std::size_t(r)] < 0) {
            groupOf[std::size_t(r)] = int(groups.size());
            groups.emplace_back();
        }
        groups[std::size_t(groupOf[std::size_t(r)])].push_back(kept[k]);
    }
    std::vector<Body> parts;
    for (const std::vector<FaceId> &group : groups) {
        const Face &face = solid.face(group.front());
        SurfacePtr plane = face.surface->type() == SurfaceType::Plane ? face.surface : planarEquivalent(*face.surface);
        if (group.size() == 1 && plane && plane->type() == SurfaceType::Plane)
            parts.push_back(planarSlab(solid, group.front(), static_cast<const Plane &>(*plane), thickness, tolerance));
        else
            parts.push_back(groupSlab(solid, group, thickness, tolerance));
    }
    // Spigoli concavi tra due facce tenute: tubo di raggio pari allo spessore.
    for (EdgeId e : solid.edges()) {
        const Edge &edge = solid.edge(e);
        if (!edge.forward.valid() || !edge.backward.valid()) continue;
        const FaceId a = solid.finFace(edge.forward), b = solid.finFace(edge.backward);
        if (a == b || !isKept(a) || !isKept(b)) continue;
        Vec3 d[2];
        const double t = 0.5 * (edge.range.lo + edge.range.hi);
        edge.curve->evaluate(t, 1, d);
        const Vec3 na = outwardNormal(solid, a, d[0]), nb = outwardNormal(solid, b, d[0]);
        const Vec3 turn = cross(na, nb);
        if (norm(turn) < 1e-6) continue;  // facce tangenti
        // La fin forward percorre l'edge nel verso della curva con la faccia a sinistra.
        if (dot(turn, d[1]) < 0.0) parts.push_back(tube(edge, thickness));
    }
    // Unione ad albero bilanciato, poi la parte dentro il solido.
    while (parts.size() > 1) {
        std::vector<Body> next;
        for (std::size_t k = 0; k + 1 < parts.size(); k += 2)
            next.push_back(booleanOperation(parts[k], parts[k + 1], BooleanOperation::Unite));
        if (parts.size() % 2 == 1) next.push_back(std::move(parts.back()));
        parts = std::move(next);
    }
    return booleanOperation(solid, parts.front(), BooleanOperation::Intersect);
}

}
