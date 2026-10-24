#pragma once
#include "cad_drawing_technical.h"
#include <functional>
#include <QTextDocument>
#include <QTextOption>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace ForgeCad {
// Record di trasporto degli agganci: non viene aggiunto alle quote della tavola.
inline DrawingDimension drawingAnnotationSupport(const DrawingAnnotation &note){
    DrawingDimension d;d.id=note.id;d.viewId=note.viewId;d.measure=DrawingMeasure::SectionLength;
    if(note.reference){d.references={*note.reference};if(note.reference->axisId.isNull())d.section=note.reference->curve;}
    return d;
}
inline void configureDrawingAnnotation(QTextDocument &document,const DrawingAnnotation &note,const QString &text,const DrawingStyle &style={}){
    const auto font=drawingFont(style);document.setDefaultFont(font);document.setDocumentMargin(0);
    QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);document.setDefaultTextOption(option);
    document.setPlainText(text);document.setTextWidth(note.width*12/note.height);
}
inline QPainterPath drawingAnnotationLeader(QPointF anchor,const QRectF &box,bool includeArrow=true){
    const bool left=anchor.x()<box.center().x();
    const QPointF end(left?box.left():box.right(),std::clamp(anchor.y(),box.top(),box.bottom()));
    const QPointF elbow=end+QPointF(left?-4:4,0);QPainterPath path;path.moveTo(anchor);path.lineTo(elbow);path.lineTo(end);
    auto direction=elbow-anchor;const double length=std::hypot(direction.x(),direction.y());
    if(includeArrow&&length>1e-8){direction/=length;const QPointF normal(-direction.y(),direction.x());path.moveTo(anchor+2*direction+.6*normal);path.lineTo(anchor);path.lineTo(anchor+2*direction-.6*normal);}
    return path;
}
inline DrawingResolvedAttachment resolveDrawingAnnotation(const DrawingSheet &sheet,const DrawingAnnotation &note,
    const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,const std::function<DrawingResolvedAttachment(const DrawingAnnotation &)> &datumResolver={}){
    DrawingResolvedAttachment result;
    if(note.technical&&note.technical->kind==DrawingTechnicalKind::FeatureControl)for(const auto &id:note.technical->datums){
        const auto *datum=drawingDatumAnnotation(sheet,id);if(!datum){result.status=DrawingDimensionStatus::Missing;result.message=QStringLiteral("Datum del cartiglio non disponibile.");return result;}
        DrawingResolvedAttachment origin;
        if(datumResolver)origin=datumResolver(*datum);
        else{const auto v=std::find_if(sheet.drawing.views.begin(),sheet.drawing.views.end(),[&](const auto &v){return v.id==datum->viewId;});
            if(v==sheet.drawing.views.end()){origin.status=DrawingDimensionStatus::Missing;origin.message=QStringLiteral("Vista del datum assente.");}
            else{std::vector<DrawingCurve> source;const auto error=drawingViewCurves(model,*v,source);if(error.isEmpty())origin=resolveDrawingAnnotation(sheet,*datum,model,source);else origin.message=error;}}
        if(origin.status!=DrawingDimensionStatus::Ready){result=origin;result.message=QStringLiteral("Datum %1 da verificare: %2").arg(datum->technical->label,origin.message);return result;}
    }
    result.status=DrawingDimensionStatus::Missing;result.message=QStringLiteral("Vista del richiamo assente.");
    if(!note.reference){result.status=DrawingDimensionStatus::Ready;result.message.clear();return result;}
    const auto v=std::find_if(sheet.drawing.views.begin(),sheet.drawing.views.end(),[&](const auto &v){return v.id==note.viewId;});if(v==sheet.drawing.views.end())return result;
    return resolveDrawingAttachment(*v,model,curves,*note.reference,drawingSheetAttachmentResolver(sheet,*v,model,curves));
}
}
