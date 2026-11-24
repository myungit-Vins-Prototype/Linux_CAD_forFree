#pragma once
#include "fk_cancel.h"
#include "fk_math.h"
#include <cstdint>
#include <optional>
#include <utility>

namespace ForgeCad::Kernel::detail {
// Requires outward-rounded interval arithmetic. A wide or unresolved
// phase has no bound here; the caller can subdivide or reject it.
template<class I>
std::optional<std::pair<I, I>> boundedSineCosine(I angle) {
    if (!std::isfinite(angle.lo) || !std::isfinite(angle.hi) || angle.lo > angle.hi)
        return std::nullopt;
    const double center = 0.5 * angle.lo + 0.5 * angle.hi;
    const double quarter = std::nearbyint(center / kHalfPi);
    if (!std::isfinite(quarter) || std::abs(quarter) > 0x1p50)
        return std::nullopt;
    // Exact binary bounds for mathematical pi/2. Machin's identity gives
    // 8*atan(1/5)-2*atan(1/239); alternating sums of 24 and 8 terms lie
    // strictly between these endpoints (checked independently with Fraction).
    const I halfPi(0x1.921fb54442d18p+0, 0x1.921fb54442d19p+0);
    const I r = angle - I(quarter) * halfPi;
    if (!(r.lo >= -1. && r.hi <= 1.))
        return std::nullopt;
    const I minusSquare = I(0) - r * r;
    I sine, cosine, sineTerm = r, cosineTerm(1);
    for (int k = 0; k < 16; ++k) {
        cancellationPoint();
        sine = sine + sineTerm;
        cosine = cosine + cosineTerm;
        sineTerm = sineTerm * minusSquare / I(double((2 * k + 2) * (2 * k + 3)));
        cosineTerm = cosineTerm * minusSquare / I(double((2 * k + 1) * (2 * k + 2)));
    }
    // Degree 31/30 Taylor remainders on |r|<=1 are bounded by 1/32!,
    // strictly less than the exact binary padding 2^-100 used here.
    const I tail(-0x1p-100, 0x1p-100);
    sine = sine + tail;
    cosine = cosine + tail;
    const auto q = static_cast<std::int64_t>(quarter);
    switch ((q % 4 + 4) % 4) {
    case 0:
        return std::pair<I, I>{sine, cosine};
    case 1:
        return std::pair<I, I>{cosine, I(0) - sine};
    case 2:
        return std::pair<I, I>{I(0) - sine, I(0) - cosine};
    default:
        return std::pair<I, I>{I(0) - cosine, sine};
    }
}
}
