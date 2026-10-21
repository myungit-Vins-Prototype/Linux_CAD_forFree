#include "cad_drawing_editor.h"
#include "cad_menu.h"
#include "toolbar_editor.h"
#include "fk_primitives.h"
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPdfWriter>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QCheckBox>
#include <iostream>
using namespace ForgeCad;using namespace Kernel;
void need(bool b,const char *s){if(!b)throw std::runtime_error(s);}
void ok(QString s){if(!s.isEmpty())throw std::runtime_error(s.toStdString());}
QByteArray bytes(const DrawingSheet &s){QByteArray b;ok(encodeDrawingSheet(s,b));return b;}
void move(DrawingSheetEditor &e,QPointF p,int phase){auto *v=e.canvas();const QPointF local=v->mapFromScene(p),global=v->viewport()->mapToGlobal(local.toPoint());QMouseEvent event(phase==0?QEvent::MouseButtonPress:phase==2?QEvent::MouseButtonRelease:QEvent::MouseMove,local,global,phase==1?Qt::NoButton:Qt::LeftButton,phase==2?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(v->viewport(),&event);}
QGraphicsItem *find(DrawingSheetEditor &e,QString kind,QUuid id={},int end=-1){QApplication::processEvents();for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()==kind&&(id.isNull()||item->data(3).toUuid()==id)&&(end<0||item->data(2).toInt()==end))return item;throw std::runtime_error(("missing "+kind).toStdString());}
void menus(){const auto original=QApplication::palette();for(bool dark:{false,true}){auto palette=original;palette.setColor(QPalette::Window,dark?QColor("#202d36"):QColor("#f2f2f2"));palette.setColor(QPalette::WindowText,dark?Qt::white:Qt::black);QApplication::setPalette(palette);CadMenu menu;auto *checked=menu.addAction("Voce selezionabile");checked->setCheckable(true);checked->setChecked(true);menu.addSeparator();menu.addAction("Disabilitata")->setEnabled(false);menu.popup({100,100});QApplication::processEvents();const auto image=menu.grab().toImage();const auto color=image.pixelColor(image.width()/2,image.height()-5);std::cerr<<"menu dark="<<dark<<" pixel="<<color.name().toStdString()<<" alpha="<<color.alpha()<<std::endl;if(!qEnvironmentVariable("FORGECAD_UI_OUTPUT").isEmpty())image.save(qEnvironmentVariable("FORGECAD_UI_OUTPUT")+(dark?"/menu-dark.png":"/menu-light.png"));need(dark?color.lightness()<100:color.lightness()>180,"menu surface follows light/dark theme");menu.close();}QApplication::setPalette(original);}
void menuIcons() {
    CadMenu menu;
    QPixmap marker(16,16); marker.fill(QColor(255,0,255));
    auto *action = menu.addAction(QIcon(marker), QStringLiteral("Icona singola"));
    action->setCheckable(true); action->setChecked(true);
    menu.popup({100,100}); QApplication::processEvents();
    const auto image = menu.grab().toImage();
    int bands = 0; bool previous = false;
    for (int x=0; x<image.width(); ++x) {
        bool present = false;
        for (int y=0; y<image.height(); ++y) {
            const auto color=image.pixelColor(x,y);
            if (color.red()>220 && color.blue()>220 && color.green()<40) { present=true; break; }
        }
        if (present && !previous) ++bands;
        previous=present;
    }
    need(bands==1, "menu paints exactly one icon beside the independent check mark");
    if(!qEnvironmentVariable("FORGECAD_UI_OUTPUT").isEmpty())
        image.save(qEnvironmentVariable("FORGECAD_UI_OUTPUT")+"/menu-single-icon.png");
    menu.close();
}
void toolbar(){QToolBar bar;bar.setObjectName("drawingControlsTest");auto *smart=bar.addAction("Quota intelligente");auto *combo=new QComboBox(&bar);combo->addItem("Automatica");auto *control=bar.addWidget(combo);control->setObjectName("drawingDimensionTypeControl");control->setProperty("toolbarRequired",true);QSettings().setValue("toolbar/layout/drawingControlsTest",QStringList{"command/Quote/Quota intelligente"});ToolbarEditor editor(&bar,{{"Quote/Quota intelligente",smart}},[]{return false;});bar.show();QApplication::processEvents();need(bar.actions().contains(control)&&combo->isVisible(),"required selector recovered from old layout without deleting other commands");need(bar.actions().contains(smart),"custom command retained");}
void drawing(){DrawingEditorModel model;model.snapshot.version={QUuid::createUuid(),{}};auto body=std::make_shared<Body>(makeBox(Frame3(),20,12,8));model.snapshot.bodies={{1,body,std::make_shared<ElementNames>(propagateNames(*body,makeHistory({},*body),{},"ui"))}};model.choices={{1,"Corpo"}};DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor e(sheet,model);e.resize(1250,820);e.show();QApplication::processEvents();e.fitSheet();DrawingView v;v.kind=DrawingViewKind::ProjectedEdges;v.plane=Frame3();v.scale=2;DrawingViewLayout l;l.viewId=v.id;l.position={50,65};l.title="Vista";l.features={1};ok(e.insertView(v,l));
 const auto ref=[&](Vec2 p){for(const auto &c:e.curves(v.id))for(auto role:{DrawingPointRole::Start,DrawingPointRole::End}){auto r=drawingAttachment(v,c,model.snapshot,role);const auto resolved=resolveDrawingAttachment(v,model.snapshot,e.curves(v.id),r);if(resolved.status==DrawingDimensionStatus::Ready&&distance(resolved.point,p)<1e-7)return r;}throw std::runtime_error("reference missing");};
 DrawingAxis axis;axis.viewId=v.id;axis.references={ref({0,0}),ref({20,0})};ok(e.addAxis(axis));find(e,"axis",axis.id)->setSelected(true);need(find(e,"axisHandle",axis.id,0)&&find(e,"axisHandle",axis.id,1),"selected manual axis has two grips");auto *grip=find(e,"axisHandle",axis.id,0);const auto first=grip->pos(),second=find(e,"axisHandle",axis.id,1)->pos();const auto before=bytes(e.sheet());move(e,first,0);move(e,first+QPointF(-8,0),1);need(bytes(e.sheet())==before&&find(e,"axisHandle",axis.id,1)->pos()==second,"axis preview leaves opposite endpoint and document untouched");need(find(e,"axisExtensionPreview"),"axis preview path");QKeyEvent esc(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(e.canvas(),&esc);move(e,first+QPointF(-8,0),2);need(bytes(e.sheet())==before&&find(e,"axisHandle",axis.id,0)->pos()==first,"axis Esc restores");
 move(e,first,0);move(e,first+QPointF(-8,0),1);move(e,first+QPointF(-8,0),2);need(e.sheet().axes[0].extension>9&&e.sheet().axes[0].endExtension.value()==2,"only first end extended");need(distance(e.axisResult(axis.id).first,Vec2(0,0))<1e-8&&distance(e.axisResult(axis.id).second,Vec2(20,0))<1e-8,"geometric axis references unchanged");const auto changed=bytes(e.sheet());ok(e.undo());need(bytes(e.sheet())==before,"one undo for axis end");ok(e.redo());need(bytes(e.sheet())==changed,"axis redo exact");
 find(e,"axis",axis.id)->setSelected(true);const auto secondGrip=find(e,"axisHandle",axis.id,1)->pos();move(e,secondGrip,0);move(e,secondGrip+QPointF(8,0),1);move(e,secondGrip+QPointF(8,0),2);need(e.sheet().axes[0].extension>9&&e.sheet().axes[0].endExtension.value()>9,"second end can extend independently");ok(e.undo());need(bytes(e.sheet())==changed,"second end undo leaves first extension intact");
 auto root=QJsonDocument::fromJson(changed).object();need(root["schema"].toInt()==11,"asymmetric axis schema11");DrawingSheet decoded;ok(decodeDrawingSheet(changed,decoded));need(decoded.axes[0].endExtension==e.sheet().axes[0].endExtension,"asymmetric axis roundtrip");root["schema"]=10;need(!decodeDrawingSheet(QJsonDocument(root).toJson(),decoded).isEmpty(),"axis downgrade rejected");
 e.canvas()->scene()->clearSelection();for(auto *item:e.canvas()->scene()->items())if(item->data(0).toString()=="projection"){const auto &c=e.curves(v.id).at(item->data(2).toInt());if(std::abs(arcLength(*c.curve,c.range)-20)<1e-7){item->setSelected(true);break;}}ok(e.addSelectedDimension(DrawingMeasure::HorizontalDistance));const auto id=e.sheet().drawing.dimensions.back().id;auto *label=find(e,"dimension",id);e.canvas()->scene()->clearSelection();label->setSelected(true);const auto nominal=e.dimensionResult(id).value;const auto normal=bytes(e.sheet());auto *oldArrow=dynamic_cast<QGraphicsPathItem *>(find(e,"dimensionArrow",id));need(oldArrow,"dimension arrow geometry");const auto oldPath=oldArrow->path();e.findChild<QAction *>("drawingReverseDimensionArrowsAction")->trigger();need(e.sheet().dimensions.back().presentation.reverseArrows,"selected dimension reversed");need(dynamic_cast<QGraphicsPathItem *>(find(e,"dimensionArrow",id))->path()!=oldPath,"rendered arrows change direction");need(e.dimensionResult(id).value==nominal,"arrow reversal preserves value");const auto reversed=bytes(e.sheet());auto noAxes=e.sheet();noAxes.axes.clear();need(QJsonDocument::fromJson(bytes(noAxes)).object()["schema"].toInt()==11,"arrow reversal alone selects schema11");ok(decodeDrawingSheet(reversed,decoded));need(decoded.dimensions.back().presentation.reverseArrows,"arrow direction saved");ok(e.undo());need(bytes(e.sheet())==normal,"arrow reversal one undo");ok(e.redo());need(bytes(e.sheet())==reversed,"arrow reversal redo");root=QJsonDocument::fromJson(reversed).object();root["schema"]=10;need(!decodeDrawingSheet(QJsonDocument(root).toJson(),decoded).isEmpty(),"arrows downgrade rejected");
 e.canvas()->scene()->clearSelection();find(e,"dimension",id)->setSelected(true);QTimer::singleShot(0,&e,[&]{auto *dialog=e.findChild<QDialog *>("drawingDimensionPropertiesDialog");need(dialog,"dimension properties");auto *check=dialog->findChild<QCheckBox *>("drawingDimensionReverseArrows");need(check&&check->isChecked(),"arrow option reflects current state");check->setChecked(false);dialog->reject();});e.findChild<QAction *>("drawingEditDimensionAction")->trigger();need(bytes(e.sheet())==reversed,"cancel properties preserves arrow state");
 QTimer::singleShot(0,&e,[&]{auto *dialog=e.findChild<QDialog *>("drawingDimensionPropertiesDialog");dialog->findChild<QCheckBox *>("drawingDimensionReverseArrows")->setChecked(false);dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});e.findChild<QAction *>("drawingEditDimensionAction")->trigger();need(!e.sheet().dimensions.back().presentation.reverseArrows,"arrow option can restore normal direction");ok(e.undo());need(bytes(e.sheet())==reversed,"properties arrow change undo exact");
 e.setReadOnly(true);need(!e.findChild<QAction *>("drawingReverseDimensionArrowsAction")->isEnabled(),"readonly reversal disabled");need(!e.findChildren<QComboBox *>("drawingDimensionType").isEmpty(),"automatic selector remains present");e.setReadOnly(false);QTemporaryDir output;DrawingPdfReport report;ok(e.exportPdf(output.filePath("controls.pdf"),report));if(!qEnvironmentVariable("FORGECAD_UI_OUTPUT").isEmpty())e.canvas()->grab().save(qEnvironmentVariable("FORGECAD_UI_OUTPUT")+"/drawing-controls.png");}
int main(int argc,char **argv){QApplication app(argc,argv);QTemporaryDir config;QCoreApplication::setOrganizationName("ForgeCAD-UI-Tests");QCoreApplication::setApplicationName("Isolated");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,config.path());try{menus();menuIcons();if(app.arguments().contains("--menus-only")){std::cout<<"PASS menus single icon\n";return 0;}toolbar();drawing();std::cout<<"PASS themed menus, restored selector, arrow reversal, one-sided axis drag, Undo/Redo, schema11 and PDF\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}}
