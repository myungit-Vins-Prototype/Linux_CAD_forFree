#include "cad_drawing_editor.h"
#include "cad_drawing_derived.h"
#include "cad_drawing_numbering.h"
#include "fk_primitives.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
void require(bool yes,const char *message){if(!yes)throw std::runtime_error(message);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
void near(double a,double b,const char *why){require(std::abs(a-b)<1e-9,why);}
QByteArray encoded(const DrawingSheetEditor &e){QByteArray data;ok(encodeDrawingSheet(e.sheet(),data));return data;}
const DrawingView &view(const DrawingSheet &s,QUuid id){for(const auto &v:s.drawing.views)if(v.id==id)return v;throw std::runtime_error("vista assente");}
const DrawingViewLayout &layout(const DrawingSheet &s,QUuid id){for(const auto &v:s.views)if(v.viewId==id)return v;throw std::runtime_error("layout assente");}
void pageScale(DrawingSheetEditor &e,double scale){ok(e.setPageSettings(e.sheet().title,e.sheet().size,e.sheet().titleBlock,scale));}
QRectF paperBounds(DrawingSheetEditor &e,QUuid id){QRectF box;bool first=true;for(auto *item:e.canvas()->scene()->items())
 if(item->data(0).toString()=="projection"&&item->data(1).toUuid()==id)if(auto *path=dynamic_cast<QGraphicsPathItem *>(item)){
  box=first?path->path().boundingRect():box.united(path->path().boundingRect());first=false;
 }require(!first,"contorni sul foglio assenti");return box;}
void select(DrawingSheetEditor &e,QUuid id){auto *tree=e.findChild<QTreeWidget *>("drawingTree");require(tree,"albero viste");for(QTreeWidgetItemIterator it(tree);*it;++it){
 if((*it)->data(0,Qt::UserRole).toString()=="view"&&(*it)->data(0,Qt::UserRole+1).toUuid()==id){tree->setCurrentItem(*it);return;}}throw std::runtime_error("selezione vista");}
void image(QWidget *widget,const QString &name){const auto dir=qEnvironmentVariable("FORGECAD_SCALE_QA_DIR");if(!dir.isEmpty()){require(QDir().mkpath(dir),"directory QA");require(widget->grab().save(dir+"/"+name+".png"),"cattura QA");}}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{
 auto body=std::make_shared<Body>(makeBox(Frame3(),10,6,4));auto names=std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"scale"));
 DrawingEditorModel model{{{QUuid::createUuid(),{}},{{1,body,names}}},{{1,"Blocco 10 × 6 × 4"}},{}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1200,850);e.show();QApplication::processEvents();
 DrawingView root;root.kind=DrawingViewKind::ProjectedEdges;root.scale=2;ok(e.insertView(root,{root.id,"Madre",{55,65},{1}}));
 DrawingView other=root;other.id=QUuid::createUuid();other.scale=3;ok(e.insertView(other,{other.id,"Indipendente",{180,65},{1}}));
 const auto add=[&](QUuid parent,DrawingDerivedKind kind,const QString &title){DrawingView child;DrawingViewLayout l{child.id,title,{100,130},{1}};l.relation.parentId=parent;l.relation.kind=kind;l.relation.aligned=false;
  if(kind==DrawingDerivedKind::Section){l.relation.cutStart={0,-2};l.relation.cutEnd={10,8};l.relation.label="S";child.sectionStyle=DrawingSectionStyle::Full;}
  if(kind==DrawingDerivedKind::Detail){l.relation.detailCenter={5,3};l.relation.detailRadius=2;l.relation.label="D";child.scale=4;}
  if(kind==DrawingDerivedKind::Auxiliary)l.relation.auxiliaryAngle=35;
  ok(e.insertView(child,l));return child.id;};
 const auto child=add(root.id,DrawingDerivedKind::Orthographic,"Figlia"),grand=add(child,DrawingDerivedKind::Orthographic,"Nipote");
 const auto section=add(root.id,DrawingDerivedKind::Section,"Sezione"),detail=add(root.id,DrawingDerivedKind::Detail,"Dettaglio"),auxiliary=add(root.id,DrawingDerivedKind::Auxiliary,"Ausiliaria");
 const auto legacy=encoded(e);DrawingSheet restored;ok(decodeDrawingSheet(legacy,restored));QByteArray same;ok(encodeDrawingSheet(restored,same));require(same==legacy&&!drawingUsesSheetScale(restored),"codec storico invariato");near(drawingSheetScale(restored),2,"scala comune storica");
 auto pieces=e.curves(root.id);std::size_t edge=0;while(edge<pieces.size()&&std::abs(distance(pieces[edge].curve->point(pieces[edge].range.lo),pieces[edge].curve->point(pieces[edge].range.hi))-10)>1e-8)++edge;require(edge<pieces.size(),"bordo 10 mm");
 auto dimension=dimensionOfCurve(view(e.sheet(),root.id),pieces[edge],DrawingMeasure::EndpointDistance);ok(e.addDimension(dimension,{55,40}));
 ok(e.setShadedFacesVisible(root.id,true));pageScale(e,.5);near(e.sheet().scale,.5,"scala foglio");for(auto id:{root.id,child,grand,section,auxiliary})near(view(e.sheet(),id).scale,.5,"propagazione scala foglio");near(view(e.sheet(),detail).scale,4,"dettaglio storico indipendente");near(view(e.sheet(),other.id).scale,3,"vista storica a scala diversa conservata");
 near(paperBounds(e,root.id).width(),5,"larghezza effettiva sul foglio");near(paperBounds(e,root.id).height(),3,"altezza effettiva sul foglio");near(e.dimensionResult(dimension.id).value,10,"quota modello immutata");
 const auto atHalf=encoded(e);pageScale(e,.25);ok(e.undo());require(encoded(e)==atHalf,"scala foglio un singolo Undo");ok(e.redo());near(view(e.sheet(),grand).scale,.25,"Redo propaga scala");
 ok(e.setViewScale(child,DrawingScaleSource::Custom,1.25));near(view(e.sheet(),grand).scale,1.25,"nipote eredita scala personalizzata madre");pageScale(e,.2);near(view(e.sheet(),child).scale,1.25,"scala personalizzata stabile");
 ok(e.setViewScale(child,DrawingScaleSource::Sheet,1.25));ok(e.setViewScale(root.id,DrawingScaleSource::Custom,4));near(view(e.sheet(),child).scale,.2,"figlia foglio indipendente da scala madre");near(view(e.sheet(),grand).scale,.2,"catena figlia foglio");
 ok(e.setViewScale(child,DrawingScaleSource::Parent,1.25));near(view(e.sheet(),grand).scale,4,"catena eredita madre");near(*layout(e.sheet(),child).customScale,1.25,"valore personalizzato conservato");
 for(auto id:{section,detail,auxiliary}){ok(e.setViewScale(id,DrawingScaleSource::Sheet,2.5));near(view(e.sheet(),id).scale,.2,"sezione dettaglio ausiliaria scala foglio");ok(e.setViewScale(id,DrawingScaleSource::Parent,2.5));near(view(e.sheet(),id).scale,4,"sezione dettaglio ausiliaria scala madre");ok(e.setViewScale(id,DrawingScaleSource::Custom,2.5));near(view(e.sheet(),id).scale,2.5,"sezione dettaglio ausiliaria personalizzata");}
 const auto beforeDetach=view(e.sheet(),child).scale;ok(e.detachView(child));near(view(e.sheet(),child).scale,beforeDetach,"sgancio conserva scala");require(drawingViewScaleSource(layout(e.sheet(),child))==DrawingScaleSource::Custom,"sgancio converte madre in personalizzata");
 auto numbered=e.sheet();numbered.dimensionNumbering.enabled=true;freezeDrawingNumbering(numbered);auto numbering=numbered.dimensionNumbering;numbering.enabled=true;ok(e.setDimensionNumbering(numbering));const auto valid=encoded(e);auto json=QJsonDocument::fromJson(valid).object();require(json["schema"].toInt()==23&&json.contains("numberingAssignments"),"schema scala e badge persistenti");ok(decodeDrawingSheet(valid,restored));require(restored.dimensionNumbering.persistent&&restored.scale==.2&&layout(restored,detail).scaleSource==DrawingScaleSource::Custom,"persistenza scala e numerazione");
 for(double scale:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})require(!e.setPageSettings(e.sheet().title,e.sheet().size,e.sheet().titleBlock,scale).isEmpty()&&encoded(e)==valid,"scala foglio invalida rifiutata atomicamente");
 require(!e.setViewScale(root.id,DrawingScaleSource::Parent,1).isEmpty()&&encoded(e)==valid,"madre vietata alle viste indipendenti");require(!e.setViewScale(root.id,DrawingScaleSource(99),1).isEmpty()&&encoded(e)==valid,"sorgente invalida rifiutata");require(!e.setViewScale(root.id,DrawingScaleSource::Custom,0).isEmpty()&&encoded(e)==valid,"scala personalizzata invalida");
 const auto rejects=[&](QJsonObject broken){const auto previous=restored.scale;require(!decodeDrawingSheet(QJsonDocument(broken).toJson(),restored).isEmpty()&&restored.scale==previous,"codec invalido atomico");};
 auto broken=json;broken["sheetScale"]="1:2";rejects(broken);broken=json;broken.remove("sheetScale");rejects(broken);broken=json;broken["schema"]=22;rejects(broken);
 for(QJsonValue mode:{QJsonValue(3),QJsonValue(.5),QJsonValue("foglio"),QJsonValue()}){broken=json;auto views=broken["views"].toArray();auto first=views[0].toObject();first["scaleSource"]=mode;views[0]=first;broken["views"]=views;rejects(broken);}
 broken=json;auto views=broken["views"].toArray();auto first=views[0].toObject();first["customScale"]=0;views[0]=first;broken["views"]=views;rejects(broken);
 e.setReadOnly(true);require(!e.setViewScale(root.id,DrawingScaleSource::Sheet,1).isEmpty()&&!e.setPageSettings("vietato",e.sheet().size,e.sheet().titleBlock,1).isEmpty()&&encoded(e)==valid,"sola lettura");e.setReadOnly(false);
 // Dialogo pagina reale: scala e rapporto leggibili, conferma e annulla.
 bool pageSeen=false;QTimer::singleShot(0,[&]{auto *d=e.findChild<QDialog *>("drawingPageDialog");if(!d)return;pageSeen=true;auto *scale=d->findChild<QDoubleSpinBox *>("drawingSheetScale");require(scale,"campo scala foglio");scale->setValue(.5);require(d->findChild<QLabel *>("drawingSheetScaleRatio")->text()=="1:2","rapporto leggibile");image(d,"sheet-scale");d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});e.findChild<QAction *>("drawingPageSettingsAction")->trigger();require(pageSeen&&e.sheet().scale==.5,"dialogo scala applicato");ok(e.undo());require(encoded(e)==valid,"Undo dialogo pagina");
 // Proprietà della sezione e del dettaglio espongono tutte le sorgenti.
 for(auto id:{section,detail}){select(e,id);bool seen=false;QTimer::singleShot(0,[&]{auto *d=e.findChild<QDialog *>(id==detail?"drawingDetailDialog":"drawingDerivedPropertiesDialog");if(!d)return;seen=true;auto *mode=d->findChild<QComboBox *>("drawingViewScaleSource");require(mode&&mode->count()==3,"tre sorgenti per vista figlia");mode->setCurrentIndex(mode->findData(int(DrawingScaleSource::Parent)));auto *custom=d->findChild<QDoubleSpinBox *>(id==detail?"drawingDetailScale":"drawingViewScale");require(custom&&!custom->isEnabled(),"valore personalizzato disabilitato quando ereditato");image(d,id==detail?"detail-scale":"section-scale");d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});e.findChild<QAction *>("drawingEditViewAction")->trigger();require(seen&&drawingViewScaleSource(layout(e.sheet(),id))==DrawingScaleSource::Parent,"dialogo figlia salva sorgente");near(view(e.sheet(),id).scale,4,"dialogo figlia eredita scala");}
 // Dialogo della madre con anteprima: nessuna opzione madre e scala originale esatta.
 select(e,root.id);bool configured=false,rootSeen=false;QTimer timer;timer.setInterval(15);QObject::connect(&timer,&QTimer::timeout,[&]{auto *d=e.findChild<QDialog *>("drawingViewDialog");if(!d)return;auto *mode=d->findChild<QComboBox *>("drawingViewScaleSource");if(!configured){require(mode&&mode->count()==2,"due sorgenti per vista indipendente");mode->setCurrentIndex(mode->findData(int(DrawingScaleSource::Sheet)));configured=true;}auto *preview=d->findChild<QGraphicsView *>("drawingViewPreview");if(preview&&preview->property("previewReady").toBool()){rootSeen=true;image(d,"root-scale");d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();}});timer.start();QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,[&]{if(auto *d=e.findChild<QDialog *>("drawingViewDialog"))d->reject();});timeout.start(10000);e.findChild<QAction *>("drawingEditViewAction")->trigger();timer.stop();timeout.stop();require(rootSeen&&view(e.sheet(),root.id).scale==.2,"dialogo madre e anteprima applicano scala foglio");
 DrawingSheetEditor second(sheet,model);pageScale(second,3);require(e.sheet().scale==.2&&second.sheet().scale==3,"scale indipendenti fra fogli");
 DrawingSheet legacyOne;legacyOne.drawing.model=model.snapshot.version;DrawingSheetEditor one(legacyOne,model);DrawingView prior=root;prior.id=QUuid::createUuid();prior.scale=1.23456789;ok(one.insertView(prior,{prior.id,"Storica",{50,50},{1}}));
 select(one,prior.id);bool preserved=false;QTimer keep;keep.setInterval(15);QObject::connect(&keep,&QTimer::timeout,[&]{auto *d=one.findChild<QDialog *>("drawingViewDialog");if(!d)return;auto *preview=d->findChild<QGraphicsView *>("drawingViewPreview");if(preview&&preview->property("previewReady").toBool()){preserved=true;d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();}});keep.start();QTimer stop;stop.setSingleShot(true);QObject::connect(&stop,&QTimer::timeout,[&]{if(auto *d=one.findChild<QDialog *>("drawingViewDialog"))d->reject();});stop.start(10000);one.findChild<QAction *>("drawingEditViewAction")->trigger();keep.stop();stop.stop();require(preserved&&!drawingUsesSheetScale(one.sheet())&&view(one.sheet(),prior.id).scale==prior.scale,"conferma vista storica conserva schema e precisione");
 pageScale(one,1);near(view(one.sheet(),prior.id).scale,1,"prima assegnazione esplicita 1:1");
 require(drawingScaleText(.5)=="1:2"&&drawingScaleText(2)=="2:1","notazione scala");e.fitSheet();QApplication::processEvents();image(&e,"sheet-final");
 const auto dir=qEnvironmentVariable("FORGECAD_SCALE_QA_DIR");if(!dir.isEmpty()){DrawingPdfReport report;ok(e.exportPdf(dir+"/sheet-scale.pdf",report));}
 std::cout<<"PASS sheet/view/parent scales, geometry and dimensions, legacy codec, schema23/badges, Undo/read-only, section/detail/preview dialogs\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
