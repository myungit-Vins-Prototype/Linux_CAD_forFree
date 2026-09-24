#include <GeomAPI_IntCS.hxx>
#include <Geom_Circle.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Line.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Ax1.hxx>

#include "fk_classify.h"
#include "fk_curve_surface.h"
#include "fk_intersect.h"
#include "fk_surface_algo.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

gp_Ax2 toAx2(const Frame3 &frame) { return gp_Ax2(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir())); }

struct Case {
    CurvePtr<3> curve;
    Interval range;
    Handle(Geom_Curve) occt;
};

// Punti OCCT (GeomAPI_IntCS) sul tratto, contro i nostri: ogni punto nostro
// sta su curva e superficie, ogni punto OCCT ha un nostro punto vicino.
void compare(const Case &c, const SurfacePtr &surface, const Handle(Geom_Surface) &occtSurface, int &total) {
    const CurveSurfaceIntersection ours = intersectCurveSurface(*c.curve, c.range, *surface, 1e-7);
    for (double t : ours.parameters) {
        FK_CHECK(c.range.contains(t, 1e-9));
        FK_CHECK(projectPoint(*surface, c.curve->point(t)).distance <= 1e-6);
    }
    GeomAPI_IntCS reference(new Geom_TrimmedCurve(c.occt, c.range.lo, c.range.hi), occtSurface);
    FK_CHECK(reference.IsDone());
    int found = 0;
    for (int i = 1; i <= reference.NbPoints(); ++i) {
        const Vec3 p = fromOcct(reference.Point(i));
        bool near = false;
        for (double t : ours.parameters) near = near || distance(c.curve->point(t), p) <= 1e-5;
        FK_CHECK(near);
        ++found;
    }
    // OCCT non perde punti trasversali qui: stesso numero.
    FK_CHECK(int(ours.parameters.size()) == found);
    total += found;
}

}

// Rette, cerchi, ellissi e B-spline contro sfere, coni, tori, superfici di
// rivoluzione e B-spline: equazione implicita o suddivisione, contro GeomAPI_IntCS.
FK_TEST(CurveSurfaceIntersectionsMatchOcct) {
    std::mt19937 rng(301);
    int total = 0;
    for (int trial = 0; trial < 12; ++trial) {
        const Frame3 frame(randomVec<3>(rng, 3.0), randomDirection(rng), randomDirection(rng));
        std::vector<std::pair<SurfacePtr, Handle(Geom_Surface)>> surfaces;
        const double r = uniform(rng, 3.0, 6.0);
        surfaces.push_back({std::make_shared<SphericalSurface>(frame, r), new Geom_SphericalSurface(gp_Ax3(toAx2(frame)), r)});
        const double angle = uniform(rng, 0.3, 1.1);
        surfaces.push_back({std::make_shared<ConicalSurface>(frame, angle, r), new Geom_ConicalSurface(gp_Ax3(toAx2(frame)), angle, r)});
        const double minor = uniform(rng, 1.0, 2.5);
        surfaces.push_back({std::make_shared<ToroidalSurface>(frame, r + minor + 0.5, minor),
                            new Geom_ToroidalSurface(gp_Ax3(toAx2(frame)), r + minor + 0.5, minor)});
        // Rivoluzione di un arco di cerchio fuori dall'asse.
        const Circle<3> meridian(frame.origin() + (r + 1.0) * frame.xDir(), frame.xDir(), frame.zDir(), 2.0);
        surfaces.push_back({std::make_shared<RevolutionSurface>(std::make_shared<Circle<3>>(meridian), frame.origin(), frame.zDir()),
                            new Geom_SurfaceOfRevolution(new Geom_TrimmedCurve(new Geom_Circle(gp_Ax2(toPnt(meridian.center()),
                                                                                                         toDir(cross(meridian.xAxis(), meridian.yAxis())),
                                                                                                         toDir(meridian.xAxis())), 2.0),
                                                                               0.0, kTwoPi),
                                                         gp_Ax1(toPnt(frame.origin()), toDir(frame.zDir())))});
        const BSplineSurface patch = randomBSplineSurface(rng, trial % 2 == 1, 3.0);
        surfaces.push_back({std::make_shared<BSplineSurface>(patch), toOcct(patch)});

        std::vector<Case> cases;
        const Vec3 o = randomVec<3>(rng, 8.0), d = randomDirection(rng);
        cases.push_back({std::make_shared<Line<3>>(o, d), {-30.0, 30.0}, new Geom_Line(toPnt(o), toDir(d))});
        const Frame3 cf(randomVec<3>(rng, 4.0), randomDirection(rng), randomDirection(rng));
        const double cr = uniform(rng, 2.0, 8.0);
        cases.push_back({std::make_shared<Circle<3>>(makeCircle(cf, cr)), {0.3, 5.5}, new Geom_Circle(toAx2(cf), cr)});
        cases.push_back({std::make_shared<Ellipse<3>>(makeEllipse(cf, cr + 2.0, cr)), {-1.0, 4.0}, new Geom_Ellipse(toAx2(cf), cr + 2.0, cr)});
        auto spline = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, trial % 3 == 0));
        cases.push_back({spline, spline->domain(), toOcct(*spline)});
        for (const auto &[surface, occt] : surfaces)
            for (const Case &c : cases) compare(c, surface, occt, total);
    }
    FK_CHECK(total > 100);
}

// Semirette per il punto-in-solido e la selezione su coni, tori e rivoluzioni.
FK_TEST(LineSurfaceIntersectionsOnNewSurfaces) {
    const Frame3 frame(Vec3(1, 2, 3), Vec3(0.2, 0.3, 1), Vec3(1, 0, 0));
    bool grazing = false;
    const ToroidalSurface torus(frame, 6.0, 2.0);
    // Retta lungo l'asse: nessuna intersezione. Retta nel piano equatoriale per il centro: 4.
    FK_CHECK(intersectLineSurface(frame.origin(), frame.zDir(), torus, grazing, 1e-7).empty());
    const std::vector<double> equator = intersectLineSurface(frame.origin() - 20.0 * frame.xDir(), frame.xDir(), torus, grazing, 1e-7);
    FK_CHECK(equator.size() == 4);
    if (equator.size() == 4) {
        FK_CHECK_NEAR(equator[0], 12.0, 1e-9);
        FK_CHECK_NEAR(equator[1], 16.0, 1e-9);
        FK_CHECK_NEAR(equator[2], 24.0, 1e-9);
        FK_CHECK_NEAR(equator[3], 28.0, 1e-9);
    }
    FK_CHECK(!grazing);
    // Tangente al tubo in alto: una radice doppia.
    intersectLineSurface(frame.origin() + 2.0 * frame.zDir() - 20.0 * frame.xDir(), frame.xDir(), torus, grazing, 1e-7);
    FK_CHECK(grazing);
    const ConicalSurface cone(frame, 0.5, 2.0);
    const std::vector<double> across = intersectLineSurface(frame.origin() - 10.0 * frame.xDir(), frame.xDir(), cone, grazing, 1e-7);
    FK_CHECK(across.size() == 2);
    if (across.size() == 2) {
        FK_CHECK_NEAR(across[0], 8.0, 1e-9);
        FK_CHECK_NEAR(across[1], 12.0, 1e-9);
    }
}
