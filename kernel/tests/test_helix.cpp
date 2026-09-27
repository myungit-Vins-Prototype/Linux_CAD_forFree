#include <Geom2d_Line.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>

#include <cmath>

#include "fk_curve_algo.h"
#include "fk_helix.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

HelixSpec helixSpec(double radius, double pitch, double turns, double taper = 0.0, bool spiral = false, bool left = false, double start = 0.0) {
    HelixSpec spec;
    spec.frame = Frame3(Vec3(1, -2, 0.5), normalized(Vec3(0.2, 0.3, 1.0)), Vec3(1, 0, 0));
    spec.radius = radius;
    spec.pitch = pitch;
    spec.turns = turns;
    spec.taper = taper;
    spec.spiral = spiral;
    spec.leftHanded = left;
    spec.startAngle = start;
    return spec;
}

}

FK_TEST(HelixDerivativesAndOcctReference) {
    // Elica cilindrica e conica: i punti coincidono con la retta nello spazio (u, v)
    // della superficie OCCT (l'elica esatta di OCCT), le derivate con le differenze finite.
    for (double taper : {0.0, 0.3, -0.2})
        for (bool left : {false, true}) {
            const HelixSpec spec = helixSpec(3.0, 1.25, 4.0, taper, false, left, 0.4);
            const HelixCurve helix(spec);
            const gp_Ax3 axes(toPnt(spec.frame.origin()), toDir(spec.frame.zDir()), toDir(spec.frame.xDir()));
            const double sense = left ? -1.0 : 1.0;
            for (double t = 0.0; t <= helix.domain().hi; t += 0.37) {
                const double z = spec.pitch * t / kTwoPi;
                gp_Pnt reference;
                if (taper == 0.0) {
                    Handle(Geom_CylindricalSurface) cylinder = new Geom_CylindricalSurface(axes, spec.radius);
                    reference = cylinder->Value(spec.startAngle + sense * t, z);
                } else {
                    // Sul cono di OCCT v e' la distanza lungo la generatrice: z = v cos(a).
                    Handle(Geom_ConicalSurface) cone = new Geom_ConicalSurface(axes, taper, spec.radius);
                    reference = cone->Value(spec.startAngle + sense * t, z / std::cos(taper));
                }
                FK_CHECK(distance(helix.point(t), fromOcct(reference)) < 1e-12);
                Vec3 d[4];
                helix.evaluate(t, 3, d);
                const double h = 1e-5;
                for (int k = 1; k <= 3; ++k) {
                    Vec3 a[4], b[4];
                    helix.evaluate(t - h, 3, a);
                    helix.evaluate(t + h, 3, b);
                    FK_CHECK(distance((b[k - 1] - a[k - 1]) / (2 * h), d[k]) < 1e-7 * std::max(1.0, norm(d[k])));
                }
            }
        }
}

FK_TEST(HelixBSplineWithinTolerance) {
    // Elica cilindrica, conica, spirale; lunghezze esatte.
    const std::vector<HelixSpec> specs = {helixSpec(10.0, 2.0, 10.0), helixSpec(2.0, 0.5, 3.5, 0.25, false, true, 1.0),
                                          helixSpec(0.0, 1.5, 6.0, 0.0, true), helixSpec(4.0, 3.0, 2.25, -0.4)};
    for (const HelixSpec &spec : specs) {
        const HelixCurve helix(spec);
        const BSplineCurve<3> spline = helixBSpline(helix, 1e-9);
        FK_CHECK(spline.degree() == 5);
        FK_CHECK_NEAR(spline.domain().lo, 0.0, 0.0);
        FK_CHECK_NEAR(spline.domain().hi, helix.domain().hi, 1e-12);
        double worst = 0.0;
        const int samples = 4000;
        for (int i = 0; i <= samples; ++i) {
            const double t = helix.domain().hi * i / samples;
            worst = std::max(worst, distance(spline.point(t), helix.point(t)));
        }
        FK_CHECK(worst < 1e-9);
        // C2: derivate seconde continue nei nodi.
        for (double knot : spline.breakpoints(spline.domain())) {
            Vec3 a[3], b[3];
            spline.evaluateLeft(knot, 2, a);
            spline.evaluate(knot, 2, b);
            FK_CHECK(distance(a[2], b[2]) < 1e-8 * std::max(1.0, norm(b[2])));
        }
        const double exact = helix.length();
        FK_CHECK_NEAR(arcLength(spline, spline.domain(), 1e-12), exact, 1e-8 * exact);
        if (spec.taper == 0.0 && !spec.spiral)
            FK_CHECK_NEAR(exact, spec.turns * std::hypot(kTwoPi * spec.radius, spec.pitch), 1e-12 * exact);
    }
    FK_CHECK_THROWS(HelixCurve(helixSpec(1.0, 1.0, 5.0, -0.5)));  // il raggio si annullerebbe
    FK_CHECK_THROWS(HelixCurve(helixSpec(1.0, 0.0, 5.0)));
}
