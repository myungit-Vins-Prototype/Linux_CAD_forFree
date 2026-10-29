#pragma once
#include "fk_revolve.h"
namespace {
void drawingSectionRadiusTests(){
 DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};
 model.snapshot.bodies={source(makeTorus(Frame3(),5,1.5),1),source(makeBox(Frame3(Vec3(6.55,-1,-2),Vec3(0,0,1),Vec3(1,0,0)),1,2,4),2)};model.choices={{1,"Raccordo toroidale"},{2,"Bordo vicino"}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1450,900);e.show();QApplication::processEvents();DrawingView v;v.scale=3;v.sectionStyle=DrawingSectionStyle::Full;v.plane=Frame3(Vec3(),Vec3(0,1,0),Vec3(1,0,0));ok(e.insertView(v,{v.id,"Sezione",{75,120},{1,2}}));e.fitSheet();QApplication::processEvents();
 int arcs=0;for(const auto &c:e.sections(v.id))if(c.featureId==1){require(c.curve->type()==CurveType::Circle,"sezione toroidale conserva cerchi esatti");++arcs;}require(arcs==2,"entrambi i rami della sezione toroidale");
 // Il bordo rettilineo e' piu' vicino del cerchio: il comando Raggio deve scartarlo.
 mode(e,int(DrawingMeasure::Radius));click(e,paper({6.55,0}));const auto outside=paper({14,5});move(e,outside);require(items(e,"dimensionPreview")==1,"raggio selezionabile accanto a una retta e alle proiezioni sovrapposte");
 QPainterPath previewArrow;for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimensionArrow")previewArrow=qgraphicsitem_cast<QGraphicsPathItem*>(i)->path();require(!previewArrow.isEmpty(),"freccia presente in anteprima");
 click(e,outside);auto dim=last(e);near(e.dimensionResult(dim.id).value,1.5,"quota del raccordo toroidale esatta");e.findChild<QAction*>("drawingSmartDimensionAction")->setChecked(false);
 auto radial=[&](DrawingSheetEditor &editor,bool external){QApplication::processEvents();const auto result=editor.dimensionResult(dim.id);const auto a=paper(result.first),b=paper(result.second);int arrows=0;double centerLength=0;QPainterPath path;
  for(auto *i:editor.canvas()->scene()->items()){
   if(i->data(0).toString()=="dimensionArrow"){path=qgraphicsitem_cast<QGraphicsPathItem*>(i)->path();++arrows;auto first=path.elementAt(0),tip=path.elementAt(1);near(tip.x,b.x(),"punta sul riferimento geometrico scelto");near(tip.y,b.y(),"punta sul raccordo");const double sign=QPointF::dotProduct(QPointF(first.x,first.y)-b,b-a);require(external?sign>0:sign<0,"corpo della freccia sul lato richiesto");}
   if(auto *line=qgraphicsitem_cast<QGraphicsLineItem*>(i)){const auto p=line->line().p1(),q=line->line().p2();const auto delta=b-a;const double squared=QPointF::dotProduct(delta,delta);const auto on=[&](QPointF point){const double t=QPointF::dotProduct(point-a,delta)/squared;return t>=-1e-8&&t<=1+1e-8&&QLineF(a+t*delta,point).length()<1e-6;};if(on(p)&&on(q))centerLength+=line->line().length();}
  }require(arrows==1,"una sola freccia per il raggio");QPainterPath internal;internal.moveTo(a);internal.lineTo(b);QGraphicsItem *label=nullptr;for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="dimension"&&item->data(3).toUuid()==dim.id)label=item;near(centerLength,external?0:drawingDimensionStroke(internal,drawingDimensionTextMask(label)).length(),"richiamo esterno senza linea interna; quota interna ritagliata solo sotto il proprio testo");return path;
 };
 const auto finalArrow=radial(e,false);require(finalArrow.elementCount()==previewArrow.elementCount(),"stessa freccia in anteprima e dopo inserimento");
 for(int i=0;i<finalArrow.elementCount();++i){near(finalArrow.elementAt(i).x,previewArrow.elementAt(i).x,"freccia senza salto orizzontale al clic");near(finalArrow.elementAt(i).y,previewArrow.elementAt(i).y,"freccia senza salto verticale al clic");}
 QPointF labelCenter;for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimension"&&i->data(3).toUuid()==dim.id)labelCenter=i->sceneBoundingRect().center()-i->pos();
 ok(e.moveDimension(dim.id,outside-labelCenter));radial(e,true);const auto saved=encoded(e);
 ok(e.moveDimension(dim.id,paper({5.5,0})-labelCenter));radial(e,false);ok(e.undo());require(encoded(e)==saved,"Undo ripristina posizione esterna");radial(e,true);
 DrawingSheet restored;ok(decodeDrawingSheet(saved,restored));DrawingSheetEditor reopened(restored,model);radial(reopened,true);near(reopened.dimensionResult(dim.id).value,1.5,"raggio associativo dopo riapertura");
 // Il filtro dei raggi non deve interferire con gli strumenti che riusano lo snap.
 ok(e.beginAxis(DrawingAxisKind::BetweenLines));click(e,paper({6.55,-1}));click(e,paper({7.55,-1}));require(items(e,"axisPreview")==1,"assi fra rette disponibili dopo la quota Raggio");click(e,{150,160});require(e.sheet().axes.size()==1,"asse fra rette inserito dopo un raggio");ok(e.undo());e.findChild<QAction*>("drawingSmartDimensionAction")->setChecked(false);
 const auto output=qEnvironmentVariable("FORGECAD_SECTION_RADIUS_OUTPUT");if(!output.isEmpty()){QDir().mkpath(output);DrawingPdfReport report;ok(e.exportPdf(output+"/raggi.pdf",report));require(e.grab().save(output+"/raggi.png"),"anteprima sezione quotata");}
 std::cout<<"PASS raggi sezione toroidale, bordi sovrapposti, freccia interna/esterna, preview, Undo e riapertura\n";
}
}
