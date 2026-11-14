#include "cad_drawing_editor.h"
#include "cad_drawing_local_section_depth.h"
#include "fk_primitives.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QPainterPathStroker>
#include <QPushButton>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
void need(bool b, const char *message) { if (!b) throw std::runtime_error(message); }
void ok(const QString &error) { if (!error.isEmpty()) throw std::runtime_error(error.toStdString()); }
void near(double a, double b, const char *message) { need(std::abs(a-b)<1e-6, message); }
void events() { QApplication::processEvents(); QApplication::processEvents(); }
QByteArray bytes(const DrawingSheet &sheet) { QByteArray result; ok(encodeDrawingSheet(sheet,result)); return result; }
DrawingSourceBody body(quint64 id, Vec3 origin) {
    auto solid=std::make_shared<Body>(makeBox(Frame3(origin,Vec3(0,0,1),Vec3(1,0,0)),10,8,16));
    return {id,solid,std::make_shared<ElementNames>(propagateNames(*solid,makeHistory({},*solid),{},"depth"+std::to_string(id)))};
}
DrawingEditorModel model() {
    DrawingEditorModel result;
    result.snapshot.version={QUuid::createUuid(),{}};
    result.snapshot.bodies={body(1,{}),body(2,{100,0,0})};
    result.choices={{1,"Blocco"},{2,"Altro blocco"}};
    return result;
}
DrawingSheet blank(const DrawingEditorModel &model) {
    DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;return sheet;
}
QGraphicsLineItem *line(QGraphicsScene *scene,const char *kind) {
    for(auto *item:scene->items())if(item->data(0).toString()==QLatin1String(kind))
        return dynamic_cast<QGraphicsLineItem *>(item);
    return nullptr;
}
QPointF projected(const DrawingView &view,Vec3 point) {
    const auto p=view.plane.toLocal(point);return {p.x()*view.scale,-p.y()*view.scale};
}
QPointF click(QGraphicsView *view,QPointF scene,Qt::KeyboardModifiers modifiers = {}) {
    const QPoint local=view->mapFromScene(scene);const auto actual=view->mapToScene(local);
    const QPointF global=view->viewport()->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(local),global,Qt::LeftButton,Qt::LeftButton,modifiers);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(local),global,Qt::LeftButton,Qt::NoButton,modifiers);
    QApplication::sendEvent(view->viewport(),&press);QApplication::sendEvent(view->viewport(),&release);events();
    return actual;
}
QGraphicsItem *snapItem(QGraphicsScene *scene) {
    for(auto *item:scene->items())if(item->data(0).toString()=="localSectionDepthSnap")return item;
    return nullptr;
}
QPointF hover(QGraphicsView *view,QPointF scene,Qt::KeyboardModifiers modifiers = {}) {
    const QPoint local=view->mapFromScene(scene);
    QMouseEvent event(QEvent::MouseMove,QPointF(local),QPointF(view->viewport()->mapToGlobal(local)),Qt::NoButton,Qt::NoButton,modifiers);
    QApplication::sendEvent(view->viewport(),&event);events();return view->mapToScene(local);
}
void tab(QGraphicsView *view) {
    QKeyEvent event(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(view,&event);events();
}
void escape(DrawingSheetEditor &editor) {
    QKeyEvent event(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
    QApplication::sendEvent(editor.canvas(),&event);events();
}
void geometry() {
    DrawingView target;target.kind=DrawingViewKind::ProjectedEdges;
    target.plane=Frame3(Vec3(17,-22,9),normalized(Vec3(1,2,3)),normalized(Vec3(2,-1,0)));
    const auto x=target.plane.xDir(),y=target.plane.yDir(),z=target.plane.zDir();
    for(const double angle:{0.,.53,1.5707963267948966})for(const double scale:{.4,1.,2.5})for(const double sign:{-1.,1.}) {
        DrawingLocalSectionDepthView ref;
        ref.view.kind=DrawingViewKind::ProjectedEdges;ref.view.scale=scale;
        const auto rx=sign*(std::cos(angle)*x+std::sin(angle)*z);
        const auto ry=-std::sin(angle)*x+std::cos(angle)*z;
        ref.view.plane=Frame3::fromAxes(target.plane.origin()+3*x+4*y+8*z,rx,ry,cross(rx,ry));
        ref.profile.addRect(-10,-20,30,40);
        const auto trace=drawingLocalSectionDepthTrace(target,ref,16,7,{{0,0},{10,0},{10,8},{0,8}});
        for(const auto &p:{trace.zero.p1(),trace.zero.p2()})near(drawingLocalSectionDepthAt(target,ref,p),16,"zero sul piano iniziale in frame ruotati e invertiti");
        for(const auto &p:{trace.cut.p1(),trace.cut.p2()})near(drawingLocalSectionDepthAt(target,ref,p),9,"taglio al piano front-depth con scale diverse");
        near(trace.dimension.length(),7*scale,"distanza disegnata in scala reale della vista di riferimento");
        const auto picked=projected(ref.view,target.plane.toGlobal(Vec3(5,3,11)));
        near(drawingLocalSectionDepthAt(target,ref,picked),11,"clic misura la coordinata lungo la normale della vista sezionata");
        near(drawingLocalSectionDepthAt(target,ref,trace.band[0]),16,"inizio fascia sullo zero");
        near(drawingLocalSectionDepthAt(target,ref,trace.band[2]),9,"fine fascia sul taglio");
    }
    DrawingLocalSectionDepthView parallel;parallel.view=target;parallel.profile.addRect(0,0,10,10);
    bool rejected=false;try{drawingLocalSectionDepthTrace(target,parallel,16,7,{});}catch(const std::invalid_argument &){rejected=true;}
    need(rejected,"vista parallela rifiutata, senza una falsa rappresentazione della profondita'");
}
void selection() {
    const auto m=model();auto sheet=blank(m);
    DrawingView target;target.kind=DrawingViewKind::ProjectedEdges;
    DrawingView side=target;side.id=QUuid::createUuid();side.plane=Frame3({},Vec3(0,-1,0),Vec3(1,0,0));
    auto parallel=target;parallel.id=QUuid::createUuid();auto foreign=side;foreign.id=QUuid::createUuid();
    auto detail=side;detail.id=QUuid::createUuid();auto broken=side;broken.id=QUuid::createUuid();
    sheet.drawing.views={target,parallel,foreign,detail,broken,side};
    sheet.views={{target.id,"Sezionata",{},{1}}, {parallel.id,"Parallela",{},{1}},
                 {foreign.id,"Altro corpo",{},{2}}, {detail.id,"Dettaglio",{},{1}},
                 {broken.id,"Interrotta",{},{1}}, {side.id,"Ortogonale",{},{1}}};
    sheet.views[3].relation.kind=DrawingDerivedKind::Detail;
    sheet.views[4].interruption=DrawingViewBreak{false,2,4,6};
    int profiles=0;const auto profile=[&](const QUuid &id){++profiles;need(id==side.id,"solo la vista compatibile richiede il profilo");QPainterPath p;p.addRect(0,-16,10,16);return p;};
    auto refs=drawingLocalSectionDepthViews(sheet,target,m.snapshot,profile);
    need(refs.size()==1&&refs[0].view.id==side.id&&refs[0].onSheet&&profiles==1,"scelta soltanto di ortogonali dello stesso corpo non interrotte/non dettagli");
    sheet.drawing.views={target};sheet.views.resize(1);
    refs=drawingLocalSectionDepthViews(sheet,target,m.snapshot,[](const QUuid &){throw std::runtime_error("nessun profilo da richiedere");return QPainterPath();});
    need(refs.size()==1&&!refs[0].onSheet&&!refs[0].profile.isEmpty(),"senza ortogonale esistente si genera solo l'anteprima d'appoggio");
    need(refs[0].profile.boundingRect().width()<11,"anteprima limitata alle sorgenti selezionate, non a tutto il modello");
    near(std::abs(dot(refs[0].view.plane.zDir(),target.plane.zDir())),0,"anteprima d'appoggio perpendicolare");
    auto curved=m.snapshot;
    auto cylinder=std::make_shared<Body>(makeCylinder(Frame3(),4,16));
    curved.bodies={{1,cylinder,std::make_shared<ElementNames>(propagateNames(*cylinder,makeHistory({},*cylinder),{},"cylinder"))}};
    const auto curvedRefs=drawingLocalSectionDepthViews(sheet,target,curved,[](const QUuid &){return QPainterPath();});
    QPainterPathStroker stroke;stroke.setWidth(.1);
    need(stroke.createStroke(curvedRefs[0].profile).contains({-4,-8}),"anteprima del cilindro include la generatrice apparente oltre ai bordi B-rep");
    // L'ordine delle sorgenti non cambia la compatibilita'.
    sheet.drawing.views.push_back(side);sheet.views[0].features={1,2};sheet.views.append({side.id,"Doppia",{},{2,1}});
    refs=drawingLocalSectionDepthViews(sheet,target,m.snapshot,[](const QUuid &){QPainterPath p;p.addRect(0,0,10,16);return p;});
    need(refs.size()==1&&refs[0].onSheet,"stesse sorgenti in ordine diverso riconosciute");
}
void snapGeometry() {
    DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;
    view.plane=Frame3(Vec3(13,-7,11),normalized(Vec3(2,3,1)),normalized(Vec3(3,-2,0)));
    for(double scale:{.5,1.,3.}) {
        view.scale=scale;
        DrawingCurve edge;edge.featureId=1;edge.kind=DrawingCurveKind::ProjectedEdge;
        edge.curve=std::make_shared<Line<3>>(view.plane.toGlobal(Vec3(0,0,5)),normalized(view.plane.xDir()+view.plane.yDir()));
        edge.range={0,10*std::sqrt(2.)};
        const auto cache=drawingLocalSectionSnapGeometry(view,{edge});
        const auto first=projected(view,edge.curve->point(0)),last=projected(view,edge.curve->point(edge.range.hi));
        for(const auto &anchor:{first,last,.5*(first+last)}) {
            const auto points=drawingLocalSectionSnapChoices(*cache,view,anchor+QPointF(.01,.01),20);
            need(!points.empty()&&QLineF(points[0].paper,anchor).length()<1e-8,"estremi e punto medio esatti a scale diverse");
        }
        const auto cursor=QPointF(2.1*scale,-2.3*scale);
        const auto points=drawingLocalSectionSnapChoices(*cache,view,cursor,20);
        need(!points.empty()&&points[0].label=="Bordo","snap sul segmento obliquo tra i punti notevoli");
        near(points[0].paper.x(),2.2*scale,"proiezione esatta del cursore sul bordo obliquo X");
        near(points[0].paper.y(),-2.2*scale,"proiezione esatta del cursore sul bordo obliquo Y");
        need(drawingLocalSectionSnapChoices(*cache,view,first+QPointF(.1,.1),1000).empty(),"raggio snap espresso in pixel, non millimetri fissi");
        need(drawingLocalSectionSnapChoices(*cache,view,first,20,[](std::size_t,QPointF,bool){return false;}).empty(),"geometrie non mostrate escluse dallo snap");
        DrawingCurve circle;circle.featureId=2;circle.kind=DrawingCurveKind::ProjectedEdge;
        circle.curve=std::make_shared<Circle<3>>(view.plane.toGlobal(Vec3(0,0,3)),view.plane.xDir(),view.plane.yDir(),10);
        circle.range={0,kTwoPi};const auto circular=drawingLocalSectionSnapGeometry(view,{circle});
        const auto center=drawingLocalSectionSnapChoices(*circular,view,{.01,.01},20);
        need(!center.empty()&&center[0].label=="Centro"&&QLineF(center[0].paper,{}).length()<1e-8,"centro esatto della conica, indipendente dalla quota del piano");
        const auto quadrant=drawingLocalSectionSnapChoices(*circular,view,{10*scale+.01,.01},20);
        need(!quadrant.empty()&&quadrant[0].label=="Quadrante","quadrante del cerchio senza falsa giunzione alla cucitura");
        const double angle=.7;
        const auto curved=drawingLocalSectionSnapChoices(*circular,view,{10.01*scale*std::cos(angle),-10.01*scale*std::sin(angle)},20);
        need(!curved.empty()&&curved[0].label=="Bordo","snap esatto sull'arco tra i quadranti");
        near(curved[0].paper.x(),10*scale*std::cos(angle),"X sul cerchio esatto");
        near(curved[0].paper.y(),-10*scale*std::sin(angle),"Y sul cerchio esatto");
        circle.range={.2,1.1};const auto arc=drawingLocalSectionSnapGeometry(view,{circle});
        need(std::none_of(arc->points.begin(),arc->points.end(),[](const auto &p){return p.label=="Quadrante";}),"quadranti esterni all'arco rifilato non disponibili");
    }
}
void snapInteraction() {
    const auto m=model();DrawingView top;top.kind=DrawingViewKind::ProjectedEdges;
    top.localSection=DrawingLocalSection{{{0,0},{6,0},{6,8},{0,8}},16,4};
    DrawingView side;side.kind=DrawingViewKind::ProjectedEdges;side.scale=1.5;
    side.plane=Frame3({},Vec3(0,-1,0),Vec3(1,0,0));const QPointF position(145,100);
    DrawingSheetEditor editor(blank(m),m);editor.resize(1300,900);editor.show();
    ok(editor.insertView(top,{top.id,"Vista sezionata",{70,75},{1}}));
    ok(editor.insertView(side,{side.id,"Ortogonale",position,{1}}));editor.fitSheet();events();
    const auto baseline=bytes(editor.sheet());const auto exact=editor.curves(top.id)[0].curve.get();
    ok(editor.beginLocalSection(top.id));events();auto *panel=editor.findChild<QDialog *>("drawingLocalSectionDialog");
    auto *front=panel->findChild<QDoubleSpinBox *>("drawingLocalSectionFront");auto *depth=panel->findChild<QDoubleSpinBox *>("drawingLocalSectionDepth");
    auto *preview=panel->findChild<QGraphicsView *>("drawingLocalSectionDepthPreview");
    front->setValue(14.123);
    panel->findChild<QPushButton *>("drawingLocalSectionPickZero")->click();
    const auto endpoint=position+projected(side,Vec3(0,0,16));
    const auto offset=QPointF(2,2)/editor.canvas()->transform().m11();
    const auto raw=hover(editor.canvas(),endpoint+offset);
    auto *marker=snapItem(editor.canvas()->scene());
    need(marker&&marker->data(2).toString()=="Estremo"&&QLineF(marker->scenePos(),endpoint).length()<1e-7,"indicatore di estremo prima del clic sulla tavola");
    click(editor.canvas(),raw);near(front->value(),16,"zero sul vertice esatto anche se il clic e' vicino");
    panel->findChild<QPushButton *>("drawingLocalSectionPickCut")->click();
    const auto middle=position+projected(side,Vec3(0,0,8));hover(editor.canvas(),middle+offset);
    marker=snapItem(editor.canvas()->scene());need(marker&&marker->data(2).toString()=="Punto medio","indicatore di punto medio sulla tavola");
    click(editor.canvas(),middle+offset);near(depth->value(),8,"profondita' ricavata dal punto medio esatto");
    front->setValue(14.123);panel->findChild<QPushButton *>("drawingLocalSectionPickZero")->click();
    const auto miniEnd=projected(side,Vec3(10,0,16));const auto miniOffset=QPointF(2,2)/preview->transform().m11();
    hover(preview,miniEnd+miniOffset);marker=snapItem(preview->scene());
    need(marker&&marker->data(2).toString()=="Estremo","snap nell'anteprima dello stesso riferimento");
    const auto output=qEnvironmentVariable("FORGECAD_DEPTH_SNAP_OUTPUT");
    if(!output.isEmpty())need(panel->grab().save(output),"cattura indicatore snap");
    click(preview,miniEnd+miniOffset);near(front->value(),16,"zero esatto dall'anteprima");
    panel->findChild<QPushButton *>("drawingLocalSectionPickZero")->click();
    const auto free=hover(preview,miniEnd+miniOffset,Qt::AltModifier);
    need(!snapItem(preview->scene()),"Alt nasconde lo snap nell'anteprima");
    DrawingLocalSectionDepthView ref{side,editor.sheet().views[1],{}};
    click(preview,free,Qt::AltModifier);near(front->value(),drawingLocalSectionDepthAt(top,ref,free),"Alt mantiene il punto libero senza agganci nascosti");
    // Con zoom basso piu' punti entrano nello stesso raggio: Tab e clic devono scegliere lo stesso.
    editor.canvas()->resetTransform();editor.canvas()->scale(.7,.7);events();
    panel->findChild<QPushButton *>("drawingLocalSectionPickZero")->click();hover(editor.canvas(),endpoint);
    marker=snapItem(editor.canvas()->scene());need(marker,"snap prima del ciclo");const auto before=marker->scenePos();
    tab(editor.canvas());marker=snapItem(editor.canvas()->scene());need(marker&&QLineF(marker->scenePos(),before).length()>1e-6,"Tab cambia lo snap tra i punti vicini");
    const auto chosen=marker->data(3).toDouble();click(editor.canvas(),endpoint);near(front->value(),chosen,"clic conferma lo snap selezionato con Tab");
    need(bytes(editor.sheet())==baseline&&editor.curves(top.id)[0].curve.get()==exact,"snap non modifica il documento o ricalcola la sezione");
    panel->findChild<QPushButton *>("drawingLocalSectionPickZero")->click();hover(preview,miniEnd+miniOffset);
    QEvent leave(QEvent::Leave);QApplication::sendEvent(preview->viewport(),&leave);need(!snapItem(preview->scene()),"uscire dall'anteprima elimina lo snap");
    escape(editor);need(!snapItem(editor.canvas()->scene())&&bytes(editor.sheet())==baseline,"Esc pulisce snap e guide senza modifiche");
    DrawingSheetEditor fallback(blank(m),m);fallback.resize(1300,900);fallback.show();
    ok(fallback.insertView(top,{top.id,"Unica",{70,75},{1}}));ok(fallback.beginLocalSection(top.id));events();
    panel=fallback.findChild<QDialog *>("drawingLocalSectionDialog");preview=panel->findChild<QGraphicsView *>("drawingLocalSectionDepthPreview");
    panel->findChild<QPushButton *>("drawingLocalSectionPickCut")->click();
    const auto point=QPointF(0,-8)+QPointF(2,1)/preview->transform().m11();hover(preview,point);
    marker=snapItem(preview->scene());need(marker&&marker->data(2).toString()=="Punto medio","snap anche nella vista ortogonale d'appoggio");
    click(preview,point);near(panel->findChild<QDoubleSpinBox *>("drawingLocalSectionDepth")->value(),8,"taglio sul punto medio della vista d'appoggio");
    escape(fallback);
}
void interaction() {
    const auto m=model();DrawingView top;top.kind=DrawingViewKind::ProjectedEdges;
    top.localSection=DrawingLocalSection{{{0,0},{6,0},{6,8},{0,8}},16,4};
    DrawingView side;side.kind=DrawingViewKind::ProjectedEdges;side.scale=1.5;
    side.plane=Frame3({},Vec3(0,-1,0),Vec3(1,0,0));
    DrawingView second=side;second.id=QUuid::createUuid();second.plane=Frame3({},Vec3(1,0,0),Vec3(0,1,0));second.scale=.75;
    DrawingSheetEditor editor(blank(m),m);editor.resize(1300,900);editor.show();
    ok(editor.insertView(top,{top.id,"Vista sezionata",{70,75},{1}}));
    const QPointF sidePosition(145,100),secondPosition(220,100);
    ok(editor.insertView(side,{side.id,"Vista ortogonale",sidePosition,{1}}));
    ok(editor.insertView(second,{second.id,"Altra ortogonale",secondPosition,{1}}));
    const auto curve=std::find_if(editor.curves(top.id).begin(),editor.curves(top.id).end(),[](const auto &c){return c.kind==DrawingCurveKind::Section;});
    need(curve!=editor.curves(top.id).end(),"curva di taglio per una quota associata");
    const auto dimension=dimensionOfCurve(top,*curve);ok(editor.addDimension(dimension,{75,55}));
    const auto baseline=bytes(editor.sheet());const auto exact=editor.curves(top.id)[0].curve.get();
    editor.fitSheet();events();ok(editor.beginLocalSection(top.id));events();
    QPointer<QDialog> panel=editor.findChild<QDialog *>("drawingLocalSectionDialog");need(panel&&panel->isVisible(),"pannello della sezione aperto");
    auto *selector=panel->findChild<QComboBox *>("drawingLocalSectionDepthView");
    auto *front=panel->findChild<QDoubleSpinBox *>("drawingLocalSectionFront");
    auto *depth=panel->findChild<QDoubleSpinBox *>("drawingLocalSectionDepth");
    auto *preview=panel->findChild<QGraphicsView *>("drawingLocalSectionDepthPreview");
    need(selector&&selector->count()==2&&selector->currentData().toUuid()==side.id,"selettore delle ortogonali corrispondenti");
    const auto *values=panel->findChild<QLabel *>("drawingLocalSectionDepthValues");
    need(values&&values->text().contains("16.00")&&values->isVisibleTo(panel)&&values->height()>=30,"coordinate dello zero e del taglio leggibili nel pannello");
    need(values->geometry().top()>preview->geometry().bottom(),"riepilogo dello zero e del taglio fuori dal riquadro grafico");
    auto *zero=line(editor.canvas()->scene(),"localSectionZeroPlane");auto *cut=line(editor.canvas()->scene(),"localSectionCutPlane");
    need(zero&&cut&&zero->acceptedMouseButtons()==Qt::NoButton,"guide visibili e non quotabili sulla tavola");
    DrawingLocalSectionDepthView ref{side,editor.sheet().views[1],{}};
    near(drawingLocalSectionDepthAt(top,ref,zero->line().p1()),16,"zero visuale al piano iniziale effettivo");
    near(drawingLocalSectionDepthAt(top,ref,cut->line().p1()),12,"taglio visuale alla profondita' effettiva");
    front->setValue(17);depth->setValue(6);events();cut=line(editor.canvas()->scene(),"localSectionCutPlane");
    near(drawingLocalSectionDepthAt(top,ref,cut->line().p1()),11,"aggiornamento immediato cambiando i campi");
    need(bytes(editor.sheet())==baseline&&editor.curves(top.id)[0].curve.get()==exact,"anteprima senza storia o ricalcolo delle curve esatte");
    selector->setCurrentIndex(1);events();
    need(line(editor.canvas()->scene(),"localSectionCutPlane")->data(1).toUuid()==second.id,"cambio vista sostituisce le guide");
    selector->setCurrentIndex(0);events();
    panel->findChild<QPushButton *>("drawingLocalSectionPickCut")->click();
    const auto point=click(editor.canvas(),sidePosition+projected(side,Vec3(4,3,9)),Qt::AltModifier);
    near(depth->value(),17-drawingLocalSectionDepthAt(top,ref,point-sidePosition),"clic sulla tavola imposta la profondita' nello stesso riferimento");
    panel->findChild<QPushButton *>("drawingLocalSectionPickZero")->click();
    const auto mini=click(preview,projected(side,Vec3(4,3,15)),Qt::AltModifier);
    near(front->value(),drawingLocalSectionDepthAt(top,ref,mini),"clic sull'anteprima imposta lo zero");
    const auto beforeWrong=depth->value();auto *pickCut=panel->findChild<QPushButton *>("drawingLocalSectionPickCut");pickCut->click();
    click(preview,projected(side,Vec3(4,3,front->value()+2)));
    near(depth->value(),beforeWrong,"taglio esterno allo zero non accettato");need(pickCut->isChecked(),"scelta resta attiva dopo punto non valido");
    need(bytes(editor.sheet())==baseline,"clic sulle guide non aggiungono vertici e non modificano il foglio");
    escape(editor);need(!line(editor.canvas()->scene(),"localSectionCutPlane")&&bytes(editor.sheet())==baseline,"Esc rimuove tutte le guide e conserva il disegno");
    ok(editor.beginLocalSection(top.id));events();panel=editor.findChild<QDialog *>("drawingLocalSectionDialog");
    depth=panel->findChild<QDoubleSpinBox *>("drawingLocalSectionDepth");depth->setValue(7);
    panel->findChild<QPushButton *>("drawingLocalSectionApply")->click();events();
    near(editor.sheet().drawing.views[0].localSection->depth,7,"Applica conferma il piano mostrato");
    const auto &outline=editor.sheet().drawing.views[0].localSection->outline;
    need(outline.size()==top.localSection->outline.size()&&std::equal(outline.begin(),outline.end(),top.localSection->outline.begin(),[](Vec2 a,Vec2 b){return distance(a,b)==0;}),"contorno conservato dopo le scelte sulle guide");
    need(!line(editor.canvas()->scene(),"localSectionCutPlane")&&editor.dimensionResult(dimension.id).status==DrawingDimensionStatus::Ready,"guide eliminate alla conferma e quota conservata");
    ok(editor.undo());need(bytes(editor.sheet())==baseline,"un solo Undo ripristina la modifica");
    ok(editor.beginLocalSection(top.id));events();panel=editor.findChild<QDialog *>("drawingLocalSectionDialog");
    const auto output=qEnvironmentVariable("FORGECAD_DEPTH_GUIDE_OUTPUT");
    if(!output.isEmpty()){need(panel->grab().save(output+"-pannello.png"),"cattura pannello");need(editor.grab().save(output+"-tavola.png"),"cattura tavola");}
    escape(editor);
    DrawingSheetEditor single(blank(m),m);ok(single.insertView(top,{top.id,"Unica vista",{70,75},{1}}));
    const auto before=bytes(single.sheet());ok(single.beginLocalSection(top.id));events();
    auto *fallback=single.findChild<QComboBox *>("drawingLocalSectionDepthView");
    auto *miniature=single.findChild<QGraphicsView *>("drawingLocalSectionDepthPreview");
    need(fallback&&fallback->count()==1&&line(miniature->scene(),"localSectionCutPlane")&&!line(single.canvas()->scene(),"localSectionCutPlane"),"senza ortogonale le guide stanno solo nell'anteprima d'appoggio");
    need(single.sheet().drawing.views.size()==1&&bytes(single.sheet())==before,"nessuna vista aggiunta alla tavola per l'anteprima");
    escape(single);
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try {geometry();selection();snapGeometry();snapInteraction();interaction();std::cout<<"PASS profondita' locale: piani/scale/frame, viste compatibili e appoggio, guide/clic/annullamento, quote e Undo\n";}
    catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
