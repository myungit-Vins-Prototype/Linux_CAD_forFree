#include "fk_ruled_wall.h"

#include "fk_bspline_surface.h"

#include <algorithm>
#include <stdexcept>

namespace ForgeCad::Kernel::detail {

SurfacePtr cubicRuledWallSurface(BSplineCurve<3> &lower, BSplineCurve<3> &upper) {
    const Interval range = lower.domain();
    auto knots = lower.breakpoints(range);
    const auto other = upper.breakpoints(range);
    knots.insert(knots.end(), other.begin(), other.end());
    std::sort(knots.begin(), knots.end());
    knots.erase(std::unique(knots.begin(), knots.end()), knots.end());
    for (double t : knots) {
        if (t <= range.lo || t >= range.hi) continue;
        const int lowerCount = lower.multiplicity(t);
        const int upperCount = upper.multiplicity(t);
        if (lowerCount < upperCount) lower = lower.insertKnot(t, upperCount - lowerCount);
        if (upperCount < lowerCount) upper = upper.insertKnot(t, lowerCount - upperCount);
    }
    if (lower.knots() != upper.knots())
        throw std::logic_error("spessore: basi delle pareti incompatibili");

    const int count = lower.poleCount();
    std::vector<Vec3> poles;
    poles.reserve(std::size_t(count) * 2);
    for (int i = 0; i < count; ++i) {
        poles.push_back(lower.poles()[std::size_t(i)]);
        poles.push_back(upper.poles()[std::size_t(i)]);
    }
    return std::make_shared<BSplineSurface>(3, 1, lower.knots(),
        std::vector<double>{0., 0., 1., 1.}, count, 2, std::move(poles));
}

}
