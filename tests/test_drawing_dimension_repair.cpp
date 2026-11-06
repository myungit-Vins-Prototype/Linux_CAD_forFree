#include "cad_drawing_editor.h"
#include "cad_drawing_dimension_repair.h"
#include "fk_primitives.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QTimer>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
void near(double a,double b,const char *message){require(std::isfinite(a)&&std::abs(a-b)<1e-7,message);}
DrawingSourceBody source(Body value,quint64 id){auto body=std::make_shared<Body>(std::move(value));return {id,body,std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"repair"+std::to_string(id)))};}
QByteArray encoded(const DrawingSheetEditor &e){QByteArray bytes;ok(encodeDrawingSheet(e.sheet(),bytes));return bytes;}
int items(const DrawingSheetEditor &e,const QString &kind){int count=0;for(auto *item:e.canvas()->scene()->items())if(item->isVisible()&&item->data(0).toString()==kind)++count;return count;}
void pointer(DrawingSheetEditor &e,QPointF paper,bool click=false,Qt::KeyboardModifiers modifiers={}){
    auto *view=e.canvas();const QPointF point=view->mapFromScene(paper),global=view->viewport()->mapToGlobal(point.toPoint());
    if(click){QMouseEvent press(QEvent::MouseButtonPress,point,global,Qt::LeftButton,Qt::LeftButton,modifiers),release(QEvent::MouseButtonRelease,point,global,Qt::LeftButton,Qt::NoButton,modifiers);QApplication::sendEvent(view->viewport(),&press);QApplication::sendEvent(view->viewport(),&release);}
    else{QMouseEvent move(QEvent::MouseMove,point,global,Qt::NoButton,Qt::NoButton,modifiers);QApplication::sendEvent(view->viewport(),&move);}
}
void key(DrawingSheetEditor &e,int code){QKeyEvent press(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(e.canvas(),&press);}
void show(DrawingSheetEditor &e){e.resize(1400,900);e.show();QApplication::processEvents();e.fitSheet();QApplication::processEvents();}
void select(DrawingSheetEditor &e,const QUuid &id){auto *tree=e.findChild<QTreeWidget *>("drawingTree");for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+1).toUuid()==id){tree->setCurrentItem(*it);return;}throw std::runtime_error("quota nell'albero");}
struct Fixture {
    DrawingEditorModel model;
    DrawingSheet sheet;
    DrawingView view;
    std::vector<DrawingCurve> curves;
    Fixture(){
        model.snapshot.version={QUuid::createUuid(),{}};
        model.snapshot.bodies={source(makeBox(Frame3(),10,6,4),1),source(makeCylinder(Frame3(Vec3(35,0,0),Vec3(0,0,1),Vec3(1,0,0)),3,8),2)};
        model.choices={{1,"Blocco"},{2,"Cilindro"}};
        view.kind=DrawingViewKind::ProjectedEdges;view.scale=3;
        sheet.drawing.model=model.snapshot.version;sheet.drawing.views={view};sheet.views={{view.id,"Superiore",{55,130},{1,2}}};
        ok(drawingViewCurves(model.snapshot,view,curves));
    }
    QPointF at(double x,double y)const{return {55+3*x,130-3*y};}
    DrawingAttachment point(double x,double y)const{
        for(const auto &curve:curves)for(auto role:{DrawingPointRole::Start,DrawingPointRole::End})try{
            const auto ref=drawingAttachment(view,curve,model.snapshot,role);const auto found=resolveDrawingAttachment(view,model.snapshot,curves,ref);
            if(found.status==DrawingDimensionStatus::Ready&&distance(found.point,Vec2(x,y))<1e-7)return ref;
        }catch(const std::exception &){}
        throw std::runtime_error("estremo geometrico della fixture");
    }
    DrawingAttachment line(bool vertical=false)const{
        for(const auto &curve:curves){const auto p=drawingPrimitive(view,curve);if(p.kind!=DrawingPrimitive::Kind::Line)continue;
            const auto a=view.plane.toLocal(curve.curve->point(curve.range.lo)),b=view.plane.toLocal(curve.curve->point(curve.range.hi));
            const Vec2 first(a.x(),a.y()),last(b.x(),b.y());
            if(vertical?std::abs(first.x())<1e-7&&std::abs(last.x())<1e-7&&distance(first,last)>5:
                std::abs(first.y())<1e-7&&std::abs(last.y())<1e-7&&distance(first,last)>9)
                return drawingAttachment(view,curve,model.snapshot,DrawingPointRole::Curve);
        }throw std::runtime_error("retta della fixture");
    }
    DrawingDimension distanceDimension(DrawingMeasure measure=DrawingMeasure::Distance)const{return drawingDimensionBetween(view,model.snapshot,curves,{point(0,0),point(10,6)},measure,{0,15});}
    void add(DrawingDimension d){sheet.drawing.dimensions={d};sheet.dimensions={{d.id,{90,75}}};}
    void breakReference(int index){auto &d=sheet.drawing.dimensions[0];d.references.at(std::size_t(index)).curve.edgeName="bordo-eliminato";d.section=d.references[0].curve;}
};

void singleReference(){
    Fixture f;auto d=f.distanceDimension();d.decimals=4;f.add(d);f.breakReference(0);
    auto &p=f.sheet.dimensions[0].presentation;p.mode=DrawingToleranceMode::Deviations;p.upper=.1;p.lower=-.02;p.prefix="TEST ";p.suffix=" FIN";p.extensionSlant=15;p.reverseArrows=true;
    f.sheet.dimensionNumbering.enabled=true;f.sheet.dimensionNumbering.persistent=true;f.sheet.dimensionNumbering.assignments.insert(d.id,7);f.sheet.dimensionNumbering.nextNumber=8;
    DrawingSheetEditor e(f.sheet,f.model);show(e);e.markSaved();const auto before=encoded(e);
    require(e.dimensionResult(d.id).status==DrawingDimensionStatus::Missing,"quota inizialmente mancante");
    select(e,d.id);bool propertyRepair=false;
    QTimer::singleShot(0,&e,[&]{auto *dialog=e.findChild<QDialog *>("drawingDimensionPropertiesDialog");require(dialog,"dialogo proprieta quota rotta");propertyRepair=true;dialog->findChild<QPushButton *>("drawingDimensionRepairButton")->click();});
    e.findChild<QAction *>("drawingEditDimensionAction")->trigger();require(propertyRepair&&encoded(e)==before,"proprieta avviano riparazione senza modifiche");key(e,Qt::Key_Escape);
    select(e,d.id);e.findChild<QAction *>("drawingRepairDimensionAction")->trigger();
    pointer(e,f.at(0,0)+QPointF(.3,.2));require(items(e,"dimensionSnap")==1&&!e.isModified(),"snap attivo e nessuna modifica durante riparazione");
    key(e,Qt::Key_Tab);pointer(e,f.at(0,0)+QPointF(.3,.2),true);
    require(items(e,"dimensionRepairPreview")==1&&encoded(e)==before,"anteprima completa senza mutare il foglio");
    key(e,Qt::Key_Escape);require(encoded(e)==before&&items(e,"dimensionRepairPreview")==0,"Esc annulla anche l’anteprima completa");
    bool restored=false;for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="dimension"&&item->data(3).toUuid()==d.id)restored=item->isVisible()&&item->opacity()==1;
    require(restored,"testo originale ripristinato dopo annullamento");
    ok(e.beginDimensionRepair(d.id));pointer(e,f.at(0,0),true);
    QGraphicsSimpleTextItem *preview=nullptr;for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="dimensionRepairPreview")preview=dynamic_cast<QGraphicsSimpleTextItem *>(item);
    require(preview&&preview->text().contains("TEST")&&preview->text().contains("FIN"),"anteprima conserva prefisso e suffisso");
    const auto center=preview->sceneBoundingRect().center();
    const auto output=qEnvironmentVariable("FORGECAD_REPAIR_PREVIEW");if(!output.isEmpty())require(e.grab().save(output),"immagine anteprima riparazione");
    key(e,Qt::Key_Return);QApplication::processEvents();near(e.dimensionResult(d.id).value,std::sqrt(136.),"misura ricollegata esatta");
    require(e.isModified()&&e.sheet().drawing.dimensions.size()==1,"riparazione modifica senza duplicare");
    const auto &after=e.sheet().drawing.dimensions[0];require(after.id==d.id&&after.decimals==4&&after.measure==d.measure,"ID tipo e precisione invariati");
    const auto resolved=resolveDrawingAttachment(f.view,f.model.snapshot,e.curves(f.view.id),after.references[1]);near(distance(resolved.point,Vec2(10,6)),0,"riferimento valido conservato");
    require(after.references[1].vertexName==d.references[1].vertexName&&after.references[1].supportFace==d.references[1].supportFace,"identita del riferimento sano conservata");
    require(e.sheet().dimensions[0].position==f.sheet.dimensions[0].position&&e.sheet().dimensions[0].presentation==p,"posizione e tolleranze invariate");
    require(e.sheet().dimensionNumbering.assignments==f.sheet.dimensionNumbering.assignments,"badge conservato");
    for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="dimension"&&item->data(3).toUuid()==d.id)near(QLineF(center,item->sceneBoundingRect().center()).length(),0,"anteprima identica alla conferma");
    const auto repaired=encoded(e);ok(e.undo());require(encoded(e)==before&&!e.isModified(),"Undo unico ripristina stato rotto byte per byte");ok(e.redo());require(encoded(e)==repaired,"Redo esatto");
    DrawingSheet read;ok(decodeDrawingSheet(repaired,read));DrawingSheetEditor reopened(read,f.model);near(reopened.dimensionResult(d.id).value,std::sqrt(136.),"riapertura conserva agganci");
    auto changed=f.model;changed.snapshot.bodies[0]=source(makeBox(Frame3(),14,6,4),1);ok(reopened.updateModel(changed));near(reopened.dimensionResult(d.id).value,std::sqrt(232.),"nuovi agganci seguono il modello");
    std::cout<<"PASS riferimento mancante, snap/Tab, metadata, anteprima/conferma, Undo/Redo, codec e genealogia\n";
}
void twoReferencesAndCancel(){
    Fixture f;auto d=f.distanceDimension(DrawingMeasure::HorizontalDistance);f.add(d);f.breakReference(0);f.breakReference(1);
    auto other=f.view;other.id=QUuid::createUuid();f.sheet.drawing.views.push_back(other);f.sheet.views.push_back({other.id,"Altra vista",{200,130},{1}});
    DrawingSheetEditor e(f.sheet,f.model);show(e);const auto before=encoded(e);ok(e.beginDimensionRepair(d.id));
    pointer(e,{200,130},true);require(items(e,"dimensionRepairReference")==0&&encoded(e)==before,"agganci di altre viste esclusi");
    pointer(e,f.at(0,0),true);require(items(e,"dimensionRepairPreview")==0,"primo riferimento insufficiente");
    key(e,Qt::Key_Escape);require(encoded(e)==before&&items(e,"dimensionRepairReference")==0,"Esc annulla anche riparazione parziale");
    ok(e.beginDimensionRepair(d.id));pointer(e,f.at(0,0),true);pointer(e,f.at(10,6),true);require(items(e,"dimensionRepairPreview")==1,"due agganci danno anteprima");
    key(e,Qt::Key_Backspace);require(items(e,"dimensionRepairPreview")==0&&encoded(e)==before,"Backspace riapre ultimo riferimento senza storico");
    pointer(e,f.at(5,0),true,Qt::ControlModifier);require(items(e,"dimensionRepairPreview")==1,"punto medio con Ctrl");
    pointer(e,{95,65},true);QApplication::processEvents();near(e.dimensionResult(d.id).value,5,"conferma con clic e nuovo punto medio");
    require(e.sheet().drawing.dimensions[0].references[1].role==DrawingPointRole::Midpoint,"tipo di snap persistente");ok(e.undo());require(encoded(e)==before,"due riferimenti riparati in un Undo");
    ok(e.beginDimensionRepair(d.id));e.setReadOnly(true);require(items(e,"dimensionSnap")==0&&!e.findChild<QAction *>("drawingRepairDimensionAction")->isEnabled()&&!e.beginDimensionRepair(d.id).isEmpty()&&encoded(e)==before,"sola lettura annulla lo strumento e impedisce modifiche");
    std::cout<<"PASS doppio aggancio, vista vincolata, Esc, Backspace, snap medio, clic e sola lettura\n";
}
void explicitReferenceAndOrdinate(){
    Fixture f;auto d=f.distanceDimension(DrawingMeasure::OrdinateX);f.add(d);DrawingSheetEditor e(f.sheet,f.model);show(e);const auto before=encoded(e);
    select(e,d.id);e.findChild<QAction *>("drawingRepairSecondReferenceAction")->trigger();pointer(e,f.at(5,0),true,Qt::ControlModifier);key(e,Qt::Key_Return);QApplication::processEvents();near(e.dimensionResult(d.id).value,5,"riferimento singolo su ordinata");ok(e.undo());require(encoded(e)==before,"Undo ordinata");
    ok(e.beginDimensionRepair(d.id,0));e.findChild<QAction *>("drawingOriginReferenceAction")->trigger();key(e,Qt::Key_Return);QApplication::processEvents();near(e.dimensionResult(d.id).value,10,"origine del piano sostituisce primo riferimento");
    require(e.sheet().drawing.dimensions[0].references[0].role==DrawingPointRole::Origin,"origine persistente");
    require(!e.beginDimensionRepair(d.id,2).isEmpty(),"indice inesistente rifiutato");
    std::cout<<"PASS riferimenti singoli, ordinate e origine del piano\n";
}
void curveDimensions(){
    for(auto measure:{DrawingMeasure::Radius,DrawingMeasure::Diameter,DrawingMeasure::SectionLength,DrawingMeasure::EndpointDistance}){
        Fixture f;DrawingDimension d;
        if(measure==DrawingMeasure::Radius||measure==DrawingMeasure::Diameter){for(const auto &curve:f.curves)if(curve.featureId==2&&drawingPrimitive(f.view,curve).circular){auto ref=drawingAttachment(f.view,curve,f.model.snapshot,DrawingPointRole::Curve);d=drawingDimensionBetween(f.view,f.model.snapshot,f.curves,{ref},measure,{40,10});break;}require(d.viewId==f.view.id,"cerchio disponibile");}
        else{const auto ref=f.line();const auto result=resolveDrawingAttachment(f.view,f.model.snapshot,f.curves,ref);d=dimensionOfCurve(f.view,*result.curve,measure);}
        f.add(d);if(d.references.empty())f.sheet.drawing.dimensions[0].section.edgeName="bordo-eliminato";else f.breakReference(0);
        DrawingSheetEditor e(f.sheet,f.model);show(e);const auto before=encoded(e);ok(e.beginDimensionRepair(d.id));
        const bool round=measure==DrawingMeasure::Radius||measure==DrawingMeasure::Diameter;
        if(round){pointer(e,f.at(5,0),true);require(items(e,"dimensionRepairPreview")==0&&encoded(e)==before,"raggio su retta rifiutato");}
        pointer(e,round?f.at(38,0):f.at(4,0),true);require(items(e,"dimensionRepairPreview")==1,"riparazione quota su curva");key(e,Qt::Key_Return);QApplication::processEvents();near(e.dimensionResult(d.id).value,round?(measure==DrawingMeasure::Radius?3:6):10,"misura curva esatta");
        ok(e.undo());require(encoded(e)==before,"Undo quota legacy o radiale");
    }
    Fixture f;auto d=drawingDimensionBetween(f.view,f.model.snapshot,f.curves,{f.line(),f.line(true)},DrawingMeasure::Angle,{5,5});d.angleSides=2;f.add(d);f.breakReference(1);
    DrawingSheetEditor e(f.sheet,f.model);show(e);const auto before=encoded(e);ok(e.beginDimensionRepair(d.id));pointer(e,f.at(5,0),true);require(items(e,"dimensionRepairPreview")==0&&encoded(e)==before,"angolo parallelo rifiutato");pointer(e,f.at(0,3),true);require(items(e,"dimensionRepairPreview")==1,"snap retta angolare");key(e,Qt::Key_Return);QApplication::processEvents();near(e.dimensionResult(d.id).value,90,"angolo riparato");require(e.sheet().drawing.dimensions[0].angleSides==2,"settore angolare conservato");
    std::cout<<"PASS quote radiali, lunghezza/estremi legacy, incompatibili e settore angolare\n";
}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{singleReference();twoReferencesAndCancel();explicitReferenceAndOrdinate();curveDimensions();std::cout<<"PASS ricollegamento quote 2D\n";return 0;}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
