#ifndef FORGECAD_FK_NURBS_H
#define FORGECAD_FK_NURBS_H

#include <vector>

#include "fk_bspline_surface.h"

// Forme NURBS esatte di curve e superfici qualsiasi del kernel, per gli
// algoritmi che lavorano sui poli (box che contengono la geometria,
// suddivisione di Bezier, composizione con le equazioni implicite). La
// geometria coincide; il parametro NURBS di coniche, cerchi e superfici di
// rivoluzione non e' quello della curva o superficie originale (solo i nodi
// cadono sugli stessi valori): i risultati si riportano sull'originale per
// proiezione.
namespace ForgeCad::Kernel {

// Tratti di Bezier razionali della curva che coprono `range` (rette,
// coniche, B-spline, curve limitate e trasformate). Per le B-spline i tratti
// conservano il parametro della curva e quelli agli estremi sono tagliati
// esattamente sul range.
std::vector<BSplineCurve<3>> rationalBezierPieces(const Curve<3> &curve, const Interval &range);

// Gli stessi tratti in forma standard (pesi 1 agli estremi di ogni tratto:
// tratti consecutivi hanno lo stesso punto omogeneo) ed elevati al grado
// `degree` se e' maggiore del loro (stessa geometria).
std::vector<BSplineCurve<3>> standardBezierPieces(const Curve<3> &curve, const Interval &range, int degree = 0);

// Tratti di Bezier dello stesso grado, consecutivi, uniti in una B-spline
// (nodi interni di molteplicita' pari al grado); il tratto k occupa
// [breaks[k], breaks[k + 1]] (di default 0, 1, 2, ...).
BSplineCurve<3> joinBezierPieces(const std::vector<BSplineCurve<3>> &pieces, const std::vector<double> &breaks = {});

// Superficie NURBS esatta sulla finestra [u] x [v] dei parametri (finita
// nelle direzioni non periodiche; nelle periodiche si usa un periodo intero
// se la finestra lo supera): piani, cilindri, coni, sfere, tori, estrusioni,
// rivoluzioni (anche con meridiano sghembo) e B-spline (restituita intera).
BSplineSurface toBSplineSurface(const Surface &surface, const Interval &u, const Interval &v);

}

#endif
