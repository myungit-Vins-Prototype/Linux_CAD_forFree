#include "fk_surface_distance.h"

#include <cmath>
#include <stdexcept>

#include "fk_cancel.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {

// La distanza di un punto concreto della superficie e' un limite superiore
// certo del minimo. Usiamo una soglia interna prudente; i casi vicini al
// confine e i tentativi singolari restano al proiettore globale precedente.
double surfaceDeviationAboveTolerance(const Surface &surface, const Vec3 &p, double tolerance, Vec2 *hint) {
    cancellationPoint();
    if (!(tolerance >= 0.0) || !std::isfinite(tolerance) || !isFinite(p))
        throw std::invalid_argument("surfaceDeviationAboveTolerance: punto o tolleranza non validi");
    if (surface.type() == SurfaceType::BSpline && tolerance >= kLinearResolution) {
        const Interval u = surface.uDomain(), v = surface.vDomain();
        Vec2 uv = hint && isFinite(*hint) ? *hint : Vec2(0.5 * u.lo + 0.5 * u.hi, 0.5 * v.lo + 0.5 * v.hi);
        uv[0] = u.clamp(uv[0]);
        uv[1] = v.clamp(uv[1]);
        for (int iteration = 0; iteration < 12; ++iteration) {
            cancellationPoint();
            Vec3 d[4];
            surface.evaluate(uv[0], uv[1], 1, d);
            const Vec3 r = p - d[0];
            // Conferma con il valutatore del solo punto: il certificato non
            // dipende dall'uguaglianza numerica fra ordini di derivazione.
            if (norm(r) <= 0.5 * tolerance && distance(p, surface.point(uv[0], uv[1])) <= 0.5 * tolerance) {
                if (hint) *hint = uv;
                return 0.0;
            }
            const Vec3 &su = d[Surface::derivativeIndex(1, 0, 1)], &sv = d[Surface::derivativeIndex(0, 1, 1)];
            const double aa = dot(su, su), ab = dot(su, sv), bb = dot(sv, sv), det = aa * bb - ab * ab;
            if (!(det > 1e-24 * aa * bb) || !(aa > 0.0)) break;
            const double ru = dot(su, r), rv = dot(sv, r);
            const double du = (bb * ru - ab * rv) / det, dv = (aa * rv - ab * ru) / det;
            if (!std::isfinite(du) || !std::isfinite(dv)) break;
            const Vec2 next(u.clamp(uv[0] + du), v.clamp(uv[1] + dv));
            if (next[0] == uv[0] && next[1] == uv[1]) break;
            uv = next;
        }
    }
    const auto projection = projectPoint(surface, p);
    if (hint) *hint = Vec2(projection.u, projection.v);
    return projection.distance <= tolerance ? 0.0 : projection.distance;
}

}
