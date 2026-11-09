#include "fk_surface_poles_conversion.h"
#include "fk_body_check.h"
#include "fk_cancel.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_intersect.h"
#include "fk_nurbs.h"
#include "fk_pcurve.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace ForgeCad::Kernel {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::domain_error(message);
}
BSplineSurface enrichLinear(BSplineSurface s, bool u) {
    const int degree = u ? s.uDegree() : s.vDegree(), count = u ? s.uPoleCount() : s.vPoleCount();
    if (degree != 1 || count != 2)
        return s;
    const int nu = u ? 4 : s.uPoleCount(), nv = u ? s.vPoleCount() : 4;
    std::vector<Vec3> points;
    std::vector<double> weights;
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            const double t = (u ? i : j) / 3.;
            const int a = u ? 0 : i, b = u ? j : 0, c = u ? 1 : i, d = u ? j : 1;
            const double wa = s.weight(a, b), wb = s.weight(c, d), w = (1 - t) * wa + t * wb;
            points.push_back(((1 - t) * wa * s.pole(a, b) + t * wb * s.pole(c, d)) / w);
            weights.push_back(w);
        }
    const auto domain = u ? s.uDomain() : s.vDomain();
    const std::vector<double> knots{domain.lo, domain.lo, domain.lo, domain.lo,
                                    domain.hi, domain.hi, domain.hi, domain.hi};
    return BSplineSurface(u ? 3 : s.uDegree(), u ? s.vDegree() : 3, u ? knots : s.uKnots(), u ? s.vKnots() : knots, nu,
                          nv, std::move(points), std::move(weights));
}
BSplineSurfacePtr support(const Body &uv, FaceId face) {
    const auto original = uv.face(face).surface;
    if (auto spline = std::dynamic_pointer_cast<const BSplineSurface>(original))
        return spline;
    require(bool(original), "Poli: faccia priva di supporto.");
    Box bounds;
    for (auto loop : uv.face(face).loops)
        for (auto fin : uv.loopFins(loop)) {
            const auto &use = uv.fin(fin);
            const auto &edge = uv.edge(use.edge);
            require(bool(use.pcurve), "Poli: rifilature originali senza coordinate UV.");
            bounds.add(curveBox(*embedCurve(use.pcurve, Frame3()), edge.range));
        }
    Interval u{bounds.lo.x(), bounds.hi.x()}, v{bounds.lo.y(), bounds.hi.y()};
    if (!(u.isFinite() && u.length() > 0))
        u = original->uDomain();
    if (!(v.isFinite() && v.length() > 0))
        v = original->vDomain();
    require(u.isFinite() && v.isFinite() && u.length() > 0 && v.length() > 0,
            "Poli: dominio analitico non finito o degenere.");
    auto spline = enrichLinear(enrichLinear(toBSplineSurface(*original, u, v), true), false);
    return std::make_shared<const BSplineSurface>(std::move(spline));
}
} // namespace
BSplineSurfacePtr surfacePoleSupport(const Body &body, FaceId face) {
    require(body.contains(face), "Poli: faccia non disponibile.");
    if (auto spline = std::dynamic_pointer_cast<const BSplineSurface>(body.face(face).surface))
        return spline;
    Body uv = body;
    require(computePCurves(uv) == 0, "Poli: rifilature originali non disponibili.");
    return support(uv, face);
}
Body prepareSurfacePoleBody(const Body &body, const std::vector<FaceId> &faces, double tolerance) {
    require(std::isfinite(tolerance) && tolerance >= 1e-9 && tolerance <= .01,
            "Poli: tolleranza di conversione non valida.");
    bool analytic = false;
    for (auto face : faces) {
        require(body.contains(face), "Poli: faccia non disponibile.");
        analytic = analytic || body.face(face).surface->type() != SurfaceType::BSpline;
    }
    if (!analytic)
        return body;
    Body result = body;
    require(computePCurves(result) == 0, "Poli: rifilature originali non disponibili.");
    std::set<int> seen;
    for (auto face : faces) {
        cancellationPoint();
        require(body.contains(face) && seen.insert(face.index).second, "Poli: faccia assente o duplicata.");
        const auto original = body.face(face).surface;
        if (original->type() == SurfaceType::BSpline)
            continue;
        const auto converted = support(result, face);
        result.face(face).surface = converted;
        if (original->type() == SurfaceType::Plane)
            continue;
        for (auto loop : body.face(face).loops)
            for (auto fin : body.loopFins(loop)) {
                auto &use = result.fin(fin);
                const auto &edge = body.edge(use.edge);
                const double allowed = std::max({tolerance, edge.tolerance, body.fin(fin).pcurveTolerance});
                auto curve = exactPCurve(*converted, edge.curve, edge.range, std::min(allowed, pCurveTolerance()));
                double deviation = 0;
                if (!curve)
                    curve =
                        fitPCurve(*converted, edge.curve, edge.range, std::min(allowed, pCurveTolerance()), &deviation);
                require(bool(curve), "Poli: conversione NURBS senza rifilatura valida.");
                deviation = pcurveDeviation(*converted, *edge.curve, *curve, edge.range, 32);
                require(deviation <= allowed, "Poli: la conversione NURBS supera la tolleranza del bordo.");
                use.pcurve = std::move(curve);
                use.pcurveTolerance = deviation;
            }
    }
    const auto issues = checkBody(result, {true, true});
    if (!issues.empty())
        throw std::domain_error("Poli: conversione NURBS non valida: " + issues.front().message);
    return result;
}
void checkSurfacePoleClosure(const BSplineSurface &original, const BSplineSurface &modified, double tolerance) {
    const auto u = original.uDomain(), v = original.vDomain();
    const auto point = [&](const BSplineSurface &s, int axis, double a, double b) {
        return axis == 0 ? s.point(a, b) : s.point(b, a);
    };
    for (int axis = 0; axis < 2; ++axis) {
        const auto across = axis == 0 ? u : v, along = axis == 0 ? v : u;
        bool closed = true;
        double gap = 0;
        for (int i = 0; i <= 32; ++i) {
            const double t = along.lo + along.length() * i / 32.;
            closed =
                closed && distance(point(original, axis, across.lo, t), point(original, axis, across.hi, t)) <= 1e-10;
            gap = std::max(gap, distance(point(modified, axis, across.lo, t), point(modified, axis, across.hi, t)));
        }
        require(!closed || gap <= tolerance,
                "Poli: la modifica apre la cucitura della superficie chiusa; attivare Mantieni i bordi connessi.");
        for (double edge : {across.lo, across.hi}) {
            const auto before = point(original, axis, edge, along.lo), after = point(modified, axis, edge, along.lo);
            bool collapsed = true;
            gap = 0;
            for (int i = 0; i <= 32; ++i) {
                const double t = along.lo + along.length() * i / 32.;
                collapsed = collapsed && distance(before, point(original, axis, edge, t)) <= 1e-10;
                gap = std::max(gap, distance(after, point(modified, axis, edge, t)));
            }
            require(!collapsed || gap <= tolerance,
                    "Poli: la modifica separa i poli di un lato degenere; attivare Mantieni i bordi connessi.");
        }
    }
}
} // namespace ForgeCad::Kernel
