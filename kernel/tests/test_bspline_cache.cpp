#include "fk_test.h"
#include "fk_bspline_surface.h"
#include "cad_drawing_curve_recognition.h"
#include <atomic>
#include <thread>
#include <type_traits>

using namespace ForgeCad::Kernel;
namespace {
// Ogni worker possiede le destinazioni; condivide solo geometria const e cache lazy.
template<class F> void concurrent(F work) {
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) workers.emplace_back([&, i] {
        ready.fetch_add(1);
        while (!start.load()) std::this_thread::yield();
        work(i);
    });
    while (ready.load() != 4) std::this_thread::yield();
    start.store(true);
    for (auto &worker : workers) worker.join();
}
template<int N> BSplineCurve<N> curve(double offset = 0) {
    Vec<N> a, b, c, d;
    for (int i = 0; i < N; ++i) {
        a[i] = offset + i; b[i] = offset + 2 + i;
        c[i] = offset - 1 + i; d[i] = offset + 4 + i;
    }
    return BSplineCurve<N>(2, {0,0,0,.5,1,1,1}, {a,b,c,d}, {1,.7,1.3,1});
}
template<int N> bool same(const BSplineCurve<N> &a, const BSplineCurve<N> &b) {
    if (a.knots() != b.knots() || a.degree() != b.degree() || a.poleCount() != b.poleCount()) return false;
    for (int i = 0; i <= 12; ++i) {
        Vec<N> x[3], y[3]; a.evaluate(i/12., 2, x); b.evaluate(i/12., 2, y);
        for (int j = 0; j < 3; ++j) if (distance(x[j],y[j]) > 1e-12) return false;
    }
    const auto segments = b.cachedBezierSegments();
    for (const auto &segment : *segments) {
        const double t = (segment.domain().lo + segment.domain().hi)*.5;
        if (distance(a.point(t),segment.point(t)) > 1e-12) return false;
    }
    return true;
}
BSplineSurface surface(double offset = 0) {
    std::vector<Vec3> poles;
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) poles.emplace_back(offset+i,j,.2*i*j);
    return BSplineSurface(1,1,{0,0,.5,1,1},{0,0,.5,1,1},3,3,poles);
}
void warm(const BSplineSurface &s) {
    (void)s.cachedBezierPatches(); (void)s.cachedSharpKnotLines(); (void)s.cachedPatchTree();
    (void)s.uIso(.5); (void)s.vIso(.5);
}
bool same(const BSplineSurface &a, const BSplineSurface &b) {
    for (double u : {0.,.25,.5,.75,1.}) for (double v : {0.,.25,.5,.75,1.})
        if (distance(a.point(u,v), b.point(u,v)) > 1e-12) return false;
    const auto patches=b.cachedBezierPatches();
    for (const auto &p:*patches) {
        const double u=(p.uDomain().lo+p.uDomain().hi)*.5, v=(p.vDomain().lo+p.vDomain().hi)*.5;
        if (distance(a.point(u,v),p.point(u,v))>1e-12) return false;
    }
    const auto tree=b.cachedPatchTree();
    if (tree->nodes.empty()) return false;
    const auto p=a.point(.5,.5);
    for (int c=0;c<3;++c) if (p[c]<tree->nodes.front().lo[c]-1e-12 || p[c]>tree->nodes.front().hi[c]+1e-12) return false;
    return distance(a.point(.5,.25),b.uIso(.5)->point(.25)) <= 1e-12
        && distance(a.point(.25,.5),b.vIso(.5)->point(.25)) <= 1e-12;
}
template<int N> void curveValues() {
    for (bool cached : {false,true}) {
        auto source=curve<N>();
        if (cached) (void)source.cachedBezierSegments();
        auto copied=source; auto assigned=curve<N>(100); (void)assigned.cachedBezierSegments();
        assigned=source;
        FK_CHECK(same(source,copied)); FK_CHECK(same(source,assigned));
        assigned=assigned; FK_CHECK(same(source,assigned));
        auto moved=std::move(copied); assigned=std::move(moved);
        FK_CHECK(same(source,assigned));
        const auto retained=assigned.cachedBezierSegments();
        assigned=curve<N>(200);
        FK_CHECK(distance(retained->front().point(.25),source.point(.25))<1e-12);
    }
}
}
FK_TEST(BSplineCacheCurveValueSemantics) {
    static_assert(std::is_nothrow_move_constructible_v<BSplineCurve<2>>);
    static_assert(std::is_nothrow_move_assignable_v<BSplineCurve<2>>);
    static_assert(std::is_nothrow_move_constructible_v<BSplineCurve<3>>);
    static_assert(std::is_nothrow_move_assignable_v<BSplineCurve<3>>);
    curveValues<2>(); curveValues<3>();
}
FK_TEST(BSplineCacheSurfaceValueSemantics) {
    static_assert(std::is_nothrow_move_constructible_v<BSplineSurface>);
    static_assert(std::is_nothrow_move_assignable_v<BSplineSurface>);
    for (bool cached : {false,true}) {
        auto source=surface(); if (cached) warm(source);
        auto copied=source; auto assigned=surface(100); warm(assigned); assigned=source;
        FK_CHECK(same(source,copied)); FK_CHECK(same(source,assigned));
        assigned=assigned; FK_CHECK(same(source,assigned));
        auto moved=std::move(copied); assigned=std::move(moved); FK_CHECK(same(source,assigned));
        const auto retained=assigned.cachedBezierPatches(); assigned=surface(200);
        FK_CHECK(distance(retained->front().point(.25,.25),source.point(.25,.25))<1e-12);
    }
}
FK_TEST(BSplineCacheConcurrentSnapshots) {
    std::atomic<bool> valid{true};
    for (int round=0;round<32;++round) {
        const auto a=curve<2>(); const auto b=curve<3>(); const auto s=surface();
        concurrent([&](int worker) {
            for (int i=0;i<12;++i) {
                if (worker==0) { (void)a.cachedBezierSegments(); (void)b.cachedBezierSegments(); warm(s); }
                else {
                    auto ac=a.clamped(); auto bc=b.clamped(); auto sc=s;
                    auto aa=curve<2>(100); auto ba=curve<3>(100); auto sa=surface(100);
                    aa=a; ba=b; sa=s;
                    if (!same(a,ac)||!same(b,bc)||!same(s,sc)||!same(a,aa)||!same(b,ba)||!same(s,sa)) valid=false;
                }
            }
        });
    }
    FK_CHECK(valid.load());
}
FK_TEST(BSplineCacheConcurrentCircleRecognition) {
    auto base=conicArcToBSpline<3>(Vec3(2,-3,4),Vec3(10,0,0),Vec3(0,10,0),0,kTwoPi);
    for (int i=1;i<160;++i) if (i%40) base=base.insertKnot(kTwoPi*i/160);
    std::atomic<bool> valid{true};
    for (int round=0;round<24;++round) {
        // Ricostruzione dai dati geometrici: ogni giro parte con cache vuota.
        const BSplineCurve<3> shared(base.degree(),base.knots(),base.poles(),base.weights());
        concurrent([&](int) {
            const auto circle=ForgeCad::drawingRecognizeCircle(shared,1e-6);
            if (!circle || distance(circle->center,Vec3(2,-3,4))>1e-10 || std::abs(norm(circle->first)-10)>1e-10) valid=false;
        });
    }
    FK_CHECK(valid.load());
}

FK_TEST(BSplineCacheSurfaceInstanceAndOwnerLifetime) {
    std::shared_ptr<const std::vector<BSplineSurface>> patches;
    std::shared_ptr<const std::vector<BSplineCurve<3>>> segments;
    {
        auto c=curve<3>(); segments=c.cachedBezierSegments();
        auto basis=std::make_shared<const BSplineSurface>(surface());
        const auto placement=Transform3::translation(Vec3(7,-5,3));
        const BSplineSurface instance(basis,placement);
        warm(instance);
        auto copied=instance; auto assigned=surface(100); warm(assigned); assigned=instance;
        FK_CHECK(copied.basis()==basis && assigned.basis()==basis);
        FK_CHECK(same(instance,copied)); FK_CHECK(same(instance,assigned));
        FK_CHECK(distance(assigned.point(.25,.75),placement.applyToPoint(basis->point(.25,.75)))<1e-12);
        patches=assigned.cachedBezierPatches();
        std::vector<Vec3> poles;
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) poles.emplace_back(i,j,0);
        const BSplineSurface periodic(1,1,{0,0,.5,1,1},{0,0,.5,1,1},3,3,poles,{},true,true);
        assigned=periodic;
        FK_CHECK(assigned.isUPeriodic() && assigned.isVPeriodic());
        FK_CHECK(assigned.uPeriod()==1 && assigned.vPeriod()==1);
        copied=assigned; FK_CHECK(copied.isUPeriodic() && copied.isVPeriodic());
    }
    FK_CHECK(distance(segments->front().point(.25),curve<3>().point(.25))<1e-12);
    FK_CHECK(distance(patches->front().point(.25,.25),surface().point(.25,.25)+Vec3(7,-5,3))<1e-12);
}
