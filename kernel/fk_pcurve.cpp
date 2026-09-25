#include "fk_pcurve.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "fk_bspline.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

double modelScale(const Curve<3> &curve, const Interval &range) {
    return std::max({1.0, norm(curve.point(range.lo)), norm(curve.point(range.hi))});
}

// Curva nel sistema del piano: S(u, v) = O + u X + v Y, quindi (u, v) sono
// le coordinate locali x, y. Mappa affine: ogni tipo resta del suo tipo.
CurvePtr<2> planarCurve(const Frame3 &frame, const CurvePtr<3> &curve) {
    auto point = [&](const Vec3 &p) {
        const Vec3 local = frame.toLocal(p);
        return Vec2(local.x(), local.y());
    };
    auto direction = [&](const Vec3 &d) {
        const Vec3 local = frame.directionToLocal(d);
        return Vec2(local.x(), local.y());
    };
    try {
        switch (curve->type()) {
        case CurveType::Line: {
            const auto &line = static_cast<const Line<3> &>(*curve);
            return std::make_shared<Line<2>>(point(line.origin()), direction(line.direction()));
        }
        case CurveType::Circle: {
            const auto &circle = static_cast<const Circle<3> &>(*curve);
            return std::make_shared<Circle<2>>(point(circle.center()), direction(circle.xAxis()), direction(circle.yAxis()),
                                               circle.radius());
        }
        case CurveType::Ellipse: {
            const auto &ellipse = static_cast<const Ellipse<3> &>(*curve);
            return std::make_shared<Ellipse<2>>(point(ellipse.center()), direction(ellipse.xAxis()),
                                                direction(ellipse.yAxis()), ellipse.xRadius(), ellipse.yRadius());
        }
        case CurveType::BSpline: {
            const auto &spline = static_cast<const BSplineCurve<3> &>(*curve);
            std::vector<Vec2> poles;
            for (const Vec3 &pole : spline.poles()) poles.push_back(point(pole));
            return std::make_shared<BSplineCurve<2>>(spline.degree(), spline.knots(), std::move(poles), spline.weights());
        }
        case CurveType::Trimmed: {
            const auto &trimmed = static_cast<const TrimmedCurve<3> &>(*curve);
            const CurvePtr<2> basis = planarCurve(frame, trimmed.basis());
            if (!basis) return nullptr;
            return std::make_shared<TrimmedCurve<2>>(basis, trimmed.domain().lo, trimmed.domain().hi);
        }
        default:
            return nullptr;
        }
    } catch (const std::exception &) {
        return nullptr;  // curva perpendicolare al piano (direzione nulla) e simili
    }
}

// Parametri di tratti successivi della curva, continui sulle superfici
// periodiche: si parte dal punto centrale (proiezione globale, senza
// l'ambiguita' degli estremi di una curva chiusa) e si prosegue con Newton.
bool sampleParameters(const Surface &surface, const Curve<3> &curve, const std::vector<double> &ts, double tolerance,
                      double scale, std::vector<Vec2> &uvs) {
    const std::size_t count = ts.size(), middle = count / 2;
    uvs.assign(count, Vec2());
    const SurfaceProjection start = projectPoint(surface, curve.point(ts[middle]));
    if (!(start.distance <= tolerance)) return false;
    uvs[middle] = Vec2(start.u, start.v);
    for (std::size_t i = middle + 1; i < count; ++i) {
        uvs[i] = uvs[i - 1];
        if (!invertPoint(surface, curve.point(ts[i]), uvs[i], tolerance, scale)) return false;
    }
    for (std::size_t i = middle; i-- > 0;) {
        uvs[i] = uvs[i + 1];
        if (!invertPoint(surface, curve.point(ts[i]), uvs[i], tolerance, scale)) return false;
    }
    // Estremi in un polo (sfera, vertice del cono): u vale il limite lungo la curva.
    if (count >= 3) {
        const std::vector<SurfacePole> poles = surfacePoles(surface);
        const int first = poleIndex(poles, curve.point(ts.front()), tolerance), last = poleIndex(poles, curve.point(ts.back()), tolerance);
        if (first >= 0) uvs.front() = Vec2(2.0 * uvs[1][0] - uvs[2][0], poles[first].v);
        if (last >= 0) uvs.back() = Vec2(2.0 * uvs[count - 2][0] - uvs[count - 3][0], poles[last].v);
    }
    return true;
}

std::vector<double> sampleParametersOf(const Curve<3> &curve, const Interval &range, int perSpan) {
    std::vector<double> ts;
    const std::vector<double> breaks = curve.breakpoints(range);
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i)
        for (int j = 0; j < perSpan; ++j) ts.push_back(breaks[i] + (breaks[i + 1] - breaks[i]) * j / perSpan);
    ts.push_back(breaks.back());
    return ts;
}

// Porta u(range.lo) (e v, se periodico) nel dominio di base.
Vec2 baseShift(const Surface &surface, const Vec2 &start) {
    Vec2 shift;
    if (surface.isUPeriodic()) {
        const double lo = surface.uDomain().lo, period = surface.uPeriod();
        shift[0] = -period * std::floor((start[0] - lo) / period + 1e-12);
    }
    if (surface.isVPeriodic()) {
        const double lo = surface.vDomain().lo, period = surface.vPeriod();
        shift[1] = -period * std::floor((start[1] - lo) / period + 1e-12);
    }
    return shift;
}

// Derivata dell'SP-curve da C' = Su u' + Sv v'.
bool pcurveDerivative(const Surface &surface, const Vec2 &uv, const Vec3 &tangent, Vec2 &derivative) {
    Vec3 d[4];
    surface.evaluate(uv[0], uv[1], 1, d);
    const Vec3 &su = d[Surface::derivativeIndex(1, 0, 1)], &sv = d[Surface::derivativeIndex(0, 1, 1)];
    const double a = dot(su, su), b = dot(su, sv), c = dot(sv, sv), det = a * c - b * b;
    if (!(det > 1e-20 * a * c) || !(a > 0.0)) return false;  // punto singolare (polo)
    const double ru = dot(su, tangent), rv = dot(sv, tangent);
    derivative = Vec2((c * ru - b * rv) / det, (a * rv - b * ru) / det);
    return isFinite(derivative);
}

struct HermiteNode {
    double t;
    Vec2 uv, derivative;
};

Vec2 hermite(const HermiteNode &a, const HermiteNode &b, double t) {
    const double h = b.t - a.t, s = (t - a.t) / h, s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * a.uv + (s3 - 2 * s2 + s) * h * a.derivative + (-2 * s3 + 3 * s2) * b.uv
         + (s3 - s2) * h * b.derivative;
}

class Fitter {
public:
    Fitter(const Surface &surface, const Curve<3> &curve, const Interval &range, double tolerance, double scale)
        : surface_(surface), curve_(curve), range_(range), tolerance_(tolerance), scale_(scale), poles_(surfacePoles(surface)) {}

    bool node(double t, Vec2 uv, HermiteNode &out) const {
        Vec3 c[2];
        curve_.evaluate(t, 1, c);
        const int pole = poleIndex(poles_, c[0], tolerance_);
        if (pole >= 0) {
            // Nel polo u e' quello della stima (il limite lungo la curva) e la
            // derivata e' quella di un punto appena dentro il tratto.
            const double inside = t + (t < 0.5 * (range_.lo + range_.hi) ? 1.0 : -1.0) * 1e-7 * range_.length();
            HermiteNode near;
            if (!node(inside, uv, near)) return false;
            out = {t, Vec2(uv[0], poles_[pole].v), near.derivative};
            return true;
        }
        bool nearPole = false;
        for (const SurfacePole &p : poles_) nearPole = nearPole || distance(p.point, c[0]) <= 1e-3 * scale_;
        if (nearPole) {
            // Vicino a un polo Newton si ferma sul bordo del dominio (entro la
            // tolleranza ma non sul punto): proiezione, con u vicino alla stima.
            const SurfaceProjection projection = projectPoint(surface_, c[0]);
            if (!(projection.distance <= tolerance_)) return false;
            Vec2 exact(projection.u, projection.v);
            if (surface_.isUPeriodic()) exact[0] += surface_.uPeriod() * std::round((uv[0] - exact[0]) / surface_.uPeriod());
            uv = exact;
        } else if (!invertPoint(surface_, c[0], uv, tolerance_, scale_)) {
            return false;
        }
        out = {t, uv, Vec2()};
        return pcurveDerivative(surface_, uv, c[1], out.derivative);
    }

    // Infittisce [a, b] finche' l'Hermite cubica sta entro meta' tolleranza.
    bool refine(const HermiteNode &a, const HermiteNode &b, int depth, std::vector<HermiteNode> &out) const {
        double worst = 0.0;
        for (double s : {0.1, 0.25, 0.4, 0.5, 0.6, 0.75, 0.9}) {
            const double t = a.t + (b.t - a.t) * s;
            const Vec2 uv = hermite(a, b, t);
            worst = std::max(worst, distance(surface_.point(uv[0], uv[1]), curve_.point(t)));
        }
        if (worst <= 0.5 * tolerance_) {
            out.push_back(b);
            return true;
        }
        if (depth >= 24) return false;
        const double tm = 0.5 * (a.t + b.t);
        HermiteNode m;
        if (!node(tm, hermite(a, b, tm), m)) return false;
        return refine(a, m, depth + 1, out) && refine(m, b, depth + 1, out);
    }

private:
    const Surface &surface_;
    const Curve<3> &curve_;
    Interval range_;
    double tolerance_, scale_;
    std::vector<SurfacePole> poles_;
};

}

double pcurveDeviation(const Surface &surface, const Curve<3> &curve, const Curve<2> &pcurve, const Interval &range,
                       int samples) {
    std::vector<double> breaks = curve.breakpoints(range);
    const std::vector<double> more = pcurve.breakpoints(range);
    breaks.insert(breaks.end(), more.begin(), more.end());
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
    double worst = 0.0;
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i)
        for (int j = 0; j <= samples; ++j) {
            const double t = breaks[i] + (breaks[i + 1] - breaks[i]) * j / samples;
            const Vec2 uv = pcurve.point(t);
            worst = std::max(worst, distance(surface.point(uv[0], uv[1]), curve.point(t)));
        }
    return worst;
}

CurvePtr<2> exactPCurve(const Surface &surface, const CurvePtr<3> &curve, const Interval &range, double tolerance) {
    if (!curve || !range.isFinite() || !(range.lo < range.hi)) return nullptr;
    CurvePtr<2> candidate;
    if (surface.type() == SurfaceType::Plane) {
        candidate = planarCurve(static_cast<const Plane &>(surface).frame(), curve);
    } else {
        // Ipotesi affine: p(t) = p(lo) + (t - lo) (p(hi) - p(lo)) / (hi - lo).
        const double scale = modelScale(*curve, range);
        std::vector<Vec2> uvs;
        const std::vector<double> ts = sampleParametersOf(*curve, range, 8);
        if (!sampleParameters(surface, *curve, ts, tolerance, scale, uvs)) return nullptr;
        const Vec2 shift = baseShift(surface, uvs.front());
        const Vec2 lo = uvs.front() + shift, hi = uvs.back() + shift;
        const Vec2 slope = (hi - lo) / range.length();
        // Scarto preliminare sui campioni, nello spazio dei parametri.
        const double extent = std::max(norm(hi - lo), 1e-300);
        for (std::size_t i = 0; i < ts.size(); ++i)
            if (distance(uvs[i] + shift, lo + (ts[i] - range.lo) * slope) > 1e-6 * extent) return nullptr;
        if (std::fabs(norm(slope) - 1.0) <= 1e-12)
            candidate = std::make_shared<Line<2>>(lo - range.lo * slope, slope);
        else
            candidate = std::make_shared<BSplineCurve<2>>(1, std::vector<double>{range.lo, range.lo, range.hi, range.hi},
                                                          std::vector<Vec2>{lo, hi});
    }
    if (!candidate || !(pcurveDeviation(surface, *curve, *candidate, range) <= tolerance)) return nullptr;
    return candidate;
}

CurvePtr<2> fitPCurve(const Surface &surface, const CurvePtr<3> &curve, const Interval &range, double tolerance,
                      double *deviation) {
    if (!curve || !range.isFinite() || !(range.lo < range.hi)) return nullptr;
    const double scale = modelScale(*curve, range);
    const std::vector<double> ts = sampleParametersOf(*curve, range, 4);
    std::vector<Vec2> uvs;
    if (!sampleParameters(surface, *curve, ts, tolerance, scale, uvs)) return nullptr;
    const Vec2 shift = baseShift(surface, uvs.front());

    const Fitter fitter(surface, *curve, range, tolerance, scale);
    std::vector<HermiteNode> nodes(1);
    if (!fitter.node(ts[0], uvs[0], nodes[0])) return nullptr;
    for (std::size_t i = 1; i < ts.size(); ++i) {
        HermiteNode next;
        if (!fitter.node(ts[i], uvs[i], next)) return nullptr;
        const HermiteNode previous = nodes.back();
        if (!fitter.refine(previous, next, 0, nodes)) return nullptr;
    }

    // Tratti di Bezier cubici; nodi interni di molteplicita' 3 (C1 perche'
    // le derivate nei nodi sono le stesse a sinistra e a destra).
    std::vector<double> knots(4, nodes.front().t);
    std::vector<Vec2> poles{nodes.front().uv + shift};
    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
        const HermiteNode &a = nodes[i], &b = nodes[i + 1];
        const double h = b.t - a.t;
        poles.push_back(a.uv + shift + (h / 3.0) * a.derivative);
        poles.push_back(b.uv + shift - (h / 3.0) * b.derivative);
        poles.push_back(b.uv + shift);
        knots.insert(knots.end(), i + 2 == nodes.size() ? 4 : 3, b.t);
    }
    auto fitted = std::make_shared<BSplineCurve<2>>(3, std::move(knots), std::move(poles));
    const double measured = pcurveDeviation(surface, *curve, *fitted, range);
    if (!(measured <= tolerance)) return nullptr;
    if (deviation) *deviation = measured;
    return fitted;
}

int computePCurves(Body &body, double tolerance) {
    int missing = 0;
    for (FaceId f : body.faces()) {
        const SurfacePtr surface = body.face(f).surface;
        if (!surface) continue;
        for (LoopId l : body.face(f).loops)
            for (FinId finId : body.loopFins(l)) {
                Fin &fin = body.fin(finId);
                if (fin.pcurve) continue;
                const Edge &edge = body.edge(fin.edge);
                const double edgeTolerance = std::max(kLinearResolution, edge.tolerance);
                if (CurvePtr<2> exact = exactPCurve(*surface, edge.curve, edge.range, edgeTolerance)) {
                    fin.pcurve = std::move(exact);
                    fin.pcurveTolerance = 0.0;
                    continue;
                }
                double deviation = 0.0;
                if (CurvePtr<2> fitted = fitPCurve(*surface, edge.curve, edge.range, std::max(tolerance, edgeTolerance), &deviation)) {
                    fin.pcurve = std::move(fitted);
                    fin.pcurveTolerance = std::max(deviation, std::numeric_limits<double>::min());
                    continue;
                }
                ++missing;
            }
    }
    return missing;
}

std::vector<LoopPoleWalk> loopPoleWalks(const Body &body, LoopId loop, double tolerance) {
    const Face &face = body.face(body.loop(loop).face);
    const Surface &surface = *face.surface;
    if (!surface.isUPeriodic()) return {};
    const std::vector<SurfacePole> poles = surfacePoles(surface);
    if (poles.empty()) return {};
    const std::vector<FinId> fins = body.loopFins(loop);
    std::vector<LoopPoleWalk> walks;
    for (std::size_t i = 0; i < fins.size(); ++i) {
        const Fin &a = body.fin(fins[i]), &b = body.fin(fins[(i + 1) % fins.size()]);
        if (!a.pcurve || !b.pcurve) continue;
        const Edge &ea = body.edge(a.edge), &eb = body.edge(b.edge);
        const double aEnd = a.sense ? ea.range.hi : ea.range.lo, aStart = a.sense ? ea.range.lo : ea.range.hi;
        const double bStart = b.sense ? eb.range.lo : eb.range.hi;
        const int pole = poleIndex(poles, ea.curve->point(aEnd), std::max(tolerance, 10.0 * ea.tolerance));
        if (pole < 0) continue;
        const double from = a.pcurve->point(aEnd)[0];
        const bool top = a.pcurve->point(aEnd + (aStart - aEnd) * 1e-3)[1] < poles[pole].v;
        const double bEnd = b.sense ? eb.range.hi : eb.range.lo;
        const double fromNear = a.pcurve->point(aEnd + (aStart - aEnd) * 0.02)[0], toNear = b.pcurve->point(bStart + (bEnd - bStart) * 0.02)[0];
        const double to = poleWalk(from, b.pcurve->point(bStart)[0], surface.uPeriod(), top, face.sense, fromNear, toNear);
        walks.push_back({poles[pole].v, from, to, (i + 1) % fins.size()});
    }
    return walks;
}

}
