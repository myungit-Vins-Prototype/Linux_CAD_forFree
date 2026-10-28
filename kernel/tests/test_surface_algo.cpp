#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <gp_Ax1.hxx>

#include <cstring>
#include <future>
#include "fk_cancel.h"
#include "fk_surface_distance.h"
#include "fk_surface_algo.h"
#include "fk_nurbs.h"
#include "fk_curve_ops.h"
#include "fk_test_util.h"

using namespace fktest;

FK_TEST(InvertPointKeepsClosedBSplineBranch) {
    const CylindricalSurface cylinder(Frame3(), 3.0);
    const BSplineSurface source = toBSplineSurface(cylinder, {0.0, kTwoPi}, {0.0, 2.0});
    // Stesso cilindro, prima chiuso in u e poi in v. Le B-spline restano
    // non periodiche: entrambi i bordi sono rappresentazioni valide del seam.
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int j = 0; j < source.vPoleCount(); ++j)
        for (int i = 0; i < source.uPoleCount(); ++i) {
            poles.push_back(source.pole(i, j));
            weights.push_back(source.weight(i, j));
        }
    const BSplineSurface transposed(source.vDegree(), source.uDegree(), source.vKnots(), source.uKnots(),
                                    source.vPoleCount(), source.uPoleCount(), poles, weights);
    for (int axis : {0, 1}) {
        const BSplineSurface &surface = axis == 0 ? source : transposed;
        const Interval domain = axis == 0 ? surface.uDomain() : surface.vDomain();
        for (bool high : {false, true}) {
            // Punto appena oltre il seam rispetto al ramo della stima. La
            // proiezione globale preferisce l'altro bordo; quello vicino
            // alla stima e' comunque entro la tolleranza dell'edge.
            Vec2 target(1.0, 1.0), guess(1.0, 1.0);
            target[axis] = high ? domain.lo + 1e-8 : domain.hi - 1e-8;
            guess[axis] = high ? domain.hi - 0.1 : domain.lo + 0.1;
            Vec3 point = surface.point(target[0], target[1]);
            point[0] *= 1.0001;
            point[1] *= 1.0001;
            FK_CHECK(invertPoint(surface, point, guess, 1e-3, 3.0));
            FK_CHECK(high ? guess[axis] > domain.hi - 0.01 : guess[axis] < domain.lo + 0.01);
            FK_CHECK(distance(point, surface.point(guess[0], guess[1])) <= 1e-3);
        }
    }
}

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

FK_TEST(PlanarEquivalentOfBSplineSurface) {
    // La loft passa da qui durante la rigenerazione delle sezioni. Il buffer
    // delle derivate deve rispettare il contratto quadrato di Surface::evaluate.
    const BSplineSurface surface(1, 1, {0, 0, 1, 1}, {0, 0, 1, 1}, 2, 2,
                                 {Vec3(0, 0, 2), Vec3(0, 3, 2), Vec3(4, 0, 2), Vec3(4, 3, 2)});
    const SurfacePtr equivalent = planarEquivalent(surface);
    FK_CHECK(equivalent && equivalent->type() == SurfaceType::Plane);
    FK_CHECK(near(equivalent->point(0.0, 0.0), Vec3(2, 1.5, 2), 1e-14));
    FK_CHECK(near(equivalent->normal(0.0, 0.0), Vec3(0, 0, 1), 1e-14));
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

// Il meridiano sghembo mantiene v nella forma NURBS, anche se la finestra
// di proiezione ne copre solo una parte e u attraversa la cucitura.
FK_TEST(ProjectOnSkewRevolutionWindows) {
    auto curve = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0,0,0,0,1,1,1,1},
        std::vector<Vec3>{{2,1,-2},{-1,3,1},{3,-2,-1},{1,2,3}});
    const RevolutionSurface surface(curve, Vec3(), Vec3(0,0,1));
    for (Interval u : {Interval{5.8,7.0}, Interval{-8,-8+kTwoPi}, Interval{0,kTwoPi}}) {
        const Interval v{0.3,0.7};
        for (int i=0; i<=8; ++i) for (int j=0; j<=8; ++j) {
            const Vec3 p = surface.point(u.lo+u.length()*i/8.0, v.lo+v.length()*j/8.0);
            const auto result = projectPoint(surface,p,u,v);
            FK_CHECK(result.distance <= 1e-9);
            FK_CHECK(u.contains(result.u) && v.contains(result.v));
        }
        // Un punto fuori dalla finestra: confronto con la ricerca esaustiva
        // sui campioni, che fornisce un limite superiore alla distanza minima.
        const Vec3 p = surface.point(u.lo+0.2,0.05);
        const auto result = projectPoint(surface,p,u,v);
        FK_CHECK(u.contains(result.u) && v.contains(result.v));
        for(int i=0;i<=20;++i) for(int j=0;j<=20;++j)
            FK_CHECK(result.distance <= distance(p,surface.point(u.lo+u.length()*i/20.0,v.lo+v.length()*j/20.0))+1e-9);
    }
}

FK_TEST(SameExtrusionSurfaceRequiresWholeCurveCertificate) {
    // Ogni campata cubica interseca il piano all'inizio, a meta' e alla fine:
    // tre campioni la scambiavano per la curva piatta, pur discostandosi di 0.28125.
    std::vector<double> knots(4, 0.0);
    std::vector<Vec3> wave{{0, 0, 0}}, flat{{0, 0, 0}};
    for (int i = 0; i < 40; ++i) {
        wave.insert(wave.end(), {{i + 1.0 / 3, 1, 0}, {i + 2.0 / 3, -1, 0}, {double(i + 1), 0, 0}});
        flat.insert(flat.end(), {{i + 1.0 / 3, 0, 0}, {i + 2.0 / 3, 0, 0}, {double(i + 1), 0, 0}});
        knots.insert(knots.end(), i == 39 ? 4 : 3, double(i + 1));
    }
    const auto a = std::make_shared<BSplineCurve<3>>(3, knots, wave);
    const auto b = std::make_shared<BSplineCurve<3>>(3, knots, flat);
    const ExtrusionSurface waved(a, Vec3(0, 0, 1)), plane(b, Vec3(0, 0, 1));
    FK_CHECK_NEAR(projectPoint(plane, waved.point(0.25, 0)).distance, 0.28125, 1e-12);
    FK_CHECK(!sameSurface(waved, plane, 1e-9));
    FK_CHECK(!sameSurface(plane, waved, 1e-9));
    FK_CHECK(sameSurface(waved, ExtrusionSurface(translatedCurve<3>(a, Vec3(0, 0, 7)), Vec3(0, 0, -1)), 1e-9));
    FK_CHECK(sameSurface(waved, ExtrusionSurface(reversedCurve<3>(a), Vec3(0, 0, 1)), 1e-9));
    FK_CHECK(sameSurface(waved, ExtrusionSurface(std::make_shared<BSplineCurve<3>>(a->insertKnot(0.25)), Vec3(0, 0, 1)), 1e-9));
    FK_CHECK(!sameSurface(waved, ExtrusionSurface(translatedCurve<3>(a, Vec3(0, 0.01, 0)), Vec3(0, 0, 1)), 1e-9));

    const auto rational = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0,0,0,0,1,1,1,1},
        std::vector<Vec3>{{0,0,0},{1,2,0},{3,2,0},{4,0,0}}, std::vector<double>{1,0.5,2,1});
    const ExtrusionSurface curved(rational, Vec3(0, 0, 1));
    FK_CHECK(sameSurface(curved, ExtrusionSurface(std::make_shared<BSplineCurve<3>>(rational->insertKnot(0.5)), Vec3(0, 0, 1)), 1e-9));
    FK_CHECK(sameSurface(curved, ExtrusionSurface(translatedCurve<3>(rational, Vec3(0,0,10)), Vec3(0, 0, 1)), 1e-9));
}

namespace {
BSplineSurface certificatePlane(bool rational) {
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j) {
            poles.push_back(Vec3(i, j, 2));
            if (rational) weights.push_back(0.1 * (i + 1) * (j + 1));
        }
    return BSplineSurface(3, 2, {0,0,0,0,1,1,1,1}, {0,0,0,1,1,1}, 4, 3, poles, weights);
}
}

FK_TEST(SurfaceDeviationCertificateAnalytic) {
    for (bool rational : {false, true}) {
        const auto surface = certificatePlane(rational);
        for (double tolerance : {0.0, 1e-14, 1e-7, 1e-4})
            for (double offset : {0.0, .25, .75, 1.0 - 1e-6, 1.0, 1.0 + 1e-6, 2.0, 1e6}) {
                Vec3 p = surface.point(.37, .68);
                p[2] += tolerance == 0.0 ? offset : tolerance * offset;
                Vec2 hint(-100, 100);
                const auto exact = projectPoint(surface, p);
                const double actual = surfaceDeviationAboveTolerance(surface, p, tolerance, &hint);
                FK_CHECK((actual == 0.0) == (exact.distance <= tolerance));
                const double expected = exact.distance <= tolerance ? 0.0 : std::fabs(p[2] - 2.0);
                // Il proiettore originale ferma il branch-and-bound a
                // 1e-12 * max(1, distanza), anche per punti molto lontani.
                FK_CHECK_NEAR(actual, expected, 2e-12 * std::max(1.0, expected));
                if (actual > 0.0) FK_CHECK(std::memcmp(&actual, &exact.distance, sizeof(double)) == 0);
                FK_CHECK(surface.uDomain().contains(hint[0]) && surface.vDomain().contains(hint[1]));
                FK_CHECK(surfaceDeviationAboveTolerance(surface, p, tolerance) == actual);
            }
        // Fuori dal rettangolo: il certificato non deve accettare il
        // prolungamento della superficie al di la' dei parametri ammessi.
        Vec2 hint(-1, -.5);
        const Vec3 outside(-1, -.5, 2);
        FK_CHECK_NEAR(surfaceDeviationAboveTolerance(surface, outside, 1e-7, &hint), std::sqrt(1.25), 1e-12);
    }
}

FK_TEST(SurfaceDeviationCertificateCurvedAndHints) {
    std::mt19937 rng(1273);
    for (int trial = 0; trial < 48; ++trial) {
        const auto surface = randomBSplineSurface(rng, trial % 2);
        const auto u = surface.uDomain(), v = surface.vDomain();
        Vec2 hint(std::numeric_limits<double>::quiet_NaN(), 0);
        for (int k = 0; k < 8; ++k) {
            Vec3 p = surface.point(u.lo + u.length() * k / 7.0, v.lo + v.length() * .43);
            if (k % 3 == 1) p += randomVec<3>(rng, 10.0);
            if (k % 3 == 2) p += randomVec<3>(rng, 1e-8);
            const double tolerance = trial % 3 ? 1e-7 : 1e-4;
            const auto exact = projectPoint(surface, p);
            const double actual = surfaceDeviationAboveTolerance(surface, p, tolerance, &hint);
            FK_CHECK((actual == 0.0) == (exact.distance <= tolerance));
            if (actual > 0.0) FK_CHECK(std::memcmp(&actual, &exact.distance, sizeof(double)) == 0);
            else FK_CHECK(distance(p, surface.point(hint[0], hint[1])) <= tolerance);
        }
    }
}

FK_TEST(SurfaceDeviationCertificateDegenerateConcurrentAndCancelled) {
    const BSplineSurface collapsed(1, 1, {0,0,1,1}, {0,0,1,1}, 2, 2, std::vector<Vec3>(4, Vec3(1,2,3)));
    FK_CHECK(surfaceDeviationAboveTolerance(collapsed, Vec3(1,2,3), 1e-7) == 0.0);
    FK_CHECK_NEAR(surfaceDeviationAboveTolerance(collapsed, Vec3(2,3,4), 1e-7), std::sqrt(3.), 1e-12);
    const auto surface = certificatePlane(true);
    auto request = [&] {
        Vec2 hint(.5, .5);
        for (int k = 0; k <= 64; ++k) {
            const auto p = surface.point(k / 64., .3);
            if (surfaceDeviationAboveTolerance(surface, p, 1e-7, &hint) != 0.0) return false;
        }
        return true;
    };
    auto first = std::async(std::launch::async, request);
    FK_CHECK(request());
    FK_CHECK(first.get());
    for (double tolerance : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        bool rejected = false;
        try { (void)surfaceDeviationAboveTolerance(surface, Vec3(), tolerance); }
        catch (const std::invalid_argument &) { rejected = true; }
        FK_CHECK(rejected);
    }
    bool rejected = false;
    try { (void)surfaceDeviationAboveTolerance(surface, Vec3(std::numeric_limits<double>::quiet_NaN(),0,0), 1e-7); }
    catch (const std::invalid_argument &) { rejected = true; }
    FK_CHECK(rejected);
    auto token = std::make_shared<std::atomic<bool>>(true);
    bool cancelled = false;
    {
        CancellationScope scope(token);
        try { (void)surfaceDeviationAboveTolerance(surface, Vec3(), 1e-7); }
        catch (const OperationCancelled &) { cancelled = true; }
    }
    FK_CHECK(cancelled);
    FK_CHECK(request());
}
