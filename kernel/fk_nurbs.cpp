#include "fk_nurbs.h"

#include <cmath>
#include <stdexcept>

namespace ForgeCad::Kernel {
namespace {

std::vector<BSplineCurve<3>> splinePieces(const BSplineCurve<3> &spline, const Interval &range) {
    std::vector<BSplineCurve<3>> pieces;
    for (BSplineCurve<3> segment : *spline.cachedBezierSegments()) {
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

BSplineCurve<3> transformed(const BSplineCurve<3> &spline, const Transform3 &transform) {
    std::vector<Vec3> poles;
    for (const Vec3 &p : spline.poles()) poles.push_back(transform.applyToPoint(p));
    return BSplineCurve<3>(spline.degree(), spline.knots(), std::move(poles), spline.weights());
}

// Curva (non limitata ne' trasformata) come B-spline esatta che copre il tratto.
BSplineCurve<3> wholeSpline(const Curve<3> &curve, const Interval &range) {
    switch (curve.type()) {
    case CurveType::Line: {
        const auto &line = static_cast<const Line<3> &>(curve);
        return BSplineCurve<3>(1, {range.lo, range.lo, range.hi, range.hi}, {line.point(range.lo), line.point(range.hi)});
    }
    case CurveType::Circle: return toBSpline(static_cast<const Circle<3> &>(curve), range.lo, range.hi);
    case CurveType::Ellipse: return toBSpline(static_cast<const Ellipse<3> &>(curve), range.lo, range.hi);
    case CurveType::BSpline: return static_cast<const BSplineCurve<3> &>(curve);
    case CurveType::Trimmed: return wholeSpline(*static_cast<const TrimmedCurve<3> &>(curve).basis(), range);
    case CurveType::Transformed: {
        const auto &t = static_cast<const TransformedCurve &>(curve);
        return transformed(wholeSpline(*t.basis(), range), t.transform());
    }
    default:
        break;
    }
    throw std::domain_error("rationalBezierPieces: tipo di curva non gestito");
}

// Finestra effettiva in una direzione: un periodo intero se la finestra lo supera.
Interval window(const Interval &range, bool periodic, double period, const Interval &domain) {
    if (periodic && range.length() >= period * (1.0 - 1e-12)) return {domain.lo, domain.lo + period};
    if (!range.isFinite()) throw std::domain_error("toBSplineSurface: finestra infinita");
    return range;
}

// Superficie ottenuta ruotando i poli di una curva (meridiano) attorno
// all'asse (punto A, direzione a unitaria) per gli angoli della finestra:
// prodotto tensoriale del cerchio unitario NURBS e del meridiano (NURBS
// Book, A8.1), valido per qualsiasi meridiano (anche fuori da un piano per l'asse).
BSplineSurface revolve(const BSplineCurve<3> &meridian, const Vec3 &axisPoint, const Frame3 &axes, const Interval &u) {
    const BSplineCurve<2> circle = conicArcToBSpline<2>(Vec2(0, 0), Vec2(1, 0), Vec2(0, 1), u.lo, u.hi);
    const Vec3 &e1 = axes.xDir(), &e2 = axes.yDir(), &a = axes.zDir();
    const int nu = circle.poleCount(), nv = meridian.poleCount();
    std::vector<Vec3> poles(std::size_t(nu * nv));
    std::vector<double> weights(std::size_t(nu * nv));
    for (int j = 0; j < nv; ++j) {
        const Vec3 d = meridian.poles()[j] - axisPoint;
        const double zeta = dot(d, a), x = dot(d, e1), y = dot(d, e2);
        for (int i = 0; i < nu; ++i) {
            const Vec2 c = circle.poles()[i];
            // Rotazione destrorsa attorno ad a: (x, y) -> (x cos - y sin, x sin + y cos).
            poles[std::size_t(i * nv + j)] = axisPoint + zeta * a + (x * c.x() - y * c.y()) * e1 + (x * c.y() + y * c.x()) * e2;
            weights[std::size_t(i * nv + j)] = circle.weight(i) * meridian.weight(j);
        }
    }
    return BSplineSurface(2, meridian.degree(), circle.knots(), meridian.knots(), nu, nv, std::move(poles), std::move(weights));
}

// Meridiano nel semipiano (e1, a) dato come curva 2D (rho, zeta) portata in 3D.
BSplineCurve<3> meridianIn(const Frame3 &axes, const BSplineCurve<2> &profile) {
    std::vector<Vec3> poles;
    for (const Vec2 &p : profile.poles()) poles.push_back(axes.origin() + p.x() * axes.xDir() + p.y() * axes.zDir());
    return BSplineCurve<3>(profile.degree(), profile.knots(), std::move(poles), profile.weights());
}

BSplineCurve<2> segment2(const Vec2 &a, const Vec2 &b, const Interval &v) {
    return BSplineCurve<2>(1, {v.lo, v.lo, v.hi, v.hi}, {a, b});
}

}

std::vector<BSplineCurve<3>> rationalBezierPieces(const Curve<3> &curve, const Interval &range) {
    if (curve.type() == CurveType::BSpline) return splinePieces(static_cast<const BSplineCurve<3> &>(curve), range);
    if (curve.type() == CurveType::Trimmed) return rationalBezierPieces(*static_cast<const TrimmedCurve<3> &>(curve).basis(), range);
    if (curve.type() == CurveType::Transformed) {
        const auto &t = static_cast<const TransformedCurve &>(curve);
        std::vector<BSplineCurve<3>> pieces;
        for (const BSplineCurve<3> &piece : rationalBezierPieces(*t.basis(), range)) pieces.push_back(transformed(piece, t.transform()));
        return pieces;
    }
    return wholeSpline(curve, range).bezierSegments();
}

BSplineSurface toBSplineSurface(const Surface &surface, const Interval &uRange, const Interval &vRange) {
    const Interval u = window(uRange, surface.isUPeriodic(), surface.uPeriod(), surface.uDomain());
    const Interval v = window(vRange, surface.isVPeriodic(), surface.vPeriod(), surface.vDomain());
    switch (surface.type()) {
    case SurfaceType::Plane: {
        std::vector<Vec3> poles{surface.point(u.lo, v.lo), surface.point(u.lo, v.hi), surface.point(u.hi, v.lo), surface.point(u.hi, v.hi)};
        return BSplineSurface(1, 1, {u.lo, u.lo, u.hi, u.hi}, {v.lo, v.lo, v.hi, v.hi}, 2, 2, std::move(poles));
    }
    case SurfaceType::Cylinder: {
        const auto &s = static_cast<const CylindricalSurface &>(surface);
        const double r = s.radius();
        return revolve(meridianIn(s.frame(), segment2(Vec2(r, v.lo), Vec2(r, v.hi), v)), s.frame().origin(), s.frame(), u);
    }
    case SurfaceType::Cone: {
        const auto &s = static_cast<const ConicalSurface &>(surface);
        const double r = s.referenceRadius(), sa = std::sin(s.semiAngle()), ca = std::cos(s.semiAngle());
        return revolve(meridianIn(s.frame(), segment2(Vec2(r + v.lo * sa, v.lo * ca), Vec2(r + v.hi * sa, v.hi * ca), v)),
                       s.frame().origin(), s.frame(), u);
    }
    case SurfaceType::Sphere: {
        const auto &s = static_cast<const SphericalSurface &>(surface);
        const double r = s.radius();
        return revolve(meridianIn(s.frame(), conicArcToBSpline<2>(Vec2(0, 0), Vec2(r, 0), Vec2(0, r), v.lo, v.hi)), s.frame().origin(),
                       s.frame(), u);
    }
    case SurfaceType::Torus: {
        const auto &s = static_cast<const ToroidalSurface &>(surface);
        const double big = s.majorRadius(), small = s.minorRadius();
        return revolve(meridianIn(s.frame(), conicArcToBSpline<2>(Vec2(big, 0), Vec2(small, 0), Vec2(0, small), v.lo, v.hi)),
                       s.frame().origin(), s.frame(), u);
    }
    case SurfaceType::Revolution: {
        const auto &s = static_cast<const RevolutionSurface &>(surface);
        const Vec3 a = s.axisDirection();
        const Vec3 reference = std::fabs(a.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        const Frame3 axes(s.axisPoint(), a, reference);
        return revolve(wholeSpline(*s.meridian(), v), s.axisPoint(), axes, u);
    }
    case SurfaceType::Extrusion: {
        const auto &s = static_cast<const ExtrusionSurface &>(surface);
        const Interval base = surface.isUPeriodic() ? s.curve()->domain() : u;
        const BSplineCurve<3> curve = wholeSpline(*s.curve(), base);
        const int nu = curve.poleCount();
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < nu; ++i)
            for (double t : {v.lo, v.hi}) {
                poles.push_back(curve.poles()[i] + t * s.direction());
                weights.push_back(curve.weight(i));
            }
        if (!curve.isRational()) weights.clear();
        return BSplineSurface(curve.degree(), 1, curve.knots(), {v.lo, v.lo, v.hi, v.hi}, nu, 2, std::move(poles), std::move(weights));
    }
    case SurfaceType::BSpline:
        return static_cast<const BSplineSurface &>(surface);
    }
    throw std::domain_error("toBSplineSurface: tipo di superficie non gestito");
}

}
