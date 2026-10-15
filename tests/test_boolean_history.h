#ifndef FORGECAD_TEST_BOOLEAN_HISTORY_H
#define FORGECAD_TEST_BOOLEAN_HISTORY_H

// Incluso dal runner dopo la definizione del viewport e di require.
static void booleanHistoryTest() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    const auto faceThrough = [](const Body &body, const Vec3 &point) {
        for (FaceId face : body.faces()) {
            const auto on = projectPoint(*body.face(face).surface, point);
            if (on.distance < 1e-8 && classifyPointOnFace(body, face, point, 1e-8) == PointLocation::Inside) return face;
        }
        return FaceId();
    };
    QTemporaryDir directory;
    require(directory.isValid(), "directory temporanea per le booleane dichiarate");
    for (bool repeated : {false, true}) {
        DocumentState definition;
        for (int i = 0; i < 3; ++i) {
            ExtrusionObject box;
            box.feature = BodyFeature::Primitive; box.featureId = 94000 + i;
            box.primitive.origin[0] = 10 * i;
            box.primitive.size[0] = box.primitive.size[1] = box.primitive.size[2] = 10;
            definition.extrusions.append(box);
        }
        ExtrusionObject united;
        united.operation = 0; united.firstBody = 0; united.secondBody = 1; united.featureId = 94003;
        united.booleanTools = repeated ? QVector<int>{2, 1, 2} : QVector<int>{2};
        definition.extrusions.append(united);
        CadViewport view; view.loadDocument(definition);
        const auto original = view.currentDocument();
        const auto &result = original.extrusions.last();
        require(result.forgeBody && result.error.isEmpty(), "unione di tre operandi costruita");
        const auto bytes = writeBodyBinary(*result.forgeBody);
        const auto originalNames = elementNamesOf(*result.forgeBody);
        require(bool(originalNames), "nomi della booleana multistrumento");
        const auto verify = [&](const DocumentState &state) {
            const auto &body = state.extrusions.last();
            require(body.forgeBody && body.error.isEmpty() && writeBodyBinary(*body.forgeBody) == bytes, "B-rep della booleana stabile");
            const auto history = featureHistoryOf(*body.forgeBody);
            require(history && history->operandCount() == 3, "intermedi ricondotti ai tre operandi effettivi");
            for (FaceId face : body.forgeBody->faces()) {
                require(history->isDeclared(EntityKind::Face, face.index), "tutte le facce della booleana hanno origine dichiarata");
                for (const auto &link : history->origins(EntityKind::Face, face.index))
                    require(link.operand >= 0 && link.operand < 3 && state.extrusions[link.operand].forgeBody->contains(FaceId(link.index)),
                        "nessun indice riferito a un corpo intermedio");
            }
            const FaceId top = faceThrough(*body.forgeBody, Vec3(5, 5, 10));
            require(top.valid() && history->origins(EntityKind::Face, top.index).size() == 3, "faccia fusa da tutti e tre i corpi");
            const auto names = elementNamesOf(*body.forgeBody);
            require(names && names->faces == originalNames->faces && names->edges == originalNames->edges
                && names->vertices == originalNames->vertices, "nomi della booleana stabili dopo riapertura e rigenerazione");
            require(names->faces[std::size_t(top.index)].size() == 3, "tre nomi ereditati dalla faccia fusa");
            for (int operand = 0; operand < 3; ++operand) {
                const FaceId source = faceThrough(*state.extrusions[operand].forgeBody, Vec3(10 * operand + 5, 5, 10));
                require(source.valid() && history->evolution(EntityKind::Face, operand, source.index) == Evolution::Merged,
                    "fusione dichiarata per ognuna delle facce originali");
            }
        };
        verify(original);
        const QString path = directory.filePath(repeated ? QStringLiteral("ripetuta.prt") : QStringLiteral("tre-operandi.prt"));
        require(saveDocumentFile(path, original, true).isEmpty(), "salvataggio della booleana dichiarata");
        DocumentState loaded;
        require(loadDocumentFile(path, loaded).isEmpty() && loaded.extrusions.last().cachedGeometry, "booleana ripristinata dalla cache 6");
        const auto snapshot = declaredHistorySnapshot(loaded.extrusions.last(), 3, loaded.extrusions);
        require(snapshot.history && snapshot.operandIndices == QVector<int>({0, 1, 2}), "metadati dei tre operandi persistenti");
        CadViewport reopened; reopened.loadDocument(loaded); verify(reopened.currentDocument());
        require(reopened.rebuildAllFeatures() == 0, "rigenerazione della booleana multistrumento");
        verify(reopened.currentDocument());

        // Stessa catena, ancora senza registrazione di una feature: anche
        // il salvataggio deve comporre le dichiarazioni gia' preparate.
        DocumentState pending = original;
        QString error;
        auto intermediate = forgeBoolean(original.extrusions[0].forgeBody, original.extrusions[1].forgeBody, ::BooleanOperation::Union, &error);
        pending.extrusions[3].forgeBody = forgeBoolean(intermediate, original.extrusions[2].forgeBody, ::BooleanOperation::Union, &error);
        pending.extrusions[3].booleanTools = {2};
        require(pending.extrusions[3].forgeBody && !featureHistoryOf(*pending.extrusions[3].forgeBody), "booleana preparata senza nomi");
        const QString pendingPath = directory.filePath(QStringLiteral("preparata-%1.prt").arg(repeated));
        require(saveDocumentFile(pendingPath, pending, true).isEmpty() && !featureHistoryOf(*pending.extrusions[3].forgeBody),
            "salvataggio degli intermedi senza inferenza");
        DocumentState pendingLoaded;
        require(loadDocumentFile(pendingPath, pendingLoaded).isEmpty(), "lettura degli intermedi composti al salvataggio");
        const auto prepared = declaredHistorySnapshot(pendingLoaded.extrusions[3], 3, pendingLoaded.extrusions);
        require(prepared.history && prepared.history->operandCount() == 3, "intermedi preparati ridotti agli operandi del documento");

        // Se il corpo intermedio non emette una dichiarazione, si deve
        // conservare il ripiego solo sulle facce che ne hanno bisogno.
        BooleanOptions options;
        auto unknown = std::make_shared<const Body>(booleanOperation(*original.extrusions[0].forgeBody,
            *original.extrusions[1].forgeBody, ForgeCad::Kernel::BooleanOperation::Unite, options));
        auto partial = original.extrusions[3]; partial.booleanTools = {2};
        partial.forgeBody = forgeBoolean(unknown, original.extrusions[2].forgeBody, ::BooleanOperation::Union, &error);
        require(bool(partial.forgeBody), "booleana con intermedio senza dichiarazione");
        registerFeatureGenealogy(partial, 3, {}, original.extrusions);
        const auto history = featureHistoryOf(*partial.forgeBody);
        const auto left = faceThrough(*partial.forgeBody, Vec3(0, 5, 5));
        const auto right = faceThrough(*partial.forgeBody, Vec3(30, 5, 5));
        require(history && left.valid() && right.valid() && !history->isDeclared(EntityKind::Face, left.index)
            && history->isDeclared(EntityKind::Face, right.index), "copertura parziale distinta dall'inferenza");
    }
    // Due ruoli che consumano la stessa feature si riconducono a un solo
    // operando effettivo, senza duplicare nomi o dichiarare false fusioni.
    DocumentState self;
    ExtrusionObject root; root.feature = BodyFeature::Primitive; root.featureId = 95000;
    root.primitive.size[0] = root.primitive.size[1] = root.primitive.size[2] = 3;
    ExtrusionObject united; united.operation = 0; united.firstBody = united.secondBody = 0; united.featureId = 95001;
    self.extrusions = {root, united};
    CadViewport same; same.loadDocument(self);
    const auto sameState = same.currentDocument();
    const auto &shape = sameState.extrusions[1].forgeBody;
    require(bool(shape), "booleana della stessa feature con se stessa");
    const auto sameHistory = featureHistoryOf(*shape);
    require(sameHistory && sameHistory->operandCount() == 1, "operando ripetuto collassato");
    for (FaceId face : shape->faces())
        require(sameHistory->isDeclared(EntityKind::Face, face.index) && sameHistory->origins(EntityKind::Face, face.index).size() == 1,
            "origine ripetuta deduplicata");

    // Un risultato gia' registrato come feature e' un confine: la booleana
    // seguente si riferisce a esso, senza saltare alle sue primitive.
    DocumentState chain = self;
    chain.extrusions[1] = root; chain.extrusions[1].featureId = 95002; chain.extrusions[1].primitive.origin[0] = 3;
    auto first = united; first.firstBody = 0; first.secondBody = 1; first.featureId = 95003;
    auto tool = root; tool.featureId = 95004; tool.primitive.origin[0] = 6;
    auto next = united; next.firstBody = 2; next.secondBody = 3; next.featureId = 95005;
    chain.extrusions << first << tool << next;
    CadViewport boundary; boundary.loadDocument(chain);
    const auto boundaryState = boundary.currentDocument();
    const auto &last = boundaryState.extrusions.last();
    require(last.forgeBody && last.error.isEmpty(), "catena di due feature booleane");
    const auto operands = genealogyOperandsOf(*last.forgeBody);
    const auto history = featureHistoryOf(*last.forgeBody);
    require(history && history->operandCount() == 2 && operands.size() == 2 && operands[0].index == 2 && operands[1].index == 3,
        "composizione fermata agli operandi effettivi della feature");
    for (FaceId face : last.forgeBody->faces()) require(history->isDeclared(EntityKind::Face, face.index), "origine dichiarata nella catena");
}

#endif
