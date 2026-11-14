#include "cad_forge.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <vector>

#include "fk_classify.h"
#include "fk_intersect.h"
#include "fk_surface.h"
#include "fk_surface_algo.h"

namespace ForgeCad {
namespace {
using namespace Kernel;

// I B-rep sono immutabili. Per una selezione i box e le tolleranze dei
// candidati si calcolano una volta sola, anche scegliendo centinaia di facce.
// Preparazione pigra: un picking singolo si ferma al primo proprietario.
class FaceOwnerSearch {
public:
    explicit FaceOwnerSearch(const std::vector<std::pair<int, ForgeBody>> &chain) {
        for (const auto &[index, body] : chain) {
            if (!body) continue;
            for (FaceId face : body->faces())
                if (body->face(face).surface) candidates_.push_back({index, body.get(), face, 1e-6, {}});
        }
    }

    int owner(const Body &picked, int face, const Vec3 &point) {
        try {
            const Face &f = picked.face(FaceId(face));
            if (!f.surface) return -1;
            const SurfaceProjection onPicked = projectPoint(*f.surface, point);
            const Vec3 normal = f.surface->normal(onPicked.u, onPicked.v);
            for (auto &candidate : candidates_) {
                const Body &body = *candidate.body;
                const Face &g = body.face(candidate.face);
                if (!candidate.box) {
                    // Anche i bordi tolleranti degli STEP contribuiscono.
                    for (LoopId loop : g.loops)
                        for (FinId fin : body.loopFins(loop))
                            candidate.tolerance = std::max(candidate.tolerance, body.edge(body.fin(fin).edge).tolerance);
                    candidate.box = faceBox(body, candidate.face).padded(candidate.tolerance);
                }
                bool inside = true;
                for (int k = 0; k < 3; ++k)
                    inside = inside && point[k] >= candidate.box->lo[k] && point[k] <= candidate.box->hi[k];
                if (!inside) continue;
                bool same = sameSurface(*g.surface, *f.surface, candidate.tolerance);
                // Le copie rilette di superfici libere non condividono il
                // puntatore: resta il confronto sulla geometria esatta.
                if (!same && g.surface->type() == f.surface->type()) {
                    const SurfaceProjection on = projectPoint(*g.surface, point);
                    same = on.distance <= candidate.tolerance
                        && std::fabs(dot(g.surface->normal(on.u, on.v), normal)) > 1.0 - 1e-6;
                }
                if (same && classifyPointOnFace(body, candidate.face, point, candidate.tolerance) != PointLocation::Outside)
                    return candidate.feature;
            }
        } catch (const std::exception &) {
        }
        return -1;
    }

private:
    struct Candidate {
        int feature;
        const Body *body;
        FaceId face;
        double tolerance;
        std::optional<Box> box;
    };
    std::vector<Candidate> candidates_;
};
}

int forgeFaceOwner(const Kernel::Body &picked, int face, const Kernel::Vec3 &point,
                   const std::vector<std::pair<int, ForgeBody>> &chain) {
    return FaceOwnerSearch(chain).owner(picked, face, point);
}

QVector<int> forgeFeatureFaces(const Kernel::Body &result, const QVector<int> &faceIds, const QVector<QVector3D> &labelPoints,
                               int feature, const std::vector<std::pair<int, ForgeBody>> &chain) {
    using namespace Kernel;
    QVector<int> faces;
    std::vector<std::pair<int, ForgeBody>> upTo;
    for (const auto &entry : chain)
        if (entry.first <= feature) upTo.push_back(entry);
    if (upTo.empty()) return faces;
    FaceOwnerSearch search(upTo);
    for (qsizetype k = 0; k < faceIds.size() && k < labelPoints.size(); ++k) {
        try {
            const FaceId face(faceIds.at(k));
            const Face &f = result.face(face);
            if (!f.surface) continue;
            const QVector3D &label = labelPoints.at(k);
            const SurfaceProjection on = projectPoint(*f.surface, Vec3(label.x(), label.y(), label.z()));
            if (search.owner(result, faceIds.at(k), f.surface->point(on.u, on.v)) == feature) faces.append(faceIds.at(k));
        } catch (const std::exception &) {
        }
    }
    return faces;
}

}
