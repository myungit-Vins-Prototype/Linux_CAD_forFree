#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <gp_Ax1.hxx>

#include "fk_surface.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

// Tutte le derivate con k + l <= 3 contro Geom_Surface::Value/DN di OCCT.
// La tolleranza (relativa) cresce di un fattore `growth` per ordine.
void compareWithOcct(const Surface &ours, const Handle(Geom_Surface) &reference, double u, double v,
                     double tolerance, double growth = 1.0) {
    constexpr int order = 3;
    Vec3 d[(order + 1) * (order + 1)];
    ours.evaluate(u, v, order, d);
    FK_CHECK(near(d[0], fromOcct(reference->Value(u, v)), tolerance));
    for (int k = 0; k <= order; ++k)
        for (int l = 0; k + l <= order; ++l)
            if (k + l > 0)
                FK_CHECK(near(d[Surface::derivativeIndex(k, l, order)], fromOcct(reference->DN(u, v, k, l)),
                              tolerance * std::pow(growth, k + l)));
    // Normale contro Su x Sv di OCCT.
    gp_Pnt p;
    gp_Vec su, sv;
    reference->D1(u, v, p, su, sv);
    const gp_Vec n = su.Crossed(sv);
    if (n.Magnitude() > 1e-9 * su.Magnitude() * sv.Magnitude())
        FK_CHECK(near(ours.normal(u, v), fromOcct(n.Normalized()), 1e-12));
}

// Le isoparametriche devono ripercorrere la superficie con lo stesso parametro.
void checkIsoCurves(const Surface &surface, double u, double v, double tolerance) {
    if (const CurvePtr<3> iso = surface.uIso(u)) FK_CHECK(near(iso->point(v), surface.point(u, v), tolerance));
    if (const CurvePtr<3> iso = surface.vIso(v)) FK_CHECK(near(iso->point(u), surface.point(u, v), tolerance));
}

}

FK_TEST(PlaneMatchesOcct) {
    std::mt19937 rng(50);
    for (int trial = 0; trial < 50; ++trial) {
        const Frame3 frame = randomFrame(rng);
        const Plane plane(frame);
        const Handle(Geom_Plane) reference = new Geom_Plane(toAx3(frame));
        for (int i = 0; i < 10; ++i) {
            const double u = uniform(rng, -500, 500), v = uniform(rng, -500, 500);
            compareWithOcct(plane, reference, u, v, 1e-14);
            checkIsoCurves(plane, u, v, 1e-14);
        }
    }
}

FK_TEST(CylinderMatchesOcct) {
    std::mt19937 rng(51);
    for (int trial = 0; trial < 50; ++trial) {
        const Frame3 frame = randomFrame(rng);
        const double radius = uniform(rng, 0.01, 100.0);
        const CylindricalSurface cylinder(frame, radius);
        const Handle(Geom_CylindricalSurface) reference = new Geom_CylindricalSurface(toAx3(frame), radius);
        for (int i = 0; i < 10; ++i) {
            const double u = uniform(rng, -7, 7), v = uniform(rng, -300, 300);
            compareWithOcct(cylinder, reference, u, v, 1e-14);
            checkIsoCurves(cylinder, u, v, 1e-14);
        }
    }
    FK_CHECK_THROWS(CylindricalSurface(Frame3(), 0.0));
}

FK_TEST(ConeMatchesOcct) {
    std::mt19937 rng(52);
    for (int trial = 0; trial < 50; ++trial) {
        const Frame3 frame = randomFrame(rng);
        const double angle = uniform(rng, 0.05, 1.5) * (trial % 2 ? 1.0 : -1.0);
        const double radius = trial % 5 == 0 ? 0.0 : uniform(rng, 0.01, 50.0);
        const ConicalSurface cone(frame, angle, radius);
        const Handle(Geom_ConicalSurface) reference = new Geom_ConicalSurface(toAx3(frame), angle, radius);
        FK_CHECK(near(cone.apex(), fromOcct(reference->Apex()), 1e-12));
        for (int i = 0; i < 10; ++i) {
            const double u = uniform(rng, -7, 7), v = uniform(rng, -100, 100);
            compareWithOcct(cone, reference, u, v, 1e-14);
            checkIsoCurves(cone, u, v, 1e-13);
        }
    }
    FK_CHECK_THROWS(ConicalSurface(Frame3(), 0.0, 1.0));
    FK_CHECK_THROWS(ConicalSurface(Frame3(), kHalfPi, 1.0));
}

FK_TEST(SphereMatchesOcct) {
    std::mt19937 rng(53);
    for (int trial = 0; trial < 50; ++trial) {
        const Frame3 frame = randomFrame(rng);
        const double radius = uniform(rng, 0.01, 100.0);
        const SphericalSurface sphere(frame, radius);
        const Handle(Geom_SphericalSurface) reference = new Geom_SphericalSurface(toAx3(frame), radius);
        for (int i = 0; i < 10; ++i) {
            const double u = uniform(rng, -7, 7), v = uniform(rng, -1.5, 1.5);
            compareWithOcct(sphere, reference, u, v, 1e-14);
            checkIsoCurves(sphere, u, v, 1e-13);
            FK_CHECK(near(sphere.normal(u, v), (sphere.point(u, v) - frame.origin()) / radius, 1e-13));
        }
    }
    FK_CHECK_THROWS(SphericalSurface(Frame3(), 1.0).normal(0.3, kHalfPi));  // polo
    FK_CHECK(SphericalSurface(Frame3(), 1.0).vIso(kHalfPi) == nullptr);
}

FK_TEST(TorusMatchesOcct) {
    std::mt19937 rng(54);
    for (int trial = 0; trial < 50; ++trial) {
        const Frame3 frame = randomFrame(rng);
        const double major = uniform(rng, 1.0, 80.0), minor = major * uniform(rng, 0.05, 1.5);
        const ToroidalSurface torus(frame, major, minor);
        const Handle(Geom_ToroidalSurface) reference = new Geom_ToroidalSurface(toAx3(frame), major, minor);
        for (int i = 0; i < 10; ++i) {
            const double u = uniform(rng, -7, 7), v = uniform(rng, -7, 7);
            compareWithOcct(torus, reference, u, v, 1e-14);
            checkIsoCurves(torus, u, v, 1e-13);
        }
    }
}

FK_TEST(ExtrusionMatchesOcct) {
    std::mt19937 rng(55);
    for (int trial = 0; trial < 100; ++trial) {
        const auto curve = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, trial % 2 == 1));
        const Vec3 direction = randomDirection(rng);
        const ExtrusionSurface surface(curve, direction);
        const Handle(Geom_SurfaceOfLinearExtrusion) reference =
            new Geom_SurfaceOfLinearExtrusion(toOcct(*curve), toDir(direction));
        for (int i = 0; i < 10; ++i) {
            const double u = uniform(rng, curve->domain().lo, curve->domain().hi), v = uniform(rng, -100, 100);
            compareWithOcct(surface, reference, u, v, 1e-12, 10.0);
            checkIsoCurves(surface, u, v, 1e-13);
        }
    }
}

FK_TEST(RevolutionMatchesOcct) {
    std::mt19937 rng(56);
    for (int trial = 0; trial < 100; ++trial) {
        const auto meridian = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, trial % 2 == 0));
        const Vec3 axisPoint = randomVec<3>(rng, 50.0), axisDirection = randomDirection(rng);
        const RevolutionSurface surface(meridian, axisPoint, axisDirection);
        const Handle(Geom_SurfaceOfRevolution) reference =
            new Geom_SurfaceOfRevolution(toOcct(*meridian), gp_Ax1(toPnt(axisPoint), toDir(axisDirection)));
        for (int i = 0; i < 10; ++i) {
            const double u = uniform(rng, -7, 7), v = uniform(rng, meridian->domain().lo, meridian->domain().hi);
            compareWithOcct(surface, reference, u, v, 1e-12, 10.0);
            checkIsoCurves(surface, u, v, 1e-12);
        }
    }
}
