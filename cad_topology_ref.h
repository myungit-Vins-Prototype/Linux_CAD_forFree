#ifndef FORGECAD_TOPOLOGY_REF_H
#define FORGECAD_TOPOLOGY_REF_H

#include "cad_types.h"
#include <vector>

#include "fk_topology.h"

namespace ForgeCad {

EdgePoint edgeReference(const Kernel::Body &body, Kernel::EdgeId edge, const Kernel::Vec3 &point);
EdgePoint faceReference(const Kernel::Body &body, Kernel::FaceId face, const Kernel::Vec3 &point);
EdgePoint vertexReference(const Kernel::Body &body, Kernel::VertexId vertex);

// Bordo libero di una lamina che contiene `edge` (edge con una sola fin): la
// catena degli edge di bordo collegati nei vertici in cui se ne incontrano due
// soli, chiusa o fino ai vertici ambigui. Riferimenti con il punto medio di
// ogni edge, nell'ordine della catena. Vuoto se `edge` non e' di bordo.
QVector<EdgePoint> freeBoundaryLoop(const Kernel::Body &body, Kernel::EdgeId edge);

// Lo stato del corpo in cui si cerca rispetto a quello in cui il riferimento
// e' stato preso: lo stesso (anche rigenerato con altri parametri: l'ID vale
// piu' del punto) o diverso / non noto (un riordino della storia: l'ID vale
// solo se contiene il punto, altrimenti si cerca per geometria).
enum class ReferenceState { Same, Other };

Kernel::EdgeId resolveEdgeReference(const Kernel::Body &body, const EdgePoint &reference, double legacyTolerance,
                                    ReferenceState state = ReferenceState::Same);
Kernel::FaceId resolveFaceReference(const Kernel::Body &body, const EdgePoint &reference, double legacyTolerance,
                                    ReferenceState state = ReferenceState::Same);
Kernel::VertexId resolveVertexReference(const Kernel::Body &body, const EdgePoint &reference, double legacyTolerance,
                                        ReferenceState state = ReferenceState::Same);

// Gli edge di tutti i loop della faccia, senza ripetizioni.
std::vector<Kernel::EdgeId> faceBoundaryEdges(const Kernel::Body &body, Kernel::FaceId face);
// Spigoli dei raccordi e degli smussi: i riferimenti di spigolo e, per quelli
// di faccia (role = kEdgePointFaceBoundary), tutti i bordi della faccia nello
// stato del corpo dato. Falso se un riferimento non si ritrova.
bool resolveBlendEdges(const Kernel::Body &body, const QVector<EdgePoint> &references, double legacyTolerance,
                       std::vector<Kernel::EdgeId> &edges, ReferenceState state = ReferenceState::Same);

// Completa i riferimenti dei documenti precedenti usando i B-rep gia'
// rigenerati. Non modifica la geometria e puo' essere chiamata prima del save.
void upgradeTopologyReferences(QVector<ExtrusionObject> &features);

}

#endif
