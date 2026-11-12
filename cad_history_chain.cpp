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

void invalidateHistoryCaches(DocumentState &state, const QVector<int> &changed) {
    if (changed.isEmpty()) return;
    QSet<int> dirty(changed.cbegin(), changed.cend()), sketches;
    QSet<quint64> featureIds, sketchIds;
    for (int index : changed)
        if (index >= 0 && index < state.extrusions.size()) featureIds.insert(state.extrusions.at(index).featureId);
    // Punto fisso anche per vecchi documenti con schizzi ancora privi di una
    // posizione autonoma: nessuna dipendenza viene persa per l'ordine di visita.
    bool grew;
    do {
        grew = false;
        for (int index = 0; index < state.sketches.size(); ++index) {
            if (sketches.contains(index)) continue;
            const auto &sketch = state.sketches.at(index);
            bool affected = dirty.contains(sketch.datumPlane);
            visitSketchGeometryRefs(sketch, [&](const GeometryRef &ref, SketchGeometryRefFamily, int) {
                if (isBodyRef(ref)) affected |= ref.featureId ? featureIds.contains(ref.featureId) : dirty.contains(ref.index);
                if (isSketchRef(ref)) affected |= ref.featureId ? sketchIds.contains(ref.featureId) : sketches.contains(ref.index);
            });
            if (affected) { sketches.insert(index); sketchIds.insert(sketch.storyId); grew = true; }
        }
        for (int index = 0; index < state.extrusions.size(); ++index) {
            if (dirty.contains(index)) continue;
            bool affected = false;
            for (int operand : bodyOperands(state.extrusions.at(index))) affected |= dirty.contains(operand);
            for (int sketch : sketchesOf(state.extrusions.at(index))) affected |= sketches.contains(sketch);
            if (affected) { dirty.insert(index); featureIds.insert(state.extrusions.at(index).featureId); grew = true; }
        }
    } while (grew);
    for (int index : dirty)
        if (index >= 0 && index < state.extrusions.size()) state.extrusions[index].cachedGeometry = false;
}

}
