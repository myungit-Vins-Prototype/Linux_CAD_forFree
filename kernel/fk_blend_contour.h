#pragma once
#include "fk_topology.h"
namespace ForgeCad::Kernel {
// Contorno chiuso di una cavita' estrusa: separa i fianchi paralleli al
// generatore dagli appoggi esterni. Solo adiacenze topologiche, senza snap.
// Vuoto se manca un contorno chiuso univoco attraverso seed.
std::vector<EdgeId> blendCavityContour(const Body &body, EdgeId seed);
// Verifica geometrica dei contorni non periodici rifilati: un appoggio
// consumato non puo' restare nel risultato come una faccia invertita.
void validateBlendTrimOrientation(const Body &body, const std::vector<FaceId> &faces);
}
