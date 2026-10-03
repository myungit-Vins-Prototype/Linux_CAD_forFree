#ifndef FORGECAD_TOPOLOGY_REF_H
#define FORGECAD_TOPOLOGY_REF_H

#include "cad_types.h"
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

Kernel::EdgeId resolveEdgeReference(const Kernel::Body &body, const EdgePoint &reference, double legacyTolerance);
Kernel::FaceId resolveFaceReference(const Kernel::Body &body, const EdgePoint &reference, double legacyTolerance);
Kernel::VertexId resolveVertexReference(const Kernel::Body &body, const EdgePoint &reference, double legacyTolerance);

// Completa i riferimenti dei documenti precedenti usando i B-rep gia'
// rigenerati. Non modifica la geometria e puo' essere chiamata prima del save.
void upgradeTopologyReferences(QVector<ExtrusionObject> &features);

}

#endif
