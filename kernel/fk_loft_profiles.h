#ifndef FORGECAD_FK_LOFT_PROFILES_H
#define FORGECAD_FK_LOFT_PROFILES_H
#include "fk_bspline.h"
#include <vector>
namespace ForgeCad::Kernel::detail {
// Operazioni algebriche sui profili: nessun fit della geometria di sezione.
std::vector<BSplineCurve<3>> loftParameterPieces(const Curve<3> &, const Interval &, int degree);
int normalizeLoftParameterJoins(std::vector<std::vector<BSplineCurve<3>>> &, const std::vector<std::vector<double>> &);
void commonLoftDenominator(std::vector<BSplineCurve<3>> &, bool blendGuides, std::vector<double> &leftBlend,
                           std::vector<double> &rightBlend);
} // namespace ForgeCad::Kernel::detail
#endif
