#include "fk_classify.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "fk_curve_algo.h"
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

PointLocation classifyPointOnFace(const Body &body, FaceId faceId, const Vec3 &point, double tolerance) {
    const Face &face = body.face(faceId);
    const Surface &surface = *face.surface;
    if (surface.isVPeriodic()) throw std::domain_error("classifyPointOnFace: superfici periodiche in v non gestite");
    if (distanceToFaceBoundary(body, faceId, point) <= tolerance) return PointLocation::Boundary;

    std::vector<FinId> fins;
    for (LoopId l : face.loops)
        for (FinId f : body.loopFins(l)) fins.push_back(f);
    if (fins.empty()) return PointLocation::Inside;  // faccia senza bordo

    const SurfaceProjection projection = projectPoint(surface, point);
    const double period = surface.isUPeriodic() ? surface.uPeriod() : 0.0;
    Vec3 d[4];
    surface.evaluate(projection.u, projection.v, 1, d);
    const double uStep = 0.1 * tolerance / std::max(norm(d[Surface::derivativeIndex(1, 0, 1)]), 1e-300);
    const double faceSign = face.sense ? 1.0 : -1.0;

    for (int attempt = 0; attempt < 12; ++attempt) {
        // Piccolo spostamento in u se la semiretta passa per un vertice o e'
        // tangente a una SP-curve (lo spostamento in 3D resta << tolerance).
        const double u = projection.u + uStep * (attempt % 2 ? -1.0 : 1.0) * ((attempt + 1) / 2), v = projection.v;
        double above = 1e300, below = -1e300;
        int aboveOrientation = 0, belowOrientation = 0;
        bool degenerate = false;
        for (FinId f : fins) {
            const Fin &fin = body.fin(f);
            const Edge &edge = body.edge(fin.edge);
            if (!fin.pcurve) throw std::logic_error("classifyPointOnFace: fin senza SP-curve");
            const Curve<2> &pcurve = *fin.pcurve;
            double uMin = 1e300, uMax = -1e300;
            for (int i = 0; i <= 32; ++i) {
                const double pu = pcurve.point(edge.range.lo + edge.range.length() * i / 32.0)[0];
                uMin = std::min(uMin, pu);
                uMax = std::max(uMax, pu);
            }
            int kLo = 0, kHi = 0;
            if (period > 0.0) {
                kLo = int(std::floor((uMin - u) / period)) - 1;
                kHi = int(std::ceil((uMax - u) / period)) + 1;
            }
            for (int k = kLo; k <= kHi && !degenerate; ++k) {
                const PlaneRoots<2> roots = planeRoots<2>(pcurve, edge.range, Vec2(1.0, 0.0), u + k * period,
                                                          1e-14 * (1.0 + std::fabs(u)));
                if (!roots.coincident.empty()) {
                    degenerate = true;
                    break;
                }
                for (double t : roots.parameters) {
                    const double margin = 1e-11 * std::max(1.0, edge.range.length());
                    if (t - edge.range.lo <= margin || edge.range.hi - t <= margin) {
                        degenerate = true;
                        break;
                    }
                    Vec2 p[2];
                    pcurve.evaluate(t, 1, p);
                    if (std::fabs(p[1][0]) <= 1e-9 * norm(p[1])) {
                        degenerate = true;
                        break;
                    }
                    // +1: il dominio sta sopra (v maggiori) nel punto di attraversamento.
                    const int orientation = (p[1][0] > 0.0 ? 1 : -1) * (fin.sense ? 1 : -1) * int(faceSign);
                    if (p[0][1] > v && p[0][1] < above) {
                        above = p[0][1];
                        aboveOrientation = orientation;
                    } else if (p[0][1] <= v && p[0][1] > below) {
                        below = p[0][1];
                        belowOrientation = orientation;
                    }
                }
            }
            if (degenerate) break;
        }
        if (degenerate) continue;
        if (aboveOrientation != 0) return aboveOrientation < 0 ? PointLocation::Inside : PointLocation::Outside;
        if (belowOrientation != 0) return belowOrientation > 0 ? PointLocation::Inside : PointLocation::Outside;
        return PointLocation::Outside;
    }
    throw std::domain_error("classifyPointOnFace: configurazione degenere");
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
    return box;
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
