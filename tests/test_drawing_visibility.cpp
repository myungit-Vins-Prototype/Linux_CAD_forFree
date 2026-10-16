#include "cad_drawing_visibility.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sheet.h"
#include <QCoreApplication>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
DrawingSourceBody source(quint64 id,Body body){auto p=std::make_shared<Body>(std::move(body));return {id,p,std::make_shared<ElementNames>(propagateNames(*p,makeHistory({},*p),{},"visibility"))};}
int starts(const QPainterPath &path){int n=0;for(int i=0;i<path.elementCount();++i)n+=path.elementAt(i).isMoveTo();return n;}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
 DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,makeBox(Frame3(),10,6,1))}};DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;std::vector<DrawingCurve> curves;ok(projectDrawingEdges(model,view,curves));DrawingSilhouetteCache cache;DrawingViewVisibility visible;ok(drawingViewVisibility(model,view,curves,{},cache,visible));require(visible.curves.size()==curves.size(),"un risultato per ogni curva esatta");int back=0,front=0;
 for(std::size_t i=0;i<curves.size();++i){const auto &edge=model.bodies[0].body->edge(curves[i].sourceEdge);const auto p=edge.curve->point((edge.range.lo+edge.range.hi)/2);if(std::abs(p.z())<1e-8){++back;require(visible.curves[i].visible.isEmpty()&&!visible.curves[i].hidden.isEmpty(),"bordi inferiori coperti dalla faccia superiore");}if(std::abs(p.z()-1)<1e-8){++front;require(!visible.curves[i].visible.isEmpty()&&visible.curves[i].hidden.isEmpty(),"bordi anteriori visibili, nessuna auto-occlusione");}}
 require(back==4&&front==4,"otto bordi non collassati del parallelepipedo");
 model.bodies.push_back(source(2,makeBox(Frame3(Vec3(4,-2,3),Vec3(0,0,1),Vec3(1,0,0)),2,10,1)));ok(projectDrawingEdges(model,view,curves));ok(drawingViewVisibility(model,view,curves,{},cache,visible));bool split=false;
 for(std::size_t i=0;i<curves.size();++i)if(curves[i].featureId==1){const auto &edge=model.bodies[0].body->edge(curves[i].sourceEdge);const auto p=edge.curve->point((edge.range.lo+edge.range.hi)/2);if(std::abs(p.z()-1)<1e-8&&std::abs(p.y())<1e-8){const auto &path=visible.curves[i];require(starts(path.visible)==2&&starts(path.hidden)==1,"il bordo viene diviso in visibile-nascosto-visibile");require(std::abs(path.hidden.boundingRect().left()-4)<1e-5&&std::abs(path.hidden.boundingRect().right()-6)<1e-5,"occlusione solo nel tratto coperto dall'altro corpo");split=true;}}
 require(split,"prova del bordo parzialmente nascosto");
 auto second=view;second.plane=Frame3(Vec3(),Vec3(0,0,-1),Vec3(1,0,0));ok(projectDrawingEdges(model,second,curves));ok(drawingViewVisibility(model,second,curves,{},cache,visible));require(std::any_of(visible.curves.begin(),visible.curves.end(),[](const auto &p){return !p.hidden.isEmpty();}),"orientamento opposto ricalcola le occlusioni");
 // I ritagli della faccia non sono riempiti da una sagoma del box.
 std::vector<Vec3> vertices{{-1,-1,3},{11,-1,3},{11,7,3},{-1,7,3},{3,2,3},{3,4,3},{7,4,3},{7,2,3}};std::vector<Body::BuildEdge> edges;Body::BuildFace face;face.surface=std::make_shared<Plane>(Frame3(Vec3(0,0,3),Vec3(0,0,1),Vec3(1,0,0)));
 for(int loop=0;loop<2;++loop){std::vector<Body::BuildFin> fins;for(int i=0;i<4;++i){int a=4*loop+i,b=4*loop+(i+1)%4;const auto delta=vertices[b]-vertices[a];int edge=int(edges.size());edges.push_back({a,b,std::make_shared<Line<3>>(vertices[a],delta),{0,1},0});fins.push_back({edge,true,std::make_shared<Line<2>>(Vec2(vertices[a].x(),vertices[a].y()),Vec2(delta.x(),delta.y())),0});}face.loops.push_back(fins);}Body ring=Body::buildSheet(vertices,edges,{face});
 model.bodies={source(1,makeBox(Frame3(),10,6,1)),source(2,std::move(ring))};model.bodies.push_back(source(3,makeBox(Frame3(Vec3(0,3,0),Vec3(0,0,1),Vec3(1,0,0)),10,.2,1)));
 ok(projectDrawingEdges(model,view,curves));ok(drawingViewVisibility(model,view,curves,{},cache,visible));bool hole=false;
 for(std::size_t i=0;i<curves.size();++i)if(curves[i].featureId==3){const auto &edge=model.bodies[2].body->edge(curves[i].sourceEdge);const auto p=edge.curve->point((edge.range.lo+edge.range.hi)/2);if(std::abs(p.z()-1)<1e-8&&std::abs(p.y()-3)<1e-8){const auto &path=visible.curves[i];require(starts(path.visible)==1&&starts(path.hidden)==2&&std::abs(path.visible.boundingRect().left()-3)<1e-5&&std::abs(path.visible.boundingRect().right()-7)<1e-5,"la geometria dietro il foro resta visibile solo nel ritaglio");hole=true;}}
 require(hole,"superficie aperta con foro verificata");
 // Le sezioni del piano restano riferimenti visibili anche all'interno del volume.
 DrawingView cut;cut.plane=Frame3(Vec3(0,0,.5),Vec3(0,0,1),Vec3(1,0,0));ok(sectionDrawingView(model,cut,curves));ok(drawingViewVisibility(model,cut,curves,{},cache,visible));for(const auto &path:visible.curves)require(path.hidden.isEmpty()&&!path.visible.isEmpty(),"nessuna occlusione dei riferimenti di sezione");
 DrawingModelSnapshot round{{QUuid::createUuid(),{}},{source(1,makeSphere(Frame3(),5))}};DrawingSilhouetteResult silhouettes;ok(drawingViewSilhouettes(round,view,silhouettes,cache));ok(projectDrawingEdges(round,view,curves));ok(drawingViewVisibility(round,view,curves,silhouettes,cache,visible));require(!visible.silhouettes.empty()&&std::any_of(visible.silhouettes.begin(),visible.silhouettes.end(),[](const auto &p){return !p.visible.isEmpty();}),"profilo della sfera conservato");
 auto invalid=round;invalid.bodies.push_back(invalid.bodies[0]);const auto before=visible.silhouettes.size();require(!drawingViewVisibility(invalid,view,curves,silhouettes,cache,visible).isEmpty()&&visible.silhouettes.size()==before,"sorgente incoerente rifiutata atomicamente");
 std::cout<<"PASS visible/hidden spans, partial and inter-body occlusion, view direction, sections, surfaces and silhouettes\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
