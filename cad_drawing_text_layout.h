#pragma once
#include "cad_drawing_style.h"
#include <QGraphicsItem>
#include <QLineF>
#include <algorithm>

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
}
