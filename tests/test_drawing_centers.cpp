#include "cad_drawing_editor.h"
#include "cad_drawing_centers.h"
#include "fk_primitives.h"
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void need(bool b,const char *why){if(!b)throw std::runtime_error(why);}
void ok(const QString &s){if(!s.isEmpty())throw std::runtime_error(s.toStdString());QApplication::processEvents();}
void near(double a,double b,const char *why){need(std::abs(a-b)<1e-6,why);}
class Counted final:public Curve<3>{CurvePtr<3> basis_;std::shared_ptr<int> count_;public:
 Counted(CurvePtr<3> basis,std::shared_ptr<int> count):basis_(std::move(basis)),count_(std::move(count)){}
 CurveType type()const override{return CurveType::Other;}Interval domain()const override{return basis_->domain();}
 void evaluate(double t,int order,Vec3 *out)const override{++*count_;basis_->evaluate(t,order,out);}
};
std::shared_ptr<int> evaluations=std::make_shared<int>(0);
DrawingSourceBody source(quint64 id,Body body){auto p=std::make_shared<Body>(std::move(body));for(auto edge:p->edges())if(p->edge(edge).curve->type()==CurveType::Line)p->edge(edge).curve=std::make_shared<Counted>(p->edge(edge).curve,evaluations);return {id,p,std::make_shared<ElementNames>(propagateNames(*p,makeHistory({},*p),{},"center"+std::to_string(id)))};}
QByteArray bytes(const DrawingSheetEditor &editor){QByteArray b;ok(encodeDrawingSheet(editor.sheet(),b));return b;}
void select(DrawingSheetEditor &e,QUuid id){auto *tree=e.findChild<QTreeWidget *>("drawingTree");for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+1).toUuid()==id){tree->setCurrentItem(*it);return;}throw std::runtime_error("selezione vista");}
QGraphicsPathItem *path(DrawingSheetEditor &e,QUuid id,const char *kind){for(auto *i:e.canvas()->scene()->items())if(i->data(1).toUuid()==id&&i->data(0).toString()==kind)return dynamic_cast<QGraphicsPathItem *>(i);return nullptr;}
void mouse(DrawingSheetEditor &e,QPointF p,bool click=false){auto *canvas=e.canvas();const QPointF point=canvas->mapFromScene(p),global=canvas->viewport()->mapToGlobal(point.toPoint());QMouseEvent event(click?QEvent::MouseButtonPress:QEvent::MouseMove,point,global,click?Qt::LeftButton:Qt::NoButton,click?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&event);if(click){QMouseEvent release(QEvent::MouseButtonRelease,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&release);}QApplication::processEvents();}
DrawingAttachment center(const DrawingView &v,const DrawingEditorModel &model,const std::vector<DrawingCurve> &curves,quint64 feature){for(const auto &c:curves)if(c.featureId==feature&&drawingPrimitive(v,c).kind==DrawingPrimitive::Kind::Conic){auto p=c.sourceGeometry->point(c.sourceRange.lo);if(p.z()>5)return drawingAttachment(v,c,model.snapshot,DrawingPointRole::Center);}throw std::runtime_error("centro del bordo superiore");}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{
 // Proiezione ellittica obliqua: gli assi sono gli autovettori della conica,
 // non gli assi parametrici del cerchio 3D, che possono risultare obliqui fra loro.
 DrawingView oblique;oblique.kind=DrawingViewKind::ProjectedEdges;oblique.scale=2;oblique.plane=Frame3({},normalized(Vec3(1,2,3)),normalized(Vec3(2,-1,0)));
 auto circle=std::make_shared<Circle<3>>(Vec3(3,4,2),Vec3(1,0,0),Vec3(0,1,0),5);
 DrawingCurve curve;curve.viewId=oblique.id;curve.sourceGeometry=circle;curve.curve=circle;curve.range={0,kTwoPi};
 const auto geometry=drawingCenterGeometry(oblique,{curve},{true});need(geometry.axes.elementCount()==4&&geometry.crosses.elementCount()==4,"due assi principali della conica");
 QPointF u(geometry.axes.elementAt(1).x-geometry.axes.elementAt(0).x,geometry.axes.elementAt(1).y-geometry.axes.elementAt(0).y),v(geometry.axes.elementAt(3).x-geometry.axes.elementAt(2).x,geometry.axes.elementAt(3).y-geometry.axes.elementAt(2).y);
 near(QPointF::dotProduct(u,v),0,"assi della proiezione ellittica ortogonali");const auto p=drawingPrimitive(oblique,curve);const auto c=oblique.plane.toLocal(circle->center());near((geometry.axes.elementAt(0).x+geometry.axes.elementAt(1).x)/2,c.x()*2,"centro proiettato esatto");
 // Ciascuna direzione deve essere autovettore della matrice della conica.
 const double xx=p.firstAxis.x()*p.firstAxis.x()+p.secondAxis.x()*p.secondAxis.x(),yy=p.firstAxis.y()*p.firstAxis.y()+p.secondAxis.y()*p.secondAxis.y(),xy=p.firstAxis.x()*p.firstAxis.y()+p.secondAxis.x()*p.secondAxis.y();
 Vec2 d(u.x(),-u.y());Vec2 transformed(xx*d.x()+xy*d.y(),xy*d.x()+yy*d.y());near(d.x()*transformed.y()-d.y()*transformed.x(),0,"direzione principale esatta");
 auto planar=oblique;planar.plane=Frame3();planar.scale=3;curve.viewId=planar.id;const auto concentric=drawingCenterGeometry(planar,{curve,curve},{true,true});need(concentric.axes.elementCount()==4,"bordi concentrici senza assi duplicati");near(concentric.axes.boundingRect().width(),34,"diametro 10 a scala 3 piu due prolungamenti da 2 mm");near(concentric.crosses.boundingRect().width(),1.6,"croce in mm carta indipendente dalla scala");
 auto edgeOn=planar;edgeOn.plane=Frame3({},Vec3(0,1,0),Vec3(1,0,0));need(drawingCenterGeometry(edgeOn,{curve},{true}).axes.isEmpty(),"conica di taglio degenere esclusa");
 auto arc=curve;arc.range={0,kPi/2};need(drawingCenterGeometry(planar,{arc},{true}).axes.elementCount()==4,"centro ed assi dell'arco analitico");
 DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(1,makeCylinder(Frame3(),4,6)),source(2,makeCylinder(Frame3(Vec3(20,0,0),Vec3(0,0,1),Vec3(1,0,0)),2,6)),source(3,makeBox(Frame3(Vec3(-5,-5,10),Vec3(0,0,1),Vec3(1,0,0)),10,10,2))};model.choices={{1,"Cilindro"},{2,"Secondo cilindro"},{3,"Copertura"}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;sheet.title="Assi e centri associati alle viste";sheet.size={420,297};sheet.titleBlock.visible=true;
 DrawingSheetEditor editor(sheet,model);editor.resize(1400,900);editor.show();DrawingView top;top.kind=DrawingViewKind::ProjectedEdges;top.scale=3;ok(editor.insertView(top,{top.id,"Cilindri",{100,95},{1,2}}));QApplication::processEvents();
 auto *marks=editor.findChild<QAction *>("drawingCenterMarksAction"),*axes=editor.findChild<QAction *>("drawingCenterLinesAction");need(marks&&axes&&!marks->isEnabled()&&!axes->isEnabled(),"comandi disponibili e disabilitati senza selezione");select(editor,top.id);need(marks->isEnabled()&&!marks->isChecked(),"comandi sincronizzati con la vista");editor.markSaved();const auto before=bytes(editor);const auto *exact=editor.curves(top.id)[0].curve.get();
 *evaluations=0;marks->trigger();QApplication::processEvents();need(marks->isChecked()&&path(editor,top.id,"centerMarks")&&!path(editor,top.id,"centerLines"),"croci indipendenti dagli assi");need(editor.isModified(),"metadati segnano tavola modificata");ok(editor.undo());need(bytes(editor)==before&&!editor.isModified()&&!path(editor,top.id,"centerMarks"),"Undo ripristina checkpoint e presentazione");ok(editor.redo());need(path(editor,top.id,"centerMarks"),"Redo ripristina centri");
 axes->trigger();QApplication::processEvents();auto *axisItem=path(editor,top.id,"centerLines");need(axisItem&&axisItem->pen().style()==Qt::CustomDashLine&&!(axisItem->flags()&QGraphicsItem::ItemIsSelectable)&&axisItem->acceptedMouseButtons()==Qt::NoButton,"assi a tratto-punto senza falsa selezione geometrica");need(editor.curves(top.id)[0].curve.get()==exact,"curve e agganci non ricostruiti dai comandi grafici");
 need(*evaluations==0,"toggle e Undo/Redo riusano cache senza valutare geometria sorgente");
 DrawingSheet restored;ok(decodeDrawingSheet(bytes(editor),restored));need(restored.views[0].showCenterMarks&&restored.views[0].showCenterLines,"opzioni persistite");const auto schema=QJsonDocument::fromJson(before).object()["schema"].toInt();need(QJsonDocument::fromJson(bytes(editor)).object()["schema"].toInt()==schema,"schema obbligatorio invariato");
 auto json=QJsonDocument::fromJson(bytes(editor)).object();auto layouts=json["views"].toArray();auto first=layouts[0].toObject();first.remove("showCenterMarks");first.remove("showCenterLines");layouts[0]=first;json["views"]=layouts;ok(decodeDrawingSheet(QJsonDocument(json).toJson(),restored));need(!restored.views[0].showCenterMarks&&!restored.views[0].showCenterLines,"vecchie tavole senza nuovi campi leggibili");first["showCenterMarks"]="true";layouts[0]=first;json["views"]=layouts;need(!decodeDrawingSheet(QJsonDocument(json).toJson(),restored).isEmpty()&&!restored.views[0].showCenterMarks,"metadati invalidi rifiutati senza alterare output");
 auto dim=drawingDimensionBetween(top,model.snapshot,editor.curves(top.id),{center(top,model,editor.curves(top.id),1),center(top,model,editor.curves(top.id),2)},DrawingMeasure::Distance,{10,-12});ok(editor.addDimension(dim,{125,70}));near(editor.dimensionResult(dim.id).value,20,"quota centro-centro esatta");ok(editor.setCenterMarksVisible(top.id,false));ok(editor.setCenterLinesVisible(top.id,false));near(editor.dimensionResult(dim.id).value,20,"nascondere riferimenti grafici conserva quote");ok(editor.setCenterLinesVisible(top.id,true));
 auto *item=path(editor,top.id,"centerLines");auto oldPosition=item->sceneBoundingRect().center();ok(editor.moveView(top.id,{110,105}));near(path(editor,top.id,"centerLines")->sceneBoundingRect().center().x()-oldPosition.x(),10,"assi seguono spostamento vista");
 auto scaleView=editor.sheet().drawing.views[0];scaleView.scale=2;ok(editor.updateView(scaleView,editor.sheet().views[0]));near(editor.dimensionResult(dim.id).value,20,"scala non cambia la quota");near(path(editor,top.id,"centerLines")->path().boundingRect().width(),56,"assi seguono scala con prolungamento costante");
 // Quotatura reale con mouse sui centri: i riferimenti appartengono alle
 // coniche del modello, senza riferimenti ai tratti grafici appena aggiunti.
 auto *smart=editor.findChild<QAction *>("drawingSmartDimensionAction");smart->setChecked(true);mouse(editor,{110,105});
 bool snap=false;for(auto *i:editor.canvas()->scene()->items())if(i->data(0).toString()=="dimensionSnap")snap=QLineF(i->pos(),{110,105}).length()<.01;need(snap,"snap reale sul centro mostrato");
 const auto n=editor.sheet().drawing.dimensions.size();mouse(editor,{110,105},true);mouse(editor,{150,105},true);mouse(editor,{130,75},true);
 need(editor.sheet().drawing.dimensions.size()==n+1,"quota centro-centro inserita con mouse");const auto &picked=editor.sheet().drawing.dimensions.back();need(picked.references.size()==2&&picked.references[0].role==DrawingPointRole::Center&&picked.references[1].role==DrawingPointRole::Center,"snap conserva due centri geometrici");near(editor.dimensionResult(picked.id).value,20,"misura della quota da snap");smart->setChecked(false);
 DrawingView covered=top;covered.id=QUuid::createUuid();ok(editor.insertView(covered,{covered.id,"Cilindro coperto",{260,95},{1,3}}));ok(editor.setCenterMarksVisible(covered.id,true));ok(editor.setCenterLinesVisible(covered.id,true));need(!path(editor,covered.id,"centerMarks")&&!path(editor,covered.id,"centerLines"),"bordi completamente nascosti non mostrano centri");ok(editor.setHiddenLinesVisible(covered.id,true));need(path(editor,covered.id,"centerLines"),"Mostra linee nascoste abilita centri coperti");ok(editor.setHiddenLinesVisible(covered.id,false));need(!path(editor,covered.id,"centerLines"),"nascondi conserva opzione ma rimuove assi coperti");
 DrawingView cut;cut.plane=Frame3(Vec3(0,0,3),Vec3(0,0,1),Vec3(1,0,0));cut.scale=3;ok(editor.insertView(cut,{cut.id,"Sezione superficie cilindrica",{100,190},{1}}));ok(editor.setCenterLinesVisible(cut.id,true));need(path(editor,cut.id,"centerLines"),"assi presenti anche nelle sezioni rimosse");
 auto changed=model;changed.snapshot.bodies[1]=source(2,makeCylinder(Frame3(Vec3(24,0,0),Vec3(0,0,1),Vec3(1,0,0)),2,6));ok(editor.updateModel(changed));near(editor.dimensionResult(dim.id).value,24,"quota segue aggiornamento modello");near(path(editor,top.id,"centerLines")->path().boundingRect().width(),64,"riferimenti grafici seguono modello aggiornato");

 // Viste derivate: la trasformazione della madre aggiorna anche gli assi figli.
 ok(editor.beginDerivedViewPlacement(top.id,DrawingProjectionSide::Right,DrawingProjectionMethod::ThirdAngle));
 {auto *canvas=editor.canvas();const QPointF point=canvas->mapFromScene(QPointF(330,160)),global=canvas->viewport()->mapToGlobal(point.toPoint());QMouseEvent event(QEvent::MouseButtonPress,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&event);QMouseEvent release(QEvent::MouseButtonRelease,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&release);}
 need(!editor.hasViewPlacement()&&editor.sheet().drawing.views.size()==4,"inserimento vista figlia da anteprima");const auto child=editor.sheet().drawing.views.back().id;
 ok(editor.setCenterLinesVisible(child,true));
 auto parent=editor.sheet().drawing.views[0];parent.plane=Frame3({},normalized(Vec3(1,1,2)),normalized(Vec3(1,1,-1)));
 ok(editor.updateView(parent,editor.sheet().views[0]));need(path(editor,child,"centerLines"),"coniche della figlia aggiornate al piano madre");const auto childPos=path(editor,child,"centerLines")->pos();
 auto parentPosition=editor.sheet().views[0].position;ok(editor.moveView(top.id,parentPosition+QPointF(6,9)));near(path(editor,child,"centerLines")->pos().x()-childPos.x(),6,"assi figlia seguono spostamento madre");near(path(editor,child,"centerLines")->pos().y()-childPos.y(),9,"allineamento figlia segue madre");
 ok(editor.undo());ok(editor.undo());
 DrawingView half;half.plane=cut.plane;half.sectionStyle=DrawingSectionStyle::HalfRight;half.scale=2;
 ok(editor.insertView(half,{half.id,"Mezza sezione",{240,190},{1}}));ok(editor.setCenterLinesVisible(half.id,true));need(path(editor,half.id,"centerLines"),"assi nelle mezze sezioni con curve ritagliate");
 oblique.id=QUuid::createUuid();ok(editor.insertView(oblique,{oblique.id,"Proiezione ellittica",{300,210},{1}}));ok(editor.setCenterLinesVisible(oblique.id,true));
 const auto output=qEnvironmentVariable("FORGECAD_CENTERS_OUTPUT");if(!output.isEmpty()){need(QDir().mkpath(output),"cartella QA");DrawingPdfReport report;ok(editor.exportPdf(output+"/assi-centri.pdf",report));need(QFile::exists(output+"/assi-centri.pdf"),"PDF con assi esportato");editor.fitSheet();QApplication::processEvents();need(editor.grab().save(output+"/editor.png"),"immagine editor");}
 auto saved=editor.sheet();DrawingSheetEditor reopened(saved,changed);need(path(reopened,top.id,"centerLines")&&path(reopened,cut.id,"centerLines"),"riapertura rigenera riferimenti dalla versione corretta del modello");editor.setReadOnly(true);select(editor,top.id);need(!marks->isEnabled()&&!axes->isEnabled()&&!editor.setCenterLinesVisible(top.id,false).isEmpty()&&!editor.setCenterMarksVisible(top.id,true).isEmpty(),"sola lettura rispettata");
 editor.setReadOnly(false);need(!editor.setCenterLinesVisible(QUuid::createUuid(),true).isEmpty()&&!editor.setCenterMarksVisible(QUuid::createUuid(),true).isEmpty(),"ID inesistente rifiutato anche in modifica");
 std::cout<<"PASS assi principali esatti, coniche degeneri, fusione concentrici, scala, UI, persistenza, Undo/Redo, quote, visibilita', sezioni, aggiornamento modello, PDF e sola lettura\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
