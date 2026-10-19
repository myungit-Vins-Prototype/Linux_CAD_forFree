#pragma once
#include "../cad_drawing_window.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QTimer>
#include <QEvent>
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <stdexcept>
namespace {
void drawingModelUpdateTests(){
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    const auto ok=[](const QString &e){if(!e.isEmpty())throw std::runtime_error(e.toStdString());};
    const auto check=[](bool yes,const char *e){if(!yes)throw std::runtime_error(e);};
    QTemporaryDir temporary;check(temporary.isValid(),"update fixture directory");
    const auto sourcePath=temporary.filePath("modello.prt"),path=temporary.filePath("tavola.drw");
    DocumentState source;ExtrusionObject box;box.feature=BodyFeature::Primitive;box.featureId=81001;
    box.primitive.kind=PrimitiveKind::Box;box.primitive.size[0]=10;box.primitive.size[1]=6;box.primitive.size[2]=4;source.extrusions={box};
    DrawingEditorModel geometry;ok(prepareDrawingEditorModel(source,geometry));source.modelBodies[0].meshColor=QColor("#3d3846");
    ok(saveDocumentFile(sourcePath,source));ok(checkoutWorkspaceDocument(sourcePath,source,"modellatore"));RevisionRecord a,b,c,d;
    ok(checkinWorkspaceDocument(sourcePath,source,"A","grigio",a));
    DrawingWindow window;ok(window.createFromModel(sourcePath));
    DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.plane=Frame3(Vec3(),Vec3(0,0,1),Vec3(1,0,0));
    DrawingViewLayout layout;layout.viewId=view.id;layout.title="Vista";layout.position={100,100};layout.features={geometry.choices[0].id};layout.showMesh=true;
    ok(window.editor()->insertView(view,layout));ok(window.savePath(path));ok(window.pinModelRevision(a.version));ok(window.savePath(path));
    ok(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));ok(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,"T-A",""));
    const auto archived=window.document().baseCheckinId;
    auto *notice=window.findChild<QWidget *>("drawingModelUpdateNotice");auto *load=window.findChild<QPushButton *>("drawingLoadLatestModelButton");
    check(notice&&load&&notice->isHidden(),"no update when model is current");
    ok(checkoutWorkspaceDocument(sourcePath,source,"modellatore"));source.modelBodies[0].meshColor=QColor("#00aaff");ok(checkinWorkspaceDocument(sourcePath,source,"B","blu",b));
    window.show();QEvent activate(QEvent::WindowActivate);QApplication::sendEvent(&window,&activate);QApplication::processEvents();
    check(!notice->isHidden()&&!load->isEnabled(),"new checkin announced but read-only drawing needs checkout");
    check(window.document().sheet.drawing.model.checkinId==a.version.checkinId&&!window.isModified(),"detection does not change drawing");
    ok(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));check(load->isEnabled(),"checkout enables loading revision");
    const auto answer=[&](QMessageBox::StandardButton button){QTimer::singleShot(0,&window,[&,button]{auto *q=window.findChild<QMessageBox *>("drawingLatestModelQuestion");check(q,"update question displayed");q->done(button);});};
    answer(QMessageBox::No);ok(window.updateFromSource());check(window.document().sheet.drawing.model.checkinId==a.version.checkinId&&!notice->isHidden(),"decline preserves reference and warning");
    answer(QMessageBox::Yes);ok(window.updateFromSource());check(window.document().sheet.drawing.model.checkinId==b.version.checkinId&&notice->isHidden()&&window.isModified(),"accept applies latest and marks dirty");
    const auto blue=[&](DrawingWindow &w){for(auto *item:w.editor()->canvas()->scene()->items())if(item->data(0).toString()=="viewShading"){
        const auto image=qgraphicsitem_cast<QGraphicsPixmapItem *>(item)->pixmap().toImage();for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){auto p=image.pixelColor(x,y);if(p.alpha()>200&&p.blue()>p.red()+30)return true;}}
        return false;};
    check(blue(window),"accepted revision renders blue model color");ok(window.savePath(path));
    {DrawingWindow historical;ok(historical.openRevision(path,{window.document().sheet.drawing.version.documentId,archived}));check(historical.findChild<QWidget *>("drawingModelUpdateNotice")->isHidden()&&historical.document().sheet.drawing.model.checkinId==a.version.checkinId,"historical drawing remains fixed without update notice");}
    ok(checkoutWorkspaceDocument(sourcePath,source,"modellatore"));source.modelBodies[0].meshColor=QColor("#22bb77");ok(checkinWorkspaceDocument(sourcePath,source,"C","verde",c));
    QApplication::sendEvent(&window,&activate);QApplication::processEvents();check(!notice->isHidden(),"returning to drawing detects another checkin");load->click();check(window.document().sheet.drawing.model.checkinId==c.version.checkinId&&notice->isHidden(),"banner button loads new checkin");
    ok(window.useWorkingModel());check(notice->isHidden(),"working model based on latest does not report stale version");
    ok(checkoutWorkspaceDocument(sourcePath,source,"modellatore"));ok(checkinWorkspaceDocument(sourcePath,source,"D","successiva",d));QApplication::sendEvent(&window,&activate);QApplication::processEvents();
    check(!notice->isHidden(),"working model also detects newer archived base");ok(window.savePath(path));
    window.hide();
}
}
