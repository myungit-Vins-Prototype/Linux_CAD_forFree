#include "fk_sweep_caps.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_curve_ops.h"
#include "fk_nurbs.h"
#include "fk_precision.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ForgeCad::Kernel::detail {
namespace {
const double inf = std::numeric_limits<double>::infinity();
struct I {
    double lo = 0, hi = 0;
    I() = default;
    I(double x) : lo(x), hi(x) {}
    I(double a, double b) : lo(a), hi(b) {}
};
bool valid(I a) { return std::isfinite(a.lo) && std::isfinite(a.hi) && a.lo <= a.hi; }
I entire() { return {-inf, inf}; }
I checked(double lo, double hi) { return std::isfinite(lo) && std::isfinite(hi) && lo <= hi ? I(lo, hi) : entire(); }
double down(double x) { return std::nextafter(x, -inf); }
double up(double x) { return std::nextafter(x, inf); }
I operator+(I a, I b) {
    if (!valid(a) || !valid(b))
        return entire();
    return checked(down(a.lo + b.lo), up(a.hi + b.hi));
}
I operator-(I a, I b) {
    if (!valid(a) || !valid(b))
        return entire();
    return checked(down(a.lo - b.hi), up(a.hi - b.lo));
}
I operator*(I a, I b) {
    if (!valid(a) || !valid(b))
        return entire();
    double q[] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    for (double x : q)
        if (!std::isfinite(x))
            return entire();
    return checked(down(*std::min_element(q, q + 4)), up(*std::max_element(q, q + 4)));
}
I operator/(I a, I b) {
    if (!valid(a) || !valid(b) || !(b.lo > 0))
        return entire();
    return a * checked(down(1 / b.hi), up(1 / b.lo));
}
// Taylor con resto esplicito per |x| <= 7; nessuna ipotesi su libm.
I trig(I x, bool sine) {
    if (!valid(x) || std::max(std::fabs(x.lo), std::fabs(x.hi)) > 7)
        return entire();
    I term = sine ? x : I(1), sum = term;
    const I xx = x * x;
    for (int k = 1; k <= 24; ++k) {
        const int a = sine ? 2 * k : 2 * k - 1;
        term = term * xx / I(a * (a + 1)) * I(-1);
        sum = sum + term;
    }
    I remainder(1);
    for (int k = 1; k <= 50; ++k)
        remainder = remainder * I(7) / I(k);
    return sum + I(-remainder.hi, remainder.hi);
}
I hull(I a, I b) {
    if (a.lo > a.hi)
        return b;
    if (b.lo > b.hi)
        return a;
    if (!valid(a) || !valid(b))
        return entire();
    return {std::min(a.lo, b.lo), std::max(a.hi, b.hi)};
}
using H = std::array<I, 3>;
using P = std::array<I, 2>;
using Span = std::vector<H>;
H add(const H &a, const H &b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
H mul(const H &a, I b) { return {a[0] * b, a[1] * b, a[2] * b}; }
P point(const H &a) { return {a[0] / a[2], a[1] / a[2]}; }
std::array<I, 2> spanBox(const Span &s) {
    P b{I(inf, -inf), I(inf, -inf)};
    for (const H &h : s) {
        const P p = point(h);
        for (int k = 0; k < 2; ++k)
            b[k] = hull(b[k], p[k]);
    }
    return b;
}
bool apart(const P &a, const P &b) {
    return a[0].hi < b[0].lo || b[0].hi < a[0].lo || a[1].hi < b[1].lo || b[1].hi < a[1].lo;
}
std::pair<Span, Span> split(const Span &s) {
    Span a = s, b = s, t = s;
    for (std::size_t k = 1; k < s.size(); ++k) {
        for (std::size_t i = 0; i + k < s.size(); ++i)
            t[i] = mul(add(t[i], t[i + 1]), I(.5));
        a[k] = t[0];
        b[s.size() - k - 1] = t[s.size() - k - 1];
    }
    return {a, b};
}
I orientation(const P &a, const P &b, const P &p) {
    return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
}
// Outward insertion on the stored B-spline, not on rounded Bezier cache.
bool insert(std::vector<double> &knots, Span &p, int degree, double u) {
    const int n = int(p.size()) - 1;
    int k = degree;
    while (k < n && knots[std::size_t(k + 1)] <= u)
        ++k;
    const int mult = int(std::count(knots.begin(), knots.end(), u));
    if (mult > degree)
        return false;
    Span q(p.size() + 1);
    for (int i = 0; i <= k - degree; ++i)
        q[std::size_t(i)] = p[std::size_t(i)];
    for (int i = k - mult; i <= n; ++i)
        q[std::size_t(i + 1)] = p[std::size_t(i)];
    for (int i = k - degree + 1; i <= k - mult; ++i) {
        const I alpha =
            (I(u) - I(knots[std::size_t(i)])) / (I(knots[std::size_t(i + degree)]) - I(knots[std::size_t(i)]));
        q[std::size_t(i)] = add(mul(p[std::size_t(i - 1)], I(1) - alpha), mul(p[std::size_t(i)], alpha));
    }
    knots.insert(knots.begin() + k + 1, u);
    p = std::move(q);
    return true;
}
bool splineSpans(const BSplineCurve<3> &c, const Interval &range, std::vector<Span> &out, double roundoff = 0) {
    if (!c.isClamped() || range.lo < c.domain().lo || range.hi > c.domain().hi)
        return false;
    const int d = c.degree();
    auto knots = c.knots();
    Span h;
    double maxWeight = 0;
    for (int i = 0; i < c.poleCount(); ++i)
        maxWeight = std::max(maxWeight, c.weight(i));
    if (!(maxWeight > 0) || !std::isfinite(maxWeight))
        return false;
    for (int i = 0; i < c.poleCount(); ++i) {
        cancellationPoint();
        const I w = I(c.weight(i)) / I(maxWeight);
        const auto p = c.poles()[std::size_t(i)];
        h.push_back({(I(p.x()) + I(-roundoff, roundoff)) * w, (I(p.y()) + I(-roundoff, roundoff)) * w, w});
    }
    std::vector<double> breaks{range.lo, range.hi};
    for (double u : c.knots())
        if (u > range.lo && u < range.hi)
            breaks.push_back(u);
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
    for (double u : breaks) {
        if (u == c.domain().lo || u == c.domain().hi)
            continue;
        while (std::count(knots.begin(), knots.end(), u) < d) {
            cancellationPoint();
            if (!insert(knots, h, d, u))
                return false;
        }
    }
    for (int k = d; k < int(h.size()); ++k)
        if (knots[std::size_t(k)] >= range.lo && knots[std::size_t(k + 1)] <= range.hi &&
            knots[std::size_t(k)] < knots[std::size_t(k + 1)])
            out.emplace_back(h.begin() + k - d, h.begin() + k + 1);
    return !out.empty();
}
bool curveSpans(const CurvePtr<2> &curve, const Interval &range, std::vector<Span> &out) {
    if (!curve || !range.isFinite() || !(range.lo < range.hi))
        return false;
    const Curve<2> *basis = curve.get();
    while (basis->type() == CurveType::Trimmed)
        basis = static_cast<const TrimmedCurve<2> *>(basis)->basis().get();
    if (basis->type() == CurveType::Line) {
        const auto &c = static_cast<const Line<2> &>(*basis);
        Span s;
        for (double t : {range.lo, range.hi})
            s.push_back({I(c.origin().x()) + I(t) * I(c.direction().x()),
                         I(c.origin().y()) + I(t) * I(c.direction().y()), I(1)});
        out.push_back(s);
        return true;
    }
    if (basis->type() == CurveType::BSpline) {
        const auto &c = static_cast<const BSplineCurve<2> &>(*basis);
        std::vector<Vec3> p;
        for (auto q : c.poles())
            p.emplace_back(q.x(), q.y(), 0);
        const BSplineCurve<3> embedded(c.degree(), c.knots(), p, c.weights());
        return splineSpans(embedded, range, out);
    }
    if (basis->type() != CurveType::Circle && basis->type() != CurveType::Ellipse)
        return false;
    // Rappresentazione razionale della conica con coefficienti outward:
    // non attribuire esattezza alla conversione NURBS gia' arrotondata.
    if (std::fabs(range.lo) > 6.4 || std::fabs(range.hi) > 6.4 || range.hi - range.lo > 6.4)
        return false;
    Vec2 center, x, y;
    if (basis->type() == CurveType::Circle) {
        const auto &c = static_cast<const Circle<2> &>(*basis);
        center = c.center();
        x = c.xAxis();
        y = c.yAxis();
    } else {
        const auto &c = static_cast<const Ellipse<2> &>(*basis);
        center = c.center();
        x = c.xAxis();
        y = c.yAxis();
    }
    const I rx(basis->type() == CurveType::Circle ? static_cast<const Circle<2> &>(*basis).radius()
                                                  : static_cast<const Ellipse<2> &>(*basis).xRadius());
    const I ry(basis->type() == CurveType::Circle ? static_cast<const Circle<2> &>(*basis).radius()
                                                  : static_cast<const Ellipse<2> &>(*basis).yRadius());
    const int count = std::max(1, int(std::ceil((range.hi - range.lo) / kHalfPi)));
    const I step = (I(range.hi) - I(range.lo)) / I(count), weight = trig(step / I(2), false);
    if (!(weight.lo > 0))
        return false;
    const auto conicPoint = [&](I angle, I w) {
        const I c = trig(angle, false), s = trig(angle, true);
        return H{I(center.x()) * w + I(x.x()) * rx * c + I(y.x()) * ry * s,
                 I(center.y()) * w + I(x.y()) * rx * c + I(y.y()) * ry * s, w};
    };
    for (int k = 0; k < count; ++k) {
        cancellationPoint();
        const I lo = k == 0 ? I(range.lo) : I(range.lo) + I(k) * step;
        const I hi = k + 1 == count ? I(range.hi) : I(range.lo) + I(k + 1) * step;
        out.push_back({conicPoint(lo, I(1)), conicPoint(lo + step / I(2), weight), conicPoint(hi, I(1))});
    }
    return true;
}
struct Query {
    P box;
    Vec2 center, rayNormal{0, 1};
    CapDomainOptions options;
    std::size_t *remaining = nullptr;
    std::vector<std::pair<P, P>> chords;
    bool collect(const Span &s, int depth) {
        cancellationPoint();
        if (!remaining || *remaining == 0)
            return false;
        --*remaining;
        const auto b = spanBox(s);
        if (apart(b, box)) {
            chords.push_back({point(s.front()), point(s.back())});
            return true;
        }
        if (depth >= options.maxDepth)
            return false;
        const auto p = split(s);
        return collect(p.first, depth + 1) && collect(p.second, depth + 1);
    }
    bool edge(const P &a, const P &b, int &winding) {
        if (!remaining || *remaining == 0)
            return false;
        --*remaining;
        const P edgeBox{hull(a[0], b[0]), hull(a[1], b[1])};
        if (!apart(edgeBox, box))
            return false;
        const I ay = (a[0] - I(center.x())) * I(rayNormal.x()) + (a[1] - I(center.y())) * I(rayNormal.y());
        const I by = (b[0] - I(center.x())) * I(rayNormal.x()) + (b[1] - I(center.y())) * I(rayNormal.y());
        const bool upCross = ay.hi <= 0 && by.lo > 0, downCross = ay.lo > 0 && by.hi <= 0;
        if (!upCross && !downCross) {
            if ((ay.hi <= 0 && by.hi <= 0) || (ay.lo > 0 && by.lo > 0))
                return true;
            return false;
        }
        const I o = orientation(a, b, P{I(center.x()), I(center.y())});
        if (!valid(o) || !(o.lo > 0 || o.hi < 0))
            return false;
        if (upCross && o.lo > 0)
            ++winding;
        if (downCross && o.hi < 0)
            --winding;
        return true;
    }
};
P mapBox(const Frame3 &a, const Frame3 &b, const P &box, I *height) {
    P uv{I(inf, -inf), I(inf, -inf)};
    *height = I(inf, -inf);
    for (double u : {box[0].lo, box[0].hi})
        for (double v : {box[1].lo, box[1].hi}) {
            I p[3];
            for (int k = 0; k < 3; ++k)
                p[k] = I(a.origin()[k]) - I(b.origin()[k]) + I(u) * I(a.xDir()[k]) + I(v) * I(a.yDir()[k]);
            I x, y, z;
            for (int k = 0; k < 3; ++k) {
                x = x + p[k] * I(b.xDir()[k]);
                y = y + p[k] * I(b.yDir()[k]);
                z = z + p[k] * I(b.zDir()[k]);
            }
            uv[0] = hull(uv[0], x);
            uv[1] = hull(uv[1], y);
            *height = hull(*height, z);
        }
    return uv;
}
} // namespace
struct CapDomainIndex::Data {
    Frame3 plane;
    double tolerance = 0;
    bool supported = true;
    std::vector<std::vector<Span>> loops;
    P box{I(inf, -inf), I(inf, -inf)};
};
CapDomainIndex::CapDomainIndex(const std::vector<ProfileLoop> &loops, const Frame3 &plane, double tolerance) {
    auto d = std::make_shared<Data>();
    d->plane = plane;
    d->tolerance = tolerance;
    if (!std::isfinite(tolerance) || tolerance < 0)
        d->supported = false;
    for (const auto &loop : loops) {
        cancellationPoint();
        std::vector<Span> spans;
        for (const auto &s : loop.segments)
            if (!curveSpans(s.curve, s.range, spans))
                d->supported = false;
        for (const auto &s : spans) {
            const auto b = spanBox(s);
            for (int k = 0; k < 2; ++k)
                d->box[k] = hull(d->box[k], b[k]);
        }
        d->loops.push_back(std::move(spans));
    }
    data_ = std::move(d);
}
CapDomainIndex::CapDomainIndex(const Body &body, FaceId face, double tolerance) {
    const auto &f = body.face(face);
    if (!f.surface || f.surface->type() != SurfaceType::Plane)
        throw std::invalid_argument("CapDomainIndex: faccia non piana");
    std::vector<ProfileLoop> loops;
    for (auto l : f.loops) {
        ProfileLoop loop;
        for (auto id : body.loopFins(l)) {
            const auto &fin = body.fin(id);
            const auto &e = body.edge(fin.edge);
            tolerance = std::max(tolerance, std::max(linearResolution(), e.tolerance) + fin.pcurveTolerance);
            if (!fin.pcurve)
                throw std::invalid_argument("CapDomainIndex: SP-curve assente");
            ProfileSegment s{fin.pcurve, e.range};
            if (!fin.sense)
                s = reversed(s);
            loop.segments.push_back(s);
        }
        loops.push_back(std::move(loop));
    }
    *this = CapDomainIndex(loops, static_cast<const Plane &>(*f.surface).frame(), tolerance);
}
CapBoxLocation CapDomainIndex::classifyBox(const Vec2 &lo, const Vec2 &hi, int selected,
                                           const CapDomainOptions &options) const {
    std::size_t remaining = options.maxVisits;
    return classifyBoxBudget(lo, hi, selected, options, remaining);
}
CapBoxLocation CapDomainIndex::classifyBoxBudget(const Vec2 &lo, const Vec2 &hi, int selected,
                                                 const CapDomainOptions &options, std::size_t &remaining) const {
    cancellationPoint();
    if (!data_->supported || data_->loops.empty() || selected >= int(data_->loops.size()) || selected < -1 ||
        options.maxDepth < 0 || options.maxVisits == 0 || !isFinite(lo) || !isFinite(hi) || lo.x() > hi.x() ||
        lo.y() > hi.y())
        return CapBoxLocation::Unresolved;
    Query q;
    q.options = options;
    q.remaining = &remaining;
    for (int k = 0; k < 2; ++k)
        q.box[k] = I(lo[k], hi[k]) + I(-data_->tolerance, data_->tolerance);
    q.center = Vec2(.5 * lo.x() + .5 * hi.x(), .5 * lo.y() + .5 * hi.y());
    bool inOuter = false, inHole = false;
    for (std::size_t l = 0; l < data_->loops.size(); ++l) {
        if (selected >= 0 && int(l) != selected)
            continue;
        q.chords.clear();
        for (const auto &s : data_->loops[l])
            if (!q.collect(s, 0))
                return CapBoxLocation::Unresolved;
        if (q.chords.empty())
            return CapBoxLocation::Unresolved;
        std::vector<Vec2> normals{Vec2(0, 1), Vec2(1, .3819660112501051), Vec2(1, -.6180339887498949),
                                  Vec2(.4142135623730951, 1)};
        std::vector<double> angles;
        for (const auto &edge : q.chords)
            for (const auto &p : {edge.first, edge.second})
                angles.push_back(
                    std::atan2(.5 * p[1].lo + .5 * p[1].hi - q.center.y(), .5 * p[0].lo + .5 * p[0].hi - q.center.x()));
        std::sort(angles.begin(), angles.end());
        double bestGap = -1, bestAngle = 0;
        for (std::size_t i = 0; i < angles.size(); ++i) {
            const double next = i + 1 < angles.size() ? angles[i + 1] : angles[0] + kTwoPi;
            const double gap = next - angles[i];
            if (gap > bestGap) {
                bestGap = gap;
                bestAngle = angles[i] + .5 * gap + kHalfPi;
            }
        }
        normals.emplace_back(std::cos(bestAngle), std::sin(bestAngle));
        bool classified = false;
        int loopWinding = 0;
        for (Vec2 normal : normals) {
            q.rayNormal = normal;
            int local = 0;
            bool okay = true;
            for (std::size_t i = 0; i < q.chords.size(); ++i) {
                if (!q.edge(q.chords[i].first, q.chords[i].second, local)) {
                    okay = false;
                    break;
                }
                const auto &a = q.chords[i].second, &b = q.chords[(i + 1) % q.chords.size()].first;
                if (!q.edge(a, b, local)) {
                    okay = false;
                    break;
                }
            }
            if (okay) {
                loopWinding = local;
                classified = true;
                break;
            }
        }
        if (!classified)
            return CapBoxLocation::Unresolved;
        if (selected >= 0)
            return loopWinding == 0 ? CapBoxLocation::Outside : CapBoxLocation::Inside;
        if (l == 0) {
            inOuter = loopWinding != 0;
            if (!inOuter)
                return CapBoxLocation::Outside;
        } else
            inHole = inHole || loopWinding != 0;
    }
    return inOuter && !inHole ? CapBoxLocation::Inside : CapBoxLocation::Outside;
}
CapBoxLocation CapDomainIndex::classifyPoint(const Vec2 &p, int loop, const CapDomainOptions &o) const {
    return classifyBox(p, p, loop, o);
}
const Frame3 &CapDomainIndex::frame() const { return data_->plane; }
std::pair<Vec2, Vec2> CapDomainIndex::bounds() const {
    return {Vec2(data_->box[0].lo, data_->box[1].lo), Vec2(data_->box[0].hi, data_->box[1].hi)};
}
std::size_t CapDomainIndex::loopCount() const { return data_->loops.size(); }
SweepSurfaceValidationResult validateSweepSurfaceCap(const std::vector<SweepSurfaceChart> &charts,
                                                     const CapDomainIndex &cap,
                                                     const std::vector<int> &boundaryFamilies, double boundaryV,
                                                     double tolerance, const SweepSurfaceValidationOptions &options) {
    SweepSurfaceValidationResult result;
    result.diagnostic = "intersezione laterale/cap non esclusa";
    if (charts.empty() || !std::isfinite(boundaryV) || !(tolerance >= 0) || !std::isfinite(tolerance) ||
        options.maxVisits == 0 || options.maxCells == 0 || options.maxDepth < 0)
        return result;
    struct Cell {
        std::size_t chart;
        Interval u, v;
        int depth;
    };
    // L'estrazione outward del carrier e' invariabile durante la traversal.
    std::vector<std::unique_ptr<SweepSurfaceQuery>> queries;
    queries.reserve(charts.size());
    std::vector<Cell> pending;
    for (std::size_t i = 0; i < charts.size(); ++i) {
        const auto &chart = charts[i];
        if (!chart.surface || !chart.u.isFinite() || !chart.v.isFinite() || !(chart.u.hi > chart.u.lo) ||
            !(chart.v.hi > chart.v.lo))
            return result;
        cancellationPoint();
        queries.push_back(std::make_unique<SweepSurfaceQuery>(chart));
        pending.push_back({i, chart.u, chart.v, 0});
    }
    if (pending.size() > options.maxCells)
        return result;
    result.cells = pending.size();
    std::size_t remaining = options.maxVisits;
    const CapDomainOptions capOptions{options.maxVisits, options.maxDepth};
    while (!pending.empty()) {
        cancellationPoint();
        if (remaining == 0) {
            result.diagnostic = "budget laterale/cap esaurito";
            result.visits = options.maxVisits;
            return result;
        }
        --remaining;
        const auto c = pending.back();
        pending.pop_back();
        const auto &chart = charts[c.chart];
        const auto bounds = queries[c.chart]->projection(cap.frame(), c.u, c.v);
        if (!bounds.resolved) {
            result.diagnostic = "bound laterale/cap non disponibile";
            result.visits = options.maxVisits - remaining;
            return result;
        }
        if (bounds.height.lo > tolerance || bounds.height.hi < -tolerance)
            continue;
        const bool genealogical =
            std::find(boundaryFamilies.begin(), boundaryFamilies.end(), chart.family) != boundaryFamilies.end();
        if (genealogical &&
            ((boundaryV == chart.v.lo && c.v.lo == boundaryV) || (boundaryV == chart.v.hi && c.v.hi == boundaryV))) {
            const auto boundary = boundaryV == chart.v.lo ? SweepSurfaceBoundary::VMin : SweepSurfaceBoundary::VMax;
            const auto contact = queries[c.chart]->capContact(cap.frame(), boundary, c.u, c.v, tolerance);
            if (contact.confined) {
                result.adjacencyDeviation = std::max(result.adjacencyDeviation, contact.deviation);
                continue;
            }
        }
        if (cap.classifyBoxBudget(bounds.lo, bounds.hi, -1, capOptions, remaining) == CapBoxLocation::Outside)
            continue;
        if (c.depth >= options.maxDepth || result.cells + 2 > options.maxCells) {
            result.diagnostic = "limite suddivisione laterale/cap raggiunto";
            result.visits = options.maxVisits - remaining;
            return result;
        }
        // Alternare gli assi garantisce restringimento di entrambi anche con
        // parametrizzazioni estremamente diverse; la scelta non certifica nulla.
        const bool alongU = c.depth % 2 != 0;
        Cell left = c, right = c;
        auto &a = alongU ? left.u : left.v;
        auto &b = alongU ? right.u : right.v;
        const double middle = .5 * a.lo + .5 * a.hi;
        if (!(middle > a.lo && middle < a.hi)) {
            result.visits = options.maxVisits - remaining;
            return result;
        }
        a.hi = middle;
        b.lo = middle;
        left.depth = right.depth = c.depth + 1;
        result.deepestSubdivision = std::max(result.deepestSubdivision, left.depth);
        result.cells += 2;
        pending.push_back(right);
        pending.push_back(left);
    }
    result.status = SweepSurfaceValidationStatus::Certified;
    result.diagnostic = "laterali disgiunti dal cap salvo bordi genealogici entro tolleranza";
    result.visits = options.maxVisits - remaining;
    return result;
}
bool capDomainsDisjoint(const CapDomainIndex &a, const CapDomainIndex &b, double tolerance,
                        const CapDomainOptions &options) {
    if (!(tolerance >= 0) || !std::isfinite(tolerance) || options.maxDepth < 0 || options.maxVisits == 0)
        return false;
    const auto initial = a.bounds();
    if (!isFinite(initial.first) || !isFinite(initial.second))
        return false;
    struct Cell {
        Vec2 lo, hi;
        int depth;
    };
    std::vector<Cell> pending{{initial.first, initial.second, 0}};
    std::size_t remaining = options.maxVisits;
    while (!pending.empty()) {
        cancellationPoint();
        if (remaining == 0)
            return false;
        --remaining;
        const auto c = pending.back();
        pending.pop_back();
        P box{I(c.lo.x(), c.hi.x()), I(c.lo.y(), c.hi.y())};
        I height;
        const auto mapped = mapBox(a.frame(), b.frame(), box, &height);
        if (valid(height) && (height.lo > tolerance || height.hi < -tolerance))
            continue;
        const auto inA = a.classifyBoxBudget(c.lo, c.hi, -1, options, remaining);
        if (inA == CapBoxLocation::Outside)
            continue;
        const Vec2 blo(mapped[0].lo, mapped[1].lo), bhi(mapped[0].hi, mapped[1].hi);
        const auto inB = b.classifyBoxBudget(blo, bhi, -1, options, remaining);
        if (inB == CapBoxLocation::Outside)
            continue;
        if (inA == CapBoxLocation::Inside && inB == CapBoxLocation::Inside)
            return false;
        if (c.depth >= options.maxDepth)
            return false;
        const int axis = c.hi.x() - c.lo.x() >= c.hi.y() - c.lo.y() ? 0 : 1;
        const double mid = .5 * c.lo[axis] + .5 * c.hi[axis];
        if (!(mid > c.lo[axis] && mid < c.hi[axis]))
            return false;
        Cell left = c, right = c;
        left.hi[axis] = mid;
        right.lo[axis] = mid;
        left.depth = right.depth = c.depth + 1;
        pending.push_back(right);
        pending.push_back(left);
    }
    return true;
}
} // namespace ForgeCad::Kernel::detail
