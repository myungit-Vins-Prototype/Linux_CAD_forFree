#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace ForgeCad::Kernel::detail::surface_bounds {
// The certificate translation units compile these operations without
// fast-math or FP contraction. Each operation encloses its own roundoff.
inline constexpr double infinity = std::numeric_limits<double>::infinity();
struct I {
    double lo = 0, hi = 0;
    I() = default;
    explicit I(double value) : lo(value), hi(value) {}
    I(double first, double last) : lo(first), hi(last) {}
};
inline bool finite(I value) {
    return std::isfinite(value.lo) && std::isfinite(value.hi) && value.lo <= value.hi;
}
inline bool exact(I value, double number) { return value.lo == number && value.hi == number; }
inline I whole() { return {-infinity, infinity}; }
inline I outward(double lo, double hi) {
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi) return whole();
    return {std::nextafter(lo, -infinity), std::nextafter(hi, infinity)};
}
inline I operator+(I a, I b) {
    if (!finite(a) || !finite(b)) return whole();
    if (exact(a, 0)) return b;
    if (exact(b, 0)) return a;
    if (a.lo == a.hi && b.lo == b.hi && a.lo == -b.lo) return I(0);
    return outward(a.lo + b.lo, a.hi + b.hi);
}
inline I operator-(I a, I b) {
    if (!finite(a) || !finite(b)) return whole();
    if (exact(b, 0)) return a;
    if (a.lo == a.hi && b.lo == b.hi && a.lo == b.lo) return I(0);
    if (exact(a, 0)) return {-b.hi, -b.lo};
    return outward(a.lo - b.hi, a.hi - b.lo);
}
inline I operator*(I a, I b) {
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
inline I operator/(I a, I b) {
    if (!finite(a) || !finite(b) || !(b.lo > 0)) return whole();
    if (exact(a, 0)) return I(0);
    if (exact(b, 1)) return a;
    const double quotients[] = {a.lo / b.lo, a.lo / b.hi, a.hi / b.lo, a.hi / b.hi};
    for (double value : quotients) if (!std::isfinite(value)) return whole();
    return outward(*std::min_element(quotients, quotients + 4), *std::max_element(quotients, quotients + 4));
}
using P = std::vector<I>;

inline P elevated(P values, std::size_t degree) {
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
inline P sum(P a, P b, bool subtract = false) {
    const auto degree = std::max(a.size(), b.size()) - 1;
    a = elevated(std::move(a), degree); b = elevated(std::move(b), degree);
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = subtract ? a[i] - b[i] : a[i] + b[i];
    return a;
}
inline P scaled(P values, I factor) {
    for (I &value : values) value = value * factor;
    return values;
}
// Binomial coefficients through degree 21 fit exactly in double.
// Enclose the Bernstein product factor with interval arithmetic.
inline double choose(int n, int k) {
    std::uint64_t value = 1;
    for (int i = 1; i <= std::min(k, n - k); ++i) value = value * std::uint64_t(n - i + 1) / std::uint64_t(i);
    return double(value);
}
inline I bernsteinProductFactor(int m, int n, int i, int j) {
    return I(choose(m, i)) * I(choose(n, j)) / I(choose(m + n, i + j));
}
template<int MaxDegree, class ProofFailed>
P product(const P &a, const P &b) {
    const int m = int(a.size()) - 1, n = int(b.size()) - 1;
    static_assert(MaxDegree <= 21);
    if (m + n > MaxDegree) throw ProofFailed{};
    P values(std::size_t(m + n + 1));
    for (int i = 0; i <= m; ++i)
        for (int j = 0; j <= n; ++j) {
            const I factor = bernsteinProductFactor(m, n, i, j);
            values[std::size_t(i + j)] = values[std::size_t(i + j)] + a[std::size_t(i)] * b[std::size_t(j)] * factor;
        }
    return values;
}
inline I hull(const P &values) {
    I result(infinity, -infinity);
    for (I value : values) {
        if (!finite(value)) return whole();
        result.lo = std::min(result.lo, value.lo); result.hi = std::max(result.hi, value.hi);
    }
    return result;
}
} // namespace ForgeCad::Kernel::detail::surface_bounds
