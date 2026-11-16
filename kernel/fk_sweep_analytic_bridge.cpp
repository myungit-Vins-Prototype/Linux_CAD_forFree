#include "fk_sweep_analytic_bridge.h"
#include "fk_cancel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace ForgeCad::Kernel::detail {
namespace {
constexpr double inf = std::numeric_limits<double>::infinity();
struct I {
    double lo = 0, hi = 0;
    I() = default;
    I(double x) : lo(x), hi(x) {}
    I(double a, double b) : lo(a), hi(b) {}
};
double dn(double x) { return std::nextafter(x, -inf); }
double up(double x) { return std::nextafter(x, inf); }
bool finite(I a) { return std::isfinite(a.lo) && std::isfinite(a.hi) && a.lo <= a.hi; }
I all() { return {-inf, inf}; }
I checked(double a, double b) { return std::isfinite(a) && std::isfinite(b) && a <= b ? I(a, b) : all(); }
I operator+(I a, I b) { return finite(a) && finite(b) ? checked(dn(a.lo + b.lo), up(a.hi + b.hi)) : all(); }
I operator-(I a, I b) { return finite(a) && finite(b) ? checked(dn(a.lo - b.hi), up(a.hi - b.lo)) : all(); }
I operator*(I a, I b) {
    if (!finite(a) || !finite(b))
        return all();
    if (a.lo == 1 && a.hi == 1)
        return b;
    if (b.lo == 1 && b.hi == 1)
        return a;
    if ((a.lo == 0 && a.hi == 0) || (b.lo == 0 && b.hi == 0))
        return I(0);
    double p[] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    for (double x : p)
        if (!std::isfinite(x))
            return all();
    return checked(dn(*std::min_element(p, p + 4)), up(*std::max_element(p, p + 4)));
}
I operator/(I a, I b) {
    if (!finite(b) || !(b.lo > 0))
        return all();
    return a * checked(dn(1 / b.hi), up(1 / b.lo));
}
using V = std::array<I, 3>;
V vec(Vec3 p) { return {I(p.x()), I(p.y()), I(p.z())}; }
V operator+(V a, V b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V operator-(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V operator*(V a, I s) { return {a[0] * s, a[1] * s, a[2] * s}; }
V operator/(V a, I s) { return {a[0] / s, a[1] / s, a[2] / s}; }
I dotI(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V crossI(V a, V b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
// Taylor about zero, no floating range reduction or libm assumption. For
// |x|<=7, degree 48/49 remainder <= 7^50/50! < 6e-23. All polynomial
// arithmetic and the remainder calculation round outward.
I trig(I x, bool sine) {
    if (!finite(x) || std::max(std::fabs(x.lo), std::fabs(x.hi)) > 7)
        return all();
    I term = sine ? x : I(1), sum = term;
    const I xx = x * x;
    for (int k = 1; k <= 24; ++k) {
        const int a = sine ? 2 * k : 2 * k - 1;
        term = (term * xx / I(a * (a + 1))) * I(-1);
        sum = sum + term;
    }
    I remainder(1);
    for (int k = 1; k <= 50; ++k)
        remainder = remainder * I(7) / I(k);
    return sum + I(-remainder.hi, remainder.hi);
}
struct Pole {
    V p;
    I w = I(1);
};
std::vector<Pole> conic(V c, V a, V b, Interval r, bool cubic) {
    if (!r.isFinite() || !(r.hi > r.lo) || std::fabs(r.lo) > 6.4 || std::fabs(r.hi) > 6.4 || r.hi - r.lo > 6.4)
        return {};
    const int arcs = std::max(1, int(std::ceil((r.hi - r.lo) / kHalfPi - 1e-12)));
    if (arcs > 5)
        return {};
    const I step = (I(r.hi) - I(r.lo)) / I(arcs), weight = trig(step / I(2), false);
    if (!(weight.lo > 0))
        return {};
    std::vector<Pole> out;
    for (int k = 0; k < arcs; ++k) {
        cancellationPoint();
        const I t0 = k == 0 ? I(r.lo) : I(r.lo) + I(k) * step, t1 = k + 1 == arcs ? I(r.hi) : I(r.lo) + I(k + 1) * step,
                tm = t0 + step / I(2);
        const Pole p0{c + a * trig(t0, false) + b * trig(t0, true), I(1)},
            p1{c + (a * trig(tm, false) + b * trig(tm, true)) / weight, weight},
            p2{c + a * trig(t1, false) + b * trig(t1, true), I(1)};
        if (out.empty())
            out.push_back(p0);
        if (cubic) {
            const I w1 = (I(1) + I(2) * weight) / I(3), w2 = (I(2) * weight + I(1)) / I(3);
            out.push_back({(p0.p / I(3) + p1.p * (I(2) * weight / I(3))) / w1, w1});
            out.push_back({(p1.p * (I(2) * weight / I(3)) + p2.p / I(3)) / w2, w2});
        } else
            out.push_back(p1);
        out.push_back(p2);
    }
    return out;
}
std::vector<Pole> sectionPoles(const Curve<3> &curve, Interval r, int degree) {
    if (curve.type() == CurveType::Trimmed)
        return sectionPoles(*static_cast<const TrimmedCurve<3> &>(curve).basis(), r, degree);
    if (curve.type() == CurveType::Line) {
        const auto &c = static_cast<const Line<3> &>(curve);
        const V a = vec(c.origin()) + vec(c.direction()) * I(r.lo), b = vec(c.origin()) + vec(c.direction()) * I(r.hi);
        if (degree == 1)
            return {{a, I(1)}, {b, I(1)}};
        if (degree != 3)
            return {};
        return {{a, I(1)}, {(a * I(2) + b) / I(3), I(1)}, {(a + b * I(2)) / I(3), I(1)}, {b, I(1)}};
    }
    if (curve.type() == CurveType::Circle) {
        const auto &c = static_cast<const Circle<3> &>(curve);
        return conic(vec(c.center()), vec(c.xAxis()) * I(c.radius()), vec(c.yAxis()) * I(c.radius()), r, degree == 3);
    }
    if (curve.type() == CurveType::Ellipse) {
        const auto &c = static_cast<const Ellipse<3> &>(curve);
        return conic(vec(c.center()), vec(c.xAxis()) * I(c.xRadius()), vec(c.yAxis()) * I(c.yRadius()), r, degree == 3);
    }
    return {};
}
std::vector<Pole> linear(V a, V b, int degree) {
    if (degree == 1)
        return {{a, I(1)}, {b, I(1)}};
    if (degree != 3)
        return {};
    return {{a, I(1)}, {(a * I(2) + b) / I(3), I(1)}, {(a + b * I(2)) / I(3), I(1)}, {b, I(1)}};
}
const Curve<3> &basis(const Curve<3> &c) {
    return c.type() == CurveType::Trimmed ? basis(*static_cast<const TrimmedCurve<3> &>(c).basis()) : c;
}
// Reconstruct an exact parametrization of the chosen analytic carrier itself,
// not merely of the section from which a near-analytic promotion was inferred.
bool analyticProfile(const Surface &surface, const Curve<3> &section, Interval range, bool rotation,
                     std::vector<Pole> &profile, V &origin, V &x, V &y, V &z, V &translation, int degree) {
    const auto &b = basis(section);
    const Frame3 *frame = nullptr;
    if (surface.type() == SurfaceType::Cylinder)
        frame = &static_cast<const CylindricalSurface &>(surface).frame();
    else if (surface.type() == SurfaceType::Cone)
        frame = &static_cast<const ConicalSurface &>(surface).frame();
    else if (surface.type() == SurfaceType::Torus)
        frame = &static_cast<const ToroidalSurface &>(surface).frame();
    else if (surface.type() == SurfaceType::Sphere)
        frame = &static_cast<const SphericalSurface &>(surface).frame();
    else if (surface.type() == SurfaceType::Plane)
        frame = &static_cast<const Plane &>(surface).frame();
    else
        return false;
    origin = vec(frame->origin());
    x = vec(frame->xDir());
    y = vec(frame->yDir());
    z = vec(frame->zDir());
    if (!rotation) {
        if (surface.type() == SurfaceType::Cylinder && b.type() == CurveType::Circle) {
            const auto &c = static_cast<const Circle<3> &>(b);
            const auto &s = static_cast<const CylindricalSurface &>(surface);
            const double sign = dot(c.yAxis(), frame->yDir()) >= 0 ? 1 : -1;
            profile = conic(origin, x * I(s.radius()), y * I(sign * s.radius()), range, degree == 3);
            translation = z * dotI(translation, z) / dotI(z, z);
            return !profile.empty();
        }
        if (surface.type() == SurfaceType::Plane && b.type() == CurveType::Line) {
            const V n = crossI(x, y);
            const I nn = dotI(n, n);
            if (!(nn.lo > 0))
                return false;
            for (auto &p : profile)
                p.p = p.p - n * (dotI(p.p - origin, n) / nn);
            translation = translation - n * (dotI(translation, n) / nn);
            return true;
        }
        return false;
    }
    if (b.type() == CurveType::Line) {
        const auto &line = static_cast<const Line<3> &>(b);
        const V a = vec(line.origin()) + vec(line.direction()) * I(range.lo),
                bb = vec(line.origin()) + vec(line.direction()) * I(range.hi);
        const I za = dotI(a - origin, z) / dotI(z, z), zb = dotI(bb - origin, z) / dotI(z, z);
        I ra, rb;
        if (surface.type() == SurfaceType::Cylinder)
            ra = rb = I(static_cast<const CylindricalSurface &>(surface).radius());
        else if (surface.type() == SurfaceType::Cone) {
            const auto &s = static_cast<const ConicalSurface &>(surface);
            const I angle(s.semiAngle()), tangent = trig(angle, true) / trig(angle, false);
            ra = I(s.referenceRadius()) + za * tangent;
            rb = I(s.referenceRadius()) + zb * tangent;
        } else if (surface.type() == SurfaceType::Plane) {
            ra = dotI(a - origin, x) / dotI(x, x);
            rb = dotI(bb - origin, x) / dotI(x, x);
        } else
            return false;
        profile = linear({ra, I(0), surface.type() == SurfaceType::Plane ? I(0) : za},
                         {rb, I(0), surface.type() == SurfaceType::Plane ? I(0) : zb}, degree);
        return true;
    }
    if (b.type() == CurveType::Circle &&
        (surface.type() == SurfaceType::Torus || surface.type() == SurfaceType::Sphere)) {
        const auto &c = static_cast<const Circle<3> &>(b);
        const double phase = std::atan2(dot(c.xAxis(), frame->zDir()), dot(c.xAxis(), frame->xDir()));
        const double sign = dot(c.yAxis(), frame->zDir()) * dot(c.xAxis(), frame->xDir()) -
                                        dot(c.yAxis(), frame->xDir()) * dot(c.xAxis(), frame->zDir()) >=
                                    0
                                ? 1
                                : -1;
        const double radius = surface.type() == SurfaceType::Torus
                                  ? static_cast<const ToroidalSurface &>(surface).minorRadius()
                                  : static_cast<const SphericalSurface &>(surface).radius();
        const double major =
            surface.type() == SurfaceType::Torus ? static_cast<const ToroidalSurface &>(surface).majorRadius() : 0;
        const I cp = trig(I(phase), false), sp = trig(I(phase), true);
        profile = conic({I(major), I(0), I(0)}, {I(radius) * cp, I(0), I(radius) * sp},
                        {I(-sign * radius) * sp, I(0), I(sign * radius) * cp}, range, degree == 3);
        return !profile.empty();
    }
    return false;
}
} // namespace
SweepAnalyticBridge boundSweepAnalyticBridge(const Curve<3> &section, Interval range, const BSplineSurface &chart,
                                             const Vec3 &displacement, bool rotation, const Vec3 &center,
                                             const Vec3 &axis, double angle, const Surface *actualAnalytic) {
    SweepAnalyticBridge result;
    auto profile = sectionPoles(section, range, chart.uDegree());
    if (profile.empty() || int(profile.size()) != chart.uPoleCount())
        return result;
    std::vector<Pole> path;
    if (rotation)
        path = conic(vec(Vec3()), vec(Vec3(1, 0, 0)), vec(Vec3(0, 1, 0)), {0, angle}, false);
    else
        path = {Pole{}, Pole{}};
    if (path.empty() || int(path.size()) != chart.vPoleCount())
        return result;
    const V axisI = vec(axis), centerI = vec(center);
    V actualOrigin, actualX, actualY, actualZ, translation = vec(displacement);
    if (actualAnalytic && !analyticProfile(*actualAnalytic, section, range, rotation, profile, actualOrigin, actualX,
                                           actualY, actualZ, translation, chart.uDegree()))
        return {};
    for (std::size_t i = 0; i < profile.size(); ++i)
        for (std::size_t j = 0; j < path.size(); ++j) {
            cancellationPoint();
            V ideal;
            I weight = profile[i].w;
            if (rotation && actualAnalytic) {
                ideal = actualOrigin + actualZ * profile[i].p[2] +
                        (actualX * path[j].p[0] + actualY * path[j].p[1]) * profile[i].p[0];
                weight = weight * path[j].w;
            } else if (rotation) {
                const V q = profile[i].p - centerI, parallel = axisI * dotI(q, axisI), radial = q - parallel;
                ideal = centerI + parallel + radial * path[j].p[0] + crossI(axisI, radial) * path[j].p[1];
                weight = weight * path[j].w;
            } else
                ideal = profile[i].p + translation * I(j);
            const Vec3 actual = chart.pole(int(i), int(j));
            for (int k = 0; k < 3; ++k) {
                I delta = ideal[k] - I(actual[k]);
                if (!finite(delta))
                    return {};
                result.poleError = std::max(result.poleError, std::max(std::fabs(delta.lo), std::fabs(delta.hi)));
            }
            const double actualWeight = chart.weight(int(i), int(j));
            const I delta = weight.lo == actualWeight && weight.hi == actualWeight ? I(0) : weight - I(actualWeight);
            if (!finite(delta))
                return {};
            result.weightError = std::max(result.weightError, std::max(std::fabs(delta.lo), std::fabs(delta.hi)));
        }
    result.bounded = true;
    return result;
}
} // namespace ForgeCad::Kernel::detail
