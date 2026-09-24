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

// Superficie NURBS esatta sulla finestra [u] x [v] dei parametri (finita
// nelle direzioni non periodiche; nelle periodiche si usa un periodo intero
// se la finestra lo supera): piani, cilindri, coni, sfere, tori, estrusioni,
// rivoluzioni (anche con meridiano sghembo) e B-spline (restituita intera).
BSplineSurface toBSplineSurface(const Surface &surface, const Interval &u, const Interval &v);

}

#endif
