#pragma once
#include "../cad_drawing_file.h"
#include "fk_classify.h"
#include <QGraphicsPathItem>
#include <QMouseEvent>
namespace {
void drawingSectionModelTest(const QString &path){
 using namespace ForgeCad;using namespace ForgeCad::Kernel;
 DocumentState document;clientOk(loadDocumentFile(path,document));DrawingEditorModel model;clientOk(prepareDrawingEditorModel(document,model));require(!model.snapshot.bodies.empty(),"modello reale con corpi");
 Box bounds;for(const auto &b:model.snapshot.bodies)for(auto face:b.body->faces())bounds.add(faceBox(*b.body,face));const auto center=.5*(bounds.lo+bounds.hi);const double height=bounds.hi.z()-bounds.lo.z();
 DrawingSheet sheet;sheet.title="Orientamento sezione meccanico";sheet.size={420,297};sheet.titleBlock.visible=true;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1450,950);e.show();QApplication::processEvents();e.fitSheet();
 DrawingView p;p.kind=DrawingViewKind::ProjectedEdges;p.scale=5;p.plane=Frame3(center,Vec3(1,0,0),Vec3(0,1,0));DrawingViewLayout layout{p.id,"Vista madre",{85,115},{}};for(const auto &b:model.snapshot.bodies)layout.features.append(b.featureId);clientOk(e.insertView(p,layout));clientOk(e.setHiddenLinesVisible(p.id,true));
 clientOk(e.beginSectionPlacement(p.id,{0,height/2+3},{0,-height/2-3},"A",false,DrawingSectionStyle::Full));auto *canvas=e.canvas();const QPointF local=canvas->mapFromScene(QPointF(250,115)),global=canvas->viewport()->mapToGlobal(local.toPoint());QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&press);QApplication::sendEvent(canvas->viewport(),&release);QApplication::processEvents();require(e.sheet().drawing.views.size()==2&&!e.hasViewPlacement(),"sezione reale inserita da anteprima");
 const auto section=e.sheet().drawing.views[1];require(distance(section.plane.yDir(),p.plane.yDir())<1e-10,"sezione reale verticale coerente con madre");require(!e.sections(section.id).empty(),"sezione reale interseca il corpo");
 DrawingAnnotation note;note.text="Taglio verticale: alto della vista madre conservato.";note.position={25,25};note.width=100;clientOk(e.addAnnotation(note));
 DrawingAnnotation leader;leader.text="Richiamo associato alla sezione";leader.position={295,80};leader.width=70;leader.viewId=section.id;const auto &curve=e.sections(section.id).front();leader.reference=drawingAttachment(section,curve,model.snapshot,DrawingPointRole::Midpoint);clientOk(e.addAnnotation(leader));require(e.annotationResult(leader.id).status==DrawingDimensionStatus::Ready,"richiamo sulla sezione del modello reale");
 const auto output=qEnvironmentVariable("FORGECAD_SECTION_MODEL_OUTPUT");if(!output.isEmpty()){require(QDir().mkpath(output),"cartella QA sezione reale");DrawingPdfReport report;clientOk(e.exportPdf(output+"/meccanico-corretto.pdf",report));require(e.grab().save(output+"/meccanico-editor.png"),"anteprima reale");
  auto legacy=e.sheet();legacy.annotations.clear();auto &old=legacy.drawing.views[1];old.plane=Frame3(old.plane.origin(),old.plane.zDir(),-p.plane.yDir());DrawingSheetEditor before(legacy,model);clientOk(before.exportPdf(output+"/meccanico-prima.pdf",report));
 }
 std::cout<<"PASS real section orientation: modelSize="<<(bounds.hi.x()-bounds.lo.x())<<','<<(bounds.hi.y()-bounds.lo.y())<<','<<height<<" sectionCurves="<<e.sections(section.id).size()<<" source="<<path.toStdString()<<'\n';
}
}
