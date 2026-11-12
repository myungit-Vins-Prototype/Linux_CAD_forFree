#ifndef FORGECAD_FK_NURBS_JOIN_H
#define FORGECAD_FK_NURBS_JOIN_H

#include <optional>
#include "fk_bspline_surface.h"

namespace ForgeCad::Kernel::detail {

struct JoinedNurbsSurface {
    std::shared_ptr<const BSplineSurface> surface;
    bool reverseNormalA = false, reverseNormalB = false;
    int boundaryAxisA = 0, boundaryAxisB = 0;
    double boundaryA = 0.0, boundaryB = 0.0;
};

// Atlante a due carte: concatenazione omogenea, senza refit. Si accettano
// bordi isoparametrici con base trasversa affine identica e jet G1
// certificati sui coefficienti (D_B = alpha D_A + beta H, alpha > 0).
// Scambio U/V e inversioni sono inclusi. Un risultato assente non modifica
// le superfici e non implica che esse non siano geometricamente tangenti.
std::optional<JoinedNurbsSurface> joinTangentNurbs(const BSplineSurface &a, const BSplineSurface &b);

// Concatenazione dei trim polinomiali G1. Le micro-campate terminali del
// marching possono essere regolarizzate solo con un limite sui coefficienti
// di Bernstein <= budget/2; l'altro mezzo budget copre il punto di giunzione.
// Nessun campione o allargamento delle tolleranze decide la sostituzione.
CurvePtr<3> joinTangentSplineCurves(const CurvePtr<3> &a, const Interval &rangeA, bool forwardA,
                                  const CurvePtr<3> &b, const Interval &rangeB, bool forwardB, double budget);

}
#endif
