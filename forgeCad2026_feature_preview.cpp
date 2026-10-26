#include "forgeCad2026_feature_preview.h"
#include "cad_feature_build.h"
#include "cad_feature_dependencies.h"
#include "cad_forge.h"
#include <QStringList>
namespace ForgeCad::Gui {
bool FeaturePreviewBuilder::localMergePreview(const ExtrusionObject &body) {
    return body.operation < 0 && (body.feature == BodyFeature::Extrusion || body.feature == BodyFeature::Revolution);
}

QString FeaturePreviewBuilder::previewKey(const ExtrusionObject &d, int index) {
    const auto n = [](double v) { return QString::number(v, 'g', 17); };
    QStringList parts{QString::number(index), QString::number(d.operation), QString::number(int(d.feature)), QString::number(d.sketchIndex),
                      n(d.distance), QString::number(d.revolveAxis), n(d.revolveAngle), QString::number(d.firstBody),
                      QString::number(d.secondBody), QString::number(d.deleteComponent), n(d.blendSize), QString::number(d.blendChamfer), QString::number(d.trimPlane),
                      n(d.trimKeep.x), n(d.trimKeep.y), n(d.trimKeep.z), QString::number(d.trimBoth),
                      n(d.trimToolKeep.x), n(d.trimToolKeep.y), n(d.trimToolKeep.z),
                      QString::number(d.extendLinear), n(d.scaleFactor),
                      QString::number(d.scaleCenterMode), n(d.scaleCenter.x), n(d.scaleCenter.y), n(d.scaleCenter.z),
                      QString::number(d.blendChamfer && d.chamferSpec.mode != 0 ? d.chamferSpec.mode : 0),
                      n(d.blendChamfer && d.chamferSpec.mode != 0 ? d.chamferSpec.second : 0.0),
                      QString::number(d.blendChamfer && d.chamferSpec.mode != 0 && d.chamferSpec.flip)};
    const PrimitiveParameters &p = d.primitive;
    parts << QString::number(int(p.kind)) << QString::number(p.plane);
    for (int k = 0; k < 3; ++k) parts << n(p.origin[k]) << n(p.size[k]);
    for (const EdgePoint &e : d.blendEdges)
        parts << n(e.x) << n(e.y) << n(e.z) << QString::number(e.subshape) << QString::number(e.geometry) << QString::number(e.context)
              << QString::number(e.role);
    const HelixParameters &h = d.helix;
    parts << QString::number(h.spiral) << QString::number(h.mode) << n(h.pitch) << n(h.turns) << n(h.height) << n(h.taper) << n(h.startAngle)
          << QString::number(h.leftHanded) << QString::number(h.reverse) << QString::number(h.source) << QString::number(h.curve)
          << n(h.reference.x) << n(h.reference.y) << n(h.reference.z) << QString::number(h.reference.subshape)
          << QString::number(h.reference.geometry) << QString::number(h.reference.context) << QString::number(d.sweepPath) << QString::number(d.pathSketch)
          << QString::number(d.sweepMode) << QString::number(d.loftRuled) << QStringLiteral("|");
    for (int segment : d.pathSegments) parts << QStringLiteral("ps%1").arg(segment);
    for (int curve : d.pathCurves) parts << QStringLiteral("pc%1").arg(curve);
    for (int section : d.loftSketches) parts << QString::number(section);
    parts << QStringLiteral("G");
    for (int guide : d.loftGuides) parts << QString::number(guide);
    for (int index : d.loftGuideOrder) parts << QStringLiteral("GO%1").arg(index);
    for (const SketchPathRef &guide : d.loftGuidePaths) {
        parts << QStringLiteral("gp%1").arg(guide.sketch);
        for (int segment : guide.segments) parts << QStringLiteral("s%1").arg(segment);
        for (int curve : guide.curves) parts << QStringLiteral("c%1").arg(curve);
    }
    for (int value : d.loftGuideContinuities) parts << QStringLiteral("GC%1").arg(value);
    for (double value : d.loftGuideInfluences) parts << QStringLiteral("GI") << n(value);
    parts << QString::number(d.loftStartContinuity) << QString::number(d.loftEndContinuity) << QString::number(d.loftGuideContinuity)
          << n(d.loftGuideInfluence)
          << n(d.loftStartInfluence) << n(d.loftEndInfluence);
    const auto ref = [&](const GeometryRef &r) {
        parts << QString::number(r.kind) << QString::number(r.index) << QString::number(r.element.kind) << QString::number(r.element.element)
              << QString::number(r.element.point) << n(r.point.x) << n(r.point.y) << n(r.point.z) << QString::number(r.featureId)
              << QString::number(r.point.subshape) << QString::number(r.point.geometry) << QString::number(r.point.context);
    };
    parts << QStringLiteral("D") << QString::number(d.datum.mode) << n(d.datum.distance) << n(d.datum.angle) << QString::number(d.datum.flip)
          << QString::number(d.datum.onCurve) << n(d.datum.size);
    for (const GeometryRef &r : d.datum.refs) ref(r);
    const PatternParameters &r = d.pattern;
    parts << QStringLiteral("P") << QString::number(r.kind) << QString::number(r.count) << QString::number(r.count2) << n(r.spacing) << n(r.spacing2)
          << n(r.angle) << QString::number(r.spread) << QString::number(r.flip) << QString::number(r.flip2) << QString::number(r.keepOriginal)
          << QString::number(r.featureOnly) << QString::number(d.patternComponent);
    auto excluded = r.excluded;
    std::sort(excluded.begin(), excluded.end());
    for (int occurrence : excluded) parts << QStringLiteral("skip%1").arg(occurrence);
    for (const GeometryRef &g : r.refs) ref(g);
    parts << QStringLiteral("E") << QString::number(d.extent) << QString::number(d.mergeOperation) << QString::number(d.mergeAuto)
          << QString::number(d.mergeProbe) << QString::number(d.extrudeSides) << n(d.distance2)
          << QString::number(d.extrudeSurface);
    ref(d.extentRef);
    for (int body : d.mergeBodies) parts << QString::number(body);
    parts << QStringLiteral("T");
    for (int body : d.booleanTools) parts << QString::number(body);
    parts << QStringLiteral("M") << n(d.move.translation[0]) << n(d.move.translation[1]) << n(d.move.translation[2]) << n(d.move.angle)
          << QString::number(d.move.copy);
    ref(d.move.axis);
    ref(d.revolveAxisRef);
    ref(d.draftNeutral);
    parts << n(d.draftAngle) << QString::number(d.draftReverse);
    parts << QStringLiteral("TK") << QString::number(d.thickenSide);
    ref(d.thickenDirection);
    ref(d.sweepProfileRef); ref(d.sweepPathRef);
    parts << QStringLiteral("S3D") << QString::number(d.spatialSketch.nextId);
    for(const auto &e:d.spatialSketch.entities){parts << QString::number(e.id) << QString::number(e.spline) << QString::number(e.closed);
        for(const auto &p:e.points)parts << n(p.x) << n(p.y) << n(p.z);}
    ref(d.curveExtraction.source);
    ref(d.curveExtraction.tool);
    parts << QString::number(d.curveExtraction.trimmed) << QString::number(d.curveExtraction.projected)
          << QString::number(d.curveExtraction.keepStart) << n(d.curveExtraction.cutHint);
    for (double v : d.curveExtraction.view.origin) parts << n(v);
    for (double v : d.curveExtraction.view.normal) parts << n(v);
    for (double v : d.curveExtraction.view.xAxis) parts << n(v);
    parts << QStringLiteral("PJ") << QString::number(d.trimProject) << QString::number(d.projectionMode) << QString::number(d.projectionReverse);
    // Offset e cucitura; loft e sweep di superficie, rigata e planare.
    parts << QStringLiteral("O") << QString::number(d.offsetSew);
    for (const EdgePoint &e : d.offsetFaces)
        parts << n(e.x) << n(e.y) << n(e.z) << QString::number(e.subshape) << QString::number(e.geometry) << QString::number(e.context);
    parts << n(d.sewTolerance) << QString::number(d.sewSolid) << QString::number(d.loftSurface) << QString::number(d.sweepSurface);
    parts << QStringLiteral("FS") << QString::number(d.fillContinuity) << n(d.fillInfluence) << n(d.fillGuideWeight);
    for(int value:d.fillFaceContinuities)parts << QStringLiteral("FC") << QString::number(value);
    if(d.feature==BodyFeature::SurfaceTools){
        parts << QStringLiteral("ST") << QString::number(d.surfaceTool.mode) << n(d.surfaceTool.maximum) << n(d.surfaceTool.tolerance) << QString::fromLatin1(d.surfaceTool.basis.toHex());
        for(const auto &target:d.surfaceTool.targets) { parts << QStringLiteral("STT"); ref(target); }
        for(const auto &edit:d.surfaceTool.edits)parts << QString::number(edit.u) << QString::number(edit.v) << n(edit.dx) << n(edit.dy) << n(edit.dz) << n(edit.weightFactor);
    }
    for (double value : d.fillFaceInfluences) parts << QStringLiteral("FI") << n(value);
    parts << QStringLiteral("FS") << QString::number(d.fillShapeFace) << n(d.fillShapeWeight);
    for(const auto &source:d.curveExtraction.sources){parts<<QStringLiteral("ES");ref(source);}
    parts << QStringLiteral("LS");
    for (const GeometryRef &g : d.loftSections) ref(g);
    parts << QStringLiteral("LG");
    for (const GeometryRef &g : d.loftEdgeGuides) ref(g);
    parts << QStringLiteral("TF1");
    for (const GeometryRef &g : d.loftStartFaces) ref(g);
    parts << QStringLiteral("TF2");
    for (const GeometryRef &g : d.loftEndFaces) ref(g);
    parts << QStringLiteral("R1");
    for (const GeometryRef &g : d.ruledFirst) ref(g);
    parts << QStringLiteral("R2");
    for (const GeometryRef &g : d.ruledSecond) ref(g);
    parts << QStringLiteral("PL");
    for (const GeometryRef &g : d.planarRefs) ref(g);
    const ThreadParameters &thread = d.thread;
    parts << QStringLiteral("TH") << QString::number(thread.standard) << thread.designation << n(thread.pitch) << n(thread.length)
          << QString::number(thread.leftHanded) << QString::number(thread.reverse)
          << n(thread.face.x) << n(thread.face.y) << n(thread.face.z)
          << QString::number(thread.face.subshape) << QString::number(thread.face.geometry) << QString::number(thread.face.context);
    return parts.join(QLatin1Char(','));
}

bool FeaturePreviewBuilder::computePreview(const PreviewInputs &in, BodyDisplay &display, BodyDisplay &retainedDisplay,
                               BodyDisplay &resultDisplay, ForgeCad::ForgeBody &geometry,
                               QString &error, QVector<int> *merged , QString *notice ) {
    ExtrusionObject body = in.definition;
    if (in.reuse) {
        body.forgeBody = in.existing;
        body.mergeBodies = in.existingMerge;
        body.error.clear();
    } else {
        ForgeCad::buildGeometry(body, in.index, in.sketches, in.bodies);
    }
    if (merged && body.mergeProbe) *merged = body.mergeBodies;  // i corpi toccati dalla fusione automatica
    if (notice) *notice = body.notice;
    if (!ForgeCad::hasGeometry(body)) {
        error = body.error;
        return false;
    }
    geometry = body.forgeBody;
    const bool blend = in.definition.operation < 0 && in.definition.feature == BodyFeature::Blend && body.forgeBody
        && in.definition.firstBody >= 0 && in.definition.firstBody < in.bodies.size()
        && in.bodies.at(in.definition.firstBody).forgeBody;
    const bool localExtrusion = localMergePreview(in.definition)
        && body.mergeOperation != 0 && !body.mergeBodies.isEmpty() && body.forgeBody;
    if (blend) {
        if (in.reuse) resultDisplay = in.existingDisplay;
        ForgeCad::forgeBlendPreviewDisplay(*in.bodies.at(in.definition.firstBody).forgeBody, *body.forgeBody, in.quality, display,
                                           in.quality <= 0 ? 3 : in.quality == 1 ? 5 : 7,
                                           in.reuse ? &in.existingDisplay : nullptr);
    } else if (localExtrusion) {
        if (in.reuse) resultDisplay = in.existingDisplay;
        QVector<ForgeCad::ForgeBody> bases;
        for (int index : body.mergeBodies)
            if (index >= 0 && index < in.bodies.size() && in.bodies.at(index).forgeBody)
                bases.append(in.bodies.at(index).forgeBody);
        ForgeCad::forgeExtrusionPreviewDisplay(bases, *body.forgeBody, in.quality, display,
                                               in.quality <= 0 ? 3 : in.quality == 1 ? 5 : 7,
                                               body.mergeOperation == 2 ? &retainedDisplay : nullptr);
    } else if (in.reuse) {
        display = in.existingDisplay;
        display.constructionCurves.clear();
    } else {
        ForgeCad::tessellateGeometry(body, in.quality, display);
    }
    if (in.definition.operation < 0 && body.forgeBody) {
        const int divisions = in.quality <= 0 ? 3 : in.quality == 1 ? 5 : 7;
        if (in.definition.feature == BodyFeature::Loft)
            ForgeCad::forgeSurfaceConstructionCurves(*body.forgeBody, display, divisions);
        else if ((in.definition.feature == BodyFeature::Extrusion && !localExtrusion)
                 || in.definition.feature == BodyFeature::Revolution)
            ForgeCad::forgeSurfaceConstructionCurves(*body.forgeBody, display, divisions, true);
    }
    return true;
}
}
