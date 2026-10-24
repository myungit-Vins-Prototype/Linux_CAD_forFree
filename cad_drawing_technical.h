#pragma once
#include "cad_drawing_sheet.h"
#include <QFontMetricsF>
#include <QPainterPath>
#include <QSet>
#include <QTransform>
#include <algorithm>
#include <cmath>
namespace ForgeCad {
inline QString drawingGeometricName(DrawingGeometricCharacteristic c){
    switch(c){case DrawingGeometricCharacteristic::Straightness:return QStringLiteral("Rettilineità");case DrawingGeometricCharacteristic::Flatness:return QStringLiteral("Planarità");case DrawingGeometricCharacteristic::Circularity:return QStringLiteral("Circolarità");case DrawingGeometricCharacteristic::Parallelism:return QStringLiteral("Parallelismo");case DrawingGeometricCharacteristic::Perpendicularity:return QStringLiteral("Perpendicolarità");case DrawingGeometricCharacteristic::Position:return QStringLiteral("Posizione");}return {};
}
inline QString drawingTechnicalNumber(double value){auto text=QString::number(value,'f',6);while(text.endsWith('0'))text.chop(1);if(text.endsWith('.'))text.chop(1);return text;}
inline const DrawingAnnotation *drawingDatumAnnotation(const DrawingSheet &sheet,const QUuid &id){
    for(const auto &n:sheet.annotations)if(n.id==id&&n.technical&&n.technical->kind==DrawingTechnicalKind::Datum)return &n;
    return nullptr;
}
inline QString validateDrawingTechnicalAnnotation(const DrawingAnnotation &note,const DrawingSheet *sheet=nullptr,bool requireAnchor=true){
    if(!note.technical)return {};
    const auto &t=*note.technical;
    if(!note.text.isEmpty()||int(t.kind)<0||int(t.kind)>1)return QStringLiteral("Dati dell’annotazione strutturata non validi.");
    if(requireAnchor&&!note.reference)return QStringLiteral("Il simbolo tecnico richiede un aggancio geometrico.");
    if(t.kind==DrawingTechnicalKind::Datum){
        if(t.label.isEmpty()||t.label.size()>3||std::any_of(t.label.cbegin(),t.label.cend(),[](QChar c){return c<'A'||c>'Z';}))return QStringLiteral("Il datum richiede da una a tre lettere A–Z.");
        if(t.characteristic!=DrawingGeometricCharacteristic::Straightness||t.tolerance!=0||t.diameter||!t.datums.isEmpty())return QStringLiteral("Parametri di tolleranza estranei al datum.");
        if(sheet)for(const auto &n:sheet->annotations)if(n.id!=note.id&&n.technical&&n.technical->kind==DrawingTechnicalKind::Datum&&n.technical->label==t.label)return QStringLiteral("Identificatore datum già usato nel foglio.");
    }else{
        if(!t.label.isEmpty()||int(t.characteristic)<0||int(t.characteristic)>5||!std::isfinite(t.tolerance)||t.tolerance<1e-6||t.tolerance>1e6||std::abs(t.tolerance*1e6-std::round(t.tolerance*1e6))>1e-6)return QStringLiteral("Tolleranza positiva da 0,000001 a 1000000 mm, con massimo sei decimali.");
        const bool form=int(t.characteristic)<=2;
        if(t.datums.size()>3||(form&&!t.datums.isEmpty())||(!form&&t.datums.isEmpty()))return QStringLiteral("Forma: senza datum. Orientamento/posizione: da uno a tre datum.");
        if(t.diameter&&t.characteristic!=DrawingGeometricCharacteristic::Position)return QStringLiteral("In questo comando la zona diametrale è disponibile per la posizione.");
        QSet<QUuid> ids;for(const auto &id:t.datums){if(id.isNull()||id==note.id||ids.contains(id))return QStringLiteral("Riferimenti datum nulli o ripetuti.");ids.insert(id);
            if(sheet&&!drawingDatumAnnotation(*sheet,id)){
                for(const auto &n:sheet->annotations)if(n.id==id)return QStringLiteral("Il riferimento non identifica un datum.");
                for(const auto &v:sheet->drawing.views)if(v.id==id)return QStringLiteral("Riferimento datum confuso con una vista.");
                for(const auto &d:sheet->drawing.dimensions)if(d.id==id)return QStringLiteral("Riferimento datum confuso con una quota.");
                for(const auto &a:sheet->axes)if(a.id==id)return QStringLiteral("Riferimento datum confuso con un asse.");
            }
        }
    }return {};
}
inline QString drawingAnnotationText(const DrawingSheet &sheet,const DrawingAnnotation &note){
    if(!note.technical)return note.text;
    const auto &t=*note.technical;if(t.kind==DrawingTechnicalKind::Datum)return QStringLiteral("Datum %1").arg(t.label);
    QString text=drawingGeometricName(t.characteristic)+QStringLiteral(" | ")+(t.diameter?QStringLiteral("⌀ "):QString())+drawingTechnicalNumber(t.tolerance);
    for(const auto &id:t.datums){const auto *datum=drawingDatumAnnotation(sheet,id);text+=QStringLiteral(" | ")+(datum?datum->technical->label:QStringLiteral("?"));}return text;
}
struct DrawingTechnicalLayout {QPainterPath frame,symbol,text;QRectF bounds;};
// Celle, simboli e caratteri sono vettoriali; nessuna dipendenza da un font GD&T.
inline DrawingTechnicalLayout layoutDrawingTechnicalAnnotation(const DrawingSheet &sheet,const DrawingAnnotation &note,const DrawingStyle &style={},bool missing=false){
    DrawingTechnicalLayout out;if(!note.technical)return out;
    const auto &t=*note.technical;const auto font=drawingFont(style);const QFontMetricsF metrics(font);const double scale=note.height/12,h=note.height*2.2;
    const auto cellText=[&](const QString &text,double x,double width){QPainterPath glyph;glyph.addText(QPointF(0,0),font,text);auto path=QTransform::fromScale(scale,scale).map(glyph);const auto box=path.boundingRect();path.translate(x+(width-box.width())/2-box.left(),(h-box.height())/2-box.top());out.text.addPath(path);};
    const auto cellWidth=[&](const QString &text){return std::max(h,metrics.horizontalAdvance(text)*scale+note.height*.8);};
    double x=0;
    if(t.kind==DrawingTechnicalKind::Datum){const double width=cellWidth(t.label);out.frame.addRect(QRectF(0,0,width,h));cellText(t.label,0,width);x=width;}
    else{
        x=h;out.frame.moveTo(x,0);out.frame.lineTo(x,h);
        const auto line=[&](double ax,double ay,double bx,double by){out.symbol.moveTo(h*ax,h*ay);out.symbol.lineTo(h*bx,h*by);};
        switch(t.characteristic){
        case DrawingGeometricCharacteristic::Straightness:line(.2,.5,.8,.5);break;
        case DrawingGeometricCharacteristic::Flatness:out.symbol.moveTo(.2*h,.7*h);out.symbol.lineTo(.65*h,.7*h);out.symbol.lineTo(.8*h,.3*h);out.symbol.lineTo(.35*h,.3*h);out.symbol.closeSubpath();break;
        case DrawingGeometricCharacteristic::Circularity:out.symbol.addEllipse(QPointF(h*.5,h*.5),h*.25,h*.25);break;
        case DrawingGeometricCharacteristic::Parallelism:line(.25,.75,.45,.25);line(.55,.75,.75,.25);break;
        case DrawingGeometricCharacteristic::Perpendicularity:line(.5,.25,.5,.75);line(.2,.75,.8,.75);break;
        case DrawingGeometricCharacteristic::Position:out.symbol.addEllipse(QPointF(h*.5,h*.5),h*.23,h*.23);line(.1,.5,.9,.5);line(.5,.1,.5,.9);break;
        }
        const auto tolerance=(t.diameter?QStringLiteral("⌀ "):QString())+drawingTechnicalNumber(t.tolerance);const double width=cellWidth(tolerance);cellText(tolerance,x,width);x+=width;
        for(const auto &id:t.datums){out.frame.moveTo(x,0);out.frame.lineTo(x,h);const auto *datum=drawingDatumAnnotation(sheet,id);const auto label=datum?datum->technical->label:QStringLiteral("?");const auto w=cellWidth(label);cellText(label,x,w);x+=w;}out.frame.addRect(QRectF(0,0,x,h));
    }
    out.bounds=QRectF(0,0,x,h);
    if(missing){QPainterPath warning;warning.addText(QPointF(0,0),font,QStringLiteral("Da verificare"));warning=QTransform::fromScale(scale,scale).map(warning);warning.translate(0,-note.height*.6);out.text.addPath(warning);out.bounds=out.bounds.united(warning.boundingRect());}
    out.bounds=out.bounds.adjusted(-.2,-.2,.2,.2);return out;
}
}
