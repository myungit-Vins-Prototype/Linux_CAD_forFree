#include "cad_history_chain.h"
#include "cad_feature_dependencies.h"
#include "cad_geometry_refs.h"

#include <QHash>

namespace ForgeCad {

QVector<int> reconnectMergedHistory(QVector<ExtrusionObject> &features, const QSet<quint64> &bodyIds) {
    QHash<quint64, int> previous;
    QVector<int> changed;
    for (int index = 0; index < features.size(); ++index) {
        auto &feature = features[index];
        const quint64 owner = feature.modelBodyId;
        if (!owner || (!bodyIds.isEmpty() && !bodyIds.contains(owner))) continue;
        const int base = previous.value(owner, -1);
        if (base >= 0 && mergingFeature(feature) && feature.mergeOperation != 0) {
            for (int &target : feature.mergeBodies) {
                // Conserva bersagli di altri corpi e riferimenti non validi:
                // quelli devono mantenere la propria diagnostica esplicita.
                if (target < 0 || target >= index || features.at(target).modelBodyId != owner) continue;
                if (target != base) {
                    target = base;
                    feature.cachedGeometry = false;
                    changed.append(index);
                }
                break;
            }
        }
        previous.insert(owner, index);
    }
    return changed;
}

HistoryDependents historyDependents(const QVector<SketchObject> &sketches,
                                   const QVector<ExtrusionObject> &features,
                                   const QVector<bool> &changedFeatures,
                                   const QSet<int> &changedSketches) {
    const int featureCount = features.size();
    HistoryDependents result{QVector<bool>(featureCount, false), {}};
    QVector<int> queue;
    // Nodi delle lavorazioni seguiti dai nodi degli schizzi.
    const auto mark = [&](int node) {
        if (node < featureCount) {
            if (result.features.at(node)) return;
            result.features[node] = true;
        } else {
            const int sketch = node - featureCount;
            if (result.sketches.contains(sketch)) return;
            result.sketches.insert(sketch);
        }
        queue.append(node);
    };
    for (int index = 0; index < featureCount && index < changedFeatures.size(); ++index)
        if (changedFeatures.at(index)) mark(index);
    for (int index : changedSketches)
        if (index >= 0 && index < sketches.size()) mark(featureCount + index);
    if (queue.isEmpty()) return result;

    QVector<QVector<int>> consumers(featureCount + sketches.size());
    QVector<int> nextExtents;
    QHash<quint64, int> featureIndices, sketchIndices, previousBodyStage;
    for (int index = 0; index < featureCount; ++index) {
        const auto &feature = features.at(index);
        if (feature.featureId) featureIndices.insert(feature.featureId, index);
        const int previous = previousBodyStage.value(feature.modelBodyId, -1);
        previousBodyStage.insert(feature.modelBodyId, index);
        if (feature.suppressed) {
            // La soppressione copia il risultato dello stadio precedente.
            if (previous >= 0) consumers[previous].append(index);
            continue;
        }
        for (int source : bodyOperands(feature))
            if (source >= 0 && source < featureCount) consumers[source].append(index);
        for (int source : sketchesOf(feature))
            if (source >= 0 && source < sketches.size()) consumers[featureCount + source].append(index);
        if (feature.operation < 0 && feature.feature == BodyFeature::Extrusion && feature.extent == 4)
            nextExtents.append(index);
    }
    for (int index = 0; index < sketches.size(); ++index)
        if (sketches.at(index).storyId) sketchIndices.insert(sketches.at(index).storyId, index);
    for (int index = 0; index < sketches.size(); ++index) {
        const auto &sketch = sketches.at(index);
        const int node = featureCount + index;
        if (sketch.datumPlane >= 0 && sketch.datumPlane < featureCount)
            consumers[sketch.datumPlane].append(node);
        visitSketchGeometryRefs(sketch, [&](const GeometryRef &ref, SketchGeometryRefFamily, int) {
            if (isBodyRef(ref)) {
                const int source = ref.featureId ? featureIndices.value(ref.featureId, -1) : ref.index;
                if (source >= 0 && source < featureCount) consumers[source].append(node);
            } else if (isSketchRef(ref)) {
                const int source = ref.featureId ? sketchIndices.value(ref.featureId, -1) : ref.index;
                if (source >= 0 && source < sketches.size()) consumers[featureCount + source].append(node);
            }
        });
    }
    for (int pos = 0; pos < queue.size(); ++pos) {
        const int node = queue.at(pos);
        for (int consumer : consumers.at(node)) mark(consumer);
        if (node >= featureCount || isCurveBody(features.at(node)) || isDatumBody(features.at(node))) continue;
        // Anche una forma assente o esclusa prima della modifica puo' diventare
        // un solido candidato per un'estrusione successiva "Fino al prossimo".
        while (!nextExtents.isEmpty() && nextExtents.last() > node) mark(nextExtents.takeLast());
    }
    return result;
}

void invalidateHistoryCaches(DocumentState &state, const QVector<int> &changed) {
    if (changed.isEmpty()) return;
    QVector<bool> seeds(state.extrusions.size(), false);
    for (int index : changed)
        if (index >= 0 && index < seeds.size()) seeds[index] = true;
    const auto affected = historyDependents(state.sketches, state.extrusions, seeds, {});
    for (int index = 0; index < affected.features.size(); ++index)
        if (affected.features.at(index)) state.extrusions[index].cachedGeometry = false;
}

}
