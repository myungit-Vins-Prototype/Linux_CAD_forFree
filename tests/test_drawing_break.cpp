#include "cad_drawing_editor.h"
#include "cad_drawing_break.h"
#include "fk_primitives.h"
#include "fk_curve_algo.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
void require(bool value,const char *why){if(!value)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
void near(double a,double b,const char *why){if(std::abs(a-b)>1e-6)throw std::runtime_error(std::string(why)+": "+std::to_string(a)+" != "+std::to_string(b));}
QByteArray bytes(const DrawingSheet &sheet){QByteArray result;ok(encodeDrawingSheet(sheet,result));return result;}
DrawingEditorModel model(double width=100,double height=10){auto solid=std::make_shared<Body>(makeBox(Frame3(),width,height,8));DrawingEditorModel m;m.snapshot.version={QUuid::createUuid(),{}};m.snapshot.bodies={{1,solid,std::make_shared<ElementNames>(propagateNames(*solid,makeHistory({},*solid),{},"brokenBox"))}};m.choices={{1,"Barra"}};return m;}
QRectF paths(const DrawingSheetEditor &editor,const QUuid &id){QRectF box;for(auto *item:editor.canvas()->scene()->items())if(item->data(1).toUuid()==id&&item->data(0).toString()=="projection"){const auto b=item->sceneBoundingRect();box=box.isNull()?b:box.united(b);}return box;}
void select(DrawingSheetEditor &editor,const QUuid &id){editor.canvas()->scene()->clearSelection();for(auto *item:editor.canvas()->scene()->items())if(item->data(1).toUuid()==id&&item->flags().testFlag(QGraphicsItem::ItemIsSelectable)){item->setSelected(true);break;}QApplication::processEvents();}
void finiteScene(const DrawingSheetEditor &editor){for(auto *item:editor.canvas()->scene()->items()){const auto b=item->sceneBoundingRect();require(std::isfinite(b.x())&&std::isfinite(b.y())&&std::isfinite(b.width())&&std::isfinite(b.height()),"grafica della tavola finita");}}
void dialogCommand(DrawingSheetEditor &editor,QAction *action,const std::function<void(QDialog *)> &callback){
    QTimer timer;QObject::connect(&timer,&QTimer::timeout,&editor,[&]{auto *dialog=editor.findChild<QDialog *>("drawingBreakViewDialog");if(!dialog)return;timer.stop();callback(dialog);});timer.start(10);action->trigger();require(!timer.isActive(),"comando ha aperto il dialogo");
}
void mappings(){
    DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;
    for(double scale:{.5,1.,2.})for(bool vertical:{false,true}){
        view.scale=scale;DrawingViewLayout layout;layout.interruption=DrawingViewBreak{vertical,30,70,6};ok(validateDrawingBreak(view,layout));
        const auto point=[&](double axis){return vertical?QPointF(4,axis):QPointF(axis,4);};
        const auto paper=drawingBreakToPaper(view,layout,point(100));near(vertical?-paper.y():paper.x(),60*scale+6,"riduzione grafica con distacco costante");
        const auto inverse=drawingBreakFromPaper(view,layout,paper);near(vertical?inverse.y():inverse.x(),100,"inversa conserva coordinate modello");
        require(!std::isfinite(drawingBreakToPaper(view,layout,point(50)).x()),"aggancio nel tratto rimosso non mostrabile");
        const auto gapPoint=vertical?QPointF(4*scale,-30*scale-1):QPointF(30*scale+1,-4*scale);const auto clamped=drawingBreakFromPaper(view,layout,gapPoint);near(vertical?clamped.y():clamped.x(),30,"inversa del distacco aggancia bordo vicino");
        QPainterPath line;line.moveTo(0,0);line.lineTo(vertical?QPointF(0,-100*scale):QPointF(100*scale,0));const auto cut=drawingBreakPath(line,view,layout);require(cut.toSubpathPolygons().size()==2,"nessun segmento attraverso il distacco");near(vertical?cut.boundingRect().height():cut.boundingRect().width(),60*scale+6,"polilinea ritagliata");
    }
}
void editorTests(){
    auto source=model();DrawingSheet sheet;sheet.drawing.model=source.snapshot.version;DrawingSheetEditor editor(sheet,source);editor.resize(1200,820);editor.show();QApplication::processEvents();
    DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;DrawingViewLayout layout{view.id,"Barra interrotta",{60,95},{1}};layout.showMesh=true;layout.showHiddenLines=true;ok(editor.insertView(view,layout));
    const DrawingCurve *longEdge=nullptr;for(const auto &curve:editor.curves(view.id))if(std::abs(arcLength(*curve.curve,curve.range)-100)<1e-7){longEdge=&curve;break;}require(longEdge,"bordo lungo proiettato");
    const auto dimension=dimensionOfCurve(view,*longEdge,DrawingMeasure::EndpointDistance);const auto length=dimensionOfCurve(view,*longEdge);ok(editor.addDimension(dimension,{75,110}));ok(editor.addDimension(length,{75,120}));near(editor.dimensionResult(dimension.id).value,100,"misura originale");
    DrawingAxis axis;axis.viewId=view.id;axis.references={drawingAttachment(view,*longEdge,source.snapshot,DrawingPointRole::Start),drawingAttachment(view,*longEdge,source.snapshot,DrawingPointRole::End)};ok(editor.addAxis(axis));
    DrawingAnnotation note;note.viewId=view.id;note.text="Aggancio rimosso";note.position={150,60};note.reference=drawingAttachment(view,*longEdge,source.snapshot,DrawingPointRole::Midpoint);ok(editor.addAnnotation(note));
    const auto original=bytes(editor.sheet());const auto before=paths(editor,view.id);ok(editor.setViewBreak(view.id,DrawingViewBreak{false,30,70,6}));
    near(before.width()-paths(editor,view.id).width(),34,"vista accorciata di 34mm");near(editor.dimensionResult(dimension.id).value,100,"quota completa resta 100");near(editor.dimensionResult(length.id).value,100,"lunghezza resta 100 con aggancio nascosto");
    bool marks=false,missing=false,shading=false,axisSplit=false;for(auto *item:editor.canvas()->scene()->items()){
        if(item->data(0).toString()=="axis"){const auto *path=dynamic_cast<QGraphicsPathItem *>(item);axisSplit=path&&path->path().toSubpathPolygons().size()==2;}
        if(item->data(0).toString()=="viewBreak"){marks=true;require(!item->flags().testFlag(QGraphicsItem::ItemIsSelectable),"segni non quotabili");}
        if(item->data(0).toString()=="dimension"&&item->data(3).toUuid()==length.id){auto *label=dynamic_cast<QGraphicsSimpleTextItem *>(item);missing=label&&label->brush().color()==QColor("#c42b32")&&item->toolTip().contains("interruzione");}
        if(item->data(0).toString()=="viewShading"){shading=true;const auto *image=dynamic_cast<QGraphicsPixmapItem *>(item);require(image,"immagine ombreggiata");const auto gap=layout.position+QPointF(33,-5);const auto local=image->mapFromScene(gap);require(qAlpha(image->pixmap().toImage().pixel(int(local.x()),int(local.y())))==0,"distacco ombreggiato trasparente");}
    }require(axisSplit,"asse manuale interrotto");require(editor.annotationResult(note.id).status==DrawingDimensionStatus::Missing,"richiamo nascosto segnalato");require(marks&&missing&&shading,"simboli, quota da verificare e ombreggiatura");finiteScene(editor);
    select(editor,view.id);for(auto *item:editor.canvas()->scene()->items())item->setSelected(false);
    for(auto *item:editor.canvas()->scene()->items())if((item->data(0).toString()=="projection"||item->data(0).toString()=="section")&&std::abs(arcLength(*editor.curves(view.id).at(item->data(2).toULongLong()).curve,editor.curves(view.id).at(item->data(2).toULongLong()).range)-100)<1e-7){item->setSelected(true);break;}
    ok(editor.addSelectedDimension(DrawingMeasure::EndpointDistance));finiteScene(editor);ok(editor.undo());
    const auto broken=bytes(editor.sheet());ok(editor.undo());require(bytes(editor.sheet())==original,"Undo ripristina vista intera");ok(editor.redo());require(bytes(editor.sheet())==broken,"Redo ripristina interruzione");
    auto object=QJsonDocument::fromJson(broken).object();require(object["schema"].toInt()==14,"schema 14 quando serve");DrawingSheet decoded;ok(decodeDrawingSheet(broken,decoded));require(decoded.views[0].interruption&&decoded.views[0].interruption->end==70,"round trip interruzione");DrawingSheetEditor reopened(decoded,source);near(paths(reopened,view.id).width(),paths(editor,view.id).width(),"riapertura ricostruisce ritaglio");near(reopened.dimensionResult(dimension.id).value,100,"riapertura mantiene misura esatta");
    DrawingEditorModel unavailable;unavailable.snapshot.version=source.snapshot.version;DrawingSheetEditor missingModel(decoded,unavailable);require(missingModel.sheet().views[0].interruption&&missingModel.dimensionResult(dimension.id).status==DrawingDimensionStatus::Missing,"modello indisponibile conserva interruzione e segnala riferimenti mancanti");finiteScene(missingModel);
    const auto unchanged=bytes(decoded);object["schema"]=13;require(!decodeDrawingSheet(QJsonDocument(object).toJson(),decoded).isEmpty()&&bytes(decoded)==unchanged,"downgrade rifiutato atomicamente");object["schema"]=14;auto items=object["views"].toArray();auto item=items[0].toObject();auto b=item["interruption"].toObject();b.remove("end");item["interruption"]=b;items[0]=item;object["views"]=items;require(!decodeDrawingSheet(QJsonDocument(object).toJson(),decoded).isEmpty(),"metadati incompleti rifiutati");
    for(auto bad:std::vector<DrawingViewBreak>{{false,70,30,6},{false,30,70,60},{false,-1,70,6},{false,30,101,6},{false,30,70,0}})require(!editor.setViewBreak(view.id,bad).isEmpty()&&bytes(editor.sheet())==broken,"interruzione invalida rifiutata senza modifiche");
    require(!editor.beginAuxiliaryViewPlacement(view.id,45).isEmpty()&&!editor.hasViewPlacement(),"derivata di vista interrotta rifiutata");
    require(!editor.beginSectionLine(view.id,"A").isEmpty(),"linea di taglio su madre interrotta rifiutata");
    require(!editor.beginDetailCircle(view.id,"Z",2).isEmpty(),"richiamo dettaglio su madre interrotta rifiutato");
    auto scaled=view;scaled.scale=2;ok(editor.updateView(scaled,editor.sheet().views[0]));near(paths(editor,view.id).width(),126+before.width()-100,"distacco invariato al cambio scala");near(editor.dimensionResult(dimension.id).value,100,"scala non altera quota");ok(editor.undo());
    auto larger=model(120);larger.snapshot.version=source.snapshot.version;ok(reopened.updateModel(larger));near(reopened.dimensionResult(dimension.id).value,120,"aggiornamento modello conserva riferimento e misura");near(paths(reopened,view.id).width()-paths(editor,view.id).width(),20,"aggiornamento modello conserva interruzione");
    select(editor,view.id);auto *action=editor.findChild<QAction *>("drawingBreakViewAction");require(action&&action->isEnabled()&&!action->icon().isNull(),"comando con icona disponibile");
    dialogCommand(editor,action,[&](QDialog *dialog){require(dialog->findChild<QGraphicsView *>("drawingBreakPreview"),"dialogo con anteprima");dialog->findChild<QDoubleSpinBox *>("drawingBreakStart")->setValue(25);require(bytes(editor.sheet())==broken,"anteprima senza modifica documento");const auto output=qEnvironmentVariable("FORGECAD_BREAK_OUTPUT");if(!output.isEmpty())require(dialog->grab().save(output+"/dialog.png"),"immagine dialogo");dialog->reject();});require(bytes(editor.sheet())==broken,"Cancel conserva documento");
    dialogCommand(editor,action,[&](QDialog *dialog){dialog->findChild<QCheckBox *>("drawingBreakEnabled")->setChecked(false);dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});require(!editor.sheet().views[0].interruption,"rimozione da dialogo");ok(editor.undo());require(bytes(editor.sheet())==broken,"Undo rimozione");
    QTemporaryDir temporary;const auto output=qEnvironmentVariable("FORGECAD_BREAK_OUTPUT");const auto pdf=output.isEmpty()?temporary.filePath("interrotta.pdf"):output+"/interrotta.pdf";DrawingPdfReport report;ok(editor.exportPdf(pdf,report));require(report.warnings.contains(QStringLiteral("1 quote da verificare.")),"PDF segnala quota con aggancio rimosso");
    editor.setReadOnly(true);require(!action->isEnabled()&&!editor.setViewBreak(view.id,{}).isEmpty(),"sola lettura protegge interruzione");ok(editor.exportPdf(pdf,report));require(bytes(editor.sheet())==broken,"export conserva storico");editor.setReadOnly(false);
    if(!output.isEmpty()){editor.fitSheet();QApplication::processEvents();require(editor.grab().save(output+"/editor.png"),"immagine editor");}
    ok(editor.setViewBreak(view.id,{}));require(QJsonDocument::fromJson(bytes(editor.sheet())).object()["schema"].toInt()<14,"schema minimo dopo rimozione");ok(editor.beginAuxiliaryViewPlacement(view.id,45));editor.cancelViewPlacement();
    // Una dipendente reale impedisce l'interruzione della madre anche nel codec.
    auto withChild=editor.sheet();DrawingView child;child.kind=DrawingViewKind::ProjectedEdges;withChild.drawing.views.push_back(child);DrawingViewLayout childLayout{child.id,"Figlia",{180,90},{1}};childLayout.relation.kind=DrawingDerivedKind::Auxiliary;childLayout.relation.parentId=view.id;withChild.views.push_back(childLayout);withChild.views[0].interruption=DrawingViewBreak{false,30,70,6};QByteArray rejected="sentinel";require(!encodeDrawingSheet(withChild,rejected).isEmpty()&&rejected=="sentinel","madre con dipendenti rifiutata atomicamente");
    auto invalid=editor.sheet();invalid.drawing.views[0].kind=DrawingViewKind::Section;QByteArray invalidBytes="sentinel";require(!encodeDrawingSheet(invalid,invalidBytes).isEmpty()&&invalidBytes=="sentinel","sezione interrotta rifiutata");
    invalid=editor.sheet();invalid.views[0].interruption=DrawingViewBreak{false,std::numeric_limits<double>::quiet_NaN(),70,6};require(!encodeDrawingSheet(invalid,invalidBytes).isEmpty(),"intervallo non finito rifiutato");
    auto tall=model(10,100);DrawingSheet verticalSheet;verticalSheet.drawing.model=tall.snapshot.version;DrawingSheetEditor vertical(verticalSheet,tall);DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;DrawingViewLayout vl{v.id,"Verticale",{120,140},{1}};vl.showMesh=true;ok(vertical.insertView(v,vl));const auto height=paths(vertical,v.id).height();ok(vertical.setViewBreak(v.id,DrawingViewBreak{true,30,70,6}));near(height-paths(vertical,v.id).height(),34,"ritaglio verticale con segno Y corretto");finiteScene(vertical);
    bool verticalGap=false;for(auto *item:vertical.canvas()->scene()->items())if(item->data(0).toString()=="viewShading"){const auto *image=dynamic_cast<QGraphicsPixmapItem *>(item);const auto p=image->mapFromScene(vl.position+QPointF(5,-33));verticalGap=qAlpha(image->pixmap().toImage().pixel(int(p.x()),int(p.y())))==0;}require(verticalGap,"distacco verticale ombreggiato trasparente");
}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{mappings();editorTests();std::cout<<"PASS viste interrotte: mapping XY, quote reali, ombreggiatura, codec, Undo, modello, dialogo, sola lettura e PDF\n";}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
