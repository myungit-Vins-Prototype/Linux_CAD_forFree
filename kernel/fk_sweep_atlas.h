#ifndef FORGECAD_FK_SWEEP_ATLAS_H
#define FORGECAD_FK_SWEEP_ATLAS_H
#include "fk_sweep_caps.h"
#include "fk_sweep_surface_validation.h"
namespace ForgeCad::Kernel::detail {
struct SweepCap {
    FaceId face;
    std::vector<int> families;
    double boundaryV = 0;
};
void requireDisjointSweepCaps(const Body &body, const std::vector<SweepSurfaceChart> &charts,
                              const std::vector<SweepCap> &caps);
std::shared_ptr<const BSplineSurface> translationChart(const Curve<3> &section, const Interval &range,
                                                       const Vec3 &displacement);
std::shared_ptr<const BSplineSurface> rotationChart(const Curve<3> &section, const Interval &range, const Vec3 &center,
                                                    const Vec3 &axis, double angle);
void requireProfileNesting(const std::vector<ProfileLoop> &loops, const std::vector<int> &regions, const Frame3 &frame);
} // namespace ForgeCad::Kernel::detail
#endif
