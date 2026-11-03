#include "cad_sketch_reference_usage.h"
#include "cad_geometry_refs.h"
#include "cad_sketch_edit.h"
namespace ForgeCad {
namespace {
QVector<ConstraintRef> references(const SketchConstraint &c) {
    QVector<ConstraintRef> result{c.first, c.second, c.third};
    if (c.type == ConstraintType::Pattern) {
        result += c.pattern.sources; result += c.pattern.copies;
        result += QVector<ConstraintRef>{c.pattern.direction, c.pattern.direction2, c.pattern.axis, c.pattern.center};
    }
    if (c.type == ConstraintType::Offset) { result += c.offset.sources; result += c.offset.copies; }
    return result;
}
bool targetsSketch(const GeometryRef &r, int index, const QVector<SketchObject> &sketches) {
    return (r.kind == 6 || r.kind == 7) && (r.featureId ? r.featureId == sketches[index].storyId : r.index == index);
}
void remapList(QVector<int> &values, const QVector<int> &map) {
    for (auto &v : values) v = map.value(v, -1);
}
}
SketchReferenceRemap pruneUnusedSketchReferences(int index, QVector<SketchObject> &sketches,
    QVector<ExtrusionObject> &bodies, const QVector<ConstraintRef> &pending) {
    if (index < 0 || index >= sketches.size() || sketches.at(index).linkedReferences.isEmpty()) return {};
    const auto &input = sketches.at(index);
    const int segments = input.segments.size(), curves = input.curves.size();
    QVector<bool> used(segments + curves, false);
    const auto key = [&](const ConstraintRef &r) {
        if (r.kind == 0 && r.element >= 0 && r.element < segments) return r.element;
        if (r.kind == 1 && r.element >= 0 && r.element < curves) return segments + r.element;
        return -1;
    };
    const auto mark = [&](const ConstraintRef &r) { if (key(r) >= 0) used[key(r)] = true; };
    for (const auto &link : input.linkedReferences) if (link && (link->visible || !link->construction))
        for (const auto &r : link->entities) mark(r);
    for (const auto &c : input.geometricConstraints) {
        // Una cache non deve "riparare" perdendo vincoli gia' malformati.
        // Lascia lo schizzo diagnosticabile finche' l'utente li risolve.
        if ((c.type == ConstraintType::Pattern && c.pattern.sources.isEmpty())
            || (c.type == ConstraintType::Offset && (c.offset.sources.isEmpty() || c.offset.copies.isEmpty()))) return {};
        for (const auto &r : references(c)) {
            if ((r.kind == 0 || r.kind == 1) && key(r) < 0) return {};
            mark(r);
        }
    }
    for (const auto &r : pending) mark(r);
    const auto protect = [&](const GeometryRef &r, auto, int) {
        if (targetsSketch(r, index, sketches)) mark(r.element);
    };
    for (const auto &s : std::as_const(sketches)) visitSketchGeometryRefs(s, protect);
    for (const auto &body : std::as_const(bodies)) {
        visitGeometryRefs(body, protect);
        if (body.feature == BodyFeature::Revolution && body.sketchIndex == index && body.revolveAxis >= 0)
            mark({0, body.revolveAxis, -1});
        if (body.pathSketch == index) {
            for (int i : body.pathSegments) mark({0, i, -1});
            for (int i : body.pathCurves) mark({1, i, -1});
        }
        for (const auto &p : body.loftGuidePaths) if (p.sketch == index) {
            for (int i : p.segments) mark({0, i, -1});
            for (int i : p.curves) mark({1, i, -1});
        }
    }
    QSet<int> removedSegments, removedCurves;
    bool removedReportedError = false;
    for (const auto &link : input.linkedReferences) if (link && !link->visible && link->construction) {
        bool keep = false;
        for (const auto &r : link->entities) if (key(r) >= 0 && used[key(r)]) keep = true;
        if (!keep && !link->entities.isEmpty() && !link->error.isEmpty() && link->error == input.referenceError)
            removedReportedError = true;
        if (!keep) for (const auto &r : link->entities) {
            if (r.kind == 0 && key(r) >= 0) removedSegments.insert(r.element);
            if (r.kind == 1 && key(r) >= 0) removedCurves.insert(r.element);
        }
    }
    if (removedSegments.isEmpty() && removedCurves.isEmpty()) return {};
    SketchReferenceRemap result;
    result.curves.fill(-1, curves); int next = 0;
    for (int i = 0; i < curves; ++i) if (!removedCurves.contains(i)) result.curves[i] = next++;
    result.segments = removeSketchEntities(sketches[index], removedSegments, removedCurves);
    if (removedReportedError) {
        auto &sketch = sketches[index];
        sketch.referenceError.clear();
        for (const auto &link : sketch.linkedReferences)
            if (!link || !link->error.isEmpty())
                sketch.referenceError = link ? link->error : QStringLiteral("Riferimento associativo mancante.");
        for (const auto &c : sketch.geometricConstraints)
            if (c.pierce && !c.pierce->error.isEmpty()) sketch.referenceError = c.pierce->error;
    }
    const auto remap = [&](GeometryRef &r, auto, int) {
        if (!targetsSketch(r, index, sketches)) return;
        if (r.element.kind == 0) r.element.element = result.segments.value(r.element.element, -1);
        if (r.element.kind == 1) r.element.element = result.curves.value(r.element.element, -1);
    };
    for (auto &s : sketches) visitSketchGeometryRefs(s, remap);
    for (auto &body : bodies) {
        visitGeometryRefs(body, remap);
        if (body.feature == BodyFeature::Revolution && body.sketchIndex == index && body.revolveAxis >= 0)
            body.revolveAxis = result.segments.value(body.revolveAxis, -3);
        if (body.pathSketch == index) { remapList(body.pathSegments, result.segments); remapList(body.pathCurves, result.curves); }
        for (auto &p : body.loftGuidePaths) if (p.sketch == index) { remapList(p.segments, result.segments); remapList(p.curves, result.curves); }
    }
    return result;
}
void pruneUnusedSketchReferences(DocumentState &state) {
    for (int pass = 0; pass < state.sketches.size(); ++pass) {
        bool changed = false;
        for (int i = state.sketches.size() - 1; i >= 0; --i)
            changed = pruneUnusedSketchReferences(i, state.sketches, state.extrusions).changed() || changed;
        if (!changed) break;
    }
}
}
