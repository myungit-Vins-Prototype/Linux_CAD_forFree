#include "fk_curve.h"

#include <algorithm>

#include "fk_precision.h"

namespace ForgeCad::Kernel {
namespace {

template <int N>
void orthonormalize(Vec<N> &x, Vec<N> &y) {
    x = normalized(x);
    const Vec<N> projected = y - dot(y, x) * x;
    if (norm(projected) <= kAngularResolution * norm(y))
        throw std::invalid_argument("Conica: assi X e Y paralleli");
    y = normalized(projected);
}

// P(t) = center + cos t A + sin t B e derivate: le derivate di (cos, sin)
// ruotano di 90 gradi a ogni ordine.
template <int N>
void evaluateConic(const Vec<N> &center, const Vec<N> &a, const Vec<N> &b, double t, int order, Vec<N> *out) {
    const double c = std::cos(t);
    const double s = std::sin(t);
    for (int k = 0; k <= order; ++k) {
        double ck, sk;
        switch (k % 4) {
        case 0: ck = c; sk = s; break;
        case 1: ck = -s; sk = c; break;
        case 2: ck = -c; sk = -s; break;
        default: ck = s; sk = -c; break;
        }
        out[k] = ck * a + sk * b;
        if (k == 0) out[k] += center;
    }
}

}

namespace detail {

std::vector<double> quadrantBreakpoints(const Interval &range) {
    std::vector<double> result{range.lo};
    const double tolerance = 1.0e-12 * std::max(1.0, std::fabs(range.hi));
    for (double k = std::ceil(range.lo / kHalfPi); k * kHalfPi < range.hi - tolerance; k += 1.0) {
        const double t = k * kHalfPi;
        if (t > range.lo + tolerance) result.push_back(t);
    }
    result.push_back(range.hi);
    return result;
}

}

template <int N>
Line<N>::Line(const Vec<N> &origin, const Vec<N> &direction) : origin_(origin), direction_(normalized(direction)) {
    if (!isFinite(origin)) throw std::invalid_argument("Line: origine non finita");
}

template <int N>
Interval Line<N>::domain() const {
    const double infinity = std::numeric_limits<double>::infinity();
    return {-infinity, infinity};
}

template <int N>
void Line<N>::evaluate(double t, int order, Vec<N> *out) const {
    out[0] = origin_ + t * direction_;
    if (order >= 1) out[1] = direction_;
    for (int k = 2; k <= order; ++k) out[k] = Vec<N>();
}

template <int N>
Circle<N>::Circle(const Vec<N> &center, const Vec<N> &xAxis, const Vec<N> &yAxis, double radius)
    : center_(center), x_(xAxis), y_(yAxis), radius_(radius) {
    if (!isFinite(center)) throw std::invalid_argument("Circle: centro non finito");
    if (!(radius > kLinearResolution) || !std::isfinite(radius)) throw std::invalid_argument("Circle: raggio non valido");
    orthonormalize(x_, y_);
}

template <int N>
void Circle<N>::evaluate(double t, int order, Vec<N> *out) const {
    evaluateConic(center_, radius_ * x_, radius_ * y_, t, order, out);
}

template <int N>
std::vector<double> Circle<N>::breakpoints(const Interval &range) const {
    return detail::quadrantBreakpoints(range);
}

template <int N>
Ellipse<N>::Ellipse(const Vec<N> &center, const Vec<N> &xAxis, const Vec<N> &yAxis, double xRadius, double yRadius)
    : center_(center), x_(xAxis), y_(yAxis), xRadius_(xRadius), yRadius_(yRadius) {
    if (!isFinite(center)) throw std::invalid_argument("Ellipse: centro non finito");
    if (!(xRadius > kLinearResolution) || !(yRadius > kLinearResolution) || !std::isfinite(xRadius) || !std::isfinite(yRadius))
        throw std::invalid_argument("Ellipse: semiassi non validi");
    orthonormalize(x_, y_);
}

template <int N>
void Ellipse<N>::evaluate(double t, int order, Vec<N> *out) const {
    evaluateConic(center_, xRadius_ * x_, yRadius_ * y_, t, order, out);
}

template <int N>
std::vector<double> Ellipse<N>::breakpoints(const Interval &range) const {
    return detail::quadrantBreakpoints(range);
}

template <int N>
TrimmedCurve<N>::TrimmedCurve(CurvePtr<N> basis, double first, double last)
    : basis_(std::move(basis)), first_(first), last_(last) {
    if (!basis_) throw std::invalid_argument("TrimmedCurve: curva base nulla");
    if (!std::isfinite(first) || !std::isfinite(last)) throw std::invalid_argument("TrimmedCurve: estremi non finiti");
    if (basis_->isPeriodic()) {
        const double period = basis_->period();
        const double tolerance = 1.0e-12 * std::max(1.0, std::fabs(first));
        last_ -= period * std::floor((last_ - first_) / period);
        if (last_ <= first_ + tolerance) last_ += period;
    } else {
        const Interval domain = basis_->domain();
        const double tolerance = 1.0e-12 * std::max({1.0, std::fabs(first), std::fabs(last)});
        if (!domain.contains(first, tolerance) || !domain.contains(last, tolerance))
            throw std::invalid_argument("TrimmedCurve: estremi fuori dal dominio della curva base");
    }
    if (!(last_ > first_)) throw std::invalid_argument("TrimmedCurve: intervallo vuoto");
}

template <int N>
std::vector<double> TrimmedCurve<N>::breakpoints(const Interval &range) const {
    return basis_->breakpoints({std::max(range.lo, first_), std::min(range.hi, last_)});
}

TransformedCurve::TransformedCurve(CurvePtr<3> basis, const Transform3 &transform)
    : basis_(std::move(basis)), transform_(transform) {
    if (!basis_) throw std::invalid_argument("TransformedCurve: curva base nulla");
}

void TransformedCurve::evaluate(double t, int order, Vec3 *out) const {
    basis_->evaluate(t, order, out);
    out[0] = transform_.applyToPoint(out[0]);
    for (int k = 1; k <= order; ++k) out[k] = transform_.applyToVector(out[k]);
}

void TransformedCurve::evaluateLeft(double t, int order, Vec3 *out) const {
    basis_->evaluateLeft(t, order, out);
    out[0] = transform_.applyToPoint(out[0]);
    for (int k = 1; k <= order; ++k) out[k] = transform_.applyToVector(out[k]);
}

template class Line<2>;
template class Line<3>;
template class Circle<2>;
template class Circle<3>;
template class Ellipse<2>;
template class Ellipse<3>;
template class TrimmedCurve<2>;
template class TrimmedCurve<3>;

}
