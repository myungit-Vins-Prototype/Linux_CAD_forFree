#pragma once
#include "cad_drawing_dimension.h"
namespace {
void drawingFlaconeInteractionTests(){
 DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeCylinder(Frame3(),12,40),1)};model.choices={{1,"Cilindro"}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1450,900);e.show();QApplication::processEvents();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=3;v.plane=Frame3({},Vec3(0,-1,0),Vec3(1,0,0));ok(e.insertView(v,{v.id,"Vista",{75,160},{1}}));e.fitSheet();QApplication::processEvents();
 const auto paperAt=[](Vec2 p){return QPointF(75+3*p.x(),160-3*p.y());};
 int collapsed=0;for(const auto &c:e.curves(v.id)){const auto primitive=drawingPrimitive(v,c);if(primitive.linearProjection){++collapsed;near(distance(primitive.lineFirst,primitive.lineSecond),24,"cerchio visto di taglio e' un segmento esatto");}}
 require(collapsed==2,"entrambi gli anelli riconosciuti come segmenti");
 mode(e,int(DrawingMeasure::VerticalDistance));click(e,paperAt({3,0}),Qt::ShiftModifier);click(e,paperAt({3,40}),Qt::ShiftModifier);move(e,{130,95});require(items(e,"dimensionPreview")==1,"altezza tra anelli proiettati disponibile in anteprima");click(e,{130,95});const auto d=last(e);near(e.dimensionResult(d.id).value,40,"altezza totale del cilindro quotabile dai bordi proiettati");
 e.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(false);QApplication::processEvents();const auto before=encoded(e);QGraphicsItem *label=nullptr;for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimension"&&i->data(3).toUuid()==d.id)label=i;require(label,"testo quota");
 const QPointF start=e.canvas()->mapFromScene(label->sceneBoundingRect().center()),end=start+QPointF(35,22),global=e.canvas()->viewport()->mapToGlobal(start.toPoint());
 const auto geometry=[&]{QRectF box;for(auto *i:e.canvas()->scene()->items())if(i->data(5).toString()=="dimensionGeometry"&&i->data(3).toUuid()==d.id)box=box.united(i->sceneBoundingRect());return box;};const auto oldGeometry=geometry();
 QMouseEvent press(QEvent::MouseButtonPress,start,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(e.canvas()->viewport(),&press);
 QMouseEvent drag(QEvent::MouseMove,end,e.canvas()->viewport()->mapToGlobal(end.toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(e.canvas()->viewport(),&drag);QApplication::processEvents();require(geometry()!=oldGeometry,"frecce e linee cambiano PRIMA di rilasciare il mouse");require(encoded(e)==before,"anteprima non modifica lo storico");
 QMouseEvent release(QEvent::MouseButtonRelease,end,e.canvas()->viewport()->mapToGlobal(end.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(e.canvas()->viewport(),&release);QApplication::processEvents();require(encoded(e)!=before,"rilascio salva la posizione");ok(e.undo());require(encoded(e)==before,"un solo Undo ripristina il trascinamento");
 {
 DrawingEditorModel block;block.snapshot.version={QUuid::createUuid(),{}};block.snapshot.bodies={source(makeBox(Frame3(),10,6,4),1)};block.choices={{1,"Blocco"}};DrawingSheet page;page.drawing.model=block.snapshot.version;DrawingSheetEditor box(page,block);box.resize(1450,900);box.show();QApplication::processEvents();DrawingView front;front.kind=DrawingViewKind::ProjectedEdges;front.scale=3;ok(box.insertView(front,{front.id,"Vista",{75,120},{1}}));box.fitSheet();QApplication::processEvents();
 mode(box,int(DrawingMeasure::Distance));click(box,paper({0,3}),Qt::ShiftModifier);click(box,paper({10,3}),Qt::ShiftModifier);click(box,{90,75});const auto width=last(box);near(box.dimensionResult(width.id).value,10,"distanza tra bordi verticali");int witnesses=0;
 for(auto *item:box.canvas()->scene()->items())if(item->data(5).toString()=="dimensionGeometry"&&item->data(3).toUuid()==width.id)if(auto *line=qgraphicsitem_cast<QGraphicsLineItem *>(item)){const auto a=line->line().p1(),b=line->line().p2();if(std::abs(a.x()-b.x())<1e-7&&std::abs(a.y()-b.y())>5){require(a.y()<=102&&a.y()>100,"richiamo parte dal bordo superiore vicino alla quota, non dal fondo del segmento");++witnesses;}}
 require(witnesses==2,"entrambi i richiami verificati");
 }
 std::cout<<"PASS segmenti da cerchi visti di taglio, altezza totale GUI, anteprima completa durante drag e Undo\n";
}
}
