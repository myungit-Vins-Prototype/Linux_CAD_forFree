#include "fk_loft.h"
#include "fk_body_check.h"
#include "fk_tessellate.h"
#include "fk_mass.h"
#include "fk_step.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace ForgeCad::Kernel;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
LoftSection circle(double z,double radius=5){
    LoftSection s;s.frame=Frame3(Vec3(0,0,z),Vec3(0,0,1),Vec3(1,0,0));
    s.loop.segments={{std::make_shared<Circle<2>>(Vec2(),Vec2(1,0),Vec2(0,1),radius),{0,kTwoPi}}};return s;
}
LoftSection point(Vec3 p){LoftSection s;s.point=p;return s;}
PathSegment line(Vec3 a,Vec3 b){return {std::make_shared<Line<3>>(a,b-a),{0,distance(a,b)}};}
void checked(const Body &body){
    require(checkBody(body).empty(),"B-rep invalido");
    TessellationOptions options;options.deflection=0.1;
    require(tessellate(body,options).failedFaces==0,"tassellazione incompleta");
    for(auto id:body.edges()) {
        const auto &e=body.edge(id);
        require(distance(e.curve->point(e.range.lo),e.curve->point((e.range.lo+e.range.hi)*0.5))>1e-9,"bordo collassato presente");
    }
}
template<class F>void rejected(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,"caso invalido accettato");}
}
int main(){try{
    const auto c=circle(0),p=point(Vec3(0,0,10));
    for(bool ruled:{false,true})for(bool reversed:{false,true}) {
        LoftOptions o;o.ruled=ruled;
        const auto sections=reversed?std::vector<LoftSection>{p,c}:std::vector<LoftSection>{c,p};
        const auto b=loftSolid(sections,o);checked(b);
        require(std::fabs(std::fabs(massProperties(b,1e-10).volume)-kPi*250/3)<1e-6,"volume cono");
        checked(loftSheet(sections,o));
    }
    checked(loftSolid({point(Vec3(0,0,-10)),c,p}));
    LoftSection chain;chain.path={line(Vec3(-5,0,0),Vec3(5,0,0))};checked(loftSheet({chain,p}));
    LoftOptions manual;manual.correspondencePoints={{Vec3(5,0,0),Vec3(-5,0,0)},{*p.point,*p.point}};
    checked(loftSolid({c,p},manual));
    auto defaults=loftCorrespondenceDefaults({p,c});require(distance(defaults[0][0],*p.point)==0,"corrispondenza sul punto");
    LoftOptions guides;guides.guides={{line(Vec3(5,0,0),*p.point)},{line(Vec3(-5,0,0),*p.point)}};
    checked(loftSolid({c,p},guides));
    LoftContact plane;plane.plane=Frame3(*p.point,Vec3(0,0,1),Vec3(1,0,0));
    LoftOptions tangent;tangent.endContinuity=1;tangent.endFaces={plane};
    auto cap=loftSolid({c,p},tangent);checked(cap);
    ExchangeBody exported;exported.name="punta";exported.body=cap;
    const auto imported=readStep(writeStep({exported}));
    require(imported.bodies.size()==1,"STEP punta riletto");checked(imported.bodies[0].body);
    for(auto id:cap.faces()) {
        const auto &s=*cap.face(id).surface;
        if(s.type()!=SurfaceType::BSpline)continue;
        for(double u:{0.12,0.37,0.63,0.88}) {
            Vec3 d[9];s.evaluate(u,1,2,d);
            const auto normal=cross(d[Surface::derivativeIndex(1,1,2)],d[Surface::derivativeIndex(0,1,2)]);
            require(norm(normal)>1e-9 && norm(cross(normalized(normal),Vec3(0,0,1)))<1e-8,"limite G1 al piano sul punto");
        }
    }
    LoftOptions planar;planar.startContinuity=1;plane.plane=Frame3();planar.startFaces={plane};
    checked(loftSheet({c,circle(10,3)},planar));
    rejected([&]{loftSolid({p,p});});
    rejected([&]{loftSolid({c,p,circle(20)});});
    plane.plane=Frame3(Vec3(0,0,11),Vec3(0,0,1),Vec3(1,0,0));tangent.endFaces={plane};
    rejected([&]{loftSolid({c,p},tangent);});
    guides.endContinuity=1;plane.plane=Frame3(*p.point,Vec3(0,0,1),Vec3(1,0,0));guides.endFaces={plane};
    rejected([&]{loftSolid({c,p},guides);});
    std::cout<<"PASS loft puntuali: solidi/superfici, volume, topologia, mesh, guide, corrispondenze, G1 piano e negativi\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
