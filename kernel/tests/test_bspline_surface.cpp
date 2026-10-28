#include "fk_test_util.h"
#include "fk_cancel.h"
#include <array>
#include <cstring>
#include <future>

using namespace fktest;

FK_TEST(BSplineSurfaceMatchesOcct) {
    std::mt19937 rng(60);
    for (int trial = 0; trial < 300; ++trial) {
        const BSplineSurface surface = randomBSplineSurface(rng, trial % 2 == 1);
        const Handle(Geom_BSplineSurface) reference = toOcct(surface);
        const Interval uDomain = surface.uDomain(), vDomain = surface.vDomain();
        constexpr int order = 4;
        Vec3 d[(order + 1) * (order + 1)];
        for (int i = 0; i <= 15; ++i) {
            // Anche esattamente sui nodi.
            const double u = i % 5 == 0 ? surface.uKnots()[uniformInt(rng, surface.uDegree(), surface.uPoleCount())]
                                        : uniform(rng, uDomain.lo, uDomain.hi);
            const double v = i % 3 == 0 ? surface.vKnots()[uniformInt(rng, surface.vDegree(), surface.vPoleCount())]
                                        : uniform(rng, vDomain.lo, vDomain.hi);
            surface.evaluate(u, v, order, d);
            FK_CHECK(near(d[0], fromOcct(reference->Value(u, v)), 1e-13));
            for (int k = 0; k <= order; ++k)
                for (int l = 0; k + l <= order; ++l)
                    if (k + l > 0)
                        FK_CHECK(near(d[Surface::derivativeIndex(k, l, order)], fromOcct(reference->DN(u, v, k, l)),
                                      1e-12 * std::pow(10.0, k + l)));

            // Il percorso del solo punto deve rispettare anche estremi e nodi.
            Vec3 point;
            surface.evaluate(u, v, 0, &point);
            FK_CHECK(near(point, fromOcct(reference->Value(u, v)), 1e-13));
            FK_CHECK(std::memcmp(&point, &d[0], sizeof(Vec3)) == 0);

            // Isoparametriche esatte.
            FK_CHECK(near(surface.uIsoCurve(u).point(v), d[0], 1e-13));
            FK_CHECK(near(surface.vIsoCurve(v).point(u), d[0], 1e-13));
            FK_CHECK(near(surface.uIso(u)->point(v), d[0], 1e-13));
            FK_CHECK(near(surface.vIso(v)->point(u), d[0], 1e-13));
            FK_CHECK(near(surface.uIsoCurve(u).derivative(v, 1), d[Surface::derivativeIndex(0, 1, order)], 1e-11));
            FK_CHECK(near(surface.vIsoCurve(v).derivative(u, 1), d[Surface::derivativeIndex(1, 0, order)], 1e-11));
        }
    }
}

FK_TEST(BSplineSurfaceBezierPatches) {
    std::mt19937 rng(61);
    for (int trial = 0; trial < 100; ++trial) {
        const BSplineSurface surface = randomBSplineSurface(rng, trial % 2 == 0);
        const std::vector<BSplineSurface> patches = surface.bezierPatches();
        const std::size_t expected = (surface.uBreakpoints(surface.uDomain()).size() - 1)
                                   * (surface.vBreakpoints(surface.vDomain()).size() - 1);
        FK_CHECK(patches.size() == expected);
        for (const BSplineSurface &patch : patches) {
            FK_CHECK(patch.uPoleCount() == patch.uDegree() + 1 && patch.vPoleCount() == patch.vDegree() + 1);
            for (int i = 0; i <= 4; ++i)
                for (int j = 0; j <= 4; ++j) {
                    const double u = patch.uDomain().lo + patch.uDomain().length() * i / 4;
                    const double v = patch.vDomain().lo + patch.vDomain().length() * j / 4;
                    FK_CHECK(near(patch.point(u, v), surface.point(u, v), 1e-12));
                }
        }
    }
}

FK_TEST(BSplineSurfaceValidation) {
    const std::vector<Vec3> poles(4, Vec3());
    FK_CHECK_THROWS(BSplineSurface(1, 1, {0, 0, 1, 1}, {0, 0, 1, 1}, 2, 2, std::vector<Vec3>(3)));  // poli mancanti
    FK_CHECK_THROWS(BSplineSurface(1, 1, {0, 0, 1}, {0, 0, 1, 1}, 2, 2, poles));                  // nodi u
    FK_CHECK_THROWS(BSplineSurface(1, 1, {0, 0, 1, 1}, {0, 0, 1, 1}, 2, 2, poles, {1, 1, 0, 1}));   // peso nullo
    // Bilineare: S(u, v) = (u, v, u v).
    const BSplineSurface bilinear(1, 1, {0, 0, 1, 1}, {0, 0, 1, 1}, 2, 2,
                                  {Vec3(0, 0, 0), Vec3(0, 1, 0), Vec3(1, 0, 0), Vec3(1, 1, 1)});
    FK_CHECK(near(bilinear.point(0.25, 0.5), Vec3(0.25, 0.5, 0.125), 1e-16));
    Vec3 d[9];
    bilinear.evaluate(0.25, 0.5, 2, d);
    FK_CHECK(near(d[Surface::derivativeIndex(1, 1, 2)], Vec3(0, 0, 1), 1e-15));
    FK_CHECK(near(bilinear.normal(0.0, 0.0), Vec3(0, 0, 1), 1e-15));
}

// Oracolo indipendente: superficie di Bezier tensoriale calcolata in long double.
FK_TEST(BSplineSurfaceLowOrdersBernstein) {
    const auto bernstein = [](int p, int i, long double t) {
        long double choose = 1;
        for (int k = 1; k <= i; ++k) choose *= (p - k + 1) / (long double)k;
        return choose * std::pow(t, i) * std::pow(1-t, p-i);
    };
    for (const auto degrees : {std::pair<int,int>{1,1}, {2,3}, {3,5}, {5,3}, {7,9}, {15,2}, {31,3}})
        for (bool rational : {false,true}) {
            const int p = degrees.first, q = degrees.second;
            std::vector<double> u(p+1,0), v(q+1,0), weights;
            u.insert(u.end(),p+1,1); v.insert(v.end(),q+1,1);
            std::vector<Vec3> poles;
            for (int i=0;i<=p;++i) for (int j=0;j<=q;++j) {
                poles.push_back(Vec3(i*.7+j*.3, j*.9-i*.2, std::sin(i+2*j)));
                if (rational) weights.push_back(std::pow(10.,((i+3*j)%9-4)*.5));
            }
            const BSplineSurface s(p,q,u,v,p+1,q+1,poles,weights);
            for (double x : {0.,1e-12,.17,.5,.83,1.-1e-12,1.})
                for (double y : {0.,1e-12,.21,.5,.79,1.-1e-12,1.}) {
                    long double sum[3]{}, total=0;
                    for (int i=0;i<=p;++i) for (int j=0;j<=q;++j) {
                        const long double w=bernstein(p,i,x)*bernstein(q,j,y)*s.weight(i,j);
                        total+=w;
                        for (int c=0;c<3;++c) sum[c]+=w*s.pole(i,j)[c];
                    }
                    Vec3 expected;
                    for (int c=0;c<3;++c) expected[c]=double(rational?sum[c]/total:sum[c]);
                    for (int order : {0,1,2,4}) {
                        Vec3 d[25]; s.evaluate(x,y,order,d);
                        FK_CHECK(near(d[0],expected,2e-13));
                    }
                }
        }
}

FK_TEST(BSplineSurfaceLowOrdersBoundsConcurrentAndCancelled) {
    // Diagonali corte, nodi ripetuti e limiti a un ulp: il riferimento e' il
    // percorso generale di ordine4, che resta indipendente dalle specializzazioni.
    const std::vector<double> u{0,0,0,0,.2,.2,.2,.2+1e-9,.7,1,1,1,1};
    const std::vector<double> v{0,0,0,0,0,0,.3,.3,.3,.3,.3,.8,1,1,1,1,1,1};
    const int n=int(u.size())-4, m=int(v.size())-6;
    for (bool rational : {false,true}) {
        std::vector<Vec3> poles; std::vector<double> weights;
        for (int i=0;i<n;++i) for (int j=0;j<m;++j) {
            poles.emplace_back(i*.7,j*.3,std::sin(i+j));
            if (rational) weights.push_back(std::pow(10.,((i+j)%9-4)*.5));
        }
        const BSplineSurface s(3,5,u,v,n,m,poles,weights);
        std::vector<std::pair<double,double>> points;
        std::vector<std::array<Vec3,25>> expected;
        for (double x : {0.,.2,std::nextafter(.2,0.),std::nextafter(.2,1.),.2+1e-9,.7,1.,-.01,1.01})
            for (double y : {0.,.3,std::nextafter(.3,0.),std::nextafter(.3,1.),.8,1.,-.01,1.01}) {
                points.emplace_back(x,y); expected.emplace_back();
                s.evaluate(x,y,4,expected.back().data());
            }
        const auto marker = [](const Vec3 &p) { return p.x()==123 && p.y()==456 && p.z()==789; };
        const auto check = [&] {
            for (int repeat=0;repeat<3;++repeat) for (std::size_t t=0;t<points.size();++t)
                for (int order : {0,1,2}) {
                    std::array<Vec3,11> d; d.fill(Vec3(123,456,789));
                    s.evaluate(points[t].first,points[t].second,order,d.data()+1);
                    if (!marker(d.front())) return false;
                    for (int slot=(order+1)*(order+1)+1;slot<11;++slot)
                        if (!marker(d[slot])) return false;
                    for (int k=0;k<=order;++k) for (int l=0;k+l<=order;++l)
                        if (std::memcmp(&d[1+Surface::derivativeIndex(k,l,order)],
                                        &expected[t][Surface::derivativeIndex(k,l,4)],sizeof(Vec3))) return false;
                }
            return true;
        };
        auto first=std::async(std::launch::async,check), second=std::async(std::launch::async,check);
        FK_CHECK(first.get()); FK_CHECK(second.get());
        for (int order : {0,1,2,4}) {
            std::array<Vec3,25> d; d.fill(Vec3(123,456,789)); bool cancelled=false;
            try { CancellationScope scope(std::make_shared<std::atomic<bool>>(true)); s.evaluate(.4,.5,order,d.data()); }
            catch (const OperationCancelled &) { cancelled=true; }
            FK_CHECK(cancelled);
            FK_CHECK(std::all_of(d.begin(),d.end(),marker));
        }
    }
}
