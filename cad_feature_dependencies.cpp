#include "cad_feature_dependencies.h"

namespace ForgeCad {

bool hasGeometry(const ExtrusionObject &body) {
    if (isCurveBody(body)) return body.curve != nullptr;
    if (isDatumBody(body)) return body.datumValid;
    return body.forgeBody != nullptr;
}
// Funzione curva (elica, spirale): niente solido ne' superficie, solo la curva.
bool isCurveBody(const ExtrusionObject &body) {
    return body.operation < 0 && (body.feature == BodyFeature::Helix || body.feature == BodyFeature::ProjectedCurve || body.feature == BodyFeature::ExtractedCurve || body.feature == BodyFeature::SpatialSketch);
}
// Funzioni che si possono fondere con i solidi (mergeOperation): estrusione, rivoluzione, sweep.
bool mergingFeature(const ExtrusionObject &body) {
    return body.operation < 0
        && (body.feature == BodyFeature::Extrusion || body.feature == BodyFeature::Revolution || body.feature == BodyFeature::Sweep);
}
// Piano di costruzione: niente solido, solo il piano (datumFrame).
bool isDatumBody(const ExtrusionObject &body) { return body.operation < 0 && body.feature == BodyFeature::DatumPlane; }
// Corpo con una forma (solido o superficie): non una curva ne' un piano di costruzione.
bool isShapeBody(const ExtrusionObject &body) { return hasGeometry(body) && !isCurveBody(body) && !isDatumBody(body); }
// Corpi da cui dipende il corpo: operandi delle booleane, base dei raccordi,
// superficie (e strumento) dei tagli e delle estensioni. Hanno indice minore.
QVector<int> bodyOperands(const ExtrusionObject &body) {
    if (body.operation >= 0) return QVector<int>{body.firstBody, body.secondBody} + body.booleanTools;
    switch (body.feature) {
    case BodyFeature::Extrusion: {
        QVector<int> bodies = body.mergeOperation != 0 ? body.mergeBodies : QVector<int>();
        if (body.extent != 0 && isBodyRef(body.extentRef)) bodies.append(body.extentRef.index);
        return bodies;
    }
    case BodyFeature::Revolution: {
        QVector<int> bodies = body.mergeOperation != 0 ? body.mergeBodies : QVector<int>();
        if (body.revolveAxis == kRevolveAxisReference && isBodyRef(body.revolveAxisRef)) bodies.append(body.revolveAxisRef.index);
        return bodies;
    }
    case BodyFeature::ExtractedCurve: {
        QVector<int> result;
        const auto &p = body.curveExtraction;
        for (const auto &ref : (p.sources.isEmpty()?QVector<GeometryRef>{p.source}:p.sources)+QVector<GeometryRef>{p.trimmed?p.tool:GeometryRef{}})
            if (isBodyRef(ref) && !result.contains(ref.index)) result.append(ref.index);
        return result;
    }
    case BodyFeature::Thicken: {
        QVector<int> refs{body.firstBody};
        if (isBodyRef(body.thickenDirection) && !refs.contains(body.thickenDirection.index)) refs.append(body.thickenDirection.index);
        return refs;
    }
    case BodyFeature::Draft: {
        QVector<int> refs{body.firstBody};
        if (isBodyRef(body.draftNeutral) && !refs.contains(body.draftNeutral.index)) refs.append(body.draftNeutral.index);
        return refs;
    }
    case BodyFeature::SurfaceTools: {
        QVector<int> refs;if(body.surfaceTool.mode!=1)refs.append(body.firstBody);
        for(const auto *family:{&body.surfaceTool.targets,&body.ruledFirst,&body.ruledSecond})for(const auto &ref:*family)if(isBodyRef(ref)&&!refs.contains(ref.index))refs.append(ref.index);
        if(body.surfaceTool.mode==0&&body.surfaceTool.targets.isEmpty()&&isBodyRef(body.extentRef)&&!refs.contains(body.extentRef.index))refs.append(body.extentRef.index);
        return refs;
    }
    case BodyFeature::Blend:
    case BodyFeature::SheetExtend:
    case BodyFeature::SurfaceOffset:
    case BodyFeature::DeleteFace:
    case BodyFeature::Shell:
    case BodyFeature::Thread:
    case BodyFeature::ProjectedCurve:
    case BodyFeature::Scale: return {body.firstBody};
    case BodyFeature::Sew: return QVector<int>{body.firstBody} + body.booleanTools;
    case BodyFeature::SheetTrim: return body.secondBody >= 0 ? QVector<int>{body.firstBody, body.secondBody} : QVector<int>{body.firstBody};
    case BodyFeature::Helix: return body.helix.source != 0 ? QVector<int>{body.firstBody} : QVector<int>{};
    case BodyFeature::Sweep: {
        QVector<int> result = body.sweepPathRef.kind < 0 && body.sweepPath == 1 ? QVector<int>{body.firstBody} : QVector<int>();
        for (const auto &ref : {body.sweepProfileRef, body.sweepPathRef})
            if (isBodyRef(ref) && !result.contains(ref.index)) result.append(ref.index);
        if (body.mergeOperation != 0) result += body.mergeBodies;
        return result;
    }
    case BodyFeature::DatumPlane: {
        QVector<int> bodies;
        for (const GeometryRef &ref : body.datum.refs)
            if (isBodyRef(ref)) bodies.append(ref.index);
        return bodies;
    }
    case BodyFeature::Pattern: {
        QVector<int> bodies{body.firstBody};
        for (const GeometryRef &ref : body.pattern.refs)
            if (isBodyRef(ref)) bodies.append(ref.index);
        return bodies;
    }
    case BodyFeature::Transform: {
        QVector<int> bodies{body.firstBody};
        if (isBodyRef(body.move.axis)) bodies.append(body.move.axis.index);
        return bodies;
    }
    case BodyFeature::Loft:
    case BodyFeature::Ruled:
    case BodyFeature::PlanarSurface:
    case BodyFeature::BoundarySurface:
    case BodyFeature::FillSurface: {
        QVector<int> bodies;
        for (const QVector<GeometryRef> *refs : {&body.surfaceTool.targets, &body.ruledFirst, &body.ruledSecond, &body.planarRefs, &body.loftStartFaces, &body.loftEndFaces, &body.loftSections, &body.loftEdgeGuides})
            for (const GeometryRef &ref : *refs)
                if (isBodyRef(ref) && !bodies.contains(ref.index)) bodies.append(ref.index);
        return bodies;
    }
    default: return {};
    }
}
// Riferimento a un corpo (vertice, spigolo, faccia, piano di costruzione, curva) o a uno schizzo (punto, entita').
bool isBodyRef(const GeometryRef &ref) {
    return ref.kind == 3 || ref.kind == 4 || ref.kind == 5 || ref.kind == 8 || ref.kind == 9 || ref.kind == 10;
}
bool isSketchRef(const GeometryRef &ref) { return ref.kind == 6 || ref.kind == 7; }
// Schizzi da cui nasce il corpo: estrusione, rivoluzione, base dell'elica,
// profilo e percorso dello sweep, sezioni e guide del loft.
QVector<int> sketchesOf(const ExtrusionObject &body) {
    if (body.operation >= 0) return {};
    switch (body.feature) {
    case BodyFeature::Extrusion:
        if (body.extent != 0 && isSketchRef(body.extentRef) && body.extentRef.index != body.sketchIndex) return {body.sketchIndex, body.extentRef.index};
        return {body.sketchIndex};
    case BodyFeature::Revolution:
        if (body.revolveAxis == kRevolveAxisReference && isSketchRef(body.revolveAxisRef) && body.revolveAxisRef.index != body.sketchIndex)
            return {body.sketchIndex, body.revolveAxisRef.index};
        return {body.sketchIndex};
    case BodyFeature::ExtractedCurve: {
        QVector<int> result;
        const auto &p = body.curveExtraction;
        for (const auto &ref : (p.sources.isEmpty()?QVector<GeometryRef>{p.source}:p.sources)+QVector<GeometryRef>{p.trimmed?p.tool:GeometryRef{}})
            if (isSketchRef(ref) && !result.contains(ref.index)) result.append(ref.index);
        return result;
    }
    case BodyFeature::Draft: return isSketchRef(body.draftNeutral) ? QVector<int>{body.draftNeutral.index} : QVector<int>{};
    case BodyFeature::Thicken: return isSketchRef(body.thickenDirection) ? QVector<int>{body.thickenDirection.index} : QVector<int>{};
    case BodyFeature::Transform: return isSketchRef(body.move.axis) ? QVector<int>{body.move.axis.index} : QVector<int>{};
    case BodyFeature::Helix: return body.helix.source == 0 ? QVector<int>{body.sketchIndex} : QVector<int>{};
    case BodyFeature::SheetTrim: return body.sketchIndex >= 0 ? QVector<int>{body.sketchIndex} : QVector<int>{};
    case BodyFeature::ProjectedCurve: return {body.sketchIndex};
    case BodyFeature::Sweep: {
        QVector<int> result;
        if (body.sweepProfileRef.kind < 0) result.append(body.sketchIndex);
        if (body.sweepPathRef.kind < 0 && body.sweepPath == 0) result.append(body.pathSketch);
        for (const auto &ref : {body.sweepProfileRef, body.sweepPathRef})
            if (isSketchRef(ref) && !result.contains(ref.index)) result.append(ref.index);
        return result;
    }
    case BodyFeature::Loft: {
        QVector<int> result = body.loftSections.isEmpty() ? body.loftSketches : QVector<int>{};
        for (const auto *refs : {&body.loftSections,&body.loftEdgeGuides})
            for (const auto &ref : *refs)
                if (isSketchRef(ref) && !result.contains(ref.index)) result.append(ref.index);
        for (int guide : body.loftGuides)
            if (!result.contains(guide)) result.append(guide);
        for (const SketchPathRef &guide : body.loftGuidePaths)
            if (!result.contains(guide.sketch)) result.append(guide.sketch);
        return result;
    }
    case BodyFeature::DatumPlane:
    case BodyFeature::Pattern: {
        QVector<int> sketches;
        for (const GeometryRef &ref : body.feature == BodyFeature::Pattern ? body.pattern.refs : body.datum.refs)
            if (isSketchRef(ref)) sketches.append(ref.index);
        return sketches;
    }
    case BodyFeature::SurfaceTools:
    case BodyFeature::Ruled:
    case BodyFeature::PlanarSurface:
    case BodyFeature::BoundarySurface:
    case BodyFeature::FillSurface: {
        QVector<int> sketches;
        if (body.feature == BodyFeature::PlanarSurface && body.planarRefs.isEmpty()) sketches.append(body.sketchIndex);
        if(body.feature==BodyFeature::SurfaceTools&&body.surfaceTool.mode==0&&body.surfaceTool.targets.isEmpty()&&isSketchRef(body.extentRef))sketches.append(body.extentRef.index);
        for (const QVector<GeometryRef> *refs : {&body.surfaceTool.targets, &body.ruledFirst, &body.ruledSecond, &body.planarRefs, &body.loftStartFaces, &body.loftEndFaces, &body.loftSections, &body.loftEdgeGuides})
            for (const GeometryRef &ref : *refs)
                if (isSketchRef(ref) && !sketches.contains(ref.index)) sketches.append(ref.index);
        return sketches;
    }
    default: return {};
    }
}
bool usesSketch(const ExtrusionObject &body) { return !sketchesOf(body).isEmpty(); }

// Corpi che la funzione nasconde (operandi delle booleane, corpi in cui si fonde l'estrusione).
QVector<int> hiddenOperands(const ExtrusionObject &body) {
    if (body.operation >= 0) return QVector<int>{body.firstBody, body.secondBody} + body.booleanTools;
    if (body.feature == BodyFeature::ExtractedCurve && body.curveExtraction.trimmed) {
        QVector<int> replaced;
        for(const auto &ref:body.curveExtraction.sources.isEmpty()?QVector<GeometryRef>{body.curveExtraction.source}:body.curveExtraction.sources)
            if(ref.kind==9&&!replaced.contains(ref.index))replaced.append(ref.index);
        return replaced;
    }
    if (mergingFeature(body) && body.mergeOperation != 0) return body.mergeBodies;
    if (body.feature == BodyFeature::Transform && !body.move.copy) return {body.firstBody};
    if (body.feature == BodyFeature::Sew) return QVector<int>{body.firstBody} + body.booleanTools;
    if (body.feature == BodyFeature::SheetTrim)
        return body.trimBoth && body.secondBody >= 0 ? QVector<int>{body.firstBody, body.secondBody}
                                                    : QVector<int>{body.firstBody};
    return {};
}


}
