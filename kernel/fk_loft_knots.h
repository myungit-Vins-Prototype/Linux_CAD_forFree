#ifndef FORGECAD_FK_LOFT_KNOTS_H
#define FORGECAD_FK_LOFT_KNOTS_H
#include "fk_bspline.h"
#include <vector>
namespace ForgeCad::Kernel::detail {
// Nodi delle guide riuniti entro il solo arrotondamento del parametro.
// Conserva nodi distinti della stessa guida e tutte le sezioni originali;
// verifica la variazione delle curve sui coefficienti raffinati comuni.
std::vector<double> harmonizeLoftGuideKnots(std::vector<BSplineCurvePtr<3>> &curves,
                                           const std::vector<double> &anchors);
}
#endif
