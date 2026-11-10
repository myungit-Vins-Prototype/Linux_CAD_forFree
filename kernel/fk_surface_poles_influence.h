#pragma once
#include "fk_bspline_surface.h"
#include <array>
namespace ForgeCad::Kernel {
// Influenza sulla topologia della griglia, senza ricerca spaziale fra facce.
// Ogni raggio conta le fasce di poli vicini; il peso si annulla dopo l'ultima
// fascia con raccordo C2. I comandi hanno peso1, le sovrapposizioni usano il
// massimo, mai una somma. Raggio0/0: soli comandi. Non e' un vincolo G1.
struct SurfacePoleInfluence { int u = 0, v = 0; double weight = 0; };
std::vector<SurfacePoleInfluence> surfacePoleInfluence(const BSplineSurface &surface,
    const std::vector<std::array<int,2>> &drivers, int radiusU, int radiusV);
} // namespace ForgeCad::Kernel
