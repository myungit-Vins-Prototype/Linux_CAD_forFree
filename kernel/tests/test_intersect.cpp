#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <gp_Ax22d.hxx>

#include "fk_intersect.h"
#include "fk_surface_algo.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

Handle(Geom2d_Curve) occt2d(const Curve<2> &curve, const Interval &range) {
    Handle(Geom2d_Curve) base;
    switch (curve.type()) {
    case CurveType::Line: {
        const auto &l = static_cast<const Line<2> &>(curve);
        base = new Geom2d_Line(toPnt(l.origin()), gp_Dir2d(l.direction().x(), l.direction().y()));
        break;
    }
    case CurveType::Circle: {
        const auto &c = static_cast<const Circle<2> &>(curve);
        base = new Geom2d_Circle(gp_Ax22d(toPnt(c.center()), gp_Dir2d(c.xAxis().x(), c.xAxis().y()), gp_Dir2d(c.yAxis().x(), c.yAxis().y())),
                                 c.radius());
        break;
    }
    case CurveType::Ellipse: {
        const auto &e = static_cast<const Ellipse<2> &>(curve);
        base = new Geom2d_Ellipse(gp_Ax22d(toPnt(e.center()), gp_Dir2d(e.xAxis().x(), e.xAxis().y()), gp_Dir2d(e.yAxis().x(), e.yAxis().y())),
                                  e.xRadius(), e.yRadius());
        break;
    }
    case CurveType::BSpline:
        base = toOcct(static_cast<const BSplineCurve<2> &>(curve));
        break;
    default:
        throw std::logic_error("occt2d: tipo non gestito");
    }
    return new Geom2d_TrimmedCurve(base, range.lo, range.hi);
}

struct Sample2 {
    CurvePtr<2> curve;
    Interval range;
};

Sample2 randomCurve2(std::mt19937 &rng, int kind) {
    switch (kind) {
    case 0: {
        auto line = std::make_shared<Line<2>>(randomVec<2>(rng, 20.0), randomVec<2>(rng, 1.0));
        return {line, {-60.0, 60.0}};
    }
    case 1: {
        const double angle = uniform(rng, 0, kTwoPi);
        auto circle = std::make_shared<Circle<2>>(randomVec<2>(rng, 20.0), Vec2(std::cos(angle), std::sin(angle)),
                                                  Vec2(-std::sin(angle), std::cos(angle)), uniform(rng, 5.0, 40.0));
        const double lo = uniform(rng, -3.0, 3.0);
        return {circle, {lo, lo + uniform(rng, 1.0, kTwoPi)}};
    }
    case 2: {
        const double angle = uniform(rng, 0, kTwoPi);
        auto ellipse = std::make_shared<Ellipse<2>>(randomVec<2>(rng, 20.0), Vec2(std::cos(angle), std::sin(angle)),
                                                    Vec2(-std::sin(angle), std::cos(angle)), uniform(rng, 20.0, 40.0), uniform(rng, 5.0, 19.0));
        return {ellipse, {0.0, kTwoPi}};
    }
    default: {
        auto spline = std::make_shared<BSplineCurve<2>>(randomBSpline<2>(rng, kind == 4));
        return {spline, spline->domain()};
    }
    }
}

}

// Radici curva-piano: ogni radice sta sul piano e non se ne perde nessuna
// (confronto con i cambi di segno su un campionamento fitto).
FK_TEST(PlaneRootsOfCurves) {
    std::mt19937 rng(400);
    for (int trial = 0; trial < 60; ++trial) {
        const BSplineCurve<3> spline = randomBSpline<3>(rng, trial % 2);
        const Vec3 n = randomDirection(rng);
        const double offset = dot(n, spline.point(spline.domain().lo + 0.37 * spline.domain().length()));
        const PlaneRoots<3> roots = planeRoots<3>(spline, spline.domain(), n, offset, 1e-9);
        for (double t : roots.parameters) FK_CHECK(std::fabs(dot(n, spline.point(t)) - offset) <= 1e-8);
        int changes = 0;
        double previous = dot(n, spline.point(spline.domain().lo)) - offset;
        for (int i = 1; i <= 4000; ++i) {
            const double value = dot(n, spline.point(spline.domain().lo + spline.domain().length() * i / 4000.0)) - offset;
            if ((value > 0.0) != (previous > 0.0)) ++changes;
            previous = value;
        }
        FK_CHECK(int(roots.parameters.size()) >= changes);
    }
}

// Intersezioni curva-curva nel piano contro Geom2dAPI_InterCurveCurve.
FK_TEST(CurveCurveIntersectionsMatchOcct) {
    std::mt19937 rng(401);
    int compared = 0;
    for (int trial = 0; trial < 150; ++trial) {
        const int kindA = trial % 5, kindB = (trial / 5) % 5;
        if (kindA == 0 && kindB == 0) continue;
        const Sample2 a = randomCurve2(rng, kindA), b = randomCurve2(rng, kindB);
        const CurveCurveIntersection ours = intersectCurves(*a.curve, a.range, *b.curve, b.range, 1e-9);
        for (const CurveCurvePoint &p : ours.points) {
            FK_CHECK(distance(a.curve->point(p.s), p.point) <= 1e-7);
            FK_CHECK(distance(b.curve->point(p.t), p.point) <= 1e-7);
        }
        Geom2dAPI_InterCurveCurve reference(occt2d(*a.curve, a.range), occt2d(*b.curve, b.range), 1e-9);
        // Ogni punto di OCCT deve essere tra i nostri.
        for (int i = 1; i <= reference.NbPoints(); ++i) {
            const Vec2 q = fromOcct(reference.Point(i));
            double best = 1e300;
            for (const CurveCurvePoint &p : ours.points) best = std::min(best, distance(p.point, q));
            FK_CHECK(best <= 1e-6);
        }
        FK_CHECK(int(ours.points.size()) >= reference.NbPoints());
        compared += reference.NbPoints();
    }
    FK_CHECK(compared > 50);
}

// Piano con cilindro, sfera e superficie estrusa: le curve stanno su entrambe.
FK_TEST(PlaneSurfaceIntersections) {
    std::mt19937 rng(402);
    Box bounds;
    bounds.add(Vec3(-200, -200, -200));
    bounds.add(Vec3(200, 200, 200));
    for (int trial = 0; trial < 40; ++trial) {
        const Frame3 frame = randomFrame(rng, 20.0);
        const Plane plane(Frame3(randomVec<3>(rng, 5.0), randomDirection(rng), randomDirection(rng)));
        std::shared_ptr<Surface> surface;
        switch (trial % 3) {
        case 0: surface = std::make_shared<CylindricalSurface>(frame, uniform(rng, 5.0, 30.0)); break;
        case 1: surface = std::make_shared<SphericalSurface>(frame, uniform(rng, 10.0, 40.0)); break;
        default: {
            auto spline = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, trial % 2));
            surface = std::make_shared<ExtrusionSurface>(spline, randomDirection(rng));
            break;
        }
        }
        const PlaneSurfaceIntersection result = intersectPlaneSurface(plane, *surface, bounds, 1e-9);
        for (std::size_t i = 0; i < result.curves.size(); ++i)
            for (int j = 0; j <= 20; ++j) {
                const Vec3 p = result.curves[i]->point(result.ranges[i].lo + result.ranges[i].length() * j / 20.0);
                FK_CHECK(std::fabs(dot(plane.frame().zDir(), p - plane.frame().origin())) <= 1e-8);
                FK_CHECK(projectPoint(*surface, p).distance <= 1e-7);
            }
    }
}
