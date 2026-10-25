#include "cad_drawing_preparation.h"
#include "cad_drawing_derived.h"
#include "cad_drawing_detail.h"
#include "cad_drawing_break.h"
#include "cad_drawing_annotations.h"
#include "fk_tessellate.h"
#include "fk_cancel.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ForgeCad {
using namespace Kernel;
namespace {
QPainterPath sectionPath(const DrawingView &view,const DrawingViewLayout &layout,const DrawingSection &section){
    TessellationOptions options;options.deflection=std::clamp(0.03/view.scale,1e-6,1.0);options.angle=0.12;
    const auto samples=sampleCurve(*section.curve,section.range,options);
    if(samples.size()>200000)throw std::runtime_error("Curva troppo complessa per l'anteprima della tavola.");
    QPainterPath path;for(std::size_t i=0;i<samples.size();++i){const auto local=view.plane.toLocal(section.curve->point(samples[i]));const auto point=layout.position+QPointF(local.x()*view.scale,-local.y()*view.scale);
        if(!std::isfinite(point.x())||!std::isfinite(point.y()))throw std::runtime_error("Curva della tavola non finita.");
        if(i)path.lineTo(point);else path.moveTo(point);}
    return path;
}
QPainterPath silhouettePath(const DrawingView &view,const DrawingViewLayout &layout,const DrawingSilhouette &contour){
    QPainterPath path;bool first=true;for(const auto &point:contour.points){const auto local=view.plane.toLocal(point);const auto p=layout.position+QPointF(local.x()*view.scale,-local.y()*view.scale);if(!std::isfinite(p.x())||!std::isfinite(p.y()))throw std::runtime_error("Contorno apparente non finito sul foglio.");if(first){path.moveTo(p);first=false;}else path.lineTo(p);}return path;
}
}
void validateDrawingViewPaths(const DrawingView &view,const DrawingViewLayout &layout,
    const std::vector<DrawingCurve> &curves,const DrawingSilhouetteResult &contours){
    for(const auto &curve:curves)sectionPath(view,layout,curve);
    for(const auto &contour:contours.paths)silhouettePath(view,layout,contour);
}
PreparedDrawingView prepareDrawingView(const DrawingModelSnapshot &source,const DrawingView &definition,const DrawingViewLayout &layout,
        DrawingSilhouetteCache seeds,const std::function<void(int)> &phase,const std::vector<DrawingCurve> *reuseCurves,const DrawingSilhouetteResult *reuseContours){
        PreparedDrawingView result;result.cache=std::move(seeds);const auto ok=[](const QString &message){if(!message.isEmpty())throw std::runtime_error(message.toStdString());};
        if(reuseCurves)result.curves=*reuseCurves;else ok(drawingViewCurves(source,definition,result.curves));phase(1);
        if(reuseContours)result.contours=*reuseContours;else ok(drawingViewSilhouettes(source,definition,result.contours,result.cache));phase(2);
        DrawingSectionHatch hatch;ok(drawingSectionHatch(source,definition,result.curves,hatch));phase(3);
        result.display=std::make_shared<DrawingViewVisibility>();ok(drawingViewVisibility(source,definition,result.curves,result.contours,result.cache,*result.display,&hatch.material,layout.showMesh));
        result.display->sectionHatch=std::move(hatch.lines);result.display->sectionMaterial=std::move(hatch.material);result.display->warnings.append(hatch.warnings);clipDrawingDetailVisibility(definition,layout,*result.display);if(!source.bodies.empty()){ok(drawingBreakFits(definition,layout,drawingBreakBounds(*result.display)));applyDrawingBreak(definition,layout,*result.display);}
        else if(layout.interruption)result.display->warnings.append(QStringLiteral("Interruzione non verificabile: modello non disponibile."));phase(4);validateDrawingViewPaths(definition,layout,result.curves,result.contours);phase(5);return result;
    }

DrawingSheet retargetDrawingSheetModel(DrawingSheet next,const DrawingEditorModel &model){
    if(model.snapshot.version.documentId!=next.drawing.model.documentId)
        throw std::runtime_error("La tavola appartiene a un altro modello.");
    const auto error=retargetDrawingModel(next.drawing,model.snapshot.version);if(!error.isEmpty())throw std::runtime_error(error.toStdString());
    for(auto &layout:next.views){QVector<quint64> features;for(auto id:layout.features){const auto current=model.replacements.value(id,id);if(!features.contains(current))features.append(current);}layout.features=features;}
    for(auto &dimension:next.drawing.dimensions){dimension.section.featureId=model.replacements.value(dimension.section.featureId,dimension.section.featureId);for(auto &ref:dimension.references)if(ref.axisId.isNull()&&!drawingDatum(ref.role))ref.curve.featureId=model.replacements.value(ref.curve.featureId,ref.curve.featureId);}
    for(auto &axis:next.axes)for(auto &ref:axis.references)if(ref.axisId.isNull()&&!drawingDatum(ref.role))ref.curve.featureId=model.replacements.value(ref.curve.featureId,ref.curve.featureId);
    for(auto &n:next.annotations)if(n.reference&&n.reference->axisId.isNull())n.reference->curve.featureId=model.replacements.value(n.reference->curve.featureId,n.reference->curve.featureId);
    return next;
}
PreparedDrawingSheet prepareDrawingSheet(DrawingSheet sheet,DrawingEditorModel model,const std::function<void(int)> &phase){
    QByteArray validation;
    const auto valid=encodeDrawingSheet(sheet,validation);
    if(!valid.isEmpty())throw std::runtime_error(valid.toStdString());
    PreparedDrawingSheet prepared;
    for(const auto &definition:sheet.drawing.views){
        cancellationPoint();
        const auto layout=std::find_if(sheet.views.begin(),sheet.views.end(),[&](const auto &l){return l.viewId==definition.id;});
        DrawingModelSnapshot source;source.version=model.snapshot.version;
        for(const auto &body:model.snapshot.bodies)if(layout->features.contains(body.featureId))source.bodies.push_back(body);
        auto result=prepareDrawingView(source,definition,*layout,std::move(prepared.cache),phase);
        for(const auto &piece:result.curves)if(piece.kind==DrawingCurveKind::Section)prepared.sections[definition.id].push_back(piece);
        prepared.cache=std::move(result.cache);
        prepared.curves[definition.id]=std::move(result.curves);
        prepared.silhouettes[definition.id]=std::move(result.contours);
        prepared.visibility[definition.id]=std::move(result.display);
    }
    for(const auto &dimension:sheet.drawing.dimensions){
        cancellationPoint();
        const auto view=std::find_if(sheet.drawing.views.begin(),sheet.drawing.views.end(),[&](const auto &v){return v.id==dimension.viewId;});
        const auto &curves=prepared.curves.at(dimension.viewId);
        prepared.dimensions[dimension.id]=dimension.references.empty()?evaluateDrawingDimension(sheet.drawing,model.snapshot,dimension.id)
            :evaluateDrawingAttachments(dimension,*view,model.snapshot,curves,drawingSheetAttachmentResolver(sheet,*view,model.snapshot,curves));
    }
    prepared.sheet=std::move(sheet);prepared.model=std::move(model);return prepared;
}
}
