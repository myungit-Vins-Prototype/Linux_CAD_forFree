#include "cad_drawing_editor.h"
#include "fk_boolean.h"
#include "fk_sheet.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace ForgeCad;
using namespace ForgeCad::Kernel;
namespace {
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
DrawingEditorModel model(const QUuid &id,double half){
    auto body=std::make_shared<Body>(makePlaneSheet(Frame3(),half));
    auto names=std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"fixture"));
    DrawingEditorModel result;result.snapshot={{id,{}},{{1,body,names}}};result.choices={{1,"Superficie aperta"}};return result;
}
void click(QGraphicsView *view,QPointF scene){
    const QPointF local=view->mapFromScene(scene);const QPointF global=view->viewport()->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(view->viewport(),&press);QApplication::sendEvent(view->viewport(),&release);
}
}
int main(int argc,char **argv){
    QApplication app(argc,argv);
    try{
        const auto id=QUuid::createUuid();auto source=model(id,5);DrawingSheet sheet;sheet.drawing.model=source.snapshot.version;sheet.title="Sezione di una superficie";
        DrawingSheetEditor editor(sheet,source);editor.resize(1100,760);editor.show();QApplication::processEvents();
        bool dialogSeen=false;
        QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingViewDialog");if(!dialog)return;
            dialog->findChild<QComboBox *>("drawingViewPlane")->setCurrentIndex(1);
            dialog->findChild<QDoubleSpinBox *>("drawingViewDepth")->setValue(0);
            dialog->findChild<QDoubleSpinBox *>("drawingViewScale")->setValue(2);
            dialog->findChild<QLineEdit *>("drawingViewTitle")->setText("Sezione A-A");dialogSeen=true;dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});
        editor.findChild<QAction *>("drawingAddViewAction")->trigger();
        require(dialogSeen&&editor.hasViewPlacement()&&editor.sheet().drawing.views.empty()&&!editor.isModified(),"dialogo prepara l'anteprima senza modificare il foglio");
        click(editor.canvas(),QPointF(145,100));require(!editor.hasViewPlacement()&&editor.sheet().drawing.views.size()==1,"clic inserisce realmente la vista");
        const auto viewId=editor.sheet().drawing.views[0].id;
        require(editor.sections(viewId).size()==1,"sezione interna di superficie aperta");
        QGraphicsPathItem *curve=nullptr;for(auto *item:editor.canvas()->scene()->items())if(item->data(0).toString()=="section")curve=dynamic_cast<QGraphicsPathItem *>(item);
        require(curve,"curva selezionabile sul foglio");editor.fitSheet();QApplication::processEvents();click(editor.canvas(),curve->path().pointAtPercent(.5));
        require(curve->isSelected(),"selezione curva con il mouse");
        editor.findChild<QAction *>("drawingEndpointDimensionAction")->trigger();
        require(editor.sheet().drawing.dimensions.size()==1,"quota inserita dalla GUI");const auto dimension=editor.sheet().drawing.dimensions[0].id;
        require(editor.dimensionResult(dimension).status==DrawingDimensionStatus::Ready&&std::abs(editor.dimensionResult(dimension).value-10)<1e-7,"misura esatta indipendente da scala 2");
        editor.setReadOnly(true);
        require(!editor.findChild<QAction *>("drawingAddViewAction")->isEnabled()&&!editor.addSelectedDimension(DrawingMeasure::EndpointDistance).isEmpty(),"sola lettura impedisce modifiche GUI e API");
        require(!editor.updateModel(model(id,7)).isEmpty(),"sola lettura impedisce retarget");
        editor.setReadOnly(false);
        QByteArray bytes;ok(encodeDrawingSheet(editor.sheet(),bytes));DrawingSheet reopened;ok(decodeDrawingSheet(bytes,reopened));
        require(reopened.views.size()==1&&reopened.dimensions.size()==1&&reopened.drawing.dimensions[0].id==dimension,"metadati del foglio riletti");
        DrawingSheetEditor reloaded(reopened,source);require(std::abs(reloaded.dimensionResult(dimension).value-10)<1e-7,"quota valida dopo riapertura dei metadati");
        ok(editor.updateModel(model(id,7)));require(std::abs(editor.dimensionResult(dimension).value-14)<1e-7,"associazione dopo modifica superficie");
        auto missing=model(id,7);missing.snapshot.bodies.clear();missing.choices.clear();ok(editor.updateModel(missing));
        require(editor.dimensionResult(dimension).status==DrawingDimensionStatus::Missing&&std::isnan(editor.dimensionResult(dimension).value),"riferimento perso senza conservare vecchio valore");
        ok(editor.updateModel(model(id,7)));QApplication::processEvents();const auto preview=qEnvironmentVariable("FORGECAD_DRAWING_PREVIEW");if(!preview.isEmpty())require(editor.grab().save(preview),"salva anteprima QA");
        std::cout<<"PASS drawing sheet GUI, metadata bytes="<<bytes.size()<<'\n';return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
