#pragma once
#include "cad_drawing_sheet.h"
#include <QHash>
#include <QPainterPath>
#include <QTransform>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
namespace ForgeCad {
inline QString validateDrawingDimensionNumbering(const DrawingDimensionNumbering &n,qsizetype count){
    if(n.firstNumber<1||n.firstNumber>999999||!std::isfinite(n.diameter)||n.diameter<3||n.diameter>20
       ||!std::isfinite(n.rowTolerance)||n.rowTolerance<0||n.rowTolerance>20)
        return QStringLiteral("Numerazione quote: primo numero 1–999999, diametro 3–20 mm e tolleranza di riga 0–20 mm.");
    if(n.enabled&&count>1000000-n.firstNumber)return QStringLiteral("La numerazione delle quote supera 999999: ridurre il numero iniziale.");
    if(n.persistent){
        if(n.primaryDirection<0||n.primaryDirection>3||n.secondaryDirection<0||n.secondaryDirection>3||n.primaryDirection%2==n.secondaryDirection%2||n.nextNumber<1||n.nextNumber>1000000||n.assignments.size()>100000||n.excludePatterns.size()>32)
            return QStringLiteral("Configurazione persistente dei badge non valida.");
        QSet<int> used;for(auto it=n.assignments.cbegin();it!=n.assignments.cend();++it){if(it.key().isNull()||it.value()<1||it.value()>=n.nextNumber||used.contains(it.value()))return QStringLiteral("Numero duplicato, identità nulla o progressivo non valido.");used.insert(it.value());}
        for(const auto &pattern:n.excludePatterns)if(pattern.isEmpty()||pattern.size()>128)return QStringLiteral("Pattern vuoto o oltre 128 caratteri.");
    }else if(!n.assignments.isEmpty()||!n.excludePatterns.isEmpty())return QStringLiteral("Metadati persistenti senza numerazione persistente.");
    return {};
}
inline bool drawingDimensionParenthesized(const DrawingDimensionPresentation &presentation){
    return presentation.prefix.trimmed().startsWith('(')&&presentation.suffix.trimmed().endsWith(')');
}
inline bool drawingDimensionNumberExcluded(const DrawingDimensionLayout &layout,const DrawingDimensionNumbering &numbering){
    for(const auto &pattern:numbering.excludePatterns){
        const QRegularExpression expression(QRegularExpression::wildcardToRegularExpression(pattern),QRegularExpression::CaseInsensitiveOption);
        if(expression.match(layout.presentation.prefix.trimmed()).hasMatch()||expression.match(layout.presentation.suffix.trimmed()).hasMatch())return true;
    }
    return layout.presentation.numberingExcluded||(numbering.excludeParenthesized&&drawingDimensionParenthesized(layout.presentation));
}
// Ordinamento totale prima per Y, poi righe a banda fissa: nessun confronto
// approssimato non transitivo e nessuna propagazione della tolleranza tra righe.
inline QHash<QUuid,int> drawingDimensionNumbers(const DrawingSheet &sheet){
    QHash<QUuid,int> out;if(!sheet.dimensionNumbering.enabled)return out;
    if(sheet.dimensionNumbering.persistent){for(const auto &d:sheet.dimensions)if(!drawingDimensionNumberExcluded(d,sheet.dimensionNumbering)&&sheet.dimensionNumbering.assignments.contains(d.dimensionId))out.insert(d.dimensionId,sheet.dimensionNumbering.assignments.value(d.dimensionId));return out;}
    QVector<const DrawingDimensionLayout *> sorted;sorted.reserve(sheet.dimensions.size());
    for(const auto &d:sheet.dimensions)sorted.push_back(&d);
    const auto byX=[](const auto *a,const auto *b){if(a->position.x()!=b->position.x())return a->position.x()<b->position.x();if(a->position.y()!=b->position.y())return a->position.y()<b->position.y();return a->dimensionId<b->dimensionId;};
    std::sort(sorted.begin(),sorted.end(),[&](const auto *a,const auto *b){return a->position.y()!=b->position.y()?a->position.y()<b->position.y():byX(a,b);});
    int number=sheet.dimensionNumbering.firstNumber;
    for(qsizetype first=0;first<sorted.size();){qsizetype end=first+1;const auto top=sorted[first]->position.y();
        while(end<sorted.size()&&sorted[end]->position.y()-top<=sheet.dimensionNumbering.rowTolerance)++end;
        std::sort(sorted.begin()+first,sorted.begin()+end,byX);
        // Le esclusioni sono solo visibilità: il numero resta riservato.
        for(auto i=first;i<end;++i){const auto *layout=sorted[i];if(!drawingDimensionNumberExcluded(*layout,sheet.dimensionNumbering))out.insert(layout->dimensionId,number);++number;}
        first=end;
    }return out;
}
// Direzione primaria dentro la banda; secondaria per l'avanzamento fra bande.
inline QVector<QUuid> drawingNumberingOrder(const DrawingSheet &sheet){
    const auto &n=sheet.dimensionNumbering;QVector<const DrawingDimensionLayout*> sorted;
    for(const auto &d:sheet.dimensions)if(!drawingDimensionNumberExcluded(d,n))sorted.append(&d);
    const auto coord=[](const DrawingDimensionLayout *d,int direction){return (direction%2?d->position.y():d->position.x())*(direction>=2?-1:1);};
    const auto along=[&](auto a,auto b){const auto x=coord(a,n.primaryDirection),y=coord(b,n.primaryDirection);return x!=y?x<y:a->dimensionId<b->dimensionId;};
    std::sort(sorted.begin(),sorted.end(),[&](auto a,auto b){const auto x=coord(a,n.secondaryDirection),y=coord(b,n.secondaryDirection);return x!=y?x<y:along(a,b);});
    for(qsizetype start=0;start<sorted.size();){auto end=start+1;const auto edge=coord(sorted[start],n.secondaryDirection);while(end<sorted.size()&&coord(sorted[end],n.secondaryDirection)-edge<=n.rowTolerance)++end;std::sort(sorted.begin()+start,sorted.begin()+end,along);start=end;}
    QVector<QUuid> out;for(auto d:sorted)out.append(d->dimensionId);
    const auto first=std::find(out.begin(),out.end(),n.firstDimension);if(first!=out.end())std::rotate(out.begin(),first,out.end());return out;
}
inline void freezeDrawingNumbering(DrawingSheet &sheet){
    auto &n=sheet.dimensionNumbering;if(n.persistent||!n.enabled)return;
    auto all=sheet;all.dimensionNumbering.excludeParenthesized=false;for(auto &d:all.dimensions)d.presentation.numberingExcluded=false;
    const auto numbers=drawingDimensionNumbers(all);for(auto it=numbers.cbegin();it!=numbers.cend();++it)n.assignments.insert(it.key(),it.value());
    n.persistent=true;n.nextNumber=n.firstNumber+sheet.dimensions.size();
}
inline QString extendDrawingNumbering(DrawingSheet &sheet){
    auto &n=sheet.dimensionNumbering;if(!n.persistent||!n.enabled)return {};
    for(const auto &id:drawingNumberingOrder(sheet))if(!n.assignments.contains(id)){if(n.nextNumber>999999)return QStringLiteral("Numeri dei badge esauriti.");n.assignments.insert(id,n.nextNumber++);}
    return validateDrawingDimensionNumbering(n,sheet.dimensions.size());
}
inline QString renumberDrawingDimensions(DrawingSheet &sheet){
    auto candidate=sheet;auto &n=candidate.dimensionNumbering;n.persistent=true;n.enabled=true;
    // L'operazione esplicita conserva le riserve delle quote nascoste o eliminate.
    for(const auto &id:drawingNumberingOrder(candidate))n.assignments.remove(id);
    QSet<int> reserved;for(auto value:n.assignments)reserved.insert(value);
    int number=n.firstNumber;for(const auto &id:drawingNumberingOrder(candidate)){while(reserved.contains(number))++number;if(number>999999)return QStringLiteral("Numeri dei badge esauriti.");n.assignments.insert(id,number++);}
    n.nextNumber=std::max(n.nextNumber,number);for(auto value:n.assignments)n.nextNumber=std::max(n.nextNumber,value+1);
    const auto error=validateDrawingDimensionNumbering(n,candidate.dimensions.size());if(error.isEmpty())sheet=std::move(candidate);return error;
}
inline QString changeDrawingDimensionNumber(DrawingSheet &sheet,const QUuid &id,int number,bool swap){
    auto candidate=sheet;freezeDrawingNumbering(candidate);auto &n=candidate.dimensionNumbering;
    if(!n.assignments.contains(id)||number<1||number>999999)return QStringLiteral("Quota senza numero o numero non valido.");
    const auto live=[&](const QUuid &key){return std::any_of(candidate.dimensions.cbegin(),candidate.dimensions.cend(),[&](const auto &d){return d.dimensionId==key;});};
    if(!live(id))return QStringLiteral("Quota eliminata.");
    for(auto it=n.assignments.begin();it!=n.assignments.end();++it)if(it.key()!=id&&it.value()==number){if(!swap||!live(it.key()))return QStringLiteral("Numero già riservato; per una quota esistente scegliere Scambia.");it.value()=n.assignments.value(id);break;}
    n.assignments[id]=number;n.nextNumber=std::max(n.nextNumber,number+1);sheet=std::move(candidate);return {};
}
inline QStringList drawingNumberingChanges(const DrawingSheet &before,const DrawingSheet &after){
    auto previous=before,current=after;freezeDrawingNumbering(previous);freezeDrawingNumbering(current);
    const auto oldVisible=drawingDimensionNumbers(before),newVisible=drawingDimensionNumbers(after);
    QSet<QUuid> oldIds,newIds;for(const auto &d:before.dimensions)oldIds.insert(d.dimensionId);for(const auto &d:after.dimensions)newIds.insert(d.dimensionId);
    auto ids=oldIds;ids.unite(newIds);QList<QUuid> sorted=ids.values();std::sort(sorted.begin(),sorted.end());QStringList changes;
    for(const auto &id:sorted){
        const int old=previous.dimensionNumbering.assignments.value(id),now=current.dimensionNumbering.assignments.value(id);
        if(old==now&&oldVisible.contains(id)==newVisible.contains(id)&&oldIds.contains(id)==newIds.contains(id))continue;
        const auto label=[](int number,bool visible){return number?QString::number(number)+(visible?QString():QStringLiteral(" (riservato)")):QStringLiteral("senza numero");};
        const QString status=!oldIds.contains(id)?QStringLiteral("aggiunta"):!newIds.contains(id)?QStringLiteral("eliminata"):oldVisible.contains(id)&&!newVisible.contains(id)?QStringLiteral("esclusa"):!oldVisible.contains(id)&&newVisible.contains(id)?QStringLiteral("attivata"):QStringLiteral("rinumerata");
        const auto &layouts=newIds.contains(id)?after.dimensions:before.dimensions;
        const auto layout=std::find_if(layouts.cbegin(),layouts.cend(),[&](const auto &d){return d.dimensionId==id;});
        const auto location=QStringLiteral("Quota a %1, %2 mm").arg(layout->position.x(),0,'f',1).arg(layout->position.y(),0,'f',1);
        changes.append(QStringLiteral("%1 · %2: %3 → %4").arg(location,status,label(old,oldVisible.contains(id)),label(now,newVisible.contains(id))));
    }return changes;
}
// Numero e cerchio restano vettoriali e diritti anche accanto a quote ruotate.
inline QPainterPath drawingDimensionBadgeText(int number,double diameter,const DrawingStyle &style){
    QPainterPath text;text.addText(QPointF(0,0),drawingFont(style),QString::number(number));const auto bounds=text.boundingRect();
    if(bounds.isEmpty())return {};
    const auto scale=std::min({2.8/bounds.height(),diameter*.48/bounds.height(),diameter*.72/bounds.width()});
    text=QTransform::fromScale(scale,scale).map(text);text.translate(-text.boundingRect().center());return text;
}
}
