#ifndef FORGECAD_FK_BOOLEAN_BOUNDARY_H
#define FORGECAD_FK_BOOLEAN_BOUNDARY_H
#include "fk_surface.h"
namespace ForgeCad::Kernel::detail {
// Certificato continuo della corrispondenza monotona ellisse/retta finita e
// dello scarto da entrambi i carrier originali. False include i casi non
// supportati, non finiti e non risolti; non autorizza alcun ripiego campionato.
bool certifyEllipseBoundarySegment(const Ellipse<3> &ellipse, Interval range, bool forward, const Line<3> &line,
                                   double first, double last, const Surface &firstSurface, const Surface &secondSurface,
                                   double budget);
// Immagine UV affine nel parametro ORIGINALE dell'ellisse. Restituisce un
// limite continuo, oppure infinito se dati/famiglia/budget non sono risolti.
double ellipseAffinePCurveError(const Ellipse<3> &ellipse, Interval range, const Surface &surface,
                                const Curve<2> &pcurve);
} // namespace ForgeCad::Kernel::detail
#endif
