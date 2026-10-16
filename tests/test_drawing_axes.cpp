#include "cad_drawing_editor.h"
#include "cad_drawing_axes.h"
#include "fk_primitives.h"
#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QDir>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void need(bool b,const char *s){if(!b)throw std::runtime_error(s);}
void ok(const QString &s){if(!s.isEmpty())throw std::runtime_error(s.toStdString());QApplication::processEvents();}
void near(double x,double y,const char *s){need(std::abs(x-y)<1e-6,s);}
DrawingSourceBody body(quint64 id,Body shape){auto p=std::make_shared<Body>(std::move(shape));return {id,p,std::make_shared<ElementNames>(propagateNames(*p,makeHistory({},*p),{},"axis"+std::to_string(id)))};}
QByteArray bytes(const DrawingSheetEditor &e){QByteArray b;ok(encodeDrawingSheet(e.sheet(),b));return b;}
QGraphicsPathItem *item(DrawingSheetEditor &e,QUuid id,const char *kind){for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()==kind&&(i->data(1).toUuid()==id||i->data(3).toUuid()==id))if(auto *p=dynamic_cast<QGraphicsPathItem *>(i))return p;return nullptr;}
DrawingAttachment point(const DrawingView &v,const DrawingEditorModel &m,const std::vector<DrawingCurve> &curves,Vec2 p){for(const auto &c:curves)if(c.featureId==1)for(auto role:{DrawingPointRole::Start,DrawingPointRole::End}){auto ref=drawingAttachment(v,c,m.snapshot,role);const auto resolved=resolveDrawingAttachment(v,m.snapshot,curves,ref);if(resolved.status==DrawingDimensionStatus::Ready&&distance(resolved.point,p)<1e-7)return ref;}throw std::runtime_error("punto richiesto");}
DrawingAttachment vertical(const DrawingView &v,const DrawingEditorModel &m,const std::vector<DrawingCurve> &curves,double x){for(const auto &c:curves)if(c.featureId==1){const auto p=v.plane.toLocal(c.curve->point(c.range.lo)),q=v.plane.toLocal(c.curve->point(c.range.hi));if(std::abs(p.x()-x)<1e-7&&std::abs(q.x()-x)<1e-7&&std::abs(q.y()-p.y())>5)return drawingAttachment(v,c,m.snapshot,DrawingPointRole::Curve);}throw std::runtime_error("retta richiesta");}
void mouse(DrawingSheetEditor &e,QPointF p,bool click=false,Qt::KeyboardModifiers modifiers=Qt::NoModifier){auto *v=e.canvas();const QPointF q=v->mapFromScene(p),global=v->viewport()->mapToGlobal(q.toPoint());QMouseEvent event(click?QEvent::MouseButtonPress:QEvent::MouseMove,q,global,click?Qt::LeftButton:Qt::NoButton,click?Qt::LeftButton:Qt::NoButton,modifiers);QApplication::sendEvent(v->viewport(),&event);if(click){QMouseEvent release(QEvent::MouseButtonRelease,q,global,Qt::LeftButton,Qt::NoButton,modifiers);QApplication::sendEvent(v->viewport(),&release);}QApplication::processEvents();}
void select(DrawingSheetEditor &e,QUuid id,const char *kind){auto *tree=e.findChild<QTreeWidget *>("drawingTree");for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==kind&&(*it)->data(0,Qt::UserRole+1).toUuid()==id){tree->setCurrentItem(*it);return;}throw std::runtime_error("selezione asse/vista");}
}
#include "test_drawing_axis_dimensions.h"
#include "test_drawing_annotations.h"
#include "test_drawing_tolerances.h"
int main(int argc,char **argv){QApplication app(argc,argv);try{
 DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={body(1,makeBox(Frame3(),12,8,6)),body(2,makeCylinder(Frame3(Vec3(30,0,0),Vec3(0,0,1),Vec3(1,0,0)),4,10)),body(3,makeCylinder(Frame3(Vec3(30,0,0),Vec3(0,0,1),Vec3(1,0,0)),2,10)),body(4,makeBox(Frame3(Vec3(25,-10,-2),Vec3(0,0,1),Vec3(1,0,0)),10,3,14))};model.choices={{1,"Blocco"},{2,"Cilindro"},{3,"Coassiale"},{4,"Copertura"}};
 DrawingSheet sheet;sheet.title="Assi manuali associati e cilindri";sheet.drawing.model=model.snapshot.version;sheet.size={420,297};sheet.titleBlock.visible=true;DrawingSheetEditor editor(sheet,model);editor.resize(1400,900);editor.show();QApplication::processEvents();editor.fitSheet();
 DrawingView top;top.kind=DrawingViewKind::ProjectedEdges;top.scale=4;ok(editor.insertView(top,{top.id,"Due punti",{75,75},{1}}));DrawingView front=top;front.id=QUuid::createUuid();front.plane=Frame3({},Vec3(0,-1,0),Vec3(1,0,0));ok(editor.insertView(front,{front.id,"Mediana e asse cilindrico",{200,115},{1,2,3}}));
 DrawingAxis diagonal;diagonal.viewId=top.id;diagonal.name="Asse diagonale";diagonal.references={point(top,model,editor.curves(top.id),{0,0}),point(top,model,editor.curves(top.id),{12,8})};editor.markSaved();const auto before=bytes(editor);ok(editor.addAxis(diagonal));need(editor.isModified()&&editor.axisResult(diagonal.id).status==DrawingDimensionStatus::Ready,"asse per punti inserito");const auto exact=editor.curves(top.id)[0].curve.get();ok(editor.undo());need(bytes(editor)==before&&!editor.isModified(),"Undo asse e checkpoint");ok(editor.redo());need(editor.curves(top.id)[0].curve.get()==exact,"Redo riusa curve esatte");
 DrawingAxis median;median.kind=DrawingAxisKind::BetweenLines;median.viewId=front.id;median.name="Mediana";median.references={vertical(front,model,editor.curves(front.id),0),vertical(front,model,editor.curves(front.id),12)};ok(editor.addAxis(median));near(editor.axisResult(median.id).first.x(),6,"mediana esatta delle rette");near(editor.axisResult(median.id).second.y()-editor.axisResult(median.id).first.y(),6,"porzione longitudinale comune");
 auto invalid=median;invalid.id=QUuid::createUuid();invalid.references[1]=invalid.references[0];need(!editor.addAxis(invalid).isEmpty(),"rette coincidenti rifiutate");invalid=diagonal;invalid.id=QUuid::createUuid();invalid.references[1]=invalid.references[0];need(!editor.addAxis(invalid).isEmpty(),"punti coincidenti rifiutati");
 ok(editor.setCylinderAxesVisible(front.id,true));auto *cylinder=item(editor,front.id,"cylinderAxes");need(cylinder&&cylinder->path().elementCount()==2,"cilindri coassiali fusi senza duplicazioni");near(cylinder->path().boundingRect().height(),44,"asse cilindrico dalla rifilatura con prolungamento carta");near(cylinder->path().elementAt(0).x,120,"posizione esatta dell'asse cilindrico");
 DrawingSheet decoded;ok(decodeDrawingSheet(bytes(editor),decoded));need(decoded.axes.size()==2&&decoded.views[1].showCylinderAxes,"assi e impostazioni persistiti");auto root=QJsonDocument::fromJson(bytes(editor)).object();need(root["schema"].toInt()==4,"schema 4 del solo foglio con assi manuali");root["schema"]=3;need(!decodeDrawingSheet(QJsonDocument(root).toJson(),decoded).isEmpty()&&decoded.axes.size()==2,"schema declassato rifiutato atomicamente");root=QJsonDocument::fromJson(bytes(editor)).object();auto axes=root["axes"].toArray();auto first=axes[0].toObject();first["extension"]=-1;axes[0]=first;root["axes"]=axes;need(!decodeDrawingSheet(QJsonDocument(root).toJson(),decoded).isEmpty(),"prolungamento negativo rifiutato");
 select(editor,median.id,"axis");bool edited=false;QTimer::singleShot(0,&editor,[&]{auto *dialog=editor.findChild<QDialog *>("drawingAxisDialog");need(dialog,"dialogo proprieta'");dialog->findChild<QDoubleSpinBox *>("drawingAxisExtension")->setValue(5);dialog->findChild<QLineEdit *>("drawingAxisName")->setText("Asse del blocco");edited=true;dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});editor.findChild<QAction *>("drawingEditAxisAction")->trigger();QApplication::processEvents();need(edited&&editor.sheet().axes[1].extension==5,"proprieta' asse applicate");near(item(editor,median.id,"axis")->path().boundingRect().height(),34,"prolungamento proprieta' in mm carta");
 auto hidden=editor.sheet().axes[1];hidden.visible=false;ok(editor.updateAxis(hidden));select(editor,median.id,"axis");need(editor.findChild<QAction *>("drawingEditAxisAction")->isEnabled(),"asse nascosto modificabile dall'albero");hidden.visible=true;ok(editor.updateAxis(hidden));
 const auto old=item(editor,diagonal.id,"axis")->pos();ok(editor.moveView(top.id,{85,85}));near(item(editor,diagonal.id,"axis")->pos().x()-old.x(),10,"asse segue posizione vista");auto scaled=top;scaled.scale=2;ok(editor.updateView(scaled,editor.sheet().views[0]));near(editor.axisResult(diagonal.id).second.x(),12,"scala non cambia geometria asse");
 auto covered=front;covered.id=QUuid::createUuid();ok(editor.insertView(covered,{covered.id,"Cilindro coperto",{50,190},{2,4}}));ok(editor.setCylinderAxesVisible(covered.id,true));need(!item(editor,covered.id,"cylinderAxes"),"cilindro coperto senza asse automatico");ok(editor.setHiddenLinesVisible(covered.id,true));need(item(editor,covered.id,"cylinderAxes"),"asse coperto su richiesta");ok(editor.setHiddenLinesVisible(covered.id,false));
 // Inserimento con snap e anteprima, nessuna quota creata dal comando asse.
 const auto n=editor.sheet().axes.size();ok(editor.beginAxis(DrawingAxisKind::ThroughPoints));mouse(editor,{85,85},true);mouse(editor,{109,69},true);need(item(editor,QUuid(),"axisPreview"),"anteprima asse dai due snap");mouse(editor,{140,50},true);need(editor.sheet().axes.size()==n+1&&editor.sheet().drawing.dimensions.empty(),"asse inserito dal mouse senza quote fittizie");
 ok(editor.beginAxis(DrawingAxisKind::BetweenLines));mouse(editor,{200,103},true);mouse(editor,{248,103},true);mouse(editor,{260,145},true);need(editor.sheet().axes.size()==n+2,"mediana inserita con mouse senza forzare Maiusc");
 const auto unchanged=bytes(editor);ok(editor.beginAxis(DrawingAxisKind::ThroughPoints,diagonal.id));QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(editor.canvas(),&escape);need(bytes(editor)==unchanged,"annulla ridefinizione conserva asse e UUID");
 auto changed=model;changed.snapshot.bodies[0]=body(1,makeBox(Frame3(),16,8,6));ok(editor.updateModel(changed));near(editor.axisResult(median.id).first.x(),8,"mediana segue modifica del modello");near(editor.axisResult(diagonal.id).second.x(),16,"punto dell'asse segue vertice nominato");
 const auto output=qEnvironmentVariable("FORGECAD_AXES_OUTPUT");if(!output.isEmpty()){need(QDir().mkpath(output),"cartella QA");DrawingPdfReport report;ok(editor.exportPdf(output+"/assi.pdf",report));editor.fitSheet();need(editor.grab().save(output+"/editor.png"),"anteprima assi");}
 const auto saved=editor.sheet();DrawingSheetEditor reopened(saved,changed);need(reopened.axisResult(median.id).status==DrawingDimensionStatus::Ready,"riapertura riferimenti associati");
 auto missing=changed;missing.snapshot.bodies.erase(missing.snapshot.bodies.begin());ok(editor.updateModel(missing));need(editor.axisResult(median.id).status==DrawingDimensionStatus::Missing,"asse con sorgente eliminata da verificare, senza riassociazione");if(!output.isEmpty()){DrawingPdfReport report;ok(editor.exportPdf(output+"/assi-mancanti.pdf",report));need(std::any_of(report.warnings.begin(),report.warnings.end(),[](const auto &s){return s.contains("assi da verificare");}),"PDF segnala assi mancanti");}
 ok(editor.updateModel(changed));ok(editor.removeView(top.id));need(std::none_of(editor.sheet().axes.begin(),editor.sheet().axes.end(),[&](const auto &a){return a.viewId==top.id;}),"rimuovi vista rimuove assi");ok(editor.undo());need(editor.axisResult(diagonal.id).status==DrawingDimensionStatus::Ready,"Undo vista ripristina assi");
 editor.setReadOnly(true);need(!editor.addAxis(diagonal).isEmpty()&&!editor.updateAxis(median).isEmpty()&&!editor.removeAxis(median.id).isEmpty()&&!editor.beginAxis(DrawingAxisKind::ThroughPoints).isEmpty()&&!editor.setCylinderAxesVisible(front.id,false).isEmpty(),"sola lettura rispettata");
 axisDimensionTests();
 annotationTests();
 toleranceTests();
 std::cout<<"PASS assi associati, mediana, superfici cilindriche, snap mouse, proprieta', codec schema 4, undo, aggiornamento, riferimenti mancanti, PDF e sola lettura\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
