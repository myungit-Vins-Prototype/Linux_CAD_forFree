#pragma once
#include "cad_drawing_sheet.h"
#include <QHash>
#include <QPainterPath>
#include <QTransform>
#include <algorithm>
#include <cmath>
namespace ForgeCad {
inline QString validateDrawingDimensionNumbering(const DrawingDimensionNumbering &n,qsizetype count){
    if(n.firstNumber<1||n.firstNumber>999999||!std::isfinite(n.diameter)||n.diameter<3||n.diameter>20
       ||!std::isfinite(n.rowTolerance)||n.rowTolerance<0||n.rowTolerance>20)
        return QStringLiteral("Numerazione quote: primo numero 1–999999, diametro 3–20 mm e tolleranza di riga 0–20 mm.");
    if(n.enabled&&count>1000000-n.firstNumber)return QStringLiteral("La numerazione delle quote supera 999999: ridurre il numero iniziale.");
    return {};
}
// Ordinamento totale prima per Y, poi righe a banda fissa: nessun confronto
// approssimato non transitivo e nessuna propagazione della tolleranza tra righe.
inline QHash<QUuid,int> drawingDimensionNumbers(const DrawingSheet &sheet){
    QHash<QUuid,int> out;if(!sheet.dimensionNumbering.enabled)return out;
    QVector<const DrawingDimensionLayout *> sorted;sorted.reserve(sheet.dimensions.size());
    for(const auto &d:sheet.dimensions)sorted.push_back(&d);
    const auto byX=[](const auto *a,const auto *b){if(a->position.x()!=b->position.x())return a->position.x()<b->position.x();if(a->position.y()!=b->position.y())return a->position.y()<b->position.y();return a->dimensionId<b->dimensionId;};
    std::sort(sorted.begin(),sorted.end(),[&](const auto *a,const auto *b){return a->position.y()!=b->position.y()?a->position.y()<b->position.y():byX(a,b);});
    int number=sheet.dimensionNumbering.firstNumber;
    for(qsizetype first=0;first<sorted.size();){qsizetype end=first+1;const auto top=sorted[first]->position.y();
        while(end<sorted.size()&&sorted[end]->position.y()-top<=sheet.dimensionNumbering.rowTolerance)++end;
        std::sort(sorted.begin()+first,sorted.begin()+end,byX);
        for(auto i=first;i<end;++i)out.insert(sorted[i]->dimensionId,number++);
        first=end;
    }return out;
}
// Numero e cerchio restano vettoriali e diritti anche accanto a quote ruotate.
inline QPainterPath drawingDimensionBadgeText(int number,double diameter,const DrawingStyle &style){
    QPainterPath text;text.addText(QPointF(0,0),drawingFont(style),QString::number(number));const auto bounds=text.boundingRect();
    if(bounds.isEmpty())return {};
    const auto scale=std::min({2.8/bounds.height(),diameter*.48/bounds.height(),diameter*.72/bounds.width()});
    text=QTransform::fromScale(scale,scale).map(text);text.translate(-text.boundingRect().center());return text;
}
}
