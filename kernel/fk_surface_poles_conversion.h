#pragma once
#include "fk_bspline_surface.h"
#include "fk_topology.h"
namespace ForgeCad::Kernel {
// Griglia esatta del supporto sulla finestra finita della faccia. I piani
// e le direzioni lineari hanno almeno quattro poli, senza approssimazione.
BSplineSurfacePtr surfacePoleSupport(const Body &body, FaceId face);
// Converte solo le facce richieste, conservando identita, geometria 3D dei
// bordi e vertici. Aggiorna le SP-curve quando cambia il parametro NURBS.
Body prepareSurfacePoleBody(const Body &body, const std::vector<FaceId> &faces, double tolerance);
// Mantiene le chiusure e i lati degeneri gia presenti nel supporto neutro.
void checkSurfacePoleClosure(const BSplineSurface &original, const BSplineSurface &modified, double tolerance);
} // namespace ForgeCad::Kernel
