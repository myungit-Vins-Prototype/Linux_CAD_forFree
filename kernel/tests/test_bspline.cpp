#include "fk_test_util.h"

using namespace fktest;

namespace {

// Tolleranza relativa per derivate di ordine k: le derivate alte di NURBS con
// pesi molto diversi crescono di ordini di grandezza, e con loro l'errore.
double derivativeTolerance(int order) { return 1e-12 * std::pow(10.0, order); }

template <int N>
bool sameCurve(const Curve<N> &a, const Curve<N> &b, const Interval &range, double tolerance) {
    for (int i = 0; i <= 50; ++i) {
        const double t = range.lo + range.length() * i / 50.0;
        if (!near(a.point(t), b.point(t), tolerance)) return false;
    }
    return true;
}

}

FK_TEST(BSplineEvaluateMatchesOcct3d) {
    std::mt19937 rng(20);
    for (int trial = 0; trial < 400; ++trial) {
        const BSplineCurve<3> curve = randomBSpline<3>(rng, trial % 2 == 1);
        const Handle(Geom_BSplineCurve) reference = toOcct(curve);
        const Interval domain = curve.domain();
        FK_CHECK_NEAR(reference->FirstParameter(), domain.lo, 0.0);
        FK_CHECK_NEAR(reference->LastParameter(), domain.hi, 0.0);
        const int maxOrder = curve.degree() + 2;
        std::vector<Vec3> d(maxOrder + 1);
        for (int i = 0; i <= 20; ++i) {
            // Anche esattamente sui nodi e sugli estremi.
            const double t = i % 5 == 0 ? curve.knots()[uniformInt(rng, curve.degree(), curve.poleCount())]
                                        : uniform(rng, domain.lo, domain.hi);
            curve.evaluate(t, maxOrder, d.data());
            gp_Pnt p;
            gp_Vec d1, d2, d3;
            reference->D3(t, p, d1, d2, d3);
            FK_CHECK(near(d[0], fromOcct(p), 1e-13));
            FK_CHECK(near(d[1], fromOcct(d1), derivativeTolerance(1)));
            FK_CHECK(near(d[2], fromOcct(d2), derivativeTolerance(2)));
            FK_CHECK(near(d[3], fromOcct(d3), derivativeTolerance(3)));
            for (int k = 4; k <= maxOrder; ++k) FK_CHECK(near(d[k], fromOcct(reference->DN(t, k)), derivativeTolerance(k)));
        }
    }
}

FK_TEST(BSplineEvaluateMatchesOcct2d) {
    std::mt19937 rng(21);
    for (int trial = 0; trial < 200; ++trial) {
        const BSplineCurve<2> curve = randomBSpline<2>(rng, trial % 2 == 0);
        const Handle(Geom2d_BSplineCurve) reference = toOcct(curve);
        const Interval domain = curve.domain();
        for (int i = 0; i < 20; ++i) {
            const double t = uniform(rng, domain.lo, domain.hi);
            Vec2 d[3];
            curve.evaluate(t, 2, d);
            gp_Pnt2d p;
            gp_Vec2d d1, d2;
            reference->D2(t, p, d1, d2);
            FK_CHECK(near(d[0], fromOcct(p), 1e-13));
            FK_CHECK(near(d[1], fromOcct(d1), derivativeTolerance(1)));
            FK_CHECK(near(d[2], fromOcct(d2), derivativeTolerance(2)));
        }
    }
}

FK_TEST(BSplineKnotInsertionMatchesOcct) {
    std::mt19937 rng(22);
    for (int trial = 0; trial < 300; ++trial) {
        const BSplineCurve<3> curve = randomBSpline<3>(rng, trial % 2 == 1);
        const Interval domain = curve.domain();
        // Meta' delle volte su un nodo esistente interno (se c'e').
        double u = uniform(rng, domain.lo, domain.hi);
        for (double knot : curve.knots())
            if (trial % 2 == 0 && knot > domain.lo && knot < domain.hi) { u = knot; break; }
        const int times = uniformInt(rng, 1, curve.degree());
        const BSplineCurve<3> refined = curve.insertKnot(u, times);

        FK_CHECK(refined.multiplicity(u) == std::min(curve.degree(), curve.multiplicity(u) + times));
        FK_CHECK(sameCurve(curve, refined, domain, 1e-13));

        const Handle(Geom_BSplineCurve) reference = toOcct(curve);
        reference->InsertKnot(u, times, 0.0, true);
        FK_CHECK(reference->NbPoles() == refined.poleCount());
        if (reference->NbPoles() != refined.poleCount()) continue;
        for (int i = 0; i < refined.poleCount(); ++i) {
            FK_CHECK(near(refined.poles()[i], fromOcct(reference->Pole(i + 1)), 1e-12));
            FK_CHECK_NEAR(refined.weight(i), reference->Weight(i + 1), 1e-12 * refined.weight(i));
        }
    }
}

FK_TEST(BSplineBezierSegments) {
    std::mt19937 rng(23);
    for (int trial = 0; trial < 200; ++trial) {
        const BSplineCurve<3> curve = randomBSpline<3>(rng, trial % 2 == 0);
        const std::vector<BSplineCurve<3>> segments = curve.bezierSegments();
        FK_CHECK(segments.size() == curve.breakpoints(curve.domain()).size() - 1);
        double previousEnd = curve.domain().lo;
        for (const BSplineCurve<3> &segment : segments) {
            FK_CHECK(segment.poleCount() == segment.degree() + 1);
            FK_CHECK(segment.domain().lo == previousEnd);
            previousEnd = segment.domain().hi;
            FK_CHECK(sameCurve<3>(curve, segment, segment.domain(), 1e-13));
            // Una Bezier clamped interpola i poli estremi.
            FK_CHECK(near(segment.point(segment.domain().lo), segment.poles().front(), 1e-13));
        }
        FK_CHECK(previousEnd == curve.domain().hi);
    }
}

// Nodi uniformi non clamped: la curva non interpola i poli estremi.
FK_TEST(BSplineUnclampedAndLeftLimit) {
    std::mt19937 rng(25);
    for (int trial = 0; trial < 100; ++trial) {
        const int degree = uniformInt(rng, 1, 5);
        const int poleCount = degree + 1 + uniformInt(rng, 0, 6);
        std::vector<double> knots(poleCount + degree + 1);
        for (std::size_t i = 0; i < knots.size(); ++i) knots[i] = double(i) * 0.5;
        std::vector<Vec3> poles(poleCount);
        for (Vec3 &pole : poles) pole = randomVec<3>(rng, 50.0);
        std::vector<double> weights;
        if (trial % 2) for (int i = 0; i < poleCount; ++i) weights.push_back(uniform(rng, 0.5, 2.0));
        const BSplineCurve<3> curve(degree, knots, poles, weights);
        FK_CHECK(!curve.isClamped());

        const Handle(Geom_BSplineCurve) reference = toOcct(curve);
        const BSplineCurve<3> clamped = curve.clamped();
        FK_CHECK(clamped.isClamped());
        FK_CHECK(clamped.domain().lo == curve.domain().lo && clamped.domain().hi == curve.domain().hi);
        for (int i = 0; i <= 30; ++i) {
            const double t = curve.domain().lo + curve.domain().length() * i / 30.0;
            FK_CHECK(near(curve.point(t), fromOcct(reference->Value(t)), 1e-13));
            FK_CHECK(near(clamped.point(t), curve.point(t), 1e-12));
        }
        double previousEnd = curve.domain().lo;
        for (const BSplineCurve<3> &segment : curve.bezierSegments()) {
            FK_CHECK(segment.domain().lo == previousEnd);
            previousEnd = segment.domain().hi;
            FK_CHECK(sameCurve<3>(curve, segment, segment.domain(), 1e-12));
        }
    }

    // Polilinea (grado 1): nel vertice la derivata sinistra e' quella del lato precedente.
    const BSplineCurve<2> polyline(1, {0, 0, 1, 2, 2}, {Vec2(0, 0), Vec2(1, 0), Vec2(1, 1)});
    FK_CHECK(near(polyline.derivative(1.0), Vec2(0, 1), 0.0));
    Vec2 left[2];
    polyline.evaluateLeft(1.0, 1, left);
    FK_CHECK(near(left[1], Vec2(1, 0), 0.0));
    FK_CHECK(near(left[0], Vec2(1, 0), 0.0));
}

FK_TEST(ConicArcsAreExact) {
    std::mt19937 rng(24);
    for (int trial = 0; trial < 200; ++trial) {
        const Frame3 frame(randomVec<3>(rng, 100.0), randomDirection(rng), randomDirection(rng));
        const double radius = uniform(rng, 0.1, 300.0);
        const Circle<3> circle = makeCircle(frame, radius);
        const double first = uniform(rng, -7.0, 7.0);
        const double sweep = trial == 0 ? kTwoPi : uniform(rng, 0.01, kTwoPi);
        const BSplineCurve<3> nurbs = toBSpline(circle, first, first + sweep);
        FK_CHECK(nurbs.degree() == 2);
        FK_CHECK(nurbs.poleCount() == 2 * int(std::ceil(sweep / kHalfPi - 1e-12)) + 1);
        FK_CHECK(near(nurbs.point(nurbs.domain().lo), circle.point(first), 1e-14));
        FK_CHECK(near(nurbs.point(nurbs.domain().hi), circle.point(first + sweep), 1e-14));
        for (int i = 0; i <= 100; ++i) {
            const double t = nurbs.domain().lo + nurbs.domain().length() * i / 100.0;
            const Vec3 local = frame.toLocal(nurbs.point(t));
            FK_CHECK_NEAR(local.z(), 0.0, 1e-12 * radius + 1e-13);
            FK_CHECK_NEAR(std::hypot(local.x(), local.y()), radius, 1e-14 * radius + 1e-13);
        }

        // Ellisse: (x/a)^2 + (y/b)^2 = 1.
        const double a = radius, b = radius * uniform(rng, 0.05, 1.0);
        const BSplineCurve<3> ellipse = toBSpline(makeEllipse(frame, a, b), first, first + sweep);
        for (int i = 0; i <= 100; ++i) {
            const Vec3 local = frame.toLocal(ellipse.point(ellipse.domain().lo + ellipse.domain().length() * i / 100.0));
            FK_CHECK_NEAR(std::pow(local.x() / a, 2) + std::pow(local.y() / b, 2), 1.0, 1e-13);
        }
    }
    FK_CHECK_THROWS(toBSpline(makeCircle(Vec2(), 1.0), 1.0, 1.0));
}

FK_TEST(BSplineValidation) {
    const std::vector<Vec2> poles{Vec2(0, 0), Vec2(1, 1), Vec2(2, 0)};
    FK_CHECK_THROWS(BSplineCurve<2>(0, {0, 1, 2}, {Vec2(), Vec2()}));
    FK_CHECK_THROWS(BSplineCurve<2>(2, {0, 0, 0, 1, 1}, poles));             // nodi mancanti
    FK_CHECK_THROWS(BSplineCurve<2>(2, {0, 0, 1, 0, 1, 1}, poles));          // non crescenti
    FK_CHECK_THROWS(BSplineCurve<2>(2, {0, 0, 0, 1, 1, 1}, poles, {1, -1, 1}));
    const std::vector<Vec2> five{Vec2(0, 0), Vec2(1, 1), Vec2(2, 0), Vec2(3, 1), Vec2(4, 0)};
    FK_CHECK_THROWS(BSplineCurve<2>(1, {0, 0, 0.5, 0.5, 0.5, 1, 1}, five));  // nodo interno > grado
    const BSplineCurve<2> ok(2, {0, 0, 0, 1, 1, 1}, poles);
    FK_CHECK(ok.isClamped());
    FK_CHECK_THROWS(ok.insertKnot(-0.5));        // fuori dal dominio
    FK_CHECK(ok.insertKnot(0.0).poleCount() == 3);  // estremo gia' clamped: invariata
    FK_CHECK(expandKnots({0.0, 0.5, 1.0}, {3, 1, 3}).size() == 7);
}
