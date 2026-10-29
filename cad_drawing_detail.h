#pragma once
#include "cad_drawing_sheet.h"
#include "cad_drawing_visibility.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QLineF>
#include <QTransform>
#include <algorithm>
#include <cmath>
namespace ForgeCad {
inline bool drawingIsDetail(const DrawingViewLayout &layout){return layout.relation.kind==DrawingDerivedKind::Detail;}
inline bool sameDrawingDetail(const DrawingViewLayout &a,const DrawingViewLayout &b){
    return drawingIsDetail(a)==drawingIsDetail(b)&&(!drawingIsDetail(a)||(a.relation.detailCenter==b.relation.detailCenter&&a.relation.detailRadius==b.relation.detailRadius&&a.relation.detailOutline==b.relation.detailOutline));
}
inline QPainterPath drawingDetailBoundary(const DrawingView &view,const DrawingViewLayout &layout){
    const auto &r=layout.relation;QPainterPath path;
    if(r.detailOutline.isEmpty()){const QPointF c(r.detailCenter.x()*view.scale,-r.detailCenter.y()*view.scale);const double radius=r.detailRadius*view.scale;path.addEllipse(c,radius,radius);}
    else{for(int i=0;i<r.detailOutline.size();++i){const auto p=r.detailOutline[i];const QPointF q(p.x()*view.scale,-p.y()*view.scale);if(i==0)path.moveTo(q);else path.lineTo(q);}path.closeSubpath();}
    return path;
}
inline QRectF drawingDetailBounds(const DrawingView &view,const DrawingViewLayout &layout){
    return drawingDetailBoundary(view,layout).boundingRect();
}
// Ritaglio delle sole linee campionate di presentazione. Nessun nuovo arco/edge di misura.
inline QPainterPath clipDrawingDetailPath(const QPainterPath &source,const QPointF &center,double radius){
    QPainterPath result;const double radius2=radius*radius;
    // Appiattimento a scala elevata per mantenere fini i contorni sul foglio.
    const auto polygons=source.toSubpathPolygons(QTransform::fromScale(1000,1000));
    for(const auto &poly:polygons)for(int i=1;i<poly.size();++i){
        const auto a=poly[i-1]/1000.,b=poly[i]/1000.,delta=b-a,offset=a-center;
        const double aa=QPointF::dotProduct(delta,delta);if(aa<1e-24)continue;
        const double bb=2*QPointF::dotProduct(offset,delta),cc=QPointF::dotProduct(offset,offset)-radius2;
        double low=0,high=1;
        if(cc>0||QPointF::dotProduct(b-center,b-center)>radius2){
            const double discriminant=bb*bb-4*aa*cc;if(discriminant<=0)continue;
            const double root=std::sqrt(discriminant);low=std::max(0.,(-bb-root)/(2*aa));high=std::min(1.,(-bb+root)/(2*aa));if(high-low<=1e-12)continue;
        }
        const auto first=a+low*delta,last=a+high*delta;
        if(result.isEmpty()||QLineF(result.currentPosition(),first).length()>1e-8)result.moveTo(first);
        result.lineTo(last);
    }
    return result;
}
// Segmenti spezzati soltanto alle intersezioni: non aggiunge il bordo del ritaglio alle entita'.
inline QPainterPath clipDrawingDetailPath(const QPainterPath &source,const QPainterPath &boundary){
    QPainterPath result;const auto borders=boundary.toSubpathPolygons();
    for(const auto &poly:source.toSubpathPolygons(QTransform::fromScale(1000,1000)))for(int i=1;i<poly.size();++i){
        const auto a=poly[i-1]/1000.,b=poly[i]/1000.,delta=b-a;const double length2=QPointF::dotProduct(delta,delta);if(length2<1e-24)continue;QVector<double> cuts{0,1};
        for(const auto &border:borders)for(int j=1;j<border.size();++j){QPointF hit;if(QLineF(a,b).intersects(QLineF(border[j-1],border[j]),&hit)==QLineF::BoundedIntersection)cuts.append(std::clamp(QPointF::dotProduct(hit-a,delta)/length2,0.,1.));
            // Include estremi dei bordi collineari e segmenti appoggiati al contorno.
            for(const auto p:{border[j-1],border[j]}){const double t=QPointF::dotProduct(p-a,delta)/length2;if(t>0&&t<1&&QLineF(p,a+t*delta).length()<1e-8)cuts.append(t);}}
        std::sort(cuts.begin(),cuts.end());for(int j=1;j<cuts.size();++j){if(cuts[j]-cuts[j-1]<1e-12)continue;const auto middle=a+.5*(cuts[j]+cuts[j-1])*delta;
            bool inside=boundary.contains(middle);if(!inside)for(const auto &border:borders)for(int k=1;k<border.size();++k){const auto v=border[k]-border[k-1];const double vv=QPointF::dotProduct(v,v);if(vv>0){const double t=std::clamp(QPointF::dotProduct(middle-border[k-1],v)/vv,0.,1.);if(QLineF(middle,border[k-1]+t*v).length()<1e-8)inside=true;}}
            if(!inside)continue;
            const auto first=a+cuts[j-1]*delta,last=a+cuts[j]*delta;if(result.isEmpty()||QLineF(result.currentPosition(),first).length()>1e-8)result.moveTo(first);result.lineTo(last);
        }
    }return result;
}
inline QPainterPath clipDrawingDetailPath(const QPainterPath &source,const DrawingView &view,const DrawingViewLayout &layout,QPointF offset={}){
    if(layout.relation.detailOutline.isEmpty()){const auto box=drawingDetailBounds(view,layout).translated(offset);return clipDrawingDetailPath(source,box.center(),box.width()/2);}
    return clipDrawingDetailPath(source,drawingDetailBoundary(view,layout).translated(offset));
}
inline bool drawingDetailContains(const DrawingView &view,const DrawingViewLayout &layout,QPointF point){
    const auto boundary=drawingDetailBoundary(view,layout);if(boundary.contains(point))return true;
    QPainterPathStroker stroke;stroke.setWidth(2e-8);return stroke.createStroke(boundary).contains(point);
}
inline void clipDrawingDetailVisibility(const DrawingView &view,const DrawingViewLayout &layout,DrawingViewVisibility &display){
    if(!drawingIsDetail(layout))return;
    const auto boundary=drawingDetailBoundary(view,layout);const auto box=boundary.boundingRect();
    const auto clip=[&](QPainterPath &path){path=clipDrawingDetailPath(path,view,layout);};
    for(auto *paths:{&display.curves,&display.silhouettes})for(auto &path:*paths){clip(path.visible);clip(path.hidden);}
    clip(display.sectionHatch);display.sectionMaterial=display.sectionMaterial.intersected(boundary);
    clip(display.visibleCylinderAxes);clip(display.allCylinderAxes);
    clip(display.visibleCenters.crosses);clip(display.visibleCenters.axes);clip(display.allCenters.crosses);clip(display.allCenters.axes);
    if(!display.shadedImage.isNull()){
        const auto old=display.shadedBounds;const double sx=display.shadedImage.width()/old.width(),sy=display.shadedImage.height()/old.height();
        const QRect pixels=QRectF((box.left()-old.left())*sx,(box.top()-old.top())*sy,box.width()*sx,box.height()*sy).toAlignedRect().intersected(display.shadedImage.rect());
        if(pixels.isEmpty()){display.shadedImage={};display.shadedBounds={};return;}
        const auto cropped=display.shadedImage.copy(pixels);const QRectF bounds(old.left()+pixels.left()/sx,old.top()+pixels.top()/sy,pixels.width()/sx,pixels.height()/sy);
        QImage masked(cropped.size(),QImage::Format_ARGB32_Premultiplied);masked.fill(Qt::transparent);QPainter painter(&masked);painter.setRenderHint(QPainter::Antialiasing);
        QTransform transform;transform.scale(sx,sy);transform.translate(-bounds.left(),-bounds.top());const auto mask=transform.map(boundary);painter.setClipPath(mask);painter.drawImage(0,0,cropped);painter.end();display.shadedImage=std::move(masked);display.shadedBounds=bounds;
    }
}
}
