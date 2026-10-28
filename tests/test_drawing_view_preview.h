#pragma once
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoop>
#include <atomic>
#include <QThread>
#include <QThreadPool>
#include <QListWidget>
#include <QProgressBar>
namespace {
void previewWait(const std::function<bool()> &ready,const char *why){QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<8000){QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}require(ready(),why);}
class PreviewCountedCurve final:public Curve<3>{CurvePtr<3> base_;std::shared_ptr<std::atomic<int>> count_;public:PreviewCountedCurve(CurvePtr<3> base,std::shared_ptr<std::atomic<int>> count):base_(std::move(base)),count_(std::move(count)){}CurveType type()const override{return CurveType::Other;}Interval domain()const override{return base_->domain();}void evaluate(double t,int n,Vec3 *out)const override{++*count_;base_->evaluate(t,n,out);}};
void previewGuarded(DrawingSheetEditor &e,const char *action,const std::function<void()> &callback){
 std::exception_ptr failure;QTimer::singleShot(0,&e,[&]{try{callback();}catch(...){failure=std::current_exception();if(auto *d=e.findChild<QDialog *>("drawingViewDialog")){std::cerr<<"Errore dialogo: "<<d->findChild<QLabel *>("drawingViewError")->text().toStdString()<<'\n';d->reject();}}});e.findChild<QAction *>(action)->trigger();if(failure)std::rethrow_exception(failure);
}
void viewDialogPreviewTests(){
 auto count=std::make_shared<std::atomic<int>>(0);auto shape=std::make_shared<Body>(makeBox(Frame3(),12,8,6));for(auto edge:shape->edges())shape->edge(edge).curve=std::make_shared<PreviewCountedCurve>(shape->edge(edge).curve,count);
 auto names=std::make_shared<ElementNames>(propagateNames(*shape,makeHistory({},*shape),{},"anteprima"));DrawingEditorModel model{{{QUuid::createUuid(),{}},{{1,shape,names}}},{{1,"Corpo"}},{}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1250,900);e.show();QApplication::processEvents();const auto before=encoded(e);bool inspected=false;
 previewGuarded(e,"drawingAddProjectionAction",[&]{auto *d=e.findChild<QDialog *>("drawingViewDialog");require(d,"dialogo nuova vista");auto *v=d->findChild<QGraphicsView *>("drawingViewPreview");auto *plane=d->findChild<QComboBox *>("drawingViewPlane");auto *scale=d->findChild<QDoubleSpinBox *>("drawingViewScale");require(v&&v->isVisible()&&v->height()>=160,"anteprima sottostante visibile");
  const auto bounds=[&]{return v->property("previewBounds").toRectF();};const auto ready=[&]{return v->property("previewReady").toBool();};
  auto *scaleSource=d->findChild<QComboBox *>("drawingViewScaleSource");require(scaleSource,"sorgente scala disponibile");scaleSource->setCurrentIndex(scaleSource->findData(int(DrawingScaleSource::Custom)));
  plane->setCurrentIndex(0);scale->setValue(2);previewWait(ready,"anteprima superiore pronta");require(std::abs(bounds().width()-24)<1e-6&&std::abs(bounds().height()-16)<1e-6,"superiore12x8 alla scala2");require(encoded(e)==before&&!e.canUndo(),"anteprima fuori da documento e Undo");
  const auto generation=v->property("previewGeneration").toULongLong();plane->setCurrentIndex(1);require(!ready()&&v->scene()->items().empty(),"vecchia immagine rimossa al cambio piano");plane->setCurrentIndex(2);plane->setCurrentIndex(1);previewWait(ready,"ultima richiesta frontale pronta");require(v->property("previewGeneration").toULongLong()>generation&&std::abs(bounds().height()-12)<1e-6,"nessun risultato obsoleto dopo selezioni rapide");
  auto *bodies=d->findChild<QListWidget *>("drawingViewBodies");bodies->item(0)->setCheckState(Qt::Unchecked);require(!ready()&&v->scene()->items().empty()&&!d->findChild<QLabel *>("drawingViewError")->text().isEmpty(),"selezione vuota senza vecchia anteprima");bodies->item(0)->setCheckState(Qt::Checked);plane->setCurrentIndex(0);previewWait(ready,"anteprima ripristinata");
  const auto output=qEnvironmentVariable("FORGECAD_VIEW_DIALOG_OUTPUT");if(!output.isEmpty()){QDir().mkpath(output);require(d->grab().save(output+"/dialogo-vista.png"),"QA dialogo vista");}
  *count=0;inspected=true;confirm(d);
 });require(inspected&&e.hasViewPlacement()&&count->load()==0,"conferma riusa geometria pronta senza rivalutare il corpo");require(encoded(e)==before&&!e.canUndo(),"conferma avvia solo anteprima sul foglio");e.cancelViewPlacement();
 // Chiudere durante il worker non applica niente e il task non accede ai widget.
 previewGuarded(e,"drawingAddProjectionAction",[&]{auto *d=e.findChild<QDialog *>("drawingViewDialog");require(d,"dialogo annullamento");auto *plane=d->findChild<QComboBox *>("drawingViewPlane");plane->setCurrentIndex(6);confirm(d);d->reject();});require(!e.hasViewPlacement()&&encoded(e)==before,"chiusura durante calcolo atomica");require(QThreadPool::globalInstance()->waitForDone(8000),"worker annullato termina senza callback sui widget");
 // CurveType::Other e' solo una sonda del contatore: planeRoots richiede i tipi reali per le sezioni.
 auto sectionModel=model;auto clean=std::make_shared<Body>(makeBox(Frame3(),12,8,6));sectionModel.snapshot.bodies[0]={1,clean,std::make_shared<ElementNames>(propagateNames(*clean,makeHistory({},*clean),{},"anteprima"))};ok(e.updateModel(sectionModel));
 // La sezione segue posizione e stile, e rende esplicito il piano vuoto.
 previewGuarded(e,"drawingAddViewAction",[&]{auto *d=e.findChild<QDialog *>("drawingViewDialog");require(d,"dialogo sezione");auto *v=d->findChild<QGraphicsView *>("drawingViewPreview");auto *depth=d->findChild<QDoubleSpinBox *>("drawingViewDepth");depth->setValue(3);previewWait([&]{return v->property("previewReady").toBool();},"anteprima sezione");auto *style=d->findChild<QComboBox *>("drawingSectionStyle");style->setCurrentIndex(1);previewWait([&]{return v->property("previewReady").toBool();},"anteprima sezione completa");
  style->setCurrentIndex(0);depth->setValue(30);previewWait([&]{return !v->property("previewBusy").toBool();},"piano esterno risolto");require(!v->property("previewReady").toBool()&&v->scene()->items().empty()&&d->findChild<QLabel *>("drawingViewError")->text().contains("non interseca"),"sezione vuota dichiarata senza immagine precedente");d->reject();});require(encoded(e)==before,"annullamento sezione non cambia foglio");
 // Selezione di un corpo fra due e sostituzione della sorgente mentre il dialogo e' aperto.
 auto multi=model;auto second=std::make_shared<Body>(makeBox(Frame3(Vec3(20,0,0),Vec3(0,0,1),Vec3(1,0,0)),4,4,4));multi.snapshot.bodies.push_back({2,second,std::make_shared<ElementNames>(propagateNames(*second,makeHistory({},*second),{},"secondo"))});multi.choices.push_back({2,"Secondo"});DrawingSheetEditor selected(sheet,multi);
 previewGuarded(selected,"drawingAddProjectionAction",[&]{auto *d=selected.findChild<QDialog *>("drawingViewDialog");require(d,"dialogo corpi");auto *v=d->findChild<QGraphicsView *>("drawingViewPreview");d->findChild<QComboBox *>("drawingViewPlane")->setCurrentIndex(0);auto *list=d->findChild<QListWidget *>("drawingViewBodies");list->item(0)->setCheckState(Qt::Unchecked);previewWait([&]{return v->property("previewReady").toBool();},"corpo selezionato pronto");require(v->property("previewBodyCount").toInt()==1&&std::abs(v->property("previewBounds").toRectF().width()-4)<1e-6,"solo il corpo spuntato entra nell'anteprima");
  auto changed=multi;auto replacement=std::make_shared<Body>(makeBox(Frame3(Vec3(20,0,0),Vec3(0,0,1),Vec3(1,0,0)),9,4,4));changed.snapshot.bodies[1]={2,replacement,std::make_shared<ElementNames>(propagateNames(*replacement,makeHistory({},*replacement),{},"secondo"))};ok(selected.updateModel(changed));previewWait([&]{return v->property("previewReady").toBool()&&std::abs(v->property("previewBounds").toRectF().width()-9)<1e-6;},"la sorgente aggiornata sostituisce la vecchia anteprima");selected.setReadOnly(true);previewWait([&]{return !v->property("previewReady").toBool();},"sola lettura invalida anteprima");require(!d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(),"conferma protetta dopo check-in");d->reject();});require(selected.sheet().drawing.views.empty()&&!selected.canUndo(),"nessuna vista applicata dopo annullamento o cambio accesso");
 std::cout<<"PASS dialog preview: piani/corpi/scala/sezioni, cancellazione, richieste obsolete, riuso geometria e Undo\n";
}
}
