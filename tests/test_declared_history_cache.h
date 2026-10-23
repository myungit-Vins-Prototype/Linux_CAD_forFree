#ifndef FORGECAD_TEST_DECLARED_HISTORY_CACHE_H
#define FORGECAD_TEST_DECLARED_HISTORY_CACHE_H

// Incluso dal runner dopo la definizione del viewport e di require.
static void declaredHistoryCacheTest() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    DocumentState definition;
    ExtrusionObject root;
    root.feature = BodyFeature::Primitive; root.featureId = 92000;
    root.primitive.size[0] = 2; root.primitive.size[1] = 3; root.primitive.size[2] = 4;
    ExtrusionObject offset;
    offset.feature = BodyFeature::SurfaceOffset; offset.featureId = 92001;
    offset.firstBody = 0; offset.distance = 0.2; offset.offsetSew = false;
    ExtrusionObject copy = offset;
    copy.featureId = 92002; copy.firstBody = 1; copy.distance = 0;
    ExtrusionObject nested = offset;
    nested.featureId = 92003; nested.firstBody = 1; nested.distance = -0.1;
    definition.extrusions = {root, offset, copy, nested};
    CadViewport built; built.loadDocument(definition);
    const DocumentState original = built.currentDocument();
    std::vector<ElementNames> names;
    std::vector<std::string> geometry;
    for (const auto &body : original.extrusions) {
        require(body.forgeBody && body.error.isEmpty(), "costruzione della catena di offset per la persistenza");
        const auto table = elementNamesOf(*body.forgeBody);
        require(bool(table), "nomi della catena di offset");
        names.push_back(*table); geometry.push_back(writeBodyBinary(*body.forgeBody));
    }
    const auto verify = [&](const DocumentState &state) {
        require(state.extrusions.size() == original.extrusions.size(), "numero di feature nel ciclo di persistenza");
        for (int i = 0; i < state.extrusions.size(); ++i) {
            const auto &body = state.extrusions.at(i);
            require(body.forgeBody && body.error.isEmpty(), "geometria disponibile dopo riapertura");
            require(writeBodyBinary(*body.forgeBody) == geometry[std::size_t(i)], "B-rep invariato nel ciclo salva-riapri-rigenera");
            const auto table = elementNamesOf(*body.forgeBody);
            require(table && table->faces == names[std::size_t(i)].faces && table->edges == names[std::size_t(i)].edges
                && table->vertices == names[std::size_t(i)].vertices, "tutti i nomi persistono nel ciclo salva-riapri-rigenera");
            if (i == 0) continue;
            const auto history = featureHistoryOf(*body.forgeBody);
            require(bool(history), "genealogia ripristinata");
            for (FaceId face : body.forgeBody->faces()) {
                require(history->isDeclared(EntityKind::Face, face.index), "copertura dichiarata presente dopo riapertura");
                const auto &links = history->origins(EntityKind::Face, face.index);
                require(links.size() == 1 && links.front().relation == (i == 2 ? HistoryRelation::Same : HistoryRelation::Modified),
                    "relazioni della copia e dell'offset ripristinate");
            }
        }
    };
    QTemporaryDir directory;
    require(directory.isValid(), "directory temporanea per la genealogia");
    const QString path = directory.filePath(QStringLiteral("genealogia-cache6.prt"));
    require(saveDocumentFile(path, original, true).isEmpty(), "salvataggio delle dichiarazioni");
    DocumentState loaded;
    require(loadDocumentFile(path, loaded).isEmpty(), "lettura delle dichiarazioni");
    for (int i = 0; i < loaded.extrusions.size(); ++i)
        require(loaded.extrusions[i].cachedGeometry && loaded.extrusions[i].forgeBody != original.extrusions[i].forgeBody,
            "nuovi B-rep dalla cache, senza riusare il registro originale");
    CadViewport reopened; reopened.loadDocument(loaded); verify(reopened.currentDocument());
    const QString resaved = directory.filePath(QStringLiteral("risalvata.prt"));
    require(saveDocumentFile(resaved, reopened.currentDocument(), true).isEmpty(), "risalvataggio delle dichiarazioni ripristinate");
    DocumentState twice;
    require(loadDocumentFile(resaved, twice).isEmpty(), "seconda riapertura");
    CadViewport twiceView; twiceView.loadDocument(twice); verify(twiceView.currentDocument());
    require(reopened.rebuildAllFeatures() == 0, "rigenerazione della catena dopo riapertura");
    verify(reopened.currentDocument());

    // Il registro puo' precedere il riordino: si salvano gli indici correnti.
    DocumentState shifted = original;
    ExtrusionObject extra = original.extrusions.front(); extra.featureId = 92010;
    extra.forgeBody = std::make_shared<const Body>(makeBox(Frame3(), 1, 1, 1));
    shifted.extrusions.prepend(extra);
    for (int i = 1; i < shifted.extrusions.size(); ++i)
        if (shifted.extrusions[i].firstBody >= 0) ++shifted.extrusions[i].firstBody;
    const QString reordered = directory.filePath(QStringLiteral("riordinata.prt"));
    require(saveDocumentFile(reordered, shifted, true).isEmpty(), "salvataggio dopo spostamento degli indici");
    DocumentState reorderedState;
    require(loadDocumentFile(reordered, reorderedState).isEmpty(), "lettura con nuovi indici");
    const auto reorderedHistory = declaredHistorySnapshot(reorderedState.extrusions[2], 2, reorderedState.extrusions);
    require(reorderedHistory.history && reorderedHistory.operandIndices == QVector<int>{1}, "operando salvato con ID e indice correnti");

    DocumentState pending;
    pending.extrusions = {original.extrusions[0], original.extrusions[1]};
    QString error;
    pending.extrusions[1].forgeBody = forgeOffsetFaces(pending.extrusions[0].forgeBody, {}, 0.2, &error, nullptr, false);
    require(pending.extrusions[1].forgeBody && !featureHistoryOf(*pending.extrusions[1].forgeBody), "dichiarazione ancora senza nomi");
    const QString pendingPath = directory.filePath(QStringLiteral("preparata.prt"));
    require(saveDocumentFile(pendingPath, pending, true).isEmpty() && !featureHistoryOf(*pending.extrusions[1].forgeBody),
        "salvare le dichiarazioni non avvia inferenza o calcolo dei nomi");
    DocumentState pendingLoaded;
    require(loadDocumentFile(pendingPath, pendingLoaded).isEmpty()
        && declaredHistorySnapshot(pendingLoaded.extrusions[1], 1, pendingLoaded.extrusions).history, "ripristino prima della registrazione");

    // Il formato comprende anche Generated e dichiarazioni vuote esplicite.
    DocumentState kindsState = pending;
    kindsState.extrusions[1].forgeBody = std::make_shared<const Body>(*kindsState.extrusions[0].forgeBody);
    const auto &source = *kindsState.extrusions[0].forgeBody;
    const auto f = source.faces().front(); const auto e = source.edges().front(); const auto v = source.vertices().front();
    auto declarations = makeHistory({&source}, *kindsState.extrusions[1].forgeBody);
    declarations.declareOrigins(EntityKind::Face, f.index, {});
    declarations.declareOrigins(EntityKind::Edge, e.index, {{0, EntityKind::Face, f.index, HistoryRelation::Generated}});
    declarations.declareOrigins(EntityKind::Vertex, v.index, {{0, EntityKind::Vertex, v.index, HistoryRelation::Modified}});
    registerDeclaredHistory(kindsState.extrusions[1].forgeBody, {kindsState.extrusions[0].forgeBody}, std::move(declarations));
    const QString kindsPath = directory.filePath(QStringLiteral("tutti-tipi.prt"));
    require(saveDocumentFile(kindsPath, kindsState, true).isEmpty(), "salvataggio tipi e dichiarazione vuota");
    DocumentState kindsLoaded;
    require(loadDocumentFile(kindsPath, kindsLoaded).isEmpty(), "lettura tipi e dichiarazione vuota");
    const auto kinds = declaredHistorySnapshot(kindsLoaded.extrusions[1], 1, kindsLoaded.extrusions).history;
    require(kinds && kinds->isDeclared(EntityKind::Face, f.index) && kinds->origins(EntityKind::Face, f.index).empty()
        && kinds->isDeclared(EntityKind::Edge, e.index) && kinds->origins(EntityKind::Edge, e.index).size() == 1
        && kinds->origins(EntityKind::Edge, e.index).front().relation == HistoryRelation::Generated
        && kinds->isDeclared(EntityKind::Vertex, v.index), "copertura e Generated attraversano il file");

    // Fixture derivate dal vero salvataggio, con la definizione integra.
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "lettura dei byte della cache");
    const QByteArray bytes = file.readAll();
    QDataStream header(bytes); header.skipRawData(4);
    quint16 fileVersion = 0; quint8 compression = 0; quint32 payloadSize = 0;
    header >> fileVersion >> compression >> payloadSize;
    require(fileVersion == kDocumentFormatVersion && compression == 1, "definizione parametrica nel formato corrente con identita' del documento");
    const qsizetype cacheOffset = 11 + qsizetype(payloadSize);
    const QByteArray prefix = bytes.left(cacheOffset), cacheData = qUncompress(bytes.mid(cacheOffset));
    QDataStream cacheIn(cacheData); cacheIn.setVersion(QDataStream::Qt_6_0);
    QByteArray sourceHash, payloadHash; quint32 tag = 0, version = 0, count = 0;
    cacheIn >> sourceHash >> payloadHash >> tag >> version >> count;
    require(version == 7 && count == 4 && payloadHash == QCryptographicHash::hash(qUncompress(bytes.mid(11, payloadSize)), QCryptographicHash::Sha256),
        "cache 7 legata alla stessa definizione");
    struct Record { bool has = false; QString error; QByteArray body, history; };
    std::vector<Record> records(count);
    SnapshotChunkReader chunks(cacheIn);
    for (auto &record : records) {
        quint8 has = 0, display = 0;
        cacheIn >> has; record.has = has != 0;
        if (record.has) { cacheIn >> record.error; record.body = chunks.read(); }
        cacheIn >> display;
        if (display) {
            qint32 quality = -1; BodyDisplay discarded;
            cacheIn.setFloatingPointPrecision(QDataStream::SinglePrecision);
            cacheIn >> quality >> discarded.vertices >> discarded.normals >> discarded.edges >> discarded.edgeIds
                >> discarded.constructionCurves >> discarded.faceEdges >> discarded.faceIds >> discarded.faceLabelPoints >> discarded.triangleFaces;
            cacheIn.setFloatingPointPrecision(QDataStream::DoublePrecision);
        }
        record.history = chunks.read();
    }
    require(cacheIn.status() == QDataStream::Ok && cacheIn.atEnd() && !records[1].history.isEmpty(), "fixture con genealogia reale");
    const auto fixture = [&](const QString &name, quint32 cacheVersion, const std::vector<Record> &entries) {
        QByteArray encoded;
        QDataStream out(&encoded, QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_6_0);
        out << sourceHash << payloadHash;
        if (cacheVersion == 1) out << quint32(entries.size());
        else out << tag << cacheVersion << quint32(entries.size());
        SnapshotChunkWriter writer(out);
        for (const auto &record : entries) {
            out << quint8(record.has);
            if (record.has) {
                out << record.error;
                if (cacheVersion >= 5) writer.write(record.body); else out << record.body;
            }
            if (cacheVersion >= 2) out << quint8(0);
            if (cacheVersion >= 6) writer.write(record.history);
        }
        const QString target = directory.filePath(name + QStringLiteral(".prt"));
        QFile output(target); const QByteArray data = prefix + qCompress(encoded, 6);
        require(out.status() == QDataStream::Ok && output.open(QIODevice::WriteOnly) && output.write(data) == data.size(), "scrittura fixture cache");
        output.close(); return target;
    };
    for (quint32 old : {1u, 4u, 5u}) {
        DocumentState legacy;
        require(loadDocumentFile(fixture(QStringLiteral("cache-%1").arg(old), old, records), legacy).isEmpty(), "lettura cache precedente");
        for (int i = 0; i < legacy.extrusions.size(); ++i)
            require(legacy.extrusions[i].cachedGeometry && legacy.extrusions[i].forgeBody
                && writeBodyBinary(*legacy.extrusions[i].forgeBody) == geometry[std::size_t(i)], "cache precedente conserva il B-rep esatto");
        require(!declaredHistorySnapshot(legacy.extrusions[1], 1, legacy.extrusions).history, "cache vecchia non inventa dichiarazioni");
    }
    DocumentState legacy6;
    require(loadDocumentFile(fixture(QStringLiteral("cache-6"), 6, records), legacy6).isEmpty()
        && legacy6.extrusions[1].cachedGeometry && declaredHistorySnapshot(legacy6.extrusions[1], 1, legacy6.extrusions).history,
        "cache 6 con dichiarazioni ancora leggibile");
    QDataStream metadata(records[1].history);
    quint32 schema = 0, operands = 0; metadata >> schema >> operands;
    require(schema == 1 && operands == 1, "metadati della prima feature offset");
    metadata.skipRawData(12);
    qint64 entityOffset = -1;
    for (int kind = 0; kind < 3; ++kind) {
        quint32 entities = 0; metadata >> entities;
        if (entities) { entityOffset = metadata.device()->pos(); break; }
    }
    require(entityOffset >= 0, "posizione della prima entita' dichiarata");
    for (int corruption = 0; corruption < 10; ++corruption) {
        auto damaged = records; QByteArray &data = damaged[1].history;
        QDataStream edit(&data, QIODevice::ReadWrite);
        if (corruption == 0) { edit.device()->seek(12); edit << quint64(999999); } // ID operando
        if (corruption == 1) { edit.device()->seek(8); edit << qint32(1); } // operando auto-referenziale
        if (corruption == 2) { edit.device()->seek(entityOffset); edit << qint32(-1); }
        if (corruption == 3) { edit.device()->seek(entityOffset + 13); edit << qint32(999999); } // origine non viva
        if (corruption == 4) { edit.device()->seek(entityOffset + 17); edit << quint8(255); } // relazione
        if (corruption == 5) data.chop(1);
        if (corruption == 6) { edit.device()->seek(entityOffset - 4); edit << quint32(0xffffffffu); }
        if (corruption == 7) edit << quint32(99); // schema ignoto
        if (corruption == 8) data.append('x');
        if (corruption == 9) { edit.device()->seek(entityOffset + 8); edit << qint32(99); } // slot operando
        DocumentState rejected;
        require(loadDocumentFile(fixture(QStringLiteral("cache-incoerente-%1").arg(corruption), 6, damaged), rejected).isEmpty(),
            "metadati incoerenti lasciano leggibile la definizione");
        for (const auto &body : rejected.extrusions) require(!body.cachedGeometry && !body.forgeBody, "cache incoerente rifiutata atomicamente");
        if (corruption == 0) { CadViewport regenerated; regenerated.loadDocument(rejected); verify(regenerated.currentDocument()); }
    }
    DocumentState future;
    require(loadDocumentFile(fixture(QStringLiteral("cache-futura"), 8, records), future).isEmpty(), "cache futura lascia leggibile la definizione");
    for (const auto &body : future.extrusions) require(!body.cachedGeometry, "cache futura ignorata");
    const QString noCache = directory.filePath(QStringLiteral("senza-cache.prt"));
    require(saveDocumentFile(noCache, original, false).isEmpty(), "salvataggio con sola definizione");
    DocumentState definitionOnly;
    require(loadDocumentFile(noCache, definitionOnly).isEmpty(), "apertura con sola definizione");
    CadViewport regenerated; regenerated.loadDocument(definitionOnly); verify(regenerated.currentDocument());
}

#endif
