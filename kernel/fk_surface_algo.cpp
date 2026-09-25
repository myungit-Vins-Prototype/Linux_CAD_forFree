#include "fk_surface_algo.h"

#include <algorithm>
#include <queue>

#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_precision.h"

namespace ForgeCad::Kernel {
namespace {

constexpr int kGridPerPatch = 8;

SurfaceProjection projectionAt(const Surface &surface, const Vec3 &p, double u, double v) {
    const Vec3 point = surface.point(u, v);
    return {u, v, point, distance(point, p)};
}

class Candidates {
public:
    Candidates(const Surface &surface, const Vec3 &p, const Interval &uRange, const Interval &vRange)
        : surface_(surface), p_(p), uRange_(uRange), vRange_(vRange) {}

    void consider(const SurfaceProjection &candidate) {
        if (!valid_ || candidate.distance < best_.distance) {
            best_ = candidate;
            valid_ = true;
        }
    }
    void consider(double u, double v) { consider(projectionAt(surface_, p_, u, v)); }

    // Punto stazionario in forma chiusa: conta solo se cade nella pezza
    // (nelle direzioni periodiche lo si riporta nel periodo giusto).
    void considerInterior(double u, double v) {
        if (bringIntoRange(u, uRange_, surface_.isUPeriodic(), surface_.uPeriod(), u)
            && bringIntoRange(v, vRange_, surface_.isVPeriodic(), surface_.vPeriod(), v))
            consider(u, v);
    }

    const SurfaceProjection &best() const { return best_; }
    double bestDistance() const { return valid_ ? best_.distance : std::numeric_limits<double>::infinity(); }

private:
    static bool bringIntoRange(double t, const Interval &range, bool periodic, double period, double &out) {
        if (periodic && std::isfinite(range.lo)) {
            double shifted = t - range.lo;
            shifted -= period * std::floor(shifted / period);
            out = range.lo + shifted;
        } else {
            out = t;
        }
        return range.contains(out);
    }

    const Surface &surface_;
    const Vec3 &p_;
    Interval uRange_, vRange_;
    SurfaceProjection best_;
    bool valid_ = false;
};

// Isoparametrica generica (senza forma esatta) letta dalla superficie.
class IsoAdapter final : public Curve<3> {
public:
    IsoAdapter(const Surface &surface, bool fixedU, double value) : surface_(surface), fixedU_(fixedU), value_(value) {}

    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return fixedU_ ? surface_.vDomain() : surface_.uDomain(); }
    void evaluate(double t, int order, Vec3 *out) const override {
        std::vector<Vec3> d((order + 1) * (order + 1));
        if (fixedU_) surface_.evaluate(value_, t, order, d.data());
        else surface_.evaluate(t, value_, order, d.data());
        for (int k = 0; k <= order; ++k)
            out[k] = fixedU_ ? d[Surface::derivativeIndex(0, k, order)] : d[Surface::derivativeIndex(k, 0, order)];
    }
    std::vector<double> breakpoints(const Interval &range) const override {
        return fixedU_ ? surface_.vBreakpoints(range) : surface_.uBreakpoints(range);
    }

private:
    const Surface &surface_;
    bool fixedU_;
    double value_;
};

// Bordi della pezza, e con `allPatchEdges` anche le isoparametriche dei
// breakpoint interni: su uno spigolo interno (nodo C0) il minimo non e' un
// punto stazionario di nessuna delle due pezze, ma e' il minimo della curva
// isoparametrica, che si trova in modo esatto.
void projectOnBoundary(const Surface &surface, const Vec3 &p, const Interval &uRange, const Interval &vRange,
                       Candidates &candidates, bool allPatchEdges = false) {
    auto edge = [&](bool fixedU, double value, const Interval &range) {
        if (!std::isfinite(value)) return;
        const CurvePtr<3> iso = fixedU ? surface.uIso(value) : surface.vIso(value);
        CurveProjection<3> result;
        if (iso) {
            result = projectPoint(*iso, p, range);
        } else {
            const IsoAdapter adapter(surface, fixedU, value);
            // Isoparametrica degenere (polo della sfera, vertice del cono): un punto solo.
            const Vec3 first = adapter.point(range.lo);
            if (range.isFinite() && distance(first, adapter.point(0.5 * (range.lo + range.hi))) <= kLinearResolution
                && distance(first, adapter.point(range.hi)) <= kLinearResolution) {
                result.parameter = range.lo;
            } else {
                result = projectPoint(adapter, p, range);
            }
        }
        if (fixedU) candidates.consider(value, result.parameter);
        else candidates.consider(result.parameter, value);
    };
    if (allPatchEdges) {
        for (double u : surface.uBreakpoints(uRange)) edge(true, u, vRange);
        for (double v : surface.vBreakpoints(vRange)) edge(false, v, uRange);
        return;
    }
    edge(true, uRange.lo, vRange);
    edge(true, uRange.hi, vRange);
    edge(false, vRange.lo, uRange);
    edge(false, vRange.hi, uRange);
}

// Newton su grad(|S - p|^2 / 2) = (Su.d, Sv.d) = 0 con d = S - p. Hessiana
// H = [Suu.d + Su.Su, Suv.d + Su.Sv; ..., Svv.d + Sv.Sv]; dove non e' definita
// positiva si usa Gauss-Newton (J^T J). Passo dimezzato se la distanza non
// scende, sempre dentro la pezza liscia `box`.
void refineProjection(const Surface &surface, const Vec3 &p, double &u, double &v, const Interval &uBox,
                      const Interval &vBox) {
    auto evaluate = [&](double uu, double vv, Vec3 *d) { surface.evaluate(uu, vv, 2, d); };
    auto at = [](const Vec3 *d, int k, int l) -> const Vec3 & { return d[Surface::derivativeIndex(k, l, 2)]; };
    Vec3 d[9];
    evaluate(u, v, d);
    double current = squaredNorm(at(d, 0, 0) - p);
    for (int iteration = 0; iteration < 100; ++iteration) {
        const Vec3 diff = at(d, 0, 0) - p;
        const Vec3 &su = at(d, 1, 0), &sv = at(d, 0, 1);
        const double fu = dot(su, diff), fv = dot(sv, diff);
        const double guu = dot(su, su), guv = dot(su, sv), gvv = dot(sv, sv);
        double a = dot(at(d, 2, 0), diff) + guu;
        double b = dot(at(d, 1, 1), diff) + guv;
        double c = dot(at(d, 0, 2), diff) + gvv;
        double det = a * c - b * b;
        if (!(a > 0.0 && det > 1.0e-14 * a * c)) {
            a = guu, b = guv, c = gvv;
            det = a * c - b * b;
        }
        double stepU, stepV;
        if (det > 1.0e-14 * a * c && det > 0.0) {
            stepU = -(c * fu - b * fv) / det;
            stepV = -(a * fv - b * fu) / det;
        } else if (guu + gvv > 0.0) {  // superficie degenere (polo): discesa del gradiente
            stepU = -fu / (guu + gvv);
            stepV = -fv / (guu + gvv);
        } else {
            break;
        }
        // Active set: una coordinata ferma sul bordo che il passo spingerebbe
        // fuori resta bloccata, e si minimizza solo lungo l'altra.
        const bool lockU = (u <= uBox.lo && stepU < 0.0) || (u >= uBox.hi && stepU > 0.0);
        const bool lockV = (v <= vBox.lo && stepV < 0.0) || (v >= vBox.hi && stepV > 0.0);
        if (lockU && lockV) break;
        if (lockU) {
            stepU = 0.0;
            stepV = c > 0.0 ? -fv / c : 0.0;
        } else if (lockV) {
            stepV = 0.0;
            stepU = a > 0.0 ? -fu / a : 0.0;
        }
        if (stepU == 0.0 && stepV == 0.0) break;

        const double noise = 64.0 * kMachineEpsilon * (current + squaredNorm(at(d, 0, 0)) + squaredNorm(p));
        double nextU = uBox.clamp(u + stepU), nextV = vBox.clamp(v + stepV);
        Vec3 nd[9];
        evaluate(nextU, nextV, nd);
        double candidate = squaredNorm(at(nd, 0, 0) - p);
        for (int halving = 0; halving < 40 && candidate > current + noise; ++halving) {
            stepU *= 0.5;
            stepV *= 0.5;
            nextU = uBox.clamp(u + stepU);
            nextV = vBox.clamp(v + stepV);
            evaluate(nextU, nextV, nd);
            candidate = squaredNorm(at(nd, 0, 0) - p);
        }
        if (candidate > current + noise) break;
        const double changeU = std::fabs(nextU - u), changeV = std::fabs(nextV - v);
        u = nextU;
        v = nextV;
        current = candidate;
        std::copy(nd, nd + 9, d);
        if (changeU <= 4.0 * kMachineEpsilon * std::max(1.0, std::fabs(u))
            && changeV <= 4.0 * kMachineEpsilon * std::max(1.0, std::fabs(v)))
            break;
    }
}

void projectGeneric(const Surface &surface, const Vec3 &p, const Interval &uRange, const Interval &vRange,
                    Candidates &candidates) {
    if (!uRange.isFinite() || !vRange.isFinite())
        throw std::invalid_argument("projectPoint: questa superficie richiede intervalli finiti");
    const std::vector<double> uBreaks = surface.uBreakpoints(uRange);
    const std::vector<double> vBreaks = surface.vBreakpoints(vRange);
    constexpr int n = kGridPerPatch;
    double squared[n + 1][n + 1];
    for (std::size_t iu = 0; iu + 1 < uBreaks.size(); ++iu) {
        const Interval uBox{uBreaks[iu], uBreaks[iu + 1]};
        if (!(uBox.hi > uBox.lo)) continue;
        for (std::size_t iv = 0; iv + 1 < vBreaks.size(); ++iv) {
            const Interval vBox{vBreaks[iv], vBreaks[iv + 1]};
            if (!(vBox.hi > vBox.lo)) continue;
            auto uAt = [&](int i) { return i == n ? uBox.hi : uBox.lo + uBox.length() * i / n; };
            auto vAt = [&](int j) { return j == n ? vBox.hi : vBox.lo + vBox.length() * j / n; };
            for (int i = 0; i <= n; ++i)
                for (int j = 0; j <= n; ++j) squared[i][j] = squaredNorm(surface.point(uAt(i), vAt(j)) - p);
            // Minimi locali della griglia (confronto con gli 8 vicini nella pezza).
            for (int i = 0; i <= n; ++i) {
                for (int j = 0; j <= n; ++j) {
                    bool minimum = true;
                    for (int di = -1; di <= 1 && minimum; ++di)
                        for (int dj = -1; dj <= 1 && minimum; ++dj) {
                            const int ni = i + di, nj = j + dj;
                            if ((di || dj) && ni >= 0 && ni <= n && nj >= 0 && nj <= n && squared[ni][nj] < squared[i][j])
                                minimum = false;
                        }
                    if (!minimum) continue;
                    double u = uAt(i), v = vAt(j);
                    refineProjection(surface, p, u, v, uBox, vBox);
                    candidates.consider(u, v);
                }
            }
        }
    }
    projectOnBoundary(surface, p, uRange, vRange, candidates, true);
}

// --- B-spline: branch and bound sulle pezze di Bezier -----------------------
//
// I poli (euclidei) di una pezza con pesi positivi ne racchiudono la
// superficie, quindi la distanza di p dal loro box e' un limite inferiore
// certo. Le pezze che non possono battere il miglior punto trovato si
// scartano, le altre si dividono a meta' (de Casteljau in coordinate
// omogenee) fino a una dimensione in cui Newton converge al minimo locale.

struct BezierPatch {
    int uDegree = 0, vDegree = 0;
    Interval u, v;
    std::vector<Vec3> homogeneous;  // w P, indice i * (vDegree + 1) + j
    std::vector<double> weights;

    Vec3 pole(int i, int j) const { return homogeneous[i * (vDegree + 1) + j] / weights[i * (vDegree + 1) + j]; }
};

BezierPatch toPatch(const BSplineSurface &surface) {
    BezierPatch patch{surface.uDegree(), surface.vDegree(), surface.uDomain(), surface.vDomain(), {}, {}};
    for (int i = 0; i <= patch.uDegree; ++i)
        for (int j = 0; j <= patch.vDegree; ++j) {
            patch.weights.push_back(surface.weight(i, j));
            patch.homogeneous.push_back(surface.weight(i, j) * surface.pole(i, j));
        }
    return patch;
}

// Divide a meta' lungo u (alongU) o lungo v.
void splitPatch(const BezierPatch &patch, bool alongU, BezierPatch &first, BezierPatch &second) {
    first = second = patch;
    const int count = alongU ? patch.uDegree + 1 : patch.vDegree + 1;
    const int lines = alongU ? patch.vDegree + 1 : patch.uDegree + 1;
    auto index = [&](int along, int line) { return alongU ? along * (patch.vDegree + 1) + line : line * (patch.vDegree + 1) + along; };
    std::vector<Vec3> h(count);
    std::vector<double> w(count);
    for (int line = 0; line < lines; ++line) {
        for (int k = 0; k < count; ++k) {
            h[k] = patch.homogeneous[index(k, line)];
            w[k] = patch.weights[index(k, line)];
        }
        first.homogeneous[index(0, line)] = h[0];
        first.weights[index(0, line)] = w[0];
        second.homogeneous[index(count - 1, line)] = h[count - 1];
        second.weights[index(count - 1, line)] = w[count - 1];
        for (int level = 1; level < count; ++level) {
            for (int k = 0; k < count - level; ++k) {
                h[k] = 0.5 * (h[k] + h[k + 1]);
                w[k] = 0.5 * (w[k] + w[k + 1]);
            }
            first.homogeneous[index(level, line)] = h[0];
            first.weights[index(level, line)] = w[0];
            second.homogeneous[index(count - 1 - level, line)] = h[count - 1 - level];
            second.weights[index(count - 1 - level, line)] = w[count - 1 - level];
        }
    }
    Interval &a = alongU ? first.u : first.v;
    Interval &b = alongU ? second.u : second.v;
    const double middle = 0.5 * (a.lo + a.hi);
    a.hi = middle;
    b.lo = middle;
}

struct PatchBounds {
    double lowerBound;  // limite inferiore della distanza di p dalla pezza
    double diagonal;
    bool splitAlongU;   // la rete di controllo e' piu' lunga in u
};

// Due limiti inferiori della distanza dall'inviluppo convesso dei poli: dal
// loro box e, per una direzione unitaria n qualsiasi, min_i (p - q_i).n.
// Con n dal centro della pezza verso p il secondo e' esatto al secondo
// ordine: per un punto lontano da una lamiera quasi piana il box da solo
// lascerebbe in gioco O(d / s) pezze di lato s a ogni livello.
PatchBounds patchBounds(const BezierPatch &patch, const Vec3 &p, const Vec3 *center = nullptr) {
    Vec3 lo = patch.pole(0, 0), hi = lo;
    double uLength = 0.0, vLength = 0.0;
    Vec3 direction;
    const bool directional = center && norm(p - *center) > 0.0;
    if (directional) direction = normalized(p - *center);
    double directionalBound = std::numeric_limits<double>::infinity();
    for (int i = 0; i <= patch.uDegree; ++i)
        for (int j = 0; j <= patch.vDegree; ++j) {
            const Vec3 q = patch.pole(i, j);
            if (directional) directionalBound = std::min(directionalBound, dot(p - q, direction));
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], q[c]);
                hi[c] = std::max(hi[c], q[c]);
            }
            if (i > 0) uLength = std::max(uLength, distance(q, patch.pole(i - 1, j)));
            if (j > 0) vLength = std::max(vLength, distance(q, patch.pole(i, j - 1)));
        }
    Vec3 outside;
    for (int c = 0; c < 3; ++c) outside[c] = std::max({lo[c] - p[c], 0.0, p[c] - hi[c]});
    const double lowerBound = directional ? std::max(norm(outside), directionalBound) : norm(outside);
    return {lowerBound, distance(lo, hi), uLength * patch.uDegree >= vLength * patch.vDegree};
}

void projectOnBSplineSurface(const BSplineSurface &surface, const Vec3 &p, const Interval &uRange,
                             const Interval &vRange, Candidates &candidates) {
    if (!uRange.isFinite() || !vRange.isFinite())
        throw std::invalid_argument("projectPoint: questa superficie richiede intervalli finiti");
    projectOnBoundary(surface, p, uRange, vRange, candidates, true);  // primo limite superiore

    // Best-first: si esplora prima la pezza con il limite inferiore piu' basso,
    // cosi' il minimo si trova presto e il resto viene scartato subito.
    struct Node {
        double lowerBound;
        BezierPatch patch;
        int depth;
        bool operator<(const Node &other) const { return lowerBound > other.lowerBound; }
    };
    std::priority_queue<Node> queue;
    double rootDiagonal = 0.0;
    for (const BSplineSurface &piece : surface.bezierPatches()) {
        BezierPatch patch = toPatch(piece);
        if (patch.u.hi < uRange.lo || patch.u.lo > uRange.hi || patch.v.hi < vRange.lo || patch.v.lo > vRange.hi) continue;
        const PatchBounds bounds = patchBounds(patch, p);
        rootDiagonal = std::max(rootDiagonal, bounds.diagonal);
        queue.push({bounds.lowerBound, std::move(patch), 0});
    }
    const double leafDiagonal = std::max(1.0e-4 * rootDiagonal, kLinearResolution);
    while (!queue.empty()) {
        const Node node = queue.top();
        queue.pop();
        const double best = candidates.bestDistance();
        const double margin = 1.0e-12 * std::max(1.0, best);
        if (node.lowerBound >= best - margin) break;  // tutte le altre hanno un limite ancora piu' alto
        const Interval uBox{std::max(node.patch.u.lo, uRange.lo), std::min(node.patch.u.hi, uRange.hi)};
        const Interval vBox{std::max(node.patch.v.lo, vRange.lo), std::min(node.patch.v.hi, vRange.hi)};
        if (!(uBox.lo <= uBox.hi && vBox.lo <= vBox.hi)) continue;
        double u = 0.5 * (node.patch.u.lo + node.patch.u.hi), v = 0.5 * (node.patch.v.lo + node.patch.v.hi);
        const Vec3 center = surface.point(u, v);
        const PatchBounds bounds = patchBounds(node.patch, p, &center);
        if (bounds.lowerBound >= candidates.bestDistance() - margin) continue;
        u = uBox.clamp(u);
        v = vBox.clamp(v);
        if (bounds.diagonal <= leafDiagonal || node.depth >= 60) {
            refineProjection(surface, p, u, v, uBox, vBox);
            candidates.consider(u, v);
            continue;
        }
        candidates.consider(u, v);  // migliora il limite superiore
        BezierPatch first, second;
        splitPatch(node.patch, bounds.splitAlongU, first, second);
        const double firstBound = patchBounds(first, p).lowerBound, secondBound = patchBounds(second, p).lowerBound;
        queue.push({std::max(firstBound, bounds.lowerBound), std::move(first), node.depth + 1});
        queue.push({std::max(secondBound, bounds.lowerBound), std::move(second), node.depth + 1});
    }
}

// --- Estrusione: riduzione esatta a una curva --------------------------------
//
// |C(u) + v D - p|^2 = |P(C(u) - p)|^2 + ((C(u) - p).D + v)^2 con P proiezione
// sul piano normale a D. Per u fisso il v migliore e' (p - C(u)).D; i punti
// stazionari interni sono quindi i minimi della curva proiettata P C rispetto
// a P p. Per una base B-spline la curva proiettata e' ancora una B-spline
// (stessi nodi e pesi, poli proiettati) e i suoi minimi si trovano tutti.
void projectOnExtrusion(const ExtrusionSurface &extrusion, const Vec3 &p, const Interval &uRange,
                        const Interval &vRange, Candidates &candidates) {
    const Transform3 projection = Transform3::projectionAlong(extrusion.direction());
    CurvePtr<3> projected;
    if (extrusion.curve()->type() == CurveType::BSpline) {
        const auto &curve = static_cast<const BSplineCurve<3> &>(*extrusion.curve());
        std::vector<Vec3> poles;
        for (const Vec3 &pole : curve.poles()) poles.push_back(projection.applyToVector(pole));
        projected = std::make_shared<BSplineCurve<3>>(curve.degree(), curve.knots(), std::move(poles), curve.weights());
    } else {
        projected = std::make_shared<TransformedCurve>(extrusion.curve(), projection);
    }
    for (const CurveProjection<3> &candidate : projectPointCandidates(*projected, projection.applyToVector(p), uRange)) {
        const double u = candidate.parameter;
        candidates.considerInterior(u, dot(p - extrusion.curve()->point(u), extrusion.direction()));
    }
    projectOnBoundary(extrusion, p, uRange, vRange, candidates);
}

// --- Rivoluzione con meridiano piano: riduzione esatta al meridiano ----------
//
// Se il meridiano giace in un piano che contiene l'asse, per v fisso il punto
// piu' vicino del parallelo e' nel semipiano di p (u = angolo di p) o in
// quello opposto (u + pi). La distanza diventa quella dal meridiano di p
// ruotato nel piano del meridiano (p1) o del suo simmetrico rispetto
// all'asse (p2): minimi di curve, come per il toro.

// Punti che determinano il piano della curva (poli, estremi, assi del cerchio).
bool definingPoints(const Curve<3> &curve, std::vector<Vec3> &points) {
    switch (curve.type()) {
    case CurveType::BSpline:
        for (const Vec3 &pole : static_cast<const BSplineCurve<3> &>(curve).poles()) points.push_back(pole);
        return true;
    case CurveType::Line: {
        const auto &line = static_cast<const Line<3> &>(curve);
        points.push_back(line.origin());
        points.push_back(line.origin() + line.direction());
        return true;
    }
    case CurveType::Circle: {
        const auto &circle = static_cast<const Circle<3> &>(curve);
        for (const Vec3 &axis : {circle.xAxis(), circle.yAxis(), -circle.xAxis()})
            points.push_back(circle.center() + circle.radius() * axis);
        return true;
    }
    case CurveType::Ellipse: {
        const auto &ellipse = static_cast<const Ellipse<3> &>(curve);
        points.push_back(ellipse.center() + ellipse.xRadius() * ellipse.xAxis());
        points.push_back(ellipse.center() + ellipse.yRadius() * ellipse.yAxis());
        points.push_back(ellipse.center() - ellipse.xRadius() * ellipse.xAxis());
        return true;
    }
    case CurveType::Trimmed:
        return definingPoints(*static_cast<const TrimmedCurve<3> &>(curve).basis(), points);
    default:
        return false;
    }
}

// Direzione radiale del piano del meridiano, se il meridiano e' piano e
// contiene l'asse nel suo piano.
bool meridianPlane(const RevolutionSurface &revolution, Vec3 &radial) {
    std::vector<Vec3> points;
    if (!definingPoints(*revolution.meridian(), points)) return false;
    const Vec3 &a = revolution.axisDirection();
    double largest = 0.0;
    for (const Vec3 &point : points) {
        const Vec3 w = point - revolution.axisPoint();
        const Vec3 perpendicular = w - dot(w, a) * a;
        if (norm(perpendicular) > largest) {
            largest = norm(perpendicular);
            radial = perpendicular;
        }
    }
    if (largest <= kLinearResolution) return false;
    radial = radial / largest;
    const Vec3 side = cross(a, radial);
    for (const Vec3 &point : points)
        if (std::fabs(dot(point - revolution.axisPoint(), side)) > kLinearResolution) return false;
    return true;
}

bool projectOnPlanarRevolution(const RevolutionSurface &revolution, const Vec3 &p, const Interval &uRange,
                               const Interval &vRange, Candidates &candidates) {
    Vec3 radial;
    if (!vRange.isFinite() || !meridianPlane(revolution, radial)) return false;
    const Vec3 &a = revolution.axisDirection();
    const Vec3 side = cross(a, radial);
    const Vec3 q = p - revolution.axisPoint();
    const double height = dot(q, a);
    const Vec3 perpendicular = q - height * a;
    const double rho = norm(perpendicular);
    const double angle = rho > 0.0 ? std::atan2(dot(perpendicular, side), dot(perpendicular, radial)) : uRange.lo;
    const Vec3 onAxis = revolution.axisPoint() + height * a;
    for (const CurveProjection<3> &c : projectPointCandidates(*revolution.meridian(), onAxis + rho * radial, vRange))
        candidates.considerInterior(angle, c.parameter);
    for (const CurveProjection<3> &c : projectPointCandidates(*revolution.meridian(), onAxis - rho * radial, vRange))
        candidates.considerInterior(angle + kPi, c.parameter);
    projectOnBoundary(revolution, p, uRange, vRange, candidates);
    return true;
}

SurfaceProjection projectOnPlane(const Plane &plane, const Vec3 &p, const Interval &uRange, const Interval &vRange) {
    // Parametrizzazione isometrica: il problema si separa in u e v.
    const Vec3 local = plane.frame().toLocal(p);
    return projectionAt(plane, p, uRange.clamp(local.x()), vRange.clamp(local.y()));
}

SurfaceProjection projectOnCylinder(const CylindricalSurface &cylinder, const Vec3 &p, const Interval &uRange,
                                    const Interval &vRange) {
    // |S - p|^2 = rho^2 + R^2 - 2 rho R cos(u - u0) + (z - v)^2: separabile.
    const Vec3 local = cylinder.frame().toLocal(p);
    const double v = vRange.clamp(local.z());
    if (!(local.x() * local.x() + local.y() * local.y() > 0.0)) return projectionAt(cylinder, p, uRange.lo, v);
    double shifted = std::atan2(local.y(), local.x()) - uRange.lo;
    shifted -= kTwoPi * std::floor(shifted / kTwoPi);
    if (uRange.lo + shifted <= uRange.hi) return projectionAt(cylinder, p, uRange.lo + shifted, v);
    const SurfaceProjection first = projectionAt(cylinder, p, uRange.lo, v);
    const SurfaceProjection last = projectionAt(cylinder, p, uRange.hi, v);
    return first.distance <= last.distance ? first : last;
}

// Nel semipiano meridiano di p il cono e' una coppia di rette (u0 e u0 + pi):
// la proiezione su ciascuna da' un punto stazionario.
void coneCandidates(const ConicalSurface &cone, const Vec3 &p, Candidates &candidates, const Interval &uRange) {
    const Vec3 local = cone.frame().toLocal(p);
    const double rho = std::hypot(local.x(), local.y());
    const double u0 = rho > 0.0 ? std::atan2(local.y(), local.x()) : uRange.lo;
    const double sa = std::sin(cone.semiAngle()), ca = std::cos(cone.semiAngle());
    const double r = cone.referenceRadius();
    candidates.considerInterior(u0, (rho - r) * sa + local.z() * ca);
    candidates.considerInterior(u0 + kPi, -(rho + r) * sa + local.z() * ca);
    candidates.considerInterior(uRange.lo, -r / sa);  // vertice
}

void sphereCandidates(const SphericalSurface &sphere, const Vec3 &p, Candidates &candidates, const Interval &uRange,
                      const Interval &vRange) {
    const Vec3 local = sphere.frame().toLocal(p);
    const double rho = std::hypot(local.x(), local.y());
    if (!(rho > 0.0 || local.z() != 0.0)) {  // centro: tutti equidistanti
        candidates.consider(uRange.lo, vRange.lo);
        return;
    }
    candidates.considerInterior(rho > 0.0 ? std::atan2(local.y(), local.x()) : uRange.lo, std::atan2(local.z(), rho));
}

void torusCandidates(const ToroidalSurface &torus, const Vec3 &p, Candidates &candidates, const Interval &uRange,
                     const Interval &vRange) {
    const Vec3 local = torus.frame().toLocal(p);
    const double rho = std::hypot(local.x(), local.y());
    const double u0 = rho > 0.0 ? std::atan2(local.y(), local.x()) : uRange.lo;
    const double dr = rho - torus.majorRadius();
    const double v0 = (dr != 0.0 || local.z() != 0.0) ? std::atan2(local.z(), dr) : vRange.lo;
    candidates.considerInterior(u0, v0);
}

}

SurfaceProjection projectPoint(const Surface &surface, const Vec3 &p, const Interval &uRange, const Interval &vRange) {
    if (!(uRange.lo <= uRange.hi) || !(vRange.lo <= vRange.hi))
        throw std::invalid_argument("projectPoint: intervallo vuoto");
    Candidates candidates(surface, p, uRange, vRange);
    switch (surface.type()) {
    case SurfaceType::Plane:
        return projectOnPlane(static_cast<const Plane &>(surface), p, uRange, vRange);
    case SurfaceType::Cylinder:
        return projectOnCylinder(static_cast<const CylindricalSurface &>(surface), p, uRange, vRange);
    case SurfaceType::Cone:
        coneCandidates(static_cast<const ConicalSurface &>(surface), p, candidates, uRange);
        projectOnBoundary(surface, p, uRange, vRange, candidates);
        break;
    case SurfaceType::Sphere:
        sphereCandidates(static_cast<const SphericalSurface &>(surface), p, candidates, uRange, vRange);
        projectOnBoundary(surface, p, uRange, vRange, candidates);
        break;
    case SurfaceType::Torus:
        torusCandidates(static_cast<const ToroidalSurface &>(surface), p, candidates, uRange, vRange);
        projectOnBoundary(surface, p, uRange, vRange, candidates);
        break;
    case SurfaceType::BSpline:
        projectOnBSplineSurface(static_cast<const BSplineSurface &>(surface), p, uRange, vRange, candidates);
        break;
    case SurfaceType::Extrusion:
        projectOnExtrusion(static_cast<const ExtrusionSurface &>(surface), p, uRange, vRange, candidates);
        break;
    case SurfaceType::Revolution:
        if (!projectOnPlanarRevolution(static_cast<const RevolutionSurface &>(surface), p, uRange, vRange, candidates))
            projectGeneric(surface, p, uRange, vRange, candidates);
        break;
    default:
        projectGeneric(surface, p, uRange, vRange, candidates);
        break;
    }
    return candidates.best();
}

SurfaceProjection projectPoint(const Surface &surface, const Vec3 &p) {
    return projectPoint(surface, p, surface.uDomain(), surface.vDomain());
}

namespace {

void unwrapNear(const Surface &surface, Vec2 &uv, const Vec2 &guess) {
    if (surface.isUPeriodic()) uv[0] += surface.uPeriod() * std::round((guess[0] - uv[0]) / surface.uPeriod());
    if (surface.isVPeriodic()) uv[1] += surface.vPeriod() * std::round((guess[1] - uv[1]) / surface.vPeriod());
}

}

bool invertPoint(const Surface &surface, const Vec3 &p, Vec2 &uv, double tolerance, double scale) {
    const Vec2 guess = uv;
    const Interval uDomain = surface.uDomain(), vDomain = surface.vDomain();
    bool converged = false;
    for (int iteration = 0; iteration < 30; ++iteration) {
        Vec3 d[4];
        surface.evaluate(uv[0], uv[1], 1, d);
        const Vec3 &su = d[Surface::derivativeIndex(1, 0, 1)], &sv = d[Surface::derivativeIndex(0, 1, 1)];
        const Vec3 r = p - d[0];
        const double a = dot(su, su), b = dot(su, sv), c = dot(sv, sv), det = a * c - b * b;
        if (!(det > 1e-24 * a * c) || !(a > 0.0)) break;
        const double ru = dot(su, r), rv = dot(sv, r);
        const double du = (c * ru - b * rv) / det, dv = (a * rv - b * ru) / det;
        if (!std::isfinite(du) || !std::isfinite(dv)) break;
        uv[0] += du;
        uv[1] += dv;
        if (!surface.isUPeriodic() && uDomain.isFinite()) uv[0] = uDomain.clamp(uv[0]);
        if (!surface.isVPeriodic() && vDomain.isFinite()) uv[1] = vDomain.clamp(uv[1]);
        if (norm(du * su + dv * sv) <= 1e-15 * (scale + norm(p))) {
            converged = true;
            break;
        }
    }
    if (!converged || !(distance(p, surface.point(uv[0], uv[1])) <= tolerance)) {
        const SurfaceProjection projection = projectPoint(surface, p);
        uv = Vec2(projection.u, projection.v);
        if (!(projection.distance <= tolerance)) {
            unwrapNear(surface, uv, guess);
            return false;
        }
    }
    unwrapNear(surface, uv, guess);
    return true;
}


namespace {

bool parallelAxes(const Vec3 &a, const Vec3 &b) { return norm(cross(a, b)) <= 1e-12; }

double distanceToAxis(const Vec3 &p, const Frame3 &frame) {
    const Vec3 d = p - frame.origin();
    return norm(d - dot(d, frame.zDir()) * frame.zDir());
}

// Punti della curva base di `from` (sui tratti lisci) tutti sulla superficie `onto`.
bool baseCurveOn(const ExtrusionSurface &from, const Surface &onto, double tolerance) {
    const Curve<3> &curve = *from.curve();
    const Interval domain = curve.domain();
    std::vector<double> breaks = curve.breakpoints(domain);
    if (breaks.size() < 2) breaks = {domain.lo, domain.hi};
    const int perSpan = std::max(2, 64 / int(breaks.size()));
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i)
        for (int j = 0; j <= perSpan; ++j)
            if (projectPoint(onto, curve.point(breaks[i] + (breaks[i + 1] - breaks[i]) * j / perSpan)).distance > tolerance) return false;
    return true;
}

}

bool sameSurface(const Surface &a, const Surface &b, double tolerance) {
    if (&a == &b) return true;
    if (a.type() != b.type()) return false;
    switch (a.type()) {
    case SurfaceType::Plane: {
        const Frame3 &fa = static_cast<const Plane &>(a).frame(), &fb = static_cast<const Plane &>(b).frame();
        return parallelAxes(fa.zDir(), fb.zDir()) && std::fabs(dot(fa.zDir(), fb.origin() - fa.origin())) <= tolerance;
    }
    case SurfaceType::Cylinder: {
        const auto &ca = static_cast<const CylindricalSurface &>(a), &cb = static_cast<const CylindricalSurface &>(b);
        return parallelAxes(ca.frame().zDir(), cb.frame().zDir()) && std::fabs(ca.radius() - cb.radius()) <= tolerance
            && distanceToAxis(cb.frame().origin(), ca.frame()) <= tolerance;
    }
    case SurfaceType::Sphere: {
        const auto &sa = static_cast<const SphericalSurface &>(a), &sb = static_cast<const SphericalSurface &>(b);
        return distance(sa.frame().origin(), sb.frame().origin()) <= tolerance && std::fabs(sa.radius() - sb.radius()) <= tolerance;
    }
    case SurfaceType::Cone: {
        // Il cono prosegue oltre il vertice: stesso vertice, asse parallelo o
        // opposto e stesso semiangolo in valore assoluto.
        const auto &ca = static_cast<const ConicalSurface &>(a), &cb = static_cast<const ConicalSurface &>(b);
        return parallelAxes(ca.frame().zDir(), cb.frame().zDir()) && distance(ca.apex(), cb.apex()) <= tolerance
            && std::fabs(std::fabs(ca.semiAngle()) - std::fabs(cb.semiAngle())) <= 1e-12;
    }
    case SurfaceType::Torus: {
        const auto &ta = static_cast<const ToroidalSurface &>(a), &tb = static_cast<const ToroidalSurface &>(b);
        return parallelAxes(ta.frame().zDir(), tb.frame().zDir()) && distance(ta.frame().origin(), tb.frame().origin()) <= tolerance
            && std::fabs(ta.majorRadius() - tb.majorRadius()) <= tolerance && std::fabs(ta.minorRadius() - tb.minorRadius()) <= tolerance;
    }
    case SurfaceType::Extrusion: {
        const auto &ea = static_cast<const ExtrusionSurface &>(a), &eb = static_cast<const ExtrusionSurface &>(b);
        return parallelAxes(ea.direction(), eb.direction()) && baseCurveOn(ea, eb, tolerance) && baseCurveOn(eb, ea, tolerance);
    }
    default:
        return false;
    }
}

}

namespace ForgeCad::Kernel {

std::vector<SurfacePole> surfacePoles(const Surface &surface) {
    std::vector<SurfacePole> poles;
    switch (surface.type()) {
    case SurfaceType::Sphere: {
        const auto &sphere = static_cast<const SphericalSurface &>(surface);
        const Frame3 &f = sphere.frame();
        poles.push_back({f.origin() - sphere.radius() * f.zDir(), -kHalfPi});
        poles.push_back({f.origin() + sphere.radius() * f.zDir(), kHalfPi});
        break;
    }
    case SurfaceType::Cone: {
        const auto &cone = static_cast<const ConicalSurface &>(surface);
        poles.push_back({cone.apex(), -cone.referenceRadius() / std::sin(cone.semiAngle())});
        break;
    }
    case SurfaceType::Revolution: {
        const auto &revolution = static_cast<const RevolutionSurface &>(surface);
        const Interval v = revolution.vDomain();
        if (!v.isFinite() || revolution.isVPeriodic()) break;
        for (double t : {v.lo, v.hi}) {
            const Vec3 p = revolution.meridian()->point(t), a = revolution.axisPoint(), d = revolution.axisDirection();
            const Vec3 offset = p - a;
            if (norm(offset - dot(offset, d) * d) <= 1e-9 * std::max(1.0, norm(offset))) poles.push_back({a + dot(offset, d) * d, t});
        }
        break;
    }
    default:
        break;
    }
    return poles;
}

int poleIndex(const std::vector<SurfacePole> &poles, const Vec3 &p, double tolerance) {
    for (std::size_t i = 0; i < poles.size(); ++i)
        if (distance(poles[i].point, p) <= tolerance) return int(i);
    return -1;
}

double poleWalk(double from, double to, double period, bool top, bool sense) {
    // Col dominio a sinistra: sopra si cammina verso u minori, sotto verso u maggiori.
    const bool decreasing = top == sense;
    double delta = std::fmod(decreasing ? from - to : to - from, period);
    if (delta < 0.0) delta += period;
    if (delta > period * (1.0 - 1e-12)) delta = 0.0;
    return decreasing ? from - delta : from + delta;
}

double poleWalk(double from, double to, double period, bool top, bool sense, double fromNear, double toNear) {
    const double reference = poleWalk(fromNear, toNear, period, top, sense) - fromNear + from;
    // Il valore congruo a `to` piu' vicino al riferimento.
    return to + period * std::round((reference - to) / period);
}

Vec3 normalAt(const Surface &surface, double u, double v, int side) {
    try {
        return surface.normal(u, v);
    } catch (const std::domain_error &) {
    }
    const Interval domain = surface.vDomain();
    if (side == 0) side = domain.isFinite() && v > 0.5 * (domain.lo + domain.hi) ? -1 : 1;
    const double step = 1e-6 * (domain.isFinite() ? std::min(1.0, domain.length()) : 1.0) * side;
    Vec3 sum;
    for (int k = 0; k < 8; ++k) sum += surface.normal(u + k * kTwoPi / 8.0, v + step);
    return normalized(sum);
}

}
