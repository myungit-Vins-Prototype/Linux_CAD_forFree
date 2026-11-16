#pragma once
#include "cad_types.h"
#include "fk_topology.h"
namespace ForgeCad {
// Mode0 estensione ai surfaceTool.targets (extentRef per i file precedenti),
// mode1 rete ruledFirst/Second,
// mode2 poli relativi di facce NURBS. I riferimenti restano quelli standard.
void buildSurfaceTool(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies);
// Risoluzione dei poli per nome univoco o identita esplicita nello stesso stadio;
// mai proiezioni o riassegnazioni alla faccia piu vicina.
Kernel::FaceId resolveSurfacePoleFace(const Kernel::Body &body, const EdgePoint &reference);
QByteArray surfaceBasisHash(const ForgeBody &body);
QByteArray surfaceBasisHash(const ForgeBody &body, Kernel::FaceId face);
QByteArray surfaceToolsExtras(const QVector<ExtrusionObject> &bodies);
QString applySurfaceToolsExtras(const QByteArray &bytes, QVector<ExtrusionObject> &bodies);
} // namespace ForgeCad
