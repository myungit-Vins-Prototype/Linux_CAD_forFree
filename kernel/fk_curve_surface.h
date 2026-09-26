#ifndef FORGECAD_FK_CURVE_SURFACE_H
#define FORGECAD_FK_CURVE_SURFACE_H

#include <utility>
#include <vector>

#include "fk_bspline_surface.h"
#include "fk_intersect.h"

// Intersezione curva-superficie per le superfici che intersectCurveSurface
// non riduce a un problema piano:
//  - sfera, cono (entrambi i fogli) e toro hanno un'equazione implicita
//    polinomiale F(x) = 0 (grado 2 e 4): sostituendo i tratti di Bezier
//    razionali della curva si ottiene un polinomio di Bernstein nel
//    parametro del tratto, le cui radici sono tutte le intersezioni (esatte,
//    anche le tangenze entro la tolleranza);
//  - superfici B-spline e di rivoluzione: suddivisione dei tratti della
//    curva e delle pezze di Bezier della forma NURBS finche' i box dei poli
//    si separano o diventano piccoli, poi Newton su (s, u, v).
// I tratti in cui la curva giace sulla superficie vanno in `coincident`.
namespace ForgeCad::Kernel {

bool hasImplicitEquation(const Surface &surface);
CurveSurfaceIntersection implicitCurveSurface(const Curve<3> &curve, const Interval &range, const Surface &surface, double tolerance,
                                              bool *touching = nullptr);
CurveSurfaceIntersection numericCurveSurface(const Curve<3> &curve, const Interval &range, const Surface &surface, double tolerance,
                                             bool *touching = nullptr);

// Tratti della curva che giacciono sulla superficie (dentro il suo dominio),
// da campioni e bisezione; gli estremi si agganciano al punto in cui la curva
// incontra il bordo del dominio o una delle linee `snap` (per esempio la linea
// di nodo in cui una B-spline si separa da un'altra: li' lo scarto cresce
// lentamente e la bisezione da sola non basta).
std::vector<Interval> curveOnSurface(const Curve<3> &curve, const Interval &range, const Surface &surface, double tolerance,
                                     const std::vector<std::pair<CurvePtr<3>, Interval>> &snap = {});

// Box dei poli di una pezza e divisione a meta' del dominio in u o in v
// (per gli algoritmi di suddivisione).
Box patchBox(const BSplineSurface &patch);
std::pair<BSplineSurface, BSplineSurface> splitPatch(const BSplineSurface &patch, bool alongU);

}

#endif
