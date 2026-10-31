#include "cad_fillet_paths.h"
#include "cad_topology_ref.h"
#include "fk_surface_algo.h"
#include <algorithm>
#include <map>
namespace ForgeCad {
QVector<FilletPathGeometry>
filletPathGeometry(const ForgeBody &body, const QVector<EdgePoint> &references,
                   QString *error) {
  QVector<FilletPathGeometry> result;
  if (error)
    error->clear();
  if (!body)
    return result;
  try {
    std::vector<Kernel::EdgeId> selected;
    std::map<int, int> indices;
    for (int i = 0; i < references.size(); ++i) {
      if (references[i].role != kEdgePointEdge)
        throw std::domain_error(
            "Nel modo Variabile seleziona singoli bordi nella vista.");
      const auto edge = resolveEdgeReference(*body, references[i], 1e-4);
      if (!edge.valid())
        throw std::domain_error(
            "Uno dei bordi non e piu disponibile: selezionalo di nuovo.");
      selected.push_back(edge);
      indices.emplace(edge.index, i);
    }
    for (auto &path : Kernel::blendPaths(*body, selected)) {
      FilletPathGeometry geometry;
      for (const auto &piece : path.edges)
        geometry.edges.append(indices.at(piece.edge.index));
      geometry.path = std::move(path);
      result.append(std::move(geometry));
    }
  } catch (const std::exception &e) {
    if (error)
      *error = QString::fromUtf8(e.what());
    result.clear();
  }
  return result;
}
QVector<FilletRadiusHandle> filletStations(const ForgeBody &body,
                                           const FilletPathGeometry &geometry,
                                           const FilletPathSpec &parameters,
                                           int pathIndex) {
  QVector<FilletRadiusHandle> handles;
  if (!body || parameters.radii.size() < 2)
    return handles;
  const auto display = [](const Kernel::Vec3 &v) {
    return QVector3D(v.x(), v.y(), v.z());
  };
  for (int i = 0; i < parameters.radii.size(); ++i) {
    if (geometry.path.closed && i + 1 == parameters.radii.size())
      continue;
    const double fraction = double(i) / (parameters.radii.size() - 1);
    const auto station = Kernel::blendPathPoint(
        *body, geometry.path, parameters.reverse ? 1.0 - fraction : fraction);
    const auto &edge = body->edge(station.edge);
    Kernel::Vec3 direction;
    for (auto fin : {edge.forward, edge.backward}) {
      const auto &face = body->face(body->finFace(fin));
      const auto &use = body->fin(fin);
      Kernel::Vec2 uv;
      if (use.pcurve)
        uv = use.pcurve->point(station.parameter);
      if (!use.pcurve || Kernel::distance(face.surface->point(uv.x(), uv.y()),
                                          station.point) > 1e-6) {
        const auto projected =
            Kernel::projectPoint(*face.surface, station.point);
        uv = {projected.u, projected.v};
      }
      direction += (face.sense ? -1.0 : 1.0) *
                   Kernel::normalAt(*face.surface, uv.x(), uv.y());
    }
    direction -= Kernel::dot(direction, station.tangent) * station.tangent;
    if (Kernel::norm(direction) < 1e-10)
      direction = Kernel::cross(station.tangent, Kernel::Vec3(0, 0, 1));
    if (Kernel::norm(direction) < 1e-10)
      direction = Kernel::cross(station.tangent, Kernel::Vec3(0, 1, 0));
    handles.append({display(station.point),
                    display(Kernel::normalized(direction)), parameters.radii[i],
                    pathIndex, i});
  }
  return handles;
}
} // namespace ForgeCad
