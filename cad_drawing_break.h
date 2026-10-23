#pragma once
#include "cad_drawing_sheet.h"
#include "cad_drawing_visibility.h"
#include <QPainter>
#include <QLineF>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <limits>
namespace ForgeCad {
inline bool sameDrawingBreak(const DrawingViewLayout &a,const DrawingViewLayout &b){
    if(a.interruption.has_value()!=b.interruption.has_value())return false;
    if(!a.interruption)return true;
    const auto &x=*a.interruption,&y=*b.interruption;
    return x.vertical==y.vertical&&x.start==y.start&&x.end==y.end&&x.gap==y.gap;
}
inline QString validateDrawingBreak(const DrawingView &view,const DrawingViewLayout &layout){
    if(!layout.interruption)return {};
    const auto &b=*layout.interruption;const double span=(b.end-b.start)*view.scale;
    if(view.kind!=DrawingViewKind::ProjectedEdges||layout.relation.kind==DrawingDerivedKind::Detail)
        return QStringLiteral("L'interruzione richiede una vista proiettata, escluso il dettaglio.");
    if(!std::isfinite(b.start)||!std::isfinite(b.end)||!std::isfinite(b.gap)||!std::isfinite(view.scale)
       ||!std::isfinite(span)||!std::isfinite(b.start*view.scale)||!std::isfinite(b.end*view.scale)||b.end-b.start<=1e-7||b.gap<.5||b.gap>100||view.scale<=0||(b.end-b.start)*view.scale<=b.gap+1e-6)
        return QStringLiteral("Interruzione non valida: intervallo crescente e maggiore del distacco sul foglio (0,5–100 mm).");
    return {};
}
inline bool drawingBreakRemoves(const DrawingViewLayout &layout,QPointF model){
    if(!layout.interruption)return false;
    const auto &b=*layout.interruption;const double p=b.vertical?model.y():model.x();
    return p>b.start+1e-9&&p<b.end-1e-9;
}
inline QPointF drawingBreakToPaper(const DrawingView &view,const DrawingViewLayout &layout,QPointF model){
    if(drawingBreakRemoves(layout,model)){const auto n=std::numeric_limits<double>::quiet_NaN();return {n,n};}
    if(layout.interruption){const auto &b=*layout.interruption;auto p=b.vertical?model.y():model.x();
        if(p>=b.end-1e-9){p-=b.end-b.start-b.gap/view.scale;if(b.vertical)model.setY(p);else model.setX(p);}}
    return {model.x()*view.scale,-model.y()*view.scale};
}
inline QPointF drawingBreakFromPaper(const DrawingView &view,const DrawingViewLayout &layout,QPointF paper){
    QPointF model(paper.x()/view.scale,-paper.y()/view.scale);
    if(layout.interruption){const auto &b=*layout.interruption;auto p=b.vertical?model.y():model.x();const double gap=b.gap/view.scale;
        if(p>=b.start+gap)p+=b.end-b.start-gap;
        else if(p>b.start)p=p<b.start+gap/2?b.start:b.end;
        if(b.vertical)model.setY(p);else model.setX(p);}
    return model;
}
// Taglia solo polilinee di presentazione; non crea estremi o curve di misura.
inline QPainterPath drawingBreakPath(const QPainterPath &source,const DrawingView &view,const DrawingViewLayout &layout){
    if(!layout.interruption)return source;
    const auto &b=*layout.interruption;const double lo=(b.vertical?-b.end:b.start)*view.scale,hi=(b.vertical?-b.start:b.end)*view.scale;
    const double shift=hi-lo-b.gap;QPainterPath result;
    for(const auto &polygon:source.toSubpathPolygons(QTransform::fromScale(1000,1000)))for(int i=1;i<polygon.size();++i){
        const auto a=polygon[i-1]/1000.,d=polygon[i]/1000.-a;
        const double p=b.vertical?a.y():a.x(),delta=b.vertical?d.y():d.x();
        QVector<double> cuts{0,1};if(std::abs(delta)>1e-15)for(double edge:{lo,hi}){const auto t=(edge-p)/delta;if(t>0&&t<1)cuts.append(t);}
        std::sort(cuts.begin(),cuts.end());
        for(int j=1;j<cuts.size();++j){const double middle=p+delta*(cuts[j-1]+cuts[j])/2;
            if(middle>lo+1e-9&&middle<hi-1e-9)continue;
            QPointF offset;if(b.vertical&&middle<=lo+1e-9)offset.setY(shift);else if(!b.vertical&&middle>=hi-1e-9)offset.setX(-shift);
            const auto first=a+cuts[j-1]*d+offset,last=a+cuts[j]*d+offset;
            if(result.isEmpty()||QLineF(result.currentPosition(),first).length()>1e-8)result.moveTo(first);
            result.lineTo(last);
        }
    }
    return result;
}
inline QRectF drawingBreakBounds(const DrawingViewVisibility &display){
    QRectF result;for(const auto *paths:{&display.curves,&display.silhouettes})for(const auto &p:*paths)for(const auto &path:{p.visible,p.hidden})if(!path.isEmpty())result=result.isNull()?path.boundingRect():result.united(path.boundingRect());return result;
}
inline QString drawingBreakFits(const DrawingView &view,const DrawingViewLayout &layout,const QRectF &bounds){
    const auto error=validateDrawingBreak(view,layout);if(!error.isEmpty()||!layout.interruption)return error;
    const auto &b=*layout.interruption;const double low=b.vertical?-bounds.bottom()/view.scale:bounds.left()/view.scale,high=b.vertical?-bounds.top()/view.scale:bounds.right()/view.scale;
    return b.start>low+1e-7&&b.end<high-1e-7?QString():QStringLiteral("Entrambi i limiti dell'interruzione devono essere interni alla geometria proiettata.");
}
inline void applyDrawingBreak(const DrawingView &view,const DrawingViewLayout &layout,DrawingViewVisibility &display){
    if(!layout.interruption)return;
    const auto bounds=drawingBreakBounds(display);const auto problem=drawingBreakFits(view,layout,bounds);
    if(!problem.isEmpty()){display.warnings.append(problem);return;}
    const auto &b=*layout.interruption;
    for(auto *paths:{&display.curves,&display.silhouettes})for(auto &p:*paths){p.visible=drawingBreakPath(p.visible,view,layout);p.hidden=drawingBreakPath(p.hidden,view,layout);}
    for(auto *path:{&display.sectionHatch,&display.visibleCylinderAxes,&display.allCylinderAxes,&display.visibleCenters.crosses,&display.visibleCenters.axes,&display.allCenters.crosses,&display.allCenters.axes})*path=drawingBreakPath(*path,view,layout);
    const double lo=(b.vertical?-b.end:b.start)*view.scale,hi=(b.vertical?-b.start:b.end)*view.scale,shift=hi-lo-b.gap;
    // Due ritagli della stessa immagine, traslati come le curve; nessuna nuova tassellazione.
    if(!display.shadedImage.isNull()){
        const auto old=display.shadedBounds;const auto source=display.shadedImage;
        QRectF first=old,second=old;if(b.vertical){first.setBottom(std::min(old.bottom(),lo));second.setTop(std::max(old.top(),hi));}else{first.setRight(std::min(old.right(),lo));second.setLeft(std::max(old.left(),hi));}
        const QPointF delta=b.vertical?QPointF(0,shift):QPointF(-shift,0);
        const auto target1=first.translated(b.vertical?delta:QPointF()),target2=second.translated(b.vertical?QPointF():delta);
        const auto combined=target1.united(target2);const double sx=source.width()/old.width(),sy=source.height()/old.height();
        QImage image(QSize(std::max(1,int(std::ceil(combined.width()*sx))),std::max(1,int(std::ceil(combined.height()*sy)))),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        QPainter painter(&image);painter.scale(sx,sy);painter.translate(-combined.topLeft());
        for(const auto &pair:{qMakePair(first,target1),qMakePair(second,target2)})if(pair.first.width()>0&&pair.first.height()>0){
            const auto r=pair.first;const QRectF pixels((r.left()-old.left())*sx,(r.top()-old.top())*sy,r.width()*sx,r.height()*sy);painter.drawImage(pair.second,source,pixels);}
        painter.end();display.shadedImage=std::move(image);display.shadedBounds=combined;
    }
    for(double edge:{lo+(b.vertical?shift:0),hi-(b.vertical?0:shift)}){
        const double first=b.vertical?bounds.left()-1:bounds.top()-1,last=b.vertical?bounds.right()+1:bounds.bottom()+1;
        const int pieces=std::max(2,int(std::min(2000.,std::ceil((last-first)/2))));
        for(int i=0;i<=pieces;++i){const double t=first+(last-first)*i/pieces,offset=(i==0||i==pieces)?0:(i%2?.5:-.5);
            const QPointF point=b.vertical?QPointF(t,edge+offset):QPointF(edge+offset,t);if(i)display.breakMarks.lineTo(point);else display.breakMarks.moveTo(point);}
    }
}
}
