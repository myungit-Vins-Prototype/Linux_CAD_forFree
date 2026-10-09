#ifndef FORGECAD_FK_DRAFT_H
#define FORGECAD_FK_DRAFT_H
#include "fk_topology.h"

namespace ForgeCad::Kernel {
// Sformo a piano neutro: angolo in radianti rispetto alla direzione di
// estrazione (normale al piano). Positivo: restringe nella direzione data.
// Solidi convessi poliedrici, senza cambiamenti di topologia. Mantiene gli ID.
// Rifiuta facce parallele al piano neutro, collassi e casi non supportati.
Body draftFaces(const Body &body, const std::vector<FaceId> &faces,
                const Vec3 &neutralPoint, const Vec3 &pullDirection, double angle);
}
#endif
