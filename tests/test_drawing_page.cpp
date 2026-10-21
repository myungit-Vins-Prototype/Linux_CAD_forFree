#include "cad_drawing_editor.h"
#include "fk_primitives.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
QByteArray bytes(const DrawingSheetEditor &editor){QByteArray out;ok(encodeDrawingSheet(editor.sheet(),out));return out;}
class Counted final:public Curve<3>{CurvePtr<3> basis_;std::shared_ptr<int> count_;public:Counted(CurvePtr<3> basis,std::shared_ptr<int> count):basis_(std::move(basis)),count_(std::move(count)){}CurveType type()const override{return CurveType::Other;}Interval domain()const override{return basis_->domain();}void evaluate(double t,int order,Vec3 *out)const override{++*count_;basis_->evaluate(t,order,out);}};
QByteArray read(const QString &path){QFile file(path);require(file.open(QIODevice::ReadOnly),"lettura artefatto");return file.readAll();}
void write(const QString &path,QByteArray data){QFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(data)==data.size(),"scrittura fixture");}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{
 const auto count=std::make_shared<int>(0);auto body=std::make_shared<Body>(makeBox(Frame3(),10,6,4));for(auto edge:body->edges())body->edge(edge).curve=std::make_shared<Counted>(body->edge(edge).curve,count);auto names=std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"page"));DrawingEditorModel model{{{QUuid::createUuid(),{}},{{1,body,names}}},{{1,"Modello prova"}},{}};
 DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor editor(sheet,model);editor.resize(1200,820);editor.show();QApplication::processEvents();DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.scale=3;ok(editor.insertView(view,{view.id,"Vista confermata",QPointF(90,90),{1}}));
 const auto untouched=bytes(editor);require(!QJsonDocument::fromJson(untouched).object().contains("titleBlock"),"tavole precedenti prive di campi aggiuntivi");editor.markSaved();DrawingTitleBlock block{true,"FC-001","Officina test","Mario Rossi","2026-10-16"};ok(editor.setPageSettings("Supporto prova",{297,210},block));require(editor.isModified(),"cartiglio sporca il foglio");QApplication::processEvents();ok(editor.undo());require(bytes(editor)==untouched&&!editor.isModified(),"Undo ripristina cartiglio legacy e checkpoint");ok(editor.redo());require(editor.sheet().titleBlock.visible&&editor.sheet().titleBlock.code=="FC-001","Redo cartiglio");
 DrawingSheet loaded;ok(decodeDrawingSheet(bytes(editor),loaded));require(loaded.titleBlock.author=="Mario Rossi"&&loaded.titleBlock.date=="2026-10-16","cartiglio persistito senza data dinamica");const auto valid=bytes(editor);require(!editor.setPageSettings("",{297,210},block).isEmpty()&&bytes(editor)==valid,"titolo invalido rifiutato atomicamente");require(!editor.setPageSettings("Titolo",{100,100},block).isEmpty()&&bytes(editor)==valid,"cartiglio su foglio troppo piccolo rifiutato");
 auto json=QJsonDocument::fromJson(valid).object();auto invalid=json["titleBlock"].toObject();invalid["visible"]="yes";json["titleBlock"]=invalid;const auto oldTitle=loaded.title;require(!decodeDrawingSheet(QJsonDocument(json).toJson(),loaded).isEmpty()&&loaded.title==oldTitle,"metadati invalidi rifiutati senza perdere il foglio");
 auto context=defaultDrawingPageContext(editor.sheet());context.modelName="Supporto.prt";context.drawingRevision="Lavoro (base D1)";context.modelRevision="M1";editor.setPageContext(context);
 bool dialogSeen=false;QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingPageDialog");if(!dialog)return;dialogSeen=true;dialog->findChild<QComboBox *>("drawingPaperFormat")->setCurrentText("A3");dialog->findChild<QComboBox *>("drawingPaperOrientation")->setCurrentIndex(1);dialog->findChild<QLineEdit *>("drawingTitleBlockCode")->setText("FC-002");dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});editor.findChild<QAction *>("drawingPageSettingsAction")->trigger();require(dialogSeen&&editor.sheet().size==QSizeF(297,420)&&editor.sheet().titleBlock.code=="FC-002","dialogo pagina reale e orientamento");ok(editor.undo());require(editor.sheet().size==QSizeF(297,210)&&editor.sheet().titleBlock.code=="FC-001","cambio pagina un solo Undo");
 QTemporaryDir temporary;QString output=qEnvironmentVariable("FORGECAD_PAGE_OUTPUT");if(output.isEmpty())output=temporary.path();require(QDir().mkpath(output),"cartella output");DrawingPdfReport report;
 for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="projection"){item->setSelected(true);break;}
 const auto selected=editor.canvas()->scene()->selectedItems();const auto before=bytes(editor);*count=0;ok(editor.exportPdf(output+"/a4-landscape.pdf",report));require(*count==0&&bytes(editor)==before&&editor.canvas()->scene()->selectedItems()==selected,"PDF riusa geometria senza alterare selezione e metadati");require(report.warnings.isEmpty(),"nessun falso avviso di ritaglio sul foglio valido");require(read(output+"/a4-landscape.pdf").startsWith("%PDF-"),"PDF reale generato");
 auto ghost=view;ghost.id=QUuid::createUuid();ok(editor.beginViewPlacement(ghost,{ghost.id,"ANTEPRIMA_DA_ESCLUDERE",QPointF(180,70),{1}}));const auto beforeGhost=bytes(editor);const auto itemsBefore=editor.canvas()->scene()->items().size();*count=0;ok(editor.exportPdf(output+"/with-preview.pdf",report));require(editor.hasViewPlacement()&&editor.canvas()->scene()->items().size()==itemsBefore&&bytes(editor)==beforeGhost&&*count==0,"esportazione conserva l'anteprima ma non la inserisce");editor.cancelViewPlacement();
 editor.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(true);auto *canvas=editor.canvas();const QPointF local=canvas->mapFromScene(QPointF(90,90)),global=canvas->viewport()->mapToGlobal(local.toPoint());QMouseEvent move(QEvent::MouseMove,local,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&move);bool snap=false;for(auto *item:canvas->scene()->items())snap|=item->data(0).toString()=="dimensionSnap";require(snap,"snap presente prima dell'export");ok(editor.exportPdf(output+"/with-snap.pdf",report));require(editor.findChild<QAction *>("drawingSmartDimensionAction")->isChecked(),"export conserva strumento quota");editor.findChild<QAction *>("drawingSmartDimensionAction")->setChecked(false);
 ok(editor.setPageSettings("Supporto A3",{297,420},block));ok(editor.exportPdf(output+"/a3-portrait.pdf",report));require(editor.sheet().size==QSizeF(297,420),"export legge stato corrente prima del ridisegno accodato");QApplication::processEvents();
 editor.setReadOnly(true);require(!editor.findChild<QAction *>("drawingPageSettingsAction")->isEnabled()&&!editor.setPageSettings("vietato",{297,210},block).isEmpty(),"sola lettura impedisce modifica pagina");ok(editor.exportPdf(output+"/readonly.pdf",report));editor.setReadOnly(false);
 write(output+"/modello.prt","ORIGINALE");require(!editor.exportPdf(output+"/modello.prt",report).isEmpty()&&read(output+"/modello.prt")=="ORIGINALE","export non sovrascrive modello");
#ifdef Q_OS_UNIX
 QFile::remove(output+"/alias.pdf");require(QFile::link(output+"/modello.prt",output+"/alias.pdf"),"alias fixture");require(!editor.exportPdf(output+"/alias.pdf",report).isEmpty()&&read(output+"/modello.prt")=="ORIGINALE","alias del modello protetto");
#endif
 report.warnings={"precedente"};require(!editor.exportPdf(output+"/inesistente/output.pdf",report).isEmpty()&&report.warnings==QStringList{"precedente"},"errore di scrittura conserva report e documenti");
 // Quota su un bordo che collassa cambiando piano: stampa un avviso, mai un valore obsoleto.
 const auto pieces=editor.curves(view.id);std::size_t index=0;while(index<pieces.size()&&std::abs(std::abs((pieces[index].curve->point(pieces[index].range.hi)-pieces[index].curve->point(pieces[index].range.lo)).x())-10)>1e-7)++index;require(index<pieces.size(),"bordo X per la quota");auto dimension=dimensionOfCurve(view,pieces[index],DrawingMeasure::EndpointDistance);ok(editor.addDimension(dimension,{90,60}));auto side=view;side.plane=Frame3(Vec3(),Vec3(1,0,0),Vec3(0,1,0));ok(editor.updateView(side,editor.sheet().views[0]));require(editor.dimensionResult(dimension.id).status==DrawingDimensionStatus::Missing,"riferimento perso verificato");ok(editor.exportPdf(output+"/missing-dimension.pdf",report));require(!report.warnings.isEmpty(),"quote non verificate segnalate nell'export");
 const auto preview=qEnvironmentVariable("FORGECAD_PAGE_PREVIEW");if(!preview.isEmpty())require(editor.grab().save(preview),"anteprima pagina");std::cout<<"PASS page layout, title block, codec/history/read-only, vector PDF, transient overlays and atomic errors\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
