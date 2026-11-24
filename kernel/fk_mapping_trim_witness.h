#pragma once
#include "fk_surface_mapping.h"
#include <functional>

namespace ForgeCad::Kernel::detail {
bool isStraightMappingEdge(const Curve<3> &curve);
enum class MappingTrimCoverage { Inconclusive, Covered, Uncovered };
// Continuous inclusion of closed planar trims in Cartesian projection charts:
// orthogonal arrangements, or convex polygonal or full circular outer loops
// with optional circular holes and unions of convex pieces. Native planar
// B-spline trims can supply a source enclosure or an inscribed support
// region. Bounds and a certified winding test handle spline openings.
// Matching spline seams can cancel in the union of two supports, including
// exact subintervals inside Bezier spans. Certified triangles can also
// cover reentrant residual perimeters.
// A gap needs a witness beyond the boundary tolerance.
MappingTrimCoverage certifyMappingTrimCoverage(const Body &source, const std::vector<SurfaceTargetFace> &targets,
                                               const Frame3 &sourceChart, const Frame3 &targetChart,
                                               double boundaryTolerance = 0., Vec2 *gapCenter = nullptr);

// The two charts express the same support XY on source and target. Probe
// source material over inner holes and between the projected outer trims of
// selected planar or curved patches.
void checkMappingTrimCoverage(const Body &source, const std::vector<SurfaceTargetFace> &targets,
                              const Frame3 &sourceChart, const Frame3 &targetChart, double precision,
                              const std::function<void(const Vec3 &)> &map);
// Measure the fit at a bounded set of interior knot-span witnesses.
double measureMappingKnotResidual(const Body &source, FaceId face, const std::vector<SurfaceTargetFace> &targets,
                                  const Frame3 &sourceChart, const Frame3 &targetChart, const Surface &image,
                                  Interval u, Interval v, double precision,
                                  const std::function<Vec3(const Vec3 &)> &map);
}
