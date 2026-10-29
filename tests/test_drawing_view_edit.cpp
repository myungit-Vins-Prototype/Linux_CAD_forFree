#include "cad_drawing_editor.h"
#include "cad_icons.h"
#include "fk_primitives.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolBar>
#include <QWidgetAction>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;using namespace ForgeCad::Kernel;
namespace {
void require(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
QByteArray encoded(const DrawingSheetEditor &editor){QByteArray result;ok(encodeDrawingSheet(editor.sheet(),result));return result;}
QTreeWidgetItem *itemFor(QTreeWidget *tree,const QUuid &id){for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+1).toUuid()==id)return *it;return nullptr;}
void selectCurve(DrawingSheetEditor &editor,const QUuid &id,int index){editor.canvas()->scene()->clearSelection();for(auto *item:editor.canvas()->scene()->items())if(item->data(1).toUuid()==id&&item->data(2).toInt()==index&&(item->data(0).toString()=="projection"||item->data(0).toString()=="section"))item->setSelected(true);}
void confirm(QDialog *dialog){dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();}
}
#include "test_drawing_view_preview.h"
#include "test_drawing_pan.h"
int main(int argc,char **argv){
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForgeCADTests"));
    QCoreApplication::setApplicationName(QStringLiteral("DrawingViewEdit"));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try{
        auto body=std::make_shared<Body>(makeBox(Frame3(),10,6,4));auto names=std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"edit"));
        DrawingEditorModel model{{{QUuid::createUuid(),{}},{{1,body,names}}},{{1,"Corpo"}},{}};
        DrawingSheet sheet;sheet.title="Tavola di prova";sheet.drawing.model=model.snapshot.version;DrawingSheetEditor editor(sheet,model);editor.resize(1200,800);editor.show();QApplication::processEvents();
        require(!editor.findChild<QMainWindow *>("drawingWorkspace")->isWindow()&&editor.canvas()->isVisible()&&editor.canvas()->height()>editor.height()*.6,"canvas incorporato e visibile nella finestra del foglio");
        DrawingView top;top.kind=DrawingViewKind::ProjectedEdges;top.scale=4;top.plane=Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));top.sectionReferences=true;
        ok(editor.insertView(top,{top.id,"Superiore",QPointF(100,100),{1}}));
        DrawingView iso;iso.kind=DrawingViewKind::ProjectedEdges;iso.scale=4;iso.plane=Frame3(Vec3(),normalized(Vec3(1,-1,1)),normalized(Vec3(1,1,0)));
        ok(editor.insertView(iso,{iso.id,"Isometrica",QPointF(220,120),{1}}));const auto *unchanged=editor.curves(iso.id)[0].curve.get();
        int horizontal=-1,section=-1;for(std::size_t i=0;i<editor.curves(top.id).size();++i){const auto &curve=editor.curves(top.id)[i];
            const auto delta=curve.curve->point(curve.range.hi)-curve.curve->point(curve.range.lo);
            if(std::abs(std::abs(delta.x())-10)<1e-8){if(curve.kind==DrawingCurveKind::ProjectedEdge)horizontal=int(i);else section=int(i);}}
        require(horizontal>=0&&section>=0,"curve da quotare");selectCurve(editor,top.id,horizontal);ok(editor.addSelectedDimension(DrawingMeasure::HorizontalDistance));const auto dimension=editor.sheet().drawing.dimensions[0].id;
        selectCurve(editor,top.id,section);ok(editor.addSelectedDimension(DrawingMeasure::HorizontalDistance));
        auto *tree=editor.findChild<QTreeWidget *>("drawingTree");auto *bar=editor.findChild<QToolBar *>("drawingCommandBar");require(tree&&editor.findChild<QDockWidget *>("drawingTreeDock"),"albero agganciabile disponibile");
        require(bar&&bar->toolButtonStyle()==Qt::ToolButtonIconOnly&&bar->iconSize()==QSize(24,24),"densita' e icone condivise con il 3D");
        for(auto *action:bar->actions())if(!action->isSeparator()&&!qobject_cast<QWidgetAction *>(action))require(!action->icon().isNull(),"tutti i comandi hanno un'icona");
        tree->setCurrentItem(itemFor(tree,dimension));const auto selected=editor.canvas()->scene()->selectedItems();
        require(selected.size()==1&&selected[0]->data(3).toUuid()==dimension,"selezione quota albero -> foglio");
        selectCurve(editor,top.id,horizontal);require(tree->currentItem()&&tree->currentItem()->data(0,Qt::UserRole+1).toUuid()==top.id,"selezione bordo foglio -> vista nell'albero");
        const auto oldPlacement=editor.sheet().dimensions[0].position;bool edited=false;
        QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingViewDialog");if(!dialog)return;
            require(dialog->findChild<QComboBox *>("drawingViewPlane")->currentIndex()==0,"piano iniziale della vista esistente");
            dialog->findChild<QLineEdit *>("drawingViewTitle")->setText("Superiore aggiornata");dialog->findChild<QDoubleSpinBox *>("drawingViewScale")->setValue(3);edited=true;confirm(dialog);});
        editor.findChild<QAction *>("drawingEditViewAction")->trigger();
        require(edited&&editor.sheet().views[0].title=="Superiore aggiornata"&&editor.sheet().drawing.views[0].scale==3,"dialogo reale modifica vista");
        require(editor.sheet().drawing.views[0].id==top.id&&editor.sheet().drawing.dimensions[0].id==dimension&&editor.sheet().dimensions[0].position==oldPlacement,"quote, ID e posizione del testo conservati");
        require(editor.dimensionResult(dimension).status==DrawingDimensionStatus::Ready&&std::abs(editor.dimensionResult(dimension).value-10)<1e-8,"scala non cambia la misura");
        require(editor.curves(iso.id)[0].curve.get()==unchanged,"modificare una vista non rigenera le altre");
        const auto before=encoded(editor);auto invalidLayout=editor.sheet().views[0];invalidLayout.features={2};
        require(!editor.updateView(editor.sheet().drawing.views[0],invalidLayout).isEmpty()&&encoded(editor)==before,"sorgenti quotate protette, modifica atomica");
        auto noSections=editor.sheet().drawing.views[0];noSections.sectionReferences=false;
        require(!editor.updateView(noSections,editor.sheet().views[0]).isEmpty()&&encoded(editor)==before,"riferimenti di sezione quotati protetti");
        tree->setCurrentItem(itemFor(tree,top.id));bool rejected=false;
        QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingViewDialog");if(!dialog)return;
            dialog->findChild<QLineEdit *>("drawingViewTitle")->clear();confirm(dialog);
            rejected=dialog->isVisible()&&!dialog->findChild<QLabel *>("drawingViewError")->text().isEmpty();dialog->reject();});
        editor.findChild<QAction *>("drawingEditViewAction")->trigger();require(rejected&&encoded(editor)==before,"errore nel dialogo senza perdere valori o applicare modifiche");
        DrawingSheet restored;ok(decodeDrawingSheet(before,restored));DrawingSheetEditor reopened(restored,model);
        require(reopened.sheet().views[0].title=="Superiore aggiornata"&&reopened.sheet().drawing.views[0].scale==3&&std::abs(reopened.dimensionResult(dimension).value-10)<1e-8,"modifiche rileggibili senza nuovo formato");
        editor.setReadOnly(true);require(!editor.findChild<QAction *>("drawingEditViewAction")->isEnabled()&&!editor.updateView(top,editor.sheet().views[0]).isEmpty(),"modifica bloccata in sola lettura");editor.setReadOnly(false);
        // Il piano spostato fuori dal corpo mantiene la quota, con diagnostica e NaN.
        DrawingSheetEditor sectionEditor(sheet,model);DrawingView cut;cut.plane=top.plane;ok(sectionEditor.insertView(cut,{cut.id,"Sezione",QPointF(130,100),{1}}));
        selectCurve(sectionEditor,cut.id,0);ok(sectionEditor.addSelectedDimension(DrawingMeasure::EndpointDistance));const auto cutDimension=sectionEditor.sheet().drawing.dimensions[0].id;
        cut.plane=Frame3(Vec3(0,0,100),Vec3(0,0,1),Vec3(1,0,0));ok(sectionEditor.updateView(cut,sectionEditor.sheet().views[0]));
        require(sectionEditor.curves(cut.id).empty()&&sectionEditor.dimensionResult(cutDimension).status==DrawingDimensionStatus::Missing&&std::isnan(sectionEditor.dimensionResult(cutDimension).value),"quota rimasta fuori sezione marcata mancante");
        require(itemFor(sectionEditor.findChild<QTreeWidget *>("drawingTree"),cutDimension)->text(0).contains("Da verificare"),"diagnostica visibile nell'albero");
        // Un frame non standard e valori a precisione maggiore di quella visualizzata restano identici dopo una rinomina.
        iso.plane=Frame3(Vec3(1.123456789,2.234567891,3.345678912),normalized(Vec3(2,-3,4)),normalized(Vec3(3,2,0)));iso.scale=1.23456789;
        ok(editor.updateView(iso,editor.sheet().views[1]));tree->setCurrentItem(itemFor(tree,iso.id));
        QTimer::singleShot(0,[&]{auto *dialog=editor.findChild<QDialog *>("drawingViewDialog");if(!dialog)return;dialog->findChild<QLineEdit *>("drawingViewTitle")->setText("Piano arbitrario");confirm(dialog);});editor.findChild<QAction *>("drawingEditViewAction")->trigger();
        require(distance(editor.sheet().drawing.views[1].plane.origin(),iso.plane.origin())==0&&distance(editor.sheet().drawing.views[1].plane.xDir(),iso.plane.xDir())==0&&editor.sheet().drawing.views[1].scale==iso.scale,"rinomina conserva precisione e piano arbitrario");
        const auto preview=qEnvironmentVariable("FORGECAD_DRAWING_UI_PREVIEW");if(!preview.isEmpty()){QApplication::processEvents();require(editor.grab().save(preview),"anteprima interfaccia tavola");}
        viewDialogPreviewTests();
        orientedIsometricTests();
        drawingPanTests();
        std::cout<<"PASS drawing tree, view edit, preserved references and atomic validation\n";
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
