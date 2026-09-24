#include "fk_classify.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "fk_curve_algo.h"
#include "fk_nurbs.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {

double distanceToFaceBoundary(const Body &body, FaceId face, const Vec3 &point) {
    double best = 1e300;
    for (LoopId l : body.face(face).loops)
        for (FinId f : body.loopFins(l)) {
            const Edge &edge = body.edge(body.fin(f).edge);
            best = std::min(best, projectPoint(*edge.curve, point, edge.range).distance);
        }
    return best;
}

namespace {
bool degenerateAt(const Surface &surface, double v);
}

PointLocation classifyPointOnFace(const Body &body, FaceId faceId, const Vec3 &point, double tolerance) {
    const Face &face = body.face(faceId);
    const Surface &surface = *face.surface;
    if (distanceToFaceBoundary(body, faceId, point) <= tolerance) return PointLocation::Boundary;

    std::vector<FinId> fins;
    for (LoopId l : face.loops)
        for (FinId f : body.loopFins(l)) fins.push_back(f);
    if (fins.empty()) return PointLocation::Inside;  // faccia senza bordo

    const SurfaceProjection projection = projectPoint(surface, point);
    const double periods[2] = {surface.isUPeriodic() ? surface.uPeriod() : 0.0, surface.isVPeriodic() ? surface.vPeriod() : 0.0};
    Vec3 d[4];
    surface.evaluate(projection.u, projection.v, 1, d);
    const double steps[2] = {0.1 * tolerance / std::max(norm(d[Surface::derivativeIndex(1, 0, 1)]), 1e-300),
                             0.1 * tolerance / std::max(norm(d[Surface::derivativeIndex(0, 1, 1)]), 1e-300)};
    const double faceSign = face.sense ? 1.0 : -1.0;

    // Semiretta lungo la coordinata `along` (1: verticale, v varia; 0:
    // orizzontale) dal punto (spostato di poco nell'altra coordinata se passa
    // per un vertice o e' tangente a una SP-curve). Nelle direzioni
    // periodiche fa il giro. Risultato: 1 dentro, 0 fuori, -1 nessun
    // attraversamento, -2 configurazione degenere.
    auto cast = [&](int along, int attempt) {
        const int across = 1 - along;
        Vec2 uv(projection.u, projection.v);
        uv[across] += steps[across] * (attempt % 2 ? -1.0 : 1.0) * ((attempt + 1) / 2);
        const double c = uv[across], position = uv[along];
        const double period = periods[across], periodAlong = periods[along];
        double above = 1e300, below = -1e300;
        int aboveOrientation = 0, belowOrientation = 0;
        Vec2 normal;
        normal[across] = 1.0;
        for (FinId f : fins) {
            const Fin &fin = body.fin(f);
            const Edge &edge = body.edge(fin.edge);
            if (!fin.pcurve) throw std::logic_error("classifyPointOnFace: fin senza SP-curve");
            const Curve<2> &pcurve = *fin.pcurve;
            double lo = 1e300, hi = -1e300;
            for (int i = 0; i <= 32; ++i) {
                const double value = pcurve.point(edge.range.lo + edge.range.length() * i / 32.0)[across];
                lo = std::min(lo, value);
                hi = std::max(hi, value);
            }
            int kLo = 0, kHi = 0;
            if (period > 0.0) {
                kLo = int(std::floor((lo - c) / period)) - 1;
                kHi = int(std::ceil((hi - c) / period)) + 1;
            }
            for (int k = kLo; k <= kHi; ++k) {
                const PlaneRoots<2> roots = planeRoots<2>(pcurve, edge.range, normal, c + k * period, 1e-14 * (1.0 + std::fabs(c)));
                if (!roots.coincident.empty()) return -2;
                for (double t : roots.parameters) {
                    const double margin = 1e-11 * std::max(1.0, edge.range.length());
                    if (t - edge.range.lo <= margin || edge.range.hi - t <= margin) return -2;
                    Vec2 p[2];
                    pcurve.evaluate(t, 1, p);
                    if (std::fabs(p[1][across]) <= 1e-9 * norm(p[1])) return -2;
                    // +1: il dominio sta dalla parte delle quote maggiori lungo la semiretta.
                    const int orientation = (p[1][across] > 0.0 ? 1 : -1) * (fin.sense ? 1 : -1) * int(faceSign) * (along == 1 ? 1 : -1);
                    const double value = p[0][along];
                    if (periodAlong > 0.0) {
                        double up = value + periodAlong * std::ceil((position - value) / periodAlong);
                        if (up <= position) up += periodAlong;
                        if (up < above) {
                            above = up;
                            aboveOrientation = orientation;
                        }
                        if (up - periodAlong > below) {
                            below = up - periodAlong;
                            belowOrientation = orientation;
                        }
                    } else if (value > position && value < above) {
                        above = value;
                        aboveOrientation = orientation;
                    } else if (value <= position && value > below) {
                        below = value;
                        belowOrientation = orientation;
                    }
                }
            }
        }
        if (aboveOrientation != 0) return aboveOrientation < 0 ? 1 : 0;
        if (belowOrientation != 0) return belowOrientation > 0 ? 1 : 0;
        return -1;
    };
    bool degenerate = false;
    for (int along : {1, 0}) {
        // La semiretta orizzontale serve solo sul toro (loop avvolti in v).
        if (along == 0 && !surface.isVPeriodic()) break;
        degenerate = false;
        for (int attempt = 0; attempt < 12; ++attempt) {
            const int result = cast(along, attempt);
            if (result == -2) {
                degenerate = true;
                continue;
            }
            if (result >= 0) return result == 1 ? PointLocation::Inside : PointLocation::Outside;
            degenerate = false;
            break;  // nessun attraversamento: si prova l'altra direzione
        }
    }
    if (degenerate) throw std::domain_error("classifyPointOnFace: configurazione degenere");
    // Nessun loop incontrato: su una superficie chiusa (sfera, toro) la
    // faccia e' "tutto meno i fori" se i suoi loop sono fori (area negativa
    // nel verso della faccia), altrimenti il punto e' fuori.
    const Interval vDomain = surface.vDomain();
    const bool closed = surface.isUPeriodic() && (surface.isVPeriodic() || (vDomain.isFinite() && degenerateAt(surface, vDomain.lo) && degenerateAt(surface, vDomain.hi)));
    if (!closed) return PointLocation::Outside;
    double area = 0.0;
    for (FinId f : fins) {
        const Fin &fin = body.fin(f);
        const Edge &edge = body.edge(fin.edge);
        for (int i = 0; i < 16; ++i) {
            const double t0 = edge.range.lo + edge.range.length() * i / 16.0, t1 = edge.range.lo + edge.range.length() * (i + 1) / 16.0;
            area += cross(fin.pcurve->point(t0), fin.pcurve->point(t1)) * (fin.sense ? 1.0 : -1.0);
        }
    }
    return area * faceSign < 0.0 ? PointLocation::Inside : PointLocation::Outside;
}

namespace {

bool degenerateAt(const Surface &surface, double v) {
    try {
        for (double u : {0.3, 2.1, 4.4}) {
            Vec3 d[4];
            surface.evaluate(u, v, 1, d);
            if (norm(d[Surface::derivativeIndex(1, 0, 1)]) > 1e-9 * (1.0 + norm(d[0]))) return false;
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

}

Box faceBox(const Body &body, FaceId face) {
    Box box;
    const Surface &surface = *body.face(face).surface;
    if (surface.type() == SurfaceType::Sphere) {
        const auto &sphere = static_cast<const SphericalSurface &>(surface);
        return Box{sphere.frame().origin(), sphere.frame().origin()}.padded(sphere.radius());
    }
    for (LoopId l : body.face(face).loops) {
        for (FinId f : body.loopFins(l)) {
            const Edge &edge = body.edge(body.fin(f).edge);
            box.add(curveBox(*edge.curve, edge.range));
        }
        if (body.loop(l).isolatedVertex.valid()) box.add(body.vertex(body.loop(l).isolatedVertex).point);
    }
    const SurfaceType type = surface.type();
    if (type == SurfaceType::Plane || type == SurfaceType::Cylinder || type == SurfaceType::Extrusion) return box;
    // Coni, tori, rivoluzioni, B-spline: la faccia puo' uscire dal box dei
    // suoi edge (e senza edge non ne ha). Box dei poli della forma NURBS
    // sulla finestra dei parametri della faccia: il periodo intero nelle
    // direzioni periodiche, altrimenti i valori delle SP-curve allargati ai
    // poli e al vertice del cono se la faccia ha un solo loop avvolto (una calotta).
    Interval u = surface.uDomain(), v = surface.vDomain();
    Interval pu{1e300, -1e300}, pv{1e300, -1e300};
    int wrapping = 0;
    bool any = false;
    for (LoopId l : body.face(face).loops) {
        double start = 0.0, travelled = 0.0;
        bool first = true;
        for (FinId f : body.loopFins(l)) {
            const Fin &fin = body.fin(f);
            if (!fin.pcurve) continue;
            const Edge &edge = body.edge(fin.edge);
            for (int i = 0; i <= 16; ++i) {
                const Vec2 uv = fin.pcurve->point(edge.range.lo + edge.range.length() * i / 16.0);
                pu = {std::min(pu.lo, uv[0]), std::max(pu.hi, uv[0])};
                pv = {std::min(pv.lo, uv[1]), std::max(pv.hi, uv[1])};
                any = true;
            }
            const double a = fin.pcurve->point(fin.sense ? edge.range.lo : edge.range.hi)[0];
            const double b = fin.pcurve->point(fin.sense ? edge.range.hi : edge.range.lo)[0];
            if (first) start = a;
            first = false;
            travelled += b - a;
        }
        (void)start;
        if (surface.isUPeriodic() && std::fabs(travelled) > 0.5 * surface.uPeriod()) ++wrapping;
    }
    if (surface.isUPeriodic()) u = {u.lo, u.lo + surface.uPeriod()};
    else if (any) u = pu;
    if (surface.isVPeriodic()) {
        v = {v.lo, v.lo + surface.vPeriod()};
    } else if (any) {
        Interval w = pv;
        if (v.isFinite()) {
            if (degenerateAt(surface, v.lo)) w.lo = v.lo;
            if (degenerateAt(surface, v.hi)) w.hi = v.hi;
        }
        if (type == SurfaceType::Cone && wrapping == 1) {
            const auto &cone = static_cast<const ConicalSurface &>(surface);
            const double apex = -cone.referenceRadius() / std::sin(cone.semiAngle());
            w = {std::min(w.lo, apex), std::max(w.hi, apex)};
        }
        v = w;
    }
    if (!u.isFinite() || !v.isFinite()) return box;
    Box result;
    const BSplineSurface nurbs = toBSplineSurface(surface, u, v);
    for (int i = 0; i < nurbs.uPoleCount(); ++i)
        for (int j = 0; j < nurbs.vPoleCount(); ++j) result.add(nurbs.pole(i, j));
    return result;
}

SolidClassifier::SolidClassifier(const Body &body, double tolerance) : body_(body), tolerance_(tolerance) {
    for (FaceId f : body.faces()) {
        faces_.push_back(f);
        boxes_.push_back(faceBox(body, f).padded(tolerance));
    }
}

namespace {

bool rayHitsBox(const Vec3 &origin, const Vec3 &direction, const Box &box) {
    double lo = 0.0, hi = 1e300;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(direction[i]) < 1e-300) {
            if (origin[i] < box.lo[i] || origin[i] > box.hi[i]) return false;
            continue;
        }
        double a = (box.lo[i] - origin[i]) / direction[i], b = (box.hi[i] - origin[i]) / direction[i];
        if (a > b) std::swap(a, b);
        lo = std::max(lo, a);
        hi = std::min(hi, b);
    }
    return lo <= hi;
}

}

bool firstRayHit(const Body &body, const Vec3 &origin, const Vec3 &direction, double tolerance, double &t) {
    const Vec3 unit = normalized(direction);
    const double scale = norm(direction);
    double best = 1e300;
    for (FaceId f : body.faces()) {
        const Box box = faceBox(body, f).padded(10.0 * tolerance);
        Interval range;
        if (!clipLineToBox(origin, unit, box, range) || range.hi < 0.0 || range.lo > best) continue;
        bool grazing = false;
        std::vector<double> hits;
        try {
            hits = intersectLineSurface(origin, unit, *body.face(f).surface, grazing, tolerance);
        } catch (const std::domain_error &) {
            continue;  // superficie non gestita: la faccia non si seleziona
        }
        for (double hit : hits) {
            if (hit < 0.0 || hit >= best) continue;
            try {
                if (classifyPointOnFace(body, f, origin + hit * unit, tolerance) != PointLocation::Outside) best = hit;
            } catch (const std::exception &) {
            }
        }
    }
    if (best == 1e300) return false;
    t = best / scale;
    return true;
}

PointLocation SolidClassifier::classify(const Vec3 &point) const {
    static const Vec3 directions[] = {
        Vec3(0.5773502691896258, 0.5773502691896258, 0.5773502691896258), Vec3(0.2672612419124244, -0.5345224838248488, 0.8017837257372732),
        Vec3(-0.7071067811865475, 0.1, 0.7), Vec3(0.3, 0.9, -0.316227766), Vec3(-0.41, -0.62, -0.668),
        Vec3(0.83, -0.21, 0.516), Vec3(-0.12, 0.77, 0.626), Vec3(0.6, -0.7, -0.387)};
    for (const Vec3 &raw : directions) {
        const Vec3 direction = normalized(raw);
        int crossings = 0;
        bool reliable = true;
        for (std::size_t i = 0; i < faces_.size() && reliable; ++i) {
            if (!rayHitsBox(point, direction, boxes_[i])) continue;
            const Face &face = body_.face(faces_[i]);
            bool grazing = false;
            const std::vector<double> hits = intersectLineSurface(point, direction, *face.surface, grazing, tolerance_);
            for (double t : hits) {
                if (t < -tolerance_) continue;
                const Vec3 x = point + t * direction;
                const PointLocation location = classifyPointOnFace(body_, faces_[i], x, tolerance_);
                if (t <= tolerance_) {
                    if (location != PointLocation::Outside) return PointLocation::Boundary;
                    continue;
                }
                if (location == PointLocation::Boundary) {
                    reliable = false;
                    break;
                }
                if (location == PointLocation::Inside) ++crossings;
            }
            if (grazing) {
                // Raggio tangente o sulla superficie: conta solo se tocca la faccia.
                reliable = false;
            }
        }
        if (reliable) return crossings % 2 ? PointLocation::Inside : PointLocation::Outside;
    }
    throw std::domain_error("SolidClassifier: nessun raggio affidabile");
}

}
