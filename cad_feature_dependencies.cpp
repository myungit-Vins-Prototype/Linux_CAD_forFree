#include "cad_feature_dependencies.h"
#include "cad_geometry_refs.h"

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
    using Family = GeometryRefFamily;
    const auto appendRefs = [&](QVector<int> &indices, std::initializer_list<Family> families, bool distinct = true) {
        visitGeometryRefs(body, families, [&](const GeometryRef &ref, Family, int) {
            if (isBodyRef(ref) && (!distinct || !indices.contains(ref.index))) indices.append(ref.index);
        });
    };
    if (body.operation >= 0) return QVector<int>{body.firstBody, body.secondBody} + body.booleanTools;
    switch (body.feature) {
    case BodyFeature::Extrusion: {
        QVector<int> bodies = body.mergeOperation != 0 ? body.mergeBodies : QVector<int>();
        if (body.extent != 0) appendRefs(bodies, {Family::Extent}, false);
        return bodies;
    }
    case BodyFeature::Revolution: {
        QVector<int> bodies = body.mergeOperation != 0 ? body.mergeBodies : QVector<int>();
        if (body.revolveAxis == kRevolveAxisReference) appendRefs(bodies, {Family::RevolveAxis}, false);
        return bodies;
    }
    case BodyFeature::ExtractedCurve: {
        QVector<int> result;
        const auto &p = body.curveExtraction;
        appendRefs(result, {p.sources.isEmpty() ? Family::ExtractedSource : Family::ExtractedSources});
        if (p.trimmed) appendRefs(result, {Family::ExtractedTool});
        return result;
    }
    case BodyFeature::Thicken: {
        QVector<int> refs{body.firstBody};
        appendRefs(refs, {Family::ThickenDirection});
        return refs;
    }
    case BodyFeature::Draft: {
        QVector<int> refs{body.firstBody};
        appendRefs(refs, {Family::DraftNeutral});
        return refs;
    }
    case BodyFeature::SurfaceTools: {
        QVector<int> refs;if(body.surfaceTool.mode!=1)refs.append(body.firstBody);
        appendRefs(refs, {Family::SurfaceTargets, Family::RuledFirst, Family::RuledSecond});
        if(body.surfaceTool.mode==0&&body.surfaceTool.targets.isEmpty())appendRefs(refs, {Family::Extent});
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
        appendRefs(result, {Family::SweepProfile, Family::SweepPath});
        if (body.mergeOperation != 0) result += body.mergeBodies;
        return result;
    }
    case BodyFeature::DatumPlane: {
        QVector<int> bodies;
        appendRefs(bodies, {Family::Datum}, false);
        return bodies;
    }
    case BodyFeature::Pattern: {
        QVector<int> bodies{body.firstBody};
        appendRefs(bodies, {Family::Pattern}, false);
        return bodies;
    }
    case BodyFeature::Transform: {
        QVector<int> bodies{body.firstBody};
        appendRefs(bodies, {Family::MoveAxis}, false);
        return bodies;
    }
    case BodyFeature::Loft:
    case BodyFeature::Ruled:
    case BodyFeature::PlanarSurface:
    case BodyFeature::BoundarySurface:
    case BodyFeature::FillSurface: {
        QVector<int> bodies;
        appendRefs(bodies, {Family::SurfaceTargets, Family::RuledFirst, Family::RuledSecond, Family::Planar,
                            Family::LoftStartFaces, Family::LoftEndFaces, Family::LoftSections, Family::LoftEdgeGuides});
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
    using Family = GeometryRefFamily;
    const auto appendRefs = [&](QVector<int> &indices, std::initializer_list<Family> families, bool distinct = true) {
        visitGeometryRefs(body, families, [&](const GeometryRef &ref, Family, int) {
            if (isSketchRef(ref) && (!distinct || !indices.contains(ref.index))) indices.append(ref.index);
        });
    };
    if (body.operation >= 0) return {};
    switch (body.feature) {
    case BodyFeature::Extrusion: {
        QVector<int> refs{body.sketchIndex};
        if (body.extent != 0) appendRefs(refs, {Family::Extent});
        return refs;
    }
    case BodyFeature::Revolution: {
        QVector<int> refs{body.sketchIndex};
        if (body.revolveAxis == kRevolveAxisReference) appendRefs(refs, {Family::RevolveAxis});
        return refs;
    }
    case BodyFeature::ExtractedCurve: {
        QVector<int> result;
        const auto &p = body.curveExtraction;
        appendRefs(result, {p.sources.isEmpty() ? Family::ExtractedSource : Family::ExtractedSources});
        if (p.trimmed) appendRefs(result, {Family::ExtractedTool});
        return result;
    }
    case BodyFeature::Draft: {
        QVector<int> refs; appendRefs(refs, {Family::DraftNeutral}); return refs;
    }
    case BodyFeature::Thicken: {
        QVector<int> refs; appendRefs(refs, {Family::ThickenDirection}); return refs;
    }
    case BodyFeature::Transform: {
        QVector<int> refs; appendRefs(refs, {Family::MoveAxis}); return refs;
    }
    case BodyFeature::Helix: return body.helix.source == 0 ? QVector<int>{body.sketchIndex} : QVector<int>{};
    case BodyFeature::SheetTrim: return body.sketchIndex >= 0 ? QVector<int>{body.sketchIndex} : QVector<int>{};
    case BodyFeature::ProjectedCurve: return {body.sketchIndex};
    case BodyFeature::Sweep: {
        QVector<int> result;
        if (body.sweepProfileRef.kind < 0) result.append(body.sketchIndex);
        if (body.sweepPathRef.kind < 0 && body.sweepPath == 0) result.append(body.pathSketch);
        appendRefs(result, {Family::SweepProfile, Family::SweepPath});
        return result;
    }
    case BodyFeature::Loft: {
        QVector<int> result = body.loftSections.isEmpty() ? body.loftSketches : QVector<int>{};
        appendRefs(result, {Family::LoftSections, Family::LoftEdgeGuides});
        for (int guide : body.loftGuides)
            if (!result.contains(guide)) result.append(guide);
        for (const SketchPathRef &guide : body.loftGuidePaths)
            if (!result.contains(guide.sketch)) result.append(guide.sketch);
        return result;
    }
    case BodyFeature::DatumPlane:
    case BodyFeature::Pattern: {
        QVector<int> sketches;
        appendRefs(sketches, {body.feature == BodyFeature::Pattern ? Family::Pattern : Family::Datum}, false);
        return sketches;
    }
    case BodyFeature::SurfaceTools:
    case BodyFeature::Ruled:
    case BodyFeature::PlanarSurface:
    case BodyFeature::BoundarySurface:
    case BodyFeature::FillSurface: {
        QVector<int> sketches;
        if (body.feature == BodyFeature::PlanarSurface && body.planarRefs.isEmpty()) sketches.append(body.sketchIndex);
        if(body.feature==BodyFeature::SurfaceTools&&body.surfaceTool.mode==0&&body.surfaceTool.targets.isEmpty())appendRefs(sketches, {Family::Extent});
        appendRefs(sketches, {Family::SurfaceTargets, Family::RuledFirst, Family::RuledSecond, Family::Planar,
                             Family::LoftStartFaces, Family::LoftEndFaces, Family::LoftSections, Family::LoftEdgeGuides});
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
        visitGeometryRefs(body, body.curveExtraction.sources.isEmpty() ? GeometryRefFamily::ExtractedSource : GeometryRefFamily::ExtractedSources,
            [&](const GeometryRef &ref, GeometryRefFamily, int) {
                if(ref.kind==9&&!replaced.contains(ref.index))replaced.append(ref.index);
            });
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
