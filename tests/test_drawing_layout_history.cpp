#include "cad_drawing_editor.h"
#include "fk_primitives.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWindow>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool value,const char *why){if(!value)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
QByteArray encoded(const DrawingSheetEditor &editor){QByteArray bytes;ok(encodeDrawingSheet(editor.sheet(),bytes));return bytes;}
QTreeWidgetItem *treeItem(DrawingSheetEditor &editor,const QUuid &id){for(QTreeWidgetItemIterator it(editor.findChild<QTreeWidget *>("drawingTree"));*it;++it)if((*it)->data(0,Qt::UserRole+1).toUuid()==id)return *it;return nullptr;}
void select(DrawingSheetEditor &editor,const QUuid &id){editor.findChild<QTreeWidget *>("drawingTree")->setCurrentItem(treeItem(editor,id));}
QGraphicsItem *label(DrawingSheetEditor &editor,const QUuid &id){for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="dimension"&&item->data(3).toUuid()==id)return item;else if(item->data(0).toString()=="view"&&item->data(1).toUuid()==id)return item;return nullptr;}
QPointF center(DrawingSheetEditor &editor,const QUuid &id){QApplication::processEvents();QRectF box;bool first=true;for(auto *item:editor.canvas()->scene()->items())if(item->data(1).toUuid()==id){auto *path=dynamic_cast<QGraphicsPathItem *>(item);if(!path)continue;box=first?path->path().boundingRect():box.united(path->path().boundingRect());first=false;}require(!first,"centro di una vista visibile");return box.center();}
void near(double a,double b,const char *why){require(std::isfinite(a)&&std::abs(a-b)<1e-6,why);}
DrawingEditorModel model(const QUuid &id,double width){auto body=std::make_shared<Body>(makeBox(Frame3(),width,6,4));auto names=std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"layout"));return {{{id,{}},{{1,body,names}}},{{1,"Corpo"}},{}};}
void key(QWidget *widget,int code,Qt::KeyboardModifiers modifiers){QKeyEvent press(QEvent::KeyPress,code,modifiers),release(QEvent::KeyRelease,code,modifiers);QApplication::sendEvent(widget,&press);QApplication::sendEvent(widget,&release);}
void drag(DrawingSheetEditor &editor,const QUuid &id,QPointF delta){
    QApplication::processEvents();auto *view=editor.canvas();auto *item=label(editor,id);require(item,"etichetta da trascinare");view->ensureVisible(item);QApplication::processEvents();
    const QPointF start=view->mapFromScene(item->sceneBoundingRect().center()),end=view->mapFromScene(item->sceneBoundingRect().center()+delta);
    QMouseEvent press(QEvent::MouseButtonPress,start,view->viewport()->mapToGlobal(start.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove,end,view->viewport()->mapToGlobal(end.toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,end,view->viewport()->mapToGlobal(end.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(view->viewport(),&press);QApplication::sendEvent(view->viewport(),&move);QApplication::sendEvent(view->viewport(),&release);QApplication::processEvents();
}
}
int main(int argc,char **argv){
    QApplication app(argc,argv);
    try{
        const auto documentId=QUuid::createUuid();auto source=model(documentId,10);DrawingSheet sheet;sheet.drawing.model=source.snapshot.version;
        DrawingSheetEditor editor(sheet,source);editor.resize(1200,820);editor.show();QApplication::processEvents();
        require(!editor.canUndo()&&!editor.canRedo()&&!editor.isModified(),"editor iniziale pulito");const auto empty=encoded(editor);
        DrawingView top;top.kind=DrawingViewKind::ProjectedEdges;top.scale=3;top.plane=Frame3(Vec3(),Vec3(0,0,1),Vec3(1,0,0));
        // La fixture usa tutti i bordi per verificare la cronologia delle quote.
        ok(editor.insertView(top,{top.id,"Superiore",QPointF(100,100),{1},true}));const auto inserted=encoded(editor);
        ok(editor.undo());require(encoded(editor)==empty&&!editor.isModified()&&editor.canRedo(),"inserimento annullato senza metadati residui");ok(editor.redo());require(encoded(editor)==inserted,"vista ripristinata con stesso UUID");
        int chosen=-1;for(std::size_t i=0;i<editor.curves(top.id).size();++i){const auto &c=editor.curves(top.id)[i];if(std::abs(std::abs((c.curve->point(c.range.hi)-c.curve->point(c.range.lo)).x())-10)<1e-7){chosen=int(i);break;}}
        require(chosen>=0,"bordo orizzontale");for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="projection"&&item->data(1).toUuid()==top.id&&item->data(2).toInt()==chosen)item->setSelected(true);
        ok(editor.addSelectedDimension(DrawingMeasure::HorizontalDistance));const auto dimension=editor.sheet().drawing.dimensions[0].id;
        const auto withDimension=encoded(editor);ok(editor.undo());require(editor.sheet().drawing.dimensions.empty(),"inserimento quota annullato");ok(editor.redo());require(encoded(editor)==withDimension,"quota ripristinata con stesso riferimento");
        DrawingView front=top;front.id=QUuid::createUuid();front.scale=2;front.plane=Frame3(Vec3(),Vec3(0,-1,0),Vec3(1,0,0));ok(editor.insertView(front,{front.id,"Frontale",QPointF(205,145),{1}}));
        editor.markSaved();require(!editor.isModified(),"checkpoint dopo salvataggio");const auto clean=encoded(editor);const auto *curve=editor.curves(top.id)[0].curve.get();
        const auto oldPosition=editor.sheet().views[0].position,oldText=editor.sheet().dimensions[0].position;
        ok(editor.moveView(top.id,oldPosition+QPointF(15,-9)));require(editor.isModified(),"spostamento sporca il foglio");near(editor.sheet().dimensions[0].position.x(),oldText.x()+15,"quota segue vista X");near(editor.sheet().dimensions[0].position.y(),oldText.y()-9,"quota segue vista Y");
        require(editor.curves(top.id)[0].curve.get()==curve,"spostamento riusa curve");ok(editor.undo());require(encoded(editor)==clean&&!editor.isModified()&&editor.curves(top.id)[0].curve.get()==curve,"undo del layout senza rigenerare curve");
        ok(editor.redo());editor.markSaved();const auto movedSaved=encoded(editor);ok(editor.undo());require(editor.isModified(),"undo oltre il checkpoint rende sporco");ok(editor.redo());require(!editor.isModified(),"redo ritorna al checkpoint");
        ok(editor.moveView(top.id,editor.sheet().views[0].position));require(!editor.isModified(),"spostamento nullo non crea un passo");
        const auto beforeDrag=encoded(editor);drag(editor,top.id,QPointF(9,7));require(encoded(editor)!=beforeDrag,"trascinamento reale vista");ok(editor.undo());require(encoded(editor)==beforeDrag,"un trascinamento equivale a un solo undo");
        drag(editor,dimension,QPointF(-6,8));require(encoded(editor)!=beforeDrag,"trascinamento reale testo quota");ok(editor.undo());require(encoded(editor)==beforeDrag,"testo ripristinato con un undo");
        const auto beforeAlign=encoded(editor);const auto originBefore=editor.sheet().views[0].position,textBefore=editor.sheet().dimensions[0].position;select(editor,top.id);bool dialogSeen=false;
        QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingAlignViewsDialog");if(!dialog)return;dialogSeen=true;dialog->findChild<QComboBox *>("drawingAlignmentDirection")->setCurrentIndex(1);dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
        editor.findChild<QAction *>("drawingAlignViewAction")->trigger();require(dialogSeen,"dialogo reale allineamento");near(center(editor,top.id).x(),center(editor,front.id).x(),"centri sulla stessa colonna con scale e orientamenti diversi");
        near(editor.sheet().dimensions[0].position.x()-textBefore.x(),editor.sheet().views[0].position.x()-originBefore.x(),"testo quota segue allineamento");
        near(editor.dimensionResult(dimension).value,10,"allineamento non cambia misura");require(editor.curves(top.id)[0].curve.get()==curve,"allineamento conserva geometria");
        ok(editor.undo());require(encoded(editor)==beforeAlign,"allineamento annullato");ok(editor.alignView(top.id,front.id,DrawingSheetEditor::ViewAlignment::SameRow));near(center(editor,top.id).y(),center(editor,front.id).y(),"centri sulla stessa riga");
        ok(editor.undo());require(encoded(editor)==beforeAlign&&editor.canRedo(),"ramo redo prima di input invalido");
        require(!editor.moveView(top.id,QPointF(std::numeric_limits<double>::quiet_NaN(),0)).isEmpty()&&encoded(editor)==beforeAlign&&editor.canRedo(),"coordinate non finite rifiutate senza perdere redo");
        require(!editor.alignView(top.id,top.id,DrawingSheetEditor::ViewAlignment::SameRow).isEmpty()&&encoded(editor)==beforeAlign,"auto-allineamento rifiutato");
        ok(editor.moveDimension(dimension,textBefore+QPointF(2,3)));require(!editor.canRedo(),"nuova modifica scarta solo il ramo redo");ok(editor.undo());require(encoded(editor)==beforeAlign,"nuova modifica reversibile");
        // La cancellazione di una vista quotata richiede conferma; No lascia invariato tutto.
        select(editor,top.id);bool noSeen=false;QTimer::singleShot(0,[&]{auto *box=editor.findChild<QMessageBox *>("drawingDeleteViewDialog");if(box){noSeen=true;box->button(QMessageBox::No)->click();}});editor.findChild<QAction *>("drawingDeleteSelectionAction")->trigger();require(noSeen&&encoded(editor)==beforeAlign,"annulla cancellazione conserva vista e quote");
        select(editor,top.id);bool yesSeen=false;QTimer::singleShot(0,[&]{auto *box=editor.findChild<QMessageBox *>("drawingDeleteViewDialog");if(box){yesSeen=true;box->button(QMessageBox::Yes)->click();}});editor.findChild<QAction *>("drawingDeleteSelectionAction")->trigger();
        require(yesSeen&&editor.sheet().drawing.views.size()==1&&editor.sheet().drawing.dimensions.empty()&&editor.curves(top.id).empty(),"vista e tutte le sue quote rimosse insieme");ok(editor.undo());require(encoded(editor)==beforeAlign&&editor.dimensionResult(dimension).status==DrawingDimensionStatus::Ready,"ripristino atomico vista e quote");
        select(editor,dimension);editor.findChild<QAction *>("drawingDeleteSelectionAction")->trigger();require(editor.sheet().drawing.dimensions.empty()&&editor.sheet().drawing.views.size()==2,"cancellazione della sola quota");ok(editor.undo());require(encoded(editor)==beforeAlign,"cancellazione quota reversibile");
        editor.canvas()->scene()->clearSelection();for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="projection"){item->setSelected(true);break;}
        editor.findChild<QAction *>("drawingDeleteSelectionAction")->trigger();require(encoded(editor)==beforeAlign,"un bordo selezionato non cancella l'intera vista");
        // Undo nella casella di testo del dialogo deve restare alla casella di testo.
        select(editor,front.id);bool textUndo=false;QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingViewDialog");if(!dialog)return;auto *title=dialog->findChild<QLineEdit *>("drawingViewTitle");title->setFocus();title->insert(" aggiunta");key(title,Qt::Key_Z,Qt::ControlModifier);textUndo=title->text()=="Frontale"&&encoded(editor)==beforeAlign;dialog->reject();});editor.findChild<QAction *>("drawingEditViewAction")->trigger();require(textUndo,"Ctrl+Z del dialogo non annulla il foglio");
        editor.setReadOnly(true);require(!editor.canUndo()&&!editor.canRedo()&&!editor.undo().isEmpty()&&!editor.removeView(top.id).isEmpty()&&!editor.moveDimension(dimension,QPointF()).isEmpty(),"sola lettura su storia e layout");editor.setReadOnly(false);
        // La cronologia deve ripristinare anche piani modificati e quote divenute mancanti.
        auto side=editor.sheet().drawing.views[0];side.plane=Frame3(Vec3(),Vec3(1,0,0),Vec3(0,1,0));
        ok(editor.updateView(side,editor.sheet().views[0]));require(editor.dimensionResult(dimension).status==DrawingDimensionStatus::Missing,"cambio piano perde il bordo collassato");
        ok(editor.undo());near(editor.dimensionResult(dimension).value,10,"undo piano ripristina quota associativa");
        ok(editor.redo());require(std::isnan(editor.dimensionResult(dimension).value),"redo piano non mantiene una misura obsoleta");ok(editor.undo());
        const auto beforeFailure=encoded(editor);const bool undoBeforeFailure=editor.canUndo();
        require(!editor.updateModel(model(QUuid::createUuid(),14)).isEmpty()&&encoded(editor)==beforeFailure&&editor.canUndo()==undoBeforeFailure,"sorgente estranea rifiutata senza toccare storia");
        const auto beforeKeys=encoded(editor);ok(editor.moveView(top.id,editor.sheet().views[0].position+QPointF(2,1)));QApplication::processEvents();editor.activateWindow();editor.canvas()->setFocus();QApplication::processEvents();
        require(editor.canvas()->hasFocus(),"focus canvas per le scorciatoie");
        const auto undoKey=editor.findChild<QAction *>("drawingUndoAction")->shortcut()[0];QKeyEvent undoPress(QEvent::KeyPress,int(undoKey.key()),undoKey.keyboardModifiers()),undoRelease(QEvent::KeyRelease,int(undoKey.key()),undoKey.keyboardModifiers());
        QApplication::sendEvent(editor.windowHandle(),&undoPress);QApplication::sendEvent(editor.windowHandle(),&undoRelease);
        require(encoded(editor)==beforeKeys,"scorciatoia Annulla nel canvas");
        const auto redoKey=editor.findChild<QAction *>("drawingRedoAction")->shortcut()[0];QKeyEvent redoPress(QEvent::KeyPress,int(redoKey.key()),redoKey.keyboardModifiers()),redoRelease(QEvent::KeyRelease,int(redoKey.key()),redoKey.keyboardModifiers());
        QApplication::sendEvent(editor.windowHandle(),&redoPress);QApplication::sendEvent(editor.windowHandle(),&redoRelease);
        require(encoded(editor)!=beforeKeys,"scorciatoia Ripristina nel canvas");ok(editor.undo());
        // Aggiornare il modello e' una barriera: vecchie quote e sorgenti non tornano con Undo.
        ok(editor.updateModel(model(documentId,14)));require(!editor.canUndo()&&!editor.canRedo(),"nuova sorgente azzera cronologia locale");near(editor.dimensionResult(dimension).value,14,"quote aggiornate alla nuova sorgente");
        ok(editor.moveView(top.id,editor.sheet().views[0].position+QPointF(3,4)));ok(editor.undo());near(editor.dimensionResult(dimension).value,14,"undo layout mantiene modello aggiornato");
        // La storia limitata conserva solo JSON: nessun riferimento aggiuntivo ai corpi.
        const auto bodyReferences=editor.curves(top.id)[0].curve.use_count();const auto *sameCurve=editor.curves(top.id)[0].curve.get();
        for(int i=0;i<130;++i)ok(editor.moveView(top.id,QPointF(30+i*.1,60)));
        require(editor.curves(top.id)[0].curve.get()==sameCurve&&editor.curves(top.id)[0].curve.use_count()==bodyReferences,"cronologia layout non trattiene copie delle curve");
        int count=0;while(editor.canUndo()){ok(editor.undo());++count;}require(count==100,"limite di 100 passi conservato");
        ok(editor.redo());DrawingSheet reloaded;ok(decodeDrawingSheet(encoded(editor),reloaded));DrawingSheetEditor reopened(reloaded,model(documentId,14));require(!reopened.canUndo()&&!reopened.canRedo(),"cronologia locale non serializzata nel file");
        const auto preview=qEnvironmentVariable("FORGECAD_DRAWING_LAYOUT_PREVIEW");if(!preview.isEmpty()){ok(editor.alignView(top.id,front.id,DrawingSheetEditor::ViewAlignment::SameColumn));QApplication::processEvents();editor.fitSheet();require(editor.grab().save(preview),"anteprima layout");}
        require(!movedSaved.isEmpty(),"checkpoint presente");std::cout<<"PASS layout alignment, atomic deletion, local history, drag and source barriers\n";
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
