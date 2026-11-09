#pragma once
#include "fk_bspline_surface.h"

namespace ForgeCad::Kernel {
inline constexpr int surfaceRefitMaxPoles = 256;
struct SurfaceRefitResult {
    BSplineSurfacePtr surface;
    double sampledDeviation = 0;
};
// Ricostruzione bicubica ai minimi quadrati con numero esplicito di poli.
// Mantiene dominio UV e quattro angoli; lo scarto e' campionato anche ai
// nodi e nei loro intervalli, e non costituisce un limite globale certificato.
SurfaceRefitResult refitSurfacePoles(const BSplineSurface &source, int uCount, int vCount);
double sampledSurfacePoleDeviation(const BSplineSurface &source,const BSplineSurface &surface);
struct SurfaceRefitGrid {
    int u = 0, v = 0;
    double sampledDeviation = 0;
    std::vector<SurfaceRefitResult> faces;
};
// Aumenta U o V scegliendo a ogni passo la riduzione dello scarto peggiore
// fra le facce. Restituisce la migliore griglia trovata entro il limite di poli;
// il chiamante deve comunque verificare sampledDeviation <= tolerance.
SurfaceRefitGrid refitSurfacePolesWithinTolerance(const std::vector<BSplineSurfacePtr> &sources, int u, int v,
                                                  double tolerance);
} // namespace ForgeCad::Kernel
