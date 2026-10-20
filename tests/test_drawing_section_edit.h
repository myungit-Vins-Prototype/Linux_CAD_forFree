#pragma once
#include <QMenu>
#include <QTimer>
namespace {
QGraphicsPathItem *cutLine(DrawingSheetEditor &e,QUuid id){
 for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="cuttingLine"&&item->data(3).toUuid()==id)
  if(auto *path=dynamic_cast<QGraphicsPathItem *>(item);path&&path->path().elementCount()==2)return path;
 return nullptr;
}
QGraphicsItem *cutGrip(DrawingSheetEditor &e,QUuid id,int end){for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="cuttingHandle"&&i->data(3).toUuid()==id&&i->data(2).toInt()==end)return i;return nullptr;}
void cutDragEvent(DrawingSheetEditor &e,QPointF p,int phase,Qt::KeyboardModifiers mods=Qt::AltModifier){
 auto *v=e.canvas();const QPointF local=v->mapFromScene(p),global=v->viewport()->mapToGlobal(local.toPoint());
 QMouseEvent event(phase==0?QEvent::MouseButtonPress:phase==1?QEvent::MouseMove:QEvent::MouseButtonRelease,local,global,phase==1?Qt::NoButton:Qt::LeftButton,phase==2?Qt::NoButton:Qt::LeftButton,mods);QApplication::sendEvent(v->viewport(),&event);
}
void sectionEditTests(){
 DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeBox(Frame3(),12,8,6))};model.choices={{1,"Blocco"}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1350,900);e.show();QApplication::processEvents();e.fitSheet();
 DrawingView parent;parent.kind=DrawingViewKind::ProjectedEdges;parent.scale=5;ok(e.insertView(parent,{parent.id,"Madre",{80,105},{1}}));
 const auto beforeSection=bytes(e.sheet());ok(e.beginSectionLine(parent.id,"A",false,{},DrawingSectionStyle::Full));mouse(e,{110,85},true);mouse(e,{130,85},false,Qt::AltModifier|Qt::ShiftModifier);
 for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="cuttingPreview"){auto *preview=dynamic_cast<QGraphicsPathItem *>(item);require(preview&&preview->path().elementCount()>2,"anteprima linea completa");close(preview->path().elementAt(0).x,76,"anteprima estesa dal centro verso sinistra");close(preview->path().elementAt(1).x,144,"anteprima estesa verso destra");}key(e,Qt::Key_Escape);require(bytes(e.sheet())==beforeSection,"anteprima estesa non modifica documento");
 ok(e.beginSectionPlacement(parent.id,{6,4},{10,4},"A",false,DrawingSectionStyle::Full));mouse(e,{220,130},true);const auto id=e.sheet().drawing.views.back().id;
 auto *line=cutLine(e,id);require(line,"linea completa presente");auto path=line->path();close(path.elementAt(0).x,76,"sezione dal centro estesa oltre bordo sinistro");close(path.elementAt(1).x,144,"sezione estesa oltre bordo destro");close(path.elementAt(0).y,85,"asse resta sul centro scelto");
 const auto original=bytes(e.sheet());mouse(e,{77,85},true);require(e.findChild<QAction *>("drawingEditSectionLineAction")->isEnabled(),"selezione asse abilita ridefinizione della figlia");
 require(cutGrip(e,id,0)&&cutGrip(e,id,1),"due punti di definizione editabili");
 bool menuChecked=false;QTimer::singleShot(0,&e,[&]{auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());require(menu&&!menu->actions().isEmpty()&&menu->actions()[0]->objectName()=="drawingEditSectionLineAction"&&menu->actions()[0]->isEnabled(),"menu contestuale asse offre ridisegno");menuChecked=true;menu->close();});QMetaObject::invokeMethod(e.canvas(),"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,e.canvas()->mapFromScene(QPointF(77,85))));require(menuChecked,"menu asse aperto");

 auto a=cutGrip(e,id,0)->pos();const auto quantized=[&](QPointF p){return e.canvas()->mapToScene(e.canvas()->mapFromScene(p));};const double movedY=4-(quantized(a+QPointF(0,5)).y()-quantized(a).y())/parent.scale;cutDragEvent(e,a,0);cutDragEvent(e,a+QPointF(0,5),1);require(bytes(e.sheet())==original&&items(e,"cuttingEditPreview")==1,"trascinamento punto solo anteprima");
 key(e,Qt::Key_Escape);require(bytes(e.sheet())==original&&items(e,"cuttingEditPreview")==0&&QLineF(cutGrip(e,id,0)->pos(),a).length()<1e-8,"Esc ripristina punto e cronologia");
 cutDragEvent(e,a,0);cutDragEvent(e,a+QPointF(0,5),1);cutDragEvent(e,a+QPointF(0,5),2);const auto changed=bytes(e.sheet());require(changed!=original,"rilascio modifica asse");close(layout(e,id).relation.cutStart.y(),movedY,"punto spostato nel piano modello");require(layout(e,id).relation.cutEnd==QPointF(10,4),"altro punto fermo");require(e.sheet().drawing.views.back().id==id,"identita sezione preservata");
 ok(e.undo());require(bytes(e.sheet())==original,"un Undo per trascinamento punto");ok(e.redo());require(bytes(e.sheet())==changed,"Redo esatto");ok(e.undo());
 mouse(e,{77,85},true);const double translatedY=4-(quantized({77,80}).y()-quantized({77,85}).y())/parent.scale;cutDragEvent(e,{77,85},0);cutDragEvent(e,{77,80},1);require(bytes(e.sheet())==original,"spostamento intero asse solo anteprima");cutDragEvent(e,{77,80},2);close(layout(e,id).relation.cutStart.y(),translatedY,"traslazione primo punto");close(layout(e,id).relation.cutEnd.y(),translatedY,"traslazione secondo punto");ok(e.undo());require(bytes(e.sheet())==original,"Undo traslazione atomico");
 mouse(e,{77,85},true);e.findChild<QAction *>("drawingEditSectionLineAction")->trigger();mouse(e,{110,85},true);mouse(e,{110,70},true,Qt::AltModifier|Qt::ShiftModifier);require(e.sheet().drawing.views.size()==2&&!e.hasViewPlacement(),"ridisegno sostituisce asse senza nuova vista");close(layout(e,id).relation.cutStart.x(),6,"ridisegno primo punto");close(layout(e,id).relation.cutEnd.x(),6,"ridisegno verticale");ok(e.undo());require(bytes(e.sheet())==original,"Undo ridisegno atomico");
 mouse(e,{77,85},true);const auto second=cutGrip(e,id,1)->pos();cutDragEvent(e,second,0,Qt::NoModifier);cutDragEvent(e,{140.1,85.1},1,Qt::ShiftModifier);cutDragEvent(e,{140.1,85.1},2,Qt::ShiftModifier);require(layout(e,id).relation.cutStart==QPointF(6,4)&&layout(e,id).relation.cutEnd==QPointF(12,4),"secondo punto snappa al punto medio con Maiusc senza spostare il primo");ok(e.undo());require(bytes(e.sheet())==original,"Undo snap secondo punto");
 mouse(e,{77,85},true);const auto first=cutGrip(e,id,0)->pos();const auto last=cutGrip(e,id,1)->pos();cutDragEvent(e,first,0);cutDragEvent(e,last,1);cutDragEvent(e,last,2);require(bytes(e.sheet())==original,"punti coincidenti rifiutati senza mutazioni");
 // La mezza sezione conserva il centro come origine del semiasse.
 ok(e.beginSectionPlacement(parent.id,{6,4},{10,4},"B",false,DrawingSectionStyle::HalfRight));mouse(e,{220,200},true);const auto half=e.sheet().drawing.views.back().id;line=cutLine(e,half);require(line,"linea mezza sezione");close(line->path().elementAt(0).x,110,"mezza sezione mantiene inizio al centro");ok(e.undo());
 // Estensione indipendente da scala e orientamento, anche dopo riapertura.
 ok(e.setSectionLine(id,{6,4},{8,6}));line=cutLine(e,id);const QPointF t(1/std::sqrt(2.),-1/std::sqrt(2.));const auto origin=QPointF(110,85);const auto lo=QPointF(line->path().elementAt(0).x,line->path().elementAt(0).y),hi=QPointF(line->path().elementAt(1).x,line->path().elementAt(1).y);
 for(auto corner:{QPointF(80,65),QPointF(140,65),QPointF(80,105),QPointF(140,105)}){require(QPointF::dotProduct(corner-lo,t)>=4-1e-6&&QPointF::dotProduct(hi-corner,t)>=4-1e-6,"asse obliquo comprende ingombro completo");}
 DrawingSheet decoded;ok(decodeDrawingSheet(bytes(e.sheet()),decoded));DrawingSheetEditor reopened(decoded,model);require(cutLine(reopened,id)&&cutLine(reopened,id)->path()==line->path(),"riapertura mantiene asse senza cambiare formato");
 auto scaled=parent;scaled.scale=3;ok(e.updateView(scaled,layout(e,parent.id)));line=cutLine(e,id);require(line,"asse dopo cambio scala");const auto start=QPointF(line->path().elementAt(0).x,line->path().elementAt(0).y),end=QPointF(line->path().elementAt(1).x,line->path().elementAt(1).y);
 for(auto corner:{QPointF(80,81),QPointF(116,81),QPointF(80,105),QPointF(116,105)})require(QPointF::dotProduct(corner-start,t)>=4-1e-6&&QPointF::dotProduct(end-corner,t)>=4-1e-6,"estensione segue scala della madre con margine carta costante");
 e.setReadOnly(true);require(!cutGrip(e,id,0)&&!e.findChild<QAction *>("drawingEditSectionLineAction")->isEnabled(),"sola lettura nasconde punti e disabilita ridisegno");const auto locked=bytes(e.sheet());cutDragEvent(e,origin,0);cutDragEvent(e,origin+QPointF(4,4),1);cutDragEvent(e,origin+QPointF(4,4),2);require(bytes(e.sheet())==locked,"sola lettura impedisce drag");
 const auto output=qEnvironmentVariable("FORGECAD_SECTION_EDIT_OUTPUT");if(!output.isEmpty()){e.setReadOnly(false);mouse(e,lo+QPointF(1,-1),true);QDir().mkpath(output);require(e.grab().save(output+"/section-axis.png"),"QA asse editabile");DrawingPdfReport report;ok(e.exportPdf(output+"/section-axis.pdf",report));}
 std::cout<<"PASS centered full cut, endpoints/translation/redraw, preview, Esc, history, half, oblique, persistence, read-only\n";
}
}
