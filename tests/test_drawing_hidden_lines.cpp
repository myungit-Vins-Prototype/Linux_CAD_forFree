#include "cad_drawing_editor.h"
#include "fk_primitives.h"
#include <QApplication>
#include <QAction>
#include <QGraphicsPathItem>
#include <QGraphicsItemGroup>
#include <QGraphicsView>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QMouseEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool value,const char *why){if(!value)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
class Counted final:public Curve<3>{CurvePtr<3> basis_;std::shared_ptr<int> count_;public:Counted(CurvePtr<3> basis,std::shared_ptr<int> count):basis_(std::move(basis)),count_(std::move(count)){}CurveType type()const override{return CurveType::Other;}Interval domain()const override{return basis_->domain();}void evaluate(double t,int order,Vec3 *out)const override{++*count_;basis_->evaluate(t,order,out);}};
DrawingSourceBody source(quint64 id,Body body,std::shared_ptr<int> count){auto p=std::make_shared<Body>(std::move(body));for(auto edge:p->edges())p->edge(edge).curve=std::make_shared<Counted>(p->edge(edge).curve,count);return {id,p,std::make_shared<ElementNames>(propagateNames(*p,makeHistory({},*p),{},"hidden"))};}
QByteArray encoded(const DrawingSheetEditor &e){QByteArray b;ok(encodeDrawingSheet(e.sheet(),b));return b;}
void select(DrawingSheetEditor &editor,const QUuid &id){auto *tree=editor.findChild<QTreeWidget *>("drawingTree");for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+1).toUuid()==id){tree->setCurrentItem(*it);return;}throw std::runtime_error("vista da selezionare");}
int hidden(DrawingSheetEditor &editor,const QUuid &id){int count=0;for(auto *item:editor.canvas()->scene()->items())if(item->data(1).toUuid()==id&&item->data(4).toBool())++count;return count;}
void mouse(DrawingSheetEditor &editor,QPointF paper,bool click=false){auto *canvas=editor.canvas();const QPointF p=canvas->mapFromScene(paper),global=canvas->viewport()->mapToGlobal(p.toPoint());QMouseEvent event(click?QEvent::MouseButtonPress:QEvent::MouseMove,p,global,click?Qt::LeftButton:Qt::NoButton,click?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&event);if(click){QMouseEvent release(QEvent::MouseButtonRelease,p,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&release);}}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{
 auto count=std::make_shared<int>(0);DrawingEditorModel model{{{QUuid::createUuid(),{}},{source(1,makeBox(Frame3(),2,2,1),count),source(2,makeBox(Frame3(Vec3(-1,-1,3),Vec3(0,0,1),Vec3(1,0,0)),4,4,1),count)}},{{1,"Posteriore"},{2,"Anteriore"}}, {}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor editor(sheet,model);editor.resize(1200,820);editor.show();QApplication::processEvents();DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.scale=8;
 ok(editor.insertView(view,{view.id,"Vista",QPointF(90,100),{1,2}}));auto other=view;other.id=QUuid::createUuid();ok(editor.insertView(other,{other.id,"Altra vista",QPointF(190,100),{1,2}}));QApplication::processEvents();editor.fitSheet();auto *toggle=editor.findChild<QAction *>("drawingHiddenLinesAction");require(toggle&&!toggle->isEnabled()&&!toggle->isChecked(),"comando disabilitato senza selezione");
 require(!editor.sheet().views[0].showHiddenLines&&hidden(editor,view.id)==0,"linee nascoste escluse per impostazione predefinita");int edgeIndex=-1;
 for(std::size_t i=0;i<editor.curves(view.id).size();++i)if(editor.curves(view.id)[i].featureId==1){edgeIndex=int(i);break;}
 require(edgeIndex>=0,"bordo del corpo coperto");
 for(auto *item:editor.canvas()->scene()->items())require(!(item->data(1).toUuid()==view.id&&item->data(0).toString()=="projection"&&item->data(2).toInt()==edgeIndex),"bordo coperto assente dalla selezione");
 // Gli snap del corpo completamente coperto sono assenti anche se geometricamente vicini.
 auto *quote=editor.findChild<QAction *>("drawingSmartDimensionAction");quote->setChecked(true);mouse(editor,{90,100});for(auto *item:editor.canvas()->scene()->items())require(item->data(0).toString()!="dimensionSnap","nessuno snap su bordo coperto");quote->setChecked(false);
 select(editor,view.id);require(toggle->isEnabled()&&!toggle->isChecked(),"stato comando sincronizzato con la vista");editor.markSaved();const auto before=encoded(editor);const auto *exact=editor.curves(view.id)[edgeIndex].curve.get();*count=0;toggle->trigger();QApplication::processEvents();require(*count==0,"il pulsante riusa i tracciati e non rivaluta il modello");
 require(toggle->isChecked()&&editor.sheet().views[0].showHiddenLines&&!editor.sheet().views[1].showHiddenLines&&hidden(editor,view.id)>0&&hidden(editor,other.id)==0,"toggle solo della vista selezionata");require(editor.isModified()&&editor.curves(view.id)[edgeIndex].curve.get()==exact,"presentazione modificata, riferimenti esatti invariati");
 ok(editor.undo());require(*count==0,"Undo del toggle riusa le curve e la visibilita'");require(encoded(editor)==before&&!editor.isModified()&&hidden(editor,view.id)==0&&!toggle->isChecked(),"un solo Undo ripristina visibilita' e checkpoint");ok(editor.redo());require(*count==0&&hidden(editor,view.id)>0&&toggle->isChecked(),"Redo riusa tracciati e ripristina linee tratteggiate");
 quote->setChecked(true);mouse(editor,{90,100});bool snap=false;for(auto *item:editor.canvas()->scene()->items())snap|=item->data(0).toString()=="dimensionSnap";require(snap,"snap riattivati sui bordi nascosti mostrati");quote->setChecked(false);
 QGraphicsItem *chosen=nullptr;for(auto *item:editor.canvas()->scene()->items())if(item->data(1).toUuid()==view.id&&item->data(4).toBool()&&item->data(2).toInt()==edgeIndex&&item->data(0).toString()=="projection"){chosen=item;break;}require(chosen&&bool(chosen->flags()&QGraphicsItem::ItemIsSelectable),"bordo nascosto mostrato e selezionabile");editor.canvas()->scene()->clearSelection();chosen->setSelected(true);ok(editor.addSelectedDimension(DrawingMeasure::EndpointDistance));const auto dimension=editor.sheet().drawing.dimensions.back().id;require(editor.dimensionResult(dimension).status==DrawingDimensionStatus::Ready&&std::abs(editor.dimensionResult(dimension).value-2)<1e-7,"quota associativa esatta su bordo nascosto");
 ok(editor.setHiddenLinesVisible(view.id,false));QApplication::processEvents();require(hidden(editor,view.id)==0&&editor.dimensionResult(dimension).status==DrawingDimensionStatus::Ready,"nascondere i tratti conserva le quote gia' create");
 auto bytes=encoded(editor);DrawingSheet loaded;ok(decodeDrawingSheet(bytes,loaded));require(!loaded.views[0].showHiddenLines,"default false riletto");ok(editor.setHiddenLinesVisible(other.id,true));QApplication::processEvents();bytes=encoded(editor);ok(decodeDrawingSheet(bytes,loaded));require(!loaded.views[0].showHiddenLines&&loaded.views[1].showHiddenLines,"persistenza indipendente fra viste");
 auto object=QJsonDocument::fromJson(bytes).object();auto views=object["views"].toArray();auto invalid=views[0].toObject();invalid["showHiddenLines"]="yes";views[0]=invalid;object["views"]=views;const auto preserved=loaded.views[1].showHiddenLines;require(!decodeDrawingSheet(QJsonDocument(object).toJson(),loaded).isEmpty()&&loaded.views[1].showHiddenLines==preserved,"metadati invalidi rifiutati atomicamente");
 DrawingSheetEditor reopened(editor.sheet(),model);require(reopened.sheet().views[1].showHiddenLines,"riapertura conserva impostazione");editor.setReadOnly(true);select(editor,view.id);require(!toggle->isEnabled()&&!editor.setHiddenLinesVisible(view.id,true).isEmpty(),"sola lettura rispettata");editor.setReadOnly(false);
 // La ricomposizione visibile+nascosto conserva i tratti aperti nell'anteprima.
 {auto partialModel=model;partialModel.snapshot.bodies={source(1,makeCylinder(Frame3(),5,1),count),source(2,makeBox(Frame3(Vec3(-1,-6,3),Vec3(0,0,1),Vec3(1,0,0)),2,12,1),count)};DrawingSheetEditor partial(sheet,partialModel);DrawingView split;split.kind=DrawingViewKind::ProjectedEdges;split.scale=3;DrawingViewLayout layout{split.id,"Parziale",QPointF(100,100),{1,2},true};
  std::vector<DrawingCurve> pieces;ok(projectDrawingEdges(partialModel.snapshot,split,pieces));int index=-1;for(std::size_t i=0;i<pieces.size();++i)if(pieces[i].featureId==1){const auto &edge=partialModel.snapshot.bodies[0].body->edge(pieces[i].sourceEdge);const auto point=edge.curve->point((edge.range.lo+edge.range.hi)/2);if(std::abs(point.z()-1)<1e-8)index=int(i);}require(index>=0,"bordo parzialmente coperto da verificare");ok(partial.beginViewPlacement(split,layout));QGraphicsItemGroup *group=nullptr;for(auto *item:partial.canvas()->scene()->items())if(item->data(0).toString()=="viewPreview")group=qgraphicsitem_cast<QGraphicsItemGroup *>(item);require(group,"anteprima parziale");auto *path=qgraphicsitem_cast<QGraphicsPathItem *>(group->childItems().at(index));require(path&&!path->path().isEmpty()&&std::abs(path->path().length()-30*kPi)<.3,"tratti aperti visibili e nascosti concatenati senza unione di aree");}
 select(editor,other.id);require(toggle->isChecked(),"selezione altra vista aggiorna il pulsante");const auto preview=qEnvironmentVariable("FORGECAD_HIDDEN_LINES_PREVIEW");if(!preview.isEmpty())require(editor.grab().save(preview),"anteprima linee nascoste");
 std::cout<<"PASS default hidden lines, selected-view toggle, exact dimensions, snap exclusion, cached rendering, undo and persistence\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
