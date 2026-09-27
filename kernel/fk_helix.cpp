#include "fk_helix.h"

#include <cmath>
#include <stdexcept>

#include "fk_curve_algo.h"
#include "fk_hermite.h"

namespace ForgeCad::Kernel {

HelixCurve::HelixCurve(const HelixSpec &spec) : spec_(spec) {
    if (!(spec.turns > 0.0) || !std::isfinite(spec.turns)) throw std::invalid_argument("elica: il numero di giri deve essere positivo");
    if (!(spec.pitch > 0.0) || !std::isfinite(spec.pitch)) throw std::invalid_argument("elica: il passo deve essere positivo");
    if (!(spec.radius >= 0.0) || !std::isfinite(spec.radius)) throw std::invalid_argument("elica: raggio non valido");
    if (!(std::fabs(spec.taper) < kHalfPi - 1e-9)) throw std::invalid_argument("elica: angolo di conicita' non valido");
    sense_ = spec.leftHanded ? -1.0 : 1.0;
    if (spec.spiral) {
        slope_ = spec.pitch / kTwoPi;
    } else {
        rise_ = spec.pitch / kTwoPi;
        slope_ = rise_ * std::tan(spec.taper);
    }
    const double end = radiusAt(domain().hi);
    if (end < -1e-12 * std::max(1.0, spec.radius)) throw std::invalid_argument("elica: il raggio si annulla prima della fine (conicita' troppo forte)");
    if (spec.radius <= 0.0 && slope_ <= 0.0) throw std::invalid_argument("elica: raggio nullo");
}

void HelixCurve::evaluate(double t, int order, Vec3 *out) const {
    const Frame3 &f = spec_.frame;
    const double theta = spec_.startAngle + sense_ * t;
    const double r = radiusAt(t);
    // E_n = d^n/dt^n (cos theta X + sin theta Y) = s^n (cos(theta + n pi/2) X + sin(theta + n pi/2) Y).
    const auto radial = [&](int n) {
        const double angle = theta + n * kHalfPi;
        const double scale = (n % 2 == 1) ? sense_ : 1.0;
        return scale * (std::cos(angle) * f.xDir() + std::sin(angle) * f.yDir());
    };
    Vec3 previous = radial(0);
    out[0] = f.origin() + r * previous + (rise_ * t) * f.zDir();
    for (int n = 1; n <= order; ++n) {
        const Vec3 current = radial(n);
        out[n] = r * current + (n * slope_) * previous;
        if (n == 1) out[n] += rise_ * f.zDir();
        previous = current;
    }
}

double HelixCurve::length() const {
    if (slope_ == 0.0) return spec_.turns * std::sqrt(std::pow(kTwoPi * spec_.radius, 2) + spec_.pitch * spec_.pitch);
    return arcLength(*this, domain(), 1e-13);
}

BSplineCurve<3> helixBSpline(const HelixCurve &helix, double tolerance) {
    const Interval d = helix.domain();
    const detail::RowSpline spline = detail::fitQuinticRows(
        [&](double t, bool, Vec3 *out) { helix.evaluate(t, 2, out); }, 1, {d.lo, d.hi}, tolerance, kHalfPi);
    return detail::rowCurve(spline, 0);
}

}
