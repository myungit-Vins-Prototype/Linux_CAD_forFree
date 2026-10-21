#pragma once
#include "cad_drawing_detail.h"
namespace {
std::vector<QLineF> angularLines(DrawingSheetEditor &e,QUuid id,bool preview=false){
 std::vector<QLineF> out;
 for(auto *item:e.canvas()->scene()->items())if(auto *line=dynamic_cast<QGraphicsLineItem *>(item)){
  if(preview?line->zValue()==20:(item->data(5).toString()=="dimensionGeometry"&&item->data(3).toUuid()==id))out.push_back(line->line());
 }
 return out;
}
void checkAngularExtensions(DrawingSheetEditor &e,const DrawingView &v,const DrawingEditorModel &model,const DrawingDimension &d,QPointF anchor,bool preview=false){
 const auto &curves=e.curves(v.id);const auto resolver=drawingSheetAttachmentResolver(e.sheet(),v,model.snapshot,curves);
 const auto r=evaluateDrawingAttachments(d,v,model.snapshot,curves,resolver);require(r.status==DrawingDimensionStatus::Ready,"angolo valido nella prova dei richiami");
 const auto layout=*std::find_if(e.sheet().views.begin(),e.sheet().views.end(),[&](const auto &l){return l.viewId==v.id;});
 const auto paperPoint=[&](Vec2 p){return layout.position+v.scale*QPointF(p.x(),-p.y());};
 const auto vertex=paperPoint(r.vertex);const double radius=std::max(5.,QLineF(anchor,vertex).length());std::vector<QLineF> expected;
 for(int i=0;i<2;++i){
  if(drawingDatum(d.references[i].role))continue;
  const auto ref=resolveDrawingAttachment(v,model.snapshot,curves,d.references[i],resolver);require(ref.isLine,"riferimento segmento o asse");
  auto a=paperPoint(ref.first),b=paperPoint(ref.second);
  if(drawingIsDetail(layout)){QPainterPath path;path.moveTo(a);path.lineTo(b);const auto box=drawingDetailBounds(v,layout).translated(layout.position);const auto cut=clipDrawingDetailPath(path,box.center(),box.width()/2);if(cut.isEmpty())continue;a=cut.pointAtPercent(0);b=cut.pointAtPercent(1);}
  const auto u=i?r.secondDirection:r.firstDirection;const auto target=vertex+radius*QPointF(u.x(),-u.y());
  // Se il punto dell'arco e' fra gli estremi, il modello fa gia' da riferimento.
  const double length=QLineF(a,b).length();if(QLineF(a,target).length()+QLineF(target,b).length()<=length+1e-7)continue;
  const auto end=QLineF(a,target).length()<QLineF(b,target).length()?a:b;const auto delta=target-end;const double gap=QLineF(end,target).length();
  if(gap>e.sheet().style.extensionGap+1e-8)expected.emplace_back(end+delta*(e.sheet().style.extensionGap/gap),target);
 }
 const auto actual=angularLines(e,d.id,preview);
 for(const auto &want:expected){bool found=false;for(const auto &line:actual)if(QLineF(want.p1(),line.p1()).length()<1e-6&&QLineF(want.p2(),line.p2()).length()<1e-6)found=true;require(found,"richiamo angolare dal solo estremo necessario, con distacco in mm foglio");}
 // Un eventuale collegamento al testo e' distinto dai due richiami.
 for(const auto &line:actual){bool witness=false;for(const auto &want:expected)if(QLineF(want.p1(),line.p1()).length()<1e-6&&QLineF(want.p2(),line.p2()).length()<1e-6)witness=true;
  require(witness||QLineF(line.p2(),anchor).length()<1e-6,"nessun richiamo superfluo dal vertice o sopra il segmento");}
}
void drawingAngularExtensionCase(double degrees){
 const double angle=-degrees*kPi/180;DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};
 model.snapshot.bodies={source(makePlaneSheet(Frame3(Vec3(0,5,0),Vec3(0,0,1),Vec3(1,0,0)),5),1),source(makePlaneSheet(Frame3(Vec3(0,8,0),Vec3(0,0,1),Vec3(std::cos(angle),std::sin(angle),0)),5),2)};model.choices={{1,"Primo segmento"},{2,"Secondo segmento"}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;sheet.style.extensionGap=1.25;DrawingSheetEditor e(sheet,model);e.resize(1450,950);e.show();QApplication::processEvents();e.fitSheet();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=3;ok(e.insertView(v,{v.id,"Angolo 10°",{95,120},{1,2}}));
 const auto reference=[&](quint64 feature){const DrawingCurve *best=nullptr;double y=1e9;for(const auto &c:e.curves(v.id))if(c.featureId==feature){const auto p=drawingPrimitive(v,c);if(p.kind!=DrawingPrimitive::Kind::Line)continue;const auto a=v.plane.toLocal(c.curve->point(c.range.lo)),b=v.plane.toLocal(c.curve->point(c.range.hi));const auto u=normalized(b-a);if(std::abs(feature==1?u.x():u.x()*std::cos(angle)+u.y()*std::sin(angle))<.99)continue;const double mid=(a.y()+b.y())/2;if(mid<y){y=mid;best=&c;}}require(best,"segmento inclinato della fixture");return drawingAttachment(v,*best,model.snapshot,DrawingPointRole::Curve);};
 const auto first=reference(1),second=reference(2);auto d=drawingDimensionBetween(v,model.snapshot,e.curves(v.id),{first,second},DrawingMeasure::Angle,{0,0});
 ok(e.addDimension(d,{70,130}));
 for(double scale:{1.,3.,5.}){v.scale=scale;ok(e.updateView(v,e.sheet().views[0]));
  for(bool reverse:{false,true})for(int side=0;side<4;++side)for(double reach:{5.,17.,35.}){
   d.references=reverse?std::vector<DrawingAttachment>{second,first}:std::vector<DrawingAttachment>{first,second};d.section=d.references[0].curve;d.angleSides=side;ok(e.updateDimension(d));const auto r=e.dimensionResult(d.id);near(std::min(r.value,180-r.value),degrees,"settori e riferimenti invertiti conservano la misura");
   const double a=std::atan2(r.firstDirection.y(),r.firstDirection.x())+r.sweep/2;const auto center=QPointF(95,120)+scale*QPointF(r.vertex.x()+reach*std::cos(a),-r.vertex.y()-reach*std::sin(a));ok(e.moveDimension(d.id,center));ok(e.updateDimension(d));QApplication::processEvents();checkAngularExtensions(e,v,model,d,dimensionTextItem(e,d.id)->data(6).toPointF());
  }
 }
 // Testo trascinato: controllo anche la geometria live prima del rilascio.
 v.scale=3;ok(e.updateView(v,e.sheet().views[0]));d.references={first,second};d.section=first.curve;d.angleSides=3;ok(e.updateDimension(d));ok(e.moveDimension(d.id,{66,125}));QApplication::processEvents();d=e.sheet().drawing.dimensions[0];const auto saved=encoded(e);auto *label=dimensionTextItem(e,d.id);const auto start=label->sceneBoundingRect().center();dimensionTextMouse(e,start,0);dimensionTextMouse(e,start+QPointF(-10,9),1);checkAngularExtensions(e,v,model,d,label->data(6).toPointF());require(encoded(e)==saved,"drag angolare non muta il foglio durante anteprima");dimensionTextMouse(e,start+QPointF(-10,9),2);QApplication::processEvents();d=e.sheet().drawing.dimensions[0];checkAngularExtensions(e,v,model,d,dimensionTextItem(e,d.id)->data(6).toPointF());ok(e.undo());require(encoded(e)==saved,"singolo Undo del trascinamento angolare");
 // Anteprima dello strumento Quota con selezione reale dei segmenti.
 mode(e,int(DrawingMeasure::Angle));click(e,paper({-2,0})+QPointF(20,0),Qt::ShiftModifier);const auto tilted=resolveDrawingAttachment(v,model.snapshot,e.curves(v.id),second);const auto mid=(tilted.first+tilted.second)*.5;click(e,QPointF(95+3*mid.x(),120-3*mid.y()),Qt::ShiftModifier);move(e,{65,130});
 QGraphicsItem *preview=nullptr;for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimensionPreview")preview=i;require(preview,"anteprima quota angolare");auto pending=drawingDimensionBetween(v,model.snapshot,e.curves(v.id),{first,second},DrawingMeasure::Angle,Vec2(-10,-10./3));checkAngularExtensions(e,v,model,pending,preview->data(6).toPointF(),true);key(e,Qt::Key_Escape);e.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(false);
 // Assi del piano: nessuna semiretta artificiale dal vertice.
 DrawingAttachment datum;datum.role=DrawingPointRole::AxisX;auto datumAngle=drawingDimensionBetween(v,model.snapshot,e.curves(v.id),{datum,second},DrawingMeasure::Angle,{0,0});ok(e.addDimension(datumAngle,{65,130}));QApplication::processEvents();checkAngularExtensions(e,v,model,datumAngle,dimensionTextItem(e,datumAngle.id)->data(6).toPointF());
 // Asse esplicito finito: il richiamo usa gli estremi associativi dell'asse.
 const auto edge=resolveDrawingAttachment(v,model.snapshot,e.curves(v.id),first).curve;require(edge,"curva dell'asse finito");DrawingAxis axis;axis.viewId=v.id;axis.references={drawingAttachment(v,*edge,model.snapshot,DrawingPointRole::Start),drawingAttachment(v,*edge,model.snapshot,DrawingPointRole::End)};ok(e.addAxis(axis));DrawingAttachment axisRef;axisRef.axisId=axis.id;axisRef.role=DrawingPointRole::Curve;
 auto axisAngle=drawingDimensionBetween(v,model.snapshot,e.curves(v.id),{axisRef,second},DrawingMeasure::Angle,{-10,-3},false,drawingSheetAttachmentResolver(e.sheet(),v,model.snapshot,e.curves(v.id)));ok(e.addDimension(axisAngle,{65,130}));QApplication::processEvents();checkAngularExtensions(e,v,model,axisAngle,dimensionTextItem(e,axisAngle.id)->data(6).toPointF());
 if(degrees!=10){std::cout<<"PASS richiami angolari 45 gradi:72 casi, drag, anteprima, datum e asse finito\n";return;}
 // Ritaglio al bordo del dettaglio, anche quando la sorgente continua oltre.
 DrawingSheet detail=e.sheet();detail.drawing.dimensions.clear();detail.dimensions.clear();auto parent=v;parent.id=QUuid::createUuid();detail.drawing.views.push_back(parent);auto parentLayout=detail.views[0];parentLayout.viewId=parent.id;parentLayout.position={30,50};detail.views.push_back(parentLayout);auto &dl=detail.views[0];dl.relation.parentId=parent.id;dl.relation.kind=DrawingDerivedKind::Detail;dl.relation.label="B";dl.relation.detailCenter={0,2};dl.relation.detailRadius=3;dl.relation.aligned=false;
 d.references={first,second};d.section=first.curve;d.angleSides=3;detail.drawing.dimensions={d};detail.dimensions={{d.id,{65,130}}};DrawingSheetEditor cropped(detail,model);cropped.resize(1450,950);cropped.show();QApplication::processEvents();checkAngularExtensions(cropped,v,model,d,dimensionTextItem(cropped,d.id)->data(6).toPointF());
 const auto output=qEnvironmentVariable("FORGECAD_ANGULAR_OUTPUT");if(!output.isEmpty()){QDir().mkpath(output);cropped.fitSheet();QApplication::processEvents();require(cropped.grab().save(output+"/angular-detail.png"),"QA angolo dettaglio");DrawingPdfReport report;ok(cropped.exportPdf(output+"/angular-detail.pdf",report));require(report.warnings.isEmpty(),"PDF angolare senza avvisi");}
 const auto stored=encoded(cropped);DrawingSheet restored;ok(decodeDrawingSheet(stored,restored));DrawingSheetEditor reopened(restored,model);checkAngularExtensions(reopened,v,model,d,dimensionTextItem(reopened,d.id)->data(6).toPointF());require(encoded(reopened)==stored,"riapertura non modifica riferimenti e layout");
 std::cout<<"PASS richiami angolari: 72 casi scala/settore/ordine/raggio, drag/Undo, anteprima, datum, asse finito, dettaglio, codec e PDF\n";
}
void drawingAngularExtensionTests(){drawingAngularExtensionCase(10);drawingAngularExtensionCase(45);}
}
