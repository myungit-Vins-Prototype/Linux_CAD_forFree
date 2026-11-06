#include "fk_surface_mapping.h"
#include "fk_quilt_mapping.h"
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
#include "fk_classify_uv.h"
#include "fk_curve_surface.h"
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
    SurfaceQuiltMap quilt({{&cylinder,side}},Vec2(kHalfPi,10));
    for(double x:{-2.,0.,2.})for(double y:{-1.,0.,1.}) {
        const Vec2 uv(kHalfPi+x/10.,10+y);const auto flat=quilt.flatten(0,uv);
        require(distance(flat,Vec2(x,y))<1e-9,"sviluppo metrico cilindrico");
        require(distance(quilt.fold(Vec3(flat.x(),flat.y(),0)),cylinder.face(side).surface->point(uv.x(),uv.y()))<1e-9,"mappa quilt andata/ritorno");
    }
    auto quiltBody=quilt.bend(source,options,&history,&error);
    require(checkBody(quiltBody,{true,true}).empty()&&error<=options.tolerance,"piegatura da quilt appiattito B-rep");
    bool exactContact=false;
    for(auto f:quiltBody.faces())exactContact=exactContact||quiltBody.face(f).surface.get()==cylinder.face(side).surface.get();
    require(exactContact,"faccia a quota zero conserva il carrier esatto e la parametrizzazione");
    for(auto e:quiltBody.edges())for(int k=0;k<=12;++k) {
        const auto &edge=quiltBody.edge(e);const auto p=edge.curve->point(edge.range.lo+(edge.range.hi-edge.range.lo)*k/12.);
        require(std::fabs(std::hypot(p.x(),p.y())-10)<1e-7||std::fabs(std::hypot(p.x(),p.y())-11)<1e-7||k!=0,"appoggio e quota della cella cilindrica");
    }
    ExchangeBody quiltExchange;quiltExchange.name="quilt";quiltExchange.body=quiltBody;
    const auto quiltStep=readStep(writeStep({quiltExchange}));
    require(quiltStep.bodies.size()==1&&checkBody(quiltStep.bodies[0].body,{true,true}).empty(),"STEP del corpo piegato con superfici quintiche");
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
    // Due facce del medesimo carrier, una sola giunzione topologica.
    const std::vector<Vec3> joinedPoints{curved.point(0,0),curved.point(.5,0),curved.point(1,0),curved.point(0,1),curved.point(.5,1),curved.point(1,1)};
    std::vector<Body::BuildEdge> joinedEdges{{0,1,curved.vIso(0),{0,.5},0},{1,2,curved.vIso(0),{.5,1},0},
        {2,5,curved.uIso(1),{0,1},0},{4,5,curved.vIso(1),{.5,1},0},{3,4,curved.vIso(1),{0,.5},0},
        {0,3,curved.uIso(0),{0,1},0},{1,4,curved.uIso(.5),{0,1},0}};
    Body::BuildFace joinedLeft,joinedRight;joinedLeft.surface=joinedRight.surface=std::make_shared<BSplineSurface>(curved);
    joinedLeft.loops={{{0,true,nullptr,0},{6,true,nullptr,0},{4,false,nullptr,0},{5,false,nullptr,0}}};
    joinedRight.loops={{{1,true,nullptr,0},{2,true,nullptr,0},{3,false,nullptr,0},{6,false,nullptr,0}}};
    auto quiltSupport=Body::buildSheet(joinedPoints,joinedEdges,{joinedLeft,joinedRight});
    require(computePCurves(quiltSupport)==0&&checkBody(quiltSupport,{true,true}).empty(),"quilt topologico di due NURBS");
    const std::vector<SurfaceTargetFace> flattenTargets{{&quiltSupport,quiltSupport.faces()[0]},{&quiltSupport,quiltSupport.faces()[1]}};
    const auto autoAnchor=surfaceQuiltAnchor(flattenTargets);
    require(distance(autoAnchor,Vec2(.5,.5))<1e-12,"origine sul bordo topologico comune");
    SurfaceQuiltMap flattenMap(flattenTargets,autoAnchor);
    SurfaceFlattenStats flattenStats;BodyHistory flattenHistory;
    const Frame3 flattenPlane(Vec3(2,4,6),normalized(Vec3(1,2,3)),Vec3(1,0,0));
    const auto flatJoined=flattenMap.flattenSheet(flattenPlane,1e-6,&flattenStats,&flattenHistory);
    require(flatJoined.isSheet()&&flatJoined.faces().size()==2&&flatJoined.edges().size()==7&&flatJoined.vertices().size()==6&&
        checkBody(flatJoined,{true,true}).empty(),"appiattimento conserva il bordo comune e la lamina");
    for(auto v:flatJoined.vertices())require(std::fabs(flattenPlane.toLocal(flatJoined.vertex(v).point).z())<1e-12,"tutti i vertici sul piano destinazione");
    require(flattenStats.samples>400&&flattenStats.minimumStretch<1&&flattenStats.maximumStretch>1&&
        flattenStats.boundaryDeviation<=2e-8,"deformazione della doppia curvatura misurata, contorni nel budget");
    for(auto f:flatJoined.faces())require(flattenHistory.isDeclared(EntityKind::Face,f.index),"genealogia delle facce appiattite");
    require(flattenHistory.descendants(EntityKind::Edge,0,6).size()==1,"un solo discendente del bordo comune");
    SurfaceQuiltMap reverseMap({flattenTargets[1],flattenTargets[0]},autoAnchor);
    require(checkBody(reverseMap.flattenSheet(flattenPlane,1e-6),{true,true}).empty(),"ordine inverso delle facce appiattite valido");
    const std::vector<SurfaceTargetFace> planeTarget{{&plane,plane.faces()[0]}};
    SurfaceQuiltMap planeMap(planeTarget,surfaceQuiltAnchor(planeTarget));
    const auto flatPlane=planeMap.flattenSheet(flattenPlane,1e-6,&flattenStats);
    require(std::fabs(faceArea(flatPlane,flatPlane.faces()[0])-36)<1e-9&&
        std::fabs(flattenStats.minimumStretch-1)<1e-10&&std::fabs(flattenStats.maximumStretch-1)<1e-10,
        "appiattimento di un piano senza deformazione artificiale");
    rejected=false;try{quilt.flattenSheet(flattenPlane,1e-6);}catch(const std::domain_error&){rejected=true;}
    require(rejected,"superficie periodica non tagliata rifiutata esplicitamente");
    ExchangeBody flattenExchange;flattenExchange.name="superfici appiattite";flattenExchange.body=flatJoined;
    const auto flattenStep=readStep(writeStep({flattenExchange}));
    require(flattenStep.bodies.size()==1&&flattenStep.bodies[0].body.faces().size()==2&&
        checkBody(flattenStep.bodies[0].body,{true,true}).empty(),"STEP dell'insieme appiattito");
    SurfaceQuiltMap multiQuilt({{&quiltSupport,quiltSupport.faces()[0]},{&quiltSupport,quiltSupport.faces()[1]}},Vec2(.5,.5));
    for(int side=0;side<2;++side)for(double u:{.1,.4})for(double v:{.2,.7}) {
        const Vec2 uv(side?.5+u:u,v);const auto xy=multiQuilt.flatten(side,uv);
        require(distance(multiQuilt.fold(Vec3(xy.x(),xy.y(),0)),curved.point(uv.x(),uv.y()))<1e-9,"corrispondenza multifaccia reversibile");
    }
    SurfaceQuiltMap offGridQuilt({{&quiltSupport,quiltSupport.faces()[0]}},Vec2(.37,.43));
    for(double v:{.13,.41,.77}) {
        const auto xy=offGridQuilt.flatten(0,Vec2(.37,v));
        require(std::fabs(xy.x())<1e-12,"origine della mappa esatta anche fuori dai nodi del fit");
        require(distance(offGridQuilt.fold(Vec3(0,xy.y(),.1)),curved.point(.37,v)+.1*curved.normal(.37,v))<1e-9,"ancoraggio e normale coerenti lungo la meridiana");
    }
    const auto seamLeft=multiQuilt.flatten(0,Vec2(.5,.5)),seamRight=multiQuilt.flatten(1,Vec2(.5,.5));
    require(distance(seamLeft,seamRight)<1e-10,"nessuna cucitura artificiale nella mappa piana");
    const auto bentAcross=multiQuilt.bend(small,onNurbs,&history,&error);
    require(checkBody(bentAcross,{true,true}).empty()&&error<=onNurbs.tolerance,"cella solida piegata attraverso la giunzione");
    rejected=false;try{multiQuilt.fold(Vec3(100,0,0));}catch(const std::domain_error&){rejected=true;}
    require(rejected,"materiale fuori dal quilt non viene esteso silenziosamente");
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
    left=support(-3,0,false);right=support(0,3,false);SurfaceWrapOptions centered;
    require(fitSurfaceWrapPlacement({Vec2(3.1,0),Vec2(-2,0)},{{&left,left.faces()[0]},{&right,right.faces()[0]}},centered,.5),"centraggio trova piccola traslazione con tutti i centri nelle facce");
    require(centered.shift.x()<0&&norm(centered.shift)<=.5,"traslazione limitata senza scala");
    const auto previous=centered.shift;
    require(!fitSurfaceWrapPlacement({Vec2(10,0)},{{&left,left.faces()[0]},{&right,right.faces()[0]}},centered,.1)&&distance(centered.shift,previous)==0,"ricerca fallita conserva la posizione");
    Body::BuildFace triangleFace;triangleFace.surface=std::make_shared<Plane>(Frame3());
    const std::vector<Vec3> trianglePoints{{0,0,0},{1,0,0},{0,1,0}};
    std::vector<Body::BuildEdge> triangleEdges;
    for(int i=0;i<3;++i){const auto d=trianglePoints[(i+1)%3]-trianglePoints[i];triangleEdges.push_back({i,(i+1)%3,std::make_shared<Line<3>>(trianglePoints[i],normalized(d)),{0,norm(d)},0});}
    triangleFace.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,true,nullptr,0}}};
    auto triangle=Body::buildSheet(trianglePoints,triangleEdges,{triangleFace});require(computePCurves(triangle)==0,"SP-curve triangolo sorgente");
    const auto distant=makePlaneSheet(Frame3(Vec3(10,0,0),Vec3(0,0,1),Vec3(1,0,0)),1);
    SurfaceWrapOptions polygonMapping;
    const auto mappedTriangle=wrapBodyOnFaces(triangle,{{&triangle,triangle.faces()[0]},{&distant,distant.faces()[0]}},polygonMapping,&history,&error);
    require(checkBody(mappedTriangle,{true,true}).empty()&&std::fabs(faceArea(mappedTriangle,mappedTriangle.faces()[0])-.5)<1e-9,"fit rettangolare esteso soltanto fuori dal triangolo materiale");
    // Ogni sorgente e' una rototraslazione: distanze, volume e carrier
    // analitici restano invariati, Z segue la normale al centro del profilo.
    options.rigid=true;options.normalOffset=.3;
    auto rigid=wrapBodyOnSurface(source,target,options,&history,&error);
    require(error==0&&checkBody(rigid,{true,true}).empty(),"posizionamento rigido valido senza fit");
    require(std::fabs(massProperties(rigid).volume-8)<1e-8,"volume rigido invariato");
    const auto originalVertices=source.vertices(),rigidVertices=rigid.vertices();
    for(std::size_t i=0;i<originalVertices.size();++i)for(std::size_t j=0;j<i;++j)
        require(std::fabs(distance(source.vertex(originalVertices[i]).point,source.vertex(originalVertices[j]).point)-
                          distance(rigid.vertex(rigidVertices[i]).point,rigid.vertex(rigidVertices[j]).point))<1e-10,"distanze rigide conservate");
    for(auto f:rigid.faces())require(rigid.face(f).surface->type()==SurfaceType::Plane,"piani sorgenti conservati");
    const auto near=makeBox(Frame3(Vec3(1,-.5,0),Vec3(0,0,1),Vec3(1,0,0)),1,1,.2);
    left=support(-3,0,true);right=support(0,3,true);joined.anchor=Vec2(1,.5);joined.rigid=true;
    rigid=wrapBodyOnFaces(near,{{&left,left.faces()[0]},{&right,right.faces()[0]}},joined,&history,&error);
    require(std::fabs(massProperties(rigid).volume-.2)<1e-9,"volume rigido su due facce");
    const auto ids=rigid.vertices();Vec3 baseCenter,topCenter;int countBase=0,countTop=0;
    const auto from=near.vertices();for(std::size_t i=0;i<from.size();++i) {
        if(near.vertex(from[i]).point.z()<.1){baseCenter+=rigid.vertex(ids[i]).point;++countBase;}
        else {topCenter+=rigid.vertex(ids[i]).point;++countTop;}
    }
    baseCenter=baseCenter/double(countBase);topCenter=topCenter/double(countTop);
    require(distance(baseCenter,Vec3(1.5,0,.025*1.5*1.5))<1e-9,"centro sorgente sul supporto corretto");
    require(distance(normalized(topCenter-baseCenter),normalized(Vec3(-.075,0,1)))<1e-10,"ogni copia segue la normale locale");
    FaceParameterClassifier classifier(left,left.faces()[0],1e-7);
    require(classifier.classify(Vec2(.5,.5))==PointLocation::Inside&&classifier.classify(Vec2(-.1,.5))==PointLocation::Outside&&
            classifier.classify(Vec2(1,.5))==PointLocation::Boundary,"classificazione UV interno/esterno/bordo");
    // La scorciatoia retta/Bezier non deve perdere radici su una falda
    // ripiegata, ne' nascondere due destinazioni sovrapposte.
    std::vector<Vec3> foldPoles;
    for(int i=0;i<3;++i)for(double y:{-1.,1.})foldPoles.emplace_back(i==1?-1.:1.,y,i-1.);
    const BSplineSurface fold(2,1,{0,0,0,1,1,1},{0,0,1,1},3,2,foldPoles);
    auto hits=numericCurveSurface(Line<3>(Vec3(.25,0,-2),Vec3(0,0,1)),{0,4},fold,1e-8);
    require(hits.parameters.size()==2&&std::fabs(hits.parameters[0]-1.5)<1e-7&&std::fabs(hits.parameters[1]-2.5)<1e-7,"ricerca completa delle due radici su superficie ripiegata");
    // Un secondo carrier regolare alla stessa impronta resta ambiguo.
    const auto over=makePlaneSheet(Frame3(Vec3(0,0,1),Vec3(0,0,1),Vec3(1,0,0)),3);
    left=support(-3,0,false);right=support(0,3,false);joined.anchor=Vec2();rejected=false;
    try{wrapBodyOnFaces(spanning,{{&left,left.faces()[0]},{&right,right.faces()[0]},{&over,over.faces()[0]}},joined);}catch(const std::domain_error&){rejected=true;}
    require(rejected,"sovrapposizioni rigide rifiutate senza scelta arbitraria");
    options.rigid=false;
    options.normalOffset=-21;rejected=false;try{wrapBodyOnSurface(source,target,options);}catch(const std::domain_error&){rejected=true;}require(rejected,"inversione deformazione accettata");
    std::cout<<"PASS sviluppo piano/cilindro/cono, aree/lunghezze, rifiuto doppia curvatura, deformazione cilindrica/volume, genealogia, B-rep, STEP e mesh, appoggio multifaccia continuo/NURBS/vuoti\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
