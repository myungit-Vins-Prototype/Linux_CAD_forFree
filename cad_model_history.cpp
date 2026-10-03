#include "cad_model_history.h"

#include <algorithm>

#include <QHash>
#include <QSet>

namespace ForgeCad {
namespace {

bool isReferenceFeature(const ExtrusionObject &feature) {
    return feature.operation < 0 && (feature.feature == BodyFeature::DatumPlane || feature.feature == BodyFeature::Helix);
}

quint64 inheritedBody(const QVector<ExtrusionObject> &features, const ExtrusionObject &feature) {
    const auto owner = [&](int index) {
        return index >= 0 && index < features.size() ? features.at(index).modelBodyId : quint64(0);
    };
    if (isReferenceFeature(feature)) return 0;
    if (feature.operation >= 0) return owner(feature.firstBody);
    switch (feature.feature) {
    case BodyFeature::Blend:
    case BodyFeature::SheetTrim:
    case BodyFeature::SheetExtend:
    case BodyFeature::Scale:
    case BodyFeature::Sew:
    case BodyFeature::DeleteFace:
    case BodyFeature::Shell:
        return owner(feature.firstBody);
    case BodyFeature::Transform:
        return feature.move.copy ? 0 : owner(feature.firstBody);
    case BodyFeature::Pattern:
        return owner(feature.firstBody);
    case BodyFeature::Ruled:
    case BodyFeature::PlanarSurface:
    case BodyFeature::BoundarySurface:
        return 0;  // superfici nuove: le curve e i bordi scelti restano dei loro corpi
    case BodyFeature::Extrusion:
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
        if (state.extrusions.at(index).modelBodyId == bodyId && !state.extrusions.at(index).suppressed) return index;
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
        if (!feature.modelBodyId) feature.modelBodyId = inheritedBody(features, feature);
        if (!feature.modelBodyId) feature.modelBodyId = nextBody++;
        bodyIds.insert(feature.modelBodyId);
    }

    // Migrazione dei riferimenti nati prima della storyboard: l'indice resta
    // la cache usata dal grafo, l'ID impedisce che un riordino li leghi a una
    // feature diversa.
    const auto bindOwner = [&](GeometryRef &ref) {
        const bool bodyRef = ref.kind == 3 || ref.kind == 4 || ref.kind == 5 || ref.kind == 8 || ref.kind == 9 || ref.kind == 10;
        if (bodyRef && !ref.featureId && ref.index >= 0 && ref.index < features.size())
            ref.featureId = features.at(ref.index).featureId;
    };
    for (ExtrusionObject &feature : features) {
        for (GeometryRef &ref : feature.datum.refs) bindOwner(ref);
        for (GeometryRef &ref : feature.pattern.refs) bindOwner(ref);
        for (QVector<GeometryRef> *refs : {&feature.ruledFirst, &feature.ruledSecond, &feature.planarRefs})
            for (GeometryRef &ref : *refs) bindOwner(ref);
        bindOwner(feature.extentRef);
        bindOwner(feature.move.axis);
    }

    QHash<quint64, ModelBody> existing;
    for (const ModelBody &body : modelBodies)
        if (body.id && bodyIds.contains(body.id)) existing.insert(body.id, body);
    QVector<quint64> ordered;
    for (const ExtrusionObject &feature : features)
        if (feature.modelBodyId && !ordered.contains(feature.modelBodyId)) ordered.append(feature.modelBodyId);

    QVector<ModelBody> bodies;
    int number = 1;
    for (quint64 id : ordered) {
        ModelBody body = existing.value(id);
        body.id = id;
        if (body.name.trimmed().isEmpty()) body.name = QStringLiteral("Corpo %1").arg(number);
        int tip = -1;
        for (int index = features.size() - 1; index >= 0; --index)
            if (features.at(index).modelBodyId == id && !features.at(index).suppressed) { tip = index; break; }
        body.tipFeatureId = tip >= 0 ? features.at(tip).featureId : 0;
        // Il flag storico del tip registra la visibilita' del corpo anche per
        // i documenti precedenti al formato storyboard.
        if (tip >= 0) body.visible = features.at(tip).visible;
        for (int index = 0; index < features.size(); ++index)
            if (features.at(index).modelBodyId == id) features[index].visible = index == tip && body.visible;
        bodies.append(body);
        ++number;
    }
    modelBodies = std::move(bodies);
}

void normalizeModelHistory(DocumentState &state) { normalizeModelHistory(state.extrusions, state.modelBodies); }

}
