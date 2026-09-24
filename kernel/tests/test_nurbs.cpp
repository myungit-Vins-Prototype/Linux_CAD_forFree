#include "fk_nurbs.h"
#include "fk_surface_algo.h"
#include "fk_curve_algo.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

// La NURBS sta sulla superficie e copre la finestra (anche parziale in u:
// verso di rotazione giusto).
void checkSurface(const Surface &surface, const Interval &u, const Interval &v) {
    const BSplineSurface nurbs = toBSplineSurface(surface, u, v);
    const Interval nu = nurbs.uDomain(), nv = nurbs.vDomain();
    double worst = 0.0;
    for (int i = 0; i <= 12; ++i)
        for (int j = 0; j <= 12; ++j) {
            const Vec3 p = nurbs.point(nu.lo + nu.length() * i / 12.0, nv.lo + nv.length() * j / 12.0);
            worst = std::max(worst, projectPoint(surface, p).distance);
        }
    FK_CHECK(worst <= 1e-9);
    double gap = 0.0;
    for (int i = 0; i <= 6; ++i)
        for (int j = 0; j <= 6; ++j) {
            const Vec3 p = surface.point(u.lo + u.length() * i / 6.0, v.lo + v.length() * j / 6.0);
            gap = std::max(gap, projectPoint(nurbs, p).distance);
        }
    FK_CHECK(gap <= 1e-8);
}

}

FK_TEST(NurbsFormsOfSurfaces) {
    std::mt19937 rng(91);
    for (int trial = 0; trial < 4; ++trial) {
        const Frame3 frame = randomFrame(rng, 20.0);
        const double u0 = uniform(rng, -3.0, 3.0), u1 = u0 + uniform(rng, 0.3, 5.0);
        checkSurface(Plane(frame), {-3, 4}, {-2, 5});
        checkSurface(CylindricalSurface(frame, uniform(rng, 1, 5)), {u0, u1}, {-2, 3});
        checkSurface(ConicalSurface(frame, uniform(rng, 0.2, 1.2), uniform(rng, 1, 5)), {u0, u1}, {-1, 2});
        checkSurface(SphericalSurface(frame, uniform(rng, 1, 5)), {u0, u1}, {-1.2, 0.9});
        checkSurface(ToroidalSurface(frame, 6.0, uniform(rng, 0.5, 2.5)), {u0, u1}, {uniform(rng, -3, 0), uniform(rng, 0.5, 3)});
        checkSurface(ToroidalSurface(frame, 6.0, 2.0), {0, kTwoPi}, {0, kTwoPi});
        auto curve = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, trial % 2 == 1));
        checkSurface(ExtrusionSurface(curve, randomDirection(rng)), curve->domain(), {-2, 3});
        checkSurface(RevolutionSurface(curve, frame.origin(), frame.zDir()), {u0, u1}, curve->domain());  // meridiano sghembo
        auto arc = std::make_shared<Circle<3>>(Frame3(frame.origin() + 4.0 * frame.xDir(), frame.yDir(), frame.xDir()).origin(),
                                               frame.xDir(), frame.zDir(), 1.5);
        checkSurface(RevolutionSurface(arc, frame.origin(), frame.zDir()), {u0, u1}, {0.2, 2.5});
    }
}

FK_TEST(NurbsPiecesOfCurves) {
    std::mt19937 rng(92);
    const Frame3 frame = randomFrame(rng, 20.0);
    std::vector<std::pair<CurvePtr<3>, Interval>> curves{
        {std::make_shared<Line<3>>(frame.origin(), frame.xDir()), {-2.0, 3.0}},
        {std::make_shared<Circle<3>>(makeCircle(frame, 3.0)), {0.4, 5.9}},
        {std::make_shared<Ellipse<3>>(makeEllipse(frame, 4.0, 2.0)), {-1.0, 2.0}},
    };
    auto spline = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, true));
    const Interval d = spline->domain();
    curves.push_back({spline, {d.lo + 0.2 * d.length(), d.lo + 0.7 * d.length()}});
    curves.push_back({std::make_shared<TransformedCurve>(spline, Transform3::rotation(frame.origin(), frame.zDir(), 0.7)), d});
    for (const auto &[curve, range] : curves) {
        const std::vector<BSplineCurve<3>> pieces = rationalBezierPieces(*curve, range);
        FK_CHECK(!pieces.empty());
        double worst = 0.0;
        for (const BSplineCurve<3> &piece : pieces)
            for (int i = 0; i <= 10; ++i) {
                const Interval pd = piece.domain();
                worst = std::max(worst, projectPoint(*curve, piece.point(pd.lo + pd.length() * i / 10.0), range).distance);
            }
        FK_CHECK(worst <= 1e-9);
        FK_CHECK(distance(pieces.front().point(pieces.front().domain().lo), curve->point(range.lo)) <= 1e-9);
        FK_CHECK(distance(pieces.back().point(pieces.back().domain().hi), curve->point(range.hi)) <= 1e-9);
    }
}
