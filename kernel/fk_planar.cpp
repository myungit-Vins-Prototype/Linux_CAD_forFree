#include "fk_planar.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "fk_curve_ops.h"
#include "fk_exchange.h"
#include "fk_intersect.h"
#include "fk_profile.h"
#include "fk_nurbs.h"
#include "fk_surface.h"

namespace ForgeCad::Kernel {

namespace {

// Loop orientato: il tratto k va dal vertice k al vertice k + 1 (il primo dopo
// l'ultimo), nel verso della curva se forward[k].
struct OrientedLoop {
    std::vector<PathSegment> pieces;
    std::vector<bool> forward;
    std::vector<Vec3> vertices;
};

OrientedLoop reversedLoop(const OrientedLoop &loop) {
    const std::size_t n = loop.pieces.size();
    OrientedLoop result;
    for (std::size_t k = 0; k < n; ++k) {
        result.pieces.push_back(loop.pieces[n - 1 - k]);
        result.forward.push_back(!loop.forward[n - 1 - k]);
        result.vertices.push_back(loop.vertices[(n - k) % n]);
    }
    return result;
}

// Campioni del loop nel suo verso (senza il punto finale di ogni tratto, che
// e' l'inizio del successivo): il poligono per Newell e l'annidamento.
std::vector<Vec3> loopSamples(const OrientedLoop &loop) {
    std::vector<Vec3> samples;
    for (std::size_t k = 0; k < loop.pieces.size(); ++k) {
        const PathSegment &piece = loop.pieces[k];
        std::vector<double> breaks = piece.curve->breakpoints(piece.range);
        if (breaks.size() < 2 || breaks.size() > 65) breaks = {piece.range.lo, piece.range.hi};
        const int perSpan = breaks.size() == 2 ? 64 : 16;
        std::vector<double> parameters;
        for (std::size_t b = 0; b + 1 < breaks.size(); ++b)
            for (int i = 0; i < perSpan; ++i) parameters.push_back(breaks[b] + (breaks[b + 1] - breaks[b]) * i / perSpan);
        parameters.push_back(piece.range.hi);
        if (!loop.forward[k]) std::reverse(parameters.begin(), parameters.end());
        for (std::size_t i = 0; i + 1 < parameters.size(); ++i) samples.push_back(piece.curve->point(parameters[i]));
    }
    return samples;
}

// Area vettoriale (normale di Newell, lunghezza = area) del poligono.
Vec3 newellNormal(const std::vector<Vec3> &polygon) {
    Vec3 c(0, 0, 0);
    for (const Vec3 &p : polygon) c = c + p;
    c = c / double(polygon.size());
    Vec3 n(0, 0, 0);
    for (std::size_t i = 0; i < polygon.size(); ++i) n = n + cross(polygon[i] - c, polygon[(i + 1) % polygon.size()] - c);
    return 0.5 * n;
}

double planeDistance(const Frame3 &plane, const Vec3 &p) { return std::fabs(dot(p - plane.origin(), plane.zDir())); }

// Scarto massimo del tratto dal piano, calcolato in modo esatto dove si puo'.
double planeDeviation(const Curve<3> &curve, const Interval &range, const Frame3 &plane) {
    switch (curve.type()) {
    case CurveType::Line:
        return std::max(planeDistance(plane, curve.point(range.lo)), planeDistance(plane, curve.point(range.hi)));
    case CurveType::Circle: {
        const auto &c = static_cast<const Circle<3> &>(curve);
        const Vec3 axis = normalized(cross(c.xAxis(), c.yAxis()));
        return planeDistance(plane, c.center()) + c.radius() * norm(cross(axis, plane.zDir()));
    }
    case CurveType::Ellipse: {
        const auto &e = static_cast<const Ellipse<3> &>(curve);
        const Vec3 axis = normalized(cross(e.xAxis(), e.yAxis()));
        return planeDistance(plane, e.center()) + std::max(e.xRadius(), e.yRadius()) * norm(cross(axis, plane.zDir()));
    }
    default:
        break;
    }
    // B-spline, curve limitate e trasformate: i poli dei tratti di Bezier
    // razionali (pesi positivi: la curva sta nell'inviluppo convesso).
    try {
        double worst = 0.0;
        for (const BSplineCurve<3> &piece : rationalBezierPieces(curve, range))
            for (const Vec3 &pole : piece.poles()) worst = std::max(worst, planeDistance(plane, pole));
        return worst;
    } catch (const std::exception &) {
    }
    // Le altre (eliche e simili): campioni fitti.
    std::vector<double> breaks = curve.breakpoints(range);
    if (breaks.size() < 2 || breaks.size() > 257) breaks = {range.lo, range.hi};
    const int perSpan = breaks.size() == 2 ? 512 : 32;
    double worst = 0.0;
    for (std::size_t b = 0; b + 1 < breaks.size(); ++b)
        for (int i = 0; i <= perSpan; ++i) worst = std::max(worst, planeDistance(plane, curve.point(breaks[b] + (breaks[b + 1] - breaks[b]) * i / perSpan)));
    return worst;
}

// Concatena i tratti per estremi entro la tolleranza.
OrientedLoop chainLoop(const std::vector<PathSegment> &segments, double tolerance) {
    if (segments.empty()) throw std::domain_error("superficie planare: loop vuoto");
    for (const PathSegment &s : segments)
        if (!s.curve || !(s.range.hi > s.range.lo)) throw std::domain_error("superficie planare: tratto non valido");
    OrientedLoop loop;
    std::vector<bool> used(segments.size(), false);
    loop.pieces.push_back(segments[0]);
    loop.forward.push_back(true);
    used[0] = true;
    const Vec3 first = segments[0].curve->point(segments[0].range.lo);
    Vec3 current = segments[0].curve->point(segments[0].range.hi);
    std::vector<Vec3> ends{first};
    for (std::size_t step = 1; step < segments.size(); ++step) {
        std::size_t best = segments.size();
        bool bestForward = true;
        double bestDistance = tolerance;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            if (used[i]) continue;
            const double ds = distance(current, segments[i].curve->point(segments[i].range.lo));
            const double de = distance(current, segments[i].curve->point(segments[i].range.hi));
            if (ds <= bestDistance) best = i, bestForward = true, bestDistance = ds;
            if (de < bestDistance) best = i, bestForward = false, bestDistance = de;
        }
        if (best == segments.size()) throw std::domain_error("superficie planare: loop aperto");
        used[best] = true;
        const PathSegment &s = segments[best];
        const Vec3 a = s.curve->point(bestForward ? s.range.lo : s.range.hi);
        ends.push_back(0.5 * (current + a));
        loop.pieces.push_back(s);
        loop.forward.push_back(bestForward);
        current = s.curve->point(bestForward ? s.range.hi : s.range.lo);
    }
    if (distance(current, first) > tolerance) throw std::domain_error("superficie planare: loop aperto");
    ends[0] = segments.size() == 1 ? first : 0.5 * (current + first);
    loop.vertices = ends;
    return loop;
}

struct PlanarFace {
    OrientedLoop outer;
    std::vector<OrientedLoop> holes;
};

// Lamina dalle facce gia' orientate (esterni antiorari attorno a plane.zDir(), fori orari).
Body buildSheet(const Frame3 &plane, const std::vector<PlanarFace> &faces) {
    detail::RawModel model;
    const auto surface = std::make_shared<Plane>(plane);
    for (const PlanarFace &face : faces) {
        detail::RawFace raw;
        raw.surface = surface;
        raw.sense = true;
        std::vector<const OrientedLoop *> loops{&face.outer};
        for (const OrientedLoop &hole : face.holes) loops.push_back(&hole);
        for (const OrientedLoop *loop : loops) {
            const int base = int(model.points.size());
            const int n = int(loop->pieces.size());
            for (const Vec3 &v : loop->vertices) model.points.push_back(v);
            std::vector<detail::RawFin> fins;
            for (int k = 0; k < n; ++k) {
                detail::RawEdge edge;
                const int a = base + k, b = base + (k + 1) % n;
                edge.start = loop->forward[std::size_t(k)] ? a : b;
                edge.end = loop->forward[std::size_t(k)] ? b : a;
                edge.curve = loop->pieces[std::size_t(k)].curve;
                edge.hasRange = true;
                edge.range = loop->pieces[std::size_t(k)].range;
                fins.push_back({int(model.edges.size()), loop->forward[std::size_t(k)]});
                model.edges.push_back(edge);
            }
            raw.loops.push_back(fins);
        }
        model.faces.push_back(raw);
    }
    return detail::assembleBody(model, false);
}

}

Body planarSheet(const std::vector<std::vector<PathSegment>> &input, double tolerance) {
    if (input.empty()) throw std::domain_error("superficie planare: nessun loop");
    std::vector<OrientedLoop> loops;
    std::vector<std::vector<Vec3>> samples;
    for (const auto &segments : input) {
        loops.push_back(chainLoop(segments, tolerance));
        samples.push_back(loopSamples(loops.back()));
    }

    // Piano: normale di Newell del loop piu' grande, verso del primo; origine nel baricentro.
    std::vector<Vec3> areas;
    std::size_t largest = 0;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        areas.push_back(newellNormal(samples[i]));
        if (norm(areas[i]) > norm(areas[largest])) largest = i;
    }
    Vec3 normal = areas[largest];
    if (!(norm(normal) > 0.0)) throw std::domain_error("superficie planare: loop degenere");
    normal = normalized(normal);
    if (dot(areas[0], normal) < 0.0) normal = -1.0 * normal;
    Vec3 centroid(0, 0, 0);
    std::size_t count = 0;
    for (const auto &polygon : samples)
        for (const Vec3 &p : polygon) centroid = centroid + p, ++count;
    centroid = centroid / double(count);
    const Vec3 reference = std::fabs(normal.x()) < 0.6 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    const Frame3 plane(centroid, normal, reference);

    // Verifica esatta della complanarita' (anche dei vertici uniti).
    for (const OrientedLoop &loop : loops) {
        for (const PathSegment &piece : loop.pieces)
            if (planeDeviation(*piece.curve, piece.range, plane) > tolerance) throw std::domain_error("superficie planare: le curve non stanno in un piano");
        for (const Vec3 &v : loop.vertices)
            if (planeDistance(plane, v) > tolerance) throw std::domain_error("superficie planare: le curve non stanno in un piano");
    }

    // Loop come curve 2D esatte nel sistema del piano (planarImage: stesso
    // tipo e parametro per rette e coniche nel piano, NURBS esatta per le
    // altre). Servono a verificare che i loop non si tocchino e
    // all'annidamento con il numero di avvolgimento (integrale sulle curve,
    // non sui poligoni delle corde: un foro vicino a un bordo curvo cadeva tra
    // la corda e l'arco e diventava una faccia sovrapposta).
    const std::size_t n = loops.size();
    std::vector<ProfileLoop> flat(n);
    std::vector<double> signedAreas(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < loops[i].pieces.size(); ++k) {
            const PathSegment &piece = loops[i].pieces[k];
            const PlanarImage image = planarImage(*piece.curve, piece.range, plane);
            if (!image.curve) throw std::domain_error("superficie planare: tratto perpendicolare al piano");
            if (loops[i].forward[k]) flat[i].segments.push_back({image.curve, image.range});
            else flat[i].segments.push_back({reversedCurve(image.curve), {-image.range.hi, -image.range.lo}});
        }
        signedAreas[i] = signedArea(flat[i]);
        if (!(std::fabs(signedAreas[i]) > tolerance * tolerance)) throw std::domain_error("superficie planare: loop degenere");
    }
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j)
            for (const ProfileSegment &a : flat[i].segments)
                for (const ProfileSegment &b : flat[j].segments) {
                    const CurveCurveIntersection hits = intersectCurves(*a.curve, a.range, *b.curve, b.range, tolerance);
                    if (!hits.points.empty() || hits.overlap)
                        throw std::domain_error("superficie planare: due contorni si toccano o si intersecano");
                }
    std::vector<int> depth(n, 0), parent(n, -1);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j || std::fabs(signedAreas[j]) <= std::fabs(signedAreas[i])) continue;
            if (windingNumber(flat[j], flat[i].segments.front().start()) == 0) continue;
            ++depth[i];
            if (parent[i] < 0 || std::fabs(signedAreas[j]) < std::fabs(signedAreas[std::size_t(parent[i])])) parent[i] = int(j);
        }
    std::vector<PlanarFace> faces;
    std::vector<int> faceOf(n, -1);
    for (std::size_t i = 0; i < n; ++i) {
        if (depth[i] % 2 != 0) continue;
        faceOf[i] = int(faces.size());
        faces.push_back({signedAreas[i] > 0.0 ? loops[i] : reversedLoop(loops[i]), {}});
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (depth[i] % 2 == 0) continue;
        const int owner = faceOf[std::size_t(parent[i])];
        if (owner < 0) throw std::domain_error("superficie planare: loop intrecciati");
        faces[std::size_t(owner)].holes.push_back(signedAreas[i] < 0.0 ? loops[i] : reversedLoop(loops[i]));
    }
    return buildSheet(plane, faces);
}

Body planarSheet(const Frame3 &frame, const std::vector<ProfileRegion> &regions) {
    if (regions.empty()) throw std::domain_error("superficie planare: nessuna regione");
    const auto convert = [&](const ProfileLoop &profile, bool counterclockwise) {
        if (profile.segments.empty()) throw std::domain_error("superficie planare: loop vuoto");
        const ProfileLoop loop = (signedArea(profile) > 0.0) == counterclockwise ? profile : reversed(profile);
        OrientedLoop result;
        for (const ProfileSegment &segment : loop.segments) {
            const CurvePtr<3> curve = embedCurve(segment.curve, frame, 0.0);
            result.pieces.push_back({curve, segment.range});
            result.forward.push_back(true);
            result.vertices.push_back(curve->point(segment.range.lo));
        }
        return result;
    };
    std::vector<PlanarFace> faces;
    for (const ProfileRegion &region : regions) {
        PlanarFace face{convert(region.outer, true), {}};
        for (const ProfileLoop &hole : region.holes) face.holes.push_back(convert(hole, false));
        faces.push_back(std::move(face));
    }
    return buildSheet(Frame3(frame.origin(), frame.zDir(), frame.xDir()), faces);
}

}
