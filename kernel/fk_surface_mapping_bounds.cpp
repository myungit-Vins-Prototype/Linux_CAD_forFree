#include "fk_surface_mapping_bounds.h"
#include "fk_surface_bound_math.h"
#include "fk_angular_bounds.h"
#include "fk_bspline_surface.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <type_traits>
#include <vector>

namespace ForgeCad::Kernel::detail {
namespace {
constexpr std::size_t maxPoles = 100000;
constexpr std::size_t maxVisits = 16384;
constexpr std::size_t maxCells = 256;
constexpr int maxDepth = 24;
constexpr int maxPolynomialDegree = 20; // per source parameter, including products
struct ProofFailed {};
struct Unsupported {};
struct Work {
    std::size_t left = maxVisits;
    void visit(std::size_t cost = 1) {
        cancellationPoint();
        if (cost > left) throw ProofFailed{};
        left -= cost;
    }
};
using surface_bounds::I;
using surface_bounds::P;
using surface_bounds::infinity;
using surface_bounds::scaled;
using surface_bounds::sum;
using surface_bounds::elevated;
using surface_bounds::hull;
using surface_bounds::outward;

P product(const P &a, const P &b) {
    return surface_bounds::product<maxPolynomialDegree, ProofFailed>(a, b);
}

template<class Q>
std::vector<Q> basis(const std::vector<double> &knots, int cell, const Q &q, int order, const Q *weight = nullptr) {
    const auto knot = [&](double value) { return weight ? scaled(*weight, I(value)) : Q{I(value)}; };
    std::vector<Q> previous(1, Q{I(1)});
    for (int degree = 1; degree <= order; ++degree) {
        std::vector<Q> current(std::size_t(degree + 1), Q{I(0)});
        for (int r = 0; r <= degree; ++r) {
            const int i = cell - degree + r;
            const double a = knots[std::size_t(i)], b = knots[std::size_t(i + degree)];
            const double c = knots[std::size_t(i + 1)], d = knots[std::size_t(i + degree + 1)];
            if (r && a < b)
                current[std::size_t(r)] = sum(current[std::size_t(r)], product(previous[std::size_t(r - 1)],
                    scaled(sum(q, knot(a), true), I(1) / (I(b) - I(a)))));
            if (r < degree && c < d)
                current[std::size_t(r)] = sum(current[std::size_t(r)], product(previous[std::size_t(r)],
                    scaled(sum(knot(d), q, true), I(1) / (I(d) - I(c)))));
        }
        previous = std::move(current);
    }
    return previous;
}

using IP = std::array<I, 3>;
IP point(const Vec3 &p) {
    IP out;
    for (int a = 0; a < 3; ++a) {
        if (!std::isfinite(p[a])) throw ProofFailed{};
        out[std::size_t(a)] = I(p[a]);
    }
    return out;
}
I scalar(const IP &a, const IP &b) {
    I value;
    for (int k = 0; k < 3; ++k) value = value + a[std::size_t(k)] * b[std::size_t(k)];
    return value;
}
struct Affine { I origin, du, dv; };
I at(const Affine &f, double u, double v) { return f.origin + f.du * I(u) + f.dv * I(v); }
// Exact zeros are required to establish the family; uncertain near-zero
// coefficients never become an angular/UV tolerance.
bool constant(const Affine &f) { return exact(f.du, 0) && exact(f.dv, 0); }
bool strictSign(I x) { return finite(x) && (x.lo > 0 || x.hi < 0); }
I range(const Affine &f, Interval u, Interval v) {
    return hull({at(f, u.lo, v.lo), at(f, u.lo, v.hi), at(f, u.hi, v.lo), at(f, u.hi, v.hi)});
}
enum class UVLayout { Direct, Swapped, DiagonalU, DiagonalV, Oblique };
bool diagonal(UVLayout layout) {
    return layout == UVLayout::DiagonalU || layout == UVLayout::DiagonalV;
}
struct SourceGraph {
    const BSplineSurface *surface;
    I zHeight;
    std::array<I, 2> zUV;
};
struct Mapping {
    std::array<Affine, 2> uv;
    Affine height;
    UVLayout layout;
    std::optional<SourceGraph> sourceGraph{};
};
bool smallTransverseSlope(I transverse, I main) {
    if (exact(transverse, 0)) return true;
    if (!strictSign(main)) return false;
    // This threshold chooses an enclosure representation. The transverse
    // coefficient still contributes to every Bernstein evaluation.
    return std::max(std::abs(transverse.lo), std::abs(transverse.hi))
        <= 8 * std::numeric_limits<double>::epsilon() * std::min(std::abs(main.lo), std::abs(main.hi));
}
Mapping mapping(const Plane &plane, const SurfaceWrapOptions &o, double su, double sv) {
    const auto &f = plane.frame(); const auto &g = o.sourcePlane;
    auto delta = point(f.origin()); const auto go = point(g.origin());
    for (int a = 0; a < 3; ++a) delta[std::size_t(a)] = delta[std::size_t(a)] - go[std::size_t(a)];
    const auto x = point(f.xDir()), y = point(f.yDir());
    const auto gx = point(g.xDir()), gy = point(g.yDir()), gz = point(g.zDir());
    const Affine height{scalar(delta, gz) + I(o.normalOffset), scalar(x, gz), scalar(y, gz)};
    if (!finite(height.origin) || !finite(height.du) || !finite(height.dv)) throw ProofFailed{};
    const bool constantHeight = constant(height);
    if (constantHeight && !exact(height.origin, 0) && !strictSign(height.origin)) throw ProofFailed{};
    const I c(std::cos(o.angle)), s(std::sin(o.angle));
    Affine a{I(o.anchor.x()) + (c * scalar(delta, gx) - s * scalar(delta, gy) + I(o.shift.x())) / I(su),
        (c * scalar(x, gx) - s * scalar(x, gy)) / I(su),
        (c * scalar(y, gx) - s * scalar(y, gy)) / I(su)};
    Affine b{I(o.anchor.y()) + (s * scalar(delta, gx) + c * scalar(delta, gy) + I(o.shift.y())) / I(sv),
        (s * scalar(x, gx) + c * scalar(x, gy)) / I(sv),
        (s * scalar(y, gx) + c * scalar(y, gy)) / I(sv)};
    for (auto f2 : {a, b}) if (!finite(f2.origin) || !finite(f2.du) || !finite(f2.dv)) throw ProofFailed{};
    const bool constantU = constant(a), constantV = constant(b);
    const bool normal = smallTransverseSlope(a.dv, a.du) && smallTransverseSlope(b.du, b.dv);
    const bool swapped = smallTransverseSlope(a.du, a.dv) && smallTransverseSlope(b.dv, b.du);
    if (constantU && constantV) throw Unsupported{};
    if (!normal && !swapped) {
        if (exact(a.dv, 0) && exact(b.dv, 0) && strictSign(a.du) && strictSign(b.du))
            return {{a, b}, height, UVLayout::DiagonalU};
        if (exact(a.du, 0) && exact(b.du, 0) && strictSign(a.dv) && strictSign(b.dv))
            return {{a, b}, height, UVLayout::DiagonalV};
        // Position uses the forward affine composition, which remains
        // defined when the UV determinant is zero or its sign is uncertain.
        // The natural target normal is checked independently.
        return {{a, b}, height, UVLayout::Oblique};
    }
    if (!constantHeight || constantU || constantV) {
        if ((!constantU && !strictSign(swapped ? a.dv : a.du))
            || (!constantV && !strictSign(swapped ? b.du : b.dv))) throw Unsupported{};
    }
    return {{a, b}, height, swapped ? UVLayout::Swapped : UVLayout::Direct};
}
bool knotsValid(const std::vector<double> &k, int p, int n) {
    if (n <= p || k.size() != std::size_t(n + p + 1)) return false;
    for (std::size_t i = 0; i < k.size(); ++i) {
        cancellationPoint();
        if (!std::isfinite(k[i]) || (i && k[i] < k[i - 1])) return false;
    }
    if (!(k[std::size_t(p)] < k[std::size_t(n)])) return false;
    for (int i = p + 1, repeat = 1; i < n; ++i) {
        repeat = k[std::size_t(i)] == k[std::size_t(i - 1)] ? repeat + 1 : 1;
        if (repeat > p) return false;
    }
    return true;
}
// Return true when the validated coefficients need a homogeneous denominator.
bool validate(const BSplineSurface &s, bool emitted) {
    if (s.basis()) throw Unsupported{}; // Require native coefficients before recursive accessors.
    if (emitted && (s.uDegree() != 3 || s.vDegree() != 3 || s.isUPeriodic() || s.isVPeriodic()))
        throw Unsupported{};
    if (s.uDegree() > maxPolynomialDegree || s.vDegree() > maxPolynomialDegree) throw ProofFailed{};
    const auto nu = s.uPoleCount(), nv = s.vPoleCount();
    if (nu <= 0 || nv <= 0 || std::size_t(nu) > maxPoles / std::size_t(nv)) throw ProofFailed{};
    if (!knotsValid(s.uKnots(), s.uDegree(), nu) || !knotsValid(s.vKnots(), s.vDegree(), nv)) throw ProofFailed{};
    const double w = s.weight(0, 0);
    if (!std::isfinite(w) || !(w > 0)) throw ProofFailed{};
    bool constant = true;
    for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) {
        cancellationPoint();
        const double wi = s.weight(i, j);
        if (!std::isfinite(wi) || !(wi > 0)) throw ProofFailed{};
        constant = constant && wi == w;
        point(s.pole(i, j));
    }
    if (emitted && !constant) throw Unsupported{};
    return !constant;
}
IP placedVector(const Transform3 &placement, const IP &vector, IP result = {}) {
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            result[row] = result[row] + I(placement.matrix(row, column)) * vector[column];
        if (!finite(result[row])) throw ProofFailed{};
    }
    return result;
}
struct TargetSurface {
    const BSplineSurface &base;
    const Transform3 *placement;
    IP nativeOrigin, worldOrigin;
    bool rational;
    // A projected graph can restrict the source box to one already selected
    // closed knot cell. Keep other cells from extrapolating over that box.
    std::optional<std::array<int, 2>> graphSpans;
    bool usesCone() const { return rational || base.uDegree() > 3 || base.vDegree() > 3; }
};
TargetSurface targetSurface(const BSplineSurface &s) {
    cancellationPoint();
    // BSplineSurface canonicalizes composed instances to one native base.
    // Validate that base before reading knots, weights or poles.
    const auto *placement = s.basis() ? &s.placement() : nullptr;
    const auto &base = placement ? *s.basis() : s;
    const bool rational = validate(base, false);
    const auto origin = point(base.pole(0, 0));
    if (!placement) return {base, nullptr, origin, origin, rational, {}};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            if (!std::isfinite(placement->matrix(row, column))) throw ProofFailed{};
    return {base, placement, origin, placedVector(*placement, origin, point(placement->translationPart())),
        rational, {}};
}
struct TargetCell {
    int span;
    I translation;
};
void appendCells(const std::vector<double> &k, int p, int n, Interval domain, I query,
                 I translation, std::vector<TargetCell> &out) {
    if (!finite(query)) throw ProofFailed{};
    // Clamp only for span selection. A periodic trim can expose a knot as
    // its low endpoint: findSpan selects the right span there, and wrap
    // cannot reach any parameter to its left.
    // A TrimmedCurve may extend just beyond its basis under the constructor's
    // endpoint tolerance. findSpan extrapolates the terminal polynomial there.
    const double firstKnot = k[std::size_t(p)], lastKnot = k[std::size_t(n)];
    const auto select = [&](double t) {
        return std::clamp(std::clamp(t, domain.lo, domain.hi), firstKnot, lastKnot);
    };
    const double lo = select(query.lo), hi = select(query.hi);
    // Begin on the left of the FIRST equal knot, preserving closed-cell
    // coverage at repeated knots; otherwise begin at the containing cell.
    const int first = std::max(p, int(std::lower_bound(k.begin() + p, k.begin() + n + 1, lo) - k.begin()) - 1);
    for (int i = first; i < n && k[std::size_t(i)] <= hi; ++i) {
        cancellationPoint();
        if (domain.lo < lastKnot && k[std::size_t(i + 1)] <= domain.lo) continue;
        if (k[std::size_t(i)] < k[std::size_t(i + 1)] && lo <= k[std::size_t(i + 1)]) {
            if (out.size() == maxCells) throw ProofFailed{};
            out.push_back({i, translation});
        }
    }
}
enum class ParameterWrap { None, Remainder, ClampedFloor };
template<class Visitor>
void forEachPeriodTranslation(I query, Interval domain, ParameterWrap wrap, Visitor visit) {
    if (domain.lo <= query.lo && query.hi <= domain.hi) {
        // Both evaluators preserve the closed native domain, including hi.
        visit(I(0));
        return;
    }
    const double period = domain.length();
    if (!(period > 0) || !std::isfinite(period)) throw ProofFailed{};
    const I delta = query - I(domain.lo);
    const I tiles = delta / I(period);
    constexpr double integerLimit = 0x1p52;
    if (!finite(tiles) || tiles.lo <= -integerLimit || tiles.hi >= integerLimit) throw ProofFailed{};
    double first = std::ceil(tiles.lo) - 1, last = std::floor(tiles.hi);
    if (query.lo <= domain.hi && domain.lo <= query.hi) {
        first = std::min(first, 0.);
        last = std::max(last, 0.);
    }
    if (last - first >= double(maxCells)) throw ProofFailed{};
    // The B-spline evaluator reduces fl(t-lo) by fmod, corrects negative
    // remainders and adds lo. The extrusion instead forms
    // k=floor(fl(fl(t-lo)/period)), then clamps fl(t-fl(k*period)).
    // In the latter case the rounded quotient is in [k,k+1), so the
    // exact t-k*period can exceed either endpoint only by the rounding
    // of the subtraction, division, and the stored period hi-lo.
    // Eight epsilons at this scale also cover the product and final
    // subtraction. Clamping cannot increase error to the nearest endpoint.
    const I scale = I(std::max(std::abs(delta.lo), std::abs(delta.hi))) + I(period)
        + I(std::max(std::abs(domain.lo), std::abs(domain.hi)));
    I error = scale * I(std::numeric_limits<double>::epsilon())
        + I(3 * std::numeric_limits<double>::denorm_min());
    if (wrap == ParameterWrap::ClampedFloor) error = error * I(8);
    if (!finite(error) || !(error.hi < period / 8)) throw ProofFailed{};
    for (double tile = first; tile <= last; ++tile) {
        cancellationPoint();
        const I translation = I(-tile) * I(period) + I(-error.hi, error.hi);
        visit(translation);
    }
}
std::vector<TargetCell> cells(const std::vector<double> &k, int p, int n, I query,
                              Interval domain, ParameterWrap wrap) {
    const Interval basisDomain{k[std::size_t(p)], k[std::size_t(n)]};
    if (!finite(query)) throw ProofFailed{};
    std::vector<TargetCell> out;
    if (wrap == ParameterWrap::None) {
        if (!domain.contains(query.lo, wrapTargetParameterSlack)
            || !domain.contains(query.hi, wrapTargetParameterSlack)
            || !basisDomain.contains(query.lo, wrapTargetParameterSlack)
            || !basisDomain.contains(query.hi, wrapTargetParameterSlack)) throw ProofFailed{};
        appendCells(k, p, n, basisDomain, query, I(0), out);
    } else {
        forEachPeriodTranslation(query, domain, wrap, [&](I translation) {
            appendCells(k, p, n, domain, query + translation, translation, out);
        });
    }
    if (out.empty()) throw ProofFailed{};
    return out;
}
// For bicubic targets C=Tu cross Tv has bidegree at most (5,5).
// Affine composition can combine both degrees on a source axis, so C dot C
// can reach degree 20 on either axis.
struct Polynomial {
    int u = 0, v = 0;
    std::vector<I> values{I(0)};
    Polynomial() = default;
    explicit Polynomial(I value) : values{value} {}
    Polynomial(int du, int dv) : u(du), v(dv), values(std::size_t((du + 1) * (dv + 1))) {
        if (du < 0 || dv < 0 || du > maxPolynomialDegree || dv > maxPolynomialDegree) throw ProofFailed{};
    }
    I &at(int i, int j) { return values[std::size_t(i * (v + 1) + j)]; }
    I at(int i, int j) const { return values[std::size_t(i * (v + 1) + j)]; }
};
template<std::size_t N> using PolynomialTuple = std::array<Polynomial, N>;
using VectorPolynomial = PolynomialTuple<3>;
Polynomial elevated(Polynomial p, int u, int v) {
    while (p.u < u) {
        cancellationPoint();
        Polynomial q(p.u + 1, p.v);
        for (int j = 0; j <= p.v; ++j) {
            q.at(0, j) = p.at(0, j); q.at(q.u, j) = p.at(p.u, j);
            for (int i = 1; i <= p.u; ++i) {
                const I t = I(double(i)) / I(double(q.u));
                q.at(i, j) = p.at(i - 1, j) * t + p.at(i, j) * (I(1) - t);
            }
        }
        p = std::move(q);
    }
    while (p.v < v) {
        cancellationPoint();
        Polynomial q(p.u, p.v + 1);
        for (int i = 0; i <= p.u; ++i) {
            q.at(i, 0) = p.at(i, 0); q.at(i, q.v) = p.at(i, p.v);
            for (int j = 1; j <= p.v; ++j) {
                const I t = I(double(j)) / I(double(q.v));
                q.at(i, j) = p.at(i, j - 1) * t + p.at(i, j) * (I(1) - t);
            }
        }
        p = std::move(q);
    }
    return p;
}
Polynomial sum(Polynomial a, Polynomial b, bool subtract = false) {
    const int u = std::max(a.u, b.u), v = std::max(a.v, b.v);
    a = elevated(std::move(a), u, v); b = elevated(std::move(b), u, v);
    for (std::size_t k = 0; k < a.values.size(); ++k)
        a.values[k] = subtract ? a.values[k] - b.values[k] : a.values[k] + b.values[k];
    return a;
}
Polynomial scaled(Polynomial p, I factor) {
    for (auto &value : p.values) value = value * factor;
    return p;
}
void placeVectors(VectorPolynomial &polynomial, const Transform3 *placement) {
    if (!placement) return;
    // All components from weightedSurface have the same Bernstein degrees.
    for (std::size_t k = 0; k < polynomial[0].values.size(); ++k) {
        cancellationPoint();
        const IP vector{polynomial[0].values[k], polynomial[1].values[k], polynomial[2].values[k]};
        const auto placed = placedVector(*placement, vector);
        for (std::size_t axis = 0; axis < placed.size(); ++axis)
            polynomial[axis].values[k] = placed[axis];
    }
}
Polynomial product(const Polynomial &a, const Polynomial &b) {
    if (!a.u && !a.v) return scaled(b, a.at(0, 0));
    if (!b.u && !b.v) return scaled(a, b.at(0, 0));
    Polynomial p(a.u + b.u, a.v + b.v);
    // Reuse the factors on the second axis across first-axis pairs.
    std::array<std::array<I, maxPolynomialDegree + 1>, maxPolynomialDegree + 1> factorsV;
    for (int j = 0; j <= a.v; ++j) for (int l = 0; l <= b.v; ++l)
        factorsV[j][l] = surface_bounds::bernsteinProductFactor(a.v, b.v, j, l);
    for (int i = 0; i <= a.u; ++i) for (int k = 0; k <= b.u; ++k) {
        cancellationPoint();
        const I fu = surface_bounds::bernsteinProductFactor(a.u, b.u, i, k);
        for (int j = 0; j <= a.v; ++j) for (int l = 0; l <= b.v; ++l) {
            const I fv = factorsV[j][l];
            p.at(i + k, j + l) = p.at(i + k, j + l) + a.at(i, j) * b.at(k, l) * fu * fv;
        }
    }
    return p;
}
Polynomial dot(const VectorPolynomial &a, const VectorPolynomial &b) {
    Polynomial out;
    for (std::size_t k = 0; k < 3; ++k) out = sum(std::move(out), product(a[k], b[k]));
    return out;
}
template<class Q, class Multiply>
std::array<Q, 3> polynomialCross(const std::array<Q, 3> &a, const std::array<Q, 3> &b, Multiply multiply) {
    std::array<Q, 3> out;
    for (int k = 0; k < 3; ++k) {
        const auto i = std::size_t((k + 1) % 3), j = std::size_t((k + 2) % 3);
        out[std::size_t(k)] = sum(multiply(a[i], b[j]), multiply(a[j], b[i]), true);
    }
    return out;
}
VectorPolynomial cross(const VectorPolynomial &a, const VectorPolynomial &b) {
    return polynomialCross(a, b, [](const Polynomial &x, const Polynomial &y) { return product(x, y); });
}
I hull(const Polynomial &p) { return hull(p.values); }
// With positive denominator coefficients the rational value lies in the
// convex hull of coefficient ratios in a common Bernstein basis.
I rationalHull(const Polynomial &numerator, const Polynomial &denominator) {
    const int u = std::max(numerator.u, denominator.u), v = std::max(numerator.v, denominator.v);
    const auto n = elevated(numerator, u, v), w = elevated(denominator, u, v);
    if (!(hull(w).lo > 0.)) return hull(numerator) / hull(denominator);
    I range(infinity, -infinity);
    for (std::size_t i = 0; i < n.values.size(); ++i) {
        const I value = n.values[i] / w.values[i];
        range.lo = std::min(range.lo, value.lo);
        range.hi = std::max(range.hi, value.hi);
    }
    return range;
}

double magnitude(I h) {
    if (!finite(h)) throw ProofFailed{};
    return std::max(std::abs(h.lo), std::abs(h.hi));
}
template<class Q> I squaredUpper(const std::array<Q, 3> &p) {
    I result;
    for (const auto &component : p) { const I m(magnitude(hull(component))); result = result + m * m; }
    if (!finite(result)) throw ProofFailed{};
    return result;
}
// Enclose the polynomial of each active target cell on the whole source
// rectangle. The caller retains all intersected closed cells.
template<std::size_t N = 3, class Pole>
PolynomialTuple<N> weightedSurface(const std::vector<P> &bu, const std::vector<P> &bv,
                                 UVLayout layout, Pole pole) {
    const int du = int(bu.front().size()) - 1, dv = int(bv.front().size()) - 1;
    const bool sharedAxis = diagonal(layout);
    const bool swapped = layout == UVLayout::Swapped;
    int sourceU = swapped ? dv : du, sourceV = swapped ? du : dv;
    if (sharedAxis) {
        sourceU = layout == UVLayout::DiagonalU ? du + dv : 0;
        sourceV = layout == UVLayout::DiagonalV ? du + dv : 0;
    }
    PolynomialTuple<N> out;
    for (auto &component : out) component = Polynomial(sourceU, sourceV);
    for (int i = 0; i < int(bu.size()); ++i) for (int j = 0; j < int(bv.size()); ++j) {
        cancellationPoint();
        const auto p = pole(i, j);
        const auto &u = bu[std::size_t(i)], &v = bv[std::size_t(j)];
        if (sharedAxis) {
            // Both bases use the same source parameter, even if their slopes
            // have opposite signs. Multiply them as univariate polynomials.
            const auto weights = product(u, v);
            for (std::size_t a = 0; a < p.size(); ++a)
                for (std::size_t k = 0; k < weights.size(); ++k)
                    out[a].values[k] = out[a].values[k] + p[a] * weights[k];
        } else {
            for (int x = 0; x <= du; ++x) for (int y = 0; y <= dv; ++y) {
                const I weight = u[std::size_t(x)] * v[std::size_t(y)];
                for (std::size_t a = 0; a < p.size(); ++a) {
                    auto &value = out[a].at(swapped ? y : x, swapped ? x : y);
                    value = value + p[a] * weight;
                }
            }
        }
    }
    return out;
}
template<std::size_t N = 3, class Pole>
PolynomialTuple<N> weightedSurface(const std::vector<Polynomial> &bu, const std::vector<Polynomial> &bv,
                                 UVLayout, Pole pole) {
    PolynomialTuple<N> out;
    for (int i = 0; i < int(bu.size()); ++i) for (int j = 0; j < int(bv.size()); ++j) {
        cancellationPoint();
        const auto weights = product(bu[std::size_t(i)], bv[std::size_t(j)]);
        const auto p = pole(i, j);
        for (std::size_t a = 0; a < p.size(); ++a)
            out[a] = sum(std::move(out[a]), scaled(weights, p[a]));
    }
    return out;
}
template<class Q>
VectorPolynomial surfaceOn(const BSplineSurface &s, int cu, int cv, const Q &u, const Q &v,
                           const IP &origin, UVLayout layout, const Q *weight = nullptr) {
    auto bu = basis(s.uKnots(), cu, u, s.uDegree(), weight);
    auto bv = basis(s.vKnots(), cv, v, s.vDegree(), weight);
    if constexpr (std::is_same_v<Q, P>) {
        if (!diagonal(layout)) {
            for (auto &b : bu) b = elevated(std::move(b), 3);
            for (auto &b : bv) b = elevated(std::move(b), 3);
        }
    }
    return weightedSurface(bu, bv, layout, [&](int i, int j) {
        auto p = point(s.pole(cu - s.uDegree() + i, cv - s.vDegree() + j));
        for (std::size_t a = 0; a < p.size(); ++a) p[a] = p[a] - origin[a];
        return p;
    });
}
template<class Q>
Polynomial graphZOn(const SourceGraph &graph, const Q &u, const Q &v, const Q *weight = nullptr) {
    const auto &s = *graph.surface;
    const auto bu = basis(s.uKnots(), s.uDegree(), u, s.uDegree(), weight);
    const auto bv = basis(s.vKnots(), s.vDegree(), v, s.vDegree(), weight);
    return weightedSurface<1>(bu, bv, UVLayout::Direct, [&](int i, int j) {
        return std::array<I, 1>{I(s.pole(i, j).z())};
    })[0];
}
// Derivative poles are differences of original target poles. These are the
// natural target derivatives, with no source-to-target rates in the cross product.
template<std::size_t N, class Q, class Pole>
PolynomialTuple<N> derivativeTensor(const BSplineSurface &s, int cu, int cv, const Q &u, const Q &v,
                                  bool alongU, UVLayout layout, Pole pole, const Q *weight = nullptr) {
    const int pu = s.uDegree(), pv = s.vDegree(), du = pu - int(alongU), dv = pv - int(!alongU);
    const auto bu = basis(s.uKnots(), cu, u, du, weight), bv = basis(s.vKnots(), cv, v, dv, weight);
    return weightedSurface<N>(bu, bv, layout, [&](int i, int j) {
        const int pi = cu - pu + i, pj = cv - pv + j;
        auto a = pole(pi, pj);
        const auto b = pole(pi + int(alongU), pj + int(!alongU));
        const auto &knots = alongU ? s.uKnots() : s.vKnots();
        const int degree = alongU ? pu : pv, first = alongU ? pi : pj;
        const I factor = I(double(degree)) / (I(knots[std::size_t(first + degree + 1)]) - I(knots[std::size_t(first + 1)]));
        if (!finite(factor)) throw ProofFailed{};
        for (std::size_t axis = 0; axis < a.size(); ++axis) a[axis] = (b[axis] - a[axis]) * factor;
        return a;
    });
}
struct Rectangle { Interval u, v; };
Polynomial affineOn(const Affine &f, Rectangle r) {
    // The corner values are the tensor Bernstein coefficients of an affine
    // function. Keep both source variables in every subsequent Cox product.
    Polynomial p(int(!exact(f.du, 0)), int(!exact(f.dv, 0)));
    for (int i = 0; i <= p.u; ++i)
        for (int j = 0; j <= p.v; ++j)
            p.at(i, j) = at(f, i ? r.u.hi : r.u.lo, j ? r.v.hi : r.v.lo);
    return p;
}
P affineAlong(const Affine &f, Rectangle r, bool alongU) {
    const Interval main = alongU ? r.u : r.v, other = alongU ? r.v : r.u;
    const I slope = alongU ? f.du : f.dv;
    const I transverse = (alongU ? f.dv : f.du) * I(other.lo, other.hi);
    // For each fixed transverse coordinate the same translation applies
    // to both coefficients. Their intervals enclose every such polynomial.
    return {f.origin + slope * I(main.lo) + transverse,
            f.origin + slope * I(main.hi) + transverse};
}

struct HeightPatch {
    I range;
    Polynomial polynomial; // h for the homogeneous and scaled bounds, h^2 for the signed bound.
};
double scaledNormalUpper(const VectorPolynomial &d, const VectorPolynomial &c,
                         const HeightPatch &height, double lowerC) {
    // For any positive reference length rho, replace |C|D by rho*D,
    // bounding the difference with (|C|²-rho²)/(|C|+rho). Take rho
    // from the squared norm polynomial: a rotating cylindrical normal
    // has constant length even though each of its components varies.
    const auto normSquared = dot(c, c);
    const I lengths = hull(normSquared);
    if (!finite(lengths)) throw ProofFailed{};
    const I rho(std::max(lowerC, std::sqrt(std::max(0., .5 * lengths.lo + .5 * lengths.hi))));
    Polynomial scale;
    scale.values[0] = rho;
    VectorPolynomial residual;
    for (std::size_t i = 0; i < residual.size(); ++i)
        residual[i] = sum(product(d[i], scale), product(c[i], height.polynomial), true);
    const double difference = std::nextafter(std::sqrt(squaredUpper(residual).hi), infinity);
    scale.values[0] = rho * rho;
    const I normSquaredError(magnitude(hull(sum(normSquared, scale, true))));
    const I normalError = normSquaredError / (I(lowerC) + rho);
    const I upper = (I(difference) + I(magnitude(height.range)) * normalError) / rho;
    if (!finite(upper)) throw ProofFailed{};
    return upper.hi;
}
I squareRoot(I value) {
    if (!finite(value) || value.hi < 0) throw ProofFailed{};
    return outward(std::sqrt(std::max(0.0, value.lo)), std::sqrt(value.hi));
}

template<class Q> Q projection(const std::array<Q, 3> &p, const IP &direction) {
    Q result{I(0)};
    for (std::size_t i = 0; i < p.size(); ++i)
        result = sum(std::move(result), scaled(p[i], direction[i]));
    return result;
}

bool unitDirection(const Vec3 &seed, IP &direction) {
    const double scale = std::max({std::abs(seed[0]), std::abs(seed[1]), std::abs(seed[2])});
    if (!(scale > 0) || !std::isfinite(scale)) return false;
    direction = point(seed / scale);
    const I length = squareRoot(scalar(direction, direction));
    for (I &x : direction) x = x / length;
    return true;
}

// The midpoints choose a reference direction only. Its normalization,
// orientation and angle to every natural normal are enclosed below.
bool normalCone(const VectorPolynomial &u, const VectorPolynomial &v, IP &direction, I &chord) {
    IP uh, vh, c;
    Vec3 um, vm;
    for (int i = 0; i < 3; ++i) {
        uh[i] = hull(u[i]); vh[i] = hull(v[i]);
        if (!finite(uh[i]) || !finite(vh[i])) throw ProofFailed{};
        um[i] = .5 * uh[i].lo + .5 * uh[i].hi;
        vm[i] = .5 * vh[i].lo + .5 * vh[i].hi;
    }
    Vec3 seed = cross(um, vm);
    if (!unitDirection(seed, direction)) return false;
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3, k = (i + 2) % 3;
        c[i] = uh[j] * vh[k] - uh[k] * vh[j];
    }
    // The common quotient denominator of both tangents cancels in this
    // relative regularity test. The projection also proves the orientation.
    const I forward = scalar(c, direction);
    const I uu = squaredUpper(u), vv = squaredUpper(v);
    const I threshold = I(angularResolution()) * I(std::max(uu.hi, vv.hi));
    if (!finite(forward) || !finite(threshold)) throw ProofFailed{};
    if (!(forward.lo > threshold.hi)) return false;
    // n0 x (u x v) = u*(n0.v) - v*(n0.u). Project the polynomials before
    // taking their hulls, retaining a constant normal on inclined planes.
    const I tangent = squareRoot(uu) * I(magnitude(hull(projection(v, direction))))
                    + squareRoot(vv) * I(magnitude(hull(projection(u, direction))));
    // In this hemisphere the unit-normal chord is at most sqrt(2)*sin(theta).
    chord = squareRoot(I(2)) * tangent / I(forward.lo);
    return finite(chord);
}

// W=1 for a polynomial target. The cone avoids squaring a high-degree
// cross product while bounding the same oriented normal offset.
double coneUpper(VectorPolynomial residual, const VectorPolynomial &su, const VectorPolynomial &sv,
                 const HeightPatch &height, const Polynomial &w, double lowerW,
                 const Polynomial *denominator = nullptr) {
    IP direction;
    I chord;
    if (!normalCone(su, sv, direction, chord)) return infinity;
    const auto hw = product(height.polynomial, w);
    for (int i = 0; i < 3; ++i) residual[i] = sum(std::move(residual[i]), scaled(hw, direction[i]), true);
    if (denominator) {
        I squared;
        for (const auto &component : residual) {
            const I e(magnitude(rationalHull(component, *denominator)));
            squared = squared + e * e;
        }
        return (squareRoot(squared) + I(magnitude(rationalHull(hw, *denominator))) * chord).hi;
    }
    const I bound = squareRoot(squaredUpper(residual)) / I(lowerW) + I(magnitude(height.range)) * chord;
    if (!finite(bound)) throw ProofFailed{};
    return bound.hi;
}

template<class Q, class Multiply>
double projectedNormalUpper(const std::array<Q, 3> &d, const std::array<Q, 3> &c,
                            I heightRange, const Q &height, double lowerC, Multiply multiply) {
    Vec3 seed;
    for (std::size_t i = 0; i < c.size(); ++i) {
        const I h = hull(c[i]);
        seed[i] = .5 * h.lo + .5 * h.hi;
    }
    IP direction;
    if (!unitDirection(seed, direction)) return infinity;
    const auto rho = projection(c, direction);
    const I rhoRange = hull(rho);
    if (!finite(rhoRange) || !(rhoRange.lo > 0)) return infinity;
    // With unit n0 and rho=C dot n0>0, |C|-rho equals
    // |C cross n0|^2 / (|C|+rho). Keeping rho polynomial captures
    // the changing normal scale without dividing by the signed height.
    const std::array<Q, 3> n{Q{direction[0]}, Q{direction[1]}, Q{direction[2]}};
    const I normalError = squaredUpper(polynomialCross(c, n, multiply)) / (I(lowerC) + I(rhoRange.lo));
    std::array<Q, 3> residual;
    for (std::size_t i = 0; i < residual.size(); ++i)
        residual[i] = sum(multiply(d[i], rho), multiply(c[i], height), true);
    const I upper = (squareRoot(squaredUpper(residual)) + I(magnitude(heightRange)) * normalError)
        / I(rhoRange.lo);
    if (!finite(upper)) throw ProofFailed{};
    return upper.hi;
}

double projectedNormalUpper(const VectorPolynomial &d, const VectorPolynomial &c,
                            const HeightPatch &height, double lowerC) {
    return projectedNormalUpper(d, c, height.range, height.polynomial, lowerC,
        [](const Polynomial &a, const Polynomial &b) { return product(a, b); });
}

template<class Q>
double regularNormalLower(const std::array<Q, 3> &su, const std::array<Q, 3> &sv, const std::array<Q, 3> &c) {
    I lowerSquared;
    for (const auto &component : c) {
        const I h = hull(component);
        if (!finite(h)) throw ProofFailed{};
        const double m = h.lo > 0 ? h.lo : (h.hi < 0 ? -h.hi : 0);
        lowerSquared = lowerSquared + I(m) * I(m);
    }
    if (!finite(lowerSquared)) throw ProofFailed{};
    const double lowerC = std::nextafter(std::sqrt(std::max(0.0, lowerSquared.lo)), -infinity);
    if (!(lowerC > 0)) return 0.;
    // Match the EXISTING relative regularity policy of Surface::normal,
    // using original-coefficient bounds rather than rounded evaluations.
    // This does not certify bit-for-bit behavior of that evaluator.
    const I threshold = I(angularResolution()) * I(std::max(squaredUpper(su).hi, squaredUpper(sv).hi));
    if (!finite(threshold)) throw ProofFailed{};
    return lowerC > threshold.hi ? lowerC : 0.;
}

double polynomialNormalUpper(const VectorPolynomial &d, const VectorPolynomial &su,
                             const VectorPolynomial &sv, const HeightPatch &height,
                             bool useScaledBound, UVLayout layout) {
    // Crossing the placed natural derivatives also accounts for reflections.
    const auto c = cross(su, sv);
    const double lowerC = regularNormalLower(su, sv, c);
    if (!(lowerC > 0.)) return infinity;
    if (useScaledBound) {
        // For polynomial patches within the signed bound's degree limits,
        // D and C have degrees at most 6 and 10 per source axis.
        // Two angular parameters can produce a normal whose square exceeds
        // the degree budget. The projected bound does not square it.
        const bool highDegree = 2 * std::max(c[0].u, c[0].v) > maxPolynomialDegree;
        if (diagonal(layout) || layout == UVLayout::Oblique || highDegree) {
            const double upper = projectedNormalUpper(d, c, height, lowerC);
            if (std::isfinite(upper) || highDegree) return upper;
        }
        return scaledNormalUpper(d, c, height, lowerC);
    }
    // A fixed sign of h reduces h(D dot C) >= 0 to a sign check on D dot C.
    const I orientation = hull(dot(d, c));
    if (!finite(orientation)) throw ProofFailed{};
    const bool positive = height.range.lo > 0;
    if (positive ? orientation.lo < 0 : orientation.hi > 0) return infinity;
    const I b2 = squaredUpper(cross(d, c)) / I(lowerC) / I(lowerC);
    const I r(magnitude(hull(sum(dot(d, d), height.polynomial, true))));
    const double lowerH = positive ? height.range.lo : -height.range.hi;
    const I normal = (r + b2) / I(lowerH);
    const I e2 = b2 + normal * normal;
    if (!finite(b2) || !finite(normal) || !finite(e2)) throw ProofFailed{};
    return std::nextafter(std::sqrt(e2.hi), infinity);
}
template<class Q>
double normalUpper(const VectorPolynomial &d, const TargetSurface &target, int tu, int tv,
                   const Q &uq, const Q &vq, UVLayout layout, const HeightPatch &height, bool useScaledBound) {
    const auto pole = [&](int i, int j) { return point(target.base.pole(i, j)); };
    auto su = derivativeTensor<3>(target.base, tu, tv, uq, vq, true, layout, pole);
    auto sv = derivativeTensor<3>(target.base, tu, tv, uq, vq, false, layout, pole);
    placeVectors(su, target.placement);
    placeVectors(sv, target.placement);
    if (target.usesCone()) return coneUpper(d, su, sv, height, Polynomial(I(1)), 1.);
    return polynomialNormalUpper(d, su, sv, height, useScaledBound, layout);
}
template<class Q>
double rationalUpper(const TargetSurface &target, const VectorPolynomial &actual, int cu, int cv,
                     const Q &uq, const Q &vq, UVLayout layout, const std::optional<HeightPatch> &offset) {
    const auto &s = target.base;
    // One common factor for position and natural derivatives on this span.
    // Weights belonging solely to distant spans cannot affect its scaling.
    double scale = 0;
    for (int i = cu - s.uDegree(); i <= cu; ++i)
        for (int j = cv - s.vDegree(); j <= cv; ++j)
            scale = std::max(scale, s.weight(i, j));
    const auto pole = [&](int i, int j) {
        const I w = I(s.weight(i, j)) / I(scale);
        const auto p = point(s.pole(i, j));
        return std::array<I, 4>{(p[0] - target.nativeOrigin[0]) * w, (p[1] - target.nativeOrigin[1]) * w,
                                (p[2] - target.nativeOrigin[2]) * w, w};
    };
    const auto bu = basis(s.uKnots(), cu, uq, s.uDegree()), bv = basis(s.vKnots(), cv, vq, s.vDegree());
    const auto h = weightedSurface<4>(bu, bv, layout, [&](int i, int j) {
        return pole(cu - s.uDegree() + i, cv - s.vDegree() + j);
    });
    const auto &w = h[3];
    const I denominator = hull(w);
    if (!finite(denominator)) throw ProofFailed{};
    // Adjacent spans are extended over this rectangle; W must be positive
    // there too, including the end-span extrapolation allowed by the mapper.
    if (!(denominator.lo > 0)) return infinity;
    VectorPolynomial numerator{h[0], h[1], h[2]};
    placeVectors(numerator, target.placement);
    VectorPolynomial residual;
    for (int i = 0; i < 3; ++i) residual[i] = sum(product(actual[i], w), numerator[i], true);
    if (offset) {
        const auto tangent = [&](bool alongU) {
            const auto d = derivativeTensor<4>(s, cu, cv, uq, vq, alongU, layout, pole);
            VectorPolynomial t;
            for (int i = 0; i < 3; ++i) t[i] = sum(product(w, d[i]), product(h[i], d[3]), true);
            placeVectors(t, target.placement);
            return t;
        };
        return coneUpper(std::move(residual), tangent(true), tangent(false), *offset, w, denominator.lo);
    }
    const I bound = squareRoot(squaredUpper(residual)) / I(denominator.lo);
    if (!finite(bound)) throw ProofFailed{};
    return bound.hi;
}
template<class Q>
std::optional<Q> translated(const Q &parameter, I translation) {
    if (exact(translation, 0)) return {};
    Q local = parameter;
    if constexpr (std::is_same_v<Q, P>) {
        for (I &value : local) value = value + translation;
    } else {
        for (I &value : local.values) value = value + translation;
    }
    return local;
}
double shortestSpan(const std::vector<TargetCell> &cells, const std::vector<double> &knots, Interval domain) {
    double width = infinity;
    for (const auto &cell : cells) {
        const double lo = std::max(domain.lo, knots[std::size_t(cell.span)]);
        const double hi = std::min(domain.hi, knots[std::size_t(cell.span + 1)]);
        // A trim endpoint can use the derivative of a span whose only
        // exposed parameter is that endpoint. Keep refining its active axis.
        width = std::min(width, std::max(0., hi - lo));
    }
    return width;
}
template<class Q>
double cellsUpper(const TargetSurface &target, const VectorPolynomial &actual, const Q &uq, const Q &vq,
                  UVLayout layout, const std::optional<HeightPatch> &offset,
                  bool useScaledBound, Work &work, std::array<double, 2> &periodicSpans) {
    // A closed source rectangle can touch either side of a repeated knot.
    // Query each coordinate independently, then check all candidate pairs.
    const auto &base = target.base;
    const auto uc = target.graphSpans ? std::vector<TargetCell>{{(*target.graphSpans)[0], I(0)}}
        : cells(base.uKnots(), base.uDegree(), base.uPoleCount(), hull(uq), base.uDomain(),
            base.isUPeriodic() ? ParameterWrap::Remainder : ParameterWrap::None);
    const auto vc = target.graphSpans ? std::vector<TargetCell>{{(*target.graphSpans)[1], I(0)}}
        : cells(base.vKnots(), base.vDegree(), base.vPoleCount(), hull(vq), base.vDomain(),
            base.isVPeriodic() ? ParameterWrap::Remainder : ParameterWrap::None);
    if (uc.size() > maxCells / vc.size()) throw ProofFailed{};
    if (layout != UVLayout::Oblique) {
        if (base.isUPeriodic()) periodicSpans[0] = shortestSpan(uc, base.uKnots(), base.uDomain());
        if (base.isVPeriodic()) periodicSpans[1] = shortestSpan(vc, base.vKnots(), base.vDomain());
    }
    const std::size_t support = std::size_t(base.uDegree() + 1) * (base.vDegree() + 1);
    const std::size_t cost = ((support + 15) / 16) * (layout == UVLayout::Oblique ? (offset ? 16 : 4) : 1);
    double upper = 0;
    for (const auto &cu : uc) for (const auto &cv : vc) {
        work.visit(target.rational ? 2 * cost : cost);
        const auto wrappedU = translated(uq, cu.translation), wrappedV = translated(vq, cv.translation);
        const auto &u = wrappedU ? *wrappedU : uq, &v = wrappedV ? *wrappedV : vq;
        const int tu = cu.span, tv = cv.span;
        if (target.rational) {
            upper = std::max(upper, rationalUpper(target, actual, tu, tv, u, v, layout, offset));
            continue;
        }
        // T - worldOrigin = M * (base - nativeOrigin). Keep the original
        // pole differences and avoid materializing rounded world poles.
        auto wanted = surfaceOn(base, tu, tv, u, v, target.nativeOrigin, layout);
        placeVectors(wanted, target.placement);
        VectorPolynomial residual;
        for (std::size_t a = 0; a < residual.size(); ++a)
            residual[a] = sum(actual[a], std::move(wanted[a]), true);
        if (offset)
            upper = std::max(upper, normalUpper(residual, target, tu, tv, u, v, layout, *offset, useScaledBound));
        else
            upper = std::max(upper, std::nextafter(std::sqrt(squaredUpper(residual).hi), infinity));
    }
    return upper;
}
struct ProfileBasis {
    const Curve<3> *curve = nullptr;
    std::optional<Interval> trim;
    std::vector<const Transform3 *> placements;
};

ProfileBasis profileBasis(const Curve<3> &curve) {
    ProfileBasis basis;
    basis.curve = &curve;
    for (;;) {
        cancellationPoint();
        if (const auto *trim = dynamic_cast<const TrimmedCurve<3> *>(basis.curve)) {
            if (!basis.trim) basis.trim = trim->domain();
            basis.curve = trim->basis().get();
        } else if (const auto *image = dynamic_cast<const TransformedCurve *>(basis.curve)) {
            if (basis.placements.size() == 32) throw ProofFailed{};
            basis.placements.push_back(&image->transform());
            basis.curve = image->basis().get();
        } else break;
    }
    // The evaluator applies the innermost placement first.
    std::reverse(basis.placements.begin(), basis.placements.end());
    return basis;
}

// The basis outlives every subdivision of this call to proveImage.
struct ProfilePlacement {
    IP nativeOrigin, worldOrigin;
    const std::vector<const Transform3 *> &placements;

    ProfilePlacement(const Vec3 &center, const ProfileBasis &basis)
        : nativeOrigin(point(center)), worldOrigin(nativeOrigin), placements(basis.placements) {
        for (const auto *placement : placements)
            worldOrigin = placedVector(*placement, worldOrigin, point(placement->translationPart()));
    }
};

struct ExtrudedProfile : ProfilePlacement {
    const BSplineCurve<3> &curve;
    IP direction;
    Interval domain;
    bool periodic;
    bool usesCone() const { return true; }
};

const BSplineCurve<3> &splineBasis(const ProfileBasis &profile) {
    const auto *basis = dynamic_cast<const BSplineCurve<3> *>(profile.curve);
    if (!basis) throw Unsupported{};
    if (basis->degree() > maxPolynomialDegree || basis->poles().size() > maxPoles ||
        !knotsValid(basis->knots(), basis->degree(), basis->poleCount())) throw ProofFailed{};
    return *basis;
}

ExtrudedProfile extrudedProfile(const ExtrusionSurface &surface, const ProfileBasis &profile) {
    const auto &basis = splineBasis(profile);
    return {{basis.poles().front(), profile}, basis, point(surface.direction()),
            surface.uDomain(), surface.isUPeriodic()};
}

// Numerator/weight is either the centered curve position or its derivative.
// Enclose each binary64 placement as evaluated by TransformedCurve. For example,
// adding a large translation and subtracting it in the next wrapper need not
// recover a small coordinate. Derivatives go through the linear parts only.
void placeProfile(VectorPolynomial &numerator, I weightRange,
                  const ProfilePlacement &target, bool position, Work &work) {
    IP origin = position ? target.nativeOrigin : IP{};
    for (const auto *placement : target.placements) {
        work.visit(numerator[0].values.size());
        const IP translation = position ? point(placement->translationPart()) : IP{};
        IP input, error;
        for (int i = 0; i < 3; ++i) {
            input[i] = hull(numerator[i]) / weightRange + origin[i];
            if (!finite(input[i])) throw ProofFailed{};
        }
        for (int row = 0; row < 3; ++row) {
            I scale(magnitude(translation[row]));
            int terms = 0;
            bool exactRow = exact(translation[row], 0.);
            for (int col = 0; col < 3; ++col) {
                const double factor = std::abs(placement->matrix(row, col));
                if (factor == 0.) continue;
                ++terms;
                exactRow = exactRow && factor == 1.;
                scale = scale + I(factor) * I(magnitude(input[col]));
            }
            if (terms == 0 || (terms == 1 && exactRow)) continue;
            // Three products, two sums and the optional translation. Eight
            // epsilons enclose their accumulated roundoff, including subnormals.
            const I radius = I(8.) * (scale * I(std::numeric_limits<double>::epsilon())
                + I(std::numeric_limits<double>::denorm_min()));
            if (!finite(scale + radius)) throw ProofFailed{};
            error[row] = {-radius.hi, radius.hi};
        }
        placeVectors(numerator, placement);
        for (int i = 0; i < 3; ++i) {
            if (exact(error[i], 0.)) continue;
            // Add a uniform numerator error on this rectangle. Keeping each
            // component's Bernstein degree also preserves the alignment of
            // vector coefficients for the next placement in the chain.
            const I allowance = error[i] * I(weightRange.hi);
            for (auto &coefficient : numerator[i].values)
                coefficient = coefficient + allowance;
        }
        if (position) origin = placedVector(*placement, origin, translation);
    }
}

struct ProfileJet {
    VectorPolynomial position, tangent; // numerators of C-O and dC/dt
    Polynomial weight;
    I denominator;
};

std::optional<ProfileJet> splineJet(const BSplineCurve<3> &curve, const ProfilePlacement &placement,
                                    int span, const Polynomial &u, bool needTangent, Work &work) {
    const auto &knots = curve.knots();
    const int degree = curve.degree(), first = span - degree;
    double gauge = curve.weight(first);
    bool variable = false;
    for (int i = first + 1; i <= span; ++i) {
        variable = variable || curve.weight(i) != curve.weight(first);
        gauge = std::max(gauge, curve.weight(i));
    }
    const auto values = basis(knots, span, u, degree);
    const auto derivatives = needTangent ? basis(knots, span, u, degree - 1) : std::vector<Polynomial>{};
    PolynomialTuple<4> h, dh;
    std::array<I, 4> previous{};
    for (int j = 0; j <= degree; ++j) {
        cancellationPoint();
        const int i = first + j;
        const I w = variable ? I(curve.weight(i)) / I(gauge) : I(1);
        auto pole = point(curve.poles()[i]);
        for (std::size_t k = 0; k < pole.size(); ++k)
            pole[k] = pole[k] - placement.nativeOrigin[k];
        const std::array<I, 4> homogeneous{pole[0] * w, pole[1] * w, pole[2] * w, w};
        I factor;
        if (needTangent && j > 0) factor = I(double(degree)) / (I(knots[i + degree]) - I(knots[i]));
        for (int k = 0; k < (variable ? 4 : 3); ++k) {
            h[k] = sum(std::move(h[k]), scaled(values[j], homogeneous[k]));
            if (needTangent && j > 0)
                dh[k] = sum(std::move(dh[k]), scaled(derivatives[j - 1], (homogeneous[k] - previous[k]) * factor));
        }
        previous = homogeneous;
    }
    if (!variable) h[3] = Polynomial(I(1));
    const auto &w = h[3];
    const I denominator = hull(w);
    if (!finite(denominator)) throw ProofFailed{};
    if (!(denominator.lo > 0)) return {};
    VectorPolynomial position{std::move(h[0]), std::move(h[1]), std::move(h[2])};
    VectorPolynomial tangent;
    if (needTangent) {
        // Tu = (W*H' - H*W')/W^2. Construct its numerator before placing
        // the position, because the evaluator transforms derivatives separately.
        for (int i = 0; i < 3; ++i) {
            tangent[i] = variable ? sum(product(w, dh[i]), product(position[i], dh[3]), true) : std::move(dh[i]);
        }
        placeProfile(tangent, denominator * denominator, placement, false, work);
    }
    placeProfile(position, denominator, placement, true, work);
    return ProfileJet{std::move(position), std::move(tangent), std::move(h[3]), denominator};
}

// Center the original homogeneous poles before composing the curve with U.
// The extrusion coordinate V stays affine and unbounded.
double extrusionSpanUpper(const ExtrudedProfile &target, int span, const Polynomial &u,
                          const VectorPolynomial &profileImage, const std::optional<HeightPatch> &offset, Work &work) {
    auto jet = splineJet(target.curve, target, span, u, bool(offset), work);
    if (!jet) return infinity;
    const auto &w = jet->weight;
    VectorPolynomial residual;
    for (int i = 0; i < 3; ++i)
        residual[i] = sum(product(profileImage[i], w), jet->position[i], true);
    if (!offset) return (squareRoot(squaredUpper(residual)) / I(jet->denominator.lo)).hi;
    VectorPolynomial sv;
    const auto w2 = product(w, w);
    for (int i = 0; i < 3; ++i) sv[i] = scaled(w2, target.direction[i]);
    return coneUpper(std::move(residual), jet->tangent, sv, *offset, w, jet->denominator.lo);
}

double patchUpper(const ExtrudedProfile &target, VectorPolynomial actual, const Mapping &map, Rectangle r,
                  const std::optional<HeightPatch> &offset, bool, Work &work, std::array<double, 2> &periodicSpans) {
    const auto u = affineOn(map.uv[0], r), v = affineOn(map.uv[1], r);
    for (int i = 0; i < 3; ++i)
        actual[i] = sum(std::move(actual[i]), scaled(v, target.direction[i]), true);
    const auto &curve = target.curve;
    const auto spans = cells(curve.knots(), curve.degree(), curve.poleCount(), hull(u), target.domain,
        target.periodic ? ParameterWrap::ClampedFloor : ParameterWrap::None);
    if (target.periodic && map.layout != UVLayout::Oblique)
        periodicSpans[0] = shortestSpan(spans, curve.knots(), target.domain);
    double upper = 0.;
    for (const auto &cell : spans) {
        work.visit((1 + curve.degree()) * (u.u && u.v ? 4 : 1));
        const auto local = translated(u, cell.translation);
        upper = std::max(upper, extrusionSpanUpper(target, cell.span, local ? *local : u, actual, offset, work));
    }
    return upper;
}

struct AngularExtrusion : ProfilePlacement {
    IP cosineAxis, sineAxis, direction;
    std::optional<Interval> trim;
    bool repeatTrim;
    bool usesCone() const { return false; }
};

// In the original parameter the meridian is either O + t X or
// O + cos(t) X + sin(t) Y. The axes retain their physical lengths.
struct AnalyticProfile {
    Vec3 center;
    IP x, y;
    bool linear;
};

AnalyticProfile conicProfile(const Vec3 &center, const Vec3 &x, double a, const Vec3 &y, double b) {
    AnalyticProfile profile{center, point(x), point(y), false};
    for (int i = 0; i < 3; ++i) {
        profile.x[i] = profile.x[i] * I(a);
        profile.y[i] = profile.y[i] * I(b);
    }
    return profile;
}

std::optional<AnalyticProfile> analyticProfile(const Curve<3> *curve) {
    if (const auto *line = dynamic_cast<const Line<3> *>(curve))
        return AnalyticProfile{line->origin(), point(line->direction()), {}, true};
    if (const auto *circle = dynamic_cast<const Circle<3> *>(curve))
        return conicProfile(circle->center(), circle->xAxis(), circle->radius(), circle->yAxis(), circle->radius());
    if (const auto *ellipse = dynamic_cast<const Ellipse<3> *>(curve))
        return conicProfile(ellipse->center(), ellipse->xAxis(), ellipse->xRadius(), ellipse->yAxis(), ellipse->yRadius());
    return {};
}

std::optional<AngularExtrusion> angularExtrusion(const Surface &surface, const ProfileBasis &basis) {
    if (const auto *cylinder = dynamic_cast<const CylindricalSurface *>(&surface)) {
        const auto &f = cylinder->frame();
        const auto p = conicProfile(f.origin(), f.xDir(), cylinder->radius(), f.yDir(), cylinder->radius());
        return AngularExtrusion{{p.center, basis}, p.x, p.y, point(f.zDir()), {}, false};
    }
    const auto *extrusion = dynamic_cast<const ExtrusionSurface *>(&surface);
    if (!extrusion) return {};
    const auto p = analyticProfile(basis.curve);
    if (!p || p->linear) return {};
    return AngularExtrusion{{p->center, basis}, p->x, p->y, point(extrusion->direction()),
        basis.trim, basis.trim && extrusion->isUPeriodic()};
}

struct AngularSeries { Polynomial sine, cosine; };

std::optional<AngularSeries> angularSeries(const Polynomial &u, Work &work) {
    const I angles = hull(u);
    if (!finite(angles)) throw ProofFailed{};
    const double center = .5 * angles.lo + .5 * angles.hi;
    const auto delta = sum(u, Polynomial(I(center)), true);
    const I width(magnitude(hull(delta)));
    if (width.hi > 1.) return {};
    const auto phase = boundedSineCosine(I(center));
    if (!phase) throw ProofFailed{};
    // Taylor in the original angle, including across multiples of 2*pi.
    // All fourth derivatives of sin/cos have absolute value at most one.
    const I width2 = width * width;
    const I tail = width2 * width2 / I(24.);
    const auto quadratic = product(delta, delta), cubic = product(quadratic, delta);
    const auto series = [&](I value, I derivative) {
        Polynomial constant(value + I(-tail.hi, tail.hi));
        auto p = sum(std::move(constant), scaled(delta, derivative));
        p = sum(std::move(p), scaled(quadratic, value / I(2.)), true);
        return sum(std::move(p), scaled(cubic, derivative / I(6.)), true);
    };
    AngularSeries result{series(phase->first, phase->second), series(phase->second, I(0) - phase->first)};
    work.visit(result.sine.values.size());
    return result;
}

Polynomial roundoff(I scale, double operations = 8.) {
    const I radius = I(operations) * (scale * I(std::numeric_limits<double>::epsilon())
        + I(std::numeric_limits<double>::denorm_min()));
    if (!finite(scale + radius)) throw ProofFailed{};
    return Polynomial(I(-radius.hi, radius.hi));
}

double angularResidual(const VectorPolynomial &residual, const VectorPolynomial &su, const VectorPolynomial &sv,
                       const std::optional<HeightPatch> &offset, bool useScaledBound, UVLayout layout) {
    // The public wrap evaluates the normal even when height is zero.
    if (!offset) return regularNormalLower(su, sv, cross(su, sv)) > 0.
        ? squareRoot(squaredUpper(residual)).hi : infinity;
    return polynomialNormalUpper(residual, su, sv, *offset, useScaledBound, layout);
}

double angularPatchUpper(const AngularExtrusion &target, VectorPolynomial residual, const Polynomial &u,
                         const Polynomial &v, const std::optional<HeightPatch> &offset,
                         bool useScaledBound, UVLayout layout, Work &work) {
    const auto phase = angularSeries(u, work);
    if (!phase) return infinity;
    const auto &[sine, cosine] = *phase;
    VectorPolynomial radial, su, sv;
    const I axialRange(magnitude(hull(v)));
    for (int i = 0; i < 3; ++i) {
        const I x = target.cosineAxis[i], y = target.sineAxis[i], d = target.direction[i];
        const I axes = I(magnitude(x)) + I(magnitude(y));
        radial[i] = sum(sum(scaled(cosine, x), scaled(sine, y)),
                        roundoff(axes + I(magnitude(target.nativeOrigin[i]))));
        su[i] = sum(sum(scaled(cosine, y), scaled(sine, x), true), roundoff(axes));
        sv[i] = Polynomial(d);
    }
    // Only the generator is placed. Its tangent and the unchanged world
    // extrusion direction determine the natural normal, including reflections.
    placeProfile(radial, I(1.), target, true, work);
    placeProfile(su, I(1.), target, false, work);
    for (int i = 0; i < 3; ++i) {
        const I d = target.direction[i];
        const I scale = I(magnitude(target.worldOrigin[i])) + I(magnitude(hull(radial[i])))
            + axialRange * I(magnitude(d));
        residual[i] = sum(std::move(residual[i]), sum(std::move(radial[i]), scaled(v, d)), true);
        residual[i] = sum(std::move(residual[i]), roundoff(scale));
    }
    return angularResidual(residual, su, sv, offset, useScaledBound, layout);
}

double patchUpper(const AngularExtrusion &target, const VectorPolynomial &actual, const Mapping &map, Rectangle r,
                  const std::optional<HeightPatch> &offset, bool useScaledBound, Work &work,
                  std::array<double, 2> &periodicSpans) {
    const auto u = affineOn(map.uv[0], r), v = affineOn(map.uv[1], r);
    periodicSpans[0] = .25;
    const I angles = hull(u);
    if (!finite(angles)) throw ProofFailed{};
    if (!target.repeatTrim) {
        if (target.trim && (!target.trim->contains(angles.lo, wrapTargetParameterSlack)
            || !target.trim->contains(angles.hi, wrapTargetParameterSlack))) throw ProofFailed{};
        return angularPatchUpper(target, actual, u, v, offset, useScaledBound, map.layout, work);
    }
    // Closure within linear resolution can give a trimmed conic a period
    // different from 2*pi. Use the same reduction as a closed spline profile.
    periodicSpans[0] = std::min(periodicSpans[0], target.trim->length());
    double upper = 0.;
    forEachPeriodTranslation(angles, *target.trim, ParameterWrap::ClampedFloor, [&](I translation) {
        const auto local = translated(u, translation);
        upper = std::max(upper, angularPatchUpper(target, actual, local ? *local : u, v, offset,
                                                   useScaledBound, map.layout, work));
    });
    return upper;
}

struct RevolvedMeridian {
    IP worldOrigin, axis;
    ProfilePlacement placement;
    AnalyticProfile profile;
    std::optional<Interval> trim;
    bool usesCone() const { return false; }
};

// Rotate the value and the meridian derivative just as RevolutionSurface
// does. The angular derivative has no component parallel to the axis.
VectorPolynomial rotateMeridian(const VectorPolynomial &w, const IP &axis, const AngularSeries &phase,
                                const IP &axisOrigin, VectorPolynomial *angularDerivative) {
    const auto height = projection(w, axis);
    const VectorPolynomial a{Polynomial(axis[0]), Polynomial(axis[1]), Polynomial(axis[2])};
    const auto side = cross(a, w);
    I scale;
    for (const auto &component : w) scale = scale + I(magnitude(hull(component)));
    VectorPolynomial out;
    for (int i = 0; i < 3; ++i) {
        const auto parallel = scaled(height, axis[i]);
        const auto perpendicular = sum(w[i], parallel, true);
        out[i] = sum(sum(product(phase.cosine, perpendicular), product(phase.sine, side[i])), parallel);
        // Roundoff of projection, cross product, rotation, and final recentering.
        out[i] = sum(std::move(out[i]), roundoff(scale + I(magnitude(axisOrigin[i])), 64.));
        if (angularDerivative) {
            auto du = sum(product(phase.cosine, side[i]), product(phase.sine, perpendicular), true);
            (*angularDerivative)[i] = sum(std::move(du), roundoff(scale, 64.));
        }
    }
    return out;
}

// The position is centered on the generating curve. Change the center to
// the rotation axis in homogeneous coordinates before rotating it. Both
// natural tangents must have the same scale for the regularity test.
double revolvedResidual(ProfileJet &jet, const IP &center, const IP &axisOrigin, const IP &axis,
                        const AngularSeries &phase, VectorPolynomial residual,
                        const std::optional<HeightPatch> &offset, bool useCone, bool useScaledBound, UVLayout layout) {
    const auto &w = jet.weight;
    IP originScale;
    for (int i = 0; i < 3; ++i) {
        auto &p = jet.position[i];
        p = sum(std::move(p), scaled(w, center[i] - axisOrigin[i]));
        const auto differenceError = roundoff(I(magnitude(hull(p))));
        p = sum(std::move(p), differenceError);
        originScale[i] = axisOrigin[i] * I(jet.denominator.hi);
    }
    VectorPolynomial su;
    const auto position = rotateMeridian(jet.position, axis, phase, originScale, &su);
    const auto sv = rotateMeridian(jet.tangent, axis, phase, {}, nullptr);
    for (int i = 0; i < 3; ++i) {
        if (useCone) {
            residual[i] = product(residual[i], w);
            su[i] = product(su[i], w);
        }
        residual[i] = sum(std::move(residual[i]), position[i], true);
    }
    if (!useCone) return angularResidual(residual, su, sv, offset, useScaledBound, layout);
    if (offset) return coneUpper(std::move(residual), su, sv, *offset, w, jet.denominator.lo);
    IP direction;
    I chord;
    if (!normalCone(su, sv, direction, chord)) return infinity;
    return (squareRoot(squaredUpper(residual)) / I(jet.denominator.lo)).hi;
}

double patchUpper(const RevolvedMeridian &target, VectorPolynomial residual, const Mapping &map, Rectangle r,
                  const std::optional<HeightPatch> &offset, bool useScaledBound, Work &work,
                  std::array<double, 2> &periodicSpans) {
    const auto u = affineOn(map.uv[0], r), v = affineOn(map.uv[1], r);
    const auto vr = hull(v);
    if (target.trim && (!target.trim->contains(vr.lo, wrapTargetParameterSlack)
        || !target.trim->contains(vr.hi, wrapTargetParameterSlack))) throw ProofFailed{};
    periodicSpans[0] = .25;
    const auto angle = angularSeries(u, work);
    if (!angle) return infinity;
    std::optional<AngularSeries> meridianAngle;
    if (!target.profile.linear) {
        periodicSpans[1] = .25;
        meridianAngle = angularSeries(v, work);
        if (!meridianAngle) return infinity;
    }
    ProfileJet jet{{}, {}, Polynomial(I(1)), I(1)};
    auto &w = jet.position;
    auto &dv = jet.tangent;
    for (int i = 0; i < 3; ++i) {
        const I x = target.profile.x[i], y = target.profile.y[i];
        I scale;
        if (meridianAngle) {
            const auto &[sine, cosine] = *meridianAngle;
            w[i] = sum(scaled(cosine, x), scaled(sine, y));
            scale = I(magnitude(x)) + I(magnitude(y));
            dv[i] = sum(sum(scaled(cosine, y), scaled(sine, x), true), roundoff(scale));
        } else {
            w[i] = scaled(v, x);
            scale = I(magnitude(vr)) * I(magnitude(x));
            dv[i] = Polynomial(x);
        }
        w[i] = sum(std::move(w[i]), roundoff(scale + I(magnitude(target.placement.nativeOrigin[i]))));
    }
    placeProfile(w, I(1), target.placement, true, work);
    placeProfile(dv, I(1), target.placement, false, work);
    return revolvedResidual(jet, target.placement.worldOrigin, target.worldOrigin, target.axis, *angle,
                            std::move(residual), offset, false, useScaledBound, map.layout);
}

struct RevolvedSpline {
    IP worldOrigin, axis;
    ProfilePlacement placement;
    const BSplineCurve<3> &curve;
    Interval domain;
    bool usesCone() const { return curve.isRational() || curve.degree() > 3; }
};

double patchUpper(const RevolvedSpline &target, const VectorPolynomial &actual, const Mapping &map, Rectangle r,
                  const std::optional<HeightPatch> &offset, bool useScaledBound, Work &work,
                  std::array<double, 2> &periodicSpans) {
    const auto u = affineOn(map.uv[0], r), v = affineOn(map.uv[1], r);
    periodicSpans[0] = .25;
    const auto phase = angularSeries(u, work);
    if (!phase) return infinity;
    const auto &curve = target.curve;
    const auto spans = cells(curve.knots(), curve.degree(), curve.poleCount(), hull(v),
        target.domain, ParameterWrap::None);
    periodicSpans[1] = shortestSpan(spans, curve.knots(), target.domain);
    double upper = 0.;
    for (const auto &cell : spans) {
        work.visit((curve.degree() + 1) * (v.u && v.v ? 4 : 1));
        auto jet = splineJet(curve, target.placement, cell.span, v, true, work);
        if (!jet) return infinity;
        upper = std::max(upper, revolvedResidual(*jet, target.placement.worldOrigin, target.worldOrigin,
            target.axis, *phase, actual, offset, target.usesCone(), useScaledBound, map.layout));
    }
    return upper;
}

// The canonical faces emitted by makeRevolution keep the axes of their
// stored frame. Radius is signed on the other side of a cone or spindle torus.
struct CanonicalRevolution {
    const Frame3 &frame;
    IP worldOrigin;
    double baseRadius, minorRadius;
    std::optional<double> semiAngle;
    std::optional<Interval> latitude;
    bool usesCone() const { return false; }
};
std::optional<CanonicalRevolution> canonicalRevolution(const Surface &surface) {
    if (const auto *cone = dynamic_cast<const ConicalSurface *>(&surface))
        return CanonicalRevolution{cone->frame(), point(cone->frame().origin()), cone->referenceRadius(),
            0., cone->semiAngle(), {}};
    if (const auto *sphere = dynamic_cast<const SphericalSurface *>(&surface))
        return CanonicalRevolution{sphere->frame(), point(sphere->frame().origin()), 0.,
            sphere->radius(), {}, sphere->vDomain()};
    if (const auto *torus = dynamic_cast<const ToroidalSurface *>(&surface))
        return CanonicalRevolution{torus->frame(), point(torus->frame().origin()), torus->majorRadius(),
            torus->minorRadius(), {}, {}};
    return {};
}

double patchUpper(const CanonicalRevolution &target, VectorPolynomial residual, const Mapping &map, Rectangle r,
                  const std::optional<HeightPatch> &offset, bool useScaledBound, Work &work,
                  std::array<double, 2> &periodicSpans) {
    const auto u = affineOn(map.uv[0], r), v = affineOn(map.uv[1], r);
    const I vr = hull(v);
    if (target.latitude && (!target.latitude->contains(vr.lo, wrapTargetParameterSlack)
        || !target.latitude->contains(vr.hi, wrapTargetParameterSlack))) throw ProofFailed{};
    periodicSpans[0] = .25;
    const auto phase = angularSeries(u, work);
    if (!phase) return infinity;
    Polynomial dr, dz, z, radius;
    I scale, derivativeScale;
    if (target.semiAngle) {
        const auto slope = boundedSineCosine(I(*target.semiAngle));
        if (!slope) throw ProofFailed{};
        const auto &[sa, ca] = *slope;
        dr = Polynomial(sa); dz = Polynomial(ca);
        radius = sum(Polynomial(I(target.baseRadius)), scaled(v, sa));
        z = scaled(v, ca);
        scale = I(target.baseRadius) + I(magnitude(vr));
        derivativeScale = I(1);
    } else {
        periodicSpans[1] = .25;
        const auto meridian = angularSeries(v, work);
        if (!meridian) return infinity;
        const I minor(target.minorRadius);
        dz = scaled(meridian->cosine, minor);
        z = scaled(meridian->sine, minor);
        radius = sum(Polynomial(I(target.baseRadius)), dz);
        dr = scaled(z, I(-1));
        scale = I(target.baseRadius) + minor;
        derivativeScale = minor;
    }
    const auto &f = target.frame;
    VectorPolynomial su, sv;
    for (int i = 0; i < 3; ++i) {
        const I x(f.xDir()[i]), y(f.yDir()[i]), axis(f.zDir()[i]);
        const auto radial = sum(scaled(phase->cosine, x), scaled(phase->sine, y));
        const auto tangent = sum(scaled(phase->cosine, y), scaled(phase->sine, x), true);
        residual[i] = sum(std::move(residual[i]), sum(product(radius, radial), scaled(z, axis)), true);
        residual[i] = sum(std::move(residual[i]), roundoff(scale + I(magnitude(target.worldOrigin[i])), 16.));
        su[i] = sum(product(radius, tangent), roundoff(scale, 16.));
        sv[i] = sum(sum(product(radial, dr), scaled(dz, axis)), roundoff(derivativeScale, 16.));
    }
    return angularResidual(residual, su, sv, offset, useScaledBound, map.layout);
}

std::array<bool, 2> targetAxesAlongU(UVLayout layout) {
    return {layout == UVLayout::Direct || layout == UVLayout::DiagonalU,
            layout == UVLayout::Swapped || layout == UVLayout::DiagonalU};
}
bool hasNonlinearProjection(const Mapping &map) {
    return map.sourceGraph && (!exact(map.sourceGraph->zUV[0], 0.) || !exact(map.sourceGraph->zUV[1], 0.));
}

Polynomial graphUVOn(const Mapping &map, int axis, Rectangle r) {
    auto uv = affineOn(map.uv[axis], r);
    if (!exact(map.sourceGraph->zUV[axis], 0.))
        uv = sum(std::move(uv), scaled(graphZOn(*map.sourceGraph, P{I(r.u.lo), I(r.u.hi)},
            P{I(r.v.lo), I(r.v.hi)}), map.sourceGraph->zUV[axis]));
    return uv;
}

double patchUpper(const TargetSurface &target, const VectorPolynomial &actual, const Mapping &mapping, Rectangle r,
                  const std::optional<HeightPatch> &offset, bool useScaledBound, Work &work,
                  std::array<double, 2> &periodicSpans) {
    const auto &map = mapping.uv;
    const auto layout = mapping.layout;
    const auto alongU = targetAxesAlongU(layout);
    if (hasNonlinearProjection(mapping))
        return cellsUpper(target, actual, graphUVOn(mapping, 0, r), graphUVOn(mapping, 1, r),
                          layout, offset, useScaledBound, work, periodicSpans);
    return layout == UVLayout::Oblique
        ? cellsUpper(target, actual, affineOn(map[0], r), affineOn(map[1], r),
              layout, offset, useScaledBound, work, periodicSpans)
        : cellsUpper(target, actual, affineAlong(map[0], r, alongU[0]), affineAlong(map[1], r, alongU[1]),
              layout, offset, useScaledBound, work, periodicSpans);
}
struct ProjectedCell {
    std::array<Interval, 2> domains;
    bool canReject;
};
struct ProjectedResidualExceeded {};

I signedQuotient(I numerator, I denominator) {
    return denominator.lo > 0 ? numerator / denominator
        : (I(0) - numerator) / (I(0) - denominator);
}

bool intersectProjectedRange(Interval &range, I projection) {
    // An unusable inverse contributes no restriction to the forward proof.
    if (finite(projection)) {
        range = {std::max(range.lo, projection.lo), std::min(range.hi, projection.hi)};
    }
    return range.lo <= range.hi;
}

void encloseDegenerateInterval(Interval &range, Interval within) {
    if (range.lo != range.hi) return;
    range.lo = std::max(within.lo, std::nextafter(range.lo, -infinity));
    range.hi = std::min(within.hi, std::nextafter(range.hi, infinity));
}

bool clipObliqueCell(const std::array<Affine, 2> &map, const ProjectedCell &cell, Rectangle &box) {
    const Rectangle original = box;
    const auto &[a, b] = map;
    const I x = I(cell.domains[0].lo, cell.domains[0].hi) - a.origin;
    const I y = I(cell.domains[1].lo, cell.domains[1].hi) - b.origin;
    const I determinant = a.du * b.dv - a.dv * b.du;
    if (strictSign(determinant)) {
        if (!intersectProjectedRange(box.u, signedQuotient(b.dv * x - a.dv * y, determinant)) ||
            !intersectProjectedRange(box.v, signedQuotient(a.du * y - b.du * x, determinant))) return false;
    }
    // Contract each strip using the current range of its transverse source
    // coordinate. This also applies when the two rows have dependent slopes.
    const std::array<I, 2> delta{x, y};
    for (int pass = 0; pass < 2; ++pass) for (int axis = 0; axis < 2; ++axis) {
        const auto &f = map[axis];
        const auto domain = cell.domains[axis];
        const I span = range(f, box.u, box.v);
        if (finite(span) && (span.hi < domain.lo || domain.hi < span.lo)) return false;
        if (strictSign(f.du) &&
            !intersectProjectedRange(box.u,
                signedQuotient(delta[axis] - f.dv * I(box.v.lo, box.v.hi), f.du))) return false;
        if (strictSign(f.dv) &&
            !intersectProjectedRange(box.v,
                signedQuotient(delta[axis] - f.du * I(box.u.lo, box.u.hi), f.dv))) return false;
    }
    encloseDegenerateInterval(box.u, original.u);
    encloseDegenerateInterval(box.v, original.v);
    return true;
}

bool clipGraphCell(const Mapping &map, const ProjectedCell &cell, Rectangle &box) {
    ProjectedCell affineCell = cell;
    const I z = hull(graphZOn(*map.sourceGraph,
        P{I(box.u.lo), I(box.u.hi)}, P{I(box.v.lo), I(box.v.hi)}));
    for (int axis = 0; axis < 2; ++axis) {
        if (!exact(map.sourceGraph->zUV[axis], 0.)) {
            // A contact's affine coordinates lie in the target cell minus
            // the interval of its nonlinear correction over this source box.
            const auto domain = cell.domains[axis];
            const I expanded = I(domain.lo, domain.hi) - map.sourceGraph->zUV[axis] * z;
            if (!finite(expanded)) throw ProofFailed{};
            affineCell.domains[axis] = {expanded.lo, expanded.hi};
        }
    }
    return clipObliqueCell(map.uv, affineCell, box);
}

std::optional<IP> projectedPointOffset(const VectorPolynomial &su, const VectorPolynomial &sv, I height) {
    const auto c = cross(su, sv);
    if (!(regularNormalLower(su, sv, c) > 0.)) return {};
    const I length = squareRoot(hull(dot(c, c)));
    if (!(length.lo > 0.)) return {};
    IP offset;
    for (int k = 0; k < 3; ++k) offset[k] = height * (hull(c[k]) / length);
    return offset;
}

void checkProjectedPointResidual(const VectorPolynomial &actual, const VectorPolynomial &wanted,
                                 const IP &offset, double budget) {
    I lowerSquared;
    for (int k = 0; k < 3; ++k) {
        const I r = hull(actual[k]) - hull(wanted[k]) - offset[k];
        if (!finite(r)) return;
        const I lower(std::max({0., r.lo, -r.hi}));
        lowerSquared = lowerSquared + lower * lower;
    }
    if (finite(lowerSquared) && squareRoot(lowerSquared).lo > budget) throw ProjectedResidualExceeded{};
}

void checkProjectedRationalPoint(const TargetSurface &target, int tu, int tv,
                                 const Polynomial &u, const Polynomial &v, I height,
                                 const VectorPolynomial &actual, double budget) {
    const auto &s = target.base;
    constexpr auto layout = UVLayout::Direct;
    const auto pole = [&](int i, int j) {
        const I weight(s.weight(i, j));
        const auto p = point(s.pole(i, j));
        return std::array<I, 4>{weight * (p[0] - target.nativeOrigin[0]),
                                weight * (p[1] - target.nativeOrigin[1]),
                                weight * (p[2] - target.nativeOrigin[2]), weight};
    };
    const auto bu = basis(s.uKnots(), tu, u, s.uDegree());
    const auto bv = basis(s.vKnots(), tv, v, s.vDegree());
    const auto h = weightedSurface<4>(bu, bv, layout, [&](int i, int j) {
        return pole(tu - s.uDegree() + i, tv - s.vDegree() + j);
    });
    const I weight = hull(h[3]);
    if (!(weight.lo > 0.) || !finite(weight)) return;
    const VectorPolynomial wanted{Polynomial(hull(h[0]) / weight), Polynomial(hull(h[1]) / weight),
                                  Polynomial(hull(h[2]) / weight)};
    IP offset{};
    if (!exact(height, 0.)) {
        // Both natural derivatives share the same positive denominator W²;
        // the oriented unit normal depends only on their numerators.
        const auto tangent = [&](bool alongU) {
            const auto d = derivativeTensor<4>(s, tu, tv, u, v, alongU, layout, pole);
            VectorPolynomial result;
            for (int k = 0; k < 3; ++k)
                result[k] = sum(product(h[3], d[k]), product(h[k], d[3]), true);
            return result;
        };
        const auto normalOffset = projectedPointOffset(tangent(true), tangent(false), height);
        if (!normalOffset) return;
        offset = *normalOffset;
    }
    checkProjectedPointResidual(actual, wanted, offset, budget);
}

void checkProjectedResidualWitness(const TargetSurface &target, const BSplineSurface &image,
    const Mapping &map, int iu, int iv, Rectangle box, const ProjectedCell &cell, double budget, Work &work) {
    if (!cell.canReject) return;
    const double u = .5 * box.u.lo + .5 * box.u.hi, v = .5 * box.v.lo + .5 * box.v.hi;
    I a = at(map.uv[0], u, v), b = at(map.uv[1], u, v);
    I z;
    if (map.sourceGraph) {
        z = hull(graphZOn(*map.sourceGraph, P{I(u)}, P{I(v)}));
        a = a + map.sourceGraph->zUV[0] * z;
        b = b + map.sourceGraph->zUV[1] * z;
    }
    if (!(cell.domains[0].lo < a.lo && a.hi < cell.domains[0].hi &&
          cell.domains[1].lo < b.lo && b.hi < cell.domains[1].hi)) return;
    work.visit();
    const auto [tu, tv] = *target.graphSpans;
    const auto &base = target.base;
    const Polynomial ua(a), va(b);
    constexpr auto layout = UVLayout::Direct;
    const auto actual = surfaceOn(image, iu, iv, Polynomial(I(u)), Polynomial(I(v)),
        target.worldOrigin, layout);
    const I height = at(map.height, u, v) + (map.sourceGraph ? map.sourceGraph->zHeight * z : I(0.));
    if (target.rational) {
        checkProjectedRationalPoint(target, tu, tv, ua, va, height, actual, budget);
        return;
    }
    const auto wanted = surfaceOn(base, tu, tv, ua, va, target.nativeOrigin, layout);
    IP offset{};
    if (!exact(height, 0)) {
        const auto pole = [&](int i, int j) { return point(base.pole(i, j)); };
        const auto su = derivativeTensor<3>(base, tu, tv, ua, va, true, layout, pole);
        const auto sv = derivativeTensor<3>(base, tu, tv, ua, va, false, layout, pole);
        const auto normalOffset = projectedPointOffset(su, sv, height);
        if (!normalOffset) return;
        offset = *normalOffset;
    }
    checkProjectedPointResidual(actual, wanted, offset, budget);
}

template<class Target>
double node(const Target &target, const BSplineSurface &image,
            const Mapping &mapping, int iu, int iv, Rectangle r,
            double budget, int depth, Work &work, bool useScaledBound, const ProjectedCell *projected) {
    work.visit();
    const auto &map = mapping.uv;
    if (projected && !(hasNonlinearProjection(mapping) ? clipGraphCell(mapping, *projected, r) :
                       clipObliqueCell(map, *projected, r))) return 0.;
    const auto &height = mapping.height;
    const auto layout = mapping.layout;
    const auto actual = surfaceOn(image, iu, iv, P{I(r.u.lo), I(r.u.hi)}, P{I(r.v.lo), I(r.v.hi)},
        target.worldOrigin, UVLayout::Direct);
    std::optional<HeightPatch> offset;
    if (mapping.sourceGraph || !constant(height) || !exact(height.origin, 0)) {
        auto h = affineOn(height, r);
        if (mapping.sourceGraph)
            h = sum(std::move(h), scaled(graphZOn(*mapping.sourceGraph, P{I(r.u.lo), I(r.u.hi)},
                P{I(r.v.lo), I(r.v.hi)}), mapping.sourceGraph->zHeight));
        const I heightRange = hull(h);
        if (!finite(heightRange)) throw ProofFailed{};
        useScaledBound = useScaledBound || !strictSign(heightRange);
        offset = HeightPatch{heightRange, target.usesCone() || useScaledBound ? std::move(h) : product(h, h)};
    }
    std::array<double, 2> periodicSpans{infinity, infinity};
    const double upper = patchUpper(target, actual, mapping, r, offset, useScaledBound, work, periodicSpans);
    if (upper <= budget) return upper;
    if constexpr (std::is_same_v<Target, TargetSurface>) {
        if (projected) checkProjectedResidualWitness(target, image, mapping, iu, iv, r, *projected, budget, work);
    }
    if (depth == maxDepth) throw ProofFailed{};
    bool splitU = layout == UVLayout::Oblique
        ? r.u.hi - r.u.lo >= r.v.hi - r.v.lo : depth % 2 == 0;
    // On a diagonal UV map, only its active source coordinate changes
    // the natural normal. Refine it more often while still shrinking the
    // transverse range of the height and emitted surface.
    if (useScaledBound && diagonal(layout) && !target.usesCone())
        splitU = (layout == UVLayout::DiagonalU) == (depth % 4 != 3);
    // The span polynomial of a short periodic cell may be extended across
    // several whole turns. Shrink its source axis first, then resume the
    // usual subdivision once each mapped width fits its active spans.
    double largestRatio = 1;
    for (std::size_t axis = 0; axis < map.size(); ++axis) {
        if (!std::isfinite(periodicSpans[axis]) || constant(map[axis])) continue;
        const double changeU = magnitude(map[axis].du) * r.u.length();
        const double changeV = magnitude(map[axis].dv) * r.v.length();
        const bool sourceU = changeU >= changeV;
        const double ratio = periodicSpans[axis] == 0. ? infinity : std::max(changeU, changeV) / periodicSpans[axis];
        if (ratio > largestRatio) {
            largestRatio = ratio;
            splitU = sourceU;
        }
    }
    const Interval q = splitU ? r.u : r.v;
    const double mid = .5 * q.lo + .5 * q.hi;
    if (!(q.lo < mid && mid < q.hi)) throw ProofFailed{};
    Rectangle left = r, right = r;
    (splitU ? left.u : left.v).hi = mid;
    (splitU ? right.u : right.v).lo = mid;
    const double leftUpper = node(target, image, mapping, iu, iv, left, budget, depth + 1, work,
        useScaledBound, projected);
    return std::max(leftUpper, node(target, image, mapping, iu, iv, right, budget, depth + 1, work,
        useScaledBound, projected));
}
std::vector<double> cuts(const std::vector<double> &knots, Interval range) {
    std::vector<double> out{range.lo};
    for (double k : knots) if (range.lo < k && k < range.hi && out.back() != k) out.push_back(k);
    out.push_back(range.hi);
    return out;
}
int containing(const std::vector<double> &k, int degree, int count, Interval r) {
    int cell = int(std::upper_bound(k.begin(), k.end(), r.lo) - k.begin()) - 1;
    cell = std::clamp(cell, degree, count - 1);
    if (!(k[std::size_t(cell)] <= r.lo && r.hi <= k[std::size_t(cell + 1)])) throw ProofFailed{};
    return cell;
}
template<class Target>
SurfaceMappingBoundResult proveImageRectangle(const Target &target, const BSplineSurface &image, const Mapping &mapped,
                                              Interval u, Interval v, double budget, bool useScaledBound, Work &work,
                                              const ProjectedCell *projected = nullptr) {
    const auto &uk = image.uKnots(), &vk = image.vKnots();
    if (u.lo < uk[3] || u.hi > uk[std::size_t(image.uPoleCount())] ||
        v.lo < vk[3] || v.hi > vk[std::size_t(image.vPoleCount())]) throw ProofFailed{};
    const auto us = cuts(uk, u), vs = cuts(vk, v);
    if ((us.size() - 1) > maxVisits / (vs.size() - 1)) throw ProofFailed{};
    double upper = 0;
    for (std::size_t i = 1; i < us.size(); ++i) for (std::size_t j = 1; j < vs.size(); ++j) {
        Rectangle r{{us[i - 1], us[i]}, {vs[j - 1], vs[j]}};
        upper = std::max(upper, node(target, image, mapped,
            containing(uk, 3, image.uPoleCount(), r.u), containing(vk, 3, image.vPoleCount(), r.v),
            r, budget, 0, work, useScaledBound, projected));
    }
    return {SurfaceMappingBoundStatus::Certified, upper};
}

template<class Target>
SurfaceMappingBoundResult proveImage(const Target &target, const BSplineSurface &image, const Mapping &mapped,
                                     Interval u, Interval v, double budget, bool useScaledBound) {
    validate(image, true);
    Work work;
    return proveImageRectangle(target, image, mapped, u, v, budget, useScaledBound, work);
}
}
SurfaceMappingBoundResult planarMappingResidualBound(
    const Surface &source, const Surface &target, const SurfaceWrapOptions &options,
    double scaleU, double scaleV, const Surface &image, Interval u, Interval v, double budget) {
    cancellationPoint();
    try {
        if (!std::isfinite(u.lo) || !std::isfinite(u.hi) || !(u.lo < u.hi) ||
            !std::isfinite(v.lo) || !std::isfinite(v.hi) || !(v.lo < v.hi) ||
            !std::isfinite(budget) || !(budget > 0) || !std::isfinite(scaleU) || !(scaleU > 0) ||
            !std::isfinite(scaleV) || !(scaleV > 0) || !std::isfinite(options.angle) ||
            !std::isfinite(options.normalOffset) || !std::isfinite(options.anchor.x()) ||
            !std::isfinite(options.anchor.y()) || !std::isfinite(options.shift.x()) || !std::isfinite(options.shift.y())) throw ProofFailed{};
        const auto *plane = dynamic_cast<const Plane *>(&source);
        const auto *t = dynamic_cast<const BSplineSurface *>(&target);
        const auto *f = dynamic_cast<const BSplineSurface *>(&image);
        if (!plane || !f || options.rigid) throw Unsupported{};
        const auto *extrusion = dynamic_cast<const ExtrusionSurface *>(&target);
        const auto *revolution = dynamic_cast<const RevolutionSurface *>(&target);
        const auto canonical = canonicalRevolution(target);
        const auto basis = extrusion ? profileBasis(*extrusion->curve())
            : revolution ? profileBasis(*revolution->meridian()) : ProfileBasis{};
        const auto angular = t ? std::nullopt : angularExtrusion(target, basis);
        if (!t && !extrusion && !angular && !revolution && !canonical) throw Unsupported{};
        const auto mapped = mapping(*plane, options, scaleU, scaleV);
        const I heightRange = range(mapped.height, u, v);
        if (!finite(heightRange)) throw ProofFailed{};
        const bool useScaledBound = !constant(mapped.height) && !strictSign(heightRange);
        if (t) return proveImage(targetSurface(*t), *f, mapped, u, v, budget, useScaledBound);
        if (angular)
            return proveImage(*angular, *f, mapped, u, v, budget, useScaledBound);
        if (canonical)
            return proveImage(*canonical, *f, mapped, u, v, budget, useScaledBound);
        if (revolution) {
            const auto p = analyticProfile(basis.curve);
            if (!p) {
                const auto &spline = splineBasis(basis);
                const RevolvedSpline meridian{point(revolution->axisPoint()), point(revolution->axisDirection()),
                    {spline.poles().front(), basis}, spline, revolution->vDomain()};
                return proveImage(meridian, *f, mapped, u, v, budget, useScaledBound);
            }
            const RevolvedMeridian meridian{point(revolution->axisPoint()), point(revolution->axisDirection()),
                {p->center, basis}, *p, basis.trim};
            return proveImage(meridian, *f, mapped, u, v, budget, useScaledBound);
        }
        const auto profile = extrudedProfile(*extrusion, basis);
        return proveImage(profile, *f, mapped, u, v, budget, useScaledBound);
    } catch (const Unsupported &) { return {}; }
      catch (const ProofFailed &) { return {SurfaceMappingBoundStatus::Failed, infinity}; }
}
namespace {
struct TraceCoordinates {
    std::array<P, 2> uv;
    P weight;
    double minimumWeight = 1.;

    I range(int axis) const {
        if (weight.empty()) return hull(uv[axis]);
        // Positive homogeneous Bernstein coefficients enclose the ratio
        // by the associated Euclidean values on the same trace interval.
        I bounds(infinity, -infinity);
        for (std::size_t i = 0; i < weight.size(); ++i) {
            const I w = weight[i];
            if (!(w.lo > 0.)) {
                const I interval = hull(weight);
                return hull(uv[axis]) / I(std::max(minimumWeight, interval.lo), interval.hi);
            }
            const I value = uv[axis][i] / w;
            bounds.lo = std::min(bounds.lo, value.lo);
            bounds.hi = std::max(bounds.hi, value.hi);
        }
        return bounds;
    }
};

struct OffsetTrace {
    const Line<2> *line;
    const BSplineCurve<2> *spline;

    TraceCoordinates coordinates(Interval t) const {
        const P parameter{I(t.lo), I(t.hi)};
        if (line)
            return {{sum(P{I(line->origin().x())}, scaled(parameter, I(line->direction().x()))),
                     sum(P{I(line->origin().y())}, scaled(parameter, I(line->direction().y())))}, {}};

        const auto &knots = spline->knots();
        const int degree = spline->degree();
        const int cell = containing(knots, degree, spline->poleCount(), t);
        const auto b = basis(knots, cell, parameter, degree);
        TraceCoordinates trace{{P{I(0)}, P{I(0)}}, {}};
        const bool rational = spline->isRational();
        double scale = 1.;
        if (rational) {
            const auto first = spline->weights().begin() + cell - degree;
            scale = *std::max_element(first, first + degree + 1);
            trace.weight = P{I(0)};
        }
        for (int i = 0; i <= degree; ++i) {
            const int index = cell - degree + i;
            const Vec2 &pole = spline->poles()[std::size_t(index)];
            const I weight = rational ? I(spline->weight(index)) / I(scale) : I(1);
            const auto value = scaled(b[std::size_t(i)], weight);
            for (int axis = 0; axis < 2; ++axis)
                trace.uv[axis] = sum(std::move(trace.uv[axis]), scaled(value, I(pole[axis])));
            if (rational) {
                trace.weight = sum(std::move(trace.weight), value);
                trace.minimumWeight = std::min(trace.minimumWeight, weight.lo);
            }
        }
        return trace;
    }
};

struct TraceSpanPair { int source, image; };
std::vector<TraceSpanPair> traceSpanPairs(const BSplineSurface &source, const BSplineSurface &image,
                                        bool alongU, I query, Work &work) {
    const auto sourceDomain = alongU ? source.uDomain() : source.vDomain();
    const auto imageDomain = alongU ? image.uDomain() : image.vDomain();
    if (!sourceDomain.contains(query.lo) || !sourceDomain.contains(query.hi) ||
        !imageDomain.contains(query.lo) || !imageDomain.contains(query.hi)) return {};

    const auto &a = alongU ? source.uKnots() : source.vKnots();
    const auto &b = alongU ? image.uKnots() : image.vKnots();
    std::vector<TargetCell> first, second;
    try {
        first = cells(a, alongU ? source.uDegree() : source.vDegree(),
            alongU ? source.uPoleCount() : source.vPoleCount(), query, sourceDomain, ParameterWrap::None);
        second = cells(b, alongU ? image.uDegree() : image.vDegree(),
            alongU ? image.uPoleCount() : image.vPoleCount(), query, imageDomain, ParameterWrap::None);
    } catch (const ProofFailed &) {
        // Too many candidate spans for this node: split the trace first.
        return {};
    }
    std::vector<TraceSpanPair> pairs;
    auto start = second.begin();
    for (const auto &x : first) {
        while (start != second.end() && b[start->span + 1] < a[x.span]) ++start;
        for (auto y = start; y != second.end() && b[y->span] <= a[x.span + 1]; ++y) {
            work.visit();
            if (pairs.size() == maxCells) return {};
            pairs.push_back({x.span, y->span});
        }
    }
    return pairs;
}

double traceUpper(const BSplineSurface &source, const BSplineSurface &image, const IP &origin,
                  double distance, const TraceCoordinates &trace, double budget, Work &work) {
    const auto &uv = trace.uv;
    const auto us = traceSpanPairs(source, image, true, trace.range(0), work);
    const auto vs = traceSpanPairs(source, image, false, trace.range(1), work);
    if (us.empty() || vs.empty() || us.size() > maxCells / vs.size()) return infinity;

    // D has degree <= 18, Su x Sv <= 30. The projected normalization
    // needs D*(C dot n0), of degree <= 48, without squaring C.
    const auto multiply = [](const P &a, const P &b) {
        if (a.size() == 1) return scaled(b, a.front());
        if (b.size() == 1) return scaled(a, b.front());
        return surface_bounds::product<48, ProofFailed>(a, b);
    };
    constexpr auto layout = UVLayout::DiagonalU;
    const auto pole = [&](int i, int j) { return point(source.pole(i, j)); };
    const P *weight = trace.weight.empty() ? nullptr : &trace.weight;
    P imageDenominator{I(1)}, promoteSource{I(1)};
    double denominatorLower = 1.;
    if (weight) {
        const I range = hull(*weight);
        const double lowerW = std::max(trace.minimumWeight, range.lo);
        if (!(lowerW > 0.) || !finite(range) || lowerW > range.hi) return infinity;
        const int sourceDegree = source.uDegree() + source.vDegree();
        I lower(1.);
        for (int degree = 1; degree <= 6; ++degree) {
            imageDenominator = product(imageDenominator, *weight);
            lower = lower * I(lowerW);
            if (degree == 6 - sourceDegree) promoteSource = imageDenominator;
        }
        denominatorLower = std::max(lower.lo, hull(imageDenominator).lo);
        if (!(denominatorLower > 0.)) return infinity;
    }
    const P height = scaled(imageDenominator, I(distance));
    const I heightRange = hull(height);
    double upper = 0.;
    for (const auto u : us) for (const auto v : vs) {
        work.visit(weight ? 24 : (uv[0].size() > 2 ? 16 : 4));
        auto wanted = surfaceOn(source, u.source, v.source, uv[0], uv[1], origin, layout, weight);
        auto actual = surfaceOn(image, u.image, v.image, uv[0], uv[1], origin, layout, weight);
        std::array<P, 3> delta;
        for (int axis = 0; axis < 3; ++axis)
            delta[axis] = sum(std::move(actual[axis].values),
                promoteSource.size() > 1 ? product(wanted[axis].values, promoteSource) : std::move(wanted[axis].values), true);

        double bound = squareRoot(squaredUpper(delta)).hi;
        if (distance != 0.) {
            const auto derivatives = [&](bool alongU) {
                // Both natural tangent numerators carry the same positive
                // W power. This factor cancels in the oriented normal.
                auto jet = derivativeTensor<3>(source, u.source, v.source, uv[0], uv[1], alongU, layout, pole, weight);
                return std::array<P, 3>{std::move(jet[0].values), std::move(jet[1].values), std::move(jet[2].values)};
            };
            const auto su = derivatives(true), sv = derivatives(false);
            const auto normal = polynomialCross(su, sv, multiply);
            const double lower = regularNormalLower(su, sv, normal);
            if (!(lower > 0.)) return infinity;
            bound = projectedNormalUpper(delta, normal, heightRange, height, lower, multiply);
        }
        if (weight) bound = (I(bound) / I(denominatorLower)).hi;
        if (!(bound <= budget)) return infinity;
        upper = std::max(upper, bound);
    }
    return upper;
}

double proveTrace(const BSplineSurface &source, const BSplineSurface &image, const IP &origin,
                  const OffsetTrace &trace, Interval t, double distance, double budget, int depth, Work &work) {
    work.visit();
    const double upper = traceUpper(source, image, origin, distance, trace.coordinates(t), budget, work);
    if (upper <= budget) return upper;
    const double middle = .5 * t.lo + .5 * t.hi;
    if (depth == maxDepth || !(t.lo < middle && middle < t.hi)) throw ProofFailed{};
    const double left = proveTrace(source, image, origin, trace, {t.lo, middle}, distance, budget, depth + 1, work);
    return std::max(left, proveTrace(source, image, origin, trace, {middle, t.hi}, distance, budget, depth + 1, work));
}
}

SurfaceMappingBoundResult offsetTraceResidualBound(
    const Surface &source, const Surface &image, const Curve<2> &pcurve,
    Interval range, double distance, double budget) {
    cancellationPoint();
    try {
        if (!range.isFinite() || !(range.lo < range.hi) || !std::isfinite(distance)
            || !std::isfinite(budget) || !(budget > 0.)) throw ProofFailed{};
        const auto *a = dynamic_cast<const BSplineSurface *>(&source);
        const auto *b = dynamic_cast<const BSplineSurface *>(&image);
        if (!a || !b || a->isUPeriodic() || a->isVPeriodic() ||
            a->uDegree() > 3 || a->vDegree() > 3) throw Unsupported{};

        const Curve<2> *curve = &pcurve;
        for (int depth = 0; const auto *trim = dynamic_cast<const TrimmedCurve<2> *>(curve); ++depth) {
            cancellationPoint();
            if (depth == 32 || !trim->domain().contains(range.lo) || !trim->domain().contains(range.hi))
                throw ProofFailed{};
            curve = trim->basis().get();
        }
        const OffsetTrace trace{dynamic_cast<const Line<2> *>(curve), dynamic_cast<const BSplineCurve<2> *>(curve)};
        if (!trace.line && (!trace.spline || trace.spline->degree() > 3))
            throw Unsupported{};
        if (validate(*a, false)) throw Unsupported{};
        validate(*b, true);

        std::vector<double> intervals{range.lo, range.hi};
        if (trace.spline) {
            const auto &q = *trace.spline;
            if (q.poleCount() > int(maxPoles) || !knotsValid(q.knots(), q.degree(), q.poleCount())
                || !q.domain().contains(range.lo) || !q.domain().contains(range.hi)) throw ProofFailed{};
            intervals = cuts(q.knots(), range);
            if (intervals.size() > maxVisits) throw ProofFailed{};
        }
        Work work;
        const auto origin = point(a->pole(0, 0));
        double upper = 0.;
        for (std::size_t i = 1; i < intervals.size(); ++i)
            upper = std::max(upper, proveTrace(*a, *b, origin, trace,
                {intervals[i - 1], intervals[i]}, distance, budget, 0, work));
        return {SurfaceMappingBoundStatus::Certified, upper};
    } catch (const Unsupported &) { return {}; }
      catch (const ProofFailed &) { return {SurfaceMappingBoundStatus::Failed, infinity}; }
}

namespace {
// Expansion addition keeps the exact sum of the binary64 data. A projected
// pole must reproduce its Greville parameter exactly, even on a long chart.
bool exactZeroSum(const double *first, std::size_t count) {
    std::array<double, 16> expansion{};
    if (count > expansion.size()) return false;
    std::size_t size = 0;
    for (const double *value = first; value != first + count; ++value) {
        double term = *value;
        if (!std::isfinite(term) || std::abs(term) > std::numeric_limits<double>::max() / 16)
            return false;
        std::size_t next = 0;
        for (std::size_t j = 0; j < size; ++j) {
            const double sum = term + expansion[j];
            const double high = sum - term;
            const double low = (term - (sum - high)) + (expansion[j] - high);
            if (low != 0.) expansion[next++] = low;
            term = sum;
        }
        if (term != 0.) expansion[next++] = term;
        size = next;
    }
    return size == 0;
}
bool exactZeroSum(std::initializer_list<double> terms) {
    return exactZeroSum(terms.begin(), terms.size());
}

int cartesianAxis(const Vec3 &axis) {
    int coordinate = -1;
    for (int k = 0; k < 3; ++k) {
        if (axis[k] == 0.) continue;
        if (coordinate >= 0 || std::abs(axis[k]) != 1.) return -1;
        coordinate = k;
    }
    return coordinate;
}

bool grevilleCoordinate(double pole, const std::vector<double> &knots, int index, int degree) {
    if (degree == 1) return pole == knots[index + 1];
    std::array<double, 10> terms;
    if (degree < 2 || std::size_t(degree) > terms.size() / 2) return false;
    for (int i = 0; i < degree; ++i) {
        terms[2 * i] = knots[index + i + 1];
        terms[2 * i + 1] = -pole;
    }
    return exactZeroSum(terms.data(), 2 * std::size_t(degree));
}

// Return false when a product cannot be represented exactly. Retain a
// margin from underflow so the FMA residual cannot round a nonzero error to 0.
bool exactProduct(double a, double b, double &result) {
    result = a * b;
    return a == 0. || b == 0. ||
           (std::isfinite(result) && std::abs(result) > 0x1p-900 && std::fma(a, b, -result) == 0.);
}

bool affineRationalCoordinate(const BSplineSurface &s, bool alongU, int coordinate) {
    const int degree = alongU ? s.uDegree() : s.vDegree();
    const auto &knots = alongU ? s.uKnots() : s.vKnots();
    const auto domain = alongU ? s.uDomain() : s.vDomain();
    const int count = alongU ? s.uPoleCount() : s.vPoleCount();
    const int rows = alongU ? s.vPoleCount() : s.uPoleCount();
    if (knots.front() != domain.lo || knots.back() != domain.hi) return false;
    if (count != degree + 1) {
        // Weights constant in this direction cancel from the affine
        // coordinate even when they vary along the other direction.
        for (int row = 0; row < rows; ++row) {
            const double weight = alongU ? s.weight(0, row) : s.weight(row, 0);
            for (int i = 0; i < count; ++i) {
                cancellationPoint();
                const auto p = alongU ? s.pole(i, row) : s.pole(row, i);
                if ((alongU ? s.weight(i, row) : s.weight(row, i)) != weight ||
                    !grevilleCoordinate(p[coordinate], knots, i, degree))
                    return false;
            }
        }
        return true;
    }
    for (int row = 0; row < rows; ++row) {
        cancellationPoint();
        const auto x = [&](int i) { return (alongU ? s.pole(i, row) : s.pole(row, i))[coordinate]; };
        const auto w = [&](int i) { return alongU ? s.weight(i, row) : s.weight(row, i); };
        if (x(0) != domain.lo || x(degree) != domain.hi) return false;
        // Compare homogeneous Bernstein coefficients after elevating XW
        // to degree p+1 and multiplying the linear coordinate by W.
        for (int i = 1; i <= degree; ++i) {
            double wi, previous, a, b, c, d;
            if (!exactProduct(w(i), double(degree + 1 - i), wi) ||
                !exactProduct(w(i - 1), double(i), previous) ||
                !exactProduct(wi, x(i), a) || !exactProduct(wi, domain.lo, b) ||
                !exactProduct(previous, x(i - 1), c) || !exactProduct(previous, domain.hi, d) ||
                !exactZeroSum({a, -b, c, -d})) return false;
        }
    }
    return true;
}

struct RationalProjection {
    Rectangle footprint;
    bool exactY = false;
    bool weightVariesV = false;
};

std::optional<RationalProjection> regularRationalProjection(const BSplineSurface &s, int axisX, int axisY, int axisZ) {
    const auto &ku = s.uKnots(), &kv = s.vKnots();
    const int pu = s.uDegree(), pv = s.vDegree(), nu = s.uPoleCount(), nv = s.vPoleCount();
    const auto continuousClamped = [](const std::vector<double> &knots, int degree, int count, Interval domain) {
        if (knots.front() != domain.lo || knots.back() != domain.hi) return false;
        for (int k = degree + 1; k < count; ++k)
            if (knots[k] == knots[k - degree]) return false;
        return true;
    };
    if (!continuousClamped(ku, pu, nu, s.uDomain()) || !continuousClamped(kv, pv, nv, s.vDomain()))
        return {};
    const auto origin = s.pole(0, 0);
    const double lastY = s.pole(0, nv - 1)[axisY];
    RationalProjection result{{{infinity, -infinity}, {infinity, -infinity}}};
    result.exactY = affineRationalCoordinate(s, false, axisY);
    bool constantYEnds = true;
    for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) {
        cancellationPoint();
        const auto pole = s.pole(i, j);
        const double x = pole[axisX], y = pole[axisY];
        if ((j == 0 && y != origin[axisY]) || (j == nv - 1 && y != lastY))
            constantYEnds = false;
        result.weightVariesV = result.weightVariesV || s.weight(i, j) != s.weight(i, 0);
        result.footprint.u.lo = std::min(result.footprint.u.lo, x);
        result.footprint.u.hi = std::max(result.footprint.u.hi, x);
        result.footprint.v.lo = std::min(result.footprint.v.lo, y);
        result.footprint.v.hi = std::max(result.footprint.v.hi, y);
    }
    const auto homogeneous = [&](int i, int j) {
        const auto pole = s.pole(i, j);
        const I w(s.weight(i, j));
        return std::array<I, 4>{w * (I(pole[axisX]) - I(origin[axisX])),
                                w * (I(pole[axisY]) - I(origin[axisY])),
                                w * (I(pole[axisZ]) - I(origin[axisZ])), w};
    };
    std::size_t cells = 0;
    for (int cu = pu; cu < nu; ++cu) for (int cv = pv; cv < nv; ++cv) {
        cancellationPoint();
        if (ku[cu] == ku[cu + 1] || kv[cv] == kv[cv + 1]) continue;
        if (++cells > maxCells) return {};
        constexpr auto layout = UVLayout::Direct;
        const P u{I(ku[cu]), I(ku[cu + 1])}, v{I(kv[cv]), I(kv[cv + 1])};
        const auto bu = basis(ku, cu, u, pu), bv = basis(kv, cv, v, pv);
        const auto position = weightedSurface<4>(bu, bv, layout, [&](int i, int j) {
            return homogeneous(cu - pu + i, cv - pv + j);
        });
        const auto tangent = [&](bool alongU) {
            const auto d = derivativeTensor<4>(s, cu, cv, u, v, alongU, layout, homogeneous);
            VectorPolynomial numerator;
            for (int k = 0; k < 3; ++k)
                numerator[k] = sum(product(d[k], position[3]), product(position[k], d[3]), true);
            return numerator;
        };
        const auto su = tangent(true), sv = tangent(false);
        const auto normal = cross(su, sv);
        const I yy = hull(sv[1]), det = hull(normal[2]);
        if (!finite(yy) || !finite(det) || !(yy.lo > 0.) || !(det.lo > 0.)) return {};
        if (!constantYEnds) {
            // A positive symmetric part on every closed span makes the
            // continuous projection strictly monotone on the rectangle.
            const auto shear = sum(su[1], sv[0]);
            const I symmetricDet = hull(sum(scaled(product(su[0], sv[1]), I(4.)), product(shear, shear), true));
            if (!finite(symmetricDet) || !(symmetricDet.lo > 0.)) return {};
        }
        // With constant transverse ends the level Y crosses every column
        // and X increases there. The common weight cancels in the physical
        // regularity and facing tests, including the limits at knot lines.
        if (!(regularNormalLower(su, sv, normal) > 0.)) return {};
        const I directionCutoff = I(1e-6) * squareRoot(squaredUpper(normal));
        if (!finite(directionCutoff) || !(det.lo > directionCutoff.hi)) return {};
    }
    return cells ? std::optional<RationalProjection>{result} : std::nullopt;
}

bool graphCoordinates(const BSplineSurface &surface, int axisU, int axisV) {
    for (int i = 0; i < surface.uPoleCount(); ++i) for (int j = 0; j < surface.vPoleCount(); ++j) {
        cancellationPoint();
        const Vec3 pole = surface.pole(i, j);
        if (!grevilleCoordinate(pole[axisU], surface.uKnots(), i, surface.uDegree()) ||
            !grevilleCoordinate(pole[axisV], surface.vKnots(), j, surface.vDegree())) return false;
    }
    return true;
}

struct GraphPatch {
    TargetSurface carrier;
    Mapping map;
    Interval u, v;
    bool fullDomain;
    bool contactIsRegular;
};

UVLayout projectedLayout(const std::array<Affine, 2> &uv) {
    const bool uu = exact(uv[0].du, 0), uvZero = exact(uv[0].dv, 0);
    const bool vu = exact(uv[1].du, 0), vv = exact(uv[1].dv, 0);
    if (uvZero && vu) return UVLayout::Direct;
    if (uu && vv) return UVLayout::Swapped;
    if (uvZero && vv) return UVLayout::DiagonalU;
    if (uu && vu) return UVLayout::DiagonalV;
    return UVLayout::Oblique;
}

bool clipProjectedAxis(const Affine &coordinate, Interval domain, Rectangle &box) {
    if (constant(coordinate)) {
        if (!finite(coordinate.origin)) throw ProofFailed{};
        return coordinate.origin.lo <= domain.hi && domain.lo <= coordinate.origin.hi;
    }
    const bool alongU = exact(coordinate.dv, 0);
    const I slope = alongU ? coordinate.du : coordinate.dv;
    if (!strictSign(slope)) throw Unsupported{};
    const I delta = I(domain.lo, domain.hi) - coordinate.origin;
    const I inverse = signedQuotient(delta, slope);
    if (!finite(inverse)) throw ProofFailed{};
    auto &r = alongU ? box.u : box.v;
    r = {std::max(r.lo, inverse.lo), std::min(r.hi, inverse.hi)};
    return r.lo <= r.hi;
}

bool exactIsoSide(const Curve<2> &c, Interval range, int fixed, double side, Interval varying) {
    if (const auto *line = dynamic_cast<const Line<2> *>(&c)) {
        if (line->origin()[fixed] != side || line->direction()[fixed] != 0.)
            return false;
        const double direction = line->direction()[1 - fixed];
        if (std::abs(direction) != 1.) return false;
        const double a = direction > 0 ? range.lo : range.hi;
        const double b = direction > 0 ? range.hi : range.lo;
        const double origin = line->origin()[1 - fixed];
        return exactZeroSum({origin, direction * a, -varying.lo})
            && exactZeroSum({origin, direction * b, -varying.hi});
    }
    const auto *spline = dynamic_cast<const BSplineCurve<2> *>(&c);
    if (!spline || spline->isPeriodic() || spline->isRational() ||
        spline->domain().lo != range.lo || spline->domain().hi != range.hi) return false;
    const auto &knots = spline->knots();
    const int degree = spline->degree(), count = spline->poleCount();
    if (!knotsValid(knots, degree, count) || knots.front() != range.lo || knots.back() != range.hi)
        return false;
    const auto &poles = spline->poles();
    for (const auto &pole : poles) {
        cancellationPoint();
        if (pole[fixed] != side || !varying.contains(pole[1 - fixed])) return false;
    }
    const double a = poles.front()[1 - fixed], b = poles.back()[1 - fixed];
    return std::min(a, b) == varying.lo && std::max(a, b) == varying.hi;
}

bool fullRectangle(const Body &body, FaceId faceId, Interval u, Interval v) {
    const auto &face = body.face(faceId);
    if (face.loops.size() != 1) return false;
    const auto fins = body.loopFins(face.loops.front());
    if (fins.size() != 4) return false;
    unsigned covered = 0;
    for (auto id : fins) {
        const auto &fin = body.fin(id);
        if (!fin.pcurve) return false;
        auto curve = fin.pcurve.get();
        while (const auto *trim = dynamic_cast<const TrimmedCurve<2> *>(curve))
            curve = trim->basis().get();
        const Interval range = body.edge(fin.edge).range;
        if (!range.isFinite() || !(range.lo < range.hi)) return false;
        const std::array<Interval, 2> window{u, v};
        unsigned match = 0;
        for (int fixed = 0; fixed < 2; ++fixed) for (int end = 0; end < 2; ++end) {
            const double value = end == 0 ? window[fixed].lo : window[fixed].hi;
            if (exactIsoSide(*curve, range, fixed, value, window[1 - fixed]))
                match |= 1u << (2 * fixed + end);
        }
        if (match == 0 || (match & covered)) return false;
        covered |= match;
    }
    return covered == 15;
}

bool regularGraphContact(const BSplineSurface &surface, int heightAxis) {
    // The normal's projected component is 1/sqrt(1+hu²+hv²).
    // Bound both pieces of the gradient with derivative control polygons
    // before allowing failure on this patch to reject the wrap.
    const I origin(surface.pole(0, 0)[heightAxis]);
    double lowerWeight = infinity, heightReach = 0.;
    if (surface.isRational()) {
        for (int i = 0; i < surface.uPoleCount(); ++i) for (int j = 0; j < surface.vPoleCount(); ++j) {
            cancellationPoint();
            lowerWeight = std::min(lowerWeight, surface.weight(i, j));
            heightReach = std::max(heightReach, magnitude(I(surface.pole(i, j)[heightAxis]) - origin));
        }
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto &knots = axis == 0 ? surface.uKnots() : surface.vKnots();
        const int degree = axis == 0 ? surface.uDegree() : surface.vDegree();
        const int nu = surface.uPoleCount() - (axis == 0 ? 1 : 0);
        const int nv = surface.vPoleCount() - (axis == 1 ? 1 : 0);
        for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) {
            cancellationPoint();
            const int k = axis == 0 ? i : j;
            const I span = I(knots[k + degree + 1]) - I(knots[k + 1]);
            const I current(surface.pole(i, j)[heightAxis]);
            const I following(surface.pole(i + (axis == 0), j + (axis == 1))[heightAxis]);
            const I rate = I(degree) / span;
            if (!surface.isRational()) {
                const I derivative = rate * (following - current);
                if (!finite(derivative) || magnitude(derivative) >= 1e5) return false;
                continue;
            }
            // Quotient rule: Z' = ((WZ)' - Z W') / W. Center height
            // consistently with the bound on |Z| from the control hull.
            const I wa(surface.weight(i, j)), wb(surface.weight(i + (axis == 0), j + (axis == 1)));
            const I dz = rate * (wb * (following - origin) - wa * (current - origin));
            const I dw = rate * (wb - wa);
            const auto bound = (I(magnitude(dz)) + I(heightReach) * I(magnitude(dw))) / I(lowerWeight);
            if (!finite(bound) || bound.hi >= 1e5) return false;
        }
    }
    return true;
}

bool interiorAtMidpoint(const Mapping &map, Rectangle box, Interval ud, Interval vd) {
    const double u = .5 * box.u.lo + .5 * box.u.hi;
    const double v = .5 * box.v.lo + .5 * box.v.hi;
    const I a = at(map.uv[0], u, v), b = at(map.uv[1], u, v);
    return ud.lo < a.lo && a.hi < ud.hi && vd.lo < b.lo && b.hi < vd.hi;
}

// For a common, strictly monotone Y(V), compare the entire right and
// left boundary functions at the same V. When rational edge weights are
// proportional, both edges use the same normalized B-spline basis.
bool orderedProjectedStrips(const BSplineSurface &left, const BSplineSurface &right, int axisX, int axisY) {
    if (left.vDegree() != right.vDegree() || left.vKnots() != right.vKnots()) return false;
    const int rightmost = left.uPoleCount() - 1;
    for (int j = 0; j < left.vPoleCount(); ++j) {
        cancellationPoint();
        if (left.pole(0, j)[axisY] != right.pole(0, j)[axisY] ||
            left.pole(rightmost, j)[axisX] > right.pole(0, j)[axisX])
            return false;
        if (left.isRational() || right.isRational()) {
            double a, b;
            if (!exactProduct(left.weight(rightmost, j), right.weight(0, 0), a) ||
                !exactProduct(right.weight(0, j), left.weight(rightmost, 0), b) || a != b)
                return false;
        }
    }
    return true;
}

std::optional<double> constantGraphHeight(const BSplineSurface &s, int axis) {
    const double height = s.pole(0, 0)[axis];
    for (int i = 0; i < s.uPoleCount(); ++i) for (int j = 0; j < s.vPoleCount(); ++j) {
        cancellationPoint();
        if (s.pole(i, j)[axis] != height) return {};
    }
    return height;
}

// Use exact binary64 operations or abandon the equality proof. In particular,
// the outward-enclosed residual used for the fit cannot establish an identity
// merely because its enclosure contains zero.
struct OverlapArithmeticFailed {};
double overlapAdd(double a, double b) {
    if (a == 0.) return b;
    if (b == 0.) return a;
    const double sum = a + b;
    if (!exactZeroSum({a, b, -sum})) throw OverlapArithmeticFailed{};
    return sum;
}

double overlapMultiply(double a, double b) {
    if (a == 1.) return b;
    if (b == 1.) return a;
    double value;
    if (!exactProduct(a, b, value)) throw OverlapArithmeticFailed{};
    return value;
}

double overlapDivide(double a, double b) {
    if (a == b) return 1.;
    const double value = a / b;
    if (overlapMultiply(value, b) != a) throw OverlapArithmeticFailed{};
    return value;
}

using OverlapBasis = std::vector<std::vector<double>>;

// Cox-de Boor in power coefficients about the common physical origin.
// On a zero-width overlap use a unit scale to retain the normal derivative.
OverlapBasis overlapBasis(const std::vector<double> &knots, int span, int degree, Interval physical) {
    const double step = physical.lo == physical.hi ? 1. : overlapAdd(physical.hi, -physical.lo);
    OverlapBasis previous(1, {1.});
    for (int d = 1; d <= degree; ++d) {
        OverlapBasis current(d + 1, std::vector<double>(d + 1));
        const auto add = [&](int to, int from, double origin, double slope, double width) {
            origin = overlapDivide(origin, width);
            slope = overlapDivide(slope, width);
            for (int k = 0; k < d; ++k) {
                current[to][k] = overlapAdd(current[to][k], overlapMultiply(previous[from][k], origin));
                current[to][k + 1] = overlapAdd(current[to][k + 1], overlapMultiply(previous[from][k], slope));
            }
        };
        for (int r = 0; r <= d; ++r) {
            const int i = span - d + r;
            if (r > 0 && knots[i] < knots[i + d])
                add(r, r - 1, overlapAdd(physical.lo, -knots[i]), step, overlapAdd(knots[i + d], -knots[i]));
            if (r < d && knots[i + 1] < knots[i + d + 1])
                add(r, r, overlapAdd(knots[i + d + 1], -physical.lo), -step,
                    overlapAdd(knots[i + d + 1], -knots[i + 1]));
        }
        previous = std::move(current);
    }
    return previous;
}

constexpr int maxPhysicalHeightDegree = 6;
constexpr int maxOverlapDegree = 2 * maxPhysicalHeightDegree;

struct OverlapPolynomial {
    int u, v;
    // Bound the degree of each native factor after homogenization.
    std::array<std::array<double, maxOverlapDegree + 1>, maxOverlapDegree + 1> coefficients{};
};

OverlapPolynomial overlapProduct(const OverlapPolynomial &a, const OverlapPolynomial &b) {
    OverlapPolynomial result{a.u + b.u, a.v + b.v};
    if (result.u > maxOverlapDegree || result.v > maxOverlapDegree) throw OverlapArithmeticFailed{};
    for (int i = 0; i <= a.u; ++i) for (int j = 0; j <= a.v; ++j) {
        const double lhs = a.coefficients[i][j];
        if (lhs == 0.) continue;
        for (int k = 0; k <= b.u; ++k) for (int l = 0; l <= b.v; ++l) {
            const double rhs = b.coefficients[k][l];
            if (rhs == 0.) continue;
            auto &c = result.coefficients[i + k][j + l];
            c = overlapAdd(c, overlapMultiply(lhs, rhs));
        }
    }
    return result;
}

struct OverlapAxis {
    const std::vector<double> &knots;
    int degree, count;
};

OverlapAxis overlapAxis(const BSplineSurface &s, bool u) {
    if (u) return {s.uKnots(), s.uDegree(), s.uPoleCount()};
    return {s.vKnots(), s.vDegree(), s.vPoleCount()};
}

struct CommonOverlapSpan {
    int a, b;
    Interval physical;
};

std::vector<CommonOverlapSpan> commonOverlapSpans(OverlapAxis a, OverlapAxis b) {
    std::vector<CommonOverlapSpan> result;
    int firstB = b.degree;
    for (int i = a.degree; i < a.count; ++i) {
        cancellationPoint();
        if (a.knots[i] == a.knots[i + 1]) continue;
        while (firstB < b.count && b.knots[firstB + 1] < a.knots[i]) ++firstB;
        for (int j = firstB; j < b.count && b.knots[j] <= a.knots[i + 1]; ++j) {
            if (b.knots[j] == b.knots[j + 1]) continue;
            if (result.size() == maxCells) throw OverlapArithmeticFailed{};
            result.push_back({i, j, {std::max(a.knots[i], b.knots[j]), std::min(a.knots[i + 1], b.knots[j + 1])}});
        }
    }
    return result;
}

std::array<OverlapPolynomial, 2> overlapHeight(const BSplineSurface &s, bool direct, int sx, int sy,
    Interval x, Interval y, int heightAxis) {
    const auto au = overlapAxis(s, direct), av = overlapAxis(s, !direct);
    const auto bu = overlapBasis(au.knots, sx, au.degree, x), bv = overlapBasis(av.knots, sy, av.degree, y);
    OverlapPolynomial height{au.degree, av.degree}, weight = height;
    for (int i = 0; i <= au.degree; ++i) for (int j = 0; j <= av.degree; ++j) {
        const int px = sx - au.degree + i, py = sy - av.degree + j;
        const int u = direct ? px : py, v = direct ? py : px;
        const double w = s.isRational() ? s.weight(u, v) : 1.;
        const double h = overlapMultiply(w, s.pole(u, v)[heightAxis]);
        for (int k = 0; k <= au.degree; ++k) for (int l = 0; l <= av.degree; ++l) {
            const double b = overlapMultiply(bu[i][k], bv[j][l]);
            auto &hc = height.coefficients[k][l], &wc = weight.coefficients[k][l];
            hc = overlapAdd(hc, overlapMultiply(b, h));
            wc = overlapAdd(wc, overlapMultiply(b, w));
        }
    }
    return {height, weight};
}

bool equalAffineGraphHeight(const BSplineSurface &a, bool aDirect, const BSplineSurface &b, bool bDirect,
    int heightAxis) {
    const auto xs = commonOverlapSpans(overlapAxis(a, aDirect), overlapAxis(b, bDirect));
    const auto ys = commonOverlapSpans(overlapAxis(a, !aDirect), overlapAxis(b, !bDirect));
    if (xs.empty() || ys.empty() || xs.size() > maxCells / ys.size()) return false;
    for (const auto &x : xs) for (const auto &y : ys) {
        cancellationPoint();
        const auto [az, aw] = overlapHeight(a, aDirect, x.a, y.a, x.physical, y.physical, heightAxis);
        const auto [bz, bw] = overlapHeight(b, bDirect, x.b, y.b, x.physical, y.physical, heightAxis);
        const auto left = overlapProduct(az, bw), right = overlapProduct(bz, aw);
        // Closed-cell equality includes one-sided normals at common knot
        // boundaries. On a collapsed axis the constant and linear terms
        // suffice for value and first derivatives at that boundary.
        const int du = x.physical.lo < x.physical.hi ? left.u : std::min(left.u, 1);
        const int dv = y.physical.lo < y.physical.hi ? left.v : std::min(left.v, 1);
        for (int i = 0; i <= du; ++i) for (int j = 0; j <= dv; ++j)
            if (left.coefficients[i][j] != right.coefficients[i][j]) return false;
    }
    return true;
}

std::optional<bool> affineOverlapLayout(const BSplineSurface &s, int x, int y) {
    const auto layout = [&](int u, int v) {
        if (!s.isRational()) return graphCoordinates(s, u, v);
        return affineRationalCoordinate(s, true, u) && affineRationalCoordinate(s, false, v);
    };
    if (layout(x, y)) return true;
    if (layout(y, x)) return false;
    return {};
}

constexpr std::size_t heightTermCount(int degree) { return std::size_t((degree + 1) * (degree + 2) / 2); }
constexpr auto heightPowers = [] {
    std::array<std::array<int, 2>, heightTermCount(maxPhysicalHeightDegree)> powers{};
    std::size_t i = 0;
    for (int order = 0; order <= maxPhysicalHeightDegree; ++order)
        for (int y = 0; y <= order; ++y) powers[i++] = {order - y, y};
    return powers;
}();

// For physical height of degree d, compare Z W^(d-1) with the sum of
// c_ab X^a Y^b W^(d-a-b), where X and Y are centered and scaled.
using HeightEquations = std::array<OverlapPolynomial, heightPowers.size() + 1>;

struct OverlapCoordinates {
    OverlapPolynomial x, y, z, weight;
};

OverlapCoordinates overlapCoordinates(const BSplineSurface &s, int i, int j,
    int axisX, int axisY, int axisZ, Interval x, Interval y) {
    const auto &ku = s.uKnots(), &kv = s.vKnots();
    const Interval nativeU{ku[i], ku[i + 1]}, nativeV{kv[j], kv[j + 1]};
    auto [px, w] = overlapHeight(s, true, i, j, nativeU, nativeV, axisX);
    auto py = overlapHeight(s, true, i, j, nativeU, nativeV, axisY)[0];
    const auto pz = overlapHeight(s, true, i, j, nativeU, nativeV, axisZ)[0];
    const double dx = overlapAdd(x.hi, -x.lo), dy = overlapAdd(y.hi, -y.lo);
    for (int k = 0; k <= w.u; ++k) for (int l = 0; l <= w.v; ++l) {
        px.coefficients[k][l] = overlapDivide(overlapAdd(px.coefficients[k][l],
            -overlapMultiply(w.coefficients[k][l], x.lo)), dx);
        py.coefficients[k][l] = overlapDivide(overlapAdd(py.coefficients[k][l],
            -overlapMultiply(w.coefficients[k][l], y.lo)), dy);
    }
    return {px, py, pz, w};
}

void compactHeightCoordinates(OverlapCoordinates &coordinates) {
    // Only exactly zero coefficients may lower a homogeneous degree.
    auto &[px, py, pz, w] = coordinates;
    for (auto *p : {&px, &py, &pz, &w}) {
        int u = 0, v = 0;
        for (int i = 0; i <= p->u; ++i) for (int j = 0; j <= p->v; ++j) {
            if (p->coefficients[i][j] == 0.) continue;
            u = std::max(u, i);
            v = std::max(v, j);
        }
        p->u = u;
        p->v = v;
    }
}

void boundHeightEquations(HeightEquations &terms, std::size_t count) {
    // Include powers that are present only on the left of the identity.
    auto &rhs = terms[count];
    for (std::size_t k = 0; k < count; ++k) {
        rhs.u = std::max(rhs.u, terms[k].u);
        rhs.v = std::max(rhs.v, terms[k].v);
    }
}

HeightEquations heightEquations(OverlapCoordinates coordinates, int degree) {
    if (degree > 4) compactHeightCoordinates(coordinates);
    const auto &[px, py, pz, w] = coordinates;
    HeightEquations terms{w, px, py};
    for (int order = 2; order <= degree; ++order) {
        const auto lowerTerms = heightTermCount(order - 1);
        const auto previousPowers = heightTermCount(order - 2);
        // Append the pure degree-order powers in descending X order.
        // The previous highest powers have not yet been multiplied by W.
        for (int yPower = 0; yPower < order; ++yPower)
            terms[lowerTerms + yPower] = overlapProduct(terms[previousPowers + yPower], px);
        terms[lowerTerms + order] = overlapProduct(terms[lowerTerms - 1], py);
        if (order == degree) terms[heightTermCount(degree)] = overlapProduct(pz, terms[0]);
        for (std::size_t k = 0; k < lowerTerms; ++k) terms[k] = overlapProduct(terms[k], w);
    }
    boundHeightEquations(terms, heightTermCount(degree));
    return terms;
}

using HeightCoefficients = std::array<double, heightPowers.size()>;

// Substitution needs only the terms present in the derived model.
HeightEquations sparseHeightEquations(OverlapCoordinates coordinates, int degree, const HeightCoefficients &coefficients) {
    compactHeightCoordinates(coordinates);
    const auto &[px, py, pz, w] = coordinates;
    OverlapPolynomial one{0, 0};
    one.coefficients[0][0] = 1.;
    std::vector<OverlapPolynomial> xp{one}, yp{one}, wp{one};
    const auto power = [](std::vector<OverlapPolynomial> &powers, const OverlapPolynomial &p, int exponent)
                           -> const OverlapPolynomial & {
        while (int(powers.size()) <= exponent) powers.push_back(overlapProduct(powers.back(), p));
        return powers[std::size_t(exponent)];
    };
    HeightEquations terms{};
    const auto count = heightTermCount(degree);
    for (std::size_t k = 0; k < count; ++k) {
        if (coefficients[k] == 0.) continue;
        const auto [i, j] = heightPowers[k];
        terms[k] = overlapProduct(overlapProduct(power(xp, px, i), power(yp, py, j)), power(wp, w, degree - i - j));
    }
    terms[count] = overlapProduct(pz, power(wp, w, degree - 1));
    boundHeightEquations(terms, count);
    return terms;
}

template<class Equations>
bool certifyHeightEquations(const BSplineSurface &s, const HeightCoefficients &coefficients,
    std::size_t count, const Equations &equationsAt) {
    const auto u = overlapAxis(s, true), v = overlapAxis(s, false);
    if (u.degree > 3 || v.degree > 3) return false;
    std::size_t cells = 0;
    for (int i = u.degree; i < u.count; ++i) for (int j = v.degree; j < v.count; ++j) {
        cancellationPoint();
        if (u.knots[i] == u.knots[i + 1] || v.knots[j] == v.knots[j + 1]) continue;
        if (++cells > maxCells) return false;
        const auto equations = equationsAt(i, j);
        const auto &rhs = equations[count];
        for (int k = 0; k <= rhs.u; ++k) for (int l = 0; l <= rhs.v; ++l) {
            double residual = rhs.coefficients[k][l];
            for (std::size_t col = 0; col < count; ++col)
                residual = overlapAdd(residual, -overlapMultiply(coefficients[col], equations[col].coefficients[k][l]));
            if (residual != 0.) return false;
        }
    }
    return cells != 0;
}

bool certifyPhysicalHeight(const BSplineSurface &s, int axisX, int axisY, int axisZ,
    Interval x, Interval y, const OverlapPolynomial &height, int degree) {
    const std::size_t count = heightTermCount(degree);
    HeightCoefficients coefficients{};
    for (std::size_t col = 0; col < count; ++col) {
        const auto [a, b] = heightPowers[col];
        coefficients[col] = height.coefficients[a][b];
    }
    return certifyHeightEquations(s, coefficients, count, [&](int i, int j) {
        auto coordinates = overlapCoordinates(s, i, j, axisX, axisY, axisZ, x, y);
        return degree > 4 ? sparseHeightEquations(std::move(coordinates), degree, coefficients)
                          : heightEquations(std::move(coordinates), degree);
    });
}

int firstOverlapSpan(OverlapAxis axis) {
    int span = axis.degree;
    while (span < axis.count && axis.knots[span] == axis.knots[span + 1]) ++span;
    return span;
}

bool equalDirectPhysicalHeight(const BSplineSurface &affine, bool direct, const BSplineSurface &other,
    int axisX, int axisY, int axisZ) {
    const auto u = overlapAxis(affine, direct), v = overlapAxis(affine, !direct);
    const int i = firstOverlapSpan(u), j = firstOverlapSpan(v);
    if (i == u.count || j == v.count) return false;
    const Interval x{u.knots[i], u.knots[i + 1]}, y{v.knots[j], v.knots[j + 1]};
    auto [height, weight] = overlapHeight(affine, direct, i, j, x, y, axisZ);
    for (int k = 0; k <= weight.u; ++k) for (int l = 0; l <= weight.v; ++l)
        if ((k || l) && weight.coefficients[k][l] != 0.) return false;
    const double w = weight.coefficients[0][0];
    int degree = 2;
    for (int k = 0; k <= height.u; ++k) for (int l = 0; l <= height.v; ++l) {
        auto &c = height.coefficients[k][l];
        if (c != 0.) degree = std::max(degree, k + l);
        if (degree > maxPhysicalHeightDegree) return false;
        c = overlapDivide(c, w);
    }
    return certifyPhysicalHeight(affine, axisX, axisY, axisZ, x, y, height, degree)
        && certifyPhysicalHeight(other, axisX, axisY, axisZ, x, y, height, degree);
}

// Derive a candidate from the homogeneous coefficient equations on one
// open cell. Fraction-free elimination uses only checked operations; the
// candidate is accepted only after independent substitution on every cell.
std::optional<HeightCoefficients> deriveHeightCoefficients(const HeightEquations &equations, std::size_t count,
    bool preferUnscaled = false) {
    using Equation = std::array<double, heightPowers.size() + 1>;
    std::array<Equation, heightPowers.size()> pivots{};
    const auto &rhs = equations[count];
    std::size_t rank = 0;
    for (int order = 0; order <= rhs.u + rhs.v && rank < count; ++order) {
        for (int u = std::max(0, order - rhs.v); u <= std::min(rhs.u, order); ++u) {
            Equation row{};
            for (std::size_t k = 0; k <= count; ++k)
                row[k] = equations[k].coefficients[u][order - u];
            for (std::size_t k = 0; k < count; ++k) {
                if (row[k] == 0.) continue;
                const auto &pivot = pivots[k];
                if (pivot[k] == 0.) {
                    pivots[k] = row;
                    ++rank;
                    break;
                }
                if (preferUnscaled || count > heightTermCount(4)) {
                    // Exact, unscaled elimination avoids multiplying an
                    // entire row by each successive pivot in a larger system.
                    // Keep the original row for the fraction-free fallback.
                    auto reduced = row;
                    try {
                        const double factor = overlapDivide(row[k], pivot[k]);
                        for (std::size_t l = k + 1; l <= count; ++l)
                            reduced[l] = overlapAdd(row[l], -overlapMultiply(factor, pivot[l]));
                        reduced[k] = 0.;
                        row = reduced;
                        continue;
                    } catch (const OverlapArithmeticFailed &) {
                    }
                }
                for (std::size_t l = k + 1; l <= count; ++l)
                    row[l] = overlapAdd(overlapMultiply(pivot[k], row[l]), -overlapMultiply(row[k], pivot[l]));
                row[k] = 0.;
            }
            if (rank == count) break;
        }
    }
    if (rank != count) return {};
    HeightCoefficients coefficients{};
    for (std::size_t k = count; k-- > 0;) {
        const auto &row = pivots[k];
        double value = row[count];
        for (std::size_t l = k + 1; l < count; ++l)
            value = overlapAdd(value, -overlapMultiply(row[l], coefficients[l]));
        coefficients[k] = overlapDivide(value, row[k]);
    }
    return coefficients;
}

struct PhysicalHeightModel {
    Interval x, y;
    OverlapPolynomial height;
    int degree;
};

std::optional<PhysicalHeightModel> derivePhysicalHeightModel(const BSplineSurface &s,
    int axisX, int axisY, int axisZ, int degree) {
    const auto u = overlapAxis(s, true), v = overlapAxis(s, false);
    if (degree < 2 || degree > maxPhysicalHeightDegree || u.degree > 3 || v.degree > 3) return {};
    const int i = firstOverlapSpan(u), j = firstOverlapSpan(v);
    if (i == u.count || j == v.count) return {};
    try {
        const auto origin = s.pole(0, 0);
        const Interval x{origin[axisX], overlapAdd(origin[axisX], 1.)};
        const Interval y{origin[axisY], overlapAdd(origin[axisY], 1.)};
        const auto equations = heightEquations(overlapCoordinates(s, i, j, axisX, axisY, axisZ, x, y), degree);
        const auto count = heightTermCount(degree);
        const auto coefficients = deriveHeightCoefficients(equations, count);
        if (!coefficients) return {};
        OverlapPolynomial height{degree, degree};
        for (std::size_t col = 0; col < count; ++col) {
            const auto [a, b] = heightPowers[col];
            height.coefficients[a][b] = (*coefficients)[col];
        }
        if (certifyPhysicalHeight(s, axisX, axisY, axisZ, x, y, height, degree))
            return PhysicalHeightModel{x, y, height, degree};
    } catch (const OverlapArithmeticFailed &) {
        return {};
    }
    return {};
}

std::optional<bool> equalNonaffineHeight(const BSplineSurface &a, const BSplineSurface &b, int axisX, int axisY, int axisZ) {
    // This overlap recognizer uses the quadratic native chart family.
    if (std::max({a.uDegree(), a.vDegree(), b.uDegree(), b.vDegree()}) > 2) return {};
    for (int degree = 2; degree <= maxPhysicalHeightDegree; ++degree) {
        const auto model = derivePhysicalHeightModel(a, axisX, axisY, axisZ, degree);
        if (!model) continue;
        try {
            return certifyPhysicalHeight(b, axisX, axisY, axisZ, model->x, model->y, model->height, degree);
        } catch (const OverlapArithmeticFailed &) {
            // The next degree has different homogeneous coefficients and pivots.
        }
    }
    return {};
}

struct RationalHeightOrder {
    int numerator, denominator;

    std::size_t numeratorTerms() const { return heightTermCount(numerator); }
    std::size_t terms() const { return numeratorTerms() + heightTermCount(denominator) - 1; }
};

HeightEquations rationalHeightEquations(const OverlapCoordinates &coordinates, RationalHeightOrder order) {
    // N and D are lifted to the shared homogeneous degree. In the
    // normalized physical frame the constant term of D is one.
    const int degree = std::max(order.numerator, order.denominator + 1);
    auto equations = heightEquations(coordinates, degree);
    const auto rhs = equations[heightTermCount(degree)];
    const auto denominator = heightEquations(coordinates, degree - 1);
    const auto n = order.numeratorTerms();
    for (std::size_t k = 1; k < heightTermCount(order.denominator); ++k)
        equations[n + k - 1] = overlapProduct(coordinates.z, denominator[k]);
    equations[order.terms()] = rhs;
    return equations;
}

std::array<Interval, 2> overlapPhysicalBounds(const BSplineSurface &s, int axisX, int axisY) {
    std::array<Interval, 2> bounds{{{infinity, -infinity}, {infinity, -infinity}}};
    for (int i = 0; i < s.uPoleCount(); ++i) for (int j = 0; j < s.vPoleCount(); ++j) {
        cancellationPoint();
        const auto p = s.pole(i, j);
        const std::array<double, 2> xy{p[axisX], p[axisY]};
        for (std::size_t axis = 0; axis < bounds.size(); ++axis) {
            bounds[axis].lo = std::min(bounds[axis].lo, xy[axis]);
            bounds[axis].hi = std::max(bounds[axis].hi, xy[axis]);
        }
    }
    return bounds;
}

bool positiveAffineOverlapDenominator(const BSplineSurface &s, int axisX, int axisY, Interval x, Interval y,
    const HeightCoefficients &coefficients, std::size_t offset) {
    const I dx = I(x.hi) - I(x.lo), dy = I(y.hi) - I(y.lo);
    const I a(coefficients[offset]), b(coefficients[offset + 1]);
    // An affine denominator on a positive-weight chart is a convex
    // combination of its values at the Euclidean control points.
    for (int i = 0; i < s.uPoleCount(); ++i) for (int j = 0; j < s.vPoleCount(); ++j) {
        cancellationPoint();
        const auto p = s.pole(i, j);
        const I xn = (I(p[axisX]) - I(x.lo)) / dx, yn = (I(p[axisY]) - I(y.lo)) / dy;
        const I denominator = I(1.) - a * xn - b * yn;
        if (!finite(denominator) || !(denominator.lo > 0.)) return false;
    }
    return true;
}

bool positivePolynomialOverlapDenominator(const std::array<Interval, 2> &bounds,
    Interval x, Interval y, const HeightCoefficients &coefficients, std::size_t offset, int degree) {
    const auto normalized = [](double coordinate, Interval reference) {
        return (I(coordinate) - I(reference.lo)) / (I(reference.hi) - I(reference.lo));
    };
    Polynomial px(1, 0), py(0, 1);
    px.at(0, 0) = normalized(bounds[0].lo, x);
    px.at(1, 0) = normalized(bounds[0].hi, x);
    py.at(0, 0) = normalized(bounds[1].lo, y);
    py.at(0, 1) = normalized(bounds[1].hi, y);
    std::vector<Polynomial> xp{Polynomial(I(1.))}, yp{Polynomial(I(1.))};
    for (int k = 1; k <= degree; ++k) {
        xp.push_back(product(xp.back(), px));
        yp.push_back(product(yp.back(), py));
    }
    Polynomial denominator(I(1.));
    for (std::size_t k = 1; k < heightTermCount(degree); ++k) {
        const double coefficient = coefficients[offset + k - 1];
        if (coefficient == 0.) continue;
        const auto [a, b] = heightPowers[k];
        denominator = sum(std::move(denominator), scaled(product(xp[a], yp[b]), I(-coefficient)));
    }
    // Each projected chart lies in its Euclidean control hull. The
    // Bernstein enclosure proves positivity on the surrounding rectangle.
    const I enclosure = hull(denominator);
    return finite(enclosure) && enclosure.lo > 0.;
}

// Large rational systems can exceed the mantissa during exact
// elimination. A scaled solve only proposes coefficients; callers
// publish a model solely after exact substitution on every cell.
std::optional<HeightCoefficients> estimateHeightCoefficients(const HeightEquations &equations, std::size_t count) {
    using Equation = std::array<double, heightPowers.size() + 1>;
    const auto &rhs = equations[count];
    Equation largest{};
    std::vector<Equation> rows;
    for (int u = 0; u <= rhs.u; ++u) for (int v = 0; v <= rhs.v; ++v) {
        Equation row{};
        for (std::size_t i = 0; i <= count; ++i) {
            row[i] = equations[i].coefficients[u][v];
            largest[i] = std::max(largest[i], std::abs(row[i]));
        }
        rows.push_back(row);
    }
    if (rows.size() < count) return {};
    std::array<int, heightPowers.size() + 1> exponents{};
    for (std::size_t i = 0; i <= count; ++i) {
        if (!(largest[i] > 0.)) return {};
        exponents[i] = std::ilogb(largest[i]);
        for (auto &row : rows) row[i] = std::scalbn(row[i], -exponents[i]);
    }
    for (std::size_t i = 0; i < count; ++i) {
        cancellationPoint();
        const auto pivot = std::max_element(rows.begin() + i, rows.end(),
            [i](const Equation &a, const Equation &b) { return std::abs(a[i]) < std::abs(b[i]); });
        if (!(std::abs((*pivot)[i]) > 1e-12)) return {};
        std::iter_swap(rows.begin() + i, pivot);
        for (std::size_t j = i + 1; j < rows.size(); ++j) {
            const double factor = rows[j][i] / rows[i][i];
            for (std::size_t k = i + 1; k <= count; ++k)
                rows[j][k] -= factor * rows[i][k];
        }
    }
    HeightCoefficients result{};
    for (std::size_t i = count; i-- > 0;) {
        double value = rows[i][count];
        for (std::size_t j = i + 1; j < count; ++j) value -= rows[i][j] * result[j];
        result[i] = value / rows[i][i];
    }
    for (std::size_t i = 0; i < count; ++i) {
        result[i] = std::scalbn(result[i], exponents[count] - exponents[i]);
        if (!std::isfinite(result[i])) return {};
    }
    return result;
}

HeightCoefficients dyadicHeightCandidate(HeightCoefficients candidate, RationalHeightOrder order, int bits) {
    const auto roundBlock = [&](std::size_t begin, std::size_t end) {
        double largest = 0.;
        for (std::size_t i = begin; i < end; ++i) largest = std::max(largest, std::abs(candidate[i]));
        if (!(largest > 0.)) return;
        const int exponent = bits - std::ilogb(largest);
        for (std::size_t i = begin; i < end; ++i)
            candidate[i] = std::scalbn(std::round(std::scalbn(candidate[i], exponent)), -exponent);
    };
    roundBlock(0, order.numeratorTerms());
    roundBlock(order.numeratorTerms(), order.terms());
    return candidate;
}

bool equalRationalPhysicalHeight(const BSplineSurface &a, const BSplineSurface &b,
    int axisX, int axisY, int axisZ) {
    const auto u = overlapAxis(a, true), v = overlapAxis(a, false);
    if (u.degree > 3 || v.degree > 3 || b.uDegree() > 3 || b.vDegree() > 3) return false;
    const int i = firstOverlapSpan(u), j = firstOverlapSpan(v);
    if (i == u.count || j == v.count) return false;
    const auto bounds = overlapPhysicalBounds(a, axisX, axisY);
    const auto [x, y] = bounds;
    if (!(x.lo < x.hi && y.lo < y.hi)) return false;
    constexpr std::array<RationalHeightOrder, 9> orders{{
        {2,1}, {2,2}, {3,1}, {3,2}, {4,1}, {4,2}, {2,3}, {3,3}, {4,3}
    }};
    for (auto order : orders) {
        try {
            const auto count = order.terms();
            const auto equationsAt = [&](const BSplineSurface &s, int tu, int tv) {
                return rationalHeightEquations(overlapCoordinates(s, tu, tv, axisX, axisY, axisZ, x, y),
                    order);
            };
            const auto equations = equationsAt(a, i, j);
            const auto certify = [&](const BSplineSurface &s, const HeightCoefficients &coefficients) {
                const auto offset = order.numeratorTerms();
                const bool positive = order.denominator == 1
                    ? positiveAffineOverlapDenominator(s, axisX, axisY, x, y, coefficients, offset)
                    : positivePolynomialOverlapDenominator(&s == &a ? bounds : overlapPhysicalBounds(s, axisX, axisY),
                                                            x, y, coefficients, offset, order.denominator);
                return positive && certifyHeightEquations(s, coefficients, count,
                    [&](int tu, int tv) { return equationsAt(s, tu, tv); });
            };
            try {
                const auto coefficients = deriveHeightCoefficients(equations, count, order.numerator > 2);
                if (coefficients && certify(a, *coefficients) && certify(b, *coefficients)) return true;
            } catch (const OverlapArithmeticFailed &) {
                // Exact elimination may grow even when a small model exists.
            }
            if (order.numerator < 4 && order.denominator < 3) continue;
            const auto estimate = estimateHeightCoefficients(equations, count);
            if (!estimate) continue;
            for (int bits : {48, 40, 32, 24, 16}) {
                const auto candidate = dyadicHeightCandidate(*estimate, order, bits);
                try {
                    if (certify(a, candidate) && certify(b, candidate)) return true;
                } catch (const OverlapArithmeticFailed &) {
                    // This candidate did not give an exact identity.
                }
            }
        } catch (const OverlapArithmeticFailed &) {
            // Another order may admit exact coefficients.
        }
    }
    return false;
}

// Identical native charts give the same contact and oriented normal.
// Different parametrizations of one horizontal plane also agree exactly
// wherever their projections overlap.
bool compatibleProjectedOverlap(const BSplineSurface &a, const BSplineSurface &b,
    int axisX, int axisY, int heightAxis) {
    if (&a == &b) return true;
    const bool sameBasis = a.uDegree() == b.uDegree() && a.vDegree() == b.vDegree() &&
        a.uKnots() == b.uKnots() && a.vKnots() == b.vKnots() &&
        a.uPoleCount() == b.uPoleCount() && a.vPoleCount() == b.vPoleCount();
    if (sameBasis) {
        const auto sameProjectedPole = [](const Vec3 &p, const Vec3 &q) {
            return p.x() == q.x() && p.y() == q.y() && p.z() == q.z();
        };
        bool identical = true;
        for (int i = 0; i < a.uPoleCount() && identical; ++i) for (int j = 0; j < a.vPoleCount(); ++j) {
            cancellationPoint();
            if (!sameProjectedPole(a.pole(i, j), b.pole(i, j)) || a.weight(i, j) != b.weight(i, j)) {
                identical = false;
                break;
            }
        }
        if (identical) return true;
    }
    const auto height = constantGraphHeight(a, heightAxis);
    if (height && height == constantGraphHeight(b, heightAxis)) return true;
    const auto aDirect = affineOverlapLayout(a, axisX, axisY), bDirect = affineOverlapLayout(b, axisX, axisY);
    try {
        if (aDirect && bDirect) return equalAffineGraphHeight(a, *aDirect, b, *bDirect, heightAxis);
        if (aDirect && equalDirectPhysicalHeight(a, *aDirect, b, axisX, axisY, heightAxis)) return true;
        if (bDirect && equalDirectPhysicalHeight(b, *bDirect, a, axisX, axisY, heightAxis)) return true;
        if (!aDirect && !bDirect) {
            const auto polynomial = equalNonaffineHeight(a, b, axisX, axisY, heightAxis);
            if (polynomial) return *polynomial;
        }
    } catch (const OverlapArithmeticFailed &) {
        if (aDirect && bDirect) return false;
    }
    try {
        return equalRationalPhysicalHeight(a, b, axisX, axisY, heightAxis);
    } catch (const OverlapArithmeticFailed &) {
        return false;
    }
}

bool overlapsInterior(const Rectangle &a, const Rectangle &b) {
    return std::min(a.u.hi, b.u.hi) > std::max(a.u.lo, b.u.lo)
        && std::min(a.v.hi, b.v.hi) > std::max(a.v.lo, b.v.lo);
}

struct ProjectedStrip {
    const BSplineSurface *surface;
    Rectangle footprint;
    bool coupled;
    bool exactY;
    bool weightVariesV;
    bool flat;
};

// Power coefficients in normalized source coordinates. A low total
// degree keeps the rational inverse and the bicubic image within the
// existing degree budget even when the native tensor has a higher degree.
struct SourceHeightPolynomial {
    int degree = 2;
    std::array<std::array<I, 4>, 4> coefficients{};
};

std::optional<SourceHeightPolynomial> sourceHeightPolynomial(const BSplineSurface &s, const Affine &baseline) {
    SourceHeightPolynomial height;
    const int pu = s.uDegree(), pv = s.vDegree();
    for (int u = 0; u <= pu; ++u) for (int v = 0; v <= pv; ++v) {
        // Forward differences convert the native Bézier controls to
        // power coefficients. Only a proved zero may lower the degree.
        std::array<double, 16> terms{};
        std::size_t count = 0;
        bool productsExact = true;
        I difference;
        for (int i = 0; i <= u; ++i) for (int j = 0; j <= v; ++j) {
            const double factor = ((u - i + v - j) % 2 ? -1. : 1.) *
                surface_bounds::choose(u, i) * surface_bounds::choose(v, j);
            const double z = s.pole(i, j).z();
            difference = difference + I(factor) * I(z);
            productsExact = exactProduct(factor, z, terms[count++]) && productsExact;
        }
        if (productsExact && exactZeroSum(terms.data(), count)) continue;
        if (u + v > 3) return {};
        height.degree = std::max(height.degree, u + v);
        height.coefficients[u][v] =
            I(surface_bounds::choose(pu, u) * surface_bounds::choose(pv, v)) * difference;
    }
    const auto u = s.uDomain(), v = s.vDomain();
    height.coefficients[0][0] = height.coefficients[0][0] - at(baseline, u.lo, v.lo);
    height.coefficients[1][0] = height.coefficients[1][0] - baseline.du * (I(u.hi) - I(u.lo));
    height.coefficients[0][1] = height.coefficients[0][1] - baseline.dv * (I(v.hi) - I(v.lo));
    return height;
}

Polynomial polynomialPower(const Polynomial &p, int exponent) {
    if (!exponent) return Polynomial(I(1.));
    if (exponent == 1) return p;
    const auto half = polynomialPower(p, exponent / 2);
    auto squared = product(half, half);
    return exponent % 2 ? product(squared, p) : std::move(squared);
}

Polynomial sourceHeightOn(const BSplineSurface &s, const SourceHeightPolynomial &height,
                          const std::array<Polynomial, 2> &uv, const Polynomial &w) {
    const auto normalize = [&](const Polynomial &coordinate, Interval domain) {
        return scaled(sum(coordinate, scaled(w, I(domain.lo)), true), I(1.) / (I(domain.hi) - I(domain.lo)));
    };
    const auto u = normalize(uv[0], s.uDomain()), v = normalize(uv[1], s.vDomain());
    Polynomial result;
    for (int i = 0; i <= height.degree; ++i) for (int j = 0; j <= height.degree - i; ++j) {
        const I coefficient = height.coefficients[i][j];
        if (exact(coefficient, 0.)) continue;
        const auto term = product(polynomialPower(u, i), polynomialPower(v, j));
        result = sum(std::move(result),
            scaled(product(term, polynomialPower(w, height.degree - i - j)), coefficient));
    }
    return result;
}

struct SourceProjectionAdjustment {
    std::array<I, 2> shift;
    std::optional<int> fixedAxis;
    I heightRange;
    I heightError;
    SourceHeightPolynomial relief{};
    int degree = 2;
};

std::optional<SourceProjectionAdjustment> sourceProjectionAdjustment(const SourceGraph &source,
    const std::array<Affine, 2> &world, Rectangle box, const std::optional<Affine> &baseline = {}) {
    const auto &[a, b] = world;
    const I determinant = a.du * b.dv - a.dv * b.du;
    if (!strictSign(determinant)) return {};
    const auto &[x, y] = source.zUV;
    const std::array<I, 2> shift{signedQuotient(b.dv * x - a.dv * y, determinant),
                                 signedQuotient(a.du * y - b.du * x, determinant)};
    if (!finite(shift[0]) || !finite(shift[1])) return {};
    const auto &s = *source.surface;
    if (s.uDegree() > 3 || s.vDegree() > 3) return {};
    for (int fixed = 0; fixed < 2; ++fixed) {
        if (baseline || !exact(shift[fixed], 0.)) continue;
        bool independent = true;
        for (int i = 0; i < s.uPoleCount() && independent; ++i) for (int j = 0; j < s.vPoleCount(); ++j) {
            cancellationPoint();
            const auto reference = fixed == 0 ? s.pole(i, 0) : s.pole(0, j);
            if (s.pole(i, j).z() != reference.z()) {
                independent = false;
                break;
            }
        }
        if (independent)
            return SourceProjectionAdjustment{shift, fixed, {}, {}, {},
                std::max(2, fixed == 0 ? s.uDegree() : s.vDegree())};
    }

    // Every actual inverse satisfies q = q0 - shift*relief(q).
    // Bound the segment from q to q0 using the relative control heights.
    const Affine reference = baseline.value_or(Affine{});
    const auto relief = sourceHeightPolynomial(s, reference);
    if (!relief) return {};
    const auto z = [&](int i, int j) {
        const auto p = s.pole(i, j);
        return std::array<I, 1>{I(p.z()) - at(reference, p.x(), p.y())};
    };
    I height(infinity, -infinity);
    for (int i = 0; i < s.uPoleCount(); ++i) for (int j = 0; j < s.vPoleCount(); ++j) {
        const I value = z(i, j).front();
        height.lo = std::min(height.lo, value.lo);
        height.hi = std::max(height.hi, value.hi);
    }
    const I u(box.u.lo, box.u.hi), v(box.v.lo, box.v.hi);
    const I expandedU = hull({u, u + shift[0] * height});
    const I expandedV = hull({v, v + shift[1] * height});
    const P us{I(expandedU.lo), I(expandedU.hi)}, vs{I(expandedV.lo), I(expandedV.hi)};
    const auto du = derivativeTensor<1>(s, s.uDegree(), s.vDegree(), us, vs, true, UVLayout::Direct, z).front();
    const auto dv = derivativeTensor<1>(s, s.uDegree(), s.vDegree(), us, vs, false, UVLayout::Direct, z).front();
    const double rate = magnitude(hull(sum(scaled(du, shift[0]), scaled(dv, shift[1]))));
    // The same directional derivative bounds the scalar fixed-point
    // map t -> z(q0 - shift*t) on the original box.
    const I error = I(rate) * I(magnitude(height));
    if (!(rate < 1.) || !finite(error)) return {};
    return SourceProjectionAdjustment{shift, {}, height, {-error.hi, error.hi}, *relief, relief->degree};
}

// Absorb a tangent plane of the source graph into the affine projection.
// This changes the inverse equation, leaving the projected point and
// the normal offset identical. Only the remaining relief must contract.
Affine sourceReliefBaseline(const SourceGraph &graph, Rectangle box) {
    const auto &s = *graph.surface;
    const auto middle = [](I range) { return I(.5 * range.lo + .5 * range.hi); };
    const I u = middle(I(box.u.lo, box.u.hi)), v = middle(I(box.v.lo, box.v.hi));
    const P us{u}, vs{v};
    const auto z = [&](int i, int j) { return std::array<I, 1>{I(s.pole(i, j).z())}; };
    const I du = middle(hull(derivativeTensor<1>(s, s.uDegree(), s.vDegree(), us, vs,
        true, UVLayout::Direct, z).front()));
    const I dv = middle(hull(derivativeTensor<1>(s, s.uDegree(), s.vDegree(), us, vs,
        false, UVLayout::Direct, z).front()));
    const I origin = middle(hull(graphZOn(graph, us, vs)) - du * u - dv * v);
    return {origin, du, dv};
}

Affine addReliefBaseline(const Affine &base, I scale, const Affine &baseline) {
    return {base.origin + scale * baseline.origin, base.du + scale * baseline.du, base.dv + scale * baseline.dv};
}

Polynomial shearHeight(const SourceGraph &source, int fixed, const Polynomial &parameter, const Polynomial *weight) {
    const auto &s = *source.surface;
    const auto &knots = fixed == 0 ? s.uKnots() : s.vKnots();
    const int degree = fixed == 0 ? s.uDegree() : s.vDegree();
    const auto values = basis(knots, degree, parameter, degree, weight);
    Polynomial height;
    for (int i = 0; i <= degree; ++i)
        height = sum(std::move(height), scaled(values[std::size_t(i)], I((fixed == 0 ? s.pole(i, 0) : s.pole(0, i)).z())));
    if (weight && degree == 1) height = product(height, *weight);
    return height;
}

struct ProjectedSourceCoordinates {
    std::array<Polynomial, 2> uv;
    std::optional<Polynomial> weight;
    std::optional<Polynomial> relief;
    std::optional<std::array<I, 2>> fixedPointBox;
};

bool strictlyInside(Rectangle box, I u, I v) {
    return box.u.lo < u.lo && u.hi < box.u.hi && box.v.lo < v.lo && v.hi < box.v.hi;
}

I coupledImageError(const BSplineSurface &image, int iu, int iv, I u, I v,
                    const SourceProjectionAdjustment &adjustment, const Affine &height, I heightScale) {
    // On this span the actual source point differs from the approximate
    // inverse by -shift*(z-z0). Enclose the image's directional derivative
    // along the segment, even if the approximation crosses a knot.
    const auto &k = adjustment.shift;
    const auto extend = [](I active, Interval span, I error) {
        return I(std::max(active.lo, span.lo), std::min(active.hi, span.hi)) + error;
    };
    u = extend(u, {image.uKnots()[iu], image.uKnots()[iu + 1]}, k[0] * adjustment.heightError);
    v = extend(v, {image.vKnots()[iv], image.vKnots()[iv + 1]}, k[1] * adjustment.heightError);
    const P us{I(u.lo), I(u.hi)}, vs{I(v.lo), I(v.hi)};
    const auto origin = point(image.pole(iu - 3, iv - 3));
    const auto pole = [&](int i, int j) {
        auto p = point(image.pole(i, j));
        for (int axis = 0; axis < 3; ++axis) p[axis] = p[axis] - origin[axis];
        return p;
    };
    const auto du = derivativeTensor<3>(image, iu, iv, us, vs, true, UVLayout::Direct, pole);
    const auto dv = derivativeTensor<3>(image, iu, iv, us, vs, false, UVLayout::Direct, pole);
    VectorPolynomial slope;
    for (int axis = 0; axis < 3; ++axis)
        slope[axis] = sum(scaled(du[axis], k[0]), scaled(dv[axis], k[1]));
    const I offset = heightScale - height.du * k[0] - height.dv * k[1];
    return I(magnitude(adjustment.heightError)) * (squareRoot(squaredUpper(slope)) + I(magnitude(offset)));
}

// Work in target parameters. When the source projection also curves,
// compose the image with an enclosed inverse of the source projection.
double nonlinearProjectedNode(const ProjectedStrip &strip, const BSplineSurface &image,
    int tu, int tv, Rectangle targetBox, Rectangle sourceBox, const std::array<Affine, 2> &world, const Affine &height,
    const SourceGraph *sourceGraph, const SourceProjectionAdjustment *adjustment,
    int axisX, int axisY, double budget, int depth, Work &work, bool &hasInterior) {
    constexpr auto layout = UVLayout::Direct;
    const auto &target = *strip.surface;
    const int heightAxis = 3 - axisX - axisY;
    const I normalSign((axisX + 1) % 3 == axisY ? 1. : -1.);
    const auto &a = world[0], &b = world[1];
    const I determinant = a.du * b.dv - a.dv * b.du;
    const auto pu = target.uDegree(), pv = target.vDegree();
    // Center both polynomial enclosures on the active control net.
    // A distant first pole widens the interval residual during subtraction.
    const IP origin = point(target.pole(tu - pu, tv - pv));
    const bool rational = target.isRational();
    const bool coupled = adjustment && !adjustment->fixedAxis;
    const auto localParameter = [&](Polynomial x, Polynomial y, const Polynomial &w) {
        x = sum(std::move(x), scaled(w, origin[axisX] - a.origin));
        y = sum(std::move(y), scaled(w, origin[axisY] - b.origin));
        const I inverse = signedQuotient(I(1), determinant);
        ProjectedSourceCoordinates coordinates{{
            scaled(sum(scaled(x, b.dv), scaled(y, a.dv), true), inverse),
            scaled(sum(scaled(y, a.du), scaled(x, b.du), true), inverse)}, {}, {}, {}};
        if (adjustment) {
            const auto *denominator = rational ? &w : nullptr;
            if (coupled) {
                coordinates.relief = sourceHeightOn(*sourceGraph->surface, adjustment->relief, coordinates.uv, w);
                coordinates.fixedPointBox = std::array<I, 2>{
                    rationalHull(coordinates.uv[0], w) - adjustment->shift[0] * adjustment->heightRange,
                    rationalHull(coordinates.uv[1], w) - adjustment->shift[1] * adjustment->heightRange};
            } else {
                const int fixed = *adjustment->fixedAxis;
                coordinates.relief = shearHeight(*sourceGraph, fixed, coordinates.uv[fixed], denominator);
            }
            if (rational) {
                coordinates.weight = polynomialPower(w, adjustment->degree);
                const auto promotion = polynomialPower(w, adjustment->degree - 1);
                for (auto &q : coordinates.uv) q = product(q, promotion);
            }
            for (int axis = 0; axis < 2; ++axis)
                if (!exact(adjustment->shift[axis], 0.))
                    coordinates.uv[axis] = sum(std::move(coordinates.uv[axis]),
                        scaled(*coordinates.relief, adjustment->shift[axis]), true);
        }
        return coordinates;
    };
    const auto &ku = target.uKnots(), &kv = target.vKnots();
    const auto bu = basis(ku, tu, P{I(targetBox.u.lo), I(targetBox.u.hi)}, pu);
    const auto bv = basis(kv, tv, P{I(targetBox.v.lo), I(targetBox.v.hi)}, pv);
    const auto homogeneousPole = [&](int i, int j) {
        const auto p = point(target.pole(i, j));
        const I w(target.weight(i, j));
        return std::array<I, 4>{w * (p[0] - origin[0]), w * (p[1] - origin[1]),
                                w * (p[2] - origin[2]), w};
    };
    const auto targetNumerator = [&](const auto &u, const auto &v) {
        return weightedSurface<3>(u, v, layout, [&](int i, int j) {
            i += tu - pu;
            j += tv - pv;
            if (rational) {
                const auto h = homogeneousPole(i, j);
                return IP{h[0], h[1], h[2]};
            }
            auto pole = point(target.pole(i, j));
            for (int k = 0; k < 3; ++k) pole[k] = pole[k] - origin[k];
            return pole;
        });
    };
    const auto weightOn = [&](const auto &u, const auto &v) {
        if (!rational) return Polynomial(I(1));
        if (strip.weightVariesV)
            return weightedSurface<1>(u, v, layout, [&](int i, int j) {
                return std::array<I, 1>{I(target.weight(tu - pu + i, tv - pv + j))};
            }).front();
        const int degree = int(u.front().size()) - 1;
        Polynomial weight(degree, 0);
        for (int i = 0; i <= pu; ++i) for (int j = 0; j <= degree; ++j)
            weight.at(j, 0) = weight.at(j, 0) + I(target.weight(tu - pu + i, 0)) * u[i][j];
        return weight;
    };
    const auto wanted = targetNumerator(bu, bv);
    const auto weight = weightOn(bu, bv);
    const I wRange = hull(weight);
    if (!finite(wRange) || !(wRange.lo > 0.)) throw ProofFailed{};
    const auto projectedCoordinate = [&](int parameter, int axis, int span, int degree, const auto &functions) {
        Polynomial coordinate(parameter == 0 ? degree : 0, parameter == 1 ? degree : 0);
        for (int i = 0; i <= degree; ++i) {
            const int index = span - degree + i;
            const auto pole = parameter == 0 ? target.pole(index, 0) : target.pole(0, index);
            const I relative = I(pole[axis]) - origin[axis];
            for (int j = 0; j <= degree; ++j) {
                auto &coefficient = parameter == 0 ? coordinate.at(j, 0) : coordinate.at(0, j);
                coefficient = coefficient + relative * functions[i][j];
            }
        }
        return coordinate;
    };
    const auto localUV = localParameter(strip.coupled || rational ? wanted[axisX] : projectedCoordinate(0, axisX, tu, pu, bu),
                                        rational ? wanted[axisY] : projectedCoordinate(1, axisY, tv, pv, bv), weight);
    const auto &uv = localUV.uv;
    const auto &parameterWeight = localUV.weight ? *localUV.weight : weight;
    const I localWeight = hull(parameterWeight);
    if (!(localWeight.lo > 0.) || !finite(localWeight)) throw ProofFailed{};
    const auto parameterError = [&](int axis) {
        return coupled ? adjustment->shift[axis] * adjustment->heightError : I(0.);
    };
    const I u = rationalHull(uv[0], parameterWeight) + parameterError(0);
    const I v = rationalHull(uv[1], parameterWeight) + parameterError(1);
    if (!finite(u) || !finite(v)) throw ProofFailed{};
    if (u.hi < sourceBox.u.lo || u.lo > sourceBox.u.hi ||
        v.hi < sourceBox.v.lo || v.lo > sourceBox.v.hi) return 0.;
    hasInterior = hasInterior || (localUV.fixedPointBox
        ? strictlyInside(sourceBox, (*localUV.fixedPointBox)[0], (*localUV.fixedPointBox)[1])
        : strictlyInside(sourceBox, u, v));
    const bool hasOffset = sourceGraph || !constant(height) || !exact(height.origin, 0.);
    const auto heightOn = [&](const std::array<Polynomial, 2> &parameters, const Polynomial &w) {
        return sum(scaled(w, height.origin),
            sum(scaled(parameters[0], height.du), scaled(parameters[1], height.dv)));
    };
    const auto tangents = [&](const P &u, const P &v, const VectorPolynomial &numerator, const Polynomial &w) {
        const auto tangent = [&](bool alongU) {
            if (!rational) {
                const auto pole = [&](int i, int j) { return point(target.pole(i, j)); };
                return derivativeTensor<3>(target, tu, tv, u, v, alongU, layout, pole);
            }
            const auto d = derivativeTensor<4>(target, tu, tv, u, v, alongU, layout, homogeneousPole);
            VectorPolynomial out;
            for (int k = 0; k < 3; ++k)
                out[k] = sum(product(w, d[k]), product(numerator[k], d[3]), true);
            return out;
        };
        return std::array<VectorPolynomial, 2>{tangent(true), tangent(false)};
    };
    const auto imageCells = [&](bool alongU, I span, Interval window) {
        std::vector<TargetCell> cells;
        appendCells(alongU ? image.uKnots() : image.vKnots(), 3,
            alongU ? image.uPoleCount() : image.vPoleCount(), window,
            I(std::max(span.lo, window.lo), std::min(span.hi, window.hi)), I(0), cells);
        return cells;
    };
    const auto us = imageCells(true, u, sourceBox.u), vs = imageCells(false, v, sourceBox.v);
    if (us.empty() || vs.empty()) throw ProofFailed{};
    double upper = 0.;
    const bool fewCells = us.size() <= 2 && vs.size() <= 2;
    if (fewCells) {
        // Extrapolate each intersected image polynomial on the entire box.
        // Subdivision removes irrelevant extrapolation if the bound is loose.
        Polynomial heightWeight, targetWeight;
        I minimumDenominator(1.);
        if (rational) {
            // Bicubic composition uses the sixth power of its parameter
            // weight. Lift target and height from their respective weights.
            const int degree = adjustment ? adjustment->degree : 1;
            work.visit(64 * degree);
            heightWeight = polynomialPower(parameterWeight, 5);
            targetWeight = degree == 1 ? heightWeight : product(heightWeight, polynomialPower(weight, degree - 1));
            const I w(localWeight.lo), wSquared = w * w;
            minimumDenominator = wSquared * wSquared * wSquared;
            if (!(minimumDenominator.lo > 0.) || !finite(minimumDenominator)) throw ProofFailed{};
        }
        std::optional<HeightPatch> offset;
        VectorPolynomial normal;
        std::array<VectorPolynomial, 2> derivatives;
        double lowerNormal = 0.;
        if (hasOffset) {
            work.visit(16);
            auto h = heightOn(uv, parameterWeight);
            if (adjustment) h = sum(std::move(h), scaled(*localUV.relief, sourceGraph->zHeight));
            if (rational) h = product(h, heightWeight);
            if (sourceGraph && !adjustment) {
                work.visit(16);
                auto z = graphZOn(*sourceGraph, uv[0], uv[1], rational ? &weight : nullptr);
                if (rational) {
                    // The homogeneous source graph uses W to the sum of its
                    // two degrees. Bring it to the common W⁶ denominator.
                    const int degree = sourceGraph->surface->uDegree() + sourceGraph->surface->vDegree();
                    if (degree < 6) z = product(z, polynomialPower(weight, 6 - degree));
                }
                h = sum(std::move(h), scaled(std::move(z), sourceGraph->zHeight));
            }
            offset = HeightPatch{hull(h), std::move(h)};
            if (!strip.flat) {
                derivatives = tangents(P{I(targetBox.u.lo), I(targetBox.u.hi)},
                                       P{I(targetBox.v.lo), I(targetBox.v.hi)}, wanted, weight);
                normal = cross(derivatives[0], derivatives[1]);
                lowerNormal = regularNormalLower(derivatives[0], derivatives[1], normal);
            }
        }
        std::optional<Polynomial> residualWeight;
        for (const auto &cu : us) for (const auto &cv : vs) {
            work.visit(rational ? (offset ? 128 : 64) : (offset ? 32 : 16));
            const auto candidate = surfaceOn(image, cu.span, cv.span, uv[0], uv[1], origin, layout,
                                             rational ? &parameterWeight : nullptr);
            VectorPolynomial residual;
            for (int k = 0; k < 3; ++k)
                residual[k] = sum(candidate[k], rational ? product(wanted[k], targetWeight) : wanted[k], true);
            // A flat chart is already proved regular with positive XY
            // determinant. Its axial normal is exact on every span.
            if (offset && strip.flat)
                residual[heightAxis] = sum(std::move(residual[heightAxis]), scaled(offset->polynomial, normalSign), true);
            I certified(infinity);
            if (!offset || strip.flat) {
                certified = squareRoot(squaredUpper(residual)) / minimumDenominator;
            } else if (lowerNormal > 0.) {
                const auto &d = residual.front(), &c = normal.front(), &h = offset->polynomial;
                const bool withinDegree = std::max(d.u, h.u) + c.u <= maxPolynomialDegree &&
                                          std::max(d.v, h.v) + c.v <= maxPolynomialDegree;
                if (withinDegree) {
                    certified = I(projectedNormalUpper(residual, normal, *offset, lowerNormal)) / minimumDenominator;
                } else {
                    // The normal-cone bound avoids raising the degree. Divide
                    // in the Bernstein basis before enclosing physical error.
                    if (rational && !residualWeight) residualWeight = product(heightWeight, parameterWeight);
                    certified = I(coneUpper(std::move(residual), derivatives[0], derivatives[1], *offset,
                        Polynomial(I(1.)), 1., residualWeight ? &*residualWeight : nullptr));
                }
            }
            if (!finite(certified)) { upper = infinity; break; }
            if (coupled) {
                work.visit(16);
                certified = certified + coupledImageError(image, cu.span, cv.span, u, v,
                    *adjustment, height, sourceGraph->zHeight);
            }
            upper = std::max(upper, certified.hi);
        }
        const auto testInterior = [&](double fractionU, double fractionV, bool checkResidual) {
            const double mu = (1. - fractionU) * targetBox.u.lo + fractionU * targetBox.u.hi;
            const double mv = (1. - fractionV) * targetBox.v.lo + fractionV * targetBox.v.hi;
            if (!(targetBox.u.lo < mu && mu < targetBox.u.hi &&
                  targetBox.v.lo < mv && mv < targetBox.v.hi)) return;
            VectorPolynomial atMidpoint;
            Polynomial pointWeight(I(1.));
            if (rational) {
                const auto puBasis = basis(ku, tu, P{I(mu)}, pu);
                const auto pvBasis = basis(kv, tv, P{I(mv)}, pv);
                atMidpoint = targetNumerator(puBasis, pvBasis);
                pointWeight = weightOn(puBasis, pvBasis);
            } else {
                atMidpoint = surfaceOn(target, tu, tv, Polynomial(I(mu)), Polynomial(I(mv)), origin, layout);
            }
            const auto local = localParameter(atMidpoint[axisX], atMidpoint[axisY], pointWeight);
            const I denominator = hull(pointWeight), sourceDenominator = local.weight ? hull(*local.weight) : denominator;
            if (!(denominator.lo > 0.) || !finite(denominator) ||
                !(sourceDenominator.lo > 0.) || !finite(sourceDenominator)) return;
            if (local.fixedPointBox &&
                !strictlyInside(sourceBox, (*local.fixedPointBox)[0], (*local.fixedPointBox)[1])) return;
            const I x = hull(local.uv[0]) / sourceDenominator + parameterError(0);
            const I y = hull(local.uv[1]) / sourceDenominator + parameterError(1);
            if (!strictlyInside(sourceBox, x, y)) return;
            hasInterior = true;
            if (!checkResidual) return;
            IP pointOffset{};
            if (offset) {
                const auto derivatives = tangents(P{I(mu)}, P{I(mv)}, atMidpoint, pointWeight);
                I pointHeight = height.origin + height.du * x + height.dv * y;
                if (sourceGraph) {
                    I relief = local.relief ? hull(*local.relief) / sourceDenominator :
                        hull(graphZOn(*sourceGraph, P{x}, P{y}));
                    if (coupled) relief = relief + adjustment->heightError;
                    pointHeight = pointHeight + sourceGraph->zHeight * relief;
                }
                const auto value = projectedPointOffset(derivatives[0], derivatives[1], pointHeight);
                if (!value) return;
                pointOffset = *value;
            }
            if (rational)
                for (auto &k : atMidpoint) k = Polynomial(hull(k) / denominator);
            for (const auto &cu : us) for (const auto &cv : vs) {
                const auto &imageU = image.uKnots(), &imageV = image.vKnots();
                if (!(std::max(sourceBox.u.lo, imageU[cu.span]) < x.lo && x.hi < std::min(sourceBox.u.hi, imageU[cu.span + 1])
                    && std::max(sourceBox.v.lo, imageV[cv.span]) < y.lo && y.hi < std::min(sourceBox.v.hi, imageV[cv.span + 1])))
                    continue;
                const auto actual = surfaceOn(image, cu.span, cv.span, Polynomial(x), Polynomial(y), origin, layout);
                checkProjectedPointResidual(actual, atMidpoint, pointOffset, budget);
            }
        };
        if (upper <= budget) {
            if (!hasInterior) testInterior(.5, .5, false);
            return upper;
        }
        testInterior(.5, .5, true);
        if (depth == 0) {
            // The midpoint can lie exactly on the source boundary while
            // another point in the same cell proves a budget violation.
            testInterior(.25, .5, true);
            testInterior(.75, .5, true);
            testInterior(.5, .25, true);
            testInterior(.5, .75, true);
        }
    }
    // An unresolved leaf must block certification, but it must not hide
    // a strict interior witness in the other half or on a later patch.
    if (depth == maxDepth) return infinity;
    work.visit();
    const bool splitU = targetBox.u.length() >= targetBox.v.length();
    const auto side = splitU ? targetBox.u : targetBox.v;
    const double middle = .5 * side.lo + .5 * side.hi;
    if (!(side.lo < middle && middle < side.hi)) return infinity;
    Rectangle first = targetBox, second = targetBox;
    (splitU ? first.u : first.v).hi = middle;
    (splitU ? second.u : second.v).lo = middle;
    const auto descend = [&](Rectangle box) {
        return nonlinearProjectedNode(strip, image, tu, tv, box, sourceBox, world, height, sourceGraph,
            adjustment, axisX, axisY, budget, depth + 1, work, hasInterior);
    };
    const double firstBound = descend(first);
    return std::max(firstBound, descend(second));
}

std::optional<std::array<Rectangle, 2>> bisect(Rectangle r) {
    const bool alongU = r.u.length() >= r.v.length();
    const auto side = alongU ? r.u : r.v;
    const double middle = .5 * side.lo + .5 * side.hi;
    if (!(side.lo < middle && middle < side.hi)) return {};
    auto right = r;
    (alongU ? r.u : r.v).hi = middle;
    (alongU ? right.u : right.v).lo = middle;
    return std::array<Rectangle, 2>{r, right};
}

I sourceProjectionJacobian(const SourceGraph &source, const std::array<Affine, 2> &world, Rectangle r) {
    const auto &[a, b] = world;
    const auto &[x, y] = source.zUV;
    const auto &s = *source.surface;
    const P u{I(r.u.lo), I(r.u.hi)}, v{I(r.v.lo), I(r.v.hi)};
    const auto z = [&](int i, int j) { return std::array<I, 1>{I(s.pole(i, j).z())}; };
    const auto du = derivativeTensor<1>(s, s.uDegree(), s.vDegree(), u, v, true, UVLayout::Direct, z).front();
    const auto dv = derivativeTensor<1>(s, s.uDegree(), s.vDegree(), u, v, false, UVLayout::Direct, z).front();
    const auto variation = sum(scaled(du, x * b.dv - y * a.dv), scaled(dv, y * a.du - x * b.du));
    // The two nonlinear columns are multiples of the same vector, so
    // their product cancels from the determinant before taking bounds.
    return (a.du * b.dv - a.dv * b.du) + hull(variation);
}

bool regularSourceProjection(const SourceGraph &source, const std::array<Affine, 2> &world,
                             Rectangle r, I sign, int depth, Work &work) {
    work.visit();
    const I area = sign * sourceProjectionJacobian(source, world, r);
    if (!finite(area) || area.hi <= 0.) return false;
    if (area.lo > 0.) return true;
    if (depth == maxDepth) return false;
    const auto halves = bisect(r);
    return halves &&
           regularSourceProjection(source, world, halves->front(), sign, depth + 1, work) &&
           regularSourceProjection(source, world, halves->back(), sign, depth + 1, work);
}

Rectangle projectedChartInterior(const BSplineSurface &s, int axisX, int axisY) {
    const int lastU = s.uPoleCount() - 1, lastV = s.vPoleCount() - 1;
    Rectangle inner{{-infinity, infinity}, {-infinity, infinity}};
    // Positive weights keep each boundary in its control hull. The four
    // edges lie in their respective exterior half-planes. A straight
    // homotopy to the rectangle sides proves winding one in its interior.
    for (int j = 0; j <= lastV; ++j) {
        cancellationPoint();
        inner.u.lo = std::max(inner.u.lo, s.pole(0, j)[axisX]);
        inner.u.hi = std::min(inner.u.hi, s.pole(lastU, j)[axisX]);
    }
    for (int i = 0; i <= lastU; ++i) {
        cancellationPoint();
        inner.v.lo = std::max(inner.v.lo, s.pole(i, 0)[axisY]);
        inner.v.hi = std::min(inner.v.hi, s.pole(i, lastV)[axisY]);
    }
    return inner;
}

struct DirectSourceProjection {
    const SourceGraph *source; // null for a planar source
    std::array<Affine, 2> world;
    Affine height;
    std::array<int, 2> axes;
    I normalSign;
    double level;
    std::optional<PhysicalHeightModel> graph;
    std::vector<Rectangle> interiors;
    std::optional<Rectangle> footprint;
};

struct DirectSourceSample {
    VectorPolynomial support;
    std::optional<HeightPatch> height;
    VectorPolynomial normal;
};

DirectSourceSample directSourceOn(const DirectSourceProjection &map, Rectangle box, const IP &origin) {
    const auto *source = map.source;
    const auto relief = source ? graphZOn(*source, P{I(box.u.lo), I(box.u.hi)}, P{I(box.v.lo), I(box.v.hi)}) :
                                 Polynomial(I(0.));
    const auto [x, y] = map.axes;
    const int z = 3 - x - y;
    const auto &graph = map.graph;
    const I x0 = graph ? I(graph->x.lo) : origin[x], y0 = graph ? I(graph->y.lo) : origin[y];
    const auto projectedOn = [&](int axis, I center) {
        auto affine = map.world[axis];
        affine.origin = affine.origin - center;
        auto coordinate = affineOn(affine, box);
        return source ? sum(std::move(coordinate), scaled(relief, source->zUV[axis])) : coordinate;
    };
    auto u = projectedOn(0, x0), v = projectedOn(1, y0);
    auto height = affineOn(map.height, box);
    if (source) height = sum(std::move(height), scaled(relief, source->zHeight));
    DirectSourceSample sample;
    if (!graph) {
        sample.support[x] = std::move(u);
        sample.support[y] = std::move(v);
        sample.support[z] = sum(Polynomial(I(map.level) - origin[z]), scaled(std::move(height), map.normalSign));
        return sample;
    }

    sample.support[x] = sum(u, Polynomial(x0 - origin[x]));
    sample.support[y] = sum(v, Polynomial(y0 - origin[y]));
    const I sx = I(1.) / (I(graph->x.hi) - x0), sy = I(1.) / (I(graph->y.hi) - y0);
    u = scaled(std::move(u), sx); v = scaled(std::move(v), sy);
    const auto &c = graph->height.coefficients;
    auto &zOn = sample.support[z];
    zOn = Polynomial(I(c[0][0]) - origin[z]);
    std::array<Polynomial, 2> slope;
    std::vector<Polynomial> up{Polynomial(I(1.))}, vp{Polynomial(I(1.))};
    for (int i = 1; i <= graph->degree; ++i) {
        up.push_back(product(up.back(), u));
        vp.push_back(product(vp.back(), v));
    }
    for (int i = 0; i <= graph->degree; ++i) for (int j = 0; j <= graph->degree - i; ++j) {
        const double coefficient = c[i][j];
        if ((!i && !j) || coefficient == 0.) continue;
        zOn = sum(std::move(zOn), scaled(product(up[i], vp[j]), I(coefficient)));
        if (i) slope[0] = sum(std::move(slope[0]),
            scaled(product(up[i - 1], vp[j]), I(i) * I(coefficient)));
        if (j) slope[1] = sum(std::move(slope[1]),
            scaled(product(up[i], vp[j - 1]), I(j) * I(coefficient)));
    }
    // The graph's normal in physical projected axes is proportional to
    // (-dz/dx, -dz/dy, 1). The z component gives a unit lower bound.
    sample.normal[x] = scaled(std::move(slope[0]), I(-1.) * map.normalSign * sx);
    sample.normal[y] = scaled(std::move(slope[1]), I(-1.) * map.normalSign * sy);
    sample.normal[z] = Polynomial(map.normalSign);
    sample.height = HeightPatch{hull(height), std::move(height)};
    return sample;
}

double directGraphNormalUpper(const VectorPolynomial &residual, const VectorPolynomial &normal,
                              const HeightPatch &height) {
    std::array<int, 2> difference{height.polynomial.u, height.polynomial.v}, normalDegree{};
    for (std::size_t i = 0; i < residual.size(); ++i) {
        difference[0] = std::max(difference[0], residual[i].u);
        difference[1] = std::max(difference[1], residual[i].v);
        normalDegree[0] = std::max(normalDegree[0], normal[i].u);
        normalDegree[1] = std::max(normalDegree[1], normal[i].v);
    }
    if (difference[0] + normalDegree[0] <= maxPolynomialDegree &&
        difference[1] + normalDegree[1] <= maxPolynomialDegree)
        return projectedNormalUpper(residual, normal, height, 1.);

    // A constant reference length avoids increasing the residual degree.
    // Each component's Bernstein hull encloses its contribution to |n|.
    I normSquared;
    for (const auto &component : normal) {
        const I h = hull(component);
        const I absolute(std::max({0., h.lo, -h.hi}), magnitude(h));
        normSquared = normSquared + absolute * absolute;
    }
    const I length = squareRoot(normSquared);
    const I reference(std::max(1., .5 * length.lo + .5 * length.hi));
    VectorPolynomial differenceAtReference;
    for (std::size_t i = 0; i < residual.size(); ++i)
        differenceAtReference[i] = sum(scaled(residual[i], reference), product(normal[i], height.polynomial), true);
    return ((squareRoot(squaredUpper(differenceAtReference)) +
        I(magnitude(height.range)) * I(magnitude(length - reference))) / reference).hi;
}

constexpr std::array<std::array<double, 2>, 5> directWitnessFractions{{
    {.5,.5}, {.25,.5}, {.75,.5}, {.5,.25}, {.5,.75}
}};

double directSourceNode(const DirectSourceProjection &map, const BSplineSurface &image, int iu, int iv,
                      Rectangle r, double budget, int depth, Work &work, bool &hasInterior) {
    work.visit();
    if (map.footprint && !map.source) {
        // Each polynomial applies only inside its own physical rectangle.
        // Clip on the affine projection before evaluating either surface.
        const auto &box = *map.footprint;
        const I px = range(map.world[0], r.u, r.v), py = range(map.world[1], r.u, r.v);
        // Joined coverage and matching normals extend the interior
        // bound to seams. A boundary-only touch needs no separate proof.
        if (finite(px) && finite(py) && !overlapsInterior({{px.lo, px.hi}, {py.lo, py.hi}}, box)) return 0.;
        if (!clipObliqueCell(map.world, {{box.u, box.v}, true}, r)) return 0.;
    }
    const IP origin = point(image.pole(iu - 3, iv - 3));
    const auto wanted = directSourceOn(map, r, origin);
    const auto [x, y] = map.axes;
    if (map.footprint && map.source) {
        const I px = hull(wanted.support[x]) + origin[x], py = hull(wanted.support[y]) + origin[y];
        const auto &box = *map.footprint;
        if (px.hi < box.u.lo || px.lo > box.u.hi || py.hi < box.v.lo || py.lo > box.v.hi) return 0.;
    }
    const auto actual = surfaceOn(image, iu, iv, P{I(r.u.lo), I(r.u.hi)}, P{I(r.v.lo), I(r.v.hi)},
        origin, UVLayout::Direct);
    VectorPolynomial residual;
    for (int axis = 0; axis < 3; ++axis)
        residual[axis] = sum(actual[axis], wanted.support[axis], true);
    const double upper = wanted.height ? directGraphNormalUpper(residual, wanted.normal, *wanted.height) :
                                         squareRoot(squaredUpper(residual)).hi;
    if (!hasInterior || upper > budget) {
        for (const auto &[fu, fv] : directWitnessFractions) {
            const double u = (1. - fu) * r.u.lo + fu * r.u.hi;
            const double v = (1. - fv) * r.v.lo + fv * r.v.hi;
            if (!(r.u.lo < u && u < r.u.hi && r.v.lo < v && v < r.v.hi)) continue;
            work.visit(map.interiors.size());
            const auto sample = directSourceOn(map, {{u,u}, {v,v}}, origin);
            const I px = hull(sample.support[x]) + origin[x], py = hull(sample.support[y]) + origin[y];
            if (std::none_of(map.interiors.begin(), map.interiors.end(),
                            [&](Rectangle box) { return strictlyInside(box, px, py); })) continue;
            hasInterior = true;
            if (upper <= budget) break;
            const auto atPoint = surfaceOn(image, iu, iv, Polynomial(I(u)), Polynomial(I(v)), origin, UVLayout::Direct);
            IP offset{};
            if (sample.height) {
                IP normal;
                for (int axis = 0; axis < 3; ++axis) normal[axis] = hull(sample.normal[axis]);
                const I length = squareRoot(scalar(normal, normal));
                for (int axis = 0; axis < 3; ++axis) offset[axis] = sample.height->range * normal[axis] / length;
            }
            checkProjectedPointResidual(atPoint, sample.support, offset, budget);
        }
    }
    if (upper <= budget) return upper;
    // Tighten the clipped join or the forward relief enclosure locally.
    // The planar pass with a common height still checks each span once.
    const auto halves = (map.source || map.footprint) && depth < maxDepth ? bisect(r) : std::nullopt;
    if (!halves) return infinity;
    const double first = directSourceNode(map, image, iu, iv, halves->front(), budget, depth + 1, work, hasInterior);
    return std::max(first, directSourceNode(map, image, iu, iv, halves->back(), budget, depth + 1, work, hasInterior));
}

std::optional<PhysicalHeightModel> commonDirectHeight(const std::vector<ProjectedStrip> &strips,
    int x, int y, int z, int maxDegree) {
    for (int degree = 2; degree <= maxDegree; ++degree) {
        const auto model = derivePhysicalHeightModel(*strips.front().surface, x, y, z, degree);
        if (!model) continue;
        try {
            if (std::all_of(strips.begin() + 1, strips.end(), [&](const ProjectedStrip &strip) {
                    return certifyPhysicalHeight(*strip.surface, x, y, z, model->x, model->y, model->height, model->degree);
                })) return model;
        } catch (const OverlapArithmeticFailed &) {
            // A different homogeneous degree may admit exact coefficients.
        }
    }
    return {};
}

std::array<std::array<double, maxPhysicalHeightDegree + 1>, 2> polynomialSeam(
    const PhysicalHeightModel &model, double seam, double transverseOrigin, bool alongX) {
    // Height and its crossing derivative determine the full normal.
    // Express both in powers about the same physical transverse origin.
    const double d = overlapAdd(seam, -(alongX ? model.x.lo : model.y.lo));
    const double shift = overlapAdd(transverseOrigin, -(alongX ? model.y.lo : model.x.lo));
    std::array<double, maxPhysicalHeightDegree + 1> powers{1.};
    for (int i = 1; i <= model.degree; ++i) powers[i] = overlapMultiply(powers[i - 1], shift);
    std::array<std::array<double, maxPhysicalHeightDegree + 1>, 2> result{};
    for (int t = 0; t <= model.degree; ++t) {
        double height = 0., derivative = 0.;
        for (int n = model.degree - t; n >= 0; --n) {
            const auto &c = model.height.coefficients;
            derivative = overlapAdd(height, overlapMultiply(d, derivative));
            height = overlapAdd(alongX ? c[n][t] : c[t][n], overlapMultiply(d, height));
        }
        for (int k = 0; k <= t; ++k) {
            const double basis = overlapMultiply(surface_bounds::choose(t, k), powers[t - k]);
            result[0][k] = overlapAdd(result[0][k], overlapMultiply(height, basis));
            result[1][k] = overlapAdd(result[1][k], overlapMultiply(derivative, basis));
        }
    }
    return result;
}

struct DirectHeightPiece {
    Rectangle footprint;
    PhysicalHeightModel graph;
};

bool joinedPolynomialCoverage(const std::vector<DirectHeightPiece> &pieces, Rectangle needed, Work &work) {
    // Check each shared edge at a common transverse origin. T-junctions
    // can have different transverse extents on either side of a seam.
    try {
        for (std::size_t i = 0; i < pieces.size(); ++i) for (std::size_t j = 0; j < i; ++j) {
            work.visit();
            const auto &a = pieces[i], &b = pieces[j];
            const Interval x{std::max(a.footprint.u.lo, b.footprint.u.lo), std::min(a.footprint.u.hi, b.footprint.u.hi)};
            const Interval y{std::max(a.footprint.v.lo, b.footprint.v.lo), std::min(a.footprint.v.hi, b.footprint.v.hi)};
            if (x.lo > x.hi || y.lo > y.hi) continue;
            if (x.lo < x.hi && y.lo < y.hi) return false;
            const bool alongX = x.lo == x.hi;
            const Interval crossing = alongX ? x : y, transverse = alongX ? y : x;
            const auto first = polynomialSeam(a.graph, crossing.lo, transverse.lo, alongX);
            const auto second = polynomialSeam(b.graph, crossing.lo, transverse.lo, alongX);
            if (transverse.lo < transverse.hi) {
                if (first != second) return false;
            } else if (first[0][0] != second[0][0] || first[0][1] != second[0][1] || first[1][0] != second[1][0]) {
                return false;
            }
        }
    } catch (const OverlapArithmeticFailed &) {
        return false;
    }

    // On each open X slab the active closed rectangles are constant.
    // Their Y intervals must cover the source enclosure, with no gaps.
    std::vector<double> cuts{needed.u.lo, needed.u.hi};
    for (const auto &piece : pieces) {
        for (double boundary : {piece.footprint.u.lo, piece.footprint.u.hi})
            if (needed.u.lo < boundary && boundary < needed.u.hi) cuts.push_back(boundary);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        std::vector<Interval> cover;
        for (const auto &piece : pieces) {
            work.visit();
            if (piece.footprint.u.lo <= cuts[i - 1] && cuts[i] <= piece.footprint.u.hi)
                cover.push_back(piece.footprint.v);
        }
        std::sort(cover.begin(), cover.end(), [](Interval a, Interval b) { return a.lo < b.lo; });
        double covered = needed.v.lo;
        for (const auto interval : cover) {
            if (interval.lo > covered) break;
            covered = std::max(covered, interval.hi);
        }
        if (covered < needed.v.hi) return false;
    }
    return cuts.size() > 1;
}

std::vector<DirectSourceProjection> joinedDirectProjections(const DirectSourceProjection &projection,
    const std::vector<ProjectedStrip> &strips, Rectangle sourceBox, int maxDegree, Work &work) {
    const auto [x, y] = projection.axes;
    std::vector<DirectHeightPiece> pieces;
    pieces.reserve(strips.size());
    for (const auto &strip : strips) {
        work.visit();
        const auto inner = projectedChartInterior(*strip.surface, x, y);
        const auto &box = strip.footprint;
        std::optional<PhysicalHeightModel> graph;
        for (int degree = 2; degree <= maxDegree && !graph; ++degree) {
            work.visit();
            graph = derivePhysicalHeightModel(*strip.surface, x, y, 3 - x - y, degree);
        }
        // Winding on the interior and the outer control hull prove the
        // entire closed chart rectangle in physical coordinates.
        if (!graph || inner.u.lo != box.u.lo || inner.u.hi != box.u.hi ||
            inner.v.lo != box.v.lo || inner.v.hi != box.v.hi ||
            graph->x.lo != box.u.lo || graph->y.lo != box.v.lo) return {};
        pieces.push_back({box, std::move(*graph)});
    }
    const auto projected = directSourceOn(projection, sourceBox, {});
    const I px = hull(projected.support[x]), py = hull(projected.support[y]);
    if (!finite(px) || !finite(py) || !(px.lo < px.hi && py.lo < py.hi) ||
        !joinedPolynomialCoverage(pieces, {{px.lo, px.hi}, {py.lo, py.hi}}, work)) return {};
    std::vector<DirectSourceProjection> maps;
    maps.reserve(pieces.size());
    for (auto &piece : pieces) {
        maps.push_back(projection);
        auto &map = maps.back();
        map.graph = std::move(piece.graph);
        map.footprint = piece.footprint;
        map.interiors.push_back(piece.footprint);
    }
    return maps;
}

SurfaceMappingBoundResult planarControlNetResidual(const DirectSourceProjection &map, const BSplineSurface &image,
    Rectangle r, double budget, Work &work) {
    if (!image.uDomain().contains(r.u.lo) || !image.uDomain().contains(r.u.hi) ||
        !image.vDomain().contains(r.v.lo) || !image.vDomain().contains(r.v.hi)) return {};
    const int nu = image.uPoleCount(), nv = image.vPoleCount();
    const auto &uk = image.uKnots(), &vk = image.vKnots();
    const auto activePoles = [](const std::vector<double> &knots, int count, Interval range) {
        // The bicubic is position-continuous at its knots. The right span
        // at the lower end and the left span at the upper end therefore
        // cover the closed window without pulling in inactive controls.
        const int first = containing(knots, 3, count, {range.lo, range.lo}) - 3;
        const int end = int(std::lower_bound(knots.begin() + 3, knots.begin() + count + 1, range.hi) - knots.begin());
        return std::pair{first, end};
    };
    const auto [firstU, endU] = activePoles(uk, nu, r.u);
    const auto [firstV, endV] = activePoles(vk, nv, r.v);
    const std::size_t witnessCost = 2 + (1 + directWitnessFractions.size()) * map.interiors.size();
    if (witnessCost >= work.left) return {};
    const auto insideContact = [&](const Vec2 &uv) {
        if (!(r.u.lo < uv.x() && uv.x() < r.u.hi && r.v.lo < uv.y() && uv.y() < r.v.hi)) return false;
        work.visit(map.interiors.size());
        const I x = at(map.world[0], uv.x(), uv.y()), y = at(map.world[1], uv.x(), uv.y());
        return std::any_of(map.interiors.begin(), map.interiors.end(),
            [&](Rectangle box) { return strictlyInside(box, x, y); });
    };
    std::optional<Vec2> interior;
    for (const auto &[fu, fv] : directWitnessFractions) {
        const Vec2 uv((1. - fu) * r.u.lo + fu * r.u.hi, (1. - fv) * r.v.lo + fv * r.v.hi);
        if (insideContact(uv)) {
            interior = uv;
            break;
        }
    }
    if (!interior) return {};
    const auto checkPoint = [&](const Vec2 &uv) {
        work.visit();
        const double u = uv.x(), v = uv.y();
        const int iu = containing(uk, 3, nu, {u, u}), iv = containing(vk, 3, nv, {v, v});
        const IP origin = point(image.pole(iu - 3, iv - 3));
        const auto actual = surfaceOn(image, iu, iv, Polynomial(I(u)), Polynomial(I(v)), origin, UVLayout::Direct);
        const auto wanted = directSourceOn(map, {{u, u}, {v, v}}, origin);
        checkProjectedPointResidual(actual, wanted.support, {}, budget);
    };

    const auto [x, y] = map.axes;
    const int z = 3 - x - y;
    std::array<Affine, 3> expected;
    expected[x] = map.world[0];
    expected[y] = map.world[1];
    expected[z] = {I(map.level) + map.normalSign * map.height.origin,
        map.normalSign * map.height.du, map.normalSign * map.height.dv};
    const auto terms = [&](const std::vector<double> &knots, int first, int end, bool alongU) {
        std::vector<IP> result;
        result.reserve(std::size_t(end - first));
        for (int i = first; i < end; ++i) {
            cancellationPoint();
            const I g = (I(knots[i + 1]) + I(knots[i + 2]) + I(knots[i + 3])) / I(3.);
            IP values;
            for (int axis = 0; axis < 3; ++axis)
                values[axis] = (alongU ? expected[axis].du : expected[axis].dv) * g;
            result.push_back(values);
        }
        return result;
    };
    const auto u = terms(uk, firstU, endU, true), v = terms(vk, firstV, endV, false);

    // The affine map has an exact representation on the same spline basis
    // with Greville abscissae as coefficients. Positivity and partition of
    // unity bound the norm of the residual by the maximum coefficient norm.
    // validate() already bounds the native net to maxPoles. This single
    // linear pass uses that bound. Work is reserved for geometric queries
    // and adaptive cell subdivision, including the point witness below.
    double upperSquared = 0.;
    const double budgetSquared = (I(budget) * I(budget)).hi;
    for (int i = firstU; i < endU; ++i) for (int j = firstV; j < endV; ++j) {
        cancellationPoint();
        const auto &uTerm = u[std::size_t(i - firstU)], &vTerm = v[std::size_t(j - firstV)];
        auto residual = point(image.pole(i, j));
        for (int axis = 0; axis < 3; ++axis)
            residual[axis] = (residual[axis] - expected[axis].origin) - uTerm[axis] - vTerm[axis];
        const I squared = scalar(residual, residual);
        if (!finite(squared)) return {};
        if (squared.hi > budgetSquared) {
            // A coefficient is not a rejection witness. Look on its
            // support first, then use the previously established contact.
            const Vec2 local(.5 * std::max(r.u.lo, uk[i]) + .5 * std::min(r.u.hi, uk[i + 4]),
                             .5 * std::max(r.v.lo, vk[j]) + .5 * std::min(r.v.hi, vk[j + 4]));
            if (insideContact(local)) checkPoint(local);
            if (local.x() != interior->x() || local.y() != interior->y()) checkPoint(*interior);
            return {};
        }
        upperSquared = std::max(upperSquared, squared.hi);
    }
    const double upper = squareRoot(I(upperSquared)).hi;
    return upper <= budget ? SurfaceMappingBoundResult{SurfaceMappingBoundStatus::Certified, upper}
                           : SurfaceMappingBoundResult{};
}

bool directGridFitsWork(std::size_t uSpans, std::size_t vSpans, std::size_t charts, const Work &work,
                        std::size_t projections = 1) {
    // One visit per projection and up to five candidates per chart in each cell.
    return uSpans <= work.left / vSpans / (projections + directWitnessFractions.size() * charts);
}

SurfaceMappingBoundResult affinePlanarControlNet(const std::vector<GraphPatch> &graphs, const BSplineSurface &image,
    Rectangle r, const std::array<Affine, 2> &world, const Affine &height, const std::array<int, 2> &axes,
    I normalSign, double budget, Work &work) {
    const auto us = cuts(image.uKnots(), r.u), vs = cuts(image.vKnots(), r.v);
    if (directGridFitsWork(us.size() - 1, vs.size() - 1, graphs.size(), work)) return {};
    const int z = 3 - axes[0] - axes[1];
    const double level = graphs.front().carrier.base.pole(0, 0)[z];
    DirectSourceProjection projection{nullptr, world, height, axes, normalSign, level, {}, {}, {}};
    for (const auto &patch : graphs) {
        cancellationPoint();
        if (!patch.fullDomain || !patch.contactIsRegular || constantGraphHeight(patch.carrier.base, z) != level) return {};
        // These domains are physical X/Y intervals: the accepted native
        // affine chart is either direct or transposed with proven orientation.
        projection.interiors.push_back({patch.u, patch.v});
    }
    return planarControlNetResidual(projection, image, r, budget, work);
}

SurfaceMappingBoundResult directSourceResidual(const SourceGraph *source, const BSplineSurface &image,
    const std::vector<ProjectedStrip> &strips, Rectangle r, const std::array<Affine, 2> &world,
    const Affine &height, const std::array<int, 2> &axes, double budget, Work &work) {
    if (strips.empty()) return {};
    const auto [x, y] = axes;
    const int z = 3 - x - y;
    const double level = strips.front().surface->pole(0, 0)[z];
    const I sign((x + 1) % 3 == y ? 1. : -1.);
    DirectSourceProjection projection{source, world, height, axes, sign, level, {}, {}, {}};
    const bool flat = std::all_of(strips.begin(), strips.end(), [&](const ProjectedStrip &strip) {
        return strip.flat && strip.surface->pole(0, 0)[z] == level;
    });
    // A support of degree d composes to at most d times the source
    // degree. Its normal derivative times source height fits the same bound.
    const int maxDegree = source ? std::min(maxPhysicalHeightDegree, maxPolynomialDegree /
        std::max({1, source->surface->uDegree(), source->surface->vDegree()})) : 5;
    if (!flat) projection.graph = commonDirectHeight(strips, x, y, z, maxDegree);
    std::vector<DirectSourceProjection> maps;
    if (flat || projection.graph) {
        for (const auto &strip : strips)
            projection.interiors.push_back(projectedChartInterior(*strip.surface, x, y));
        maps.push_back(std::move(projection));
    } else {
        maps = joinedDirectProjections(projection, strips, r, maxDegree, work);
        if (maps.empty()) return {};
    }
    // In the rank-one projection, any two parameters mapping to the same
    // point differ along the affine inverse of the relief direction. A
    // fixed, nonzero Jacobian sign implies strict monotonicity on that line.
    // Evaluate the physical residual forward, without composing an inverse.
    if (source) {
        const double u = .5 * r.u.lo + .5 * r.u.hi, v = .5 * r.v.lo + .5 * r.v.hi;
        const I center = sourceProjectionJacobian(*source, world, {{u,u}, {v,v}});
        if (!strictSign(center) ||
            !regularSourceProjection(*source, world, r, I(center.lo > 0. ? 1. : -1.), 0, work)) return {};
    }
    const auto &uk = image.uKnots(), &vk = image.vKnots();
    if (r.u.lo < image.uDomain().lo || r.u.hi > image.uDomain().hi ||
        r.v.lo < image.vDomain().lo || r.v.hi > image.vDomain().hi) return {};
    const auto us = cuts(uk, r.u), vs = cuts(vk, r.v);
    // Check the base cost before visiting the optional planar grid.
    if (!source && !directGridFitsWork(us.size() - 1, vs.size() - 1, strips.size(), work, maps.size()))
        return flat ? planarControlNetResidual(maps.front(), image, r, budget, work) : SurfaceMappingBoundResult{};
    bool hasInterior = false;
    double upper = 0.;
    for (std::size_t i = 1; i < us.size(); ++i) for (std::size_t j = 1; j < vs.size(); ++j) {
        const Rectangle cell{{us[i - 1], us[i]}, {vs[j - 1], vs[j]}};
        const int iu = containing(uk, 3, image.uPoleCount(), cell.u);
        const int iv = containing(vk, 3, image.vPoleCount(), cell.v);
        for (const auto &map : maps)
            upper = std::max(upper, directSourceNode(map, image, iu, iv, cell, budget, 0, work, hasInterior));
    }
    // Coverage is checked by the caller. A rejection witness above came
    // from the certified interior of an actual selected support.
    if (!hasInterior || !std::isfinite(upper)) return {};
    return {SurfaceMappingBoundStatus::Certified, upper};
}

SurfaceMappingBoundResult nonlinearProjectedResidual(const Body &source, FaceId face,
    const std::vector<SurfaceTargetFace> &targets, const BSplineSurface &image, Rectangle sourceBox,
    const std::array<Affine, 2> &world, const Affine &height, const SourceGraph *sourceGraph,
    int axisX, int axisY, int axisZ, double direction, double budget) {
    // Rectangular trims project to strips. An overlap must either have
    // an exact common contact or be excluded by ordering the boundary.
    const auto &[a, b] = world;
    const I determinant = a.du * b.dv - a.dv * b.du;
    if (!strictSign(determinant) || !fullRectangle(source, face, sourceBox.u, sourceBox.v)) return {};
    // The selected face sense may reverse the signed height used with
    // the natural target normals.
    const I sign(direction);
    const Affine originalHeight{sign * height.origin, sign * height.du, sign * height.dv};
    std::vector<ProjectedStrip> strips;
    bool nonlinear = false;
    for (const auto &target : targets) {
        const auto &patch = target.body->face(target.face);
        const auto *s = dynamic_cast<const BSplineSurface *>(patch.surface.get());
        if (!s || s->basis() || s->isUPeriodic() || s->isVPeriodic() ||
            (patch.sense ? 1. : -1.) != direction) return {};
        const int maxNativeDegree = s->isRational() ? 3 : 2;
        if (s->uDegree() < 1 || s->uDegree() > maxNativeDegree ||
            s->vDegree() < 1 || s->vDegree() > maxNativeDegree) return {};
        validate(*s, false);
        const auto ud = s->uDomain(), vd = s->vDomain();
        if (!fullRectangle(*target.body, target.face, ud, vd)) return {};
        bool coupled = false, exactY = true, weightVariesV = false;
        double minX = infinity, maxX = -infinity;
        const auto expandX = [&](double x) { minX = std::min(minX, x); maxX = std::max(maxX, x); };
        const auto checkCoordinate = [&](bool alongU, int axis) {
            const auto &knots = alongU ? s->uKnots() : s->vKnots();
            const auto domain = alongU ? ud : vd;
            const int degree = alongU ? s->uDegree() : s->vDegree();
            const int count = alongU ? s->uPoleCount() : s->vPoleCount();
            const int transverseCount = alongU ? s->vPoleCount() : s->uPoleCount();
            if (!std::all_of(knots.begin(), knots.begin() + degree + 1, [&](double k) { return k == domain.lo; }) ||
                !std::all_of(knots.end() - degree - 1, knots.end(), [&](double k) { return k == domain.hi; }))
                return false;
            const auto coordinate = [&](int i, int j) { return (alongU ? s->pole(i, j) : s->pole(j, i))[axis]; };
            for (int i = 0; i < count; ++i) {
                const double value = coordinate(i, 0);
                if (alongU) expandX(value);
                const bool affine = grevilleCoordinate(value, knots, i, degree);
                nonlinear = nonlinear || !affine;
                if (!alongU) exactY = exactY && affine;
                if (i > 0) {
                    const I slope = I(degree) * (I(value) - I(coordinate(i - 1, 0)))
                        / (I(knots[i + degree]) - I(knots[i]));
                    if (!finite(slope) || slope.lo < .5 || slope.hi > 2.) return false;
                }
                for (int j = 1; j < transverseCount; ++j) {
                    cancellationPoint();
                    const double crossPole = coordinate(i, j);
                    if (!alongU) {
                        if (crossPole != value) return false;
                    } else {
                        expandX(crossPole);
                        const double transverse = coordinate(0, j), origin = coordinate(0, 0);
                        if (!exactZeroSum({crossPole, -value, -transverse, origin})) return false;
                        coupled = coupled || transverse != origin;
                    }
                }
            }
            return true;
        };
        Rectangle footprint;
        if (s->isRational()) {
            const auto projection = regularRationalProjection(*s, axisX, axisY, axisZ);
            if (!projection) return {};
            exactY = projection->exactY;
            weightVariesV = projection->weightVariesV;
            nonlinear = true;
            footprint = projection->footprint;
        } else {
            if (!checkCoordinate(true, axisX) || !checkCoordinate(false, axisY)) return {};
            // With Xu,Yv >= 1/2 and |Xv| <= 1 the gradient bounds imply
            // |dz/dx| < 2e5 and |dz/dy| < 6e5.
            for (int j = 1; j < s->vPoleCount(); ++j) {
                const auto &knots = s->vKnots();
                const I rate = I(s->vDegree()) * (I(s->pole(0, j)[axisX]) - I(s->pole(0, j - 1)[axisX]))
                    / (I(knots[j + s->vDegree()]) - I(knots[j]));
                if (!finite(rate) || magnitude(rate) > 1.) return {};
            }
            if (!regularGraphContact(*s, axisZ)) return {};
            const auto lo = s->pole(0, 0);
            const auto hi = s->pole(s->uPoleCount() - 1, s->vPoleCount() - 1);
            footprint = {{minX, maxX}, {lo[axisY], hi[axisY]}};
        }
        for (const auto &other : strips) {
            if (!overlapsInterior(footprint, other.footprint) ||
                compatibleProjectedOverlap(*s, *other.surface, axisX, axisY, axisZ))
                continue;
            const bool comparable = (!s->isRational() && !other.surface->isRational()) || (exactY && other.exactY);
            if (!comparable || (!orderedProjectedStrips(*s, *other.surface, axisX, axisY) &&
                                !orderedProjectedStrips(*other.surface, *s, axisX, axisY)))
                return {};
        }
        strips.push_back({s, footprint, coupled, exactY, weightVariesV, constantGraphHeight(*s, axisZ).has_value()});
        nonlinear = nonlinear || coupled;
    }
    if (!nonlinear) return {};
    validate(image, true);
    auto orientedHeight = originalHeight;
    auto projectedWorld = world;
    std::optional<SourceProjectionAdjustment> adjustment;
    Work work;
    if (sourceGraph) {
        const auto directResidual = [&] {
            return directSourceResidual(sourceGraph, image, strips, sourceBox, world, originalHeight,
                                       {axisX, axisY}, budget, work);
        };
        if (sourceGraph->surface->uDegree() > 3 || sourceGraph->surface->vDegree() > 3) return directResidual();
        if (!exact(sourceGraph->zUV[0], 0.) || !exact(sourceGraph->zUV[1], 0.)) {
            adjustment = sourceProjectionAdjustment(*sourceGraph, projectedWorld, sourceBox);
            if (!adjustment) {
                const auto baseline = sourceReliefBaseline(*sourceGraph, sourceBox);
                for (std::size_t i = 0; i < world.size(); ++i)
                    projectedWorld[i] = addReliefBaseline(world[i], sourceGraph->zUV[i], baseline);
                orientedHeight = addReliefBaseline(orientedHeight, sourceGraph->zHeight, baseline);
                adjustment = sourceProjectionAdjustment(*sourceGraph, projectedWorld, sourceBox, baseline);
                if (!adjustment) return directResidual();
            }
        }
    } else {
        // A common height or a covered join of physical graphs can be
        // assessed on the image grid, even with rational charts. An
        // inconclusive optional pass keeps the existing strip traversal.
        Work directWork;
        try {
            const auto proof = directSourceResidual(nullptr, image, strips, sourceBox, world, originalHeight,
                                                   {axisX, axisY}, budget, directWork);
            if (proof.status == SurfaceMappingBoundStatus::Certified) return proof;
        } catch (const ProofFailed &) {
            // An exhausted optional join must still allow the strip proof.
        }
    }
    double upper = 0.;
    bool hasInterior = false;
    I sourceY = range(world[1], sourceBox.u, sourceBox.v);
    if (adjustment && !exact(sourceGraph->zUV[1], 0.))
        sourceY = sourceY + sourceGraph->zUV[1] * hull(graphZOn(*sourceGraph,
            P{I(sourceBox.u.lo), I(sourceBox.u.hi)}, P{I(sourceBox.v.lo), I(sourceBox.v.hi)}));
    for (const auto &strip : strips) {
        const auto *s = strip.surface;
        const auto &ku = s->uKnots(), &kv = s->vKnots();
        int firstV = s->vDegree(), endV = s->vPoleCount();
        if (strip.exactY) {
            const auto domain = s->vDomain();
            if (sourceY.hi < domain.lo || sourceY.lo > domain.hi) continue;
            // Y=V: skip spans disjoint from the outward source range, keeping
            // both one-sided polynomials when a limit is exactly on a knot.
            const auto first = kv.begin() + firstV;
            const auto last = kv.begin() + endV + 1;
            firstV = std::max(firstV, int(std::lower_bound(first, last, sourceY.lo) - kv.begin()) - 1);
            endV = std::min(endV, int(std::upper_bound(first, last, sourceY.hi) - kv.begin()));
        }
        for (int i = s->uDegree(); i < s->uPoleCount(); ++i) {
            cancellationPoint();
            if (ku[i] == ku[i + 1]) continue;
            for (int j = firstV; j < endV; ++j) {
                if (kv[j] == kv[j + 1]) continue;
                work.visit();
                Interval targetV{kv[j], kv[j + 1]};
                if (strip.exactY) {
                    // Y=V is exact on this strip. Clip before subdivision
                    // to avoid spending the budget on extrapolated image cells.
                    targetV = {std::max(targetV.lo, sourceY.lo), std::min(targetV.hi, sourceY.hi)};
                    if (targetV.lo > targetV.hi) continue;
                }
                upper = std::max(upper, nonlinearProjectedNode(strip, image, i, j,
                    {{ku[i], ku[i + 1]}, targetV}, sourceBox, projectedWorld, orientedHeight, sourceGraph,
                    adjustment ? &*adjustment : nullptr, axisX, axisY, budget, 0, work, hasInterior));
            }
        }
    }
    // Only enclose residuals where a contact exists. Coverage of the entire
    // source material is checked separately by the mapping caller.
    if (!hasInterior || !std::isfinite(upper)) return {};
    return {SurfaceMappingBoundStatus::Certified, upper};
}
}

SurfaceMappingBoundResult projectedMappingResidualBound(
    const Body &sourceBody, FaceId sourceFace, const std::vector<SurfaceTargetFace> &targets,
    const SurfaceWrapOptions &options, const Frame3 &targetChart,
    const Surface &image, Interval u, Interval v, double budget) {
    if (!u.isFinite() || !v.isFinite() || !(u.lo < u.hi) || !(v.lo < v.hi) ||
        !(budget > 0.) || !std::isfinite(budget))
        return {SurfaceMappingBoundStatus::Failed, infinity};
    bool establishedDomain = false;
    try {
        const auto &source = *sourceBody.face(sourceFace).surface;
        const auto *plane = dynamic_cast<const Plane *>(&source);
        const auto *fitted = dynamic_cast<const BSplineSurface *>(&image);
        if (!fitted || options.rigid || targets.size() < 2) return {};
        const auto *sourceGraph = dynamic_cast<const BSplineSurface *>(&source);
        const bool fullSource = fullRectangle(sourceBody, sourceFace, u, v);
        if (!plane) {
            const auto singleBezierSpan = [](int degree, int count, const std::vector<double> &knots, Interval domain) {
                return degree >= 1 && degree <= 5 && count == degree + 1 &&
                    knots.front() == domain.lo && knots.back() == domain.hi;
            };
            // World X/Y are the exact source parameters; the source height
            // contributes to both UV and offset under inclined projection.
            if (!sourceGraph || sourceGraph->basis() || sourceGraph->isRational() ||
                sourceGraph->isUPeriodic() || sourceGraph->isVPeriodic() || !fullSource ||
                !singleBezierSpan(sourceGraph->uDegree(), sourceGraph->uPoleCount(),
                                  sourceGraph->uKnots(), sourceGraph->uDomain()) ||
                !singleBezierSpan(sourceGraph->vDegree(), sourceGraph->vPoleCount(),
                                  sourceGraph->vKnots(), sourceGraph->vDomain()) ||
                !sourceGraph->uDomain().contains(u.lo) || !sourceGraph->uDomain().contains(u.hi) ||
                !sourceGraph->vDomain().contains(v.lo) || !sourceGraph->vDomain().contains(v.hi) ||
                validate(*sourceGraph, false) || !graphCoordinates(*sourceGraph, 0, 1)) return {};
        }
        const int axisX = cartesianAxis(targetChart.xDir()), axisY = cartesianAxis(targetChart.yDir());
        const int axisZ = cartesianAxis(targetChart.zDir());
        if (axisX < 0 || axisY < 0 || axisZ < 0 || axisX == axisY || axisX == axisZ || axisY == axisZ ||
            norm(cross(targetChart.xDir(), targetChart.yDir()) - targetChart.zDir()) != 0.) return {};
        auto chartOptions = options;
        chartOptions.anchor = {};
        const Plane projectedSource(Frame3{});
        const auto chartMapping = mapping(plane ? *plane : projectedSource, chartOptions, 1., 1.);
        std::array<Affine, 2> world = chartMapping.uv;
        for (std::size_t i = 0; i < world.size(); ++i) {
            const int axis = i == 0 ? axisX : axisY;
            const I sign(i == 0 ? targetChart.xDir()[axis] : targetChart.yDir()[axis]);
            auto &coordinate = world[i];
            coordinate = {I(targetChart.origin()[axis]) + sign * coordinate.origin,
                sign * coordinate.du, sign * coordinate.dv};
        }
        const auto &sourceFrame = options.sourcePlane;
        const I c(std::cos(options.angle)), s(std::sin(options.angle));
        const I gx(sourceFrame.xDir().z()), gy(sourceFrame.yDir().z());
        const std::array<I, 2> sourceHeightProjection{
            I(targetChart.xDir()[axisX]) * (c * gx - s * gy),
            I(targetChart.yDir()[axisY]) * (s * gx + c * gy)};
        std::vector<GraphPatch> graphs;
        for (const auto &target : targets) {
            cancellationPoint();
            const auto &face = target.body->face(target.face);
            const auto *spline = dynamic_cast<const BSplineSurface *>(face.surface.get());
            if (!spline || spline->basis() || spline->isUPeriodic() ||
                spline->isVPeriodic() || spline->uDegree() > 3 || spline->vDegree() > 3) return {};
            auto carrier = targetSurface(*spline);
            const auto affineGraph = [&](int x, int y) {
                if (!spline->isRational()) return graphCoordinates(*spline, x, y);
                return affineRationalCoordinate(*spline, true, x) && affineRationalCoordinate(*spline, false, y);
            };
            const bool direct = affineGraph(axisX, axisY);
            if (!direct && !affineGraph(axisY, axisX)) {
                const double direction = targetChart.xDir()[axisX] * targetChart.yDir()[axisY];
                std::optional<SourceGraph> graph;
                if (sourceGraph)
                    graph = SourceGraph{sourceGraph, I(direction) * I(sourceFrame.zDir().z()), sourceHeightProjection};
                try {
                    return nonlinearProjectedResidual(sourceBody, sourceFace, targets, *fitted, {u, v}, world,
                        chartMapping.height, graph ? &*graph : nullptr, axisX, axisY, axisZ, direction, budget);
                } catch (const ProofFailed &) {
                    return {};
                }
            }
            const Interval ud = spline->uDomain(), vd = spline->vDomain();
            const double sign = face.sense ? 1. : -1.;
            const auto &h = chartMapping.height;
            // Graphs with the opposite oriented normal never become contacts.
            if (sign != targetChart.xDir()[axisX] * targetChart.yDir()[axisY] * (direct ? 1. : -1.))
                continue;
            const std::array<Affine, 2> uv = direct ? world : std::array<Affine, 2>{world[1], world[0]};
            Mapping projected{uv, {I(sign) * h.origin, I(sign) * h.du, I(sign) * h.dv}, projectedLayout(uv)};
            if (sourceGraph) {
                auto zUV = sourceHeightProjection;
                if (!direct) std::swap(zUV[0], zUV[1]);
                projected.sourceGraph = SourceGraph{sourceGraph, I(sign) * I(sourceFrame.zDir().z()), zUV};
                if (hasNonlinearProjection(projected)) projected.layout = UVLayout::Oblique;
            }
            const GraphPatch patch{std::move(carrier), projected, direct ? ud : vd, direct ? vd : ud,
                fullRectangle(*target.body, target.face, ud, vd),
                regularGraphContact(*spline, axisZ)};
            for (const auto &other : graphs)
                if (overlapsInterior({patch.u, patch.v}, {other.u, other.v}) &&
                    !compatibleProjectedOverlap(*spline, other.carrier.base, axisX, axisY, axisZ)) return {};
            graphs.push_back(patch);
        }
        if (graphs.empty()) return {};
        // Every clipped target cell is tested against this same emitted surface.
        validate(*fitted, true);
        double upper = 0.;
        bool hadProof = false;
        bool complete = true;
        Work work;
        if (plane && fullSource) {
            const auto proof = affinePlanarControlNet(graphs, *fitted, {u, v}, world, chartMapping.height,
                {axisX, axisY}, I(targetChart.zDir()[axisZ]), budget, work);
            if (proof.status == SurfaceMappingBoundStatus::Certified) return proof;
        }
        for (const auto &patch : graphs) {
            cancellationPoint();
            Rectangle clipped{u, v};
            establishedDomain = false;
            const bool oblique = patch.map.layout == UVLayout::Oblique;
            const bool activeDomain = fullSource && patch.fullDomain && patch.contactIsRegular;
            const auto ud = patch.carrier.base.uDomain(), vd = patch.carrier.base.vDomain();
            const bool nonlinearProjection = hasNonlinearProjection(patch.map);
            if (nonlinearProjection) {
                if (!clipGraphCell(patch.map, {{ud, vd}, activeDomain}, clipped)) continue;
            } else if (oblique) {
                if (!clipObliqueCell(patch.map.uv, {{ud, vd}, activeDomain}, clipped)) continue;
            } else if (!clipProjectedAxis(patch.map.uv[0], ud, clipped) ||
                       !clipProjectedAxis(patch.map.uv[1], vd, clipped)) continue;
            const auto &us = patch.carrier.base.uKnots(), &vs = patch.carrier.base.vKnots();
            std::vector<TargetCell> uc, vc;
            const auto projectedRange = [&](int axis) {
                return nonlinearProjection ? hull(graphUVOn(patch.map, axis, clipped)) :
                    range(patch.map.uv[axis], clipped.u, clipped.v);
            };
            appendCells(us, patch.carrier.base.uDegree(), patch.carrier.base.uPoleCount(),
                patch.carrier.base.uDomain(), projectedRange(0), I(0), uc);
            appendCells(vs, patch.carrier.base.vDegree(), patch.carrier.base.vPoleCount(),
                patch.carrier.base.vDomain(), projectedRange(1), I(0), vc);
            if (uc.empty() || vc.empty() || uc.size() > maxCells / vc.size()) return {};
            auto carrier = patch.carrier;
            for (const auto &cu : uc) for (const auto &cv : vc) {
                establishedDomain = false;
                work.visit();
                const Interval uspan{us[cu.span], us[cu.span + 1]}, vspan{vs[cv.span], vs[cv.span + 1]};
                Rectangle cell = clipped;
                const ProjectedCell obliqueCell{{uspan, vspan}, activeDomain};
                if (nonlinearProjection) {
                    if (!clipGraphCell(patch.map, obliqueCell, cell)) continue;
                } else if (oblique) {
                    if (!clipObliqueCell(patch.map.uv, obliqueCell, cell)) continue;
                } else {
                    if (!clipProjectedAxis(patch.map.uv[0], uspan, cell) ||
                        !clipProjectedAxis(patch.map.uv[1], vspan, cell)) continue;
                    // A knot can expose an unused one-sided normal. An
                    // enclosure spanning only that side cannot reject.
                    establishedDomain = activeDomain && interiorAtMidpoint(patch.map, cell, uspan, vspan);
                }
                encloseDegenerateInterval(cell.u, u);
                encloseDegenerateInterval(cell.v, v);
                if (!(cell.u.lo < cell.u.hi) || !(cell.v.lo < cell.v.hi)) return {};
                carrier.graphSpans = std::array<int, 2>{cu.span, cv.span};
                const auto &h = patch.map.height;
                const bool scaled = !constant(h) && !strictSign(range(h, cell.u, cell.v));
                const bool requireWitness = oblique || patch.carrier.rational || sourceGraph;
                try {
                    const auto proof = proveImageRectangle(carrier, *fitted, patch.map,
                        cell.u, cell.v, budget, scaled, work, requireWitness ? &obliqueCell : nullptr);
                    upper = std::max(upper, proof.upperBound);
                    hadProof = true;
                } catch (const ProofFailed &) {
                    if (!requireWitness) throw;
                    // A loose enclosure of one oblique cell cannot hide a
                    // strict interior counterexample on a subsequent patch.
                    if (!work.left) return {};
                    complete = false;
                }
            }
        }
        if (complete && hadProof) return {SurfaceMappingBoundStatus::Certified, upper};
        return {};
    } catch (const Unsupported &) { return {}; }
      catch (const ProjectedResidualExceeded &) { return {SurfaceMappingBoundStatus::Failed, infinity}; }
      catch (const ProofFailed &) {
        return establishedDomain ? SurfaceMappingBoundResult{SurfaceMappingBoundStatus::Failed, infinity}
                                 : SurfaceMappingBoundResult{};
    }
}
}
