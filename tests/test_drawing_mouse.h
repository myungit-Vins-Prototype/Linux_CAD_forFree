#pragma once
#include "../cad_drawing_file.h"
#include <QGraphicsPathItem>
#include <QMouseEvent>
#include <QElapsedTimer>
namespace {
void drawingMouseProfileTests(const QString &path){
    using namespace ForgeCad;DocumentState document;auto error=loadDocumentFile(path,document);require(error.isEmpty(),qPrintable(error));DrawingEditorModel model;error=prepareDrawingEditorModel(document,model);require(error.isEmpty(),qPrintable(error));
    DrawingSheet sheet;sheet.drawing.model=model.snapshot.version;DrawingSheetEditor editor(sheet,model);editor.resize(1200,840);editor.show();QApplication::processEvents();editor.fitSheet();
    for(int orientation=1;orientation<=2;++orientation){DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.scale=2;view.plane=orientation==1?Kernel::Frame3(Kernel::Vec3(),Kernel::Vec3(0,-1,0),Kernel::Vec3(1,0,0)):Kernel::Frame3(Kernel::Vec3(),Kernel::Vec3(1,0,0),Kernel::Vec3(0,1,0));
        DrawingViewLayout layout{view.id,orientation==1?QStringLiteral("Mouse - frontale"):QStringLiteral("Mouse - laterale"),QPointF(140,105),{}};for(const auto &source:model.snapshot.bodies)layout.features.append(source.featureId);
        QElapsedTimer timer;timer.start();error=editor.beginViewPlacement(view,layout);require(error.isEmpty()&&editor.hasViewPlacement(),qPrintable(error));
        const auto preparation=timer.elapsed();timer.restart();auto *canvas=editor.canvas();const QPointF local=canvas->mapFromScene(QPointF(148.5,105)),global=canvas->viewport()->mapToGlobal(local.toPoint());QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&press);QApplication::sendEvent(canvas->viewport(),&release);require(!editor.hasViewPlacement()&&editor.sheet().drawing.views.size()==1,"inserimento della vista del modello reale");
        QRectF edges,contours;bool hasEdges=false,hasContours=false;int count=0;
        for(auto *item:canvas->scene()->items())if(auto *curve=qgraphicsitem_cast<QGraphicsPathItem *>(item)){const auto kind=item->data(0).toString();if(kind=="projection"){edges=hasEdges?edges.united(curve->path().boundingRect()):curve->path().boundingRect();hasEdges=true;}else if(kind=="silhouette"){contours=hasContours?contours.united(curve->path().boundingRect()):curve->path().boundingRect();hasContours=true;++count;require(!(item->flags()&QGraphicsItem::ItemIsSelectable),"contorno apparente non usato per false quote");}}
        require(hasEdges&&hasContours&&contours.top()<edges.top()-.5*view.scale,"profilo superiore recuperato oltre i bordi della vista");const auto origin=editor.sheet().views[0].position;
        std::cout<<"Mouse view "<<orientation<<" prepareMs="<<preparation<<" placeMs="<<timer.elapsed()<<" contours="<<count<<" edgeUpper="<<(origin.y()-edges.top())/view.scale<<" silhouetteUpper="<<(origin.y()-contours.top())/view.scale<<'\n';
        QApplication::processEvents();const auto prefix=qEnvironmentVariable("FORGECAD_MOUSE_DRAWING_PREVIEW");if(!prefix.isEmpty())require(editor.grab().save(prefix+QString::number(orientation)+".png"),"anteprima Mouse del renderer definitivo");
        for(auto *item:canvas->scene()->items())require(!item->data(4).toBool(),"nessun tratto nascosto mostrato per default sul Mouse");
        timer.restart();error=editor.setHiddenLinesVisible(view.id,true);require(error.isEmpty(),qPrintable(error));QApplication::processEvents();const auto toggleMs=timer.elapsed();int hidden=0;for(auto *item:canvas->scene()->items())if(item->data(4).toBool())++hidden;require(hidden>0,"tratti nascosti disponibili su richiesta nel Mouse reale");
        std::cout<<"Mouse view "<<orientation<<" showHiddenMs="<<toggleMs<<" hiddenItems="<<hidden<<'\n';
        if(!prefix.isEmpty())require(editor.grab().save(prefix+QString::number(orientation)+"-hidden.png"),"anteprima Mouse con linee nascoste");
        error=editor.setHiddenLinesVisible(view.id,false);require(error.isEmpty(),qPrintable(error));QApplication::processEvents();
        error=editor.removeView(view.id);require(error.isEmpty(),qPrintable(error));
    }
}
}
