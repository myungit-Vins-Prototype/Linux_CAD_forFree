#ifndef FORGECAD_FK_FACE_NORMAL_BOUNDS_H
#define FORGECAD_FK_FACE_NORMAL_BOUNDS_H

#include "fk_topology.h"

namespace ForgeCad::Kernel {

// Certificato conservativo del segno della normale orientata di una faccia:
// dot(n, direction) >= 0, entro angularResolution(). Gestisce superfici
// B-spline polinomiali (anche pesi tutti uguali) ed estrusioni di curve
// polinomiali. Le estrusioni di qualsiasi curva sono certificate quando
// direction e' parallela alla generatrice (proiezione identicamente nulla).
// Include Face::sense; direction deve essere finita e non nulla.
// I contorni limitano le pezze solo tramite hull delle SP-curve ritagliate
// esattamente al range degli edge, con orientamento bounded certificato.
// Informazioni mancanti/ambigue usano il supporto intero; tipi o pesi
// razionali variabili non certificabili restituiscono false. Nessun campione.
// La cancellazione dell'operazione resta propagata al chiamante.
bool faceHasNonnegativeNormalProjection(const Body &body, FaceId face, const Vec3 &direction);

}

#endif
