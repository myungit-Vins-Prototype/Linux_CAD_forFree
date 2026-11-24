#ifndef FK_RULED_WALL_H
#define FK_RULED_WALL_H

#include "fk_bspline.h"
#include "fk_surface.h"

namespace ForgeCad::Kernel::detail {

// Le due guide sono cubiche, polinomiali e clamped sullo stesso dominio.
// L'inserimento dei nodi allinea le basi conservando la geometria delle guide.
// I chiamanti usano le guide raffinate per i bordi delle pareti e gestiscono le curve UV.
SurfacePtr cubicRuledWallSurface(BSplineCurve<3> &lower, BSplineCurve<3> &upper);

}

#endif
