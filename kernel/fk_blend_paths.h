#ifndef FORGECAD_FK_BLEND_PATHS_H
#define FORGECAD_FK_BLEND_PATHS_H
#include "fk_topology.h"
#include <vector>
namespace ForgeCad::Kernel {
// Percorsi deterministici dei bordi selezionati: si uniscono soltanto gli
// estremi topologicamente comuni con tangenti opposte e continuazione univoca.
struct BlendPathEdge {
  EdgeId edge;
  bool reversed = false;
  double offset = 0.0, length = 0.0;
};
struct BlendPath {
  std::vector<BlendPathEdge> edges;
  double length = 0.0;
  bool closed = false;
};
std::vector<BlendPath> blendPaths(const Body &body,
                                  const std::vector<EdgeId> &selected);
struct BlendPathPoint {
  EdgeId edge;
  double parameter = 0.0;
  Vec3 point, tangent;
};
BlendPathPoint blendPathPoint(const Body &body, const BlendPath &path,
                              double fraction);
// Una legge di raggio per percorso; gli indici sono quelli della base
// originale.
struct BlendRadiusPath {
  std::vector<EdgeId> edges;
  std::vector<double> radii;
  bool smooth = true, reverse = false;
};
} // namespace ForgeCad::Kernel
#endif
