#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_loft.h"
#include "fk_loft_guide_constraints.h"
#include "fk_surface_algo.h"
#include <cmath>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
namespace {
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
BSplineCurvePtr<3> bezier(std::vector<Vec3> points){const int p=int(points.size())-1;std::vector<double> knots(p+1,0);knots.insert(knots.end(),p+1,1);return std::make_shared<BSplineCurve<3>>(p,knots,points);}
LoftSection section(double z){LoftSection s;s.path={{bezier({{0,0,z},{3,0,z},{7,1,z},{10,1,z}}),{0,1}}};return s;}
void exactPatchJoins(const BSplineSurface &surface) {
    const auto patches=surface.cachedBezierPatches();double euclidean=0,homogeneous=0,weightError=0;
    const auto homogeneousJets=[](const BSplineSurface &patch,double v) {
        std::vector<Vec3> numerator,weight;
        for(int j=0;j<patch.vPoleCount();++j) {
            double b=std::ldexp(1.,-patch.uDegree()),w=0;Vec3 h;
            for(int i=0;i<patch.uPoleCount();++i) {
                const double coefficient=b*patch.weight(i,j);h+=coefficient*patch.pole(i,j);w+=coefficient;
                b*=double(patch.uDegree()-i)/(i+1);
            }
            numerator.push_back(h);weight.push_back({w,0,0});
        }
        std::array<Vec3,6> jet;
        BSplineCurve<3>(patch.vDegree(),patch.vKnots(),numerator).evaluate(v,2,jet.data());
        BSplineCurve<3>(patch.vDegree(),patch.vKnots(),weight).evaluate(v,2,jet.data()+3);
        return jet;
    };
    for(std::size_t i=1;i<patches->size();++i) {
        const auto &a=(*patches)[i-1],&b=(*patches)[i];
        if(a.uDomain().lo!=b.uDomain().lo||a.uDomain().hi!=b.uDomain().hi)continue;
        const double u=(a.uDomain().lo+a.uDomain().hi)/2,v=a.vDomain().hi;
        Vec3 da[9],db[9];a.evaluate(u,v,2,da);b.evaluate(u,v,2,db);
        for(int k=0;k<=2;++k)for(int l=0;k+l<=2;++l) {
            const auto j=Surface::derivativeIndex(k,l,2);
            euclidean=std::max(euclidean,distance(da[j],db[j])/(1+std::max(norm(da[j]),norm(db[j]))));
        }
        const auto ha=homogeneousJets(a,v),hb=homogeneousJets(b,v);
        for(int j=0;j<3;++j) {
            homogeneous=std::max(homogeneous,distance(ha[j],hb[j])/(1+std::max(norm(ha[j]),norm(hb[j]))));
            weightError=std::max(weightError,distance(ha[j+3],hb[j+3]));
        }
    }
    std::cout<<" jetC2="<<euclidean<<" homogeneousC2="<<homogeneous<<" weightC2="<<weightError<<'\n';
    require(euclidean<1e-7&&homogeneous<1e-7&&weightError<1e-8,"exact one-sided Euclidean and homogeneous jets are C2 at V joins");
}
void normalAndReverse(){
    LoftOptions options;options.guides={{{bezier({{0,0,0},{0,2,3},{0,2,7},{0,0,10}}),{0,1}}}};
    LoftGuideCondition control;control.crossMode=1;control.profileNormal={1,0,0};options.guideConditions={control};
    const auto body=loftSheet({section(0),section(10)},options);require(checkBody(body).empty(),"normal guide builds valid B-rep");
    const auto &s=*body.face(body.faces().front()).surface;
    for(int i=0;i<=40;++i){Vec3 jet[4];s.evaluate(0,i/40.,1,jet);require(norm(cross(normalized(jet[Surface::derivativeIndex(1,0,1)]),Vec3(1,0,0)))<1e-8,"guide transverse derivative normal to sketch plane");}
    options.guideConditions[0].profileNormal={-1,0,0};options.guideConditions[0].reverse=true;
    const auto reversed=loftSheet({section(0),section(10)},options);require(checkBody(reversed).empty(),"reverse fixes opposite plane orientation");
    options.guideConditions[0].reverse=false;bool rejected=false;
    try{loftSheet({section(0),section(10)},options);}catch(const std::exception &error){rejected=std::string(error.what()).find("loft-section=0")!=std::string::npos&&std::string(error.what()).find("loft-guide=0")!=std::string::npos;}
    require(rejected,"incompatible direction identifies guide and section");
    auto twoGuides=options;
    twoGuides.guideConditions[0].crossMode=0;
    twoGuides.guides.push_back({{bezier({{10,1,0},{10,1,10}}),{0,1}}});
    LoftGuideCondition opposite;opposite.crossMode=1;opposite.profileNormal={-1,0,0};twoGuides.guideConditions.push_back(opposite);
    rejected=false;try{loftSheet({section(0),section(10)},twoGuides);}catch(const std::exception &error){rejected=std::string(error.what()).find("loft-guide=1")!=std::string::npos;}
    require(rejected,"right guide error identifies second guide");
    options.guideConditions[0].profileNormal={0,0,1};rejected=false;
    try{loftSheet({section(0),section(10)},options);}catch(const std::exception &error){rejected=std::string(error.what()).find("piano del profilo")!=std::string::npos;}
    require(rejected,"nonplanar guide rejected for wrong profile plane");
}
void sourceCornerPreserved(){
    std::vector<double> uKnots{0,0,0,0,1,1,1,1},vKnots(6,0);
    vKnots.insert(vKnots.end(),5,.5);vKnots.insert(vKnots.end(),6,1);
    std::vector<Vec3> poles;
    for(int i=0;i<4;++i)for(int j=0;j<11;++j) {
        const double z=j<=5?j:5+(j-5),y=j<=5?.4*j:2-.4*(j-5);
        poles.push_back({10.*i/3,y,z});
    }
    auto base=std::make_shared<BSplineSurface>(3,5,uKnots,vKnots,4,11,poles);
    LoftGuideCondition normal;normal.crossMode=1;normal.profileNormal={1,0,0};
    const auto constrained=detail::constrainLoftGuides(base,&normal,nullptr,{0,.5,1},10);
    for(int i=0;i<=100;++i)require(distance(base->point(0,i/100.),constrained->point(0,i/100.))<1e-12,"true source guide corner retains exact locus");
    const double before=norm(cross(base->normal(0,.5-1e-8),base->normal(0,.5+1e-8)));
    const double after=norm(cross(constrained->normal(0,.5-1e-8),constrained->normal(0,.5+1e-8)));
    require(before>.1&&std::fabs(before-after)<1e-10,"true guide corner is retained, not smoothed by correction field");
}
void transverseShapeIndependentOfDegree(){
    auto edge=bezier({{0,0,0},{0,0,10}});
    auto far=bezier({{10,0,0},{10,4,10./3},{10,4,20./3},{10,0,10}});
    auto support=std::make_shared<const Body>(ruledSurface({{edge,{0,1}}},{{far,{0,1}}}));
    LoftGuideCondition condition;condition.crossMode=2;condition.faces={{support,support->faces().front()}};
    std::vector<BSplineSurfacePtr> results;
    for(int degree:{3,34}) {
        std::vector<double> u(degree+1,0),v(6,0);u.insert(u.end(),degree+1,1);v.insert(v.end(),6,1);
        std::vector<Vec3> poles;
        // Stesso piano e stessa parametrizzazione, elevazione esatta del grado.
        for(int i=0;i<=degree;++i)for(int j=0;j<=5;++j)poles.push_back({10.*i/degree,0,2.*j});
        auto base=std::make_shared<BSplineSurface>(degree,5,u,v,degree+1,6,poles);
        results.push_back(detail::constrainLoftGuides(base,&condition,nullptr,{0,1},10));
    }
    double worst=0;
    for(int i=0;i<=24;++i)for(int j=0;j<=40;++j) {
        const double u=i/24.,v=j/40.;
        worst=std::max(worst,distance(results[0]->point(u,v),results[1]->point(u,v)));
        Vec3 jet[9];results[1]->evaluate(u,v,2,jet);
        const auto du=jet[Surface::derivativeIndex(1,0,2)],dv=jet[Surface::derivativeIndex(0,1,2)];
        require(norm(cross(du,dv))>1,"constrained elevated surface stays regular");
    }
    std::cout<<" degreeInvariantError="<<worst<<'\n';
    require(worst<1e-9,"G1 transverse shape is independent of exact degree elevation");
}
void tangentFaces(){
    // La faccia ha un bordo verticale rettilineo ma il piano tangente ruota
    // lungo quel bordo. Una loft libera piana non puo' soddisfare la G1.
    auto edge=bezier({{0,0,0},{0,0,10}});
    auto far=bezier({{10,0,0},{10,4,10./3},{10,4,20./3},{10,0,10}});
    auto support=std::make_shared<const Body>(ruledSurface({{edge,{0,1}}},{{far,{0,1}}}));
    LoftOptions options;options.guides={{{edge,{0,1}}}};
    LoftGuideCondition control;control.crossMode=2;control.faces={{support,support->faces().front()}};options.guideConditions={control};
    auto freeOptions=options;freeOptions.guideConditions[0].crossMode=0;
    const auto freeBody=loftSheet({section(0),section(10)},freeOptions);
    const auto &freeSurface=*freeBody.face(freeBody.faces().front()).surface;
    const auto &supportSurface=*support->face(support->faces().front()).surface;
    const auto uvFree=projectPoint(supportSurface,freeSurface.point(0,.5));
    require(norm(cross(freeSurface.normal(0,.5),supportSurface.normal(uvFree.u,uvFree.v)))>.1,"negative control without guide constraint violates G1");
    const auto body=loftSheet({section(0),section(10)},options);require(checkBody(body).empty(),"tangent guide builds valid B-rep");
    const auto &s=*body.face(body.faces().front()).surface;const auto &face=*support->face(support->faces().front()).surface;
    double worst=0;
    for(int i=0;i<=100;++i){const double v=i/100.;const auto p=s.point(0,v);const auto uv=projectPoint(face,p);worst=std::max(worst,norm(cross(s.normal(0,v),face.normal(uv.u,uv.v))));require(distance(p,edge->point(v))<1e-8,"guide unchanged by transverse control");}
    std::cout<<" tangentMaxSine="<<worst<<'\n';require(worst<1e-4,"G1 along whole adjacent face boundary");
    for(double v:{0.,1.})for(int i=0;i<=100;++i){const auto expected=section(10*v).path.front().curve->point(i/100.);require(distance(s.point(i/100.,v),expected)<1e-9,"exact sections conserved");}
    auto middle=section(5);middle.path={{bezier({{0,0,5},{3,.9,5},{7,1,5},{10,1,5}}),{0,1}}};
    const auto three=loftSheet({section(0),middle,section(10)},options);
    const auto &threeSurface=*three.face(three.faces().front()).surface;
    for(int i=0;i<=100;++i)require(distance(threeSurface.point(i/100.,.5),middle.path.front().curve->point(i/100.))<1e-9,"intermediate section conserved under guide G1");
    double normalJump=0;
    for(double v:threeSurface.vBreakpoints(threeSurface.vDomain()))if(v>0&&v<1)
        for(int u=1;u<20;++u)normalJump=std::max(normalJump,norm(cross(threeSurface.normal(u/20.,v-1e-8),threeSurface.normal(u/20.,v+1e-8))));
    std::cout<<" normalJump="<<normalJump<<'\n';require(normalJump<1e-6,"transverse fit does not introduce internal normal jumps");
    exactPatchJoins(static_cast<const BSplineSurface &>(threeSurface));
    auto rationalSection=[](double z,double y) {
        LoftSection s;
        s.path={{std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
            std::vector<Vec3>{{0,0,z},{3,y,z},{7,1,z},{10,1,z}},std::vector<double>{1,.7,1.3,1}),{0,1}}};
        return s;
    };
    const auto rational=loftSheet({rationalSection(0,0),rationalSection(5,.9),rationalSection(10,0)},options);
    const auto &rationalSurface=static_cast<const BSplineSurface &>(*rational.face(rational.faces().front()).surface);
    require(rationalSurface.isRational(),"rational profiles retain weights under guide constraints");
    exactPatchJoins(rationalSurface);
    options.startContinuity=2;options.endContinuity=2;
    bool jetRejected=false;
    try{loftSheet({section(0),section(10)},options);}catch(const std::exception&){jetRejected=true;}
    require(jetRejected,"incompatible endpoint jets are rejected instead of overwritten");
    options.startContinuity=options.endContinuity=0;
    // Riferimenti espliciti errati non vengono ricercati su altre facce.
    auto wrong=std::make_shared<const Body>(ruledSurface({{bezier({{5,0,0},{5,0,10}}),{0,1}}},{{bezier({{8,0,0},{8,0,10}}),{0,1}}}));
    options.guideConditions[0].faces={{wrong,wrong->faces().front()}};bool rejected=false;
    try{loftSheet({section(0),section(10)},options);}catch(const std::exception&){rejected=true;}require(rejected,"face not incident to guide rejected");
}
}
int main(){try{normalAndReverse();sourceCornerPreserved();transverseShapeIndependentOfDegree();tangentFaces();std::cout<<"PASS loft guide normal, reverse, adjacent-face tangency, sections and invalid inputs\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
