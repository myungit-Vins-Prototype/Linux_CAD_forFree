#include "fk_surface_curve_bounds.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <stdexcept>
#include <vector>

namespace ForgeCad::Kernel::detail {
namespace {
constexpr double infinity = std::numeric_limits<double>::infinity();
// Fixed work/storage limits, independent of the requested geometric budget.
constexpr std::size_t maxScalars = 2000000;
constexpr std::size_t maxVisits = 16384;
constexpr std::size_t maxCellsPerNode = 256;
constexpr int maxDepth = 24;
struct ProofFailed {};
struct Work {
    std::size_t remaining = maxVisits;
    void visit() {
        cancellationPoint();
        if (!remaining) throw ProofFailed{};
        --remaining;
    }
};
struct I {
    double lo = 0, hi = 0;
    I() = default;
    explicit I(double value) : lo(value), hi(value) {}
    I(double first, double last) : lo(first), hi(last) {}
};
bool finite(I value) {
    return std::isfinite(value.lo) && std::isfinite(value.hi) && value.lo <= value.hi;
}
bool exact(I value, double number) { return value.lo == number && value.hi == number; }
I whole() { return {-infinity, infinity}; }
I outward(double lo, double hi) {
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi) return whole();
    return {std::nextafter(lo, -infinity), std::nextafter(hi, infinity)};
}
I operator+(I a, I b) {
    if (!finite(a) || !finite(b)) return whole();
    if (exact(a, 0)) return b;
    if (exact(b, 0)) return a;
    if (a.lo == a.hi && b.lo == b.hi && a.lo == -b.lo) return I(0);
    return outward(a.lo + b.lo, a.hi + b.hi);
}
I operator-(I a, I b) {
    if (!finite(a) || !finite(b)) return whole();
    if (exact(b, 0)) return a;
    if (a.lo == a.hi && b.lo == b.hi && a.lo == b.lo) return I(0);
    if (exact(a, 0)) return {-b.hi, -b.lo};
    return outward(a.lo - b.hi, a.hi - b.lo);
}
I operator*(I a, I b) {
    if (!finite(a) || !finite(b)) return whole();
    if (exact(a, 0) || exact(b, 0)) return I(0);
    if (exact(a, 1)) return b;
    if (exact(b, 1)) return a;
    if (exact(a, -1)) return {-b.hi, -b.lo};
    if (exact(b, -1)) return {-a.hi, -a.lo};
    const double products[] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    for (double value : products) if (!std::isfinite(value)) return whole();
    return outward(*std::min_element(products, products + 4), *std::max_element(products, products + 4));
}
I operator/(I a, I b) {
    if (!finite(a) || !finite(b) || !(b.lo > 0)) return whole();
    if (exact(a, 0)) return I(0);
    if (exact(b, 1)) return a;
    const double quotients[] = {a.lo / b.lo, a.lo / b.hi, a.hi / b.lo, a.hi / b.hi};
    for (double value : quotients) if (!std::isfinite(value)) return whole();
    return outward(*std::min_element(quotients, quotients + 4), *std::max_element(quotients, quotients + 4));
}
using P = std::vector<I>;
template<int N> using VP = std::array<P, N>;
template<int N> using Point = std::array<I, N>;

P elevated(P values, std::size_t degree) {
    while (values.size() <= degree) {
        const auto n = values.size();
        P next(n + 1);
        next.front() = values.front(); next.back() = values.back();
        for (std::size_t i = 1; i < n; ++i) {
            const I t = I(double(i)) / I(double(n));
            next[i] = values[i - 1] * t + values[i] * (I(1) - t);
        }
        values = std::move(next);
    }
    return values;
}
P sum(P a, P b, bool subtract = false) {
    const auto degree = std::max(a.size(), b.size()) - 1;
    a = elevated(std::move(a), degree); b = elevated(std::move(b), degree);
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = subtract ? a[i] - b[i] : a[i] + b[i];
    return a;
}
P scaled(P values, I factor) {
    for (I &value : values) value = value * factor;
    return values;
}
// Degrees never exceed 21 in the supported rational residual. The binomial
// integers are exact (<2^53); only their ratio needs outward rounding.
double choose(int n, int k) {
    std::uint64_t value = 1;
    for (int i = 1; i <= std::min(k, n - k); ++i) value = value * std::uint64_t(n - i + 1) / std::uint64_t(i);
    return double(value);
}
P product(const P &a, const P &b) {
    const int m = int(a.size()) - 1, n = int(b.size()) - 1;
    if (m + n > 21) throw ProofFailed{};
    P values(std::size_t(m + n + 1));
    for (int i = 0; i <= m; ++i)
        for (int j = 0; j <= n; ++j) {
            const I factor = I(choose(m, i)) * I(choose(n, j)) / I(choose(m + n, i + j));
            values[std::size_t(i + j)] = values[std::size_t(i + j)] + a[std::size_t(i)] * b[std::size_t(j)] * factor;
        }
    return values;
}
I hull(const P &values) {
    I result(infinity, -infinity);
    for (I value : values) {
        if (!finite(value)) return whole();
        result.lo = std::min(result.lo, value.lo); result.hi = std::max(result.hi, value.hi);
    }
    return result;
}
// Polar form, retaining original parameter anchors. It also encloses
// roundoff slivers outside [0,1]; no rounded Bezier conversion is trusted.
P restrictP(const P &original, I first, I last) {
    if (exact(first, 0) && exact(last, 1)) return original;
    const int degree = int(original.size()) - 1;
    P result(original.size());
    for (int pole = 0; pole <= degree; ++pole) {
        P values = original;
        for (int r = 1; r <= degree; ++r) {
            const I t = r <= degree - pole ? first : last;
            for (int j = 0; j <= degree - r; ++j)
                values[std::size_t(j)] = values[std::size_t(j)] * (I(1) - t) + values[std::size_t(j + 1)] * t;
        }
        result[std::size_t(pole)] = values.front();
    }
    return result;
}
I parameter(double value, double lo, double hi) {
    if (value == lo) return I(0);
    if (value == hi) return I(1);
    return (I(value) - I(lo)) / (I(hi) - I(lo));
}
bool validKnots(const std::vector<double> &knots, int degree, int count) {
    if (count <= degree || knots.size() != std::size_t(count + degree + 1)) return false;
    for (std::size_t i = 0; i < knots.size(); ++i)
        if (!std::isfinite(knots[i]) || (i && knots[i] < knots[i - 1])) return false;
    if (!(knots[std::size_t(degree)] < knots[std::size_t(count)])) return false;
    // The supported curves/carriers are position-continuous at internal knots.
    for (int i = degree + 1, multiplicity = 1; i < count; ++i) {
        multiplicity = knots[std::size_t(i)] == knots[std::size_t(i - 1)] ? multiplicity + 1 : 1;
        if (multiplicity > degree) return false;
    }
    return true;
}
template<int N> struct Source {
    int degree = 1;
    std::vector<double> knots;
    std::vector<Point<N>> poles;
    std::array<I, N> originalHull;
};
template<int N> Source<N> snapshot(const BSplineCurve<N> &curve, const Point<N> &origin) {
    Source<N> result; result.degree = curve.degree(); result.knots = curve.knots();
    for (int axis = 0; axis < N; ++axis) result.originalHull[std::size_t(axis)] = I(infinity, -infinity);
    result.poles.reserve(std::size_t(curve.poleCount()));
    for (const auto &pole : curve.poles()) {
        cancellationPoint();
        Point<N> point;
        for (int axis = 0; axis < N; ++axis) {
            const I value(pole[axis]);
            point[std::size_t(axis)] = value - origin[std::size_t(axis)];
            auto &bound = result.originalHull[std::size_t(axis)];
            bound.lo = std::min(bound.lo, pole[axis]); bound.hi = std::max(bound.hi, pole[axis]);
        }
        result.poles.push_back(point);
    }
    return result;
}
template<int N> VP<N> onRange(const Source<N> &source, const Interval &query) {
    const int count = int(source.poles.size()), p = source.degree;
    int k = int(std::upper_bound(source.knots.begin(), source.knots.end(), query.lo) - source.knots.begin()) - 1;
    k = std::clamp(k, p, count - 1);
    const double lo = source.knots[std::size_t(k)], hi = source.knots[std::size_t(k + 1)];
    if (!(lo <= query.lo && query.hi <= hi && lo < hi)) throw ProofFailed{};
    bool bezier = true;
    for (int j = 1; j <= p; ++j)
        bezier = bezier && source.knots[std::size_t(k - p + j)] == lo && source.knots[std::size_t(k + j)] == hi;
    VP<N> values;
    for (int pole = 0; pole <= p; ++pole) {
        auto point = source.poles[std::size_t(k - p + pole)];
        if (!bezier) {
            std::vector<Point<N>> original(source.poles.begin() + k - p, source.poles.begin() + k + 1);
            for (int r = 1; r <= p; ++r) {
                const double t = r <= p - pole ? lo : hi;
                for (int j = p; j >= r; --j) {
                    const I alpha = (I(t) - I(source.knots[std::size_t(k - p + j)])) /
                        (I(source.knots[std::size_t(k + j - r + 1)]) - I(source.knots[std::size_t(k - p + j)]));
                    for (int axis = 0; axis < N; ++axis)
                        original[std::size_t(j)][std::size_t(axis)] = original[std::size_t(j - 1)][std::size_t(axis)] * (I(1) - alpha)
                            + original[std::size_t(j)][std::size_t(axis)] * alpha;
                }
            }
            point = original.back();
        }
        for (int axis = 0; axis < N; ++axis) values[std::size_t(axis)].push_back(point[std::size_t(axis)]);
    }
    for (auto &value : values) value = restrictP(value, parameter(query.lo, lo, hi), parameter(query.hi, lo, hi));
    return values;
}
// Original local Cox basis polynomial on one selected carrier knot cell.
// Q is polynomial on this t interval; all neighboring cell polynomials are
// checked, including their extensions over the remainder of the interval.
std::vector<P> basis(const std::vector<double> &knots, int cell, const P &q, int order) {
    std::vector<P> previous(1, P{I(1)});
    for (int degree = 1; degree <= order; ++degree) {
        std::vector<P> current(std::size_t(degree + 1), P{I(0)});
        for (int r = 0; r <= degree; ++r) {
            const int i = cell - degree + r;
            const double a = knots[std::size_t(i)], b = knots[std::size_t(i + degree)];
            const double c = knots[std::size_t(i + 1)], d = knots[std::size_t(i + degree + 1)];
            if (r && a < b)
                current[std::size_t(r)] = sum(current[std::size_t(r)], product(previous[std::size_t(r - 1)],
                    scaled(sum(q, P{I(a)}, true), I(1) / (I(b) - I(a)))));
            if (r < degree && c < d)
                current[std::size_t(r)] = sum(current[std::size_t(r)], product(previous[std::size_t(r)],
                    scaled(sum(P{I(d)}, q, true), I(1) / (I(d) - I(c)))));
        }
        previous = std::move(current);
    }
    return previous;
}
// Homogeneous Cox polynomial: B_i,p(U/D) * D^p. Keep the original
// polynomial-Q recurrence above unchanged, including its rounding order.
std::vector<P> homogeneousBasis(const std::vector<double> &knots, int cell,
                                const P &numerator, const P &denominator, int order) {
    std::vector<P> previous(1, P{I(1)});
    for (int degree = 1; degree <= order; ++degree) {
        std::vector<P> current(std::size_t(degree + 1), P{I(0)});
        for (int r = 0; r <= degree; ++r) {
            const int i = cell - degree + r;
            const double a = knots[std::size_t(i)], b = knots[std::size_t(i + degree)];
            const double c = knots[std::size_t(i + 1)], d = knots[std::size_t(i + degree + 1)];
            if (r && a < b)
                current[std::size_t(r)] = sum(current[std::size_t(r)], product(previous[std::size_t(r - 1)],
                    scaled(sum(numerator, scaled(denominator, I(a)), true), I(1) / (I(b) - I(a)))));
            if (r < degree && c < d)
                current[std::size_t(r)] = sum(current[std::size_t(r)], product(previous[std::size_t(r)],
                    scaled(sum(scaled(denominator, I(d)), numerator, true), I(1) / (I(d) - I(c)))));
        }
        previous = std::move(current);
    }
    return previous;
}
std::vector<int> cells(const std::vector<double> &knots, int count, I image, int degree) {
    std::vector<int> result;
    int i = int(std::lower_bound(knots.begin() + degree, knots.begin() + count + 1, image.lo) - knots.begin()) - 1;
    for (i = std::max(degree, i); i < count && knots[std::size_t(i)] <= image.hi; ++i)
        if (knots[std::size_t(i)] < knots[std::size_t(i + 1)] && knots[std::size_t(i + 1)] >= image.lo) {
            result.push_back(i);
            if (result.size() > maxCellsPerNode) break;
        }
    return result;
}
struct Carrier {
    std::vector<double> u, v;
    int nu = 0, nv = 0, pu = 3, pv = 3;
    std::vector<Point<3>> poles;
    std::vector<I> weights; // Empty preserves the polynomial fast path.
    double minimumWeight = 1;
};
double residual(const Carrier &surface, int ku, int kv, const VP<2> &uv, const VP<3> &edge,
                const P *edgeDenominator = nullptr, double edgeLower = 1) {
    const auto u = basis(surface.u, ku, uv[0], surface.pu), v = basis(surface.v, kv, uv[1], surface.pv);
    VP<3> composed{P{I(0)}, P{I(0)}, P{I(0)}};
    P denominator{I(0)};
    const bool rational = !surface.weights.empty();
    for (int i = 0; i <= surface.pu; ++i)
        for (int j = 0; j <= surface.pv; ++j) {
            const P factor = product(u[std::size_t(i)], v[std::size_t(j)]);
            const auto index = std::size_t((ku - surface.pu + i) * surface.nv + kv - surface.pv + j);
            const auto &pole = surface.poles[index];
            if (rational) denominator = sum(denominator, scaled(factor, surface.weights[index]));
            for (int axis = 0; axis < 3; ++axis)
                composed[std::size_t(axis)] = sum(composed[std::size_t(axis)], scaled(factor, pole[std::size_t(axis)]));
        }
    double lower = 1;
    if (rational) {
        const I bounds = hull(denominator);
        if (!finite(bounds)) return infinity;
        // Only the subset where this cell is ACTIVE is relevant. Q has
        // already been proved inside the natural domain, where the original
        // B-spline basis is nonnegative and sums to one. Hence W>=min(w).
        // The cell polynomial may be negative outside that subset; its full
        // interval hull is a second enclosure, not the positivity argument.
        lower = std::max(surface.minimumWeight, bounds.lo);
        if (!(lower > 0) || !std::isfinite(lower)) return infinity;
    }
    double largest = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const P projected = rational ? product(edge[std::size_t(axis)], denominator) : edge[std::size_t(axis)];
        const I value = edgeDenominator
            ? hull(sum(product(composed[std::size_t(axis)], *edgeDenominator), projected, true))
            : hull(sum(composed[std::size_t(axis)], projected, true));
        if (!finite(value)) return infinity;
        largest = std::max({largest, std::fabs(value.lo), std::fabs(value.hi)});
    }
    if (largest == 0) return 0;
    // max |coordinate| * sqrt(3) bounds the Euclidean norm without squaring
    // small values into underflow. Both operations are rounded outwards.
    const double numerator = std::nextafter(largest * std::nextafter(std::sqrt(3.0), infinity), infinity);
    if (edgeDenominator) {
        // Positive outward divisions avoid rounding an unsafe nominal product
        // of denominator lower bounds. Any intermediate overflow fails closed.
        return ((I(numerator) / I(lower)) / I(edgeLower)).hi;
    }
    return rational ? (I(numerator) / I(lower)).hi : numerator;
}
// Separate rational-Q branch: the polynomial-Q residual and its arithmetic
// stay unchanged. Both tensor axes share D, so every carrier coordinate has
// the common denominator D^(pu+pv), even when the carrier is polynomial.
double rationalUVResidual(const Carrier &surface, int ku, int kv,
                          const VP<2> &uvNumerator, const P &uvDenominator, double uvLower,
                          const VP<3> &edge, const P *edgeDenominator, double edgeLower) {
    const auto u = homogeneousBasis(surface.u, ku, uvNumerator[0], uvDenominator, surface.pu);
    const auto v = homogeneousBasis(surface.v, kv, uvNumerator[1], uvDenominator, surface.pv);
    VP<3> composed{P{I(0)}, P{I(0)}, P{I(0)}};
    P denominator{I(0)};
    const bool rational = !surface.weights.empty();
    const int power = surface.pu + surface.pv;
    for (int i = 0; i <= surface.pu; ++i)
        for (int j = 0; j <= surface.pv; ++j) {
            const P factor = product(u[std::size_t(i)], v[std::size_t(j)]);
            const auto index = std::size_t((ku - surface.pu + i) * surface.nv + kv - surface.pv + j);
            if (rational) denominator = sum(denominator, scaled(factor, surface.weights[index]));
            for (int axis = 0; axis < 3; ++axis)
                composed[std::size_t(axis)] = sum(composed[std::size_t(axis)],
                    scaled(factor, surface.poles[index][std::size_t(axis)]));
        }
    if (!rational) {
        denominator = P{I(1)};
        for (int degree = 0; degree < power; ++degree) denominator = product(denominator, uvDenominator);
    }
    const I denominatorHull = hull(denominator);
    if (!finite(denominatorHull)) return infinity;
    double largest = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const P projected = product(edge[std::size_t(axis)], denominator);
        const I value = edgeDenominator
            ? hull(sum(product(composed[std::size_t(axis)], *edgeDenominator), projected, true))
            : hull(sum(composed[std::size_t(axis)], projected, true));
        if (!finite(value)) return infinity;
        largest = std::max({largest, std::fabs(value.lo), std::fabs(value.hi)});
    }
    if (largest == 0) return 0;
    const double numerator = std::nextafter(largest * std::nextafter(std::sqrt(3.0), infinity), infinity);
    // On the ACTIVE portion of this cell, W_Sbar >= effectiveMinSW * D^power.
    // A constant carrier weight was normalized away by the original snapshot:
    // its effective minimum is ONE, not that discarded common weight.
    I upper = I(numerator) / I(rational ? surface.minimumWeight : 1);
    for (int degree = 0; degree < power; ++degree) upper = upper / I(uvLower);
    if (edgeDenominator) upper = upper / I(edgeLower);
    // The full cell polynomial hull supplies a second independent bound.
    // Taking the smaller of VALID uppers avoids an unsafe nominal product
    // of denominator lowers and remains valid if either division overflows.
    if (denominatorHull.lo > 0) {
        I alternative = I(numerator) / I(denominatorHull.lo);
        if (edgeDenominator) alternative = alternative / I(edgeLower);
        return std::min(upper.hi, alternative.hi);
    }
    return upper.hi;
}
template<int N> bool finitePoles(const BSplineCurve<N> &curve) {
    for (const auto &pole : curve.poles()) {
        cancellationPoint();
        for (int axis = 0; axis < N; ++axis) if (!std::isfinite(pole[axis])) return false;
    }
    return true;
}
template<class Weight> int weights(int count, Weight weight) {
    bool equal = true;
    const double first = weight(0);
    for (int i = 0; i < count; ++i) {
        cancellationPoint();
        const double value = weight(i);
        if (!(value > 0) || !std::isfinite(value)) return -1;
        equal = equal && value == first;
    }
    return equal ? 1 : 0;
}
// Candidate construction, before the ONE continuous acceptance check.
// Source E/S, the edge parameter and the caller's fixed budget are unchanged.
// Unsupported families retain their exact original pcurve pointer.
// Inspect only LOCAL wrapper metadata, before recursive virtual methods.
// Native-family recognition follows the walk: a valid periodic unsupported
// base must not be rejected by a nonperiodic native-domain assumption.
template<int N> struct CurveSource {
    const Curve<N> *base = nullptr;
    std::array<const Transform3 *, 32> placements{};
    std::size_t count = 0, links = 0;
};
template<int N> bool curveSource(const Curve<N> *curve, const Interval &range, CurveSource<N> &source) {
    source.base = curve;
    while (source.base) {
        cancellationPoint();
        if (const auto *trim = dynamic_cast<const TrimmedCurve<N> *>(source.base)) {
            if (source.links == 32) return false;
            ++source.links;
            const Interval domain = trim->domain(); // Stored endpoints, no recursive accessor.
            if (!domain.isFinite() || !(domain.lo < domain.hi) || !std::isfinite(domain.length())
                || range.lo < domain.lo || range.hi > domain.hi) return false;
            source.base = trim->basis().get();
            continue;
        }
        if constexpr (N == 3) {
            if (const auto *image = dynamic_cast<const TransformedCurve *>(source.base)) {
                if (source.links == 32) return false;
                ++source.links;
                const auto &placement = image->transform();
                if (!isFinite(placement.translationPart())) return false;
                for (int row = 0; row < 3; ++row)
                    for (int column = 0; column < 3; ++column)
                        if (!std::isfinite(placement.matrix(row, column))) return false;
                source.placements[source.count++] = &placement;
                source.base = image->basis().get();
                continue;
            }
        }
        return true;
    }
    return false;
}
// Center the OUTERMOST affine expression before its sums. The rounded anchor
// is only a common origin, never a replacement for original E coefficients.
bool centeredEdgePoint(const CurveSource<3> &source, const Point<3> &origin, Point<3> &point) {
    for (std::size_t level = source.count; level > 0; --level) {
        cancellationPoint();
        const auto &placement = *source.placements[level - 1];
        Point<3> transformed;
        for (int row = 0; row < 3; ++row) {
            I value(placement.translationPart()[row]);
            if (level == 1) value = value - origin[std::size_t(row)];
            for (int column = 0; column < 3; ++column)
                value = value + I(placement.matrix(row, column)) * point[std::size_t(column)];
            if (!finite(value)) return false;
            transformed[std::size_t(row)] = value;
        }
        point = transformed;
    }
    return true;
}
// Read placement links BEFORE any recursive knot/weight/pole accessor.
// The public constructor already canonicalizes to one native base. This
// bounded walk also fails closed if a future representation adds links.
struct CarrierSource {
    const BSplineSurface *base = nullptr;
    std::array<const Transform3 *, 32> placements{};
    std::size_t count = 0;
};
bool carrierSource(const BSplineSurface &surface, CarrierSource &source) {
    source.base = &surface;
    while (source.base->basis()) {
        cancellationPoint();
        if (source.count == source.placements.size()) return false;
        const auto &placement = source.base->placement();
        if (!isFinite(placement.translationPart())) return false;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (!std::isfinite(placement.matrix(i, j))) return false;
        source.placements[source.count++] = &placement;
        source.base = source.base->basis().get();
    }
    return true;
}
bool centeredCarrierPole(const CarrierSource &source, int u, int v,
                         const Point<3> &origin, Point<3> &point) {
    const auto original = source.base->pole(u, v);
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(original[axis])) return false;
        point[std::size_t(axis)] = I(original[axis]);
    }
    // Original IEEE matrices and base coefficients, NOT the rounded public
    // pole() image. No double matrix composition or similarity-scale bound.
    for (std::size_t level = source.count; level > 0; --level) {
        cancellationPoint();
        const auto &placement = *source.placements[level - 1];
        Point<3> transformed;
        for (int row = 0; row < 3; ++row) {
            I value(placement.translationPart()[row]);
            // Center the outermost affine expression before its sums. This
            // is exactly M*P+(t-origin), with outward rounding throughout.
            if (level == 1) value = value - origin[std::size_t(row)];
            for (int column = 0; column < 3; ++column)
                value = value + I(placement.matrix(row, column)) * point[std::size_t(column)];
            if (!finite(value)) return false;
            transformed[std::size_t(row)] = value;
        }
        point = transformed;
    }
    if (!source.count)
        for (int axis = 0; axis < 3; ++axis)
            point[std::size_t(axis)] = point[std::size_t(axis)] - origin[std::size_t(axis)];
    return std::all_of(point.begin(), point.end(), [](I value) { return finite(value); });
}
}

CurvePtr<2> surfaceCurvePCurveCandidate(const CurvePtr<3> &curve, const SurfacePtr &surface,
                                         const CurvePtr<2> &original, const Interval &range) {
    cancellationPoint();
    if (!range.isFinite() || !(range.lo < range.hi) || !std::isfinite(range.length())) return original;
    CurveSource<3> edgeSource;
    CurveSource<2> uvSource;
    // Do not let an unwrapped/clamped surrogate bypass ORIGINAL trim limits.
    // Invalid wrapper data stays original, so the one residual proof fails.
    if (!curveSource(curve.get(), range, edgeSource) || !curveSource(original.get(), range, uvSource)) return original;
    const auto *e = dynamic_cast<const BSplineCurve<3> *>(edgeSource.base);
    const auto *s = dynamic_cast<const BSplineSurface *>(surface.get());
    const auto *q = dynamic_cast<const BSplineCurve<2> *>(uvSource.base);
    const auto *line = dynamic_cast<const Line<2> *>(uvSource.base);
    if (!e || !s || (!q && !line) || e->degree() != 3
        || (q && q->degree() != 1 && q->degree() != 3)
        || (s->uDegree() != 2 && s->uDegree() != 3) || (s->vDegree() != 2 && s->vDegree() != 3)
        || s->isUPeriodic() || s->isVPeriodic()) return original;
    CarrierSource source;
    if (!carrierSource(*s, source)) return original;
    const auto *base = source.base;
    const int ne = e->poleCount(), nq = q ? q->poleCount() : 0;
    const int nu = base->uPoleCount(), nv = base->vPoleCount();
    // Bound setup BEFORE scans or copies, including a fourth carrier scalar.
    constexpr std::size_t maxScalars = 2000000;
    if (ne < 4 || ne > 100000 || (q && (nq <= q->degree() || nq > 100000))
        || nu <= s->uDegree() || nv <= s->vDegree()
        || std::size_t(nu) > maxScalars / (4 * std::size_t(nv))
        || 4 * std::size_t(nu) * std::size_t(nv) + 16 * std::size_t(ne + nq) > maxScalars
        || !range.isFinite() || !(range.lo < range.hi)
        || !std::isfinite(range.length())) return original;
    if (range.lo < e->domain().lo || range.hi > e->domain().hi
        || (q && (range.lo < q->domain().lo || range.hi > q->domain().hi))) return original;
    if (weights(ne, [&](int i) { return e->weight(i); }) < 0
        || (q && weights(nq, [&](int i) { return q->weight(i); }) < 0)) return original;
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            cancellationPoint();
            const double weight = base->weight(i, j);
            if (!(weight > 0) || !std::isfinite(weight)) return original;
        }
    const Interval domain[] = {base->uDomain(), base->vDomain()};
    for (const auto &range : domain)
        if (!range.isFinite() || !(range.lo < range.hi)) return original;
    const auto bounded = [&](Vec2 value) {
        for (int axis = 0; axis < 2; ++axis) {
            if (!std::isfinite(value[axis])) throw std::domain_error("SP-curve: candidato UV non finito");
            value[axis] = std::clamp(value[axis], domain[axis].lo, domain[axis].hi);
        }
        return value;
    };
    if (line) {
        // Always build endpoint Bernstein data: rounded endpoint containment
        // alone does not prove the exact affine expression o+d*t is inside.
        // Line would normalize the new velocity, losing the edge parameter.
        const auto first = bounded(line->origin() + line->direction() * range.lo);
        const auto last = bounded(line->origin() + line->direction() * range.hi);
        return std::make_shared<BSplineCurve<2>>(1,
            std::vector<double>{range.lo, range.lo, range.hi, range.hi},
            std::vector<Vec2>{first, last});
    }
    bool changed = false;
    for (const auto &pole : q->poles()) {
        cancellationPoint();
        const auto candidate = bounded(pole);
        changed = changed || candidate[0] != pole[0] || candidate[1] != pole[1];
    }
    if (!changed) return original;
    std::vector<Vec2> poles;
    poles.reserve(std::size_t(nq));
    for (const auto &pole : q->poles()) {
        cancellationPoint();
        poles.push_back(bounded(pole));
    }
    const auto candidate = std::make_shared<BSplineCurve<2>>(q->degree(), q->knots(), std::move(poles), q->weights());
    if (uvSource.links) return std::make_shared<TrimmedCurve<2>>(candidate, range.lo, range.hi);
    return candidate;
}
SurfaceCurveBoundResult surfaceCurveResidualBound(const CurvePtr<3> &edge, const SurfacePtr &surface,
                                                  const CurvePtr<2> &pcurve, const Interval &range,
                                                  double budget) {
    cancellationPoint();
    const SurfaceCurveBoundResult failed;
    const SurfaceCurveBoundResult unsupported{SurfaceCurveBoundStatus::Unsupported, infinity};
    if (!edge || !surface || !pcurve || !range.isFinite() || !(range.lo < range.hi)
        || !std::isfinite(range.length()) || !(budget > 0) || !std::isfinite(budget)) return failed;
    CurveSource<3> edgeSource;
    CurveSource<2> uvSource;
    if (!curveSource(edge.get(), range, edgeSource) || !curveSource(pcurve.get(), range, uvSource)) return failed;
    const auto *e = dynamic_cast<const BSplineCurve<3> *>(edgeSource.base);
    const auto *q = dynamic_cast<const BSplineCurve<2> *>(uvSource.base);
    const auto *line = dynamic_cast<const Line<2> *>(uvSource.base);
    const auto *s = dynamic_cast<const BSplineSurface *>(surface.get());
    if (!e || !s || (!q && !line) || e->degree() != 3 || (q && q->degree() != 1 && q->degree() != 3)
        || (s->uDegree() != 2 && s->uDegree() != 3) || (s->vDegree() != 2 && s->vDegree() != 3) || s->isUPeriodic() || s->isVPeriodic()) return unsupported;
    CarrierSource source;
    if (!carrierSource(*s, source)) return failed;
    const auto *base = source.base;
    const int ne = e->poleCount(), nq = q ? q->poleCount() : 0, nu = base->uPoleCount(), nv = base->vPoleCount();
    if (ne < 4 || (q && nq <= q->degree()) || nu <= base->uDegree() || nv <= base->vDegree() || ne > 100000 || nq > 100000
        || std::size_t(nu) > maxScalars / (3 * std::size_t(nv))
        || 3 * std::size_t(nu) * std::size_t(nv) + 16 * std::size_t(ne + nq) > maxScalars) return failed;
    if (!validKnots(e->knots(), 3, ne) || !validKnots(base->uKnots(), base->uDegree(), nu) || !validKnots(base->vKnots(), base->vDegree(), nv)
        || (q && !validKnots(q->knots(), q->degree(), nq)) || !finitePoles(*e) || (q && !finitePoles(*q))) return failed;
    if (range.lo < e->domain().lo || range.hi > e->domain().hi
        || (q && (range.lo < q->domain().lo || range.hi > q->domain().hi))) return failed;
    const int ew = weights(ne, [&](int i) { return e->weight(i); });
    const int qw = q ? weights(nq, [&](int i) { return q->weight(i); }) : 1;
    const int sw = weights(nu * nv, [&](int i) { return base->weight(i / nv, i % nv); });
    if (ew < 0 || qw < 0 || sw < 0) return failed;
    // A rational E adds Source<1>: two interval scalars and one copied knot
    // per pole, plus four additional cubic knots and its two hull scalars.
    // Charge all additional storage BEFORE any snapshot allocation.
    const std::size_t edgeWeightScalars = ew ? 0 : 3 * std::size_t(ne) + 6;
    const std::size_t uvWeightScalars = qw ? 0 : 3 * std::size_t(nq) + std::size_t(q->degree()) + 3;
    const std::size_t carrierScalars = (sw ? 3 : 4) * std::size_t(nu) * std::size_t(nv);
    if (carrierScalars + 16 * std::size_t(ne + nq) + edgeWeightScalars + uvWeightScalars > maxScalars) return failed;
    // Polynomial partition of unity allows one ORIGINAL translation to be
    // subtracted from both E and S, retaining small residuals at large poses.
    auto anchor = e->poles().front();
    for (std::size_t level = edgeSource.count; level > 0; --level) {
        cancellationPoint();
        anchor = edgeSource.placements[level - 1]->applyToPoint(anchor);
        if (!isFinite(anchor)) return failed;
    }
    Point<3> origin{I(anchor[0]), I(anchor[1]), I(anchor[2])};
    auto edgeData = snapshot<3>(*e, edgeSource.count ? Point<3>{I(0), I(0), I(0)} : origin);
    if (edgeSource.count)
        for (auto &point : edgeData.poles)
            if (!centeredEdgePoint(edgeSource, origin, point)) return failed;
    Source<1> edgeWeightData;
    double minimumEdgeWeight = infinity;
    if (!ew) {
        edgeWeightData.degree = 3;
        edgeWeightData.knots = e->knots();
        edgeWeightData.poles.reserve(std::size_t(ne));
        for (int i = 0; i < ne; ++i) {
            cancellationPoint();
            const double weight = e->weight(i);
            minimumEdgeWeight = std::min(minimumEdgeWeight, weight);
            edgeWeightData.poles.push_back(Point<1>{I(weight)});
            // Original Euclidean pole minus the SAME anchor, then its
            // original weight. No division or rounded homogeneous conversion.
            for (I &coordinate : edgeData.poles[std::size_t(i)]) coordinate = coordinate * I(weight);
        }
    }
    auto uvData = q ? snapshot<2>(*q, Point<2>{I(0), I(0)}) : Source<2>{};
    Source<1> uvWeightData;
    double minimumUVWeight = infinity;
    if (!qw) {
        uvWeightData.degree = q->degree();
        uvWeightData.knots = q->knots();
        uvWeightData.poles.reserve(std::size_t(nq));
        for (int i = 0; i < nq; ++i) {
            cancellationPoint();
            const double weight = q->weight(i);
            minimumUVWeight = std::min(minimumUVWeight, weight);
            uvWeightData.poles.push_back(Point<1>{I(weight)});
            // Keep originalHull Euclidean: its positive-weight convex hull
            // is an independent enclosure of the RATIONAL UV image.
            for (I &coordinate : uvData.poles[std::size_t(i)]) coordinate = coordinate * I(weight);
        }
    }
    Carrier carrier{base->uKnots(), base->vKnots(), nu, nv, base->uDegree(), base->vDegree(), {}, {}, infinity};
    if (!sw) carrier.weights.reserve(std::size_t(nu) * std::size_t(nv));
    carrier.poles.reserve(std::size_t(nu) * std::size_t(nv));
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            cancellationPoint();
            Point<3> centered;
            if (!centeredCarrierPole(source, i, j, origin, centered)) return failed;
            if (!sw) {
                const double weight = base->weight(i, j);
                carrier.minimumWeight = std::min(carrier.minimumWeight, weight);
                carrier.weights.push_back(I(weight));
                for (I &coordinate : centered) coordinate = coordinate * I(weight);
            }
            carrier.poles.push_back(centered);
        }
    std::array<I, 2> uvHull;
    if (q) uvHull = uvData.originalHull;
    else {
        const auto o = line->origin(), d = line->direction();
        for (int axis = 0; axis < 2; ++axis) {
            if (!std::isfinite(o[axis]) || !std::isfinite(d[axis])) return failed;
            uvHull[std::size_t(axis)] = hull(P{I(o[axis]) + I(d[axis]) * I(range.lo), I(o[axis]) + I(d[axis]) * I(range.hi)});
        }
    }
    std::vector<double> cuts{range.lo, range.hi};
    const auto &edgeKnots = std::as_const(edgeData.knots);
    const auto &uvKnots = std::as_const(uvData.knots);
    for (const auto *knots : {&edgeKnots, &uvKnots})
        for (double value : *knots) if (value > range.lo && value < range.hi) cuts.push_back(value);
    std::sort(cuts.begin(), cuts.end()); cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    if (cuts.size() > maxVisits) return failed;
    struct Node { Interval range; int depth; };
    Work work; double upper = 0;
    try {
        for (std::size_t cut = 1; cut < cuts.size(); ++cut) {
            std::vector<Node> stack{{{cuts[cut - 1], cuts[cut]}, 0}};
            while (!stack.empty()) {
                work.visit();
                const Node node = stack.back(); stack.pop_back();
                const auto actual = onRange(edgeData, node.range);
                P edgeDenominator;
                double edgeLower = 1;
                if (!ew) {
                    edgeDenominator = onRange(edgeWeightData, node.range)[0];
                    const I bounds = hull(edgeDenominator);
                    if (!finite(bounds)) return failed;
                    // This entire node is inside E's original natural domain:
                    // its positive B-spline basis gives W_E >= min(original w).
                    edgeLower = std::max(minimumEdgeWeight, bounds.lo);
                    if (!(edgeLower > 0) || !std::isfinite(edgeLower)) return failed;
                }
                VP<2> uv;
                if (q) uv = onRange(uvData, node.range);
                else {
                    const auto o = line->origin(), d = line->direction();
                    for (int axis = 0; axis < 2; ++axis)
                        uv[std::size_t(axis)] = {I(o[axis]) + I(d[axis]) * I(node.range.lo), I(o[axis]) + I(d[axis]) * I(node.range.hi)};
                }
                P uvDenominator;
                I uvDenominatorHull;
                double uvLower = 1;
                if (!qw) {
                    uvDenominator = onRange(uvWeightData, node.range)[0];
                    uvDenominatorHull = hull(uvDenominator);
                    uvLower = std::max(minimumUVWeight, uvDenominatorHull.lo);
                    if (!finite(uvDenominatorHull) || !(uvLower > 0)
                        || uvLower > uvDenominatorHull.hi || !std::isfinite(uvLower)) return failed;
                }
                std::array<I, 2> image;
                bool inDomain = true;
                const Interval domains[] = {base->uDomain(), base->vDomain()};
                for (int axis = 0; axis < 2; ++axis) {
                    image[std::size_t(axis)] = qw ? hull(uv[std::size_t(axis)])
                        : hull(uv[std::size_t(axis)]) / I(uvLower, uvDenominatorHull.hi);
                    // Intersection with the exact ORIGINAL pole hull is a
                    // second independent enclosure, not a clamp to the domain.
                    auto &value = image[std::size_t(axis)];
                    value.lo = std::max(value.lo, uvHull[std::size_t(axis)].lo);
                    value.hi = std::min(value.hi, uvHull[std::size_t(axis)].hi);
                    inDomain = inDomain && finite(value) && value.lo >= domains[axis].lo && value.hi <= domains[axis].hi;
                }
                bool passed = inDomain; double local = 0;
                if (inDomain) {
                    const auto us = cells(carrier.u, nu, image[0], carrier.pu), vs = cells(carrier.v, nv, image[1], carrier.pv);
                    passed = !us.empty() && !vs.empty() && us.size() <= maxCellsPerNode / vs.size();
                    if (passed) {
                        for (int ku : us) {
                            for (int kv : vs) {
                                work.visit();
                                const double bound = !qw
                                    ? rationalUVResidual(carrier, ku, kv, uv, uvDenominator, uvLower,
                                        actual, ew ? nullptr : &edgeDenominator, edgeLower)
                                    : (ew ? residual(carrier, ku, kv, uv, actual)
                                        : residual(carrier, ku, kv, uv, actual, &edgeDenominator, edgeLower));
                                local = std::max(local, bound);
                                if (!std::isfinite(bound) || bound > budget) { passed = false; break; }
                            }
                            if (!passed) break;
                        }
                    }
                }
                if (passed) { upper = std::max(upper, local); continue; }
                const double mid = node.range.lo + 0.5 * (node.range.hi - node.range.lo);
                if (node.depth >= maxDepth || !(node.range.lo < mid && mid < node.range.hi)) return failed;
                stack.push_back({{mid, node.range.hi}, node.depth + 1});
                stack.push_back({{node.range.lo, mid}, node.depth + 1});
            }
        }
    } catch (const ProofFailed &) { return failed; }
    return {SurfaceCurveBoundStatus::Certified, upper};
}
}
