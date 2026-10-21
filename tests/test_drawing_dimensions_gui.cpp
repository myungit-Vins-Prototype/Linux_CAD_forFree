#include "cad_drawing_editor.h"
#include "fk_primitives.h"
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
int main(int argc,char **argv){QApplication app(argc,argv);try{
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
    bool attachedText=false;for(auto *item:editor.canvas()->scene()->items())if(auto *line=qgraphicsitem_cast<QGraphicsLineItem *>(item))if(QLineF(line->line().p1(),textCenter).length()<1e-7||QLineF(line->line().p2(),textCenter).length()<1e-7)attachedText=true;require(attachedText,"testo esterno collegato alla linea di quota");
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
