#ifndef FORGECAD_FK_CURVE_ALGO_H
#define FORGECAD_FK_CURVE_ALGO_H

#include "fk_curve.h"

// Interrogazioni sulle curve: proiezione di un punto, lunghezza d'arco.
namespace ForgeCad::Kernel {

template <int N>
struct CurveProjection {
    double parameter = 0.0;
    Vec<N> point;
    double distance = 0.0;
};

// Punto di range piu' vicino a p (estremi compresi, a differenza di
// GeomAPI_ProjectPointOnCurve che restituisce solo le proiezioni ortogonali).
// Retta e cerchio in forma chiusa; B-spline e NURBS con tutte le radici della
// condizione di stazionarieta' (isolamento di Bernstein, nessun minimo perso);
// le altre curve (ellisse) con campionamento per tratti lisci e Newton.
template <int N>
CurveProjection<N> projectPoint(const Curve<N> &curve, const Vec<N> &p, const Interval &range);
// Tutti i minimi locali della distanza su range, estremi e spigoli compresi
// (in ordine qualsiasi, con possibili doppioni): servono quando il minimo
// globale da solo non basta, per esempio nelle proiezioni su superfici che
// si riducono a una curva. Esaustivo per rette, cerchi e B-spline.
template <int N>
std::vector<CurveProjection<N>> projectPointCandidates(const Curve<N> &curve, const Vec<N> &p, const Interval &range);
// Sul dominio della curva. Il dominio (o range) puo' essere infinito solo per
// la retta, anche se trasformata o limitata.
template <int N>
CurveProjection<N> projectPoint(const Curve<N> &curve, const Vec<N> &p);

// Lunghezza d'arco su range con quadratura adattiva di Gauss-Kronrod 7-15,
// tratto liscio per tratto liscio. tolerance e' l'errore assoluto stimato.
template <int N>
double arcLength(const Curve<N> &curve, const Interval &range, double tolerance = 1.0e-10);
template <int N>
double arcLength(const Curve<N> &curve, double tolerance = 1.0e-10);

}

#endif
