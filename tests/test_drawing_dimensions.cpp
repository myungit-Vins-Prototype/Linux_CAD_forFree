#include "cad_drawing_dimension.h"
#include "cad_drawing_sheet.h"
#include "fk_primitives.h"
#include "fk_sheet.h"
#include "fk_curve_algo.h"
#include "fk_bspline_surface.h"
#include "fk_curve_ops.h"
#include "fk_classify.h"
#include "fk_pcurve.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
void near(double a,double b,const char *why){require(std::isfinite(a)&&std::abs(a-b)<1e-7,why);}
DrawingSourceBody source(Body b,quint64 id=1){auto body=std::make_shared<Body>(std::move(b));return {id,body,std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"dimension"+std::to_string(id)))};}
std::vector<DrawingCurve> curves(const DrawingModelSnapshot &m,const DrawingView &v){std::vector<DrawingCurve> out;ok(drawingViewCurves(m,v,out));return out;}
DrawingDimensionResult result(const DrawingModelSnapshot &m,const DrawingView &v,const DrawingDimension &d){DrawingDocument doc;doc.model=m.version;doc.views={v};doc.dimensions={d};return evaluateDrawingDimension(doc,m,d.id);}
DrawingAttachment point(const DrawingView &v,const DrawingModelSnapshot &m,const std::vector<DrawingCurve> &cs,Vec2 wanted){
    for(const auto &c:cs)for(auto role:{DrawingPointRole::Start,DrawingPointRole::End}){auto ref=drawingAttachment(v,c,m,role);auto found=resolveDrawingAttachment(v,m,cs,ref);if(found.status==DrawingDimensionStatus::Ready&&distance(found.point,wanted)<1e-7)return ref;}
    throw std::runtime_error("punto richiesto non trovato");
}
}
int main(){try{
    DrawingModelSnapshot model{{QUuid::createUuid(),{}},{source(makeBox(Frame3(),10,6,4))}};DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;auto cs=curves(model,view);
    const auto p=point(view,model,cs,{0,0}),q=point(view,model,cs,{10,6});
    auto distanceDim=drawingDimensionBetween(view,model,cs,{p,q},DrawingMeasure::Distance,{20,20});near(result(model,view,distanceDim).value,std::sqrt(136.),"distanza fra punti di bordi diversi");
    auto horizontal=distanceDim;horizontal.measure=DrawingMeasure::HorizontalDistance;near(result(model,view,horizontal).value,10,"distanza orizzontale fra punti");
    auto vertical=distanceDim;vertical.measure=DrawingMeasure::VerticalDistance;near(result(model,view,vertical).value,6,"distanza verticale fra punti");
    const DrawingCurve *bottom=nullptr,*top=nullptr,*left=nullptr;
    for(const auto &c:cs){const auto a=view.plane.toLocal(c.curve->point(c.range.lo)),b=view.plane.toLocal(c.curve->point(c.range.hi));if(std::abs(a.y())<1e-7&&std::abs(b.y())<1e-7)bottom=&c;if(std::abs(a.y()-6)<1e-7&&std::abs(b.y()-6)<1e-7)top=&c;if(std::abs(a.x())<1e-7&&std::abs(b.x())<1e-7)left=&c;}
    require(bottom&&top&&left,"rette del rettangolo");auto line=drawingAttachment(view,*bottom,model,DrawingPointRole::Curve),other=drawingAttachment(view,*top,model,DrawingPointRole::Curve);
    auto pointLine=drawingDimensionBetween(view,model,cs,{q,line},DrawingMeasure::Distance,{12,8});auto perpendicular=result(model,view,pointLine);near(perpendicular.value,6,"punto-retta perpendicolare");near(perpendicular.second.x(),10,"piede sulla retta");
    auto linePoint=drawingDimensionBetween(view,model,cs,{line,q},DrawingMeasure::Distance,{12,8});near(result(model,view,linePoint).value,6,"linea-punto con selezione inversa");
    // La retta e' infinita, la distanza non si limita al segmento.
    DrawingAttachment origin;origin.role=DrawingPointRole::Origin;auto outside=drawingDimensionBetween(view,model,cs,{q,drawingAttachment(view,*left,model,DrawingPointRole::Curve)},DrawingMeasure::Distance,{20,20});near(result(model,view,outside).value,10,"punto-retta orientato");
    auto parallel=drawingDimensionBetween(view,model,cs,{line,other},DrawingMeasure::Distance,{3,10});near(result(model,view,parallel).value,6,"rette parallele");
    auto automaticLine=drawingDimensionBetween(view,model,cs,{line},DrawingMeasure::Distance,{5,-10},true);require(automaticLine.measure==DrawingMeasure::HorizontalDistance,"automatica propone componente secondo il testo");near(result(model,view,automaticLine).value,10,"quota automatica di una retta");
    auto automaticPoints=drawingDimensionBetween(view,model,cs,{p,q},DrawingMeasure::Distance,{-10,-10},true);require(automaticPoints.measure==DrawingMeasure::Distance,"automatica allineata fuori dalle componenti");
    auto automaticParallel=drawingDimensionBetween(view,model,cs,{line,other},DrawingMeasure::Distance,{0,10},true);near(result(model,view,automaticParallel).value,6,"automatica fra rette parallele");
    auto angle=drawingDimensionBetween(view,model,cs,{line,drawingAttachment(view,*left,model,DrawingPointRole::Curve)},DrawingMeasure::Angle,{3,3});near(result(model,view,angle).value,90,"angolo fra rette");
    auto invalid=angle;invalid.references[1]=other;require(result(model,view,invalid).status==DrawingDimensionStatus::Invalid&&std::isnan(result(model,view,invalid).value),"angolo parallelo rifiutato senza valore");
    DrawingAttachment axis;axis.role=DrawingPointRole::AxisY;auto axial=drawingDimensionBetween(view,model,cs,{q,axis},DrawingMeasure::AxisRadius,{15,8});near(result(model,view,axial).value,10,"raggio rispetto asse");axial.measure=DrawingMeasure::AxisDiameter;near(result(model,view,axial).value,20,"diametro rispetto asse");
    auto fromOrigin=drawingDimensionBetween(view,model,cs,{origin,q},DrawingMeasure::Distance,{15,10});near(result(model,view,fromOrigin).value,std::sqrt(136.),"origine e punto");
    auto midpoint=drawingAttachment(view,*bottom,model,DrawingPointRole::Midpoint);near(resolveDrawingAttachment(view,model,cs,midpoint).point.x(),5,"snap punto medio");
    auto nearest=drawingAttachment(view,*bottom,model,DrawingPointRole::Nearest,bottom->range.lo+.3*bottom->range.length());auto snapped=resolveDrawingAttachment(view,model,cs,nearest);require(snapped.status==DrawingDimensionStatus::Ready,"snap piu' vicino");
    const auto beforePoint=snapped.point;
    // La stessa genealogia con edge invertiti deve conservare gli estremi e l'orientamento.
    auto reversedBody=std::make_shared<Body>(*model.bodies[0].body);for(auto e:reversedBody->edges())reversedBody->reverseEdge(e);
    auto reversedNames=std::make_shared<ElementNames>(*model.bodies[0].names);reversedNames->body=reversedBody.get();auto reversed=model;reversed.bodies={{1,reversedBody,reversedNames}};
    near(result(reversed,view,distanceDim).value,std::sqrt(136.),"punti associati dopo inversione dei bordi");const auto reverseCurves=curves(reversed,view);near(distance(resolveDrawingAttachment(view,reversed,reverseCurves,nearest).point,beforePoint),0,"nearest stabile al cambio verso");
    auto changed=model;changed.bodies={source(makeBox(Frame3(),14,8,4))};near(result(changed,view,horizontal).value,14,"quota segue vertici nominati dopo modifica");near(result(changed,view,vertical).value,8,"secondo aggancio segue geometria");
    auto missing=horizontal;missing.references[1].vertexName="sparito";require(result(model,view,missing).status==DrawingDimensionStatus::Missing,"punto cancellato non sostituito con un estremo per indice");
    // Due corpi nella stessa vista, entrambi controllati dal foglio.
    auto multiple=model;multiple.bodies.push_back(source(makeBox(Frame3(Vec3(20,0,0),Vec3(0,0,1),Vec3(1,0,0)),2,2,2),2));auto all=curves(multiple,view);auto second=point(view,multiple,all,{20,0});auto multi=drawingDimensionBetween(view,multiple,all,{p,second},DrawingMeasure::Distance,{15,5});near(result(multiple,view,multi).value,20,"punti su corpi diversi");
    DrawingSheet sheet;sheet.drawing.model=model.version;sheet.drawing.views={view};sheet.views={{view.id,"Vista",QPointF(100,100),{1}}};sheet.drawing.dimensions={multi};sheet.dimensions={{multi.id,QPointF(50,50)}};QByteArray bytes;
    require(!encodeDrawingSheet(sheet,bytes).isEmpty(),"secondo aggancio estraneo alla vista rifiutato");sheet.views[0].features.append(2);ok(encodeDrawingSheet(sheet,bytes));DrawingSheet restored;ok(decodeDrawingSheet(bytes,restored));near(result(multiple,view,restored.drawing.dimensions[0]).value,20,"due agganci persistenti");
    auto object=QJsonDocument::fromJson(bytes).object();auto drawing=object["drawing"].toObject();require(drawing["schema"].toInt()==3,"schema 3 solo per nuove quote");drawing["schema"]=2;object["drawing"]=drawing;require(!decodeDrawingSheet(QJsonDocument(object).toJson(),restored).isEmpty(),"schema declassato non perde i nuovi agganci");
    auto malformed=QJsonDocument::fromJson(bytes).object();auto malformedDrawing=malformed["drawing"].toObject();auto malformedDimensions=malformedDrawing["dimensions"].toArray();auto malformedDimension=malformedDimensions[0].toObject();auto malformedRefs=malformedDimension["references"].toArray();auto malformedRef=malformedRefs[1].toObject();malformedRef["role"]=99;malformedRefs[1]=malformedRef;malformedDimension["references"]=malformedRefs;malformedDimensions[0]=malformedDimension;malformedDrawing["dimensions"]=malformedDimensions;malformed["drawing"]=malformedDrawing;
    const auto restoredId=restored.drawing.version.documentId;require(!decodeDrawingSheet(QJsonDocument(malformed).toJson(),restored).isEmpty()&&restored.drawing.version.documentId==restoredId,"agganci malformati rifiutati atomicamente");
    // Cerchi nel piano: raggio, diametro, centro, quadranti e nearest polare.
    DrawingModelSnapshot cylinder{model.version,{source(makeCylinder(Frame3(),3,8))}};auto circles=curves(cylinder,view);const auto circular=std::find_if(circles.begin(),circles.end(),[&](const auto &c){return drawingPrimitive(view,c).circular;});require(circular!=circles.end(),"cerchio analitico proiettato");
    auto circle=drawingAttachment(view,*circular,cylinder,DrawingPointRole::Curve);auto radial=drawingDimensionBetween(view,cylinder,circles,{circle},DrawingMeasure::Radius,{10,10});near(result(cylinder,view,radial).value,3,"raggio");radial.measure=DrawingMeasure::Diameter;near(result(cylinder,view,radial).value,6,"diametro");
    auto automaticCircle=drawingDimensionBetween(view,cylinder,circles,{circle},DrawingMeasure::Distance,{10,10},true);require(automaticCircle.measure==DrawingMeasure::Diameter,"cerchio intero propone diametro");near(result(cylinder,view,automaticCircle).value,6,"diametro automatico");
    DrawingView circularSection;circularSection.plane=Frame3(Vec3(0,0,4),Vec3(0,0,1),Vec3(1,0,0));auto cutCircles=curves(cylinder,circularSection);require(!cutCircles.empty(),"sezione circolare del cilindro");
    auto cutCircle=drawingAttachment(circularSection,cutCircles[0],cylinder,DrawingPointRole::Curve);auto sectionRadius=drawingDimensionBetween(circularSection,cylinder,cutCircles,{cutCircle},DrawingMeasure::Radius,{10,10});near(result(cylinder,circularSection,sectionRadius).value,3,"raggio sulla sezione della superficie");
    const auto center=resolveDrawingAttachment(view,cylinder,circles,drawingAttachment(view,*circular,cylinder,DrawingPointRole::Center));near(norm(center.point),0,"centro esatto");
    const auto quadrant=resolveDrawingAttachment(view,cylinder,circles,drawingAttachment(view,*circular,cylinder,DrawingPointRole::Quadrant1));near(quadrant.point.y(),3,"quadrante nel piano");
    auto circleNearest=drawingAttachment(view,*circular,cylinder,DrawingPointRole::Nearest,circular->range.lo+.2*circular->range.length());require(resolveDrawingAttachment(view,cylinder,circles,circleNearest).status==DrawingDimensionStatus::Ready,"nearest su circonferenza senza falsa origine topologica");
    auto oblique=view;oblique.plane=Frame3(Vec3(),normalized(Vec3(0,1,1)),Vec3(1,0,0));auto ellipse=curves(cylinder,oblique);require(!drawingPrimitive(oblique,ellipse[0]).circular,"ellisse proiettata non scambiata per cerchio");
    auto ellipseRef=drawingAttachment(oblique,ellipse[0],cylinder,DrawingPointRole::Curve);auto badRadius=radial;badRadius.references={ellipseRef};badRadius.section=ellipseRef.curve;require(result(cylinder,oblique,badRadius).status==DrawingDimensionStatus::Invalid,"raggio circolare su ellisse rifiutato");
    auto ellipsePoint=drawingAttachment(oblique,ellipse[0],cylinder,DrawingPointRole::Nearest,ellipse[0].range.lo+.2*ellipse[0].range.length());
    auto resolvedEllipse=resolveDrawingAttachment(oblique,cylinder,ellipse,ellipsePoint);require(resolvedEllipse.status==DrawingDimensionStatus::Ready,"nearest polare sulla conica proiettata");
    auto reversedCylinder=std::make_shared<Body>(*cylinder.bodies[0].body);for(auto e:reversedCylinder->edges())reversedCylinder->reverseEdge(e);
    auto reversedCylinderNames=std::make_shared<ElementNames>(*cylinder.bodies[0].names);reversedCylinderNames->body=reversedCylinder.get();auto reverseCylinder=cylinder;reverseCylinder.bodies={{1,reversedCylinder,reversedCylinderNames}};
    near(distance(resolveDrawingAttachment(oblique,reverseCylinder,curves(reverseCylinder,oblique),ellipsePoint).point,resolvedEllipse.point),0,"nearest della conica indipendente dal verso");
    DrawingDocument pinned;pinned.model=model.version;pinned.model.checkinId=QUuid::createUuid();pinned.views={view};pinned.dimensions={distanceDim};
    require(evaluateDrawingDimension(pinned,model,distanceDim.id).status==DrawingDimensionStatus::VersionMismatch,"quote nuove rifiutano un modello di revisione diversa");
    auto scaled=view;scaled.scale=12;near(result(model,scaled,distanceDim).value,std::sqrt(136.),"scala del foglio non altera la quota");
    // Nearest su NURBS razionale: posizione per lunghezza e origine topologica.
    const auto patch=[](double width,bool reverse=false){
        auto surface=std::make_shared<BSplineSurface>(2,1,std::vector<double>{0,0,0,1,1,1},std::vector<double>{0,0,1,1},3,2,
            std::vector<Vec3>{{-width/2,-4,0},{-width/2,4,0},{0,-4,3},{0,4,3},{width/2,-4,0},{width/2,4,0}},std::vector<double>{1,1,.7,.7,1,1});
        const std::vector<Vec3> points{surface->point(0,0),surface->point(1,0),surface->point(1,1),surface->point(0,1)};
        std::vector<Body::BuildEdge> edges{{0,1,surface->vIso(0),{0,1},0},{1,2,surface->uIso(1),{0,1},0},{3,2,surface->vIso(1),{0,1},0},{0,3,surface->uIso(0),{0,1},0}};
        Body::BuildFace face;face.surface=surface;face.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,false,nullptr,0},{3,false,nullptr,0}}};if(reverse){for(auto &e:edges){std::swap(e.start,e.end);e.curve=reversedCurve<3>(e.curve);e.range={-e.range.hi,-e.range.lo};}for(auto &fin:face.loops[0])fin.sense=!fin.sense;}auto body=Body::buildSheet(points,edges,{face});computePCurves(body);return body;};
    DrawingModelSnapshot nurbsModel{model.version,{source(patch(10))}};auto nurbsCurves=curves(nurbsModel,oblique);const auto &nurbs=nurbsCurves[0];
    auto nurbsNearest=drawingAttachment(oblique,nurbs,nurbsModel,DrawingPointRole::Nearest,nurbs.range.lo+.27*nurbs.range.length());auto exactNearest=resolveDrawingAttachment(oblique,nurbsModel,nurbsCurves,nurbsNearest);require(exactNearest.status==DrawingDimensionStatus::Ready,"nearest su NURBS");
    const auto expected=oblique.plane.toLocal(nurbs.curve->point(nurbs.range.lo+.27*nurbs.range.length()));near(distance(exactNearest.point,Vec2(expected.x(),expected.y())),0,"nearest NURBS misurato sulla curva esatta");
    auto reversedNurbs=nurbsModel;reversedNurbs.bodies={source(patch(10,true))};
    near(distance(resolveDrawingAttachment(oblique,reversedNurbs,curves(reversedNurbs,oblique),nurbsNearest).point,exactNearest.point),0,"nearest NURBS conserva il punto dopo inversione");
    nurbsModel.bodies={source(patch(14))};const auto resizedCurves=curves(nurbsModel,oblique);const auto resized=resolveDrawingAttachment(oblique,nurbsModel,resizedCurves,nurbsNearest);require(resized.status==DrawingDimensionStatus::Ready,"nearest NURBS segue la superficie modificata");
    // Sezione di una superficie aperta: estremi ancorati ai bordi che la rifilano.
    DrawingModelSnapshot surface{model.version,{source(makePlaneSheet(Frame3(),5))}};DrawingView cut;cut.plane=Frame3(Vec3(),Vec3(0,1,0),Vec3(1,0,0));auto section=curves(surface,cut);require(section.size()==1,"sezione superficie aperta");
    auto start=drawingAttachment(cut,section[0],surface,DrawingPointRole::Start),end=drawingAttachment(cut,section[0],surface,DrawingPointRole::End);require(!start.boundaryName.isEmpty()&&!end.boundaryName.isEmpty(),"provenienza dei due estremi della sezione");
    auto cutDim=drawingDimensionBetween(cut,surface,section,{start,end},DrawingMeasure::Distance,{0,10});near(result(surface,cut,cutDim).value,10,"quota punto-punto della sezione");surface.bodies={source(makePlaneSheet(Frame3(),7))};near(result(surface,cut,cutDim).value,14,"estremi della sezione seguono i bordi originari");
    std::cout<<"PASS associative points, line-point, parallel lines, angular/radial dimensions, schema and surface anchors\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
