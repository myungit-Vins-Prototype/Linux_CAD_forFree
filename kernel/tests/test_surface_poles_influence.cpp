#include "fk_surface_poles_influence.h"
#include "fk_surface_refit.h"
#include "fk_test.h"
#include "surface_tools_fixture.h"
#include <map>
using namespace ForgeCad::Kernel;
FK_TEST(SurfacePolesSoftInfluenceLocalAndOverlap) {
    const auto source=surface_fixture::patch();
    const auto s=refitSurfacePoles(*std::dynamic_pointer_cast<const BSplineSurface>(source.face(source.faces()[0]).surface),16,24).surface;
    const auto a=surfacePoleInfluence(*s,{{8,12}},3,6);
    std::map<std::pair<int,int>,double> weights;
    for(const auto &p:a){weights[{p.u,p.v}]=p.weight;FK_CHECK(p.weight>0&&p.weight<=1);}
    FK_CHECK(weights[std::make_pair(8,12)]==1);
    FK_CHECK(weights[std::make_pair(8,13)]>weights[std::make_pair(8,15)]);
    FK_CHECK(weights.count({8,19})==0 && weights.count({4,12})==0);
    const auto two=surfacePoleInfluence(*s,{{8,12},{8,13},{8,12}},3,6);
    for(const auto &p:two){FK_CHECK(p.weight<=1);if(p.u==8&&(p.v==12||p.v==13))FK_CHECK(p.weight==1);}
    FK_CHECK(surfacePoleInfluence(*s,{{8,12}},0,0).size()==1);
    FK_CHECK_THROWS(surfacePoleInfluence(*s,{{16,12}},3,6));
    FK_CHECK_THROWS(surfacePoleInfluence(*s,{{8,12}},0,6));
    FK_CHECK_THROWS(surfacePoleInfluence(*s,{{8,12}},33,6));
}
FK_TEST(SurfacePolesSoftInfluencePeriodAndBudget) {
    std::vector<Vec3> points(100*100);
    const auto knots=[](int n){std::vector<double> k{0,0};for(int i=1;i<n;++i)k.push_back(i);k.push_back(n-1);return k;};
    BSplineSurface s(1,1,knots(100),knots(100),100,100,points,{},true,false);
    const auto field=surfacePoleInfluence(s,{{0,50}},2,2);
    bool opposite=false,near=false;
    for(const auto &p:field){if(p.u==99&&p.v==50)opposite=p.weight==1;if(p.u==98&&p.v==50)near=p.weight>0;}
    FK_CHECK(opposite&&near);
    FK_CHECK(surfacePoleInfluence(s,{{0,50}},0,0).size()==1);
    FK_CHECK_THROWS(surfacePoleInfluence(s,{{30,30},{70,70}},32,32));
}
