#ifndef FORGECAD_FILLET_PATHS_H
#define FORGECAD_FILLET_PATHS_H
#include "cad_types.h"
#include "fk_blend_paths.h"
namespace ForgeCad {
struct FilletPathGeometry {
  QVector<int> edges;
  Kernel::BlendPath path;
};
// Le voci restano associate agli indici dell'elenco selezione originale.
QVector<FilletPathGeometry> filletPathGeometry(const ForgeBody &body,
                                               const QVector<EdgePoint> &edges,
                                               QString *error = nullptr);
QVector<FilletRadiusHandle> filletStations(const ForgeBody &body,
                                           const FilletPathGeometry &geometry,
                                           const FilletPathSpec &parameters,
                                           int pathIndex);
} // namespace ForgeCad
#endif
