#include "cad_drawing_visibility.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sheet.h"
#include "fk_extrude.h"
#include <QCoreApplication>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
DrawingSourceBody source(quint64 id,Body body){auto p=std::make_shared<Body>(std::move(body));return {id,p,std::make_shared<ElementNames>(propagateNames(*p,makeHistory({},*p),{},"visibility"))};}
double pathLength(const QPainterPath &path){double length=0;for(int i=1;i<path.elementCount();++i){const auto a=path.elementAt(i-1),b=path.elementAt(i);if(!b.isMoveTo())length+=std::hypot(b.x-a.x,b.y-a.y);}return length;}
int starts(const QPainterPath &path){int n=0;for(int i=0;i<path.elementCount();++i)n+=path.elementAt(i).isMoveTo();return n;}
void conicTransitions(){
 // Una lamina copre soltanto 0.001mm vicino alla tangenza della conica.
 // Gli estremi devono coincidere con l'intersezione analitica, non con la corda.
 for(bool elliptical:{false,true})for(bool tilted:{false,true})for(bool section:{false,true}){
  const Frame3 frame(Vec3(),tilted?Vec3(0,.6,.8):Vec3(0,0,1),Vec3(1,0,0));
  const double minor=elliptical?6.:10.;ProfileRegion region;region.outer.segments={{std::make_shared<Ellipse<2>>(Vec2(),Vec2(1,0),Vec2(0,1),10,minor),{0,kTwoPi},0}};
  DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,elliptical?makeExtrusion(frame,region,1):makeCylinder(frame,10,1)),source(2,makeBox(Frame3(Vec3(9.999,-20,section?.5:50),Vec3(0,0,1),Vec3(1,0,0)),1,40,section?3:1))}};
  DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;DrawingSilhouetteCache cache;
  if(section){view.kind=DrawingViewKind::Section;view.sectionStyle=DrawingSectionStyle::Full;view.plane=Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));}
  for(double scale:{.1,1.,10.}){view.scale=scale;std::vector<DrawingCurve> curves;DrawingViewVisibility result;ok(projectDrawingEdges(model,view,curves));
   // Isola l'anello anteriore: la deduplicazione puo' assegnare il contorno
   // coincidente a quello inferiore, senza cambiare la geometria disegnata.
   curves.erase(std::remove_if(curves.begin(),curves.end(),[&](const auto &c){if(c.featureId!=1)return true;const auto &edge=model.bodies[0].body->edge(c.sourceEdge);return distance(.5*(edge.curve->point(0)+edge.curve->point(kPi)),frame.toGlobal(Vec3(0,0,1)))>1e-8;}),curves.end());
   QPainterPath material;if(section)material.addRect(QRectF(9.999*scale,-20*scale,scale,40*scale));
   ok(drawingViewVisibility(model,view,curves,{},cache,result,section?&material:nullptr));bool checked=false;
   for(std::size_t i=0;i<curves.size();++i)if(curves[i].featureId==1){const auto &edge=model.bodies[0].body->edge(curves[i].sourceEdge);
    const auto center=.5*(edge.curve->point(0)+edge.curve->point(kPi));if(distance(center,frame.toGlobal(Vec3(0,0,1)))>1e-8)continue;
    const auto &path=result.curves[i];require(!path.hidden.isEmpty()&&!path.visible.isEmpty(),"copertura tangente sottile conservata");
    const double dy=minor*(tilted?.8:1.)*std::sqrt(1-9.999*9.999/100.);const auto box=path.hidden.boundingRect();
    require(std::abs(box.left()/scale-9.999)<1e-8&&std::abs(box.top()/scale+center.y()+dy)<1e-8&&std::abs(box.bottom()/scale+center.y()-dy)<1e-8,"transizioni cerchio/ellisse coincidenti con la soluzione analitica a ogni scala");
    for(const auto *part:{&path.visible,&path.hidden})for(int k=1;k<part->elementCount();++k){const auto a=part->elementAt(k-1),b=part->elementAt(k);if(b.isMoveTo())continue;
     const double x=(a.x+b.x)/(2*scale),y=(-(a.y+b.y)/(2*scale)-center.y())/((tilted?.8:1.)*minor/10.);
     require(std::abs(10-std::hypot(x,y))*scale<.0013,"scarto della corda grafica contenuto sul foglio");}
    checked=true;
   }require(checked,"verificata la conica anteriore");
   // Arco rifilato che attraversa la cucitura, anche traslato di un periodo.
   require(curves.size()==1,"anello anteriore isolato");const auto full=result.curves.front().hidden.boundingRect();
   for(double shift:{-kTwoPi,0.,kTwoPi}){auto trimmed=curves;trimmed.front().hasSourceRange=true;trimmed.front().sourceRange={shift-.1,shift+.1};trimmed.front().range=trimmed.front().sourceRange;
    DrawingViewVisibility partial;ok(drawingViewVisibility(model,view,trimmed,{},cache,partial,section?&material:nullptr));
    require(QLineF(partial.curves[0].hidden.boundingRect().topLeft(),full.topLeft()).length()<1e-7*scale&&QLineF(partial.curves[0].hidden.boundingRect().bottomRight(),full.bottomRight()).length()<1e-7*scale,"occlusione invariata su archi rifilati oltre la cucitura periodica");}
  }
 }
}
void conicDepth(){
 const Frame3 frame(Vec3(),Vec3(0,.6,.8),Vec3(1,0,0));
 DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,makeCylinder(frame,10,1)),source(2,makeBox(Frame3(Vec3(-20,-20,-.2),Vec3(0,0,1),Vec3(1,0,0)),40,40,1))}};
 DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;DrawingSilhouetteCache cache;
 for(double scale:{.1,1.,10.}){view.scale=scale;std::vector<DrawingCurve> curves;ok(projectDrawingEdges(model,view,curves));
  curves.erase(std::remove_if(curves.begin(),curves.end(),[&](const auto &c){if(c.featureId!=1)return true;const auto &edge=model.bodies[0].body->edge(c.sourceEdge);return distance(.5*(edge.curve->point(0)+edge.curve->point(kPi)),frame.origin()+frame.zDir())>1e-8;}),curves.end());
  require(curves.size()==1,"conica obliqua per il taglio in profondita'");DrawingViewVisibility result;ok(drawingViewVisibility(model,view,curves,{},cache,result));
  const auto &path=result.curves[0];require(!path.visible.isEmpty()&&!path.hidden.isEmpty(),"transizione dovuta alla profondita', senza attraversare il bordo del coprente");
  const double expected=-scale*(.6+8*(4*view.tolerance)/6);
  require(std::abs(path.hidden.boundingRect().bottom()-expected)<1e-7*scale&&std::abs(path.visible.boundingRect().top()-expected)<1e-7*scale,"taglio esatto del piano di profondita' con il margine geometrico esistente");
 }
}
void conicLimits(){
 DrawingModelSnapshot model{{QUuid::createUuid(),{}},{}};DrawingView view;DrawingCurve curve;curve.viewId=view.id;curve.curve=std::make_shared<Ellipse<3>>(Vec3(4,3,0),Vec3(1,0,0),Vec3(0,1,0),10,6);curve.range={-.1,.1};DrawingSilhouetteCache cache;DrawingViewVisibility result;
 ok(drawingViewVisibility(model,view,{curve},{},cache,result));const auto baseline=result.curves[0].visible.boundingRect();
 curve.curve=std::make_shared<TrimmedCurve<3>>(std::make_shared<TransformedCurve>(std::make_shared<Ellipse<3>>(Vec3(),Vec3(1,0,0),Vec3(0,1,0),10,6),Transform3::translation(Vec3(4,3,0))),curve.range.lo,curve.range.hi);
 ok(drawingViewVisibility(model,view,{curve},{},cache,result));require(result.curves[0].visible.boundingRect()==baseline,"coniche trasformate e rifilate conservano il parametro");
 view.scale=1e14;const auto before=result.curves[0].visible;require(!drawingViewVisibility(model,view,{curve},{},cache,result).isEmpty()&&result.curves[0].visible==before,"limite di campionamento rifiutato senza pubblicare risultati parziali");
}
void sphereOcclusion(){
 for(bool elliptical:{false,true})for(bool rotated:{false,true})for(double separation:{4.,7.9999})for(double scale:{.1,1.,10.}){
  const Frame3 frame=rotated?Frame3(Vec3(13,-7,100),Vec3(.3,-.8,1),Vec3(1,0,0)):Frame3();const double minor=elliptical?3.:5.;ProfileRegion region;region.outer.segments={{std::make_shared<Ellipse<2>>(Vec2(),Vec2(1,0),Vec2(0,1),5,minor),{0,kTwoPi},0}};
  DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,elliptical?makeExtrusion(frame,region,1):makeCylinder(frame,5,1)),source(2,makeSphere(Frame3(frame.toGlobal(Vec3(separation,0,10)),frame.zDir(),frame.xDir()),3))}};
  DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.plane=frame;view.scale=scale;std::vector<DrawingCurve> curves;ok(projectDrawingEdges(model,view,curves));
  curves.erase(std::remove_if(curves.begin(),curves.end(),[&](const auto &c){if(c.featureId!=1)return true;const auto &edge=model.bodies[0].body->edge(c.sourceEdge);return std::abs(frame.toLocal(edge.curve->point(kPi)).z()-1)>1e-8;}),curves.end());require(curves.size()==1,"conica posteriore isolata");
  DrawingSilhouetteCache cache;DrawingViewVisibility paths;ok(drawingViewVisibility(model,view,curves,{},cache,paths));const auto &path=paths.curves[0];require(!path.hidden.isEmpty()&&!path.visible.isEmpty(),"sfera nasconde anche una calotta sottile del cerchio");
  const double x=elliptical?.625*separation:(25+separation*separation-9)/(2*separation),y=minor*std::sqrt(1-x*x/25);const auto box=path.hidden.boundingRect();
  if(std::abs(box.left()/scale-x)>=2e-6||std::abs(box.top()/scale+y)>=2e-6||std::abs(box.bottom()/scale-y)>=2e-6){const auto &body=*model.bodies[1].body;const auto &face=body.face(body.faces()[0]);std::cerr<<"sphere d="<<separation<<" scale="<<scale<<" bounds="<<box.left()/scale<<","<<box.top()/scale<<","<<box.bottom()/scale<<" expected="<<x<<","<<y<<" topology="<<body.faces().size()<<","<<body.edges().size()<<","<<face.loops.size()<<","<<face.sense<<","<<body.region(body.shell(face.shell).region).solid<<'\n';}
  require(std::abs(box.left()/scale-x)<2e-6&&std::abs(box.top()/scale+y)<2e-6&&std::abs(box.bottom()/scale-y)<2e-6,"transizioni sulla sfera esatta indipendenti dalla triangolazione e dalla scala");
  if(separation>7)for(double shift:{-kTwoPi,kTwoPi}){auto trimmed=curves;trimmed[0].hasSourceRange=true;trimmed[0].sourceRange={shift-.2,shift+.2};trimmed[0].range=trimmed[0].sourceRange;DrawingViewVisibility part;
   ok(drawingViewVisibility(model,view,trimmed,{},cache,part));const auto bounds=part.curves[0].hidden.boundingRect();require(std::abs(bounds.top()/scale+y)<2e-6&&std::abs(bounds.bottom()/scale-y)<2e-6,"ombra sferica su archi oltre la cucitura periodica");}
 }
 // Segmento dietro la sfera: intersezione esatta col cerchio dell'ombra.
 for(bool rotated:{false,true})for(double scale:{.1,1.,10.})for(double z:{-100.,10.,14.}){
  const Frame3 frame=rotated?Frame3(Vec3(13,-7,100),Vec3(.3,-.8,1),Vec3(1,0,0)):Frame3();
  DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,makeBox(Frame3(frame.toGlobal(Vec3(-5,.7,z)),frame.zDir(),frame.xDir()),10,.2,.1)),source(2,makeSphere(Frame3(frame.toGlobal(Vec3(0,0,10)),frame.zDir(),frame.xDir()),3))}};
  DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.plane=frame;view.scale=scale;std::vector<DrawingCurve> curves;ok(projectDrawingEdges(model,view,curves));
  curves.erase(std::remove_if(curves.begin(),curves.end(),[&](const auto &c){if(c.featureId!=1)return true;const auto &edge=model.bodies[0].body->edge(c.sourceEdge);const auto p=frame.toLocal(edge.curve->point(.5*(edge.range.lo+edge.range.hi)));return std::abs(p.y()-.7)>1e-8||std::abs(p.z()-z-.1)>1e-8;}),curves.end());require(curves.size()==1,"segmento di controllo isolato");
  DrawingSilhouetteCache cache;DrawingViewVisibility paths;ok(drawingViewVisibility(model,view,curves,{},cache,paths));
  if(z>13){require(paths.curves[0].hidden.isEmpty(),"un bordo davanti alla sfera resta visibile");continue;}
  const double depth=std::max(0.,z+.1-10+4*view.tolerance),x=std::sqrt(9-.7*.7-depth*depth);const auto box=paths.curves[0].hidden.boundingRect();
  if(std::abs(box.left()/scale+x)>=2e-6||std::abs(box.right()/scale-x)>=2e-6)std::cerr<<"sphere line scale="<<scale<<" z="<<z<<" bounds="<<box.left()/scale<<","<<box.right()/scale<<" expected="<<x<<'\n';
  require(std::abs(box.left()/scale+x)<2e-6&&std::abs(box.right()/scale-x)<2e-6,"taglio esatto di impronta e profondita' sul segmento");
  if(!rotated&&scale==1&&z<0){auto overlap=model;overlap.bodies.push_back(source(3,makeSphere(Frame3(Vec3(1,0,10),Vec3(0,0,1),Vec3(1,0,0)),3)));DrawingViewVisibility merged;
   ok(drawingViewVisibility(overlap,view,curves,{},cache,merged));const auto bounds=merged.curves[0].hidden.boundingRect();require(starts(merged.curves[0].hidden)==1&&std::abs(bounds.left()+x)<2e-6&&std::abs(bounds.right()-1-x)<2e-6,"ombre sferiche sovrapposte unite senza lacune");}
  if(z<0){auto section=view;section.kind=DrawingViewKind::Section;section.sectionStyle=DrawingSectionStyle::Full;QPainterPath material;DrawingViewVisibility cut;
   ok(drawingViewVisibility(model,section,curves,{},cache,cut,&material));require(cut.curves[0].hidden.isEmpty(),"sfera interamente asportata dalla sezione non occlude il bordo posteriore");}
 }
 // Una calotta aperta non deve riempire l'ombra della sfera intera.
 ProfileLoop meridian;meridian.segments={{std::make_shared<Circle<2>>(Vec2(),Vec2(1,0),Vec2(0,1),3),{0,kHalfPi},0}};
 DrawingModelSnapshot open{{QUuid::createUuid(),{}},{source(1,makeBox(Frame3(Vec3(-5,5,-1),Vec3(0,0,1),Vec3(1,0,0)),10,1,.1)),source(2,makeSheetRevolution(Frame3(),{meridian},kTwoPi))}};
 DrawingView side;side.kind=DrawingViewKind::ProjectedEdges;side.plane=Frame3(Vec3(),Vec3(0,-1,0),Vec3(1,0,0));std::vector<DrawingCurve> edges;ok(projectDrawingEdges(open,side,edges));
 edges.erase(std::remove_if(edges.begin(),edges.end(),[&](const auto &c){if(c.featureId!=1)return true;const auto &edge=open.bodies[0].body->edge(c.sourceEdge);const auto p=edge.curve->point(.5*(edge.range.lo+edge.range.hi));return std::abs(p.y()-5)>1e-8||std::abs(p.z()+.9)>1e-8;}),edges.end());require(edges.size()==1,"bordo dietro la parte assente della calotta");
 DrawingSilhouetteCache cache;DrawingViewVisibility paths;ok(drawingViewVisibility(open,side,edges,{},cache,paths));require(paths.curves[0].hidden.isEmpty()&&std::abs(pathLength(paths.curves[0].visible)-10)<1e-7,"superficie sferica rifilata conserva la propria apertura");
}
void cylinderOcclusion(){
 for(bool rotated:{false,true})for(auto axis:{Vec3(0,0,1),Vec3(0,0,-1),Vec3(0,1,0),Vec3(0,.6,.8)})for(bool narrow:{false,true})for(double scale:{.1,1.,10.}){
  const Frame3 frame=rotated?Frame3(Vec3(13,-7,100),Vec3(.3,-.8,1),Vec3(1,0,0)):Frame3();
  const double y=std::abs(axis.z())<1e-10?(narrow?-.0001:2.):-std::abs(axis.z())*(narrow?2.9999:.7);
  DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,makeBox(Frame3(frame.toGlobal(Vec3(-5,y,-100)),frame.zDir(),frame.xDir()),10,.05,.1)),source(2,makeCylinder(Frame3(frame.toGlobal(Vec3(0,0,10)),frame.directionToGlobal(axis),frame.xDir()),3,4))}};
  DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.plane=frame;view.scale=scale;std::vector<DrawingCurve> curves;ok(projectDrawingEdges(model,view,curves));
  curves.erase(std::remove_if(curves.begin(),curves.end(),[&](const auto &c){if(c.featureId!=1)return true;const auto &edge=model.bodies[0].body->edge(c.sourceEdge);const auto p=frame.toLocal(edge.curve->point(.5*(edge.range.lo+edge.range.hi)));return std::abs(p.y()-y)>1e-8||std::abs(p.z()+99.9)>1e-8;}),curves.end());require(curves.size()==1,"segmento dietro il cilindro isolato");
  DrawingSilhouetteCache cache;DrawingViewVisibility paths;ok(drawingViewVisibility(model,view,curves,{},cache,paths));
  if(std::abs(axis.z())<1e-10&&narrow){require(paths.curves[0].hidden.isEmpty(),"cilindro finito non occlude oltre la base");continue;}
  const double x=std::abs(axis.z())<1e-10?3:std::sqrt(9-y*y/(axis.z()*axis.z()));const auto box=paths.curves[0].hidden.boundingRect();
  require(!paths.curves[0].hidden.isEmpty()&&std::abs(box.left()/scale+x)<2e-6&&std::abs(box.right()/scale-x)<2e-6,"ombra esatta del cilindro assiale/laterale/obliquo sul segmento");
  if(!narrow&&scale==1){auto section=view;section.kind=DrawingViewKind::Section;section.sectionStyle=DrawingSectionStyle::Full;QPainterPath material;DrawingViewVisibility cut;
   ok(drawingViewVisibility(model,section,curves,{},cache,cut,&material));require(cut.curves[0].hidden.isEmpty(),"cilindro asportato dalla sezione non conserva la propria ombra");}
 }
 // Tagli su coniche: sagoma circolare, generatrici laterali, base ellittica.
 for(int orientation:{0,1,2})for(bool elliptical:{false,true})for(double separation:{4.,7.9999})for(double scale:{.1,1.,10.}){
  if(orientation==2&&elliptical)continue;
  const double minor=elliptical?3.:5.;ProfileRegion region;region.outer.segments={{std::make_shared<Ellipse<2>>(Vec2(),Vec2(1,0),Vec2(0,1),5,minor),{0,kTwoPi},0}};
  const Vec3 axis=orientation==0?Vec3(0,0,1):orientation==1?Vec3(0,1,0):Vec3(0,.6,.8),origin(separation,orientation==1?-10:0,10);
  DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,elliptical?makeExtrusion(Frame3(),region,1):makeCylinder(Frame3(),5,1)),source(2,makeCylinder(Frame3(origin,axis,Vec3(1,0,0)),3,orientation==1?20:4))}};
  DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.scale=scale;std::vector<DrawingCurve> curves;ok(projectDrawingEdges(model,view,curves));
  curves.erase(std::remove_if(curves.begin(),curves.end(),[&](const auto &c){return c.featureId!=1||std::abs(model.bodies[0].body->edge(c.sourceEdge).curve->point(kPi).z()-1)>1e-8;}),curves.end());require(curves.size()==1,"conica dietro il cilindro isolata");
  if(orientation==2){curves[0].hasSourceRange=true;curves[0].sourceRange={-.8,0};curves[0].range=curves[0].sourceRange;}
  DrawingSilhouetteCache cache;DrawingViewVisibility paths;ok(drawingViewVisibility(model,view,curves,{},cache,paths));
  double x=orientation==1?separation-3:elliptical?.625*separation:(25+separation*separation-9)/(2*separation);
  if(orientation==2){const double a=1-1/.64,c=separation*separation+25/.64-9;x=2*c/(2*separation+std::sqrt(4*separation*separation-4*a*c));}
  const double y=minor*std::sqrt(1-x*x/25);const auto box=paths.curves[0].hidden.boundingRect();
  require(!paths.curves[0].hidden.isEmpty()&&std::abs(box.left()/scale-x)<2e-6&&std::abs(box.bottom()/scale-y)<2e-6&&std::abs(box.top()/scale-(orientation==2?0:-y))<2e-6,"conica tagliata sulla superficie esatta del cilindro e delle basi");
 }
 // Variazione di profondita': il fianco curvo, non la sua sagoma, decide il taglio.
 for(double z:{0.,10.,12.8,14.}){
  DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,makeBox(Frame3(Vec3(-5,2,z),Vec3(0,0,1),Vec3(1,0,0)),10,.05,.1)),source(2,makeCylinder(Frame3(Vec3(0,0,10),Vec3(0,1,0),Vec3(1,0,0)),3,4))}};
  DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;std::vector<DrawingCurve> curves;ok(projectDrawingEdges(model,view,curves));curves.erase(std::remove_if(curves.begin(),curves.end(),[&](const auto &c){if(c.featureId!=1)return true;const auto &edge=model.bodies[0].body->edge(c.sourceEdge);const auto p=edge.curve->point(.5*(edge.range.lo+edge.range.hi));return std::abs(p.y()-2)>1e-8||std::abs(p.z()-z-.1)>1e-8;}),curves.end());require(curves.size()==1,"segmento per il taglio in profondita' del cilindro");
  DrawingSilhouetteCache cache;DrawingViewVisibility paths;ok(drawingViewVisibility(model,view,curves,{},cache,paths));if(z>13){require(paths.curves[0].hidden.isEmpty(),"bordo davanti al cilindro visibile");continue;}
  const double depth=std::max(0.,z+.1-10+4*view.tolerance),x=std::sqrt(9-depth*depth);const auto box=paths.curves[0].hidden.boundingRect();require(std::abs(box.left()+x)<2e-6&&std::abs(box.right()-x)<2e-6,"profondita' radiale del cilindro verificata analiticamente");
 }
 ProfileLoop outside;outside.segments={{std::make_shared<Circle<2>>(Vec2(),Vec2(1,0),Vec2(0,1),3),{0,kTwoPi},0}};ProfileLoop inside;inside.segments={{std::make_shared<Circle<2>>(Vec2(),Vec2(1,0),Vec2(0,1),1),{0,kTwoPi},0}};
 for(bool open:{false,true}){ProfileRegion annulus;annulus.outer=outside;annulus.holes={reversed(inside)};const Frame3 base(Vec3(0,0,10),Vec3(0,0,1),Vec3(1,0,0));
  DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,makeBox(Frame3(Vec3(-5,.2,-100),Vec3(0,0,1),Vec3(1,0,0)),10,.05,.1)),source(2,open?makeSheetExtrusion(base,{outside},4):makeExtrusion(base,annulus,4))}};
  DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;std::vector<DrawingCurve> curves;ok(projectDrawingEdges(model,view,curves));curves.erase(std::remove_if(curves.begin(),curves.end(),[&](const auto &c){if(c.featureId!=1)return true;const auto &edge=model.bodies[0].body->edge(c.sourceEdge);const auto p=edge.curve->point(.5*(edge.range.lo+edge.range.hi));return std::abs(p.y()-.2)>1e-8||std::abs(p.z()+99.9)>1e-8;}),curves.end());require(curves.size()==1,"segmento dietro il tubo isolato");
  DrawingSilhouetteCache cache;DrawingViewVisibility paths;ok(drawingViewVisibility(model,view,curves,{},cache,paths));
  require(open?paths.curves[0].hidden.isEmpty():(starts(paths.curves[0].hidden)==2&&starts(paths.curves[0].visible)==3),"cilindro aperto/cavo conserva il foro e non diventa un cilindro pieno");
 }
}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
 conicTransitions();conicDepth();conicLimits();sphereOcclusion();cylinderOcclusion();
 DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(1,makeBox(Frame3(),10,6,1))}};DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;std::vector<DrawingCurve> curves;ok(projectDrawingEdges(model,view,curves));DrawingSilhouetteCache cache;DrawingViewVisibility visible;ok(drawingViewVisibility(model,view,curves,{},cache,visible));require(visible.curves.size()==curves.size(),"un risultato per ogni curva esatta");int back=0,front=0;
 for(std::size_t i=0;i<curves.size();++i){const auto &edge=model.bodies[0].body->edge(curves[i].sourceEdge);const auto p=edge.curve->point((edge.range.lo+edge.range.hi)/2);if(std::abs(p.z())<1e-8){++back;require(visible.curves[i].visible.isEmpty()&&visible.curves[i].hidden.isEmpty(),"bordo inferiore coincidente eliminato sotto il contorno visibile");}if(std::abs(p.z()-1)<1e-8){++front;require(!visible.curves[i].visible.isEmpty()&&visible.curves[i].hidden.isEmpty(),"bordi anteriori visibili, nessuna auto-occlusione");}}
 double contour=0;for(const auto &p:visible.curves)contour+=pathLength(p.visible);require(std::abs(contour-32)<1e-7,"ogni segmento del contorno proiettato e disegnato una sola volta");
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
 // La cache riusa la mesh piu' fine anche dopo una richiesta meno precisa.
 const auto coarse=cache.seeds(round.bodies[0]);const auto fine=cache.seeds(round.bodies[0],.001);
 require(fine!=coarse&&cache.seeds(round.bodies[0],.001)==fine&&cache.seeds(round.bodies[0])==fine,"raffinamento della cache e riuso senza ritorno alla mesh grossolana");

 // Il profilo convesso deve essere continuo: le corde approssimate non
 // possono essere nascoste dalla superficie da cui sono state estratte.
 {DrawingModelSnapshot convex{{QUuid::createUuid(),{}},{source(1,makeSphere(Frame3(),100))}};
  DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;DrawingSilhouetteCache seeds;DrawingSilhouetteResult contour;DrawingViewVisibility paths;std::vector<DrawingCurve> edges;
  for(double scale:{1.,4.}){v.scale=scale;ok(drawingViewSilhouettes(convex,v,contour,seeds));ok(projectDrawingEdges(convex,v,edges));ok(drawingViewVisibility(convex,v,edges,contour,seeds,paths));double hidden=0,shown=0;for(const auto &p:paths.silhouettes){hidden+=pathLength(p.hidden);shown+=pathLength(p.visible);}require(shown>620*scale&&hidden<.001,"profilo sferico interamente visibile, senza falsa auto-occlusione");}
  v.scale=1;convex.bodies.push_back(source(2,makeBox(Frame3(Vec3(-2.5,-150,150),Vec3(0,0,1),Vec3(1,0,0)),5,300,1)));
  ok(drawingViewSilhouettes(convex,v,contour,seeds));ok(projectDrawingEdges(convex,v,edges));ok(drawingViewVisibility(convex,v,edges,contour,seeds,paths));double hidden=0,shown=0;for(std::size_t i=0;i<contour.paths.size();++i)if(contour.paths[i].featureId==1){hidden+=pathLength(paths.silhouettes[i].hidden);shown+=pathLength(paths.silhouettes[i].visible);}require(hidden>5&&shown>600,"un altro corpo continua a nascondere il contorno tangente");
 }
 // La stessa faccia puo' invece occludere davvero un proprio ramo concavo.
 {DrawingModelSnapshot torus{{QUuid::createUuid(),{}},{source(1,makeTorus(Frame3(),10,3))}};require(torus.bodies[0].body->faces().size()==1,"toro con una sola faccia");
  DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.plane=Frame3(Vec3(),normalized(Vec3(0,-1,.35)),Vec3(1,0,0));DrawingSilhouetteCache seeds;DrawingSilhouetteResult contour;DrawingViewVisibility paths;std::vector<DrawingCurve> edges;
  ok(drawingViewSilhouettes(torus,v,contour,seeds));ok(projectDrawingEdges(torus,v,edges));ok(drawingViewVisibility(torus,v,edges,contour,seeds,paths));double hidden=0,shown=0;for(const auto &p:paths.silhouettes){hidden+=pathLength(p.hidden);shown+=pathLength(p.visible);}require(hidden>1&&shown>10,"auto-occlusione reale del toro conservata");
 }
 // Due circonferenze coincidenti viste dall'alto: il bordo nascosto non
 // deve sovrapporsi al cerchio visibile quando si abilita la sua visualizzazione.
 {DrawingModelSnapshot cylinder{{QUuid::createUuid(),{}},{source(1,makeCylinder(Frame3(),12,40))}};DrawingView top;top.kind=DrawingViewKind::ProjectedEdges;std::vector<DrawingCurve> rings;DrawingViewVisibility paths;DrawingSilhouetteCache seeds;
  ok(projectDrawingEdges(cylinder,top,rings));ok(drawingViewVisibility(cylinder,top,rings,{},seeds,paths));int owners=0;double shown=0,hidden=0;for(const auto &p:paths.curves){owners+=!p.visible.isEmpty();shown+=pathLength(p.visible);hidden+=pathLength(p.hidden);}require(owners==1&&std::abs(shown-24*kPi)<.02&&hidden<1e-6,"cerchio proiettato unico senza nascosto coincidente in overlay");
 }
 auto invalid=round;invalid.bodies.push_back(invalid.bodies[0]);const auto before=visible.silhouettes.size();require(!drawingViewVisibility(invalid,view,curves,silhouettes,cache,visible).isEmpty()&&visible.silhouettes.size()==before,"sorgente incoerente rifiutata atomicamente");
 std::cout<<"PASS visible/hidden spans, partial and inter-body occlusion, view direction, sections, surfaces and silhouettes\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
