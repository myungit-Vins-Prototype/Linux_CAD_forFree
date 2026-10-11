#ifndef FORGECAD_SPATIAL_SKETCH_H
#define FORGECAD_SPATIAL_SKETCH_H
#include "cad_types.h"
namespace ForgeCad {
// Marcatura degli ID delle entita' spaziali nei riferimenti curva esistenti.
inline constexpr int kSpatialCurveReference = 0x533344;
QVector<ForgeCurve> buildSpatialSketch(const SpatialSketchParameters &sketch, QString *error);
}
#endif
