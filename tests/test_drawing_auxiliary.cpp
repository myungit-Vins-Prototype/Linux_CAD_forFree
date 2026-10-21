#include "cad_drawing_editor.h"
#include "cad_drawing_derived.h"
#include "fk_primitives.h"
#include "fk_curve_algo.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
void require(bool value,const char *why){if(!value)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
void near(double a,double b,const char *why){require(std::abs(a-b)<1e-6,why);}
QByteArray bytes(const DrawingSheet &sheet){QByteArray data;ok(encodeDrawingSheet(sheet,data));return data;}
const DrawingView &view(const DrawingSheetEditor &editor,const QUuid &id){for(const auto &v:editor.sheet().drawing.views)if(v.id==id)return v;throw std::runtime_error("vista assente");}
const DrawingViewLayout &layout(const DrawingSheetEditor &editor,const QUuid &id){for(const auto &v:editor.sheet().views)if(v.viewId==id)return v;throw std::runtime_error("layout assente");}
void mouse(DrawingSheetEditor &editor,QPointF paper,bool click=false){
    auto *canvas=editor.canvas();const QPointF local=canvas->mapFromScene(paper),global=canvas->viewport()->mapToGlobal(local.toPoint());
    QMouseEvent event(click?QEvent::MouseButtonPress:QEvent::MouseMove,local,global,click?Qt::LeftButton:Qt::NoButton,click?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&event);
    if(click){QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&release);}
}
void frames(){
    for(const auto &frame:std::vector<Frame3>{Frame3(),Frame3(Vec3(4,5,6),Vec3(0,-1,0),Vec3(1,0,0)),Frame3(Vec3(1,2,3),normalized(Vec3(1,2,3)),Vec3(2,-1,0))})
    for(double angle:{-360.,-90.,0.,37.,45.,90.,180.,270.,360.}){
        DrawingSheet sheet;DrawingView parent;parent.kind=DrawingViewKind::ProjectedEdges;parent.plane=frame;parent.scale=3;
        DrawingView child;DrawingViewLayout root{parent.id,"Madre",{60,100},{1}},derived{child.id,"Ausiliaria",{180,30},{1}};
        derived.relation.kind=DrawingDerivedKind::Auxiliary;derived.relation.parentId=parent.id;derived.relation.auxiliaryAngle=angle;
        sheet.drawing.views={parent,child};sheet.views={root,derived};ok(synchronizeDrawingViews(sheet));ok(validateDrawingRelations(sheet));
        const auto &plane=sheet.drawing.views[1].plane;const double a=angle*kPi/180;
        const auto normal=frame.directionToGlobal(Vec3(std::cos(a),std::sin(a),0));
        require(distance(plane.zDir(),normal)<1e-10,"direzione della vista relativa agli assi della madre");
        near(dot(cross(plane.xDir(),plane.yDir()),plane.zDir()),1,"frame senza specchiature");
        require(distance(plane.origin(),frame.origin())<1e-10,"origine geometrica ereditata");near(sheet.drawing.views[1].scale,3,"scala ereditata");
        const auto tangent=frame.directionToGlobal(Vec3(-std::sin(a),std::cos(a),0));const auto projected=plane.directionToLocal(tangent);
        near(projected.x(),-std::sin(a),"retta di ribaltamento conserva X");near(projected.y(),std::cos(a),"retta di ribaltamento conserva Y");
        const auto offset=sheet.views[1].position-root.position;near(offset.x()*std::sin(a)+offset.y()*std::cos(a),0,"allineamento obliquo sul foglio");
    }
}
void editorTests(){
    const auto normal=normalized(Vec3(1,1,0)),tangent=normalized(Vec3(-1,1,0));
    auto solid=std::make_shared<Body>(makeBox(Frame3(Vec3(),normal,tangent),20,12,8));
    DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};
    model.snapshot.bodies={{1,solid,std::make_shared<ElementNames>(propagateNames(*solid,makeHistory({},*solid),{},"auxiliaryBox"))}};model.choices={{1,"Corpo inclinato"}};
    DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor editor(sheet,model);editor.resize(1200,820);editor.show();QApplication::processEvents();editor.fitSheet();
    DrawingView parent;parent.kind=DrawingViewKind::ProjectedEdges;parent.scale=2;ok(editor.insertView(parent,{parent.id,"Madre",{60,130},{1}}));
    const auto before=bytes(editor.sheet());ok(editor.beginAuxiliaryViewPlacement(parent.id,45));require(editor.hasViewPlacement()&&bytes(editor.sheet())==before,"anteprima senza modifica al documento");
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(editor.canvas(),&escape);require(!editor.hasViewPlacement()&&bytes(editor.sheet())==before,"Esc annulla senza Undo spurio");
    ok(editor.beginAuxiliaryViewPlacement(parent.id,45));mouse(editor,{170,40},true);const auto id=editor.sheet().views.back().viewId;
    require(!editor.hasViewPlacement()&&layout(editor,id).relation.kind==DrawingDerivedKind::Auxiliary,"clic inserisce la vista ausiliaria");
    require(distance(view(editor,id).plane.zDir(),normal)<1e-10,"vista normale alla faccia inclinata");
    bool length20=false,length12=false;DrawingDimension dimension;
    for(const auto &curve:editor.curves(id))if(curve.kind==DrawingCurveKind::ProjectedEdge){const auto length=arcLength(*curve.curve,curve.range);if(std::abs(length-20)<1e-7){length20=true;dimension=dimensionOfCurve(view(editor,id),curve);}if(std::abs(length-12)<1e-7)length12=true;}
    require(length20&&length12,"faccia inclinata proiettata in vera grandezza 20x12");ok(editor.addDimension(dimension,{215,45}));near(editor.dimensionResult(dimension.id).value,20,"quota esatta sulla vista ausiliaria");
    auto changed=layout(editor,id);changed.relation.auxiliaryAngle=75;ok(editor.updateView(view(editor,id),changed));
    require(editor.dimensionResult(dimension.id).status==DrawingDimensionStatus::Ready,"quota conserva il riferimento al cambio di direzione");near(editor.dimensionResult(dimension.id).value,20*std::cos(kPi/6),"misura aggiornata alla nuova proiezione");ok(editor.undo());near(editor.dimensionResult(dimension.id).value,20,"Undo direzione ripristina quota");
    auto scaled=parent;scaled.scale=3;ok(editor.updateView(scaled,layout(editor,parent.id)));near(view(editor,id).scale,3,"scala madre propagata");near(editor.dimensionResult(dimension.id).value,20,"scala non altera la misura");ok(editor.undo());
    ok(editor.moveView(parent.id,{80,140}));near(layout(editor,id).position.x()+layout(editor,id).position.y(),220,"spostamento madre aggiorna allineamento");ok(editor.undo());
    ok(editor.setViewAlignment(id,false));ok(editor.moveView(id,{190,80}));require(layout(editor,id).position==QPointF(190,80),"libera allineamento conserva relazione");ok(editor.setViewAlignment(id,true));near(layout(editor,id).position.x()+layout(editor,id).position.y(),190,"ripristino allineamento obliquo");
    const auto linked=bytes(editor.sheet());require(!editor.removeView(parent.id).isEmpty()&&linked==bytes(editor.sheet()),"madre protetta mentre ha dipendenti");
    ok(editor.beginAuxiliaryViewPlacement(id,30));mouse(editor,{100,150},true);const auto grandchild=editor.sheet().views.back().viewId;
    require(layout(editor,grandchild).relation.parentId==id,"ausiliaria di un'ausiliaria");scaled.scale=4;ok(editor.updateView(scaled,layout(editor,parent.id)));near(view(editor,grandchild).scale,4,"propagazione transitiva");ok(editor.undo());
    ok(editor.detachView(id));require(layout(editor,id).relation.parentId.isNull(),"sgancio della vista ausiliaria");const auto detached=view(editor,id).plane;
    scaled.plane=Frame3(Vec3(1,2,3),Vec3(0,-1,0),Vec3(1,0,0));ok(editor.updateView(scaled,layout(editor,parent.id)));require(distance(view(editor,id).plane.zDir(),detached.zDir())<1e-10,"sganciata resta indipendente");ok(editor.undo());ok(editor.undo());
    // Codec: schema minimo, dati incompatibili rifiutati senza mutare l'output.
    auto object=QJsonDocument::fromJson(bytes(editor.sheet())).object();require(object["schema"].toInt()==13,"schema 13 solo quando serve");DrawingSheet decoded;ok(decodeDrawingSheet(QJsonDocument(object).toJson(),decoded));
    require(decoded.views[1].relation.auxiliaryAngle==45&&decoded.views[1].relation.parentId==parent.id,"round trip relazione e direzione");
    DrawingSheet sentinel=decoded;const auto unchanged=bytes(sentinel);object["schema"]=12;require(!decodeDrawingSheet(QJsonDocument(object).toJson(),sentinel).isEmpty()&&bytes(sentinel)==unchanged,"downgrade incompatibile rifiutato atomicamente");
    object["schema"]=13;auto layouts=object["views"].toArray();auto item=layouts[1].toObject();auto relation=item["relation"].toObject();relation.remove("auxiliaryAngle");item["relation"]=relation;layouts[1]=item;object["views"]=layouts;require(!decodeDrawingSheet(QJsonDocument(object).toJson(),sentinel).isEmpty(),"direzione assente rifiutata");
    auto invalid=editor.sheet();invalid.views[1].relation.auxiliaryAngle=std::numeric_limits<double>::quiet_NaN();QByteArray untouched="sentinel";require(!encodeDrawingSheet(invalid,untouched).isEmpty()&&untouched=="sentinel","direzione non finita rifiutata");
    invalid=editor.sheet();invalid.views[0].relation=invalid.views[1].relation;invalid.views[0].relation.parentId=grandchild;require(!encodeDrawingSheet(invalid,untouched).isEmpty(),"ciclo di ausiliarie rifiutato");
    require(!editor.beginAuxiliaryViewPlacement(QUuid::createUuid(),45).isEmpty()&&!editor.beginAuxiliaryViewPlacement(parent.id,361).isEmpty(),"madre e direzione invalide rifiutate");
    DrawingSheetEditor reopened(decoded,model);near(reopened.dimensionResult(dimension.id).value,20,"riapertura ricostruisce quota e vista");
    ok(editor.setViewAlignment(grandchild,false));ok(editor.moveView(grandchild,{170,135}));
    QTemporaryDir temporary;const auto output=qEnvironmentVariable("FORGECAD_AUXILIARY_OUTPUT");const auto pdf=output.isEmpty()?temporary.filePath("ausiliaria.pdf"):output+"/ausiliaria.pdf";DrawingPdfReport report;
    ok(editor.exportPdf(pdf,report));QFile pdfFile(pdf);require(pdfFile.open(QIODevice::ReadOnly)&&pdfFile.read(5)=="%PDF-","PDF della vista ausiliaria");
    const auto readOnly=bytes(editor.sheet());editor.setReadOnly(true);require(!editor.findChild<QAction *>("drawingAuxiliaryViewAction")->isEnabled()&&!editor.beginAuxiliaryViewPlacement(parent.id,45).isEmpty()&&!editor.setViewAlignment(id,false).isEmpty(),"sola lettura protegge anche ausiliarie");ok(editor.exportPdf(pdf,report));require(readOnly==bytes(editor.sheet()),"export storico conserva i dati");editor.setReadOnly(false);
    // Percorso reale del comando e proprieta' raggiungibili da menu.
    auto *action=editor.findChild<QAction *>("drawingAuxiliaryViewAction");require(action&&!action->icon().isNull(),"comando ausiliaria con icona");
    QTimer::singleShot(0,&editor,[&]{auto *dialog=editor.findChild<QDialog *>("drawingAuxiliaryViewDialog");require(dialog,"dialogo vista ausiliaria");dialog->findChild<QDoubleSpinBox *>("drawingAuxiliaryAngle")->setValue(25);dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});action->trigger();require(editor.hasViewPlacement(),"OK del dialogo apre anteprima");editor.cancelViewPlacement();
    for(auto *item:editor.canvas()->scene()->items())item->setSelected(false);
    for(auto *item:editor.canvas()->scene()->items())if(item->data(1).toUuid()==id&&item->flags().testFlag(QGraphicsItem::ItemIsSelectable)){item->setSelected(true);break;}
    QApplication::processEvents();bool properties=false;
    QTimer::singleShot(0,&editor,[&]{auto *dialog=editor.findChild<QDialog *>("drawingDerivedPropertiesDialog");require(dialog,"proprieta' vista ausiliaria");properties=true;auto *angle=dialog->findChild<QDoubleSpinBox *>("drawingAuxiliaryAngle");require(angle&&angle->isVisible(),"direzione modificabile nelle proprieta'");angle->setValue(60);dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});editor.findChild<QAction *>("drawingEditViewAction")->trigger();require(properties,"proprieta' raggiungibili");
    near(layout(editor,id).relation.auxiliaryAngle,60,"OK proprieta' applica la direzione");near(editor.dimensionResult(dimension.id).value,20*std::cos(kPi/12),"proprieta' aggiornano la misura associata");ok(editor.undo());near(editor.dimensionResult(dimension.id).value,20,"Undo proprieta' ripristina la quota");
    if(!output.isEmpty()){editor.fitSheet();QApplication::processEvents();require(editor.grab().save(output+"/editor.png"),"immagine editor");}
}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{frames();editorTests();std::cout<<"PASS viste ausiliarie: frame, vera grandezza, quote, gerarchia, allineamento, codec, Undo, sola lettura, dialoghi e PDF\n";}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
