#include "fk_blend_contour.h"
#include "fk_intersect.h"
#include "fk_precision.h"
#include "fk_profile.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <map>
#include <set>
namespace ForgeCad::Kernel {
namespace {
std::vector<EdgeId> contourAlong(const Body &body, EdgeId seed, const Vec3 &direction) {
    std::set<int> supports;
    for (auto f : body.faces()) {
        const auto &surface = *body.face(f).surface;
        GeneralizedCylinder cylinder;
        if (generalizedCylinder(surface, cylinder)) {
            if (norm(cross(cylinder.direction, direction)) <= angularResolution()) supports.insert(f.index);
        } else if (const auto *plane = dynamic_cast<const Plane *>(&surface)) {
            if (std::abs(dot(plane->frame().zDir(), direction)) <= angularResolution()) supports.insert(f.index);
        }
    }
    std::map<int, std::vector<EdgeId>> at;
    std::set<int> boundary;
    for (auto e : body.edges()) {
        if (body.isLaminar(e)) continue;
        const auto &edge = body.edge(e);
        if (supports.count(body.finFace(edge.forward).index) == supports.count(body.finFace(edge.backward).index)) continue;
        boundary.insert(e.index);
        at[body.edgeStart(e).index].push_back(e);
        at[body.edgeEnd(e).index].push_back(e);
    }
    if (!boundary.count(seed.index)) return {};
    std::vector<EdgeId> result;
    std::set<int> seen;
    const auto start = body.edgeStart(seed);
    auto vertex = start;
    auto edge = seed;
    while (seen.insert(edge.index).second) {
        result.push_back(edge);
        vertex = body.edgeStart(edge) == vertex ? body.edgeEnd(edge) : body.edgeStart(edge);
        const auto &neighbors = at.at(vertex.index);
        if (neighbors.size() != 2) return {};
        const auto next = neighbors[0] == edge ? neighbors[1] : neighbors[0];
        if (next == seed) return vertex == start && result.size() >= 3 ? result : std::vector<EdgeId>{};
        edge = next;
    }
    return {};
}
}
std::vector<EdgeId> blendCavityContour(const Body &body, EdgeId seed) {
    if (!body.contains(seed) || body.isLaminar(seed)) return {};
    std::vector<EdgeId> result;
    std::set<int> previous;
    const auto &edge = body.edge(seed);
    std::vector<Vec3> directions;
    const auto addDirection = [&](FaceId face) {
        GeneralizedCylinder cylinder;
        if (generalizedCylinder(*body.face(face).surface, cylinder)) directions.push_back(cylinder.direction);
    };
    for (auto fin : {edge.forward, edge.backward}) {
        const auto face = body.finFace(fin);
        addDirection(face);
        // Il fondo piano non ha un generatore proprio. Le sue adiacenze
        // forniscono le direzioni candidate, poi si richiede un unico ciclo.
        if (body.face(face).surface->type() == SurfaceType::Plane)
            for (auto loop : body.face(face).loops) for (auto use : body.loopFins(loop)) {
                const auto opposite = body.otherFin(use);
                if (opposite.valid()) addDirection(body.finFace(opposite));
            }
    }
    for (const auto &direction : directions) {
        auto contour = contourAlong(body, seed, direction);
        if (contour.empty()) continue;
        std::set<int> current;
        for (auto e : contour) current.insert(e.index);
        if (!previous.empty() && previous != current) return {};
        previous = std::move(current);
        result = std::move(contour);
    }
    return result;
}
void validateBlendTrimOrientation(const Body &body, const std::vector<FaceId> &faces) {
    for (auto f : faces) {
        const auto &face = body.face(f);
        if (face.surface->isUPeriodic() || face.surface->isVPeriodic() || face.loops.empty()) continue;
        std::vector<double> areas;
        for (auto l : face.loops) {
            ProfileLoop loop;
            for (auto use : body.loopFins(l)) {
                const auto &fin = body.fin(use);
                if (!fin.pcurve) throw std::domain_error("blendEdges: contorno rifilato senza SP-curve");
                ProfileSegment segment{fin.pcurve,body.edge(fin.edge).range};
                loop.segments.push_back(fin.sense ? segment : reversed(segment));
            }
            if (!loop.segments.empty()) areas.push_back((face.sense ? 1.0 : -1.0)*signedArea(loop));
        }
        if (areas.empty()) continue;
        const auto largest = std::max_element(areas.begin(),areas.end(),[](double a,double b){return std::abs(a)<std::abs(b);});
        if (*largest <= 0.0 || std::count_if(areas.begin(),areas.end(),[](double a){return a>0.0;}) != 1)
            throw std::domain_error("blendEdges: raggio troppo grande: il raccordo consuma o inverte la faccia F"+std::to_string(f.index));
    }
}

}
