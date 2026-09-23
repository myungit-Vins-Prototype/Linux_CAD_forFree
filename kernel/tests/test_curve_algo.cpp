#include <GCPnts_AbscissaPoint.hxx>
#include <Geom_Ellipse.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <gp_Ax2.hxx>

#include "fk_curve_algo.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

// Il risultato deve essere un breakpoint (estremo o nodo, dove la curva puo'
// avere uno spigolo) oppure una proiezione ortogonale.
template <int N>
bool isStationary(const Curve<N> &curve, const Vec<N> &p, const CurveProjection<N> &result, const Interval &range) {
    for (double t : curve.breakpoints(range))
        if (result.parameter == t) return true;
    const Vec<N> tangent = curve.derivative(result.parameter, 1);
    return std::fabs(dot(tangent, result.point - p)) <= 1e-9 * norm(tangent) * std::max(1.0, result.distance);
}

}

FK_TEST(ProjectOnLineAndCircle) {
    const Line<2> line(Vec2(1.0, 1.0), Vec2(1.0, 0.0));
    const CurveProjection<2> onLine = projectPoint(line, Vec2(5.0, 3.0), Interval{-10.0, 10.0});
    FK_CHECK_NEAR(onLine.parameter, 4.0, 1e-15);
    FK_CHECK_NEAR(onLine.distance, 2.0, 1e-15);
    FK_CHECK_NEAR(projectPoint(line, Vec2(50.0, 3.0), Interval{-10.0, 10.0}).parameter, 10.0, 0.0);
    FK_CHECK_NEAR(projectPoint(line, Vec2(-7.0, 0.0)).parameter, -8.0, 1e-15);  // dominio infinito

    const Circle<2> circle = makeCircle(Vec2(), 2.0);
    const CurveProjection<2> onCircle = projectPoint(circle, Vec2(0.0, 5.0));
    FK_CHECK_NEAR(onCircle.parameter, kHalfPi, 1e-15);
    FK_CHECK_NEAR(onCircle.distance, 3.0, 1e-15);
    FK_CHECK_THROWS(projectPoint(circle, Vec2(), Interval{0.0, std::numeric_limits<double>::infinity()}));
    // Arco da 0 a 90 gradi, punto a 200 gradi: vince l'estremo piu' vicino (180 -> 0 gradi? no, 90).
    const CurveProjection<2> onArc = projectPoint(circle, Vec2(std::cos(3.5), std::sin(3.5)), Interval{0.0, kHalfPi});
    FK_CHECK_NEAR(onArc.parameter, kHalfPi, 0.0);
    // Arco trimmed attraverso lo zero.
    const TrimmedCurve<2> arc(std::make_shared<Circle<2>>(circle), 1.5 * kPi, 0.5 * kPi);
    const CurveProjection<2> onTrimmed = projectPoint(arc, Vec2(3.0, -0.1));
    FK_CHECK_NEAR(onTrimmed.parameter, kTwoPi - std::atan2(0.1, 3.0), 1e-14);
}

FK_TEST(ProjectOnBSplineMatchesOcct) {
    std::mt19937 rng(30);
    int compared = 0;
    for (int trial = 0; trial < 300; ++trial) {
        const BSplineCurve<3> curve = randomBSpline<3>(rng, trial % 2 == 1);
        const Handle(Geom_BSplineCurve) reference = toOcct(curve);
        const Interval domain = curve.domain();
        for (int i = 0; i < 10; ++i) {
            // Punti vicini alla curva e punti lontani.
            const Vec3 p = i % 2 == 0 ? curve.point(uniform(rng, domain.lo, domain.hi)) + randomVec<3>(rng, 2.0)
                                      : randomVec<3>(rng, 150.0);
            const CurveProjection<3> result = projectPoint(curve, p);
            FK_CHECK(isStationary(curve, p, result, domain));
            FK_CHECK_NEAR(distance(result.point, curve.point(result.parameter)), 0.0, 0.0);

            GeomAPI_ProjectPointOnCurve occt(toPnt(p), reference);
            if (occt.NbPoints() == 0) continue;
            ++compared;
            // Mai peggio di OCCT (che non considera gli estremi, quindi puo' essere peggio lui).
            FK_CHECK(result.distance <= occt.LowerDistance() + 1e-9 * std::max(1.0, occt.LowerDistance()));
        }
    }
    FK_CHECK(compared > 1000);
}

FK_TEST(ProjectOnEllipseMatchesOcct) {
    std::mt19937 rng(31);
    for (int trial = 0; trial < 200; ++trial) {
        const Frame3 frame(randomVec<3>(rng, 50.0), randomDirection(rng), randomDirection(rng));
        const double minor = uniform(rng, 0.1, 20.0), major = minor * uniform(rng, 1.0, 15.0);
        const Ellipse<3> ellipse = makeEllipse(frame, major, minor);
        const Handle(Geom_Ellipse) reference =
            new Geom_Ellipse(gp_Ax2(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir())), major, minor);
        const Vec3 p = frame.origin() + randomVec<3>(rng, 2.0 * major);
        const CurveProjection<3> result = projectPoint(ellipse, p);
        GeomAPI_ProjectPointOnCurve occt(toPnt(p), reference);
        FK_CHECK(occt.NbPoints() > 0);
        if (occt.NbPoints() == 0) continue;
        // Sul cerchio completo gli estremi non contano: stessa distanza.
        FK_CHECK_NEAR(result.distance, occt.LowerDistance(), 1e-9 * std::max(1.0, occt.LowerDistance()));
    }
}

FK_TEST(ArcLengthExact) {
    const Circle<3> circle = makeCircle(Frame3(Vec3(1, 2, 3), Vec3(1, 1, 1), Vec3(1, 0, 0)), 12.5);
    FK_CHECK_NEAR(arcLength(circle), kTwoPi * 12.5, 1e-11);
    FK_CHECK_NEAR(arcLength(circle, Interval{0.3, 2.0}), 1.7 * 12.5, 1e-11);
    // Stessa lunghezza sulla NURBS esatta del cerchio (parametrizzazione diversa).
    const BSplineCurve<3> nurbs = toBSpline(circle, 0.3, 5.0);
    FK_CHECK_NEAR(arcLength(nurbs), 4.7 * 12.5, 1e-9);
    const Line<2> line(Vec2(), Vec2(3.0, 4.0));
    FK_CHECK_NEAR(arcLength(line, Interval{-1.0, 6.0}), 7.0, 1e-13);
}

FK_TEST(ArcLengthMatchesOcct) {
    std::mt19937 rng(32);
    for (int trial = 0; trial < 200; ++trial) {
        const BSplineCurve<3> curve = randomBSpline<3>(rng, trial % 2 == 0);
        GeomAdaptor_Curve adaptor(toOcct(curve));
        const double reference = GCPnts_AbscissaPoint::Length(adaptor, 1e-12);
        FK_CHECK_NEAR(arcLength(curve), reference, 1e-8 * std::max(1.0, reference));
    }
}

// Curva trasformata da una similitudine: si proietta sulla base, senza campionare.
FK_TEST(ProjectOnTransformedCurve) {
    std::mt19937 rng(33);
    for (int trial = 0; trial < 100; ++trial) {
        const auto curve = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, trial % 2 == 0));
        const Transform3 transform = Transform3::rotation(randomVec<3>(rng, 10.0), randomDirection(rng), uniform(rng, -3, 3))
                                   * Transform3::scaling(randomVec<3>(rng, 10.0), uniform(rng, 0.5, 2.0));
        const TransformedCurve transformed(curve, transform);
        const Vec3 p = randomVec<3>(rng, 150.0);
        const CurveProjection<3> result = projectPoint(transformed, p);
        FK_CHECK(near(result.point, transformed.point(result.parameter), 1e-12));
        // Stesso risultato della B-spline con i poli trasformati.
        std::vector<Vec3> poles;
        for (const Vec3 &pole : curve->poles()) poles.push_back(transform.applyToPoint(pole));
        const BSplineCurve<3> explicitCurve(curve->degree(), curve->knots(), poles, curve->weights());
        FK_CHECK_NEAR(result.distance, projectPoint(explicitCurve, p).distance, 1e-9 * std::max(1.0, result.distance));
        // Tra i candidati c'e' il minimo.
        double best = std::numeric_limits<double>::infinity();
        for (const CurveProjection<3> &c : projectPointCandidates(transformed, p, curve->domain())) best = std::min(best, c.distance);
        FK_CHECK_NEAR(best, result.distance, 0.0);
    }
    FK_CHECK(!Transform3::projectionAlong(Vec3(0, 0, 1)).isSimilarity());
}
