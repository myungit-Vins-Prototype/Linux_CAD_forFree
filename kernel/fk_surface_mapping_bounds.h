#pragma once
#include "fk_surface_mapping.h"
#include <limits>
namespace ForgeCad::Kernel::detail {
inline constexpr double wrapTargetParameterSlack = 1e-12;
enum class SurfaceMappingBoundStatus { Unsupported, Certified, Failed };
struct SurfaceMappingBoundResult {
    SurfaceMappingBoundStatus status = SurfaceMappingBoundStatus::Unsupported;
    double upperBound = std::numeric_limits<double>::infinity();
};
// Continuous position bound over the WHOLE source UV rectangle. Recognizes
// Plane source with affine height and affine target UV. Supports separable
// layouts, both coordinates varying along one source axis, and oblique layouts
// including a zero determinant or an interval containing zero.
// Requires a native nonperiodic polynomial bicubic image. Targets may be
// native or instanced B-splines with positive weights, or extrusions
// of positive-weight B-splines, circles and ellipses, including nested affine
// placements and trims. Only the generating curve follows its placements.
// V is unbounded. Conics keep their angle; physically closed trims repeat
// on the exposed interval. Cylindrical surfaces use their native angle in U.
// Revolutions of lines, conics and B-spline meridians use native angle U and
// curve parameter V. Native cones, spheres and tori keep their signed radius.
// Periodic coordinates include applicable knot spans on both sides of a seam.
// Target degrees and composed Bernstein products have finite work limits. For rational
// targets each span must prove a positive homogeneous denominator over the
// evaluation rectangle. Natural derivatives bound the direction of the offset.
// Unsupported is a family boundary. Failed is terminal: invalid input, domain,
// arithmetic or fixed work limit, or inability to prove the externally set cap.
// Nonzero height requires a proved regular target normal. An oriented
// residual bound covers affine heights that touch or cross zero without
// dividing by the height. No injectivity or topology certificate.
SurfaceMappingBoundResult planarMappingResidualBound(
    const Surface &source, const Surface &target, const SurfaceWrapOptions &options,
    double scaleU, double scaleV, const Surface &image, Interval u, Interval v, double budget);
// Continuous signed normal-offset residual along one UV trace. Native
// polynomial B-splines up to bicubic on the source, native polynomial
// bicubic image, and a line or UV spline of degree at most 3 with positive
// weights. Rational traces retain their homogeneous denominator.
// Positive-width knot spans, bounded work and all applicable closed cells.
// Both surfaces are composed with the same trace parameter.
SurfaceMappingBoundResult offsetTraceResidualBound(
    const Surface &source, const Surface &image, const Curve<2> &pcurve,
    Interval range, double distance, double budget);
// Continuous residual where contact exists on the selected spline graphs.
// Recognizes affine charts and regular nonlinear charts whose footprints
// have disjoint interiors or coincident contacts. Clamped rational charts
// may have several continuous knot spans. A polynomial Bézier source graph
// can project obliquely on affine target charts. On nonlinear charts its
// projection may use an exact shear or a bounded inverse for a contractive
// total-quadratic height. Trim coverage is checked separately.
SurfaceMappingBoundResult projectedMappingResidualBound(
    const Body &sourceBody, FaceId sourceFace, const std::vector<SurfaceTargetFace> &targets,
    const SurfaceWrapOptions &options, const Frame3 &targetChart,
    const Surface &image, Interval u, Interval v, double budget);
}
