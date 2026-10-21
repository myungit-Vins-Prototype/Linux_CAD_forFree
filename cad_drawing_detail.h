#pragma once
#include "cad_drawing_sheet.h"
#include "cad_drawing_visibility.h"
#include <QPainter>
#include <QLineF>
#include <QTransform>
#include <algorithm>
#include <cmath>
namespace ForgeCad {
inline bool drawingIsDetail(const DrawingViewLayout &layout){return layout.relation.kind==DrawingDerivedKind::Detail;}
inline bool sameDrawingDetail(const DrawingViewLayout &a,const DrawingViewLayout &b){
    return drawingIsDetail(a)==drawingIsDetail(b)&&(!drawingIsDetail(a)||(a.relation.detailCenter==b.relation.detailCenter&&a.relation.detailRadius==b.relation.detailRadius));
}
inline QRectF drawingDetailBounds(const DrawingView &view,const DrawingViewLayout &layout){
    const auto &r=layout.relation;const QPointF center(r.detailCenter.x()*view.scale,-r.detailCenter.y()*view.scale);const double radius=r.detailRadius*view.scale;
    return {center-QPointF(radius,radius),QSizeF(2*radius,2*radius)};
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
inline void clipDrawingDetailVisibility(const DrawingView &view,const DrawingViewLayout &layout,DrawingViewVisibility &display){
    if(!drawingIsDetail(layout))return;
    const auto box=drawingDetailBounds(view,layout);const auto center=box.center();const double radius=box.width()/2;
    const auto clip=[&](QPainterPath &path){path=clipDrawingDetailPath(path,center,radius);};
    for(auto *paths:{&display.curves,&display.silhouettes})for(auto &path:*paths){clip(path.visible);clip(path.hidden);}
    clip(display.sectionHatch);QPainterPath boundary;boundary.addEllipse(box);display.sectionMaterial=display.sectionMaterial.intersected(boundary);
    clip(display.visibleCylinderAxes);clip(display.allCylinderAxes);
    clip(display.visibleCenters.crosses);clip(display.visibleCenters.axes);clip(display.allCenters.crosses);clip(display.allCenters.axes);
    if(!display.shadedImage.isNull()){
        const auto old=display.shadedBounds;const double sx=display.shadedImage.width()/old.width(),sy=display.shadedImage.height()/old.height();
        const QRect pixels=QRectF((box.left()-old.left())*sx,(box.top()-old.top())*sy,box.width()*sx,box.height()*sy).toAlignedRect().intersected(display.shadedImage.rect());
        if(pixels.isEmpty()){display.shadedImage={};display.shadedBounds={};return;}
        const auto cropped=display.shadedImage.copy(pixels);const QRectF bounds(old.left()+pixels.left()/sx,old.top()+pixels.top()/sy,pixels.width()/sx,pixels.height()/sy);
        QImage masked(cropped.size(),QImage::Format_ARGB32_Premultiplied);masked.fill(Qt::transparent);QPainter painter(&masked);painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath mask;mask.addEllipse(QRectF((box.left()-bounds.left())*sx,(box.top()-bounds.top())*sy,box.width()*sx,box.height()*sy));painter.setClipPath(mask);painter.drawImage(0,0,cropped);painter.end();display.shadedImage=std::move(masked);display.shadedBounds=bounds;
    }
}
}
