#include "fk_surface.h"

#include <algorithm>

#include "fk_precision.h"

namespace ForgeCad::Kernel {
namespace {

// Derivate k-esime di cos e sin, dati c = cos t e s = sin t.
double cosDerivative(int k, double c, double s) {
    switch (k % 4) {
    case 0: return c;
    case 1: return -s;
    case 2: return -c;
    default: return s;
    }
}

double sinDerivative(int k, double c, double s) {
    switch (k % 4) {
    case 0: return s;
    case 1: return c;
    case 2: return -s;
    default: return -c;
    }
}

// Derivata k-esima di E(u) = cos u X + sin u Y.
Vec3 radial(const Frame3 &frame, int k, double c, double s) {
    return cosDerivative(k, c, s) * frame.xDir() + sinDerivative(k, c, s) * frame.yDir();
}

Interval infiniteInterval() {
    const double infinity = std::numeric_limits<double>::infinity();
    return {-infinity, infinity};
}

void requireRadius(double radius, const char *message) {
    if (!(radius > kLinearResolution) || !std::isfinite(radius)) throw std::invalid_argument(message);
}

// Cerchio di raggio con segno: con raggio negativo si invertono gli assi,
// cosi' il parametro resta l'angolo u della superficie.
CurvePtr<3> signedCircle(const Vec3 &center, const Vec3 &x, const Vec3 &y, double radius) {
    if (std::fabs(radius) <= kLinearResolution) return nullptr;
    if (radius > 0.0) return std::make_shared<Circle<3>>(center, x, y, radius);
    return std::make_shared<Circle<3>>(center, -x, -y, -radius);
}

}

Vec3 Surface::point(double u, double v) const {
    Vec3 result;
    evaluate(u, v, 0, &result);
    return result;
}

Vec3 Surface::normal(double u, double v) const {
    Vec3 d[4];
    evaluate(u, v, 1, d);
    const Vec3 &su = d[derivativeIndex(1, 0, 1)];
    const Vec3 &sv = d[derivativeIndex(0, 1, 1)];
    const Vec3 n = cross(su, sv);
    const double length = norm(n);
    // Relativo alla derivata piu' grande: al polo della sfera Su -> 0.
    const double scale = std::max(norm(su), norm(sv));
    if (!(length > kAngularResolution * scale * scale))
        throw std::domain_error("Surface::normal: punto singolare");
    return n / length;
}

// --- Plane -----------------------------------------------------------------

Interval Plane::uDomain() const { return infiniteInterval(); }
Interval Plane::vDomain() const { return infiniteInterval(); }

void Plane::evaluate(double u, double v, int order, Vec3 *out) const {
    for (int i = 0; i < (order + 1) * (order + 1); ++i) out[i] = Vec3();
    out[0] = frame_.origin() + u * frame_.xDir() + v * frame_.yDir();
    if (order >= 1) {
        out[derivativeIndex(1, 0, order)] = frame_.xDir();
        out[derivativeIndex(0, 1, order)] = frame_.yDir();
    }
}

CurvePtr<3> Plane::uIso(double u) const {
    return std::make_shared<Line<3>>(frame_.origin() + u * frame_.xDir(), frame_.yDir());
}

CurvePtr<3> Plane::vIso(double v) const {
    return std::make_shared<Line<3>>(frame_.origin() + v * frame_.yDir(), frame_.xDir());
}

// --- CylindricalSurface ----------------------------------------------------

CylindricalSurface::CylindricalSurface(const Frame3 &frame, double radius) : frame_(frame), radius_(radius) {
    requireRadius(radius, "CylindricalSurface: raggio non valido");
}

Interval CylindricalSurface::vDomain() const { return infiniteInterval(); }

void CylindricalSurface::evaluate(double u, double v, int order, Vec3 *out) const {
    const double c = std::cos(u), s = std::sin(u);
    for (int k = 0; k <= order; ++k) {
        for (int l = 0; l <= order; ++l) {
            Vec3 value;
            if (l == 0) value = radius_ * radial(frame_, k, c, s);
            if (k == 0 && l == 0) value += frame_.origin() + v * frame_.zDir();
            if (k == 0 && l == 1) value = frame_.zDir();
            out[derivativeIndex(k, l, order)] = value;
        }
    }
}

CurvePtr<3> CylindricalSurface::uIso(double u) const {
    return std::make_shared<Line<3>>(frame_.origin() + radius_ * radial(frame_, 0, std::cos(u), std::sin(u)), frame_.zDir());
}

CurvePtr<3> CylindricalSurface::vIso(double v) const {
    return std::make_shared<Circle<3>>(frame_.origin() + v * frame_.zDir(), frame_.xDir(), frame_.yDir(), radius_);
}

// --- ConicalSurface --------------------------------------------------------

ConicalSurface::ConicalSurface(const Frame3 &frame, double semiAngle, double referenceRadius)
    : frame_(frame), semiAngle_(semiAngle), radius_(referenceRadius) {
    if (!(std::fabs(semiAngle) > kAngularResolution && std::fabs(semiAngle) < kHalfPi - kAngularResolution))
        throw std::invalid_argument("ConicalSurface: semiangolo fuori da (0, pi/2)");
    if (!(referenceRadius >= 0.0) || !std::isfinite(referenceRadius))
        throw std::invalid_argument("ConicalSurface: raggio negativo");
}

Interval ConicalSurface::vDomain() const { return infiniteInterval(); }

Vec3 ConicalSurface::apex() const {
    return frame_.origin() - (radius_ / std::tan(semiAngle_)) * frame_.zDir();
}

void ConicalSurface::evaluate(double u, double v, int order, Vec3 *out) const {
    const double c = std::cos(u), s = std::sin(u);
    const double sa = std::sin(semiAngle_), ca = std::cos(semiAngle_);
    for (int k = 0; k <= order; ++k) {
        for (int l = 0; l <= order; ++l) {
            const double factor = l == 0 ? radius_ + v * sa : (l == 1 ? sa : 0.0);
            Vec3 value = factor * radial(frame_, k, c, s);
            if (k == 0 && l == 0) value += frame_.origin() + v * ca * frame_.zDir();
            if (k == 0 && l == 1) value += ca * frame_.zDir();
            out[derivativeIndex(k, l, order)] = value;
        }
    }
}

CurvePtr<3> ConicalSurface::uIso(double u) const {
    const Vec3 e = radial(frame_, 0, std::cos(u), std::sin(u));
    return std::make_shared<Line<3>>(frame_.origin() + radius_ * e,
                                     std::sin(semiAngle_) * e + std::cos(semiAngle_) * frame_.zDir());
}

CurvePtr<3> ConicalSurface::vIso(double v) const {
    return signedCircle(frame_.origin() + v * std::cos(semiAngle_) * frame_.zDir(), frame_.xDir(), frame_.yDir(),
                        radius_ + v * std::sin(semiAngle_));
}

// --- SphericalSurface ------------------------------------------------------

SphericalSurface::SphericalSurface(const Frame3 &frame, double radius) : frame_(frame), radius_(radius) {
    requireRadius(radius, "SphericalSurface: raggio non valido");
}

void SphericalSurface::evaluate(double u, double v, int order, Vec3 *out) const {
    const double c = std::cos(u), s = std::sin(u);
    const double cv = std::cos(v), sv = std::sin(v);
    for (int k = 0; k <= order; ++k) {
        for (int l = 0; l <= order; ++l) {
            Vec3 value = radius_ * cosDerivative(l, cv, sv) * radial(frame_, k, c, s);
            if (k == 0) value += radius_ * sinDerivative(l, cv, sv) * frame_.zDir();
            if (k == 0 && l == 0) value += frame_.origin();
            out[derivativeIndex(k, l, order)] = value;
        }
    }
}

CurvePtr<3> SphericalSurface::uIso(double u) const {
    return std::make_shared<Circle<3>>(frame_.origin(), radial(frame_, 0, std::cos(u), std::sin(u)), frame_.zDir(), radius_);
}

CurvePtr<3> SphericalSurface::vIso(double v) const {
    return signedCircle(frame_.origin() + radius_ * std::sin(v) * frame_.zDir(), frame_.xDir(), frame_.yDir(),
                        radius_ * std::cos(v));
}

// --- ToroidalSurface -------------------------------------------------------

ToroidalSurface::ToroidalSurface(const Frame3 &frame, double majorRadius, double minorRadius)
    : frame_(frame), majorRadius_(majorRadius), minorRadius_(minorRadius) {
    requireRadius(majorRadius, "ToroidalSurface: raggio maggiore non valido");
    requireRadius(minorRadius, "ToroidalSurface: raggio minore non valido");
}

void ToroidalSurface::evaluate(double u, double v, int order, Vec3 *out) const {
    const double c = std::cos(u), s = std::sin(u);
    const double cv = std::cos(v), sv = std::sin(v);
    for (int k = 0; k <= order; ++k) {
        for (int l = 0; l <= order; ++l) {
            const double factor = l == 0 ? majorRadius_ + minorRadius_ * cv : minorRadius_ * cosDerivative(l, cv, sv);
            Vec3 value = factor * radial(frame_, k, c, s);
            if (k == 0) value += minorRadius_ * sinDerivative(l, cv, sv) * frame_.zDir();
            if (k == 0 && l == 0) value += frame_.origin();
            out[derivativeIndex(k, l, order)] = value;
        }
    }
}

CurvePtr<3> ToroidalSurface::uIso(double u) const {
    const Vec3 e = radial(frame_, 0, std::cos(u), std::sin(u));
    return std::make_shared<Circle<3>>(frame_.origin() + majorRadius_ * e, e, frame_.zDir(), minorRadius_);
}

CurvePtr<3> ToroidalSurface::vIso(double v) const {
    return signedCircle(frame_.origin() + minorRadius_ * std::sin(v) * frame_.zDir(), frame_.xDir(), frame_.yDir(),
                        majorRadius_ + minorRadius_ * std::cos(v));
}

// --- ExtrusionSurface ------------------------------------------------------

ExtrusionSurface::ExtrusionSurface(CurvePtr<3> curve, const Vec3 &direction)
    : curve_(std::move(curve)), direction_(normalized(direction)) {
    if (!curve_) throw std::invalid_argument("ExtrusionSurface: curva nulla");
    const Interval domain = curve_->domain();
    if (curve_->isPeriodic()) {
        periodic_ = true;
        period_ = curve_->period();
    } else if (domain.isFinite() && distance(curve_->point(domain.lo), curve_->point(domain.hi)) <= kLinearResolution) {
        periodic_ = wraps_ = true;
        period_ = domain.length();
    }
}

double ExtrusionSurface::wrap(double u) const {
    if (!wraps_) return u;
    const Interval domain = curve_->domain();
    if (u >= domain.lo && u <= domain.hi) return u;
    const double shifted = u - period_ * std::floor((u - domain.lo) / period_);
    return std::min(std::max(shifted, domain.lo), domain.hi);
}

std::vector<double> ExtrusionSurface::uBreakpoints(const Interval &range) const {
    if (!wraps_) return curve_->breakpoints(range);
    // Nodi della curva ripetuti in ogni periodo toccato da range.
    const Interval domain = curve_->domain();
    const std::vector<double> base = curve_->breakpoints(domain);
    std::vector<double> result{range.lo};
    const double first = std::floor((range.lo - domain.lo) / period_), last = std::floor((range.hi - domain.lo) / period_);
    for (double k = first; k <= last; ++k)
        for (double b : base) {
            const double t = b + k * period_;
            if (t > range.lo && t < range.hi) result.push_back(t);
        }
    result.push_back(range.hi);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

Interval ExtrusionSurface::vDomain() const { return infiniteInterval(); }

void ExtrusionSurface::evaluate(double u, double v, int order, Vec3 *out) const {
    std::vector<Vec3> c(order + 1);
    curve_->evaluate(wrap(u), order, c.data());
    for (int k = 0; k <= order; ++k) {
        for (int l = 0; l <= order; ++l) {
            Vec3 value;
            if (l == 0) value = c[k];
            if (k == 0 && l == 0) value += v * direction_;
            if (k == 0 && l == 1) value = direction_;
            out[derivativeIndex(k, l, order)] = value;
        }
    }
}

CurvePtr<3> ExtrusionSurface::uIso(double u) const {
    return std::make_shared<Line<3>>(curve_->point(wrap(u)), direction_);
}

CurvePtr<3> ExtrusionSurface::vIso(double v) const {
    return std::make_shared<TransformedCurve>(curve_, Transform3::translation(v * direction_));
}

// --- RevolutionSurface -----------------------------------------------------

RevolutionSurface::RevolutionSurface(CurvePtr<3> meridian, const Vec3 &axisPoint, const Vec3 &axisDirection)
    : meridian_(std::move(meridian)), axisPoint_(axisPoint), axisDirection_(normalized(axisDirection)) {
    if (!meridian_) throw std::invalid_argument("RevolutionSurface: meridiano nullo");
}

// Rot(u) w = (a.w) a + cos u w_perp + sin u (a x w): la parte parallela
// all'asse non dipende da u, le altre ruotano.
void RevolutionSurface::evaluate(double u, double v, int order, Vec3 *out) const {
    std::vector<Vec3> w(order + 1);
    meridian_->evaluate(v, order, w.data());
    w[0] -= axisPoint_;
    const double c = std::cos(u), s = std::sin(u);
    for (int l = 0; l <= order; ++l) {
        const Vec3 parallel = dot(axisDirection_, w[l]) * axisDirection_;
        const Vec3 perpendicular = w[l] - parallel;
        const Vec3 side = cross(axisDirection_, w[l]);
        for (int k = 0; k <= order; ++k) {
            Vec3 value = cosDerivative(k, c, s) * perpendicular + sinDerivative(k, c, s) * side;
            if (k == 0) value += parallel;
            if (k == 0 && l == 0) value += axisPoint_;
            out[derivativeIndex(k, l, order)] = value;
        }
    }
}

CurvePtr<3> RevolutionSurface::uIso(double u) const {
    return std::make_shared<TransformedCurve>(meridian_, Transform3::rotation(axisPoint_, axisDirection_, u));
}

CurvePtr<3> RevolutionSurface::vIso(double v) const {
    const Vec3 w = meridian_->point(v) - axisPoint_;
    const Vec3 parallel = dot(axisDirection_, w) * axisDirection_;
    const Vec3 perpendicular = w - parallel;
    const double radius = norm(perpendicular);
    if (radius <= kLinearResolution) return nullptr;
    return std::make_shared<Circle<3>>(axisPoint_ + parallel, perpendicular / radius,
                                       cross(axisDirection_, perpendicular) / radius, radius);
}

}
