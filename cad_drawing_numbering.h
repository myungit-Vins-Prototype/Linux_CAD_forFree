#pragma once
#include "cad_drawing_sheet.h"
#include <QHash>
#include <QPainterPath>
#include <QTransform>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
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
// Ordine spaziale di un singolo gruppo; la rotazione resta interna al gruppo.
// Le bande legacy servono anche a ordinare le origini delle viste.
inline QVector<QUuid> drawingNumberingSpatialOrder(QVector<const DrawingDimensionLayout*> sorted,const DrawingDimensionNumbering &n,bool guided){
    const auto coord=[](const DrawingDimensionLayout *d,int direction){return (direction%2?d->position.y():d->position.x())*(direction>=2?-1:1);};
    const auto before=[&](auto a,auto b,int direction){
        const auto x=coord(a,direction),y=coord(b,direction);if(x!=y)return x<y;
        const auto u=coord(a,direction==n.primaryDirection?n.secondaryDirection:n.primaryDirection),v=coord(b,direction==n.primaryDirection?n.secondaryDirection:n.primaryDirection);
        return u!=v?u<v:a->dimensionId<b->dimensionId;
    };
    const auto along=[&](auto a,auto b){return before(a,b,n.primaryDirection);};
    const auto across=[&](auto a,auto b){return before(a,b,n.secondaryDirection);};
    QVector<QUuid> out;out.reserve(sorted.size());
    if(guided&&sorted.size()>2){
        // Scala locale stimata dai vicini nelle due proiezioni ordinate: O(N log N),
        // senza confrontare tutte le coppie. La mediana limita l'effetto degli isolati.
        QHash<QUuid,double> nearest;
        for(auto d:sorted)nearest.insert(d->dimensionId,std::numeric_limits<double>::infinity());
        for(int direction:{n.primaryDirection,n.secondaryDirection}){
            std::sort(sorted.begin(),sorted.end(),[&](auto a,auto b){return before(a,b,direction);});
            for(qsizetype i=1;i<sorted.size();++i){const auto delta=sorted[i]->position-sorted[i-1]->position;const double squared=QPointF::dotProduct(delta,delta);
                for(auto d:{sorted[i-1],sorted[i]})nearest[d->dimensionId]=std::min(nearest.value(d->dimensionId),squared);}
        }
        QVector<double> distances;distances.reserve(nearest.size());for(double squared:nearest)distances.append(squared);
        const auto median=distances.begin()+distances.size()/2;std::nth_element(distances.begin(),median,distances.end());
        const double proximity=std::max(n.rowTolerance,std::sqrt(*median)*.5);
        struct Region{QVector<const DrawingDimensionLayout*> dimensions;int depth=0;};
        QVector<Region> pending;pending.append({std::move(sorted),0});
        while(!pending.isEmpty()){
            auto region=pending.takeLast();auto &primary=region.dimensions;auto secondary=primary;
            std::sort(primary.begin(),primary.end(),along);std::sort(secondary.begin(),secondary.end(),across);
            const auto gap=[&](const auto &items,int direction){QPair<double,qsizetype> widest{0,0};
                for(qsizetype i=1;i<items.size();++i){const double width=coord(items[i],direction)-coord(items[i-1],direction);if(width>widest.first)widest={width,i};}return widest;};
            const auto p=gap(primary,n.primaryDirection),s=gap(secondary,n.secondaryDirection);
            // A vuoti simili prevale il secondo verso: si completa prima la scansione principale.
            const double gapTolerance=std::max(n.rowTolerance,proximity*.25);
            const bool splitPrimary=p.first>s.first+gapTolerance;const auto cut=splitPrimary?p:s;
            if(cut.first>proximity&&region.depth<32){
                auto &items=splitPrimary?primary:secondary;
                // Pila esplicita e profondità limitata anche su impaginazioni sbilanciate.
                pending.append({items.mid(cut.second),region.depth+1});pending.append({items.mid(0,cut.second),region.depth+1});continue;
            }
            // Bande locali ancorate al primo testo, senza concatenare tolleranze.
            for(qsizetype start=0;start<primary.size();){auto end=start+1;const double edge=coord(primary[start],n.primaryDirection);
                while(end<primary.size()&&coord(primary[end],n.primaryDirection)-edge<=proximity)++end;
                std::sort(primary.begin()+start,primary.begin()+end,across);for(auto i=start;i<end;++i)out.append(primary[i]->dimensionId);start=end;}
        }
    }else{
        std::sort(sorted.begin(),sorted.end(),across);
        for(qsizetype start=0;start<sorted.size();){auto end=start+1;const auto edge=coord(sorted[start],n.secondaryDirection);while(end<sorted.size()&&coord(sorted[end],n.secondaryDirection)-edge<=n.rowTolerance)++end;std::sort(sorted.begin()+start,sorted.begin()+end,along);start=end;}
        for(auto d:sorted)out.append(d->dimensionId);
    }
    if(!n.firstDimension.isNull()){const auto first=std::find(out.begin(),out.end(),n.firstDimension);if(first!=out.end())std::rotate(out.begin(),first,out.end());}return out;
}
// Le quote di ciascuna vista formano sempre un blocco contiguo, anche quando
// la prima quota è interna o i testi di viste diverse si sovrappongono.
inline QVector<QUuid> drawingNumberingOrder(const DrawingSheet &sheet){
    const auto &n=sheet.dimensionNumbering;
    QHash<QUuid,QUuid> dimensionViews;for(const auto &d:sheet.drawing.dimensions)dimensionViews.insert(d.id,d.viewId);
    QHash<QUuid,QVector<const DrawingDimensionLayout*>> groups;
    QUuid firstView;bool guided=false;
    for(const auto &d:sheet.dimensions)if(!drawingDimensionNumberExcluded(d,n)){
        const auto view=dimensionViews.value(d.dimensionId);groups[view].append(&d);
        if(d.dimensionId==n.firstDimension){firstView=view;guided=true;}
    }
    QHash<QUuid,QPointF> positions;for(const auto &v:sheet.views)positions.insert(v.viewId,v.position);
    QVector<DrawingDimensionLayout> anchors;anchors.reserve(groups.size());
    for(auto it=groups.cbegin();it!=groups.cend();++it){
        // Se manca il layout di una vista orfana, usa il centro dei suoi testi.
        QPointF origin=it.value().front()->position;double left=origin.x(),right=left,top=origin.y(),bottom=top;
        if(positions.contains(it.key()))origin=positions.value(it.key());
        else{for(auto d:it.value()){left=std::min(left,d->position.x());right=std::max(right,d->position.x());top=std::min(top,d->position.y());bottom=std::max(bottom,d->position.y());}origin={left*.5+right*.5,top*.5+bottom*.5};}
        anchors.append({it.key(),origin});
    }
    QVector<const DrawingDimensionLayout*> viewAnchors;viewAnchors.reserve(anchors.size());for(const auto &anchor:anchors)viewAnchors.append(&anchor);
    auto viewNumbering=n;viewNumbering.firstDimension={};
    auto viewOrder=drawingNumberingSpatialOrder(std::move(viewAnchors),viewNumbering,false);
    if(guided){const auto first=std::find(viewOrder.begin(),viewOrder.end(),firstView);if(first!=viewOrder.end())std::rotate(viewOrder.begin(),first,viewOrder.end());}
    QVector<QUuid> out;out.reserve(sheet.dimensions.size());
    for(const auto &view:viewOrder)out+=drawingNumberingSpatialOrder(groups.value(view),n,guided);
    return out;
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
    const auto order=drawingNumberingOrder(candidate);
    for(const auto &id:order)n.assignments.remove(id);
    QSet<int> reserved;for(auto value:n.assignments)reserved.insert(value);
    int number=n.firstNumber;for(const auto &id:order){while(reserved.contains(number))++number;if(number>999999)return QStringLiteral("Numeri dei badge esauriti.");n.assignments.insert(id,number++);}
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
