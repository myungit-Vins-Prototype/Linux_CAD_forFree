#include "fk_sweep_surface_validation.h"
#include "fk_cancel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
namespace ForgeCad::Kernel::detail {
namespace {
const double inf = std::numeric_limits<double>::infinity();
// Ogni operazione elementare include l'arrotondamento double. I dati di
// ingresso sono coefficienti esatti della mappa dichiarata dal chiamante.
struct I {
    double lo = 0, hi = 0;
    I() = default;
    I(double x) : lo(x), hi(x) {}
    I(double a, double b) : lo(a), hi(b) {}
};
double down(double x) { return std::nextafter(x, -inf); }
double up(double x) { return std::nextafter(x, inf); }
I entire() { return {-inf, inf}; }
bool finite(I a) { return std::isfinite(a.lo) && std::isfinite(a.hi) && a.lo <= a.hi; }
I checked(double lo, double hi) { return std::isfinite(lo) && std::isfinite(hi) && lo <= hi ? I(lo, hi) : entire(); }
I operator+(I a, I b) {
    if (!finite(a) || !finite(b))
        return entire();
    return checked(down(a.lo + b.lo), up(a.hi + b.hi));
}
I operator-(I a, I b) {
    if (!finite(a) || !finite(b))
        return entire();
    return checked(down(a.lo - b.hi), up(a.hi - b.lo));
}
I operator*(I a, I b) {
    if (!finite(a) || !finite(b))
        return entire();
    const double x[] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    for (double y : x)
        if (!std::isfinite(y))
            return entire();
    return checked(down(*std::min_element(x, x + 4)), up(*std::max_element(x, x + 4)));
}
I operator/(I a, I b) {
    if (!finite(a) || !finite(b) || !(b.lo > 0))
        return entire();
    return a * checked(down(1 / b.hi), up(1 / b.lo));
}
I sqr(I a) {
    if (!finite(a))
        return entire();
    if (a.lo <= 0 && a.hi >= 0)
        return checked(0, up(std::max(a.lo * a.lo, a.hi * a.hi)));
    return checked(down(std::min(a.lo * a.lo, a.hi * a.hi)), up(std::max(a.lo * a.lo, a.hi * a.hi)));
}
I hull(I a, I b) {
    if (a.lo > a.hi)
        return b;
    if (b.lo > b.hi)
        return a;
    if (std::isnan(a.lo) || std::isnan(a.hi) || std::isnan(b.lo) || std::isnan(b.hi))
        return entire();
    return {std::min(a.lo, b.lo), std::max(a.hi, b.hi)};
}
double mid(I a) { return .5 * a.lo + .5 * a.hi; }
using IV = std::array<I, 3>;
IV vec(Vec3 p) { return {I(p.x()), I(p.y()), I(p.z())}; }
IV operator+(const IV &a, const IV &b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
IV operator-(const IV &a, const IV &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
IV operator*(const IV &a, I b) { return {a[0] * b, a[1] * b, a[2] * b}; }
IV operator/(const IV &a, I b) { return {a[0] / b, a[1] / b, a[2] / b}; }
Vec3 midpoint(const IV &a) { return Vec3(mid(a[0]), mid(a[1]), mid(a[2])); }
I project(const IV &a, Vec3 axis) { return a[0] * I(axis.x()) + a[1] * I(axis.y()) + a[2] * I(axis.z()); }
struct Net {
    int nu = 0, nv = 0;
    bool independentU = false, independentV = false;
    std::vector<IV> h;
    std::vector<I> w;
    mutable bool ready = false;
    mutable IV du, dv;
    mutable std::vector<IV> duCoeffs, dvCoeffs;
    mutable I duDen, dvDen;
    std::size_t index(int i, int j) const { return std::size_t(i * nv + j); }
};
struct Rect {
    double u0 = 0, u1 = 1, v0 = 0, v1 = 1;
};
struct Cell {
    Net net;
    Rect box;
    int entry = 0, family = 0;
    int depth = 0;
};
I choose(int n, int k) {
    I v(1);
    for (int i = 1; i <= k; ++i)
        v = v * I(n - k + i) / I(i);
    return v;
}
bool same(I a, I b) { return a.lo == b.lo && a.hi == b.hi; }
Net geometryNet(const Net &n) {
    Net out;
    out.nu = n.nu;
    out.nv = n.nv;
    out.independentU = n.independentU;
    out.independentV = n.independentV;
    out.h = n.h;
    out.w = n.w;
    return out;
}
std::pair<Net, Net> splitNet(const Net &n, bool alongU, I t = I(.5)) {
    Net a = geometryNet(n), b = geometryNet(n), tmp = geometryNet(n);
    const int lines = alongU ? n.nv : n.nu, len = alongU ? n.nu : n.nv;
    const auto index = [&](int k, int l) { return alongU ? n.index(k, l) : n.index(l, k); };
    for (int l = 0; l < lines; ++l)
        for (int r = 1; r < len; ++r) {
            for (int i = 0; i + r < len; ++i) {
                const auto x = index(i, l), y = index(i + 1, l);
                tmp.h[x] = tmp.h[x] * (I(1) - t) + tmp.h[y] * t;
                if (!(alongU ? n.independentU : n.independentV) &&
                    !(tmp.w[x].lo == tmp.w[x].hi && same(tmp.w[x], tmp.w[y])))
                    tmp.w[x] = tmp.w[x] * (I(1) - t) + tmp.w[y] * t;
            }
            a.h[index(r, l)] = tmp.h[index(0, l)];
            a.w[index(r, l)] = tmp.w[index(0, l)];
            b.h[index(len - r - 1, l)] = tmp.h[index(len - r - 1, l)];
            b.w[index(len - r - 1, l)] = tmp.w[index(len - r - 1, l)];
        }
    if (n.ready) {
        // Numeratori Bernstein delle derivate razionali: la restrizione commuta
        // con de Casteljau, e la derivata riceve solo il fattore di riparametrizzazione.
        const auto carry = [&](bool derivativeU) {
            const bool independent = derivativeU ? n.independentU : n.independentV;
            Net field;
            field.nu = independent ? (derivativeU ? n.nu - 1 : n.nu) : (derivativeU ? 2 * n.nu - 2 : 2 * n.nu - 1);
            field.nv = independent ? (derivativeU ? n.nv : n.nv - 1) : (derivativeU ? 2 * n.nv - 1 : 2 * n.nv - 2);
            field.h = derivativeU ? n.duCoeffs : n.dvCoeffs;
            field.w.assign(field.h.size(), I(1));
            field.independentU = field.independentV = true;
            auto split = splitNet(field, alongU, t);
            for (int k = 0; k < 2; ++k) {
                Net &child = k ? b : a;
                auto &coeffs = derivativeU ? child.duCoeffs : child.dvCoeffs;
                coeffs = std::move(k ? split.second.h : split.first.h);
                if (derivativeU == alongU)
                    for (IV &c : coeffs)
                        c = c * (k ? I(1) - t : t);
                I weights = child.w.front();
                for (I w : child.w)
                    weights = hull(weights, w);
                I &denominator = derivativeU ? child.duDen : child.dvDen;
                denominator = independent ? weights : sqr(weights);
                IV &bound = derivativeU ? child.du : child.dv;
                bound = coeffs.front();
                for (const IV &c : coeffs)
                    for (int j = 0; j < 3; ++j)
                        bound[j] = hull(bound[j], c[j]);
                bound = bound / denominator;
            }
        };
        carry(true);
        carry(false);
        a.ready = b.ready = true;
    }
    return {std::move(a), std::move(b)};
}
I ratio(double x, double lo, double hi) {
    if (x == lo)
        return I(0);
    if (x == hi)
        return I(1);
    return (I(x) - I(lo)) / (I(hi) - I(lo));
}
Net restrictNet(const Net &input, I u0, I u1, I v0, I v1) {
    Net n = input;
    if (u1.lo != 1 || u1.hi != 1)
        n = splitNet(n, true, u1).first;
    if (u0.lo != 0 || u0.hi != 0)
        n = splitNet(n, true, u0 / u1).second;
    if (v1.lo != 1 || v1.hi != 1)
        n = splitNet(n, false, v1).first;
    if (v0.lo != 0 || v0.hi != 0)
        n = splitNet(n, false, v0 / v1).second;
    return n;
}
IV netBounds(const Net &n) {
    IV out = n.h.front() / n.w.front();
    for (std::size_t i = 1; i < n.h.size(); ++i) {
        const auto v = n.h[i] / n.w[i];
        for (int k = 0; k < 3; ++k)
            out[k] = hull(out[k], v[k]);
    }
    return out;
}
IV netMiddle(Net n) {
    n.ready = false;
    while (n.nu > 1 || n.nv > 1) {
        if (n.nu > 1) {
            auto p = splitNet(n, true);
            for (int j = 0; j < n.nv; ++j) {
                n.h[n.index(0, j)] = p.first.h[n.index(n.nu - 1, j)];
                n.w[n.index(0, j)] = p.first.w[n.index(n.nu - 1, j)];
            }
            n.nu = 1;
            n.h.resize(std::size_t(n.nv));
            n.w.resize(std::size_t(n.nv));
        }
        if (n.nv > 1) {
            auto p = splitNet(n, false);
            n.h.resize(1);
            n.h.front() = p.first.h.back();
            n.w.resize(1);
            n.w.front() = p.first.w.back();
            n.nv = 1;
        }
    }
    return n.h[0] / n.w[0];
}
IV derivative(const Net &net, bool alongU, std::vector<IV> *coeffs = nullptr, I *denominator = nullptr) {
    const int pu = net.nu - 1, pv = net.nv - 1, degree = alongU ? pu : pv;
    if (degree == 0)
        return vec(Vec3());
    const bool independent = alongU ? net.independentU : net.independentV;
    Net n = net;
    const Vec3 origin = midpoint(n.h.front() / n.w.front());
    for (std::size_t i = 0; i < n.h.size(); ++i)
        n.h[i] = n.h[i] - vec(origin) * n.w[i];
    const int au = alongU ? pu - 1 : pu, av = alongU ? pv : pv - 1;
    std::vector<IV> dh(std::size_t((au + 1) * (av + 1)));
    std::vector<I> dw(dh.size());
    for (int i = 0; i <= au; ++i)
        for (int j = 0; j <= av; ++j) {
            const auto a = n.index(i, j), b = n.index(i + (alongU ? 1 : 0), j + (alongU ? 0 : 1)),
                       k = std::size_t(i * (av + 1) + j);
            dh[k] = (n.h[b] - n.h[a]) * I(degree);
            dw[k] = (n.w[b] - n.w[a]) * I(degree);
        }
    I wb = n.w.front();
    for (I w : n.w)
        wb = hull(wb, w);
    if (independent) {
        if (coeffs)
            *coeffs = dh;
        if (denominator)
            *denominator = wb;
        IV result = dh.front();
        for (const auto &v : dh)
            for (int k = 0; k < 3; ++k)
                result[k] = hull(result[k], v[k]);
        return result / wb;
    }
    const int ou = au + pu, ov = av + pv;
    std::vector<IV> numerator(std::size_t((ou + 1) * (ov + 1)), vec(Vec3()));
    for (int i = 0; i <= au; ++i)
        for (int j = 0; j <= av; ++j)
            for (int k = 0; k <= pu; ++k)
                for (int l = 0; l <= pv; ++l) {
                    const I f = choose(au, i) * choose(pu, k) / choose(ou, i + k) * choose(av, j) * choose(pv, l) /
                                choose(ov, j + l);
                    const auto a = std::size_t(i * (av + 1) + j), b = n.index(k, l),
                               c = std::size_t((i + k) * (ov + 1) + j + l);
                    numerator[c] = numerator[c] + (dh[a] * n.w[b] - n.h[b] * dw[a]) * f;
                }
    if (coeffs)
        *coeffs = numerator;
    if (denominator)
        *denominator = sqr(wb);
    IV result = numerator.front();
    for (const auto &v : numerator)
        for (int k = 0; k < 3; ++k)
            result[k] = hull(result[k], v[k]);
    return result / sqr(wb);
}
I projectedDerivative(const Net &n, bool alongU, Vec3 axis) {
    const auto &coeffs = alongU ? n.duCoeffs : n.dvCoeffs;
    I bound(inf, -inf);
    for (const IV &c : coeffs)
        bound = hull(bound, project(c, axis));
    return bound / (alongU ? n.duDen : n.dvDen);
}
void prepare(const Net &n) {
    if (!n.ready) {
        cancellationPoint();
        n.du = derivative(n, true, &n.duCoeffs, &n.duDen);
        n.dv = derivative(n, false, &n.dvCoeffs, &n.dvDen);
        n.ready = true;
    }
}
// Inserimento di nodo in omogenee intervallari: mantiene l'inclusione della
// superficie BSpline originale, senza trattare cachedBezierPatches come esatta.
void insertKnot(Net &n, std::vector<double> &knots, int degree, double u, bool alongU) {
    const int count = alongU ? n.nu : n.nv, lines = alongU ? n.nv : n.nu;
    int span = degree;
    while (span + 1 < count && knots[std::size_t(span + 1)] <= u)
        ++span;
    int multiplicity = 0;
    for (double x : knots)
        if (x == u)
            ++multiplicity;
    Net out;
    out.independentU = n.independentU;
    out.independentV = n.independentV;
    out.nu = n.nu + (alongU ? 1 : 0);
    out.nv = n.nv + (alongU ? 0 : 1);
    out.h.resize(std::size_t(out.nu * out.nv));
    out.w.resize(out.h.size());
    const auto old = [&](int i, int l) { return alongU ? n.index(i, l) : n.index(l, i); };
    const auto next = [&](int i, int l) { return alongU ? out.index(i, l) : out.index(l, i); };
    for (int l = 0; l < lines; ++l) {
        for (int i = 0; i <= span - degree; ++i) {
            out.h[next(i, l)] = n.h[old(i, l)];
            out.w[next(i, l)] = n.w[old(i, l)];
        }
        for (int i = span - multiplicity; i < count; ++i) {
            out.h[next(i + 1, l)] = n.h[old(i, l)];
            out.w[next(i + 1, l)] = n.w[old(i, l)];
        }
        for (int i = span - degree + 1; i <= span - multiplicity; ++i) {
            const I alpha =
                (I(u) - I(knots[std::size_t(i)])) / (I(knots[std::size_t(i + degree)]) - I(knots[std::size_t(i)]));
            const auto a = old(i, l), b = old(i - 1, l), c = next(i, l);
            out.h[c] = n.h[a] * alpha + n.h[b] * (I(1) - alpha);
            out.w[c] = ((alongU ? n.independentU : n.independentV) || (n.w[a].lo == n.w[a].hi && same(n.w[a], n.w[b])))
                           ? n.w[a]
                           : n.w[a] * alpha + n.w[b] * (I(1) - alpha);
        }
    }
    knots.insert(knots.begin() + span + 1, u);
    n = std::move(out);
}
// Bound della differenza fra due bordi razionali con parametro comune.
double edgeGap(const Net &a, const Net &b, bool along, bool aHigh, bool bHigh) {
    const int na = along ? a.nv : a.nu, nb = along ? b.nv : b.nu;
    std::vector<IV> h(std::size_t(na + nb - 1));
    I wa(inf, -inf), wb(inf, -inf);
    const auto ix = [&](const Net &n, int k, bool high) {
        return along ? n.index(high ? n.nu - 1 : 0, k) : n.index(k, high ? n.nv - 1 : 0);
    };
    for (int i = 0; i < na; ++i) {
        const auto x = ix(a, i, aHigh);
        wa = hull(wa, a.w[x]);
        for (int j = 0; j < nb; ++j) {
            const auto y = ix(b, j, bHigh);
            const I f = choose(na - 1, i) * choose(nb - 1, j) / choose(na + nb - 2, i + j);
            h[std::size_t(i + j)] = h[std::size_t(i + j)] + (a.h[x] * b.w[y] - b.h[y] * a.w[x]) * f;
        }
    }
    for (int j = 0; j < nb; ++j)
        wb = hull(wb, b.w[ix(b, j, bHigh)]);
    IV bound{I(inf, -inf), I(inf, -inf), I(inf, -inf)};
    for (const auto &v : h)
        for (int k = 0; k < 3; ++k)
            bound[k] = hull(bound[k], v[k] / (wa * wb));
    I sum;
    for (I x : bound)
        sum = sum + sqr(x);
    return finite(sum) ? up(std::sqrt(std::max(0., sum.hi))) : inf;
}
struct Box {
    IV xyz{I(inf, -inf), I(inf, -inf), I(inf, -inf)};
};
Box merge(Box a, const Box &b) {
    for (int k = 0; k < 3; ++k)
        a.xyz[k] = hull(a.xyz[k], b.xyz[k]);
    return a;
}
bool apart(const Box &a, const Box &b) {
    for (int k = 0; k < 3; ++k)
        if (a.xyz[k].hi < b.xyz[k].lo || b.xyz[k].hi < a.xyz[k].lo)
            return true;
    return false;
}
struct Family {
    int id = 0;
    Rect extent;
    double up = 0, vp = 0, tolerance = 0;
    std::vector<int> cells, order;
    std::vector<double> maxV;
};
struct Node {
    Box bounds;
    Rect rect;
    int family = -1;
    std::vector<int> items;
    std::unique_ptr<Node> a, b;
    std::unique_ptr<Cell> part;
    bool leaf() const { return !a; }
};
class Validator {
  public:
    Validator(const std::vector<SweepSurfaceChart> &input, const SweepSurfaceValidationOptions &o)
        : entries(input), options(o) {}
    SweepSurfaceValidationResult run();
    bool prepareQuery() { return extract(); }
    SweepSurfaceProjectionBounds projection(const Frame3 &, Interval, Interval);
    SweepSurfaceCapContactResult capContact(const Frame3 &, SweepSurfaceBoundary, Interval, Interval, double);

  private:
    const std::vector<SweepSurfaceChart> &entries;
    SweepSurfaceValidationOptions options;
    SweepSurfaceValidationResult result;
    std::vector<Cell> cells;
    std::map<int, Family> families;
    bool extracted = false;
    bool visit() {
        cancellationPoint();
        if (result.visits >= options.maxVisits) {
            result.diagnostic = "budget validazione superfici sweep esaurito";
            return false;
        }
        ++result.visits;
        return true;
    }
    bool extract();
    bool coverage();
    std::unique_ptr<Node> build(std::vector<int> items);
    bool local(const Node &a, const Node &b);
    bool monotone(const std::vector<Cell> &patches, double gap, double tolerance);
    bool separated(const Node &a, const Node &b);
    bool splitNode(Node &n);
    bool self(Node &n);
    bool pairs(Node &a, Node &b);
};
bool Validator::extract() {
    if (extracted)
        return true;
    cells.clear();
    families.clear();
    std::size_t coefficients = 0;
    for (std::size_t e = 0; e < entries.size(); ++e) {
        cancellationPoint();
        const auto &entry = entries[e];
        if (!entry.surface || !(entry.u.hi > entry.u.lo) || !(entry.v.hi > entry.v.lo) || !entry.u.isFinite() ||
            !entry.v.isFinite() || !(entry.tolerance >= 0) || !std::isfinite(entry.tolerance) ||
            !(entry.poleError >= 0) || !std::isfinite(entry.poleError) || !(entry.weightError >= 0) ||
            !std::isfinite(entry.weightError))
            return false;
        const auto &s = *entry.surface;
        if (s.basis()) {
            result.diagnostic = "carrier istanziato richiede estrazione trasformata outward";
            return false;
        }
        const int pu = s.uDegree(), pv = s.vDegree();
        const auto &uk = s.uKnots();
        const auto &vk = s.vKnots();
        const auto ud = s.uDomain(), vd = s.vDomain();
        if (pu < 1 || pv < 1 || pu > 32 || pv > 32 || !ud.isFinite() || !vd.isFinite())
            return false;
        for (const auto *knots : {&uk, &vk}) {
            const int degree = knots == &uk ? pu : pv;
            const auto domain = knots == &uk ? ud : vd;
            for (std::size_t k = 0; k < knots->size();) {
                std::size_t j = k + 1;
                while (j < knots->size() && (*knots)[j] == (*knots)[k])
                    ++j;
                if ((*knots)[k] > domain.lo && (*knots)[k] < domain.hi && j - k > std::size_t(degree)) {
                    result.diagnostic = "carrier discontinuo internamente";
                    return false;
                }
                k = j;
            }
        }

        auto &family = families[entry.family];
        if (family.cells.empty()) {
            family.id = entry.family;
            family.extent = {entry.u.lo, entry.u.hi, entry.v.lo, entry.v.hi};
            family.up = entry.uPeriod;
            family.vp = entry.vPeriod;
            family.tolerance = entry.tolerance;
        } else {
            family.extent.u0 = std::min(family.extent.u0, entry.u.lo);
            family.extent.u1 = std::max(family.extent.u1, entry.u.hi);
            family.extent.v0 = std::min(family.extent.v0, entry.v.lo);
            family.extent.v1 = std::max(family.extent.v1, entry.v.hi);
            family.tolerance = std::min(family.tolerance, entry.tolerance);
            if (family.up != entry.uPeriod || family.vp != entry.vPeriod)
                return false;
        }
        for (int ui = pu; ui < s.uPoleCount(); ++ui)
            if (uk[std::size_t(ui + 1)] > uk[std::size_t(ui)])
                for (int vi = pv; vi < s.vPoleCount(); ++vi)
                    if (vk[std::size_t(vi + 1)] > vk[std::size_t(vi)]) {
                        cancellationPoint();
                        if (cells.size() >= options.maxCells ||
                            (coefficients += std::size_t((pu + 1) * (pv + 1))) > options.maxCoefficients) {
                            result.diagnostic = "budget preparazione superfici sweep esaurito";
                            return false;
                        }
                        Net n;
                        n.independentU = entry.weightError == 0;
                        n.independentV = entry.weightError == 0;
                        n.nu = pu + 1;
                        n.nv = pv + 1;
                        n.h.resize(std::size_t(n.nu * n.nv));
                        n.w.resize(n.h.size());
                        for (int i = 0; i < n.nu; ++i)
                            for (int j = 0; j < n.nv; ++j) {
                                const int si = ui - pu + i, sj = vi - pv + j;
                                const double weight = s.weight(si, sj);
                                if (!(weight > 0) || !std::isfinite(weight) || !isFinite(s.pole(si, sj)))
                                    return false;
                                const auto k = n.index(i, j);
                                if (i > 0 && weight != s.weight(ui - pu, sj))
                                    n.independentU = false;
                                if (j > 0 && weight != s.weight(si, vi - pv))
                                    n.independentV = false;
                                n.w[k] = entry.weightError == 0 ? I(weight)
                                                                : checked(down(weight - entry.weightError),
                                                                          up(weight + entry.weightError));
                                if (!(n.w[k].lo > 0))
                                    return false;
                                IV point = vec(s.pole(si, sj));
                                if (entry.poleError > 0)
                                    for (I &coordinate : point)
                                        coordinate = coordinate + I(-entry.poleError, entry.poleError);
                                n.h[k] = weight == 1 && entry.weightError == 0 ? point : point * n.w[k];
                            }
                        std::vector<double> u(uk.begin() + ui - pu, uk.begin() + ui + pu + 2),
                            v(vk.begin() + vi - pv, vk.begin() + vi + pv + 2);
                        for (bool along : {true, false}) {
                            auto &knots = along ? u : v;
                            const int p = along ? pu : pv;
                            const double lo = along ? uk[std::size_t(ui)] : vk[std::size_t(vi)],
                                         hi = along ? uk[std::size_t(ui + 1)] : vk[std::size_t(vi + 1)];
                            for (double x : {lo, hi}) {
                                int mult = int(std::count(knots.begin(), knots.end(), x));
                                while (mult++ < p)
                                    insertKnot(n, knots, p, x, along);
                            }
                        }
                        int us = pu, vs = pv;
                        while (us + 1 < n.nu && u[std::size_t(us + 1)] <= uk[std::size_t(ui)])
                            ++us;
                        while (vs + 1 < n.nv && v[std::size_t(vs + 1)] <= vk[std::size_t(vi)])
                            ++vs;
                        Net piece;
                        piece.independentU = n.independentU;
                        piece.independentV = n.independentV;
                        piece.nu = pu + 1;
                        piece.nv = pv + 1;
                        for (int i = us - pu; i <= us; ++i)
                            for (int j = vs - pv; j <= vs; ++j) {
                                piece.h.push_back(n.h[n.index(i, j)]);
                                piece.w.push_back(n.w[n.index(i, j)]);
                            }
                        const auto mapU = [&](double x) {
                            if (x == ud.lo)
                                return entry.u.lo;
                            if (x == ud.hi)
                                return entry.u.hi;
                            return entry.u.lo + (entry.u.hi - entry.u.lo) * ((x - ud.lo) / (ud.hi - ud.lo));
                        };
                        const auto mapV = [&](double x) {
                            if (x == vd.lo)
                                return entry.v.lo;
                            if (x == vd.hi)
                                return entry.v.hi;
                            return entry.v.lo + (entry.v.hi - entry.v.lo) * ((x - vd.lo) / (vd.hi - vd.lo));
                        };
                        Cell c;
                        c.net = std::move(piece);
                        c.entry = int(e);
                        c.family = entry.family;
                        c.box = {mapU(uk[std::size_t(ui)]), mapU(uk[std::size_t(ui + 1)]), mapV(vk[std::size_t(vi)]),
                                 mapV(vk[std::size_t(vi + 1)])};
                        if (!(c.box.u1 > c.box.u0) || !(c.box.v1 > c.box.v0))
                            return false;
                        family.cells.push_back(int(cells.size()));
                        cells.push_back(std::move(c));
                    }
    }
    result.cells = cells.size();
    extracted = !cells.empty();
    return extracted;
}
bool Validator::coverage() {
    for (const auto &item : families) {
        const Family &f = item.second;
        if ((f.up != 0 && f.up != f.extent.u1 - f.extent.u0) || (f.vp != 0 && f.vp != f.extent.v1 - f.extent.v0))
            return false;
        std::vector<double> cuts;
        for (const auto &e : entries)
            if (e.family == f.id) {
                cuts.push_back(e.u.lo);
                cuts.push_back(e.u.hi);
            }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
            cancellationPoint();
            std::vector<std::pair<double, double>> ranges;
            for (const auto &e : entries)
                if (e.family == f.id && e.u.lo <= cuts[i] && e.u.hi >= cuts[i + 1])
                    ranges.push_back({e.v.lo, e.v.hi});
            std::sort(ranges.begin(), ranges.end());
            double end = f.extent.v0;
            for (auto r : ranges) {
                if (r.first != end)
                    return false;
                end = r.second;
            }
            if (end != f.extent.v1)
                return false;
        }
    }
    // I periodi dichiarano una cucitura reale: prima di qualsiasi prova locale
    // verifichiamo anche che i due bordi fondamentali coincidano entro tolerance.
    for (const auto &item : families) {
        const Family &f = item.second;
        for (bool along : {true, false}) {
            if (!(along ? f.up : f.vp))
                continue;
            std::vector<int> low, high;
            for (int id : f.cells) {
                const Cell &c = cells[std::size_t(id)];
                if ((along ? c.box.u0 : c.box.v0) == (along ? f.extent.u0 : f.extent.v0))
                    low.push_back(id);
                if ((along ? c.box.u1 : c.box.v1) == (along ? f.extent.u1 : f.extent.v1))
                    high.push_back(id);
            }
            for (int ai : low)
                for (int bi : high) {
                    cancellationPoint();
                    const Cell &a = cells[std::size_t(ai)], &b = cells[std::size_t(bi)];
                    const double lo = std::max(along ? a.box.v0 : a.box.u0, along ? b.box.v0 : b.box.u0),
                                 hi = std::min(along ? a.box.v1 : a.box.u1, along ? b.box.v1 : b.box.u1);
                    if (!(hi > lo))
                        continue;
                    if (!visit())
                        return false;
                    const auto trim = [&](const Cell &c) {
                        return along ? restrictNet(c.net, 0, 1, ratio(lo, c.box.v0, c.box.v1),
                                                   ratio(hi, c.box.v0, c.box.v1))
                                     : restrictNet(c.net, ratio(lo, c.box.u0, c.box.u1), ratio(hi, c.box.u0, c.box.u1),
                                                   0, 1);
                    };
                    if (edgeGap(trim(a), trim(b), along, false, true) > f.tolerance) {
                        result.diagnostic = "cucitura periodica fuori tolleranza";
                        return false;
                    }
                }
        }
    }
    for (auto &item : families) {
        auto &f = item.second;
        f.order = f.cells;
        std::sort(f.order.begin(), f.order.end(),
                  [&](int a, int b) { return cells[std::size_t(a)].box.v0 < cells[std::size_t(b)].box.v0; });
        double end = -inf;
        for (int id : f.order) {
            end = std::max(end, cells[std::size_t(id)].box.v1);
            f.maxV.push_back(end);
        }
    }

    return true;
}
std::unique_ptr<Node> Validator::build(std::vector<int> items) {
    cancellationPoint();
    auto n = std::make_unique<Node>();
    n->items = items;
    n->family = cells[std::size_t(items.front())].family;
    n->rect = cells[std::size_t(items.front())].box;
    for (int i : items) {
        const auto &c = cells[std::size_t(i)];
        if (c.family != n->family)
            n->family = -1;
        n->bounds = merge(n->bounds, {netBounds(c.net)});
        n->rect.u0 = std::min(n->rect.u0, c.box.u0);
        n->rect.u1 = std::max(n->rect.u1, c.box.u1);
        n->rect.v0 = std::min(n->rect.v0, c.box.v0);
        n->rect.v1 = std::max(n->rect.v1, c.box.v1);
    }
    if (items.size() == 1) {
        n->part = std::make_unique<Cell>(cells[std::size_t(items[0])]);
        return n;
    }
    int axis = 0;
    for (int k = 1; k < 3; ++k)
        if (n->bounds.xyz[k].hi - n->bounds.xyz[k].lo > n->bounds.xyz[axis].hi - n->bounds.xyz[axis].lo)
            axis = k;
    std::sort(items.begin(), items.end(), [&](int a, int b) {
        return mid(netBounds(cells[std::size_t(a)].net)[axis]) < mid(netBounds(cells[std::size_t(b)].net)[axis]);
    });
    const auto m = items.begin() + std::ptrdiff_t(items.size() / 2);
    n->a = build(std::vector<int>(items.begin(), m));
    n->b = build(std::vector<int>(m, items.end()));
    return n;
}
bool Validator::monotone(const std::vector<Cell> &patches, double gap, double tolerance) {
    if (patches.empty())
        return false;
    Vec3 a, b;
    for (const auto &p : patches) {
        prepare(p.net);
        a += midpoint(p.net.du) / (p.box.u1 - p.box.u0);
        b += midpoint(p.net.dv) / (p.box.v1 - p.box.v0);
    }
    a = a / double(patches.size());
    b = b / double(patches.size());
    const double scaleU = norm(a), scaleV = norm(b);
    if (!(scaleU > 0) || !(scaleV > 0) || !std::isfinite(scaleU) || !std::isfinite(scaleV))
        return false;
    a = a / scaleU;
    b = b / scaleV;
    const double aa = dot(a, a), bb = dot(b, b), ab = dot(a, b), det = aa * bb - ab * ab;
    if (!(det > 0) || !std::isfinite(det))
        return false;
    const Vec3 left[2] = {(bb * a - ab * b) / det, (aa * b - ab * a) / det};
    I matrix[2][2]{{I(inf, -inf), I(inf, -inf)}, {I(inf, -inf), I(inf, -inf)}};
    double lipschitz = 0;
    for (const auto &p : patches) {
        prepare(p.net);
        const IV cols[2] = {p.net.du / ((I(p.box.u1) - I(p.box.u0)) * I(scaleU)),
                            p.net.dv / ((I(p.box.v1) - I(p.box.v0)) * I(scaleV))};
        double lip = 0;
        for (int j = 0; j < 2; ++j) {
            double length = 0;
            for (int k = 0; k < 3; ++k) {
                const double x = std::max(std::fabs(cols[j][k].lo), std::fabs(cols[j][k].hi));
                length = up(length + up(x * x));
            }
            lip = up(lip + up(std::sqrt(length)));
            for (int i = 0; i < 2; ++i)
                matrix[i][j] =
                    hull(matrix[i][j], projectedDerivative(p.net, j == 0, left[i]) /
                                           ((I(j == 0 ? p.box.u1 : p.box.v1) - I(j == 0 ? p.box.u0 : p.box.v0)) *
                                            I(j == 0 ? scaleU : scaleV)));
        }
        lipschitz = std::max(lipschitz, lip);
    }
    for (double s : {aa / bb, bb / aa, 1., .1, 10., .01, 100.}) {
        const I x = I(s) * matrix[0][0], z = matrix[1][1], y = (I(s) * matrix[0][1] + matrix[1][0]) * I(.5);
        const I determinant = x * z - sqr(y);
        if (!(x.lo > 0) || !(z.lo > 0) || !(determinant.lo > 0))
            continue;
        const I eigen = determinant / (x + z);
        if (!(eigen.lo > 0))
            continue;
        const auto boundNorm = [](Vec3 x) {
            I sum;
            for (int k = 0; k < 3; ++k)
                sum = sum + sqr(I(k == 0 ? x.x() : (k == 1 ? x.y() : x.z())));
            return up(std::sqrt(sum.hi));
        };
        const I normL = I(boundNorm(left[0])) * I(s) + I(boundNorm(left[1]));
        const I deviationBound = I(lipschitz) * normL * I(gap) / I(eigen.lo) + I(gap);
        const double deviation = deviationBound.hi;
        if (gap == 0 || deviation <= tolerance) {
            result.adjacencyDeviation = std::max(result.adjacencyDeviation, deviation);
            return true;
        }
    }
    return false;
}
bool Validator::local(const Node &a, const Node &b) {
    if (!visit() || a.family < 0 || a.family != b.family)
        return false;
    const Family &f = families.at(a.family);
    const auto shift = [&](double a0, double a1, double b0, double b1, double period) {
        double best = 0, size = std::max(a1, b1) - std::min(a0, b0);
        if (period > 0)
            for (double x : {-period, period}) {
                const double next = std::max(a1, b1 + x) - std::min(a0, b0 + x);
                if (next < size) {
                    size = next;
                    best = x;
                }
            }
        return best;
    };
    const double su = shift(a.rect.u0, a.rect.u1, b.rect.u0, b.rect.u1, f.up),
                 sv = shift(a.rect.v0, a.rect.v1, b.rect.v0, b.rect.v1, f.vp);
    const Rect r{std::min(a.rect.u0, b.rect.u0 + su), std::max(a.rect.u1, b.rect.u1 + su),
                 std::min(a.rect.v0, b.rect.v0 + sv), std::max(a.rect.v1, b.rect.v1 + sv)};
    std::vector<Cell> query;
    struct Candidate {
        const Cell *c;
        Rect box;
        double u, v;
    };
    std::vector<Candidate> candidates;
    for (int ku = -1; ku <= 1; ++ku) {
        if (ku && f.up == 0)
            continue;
        for (int kv = -1; kv <= 1; ++kv) {
            if (kv && f.vp == 0)
                continue;
            const double u = ku * f.up, v = kv * f.vp;
            const auto start = std::upper_bound(f.maxV.begin(), f.maxV.end(), down(r.v0 - v));
            const std::size_t begin = std::size_t(start - f.maxV.begin());
            for (std::size_t oi = begin; oi < f.order.size(); ++oi) {
                const auto &c = cells[std::size_t(f.order[oi])];
                if (c.box.v0 >= up(r.v1 - v))
                    break;
                const Rect x{std::max(r.u0, c.box.u0 + u), std::min(r.u1, c.box.u1 + u), std::max(r.v0, c.box.v0 + v),
                             std::min(r.v1, c.box.v1 + v)};
                if (!(x.u1 > x.u0) || !(x.v1 > x.v0))
                    continue;
                if (candidates.size() >= options.maxChartCells)
                    return false;
                candidates.push_back({&c, x, u, v});
            }
        }
    }
    for (const auto &candidate : candidates) {
        const auto &c = *candidate.c;
        const auto &x = candidate.box;
        const double u = candidate.u, v = candidate.v;
        prepare(c.net);
        Cell p = c;
        if (x.u0 != c.box.u0 + u || x.u1 != c.box.u1 + u || x.v0 != c.box.v0 + v || x.v1 != c.box.v1 + v)
            p.net = restrictNet(c.net, (I(x.u0) - I(c.box.u0) - I(u)) / (I(c.box.u1) - I(c.box.u0)),
                                (I(x.u1) - I(c.box.u0) - I(u)) / (I(c.box.u1) - I(c.box.u0)),
                                (I(x.v0) - I(c.box.v0) - I(v)) / (I(c.box.v1) - I(c.box.v0)),
                                (I(x.v1) - I(c.box.v0) - I(v)) / (I(c.box.v1) - I(c.box.v0)));
        p.box = x;
        query.push_back(std::move(p));
    }

    // Ogni bordo condiviso, inclusa la chiusura periodica, riceve un bound.
    // Sommare tutti i salti domina qualsiasi segmento nella griglia rettangolare.
    I totalGap;
    for (std::size_t i = 0; i < query.size(); ++i)
        for (std::size_t j = i + 1; j < query.size(); ++j) {
            cancellationPoint();
            const Cell &a = query[i], &b = query[j];
            bool along = false, ah = false, bh = false;
            double lo = 0, hi = 0;
            if (a.box.u1 == b.box.u0 || b.box.u1 == a.box.u0) {
                along = true;
                ah = a.box.u1 == b.box.u0;
                bh = !ah;
                lo = std::max(a.box.v0, b.box.v0);
                hi = std::min(a.box.v1, b.box.v1);
            } else if (a.box.v1 == b.box.v0 || b.box.v1 == a.box.v0) {
                along = false;
                ah = a.box.v1 == b.box.v0;
                bh = !ah;
                lo = std::max(a.box.u0, b.box.u0);
                hi = std::min(a.box.u1, b.box.u1);
            } else
                continue;
            if (!(hi > lo))
                continue;
            const auto trim = [&](const Cell &c) {
                if (along)
                    return restrictNet(c.net, 0, 1, ratio(lo, c.box.v0, c.box.v1), ratio(hi, c.box.v0, c.box.v1));
                return restrictNet(c.net, ratio(lo, c.box.u0, c.box.u1), ratio(hi, c.box.u0, c.box.u1), 0, 1);
            };
            // Un nodo interno dello stesso carrier e senza traslazione periodica è C0.
            const auto &e = entries[std::size_t(a.entry)];
            const bool ordinary = a.entry == b.entry && a.box.u0 >= e.u.lo && a.box.u1 <= e.u.hi &&
                                  b.box.u0 >= e.u.lo && b.box.u1 <= e.u.hi && a.box.v0 >= e.v.lo &&
                                  a.box.v1 <= e.v.hi && b.box.v0 >= e.v.lo && b.box.v1 <= e.v.hi;
            if (!ordinary)
                totalGap = totalGap + I(edgeGap(trim(a), trim(b), along, ah, bh));
        }
    return finite(totalGap) && monotone(query, totalGap.hi, f.tolerance);
}
bool Validator::separated(const Node &a, const Node &b) {
    if (apart(a.bounds, b.bounds))
        return true;
    if (!a.part || !b.part)
        return false;
    std::vector<Vec3> axes;
    for (const Node *n : {&a, &b}) {
        prepare(n->part->net);
        axes.push_back(cross(midpoint(n->part->net.du), midpoint(n->part->net.dv)));
    }
    axes.push_back(midpoint(netMiddle(a.part->net)) - midpoint(netMiddle(b.part->net)));
    for (Vec3 axis : axes) {
        if (!isFinite(axis) || !(norm(axis) > 0))
            continue;
        I ranges[2];
        int side = 0;
        for (const Node *n : {&a, &b}) {
            bool first = true;
            for (std::size_t i = 0; i < n->part->net.h.size(); ++i) {
                const I x = project(n->part->net.h[i] / n->part->net.w[i], axis);
                ranges[side] = first ? x : hull(ranges[side], x);
                first = false;
            }
            ++side;
        }
        if (ranges[0].hi < ranges[1].lo || ranges[1].hi < ranges[0].lo)
            return true;
    }
    return false;
}
bool Validator::splitNode(Node &n) {
    if (!n.leaf())
        return true;
    if (!n.part || n.part->depth >= options.maxDepth || result.cells + 2 > options.maxCells) {
        result.diagnostic = "limite suddivisione superfici sweep raggiunto";
        return false;
    }
    prepare(n.part->net);
    const double u = norm(midpoint(n.part->net.du)),
                 v = norm(
                     midpoint(n.part->net.dv)); // On anisotropic rectangles, repeatedly bisecting the physically longer
    // direction can leave the Jacobian variation in the other direction intact.
    // Alternation only chooses the next subdivision; all certificates are unchanged.
    const bool anisotropic = u / 4 > v || v / 4 > u;
    const bool along = anisotropic ? n.part->depth % 2 == 0 : u >= v;
    auto parts = splitNet(n.part->net, along);
    n.a = std::make_unique<Node>();
    n.b = std::make_unique<Node>();
    for (int k = 0; k < 2; ++k) {
        auto &child = k ? *n.b : *n.a;
        child.family = n.family;
        child.part = std::make_unique<Cell>(*n.part);
        child.part->net = k ? std::move(parts.second) : std::move(parts.first);
        ++child.part->depth;
        child.rect = n.rect;
        if (along) {
            const double m = .5 * n.rect.u0 + .5 * n.rect.u1;
            if (k)
                child.rect.u0 = m;
            else
                child.rect.u1 = m;
        } else {
            const double m = .5 * n.rect.v0 + .5 * n.rect.v1;
            if (k)
                child.rect.v0 = m;
            else
                child.rect.v1 = m;
        }
        child.part->box = child.rect;
        child.bounds = {netBounds(child.part->net)};
        result.deepestSubdivision = std::max(result.deepestSubdivision, child.part->depth);
    }
    result.cells += 2;
    return true;
}
bool Validator::pairs(Node &a, Node &b) {
    if (!visit())
        return false;
    if (separated(a, b) || local(a, b))
        return true;
    const bool divideA = (!a.leaf() && b.leaf()) ||
                         (a.leaf() == b.leaf() &&
                          ((a.part && b.part) ? a.part->depth <= b.part->depth : a.items.size() >= b.items.size()));
    Node &node = divideA ? a : b;
    if (!splitNode(node))
        return false;
    return divideA ? (pairs(*a.a, b) && pairs(*a.b, b)) : (pairs(a, *b.a) && pairs(a, *b.b));
}
bool Validator::self(Node &n) {
    if (!visit())
        return false;
    if (local(n, n))
        return true;
    if (!splitNode(n))
        return false;
    return self(*n.a) && self(*n.b) && pairs(*n.a, *n.b);
}
SweepSurfaceProjectionBounds Validator::projection(const Frame3 &plane, Interval ur, Interval vr) {
    SweepSurfaceProjectionBounds out;
    if (!ur.isFinite() || !vr.isFinite() || !(ur.hi > ur.lo) || !(vr.hi > vr.lo) || !extract())
        return out;
    IV bounds{I(inf, -inf), I(inf, -inf), I(inf, -inf)};
    bool found = false;
    for (const Cell &c : cells) {
        cancellationPoint();
        const Rect r{std::max(c.box.u0, ur.lo), std::min(c.box.u1, ur.hi), std::max(c.box.v0, vr.lo),
                     std::min(c.box.v1, vr.hi)};
        if (!(r.u1 > r.u0) || !(r.v1 > r.v0))
            continue;
        Net n = restrictNet(c.net, ratio(r.u0, c.box.u0, c.box.u1), ratio(r.u1, c.box.u0, c.box.u1),
                            ratio(r.v0, c.box.v0, c.box.v1), ratio(r.v1, c.box.v0, c.box.v1));
        for (std::size_t i = 0; i < n.h.size(); ++i) {
            const IV p = n.h[i] / n.w[i] - vec(plane.origin());
            bounds[0] = hull(bounds[0], project(p, plane.xDir()));
            bounds[1] = hull(bounds[1], project(p, plane.yDir()));
            bounds[2] = hull(bounds[2], project(p, plane.zDir()));
        }
        found = true;
    }
    if (found && finite(bounds[0]) && finite(bounds[1]) && finite(bounds[2])) {
        out.resolved = true;
        out.lo = {bounds[0].lo, bounds[1].lo};
        out.hi = {bounds[0].hi, bounds[1].hi};
        out.height = {bounds[2].lo, bounds[2].hi};
    }
    return out;
}
SweepSurfaceCapContactResult Validator::capContact(const Frame3 &plane, SweepSurfaceBoundary side, Interval ur,
                                                   Interval vr, double tolerance) {
    SweepSurfaceCapContactResult out;
    if (!(tolerance >= 0) || !std::isfinite(tolerance) || !ur.isFinite() || !vr.isFinite() || !(ur.hi > ur.lo) ||
        !(vr.hi > vr.lo) || !extract())
        return out;
    bool found = false;
    const bool along = side == SweepSurfaceBoundary::UMin || side == SweepSurfaceBoundary::UMax;
    const bool high = side == SweepSurfaceBoundary::UMax || side == SweepSurfaceBoundary::VMax;
    for (const Cell &c : cells) {
        cancellationPoint();
        const Rect r{std::max(c.box.u0, ur.lo), std::min(c.box.u1, ur.hi), std::max(c.box.v0, vr.lo),
                     std::min(c.box.v1, vr.hi)};
        if (!(r.u1 > r.u0) || !(r.v1 > r.v0))
            continue;
        Net n = restrictNet(c.net, ratio(r.u0, c.box.u0, c.box.u1), ratio(r.u1, c.box.u0, c.box.u1),
                            ratio(r.v0, c.box.v0, c.box.v1), ratio(r.v1, c.box.v0, c.box.v1));
        found = true;
        const auto height = [&](IV p) { return project(p - vec(plane.origin()), plane.zDir()); };
        const I hb = height(netBounds(n));
        if (hb.lo > 0 || hb.hi < 0)
            continue;
        const auto &e = entries.front();
        const double edge = along ? (high ? r.u1 : r.u0) : (high ? r.v1 : r.v0),
                     declared = along ? (high ? e.u.hi : e.u.lo) : (high ? e.v.hi : e.v.lo);
        if (edge != declared) {
            out.diagnostic = "cella non separata e non incidente al bordo genealogico";
            return out;
        }
        const IV d = derivative(n, along);
        const I slope = project(d, plane.zDir());
        const double lower = slope.lo > 0 ? slope.lo : (slope.hi < 0 ? -slope.hi : 0);
        if (!(lower > 0)) {
            out.diagnostic = "altezza non monotona rispetto al bordo cap";
            return out;
        }
        I boundaryHeight(inf, -inf);
        const int count = along ? n.nv : n.nu;
        for (int i = 0; i < count; ++i) {
            const auto k = along ? n.index(high ? n.nu - 1 : 0, i) : n.index(i, high ? n.nv - 1 : 0);
            boundaryHeight = hull(boundaryHeight, height(n.h[k] / n.w[k]));
        }
        I speed;
        for (I x : d)
            speed = speed + sqr(x);
        const I length(up(std::sqrt(speed.hi)));
        const double deviation =
            (I(std::max(std::fabs(boundaryHeight.lo), std::fabs(boundaryHeight.hi))) / I(lower) * length).hi;
        if (!std::isfinite(deviation) || deviation > tolerance) {
            out.diagnostic = "contatto cap non confinato nella tolleranza";
            return out;
        }
        out.deviation = std::max(out.deviation, deviation);
    }
    out.confined = found;
    return out;
}
SweepSurfaceValidationResult Validator::run() {
    if (!extract() || !coverage()) {
        if (result.diagnostic.empty())
            result.diagnostic = "atlante superfici sweep non valido o incompleto";
        return result;
    }
    std::vector<int> ids(cells.size());
    for (std::size_t i = 0; i < ids.size(); ++i)
        ids[i] = int(i);
    auto root = build(std::move(ids));
    if (self(*root)) {
        result.status = SweepSurfaceValidationStatus::Certified;
        result.diagnostic = "superfici sweep certificate";
    } else if (result.diagnostic.empty())
        result.diagnostic = "intersezioni superfici sweep non escluse";
    return result;
}
} // namespace
struct SweepSurfaceQuery::Data {
    std::vector<SweepSurfaceChart> entries;
    Validator validator;
    bool ready;
    explicit Data(const SweepSurfaceChart &chart)
        : entries{chart}, validator(entries, {}), ready(validator.prepareQuery()) {}
};
SweepSurfaceQuery::SweepSurfaceQuery(const SweepSurfaceChart &chart) : data_(std::make_shared<Data>(chart)) {}
SweepSurfaceProjectionBounds SweepSurfaceQuery::projection(const Frame3 &plane, Interval ur, Interval vr) const {
    return data_->ready ? data_->validator.projection(plane, ur, vr) : SweepSurfaceProjectionBounds{};
}
SweepSurfaceCapContactResult SweepSurfaceQuery::capContact(const Frame3 &plane, SweepSurfaceBoundary side, Interval ur,
                                                           Interval vr, double tolerance) const {
    return data_->ready ? data_->validator.capContact(plane, side, ur, vr, tolerance) : SweepSurfaceCapContactResult{};
}
SweepSurfaceValidationResult validateSweepSurfaces(const std::vector<SweepSurfaceChart> &charts,
                                                   const SweepSurfaceValidationOptions &options) {
    SweepSurfaceValidationResult r;
    if (charts.empty() || options.maxVisits == 0 || options.maxCells == 0 || options.maxCoefficients == 0 ||
        options.maxChartCells == 0 || options.maxDepth < 0) {
        r.diagnostic = "input vuoto o budget esaurito";
        return r;
    }
    return Validator(charts, options).run();
}
SweepSurfaceCapContactResult boundSweepSurfaceCapContact(const SweepSurfaceChart &chart, const Frame3 &plane,
                                                         SweepSurfaceBoundary boundary, Interval uRange,
                                                         Interval vRange, double tolerance) {
    return Validator(std::vector<SweepSurfaceChart>{chart}, {}).capContact(plane, boundary, uRange, vRange, tolerance);
}

SweepSurfaceProjectionBounds boundSweepSurfaceProjection(const SweepSurfaceChart &chart, const Frame3 &plane,
                                                         Interval ur, Interval vr) {
    return Validator(std::vector<SweepSurfaceChart>{chart}, {}).projection(plane, ur, vr);
}

} // namespace ForgeCad::Kernel::detail
