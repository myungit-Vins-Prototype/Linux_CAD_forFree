#pragma once
#include "cad_types.h"
namespace ForgeCad {
// Mode0 estensione al riferimento extentRef, mode1 rete ruledFirst/Second,
// mode2 poli relativi di una faccia NURBS. I riferimenti restano quelli standard.
void buildSurfaceTool(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies);
QByteArray surfaceBasisHash(const ForgeBody &body);
QByteArray surfaceToolsExtras(const QVector<ExtrusionObject> &bodies);
QString applySurfaceToolsExtras(const QByteArray &bytes, QVector<ExtrusionObject> &bodies);
} // namespace ForgeCad
