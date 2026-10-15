#ifndef FORGECAD_TEST_BLEND_HISTORY_H
#define FORGECAD_TEST_BLEND_HISTORY_H

static void blendHistoryTest() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    QTemporaryDir directory;
    require(directory.isValid(), "directory temporanea per genealogia raccordi");
    for (int mode : {0, 1, 2}) {
        CadViewport view;
        PrimitiveParameters primitive;
        primitive.size[0] = primitive.size[1] = primitive.size[2] = 10;
        require(view.createPrimitive(primitive, QStringLiteral("Base interattiva")).isEmpty(), "base creata dall'interfaccia con ID definitivo");
        const ForgeBody base = view.extrusions().front().forgeBody;
        require(bool(base), "base del raccordo");
        const EdgeId edge = nearestEdge(*base, Vec3(10, 5, 10), 1e-8);
        const QVector<EdgePoint> references{edgeReference(*base, edge, Vec3(10, 5, 10))};
        ChamferSpec spec;
        if (mode == 2) { spec.mode = 1; spec.second = 1.5; }
        ForgeBody preview;
        if (mode == 0) preview = ViewportInteractionTest::preparedBlendHistoryPreview(view, references, 1, false, spec);
        require(view.createBlend(0, references, 1, mode != 0, QStringLiteral("Raccordo dichiarato"), spec).isEmpty(), "raccordo o smusso costruito");
        const auto original = view.currentDocument();
        const auto body = original.extrusions[1].forgeBody;
        if (preview) require(preview == body, "promozione dell'anteprima senza ricostruzione del B-rep");
        const auto bytes = writeBodyBinary(*body);
        const auto names = elementNamesOf(*body);
        require(bool(names), "nomi del raccordo disponibili");
        const auto verify = [&](const DocumentState &state) {
            const auto shape = state.extrusions[1].forgeBody;
            require(shape && state.extrusions[1].error.isEmpty() && writeBodyBinary(*shape) == bytes, "geometria raccordo stabile");
            const auto history = featureHistoryOf(*shape);
            require(history && history->operandCount() == 1, "raccordo riferito alla sola base");
            for (FaceId f : shape->faces()) require(history->isDeclared(EntityKind::Face, f.index), "facce del raccordo dichiarate");
            const auto generated = history->generated(EntityKind::Face, EntityKind::Edge, 0, edge.index);
            require(generated.size() == 1, "faccia generata dallo spigolo scelto");
            require(entityEvolutionText(*shape, EntityKind::Face, generated.front()).contains(QStringLiteral("origine dichiarata")),
                "diagnostica della faccia emessa dall'algoritmo");
            const auto next = elementNamesOf(*shape);
            require(next && next->faces == names->faces && next->edges == names->edges && next->vertices == names->vertices,
                "tutti i nomi conservati dopo riapertura e rigenerazione");
        };
        verify(original);
        const auto sameHistory = featureHistoryOf(*body);
        view.setHistoryPosition(1); view.setHistoryPosition(2);
        require(view.extrusions()[1].forgeBody == body && featureHistoryOf(*body) == sameHistory, "rollback senza ricalcolo del raccordo");
        const QString path = directory.filePath(QStringLiteral("raccordo-%1.prt").arg(mode));
        require(saveDocumentFile(path, original, true).isEmpty(), "salvataggio genealogia raccordo");
        DocumentState loaded;
        require(loadDocumentFile(path, loaded).isEmpty() && loaded.extrusions[1].cachedGeometry, "raccordo riletto dalla cache");
        const auto metadata = declaredHistorySnapshot(loaded.extrusions[1], 1, loaded.extrusions);
        require(metadata.history && metadata.operandIndices == QVector<int>{0}, "origini Generated persistenti");
        CadViewport reopened; reopened.loadDocument(loaded); verify(reopened.currentDocument());
        require(reopened.rebuildAllFeatures() == 0, "rigenerazione del raccordo riaperto");
        verify(reopened.currentDocument());

        // Anche un risultato d'anteprima ancora senza registrazione deve
        // conservare le dichiarazioni al salvataggio, senza inferenza.
        QString error;
        auto pending = original;
        pending.extrusions[1].forgeBody = forgeBlend(base, references, 1, mode != 0, &error, spec);
        require(pending.extrusions[1].forgeBody && !featureHistoryOf(*pending.extrusions[1].forgeBody), "anteprima preparata senza nomi");
        require(saveDocumentFile(path, pending, true).isEmpty() && !featureHistoryOf(*pending.extrusions[1].forgeBody), "anteprima salvata senza inferenza");
        require(loadDocumentFile(path, loaded).isEmpty(), "lettura della dichiarazione d'anteprima");
        CadViewport prepared; prepared.loadDocument(loaded); verify(prepared.currentDocument());
        const auto changed = forgeBlend(base, references, 2, mode != 0, &error, spec);
        require(bool(changed), "raggio o distanza modificati");
        auto feature = original.extrusions[1]; feature.forgeBody = changed; feature.blendSize = 2;
        registerFeatureGenealogy(feature, 1, {}, original.extrusions);
        const auto changedHistory = featureHistoryOf(*changed);
        require(bool(changedHistory), "storia del raccordo modificato registrata");
        const auto changedGenerated = changedHistory->generated(EntityKind::Face, EntityKind::Edge, 0, edge.index);
        require(changedGenerated.size() == 1, "origine della faccia a raggio diverso");
        const int oldFace = sameHistory->generated(EntityKind::Face, EntityKind::Edge, 0, edge.index).front();
        require(elementName(*body, EntityKind::Face, oldFace) == elementName(*changed, EntityKind::Face, changedGenerated.front()),
            "nome generato indipendente dal raggio");
    }
}

#endif
