#include <GCPnts_AbscissaPoint.hxx>
#include <Geom_Ellipse.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <gp_Ax2.hxx>

#include "fk_curve_algo.h"
#include "fk_curve_length_cache.h"
#include <future>
#include <cstring>
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

namespace {
template <int N>
void checkBoundedCandidates(const Curve<N> &curve, const Vec<N> &point, const Interval &range, double maximum) {
    auto expected = projectPointCandidates(curve, point, range);
    expected.erase(std::remove_if(expected.begin(), expected.end(), [&](const auto &p) { return p.distance > maximum; }), expected.end());
    const auto actual = projectPointCandidatesWithin(curve, point, range, maximum);
    FK_CHECK(actual.size() == expected.size());
    for (std::size_t i = 0; i < std::min(actual.size(), expected.size()); ++i) {
        FK_CHECK_NEAR(actual[i].parameter, expected[i].parameter, 0.0);
        FK_CHECK_NEAR(actual[i].distance, expected[i].distance, 0.0);
        FK_CHECK_NEAR(distance(actual[i].point, expected[i].point), 0.0, 0.0);
    }
}

template <int N>
void boundedRandomCurves() {
    std::mt19937 rng(110 + N);
    for (int trial = 0; trial < 24; ++trial) {
        const auto curve = randomBSpline<N>(rng, trial % 2 == 0);
        const auto knots = curve.knots(); const auto poles = curve.poles(); const auto weights = curve.weights();
        const auto domain = curve.domain();
        for (int query = 0; query < 4; ++query) {
            const double t = domain.lo + domain.length() * (query + 1) / 5.0;
            const Vec<N> point = query % 2 ? randomVec<N>(rng, 150) : curve.point(t);
            const Interval range = query == 3 ? Interval{t, t} : query == 2 ? Interval{domain.lo + domain.length() * .2, domain.hi - domain.length() * .1} : domain;
            const auto all = projectPointCandidates(curve, point, range);
            double best = std::numeric_limits<double>::infinity();
            for (const auto &p : all) best = std::min(best, p.distance);
            for (double limit : {0.0, 1e-7, best, std::nextafter(best, std::numeric_limits<double>::infinity()),
                                  best > 0 ? std::nextafter(best, 0.0) : 0.0, std::numeric_limits<double>::infinity()})
                checkBoundedCandidates(curve, point, range, limit);
        }
        FK_CHECK(curve.knots() == knots && curve.weights() == weights);
        for (std::size_t i = 0; i < poles.size(); ++i) FK_CHECK_NEAR(distance(curve.poles()[i], poles[i]), 0, 0);
    }
}
}

FK_TEST(ProjectBoundedCandidatesRetainEveryBranch) {
    boundedRandomCurves<2>(); boundedRandomCurves<3>();
    // Due minimi globali equidistanti della parabola y=x^2; il punto centrale
    // e' invece stazionario a distanza maggiore. Nessuno dei rami puo' sparire.
    const BSplineCurve<2> parabola(2, {0,0,0,1,1,1}, {{-1,1},{0,-1},{1,1}});
    const auto choices = projectPointCandidatesWithin(parabola, Vec2(0,1), {0,1}, .88);
    FK_CHECK(choices.size() == 2);
    if (choices.size() == 2) {
        FK_CHECK_NEAR(choices.front().parameter, (1 - std::sqrt(.5)) / 2, 1e-13);
        FK_CHECK_NEAR(choices.back().parameter, (1 + std::sqrt(.5)) / 2, 1e-13);
        FK_CHECK_NEAR(choices.front().distance, std::sqrt(.75), 1e-13);
    }
}

FK_TEST(ProjectBoundedCandidatesTransformsAndLimits) {
    auto curve = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0,0,0,0,.3,.6,1,1,1,1},
        std::vector<Vec3>{{0,0,0},{1,2,0},{2,-2,1},{3,2,2},{4,0,1},{5,1,0}}, std::vector<double>{1,.01,10,2,.1,1});
    for (const auto &transform : {Transform3::translation({4e5,-3e5,2e5}), Transform3::scaling({}, 1e-5),
                                  Transform3::rotation({}, {1,2,3}, .7) * Transform3::scaling({}, 3.0),
                                  Transform3::reflection({}, {1,1,0}), Transform3::projectionAlong({0,0,1})}) {
        auto moved = std::make_shared<TransformedCurve>(curve, transform);
        const TrimmedCurve<3> trimmed(moved, .1, .9);
        for (double t : {.1,.3,.55,.6,.9}) {
            const auto point = moved->point(t);
            for (double limit : {0.0, 1e-7, 1e-3, 1000.0}) {
                checkBoundedCandidates(*moved, point, {0,1}, limit);
                checkBoundedCandidates(trimmed, point, trimmed.domain(), limit);
            }
        }
    }
    const Line<3> line({0,0,0}, {1,0,0});
    const Circle<3> circle = makeCircle(Frame3(), 3);
    for (double limit : {0.0, 2.0, 10.0}) {
        checkBoundedCandidates(line, Vec3(2,2,0), {-10,10}, limit);
        checkBoundedCandidates(circle, Vec3(5,0,0), circle.domain(), limit);
    }
    FK_CHECK_THROWS(projectPointCandidatesWithin(line, Vec3(), {-1,1}, -1));
    FK_CHECK_THROWS(projectPointCandidatesWithin(line, Vec3(), {-1,1}, std::numeric_limits<double>::quiet_NaN()));
    FK_CHECK_THROWS(projectPointCandidatesWithin(line, Vec3(std::numeric_limits<double>::infinity(),0,0), {-1,1}, 1));
    auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    FK_CHECK_THROWS(projectPointCandidatesWithin(*curve, Vec3(), {0,1}, 1));
}

FK_TEST(CurveLengthCachePreservesQuadratureAndIdentity) {
    const auto bitEqual = [](double a, double b) { return std::memcmp(&a, &b, sizeof(a)) == 0; };
    std::mt19937 rng(115);
    for (int trial = 0; trial < 18; ++trial) {
        const auto curve = randomBSpline<3>(rng, trial % 2 == 0);
        detail::CurveLengthCache cache;
        const auto range = curve.domain();
        for (double tolerance : {1e-9,1e-13,1e-14})
            for (double fraction : {1.0,.7,.85,.7}) {
                const Interval part{range.lo, range.lo + fraction * range.length()};
                FK_CHECK(bitEqual(cache.get(curve, part, tolerance), arcLength(curve, part, tolerance)));
            }
        FK_CHECK(cache.statistics().hits > 0 && cache.statistics().peak <= 8192);
        // Una cache privata per richiesta, input condiviso immutabile.
        auto request = [&] { detail::CurveLengthCache local; return local.get(curve, range, 1e-13); };
        auto first = std::async(std::launch::async, request), second = std::async(std::launch::async, request);
        FK_CHECK(bitEqual(first.get(), second.get()));
        const auto point = curve.point(range.lo + range.length() * .4);
        auto project = [&] { return projectPointCandidatesWithin(curve, point, range, 1e-7); };
        auto p1 = std::async(std::launch::async, project), p2 = std::async(std::launch::async, project);
        const auto a=p1.get(),b=p2.get(); FK_CHECK(a.size()==b.size());
        for(std::size_t i=0;i<std::min(a.size(),b.size());++i)FK_CHECK(bitEqual(a[i].parameter,b[i].parameter));
    }
    const BSplineCurve<3> curve(3,{0,0,0,0,.3,.6,1,1,1,1},{{0,0,0},{1,2,0},{2,-2,1},{3,2,2},{4,0,1},{5,1,0}});
    detail::CurveLengthCache cache;
    FK_CHECK(bitEqual(cache.get(curve,{0,.8},1e-9),arcLength(curve,{0,.8},1e-9)));
    FK_CHECK(cache.statistics().integrations==3);
    FK_CHECK(bitEqual(cache.get(curve,{0,.9},1e-9),arcLength(curve,{0,.9},1e-9)));
    FK_CHECK(cache.statistics().integrations==4 && cache.statistics().hits==2);
    cache.get(curve,{0,.9},1e-8); FK_CHECK(cache.statistics().integrations==7);
    detail::CurveLengthCache tiny(2);
    for(double t:{.7,.8,.9,1.0,.7})FK_CHECK(bitEqual(tiny.get(curve,{0,t},1e-13),arcLength(curve,{0,t},1e-13)));
    FK_CHECK(tiny.statistics().peak<=2 && tiny.statistics().clears>0);
    const Line<3> line({}, {1,0,0}); detail::CurveLengthCache zeros;
    zeros.get(line,{0.0,1},1e-13); zeros.get(line,{-0.0,1},1e-13);
    FK_CHECK(zeros.statistics().integrations==2);
    FK_CHECK_THROWS(detail::CurveLengthCache(0));
    auto token=std::make_shared<std::atomic<bool>>(true);
    {CancellationScope scope(token); FK_CHECK_THROWS(cache.get(curve,{0,.8},1e-9));}
    FK_CHECK(bitEqual(cache.get(curve,{0,.8},1e-9),arcLength(curve,{0,.8},1e-9)));
}

FK_TEST(ProjectBoundedCandidatesHighDegreeAndNarrowSpans) {
    for (int degree : {9,15,31}) {
        std::vector<double> knots(degree+1,0);knots.insert(knots.end(),degree+1,1);
        std::vector<Vec3> poles;std::vector<double> weights;
        for(int i=0;i<=degree;++i) {
            poles.push_back({4e5+i*.2,-3e5+std::sin(i*.7),2e5+std::cos(i*.3)});
            weights.push_back(i%3==0?1e-4:i%3==1?1e4:1);
        }
        const BSplineCurve<3> curve(degree,knots,poles,weights);
        for(double t:{0.0,.37,1.0})for(double maximum:{0.0,1e-7,.001})
            checkBoundedCandidates(curve,curve.point(t),{0,1},maximum);
    }
    const double a=1000,b=std::nextafter(a+1e-10,std::numeric_limits<double>::infinity());
    const BSplineCurve<3> curve(3,{a,a,a,a,b,a+1,a+1,a+1,a+1},
        {{0,0,0},{1,1,0},{2,-1,0},{3,1,0},{4,0,0}}, {1,.001,1000,.1,1});
    for(double t:{a,std::nextafter(a,b),(a+b)*.5,b,a+.8})
        for(double maximum:{0.0,1e-7,.01})checkBoundedCandidates(curve,curve.point(t),curve.domain(),maximum);
}


namespace {
template<int N>
void restrictedMinimumCases() {
    std::mt19937 rng(901+N);
    for (int trial=0;trial<24;++trial) {
        const auto curve=randomBSpline<N>(rng,trial%2==0);
        const auto domain=curve.domain();
        const auto breaks=curve.breakpoints(domain);
        const double middle=breaks[breaks.size()/2];
        for (const auto range : {domain,Interval{domain.lo,middle},Interval{middle,domain.hi},
             Interval{middle,middle},Interval{std::nextafter(middle,domain.lo),std::nextafter(middle,domain.hi)},
             Interval{domain.lo-domain.length(),domain.lo},Interval{domain.hi,domain.hi+domain.length()}}) {
            const Vec<N> p=randomVec<N>(rng,100);
            const auto actual=projectPoint(curve,p,range);
            double expected=std::min(distance(curve.point(range.lo),p),distance(curve.point(range.hi),p));
            for (const auto &candidate : projectPointCandidates(curve,p,range))
                expected=std::min(expected,candidate.distance);
            FK_CHECK_NEAR(actual.distance,expected,1e-12*std::max(1.,expected));
            FK_CHECK_NEAR(distance(actual.point,curve.point(actual.parameter)),0.,0.);
            FK_CHECK(range.contains(actual.parameter));
        }
    }
}
}

FK_TEST(ProjectMinimumRestrictedRangesMatchesExhaustive) {
    restrictedMinimumCases<2>(); restrictedMinimumCases<3>();
}

FK_TEST(ProjectMinimumManySpansAnalytic) {
    constexpr int spans=4096;
    std::vector<double> knots{0,0}; std::vector<Vec3> poles;
    for (int i=0;i<=spans;++i) {
        const double t=double(i)/spans; poles.emplace_back(t,0,0);
        if (i>0&&i<spans) knots.push_back(t);
    }
    knots.insert(knots.end(),2,1);
    const BSplineCurve<3> curve(1,knots,poles);
    const auto check=[&] {
        for (const auto range : {Interval{0,1},Interval{.25,.75},Interval{.5,.5},
             Interval{std::nextafter(.5,0.),std::nextafter(.5,1.)},Interval{0,.01}})
            for (double x : {0.,.125,.3,.5,.75,1.}) {
                const auto result=projectPoint(curve,Vec3(x,2,0),range);
                const double t=range.clamp(x);
                if (std::fabs(result.parameter-t)>1e-13 || distance(result.point,Vec3(t,0,0))>1e-13) return false;
                if (std::fabs(result.distance-std::hypot(t-x,2.))>1e-13) return false;
            }
        return true;
    };
    FK_CHECK(check());
    auto first=std::async(std::launch::async,check),second=std::async(std::launch::async,check);
    FK_CHECK(first.get());FK_CHECK(second.get());
    bool cancelled=false;
    try { CancellationScope scope(std::make_shared<std::atomic<bool>>(true)); projectPoint(curve,Vec3(.5,2,0),{.4,.6}); }
    catch (const OperationCancelled &) { cancelled=true; }
    FK_CHECK(cancelled);
}


// Oracolo analitico indipendente: una spline lunga sulla retta x=t. Le
// interrogazioni attraversano nodi, estremi e intervalli puntuali; i pesi
// costanti non unitari esercitano anche il percorso razionale.
FK_TEST(ProjectBoundedRangeKnotEndpoints) {
    std::vector<double> knots{0.0};
    std::vector<Vec3> poles;
    for (int i = 0; i <= 256; ++i) {
        knots.push_back(double(i));
        poles.push_back(Vec3(double(i), 0, 0));
    }
    knots.push_back(256.0);
    for (bool rational : {false, true}) {
        const BSplineCurve<3> curve(1, knots, poles, rational ? std::vector<double>(poles.size(), 2.0) : std::vector<double>{});
        for (const Interval range : {Interval{0, 0}, Interval{256, 256}, Interval{128, 128},
                                    Interval{127, 129}, Interval{127.25, 128.75},
                                    Interval{128, std::nextafter(128.0, 129.0)}, Interval{0, 256}}) {
            for (double x : {range.lo - .25, range.lo, (range.lo + range.hi) * .5, range.hi, range.hi + .25}) {
                for (double y : {0.0, 1e-7, 2e-7}) {
                    const double expectedT = range.clamp(x);
                    const double expectedDistance = std::hypot(x - expectedT, y);
                    const auto candidates = projectPointCandidatesWithin(curve, Vec3(x, y, 0), range, 1e-7);
                    FK_CHECK(candidates.empty() == (expectedDistance > 1e-7));
                    if (candidates.empty()) continue;
                    const auto closest = std::min_element(candidates.begin(), candidates.end(),
                        [](const auto &a, const auto &b) { return a.distance < b.distance; });
                    FK_CHECK(std::isfinite(closest->parameter) && std::isfinite(closest->distance));
                    FK_CHECK_NEAR(closest->parameter, expectedT, 1e-12);
                    FK_CHECK_NEAR(closest->distance, expectedDistance, 1e-12);
                }
            }
        }
    }
}
