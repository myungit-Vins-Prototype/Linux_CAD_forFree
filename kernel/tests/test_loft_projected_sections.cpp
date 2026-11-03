#include "fk_loft.h"
#include "fk_bspline.h"
#include "fk_loft_sections.h"
#include "fk_sketch_projection.h"
#include "fk_curve_ops.h"
#include "fk_curve_algo.h"
#include "fk_surface_algo.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
using namespace ForgeCad::Kernel;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static CurvePtr<3> bezier(std::vector<Vec3> poles){int p=int(poles.size())-1;std::vector<double> k(p+1,0);k.insert(k.end(),p+1,1);return std::make_shared<BSplineCurve<3>>(p,k,poles);}
static CurvePtr<3> projection(){
    auto a=bezier({{0,0,0},{0,2,0},{4,3,0}}),b=bezier({{0,0,1},{1,0,4},{4,0,2}});
    auto curves=projectSketchOnSketch({{a,a->domain()}},{0,0,1},{{b,b->domain()}},{0,1,0});
    require(curves.size()==1&&isImplicitSketchProjection(*curves[0]),"fixture implicita non razionale");return curves[0];
}
static LoftSection section(CurvePtr<3> curve){LoftSection s;s.path={{curve,curve->domain()}};return s;}
static void validate(const std::vector<LoftSection> &sections,const LoftOptions &options={}){
    const auto body=loftSheet(sections,options);require(body.isSheet()&&!body.faces().empty()&&checkBody(body).empty(),"loft da sezioni proiettate B-rep valida");
    double maximum=0;
    for(const auto &s:sections) {
        auto path=s.path;
        if(path.empty())for(const auto &piece:s.loop.segments)path.push_back({embedCurve(piece.curve,s.frame),piece.range});
        for(const auto &piece:path)for(int i=0;i<=101;++i){
        const auto p=piece.curve->point(piece.range.lo+piece.range.length()*i/101.);double best=1e100;
        for(auto face:body.faces())best=std::min(best,projectPoint(*body.face(face).surface,p).distance);
        maximum=std::max(maximum,best);
    }
    }
    require(maximum<4e-9,"loft conserva le sezioni entro il budget di costruzione e verifica");
    const auto restored=readBodyBinary(writeBodyBinary(body));require(checkBody(restored).empty()&&restored.faces().size()==body.faces().size(),"cache B-rep serializzabile senza nuovi tipi");
    std::cout<<" sectionError="<<maximum<<std::endl;
}
int main(){try{
    int cases=0;const auto source=projection();const auto d=source->domain();
    std::vector<Vec3> before;for(int i=0;i<=51;++i)before.push_back(source->point(d.lo+d.length()*i/51.));
    const auto shifted=[&](double y){return std::make_shared<TransformedCurve>(source,Transform3::translation({0,y,0}));};
    std::vector<LoftSection> two{section(source),section(shifted(5))};
    std::cout<<"CASE two";validate(two);++cases;
    auto three=two;three.insert(three.begin()+1,section(shifted(2)));std::cout<<"CASE three";validate(three);++cases;
    LoftOptions ruled;ruled.ruled=true;std::cout<<"CASE ruled";validate(three,ruled);++cases;
    auto trim=std::make_shared<TrimmedCurve<3>>(source,d.lo+.13*d.length(),d.lo+.83*d.length());
    const auto transform=Transform3::translation({1200,-800,300})*Transform3::rotation({0,0,0},{.2,.8,.5},.43);
    std::vector<LoftSection> wrapped{section(std::make_shared<TransformedCurve>(trim,transform)),section(std::make_shared<TransformedCurve>(std::make_shared<TransformedCurve>(trim,Transform3::translation({0,5,0})),transform))};
    std::cout<<"CASE trimmed transformed";validate(wrapped);++cases;
    const double matrix[3][3]={{2,.3,0},{0,.6,.2},{0,0,1.4}};
    const auto affine=Transform3::fromParts(matrix,{20,-30,40});
    std::cout<<"CASE affine";validate({section(std::make_shared<TransformedCurve>(source,affine)),section(std::make_shared<TransformedCurve>(shifted(5),affine))});++cases;
    LoftSection image;image.loop.segments.push_back({std::make_shared<SketchProjectionImage>(source,d,Frame3()),d});
    LoftSection imageEnd=image;imageEnd.frame=Frame3({0,0,5},{0,0,1},{1,0,0});
    std::cout<<"CASE sketch reference";validate({image,imageEnd});++cases;
    const auto first=bezier({{0,0,0},{0,2,0},{4,3,0}}),second=bezier({{4,3,0},{5,6,0},{8,4,0}}),other=bezier({{0,0,1},{2,0,4},{8,0,2}});
    const auto joined=projectSketchOnSketch({{first,{0,1}},{second,{0,1}}},{0,0,1},{{other,{0,1}}},{0,1,0});
    require(joined.size()==1&&isImplicitSketchProjection(*joined[0]),"fixture proiezione implicita a piu tratti");
    const auto composite=joined[0];const auto domain=composite->domain();const auto nodes=composite->breakpoints(domain);require(nodes.size()>2,"corner interno della proiezione");
    const auto fitted=detail::prepareLoftSections({section(composite)})[0].path[0].curve;
    for(std::size_t i=1;i+1<nodes.size();++i){Vec3 a[3],b[3];composite->evaluateLeft(nodes[i],2,a);fitted->evaluateLeft(nodes[i],2,b);require(distance(a[1],b[1])<1e-7&&distance(a[2],b[2])<1e-5,"jet sinistro corner implicito conservato");composite->evaluate(nodes[i],2,a);fitted->evaluate(nodes[i],2,b);require(distance(a[1],b[1])<1e-7&&distance(a[2],b[2])<1e-5,"jet destro corner implicito conservato");}
    std::cout<<"CASE composite corner";validate({section(composite),section(std::make_shared<TransformedCurve>(composite,Transform3::translation({0,5,0})))});++cases;
    const auto endAtCorner=std::make_shared<TrimmedCurve<3>>(composite,domain.lo,nodes[1]);
    std::cout<<"CASE trim at corner";validate({section(endAtCorner),section(std::make_shared<TransformedCurve>(endAtCorner,Transform3::translation({0,5,0})))});++cases;
    auto reverse=two;std::reverse(reverse.begin(),reverse.end());std::cout<<"CASE section order";validate(reverse);++cases;
    for(int mode:{1,2}){LoftOptions tangent;tangent.startContinuity=tangent.endContinuity=mode;std::cout<<"CASE G"<<mode;validate(two,tangent);++cases;}
    LoftOptions guides;
    for(double t:{d.lo,d.hi}){const Vec3 p=source->point(t);guides.guides.push_back({{std::make_shared<Line<3>>(p,Vec3(0,1,0)),{0,5}}});}
    std::cout<<"CASE guided";validate(two,guides);++cases;
    const auto prepared=detail::prepareLoftSections(two);
    for(std::size_t i=0;i<two.size();++i){const auto &a=two[i].path[0],&b=prepared[i].path[0];require(a.curve!=b.curve&&isImplicitSketchProjection(*a.curve),"adattamento soltanto sulla copia di costruzione");
        for(int k=0;k<=173;++k){const double t=a.range.lo+a.range.length()*k/173.;require(distance(a.curve->point(t),b.curve->point(t))<=detail::kLoftSectionFitTolerance,"parametro e limite del fit di sezione");}
        for(double t:a.curve->breakpoints(a.range)){require(distance(a.curve->derivative(t),b.curve->derivative(t))<1e-7,"jet destro nei nodi originali");}
    }++cases;
    const auto rational=bezier({{0,0,0},{1,2,0},{4,3,0}});const auto unchanged=detail::prepareLoftSections({section(rational)});
    require(unchanged[0].path[0].curve==rational,"sezioni razionali restano esatte senza fit");++cases;
    bool rejected=false;auto invalid=two;invalid[1].path[0].range.hi=d.hi+1;
    try{detail::prepareLoftSections(invalid);}catch(const std::domain_error &error){rejected=std::string(error.what()).find("[[loft-section=1]]")!=std::string::npos;}
    require(rejected,"intervallo non valido con identita della sezione");++cases;
    for(int i=0;i<=51;++i)require(distance(before[i],source->point(d.lo+d.length()*i/51.))==0,"sorgente implicita immutata");
    std::cout<<"PASS "<<cases<<" gruppi sezioni implicite: proiezione, estrazione, trim/trasformazioni, G1/G2, guide, budget, sorgenti e BodyIO\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
