#pragma once
#include "fk_bspline_surface.h"
#include <functional>
namespace ForgeCad::Kernel::detail {
struct BlendCut {
    CurvePtr<3> curve;
    Interval range;
    bool forward = true; // dal punto `from` a `to` il parametro cresce
    double gap = 0.0;
    Interval sV{0.0, 0.0}; // traceBetween: valori di v su S toccati dai nodi
};
struct BlendContactJet {
    Vec3 point, derivative;
};
using BlendContactFunction = std::function<BlendContactJet(double)>;
// Newton smorzato sui contatti: non cambia il residuo ammesso dal chiamante.
void meetBlendContacts(const BlendContactFunction &a, const BlendContactFunction &b, double size, double scale, double &ta, double &tb);
std::shared_ptr<const BSplineSurface> orientBlendMitre(const std::shared_ptr<const BSplineSurface> &surface, bool reverse);
BlendCut traceBlendMitre(const Surface &s, const Surface &t, double t0, double s0, const double endpoint[4], double scale,
                         double fitTolerance);
} // namespace ForgeCad::Kernel::detail
