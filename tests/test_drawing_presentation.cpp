#include "cad_drawing_editor.h"
#include "cad_drawing_compute.h"
#include "fk_primitives.h"
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool b,const char *why){if(!b)throw std::runtime_error(why);}
void ok(const QString &e){if(!e.isEmpty())throw std::runtime_error(e.toStdString());}
DrawingSourceBody body(quint64 id,Vec3 p,double width,quint32 color){auto shape=std::make_shared<Body>(makeBox(Frame3(p,Vec3(0,0,1),Vec3(1,0,0)),width,width,2));auto names=std::make_shared<ElementNames>(propagateNames(*shape,makeHistory({},*shape),{},"shade"+std::to_string(id)));return {id,shape,names,color};}
QByteArray bytes(const DrawingSheetEditor &e){QByteArray b;ok(encodeDrawingSheet(e.sheet(),b));return b;}
QGraphicsPixmapItem *image(DrawingSheetEditor &e,const QUuid &id){for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="viewShading"&&item->data(1).toUuid()==id)return dynamic_cast<QGraphicsPixmapItem *>(item);return nullptr;}
QColor colorAt(QGraphicsPixmapItem *item,QPointF scene){return item->pixmap().toImage().pixelColor(item->mapFromScene(scene).toPoint());}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{
 DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};model.snapshot.bodies={body(1,{-4,-4,3},8,qRgb(220,60,30)),body(2,{-8,-8,-4},16,qRgb(40,120,220))};model.choices={{1,"Corpo rosso davanti"},{2,"Corpo blu dietro"}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;sheet.size={420,297};sheet.title="Facce ombreggiate e tabella revisioni";sheet.titleBlock.visible=true;
 DrawingSheetEditor editor(sheet,model);editor.resize(1350,900);editor.show();QApplication::processEvents();DrawingView top;top.kind=DrawingViewKind::ProjectedEdges;top.scale=3;ok(editor.insertView(top,{top.id,"Vista con facce",{100,95},{1,2}}));const auto before=bytes(editor);
 bool rejectedReentry=false,canceled=false;auto canceledView=top;canceledView.id=QUuid::createUuid();QTimer::singleShot(1,&editor,[&]{rejectedReentry=editor.isComputing()&&!editor.undo().isEmpty()&&bytes(editor)==before;auto *dialog=editor.findChild<QProgressDialog *>("drawingViewProgress");require(dialog,"dialogo avanzamento");dialog->findChild<QPushButton *>()->click();canceled=true;});const auto error=editor.beginViewPlacement(canceledView,{canceledView.id,"Annullata",{0,0},{1,2}});require(!error.isEmpty()&&canceled&&rejectedReentry&&!editor.hasViewPlacement()&&bytes(editor)==before,"annullamento non applica geometria parziale o modifiche rientranti");
 ok(editor.setShadedFacesVisible(top.id,true));QApplication::processEvents();auto *shading=image(editor,top.id);require(shading&&!(shading->flags()&QGraphicsItem::ItemIsSelectable),"facce renderizzate ma non quotabili");const auto front=colorAt(shading,{100,95}),rear=colorAt(shading,{118,95});require(front.red()>front.blue()&&rear.blue()>rear.red(),"z-buffer conserva il corpo davanti e quello posteriore ai lati");
 require(editor.curves(top.id).size()>0,"bordi esatti conservati");DrawingSheet decoded;ok(decodeDrawingSheet(bytes(editor),decoded));require(decoded.views[0].showMesh,"opzione facce persistita");ok(editor.undo());QApplication::processEvents();require(!image(editor,top.id),"Undo rimuove solo il rendering delle facce");ok(editor.redo());QApplication::processEvents();require(image(editor,top.id),"Redo ripristina facce dalla cache");
 DrawingView half;half.scale=3;half.sectionStyle=DrawingSectionStyle::HalfRight;DrawingViewLayout halfLayout{half.id,"Mezza sezione ombreggiata",{260,95},{1,2}};halfLayout.showMesh=true;ok(editor.insertView(half,halfLayout));QApplication::processEvents();auto *halfImage=image(editor,half.id);require(halfImage,"facce nella mezza sezione");require(colorAt(halfImage,{254,95}).red()>colorAt(halfImage,{254,95}).blue()&&colorAt(halfImage,{266,95}).blue()>colorAt(halfImage,{266,95}).red(),"materiale davanti asportato solo nella meta' scelta");
 auto context=editor.pageContext();const DocumentVersion projectA{sheet.drawing.model.documentId,QUuid::createUuid()},projectB{sheet.drawing.model.documentId,QUuid::createUuid()};context.projectRevisions={{projectA,1,"1","Mario","Prima emissione",QDateTime::fromString("2026-10-01T10:00:00Z",Qt::ISODate)},{projectB,2,"2","Laura","Seconda emissione",QDateTime::fromString("2026-10-16T12:00:00Z",Qt::ISODate)}};editor.setPageContext(context);
 DrawingRevisionTable table;table.visible=true;table.notes={{projectB.checkinId,"Descrizione lunga della revisione che deve andare automaticamente a capo nella cella e restare completamente leggibile nel PDF.\nVerifica delle superfici e delle quote.","Tecnico"}};ok(editor.setRevisionTable(table));DrawingRevisionTableLayout layout;ok(layoutDrawingRevisionTable(editor.sheet(),editor.pageContext(),layout));require(layout.rowHeights.size()==2&&layout.rowHeights[1]>layout.rowHeights[0],"testo lungo aumenta automaticamente l'altezza della riga");ok(decodeDrawingSheet(bytes(editor),decoded));require(decoded.revisionTable.notes[0].checkinId==projectB.checkinId,"note riferite al vero check-in, non a un numero libero");
 auto bad=table;bad.notes.append(bad.notes[0]);require(!editor.setRevisionTable(bad).isEmpty(),"riga duplicata rifiutata");const auto stable=bytes(editor);auto oversized=context;oversized.projectRevisions.clear();for(int i=0;i<100;++i)oversized.projectRevisions.append({{projectA.documentId,QUuid::createUuid()},quint64(i+1),QString::number(i+1),"Autore","Descrizione",projectA.checkinId.isNull()?QDateTime():QDateTime::currentDateTimeUtc()});editor.setPageContext(oversized);DrawingPdfReport report;QTemporaryDir temp;require(!editor.exportPdf(temp.filePath("troppo-grande.pdf"),report).isEmpty()&&bytes(editor)==stable,"tabella eccedente non viene troncata o altera il documento");editor.setPageContext(context);
 const auto output=qEnvironmentVariable("FORGECAD_PRESENTATION_OUTPUT",temp.path());QDir().mkpath(output);ok(editor.exportPdf(output+"/facce-revisioni.pdf",report));require(!report.warnings.isEmpty(),"PDF dichiara le facce raster separate dai vettori");QApplication::processEvents();editor.grab().save(output+"/editor.png");editor.setReadOnly(true);require(!editor.setShadedFacesVisible(top.id,false).isEmpty()&&!editor.setRevisionTable({}).isEmpty(),"rendering e tabella protetti negli storici");ok(editor.exportPdf(output+"/storico.pdf",report));
 // Una prova lenta controlla aggiornamenti GUI e cancellazione cooperativa anche dentro il lavoro.
 QWidget owner;bool busy=false;int value=99,ticks=0;QTimer heartbeat;QObject::connect(&heartbeat,&QTimer::timeout,[&]{++ticks;});heartbeat.start(5);QTimer::singleShot(50,&owner,[&]{auto *dialog=owner.findChild<QProgressDialog *>("drawingViewProgress");require(dialog,"progresso del lavoro lento");dialog->findChild<QPushButton *>()->click();});auto work=runDrawingComputation(&owner,busy,"Calcolo di prova",value,[](auto phase){for(int i=0;i<200;++i){phase(i/40);std::this_thread::sleep_for(std::chrono::milliseconds(5));}return 7;});require(!work.isEmpty()&&value==99&&ticks>2&&!busy,"barra viva, annullamento cooperativo, nessun risultato parziale");
 std::cout<<"PASS shaded faces/depth/half-sections, progress/cancel/reentry, codec/history/read-only, revision-table wrap/overflow and PDF\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
