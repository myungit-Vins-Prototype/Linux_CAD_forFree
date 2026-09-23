#include "fk_intersect.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

#include "fk_bernstein.h"
#include "fk_bspline.h"
#include "fk_curve_algo.h"

namespace ForgeCad::Kernel {

// --- Box ---------------------------------------------------------------------

void Box::add(const Vec3 &p) {
    for (int i = 0; i < 3; ++i) {
        lo[i] = std::min(lo[i], p[i]);
        hi[i] = std::max(hi[i], p[i]);
    }
}

void Box::add(const Box &other) {
    if (other.isEmpty()) return;
    add(other.lo);
    add(other.hi);
}

Box Box::padded(double margin) const {
    if (isEmpty()) return *this;
    Box result = *this;
    for (int i = 0; i < 3; ++i) {
        result.lo[i] -= margin;
        result.hi[i] += margin;
    }
    return result;
}

bool Box::overlaps(const Box &other) const {
    if (isEmpty() || other.isEmpty()) return false;
    for (int i = 0; i < 3; ++i)
        if (lo[i] > other.hi[i] || other.lo[i] > hi[i]) return false;
    return true;
}

Box curveBox(const Curve<3> &curve, const Interval &range) {
    Box box;
    switch (curve.type()) {
    case CurveType::Line:
        box.add(curve.point(range.lo));
        box.add(curve.point(range.hi));
        return box;
    case CurveType::Circle: {
        const auto &c = static_cast<const Circle<3> &>(curve);
        return Box{c.center(), c.center()}.padded(c.radius());
    }
    case CurveType::Ellipse: {
        const auto &e = static_cast<const Ellipse<3> &>(curve);
        return Box{e.center(), e.center()}.padded(std::max(e.xRadius(), e.yRadius()));
    }
    case CurveType::BSpline:
        for (const Vec3 &pole : static_cast<const BSplineCurve<3> &>(curve).poles()) box.add(pole);
        return box;
    case CurveType::Trimmed:
        return curveBox(*static_cast<const TrimmedCurve<3> &>(curve).basis(), range);
    case CurveType::Transformed: {
        const auto &t = static_cast<const TransformedCurve &>(curve);
        const Box base = curveBox(*t.basis(), range);
        for (int corner = 0; corner < 8; ++corner)
            box.add(t.transform().applyToPoint(Vec3(corner & 1 ? base.hi.x() : base.lo.x(), corner & 2 ? base.hi.y() : base.lo.y(),
                                                    corner & 4 ? base.hi.z() : base.lo.z())));
        return box;
    }
    default:
        for (int i = 0; i <= 64; ++i) box.add(curve.point(range.lo + range.length() * i / 64.0));
        return box.padded(0.05 * box.diagonal());
    }
}

// --- Radici di n . C(t) = offset ----------------------------------------------

namespace {

void addInRange(std::vector<double> &out, double t, const Interval &range) {
    const double slack = 1e-12 * std::max({1.0, std::fabs(range.lo), std::fabs(range.hi)});
    if (t >= range.lo - slack && t <= range.hi + slack) out.push_back(range.clamp(t));
}

// Radici di c0 + a cos t + b sin t = 0 (coniche).
template <int N>
void trigRoots(double c0, double a, double b, const Interval &range, double tolerance, PlaneRoots<N> &out) {
    const double amplitude = std::hypot(a, b);
    if (amplitude <= tolerance) {
        if (std::fabs(c0) <= tolerance) out.coincident.push_back(range);
        return;
    }
    const double k = -c0 / amplitude;
    if (std::fabs(k) > 1.0 + 1e-12) return;
    const double phase = std::atan2(b, a), alpha = std::acos(std::clamp(k, -1.0, 1.0));
    for (double base : {phase - alpha, phase + alpha}) {
        const double first = base + kTwoPi * std::ceil((range.lo - base) / kTwoPi - 1e-12);
        for (double t = first; t <= range.hi + 1e-12; t += kTwoPi) addInRange(out.parameters, t, range);
        if (alpha < 1e-12) break;  // radice doppia (tangenza)
    }
}

template <int N>
void rootsOf(const Curve<N> &curve, const Interval &range, const Vec<N> &n, double offset, double tolerance, PlaneRoots<N> &out) {
    switch (curve.type()) {
    case CurveType::Line: {
        const auto &line = static_cast<const Line<N> &>(curve);
        const double a = dot(n, line.direction()), b = dot(n, line.origin()) - offset;
        if (std::fabs(a) <= 1e-14 * norm(n)) {
            if (std::fabs(b) <= tolerance) out.coincident.push_back(range);
            return;
        }
        addInRange(out.parameters, -b / a, range);
        return;
    }
    case CurveType::Circle: {
        const auto &c = static_cast<const Circle<N> &>(curve);
        trigRoots(dot(n, c.center()) - offset, c.radius() * dot(n, c.xAxis()), c.radius() * dot(n, c.yAxis()), range, tolerance, out);
        return;
    }
    case CurveType::Ellipse: {
        const auto &e = static_cast<const Ellipse<N> &>(curve);
        trigRoots(dot(n, e.center()) - offset, e.xRadius() * dot(n, e.xAxis()), e.yRadius() * dot(n, e.yAxis()), range,
                  tolerance, out);
        return;
    }
    case CurveType::BSpline: {
        const auto &spline = static_cast<const BSplineCurve<N> &>(curve);
        const auto segments = spline.cachedBezierSegments();
        for (const BSplineCurve<N> &segment : *segments) {
            const Interval dom = segment.domain();
            if (dom.hi < range.lo || dom.lo > range.hi) continue;
            std::vector<double> coefficients;
            bool onPlane = true;
            for (int j = 0; j < segment.poleCount(); ++j) {
                const double value = dot(n, segment.poles()[j]) - offset;
                onPlane = onPlane && std::fabs(value) <= tolerance;
                coefficients.push_back(segment.weight(j) * value);
            }
            if (onPlane) {
                out.coincident.push_back({std::max(dom.lo, range.lo), std::min(dom.hi, range.hi)});
                continue;
            }
            for (double s : BernsteinPolynomial(coefficients).roots()) addInRange(out.parameters, dom.lo + s * dom.length(), range);
        }
        return;
    }
    case CurveType::Trimmed: {
        const auto &trimmed = static_cast<const TrimmedCurve<N> &>(curve);
        rootsOf(*trimmed.basis(), range, n, offset, tolerance, out);
        return;
    }
    case CurveType::Transformed:
        if constexpr (N == 3) {
            // n . (M B + t0) = offset  <=>  (M^T n) . B = offset - n . t0
            const auto &transformed = static_cast<const TransformedCurve &>(curve);
            const Transform3 &m = transformed.transform();
            Vec3 nt;
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 3; ++i) nt[j] += m.matrix(i, j) * n[i];
            rootsOf(*transformed.basis(), range, nt, offset - dot(n, m.translationPart()), tolerance, out);
            return;
        }
        break;
    default:
        break;
    }
    throw std::domain_error("planeRoots: tipo di curva non gestito");
}

}

template <int N>
PlaneRoots<N> planeRoots(const Curve<N> &curve, const Interval &range, const Vec<N> &normal, double offset, double tolerance) {
    PlaneRoots<N> out;
    rootsOf(curve, range, normal, offset, tolerance, out);
    std::sort(out.parameters.begin(), out.parameters.end());
    std::vector<double> unique;
    for (double t : out.parameters)
        if (unique.empty() || t - unique.back() > 1e-12 * std::max(1.0, std::fabs(t))) unique.push_back(t);
    out.parameters = std::move(unique);
    // Le radici dentro un tratto coincidente non sono isolate.
    out.parameters.erase(std::remove_if(out.parameters.begin(), out.parameters.end(), [&](double t) {
        for (const Interval &piece : out.coincident)
            if (t > piece.lo && t < piece.hi) return true;
        return false;
    }), out.parameters.end());
    return out;
}

template PlaneRoots<2> planeRoots<2>(const Curve<2> &, const Interval &, const Vec2 &, double, double);
template PlaneRoots<3> planeRoots<3>(const Curve<3> &, const Interval &, const Vec3 &, double, double);

// --- Curva con curva nel piano ---------------------------------------------------

namespace {

const Curve<2> &unwrap(const Curve<2> &curve) {
    const Curve<2> *c = &curve;
    while (c->type() == CurveType::Trimmed) c = static_cast<const TrimmedCurve<2> *>(c)->basis().get();
    return *c;
}

bool isConic(const Curve<2> &c) { return c.type() == CurveType::Circle || c.type() == CurveType::Ellipse; }

struct ConicData {
    Vec2 center, x, y;
    double rx, ry;
};

ConicData conicData(const Curve<2> &c) {
    if (c.type() == CurveType::Circle) {
        const auto &circle = static_cast<const Circle<2> &>(c);
        return {circle.center(), circle.xAxis(), circle.yAxis(), circle.radius(), circle.radius()};
    }
    const auto &e = static_cast<const Ellipse<2> &>(c);
    return {e.center(), e.xAxis(), e.yAxis(), e.xRadius(), e.yRadius()};
}

// Parametro angolare del punto sulla conica, riportato nel tratto (o falso).
bool angleIn(const ConicData &q, const Vec2 &p, const Interval &range, double &angle) {
    const Vec2 d = p - q.center;
    const double theta = std::atan2(dot(d, q.y) / q.ry, dot(d, q.x) / q.rx);
    const double slack = 1e-9;
    angle = theta + kTwoPi * std::ceil((range.lo - slack - theta) / kTwoPi);
    if (angle > range.hi + slack) return false;
    angle = range.clamp(angle);
    return true;
}

std::vector<BSplineCurve<2>> bezierPieces(const BSplineCurve<2> &spline, const Interval &range) {
    std::vector<BSplineCurve<2>> pieces;
    const auto segments = spline.cachedBezierSegments();
    for (BSplineCurve<2> segment : *segments) {
        Interval dom = segment.domain();
        if (dom.hi <= range.lo || dom.lo >= range.hi) continue;
        if (dom.lo < range.lo) {
            segment = segment.insertKnot(range.lo, segment.degree()).bezierSegments().back();
            dom = segment.domain();
        }
        if (dom.hi > range.hi) segment = segment.insertKnot(range.hi, segment.degree()).bezierSegments().front();
        pieces.push_back(std::move(segment));
    }
    return pieces;
}

// Tratti razionali (di Bezier) di una curva: B-spline, oppure conica come
// NURBS quadratica esatta.
std::vector<BSplineCurve<2>> rationalPieces(const Curve<2> &curve, const Interval &range) {
    const Curve<2> &c = unwrap(curve);
    if (c.type() == CurveType::BSpline) return bezierPieces(static_cast<const BSplineCurve<2> &>(c), range);
    if (c.type() == CurveType::Circle) return toBSpline(static_cast<const Circle<2> &>(c), range.lo, range.hi).bezierSegments();
    if (c.type() == CurveType::Ellipse) return toBSpline(static_cast<const Ellipse<2> &>(c), range.lo, range.hi).bezierSegments();
    throw std::domain_error("intersectCurves: tipo di curva non gestito");
}

// I doppioni si tolgono alla fine (vedi intersectCurves).
void addPoint(CurveCurveIntersection &out, const CurveCurvePoint &p, double) { out.points.push_back(p); }

// Parametro di un punto (che sta sulla curva) nel tratto.
bool parameterOn(const Curve<2> &curve, const Interval &range, const Vec2 &p, double tolerance, double &t) {
    const Curve<2> &c = unwrap(curve);
    if (isConic(c)) return angleIn(conicData(c), p, range, t);
    const CurveProjection<2> projection = projectPoint(curve, p, range);
    t = projection.parameter;
    return projection.distance <= tolerance;
}

void lineCase(const Line<2> &line, const Interval &lineRange, const Curve<2> &other, const Interval &otherRange, bool swapped,
              double tolerance, CurveCurveIntersection &out) {
    const Vec2 n = perpendicular(line.direction());
    const PlaneRoots<2> roots = planeRoots<2>(other, otherRange, n, dot(n, line.origin()), tolerance);
    auto push = [&](double s, double t, const Vec2 &p) {
        addPoint(out, swapped ? CurveCurvePoint{t, s, p} : CurveCurvePoint{s, t, p}, tolerance);
    };
    for (double t : roots.parameters) {
        const Vec2 p = other.point(t);
        const double s = dot(p - line.origin(), line.direction());
        if (lineRange.contains(s, tolerance)) push(lineRange.clamp(s), t, p);
    }
    for (const Interval &piece : roots.coincident) {
        // Tratto comune: gli estremi dell'uno che stanno sull'altro.
        std::vector<Vec2> candidates{other.point(piece.lo), other.point(piece.hi)};
        if (lineRange.isFinite()) {
            candidates.push_back(line.point(lineRange.lo));
            candidates.push_back(line.point(lineRange.hi));
        }
        for (const Vec2 &p : candidates) {
            const double s = dot(p - line.origin(), line.direction());
            double t;
            if (!lineRange.contains(s, tolerance) || !parameterOn(other, piece, p, tolerance, t)) continue;
            out.overlap = true;
            push(lineRange.clamp(s), t, p);
        }
    }
}

void conicCase(const Curve<2> &conic, const Interval &conicRange, const Curve<2> &other, const Interval &otherRange, bool swapped,
               double tolerance, CurveCurveIntersection &out) {
    const ConicData q = conicData(unwrap(conic));
    auto push = [&](double s, double t, const Vec2 &p) {
        addPoint(out, swapped ? CurveCurvePoint{t, s, p} : CurveCurvePoint{s, t, p}, tolerance);
    };
    const Curve<2> &o = unwrap(other);
    if (isConic(o)) {
        const ConicData r = conicData(o);
        const bool sameShape = distance(q.center, r.center) <= tolerance
            && ((std::fabs(q.rx - r.rx) <= tolerance && std::fabs(q.ry - r.ry) <= tolerance
                 && (std::fabs(q.rx - q.ry) <= tolerance || std::fabs(std::fabs(dot(q.x, r.x)) - 1.0) <= 1e-9)));
        if (sameShape) {
            // Stessa conica: gli estremi di un tratto che stanno sull'altro.
            for (const Vec2 &p : {conic.point(conicRange.lo), conic.point(conicRange.hi), other.point(otherRange.lo),
                                  other.point(otherRange.hi)}) {
                double s, t;
                if (angleIn(q, p, conicRange, s) && angleIn(r, p, otherRange, t)) {
                    out.overlap = true;
                    push(s, t, p);
                }
            }
            return;
        }
    }
    for (const BSplineCurve<2> &piece : rationalPieces(other, otherRange)) {
        // Equazione implicita della conica sui poli omogenei: grado 2p.
        std::vector<double> xs, ys, ws;
        for (int j = 0; j < piece.poleCount(); ++j) {
            const Vec2 d = piece.poles()[j] - q.center;
            const double w = piece.weight(j);
            xs.push_back(w * dot(d, q.x) / q.rx);
            ys.push_back(w * dot(d, q.y) / q.ry);
            ws.push_back(w);
        }
        const BernsteinPolynomial x(xs), y(ys), w(ws);
        const BernsteinPolynomial f = x * x + y * y - w * w;
        const Interval dom = piece.domain();
        for (double sRoot : f.roots()) {
            const Vec2 p = piece.point(dom.lo + sRoot * dom.length());
            double s, t;
            if (!angleIn(q, p, conicRange, s)) continue;
            if (o.type() == CurveType::BSpline) {
                t = otherRange.clamp(dom.lo + sRoot * dom.length());
            } else if (!parameterOn(other, otherRange, p, tolerance, t)) {
                continue;
            }
            push(s, t, p);
        }
    }
}

// Newton su A(s) = B(t) a partire da (s, t).
bool newtonIntersection(const Curve<2> &a, const Interval &ra, const Curve<2> &b, const Interval &rb, double &s, double &t,
                        double tolerance) {
    for (int iteration = 0; iteration < 40; ++iteration) {
        Vec2 da[2], db[2];
        a.evaluate(s, 1, da);
        b.evaluate(t, 1, db);
        const Vec2 f = da[0] - db[0];
        const double det = cross(da[1], Vec2(-db[1].x(), -db[1].y()));
        if (std::fabs(det) < 1e-300) break;
        // [A' -B'] (ds, dt) = -f
        const double ds = cross(-f, -db[1]) / det, dt = cross(da[1], -f) / det;
        s = ra.clamp(s + ds);
        t = rb.clamp(t + dt);
        if (std::fabs(ds) + std::fabs(dt) <= 1e-15 * (1.0 + std::fabs(s) + std::fabs(t))) break;
    }
    return distance(a.point(s), b.point(t)) <= tolerance;
}

struct Box2 {
    Vec2 lo{1e300, 1e300}, hi{-1e300, -1e300};
    void add(const Vec2 &p) {
        for (int i = 0; i < 2; ++i) {
            lo[i] = std::min(lo[i], p[i]);
            hi[i] = std::max(hi[i], p[i]);
        }
    }
    double diagonal() const { return distance(lo, hi); }
    bool overlaps(const Box2 &o, double margin) const {
        for (int i = 0; i < 2; ++i)
            if (lo[i] > o.hi[i] + margin || o.lo[i] > hi[i] + margin) return false;
        return true;
    }
};

Box2 polesBox(const BSplineCurve<2> &piece) {
    Box2 box;
    for (const Vec2 &p : piece.poles()) box.add(p);
    return box;
}

std::pair<BSplineCurve<2>, BSplineCurve<2>> halves(const BSplineCurve<2> &piece) {
    const Interval dom = piece.domain();
    const std::vector<BSplineCurve<2>> parts = piece.insertKnot(0.5 * (dom.lo + dom.hi), piece.degree()).bezierSegments();
    return {parts.front(), parts.back()};
}

// Suddivisione delle pezze di Bezier finche' i box dei poli (che contengono
// la curva) si separano o diventano piccoli; poi Newton sulle curve originali.
void subdivide(const Curve<2> &a, const Interval &ra, const Curve<2> &b, const Interval &rb, const BSplineCurve<2> &pa,
               const BSplineCurve<2> &pb, int depth, double scale, double tolerance, CurveCurveIntersection &out, int &leaves) {
    const Box2 boxA = polesBox(pa), boxB = polesBox(pb);
    if (!boxA.overlaps(boxB, tolerance)) return;
    if ((boxA.diagonal() <= 1e-6 * scale && boxB.diagonal() <= 1e-6 * scale) || depth >= 60) {
        if (++leaves > 4000) throw std::domain_error("intersectCurves: curve sovrapposte non gestite");
        double s = 0.5 * (pa.domain().lo + pa.domain().hi), t = 0.5 * (pb.domain().lo + pb.domain().hi);
        if (newtonIntersection(a, ra, b, rb, s, t, tolerance)) addPoint(out, {s, t, a.point(s)}, tolerance);
        return;
    }
    if (boxA.diagonal() >= boxB.diagonal()) {
        const auto [left, right] = halves(pa);
        subdivide(a, ra, b, rb, left, pb, depth + 1, scale, tolerance, out, leaves);
        subdivide(a, ra, b, rb, right, pb, depth + 1, scale, tolerance, out, leaves);
    } else {
        const auto [left, right] = halves(pb);
        subdivide(a, ra, b, rb, pa, left, depth + 1, scale, tolerance, out, leaves);
        subdivide(a, ra, b, rb, pa, right, depth + 1, scale, tolerance, out, leaves);
    }
}

}

CurveCurveIntersection intersectCurves(const Curve<2> &a, const Interval &aRange, const Curve<2> &b, const Interval &bRange,
                                       double tolerance, bool distinctParameters) {
    CurveCurveIntersection out;
    const Curve<2> &ua = unwrap(a), &ub = unwrap(b);
    if (ua.type() == CurveType::Line) {
        lineCase(static_cast<const Line<2> &>(ua), aRange, b, bRange, false, tolerance, out);
    } else if (ub.type() == CurveType::Line) {
        lineCase(static_cast<const Line<2> &>(ub), bRange, a, aRange, true, tolerance, out);
    } else if (isConic(ua)) {
        conicCase(a, aRange, b, bRange, false, tolerance, out);
    } else if (isConic(ub)) {
        conicCase(b, bRange, a, aRange, true, tolerance, out);
    } else if (ua.type() == CurveType::BSpline && ub.type() == CurveType::BSpline) {
        const std::vector<BSplineCurve<2>> piecesA = bezierPieces(static_cast<const BSplineCurve<2> &>(ua), aRange);
        const std::vector<BSplineCurve<2>> piecesB = bezierPieces(static_cast<const BSplineCurve<2> &>(ub), bRange);
        Box2 all;
        for (const auto &piece : piecesA)
            for (const Vec2 &p : piece.poles()) all.add(p);
        for (const auto &piece : piecesB)
            for (const Vec2 &p : piece.poles()) all.add(p);
        const double scale = std::max(all.diagonal(), 1e-300);
        int leaves = 0;
        for (const auto &pa : piecesA)
            for (const auto &pb : piecesB) subdivide(a, aRange, b, bRange, pa, pb, 0, scale, tolerance, out, leaves);
        // Estremi dei tratti che stanno sull'altra curva (contatti agli estremi,
        // che Newton puo' trovare solo al limite del tratto).
        for (int side = 0; side < 2; ++side)
            for (double end : {side ? bRange.lo : aRange.lo, side ? bRange.hi : aRange.hi}) {
                const Curve<2> &self = side ? b : a, &otherCurve = side ? a : b;
                const Interval &otherRange = side ? aRange : bRange;
                const Vec2 p = self.point(end);
                const CurveProjection<2> projection = projectPoint(otherCurve, p, otherRange);
                if (projection.distance <= tolerance)
                    addPoint(out, side ? CurveCurvePoint{projection.parameter, end, p} : CurveCurvePoint{end, projection.parameter, p},
                             tolerance);
            }
    } else {
        throw std::domain_error("intersectCurves: tipo di curva non gestito");
    }
    // Stesso punto: un doppione, a meno che (distinctParameters) i parametri
    // sulla prima curva siano diversi.
    const double gap = 1e-6 * std::max(1.0, aRange.isFinite() ? aRange.length() : 1.0);
    std::vector<CurveCurvePoint> unique;
    for (const CurveCurvePoint &p : out.points) {
        bool duplicate = false;
        for (const CurveCurvePoint &q : unique)
            duplicate = duplicate || (distance(p.point, q.point) <= tolerance && (!distinctParameters || std::fabs(p.s - q.s) <= gap));
        if (!duplicate) unique.push_back(p);
    }
    out.points = std::move(unique);
    std::sort(out.points.begin(), out.points.end(), [](const CurveCurvePoint &x, const CurveCurvePoint &y) { return x.s < y.s; });
    return out;
}

// --- Piano con superficie -------------------------------------------------------

// Tratto della retta dentro il box (metodo delle lastre).
bool clipLineToBox(const Vec3 &origin, const Vec3 &direction, const Box &box, Interval &range) {
    double lo = -1e300, hi = 1e300;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(direction[i]) < 1e-300) {
            if (origin[i] < box.lo[i] || origin[i] > box.hi[i]) return false;
            continue;
        }
        double a = (box.lo[i] - origin[i]) / direction[i], b = (box.hi[i] - origin[i]) / direction[i];
        if (a > b) std::swap(a, b);
        lo = std::max(lo, a);
        hi = std::min(hi, b);
    }
    if (!(lo < hi)) return false;
    range = {lo, hi};
    return true;
}

namespace {

void addLine(PlaneSurfaceIntersection &out, const Vec3 &point, const Vec3 &direction, const Box &bounds, bool tangent = false) {
    auto line = std::make_shared<Line<3>>(point, direction);
    Interval range;
    if (!clipLineToBox(line->origin(), line->direction(), bounds, range)) return;
    (tangent ? out.tangentCurves : out.curves).push_back(line);
    (tangent ? out.tangentRanges : out.ranges).push_back(range);
}

// Proiezione obliqua sul piano lungo D applicata a una curva (mappa affine:
// il tipo NURBS e' conservato; le coniche passano per la NURBS esatta).
CurvePtr<3> obliqueImage(const Curve<3> &curve, const Interval &range, const Vec3 &n, double offset, const Vec3 &d, Interval &outRange) {
    const double nd = dot(n, d);
    auto map = [&](const Vec3 &x) { return x + ((offset - dot(n, x)) / nd) * d; };
    auto mapSpline = [&](const BSplineCurve<3> &spline) {
        std::vector<Vec3> poles;
        for (const Vec3 &pole : spline.poles()) poles.push_back(map(pole));
        return std::make_shared<BSplineCurve<3>>(spline.degree(), spline.knots(), std::move(poles), spline.weights());
    };
    switch (curve.type()) {
    case CurveType::Line: {
        const auto &line = static_cast<const Line<3> &>(curve);
        const Vec3 a = map(line.point(range.lo)), b = map(line.point(range.hi));
        outRange = {0.0, distance(a, b)};
        return std::make_shared<Line<3>>(a, b - a);
    }
    case CurveType::Circle: {
        auto spline = mapSpline(toBSpline(static_cast<const Circle<3> &>(curve), range.lo, range.hi));
        outRange = spline->domain();
        return spline;
    }
    case CurveType::Ellipse: {
        auto spline = mapSpline(toBSpline(static_cast<const Ellipse<3> &>(curve), range.lo, range.hi));
        outRange = spline->domain();
        return spline;
    }
    case CurveType::BSpline: {
        auto spline = mapSpline(static_cast<const BSplineCurve<3> &>(curve));
        outRange = range;
        return spline;
    }
    case CurveType::Trimmed:
        return obliqueImage(*static_cast<const TrimmedCurve<3> &>(curve).basis(), range, n, offset, d, outRange);
    default:
        throw std::domain_error("intersectPlaneSurface: curva base dell'estrusione non gestita");
    }
}

}

PlaneSurfaceIntersection intersectPlaneSurface(const Plane &plane, const Surface &surface, const Box &bounds, double tolerance) {
    PlaneSurfaceIntersection out;
    const Vec3 n = plane.frame().zDir(), p0 = plane.frame().origin();
    const double offset = dot(n, p0);
    switch (surface.type()) {
    case SurfaceType::Plane: {
        const Frame3 &other = static_cast<const Plane &>(surface).frame();
        const Vec3 m = other.zDir();
        const Vec3 d = cross(n, m);
        const double sine = norm(d);
        if (sine <= 1e-12) {
            if (std::fabs(dot(n, other.origin()) - offset) <= tolerance) out.coincident = true;
            return out;
        }
        // Punto comune: combinazione di n e m.
        const double c1 = offset, c2 = dot(m, other.origin()), nm = dot(n, m), det = 1.0 - nm * nm;
        const Vec3 point = ((c1 - c2 * nm) / det) * n + ((c2 - c1 * nm) / det) * m;
        addLine(out, point, d / sine, bounds);
        return out;
    }
    case SurfaceType::Cylinder: {
        const auto &cylinder = static_cast<const CylindricalSurface &>(surface);
        const Frame3 &f = cylinder.frame();
        const Vec3 a = f.zDir(), c = f.origin();
        const double r = cylinder.radius(), na = dot(n, a);
        if (std::fabs(na) <= 1e-12) {
            // Piano parallelo all'asse: 0, 1 (tangente) o 2 generatrici.
            const double delta = dot(n, c) - offset;
            if (std::fabs(delta) > r + tolerance) return out;
            if (std::fabs(std::fabs(delta) - r) <= tolerance) {
                out.tangent = true;
                addLine(out, c - delta * n, a, bounds, true);
                return out;
            }
            const Vec3 foot = c - delta * n, m = normalized(cross(a, n));
            const double half = std::sqrt(r * r - delta * delta);
            addLine(out, foot + half * m, a, bounds);
            addLine(out, foot - half * m, a, bounds);
            return out;
        }
        const Vec3 center = c + ((offset - dot(n, c)) / na) * a;
        if (std::fabs(na) >= 1.0 - 1e-15) {
            // Sezione retta: cerchio con gli assi del cilindro (u = t).
            out.curves.push_back(std::make_shared<Circle<3>>(center, f.xDir(), f.yDir(), r));
            out.ranges.push_back({0.0, kTwoPi});
            return out;
        }
        // Sezione obliqua: ellisse E(t) = C + r cos t (e - k a) + r sin t m, con
        // m = a x n / |a x n|, e = m x a, k = (n.e)/(n.a).
        const Vec3 m = normalized(cross(a, n)), e = cross(m, a);
        const double k = dot(n, e) / na;
        const Vec3 major = r * (e - k * a);
        const double majorRadius = norm(major);
        out.curves.push_back(std::make_shared<Ellipse<3>>(center, major / majorRadius, m, majorRadius, r));
        out.ranges.push_back({0.0, kTwoPi});
        return out;
    }
    case SurfaceType::Sphere: {
        const auto &sphere = static_cast<const SphericalSurface &>(surface);
        const Vec3 c = sphere.frame().origin();
        const double R = sphere.radius(), delta = dot(n, c) - offset;
        if (std::fabs(delta) > R + tolerance) return out;
        if (std::fabs(std::fabs(delta) - R) <= tolerance) {
            out.tangent = true;
            return out;
        }
        out.curves.push_back(std::make_shared<Circle<3>>(c - delta * n, plane.frame().xDir(), plane.frame().yDir(),
                                                         std::sqrt(R * R - delta * delta)));
        out.ranges.push_back({0.0, kTwoPi});
        return out;
    }
    case SurfaceType::Extrusion: {
        const auto &extrusion = static_cast<const ExtrusionSurface &>(surface);
        const Vec3 d = extrusion.direction();
        const Curve<3> &base = *extrusion.curve();
        const Interval domain = base.domain();
        const double nd = dot(n, d);
        if (std::fabs(nd) <= 1e-12) {
            // Piano parallelo alla direzione: generatrici nei punti in cui la
            // curva base attraversa il piano.
            const PlaneRoots<3> roots = planeRoots<3>(base, domain, n, offset, tolerance);
            if (!roots.coincident.empty()) {
                out.coincident = true;
                return out;
            }
            for (double u : roots.parameters) {
                // Tangenza della curva base al piano: generatrice di tangenza.
                if (std::fabs(dot(n, base.derivative(u))) <= 1e-9 * norm(base.derivative(u))) {
                    out.tangent = true;
                    addLine(out, base.point(u), d, bounds, true);
                    continue;
                }
                if (base.isPeriodic() || extrusion.isUPeriodic()) {
                    // Sulle curve chiuse lo stesso punto puo' comparire a inizio e fine.
                    bool duplicate = false;
                    for (const CurvePtr<3> &curve : out.curves)
                        duplicate = duplicate || distance(static_cast<const Line<3> &>(*curve).origin(), base.point(u)) <= tolerance;
                    if (duplicate) continue;
                }
                auto line = std::make_shared<Line<3>>(base.point(u), d);
                Interval range;
                if (clipLineToBox(line->origin(), line->direction(), bounds, range)) {
                    out.curves.push_back(line);
                    out.ranges.push_back(range);
                }
            }
            return out;
        }
        Interval range;
        out.curves.push_back(obliqueImage(base, domain, n, offset, d, range));
        out.ranges.push_back(range);
        return out;
    }
    default:
        throw std::domain_error("intersectPlaneSurface: superficie non gestita");
    }
}

// --- Retta con superficie ----------------------------------------------------------

std::vector<double> intersectLineSurface(const Vec3 &origin, const Vec3 &direction, const Surface &surface, bool &grazing,
                                         double tolerance) {
    grazing = false;
    std::vector<double> result;
    auto quadratic = [&](double a, double b, double c) {
        // a t^2 + b t + c = 0, con a > 0.
        const double disc = b * b - 4.0 * a * c;
        const double scale = b * b + std::fabs(4.0 * a * c);
        if (disc < -1e-12 * scale) return;
        if (disc <= 1e-12 * scale) {
            grazing = true;
            return;
        }
        const double root = std::sqrt(disc), q = -0.5 * (b + (b >= 0.0 ? root : -root));
        result.push_back(q / a);
        result.push_back(c / q);
    };
    switch (surface.type()) {
    case SurfaceType::Plane: {
        const Frame3 &f = static_cast<const Plane &>(surface).frame();
        const double nd = dot(f.zDir(), direction), dist = dot(f.zDir(), f.origin() - origin);
        if (std::fabs(nd) <= 1e-9) {
            grazing = std::fabs(dist) <= tolerance;
            return result;
        }
        result.push_back(dist / nd);
        break;
    }
    case SurfaceType::Cylinder: {
        const auto &cylinder = static_cast<const CylindricalSurface &>(surface);
        const Vec3 a = cylinder.frame().zDir(), w = origin - cylinder.frame().origin();
        const Vec3 wp = w - dot(w, a) * a, dp = direction - dot(direction, a) * a;
        const double r = cylinder.radius();
        if (squaredNorm(dp) <= 1e-18) {
            grazing = std::fabs(norm(wp) - r) <= tolerance;
            return result;
        }
        quadratic(squaredNorm(dp), 2.0 * dot(wp, dp), squaredNorm(wp) - r * r);
        break;
    }
    case SurfaceType::Sphere: {
        const auto &sphere = static_cast<const SphericalSurface &>(surface);
        const Vec3 w = origin - sphere.frame().origin();
        quadratic(1.0, 2.0 * dot(w, direction), squaredNorm(w) - sphere.radius() * sphere.radius());
        break;
    }
    case SurfaceType::Extrusion: {
        // Piano per la retta parallelo a D: la retta incontra la superficie
        // nei punti in cui la curva base attraversa quel piano.
        const auto &extrusion = static_cast<const ExtrusionSurface &>(surface);
        const Vec3 d = extrusion.direction();
        const Vec3 normal = cross(direction, d);
        const double length = norm(normal);
        if (length <= 1e-9) {
            grazing = true;
            return result;
        }
        const Vec3 unit = normal / length;
        const Curve<3> &base = *extrusion.curve();
        const PlaneRoots<3> roots = planeRoots<3>(base, base.domain(), unit, dot(unit, origin), tolerance);
        if (!roots.coincident.empty()) {
            grazing = true;
            return result;
        }
        std::vector<Vec3> accepted;  // su una curva chiusa inizio e fine sono lo stesso punto
        for (double u : roots.parameters) {
            Vec3 c[2];
            base.evaluate(u, 1, c);
            bool duplicate = false;
            for (const Vec3 &q : accepted) duplicate = duplicate || distance(q, c[0]) <= tolerance;
            if (duplicate) continue;
            accepted.push_back(c[0]);
            if (std::fabs(dot(unit, c[1])) <= 1e-9 * norm(c[1])) grazing = true;
            // C(u) + v D = origin + t direction: minimi quadrati su (t, v).
            const Vec3 rhs = c[0] - origin;
            const double a12 = -dot(direction, d), det = 1.0 - a12 * a12;
            const double b1 = dot(direction, rhs), b2 = -dot(d, rhs);
            result.push_back((b1 - a12 * b2) / det);
        }
        break;
    }
    default:
        throw std::domain_error("intersectLineSurface: superficie non gestita");
    }
    std::sort(result.begin(), result.end());
    return result;
}


// --- Cilindri generalizzati e curva con superficie ------------------------------------

bool generalizedCylinder(const Surface &surface, GeneralizedCylinder &out) {
    if (surface.type() == SurfaceType::Cylinder) {
        const auto &cylinder = static_cast<const CylindricalSurface &>(surface);
        const Frame3 &f = cylinder.frame();
        out.profile = std::make_shared<Circle<3>>(f.origin(), f.xDir(), f.yDir(), cylinder.radius());
        out.domain = {0.0, kTwoPi};
        out.direction = f.zDir();
        out.periodic = true;
        return true;
    }
    if (surface.type() == SurfaceType::Extrusion) {
        const auto &extrusion = static_cast<const ExtrusionSurface &>(surface);
        out.profile = extrusion.curve();
        out.domain = extrusion.uDomain();
        out.direction = extrusion.direction();
        out.periodic = extrusion.isUPeriodic();
        return true;
    }
    return false;
}

PlanarImage planarImage(const Curve<3> &curve, const Interval &range, const Frame3 &frame) {
    PlanarImage out;
    auto local = [&](const Vec3 &p) {
        const Vec3 q = frame.toLocal(p);
        return Vec2(q.x(), q.y());
    };
    auto localDirection = [&](const Vec3 &d) {
        const Vec3 q = frame.directionToLocal(d);
        return Vec2(q.x(), q.y());
    };
    auto mapSpline = [&](const BSplineCurve<3> &spline) {
        std::vector<Vec2> poles;
        for (const Vec3 &pole : spline.poles()) poles.push_back(local(pole));
        return std::make_shared<BSplineCurve<2>>(spline.degree(), spline.knots(), std::move(poles), spline.weights());
    };
    switch (curve.type()) {
    case CurveType::Line: {
        const auto &line = static_cast<const Line<3> &>(curve);
        const Vec2 direction = localDirection(line.direction());
        const double k = norm(direction);
        if (k <= 1e-12) {
            out.point = local(line.origin());
            out.range = range;
            out.toCurve = [](double t) { return t; };
            return out;
        }
        out.curve = std::make_shared<Line<2>>(local(line.origin()), direction / k);
        out.range = {range.lo * k, range.hi * k};
        out.toCurve = [k](double s) { return s / k; };
        return out;
    }
    case CurveType::Circle:
    case CurveType::Ellipse: {
        Vec3 center, x, y;
        double rx, ry;
        if (curve.type() == CurveType::Circle) {
            const auto &c = static_cast<const Circle<3> &>(curve);
            center = c.center(), x = c.xAxis(), y = c.yAxis(), rx = ry = c.radius();
        } else {
            const auto &e = static_cast<const Ellipse<3> &>(curve);
            center = e.center(), x = e.xAxis(), y = e.yAxis(), rx = e.xRadius(), ry = e.yRadius();
        }
        if (std::fabs(dot(cross(x, y), frame.zDir())) >= 1.0 - 1e-12) {
            // Conica in un piano parallelo: resta una conica, stesso parametro.
            const Vec2 c2 = local(center), x2 = localDirection(x), y2 = localDirection(y);
            if (curve.type() == CurveType::Circle) out.curve = std::make_shared<Circle<2>>(c2, x2, y2, rx);
            else out.curve = std::make_shared<Ellipse<2>>(c2, x2, y2, rx, ry);
            out.range = range;
            out.toCurve = [](double t) { return t; };
            return out;
        }
        auto spline = std::make_shared<BSplineCurve<3>>(
            curve.type() == CurveType::Circle ? toBSpline(static_cast<const Circle<3> &>(curve), range.lo, range.hi)
                                              : toBSpline(static_cast<const Ellipse<3> &>(curve), range.lo, range.hi));
        out.curve = mapSpline(*spline);
        out.range = spline->domain();
        // Il parametro NURBS non e' l'angolo: lo si ricava dal punto 3D.
        out.toCurve = [spline, center, x, y, rx, ry, range](double s) {
            const Vec3 d = spline->point(s) - center;
            const double theta = std::atan2(dot(d, y) / ry, dot(d, x) / rx);
            return range.clamp(theta + kTwoPi * std::ceil((range.lo - 1e-9 - theta) / kTwoPi));
        };
        return out;
    }
    case CurveType::BSpline:
        out.curve = mapSpline(static_cast<const BSplineCurve<3> &>(curve));
        out.range = range;
        out.toCurve = [](double t) { return t; };
        return out;
    case CurveType::Trimmed:
        return planarImage(*static_cast<const TrimmedCurve<3> &>(curve).basis(), range, frame);
    default:
        throw std::domain_error("planarImage: tipo di curva non gestito");
    }
}

Frame3 normalFrame(const Vec3 &direction, const Vec3 &origin) {
    int smallest = 0;
    for (int i = 1; i < 3; ++i)
        if (std::fabs(direction[i]) < std::fabs(direction[smallest])) smallest = i;
    Vec3 reference;
    reference[smallest] = 1.0;
    return Frame3(origin, direction, reference);
}

CurveSurfaceIntersection intersectCurveSurface(const Curve<3> &curve, const Interval &range, const Surface &surface,
                                               double tolerance) {
    CurveSurfaceIntersection out;
    if (surface.type() == SurfaceType::Plane) {
        const Frame3 &f = static_cast<const Plane &>(surface).frame();
        const PlaneRoots<3> roots = planeRoots<3>(curve, range, f.zDir(), dot(f.zDir(), f.origin()), tolerance);
        out.parameters = roots.parameters;
        out.coincident = roots.coincident;
        return out;
    }
    GeneralizedCylinder cylinder;
    if (!generalizedCylinder(surface, cylinder)) throw std::domain_error("intersectCurveSurface: superficie non gestita");
    // Un punto sta sulla superficie se la sua proiezione lungo D sta sulla sezione.
    const Frame3 frame = normalFrame(cylinder.direction, cylinder.profile->point(cylinder.domain.lo));
    const PlanarImage image = planarImage(curve, range, frame);
    const PlanarImage section = planarImage(*cylinder.profile, cylinder.domain, frame);
    if (!image.curve) {
        if (projectPoint(*section.curve, image.point, section.range).distance <= tolerance) out.coincident.push_back(range);
        return out;
    }
    // La proiezione puo' sovrapporre punti diversi della curva (un cerchio in
    // un piano parallelo a D si proietta in un segmento percorso due volte).
    const CurveCurveIntersection hits = intersectCurves(*image.curve, image.range, *section.curve, section.range, tolerance, true);
    for (const CurveCurvePoint &p : hits.points) out.parameters.push_back(range.clamp(image.toCurve(p.s)));
    std::sort(out.parameters.begin(), out.parameters.end());
    if (hits.overlap) {
        // Tratti comuni: tra due parametri consecutivi, se il punto medio sta sulla superficie.
        std::vector<double> ends{range.lo};
        ends.insert(ends.end(), out.parameters.begin(), out.parameters.end());
        ends.push_back(range.hi);
        for (std::size_t i = 0; i + 1 < ends.size(); ++i) {
            if (ends[i + 1] - ends[i] <= 1e-12 * std::max(1.0, range.length())) continue;
            const Vec3 middle = curve.point(0.5 * (ends[i] + ends[i + 1]));
            if (projectPoint(*section.curve, Vec2(frame.toLocal(middle).x(), frame.toLocal(middle).y()), section.range).distance <= tolerance)
                out.coincident.push_back({ends[i], ends[i + 1]});
        }
        out.parameters.erase(std::remove_if(out.parameters.begin(), out.parameters.end(), [&](double t) {
            for (const Interval &piece : out.coincident)
                if (t > piece.lo && t < piece.hi) return true;
            return false;
        }), out.parameters.end());
    }
    return out;
}

}
