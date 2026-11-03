#ifndef FORGECAD_FK_LOFT_GUIDE_CONSTRAINTS_H
#define FORGECAD_FK_LOFT_GUIDE_CONSTRAINTS_H

#include "fk_bspline_surface.h"
#include "fk_loft.h"

namespace ForgeCad::Kernel::detail {
// Impone il jet trasversale sulle due guide di bordo della pezza. Le curve
// guida e tutte le isoparametriche di sezione restano invariate. Condizioni
// incompatibili o fit non verificati vengono rifiutati, senza muovere i bordi.
BSplineSurfacePtr constrainLoftGuides(BSplineSurfacePtr surface,
    const LoftGuideCondition *left, const LoftGuideCondition *right,
    const std::vector<double> &sections, double scale, int startOrder = 0, int endOrder = 0);
}
#endif
