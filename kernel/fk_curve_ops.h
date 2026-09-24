#ifndef FORGECAD_FK_CURVE_OPS_H
#define FORGECAD_FK_CURVE_OPS_H

#include "fk_curve.h"

// Operazioni esatte che producono nuove curve dello stesso tipo.
namespace ForgeCad::Kernel {

// Curva percorsa al contrario: R(s) = C(-s), quindi il tratto [a, b] di C
// diventa il tratto [-b, -a] di R. Rette, coniche, B-spline, curve limitate e
// trasformate restano del loro tipo.
template <int N>
CurvePtr<N> reversedCurve(const CurvePtr<N> &curve);

// Curva del piano XY di `frame` portata nello spazio, alla quota z lungo la
// normale: (x, y) -> frame.toGlobal(x, y, z). Stesso parametro, stesso tipo.
CurvePtr<3> embedCurve(const CurvePtr<2> &curve, const Frame3 &frame, double z = 0.0);

// Curva traslata di `offset` (stesso parametro, stesso tipo): per esempio
// un'SP-curve spostata di un periodo.
template <int N>
CurvePtr<N> translatedCurve(const CurvePtr<N> &curve, const Vec<N> &offset);

}

#endif
