#pragma once
#include "fk_surface_mapping.h"
#include <optional>
namespace ForgeCad::Kernel::detail {
// A supported flat source is transported onto the ORIGINAL target carrier.
// Empty means the family is unsupported; a recognized unresolved bound throws.
// Line/polynomial-degree-one source edges support affine isoparametric NURBS
// images and general affine Extrusion images. This certifies position only,
// not injectivity or regularity. Complete edge/trim bridges precede fitting.
std::optional<Body> wrapPlanarBodyOnCarrier(const Body &source, const Surface &target,
                                          const SurfaceWrapOptions &options, double scaleU, double scaleV,
                                          BodyHistory *history, double *deviation);
}
