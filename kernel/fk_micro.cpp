#include "fk_micro.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "fk_curve_algo.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {

namespace {

// Normale uscente della faccia della fin nel punto dell'edge al parametro t:
// dall'SP-curve se c'e', altrimenti dalla proiezione.
Vec3 finNormal(const Body &body, FinId finId, double t) {
    const Fin &fin = body.fin(finId);
    const Face &face = body.face(body.finFace(finId));
    Vec2 uv;
    if (fin.pcurve) {
        uv = fin.pcurve->point(t);
    } else {
        const Edge &edge = body.edge(fin.edge);
        const SurfaceProjection projection = projectPoint(*face.surface, edge.curve->point(t));
        uv = Vec2(projection.u, projection.v);
    }
    const Vec3 n = normalAt(*face.surface, uv.x(), uv.y());
    return face.sense ? n : -n;
}

}

std::vector<MicroFeature> findMicroFeatures(const Body &body, const MicroFeatureOptions &options) {
    std::vector<MicroFeature> result;
    const auto midpoint = [&](EdgeId e) {
        const Edge &edge = body.edge(e);
        return edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
    };
    // Spigoli corti e quasi tangenti.
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        if (!edge.curve) continue;
        const double length = arcLength(*edge.curve, edge.range, 1e-9);
        if (length < options.shortEdge) result.push_back({MicroFeature::Kind::ShortEdge, e.index, length, midpoint(e)});
        if (!edge.forward.valid() || !edge.backward.valid()) continue;
        if (body.finFace(edge.forward) == body.finFace(edge.backward)) continue;
        double largest = 0.0;
        Vec3 where = midpoint(e);
        for (int k = 0; k <= 8; ++k) {
            const double t = edge.range.lo + edge.range.length() * (0.02 + 0.96 * k / 8.0);
            try {
                const Vec3 a = finNormal(body, edge.forward, t), b = finNormal(body, edge.backward, t);
                const double angle = std::atan2(norm(cross(a, b)), dot(a, b));
                if (angle > largest) largest = angle, where = edge.curve->point(t);
            } catch (const std::exception &) {
            }
        }
        if (largest > options.minAngle && largest < options.maxAngle)
            result.push_back({MicroFeature::Kind::NearTangentEdge, e.index, largest, where});
    }
    // Facce sottili: la maggior parte del bordo (in lunghezza) sta a meno di
    // thinWidth da un altro spigolo della faccia che non lo tocca.
    for (FaceId f : body.faces()) {
        std::vector<EdgeId> edges;
        for (LoopId l : body.face(f).loops)
            for (FinId fin : body.loopFins(l)) edges.push_back(body.fin(fin).edge);
        if (edges.size() < 3) continue;
        double total = 0.0, close = 0.0;
        std::vector<double> widths;
        Vec3 where;
        for (EdgeId e : edges) {
            const Edge &edge = body.edge(e);
            const VertexId s = body.edgeStart(e), t = body.edgeEnd(e);
            const double length = arcLength(*edge.curve, edge.range, 1e-9);
            for (int k = 0; k < 16; ++k) {
                const double parameter = edge.range.lo + edge.range.length() * (k + 0.5) / 16.0;
                const Vec3 p = edge.curve->point(parameter);
                double nearest = 1e300;
                for (EdgeId other : edges) {
                    if (other == e) continue;
                    const VertexId os = body.edgeStart(other), ot = body.edgeEnd(other);
                    if (os == s || os == t || ot == s || ot == t) continue;  // adiacente: vicino per forza
                    const Edge &o = body.edge(other);
                    nearest = std::min(nearest, projectPoint(*o.curve, p, o.range).distance);
                }
                total += length / 16.0;
                if (nearest < options.thinWidth) {
                    close += length / 16.0;
                    widths.push_back(nearest);
                    where = p;
                }
            }
        }
        if (total > 0.0 && close >= 0.5 * total && !widths.empty()) {
            std::nth_element(widths.begin(), widths.begin() + std::ptrdiff_t(widths.size() / 2), widths.end());
            result.push_back({MicroFeature::Kind::ThinFace, f.index, widths[widths.size() / 2], where});
        }
    }
    return result;
}

}
