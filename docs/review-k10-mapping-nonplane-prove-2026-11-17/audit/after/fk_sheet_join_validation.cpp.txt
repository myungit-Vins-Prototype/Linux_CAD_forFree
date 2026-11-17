#include "fk_sheet_join_validation.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

#include "fk_cancel.h"
#include "fk_surface_curve_bounds.h"

namespace ForgeCad::Kernel::detail {
namespace {
[[noreturn]] void invalidJoin() {
    throw std::domain_error("extendSheet: contratto della giunzione non ISO modificato");
}
void requireResult(const SurfaceCurveBoundResult &result, double budget) {
    if (result.status != SurfaceCurveBoundStatus::Certified
        || !std::isfinite(result.upperBound) || !(result.upperBound >= 0.0)
        || result.upperBound > budget)
        throw std::domain_error("extendSheet: giunzione non ISO fuori dal budget continuo");
}
}

std::vector<EmittedSheetJoin> certifySheetJoins(Body &body,
    const std::vector<SheetJoinPlan> &plans, const std::vector<EdgeId> &modelEdges) {
    const auto faces = body.faces();
    std::vector<EmittedSheetJoin> result;
    std::set<std::pair<int, int>> incidences;
    for (const auto &plan : plans) {
        cancellationPoint();
        if (plan.face < 0 || std::size_t(plan.face) >= faces.size()
            || plan.edge < 0 || std::size_t(plan.edge) >= modelEdges.size()
            || !std::isfinite(plan.budget) || !(plan.budget > 0.0)) invalidJoin();
        const FaceId face = faces[std::size_t(plan.face)];
        const EdgeId edgeId = modelEdges[std::size_t(plan.edge)];
        if (!body.contains(edgeId) || !incidences.emplace(face.index, edgeId.index).second) invalidJoin();
        const Edge &edge = body.edge(edgeId);
        if (edge.tolerance != plan.budget || edge.curve != plan.curve
            || edge.range.lo != plan.range.lo || edge.range.hi != plan.range.hi) invalidJoin();
        int uses = 0;
        for (LoopId loop : body.face(face).loops)
            for (FinId finId : body.loopFins(loop)) {
                Fin &fin = body.fin(finId);
                if (fin.edge != edgeId) continue;
                ++uses;
                if (!edge.curve || !body.face(face).surface || !fin.pcurve) invalidJoin();
                const auto candidate = surfaceCurvePCurveCandidate(edge.curve, body.face(face).surface, fin.pcurve, edge.range);
                const auto proof = surfaceCurveResidualBound(edge.curve, body.face(face).surface,
                    candidate, edge.range, plan.budget);
                if (proof.status == SurfaceCurveBoundStatus::Unsupported) continue;
                requireResult(proof, plan.budget);
                fin.pcurve = candidate;
                fin.pcurveTolerance = plan.budget;
                result.push_back({face, edgeId, edge.curve, fin.pcurve, edge.range, plan.budget, fin.sense});
            }
        if (uses != 1) invalidJoin();
    }
    return result;
}

void requireUnifiedSheetJoins(const Body &body, const BodyHistory &history,
    const std::vector<EmittedSheetJoin> &plans) {
    std::set<int> incidences;
    for (const auto &plan : plans) {
        cancellationPoint();
        const auto faces = history.descendants(EntityKind::Face, 0, plan.face.index);
        const auto edges = history.descendants(EntityKind::Edge, 0, plan.edge.index);
        int uses = 0;
        for (int faceIndex : faces) {
            const FaceId face(faceIndex);
            if (!body.contains(face)) invalidJoin();
            for (LoopId loop : body.face(face).loops)
                for (FinId finId : body.loopFins(loop)) {
                    const Fin &fin = body.fin(finId);
                    if (std::find(edges.begin(), edges.end(), fin.edge.index) == edges.end()) continue;
                    const Edge &edge = body.edge(fin.edge);
                    if (!incidences.insert(finId.index).second || edge.curve != plan.curve
                        || fin.pcurve != plan.pcurve || fin.sense != plan.sense
                        || edge.range.lo != plan.range.lo || edge.range.hi != plan.range.hi
                        || edge.tolerance != plan.budget || fin.pcurveTolerance != plan.budget) invalidJoin();
                    requireResult(surfaceCurveResidualBound(edge.curve, body.face(face).surface,
                        fin.pcurve, edge.range, plan.budget), plan.budget);
                    ++uses;
                }
        }
        if (uses != 1) invalidJoin();
    }
}
}
