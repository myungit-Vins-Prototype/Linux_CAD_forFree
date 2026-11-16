#pragma once
#include "fk_curve.h"
#include "fk_surface.h"
#include "fk_topology.h"
#include <array>
namespace ForgeCad::Kernel::detail {
// Curva di riferimento del piano normale: il B-rep originale resta immutato.
// reach e' la distanza fisica massima delle ricerche oltre gli estremi.
CurvePtr<3> blendSectionCurve(const CurvePtr<3> &curve, const Interval &range, double reach = 0.0);
struct BlendModel;
struct BlendBoundaryGuide {
    CurvePtr<3> curve;
    std::array<CurvePtr<2>, 2> pcurves;
    double budget = 0.0;
};
// Empty only for unsupported families. ISO maps are recognized on original
// coefficients. Recognized invalid/unresolved proofs throw, never downgrade.
BlendBoundaryGuide blendBoundaryIsoGuide(const CurvePtr<3> &original,
    const std::array<CurvePtr<2>, 2> &pcurves, const std::array<SurfacePtr, 2> &supports,
    const Interval &originalRange, const Interval &window, double fitTolerance, double budget);
struct BlendBoundaryUse {
    int edge = -1, face = -1;
    CurvePtr<3> curve;
    SurfacePtr surface;
    CurvePtr<2> pcurve;
    Interval window;
    double budget = 0.0, edgeTolerance = 0.0, vertexBudget = 0.0;
};
// Curves and carriers are committed only after the recognized ISO branch
// proves both actual carrier compositions and the preserved source parameter.
// Other families retain the explicitly sampled Newton/legacy continuation.
void extendBlendBoundary(Body &body, BlendModel &model, EdgeId edge, const Interval &window,
                         bool tangentSeams, std::vector<BlendBoundaryUse> &certificates);
void installBlendBoundaryCertificates(BlendModel &model, std::vector<BlendBoundaryUse> &certificates);
void requireEmittedBlendBoundaries(const Body &body, const std::vector<BlendBoundaryUse> &certificates);
} // namespace ForgeCad::Kernel::detail
