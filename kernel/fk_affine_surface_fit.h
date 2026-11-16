#pragma once
#include "fk_curve_fit_bounds.h"
namespace ForgeCad::Kernel::detail {
// One original affine UV piece, active on parameter. For a Line2, first is
// its original origin and last its original direction, with line=true.
// Otherwise anchor is the ORIGINAL degree-one knot interval and first/last
// are its ORIGINAL UV poles. Restricting parameter never changes the anchors.
struct AffineUVSpan {
    Interval parameter;
    Interval anchor;
    Vec2 first, last;
    bool line = false;
};
// General affine UV composition on ORIGINAL ExtrusionSurface(Line/NURBS).
// Empty means unsupported target/UV family. Recognized unresolved, domain,
// resource or nonfinite inputs throw; no sampled fallback is a certificate.
SurfaceCurveFitData affineSurfaceCompositionErrorBound(const SurfacePtr &surface, const CurvePtr<2> &pcurve,
                                                       const Interval &range);
// Interval-arithmetic primitive owned by fk_curve_fit_bounds.cpp. Input
// pieces form a contiguous ordered partition, with original anchors above.
SurfaceCurveFitData extrusionAffineCompositionErrorBound(const SurfacePtr &surface,
                                                         const std::vector<AffineUVSpan> &spans,
                                                         const Interval &range);
}
