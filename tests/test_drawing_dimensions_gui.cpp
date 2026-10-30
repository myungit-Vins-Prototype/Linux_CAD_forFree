#include "cad_drawing_editor.h"
#include <QEventLoop>
#include <QTemporaryDir>
#include "cad_drawing_text_layout.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sheet.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWindow>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
class CountedCurve final:public Curve<3>{
    CurvePtr<3> basis_;std::shared_ptr<int> evaluations_;
public:
    CountedCurve(CurvePtr<3> basis,std::shared_ptr<int> evaluations):basis_(std::move(basis)),evaluations_(std::move(evaluations)){}
    CurveType type()const override{return CurveType::Other;}
    Interval domain()const override{return basis_->domain();}
    void evaluate(double t,int order,Vec3 *out)const override{++*evaluations_;basis_->evaluate(t,order,out);}
};
void require(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
void near(double a,double b,const char *why,double tolerance=1e-7){require(std::isfinite(a)&&std::abs(a-b)<tolerance,why);}
DrawingSourceBody source(Body b,quint64 id){auto body=std::make_shared<Body>(std::move(b));return {id,body,std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"gui"+std::to_string(id)))};}
QByteArray encoded(const DrawingSheetEditor &editor){QByteArray bytes;ok(encodeDrawingSheet(editor.sheet(),bytes));return bytes;}
QPointF paper(Vec2 p){return QPointF(75+3*p.x(),120-3*p.y());}
void move(DrawingSheetEditor &editor,QPointF p,Qt::KeyboardModifiers modifiers={}){auto *canvas=editor.canvas();const QPointF local=canvas->mapFromScene(p);QMouseEvent event(QEvent::MouseMove,local,canvas->viewport()->mapToGlobal(local.toPoint()),Qt::NoButton,Qt::NoButton,modifiers);QApplication::sendEvent(canvas->viewport(),&event);}
void click(DrawingSheetEditor &editor,QPointF p,Qt::KeyboardModifiers modifiers={}){auto *canvas=editor.canvas();const QPointF local=canvas->mapFromScene(p),global=canvas->viewport()->mapToGlobal(local.toPoint());QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,modifiers),release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,modifiers);QApplication::sendEvent(canvas->viewport(),&press);QApplication::sendEvent(canvas->viewport(),&release);}
void key(DrawingSheetEditor &editor,int code){editor.activateWindow();editor.canvas()->setFocus();QApplication::processEvents();QKeyEvent press(QEvent::KeyPress,code,Qt::NoModifier),release(QEvent::KeyRelease,code,Qt::NoModifier);QApplication::sendEvent(editor.windowHandle(),&press);QApplication::sendEvent(editor.windowHandle(),&release);}
void mode(DrawingSheetEditor &editor,int type){auto *action=editor.findChild<QAction *>("drawingSmartDimensionAction");action->setChecked(false);auto *combo=editor.findChild<QComboBox *>("drawingDimensionType");combo->setCurrentIndex(combo->findData(type));action->setChecked(true);}
int items(DrawingSheetEditor &editor,const QString &kind){int count=0;for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()==kind)++count;return count;}
void select(DrawingSheetEditor &editor,const QUuid &id){auto *tree=editor.findChild<QTreeWidget *>("drawingTree");for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+1).toUuid()==id){tree->setCurrentItem(*it);return;}throw std::runtime_error("elemento albero");}
DrawingDimension last(const DrawingSheetEditor &editor){require(!editor.sheet().drawing.dimensions.empty(),"quota inserita");return editor.sheet().drawing.dimensions.back();}
}
#include "test_drawing_segment_spacing.h"
#include "test_drawing_section_radius.h"
#include "test_drawing_flacone_interaction.h"
#include "test_drawing_navigation_contours.h"
#include "test_drawing_apparent_radii.h"
#include "test_drawing_text_layout.h"
#include "test_drawing_angular_extensions.h"
#include "test_drawing_angular_center.h"
#include "test_drawing_dimension_center.h"
#include "test_drawing_ambiguous_snaps.h"
#include "test_drawing_dimension_series.h"
#include "test_drawing_dimension_numbering.h"
#include "test_drawing_dimension_controls.h"

namespace {
void drawingDimensionAlignmentTests(){
 const auto mouse=[](DrawingSheetEditor &e,QPointF paper,int phase,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
  auto *v=e.canvas();const QPointF local=v->mapFromScene(paper),global=v->viewport()->mapToGlobal(local.toPoint());
  QMouseEvent event(phase==0?QEvent::MouseButtonPress:phase==1?QEvent::MouseMove:QEvent::MouseButtonRelease,local,global,phase==1?Qt::NoButton:Qt::LeftButton,phase==2?Qt::NoButton:Qt::LeftButton,modifiers);QApplication::sendEvent(v->viewport(),&event);
 };
 const auto guide=[](DrawingSheetEditor &e)->QGraphicsPathItem *{for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimensionAlignmentGuide")return dynamic_cast<QGraphicsPathItem *>(i);return nullptr;};
 const auto pause=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
 for(const double zoom:{2.0,5.0}){
  DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeBox(Frame3(),10,6,4),1)};model.choices={{1,"Blocco"}};
  DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;sheet.title="Allineamento grafico delle quote";sheet.style.dimensionHeight=1.5;
  DrawingSheetEditor e(sheet,model);e.resize(1450,950);e.show();QApplication::processEvents();
  DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.scale=2;ok(e.insertView(view,{view.id,"Vista superiore",{35,170},{1}}));
  auto base=dimensionOfCurve(view,e.curves(view.id)[0],DrawingMeasure::SectionLength);
  const auto add=[&](QPointF center){auto d=base;d.id=QUuid::createUuid();ok(e.addDimension(d,center));auto *label=dimensionTextItem(e,d.id);ok(e.moveDimension(d.id,e.sheet().dimensions.back().position+center-label->sceneBoundingRect().center()));return d.id;};
  const auto moving=add({140,100});const auto left=add({130,100+4/zoom});const auto farLeft=add({90,100+2/zoom});
  const auto right=add({180,100+3/zoom});const auto farRight=add({210,100+1/zoom});
  const auto above=add({140+3/zoom,60});const auto below=add({140+4/zoom,140});
  const auto farAbove=add({140+1/zoom,30});const auto farBelow=add({140+1/zoom,175});
  (void)farLeft;(void)farRight;(void)farAbove;(void)farBelow;
  DrawingAnnotation annotation;annotation.position={160,100};annotation.text="Nota";ok(e.addAnnotation(annotation));
  QApplication::processEvents();e.canvas()->setTransform(QTransform::fromScale(zoom,zoom));e.canvas()->centerOn(145,100);
  const auto saved=encoded(e);const auto value=e.dimensionResult(moving).value;
  const auto output=qEnvironmentVariable("FORGECAD_ALIGNMENT_OUTPUT");if(!output.isEmpty())QDir().mkpath(output);QTemporaryDir pdfDirectory;DrawingPdfReport pdfReport;
  const auto pdfBefore=zoom==5&&!output.isEmpty()?output+"/prima.pdf":pdfDirectory.filePath("prima.pdf");ok(e.exportPdf(pdfBefore,pdfReport));
  // La quota nel verso opposto può essere più vicina; il verso decide il semipiano.
  const std::array<QPointF,4> deltas{{{8,0},{-5,0},{0,-8},{0,8}}};const std::array<QUuid,4> expected{{right,left,above,below}};
  for(int direction=0;direction<4;++direction){
   const auto start=dimensionTextItem(e,moving)->sceneBoundingRect().center();const auto raw=start+deltas[std::size_t(direction)];
   mouse(e,start,0);mouse(e,raw,1);auto *line=guide(e);require(line&&!line->path().isEmpty(),"guida di allineamento presente nei quattro versi");
   if(line->data(direction<2?11:12).toUuid()!=expected[std::size_t(direction)])std::cerr<<"alignment zoom="<<zoom<<" direction="<<direction<<" kind="<<line->data(13).toString().toStdString()<<" actual="<<line->data(direction<2?11:12).toUuid().toString().toStdString()<<" expected="<<expected[std::size_t(direction)].toString().toStdString()<<"\n";
   require(line->data(direction<2?11:12).toUuid()==expected[std::size_t(direction)],"bersaglio più vicino nel verso del movimento");
   const auto actual=dimensionTextItem(e,moving)->sceneBoundingRect().center(),target=dimensionTextItem(e,expected[std::size_t(direction)])->sceneBoundingRect().center();
   near(direction<2?actual.y():actual.x(),direction<2?target.y():target.x(),"centri dei testi allineati esattamente",1e-6);
   require(encoded(e)==saved,"anteprima allineamento non modifica documento");
   mouse(e,raw,2);QApplication::processEvents();require(!guide(e),"rilascio elimina guida e timer");near(e.dimensionResult(moving).value,value,"allineamento non cambia valore nominale");
   const auto moved=encoded(e);require(moved!=saved,"rilascio salva la posizione allineata");ok(e.undo());require(encoded(e)==saved,"un solo Undo per allineamento");ok(e.redo());require(encoded(e)==moved,"Redo esatto allineamento");ok(e.undo());
  }
  auto start=dimensionTextItem(e,moving)->sceneBoundingRect().center();mouse(e,start,0);mouse(e,start+QPointF(8,0),1);require(guide(e)->data(11).toUuid()==right,"aggancio destro iniziale");
  mouse(e,start+QPointF(5,0),1);require(guide(e)->data(11).toUuid()==left,"inversione del movimento sceglie il bersaglio a sinistra");
  mouse(e,start+QPointF(8,0),1);require(guide(e)->data(11).toUuid()==right,"nuova inversione torna a destra");
  if(zoom==5){
   ok(e.exportPdf(output.isEmpty()?pdfDirectory.filePath("durante.pdf"):output+"/durante.pdf",pdfReport));require(encoded(e)==saved&&guide(e),"PDF usa il documento salvato senza importare guida o anteprima");
   auto *line=guide(e);require(line->isVisible(),"guida inizialmente visibile");pause(410);require(!line->isVisible(),"guida lampeggiante si spegne");pause(350);require(line->isVisible(),"guida lampeggiante si riaccende");
   const auto output=qEnvironmentVariable("FORGECAD_ALIGNMENT_OUTPUT");if(!output.isEmpty()){QDir().mkpath(output);require(e.grab().save(output+"/allineamento-orizzontale.png"),"cattura guida orizzontale");}
  }
  mouse(e,start+QPointF(8,20/zoom),1);require(guide(e)->path().isEmpty(),"uscita dalla soglia libera l'aggancio");
  mouse(e,start+QPointF(11,0),1,Qt::AltModifier);require(guide(e)->path().isEmpty(),"Alt esclude le guide");
  const auto free=dimensionTextItem(e,moving)->sceneBoundingRect().center();near(free.y(),start.y(),"Alt mantiene movimento libero",.6/zoom);
  QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(e.canvas(),&escape);require(!guide(e)&&encoded(e)==saved,"Esc annulla guida e posizione senza Undo");
  mouse(e,start+QPointF(15,0),1);mouse(e,start+QPointF(15,0),2);require(encoded(e)==saved,"eventi dopo Esc non riprendono il trascinamento annullato");
  start=dimensionTextItem(e,moving)->sceneBoundingRect().center();mouse(e,start,0);mouse(e,start+QPointF(8,0),1);require(guide(e),"guida prima della perdita della presa");dimensionTextItem(e,moving)->ungrabMouse();require(!guide(e)&&encoded(e)==saved,"perdita della presa annulla guida e anteprima");near(QLineF(dimensionTextItem(e,moving)->sceneBoundingRect().center(),start).length(),0,"testo ripristinato dopo perdita presa");mouse(e,start+QPointF(8,0),2);
  // Tutti i riferimenti a testi usano coordinate del foglio, anche con orientamento verticale.
  auto style=e.sheet().style;style.dimensionTextOrientation=DrawingTextOrientation::Aligned;style.dimensionTextPosition=DrawingTextPosition::Above;ok(e.setDrawingStyle(style));
  const auto &curves=e.curves(view.id);const auto edge=std::find_if(curves.begin(),curves.end(),[&](const auto &c){const auto a=view.plane.toLocal(c.curve->point(c.range.lo)),b=view.plane.toLocal(c.curve->point(c.range.hi));return std::abs(a.x()-b.x())<1e-6&&std::abs(a.y()-b.y())>1;});require(edge!=curves.end(),"bordo verticale della fixture");auto rotated=dimensionOfCurve(view,*edge,DrawingMeasure::EndpointDistance);rotated.id=moving;ok(e.updateDimension(rotated));QApplication::processEvents();
  auto *rotatedLabel=dimensionTextItem(e,moving);near(std::abs(rotatedLabel->rotation()),90,"testo della quota effettivamente verticale");const auto movingLayout=std::find_if(e.sheet().dimensions.begin(),e.sheet().dimensions.end(),[&](const auto &d){return d.dimensionId==moving;});const QPointF verticalStart(dimensionTextItem(e,below)->sceneBoundingRect().center().x()+3/zoom,100);ok(e.moveDimension(moving,movingLayout->position+verticalStart-rotatedLabel->sceneBoundingRect().center()));QApplication::processEvents();
  const auto styled=encoded(e);start=dimensionTextItem(e,moving)->sceneBoundingRect().center();mouse(e,start,0);mouse(e,start+QPointF(0,8),1);require(guide(e)&&guide(e)->data(12).toUuid()==below,"guida verticale con testo sopra la linea");
  near(dimensionTextItem(e,moving)->sceneBoundingRect().center().x(),dimensionTextItem(e,below)->sceneBoundingRect().center().x(),"centro visivo con stile sopra linea",1e-6);
  if(zoom==5&&!output.isEmpty())require(e.grab().save(output+"/allineamento-verticale.png"),"cattura guida verticale");
  mouse(e,start+QPointF(0,8),2);ok(e.undo());require(encoded(e)==styled,"Undo conserva stile e riferimenti");
  e.setReadOnly(true);start=dimensionTextItem(e,moving)->sceneBoundingRect().center();mouse(e,start,0);mouse(e,start+QPointF(8,0),1);mouse(e,start+QPointF(8,0),2);require(!guide(e)&&encoded(e)==styled,"sola lettura senza trascinamento o guida");
 }
 std::cout<<"PASS allineamento quote: quattro versi, vicinanza e inversione, zoom, centri visivi, lampeggio, Alt/Esc, Undo/Redo e sola lettura\n";
}
void drawingDimensionArrowAlignmentTests(){
 const auto guide=[](DrawingSheetEditor &e)->QGraphicsPathItem *{for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimensionAlignmentGuide")return dynamic_cast<QGraphicsPathItem *>(i);return nullptr;};
 const auto tips=[](DrawingSheetEditor &e,QUuid id){std::vector<QPointF> points;for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimensionArrow"&&i->data(3).toUuid()==id)points.push_back(i->data(6).toPointF());return points;};
 const auto coincidence=[&](DrawingSheetEditor &e,QUuid first,QUuid second,QPointF expected){
  for(const auto id:{first,second}){double gap=1e9;for(auto point:tips(e,id))gap=std::min(gap,QLineF(point,expected).length());near(gap,0,"punte realmente disegnate coincidenti",1e-6);}
 };
 for(double zoom:{2.,5.})for(int axis:{0,1})for(bool movingAngle:{true,false})for(int quadrant=0;quadrant<4;++quadrant){
  const bool styled=quadrant%2;DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeBox(Frame3(),12,8,6),1)};model.choices={{1,"Blocco"}};
  DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;sheet.title="Snap fra frecce di quota angolare e lineare";
  sheet.style.arrow=DrawingArrowStyle(quadrant);
  if(styled){sheet.style.dimensionTextOrientation=DrawingTextOrientation::Aligned;sheet.style.dimensionTextPosition=DrawingTextPosition::Above;sheet.style.dimensionTextGap=1.2;}
  DrawingSheetEditor e(sheet,model);e.resize(1450,950);e.show();QApplication::processEvents();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=2;const QPointF origin(110,150);ok(e.insertView(v,{v.id,"Lato comune",origin,{1}}));
  const auto &curves=e.curves(v.id);const DrawingCurve *horizontal=nullptr,*vertical=nullptr;
  for(const auto &c:curves){const auto primitive=drawingPrimitive(v,c);if(primitive.kind!=DrawingPrimitive::Kind::Line)continue;const auto a=v.plane.toLocal(c.curve->point(c.range.lo)),b=v.plane.toLocal(c.curve->point(c.range.hi));if(std::abs(a.y())<1e-7&&std::abs(b.y())<1e-7&&std::abs(a.x()-b.x())>1)horizontal=&c;if(std::abs(a.x())<1e-7&&std::abs(b.x())<1e-7&&std::abs(a.y()-b.y())>1)vertical=&c;}
  require(horizontal&&vertical,"due lati incidenti del blocco");
  const auto first=drawingAttachment(v,*horizontal,model.snapshot,DrawingPointRole::Curve),second=drawingAttachment(v,*vertical,model.snapshot,DrawingPointRole::Curve);
  const Vec2 cursor(quadrant&1?-10:10,quadrant&2?-10:10);
  auto angle=drawingDimensionBetween(v,model.snapshot,curves,{first,second},DrawingMeasure::Angle,cursor);
  auto linear=dimensionOfCurve(v,axis?*horizontal:*vertical,DrawingMeasure::EndpointDistance);
  ok(e.addDimension(angle,{145,105}));ok(e.addDimension(linear,{165,135}));QApplication::processEvents();
  if(styled)for(const auto &d:{angle,linear}){auto presentation=std::find_if(e.sheet().dimensions.begin(),e.sheet().dimensions.end(),[&](const auto &p){return p.dimensionId==d.id;})->presentation;presentation.reverseArrows=true;ok(e.updateDimension(d,presentation));}
  QApplication::processEvents();const auto result=e.dimensionResult(angle.id);near(result.value,90,"angolo retto nei quattro settori");
  const auto vertex=origin+v.scale*QPointF(result.vertex.x(),-result.vertex.y());
  const auto direction=axis?(std::abs(result.firstDirection.y())>.9?result.firstDirection:result.secondDirection):(std::abs(result.firstDirection.x())>.9?result.firstDirection:result.secondDirection);
  const QPointF ray(direction.x(),-direction.y());const double startAngle=std::atan2(-result.firstDirection.y(),result.firstDirection.x())-.5*result.sweep;const QPointF radial(std::cos(startAngle),std::sin(startAngle));
  const auto *linearLabel=dimensionTextItem(e,linear.id);const QPointF linearMid=.5*(linearLabel->data(9).toPointF()+linearLabel->data(10).toPointF())-vertex;
  const auto setAnchor=[&](QUuid id,QPointF anchor){auto *label=dimensionTextItem(e,id);const auto stored=std::find_if(e.sheet().dimensions.begin(),e.sheet().dimensions.end(),[&](const auto &p){return p.dimensionId==id;})->position;ok(e.moveDimension(id,stored+anchor-label->data(6).toPointF()));QApplication::processEvents();};
  const double reach=50,initial=reach-14/zoom;const QPointF common=vertex+reach*ray;
  setAnchor(angle.id,vertex+(movingAngle?initial:reach)*radial);setAnchor(linear.id,vertex+(movingAngle?reach:initial)*ray+linearMid);
  const auto moving=movingAngle?angle.id:linear.id,target=movingAngle?linear.id:angle.id;
  e.canvas()->setTransform(QTransform::fromScale(zoom,zoom));e.canvas()->centerOn(vertex);QApplication::processEvents();
  const auto saved=encoded(e);const double nominal=e.dimensionResult(moving).value;auto *label=dimensionTextItem(e,moving);const auto start=label->sceneBoundingRect().center();const QPointF movement=movingAngle?radial:ray,nearby=start+10/zoom*movement;
  const bool qa=zoom==5&&axis==0&&movingAngle&&quadrant==0;const auto output=qEnvironmentVariable("FORGECAD_ARROW_ALIGNMENT_OUTPUT");DrawingPdfReport report;if(qa&&!output.isEmpty()){QDir().mkpath(output);ok(e.exportPdf(output+"/prima.pdf",report));}
  angularMouse(e,start,0);angularMouse(e,nearby,1);auto *mark=guide(e);require(mark&&mark->data(13).toString()=="arrow"&&mark->data(14).toUuid()==target,"snap sulla punta della quota di altro tipo con lato comune");coincidence(e,moving,target,common);require(encoded(e)==saved,"punte coincidenti in anteprima senza modifica al documento");
  const auto centre=label->sceneBoundingRect().center(),other=dimensionTextItem(e,target)->sceneBoundingRect().center();require(std::abs(centre.x()-other.x())*zoom>8&&std::abs(centre.y()-other.y())*zoom>8,"snap delle frecce indipendente dai centri dei testi");
  if(qa&&!output.isEmpty()){ok(e.exportPdf(output+"/durante.pdf",report));require(e.grab().save(output+"/frecce-angolare-lineare.png"),"cattura snap punte coincidenti");}
  const auto preview=label->data(6).toPointF();angularMouse(e,nearby,2);QApplication::processEvents();require(!guide(e),"rilascio elimina indicatore frecce");coincidence(e,moving,target,common);near(QLineF(preview,dimensionTextItem(e,moving)->data(6).toPointF()).length(),0,"nessun salto al rilascio delle frecce");near(e.dimensionResult(moving).value,nominal,"snap frecce conserva la misura");const auto moved=encoded(e);require(moved!=saved,"posizione frecce salvata");ok(e.undo());require(encoded(e)==saved,"un solo Undo ripristina la posizione delle frecce");ok(e.redo());require(encoded(e)==moved,"Redo esatto dello snap delle frecce");
  DrawingSheet restored;ok(decodeDrawingSheet(moved,restored));DrawingSheetEditor reopened(restored,model);coincidence(reopened,moving,target,common);require(encoded(reopened)==moved,"codec esistente riapre le frecce coincidenti");ok(e.undo());QApplication::processEvents();
  // Soglie in pixel, Alt e annullamento devono valere anche per le punte.
  angularMouse(e,start,0);angularMouse(e,nearby,1,Qt::AltModifier);

  require(guide(e)&&guide(e)->path().isEmpty(),"Alt esclude lo snap delle frecce");angularMouse(e,nearby,1);require(guide(e)->data(13).toString()=="arrow"&&!guide(e)->path().isEmpty(),"rientro nello snap frecce");
  angularMouse(e,start+24/zoom*movement,1);require(guide(e)->data(13).toString()=="arrow"&&!guide(e)->path().isEmpty(),"isteresi mantiene le frecce a10pixel");coincidence(e,moving,target,common);
  angularMouse(e,start+31/zoom*movement,1);require(guide(e)->path().isEmpty(),"distacco delle frecce oltre12pixel");angularMouse(e,nearby,1);require(!guide(e)->path().isEmpty(),"nuovo aggancio frecce dopo distacco");
  QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(e.canvas(),&escape);angularMouse(e,nearby,2);require(!guide(e)&&encoded(e)==saved,"Esc annulla snap frecce senza creare Undo");
  if(qa){
   angularMouse(e,start,0);angularMouse(e,nearby,1);dimensionTextItem(e,moving)->ungrabMouse();angularMouse(e,nearby,2);require(!guide(e)&&encoded(e)==saved,"perdita presa annulla lo snap frecce");
   // Una punta solo vicina alla semiretta non deve essere dichiarata coincidente.
   auto presentation=e.sheet().dimensions[1].presentation;presentation.extensionSlant=std::atan(2/(zoom*reach))*180/kPi;ok(e.updateDimension(linear,presentation));QApplication::processEvents();
   auto *current=dimensionTextItem(e,moving);const auto from=current->sceneBoundingRect().center(),to=from+10/zoom*movement;angularMouse(e,from,0);angularMouse(e,to,1);require(guide(e)->path().isEmpty()||guide(e)->data(13).toString()!="arrow","punte non raggiungibili rifiutate anche entro soglia");QApplication::sendEvent(e.canvas(),&escape);angularMouse(e,to,2);
   e.setReadOnly(true);const auto locked=encoded(e);angularMouse(e,from,0);angularMouse(e,to,1);angularMouse(e,to,2);require(!guide(e)&&encoded(e)==locked,"sola lettura protegge anche lo snap delle frecce");
  }
 }
 // Due quote lineari oblique usano gli stessi testimoni e le stesse punte del rendering.
 for(double slant:{-35.,35.}){
  DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeBox(Frame3(),12,8,6),1)};model.choices={{1,"Blocco"}};DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1450,950);e.show();QApplication::processEvents();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=2;ok(e.insertView(v,{v.id,"Richiami obliqui",{70,140},{1}}));
  auto first=dimensionOfCurve(v,e.curves(v.id)[0],DrawingMeasure::EndpointDistance),second=first;second.id=QUuid::createUuid();ok(e.addDimension(first,{100,80}));ok(e.addDimension(second,{130,80}));
  for(const auto &d:{first,second}){auto presentation=e.sheet().dimensions[0].presentation;presentation.extensionSlant=slant;ok(e.updateDimension(d,presentation));}QApplication::processEvents();
  const auto *firstLabel=dimensionTextItem(e,first.id);const QPointF t=firstLabel->data(10).toPointF()-firstLabel->data(9).toPointF();const double length=std::hypot(t.x(),t.y());const QPointF normal(-t.y()/length,t.x()/length);auto *moving=dimensionTextItem(e,first.id),*target=dimensionTextItem(e,second.id);
  const auto stored=e.sheet().dimensions[0].position;ok(e.moveDimension(first.id,stored+target->data(6).toPointF()-moving->data(6).toPointF()-3*normal+15*t/length));QApplication::processEvents();e.canvas()->setTransform(QTransform::fromScale(3,3));e.canvas()->centerOn(110,100);
  const auto before=encoded(e);const auto start=dimensionTextItem(e,first.id)->sceneBoundingRect().center();angularMouse(e,start,0);angularMouse(e,start+normal,1);require(guide(e)&&guide(e)->data(13).toString()=="arrow"&&!guide(e)->path().isEmpty(),"snap punte con richiami obliqui positivi e negativi");double gap=1e9;for(auto a:tips(e,first.id))for(auto b:tips(e,second.id))gap=std::min(gap,QLineF(a,b).length());near(gap,0,"punte oblique coincidenti",1e-6);angularMouse(e,start+normal,2);ok(e.undo());require(encoded(e)==before,"Undo frecce con richiami obliqui");
 }

 // Angoli non retti: la punta sul lato inclinato deve coincidere, senza forzarla su X/Y.
 for(double degrees:{10.,45.})for(bool movingAngle:{true,false}){
  const double rotation=-degrees*kPi/180;DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};
  model.snapshot.bodies={source(makePlaneSheet(Frame3(Vec3(0,5,0),Vec3(0,0,1),Vec3(1,0,0)),5),1),source(makePlaneSheet(Frame3(Vec3(0,8,0),Vec3(0,0,1),Vec3(std::cos(rotation),std::sin(rotation),0)),5),2)};model.choices={{1,"Base"},{2,"Lato inclinato"}};
  DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1450,950);e.show();QApplication::processEvents();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=3;const QPointF origin(95,120);ok(e.insertView(v,{v.id,"Angolo inclinato",origin,{1,2}}));
  const auto reference=[&](quint64 feature){const DrawingCurve *best=nullptr;double y=1e9;for(const auto &c:e.curves(v.id))if(c.featureId==feature){const auto primitive=drawingPrimitive(v,c);if(primitive.kind!=DrawingPrimitive::Kind::Line)continue;const auto a=v.plane.toLocal(c.curve->point(c.range.lo)),b=v.plane.toLocal(c.curve->point(c.range.hi)),u=normalized(b-a);if(std::abs(feature==1?u.x():u.x()*std::cos(rotation)+u.y()*std::sin(rotation))<.99)continue;const double mid=(a.y()+b.y())/2;if(mid<y){y=mid;best=&c;}}require(best,"lato per lo snap inclinato");return best;};
  const auto *base=reference(1),*inclined=reference(2);const auto a=drawingAttachment(v,*base,model.snapshot,DrawingPointRole::Curve),b=drawingAttachment(v,*inclined,model.snapshot,DrawingPointRole::Curve);
  auto angle=drawingDimensionBetween(v,model.snapshot,e.curves(v.id),{a,b},DrawingMeasure::Angle,{0,0}),linear=dimensionOfCurve(v,*base,DrawingMeasure::EndpointDistance);ok(e.addDimension(angle,{130,70}));ok(e.addDimension(linear,{90,75}));
  const auto *lineLabel=dimensionTextItem(e,linear.id);const std::array<QPointF,2> lineEnds{lineLabel->data(9).toPointF(),lineLabel->data(10).toPointF()};const auto paperAt=[&](Vec2 p){return origin+v.scale*QPointF(p.x(),-p.y());};
  QPointF vertex,ray,radial,common;double reach=0;bool found=false;
  for(int sector=0;sector<4&&!found;++sector){angle.angleSides=sector;ok(e.updateDimension(angle));const auto r=e.dimensionResult(angle.id);vertex=paperAt(r.vertex);const auto direction=std::abs(r.firstDirection.y())>.01?r.firstDirection:r.secondDirection;ray={direction.x(),-direction.y()};const double middle=std::atan2(-r.firstDirection.y(),r.firstDirection.x())-.5*r.sweep;radial={std::cos(middle),std::sin(middle)};
   for(auto endpoint:lineEnds){reach=(endpoint.x()-vertex.x())/ray.x();if(reach>12&&reach<100){common=vertex+reach*ray;found=true;break;}}
  }
  require(found,"intersezione della punta lineare con il lato inclinato");QApplication::processEvents();
  const auto setAnchor=[&](QUuid id,QPointF anchor){auto *label=dimensionTextItem(e,id);const auto stored=std::find_if(e.sheet().dimensions.begin(),e.sheet().dimensions.end(),[&](const auto &p){return p.dimensionId==id;})->position;ok(e.moveDimension(id,stored+anchor-label->data(6).toPointF()));QApplication::processEvents();};
  const QPointF normal(0,1);setAnchor(angle.id,vertex+(movingAngle?reach-3:reach)*radial);setAnchor(linear.id,{common.x()+18,common.y()-(movingAngle?0:3)});e.canvas()->setTransform(QTransform::fromScale(3,3));e.canvas()->centerOn(vertex);QApplication::processEvents();const auto moving=movingAngle?angle.id:linear.id,target=movingAngle?linear.id:angle.id;
  const auto stored=encoded(e);const auto nominal=e.dimensionResult(moving).value;const auto start=dimensionTextItem(e,moving)->sceneBoundingRect().center(),to=start+(movingAngle?radial:normal);angularMouse(e,start,0);angularMouse(e,to,1);

  require(guide(e)&&guide(e)->data(13).toString()=="arrow"&&!guide(e)->path().isEmpty(),"snap sul lato inclinato a10/45gradi, trascinando ciascuna quota");coincidence(e,moving,target,common);angularMouse(e,to,2);near(e.dimensionResult(moving).value,nominal,"snap inclinato conserva il valore");ok(e.undo());require(encoded(e)==stored,"Undo snap su lato inclinato");
 }
 std::cout<<"PASS snap frecce: angolare/lineare in entrambi i versi, lato orizzontale/verticale,4settori,lati inclinati10/45gradi,zoom2/5,4simboli,testo ruotato/sopra,frecce invertite,isteresi,Alt/Esc,perdita presa,codec,PDF,Undo/Redo e richiami obliqui\n";
}

void drawingDimensionPlacementAlignmentTests(){
 const auto preview=[](DrawingSheetEditor &e)->QGraphicsItem *{for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimensionPreview")return i;return nullptr;};
 const auto guide=[](DrawingSheetEditor &e)->QGraphicsPathItem *{for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimensionAlignmentGuide")return dynamic_cast<QGraphicsPathItem *>(i);return nullptr;};
 const auto coincident=[&](DrawingSheetEditor &e,QPointF point){double gap=1e9;for(auto *i:e.canvas()->scene()->items())if(i->data(0).toString()=="dimensionArrow"&&i->zValue()==20)gap=std::min(gap,QLineF(i->data(6).toPointF(),point).length());near(gap,0,"punta di anteprima realmente coincidente",1e-6);};
 for(double zoom:{2.,5.})for(int axis:{0,1})for(bool angleNew:{true,false})for(bool styled:{false,true}){
  DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeBox(Frame3(),12,8,6),1)};model.choices={{1,"Blocco"}};DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;sheet.title="Snap nel primo posizionamento";
  if(styled){sheet.style.dimensionTextOrientation=DrawingTextOrientation::Aligned;sheet.style.dimensionTextPosition=DrawingTextPosition::Above;}
  DrawingSheetEditor e(sheet,model);e.resize(1450,950);e.show();QApplication::processEvents();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=2;const QPointF origin(100,150);ok(e.insertView(v,{v.id,"Lato comune",origin,{1}}));const auto at=[&](double x,double y){return origin+v.scale*QPointF(x,-y);};
  const DrawingCurve *horizontal=nullptr,*vertical=nullptr;for(const auto &c:e.curves(v.id)){const auto a=v.plane.toLocal(c.curve->point(c.range.lo)),b=v.plane.toLocal(c.curve->point(c.range.hi));if(std::abs(a.y())<1e-7&&std::abs(b.y())<1e-7&&std::abs(a.x()-b.x())>1)horizontal=&c;if(std::abs(a.x())<1e-7&&std::abs(b.x())<1e-7&&std::abs(a.y()-b.y())>1)vertical=&c;}require(horizontal&&vertical,"lati della fixture inserimento");
  const auto first=drawingAttachment(v,*horizontal,model.snapshot,DrawingPointRole::Curve),second=drawingAttachment(v,*vertical,model.snapshot,DrawingPointRole::Curve);auto angular=drawingDimensionBetween(v,model.snapshot,e.curves(v.id),{first,second},DrawingMeasure::Angle,{20,20});auto linear=dimensionOfCurve(v,axis?*horizontal:*vertical,DrawingMeasure::EndpointDistance);const auto target=angleNew?linear:angular;ok(e.addDimension(target,{150,100}));QApplication::processEvents();
  const double reach=50;const QPointF radial(std::sqrt(.5),-std::sqrt(.5)),ray=axis?QPointF(0,-1):QPointF(1,0),common=origin+reach*ray;
  auto *targetText=dimensionTextItem(e,target.id);const auto targetAnchor=angleNew?common+.5*(targetText->data(9).toPointF()+targetText->data(10).toPointF())-origin:origin+reach*radial;ok(e.moveDimension(target.id,e.sheet().dimensions[0].position+targetAnchor-targetText->data(6).toPointF()));QApplication::processEvents();select(e,target.id);QApplication::processEvents();
  e.canvas()->setTransform(QTransform::fromScale(zoom,zoom));e.canvas()->centerOn(origin);const auto saved=encoded(e);
  const auto output=qEnvironmentVariable("FORGECAD_PLACEMENT_ALIGNMENT_OUTPUT");const bool qa=zoom==5&&!axis&&angleNew&&!styled;DrawingPdfReport report;if(qa&&!output.isEmpty()){QDir().mkpath(output);ok(e.exportPdf(output+"/prima.pdf",report));}
  const auto begin=[&]{mode(e,int(angleNew?DrawingMeasure::Angle:axis?DrawingMeasure::HorizontalDistance:DrawingMeasure::VerticalDistance));if(angleNew){click(e,at(6,0),Qt::ShiftModifier);click(e,at(0,4),Qt::ShiftModifier);}else{click(e,at(0,0));click(e,axis?at(12,0):at(0,8));}};
  begin();const auto direction=angleNew?radial:ray;const auto raw=origin+(reach-4/zoom)*direction;move(e,origin+(reach-14/zoom)*direction);move(e,raw);require(preview(e)&&guide(e)&&guide(e)->data(13).toString()=="arrow"&&guide(e)->data(14).toUuid()==target.id,"snap delle frecce prima del primo clic, anche sul bersaglio selezionato");coincident(e,common);require(encoded(e)==saved,"definizione con snap non modifica il documento");
  if(qa&&!output.isEmpty()){ok(e.exportPdf(output+"/durante.pdf",report));require(e.grab().save(output+"/prima-posizione-frecce.png"),"cattura snap prima della conferma");}
  move(e,raw,Qt::AltModifier);require(preview(e)&&guide(e)->path().isEmpty(),"Alt esclude snap durante la definizione");move(e,raw);coincident(e,common);
  const auto anchor=preview(e)->data(6).toPointF(),visual=preview(e)->sceneBoundingRect().center();click(e,raw);QApplication::processEvents();require(e.sheet().drawing.dimensions.size()==2&&!guide(e),"clic conferma una sola nuova quota ed elimina guida");const auto inserted=last(e);auto *label=dimensionTextItem(e,inserted.id);near(QLineF(anchor,label->data(6).toPointF()).length(),0,"conferma conserva l'ancoraggio agganciato",1e-6);near(QLineF(visual,label->sceneBoundingRect().center()).length(),0,"nessun salto visivo al primo clic",1e-6);near(e.dimensionResult(inserted.id).value,angleNew?90:axis?12:8,"valore della nuova quota invariato");const auto confirmed=encoded(e);ok(e.undo());require(encoded(e)==saved,"un solo Undo annulla l'inserimento agganciato");ok(e.redo());require(encoded(e)==confirmed,"Redo esatto della prima posizione");ok(e.undo());QApplication::processEvents();
  begin();move(e,raw);require(guide(e)&&!guide(e)->path().isEmpty(),"nuova anteprima dopo Undo");key(e,Qt::Key_Escape);require(!guide(e)&&!preview(e)&&encoded(e)==saved,"Esc annulla riferimenti e guida senza inserire");move(e,raw);require(!preview(e),"Esc non riprende la quota cancellata");
  begin();move(e,raw);QEvent leave(QEvent::Leave);QApplication::sendEvent(e.canvas(),&leave);require(!guide(e)&&!preview(e)&&encoded(e)==saved,"uscita dal canvas pulisce guida e anteprima");move(e,raw);require(preview(e)&&guide(e),"rientro ricrea l'anteprima senza riferimenti persi");
  e.setReadOnly(true);require(!guide(e)&&!preview(e)&&encoded(e)==saved,"sola lettura annulla la definizione e tutti gli snap");
 }
 for(double zoom:{2.,5.})for(bool vertical:{false,true}){
  DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeBox(Frame3(),12,8,6),1)};model.choices={{1,"Blocco"}};DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;sheet.style.dimensionHeight=1.5;sheet.style.dimensionOffset=5;DrawingSheetEditor e(sheet,model);e.resize(1450,950);e.show();QApplication::processEvents();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=2;const QPointF origin(70,160);ok(e.insertView(v,{v.id,"Guide del testo",origin,{1}}));const auto at=[&](double x,double y){return origin+v.scale*QPointF(x,-y);};
  auto base=dimensionOfCurve(v,e.curves(v.id)[0],DrawingMeasure::SectionLength);const QPointF baseline=vertical?QPointF(145,152):QPointF(82,100);
  const auto add=[&](QPointF center){auto d=base;d.id=QUuid::createUuid();ok(e.addDimension(d,center));QApplication::processEvents();auto *label=dimensionTextItem(e,d.id);ok(e.moveDimension(d.id,e.sheet().dimensions.back().position+center-label->sceneBoundingRect().center()));QApplication::processEvents();return d.id;};
  const auto positive=add(baseline+(vertical?QPointF(3/zoom,40):QPointF(40,3/zoom))),negative=add(baseline+(vertical?QPointF(3/zoom,-40):QPointF(-40,3/zoom)));e.canvas()->setTransform(QTransform::fromScale(zoom,zoom));e.canvas()->centerOn(110,125);const auto stored=encoded(e);mode(e,int(vertical?DrawingMeasure::VerticalDistance:DrawingMeasure::HorizontalDistance));click(e,at(0,0));click(e,vertical?at(0,8):at(12,0));move(e,baseline,Qt::AltModifier);
  const auto nearby=baseline+(vertical?QPointF(0,5):QPointF(5,0));move(e,nearby);require(guide(e)&&guide(e)->data(13).toString()=="text"&&guide(e)->data(vertical?12:11).toUuid()==positive,"guida direzionale del testo durante la definizione");const auto center=preview(e)->sceneBoundingRect().center(),targetCenter=dimensionTextItem(e,positive)->sceneBoundingRect().center();near(vertical?center.x():center.y(),vertical?targetCenter.x():targetCenter.y(),"centri visivi iniziali allineati",1e-6);
  move(e,baseline+(vertical?QPointF(0,2):QPointF(2,0)));require(guide(e)->data(vertical?12:11).toUuid()==negative,"inversione del verso nella definizione");move(e,nearby);const auto previewAnchor=preview(e)->data(6).toPointF();click(e,nearby);QApplication::processEvents();near(QLineF(dimensionTextItem(e,last(e).id)->data(6).toPointF(),previewAnchor).length(),0,"guida del testo confermata senza salto");ok(e.undo());require(encoded(e)==stored,"inserimento allineato al testo è una sola operazione");
  if(!vertical){
   QApplication::processEvents();auto *target=dimensionTextItem(e,positive);const auto layout=std::find_if(e.sheet().dimensions.begin(),e.sheet().dimensions.end(),[&](const auto &p){return p.dimensionId==positive;});const double minimum=e.sheet().style.dimensionOffset;const QPointF inside(125,origin.y()-minimum+3/zoom);ok(e.moveDimension(positive,layout->position+inside-target->sceneBoundingRect().center()));QApplication::processEvents();const auto minimumSaved=encoded(e);
   mode(e,int(DrawingMeasure::HorizontalDistance));click(e,at(0,0));click(e,at(12,0));const QPointF raw(82,origin.y()-1);move(e,raw,Qt::AltModifier);move(e,raw+QPointF(5,0));require(preview(e)&&!guide(e),"aggancio sotto la distanza minima iniziale rifiutato");near(origin.y()-preview(e)->data(6).toPointF().y(),minimum,"distanza minima iniziale rispettata");click(e,raw+QPointF(5,0));QApplication::processEvents();near(origin.y()-dimensionTextItem(e,last(e).id)->data(6).toPointF().y(),minimum,"conferma rispetta distanza minima");ok(e.undo());require(encoded(e)==minimumSaved,"Undo della posizione con distanza minima");
  }
 }
 std::cout<<"PASS snap prima posizione:16casi frecce angolari/lineari,H/V,zoom2/5,stile;4casi guide testo e inversione;Alt/Esc,uscita canvas,readonly,anteprima/clic identici,PDF e Undo/Redo\n";
}

}

int main(int argc,char **argv){QApplication app(argc,argv);try{
    if(qEnvironmentVariableIsSet("FORGECAD_PLACEMENT_ALIGNMENT_ONLY")){drawingDimensionPlacementAlignmentTests();return 0;}
    if(qEnvironmentVariableIsSet("FORGECAD_ALIGNMENT_ONLY")){drawingDimensionAlignmentTests();drawingDimensionArrowAlignmentTests();drawingDimensionPlacementAlignmentTests();return 0;}
    // Clic reali sul tronco di cono: generatrice e bordo circolare visto di taglio.
    {
        DrawingEditorModel m;m.snapshot.version={QUuid::createUuid(),{}};m.snapshot.bodies={source(makeCone(Frame3(),6,3,10),1)};m.choices={{1,"Tronco di cono"}};DrawingSheet sheet;sheet.drawing.model=m.snapshot.version;DrawingSheetEditor e(sheet,m);e.resize(1450,900);e.show();QApplication::processEvents();e.fitSheet();
        DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=3;v.plane=Frame3({},Vec3(0,-1,0),Vec3(1,0,0));ok(e.insertView(v,{v.id,"Cono",{75,120},{1}}));mode(e,-1);
        click(e,paper({4.5,5}),Qt::ShiftModifier);move(e,paper({15,5}));require(items(e,"dimensionPreview")==1,"anteprima dopo clic singolo su generatrice conica");click(e,paper({15,5}));near(e.dimensionResult(last(e).id).value,10,"altezza conica inserita dal mouse");
        mode(e,-1);click(e,paper({0,0}),Qt::ShiftModifier);move(e,paper({0,-6}));require(items(e,"dimensionPreview")==1,"anteprima diametro bordo conico");click(e,paper({0,-6}));const auto d=last(e);near(e.dimensionResult(d.id).value,12,"diametro base conica inserito dal mouse");require(e.dimensionResult(d.id).diametral,"diametro riconosciuto dal flusso GUI");
        const auto output=qEnvironmentVariable("FORGECAD_CONE_OUTPUT");if(!output.isEmpty())e.grab().save(output+"/cone-mouse-dimensions.png");
    }
    // Nome genealogico condiviso fra cono esterno e foro: i clic scelgono le rette visibili.
    {
        ProfileRegion region;const std::vector<Vec2> points{{6,0},{3,10},{1,10},{2,0},{6,0}};
        for(std::size_t i=1;i<points.size();++i){const auto delta=points[i]-points[i-1];region.outer.segments.push_back({std::make_shared<Line<2>>(points[i-1],delta),{0,norm(delta)},int(i)});}
        auto body=source(makeRevolution(Frame3(),region),1);auto names=std::make_shared<ElementNames>(*body.names);for(auto face:body.body->faces())if(body.body->face(face).surface->type()==SurfaceType::Cone)names->faces[std::size_t(face.index)]={"origine:cono"};body.names=names;
        DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={body};model.choices={{1,"Cono cavo"}};DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;
        DrawingSheetEditor e(sheet,model);e.resize(1450,900);e.show();QApplication::processEvents();e.fitSheet();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.scale=3;v.plane=Frame3({},Vec3(0,-1,0),Vec3(1,0,0));ok(e.insertView(v,{v.id,"Cono cavo",{75,120},{1}}));mode(e,int(DrawingMeasure::Angle));
        click(e,paper({-4.5,5}));click(e,paper({4.5,5}));move(e,paper({0,16}));require(items(e,"dimensionPreview")==1,"anteprima angolo fra due generatrici omonime");click(e,paper({0,16}));
        require(e.sheet().drawing.dimensions.size()==1&&last(e).measure==DrawingMeasure::Angle,"angolo del cono cavo inserito dal mouse");near(e.dimensionResult(last(e).id).value,2*std::atan(.3)*180/kPi,"valore esterno dal mouse");
        const auto output=qEnvironmentVariable("FORGECAD_CONE_OUTPUT");if(!output.isEmpty())e.grab().save(output+"/shared-cone-angle.png");
    }
    // Aprire le opzioni senza modifiche conserva anche basi storiche non
    // rappresentabili con i due decimali del campo (H3,5 -> B2,275).
    {DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;sheet.style.arrowLength=3.5;DrawingSheetEditor legacy(sheet,model);const auto before=encoded(legacy);bool visited=false;
     QTimer::singleShot(0,&legacy,[&]{auto *dialog=legacy.findChild<QDialog *>("drawingOptionsDialog");require(dialog,"opzioni stile storico");visited=true;dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});legacy.findChild<QAction *>("drawingOptionsAction")->trigger();require(visited&&encoded(legacy)==before,"conferma invariata conserva schema8 e base2,275 esatta");}
    drawingDimensionControlsTests();
    drawingDimensionNumberingTests();
    drawingDimensionSeriesTests();
    ambiguousSnapTests();
    const double angle=kPi/6;DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={source(makeBox(Frame3(),10,6,4),1),source(makePlaneSheet(Frame3(Vec3(18,0,0),Vec3(0,0,1),Vec3(std::cos(angle),std::sin(angle),0)),4),2),source(makeCylinder(Frame3(Vec3(35,0,0),Vec3(0,0,1),Vec3(1,0,0)),3,8),3)};model.choices={{1,"Corpo"},{2,"Superficie ruotata"},{3,"Cilindro"}};
    DrawingSheet sheet;sheet.title="Quotatura associativa";sheet.drawing.model=model.snapshot.version;DrawingSheetEditor editor(sheet,model);editor.resize(1450,900);editor.show();QApplication::processEvents();DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.scale=3;ok(editor.insertView(view,{view.id,"Superiore",QPointF(75,120),{1,2,3}}));editor.fitSheet();QApplication::processEvents();editor.markSaved();
    mode(editor,int(DrawingMeasure::Distance));move(editor,paper({0,0})+QPointF(.3,.2));require(items(editor,"dimensionSnap")==1&&!editor.isModified(),"snap visibile senza sporcare il foglio");
    key(editor,Qt::Key_Tab);require(items(editor,"dimensionSnap")==1,"Tab cicla i riferimenti sovrapposti");click(editor,paper({0,0})+QPointF(.3,.2));click(editor,paper({10,6})+QPointF(-.25,.2));move(editor,QPointF(99,70));require(items(editor,"dimensionPreview")==1&&editor.sheet().drawing.dimensions.empty(),"anteprima punto-punto prima del posizionamento");
    click(editor,QPointF(99,70));require(editor.sheet().drawing.dimensions.size()==1,"tre clic inseriscono la quota");auto diagonal=last(editor);near(editor.dimensionResult(diagonal.id).value,std::sqrt(136.),"snap estremi esatti indipendentemente dal pixel");require(diagonal.references.size()==2,"entrambi gli agganci persistenti");
    const auto one=encoded(editor);ok(editor.undo());require(editor.sheet().drawing.dimensions.empty(),"inserimento interattivo come unico undo");ok(editor.redo());require(encoded(editor)==one,"redo conserva agganci e ID");
    // Il nuovo inserimento e' centrato: sposta esplicitamente il valore fuori
    // dagli estremi per conservare la regressione del collegamento esterno.
    QApplication::processEvents();const auto offset=dimensionTextItem(editor,diagonal.id)->data(6).toPointF()-editor.sheet().dimensions[0].position;ok(editor.moveDimension(diagonal.id,QPointF(99,70)-offset));QApplication::processEvents();const auto textCenter=dimensionTextItem(editor,diagonal.id)->sceneBoundingRect().center();
    bool attachedText=false;const auto mask=qgraphicsitem_cast<QGraphicsPathItem *>(dimensionTextItem(editor,diagonal.id,"dimensionTextBackground"))->path();for(auto *item:editor.canvas()->scene()->items())if(auto *line=qgraphicsitem_cast<QGraphicsLineItem *>(item))for(auto end:{line->line().p1(),line->line().p2()})if(QLineF(end,textCenter).length()>0&&mask.contains(end+(textCenter-end)*1e-5))attachedText=true;require(attachedText,"richiamo collegato al bordo del testo trasparente, senza attraversarlo");
    // Punto-retta: Maiusc sceglie la retta invece del suo punto medio.
    mode(editor,int(DrawingMeasure::Distance));click(editor,paper({10,6}));click(editor,paper({3,0}),Qt::ShiftModifier);click(editor,QPointF(125,95));auto perpendicular=last(editor);near(editor.dimensionResult(perpendicular.id).value,6,"quota linea-punto dalla GUI");require(perpendicular.references[1].role==DrawingPointRole::Curve,"Maiusc forza riferimento alla retta");
    mode(editor,int(DrawingMeasure::Distance));click(editor,paper({3,0}),Qt::ShiftModifier);click(editor,paper({10,6}));click(editor,QPointF(140,95));near(editor.dimensionResult(last(editor).id).value,6,"linea scelta prima del punto");
    mode(editor,int(DrawingMeasure::Distance));click(editor,paper({3,0}),Qt::ShiftModifier);click(editor,paper({3,6}),Qt::ShiftModifier);click(editor,QPointF(122,102));near(editor.dimensionResult(last(editor).id).value,6,"distanza fra rette parallele");
    mode(editor,int(DrawingMeasure::HorizontalDistance));click(editor,paper({0,0}));click(editor,paper({10,6}));click(editor,QPointF(90,85));near(editor.dimensionResult(last(editor).id).value,10,"punto-punto orizzontale");
    mode(editor,int(DrawingMeasure::HorizontalDistance));click(editor,paper({0,0}));click(editor,paper({0,6}));click(editor,QPointF(60,90));near(editor.dimensionResult(last(editor).id).value,0,"componente orizzontale nulla senza trasformarla in distanza verticale");
    mode(editor,int(DrawingMeasure::VerticalDistance));click(editor,paper({0,0}));click(editor,paper({10,6}));click(editor,QPointF(60,110));near(editor.dimensionResult(last(editor).id).value,6,"punto-punto verticale");
    mode(editor,int(DrawingMeasure::Distance));click(editor,paper({5,0}),Qt::ControlModifier);click(editor,paper({10,6}));click(editor,QPointF(90,140));auto midpoint=last(editor);near(editor.dimensionResult(midpoint.id).value,std::sqrt(61.),"snap punto medio");require(midpoint.references[0].role==DrawingPointRole::Midpoint,"tipo di snap medio conservato");
    mode(editor,int(DrawingMeasure::Distance));click(editor,paper({3,0}),Qt::AltModifier);click(editor,paper({10,6}));click(editor,QPointF(105,150));auto nearest=last(editor);require(nearest.references[0].role==DrawingPointRole::Nearest,"Alt sceglie il punto piu' vicino");near(editor.dimensionResult(nearest.id).value,std::sqrt(85.),"distanza dal nearest",.15);
    mode(editor,int(DrawingMeasure::Distance));click(editor,paper({35,0}));click(editor,paper({0,0}));click(editor,QPointF(130,155));auto center=last(editor);require(center.references[0].role==DrawingPointRole::Center,"snap centro del cerchio");near(editor.dimensionResult(center.id).value,35,"distanza dal centro");
    mode(editor,int(DrawingMeasure::Distance));click(editor,paper({38,0}));click(editor,paper({0,0}));click(editor,QPointF(130,162));require(last(editor).references[0].role==DrawingPointRole::Quadrant0,"snap quadrante");near(editor.dimensionResult(last(editor).id).value,38,"quadrante esatto");
    mode(editor,int(DrawingMeasure::Radius));click(editor,paper({38,0}));click(editor,QPointF(205,85));auto radius=last(editor);near(editor.dimensionResult(radius.id).value,3,"quota radiale interattiva");
    editor.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(false);select(editor,radius.id);bool properties=false;
    QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingDimensionPropertiesDialog");if(!dialog)return;auto *type=dialog->findChild<QComboBox *>("drawingDimensionMeasure");type->setCurrentIndex(type->findText("Diametro"));dialog->findChild<QSpinBox *>("drawingDimensionDecimals")->setValue(3);properties=true;dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();QTimer::singleShot(100,dialog,&QDialog::reject);});editor.findChild<QAction *>("drawingEditDimensionAction")->trigger();QApplication::processEvents();require(properties,"dialogo proprieta' effettivo");near(editor.dimensionResult(radius.id).value,6,"conversione raggio-diametro");const auto edited=std::find_if(editor.sheet().drawing.dimensions.begin(),editor.sheet().drawing.dimensions.end(),[&](const auto &d){return d.id==radius.id;});require(edited->measure==DrawingMeasure::Diameter&&edited->decimals==3,"tipo e precisione cambiati senza duplicare quota");
    // Asse del piano: il menu non richiede una geometria 3D artificiale.
    mode(editor,int(DrawingMeasure::AxisDiameter));click(editor,paper({10,6}));editor.findChild<QAction *>("drawingAxisYReferenceAction")->trigger();click(editor,QPointF(90,180));near(editor.dimensionResult(last(editor).id).value,20,"diametro rispetto asse Y");
    mode(editor,int(DrawingMeasure::Distance));click(editor,paper({10,6}));editor.findChild<QAction *>("drawingOriginReferenceAction")->trigger();click(editor,QPointF(90,190));near(editor.dimensionResult(last(editor).id).value,std::sqrt(136.),"punto-origine");
    // Angolo fra due corpi, settore interno/supplementare e cambio proprieta'.
    mode(editor,int(DrawingMeasure::Angle));click(editor,paper({3,0}),Qt::ShiftModifier);click(editor,paper({18-std::cos(angle)+4*std::sin(angle),-std::sin(angle)-4*std::cos(angle)}),Qt::ShiftModifier);click(editor,QPointF(100,65));auto angular=last(editor);const auto initial=editor.dimensionResult(angular.id).value;near(std::min(initial,180-initial),30,"angolo fra rette selezionate");
    editor.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(false);select(editor,angular.id);QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingDimensionPropertiesDialog");if(!dialog)return;dialog->findChild<QComboBox *>("drawingDimensionSector")->setCurrentIndex(angular.angleSides^1);dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();QTimer::singleShot(100,dialog,&QDialog::reject);});editor.findChild<QAction *>("drawingEditDimensionAction")->trigger();near(editor.dimensionResult(angular.id).value,180-initial,"settore supplementare");
    const auto beforeEscape=encoded(editor);mode(editor,-1);click(editor,paper({0,0}));key(editor,Qt::Key_Escape);require(items(editor,"dimensionPicked")==0&&encoded(editor)==beforeEscape,"Esc annulla la scelta senza creare uno storico");key(editor,Qt::Key_Escape);require(!editor.findChild<QAction *>("drawingSmartDimensionAction")->isChecked(),"secondo Esc esce dalla quota");
    QByteArray bytes=encoded(editor);DrawingSheet restored;ok(decodeDrawingSheet(bytes,restored));DrawingSheetEditor reopened(restored,model);near(reopened.dimensionResult(diagonal.id).value,std::sqrt(136.),"nuove quote rilette");near(reopened.dimensionResult(radius.id).value,6,"diametro riletto");
    auto changed=model;changed.snapshot.bodies[0]=source(makeBox(Frame3(),14,6,4),1);ok(editor.updateModel(changed));near(editor.dimensionResult(diagonal.id).value,std::sqrt(232.),"agganci aggiornati dopo modifica del modello");require(!editor.canUndo(),"cambio sorgente mantiene barriera locale");
    editor.setReadOnly(true);require(!editor.findChild<QAction *>("drawingSmartDimensionAction")->isEnabled()&&!editor.findChild<QAction *>("drawingEditDimensionAction")->isEnabled()&&!editor.updateDimension(diagonal).isEmpty(),"nuove quote rispettano sola lettura");editor.setReadOnly(false);
    // Uno spostamento del testo non deve richiamare la geometria, anche per quote legacy.
    {auto evaluations=std::make_shared<int>(0);auto countedBody=makeBox(Frame3(),10,6,4);
     for(auto edge:countedBody.edges())countedBody.edge(edge).curve=std::make_shared<CountedCurve>(countedBody.edge(edge).curve,evaluations);
     DrawingEditorModel countedModel;countedModel.snapshot.version=model.snapshot.version;countedModel.snapshot.bodies={source(std::move(countedBody),1)};countedModel.choices={{1,"Corpo strumentato"}};
     DrawingView countedView;countedView.kind=DrawingViewKind::ProjectedEdges;std::vector<DrawingCurve> countedCurves;ok(drawingViewCurves(countedModel.snapshot,countedView,countedCurves));
     auto legacy=dimensionOfCurve(countedView,countedCurves[0],DrawingMeasure::EndpointDistance);DrawingSheet countedSheet;countedSheet.drawing.model=countedModel.snapshot.version;countedSheet.drawing.views={countedView};countedSheet.drawing.dimensions={legacy};countedSheet.views={{countedView.id,"Vista",QPointF(100,100),{1}}};countedSheet.dimensions={{legacy.id,QPointF(80,80)}};
     DrawingSheetEditor cached(countedSheet,countedModel);*evaluations=0;ok(cached.moveDimension(legacy.id,QPointF(82,83)));require(*evaluations==0,"spostare il testo riusa il risultato senza rivalutare il modello");
     cached.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(true);cached.show();QApplication::processEvents();move(cached,QPointF(100,100));require(items(cached,"dimensionSnap")==1,"chiusura dell'editor con snap attivo");}
    drawingDimensionAlignmentTests();
    drawingDimensionArrowAlignmentTests();
    drawingDimensionPlacementAlignmentTests();
    drawingApparentRadiusTests();
    drawingNavigationContourTests();
    drawingFlaconeInteractionTests();
    drawingSegmentSpacingTests();
    drawingTextLayoutTests();
    drawingAngularExtensionTests();
    drawingAngularCenterTests();
    drawingAllDimensionCenterTests();
    drawingSectionRadiusTests();
    const auto preview=qEnvironmentVariable("FORGECAD_DIMENSION_PREVIEW");if(!preview.isEmpty()){QApplication::processEvents();editor.fitSheet();require(editor.grab().save(preview),"anteprima quotatura completa");}
    std::cout<<"PASS mouse snaps, point-point/line-point, radial/angular dimensions, properties, persistence and history\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
