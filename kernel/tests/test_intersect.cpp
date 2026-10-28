#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <gp_Ax22d.hxx>

#include <future>
#include "fk_cancel.h"
#include "fk_intersect.h"
#include "fk_surface_algo.h"
#include "fk_test_util.h"

using namespace fktest;

FK_TEST(PlanarImageTransformedCurves) {
    const Frame3 frame(Vec3(1, -2, 3), Vec3(1, 1, 2), Vec3(1, 0, 0));
    const auto spline = std::make_shared<BSplineCurve<3>>(2, std::vector<double>{0, 0, 0, 1, 1, 1},
        std::vector<Vec3>{Vec3(0, 0, 0), Vec3(1, 2, 1), Vec3(3, 0, 2)}, std::vector<double>{1, .8, 1});
    const std::vector<CurvePtr<3>> bases{
        spline, std::make_shared<Line<3>>(Vec3(), Vec3(1, 2, 0)),
        std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 2)};
    for (const auto &base : bases) {
        const auto inner = std::make_shared<TransformedCurve>(base, Transform3::scaling(Vec3(), 2.5));
        const auto trimmed = std::make_shared<TrimmedCurve<3>>(inner, .1, .9);
        const TransformedCurve curve(trimmed, Transform3::translation(Vec3(2, 3, 4)) *
            Transform3::reflection(Vec3(), Vec3(1, 0, 0)));
        const auto image = planarImage(curve, curve.domain(), frame);
        FK_CHECK(bool(image.curve));
        for (int i = 0; i <= 32; ++i) {
            const double s = image.range.lo + image.range.length() * i / 32.;
            const double t = image.toCurve(s);
            FK_CHECK(curve.domain().contains(t, 1e-10));
            const auto p = frame.toLocal(curve.point(t));
            FK_CHECK_NEAR(distance(image.curve->point(s), Vec2(p.x(), p.y())), 0, 1e-9);
        }
    }
    // La proiezione di una retta normale al piano si riduce a un punto.
    const auto normal = std::make_shared<Line<3>>(Vec3(), Vec3(0, 0, 1));
    const TransformedCurve line(normal, Transform3::scaling(Vec3(), 3));
    const auto point = planarImage(line, {1, 2}, Frame3());
    FK_CHECK(!point.curve);
    FK_CHECK_NEAR(point.toCurve(point.range.lo), 1, 1e-12);
    FK_CHECK_NEAR(point.toCurve(point.range.hi), 2, 1e-12);
}

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

// Non trasformare una sezione toroidale generica in un falso raggio circolare.
FK_TEST(PlaneTorusMeridianIntersections) {
    for (const auto &frame : {Frame3(), Frame3(Vec3(12,-7,3), normalized(Vec3(1,2,3)), Vec3(1,0,0))}) {
        const ToroidalSurface torus(frame, 5, 1.5);
        const Plane plane(Frame3(frame.origin(), frame.yDir(), frame.xDir()));
        Box bounds; bounds.add(frame.origin()-Vec3(20,20,20)); bounds.add(frame.origin()+Vec3(20,20,20));
        const auto result = intersectPlaneSurface(plane, torus, bounds, 1e-9);
        FK_CHECK(result.curves.size() == 2);
        for (const auto &curve : result.curves) {
            FK_CHECK(curve->type() == CurveType::Circle);
            FK_CHECK(std::abs(static_cast<const Circle<3> &>(*curve).radius()-1.5) < 1e-12);
            for (int i=0; i<32; ++i) {
                const auto p = curve->point(kTwoPi*i/32.);
                FK_CHECK(projectPoint(torus,p).distance < 1e-9);
                FK_CHECK(std::abs(dot(plane.frame().zDir(),p-plane.frame().origin())) < 1e-10);
            }
        }
        FK_CHECK_THROWS(intersectPlaneSurface(Plane(Frame3(frame.origin()+.1*frame.yDir(),frame.yDir(),frame.xDir())),torus,bounds,1e-9));
        FK_CHECK_THROWS(intersectPlaneSurface(Plane(Frame3(frame.origin(),normalized(frame.yDir()+.001*frame.zDir()),frame.xDir())),torus,bounds,1e-9));
    }
}

// Il filtro per inviluppo dei poli deve conservare anche i contatti senza
// cambio di segno, le radici agli estremi e i tratti coincidenti.
FK_TEST(PlaneRootsHullKeepsContacts) {
    const auto curve = [](std::vector<double> y, std::vector<double> weights = {}) {
        return BSplineCurve<2>(2, {0,0,0,1,1,1}, {{0,y[0]}, {.5,y[1]}, {1,y[2]}}, std::move(weights));
    };
    const Vec2 normal(0,1);
    for (double sign : {-1.0, 1.0}) {
        const auto separated = curve({sign*2,sign*3,sign*4}, {1e-6,1,1e6});
        const auto roots = planeRoots<2>(separated, {0,1}, normal, 0, 1e-9);
        FK_CHECK(roots.parameters.empty() && roots.coincident.empty());
    }
    for (double shift : {0.0, 5e-12}) {
        const auto tangent = curve({1+shift,-1+shift,1+shift});
        const auto roots = planeRoots<2>(tangent, {0,1}, normal, 0, 1e-9);
        FK_CHECK(roots.parameters.size() == 1);
        if (!roots.parameters.empty()) FK_CHECK_NEAR(roots.parameters.front(), .5, 1e-12);
        FK_CHECK(roots.coincident.empty());
    }
    const auto endpoints = curve({0,1,0}, {1,2,3});
    const auto ends = planeRoots<2>(endpoints, {0,1}, normal, 0, 1e-9);
    FK_CHECK(ends.parameters.size() == 2);
    if (ends.parameters.size() == 2) {
        FK_CHECK_NEAR(ends.parameters.front(), 0, 1e-14);
        FK_CHECK_NEAR(ends.parameters.back(), 1, 1e-14);
    }
    const auto coincident = curve({0,0,0}, {1,10,.1});
    const auto overlap = planeRoots<2>(coincident, {.2,.8}, normal, 0, 1e-9);
    FK_CHECK(overlap.parameters.empty() && overlap.coincident.size() == 1);
    if (!overlap.coincident.empty()) {
        FK_CHECK_NEAR(overlap.coincident.front().lo, .2, 1e-14);
        FK_CHECK_NEAR(overlap.coincident.front().hi, .8, 1e-14);
    }
    // Una normale scalata e pesi molto diversi non cambiano le radici esatte.
    const auto rational = curve({-1,0,1}, {1,100,4});
    for (double scale : {1e-3,1.0,1e3}) {
        const auto roots = planeRoots<2>(rational, {0,1}, scale*normal, 0, 1e-12);
        FK_CHECK(roots.parameters.size() == 1);
        if (!roots.parameters.empty()) FK_CHECK_NEAR(roots.parameters.front(), 1.0/3.0, 1e-12);
    }
    const auto far = curve({1e9+1,1e9-1,1e9+1});
    const auto translated = planeRoots<2>(far, {0,1}, normal, 1e9, 1e-9);
    FK_CHECK(translated.parameters.size() == 1);
    if (!translated.parameters.empty()) FK_CHECK_NEAR(translated.parameters.front(), .5, 1e-12);
}

FK_TEST(PlaneRootsManySpans) {
    std::vector<Vec2> poles;std::vector<double> knots{0};
    for(int i=0;i<=512;++i){poles.push_back(Vec2(i,i%2?1:-1));knots.push_back(i);}
    knots.push_back(512);
    const BSplineCurve<2> curve(1,knots,poles);
    for(double offset : {-2.,0.,2.}){
        const auto roots=planeRoots<2>(curve,{.25,511.75},Vec2(0,1),offset,1e-10);
        FK_CHECK(roots.coincident.empty());
        FK_CHECK(roots.parameters.size()==(offset==0?512:0));
        if(offset==0)for(std::size_t i=0;i<roots.parameters.size();++i)FK_CHECK_NEAR(roots.parameters[i],i+.5,1e-12);
    }
}

namespace {

BSplineCurve<2> manySpanPolyline(int spans, bool rational, int axis, int shape) {
    std::vector<double> knots{0.0};
    std::vector<Vec2> poles;
    std::vector<double> weights;
    for (int i = 0; i <= spans; ++i) {
        knots.push_back(double(i));
        const double x = shape == 1 ? 0.5 * i : double(i);
        const double y = shape == 0 ? (i % 2 ? 1.0 : -1.0)
                        : shape == 2 ? std::fabs(double(i - spans / 2))
                        : shape == 3 ? 3.0 : 0.0;
        poles.push_back(axis == 0 ? Vec2(x, y) : Vec2(y, x));
        if (rational) weights.push_back(0.5 + (i % 5));
    }
    knots.push_back(double(spans));
    return BSplineCurve<2>(1, std::move(knots), std::move(poles), std::move(weights));
}

}

// Molte pezze, radici note anche quando cadono sui nodi della seconda curva.
// I pesi cambiano i parametri ma non i segmenti ne' le loro intersezioni.
FK_TEST(CurveCurveManySpansAnalytic) {
    for (int spans : {7, 15, 16, 65})
        for (bool rational : {false, true})
            for (int axis : {0, 1}) {
                const auto a = manySpanPolyline(spans, rational, axis, 0);
                const auto b = manySpanPolyline(2 * spans, !rational, axis, 1);
                for (bool swapped : {false, true}) {
                    const auto result = swapped ? intersectCurves(b, b.domain(), a, a.domain(), 1e-9)
                                                : intersectCurves(a, a.domain(), b, b.domain(), 1e-9);
                    FK_CHECK(!result.overlap);
                    FK_CHECK(result.points.size() == std::size_t(spans));
                    std::vector<double> coordinates;
                    for (const auto &p : result.points) {
                        FK_CHECK_NEAR(p.point[1 - axis], 0.0, 1e-8);
                        FK_CHECK(distance((swapped ? b : a).point(p.s), p.point) <= 1e-8);
                        FK_CHECK(distance((swapped ? a : b).point(p.t), p.point) <= 1e-8);
                        coordinates.push_back(p.point[axis]);
                    }
                    std::sort(coordinates.begin(), coordinates.end());
                    for (std::size_t i = 0; i < coordinates.size(); ++i)
                        FK_CHECK_NEAR(coordinates[i], i + 0.5, 1e-8);
                }
                // Range ristretto esatto sul parametro dei vertici, con pesi
                // uniformi per conservare i limiti geometrici noti.
                const auto plain = manySpanPolyline(spans, false, axis, 0);
                const auto partial = intersectCurves(plain, {2.0, double(spans - 2)}, b, b.domain(), 1e-9);
                FK_CHECK(partial.points.size() == std::size_t(spans - 4));
                for (const auto &p : partial.points) {
                    FK_CHECK(p.point[axis] >= 2.5 - 1e-8);
                    FK_CHECK(p.point[axis] <= spans - 2.5 + 1e-8);
                }
            }
}

FK_TEST(CurveCurveManySpansContactsAndConcurrency) {
    const auto a = manySpanPolyline(64, true, 0, 2);
    const auto b = manySpanPolyline(128, false, 0, 1);
    auto contact = [&] {
        const auto result = intersectCurves(a, a.domain(), b, b.domain(), 1e-9);
        FK_CHECK(!result.overlap);
        FK_CHECK(result.points.size() == 1);
        if (!result.points.empty()) FK_CHECK_NEAR(distance(result.points.front().point, Vec2(32, 0)), 0.0, 1e-8);
    };
    // Le curve sono condivise; ogni chiamata possiede la propria gerarchia.
    auto pending = std::async(std::launch::async, [&] { return intersectCurves(a, a.domain(), b, b.domain(), 1e-9); });
    contact();
    const auto concurrent = pending.get();
    FK_CHECK(concurrent.points.size() == 1);
    if (!concurrent.points.empty()) FK_CHECK_NEAR(distance(concurrent.points.front().point, Vec2(32, 0)), 0.0, 1e-8);
    const auto disjoint = manySpanPolyline(64, false, 0, 3);
    const auto miss = intersectCurves(disjoint, disjoint.domain(), b, b.domain(), 1e-9);
    FK_CHECK(miss.points.empty());
    FK_CHECK(!miss.overlap);
    auto token = std::make_shared<std::atomic<bool>>(true);
    bool cancelled = false;
    {
        CancellationScope scope(token);
        try { (void)intersectCurves(a, a.domain(), b, b.domain(), 1e-9); }
        catch (const OperationCancelled &) { cancelled = true; }
    }
    FK_CHECK(cancelled);
    contact();
}
