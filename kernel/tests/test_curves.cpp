#include <Geom2d_Circle.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Line.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax22d.hxx>

#include "fk_test_util.h"

using namespace fktest;

FK_TEST(LineMatchesOcct) {
    std::mt19937 rng(10);
    for (int trial = 0; trial < 50; ++trial) {
        const Vec3 origin = randomVec<3>(rng, 100.0);
        const Vec3 direction = randomVec<3>(rng, 5.0);
        const Line<3> line(origin, direction);
        const Handle(Geom_Line) reference = new Geom_Line(toPnt(origin), toDir(direction));
        for (int i = 0; i < 10; ++i) {
            const double t = uniform(rng, -500.0, 500.0);
            Vec3 d[3];
            line.evaluate(t, 2, d);
            gp_Pnt p;
            gp_Vec d1;
            reference->D1(t, p, d1);
            FK_CHECK(near(d[0], fromOcct(p), 1e-14));
            FK_CHECK(near(d[1], fromOcct(d1), 1e-14));
            FK_CHECK(norm(d[2]) == 0.0);
        }
    }
    FK_CHECK_THROWS(Line<3>(Vec3(), Vec3()));
}

FK_TEST(CircleMatchesOcct) {
    std::mt19937 rng(11);
    for (int trial = 0; trial < 50; ++trial) {
        const Frame3 frame(randomVec<3>(rng, 100.0), randomDirection(rng), randomDirection(rng));
        const double radius = uniform(rng, 0.01, 200.0);
        const Circle<3> circle = makeCircle(frame, radius);
        const Handle(Geom_Circle) reference =
            new Geom_Circle(gp_Ax2(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir())), radius);
        for (int i = 0; i < 20; ++i) {
            const double t = uniform(rng, -10.0, 10.0);
            Vec3 d[5];
            circle.evaluate(t, 4, d);
            gp_Pnt p;
            gp_Vec d1, d2, d3;
            reference->D3(t, p, d1, d2, d3);
            FK_CHECK(near(d[0], fromOcct(p), 1e-14));
            FK_CHECK(near(d[1], fromOcct(d1), 1e-14));
            FK_CHECK(near(d[2], fromOcct(d2), 1e-14));
            FK_CHECK(near(d[3], fromOcct(d3), 1e-14));
            FK_CHECK(near(d[4], fromOcct(reference->DN(t, 4)), 1e-14));
        }
    }
    FK_CHECK_THROWS(makeCircle(Vec2(), 0.0));
    FK_CHECK_THROWS(Circle<2>(Vec2(), Vec2(1, 0), Vec2(2, 0), 1.0));
}

FK_TEST(Circle2dMatchesOcct) {
    std::mt19937 rng(12);
    const Vec2 center = randomVec<2>(rng, 50.0);
    const Circle<2> circle = makeCircle(center, 7.5);
    const Handle(Geom2d_Circle) reference = new Geom2d_Circle(gp_Ax22d(toPnt(center), gp_Dir2d(1, 0), gp_Dir2d(0, 1)), 7.5);
    for (int i = 0; i < 50; ++i) {
        const double t = uniform(rng, 0.0, kTwoPi);
        gp_Pnt2d p;
        gp_Vec2d d1, d2;
        reference->D2(t, p, d1, d2);
        FK_CHECK(near(circle.point(t), fromOcct(p), 1e-14));
        FK_CHECK(near(circle.derivative(t, 1), fromOcct(d1), 1e-14));
        FK_CHECK(near(circle.derivative(t, 2), fromOcct(d2), 1e-14));
    }
}

FK_TEST(EllipseMatchesOcct) {
    std::mt19937 rng(13);
    for (int trial = 0; trial < 50; ++trial) {
        const Frame3 frame(randomVec<3>(rng, 100.0), randomDirection(rng), randomDirection(rng));
        const double minor = uniform(rng, 0.01, 50.0);
        const double major = minor * uniform(rng, 1.0, 20.0);
        const Ellipse<3> ellipse = makeEllipse(frame, major, minor);
        const Handle(Geom_Ellipse) reference =
            new Geom_Ellipse(gp_Ax2(toPnt(frame.origin()), toDir(frame.zDir()), toDir(frame.xDir())), major, minor);
        for (int i = 0; i < 20; ++i) {
            const double t = uniform(rng, -10.0, 10.0);
            Vec3 d[4];
            ellipse.evaluate(t, 3, d);
            gp_Pnt p;
            gp_Vec d1, d2, d3;
            reference->D3(t, p, d1, d2, d3);
            FK_CHECK(near(d[0], fromOcct(p), 1e-14));
            FK_CHECK(near(d[1], fromOcct(d1), 1e-14));
            FK_CHECK(near(d[2], fromOcct(d2), 1e-14));
            FK_CHECK(near(d[3], fromOcct(d3), 1e-14));
        }
    }
}

FK_TEST(TrimmedCurveOnPeriodicBasis) {
    const auto circle = std::make_shared<Circle<2>>(makeCircle(Vec2(1.0, 2.0), 3.0));
    // Da 270 a 90 gradi passando per 0.
    const TrimmedCurve<2> arc(circle, 1.5 * kPi, 0.5 * kPi);
    FK_CHECK_NEAR(arc.domain().lo, 1.5 * kPi, 0.0);
    FK_CHECK_NEAR(arc.domain().hi, 2.5 * kPi, 1e-15);
    FK_CHECK(near(arc.point(kTwoPi), Vec2(4.0, 2.0), 1e-15));
    const std::vector<double> breaks = arc.breakpoints(arc.domain());
    FK_CHECK(breaks.size() == 3);  // estremi + 2 pi

    // Stesso estremo: cerchio completo.
    const TrimmedCurve<2> full(circle, 1.0, 1.0);
    FK_CHECK_NEAR(full.domain().length(), kTwoPi, 1e-15);

    const auto line = std::make_shared<Line<2>>(Vec2(), Vec2(1.0, 0.0));
    FK_CHECK_THROWS(TrimmedCurve<2>(line, 2.0, 1.0));
}
