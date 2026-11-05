#pragma once
#include "cad_types.h"
namespace ForgeCad {
void buildSurfaceMapping(ExtrusionObject &feature, int owner, const QVector<SketchObject> &sketches,
                         const QVector<ExtrusionObject> &bodies);
}
