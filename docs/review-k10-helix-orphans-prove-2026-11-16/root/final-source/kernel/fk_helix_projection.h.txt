#pragma once
#include "fk_curve_fit_bounds.h"
#include "fk_helix.h"
namespace ForgeCad::Kernel::detail {
// ORIGINAL Helix IEEE coefficients, wrapper matrices and target Frame axes.
// Both callbacks own their data and survive destruction of source geometry.
// Target: frame.toLocal(source(t)).XY in mm, in the ORIGINAL parameter.
struct HelixProjectionFitData {
    CurveFitErrorBound bound;
    std::function<Vec3(double)> value;
    double fourthDerivativeBound = 0;
};
// Empty only for a non-Helix basis. Recognized invalid domains, transforms,
// nonfinite or numerically unresolved data throw; a query may return infinity.
HelixProjectionFitData helixProjectionErrorBound(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame);
// Cubic nonrational C0 Bezier pcurve. Physical budget includes transforms and
// projection. Conservative finite work preflight may reject valid curves.
// No output/deviation is published on failure; unsupported source returns null.
std::shared_ptr<BSplineCurve<2>> projectHelixCertified(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame,
                                                       double tolerance, double *deviation = nullptr);
// Rebuilds the original-data oracle and validates all ACTUAL original 2D poles,
// knots, domains and cubic C0 Bezier structure. Returns the whole-curve bound.
double requireHelixProjection(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame, const CurvePtr<2> &actual,
                              double tolerance);
} // namespace ForgeCad::Kernel::detail
