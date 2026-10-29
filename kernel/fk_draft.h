#ifndef FORGECAD_FK_DRAFT_H
#define FORGECAD_FK_DRAFT_H
#include "fk_topology.h"

namespace ForgeCad::Kernel {
class BodyHistory;
// Sformo a piano neutro: angolo in radianti rispetto alla direzione di
// estrazione (normale al piano). Positivo: restringe nella direzione data.
// Facce piane su solidi anche concavi con supporti curvi; cilindri/coni
// coassiali all'estrazione. Rifilatura dei supporti mantenendo le adiacenze.
// Percorso esatto con ID conservati per poliedri convessi e lamine piane;
// sui solidi misti la genealogia registra gli ID ricostruiti.
// Rifiuta facce parallele al piano neutro, collassi e casi non supportati.
Body draftFaces(const Body &body, const std::vector<FaceId> &faces,
                const Vec3 &neutralPoint, const Vec3 &pullDirection, double angle, BodyHistory *history = nullptr);
}
#endif
