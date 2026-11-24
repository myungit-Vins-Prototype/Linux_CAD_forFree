#include "fk_helix_projection.h"
#include "fk_cancel.h"
#include "fk_angular_bounds.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
namespace ForgeCad::Kernel::detail {
namespace {
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr std::size_t maxWrappers = 256;
constexpr double maxUniformSpans = 16384;
struct I {
    double lo = 0, hi = 0;
    I() = default;
    I(double x) : lo(x), hi(x) {}
    I(double a, double b) : lo(a), hi(b) {}
};
bool finite(I a) { return std::isfinite(a.lo) && std::isfinite(a.hi) && a.lo <= a.hi; }
double down(double x) { return std::nextafter(x, -infinity); }
double up(double x) { return std::nextafter(x, infinity); }
I checked(double lo, double hi) { return std::isfinite(lo) && std::isfinite(hi) && lo <= hi ? I(lo, hi) : I(-infinity, infinity); }
I operator+(I a, I b) { return finite(a) && finite(b) ? checked(down(a.lo + b.lo), up(a.hi + b.hi)) : I(-infinity, infinity); }
I operator-(I a, I b) { return finite(a) && finite(b) ? checked(down(a.lo - b.hi), up(a.hi - b.lo)) : I(-infinity, infinity); }
I operator*(I a, I b) {
    if (!finite(a) || !finite(b))
        return {-infinity, infinity};
    const double p[] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    for (double x : p)
        if (!std::isfinite(x))
            return {-infinity, infinity};
    return checked(down(*std::min_element(p, p + 4)), up(*std::max_element(p, p + 4)));
}
I operator/(I a, I b) {
    if (!finite(b) || !(b.lo > 0))
        return {-infinity, infinity};
    return a * checked(down(1 / b.hi), up(1 / b.lo));
}
double absoluteUpper(I a) { return finite(a) ? std::max(std::abs(a.lo), std::abs(a.hi)) : infinity; }
using V = std::array<I, 3>;
V vector(const Vec3 &p) { return {I(p.x()), I(p.y()), I(p.z())}; }
V add(V a, V b) {
    for (int k = 0; k < 3; ++k)
        a[k] = a[k] + b[k];
    return a;
}
V subtract(V a, V b) {
    for (int k = 0; k < 3; ++k)
        a[k] = a[k] - b[k];
    return a;
}
V scale(V a, I b) {
    for (I &x : a)
        x = x * b;
    return a;
}
I dot(V a, V b) {
    I result;
    for (int k = 0; k < 3; ++k)
        result = result + a[k] * b[k];
    return result;
}
bool finite(V a) {
    for (I x : a)
        if (!finite(x))
            return false;
    return true;
}
double normUpper(V a) {
    I squared;
    for (I x : a) {
        const I m(absoluteUpper(x));
        squared = squared + m * m;
    }
    return finite(squared) ? up(std::sqrt(std::max(0., squared.hi))) : infinity;
}
V transformed(V a, const Transform3 &t, bool point) {
    V result = point ? vector(t.translationPart()) : V{};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            result[row] = result[row] + a[col] * I(t.matrix(row, col));
    return result;
}
V projected(V a, const Frame3 &f, bool point) {
    if (point)
        a = subtract(a, vector(f.origin()));
    return {dot(a, vector(f.xDir())), dot(a, vector(f.yDir())), I(0)};
}
struct Data {
    Interval range;
    HelixSpec spec;
    double slope = 0, rise = 0, sense = 1;
    std::vector<Transform3> transforms;
    Frame3 frame;
    V origin, x, y, z;
    double radialNorm = 0;
    double fourth(const Interval &span) const {
        const I radius = I(spec.radius) + I(slope) * I(span.lo, span.hi);
        const I result = (I(absoluteUpper(radius)) + I(4) * I(std::abs(slope))) * I(radialNorm);
        return finite(result) ? result.hi : infinity;
    }
    std::pair<V, V> jet(double t) const {
        const I parameter(t), radius = I(spec.radius) + I(slope) * parameter;
        const auto phase = boundedSineCosine(I(spec.startAngle) + I(sense) * parameter);
        if (!phase) throw std::domain_error("proiezione elica: riduzione della fase non risolta");
        const auto &[sine, cosine] = *phase;
        const V radial = add(scale(x, cosine), scale(y, sine));
        const V derivative = scale(add(scale(x, I(0) - sine), scale(y, cosine)), I(sense));
        const V point = add(add(origin, scale(radial, radius)), scale(z, I(rise) * parameter));
        const V tangent = add(add(scale(radial, I(slope)), scale(derivative, radius)), scale(z, I(rise)));
        return {point, tangent};
    }
    Vec3 value(double t) const {
        cancellationPoint();
        const auto &f = spec.frame;
        const double angle = spec.startAngle + sense * t, radius = spec.radius + slope * t;
        Vec3 p = f.origin() + radius * (std::cos(angle) * f.xDir() + std::sin(angle) * f.yDir()) + (rise * t) * f.zDir();
        for (auto i = transforms.rbegin(); i != transforms.rend(); ++i)
            p = i->applyToPoint(p);
        p = frame.toLocal(p);
        return {p.x(), p.y(), 0};
    }
};
std::shared_ptr<Data> prepare(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame) {
    cancellationPoint();
    if (!source || !range.isFinite() || !(range.lo < range.hi) || !std::isfinite(range.length()))
        throw std::domain_error("proiezione elica: sorgente o intervallo non valido");
    const Curve<3> *basis = source.get();
    std::vector<Transform3> transforms;
    std::size_t depth = 0;
    while (basis->type() == CurveType::Trimmed || basis->type() == CurveType::Transformed) {
        cancellationPoint();
        if (++depth > maxWrappers)
            throw std::domain_error("proiezione elica: budget wrapper esaurito");
        if (basis->type() == CurveType::Trimmed) {
            // Trimmed stores its range; Transformed::domain delegates recursively.
            const auto &trimmed = static_cast<const TrimmedCurve<3> &>(*basis);
            const auto domain = trimmed.domain();
            if ((std::isfinite(domain.lo) && range.lo < domain.lo) || (std::isfinite(domain.hi) && range.hi > domain.hi))
                throw std::domain_error("proiezione elica: intervallo fuori dominio wrapper");
            basis = trimmed.basis().get();
        } else {
            const auto &moved = static_cast<const TransformedCurve &>(*basis);
            transforms.push_back(moved.transform());
            basis = moved.basis().get();
        }
    }
    const auto *helix = dynamic_cast<const HelixCurve *>(basis);
    if (!helix)
        return {};
    const auto domain = helix->domain();
    if (!domain.isFinite() || range.lo < domain.lo || range.hi > domain.hi)
        throw std::domain_error("proiezione elica: intervallo fuori dominio originale");
    auto data = std::make_shared<Data>();
    data->range = range;
    data->spec = helix->spec();
    data->slope = helix->radialSlope();
    data->rise = helix->risePerRadian();
    data->sense = data->spec.leftHanded ? -1. : 1.;
    data->transforms = std::move(transforms);
    data->frame = frame;
    if (!std::isfinite(data->slope) || !std::isfinite(data->rise) || !std::isfinite(data->spec.radius) ||
        !std::isfinite(data->spec.startAngle))
        throw std::domain_error("proiezione elica: coefficienti originali non finiti");
    const auto &f = data->spec.frame;
    data->origin = vector(f.origin());
    data->x = vector(f.xDir());
    data->y = vector(f.yDir());
    data->z = vector(f.zDir());
    for (auto it = data->transforms.rbegin(); it != data->transforms.rend(); ++it) {
        cancellationPoint();
        data->origin = transformed(data->origin, *it, true);
        data->x = transformed(data->x, *it, false);
        data->y = transformed(data->y, *it, false);
        data->z = transformed(data->z, *it, false);
    }
    data->origin = projected(data->origin, frame, true);
    data->x = projected(data->x, frame, false);
    data->y = projected(data->y, frame, false);
    data->z = projected(data->z, frame, false);
    if (!finite(data->origin) || !finite(data->x) || !finite(data->y) || !finite(data->z))
        throw std::domain_error("proiezione elica: trasformazione o frame non finito");
    const I radialNorm = I(normUpper(data->x)) + I(normUpper(data->y));
    data->radialNorm = radialNorm.hi;
    if (!finite(radialNorm) || !std::isfinite(data->fourth(range)))
        throw std::domain_error("proiezione elica: derivate non risolte");
    // Validate endpoint phase reduction now rather than repeatedly allocating
    // candidate output for a recognized but numerically unresolved source.
    data->jet(range.lo);
    data->jet(range.hi);
    return data;
}
CurveFitErrorBound oracle(const std::shared_ptr<Data> &data) {
    return [data](const Interval &span, const std::array<Vec3, 4> &poles) {
        cancellationPoint();
        if (!span.isFinite() || !(span.lo < span.hi) || span.lo < data->range.lo || span.hi > data->range.hi)
            return infinity;
        const I h = I(span.hi) - I(span.lo), third = h / I(3);
        const auto [first, firstDerivative] = data->jet(span.lo);
        const auto [last, lastDerivative] = data->jet(span.hi);
        const std::array<V, 4> hermite = {first, add(first, scale(firstDerivative, third)), subtract(last, scale(lastDerivative, third)),
                                          last};
        double controls = 0;
        for (int k = 0; k < 4; ++k) {
            if (!isFinite(poles[k]))
                return infinity;
            controls = std::max(controls, normUpper(subtract(vector(poles[k]), hermite[k])));
        }
        // Vector Hermite Peano kernel is nonnegative. Its integral is
        // (t-a)^2(t-b)^2/4!, at most h^4/384, so the Euclidean remainder
        // is bounded by sup ||F''''|| * h^4/384 over the whole interval.
        const I remainder = I(data->fourth(span)) * h * h * h * h / I(384);
        const I result = I(controls) + remainder;
        return finite(result) ? result.hi : infinity;
    };
}
double validate(const CurvePtr<2> &curve, const Interval &range, const CurveFitErrorBound &bound, double tolerance) {
    const auto *spline = dynamic_cast<const BSplineCurve<2> *>(curve.get());
    if (!spline || spline->degree() != 3 || spline->isRational() || spline->poleCount() < 4 || (spline->poleCount() - 1) % 3 != 0 ||
        spline->domain().lo != range.lo || spline->domain().hi != range.hi || !(tolerance > 0) || !std::isfinite(tolerance))
        throw std::domain_error("proiezione elica: rappresentazione finale non riconosciuta");
    const auto &p = spline->poles();
    const auto &knots = spline->knots();
    if (knots.size() != std::size_t(spline->poleCount() + 4))
        throw std::domain_error("proiezione elica: nodi finali non validi");
    for (int i = 0; i < 4; ++i)
        if (knots[i] != range.lo || knots[knots.size() - 1 - i] != range.hi)
            throw std::domain_error("proiezione elica: estremi finali non bloccati");
    double worst = 0;
    for (int k = 0; k + 3 < spline->poleCount(); k += 3) {
        cancellationPoint();
        const Interval span{knots[k + 3], knots[k + 4]};
        if (!span.isFinite() || !(span.lo < span.hi) || knots[k + 1] != span.lo || knots[k + 2] != span.lo || knots[k + 5] != span.hi ||
            knots[k + 6] != span.hi)
            throw std::domain_error("proiezione elica: schema Bezier finale non valido");
        const double error = bound(span, {Vec3(p[k].x(), p[k].y(), 0), Vec3(p[k + 1].x(), p[k + 1].y(), 0),
                                          Vec3(p[k + 2].x(), p[k + 2].y(), 0), Vec3(p[k + 3].x(), p[k + 3].y(), 0)});
        if (!std::isfinite(error) || error < 0 || error > tolerance)
            throw std::domain_error("proiezione elica: risultato oltre il budget continuo");
        worst = std::max(worst, error);
    }
    return worst;
}
} // namespace
HelixProjectionFitData helixProjectionErrorBound(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame) {
    const auto data = prepare(source, range, frame);
    if (!data)
        return {};
    return {oracle(data), [data](double t) { return data->value(t); }, data->fourth(range)};
}
std::shared_ptr<BSplineCurve<2>> projectHelixCertified(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame,
                                                       double tolerance, double *deviation) {
    cancellationPoint();
    if (!(tolerance > 0) || !std::isfinite(tolerance))
        throw std::domain_error("proiezione elica: tolleranza non valida");
    const auto data = helixProjectionErrorBound(source, range, frame);
    if (!data.bound)
        return {};
    // This is a conservative sufficient-work policy for a uniform Hermite
    // partition, not a claim of mathematical impossibility for other fits.
    // It rejects huge inputs before constructing millions of pending spans.
    const double estimate = range.length() * std::pow(data.fourthDerivativeBound / (192 * tolerance), 0.25);
    if (!std::isfinite(estimate) || estimate > maxUniformSpans)
        throw std::domain_error("proiezione elica: budget conservativo di lavoro esaurito");
    const auto fitted = fitCurveCertified(data.value, range, {}, tolerance, data.bound);
    std::vector<Vec2> poles;
    poles.reserve(fitted->poles().size());
    for (const auto &p : fitted->poles()) {
        cancellationPoint();
        poles.emplace_back(p.x(), p.y());
    }
    auto result = std::make_shared<BSplineCurve<2>>(3, fitted->knots(), std::move(poles));
    const double error = validate(result, range, data.bound, tolerance);
    cancellationPoint();
    if (deviation)
        *deviation = error;
    return result;
}
double requireHelixProjection(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame, const CurvePtr<2> &actual,
                              double tolerance) {
    const auto data = helixProjectionErrorBound(source, range, frame);
    if (!data.bound)
        throw std::domain_error("proiezione elica: provenienza finale non riconosciuta");
    return validate(actual, range, data.bound, tolerance);
}
} // namespace ForgeCad::Kernel::detail
