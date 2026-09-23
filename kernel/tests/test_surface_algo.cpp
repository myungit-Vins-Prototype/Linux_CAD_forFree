#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <gp_Ax1.hxx>

#include "fk_surface_algo.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

// Minimo della distanza su una griglia fitta: il vero minimo non puo' essere
// maggiore, quindi il risultato della proiezione non deve superarlo.
double gridMinimum(const Surface &surface, const Vec3 &p, const Interval &uRange, const Interval &vRange, int n = 80) {
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= n; ++j)
            best = std::min(best, distance(surface.point(uRange.lo + uRange.length() * i / n,
                                                         vRange.lo + vRange.length() * j / n), p));
    return best;
}

double relative(double value) { return 1e-9 * std::max(1.0, value); }

// Controlli comuni: punto coerente con (u, v), dentro la pezza, non peggiore
// della griglia e (se OCCT trova qualcosa) non peggiore di OCCT.
void checkProjection(const Surface &surface, const Handle(Geom_Surface) &reference, const Vec3 &p,
                     const Interval &uRange, const Interval &vRange, const Interval &uGrid, const Interval &vGrid,
                     bool expectSameAsOcct) {
    const SurfaceProjection result = projectPoint(surface, p, uRange, vRange);
    FK_CHECK(uRange.contains(result.u) && vRange.contains(result.v));
    FK_CHECK(near(result.point, surface.point(result.u, result.v), 1e-14));
    FK_CHECK_NEAR(result.distance, distance(result.point, p), 1e-12);
    const double grid = gridMinimum(surface, p, uGrid, vGrid);
    FK_CHECK(result.distance <= grid + relative(grid));

    GeomAPI_ProjectPointOnSurf occt(toPnt(p), reference, uGrid.lo, uGrid.hi, vGrid.lo, vGrid.hi);
    if (occt.NbPoints() == 0) return;
    FK_CHECK(result.distance <= occt.LowerDistance() + relative(occt.LowerDistance()));
    if (expectSameAsOcct) FK_CHECK_NEAR(result.distance, occt.LowerDistance(), relative(occt.LowerDistance()));
}

Interval randomSubrange(std::mt19937 &rng, const Interval &domain) {
    const double a = uniform(rng, domain.lo, domain.hi), b = uniform(rng, domain.lo, domain.hi);
    return {std::min(a, b), std::max(a, b) + 1e-3};
}

}

FK_TEST(ProjectOnPlaneAndCylinder) {
    std::mt19937 rng(70);
    for (int trial = 0; trial < 100; ++trial) {
        const Frame3 frame = randomFrame(rng);
        const Plane plane(frame);
        const Vec3 p = randomVec<3>(rng, 200.0);
        const SurfaceProjection onPlane = projectPoint(plane, p);  // dominio infinito
        FK_CHECK_NEAR(onPlane.distance, std::fabs(dot(p - frame.origin(), frame.zDir())), 1e-12);
        const Interval u{-10.0, 5.0}, v{3.0, 40.0};
        const SurfaceProjection bounded = projectPoint(plane, p, u, v);
        FK_CHECK(bounded.distance <= gridMinimum(plane, p, u, v) + 1e-9);

        const double radius = uniform(rng, 1.0, 50.0);
        const CylindricalSurface cylinder(frame, radius);
        const SurfaceProjection onCylinder = projectPoint(cylinder, p);
        const Vec3 local = frame.toLocal(p);
        FK_CHECK_NEAR(onCylinder.distance, std::fabs(std::hypot(local.x(), local.y()) - radius), 1e-12);
        const Interval arc = randomSubrange(rng, cylinder.uDomain()), height{-20.0, 20.0};
        const SurfaceProjection onArc = projectPoint(cylinder, p, arc, height);
        FK_CHECK(onArc.distance <= gridMinimum(cylinder, p, arc, height) + 1e-9);
    }
}

FK_TEST(ProjectOnConeSphereTorus) {
    std::mt19937 rng(71);
    for (int trial = 0; trial < 150; ++trial) {
        const Frame3 frame = randomFrame(rng, 20.0);
        const Vec3 p = frame.origin() + randomVec<3>(rng, 80.0);
        const bool restricted = trial % 2 == 1;

        const double angle = uniform(rng, 0.1, 1.4) * (trial % 3 ? 1.0 : -1.0);
        const ConicalSurface cone(frame, angle, uniform(rng, 0.0, 20.0));
        const Handle(Geom_ConicalSurface) coneReference =
            new Geom_ConicalSurface(toAx3(frame), angle, cone.referenceRadius());
        const Interval coneV{-150.0, 150.0};
        const Interval coneU = restricted ? randomSubrange(rng, cone.uDomain()) : cone.uDomain();
        // Dominio v infinito quando non e' ristretto; la griglia usa una fascia finita.
        const Interval coneVRange = restricted ? randomSubrange(rng, coneV)
                                               : Interval{-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        checkProjection(cone, coneReference, p, coneU, coneVRange, coneU, restricted ? coneVRange : coneV, false);

        const SphericalSurface sphere(frame, uniform(rng, 1.0, 60.0));
        const Handle(Geom_SphericalSurface) sphereReference = new Geom_SphericalSurface(toAx3(frame), sphere.radius());
        const Interval sphereU = restricted ? randomSubrange(rng, sphere.uDomain()) : sphere.uDomain();
        const Interval sphereV = restricted ? randomSubrange(rng, sphere.vDomain()) : sphere.vDomain();
        checkProjection(sphere, sphereReference, p, sphereU, sphereV, sphereU, sphereV, !restricted);

        const double major = uniform(rng, 5.0, 50.0);
        const ToroidalSurface torus(frame, major, major * uniform(rng, 0.1, 0.9));
        const Handle(Geom_ToroidalSurface) torusReference =
            new Geom_ToroidalSurface(toAx3(frame), torus.majorRadius(), torus.minorRadius());
        const Interval torusU = restricted ? randomSubrange(rng, torus.uDomain()) : torus.uDomain();
        const Interval torusV = restricted ? randomSubrange(rng, torus.vDomain()) : torus.vDomain();
        checkProjection(torus, torusReference, p, torusU, torusV, torusU, torusV, !restricted);
    }
}

FK_TEST(ProjectOnBSplineSurface) {
    std::mt19937 rng(72);
    for (int trial = 0; trial < 150; ++trial) {
        const BSplineSurface surface = randomBSplineSurface(rng, trial % 2 == 1);
        const Handle(Geom_BSplineSurface) reference = toOcct(surface);
        const Interval u = surface.uDomain(), v = surface.vDomain();
        for (int i = 0; i < 4; ++i) {
            // Punti sopra la lamiera e punti lontani.
            const Vec3 p = i % 2 == 0 ? surface.point(uniform(rng, u.lo, u.hi), uniform(rng, v.lo, v.hi)) + randomVec<3>(rng, 10.0)
                                      : Vec3(uniform(rng, -50, 150), uniform(rng, -50, 150), uniform(rng, -80, 80));
            checkProjection(surface, reference, p, u, v, u, v, false);
        }
    }
}

FK_TEST(ProjectOnExtrusionAndRevolution) {
    std::mt19937 rng(73);
    for (int trial = 0; trial < 60; ++trial) {
        const auto curve = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, trial % 2 == 1));
        const Vec3 p = randomVec<3>(rng, 120.0);

        const ExtrusionSurface extrusion(curve, randomDirection(rng));
        const Handle(Geom_Surface) extrusionReference = new Geom_SurfaceOfLinearExtrusion(toOcct(*curve), toDir(extrusion.direction()));
        const Interval height{-60.0, 60.0};
        checkProjection(extrusion, extrusionReference, p, curve->domain(), height, curve->domain(), height, false);

        const RevolutionSurface revolution(curve, randomVec<3>(rng, 30.0), randomDirection(rng));
        const Handle(Geom_Surface) revolutionReference = new Geom_SurfaceOfRevolution(
            toOcct(*curve), gp_Ax1(toPnt(revolution.axisPoint()), toDir(revolution.axisDirection())));
        const Interval angle = trial % 3 ? revolution.uDomain() : randomSubrange(rng, revolution.uDomain());
        checkProjection(revolution, revolutionReference, p, angle, curve->domain(), angle, curve->domain(), false);
    }
}

// Meridiano piano (nel piano che contiene l'asse), anche a cavallo dell'asse:
// qui la proiezione si riduce esattamente al meridiano.
FK_TEST(ProjectOnPlanarRevolution) {
    std::mt19937 rng(74);
    for (int trial = 0; trial < 80; ++trial) {
        const Frame3 frame = randomFrame(rng, 30.0);  // asse = Z del sistema, meridiano nel piano XZ
        const int degree = uniformInt(rng, 1, 4), count = degree + 1 + uniformInt(rng, 0, 5);
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < count; ++i) {
            const double radius = uniform(rng, trial % 4 == 0 ? -20.0 : 2.0, 40.0);
            poles.push_back(frame.toGlobal(Vec3(radius, 0.0, uniform(rng, -40.0, 40.0))));
            if (trial % 2) weights.push_back(uniform(rng, 0.3, 3.0));
        }
        const auto meridian = std::make_shared<BSplineCurve<3>>(degree, randomKnots(rng, degree, count), poles, weights);
        const RevolutionSurface revolution(meridian, frame.origin(), frame.zDir());
        const Handle(Geom_Surface) reference = new Geom_SurfaceOfRevolution(
            toOcct(*meridian), gp_Ax1(toPnt(frame.origin()), toDir(frame.zDir())));
        const Vec3 p = frame.origin() + randomVec<3>(rng, 60.0);
        const Interval angle = trial % 3 ? revolution.uDomain() : randomSubrange(rng, revolution.uDomain());
        checkProjection(revolution, reference, p, angle, meridian->domain(), angle, meridian->domain(), false);
    }
}

FK_TEST(ProjectOnInfiniteExtrusion) {
    std::mt19937 rng(75);
    const double infinity = std::numeric_limits<double>::infinity();
    for (int trial = 0; trial < 60; ++trial) {
        const auto curve = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, trial % 2 == 0));
        const ExtrusionSurface extrusion(curve, randomDirection(rng));
        const Vec3 p = randomVec<3>(rng, 120.0);
        const SurfaceProjection result = projectPoint(extrusion, p, curve->domain(), Interval{-infinity, infinity});
        // Nessun punto di una fascia (larga abbastanza da contenere il minimo) e' piu' vicino.
        const Interval band{result.v - 200.0, result.v + 200.0};
        FK_CHECK(result.distance <= gridMinimum(extrusion, p, curve->domain(), band) + 1e-9);
        // Per u fisso il v scelto e' quello ottimo.
        FK_CHECK_NEAR(result.v, dot(p - curve->point(result.u), extrusion.direction()), 1e-9);
    }
}
