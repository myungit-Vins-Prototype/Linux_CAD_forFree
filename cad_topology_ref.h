#ifndef FORGECAD_TOPOLOGY_REF_H
#define FORGECAD_TOPOLOGY_REF_H

#include "cad_types.h"
#include "fk_topology.h"

namespace ForgeCad {

EdgePoint edgeReference(const Kernel::Body &body, Kernel::EdgeId edge, const Kernel::Vec3 &point);
EdgePoint faceReference(const Kernel::Body &body, Kernel::FaceId face, const Kernel::Vec3 &point);
EdgePoint vertexReference(const Kernel::Body &body, Kernel::VertexId vertex);

Kernel::EdgeId resolveEdgeReference(const Kernel::Body &body, const EdgePoint &reference, double legacyTolerance);
Kernel::FaceId resolveFaceReference(const Kernel::Body &body, const EdgePoint &reference, double legacyTolerance);
Kernel::VertexId resolveVertexReference(const Kernel::Body &body, const EdgePoint &reference, double legacyTolerance);

// Completa i riferimenti dei documenti precedenti usando i B-rep gia'
// rigenerati. Non modifica la geometria e puo' essere chiamata prima del save.
void upgradeTopologyReferences(QVector<ExtrusionObject> &features);

}

#endif
