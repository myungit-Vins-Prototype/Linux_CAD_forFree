#include "fk_curve_surface.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "fk_bernstein.h"
#include "fk_curve_algo.h"
#include "fk_nurbs.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

std::vector<double> rootsAndTouchings(const BernsteinPolynomial &p, double valueTolerance, bool &touching) {
    std::vector<double> result = p.roots();
    if (p.degree() >= 2)
        for (double s : p.derivative().roots())
            if (std::fabs(p.evaluate(s)) <= valueTolerance) {
                result.push_back(s);
                touching = true;
            }
    return result;
}

// Parametro della curva originale per un punto trovato su un tratto di
// Bezier: le B-spline conservano il parametro, le altre curve no.
double curveParameter(const Curve<3> &curve, const Interval &range, const BSplineCurve<3> &piece, double s) {
    if (curve.type() == CurveType::BSpline) return range.clamp(s);
    return projectPoint(curve, piece.point(s), range).parameter;
}

void sortAndMerge(CurveSurfaceIntersection &out, const Curve<3> &curve, double tolerance) {
    std::sort(out.parameters.begin(), out.parameters.end());
    std::vector<double> unique;
    for (double t : out.parameters)
        if (unique.empty() || distance(curve.point(t), curve.point(unique.back())) > tolerance) unique.push_back(t);
    out.parameters = std::move(unique);
    std::sort(out.coincident.begin(), out.coincident.end(), [](const Interval &a, const Interval &b) { return a.lo < b.lo; });
    std::vector<Interval> merged;
    for (const Interval &piece : out.coincident) {
        if (!merged.empty() && piece.lo <= merged.back().hi + 1e-12 * (1.0 + std::fabs(piece.lo))) merged.back().hi = std::max(merged.back().hi, piece.hi);
        else merged.push_back(piece);
    }
    out.coincident = std::move(merged);
    out.parameters.erase(std::remove_if(out.parameters.begin(), out.parameters.end(), [&](double t) {
        for (const Interval &piece : out.coincident)
            if (t > piece.lo && t < piece.hi) return true;
        return false;
    }), out.parameters.end());
}

}

bool hasImplicitEquation(const Surface &surface) {
    const SurfaceType type = surface.type();
    return type == SurfaceType::Sphere || type == SurfaceType::Cone || type == SurfaceType::Torus;
}

CurveSurfaceIntersection implicitCurveSurface(const Curve<3> &curve, const Interval &range, const Surface &surface, double tolerance,
                                              bool *touching) {
    CurveSurfaceIntersection out;
    bool anyTouching = false;
    // Sistema locale: origine nel centro (sfera, toro) o nel vertice (cono), z sull'asse.
    Frame3 frame;
    if (surface.type() == SurfaceType::Sphere) frame = static_cast<const SphericalSurface &>(surface).frame();
    else if (surface.type() == SurfaceType::Torus) frame = static_cast<const ToroidalSurface &>(surface).frame();
    else if (surface.type() == SurfaceType::Cone) {
        const auto &cone = static_cast<const ConicalSurface &>(surface);
        frame = Frame3(cone.apex(), cone.frame().zDir(), cone.frame().xDir());
    } else {
        throw std::domain_error("implicitCurveSurface: superficie senza equazione implicita");
    }
    for (const BSplineCurve<3> &piece : rationalBezierPieces(curve, range)) {
        const int count = piece.poleCount();
        std::vector<double> xs(count), ys(count), zs(count), ws(count);
        double largestWeight = 0.0, largestRho = 0.0;
        for (int i = 0; i < count; ++i) {
            const double w = piece.weight(i);
            const Vec3 local = frame.toLocal(piece.poles()[i]);
            xs[i] = w * local.x();
            ys[i] = w * local.y();
            zs[i] = w * local.z();
            ws[i] = w;
            largestWeight = std::max(largestWeight, w);
            largestRho = std::max(largestRho, std::hypot(local.x(), local.y()));
        }
        const BernsteinPolynomial x(xs), y(ys), z(zs), w(ws);
        BernsteinPolynomial f;
        double gradient = 1.0;  // |grad F| sulla superficie, per la tolleranza sui valori
        int degree = 2;
        switch (surface.type()) {
        case SurfaceType::Sphere: {
            const double r = static_cast<const SphericalSurface &>(surface).radius();
            f = x * x + y * y + z * z - w * w * (r * r);
            gradient = 2.0 * r;
            break;
        }
        case SurfaceType::Cone: {
            const double k = std::tan(static_cast<const ConicalSurface &>(surface).semiAngle());
            f = x * x + y * y - z * z * (k * k);
            gradient = 2.0 * std::max(largestRho, tolerance);
            break;
        }
        default: {
            const auto &torus = static_cast<const ToroidalSurface &>(surface);
            const double big = torus.majorRadius(), small = torus.minorRadius();
            const BernsteinPolynomial q = x * x + y * y + z * z + w * w * (big * big - small * small);
            f = q * q - w * w * (x * x + y * y) * (4.0 * big * big);
            gradient = 8.0 * big * small * std::max(largestRho, tolerance);
            degree = 4;
            break;
        }
        }
        const double valueTolerance = 0.2 * tolerance * gradient * std::pow(largestWeight, degree);
        const Interval dom = piece.domain();
        double largestCoefficient = 0.0;
        for (double c : f.coefficients()) largestCoefficient = std::max(largestCoefficient, std::fabs(c));
        if (largestCoefficient <= valueTolerance) {
            // Tratto sulla superficie (un polinomio nullo su un intervallo e' nullo ovunque).
            const double a = curveParameter(curve, range, piece, dom.lo), b = curveParameter(curve, range, piece, dom.hi);
            out.coincident.push_back({std::min(a, b), std::max(a, b)});
            continue;
        }
        for (double s : rootsAndTouchings(f, valueTolerance, anyTouching)) {
            const double t = dom.lo + s * dom.length();
            out.parameters.push_back(curveParameter(curve, range, piece, t));
        }
    }
    // Rifinitura con Newton su F(C(t)) nel parametro della curva: le radici
    // di Bernstein su un tratto lungo hanno solo la precisione relativa del tratto.
    auto implicitValue = [&](const Vec3 &p, Vec3 &gradient) {
        const Vec3 x = frame.toLocal(p);
        switch (surface.type()) {
        case SurfaceType::Sphere: {
            const double r = static_cast<const SphericalSurface &>(surface).radius();
            gradient = frame.directionToGlobal(2.0 * x);
            return squaredNorm(x) - r * r;
        }
        case SurfaceType::Cone: {
            const double k = std::tan(static_cast<const ConicalSurface &>(surface).semiAngle());
            gradient = frame.directionToGlobal(Vec3(2.0 * x.x(), 2.0 * x.y(), -2.0 * k * k * x.z()));
            return x.x() * x.x() + x.y() * x.y() - k * k * x.z() * x.z();
        }
        default: {
            const auto &torus = static_cast<const ToroidalSurface &>(surface);
            const double big = torus.majorRadius(), small = torus.minorRadius();
            const double q = squaredNorm(x) + big * big - small * small;
            gradient = frame.directionToGlobal(4.0 * q * x - 8.0 * big * big * Vec3(x.x(), x.y(), 0.0));
            return q * q - 4.0 * big * big * (x.x() * x.x() + x.y() * x.y());
        }
        }
    };
    for (double &t : out.parameters) {
        for (int iteration = 0; iteration < 8; ++iteration) {
            Vec3 c[2], gradient;
            curve.evaluate(t, 1, c);
            const double value = implicitValue(c[0], gradient), slope = dot(gradient, c[1]);
            if (!(std::fabs(slope) > 1e-300)) break;
            const double next = range.clamp(t - value / slope);
            Vec3 g2;
            if (std::fabs(implicitValue(curve.point(next), g2)) >= std::fabs(value)) break;
            t = next;
        }
    }
    sortAndMerge(out, curve, tolerance);
    if (touching) *touching = anyTouching;
    return out;
}

Box patchBox(const BSplineSurface &patch) {
    Box box;
    for (int i = 0; i < patch.uPoleCount(); ++i)
        for (int j = 0; j < patch.vPoleCount(); ++j) box.add(patch.pole(i, j));
    return box;
}

std::pair<BSplineSurface, BSplineSurface> splitPatch(const BSplineSurface &patch, bool alongU) {
    const int nu = patch.uPoleCount(), nv = patch.vPoleCount();
    const int lines = alongU ? nv : nu, length = alongU ? nu : nv, degree = alongU ? patch.uDegree() : patch.vDegree();
    const std::vector<double> &knots = alongU ? patch.uKnots() : patch.vKnots();
    const Interval dom = alongU ? patch.uDomain() : patch.vDomain();
    const double middle = 0.5 * (dom.lo + dom.hi);
    std::vector<BSplineCurve<3>> first, second;
    for (int line = 0; line < lines; ++line) {
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int k = 0; k < length; ++k) {
            const int i = alongU ? k : line, j = alongU ? line : k;
            poles.push_back(patch.pole(i, j));
            if (patch.isRational()) weights.push_back(patch.weight(i, j));
        }
        const std::vector<BSplineCurve<3>> halves = BSplineCurve<3>(degree, knots, std::move(poles), std::move(weights)).insertKnot(middle, degree).bezierSegments();
        first.push_back(halves.front());
        second.push_back(halves.back());
    }
    auto assemble = [&](const std::vector<BSplineCurve<3>> &curves) {
        const int count = curves.front().poleCount();
        const int newU = alongU ? count : nu, newV = alongU ? nv : count;
        std::vector<Vec3> poles(std::size_t(newU * newV));
        std::vector<double> weights;
        if (patch.isRational()) weights.resize(poles.size());
        for (int line = 0; line < lines; ++line)
            for (int k = 0; k < count; ++k) {
                const int i = alongU ? k : line, j = alongU ? line : k;
                poles[std::size_t(i * newV + j)] = curves[std::size_t(line)].poles()[k];
                if (patch.isRational()) weights[std::size_t(i * newV + j)] = curves[std::size_t(line)].weight(k);
            }
        return BSplineSurface(patch.uDegree(), patch.vDegree(), alongU ? curves.front().knots() : patch.uKnots(),
                              alongU ? patch.vKnots() : curves.front().knots(), newU, newV, std::move(poles), std::move(weights));
    };
    return {assemble(first), assemble(second)};
}

namespace {

struct NumericSearch {
    const Curve<3> &curve;
    Interval range;
    const Surface &surface;
    double tolerance, scale;
    CurveSurfaceIntersection &out;
    bool touching = false;
    int leaves = 0;

    bool newton(const BSplineCurve<3> &piece, const BSplineSurface &patch, double &s, double &u, double &v) const {
        const Interval ds = piece.domain(), du = patch.uDomain(), dv = patch.vDomain();
        for (int iteration = 0; iteration < 30; ++iteration) {
            Vec3 c[2], p[4];
            piece.evaluate(s, 1, c);
            patch.evaluate(u, v, 1, p);
            const Vec3 f = c[0] - p[0];
            if (norm(f) <= 1e-3 * tolerance) return true;
            const Vec3 &cs = c[1], &pu = p[Surface::derivativeIndex(1, 0, 1)], &pv = p[Surface::derivativeIndex(0, 1, 1)];
            // [C' -Pu -Pv] (ds, du, dv) = -f: Cramer.
            const Vec3 a = cs, b = -pu, e = -pv;
            const double det = dot(a, cross(b, e));
            if (!(std::fabs(det) > 1e-300)) return false;
            const double x = dot(-f, cross(b, e)) / det, y = dot(a, cross(-f, e)) / det, z = dot(a, cross(b, -f)) / det;
            s = ds.clamp(s + x);
            u = du.clamp(u + y);
            v = dv.clamp(v + z);
        }
        Vec3 c[1], p[1];
        piece.evaluate(s, 0, c);
        patch.evaluate(u, v, 0, p);
        return distance(c[0], p[0]) <= tolerance;
    }

    void leaf(const BSplineCurve<3> &piece, const BSplineSurface &patch) {
        if (++leaves > 200000) throw std::domain_error("numericCurveSurface: troppe suddivisioni");
        const Interval ds = piece.domain(), du = patch.uDomain(), dv = patch.vDomain();
        double s = 0.5 * (ds.lo + ds.hi), u = 0.5 * (du.lo + du.hi), v = 0.5 * (dv.lo + dv.hi);
        const bool converged = newton(piece, patch, s, u, v);
        const Vec3 x = piece.point(converged ? s : 0.5 * (ds.lo + ds.hi));
        if (!converged) {
            // Newton non converge nelle tangenze: basta la distanza dalla superficie.
            if (projectPoint(surface, x).distance > tolerance) return;
            touching = true;
        } else if (projectPoint(surface, x).distance > tolerance) {
            return;
        }
        out.parameters.push_back(curveParameter(curve, range, piece, converged ? s : 0.5 * (ds.lo + ds.hi)));
    }

    void search(const BSplineCurve<3> &piece, const BSplineSurface &patch, int depth) {
        Box curveBox;
        for (const Vec3 &p : piece.poles()) curveBox.add(p);
        const Box surfaceBox = patchBox(patch);
        if (!curveBox.padded(tolerance).overlaps(surfaceBox)) return;
        const double dc = curveBox.diagonal(), dsurf = surfaceBox.diagonal();
        if ((dc <= 1e-4 * scale && dsurf <= 1e-4 * scale) || depth > 60) {
            leaf(piece, patch);
            return;
        }
        if (dc >= dsurf) {
            const Interval dom = piece.domain();
            const std::vector<BSplineCurve<3>> halves = piece.insertKnot(0.5 * (dom.lo + dom.hi), piece.degree()).bezierSegments();
            search(halves.front(), patch, depth + 1);
            search(halves.back(), patch, depth + 1);
        } else {
            // Si divide nella direzione in cui la pezza e' piu' lunga.
            const double uLength = distance(patch.pole(patch.uPoleCount() - 1, 0), patch.pole(0, 0))
                                 + distance(patch.pole(patch.uPoleCount() - 1, patch.vPoleCount() - 1), patch.pole(0, patch.vPoleCount() - 1));
            const double vLength = distance(patch.pole(0, patch.vPoleCount() - 1), patch.pole(0, 0))
                                 + distance(patch.pole(patch.uPoleCount() - 1, patch.vPoleCount() - 1), patch.pole(patch.uPoleCount() - 1, 0));
            const bool alongU = uLength >= vLength;
            const auto [a, b] = splitPatch(patch, alongU);
            search(piece, a, depth + 1);
            search(piece, b, depth + 1);
        }
    }
};

}

CurveSurfaceIntersection numericCurveSurface(const Curve<3> &curve, const Interval &range, const Surface &surface, double tolerance,
                                             bool *touching) {
    CurveSurfaceIntersection out;
    const Interval u = surface.uDomain(), v = surface.vDomain();
    if (!u.isFinite() || !v.isFinite()) throw std::domain_error("numericCurveSurface: superficie illimitata");
    const BSplineSurface nurbs = toBSplineSurface(surface, u, v);
    const std::vector<BSplineCurve<3>> pieces = rationalBezierPieces(curve, range);
    Box all;
    for (int i = 0; i < nurbs.uPoleCount(); ++i)
        for (int j = 0; j < nurbs.vPoleCount(); ++j) all.add(nurbs.pole(i, j));
    for (const BSplineCurve<3> &piece : pieces)
        for (const Vec3 &p : piece.poles()) all.add(p);
    NumericSearch search{curve, range, surface, tolerance, std::max(all.diagonal(), 1e-9), out};
    const std::vector<BSplineSurface> patches = nurbs.bezierPatches();
    for (const BSplineCurve<3> &piece : pieces) {
        // Tratto che giace sulla superficie: la suddivisione non finirebbe mai.
        bool on = true;
        const Interval dom = piece.domain();
        for (int i = 0; i <= 8 && on; ++i) on = projectPoint(surface, piece.point(dom.lo + dom.length() * i / 8.0)).distance <= tolerance;
        if (on) {
            const double a = curveParameter(curve, range, piece, dom.lo), b = curveParameter(curve, range, piece, dom.hi);
            out.coincident.push_back({std::min(a, b), std::max(a, b)});
            continue;
        }
        for (const BSplineSurface &patch : patches) search.search(piece, patch, 0);
    }
    sortAndMerge(out, curve, tolerance);
    if (touching) *touching = search.touching;
    return out;
}

}
