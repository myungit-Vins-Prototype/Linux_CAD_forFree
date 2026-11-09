#pragma once
#include "fk_bspline_surface.h"

namespace ForgeCad::Kernel {
struct SurfaceRefitResult {
    BSplineSurfacePtr surface;
    double sampledDeviation = 0;
};
// Ricostruzione bicubica ai minimi quadrati con numero esplicito di poli.
// Mantiene dominio UV e quattro angoli; lo scarto e' campionato anche ai
// nodi e nei loro intervalli, e non costituisce un limite globale certificato.
SurfaceRefitResult refitSurfacePoles(const BSplineSurface &source, int uCount, int vCount);
} // namespace ForgeCad::Kernel
