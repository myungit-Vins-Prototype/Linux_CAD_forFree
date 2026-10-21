#ifndef FORGECAD_CAD_CUDA_TESSELLATION_H
#define FORGECAD_CAD_CUDA_TESSELLATION_H

#include <cstdint>
#include <memory>

#include "fk_surface_batch.h"

// Tassellazione di display accelerata con CUDA: le superfici B-spline/NURBS
// dei punti nuovi del raffinamento (fk_tessellate) si valutano a lotti sulla
// GPU, il resto sulla CPU. Solo per disegnare: niente di esatto passa di qui.
namespace ForgeCad {

// GPU con CUDA presente e funzionante (compilato con nvcc e driver attivo).
bool cudaTessellationAvailable();
// Interruttore dell'utente (Opzioni -> Tassellazione con CUDA, view/cudaTessellation).
void setCudaTessellationEnabled(bool enabled);
bool cudaTessellationEnabled();

// L'acceleratore per una tassellazione (le copie delle superfici sulla GPU
// valgono finche' vive), o nullptr se la GPU non c'e' o e' spenta.
std::unique_ptr<Kernel::SurfaceBatchEvaluator> makeTessellationAccelerator();

// Punti valutati sulla GPU dall'avvio (per FORGECAD_PROFILE e per i test).
std::uint64_t cudaTessellationPoints();

}

#endif
