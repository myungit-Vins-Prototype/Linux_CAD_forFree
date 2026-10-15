#include "cad_drawing_editor.h"
#include "fk_primitives.h"
#include "fk_sheet.h"
#include "fk_revolve.h"
#include <QAction>
#include <QApplication>
#include <QGraphicsItemGroup>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWindow>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
QByteArray encoded(const DrawingSheetEditor &editor){QByteArray bytes;ok(encodeDrawingSheet(editor.sheet(),bytes));return bytes;}
QGraphicsItem *preview(DrawingSheetEditor &editor){for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="viewPreview")return item;return nullptr;}
void mouse(DrawingSheetEditor &editor,QPointF p,bool click=false,Qt::MouseButton button=Qt::LeftButton){auto *v=editor.canvas();const QPointF local=v->mapFromScene(p),global=v->viewport()->mapToGlobal(local.toPoint());if(!click){QMouseEvent e(QEvent::MouseMove,local,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(v->viewport(),&e);return;}
 QMouseEvent press(QEvent::MouseButtonPress,local,global,button,button,Qt::NoModifier),release(QEvent::MouseButtonRelease,local,global,button,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(v->viewport(),&press);QApplication::sendEvent(v->viewport(),&release);}
void escape(DrawingSheetEditor &editor){editor.activateWindow();editor.canvas()->setFocus();QApplication::processEvents();QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(editor.windowHandle(),&key);}
class Counted final:public Curve<3>{CurvePtr<3> basis_;std::shared_ptr<int> count_;public:Counted(CurvePtr<3> basis,std::shared_ptr<int> count):basis_(std::move(basis)),count_(std::move(count)){}CurveType type()const override{return CurveType::Other;}Interval domain()const override{return basis_->domain();}void evaluate(double t,int order,Vec3 *out)const override{++*count_;basis_->evaluate(t,order,out);}};
}
int main(int argc,char **argv){QApplication app(argc,argv);try{
 auto count=std::make_shared<int>(0);auto body=std::make_shared<Body>(makeBox(Frame3(),10,6,4));for(auto edge:body->edges())body->edge(edge).curve=std::make_shared<Counted>(body->edge(edge).curve,count);auto names=std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"preview"));DrawingEditorModel model{{{QUuid::createUuid(),{}},{{1,body,names}}},{{1,"Corpo"}},{}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor editor(sheet,model);editor.resize(1200,800);editor.show();QApplication::processEvents();editor.fitSheet();const auto original=encoded(editor);
 DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.scale=3;DrawingViewLayout layout{view.id,"Vista",QPointF(100,100),{1}};
 ok(editor.beginViewPlacement(view,layout));require(editor.hasViewPlacement()&&preview(editor)&&encoded(editor)==original&&!editor.isModified()&&!editor.canUndo(),"anteprima estranea a documento e cronologia");
 auto *ghost=preview(editor);const auto paths=ghost->childItems();*count=0;mouse(editor,{90,70});const auto first=ghost->pos();mouse(editor,{180,125});require(ghost==preview(editor)&&QLineF(ghost->pos(),QPointF(180,125)).length()<1&&QLineF(first,ghost->pos()).length()>50,"anteprima segue il mouse");require(paths==ghost->childItems()&&*count==0,"movimento riusa i tracciati senza rivalutare le curve");
 {auto *canvas=editor.canvas();const QPointF local=canvas->mapFromScene(QPointF(180,125)),global=canvas->viewport()->mapToGlobal(local.toPoint());QWheelEvent wheel(local,global,QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(canvas->viewport(),&wheel);require(QLineF(ghost->pos(),canvas->mapToScene(local.toPoint())).length()<1e-7&&*count==0,"zoom mantiene l'anteprima sotto il mouse senza rivalutare le curve");}
 {auto invalid=layout;invalid.features={999};require(!editor.beginViewPlacement(view,invalid).isEmpty()&&preview(editor)==ghost&&encoded(editor)==original,"nuova anteprima non valida conserva quella precedente");}
 mouse(editor,{-3,-3},true);require(editor.hasViewPlacement()&&encoded(editor)==original,"clic fuori dal foglio non inserisce");escape(editor);require(!editor.hasViewPlacement()&&!preview(editor)&&encoded(editor)==original&&!editor.canUndo(),"Esc annulla senza modifiche");
 ok(editor.beginViewPlacement(view,layout));mouse(editor,{170,105},true);require(!editor.hasViewPlacement()&&!preview(editor)&&editor.sheet().drawing.views.size()==1&&editor.isModified(),"clic inserisce una sola vista");const auto placed=editor.sheet().views[0];require(QLineF(placed.position,QPointF(155,114)).length()<1,"centro della geometria ancorato al clic");const auto saved=encoded(editor);ok(editor.undo());require(encoded(editor)==original&&!editor.canUndo(),"un solo passo Undo");ok(editor.redo());require(encoded(editor)==saved,"Redo conserva posizione e ID");
 auto clean=std::make_shared<Body>(makeBox(Frame3(),10,6,4));model.snapshot.bodies={{1,clean,std::make_shared<ElementNames>(propagateNames(*clean,makeHistory({},*clean),{},"preview"))}};ok(editor.updateModel(model));
 auto section=view;section.id=QUuid::createUuid();section.kind=DrawingViewKind::Section;section.plane=Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));layout.viewId=section.id;layout.title="Sezione";ok(editor.beginViewPlacement(section,layout));mouse(editor,{100,150},true);require(editor.sheet().drawing.views.size()==2&&editor.sections(section.id).size()==4,"anteprima e inserimento anche per sezioni");
 auto empty=section;empty.id=QUuid::createUuid();empty.plane=Frame3(Vec3(0,0,40),Vec3(0,0,1),Vec3(1,0,0));layout.viewId=empty.id;const auto before=encoded(editor);require(!editor.beginViewPlacement(empty,layout).isEmpty()&&!editor.hasViewPlacement()&&encoded(editor)==before,"sezione vuota rifiutata atomicamente");
 view.id=QUuid::createUuid();layout.viewId=view.id;ok(editor.beginViewPlacement(view,layout));mouse(editor,{120,120},true,Qt::RightButton);require(!editor.hasViewPlacement()&&encoded(editor)==before,"tasto destro annulla");
 ok(editor.beginViewPlacement(view,layout));editor.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(true);require(!editor.hasViewPlacement()&&!preview(editor),"quotatura e inserimento sono strumenti alternativi");editor.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(false);
 ok(editor.beginViewPlacement(view,layout));editor.setReadOnly(true);require(!editor.hasViewPlacement()&&!preview(editor)&&!editor.beginViewPlacement(view,layout).isEmpty(),"sola lettura cancella e impedisce anteprima");editor.setReadOnly(false);
 ok(editor.beginViewPlacement(view,layout));ok(editor.updateModel(model));require(!editor.hasViewPlacement()&&!preview(editor),"aggiornamento sorgente cancella anteprima precedente");
 ok(editor.beginViewPlacement(view,layout));QEvent leave(QEvent::Leave);QApplication::sendEvent(editor.canvas(),&leave);require(!preview(editor)->isVisible(),"anteprima nascosta fuori dal canvas");mouse(editor,{150,100});require(preview(editor)->isVisible(),"anteprima riappare sul foglio");
 {auto sphere=std::make_shared<Body>(makeSphere(Frame3(),5));auto sphereNames=std::make_shared<ElementNames>(propagateNames(*sphere,makeHistory({},*sphere),{},"rounded"));auto roundedModel=model;roundedModel.snapshot.bodies={{1,sphere,sphereNames}};DrawingSheetEditor rounded(sheet,roundedModel);rounded.resize(900,700);rounded.show();QApplication::processEvents();DrawingView spherical;spherical.kind=DrawingViewKind::ProjectedEdges;DrawingViewLayout sphericalLayout{spherical.id,"Superficie",QPointF(100,100),{1}};ok(rounded.beginViewPlacement(spherical,sphericalLayout));mouse(rounded,{140,105},true);int contours=0;for(auto *item:rounded.canvas()->scene()->items())if(item->data(0).toString()=="silhouette"){++contours;require(!(item->flags()&QGraphicsItem::ItemIsSelectable),"contorni apparenti esclusi dalle quote");}require(contours>0,"i contorni sono presenti dopo l'inserimento della vista");}
 const auto image=qEnvironmentVariable("FORGECAD_VIEW_PLACEMENT_PREVIEW");if(!image.isEmpty())require(editor.grab().save(image),"anteprima inserimento");
 std::cout<<"PASS view/section placement, mouse preview, cancellation, cached geometry, history and read-only\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
