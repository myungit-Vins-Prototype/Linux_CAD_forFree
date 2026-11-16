#ifndef FORGECAD_FK_SWEEP_ANALYTIC_BRIDGE_H
#define FORGECAD_FK_SWEEP_ANALYTIC_BRIDGE_H
#include "fk_bspline_surface.h"
namespace ForgeCad::Kernel::detail {
struct SweepAnalyticBridge {
    bool bounded = false;
    double poleError = 0, weightError = 0;
};
// Ideal Line/Circle/Ellipse section motion, in the chart's common rational
// parameters. Unsupported inputs return bounded=false (use chart as carrier).
SweepAnalyticBridge boundSweepAnalyticBridge(const Curve<3> &section, Interval range, const BSplineSurface &chart,
                                             const Vec3 &displacement, bool rotation = false, const Vec3 &center = {},
                                             const Vec3 &axis = {}, double angle = 0,
                                             const Surface *actualAnalytic = nullptr);
} // namespace ForgeCad::Kernel::detail
#endif
