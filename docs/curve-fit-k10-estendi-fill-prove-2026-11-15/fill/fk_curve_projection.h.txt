#ifndef FORGECAD_FK_CURVE_PROJECTION_H
#define FORGECAD_FK_CURVE_PROJECTION_H
#include "fk_curve.h"
namespace ForgeCad::Kernel {
// Affine coordinates frame.toLocal(source(t)).xy, in the ORIGINAL parameter.
// NURBS retain degree, knots and weights; trims and affine wrappers are retained
// mathematically. Lines become degree-1 splines on range (linear extrapolation).
// Returns null only for unsupported source types (including angular conics).
// Invalid input throws; cancellation is propagated. This projects off-plane
// curves too: it does not claim the source lies on the frame's plane.
CurvePtr<2> projectCurveAffine(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame);
} // namespace ForgeCad::Kernel
#endif
