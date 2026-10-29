#pragma once
#include "cad_drawing_style.h"
#include <QGraphicsItem>
#include <QLineF>
#include <algorithm>
#include <array>
#include <vector>

namespace ForgeCad {
// Il punto di appoggio della linea di quota e' indipendente dalla posizione
// del testo. Il verso di lettura evita valori capovolti anche per assi invertiti.
inline void placeDrawingDimensionText(QGraphicsItem *text,const DrawingStyle &style,QPointF anchor,QPointF direction){
    double length=std::hypot(direction.x(),direction.y());if(length<1e-10){direction={1,0};length=1;}direction/=length;
    if(direction.x()<-1e-10||(std::abs(direction.x())<=1e-10&&direction.y()>0))direction=-direction;
    const double angle=style.dimensionTextOrientation==DrawingTextOrientation::Aligned?std::atan2(direction.y(),direction.x())*180/std::acos(-1.):0;
    text->setRotation(angle);text->setPos(0,0);
    const auto bounds=text->boundingRect();const auto center=text->mapToScene(bounds.center());const QPointF above(direction.y(),-direction.x());
    double extent=0;for(auto corner:{bounds.topLeft(),bounds.topRight(),bounds.bottomLeft(),bounds.bottomRight()})extent=std::max(extent,std::abs(QPointF::dotProduct(text->mapToScene(corner)-center,above)));
    const double side=style.dimensionTextPosition==DrawingTextPosition::Above?1:style.dimensionTextPosition==DrawingTextPosition::Below?-1:0;
    text->setPos(anchor+side*(extent+style.dimensionTextGap)*above-center);
}
inline QPainterPath drawingDimensionTextMask(const QGraphicsItem *text){
    const double scale=std::max(.001,std::abs(text->scale()));QPainterPath path;path.addRect(text->boundingRect().adjusted(-.6/scale,-.3/scale,.6/scale,.3/scale));return text->mapToScene(path);
}
// Ritaglia soltanto i tratti della quota sotto il proprio testo trasparente.
inline QPainterPath drawingDimensionStroke(const QPainterPath &path,const QPainterPath &mask){
    if(mask.isEmpty())return path;
    QPainterPath out;const auto polygon=mask.toFillPolygon();
    for(const auto &sub:path.toSubpathPolygons())for(int i=1;i<sub.size();++i){
        const QLineF line(sub[i-1],sub[i]);if(line.length()<1e-12)continue;
        std::vector<double> cuts{0,1};
        for(int j=1;j<polygon.size();++j){QPointF hit;if(line.intersects(QLineF(polygon[j-1],polygon[j]),&hit)==QLineF::BoundedIntersection){const auto d=line.p2()-line.p1();cuts.push_back(std::clamp(QPointF::dotProduct(hit-line.p1(),d)/QPointF::dotProduct(d,d),0.,1.));}}
        std::sort(cuts.begin(),cuts.end());
        for(std::size_t j=1;j<cuts.size();++j)if(cuts[j]-cuts[j-1]>1e-10&&!mask.contains(line.pointAt((cuts[j]+cuts[j-1])/2))){out.moveTo(line.pointAt(cuts[j-1]));out.lineTo(line.pointAt(cuts[j]));}
    }return out;
}
inline std::array<QPointF,2> drawingDimensionLineEnds(QPointF a,QPointF b,QPointF anchor,QPointF tangent,double slant){
    const double length=std::hypot(tangent.x(),tangent.y());if(length<1e-10)return {a,b};tangent/=length;
    const QPointF normal(-tangent.y(),tangent.x()),direction=normal+std::tan(slant*std::acos(-1.)/180)*tangent;
    return {a+QPointF::dotProduct(anchor-a,normal)*direction,b+QPointF::dotProduct(anchor-b,normal)*direction};
}

}
