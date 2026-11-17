#ifndef FORGECAD_FK_SHEET_JOIN_VALIDATION_H
#define FORGECAD_FK_SHEET_JOIN_VALIDATION_H

#include "fk_history.h"
#include "fk_topology.h"

namespace ForgeCad::Kernel::detail {

// Indices in BlendModel, before its explicit edge compaction. The budget is
// frozen before fitting; it must never be inferred from the emitted residual.
struct SheetJoinPlan {
    int face;
    int edge;
    double budget;
    CurvePtr<3> curve;
    Interval range;
};
struct EmittedSheetJoin {
    FaceId face;
    EdgeId edge;
    CurvePtr<3> curve;
    CurvePtr<2> pcurve;
    Interval range;
    double budget;
    bool sense;
};

// Called on the private body after computePCurves. For the recognized native
// family, first constructs a bounded pcurve candidate with the same edge
// parameter; E/S are unchanged. ONE continuous proof then accepts or throws.
// Unsupported families retain the original pointer and legacy path. Certified fins
// publish the original fixed budget, not the sampled fitting deviation.
std::vector<EmittedSheetJoin> certifySheetJoins(Body &body,
    const std::vector<SheetJoinPlan> &plans, const std::vector<EdgeId> &modelEdges);

// Tracks explicit descendants through unify, also without user history.
// No matching by geometric proximity or silent downgrade to unsupported.
void requireUnifiedSheetJoins(const Body &body, const BodyHistory &history,
    const std::vector<EmittedSheetJoin> &plans);

}
#endif
