#pragma once
#include "fk_curve.h"
namespace ForgeCad::Kernel::detail {
// Curva di riferimento del piano normale: il B-rep originale resta immutato.
// reach e' la distanza fisica massima delle ricerche oltre gli estremi.
CurvePtr<3> blendSectionCurve(const CurvePtr<3> &curve, const Interval &range, double reach = 0.0);
} // namespace ForgeCad::Kernel::detail
