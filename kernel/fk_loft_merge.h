#pragma once
#include "fk_history.h"
#include "fk_topology.h"
namespace ForgeCad::Kernel::detail {
// Concatena esattamente pezze NURBS contigue della loft con giunto G1
// certificato sui polinomi di bordo. Conserva alias e contorni esterni.
void mergeLoftTangentFaces(std::vector<Vec3> &vertices, std::vector<Body::BuildEdge> &edges,
                          std::vector<Body::BuildFace> &faces, FaceAliases *labels);
}
