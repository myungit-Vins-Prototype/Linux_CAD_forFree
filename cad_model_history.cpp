#include "cad_model_history.h"
#include "cad_feature_dependencies.h"
#include "cad_geometry_refs.h"

#include <algorithm>

#include <QHash>
#include <QSet>

namespace ForgeCad {
namespace {

bool isReferenceFeature(const ExtrusionObject &feature) {
    return feature.operation < 0
        && (feature.feature == BodyFeature::DatumPlane || feature.feature == BodyFeature::Helix || feature.feature == BodyFeature::ProjectedCurve || feature.feature == BodyFeature::ExtractedCurve || feature.feature == BodyFeature::SpatialSketch);
}

bool isValidHistoryStage(const ExtrusionObject &feature) {
    if (feature.suppressed || !feature.error.isEmpty()) return false;
    if (feature.operation < 0 && feature.feature == BodyFeature::DatumPlane) return feature.datumValid;
    if (feature.operation < 0 && (feature.feature == BodyFeature::Helix || feature.feature == BodyFeature::ProjectedCurve || feature.feature == BodyFeature::ExtractedCurve || feature.feature == BodyFeature::SpatialSketch))
        return feature.curve != nullptr;
    return feature.forgeBody != nullptr;
}

quint64 inheritedBody(const QVector<ExtrusionObject> &features, const ExtrusionObject &feature) {
    const auto owner = [&](int index) {
        return index >= 0 && index < features.size() ? features.at(index).modelBodyId : quint64(0);
    };
    if (isReferenceFeature(feature)) return 0;
    if (feature.operation >= 0) return owner(feature.firstBody);
    switch (feature.feature) {
    case BodyFeature::SurfaceTools: return feature.surfaceTool.mode==1?0:owner(feature.firstBody);
    case BodyFeature::Blend:
    case BodyFeature::SheetTrim:
    case BodyFeature::SheetExtend:
    case BodyFeature::Scale:
    case BodyFeature::Sew:
    case BodyFeature::DeleteFace:
    case BodyFeature::Draft:
    case BodyFeature::Shell:
    case BodyFeature::Thread:
        return owner(feature.firstBody);
    case BodyFeature::Transform:
        return feature.move.copy ? 0 : owner(feature.firstBody);
    case BodyFeature::Pattern:
        return feature.patternComponent > 0 ? 0 : owner(feature.firstBody);
    case BodyFeature::Thicken:
        // Tutta la superficie diventa un solido: lo stesso corpo; facce scelte: un corpo nuovo.
        return feature.offsetFaces.isEmpty() ? owner(feature.firstBody) : 0;
    case BodyFeature::Ruled:
    case BodyFeature::PlanarSurface:
    case BodyFeature::BoundarySurface:
    case BodyFeature::FillSurface:
        return 0;  // superfici nuove: le curve e i bordi scelti restano dei loro corpi
    case BodyFeature::Extrusion:
    case BodyFeature::Revolution:
    case BodyFeature::Sweep:
        if (feature.mergeOperation != 0 && !feature.mergeBodies.isEmpty()) return owner(feature.mergeBodies.first());
        return 0;
    default:
        return 0;
    }
}

}

quint64 inheritedModelBody(const QVector<ExtrusionObject> &features, const ExtrusionObject &feature) {
    return inheritedBody(features, feature);
}

QVector<int> modelBodyFeatures(const DocumentState &state, quint64 bodyId) {
    QVector<int> result;
    if (bodyId == 0) return result;
    for (int index = 0; index < state.extrusions.size(); ++index)
        if (state.extrusions.at(index).modelBodyId == bodyId) result.append(index);
    return result;
}

int modelBodyTipIndex(const DocumentState &state, quint64 bodyId) {
    for (int index = state.extrusions.size() - 1; index >= 0; --index)
        if (state.extrusions.at(index).modelBodyId == bodyId && isValidHistoryStage(state.extrusions.at(index))) return index;
    return -1;
}

void normalizeModelHistory(QVector<ExtrusionObject> &features, QVector<ModelBody> &modelBodies) {
    quint64 nextFeature = 1, nextBody = 1;
    QSet<quint64> featureIds, bodyIds;
    for (const ExtrusionObject &feature : features) {
        if (feature.featureId) nextFeature = std::max(nextFeature, feature.featureId + 1);
        if (feature.modelBodyId) nextBody = std::max(nextBody, feature.modelBodyId + 1);
    }
    for (const ModelBody &body : modelBodies)
        if (body.id) nextBody = std::max(nextBody, body.id + 1);

    for (int index = 0; index < features.size(); ++index) {
        ExtrusionObject &feature = features[index];
        if (!feature.featureId || featureIds.contains(feature.featureId)) feature.featureId = nextFeature++;
        featureIds.insert(feature.featureId);
        if (isReferenceFeature(feature)) {
            feature.modelBodyId = 0;
            continue;
        }
        // Una modifica a monte puo' cambiare il corpo della base lasciando
        // nei discendenti il vecchio ID, gia' consumato da una cucitura o
        // booleana. Riallinea tutti gli stadi in ordine di dipendenza, anche
        // alla lettura di documenti salvati prima di questa correzione.
        const quint64 inherited = inheritedBody(features, feature);
        if (inherited) feature.modelBodyId = inherited;
        if (!feature.modelBodyId) feature.modelBodyId = nextBody++;
        bodyIds.insert(feature.modelBodyId);
    }

    // Migrazione dei riferimenti nati prima della storyboard: l'indice resta
    // la cache usata dal grafo, l'ID impedisce che un riordino li leghi a una
    // feature diversa.
    const auto bindOwner = [&](GeometryRef &ref) {
        if (isBodyRef(ref) && !ref.featureId && ref.index >= 0 && ref.index < features.size())
            ref.featureId = features.at(ref.index).featureId;
    };
    for (ExtrusionObject &feature : features)
        visitGeometryRefs(feature, [&](GeometryRef &ref, GeometryRefFamily, int) { bindOwner(ref); });

    QHash<quint64, ModelBody> existing;
    for (const ModelBody &body : modelBodies)
        if (body.id && bodyIds.contains(body.id)) existing.insert(body.id, body);
    // Raggruppa una volta gli stadi: evita di scandire l'intera storia per
    // ogni corpo e conserva l'ordine della prima apparizione nel documento.
    QVector<quint64> ordered;
    QHash<quint64, QVector<int>> stages;
    for (int index = 0; index < features.size(); ++index) {
        const quint64 id = features.at(index).modelBodyId;
        if (!id) continue;
        auto found = stages.find(id);
        if (found == stages.end()) {
            ordered.append(id);
            found = stages.insert(id, {});
        }
        found.value().append(index);
    }

    QVector<ModelBody> bodies;
    int number = 1;
    for (quint64 id : ordered) {
        const bool hadBody = existing.contains(id);
        ModelBody body = existing.value(id);
        const quint64 previousTip = body.tipFeatureId;
        body.id = id;
        if (body.name.trimmed().isEmpty()) body.name = QStringLiteral("Corpo %1").arg(number);
        const QVector<int> &indices = stages[id];
        int tip = -1, lastActive = -1, lastWithoutError = -1, savedTip = -1;
        for (int index : indices) {
            const ExtrusionObject &feature = features.at(index);
            if (feature.suppressed) continue;
            lastActive = index;
            if (!feature.error.isEmpty()) continue;
            lastWithoutError = index;
            if (feature.featureId == previousTip) savedTip = index;
            if (isValidHistoryStage(feature)) tip = index;
        }
        // Durante la lettura le definizioni precedono la cache geometrica:
        // conserva il tip serializzato, o l'ultimo stadio senza errore dei
        // vecchi documenti, quando la geometria non e' ancora disponibile.
        if (tip < 0) tip = savedTip >= 0 ? savedTip : lastWithoutError;
        body.tipFeatureId = tip >= 0 ? features.at(tip).featureId : 0;
        // Il flag storico del tip registra la visibilita' del corpo anche per
        // i documenti precedenti al formato storyboard e quando un altro
        // corpo lo consuma. Se pero' la coda della storia e' fallita, il flag
        // del vecchio stadio valido era stato spento quando quella feature
        // era ancora il tip: in quel solo caso lo stato del corpo e'
        // autorevole e va conservato.
        const bool failedTail = lastActive >= 0 && lastActive != tip && !features.at(lastActive).error.isEmpty();
        // Lo stesso quando il tip si sposta da solo: una feature fallita che ora
        // riesce (riattivata, o corretta in "Modifica parametri") ha il flag
        // spento, perche' finche' falliva si mostrava lo stadio precedente.
        const bool tipMoved = hadBody && previousTip && tip >= 0 && features.at(tip).featureId != previousTip;
        if ((!hadBody || (!failedTail && !tipMoved)) && tip >= 0) body.visible = features.at(tip).visible;
        for (int index : indices) features[index].visible = index == tip && body.visible;
        bodies.append(body);
        ++number;
    }
    modelBodies = std::move(bodies);
}

void normalizeModelHistory(DocumentState &state) {
    normalizeModelHistory(state.extrusions, state.modelBodies);
    normalizeSketchHistory(state.sketches, state.extrusions);
}

int patternSourceIndex(const QVector<ExtrusionObject> &features, int index) {
    if (index < 0 || index >= features.size()) return index;
    const auto &part = features.at(index);
    const int root = part.firstBody;
    if (part.feature == BodyFeature::Pattern && part.patternComponent > 0
        && root >= 0 && root < features.size() && root != index
        && features.at(root).feature == BodyFeature::Pattern && features.at(root).patternComponent <= 0)
        return root;
    return index;
}

void remapFeatureBodies(QVector<ExtrusionObject> &features, const std::function<int(int)> &map) {
    for (auto &feature : features) {
        feature.firstBody = map(feature.firstBody);
        feature.secondBody = map(feature.secondBody);
        for (int &i : feature.booleanTools) i = map(i);
        for (int &i : feature.mergeBodies) i = map(i);
        visitGeometryRefs(feature, [&](GeometryRef &ref, GeometryRefFamily, int) {
            if (isBodyRef(ref)) ref.index = map(ref.index);
        });
    }
}

QVector<int> normalizePatternResultOrder(QVector<SketchObject> &sketches, QVector<ExtrusionObject> &features) {
    QVector<QVector<int>> children(features.size());
    for (int i = 0; i < features.size(); ++i) {
        const int root = patternSourceIndex(features, i);
        if (root != i) children[root].append(i);
    }
    QVector<int> order;
    for (int i = 0; i < features.size(); ++i) {
        if (patternSourceIndex(features, i) != i) continue;
        order.append(i);
        auto &parts = children[i];
        std::stable_sort(parts.begin(), parts.end(), [&](int a, int b) {
            return features.at(a).patternComponent < features.at(b).patternComponent;
        });
        order += parts;
    }
    bool changed = false;
    for (int i = 0; i < order.size(); ++i) if (order.at(i) != i) { changed = true; break; }
    if (!changed) return {};
    QVector<int> map(features.size(), -1);
    QVector<ExtrusionObject> reordered;
    reordered.reserve(features.size());
    for (int old : order) { map[old] = reordered.size(); reordered.append(features.at(old)); }
    const auto mapped = [&](int old) { return old >= 0 ? map.value(old, -1) : old; };
    remapFeatureBodies(reordered, mapped);
    remapSketchSupports(sketches, mapped);
    features = std::move(reordered);
    return map;
}

int storyFeatureLimit(const QVector<ExtrusionObject> &features, const QVector<StoryStep> &steps, int position) {
    if (position < 0 || position >= steps.size()) return features.size();
    int end = 0;
    for (int p = 0; p < position; ++p) if (!steps.at(p).sketch) {
        const int root = steps.at(p).index;
        int limit = root + 1;
        while (limit < features.size() && patternSourceIndex(features, limit) == root) ++limit;
        end = std::max(end, limit);
    }
    return end;
}

QVector<StoryStep> modelStory(const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &features) {
    QHash<quint64, int> featureIndices;
    for (int i = 0; i < features.size(); ++i) if (features.at(i).featureId) featureIndices.insert(features.at(i).featureId, i);
    QVector<QVector<int>> before(features.size() + 1);
    for (int s = 0; s < sketches.size(); ++s) {
        int anchor = featureIndices.value(sketches.at(s).beforeFeatureId, features.size());
        const int root = patternSourceIndex(features, anchor);
        if (root != anchor) {
            // Un vecchio ancoraggio fra madre e risultato resta dopo l'intera
            // operazione, altrimenti lo schizzo precederebbe il suo appoggio.
            anchor = root + 1;
            while (anchor < features.size() && patternSourceIndex(features, anchor) != anchor) ++anchor;
        }
        before[anchor].append(s);
    }
    QVector<StoryStep> result;
    for (int f = 0; f <= features.size(); ++f) {
        std::stable_sort(before[f].begin(), before[f].end(), [&](int a, int b) {
            return sketches.at(a).storyOrder < sketches.at(b).storyOrder;
        });
        for (int s : before.at(f)) result.append({true, s});
        if (f < features.size() && patternSourceIndex(features, f) == f) result.append({false, f});
    }
    return result;
}

void placeStorySketches(QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &features, const QVector<StoryStep> &order) {
    quint64 next = 0;
    for (int i = order.size() - 1; i >= 0; --i) {
        const auto step = order.at(i);
        if (!step.sketch) next = features.at(step.index).featureId;
        else {
            sketches[step.index].beforeFeatureId = next;
            sketches[step.index].storyOrder = i;
        }
    }
}

void normalizeSketchHistory(QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &features) {
    quint64 next = 1;
    for (const auto &s : sketches) next = std::max(next, s.storyId + 1);
    QSet<quint64> seen;
    for (int s = 0; s < sketches.size(); ++s) {
        auto &sketch = sketches[s];
        if (!sketch.support && sketch.datumPlane >= 0 && sketch.datumPlane < features.size()) {
            auto ref = std::make_shared<GeometryRef>();
            ref->kind = 8; ref->index = sketch.datumPlane; ref->featureId = features.at(sketch.datumPlane).featureId;
            sketch.support = std::move(ref);
        }
        if (sketch.storyId && !seen.contains(sketch.storyId)) { seen.insert(sketch.storyId); continue; }
        sketch.storyId = next++;
        seen.insert(sketch.storyId);
        sketch.beforeFeatureId = 0;
        sketch.storyOrder = s;
        // Migrazione: conserva il contesto che l'albero mostrava, davanti al
        // primo utilizzatore; gli schizzi liberi restano alla fine.
        for (int f = 0; f < features.size(); ++f) if (sketchesOf(features.at(f)).contains(s)) {
            sketch.beforeFeatureId = features.at(f).featureId;
            break;
        }
    }
}

QString storySketchDependencyError(const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &features,
                                   const QVector<StoryStep> &order) {
    QVector<int> sp(sketches.size(), -1), fp(features.size(), -1);
    for (int i = 0; i < order.size(); ++i) {
        const auto step = order.at(i);
        auto &positions = step.sketch ? sp : fp;
        if (step.index < 0 || step.index >= positions.size() || positions.at(step.index) >= 0)
            return QStringLiteral("Sequenza della storia non valida.");
        positions[step.index] = i;
    }
    for (int f = 0; f < features.size(); ++f)
        if (patternSourceIndex(features, f) != f) fp[f] = fp.at(patternSourceIndex(features, f));
    if (sp.contains(-1) || fp.contains(-1)) return QStringLiteral("La storia deve contenere tutti gli schizzi e le lavorazioni.");
    for (int f = 0; f < features.size(); ++f) for (int s : sketchesOf(features.at(f)))
        if (s >= 0 && s < sp.size() && sp.at(s) >= fp.at(f))
            return QStringLiteral("\"%1\" deve precedere \"%2\", che lo utilizza.").arg(sketches.at(s).name, features.at(f).name);
    const auto referenceError = [&](int s, const GeometryRef &ref) -> QString {
        const bool body = isBodyRef(ref), sketch = isSketchRef(ref);
        if (!body && !sketch) return {};
        const auto &positions = body ? fp : sp;
        int index = ref.index;
        if (body && ref.featureId) {
            index = -1;
            for (int f = 0; f < features.size(); ++f) if (features.at(f).featureId == ref.featureId) { index = f; break; }
        }
        if (index < 0 || index >= positions.size() || positions.at(index) >= sp.at(s))
            return QStringLiteral("\"%1\" deve restare dopo il riferimento che utilizza.").arg(sketches.at(s).name);
        return {};
    };
    for (int s = 0; s < sketches.size(); ++s) {
        const auto &sketch = sketches.at(s);
        if (sketch.datumPlane >= 0) {
            GeometryRef ref; ref.kind = 8; ref.index = sketch.datumPlane;
            const QString error = referenceError(s, ref); if (!error.isEmpty()) return error;
        }
        QString error;
        visitSketchGeometryRefs(sketch, [&](const GeometryRef &ref, SketchGeometryRefFamily, int) {
            if (error.isEmpty()) error = referenceError(s, ref);
        });
        if (!error.isEmpty()) return error;
    }
    return {};
}

void remapSketchSupports(QVector<SketchObject> &sketches, const std::function<int(int)> &map) {
    for (auto &sketch : sketches) {
        if (sketch.datumPlane >= 0) sketch.datumPlane = map(sketch.datumPlane);
        visitSketchGeometryRefs(sketch, [&](GeometryRef &ref, SketchGeometryRefFamily, int) {
            if (isBodyRef(ref)) ref.index = map(ref.index);
        });
    }
}

}
