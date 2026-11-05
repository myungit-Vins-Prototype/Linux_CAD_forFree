#include "fk_surface_mapping.h"
#include "fk_bspline_surface.h"
#include "fk_primitives.h"
#include "fk_sheet.h"
#include "fk_body_check.h"
#include "fk_mass.h"
#include "fk_body_io.h"
#include "fk_step.h"
#include "fk_tessellate.h"
#include "fk_pcurve.h"
#include "fk_curve_algo.h"
#include <iostream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
void require(bool v,const char*m){if(!v)throw std::runtime_error(m);}
int main(){try{
    const auto cylinder=makeCylinder(Frame3(),10,20);FaceId side;for(auto f:cylinder.faces())if(cylinder.face(f).surface->type()==SurfaceType::Cylinder)side=f;
    BodyHistory history;const auto flat=unrollFace(cylinder,side,Frame3(),&history);
    require(checkBody(flat,{true,true}).empty(),"sviluppo cilindro non valido");
    require(std::fabs(faceArea(flat,flat.faces().front())-400*kPi)<1e-8,"area cilindro non conservata");
    double length=0;for(auto e:flat.edges())length+=arcLength(*flat.edge(e).curve,flat.edge(e).range);
    require(std::fabs(length-2*(20+20*kPi))<1e-8,"perimetro sviluppo errato");
    require(history.isDeclared(EntityKind::Face,flat.faces().front().index),"genealogia sviluppo assente");
    const auto plane=makePlaneSheet(Frame3(),3);const auto planar=unrollFace(plane,plane.faces().front(),Frame3(Vec3(2,4,6),Vec3(0,1,0),Vec3(1,0,0)));
    require(std::fabs(faceArea(planar,planar.faces().front())-36)<1e-10,"area sviluppo piano errata");
    Body::BuildFace conical;conical.surface=std::make_shared<ConicalSurface>(Frame3(),.4,10);
    std::vector<Vec3> points;std::vector<Body::BuildEdge> edges;const double angle=1.5,lo=0,hi=5;
    const auto &cone=*conical.surface;
    points={cone.point(0,lo),cone.point(angle,lo),cone.point(angle,hi),cone.point(0,hi)};
    edges={{0,1,cone.vIso(lo),{0,angle},0},{1,2,cone.uIso(angle),{lo,hi},0},{3,2,cone.vIso(hi),{0,angle},0},{0,3,cone.uIso(0),{lo,hi},0}};
    conical.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,false,nullptr,0},{3,false,nullptr,0}}};
    auto coneBody=Body::buildSheet(points,edges,{conical});require(computePCurves(coneBody)==0,"pc cono");
    auto coneFlat=unrollFace(coneBody,coneBody.faces().front(),Frame3());
    require(std::fabs(faceArea(coneFlat,coneFlat.faces().front())-angle*(10*hi+.5*std::sin(.4)*hi*hi))<1e-8,"area cono non conservata");
    bool rejected=false;auto invalid=plane;invalid.face(invalid.faces().front()).surface=std::make_shared<SphericalSurface>(Frame3(),10);
    try{unrollFace(invalid,invalid.faces().front(),Frame3());}catch(const std::domain_error&){rejected=true;}require(rejected,"superficie non sviluppabile accettata");
    auto trimmedCylinder=cylinder;
    for(auto fin:trimmedCylinder.fins())trimmedCylinder.fin(fin).pcurveTolerance=1e-8;
    const auto tolerantFlat=unrollFace(trimmedCylinder,side,Frame3());
    require(checkBody(tolerantFlat,{true,true}).empty(),"sviluppo bordo con precisione sorgente");
    const auto source=makeBox(Frame3(Vec3(-2,-1,0),Vec3(0,0,1),Vec3(1,0,0)),4,2,1);
    const CylindricalSurface target(Frame3(),20);SurfaceWrapOptions options;options.anchor=Vec2(kHalfPi,10);options.tolerance=1e-6;double error;
    auto wrapped=wrapBodyOnSurface(source,target,options,&history,&error);
    require(checkBody(wrapped,{true,true}).empty()&&!wrapped.isSheet(),"corpo deformato non valido");
    require(error<=options.tolerance,"fit deformazione oltre budget");
    require(std::fabs(massProperties(wrapped,1e-8).volume-8.2)<1e-4,"volume deformazione cilindrica errato");
    for(auto face:wrapped.faces())require(history.isDeclared(EntityKind::Face,face.index),"genealogia deformazione assente");
    auto roundtrip=readBodyBinary(writeBodyBinary(wrapped));require(checkBody(roundtrip,{true,true}).empty(),"roundtrip B-rep");
    TessellationOptions mesh;mesh.deflection=.05;require(tessellate(roundtrip,mesh).failedFaces==0,"mesh deformazione incompleta");
    ExchangeBody item;item.name="wrapped";item.body=wrapped;auto step=readStep(writeStep({item}));require(step.bodies.size()==1&&checkBody(step.bodies[0].body).empty(),"scambio STEP deformazione");
    const std::vector<double> knots{0,0,0,1,1,1};std::vector<Vec3> poles;
    const double xy[3]={-5,0,5},square[3]={25,-25,25};for(int i=0;i<3;++i)for(int j=0;j<3;++j)poles.emplace_back(xy[i],xy[j],.02*(square[i]+square[j]));
    const BSplineSurface curved(2,2,knots,knots,3,3,poles);
    const auto small=makeBox(Frame3(Vec3(-.1,-.1,0),Vec3(0,0,1),Vec3(1,0,0)),.2,.2,.02);
    SurfaceWrapOptions onNurbs;onNurbs.anchor=Vec2(.5,.5);auto freeform=wrapBodyOnSurface(small,curved,onNurbs,&history,&error);
    require(checkBody(freeform,{true,true}).empty()&&error<=onNurbs.tolerance,"deformazione su NURBS a doppia curvatura");
    // Ingombro e orientamento di un riempimento su un carrier con asse
    // lungo V e dettagli locali: il fit deve rispettare edge/faccia (1e-7),
    // anche se il budget dell'operazione e' 1e-6.
    const std::vector<double> detailU{0,0,0,0,.1,.2,.3,.4,.5,.6,.7,.8,.9,1,1,1,1};
    const std::vector<double> detailV{0,0,1,1};std::vector<Vec3> detailPoles;
    for(int i=0;i<13;++i)for(double longitudinal:{-50.,50.})
        detailPoles.emplace_back(longitudinal,-15.+2.5*i,2.*std::sin(1.5*i));
    const BSplineSurface detailed(3,1,detailU,detailV,13,2,detailPoles);
    SurfaceWrapOptions repeated;repeated.sourcePlane=Frame3(Vec3(0,0,-18),Vec3(0,0,1),Vec3(1,0,0));repeated.anchor=Vec2(.5,.5);repeated.angle=kHalfPi;
    for(double pos:{-12.,3.,18.}) {
        const auto instance=makeBox(Frame3(Vec3(pos,-2,-18),Vec3(0,0,1),Vec3(1,0,0)),3.6,3.8,.1);
        auto result=wrapBodyOnSurface(instance,detailed,repeated,&history,&error);
        require(result.faces().size()==6&&checkBody(result,{true,true}).empty(),"fit con dettagli NURBS e asse lungo V");
        for(auto e:result.edges())require(result.edge(e).tolerance<=kLinearResolution,"nessuna inflazione delle tolleranze edge");
    }
    auto sheet=makePlaneSheet(Frame3(Vec3(0,0,10),Vec3(0,0,1),Vec3(1,0,0)),1);
    auto collected=Body::collect({&source,&sheet},&history);
    require(checkBody(collected,{true,true}).empty()&&!collected.isSheet(),"collezione mista topologicamente valida");
    require(std::fabs(massProperties(collected).volume-8)<1e-7,"le lamine non alterano il volume della collezione");
    require(history.operandCount()==2&&history.origins(EntityKind::Face,6)[0].operand==1,"genealogia della seconda sorgente nella collezione");
    auto mixedRoundtrip=readBodyBinary(writeBodyBinary(collected));
    require(checkBody(mixedRoundtrip,{true,true}).empty()&&mixedRoundtrip.regions().size()==collected.regions().size(),"regioni miste conservate dal codec");
    ExchangeBody mixedItem;mixedItem.name="mixed";mixedItem.body=collected;const auto mixedStep=readStep(writeStep({mixedItem}));
    int sheets=0,solids=0;for(const auto &part:mixedStep.bodies){require(checkBody(part.body,{true,true}).empty(),"STEP misto valido");if(part.body.isSheet())++sheets;else ++solids;}
    require(sheets==1&&solids==1,"STEP mantiene solido e lamina distinti");
    const auto support=[](double lo,double hi,bool curved) {
        SurfacePtr surface;
        if(curved) {
            const std::vector<double> ku{0,0,0,1,1,1},kv{0,0,1,1};std::vector<Vec3> poles;
            const double x[3]={lo,.5*(lo+hi),hi},z[3]={.025*lo*lo,.025*lo*hi,.025*hi*hi};
            for(int i=0;i<3;++i)for(double y:{-3.,3.})poles.emplace_back(x[i],y,z[i]);
            surface=std::make_shared<BSplineSurface>(2,1,ku,kv,3,2,poles);
        } else surface=std::make_shared<Plane>(Frame3());
        const double a=curved?0:lo,b=curved?1:hi,c=curved?0:-3,d=curved?1:3;
        std::vector<Vec3> vertices{surface->point(a,c),surface->point(b,c),surface->point(b,d),surface->point(a,d)};
        std::vector<Body::BuildEdge> edges{{0,1,surface->vIso(c),{a,b},0},{1,2,surface->uIso(b),{c,d},0},
            {3,2,surface->vIso(d),{a,b},0},{0,3,surface->uIso(a),{c,d},0}};
        Body::BuildFace face;face.surface=surface;face.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,false,nullptr,0},{3,false,nullptr,0}}};
        auto result=Body::buildSheet(vertices,edges,{face});require(computePCurves(result)==0,"SP-curve appoggio");return result;
    };
    const auto spanning=makeBox(Frame3(Vec3(-1,-.5,0),Vec3(0,0,1),Vec3(1,0,0)),2,1,.2);
    auto left=support(-3,0,false),right=support(0,3,false);SurfaceWrapOptions joined;
    auto across=wrapBodyOnFaces(spanning,{{&left,left.faces()[0]},{&right,right.faces()[0]}},joined,&history,&error);
    require(across.faces().size()==6&&checkBody(across,{true,true}).empty(),"un unico solido attraversa due facce destinazione");
    require(std::fabs(massProperties(across).volume-.4)<1e-7,"volume fra due facce coplanari");
    left=support(-3,0,true);right=support(0,3,true);joined.anchor=Vec2(1,.5);
    across=wrapBodyOnFaces(spanning,{{&left,left.faces()[0]},{&right,right.faces()[0]}},joined,&history,&error);
    require(across.faces().size()==6&&checkBody(across,{true,true}).empty()&&error<=joined.tolerance,"attraversamento di due facce NURBS tangenti");
    const auto vertices=spanning.vertices(),mapped=across.vertices();
    for(std::size_t i=0;i<vertices.size();++i) {
        const auto p=spanning.vertex(vertices[i]).point;const auto normal=normalized(Vec3(-.05*p.x(),0,1));
        const auto expected=Vec3(p.x(),p.y(),.025*p.x()*p.x())+p.z()*normal;
        require(distance(across.vertex(mapped[i]).point,expected)<1e-7,"mappa sulle due superfici uguale alla parabola analitica");
    }
    left=support(-3,-.25,false);right=support(.25,3,false);joined.anchor=Vec2();rejected=false;
    try{wrapBodyOnFaces(spanning,{{&left,left.faces()[0]},{&right,right.faces()[0]}},joined);}catch(const std::domain_error&){rejected=true;}
    require(rejected,"vuoto fra superfici destinazione rifiutato");
    options.normalOffset=-21;rejected=false;try{wrapBodyOnSurface(source,target,options);}catch(const std::domain_error&){rejected=true;}require(rejected,"inversione deformazione accettata");
    std::cout<<"PASS sviluppo piano/cilindro/cono, aree/lunghezze, rifiuto doppia curvatura, deformazione cilindrica/volume, genealogia, B-rep, STEP e mesh, appoggio multifaccia continuo/NURBS/vuoti\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
