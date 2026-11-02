#include "fk_loft.h"
#include "fk_sketch_projection.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_curve_ops.h"
#include "fk_curve_algo.h"
#include "fk_surface_algo.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
using namespace ForgeCad::Kernel;
static void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
static CurvePtr<3> bezier(std::vector<Vec3> poles){int p=int(poles.size())-1;std::vector<double> knots(std::size_t(p+1),0);knots.insert(knots.end(),std::size_t(p+1),1);return std::make_shared<BSplineCurve<3>>(p,knots,poles);}
static CurvePtr<3> projection(bool reverse=false){
    CurvePtr<3> a=bezier({{0,0,0},{0,2,0},{4,3,0}}),b=bezier({{0,0,1},{1,0,4},{4,0,2}});
    if(reverse){a=reversedCurve(a);b=reversedCurve(b);}
    auto curves=projectSketchOnSketch({{a,a->domain()}},{0,0,1},{{b,b->domain()}},{0,1,0});
    require(curves.size()==1&&isImplicitSketchProjection(*curves[0]),"fixture guida implicita esatta");return curves[0];
}
static LoftSection section(Vec3 p,bool straight=false,bool spatialWarp=false){
    LoftSection section;
    CurvePtr<3> c=straight?bezier({p,p+Vec3(0,2,0)}):spatialWarp?bezier({p,p+Vec3(0,.5,.3),p+Vec3(.02,1.5,.3),p+Vec3(0,2,0)}):bezier({p,p+Vec3(0,1,.3),p+Vec3(0,2,0)});
    section.path={{c,c->domain()}};return section;
}
static LoftSection mapped(LoftSection s,const Transform3 &transform){for(auto &piece:s.path)piece.curve=std::make_shared<TransformedCurve>(piece.curve,transform);return s;}
static double toEdges(const Body &body,const Vec3 &point){double best=1e100;for(EdgeId edge:body.edges())best=std::min(best,projectPoint(*body.edge(edge).curve,point,body.edge(edge).range).distance);return best;}
static void validate(const Body &body,const std::vector<LoftSection> &sections,const std::vector<std::vector<PathSegment>> &guides){
    require(body.isSheet()&&!body.faces().empty()&&checkBody(body).empty(),"loft guidato B-rep valido");
    for(const auto &section:sections)for(const auto &part:section.path)for(int i=0;i<=12;++i){
        const auto p=part.curve->point(part.range.lo+part.range.length()*i/12.0);double best=1e100;
        for(FaceId face:body.faces())best=std::min(best,projectPoint(*body.face(face).surface,p).distance);
        require(best<1e-9,"sezioni conservate dalla superficie NURBS");
    }
    for(const auto &guide:guides)for(const auto &part:guide)for(int i=0;i<=40;++i){
        const auto p=part.curve->point(part.range.lo+part.range.length()*i/40.0);
        require(toEdges(body,p)<4e-9,"bordo longitudinale segue la guida esatta entro il budget loft preesistente");
    }
    const auto copy=readBodyBinary(writeBodyBinary(body));require(checkBody(copy).empty()&&copy.faces().size()==body.faces().size(),"BodyIO della normale superficie NURBS senza nuovi tipi");
}
template<class F>static void rejects(F function,const char *message){bool failed=false;try{function();}catch(const std::domain_error &e){failed=std::string(e.what()).find(message)!=std::string::npos;}require(failed,message);}
int main(){try{
    int cases=0;const auto source=projection();const auto d=source->domain();const double lo=d.lo+.15*d.length(),hi=d.lo+.85*d.length();
    const auto other=std::make_shared<TransformedCurve>(source,Transform3::translation({0,2,0}));
    LoftOptions options;options.guides={{{source,{lo,hi}}},{{other,{lo,hi}}}};options.guideConditions={{1,1},{1,1}};
    const std::vector<LoftSection> two{section(source->point(lo)),section(source->point(hi))};
    std::cout<<"CASE base"<<std::endl;validate(loftSheet(two,options),two,options.guides);++cases;
    std::cout<<"CASE multi"<<std::endl;auto three=two;three.insert(three.begin()+1,section(source->point((lo+hi)/2)));validate(loftSheet(three,options),three,options.guides);++cases;
    std::cout<<"CASE transform"<<std::endl;const auto transform=Transform3::translation({1200,-800,300})*Transform3::rotation({0,0,0},{.2,.8,.5},.43);
    auto tilted=three;for(auto &s:tilted)s=mapped(s,transform);auto tiltedOptions=options;
    for(auto &guide:tiltedOptions.guides)for(auto &part:guide)part.curve=std::make_shared<TransformedCurve>(part.curve,transform);
    validate(loftSheet(tilted,tiltedOptions),tilted,tiltedOptions.guides);++cases;
    std::cout<<"CASE reverse"<<std::endl;
    // La guida e' orientata contro l'ordine delle sezioni: guideHit deve invertirne il verso.
    const std::vector<LoftSection> backwardSections{two.back(),two.front()};
    validate(loftSheet(backwardSections,options),backwardSections,options.guides);++cases;
    std::cout<<"CASE rank1"<<std::endl;const std::vector<LoftSection> lines{section(source->point(lo),true),section(source->point(hi),true)};validate(loftSheet(lines,options),lines,options.guides);++cases;
    std::cout<<"CASE rank1 coincident plane"<<std::endl;
    const auto pa=bezier({{0,0,0},{0,.5,0},{1,1,0}});
    const auto pb=bezier({{0,0,0},{0,0,0},{0,0,1.0/6},{0,0,.5},{1,0,1}});
    const auto planarParts=projectSketchOnSketch({{pa,{.2,1}}},{0,0,1},{{pb,{.2,1}}},{0,1,0});
    require(planarParts.size()==1 && isImplicitSketchProjection(*planarParts[0]),"fixture implicita planare con riparametrizzazione non affine");
    const auto planar=std::make_shared<TransformedCurve>(planarParts[0],Transform3::rotation({0,0,0},{1,0,0},kPi/4));
    const auto pr=planar->domain();const double pl=pr.lo+.15*pr.length(),ph=pr.lo+.85*pr.length();
    const auto planarOther=std::make_shared<TransformedCurve>(planar,Transform3::translation({2,0,0}));
    const auto lineSection=[&](double t){LoftSection s;auto line=bezier({planar->point(t),planar->point(t)+Vec3(2,0,0)});s.path={{line,line->domain()}};return s;};
    auto planarOptions=options;planarOptions.guides={{{planar,{pl,ph}}},{{planarOther,{pl,ph}}}};
    const std::vector<LoftSection> planarLines{lineSection(pl),lineSection(ph)};
    validate(loftSheet(planarLines,planarOptions),planarLines,planarOptions.guides);++cases;
    std::cout<<"CASE missing"<<std::endl;auto missing=two;missing[0]=section(source->point(lo)+Vec3(0,20,0));rejects([&]{loftSheet(missing,options);},"non incontra");++cases;
    std::cout<<"CASE ambiguous"<<std::endl;const auto roots=planeRoots<3>(*source,d,{0,0,1},2.5,1e-9);require(roots.parameters.size()==2,"fixture doppio attraversamento");
    const auto p=source->point(roots.parameters[0]),q=source->point(roots.parameters[1]);LoftSection ambiguous;
    const auto line=bezier({p,q});ambiguous.path={{line,line->domain()}};
    auto one=options;one.guides={{{source,d}}};one.guideConditions={{1,1}};
    rejects([&]{loftSheet({two[0],ambiguous},one);},"piu' volte");++cases;
    std::cout<<"CASE warped"<<std::endl;auto warped=two;warped[0]=section(source->point(lo),false,true);rejects([&]{loftSheet(warped,options);},"piane certificabili");++cases;
    std::cout<<"CASE far"<<std::endl;const auto far=Transform3::translation({1e12,-1e12,1e12});for(auto &s:warped)s=mapped(s,far);auto farOptions=options;
    for(auto &guide:farOptions.guides)for(auto &part:guide)part.curve=std::make_shared<TransformedCurve>(part.curve,far);
    rejects([&]{loftSheet(warped,farOptions);},"piane certificabili");++cases;
    std::cout<<"PASS "<<cases<<" gruppi loft guide implicite: sezioni esatte, guida entro budget, orientamento, verso, rank1, ambiguita', nonplanare, origine lontana, BodyIO\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
