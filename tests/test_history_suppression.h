#pragma once

// Una feature soppressa non usa il proprio profilo: conserva lo stadio
// precedente anche se l'appoggio o un vincolo del profilo non e' risolvibile.
static void historySuppressionTests() {
    using namespace ForgeCad;
    using namespace Kernel;
    ExtrusionObject previous;
    previous.feature = BodyFeature::Primitive;
    previous.featureId = 100;
    previous.modelBodyId = 10;
    previous.forgeBody = std::make_shared<const Body>(makeBox(Frame3{}, 3, 4, 5));
    previous.solid = true;
    const QVector<ExtrusionObject> stages{previous};
    for (int invalid = 0; invalid < 3; ++invalid) {
        SketchObject sketch;
        sketch.name = QStringLiteral("Profilo non risolvibile");
        sketch.segments = {{{0, 0}, {1, 0}}};
        if (invalid == 0) sketch.supportError = QStringLiteral("Appoggio eliminato");
        else {
            SketchConstraint constraint;
            constraint.type = ConstraintType::Pierce;
            constraint.first = {0, 0, 0};
            if (invalid == 2) {
                auto pierce = std::make_shared<SketchPierceData>();
                pierce->error = QStringLiteral("Faccia non risolta");
                constraint.pierce = std::move(pierce);
            }
            sketch.geometricConstraints.append(constraint);
        }
        const QVector<SketchObject> sketches{sketch};
        ExtrusionObject feature;
        feature.feature = BodyFeature::Extrusion;
        feature.featureId = 101;
        feature.modelBodyId = previous.modelBodyId;
        feature.sketchIndex = 0;
        feature.mergeOperation = 1;
        feature.mergeBodies = {0};
        feature.suppressed = true;
        buildGeometry(feature, 1, sketches, stages);
        require(feature.error.isEmpty() && feature.forgeBody == previous.forgeBody && feature.solid,
                "feature soppressa bypassa appoggio/Pierce e conserva lo stadio precedente");

        // Non basta nascondere l'errore: lo stadio deve essere utilizzabile
        // dalla lavorazione successiva dello stesso corpo.
        ExtrusionObject downstream;
        downstream.feature = BodyFeature::Scale;
        downstream.featureId = 102;
        downstream.modelBodyId = previous.modelBodyId;
        downstream.firstBody = 1;
        downstream.scaleFactor = 2;
        buildGeometry(downstream, 2, sketches, {previous, feature});
        require(downstream.error.isEmpty() && downstream.forgeBody && downstream.solid,
                "lavorazione dopo la feature soppressa ancora calcolabile");

        feature.suppressed = false;
        buildGeometry(feature, 1, sketches, stages);
        require(!feature.error.isEmpty() && !feature.forgeBody,
                "riattivare la feature mantiene la validazione di appoggio/Pierce");
    }

    CadViewport view;
    PrimitiveParameters box;
    box.size[0] = box.size[1] = box.size[2] = 5.0;
    require(view.createPrimitive(box, QStringLiteral("Base reticolo")).isEmpty(), "base della cache soppressa");
    ExtrusionObject structure;
    structure.name = QStringLiteral("Struttura da sopprimere");
    structure.feature = BodyFeature::VolumetricStructure;
    structure.firstBody = 0;
    structure.volumeStructure.mode = VolumeStructureMode::Voxel;
    structure.volumeStructure.samplingPitch = 0.5;
    require(view.createBody(structure).isEmpty(), "struttura volumetrica da sopprimere");
    ExtrusionObject scale;
    scale.name = QStringLiteral("Scala dipendente");
    scale.feature = BodyFeature::Scale;
    scale.firstBody = 1;
    scale.scaleFactor = 2.0;
    require(view.createBody(scale).isEmpty(), "lavorazione dipendente dalla struttura");
    box.origin[0] = 20.0;
    require(view.createPrimitive(box, QStringLiteral("Indipendente")).isEmpty(), "corpo indipendente");

    const auto result = view.extrusions().at(1).forgeBody;
    const auto independent = view.extrusions().at(3).forgeBody;
    require(view.setFeatureSuppressed(1, true).isEmpty(), "soppressione della struttura");
    require(view.extrusions().at(1).forgeBody == view.extrusions().at(0).forgeBody
                && view.extrusions().at(1).suppressedBodyCache
                && view.extrusions().at(1).suppressedBodyCache->result == result
                && view.extrusions().at(3).forgeBody == independent,
            "passaggio dell'operando e risultato attivo tenuti separati");

    QByteArray bytes;
    DocumentState restored;
    const auto saved = view.currentDocument();
    require(encodeDocument(saved, bytes).isEmpty() && decodeDocument(bytes, restored).isEmpty(),
            "rilettura della cache con risultati soppressi");
    const auto retained = restored.extrusions.at(1).suppressedBodyCache;
    require(retained && restored.extrusions.at(1).cachedGeometry
                && restored.extrusions.at(1).forgeBody == restored.extrusions.at(0).forgeBody
                && writeBodyBinary(*retained->result) == writeBodyBinary(*result),
            "risultato distinto e identita dello stadio precedente conservati dopo la lettura");
    CadViewport reopened;
    reopened.loadDocument(restored);
    const auto scaledWhileSuppressed = reopened.extrusions().at(2).forgeBody;
    const auto unrelated = reopened.extrusions().at(3).forgeBody;
    require(reopened.setFeatureSuppressed(1, false).isEmpty()
                && reopened.extrusions().at(1).forgeBody == retained->result
                && reopened.extrusions().at(2).forgeBody != scaledWhileSuppressed
                && reopened.extrusions().at(3).forgeBody == unrelated,
            "riattivazione riusa il risultato e aggiorna solo i dipendenti");

    CadViewport changed;
    changed.loadDocument(restored);
    auto larger = changed.extrusions().at(0);
    larger.primitive.size[0] = 6.0;
    require(changed.updateBody(0, larger).isEmpty(), "modifica dell'operando mentre la struttura e soppressa");
    require(changed.setFeatureSuppressed(1, false).isEmpty()
                && changed.extrusions().at(1).forgeBody
                && changed.extrusions().at(1).forgeBody != retained->result,
            "un operando cambiato invalida il risultato conservato");

    auto changedParameters = restored.extrusions.at(1);
    changedParameters.suppressed = false;
    changedParameters.volumeStructure.samplingPitch = 0.4;
    buildBody(changedParameters, 1, restored.sketches, restored.extrusions, 0, false);
    require(changedParameters.forgeBody && changedParameters.forgeBody != retained->result
                && changedParameters.forgeBody != restored.extrusions.at(0).forgeBody,
            "parametri cambiati richiedono una nuova geometria attiva");

    auto oldCache = saved;
    oldCache.extrusions[1].suppressedBodyCache.reset();
    require(encodeDocument(oldCache, bytes).isEmpty() && decodeDocument(bytes, restored).isEmpty()
                && restored.extrusions.at(1).cachedGeometry
                && restored.extrusions.at(1).forgeBody == restored.extrusions.at(0).forgeBody
                && !restored.extrusions.at(1).suppressedBodyCache,
            "la cache precedente conserva la genealogia degli stadi di passaggio");
}
