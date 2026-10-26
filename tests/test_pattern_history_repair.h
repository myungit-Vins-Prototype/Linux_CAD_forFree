// Compatibilita' delle ripetizioni storiche e riparazione dei riferimenti in modifica.
static void legacyPatternHistory() {
    using namespace ForgeCad;
    DocumentState state;
    ExtrusionObject source;
    source.feature = BodyFeature::Primitive;
    source.primitive.kind = PrimitiveKind::Box;
    source.primitive.origin[0] = 3;
    source.primitive.size[0] = source.primitive.size[1] = source.primitive.size[2] = 2;
    ExtrusionObject mirror;
    mirror.feature = BodyFeature::Pattern;
    mirror.name = QStringLiteral("Specchio storico");
    mirror.firstBody = 0;
    mirror.pattern.kind = 2;
    GeometryRef plane; plane.kind = 1; plane.index = 2;
    mirror.pattern.refs = {plane};
    ExtrusionObject next;
    next.feature = BodyFeature::Scale;
    next.firstBody = 1;
    next.scaleFactor = 1.1;
    state.extrusions = {source, mirror, next};
    CadViewport view; view.loadDocument(state);
    const quint64 id = view.extrusions_[1].featureId;
    const auto check = [&](int shells) {
        require(view.extrusions_.size() == 3, "rigenerare non inserisce risultati nei vecchi specchi");
        require(view.extrusions_[1].patternComponent == -1 && view.extrusions_[1].featureId == id,
                "semantica e identita' dello specchio storico conservate");
        for (int i : {1, 2})
            require(view.extrusions_[i].error.isEmpty() && view.extrusions_[i].forgeBody
                    && int(view.extrusions_[i].forgeBody->shells().size()) == shells,
                    "il consumatore mantiene tutte le componenti dello specchio storico");
    };
    check(2);
    require(view.rebuildAllFeatures() == 0, "rigenerazione dello specchio storico con consumatori");
    check(2);
    auto changed = view.extrusions_[1];
    changed.pattern.kind = 0; changed.pattern.count = 3; changed.pattern.spacing = 5;
    GeometryRef axis; axis.kind = 2; axis.index = 0; changed.pattern.refs = {axis};
    require(view.updateBody(1, changed).isEmpty(), "modifica storica mantiene il contratto del risultato");
    check(3); view.undo(); check(2); view.redo(); check(3);
    QByteArray bytes; require(encodeDocument(view.currentDocument(), bytes).isEmpty(), "codec dello specchio storico");
    DocumentState decoded; require(decodeDocument(bytes, decoded).isEmpty(), "rilettura dello specchio storico");
    view.loadDocument(decoded); require(view.rebuildAllFeatures() == 0, "rigenera dopo rilettura"); check(3);
    std::cout << "PASS compatibilita' ripetizioni storiche" << std::endl;
}

static void failedFeatureRepair() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    DocumentState state;
    ExtrusionObject source; source.feature = BodyFeature::Primitive; source.primitive.kind = PrimitiveKind::Box;
    source.primitive.size[0] = source.primitive.size[1] = source.primitive.size[2] = 2;
    ExtrusionObject broken; broken.feature = BodyFeature::PlanarSurface; broken.sketchIndex = -1;
    ExtrusionObject later; later.feature = BodyFeature::Scale; later.firstBody = 0; later.scaleFactor = 1.2;
    state.extrusions = {source, broken, later};
    CadViewport view; view.loadDocument(state);
    require(!view.extrusions_[1].error.isEmpty() && !view.isBodyDrawn(0) && view.isBodyDrawn(2),
            "fixture: feature fallita, base nascosta da una feature successiva indipendente");
    const auto original = view.documentState();
    const quint64 feature = view.extrusions_[1].featureId;
    require(view.beginReferencePick(DatumRoleCurve, 1).isEmpty(), "avvia scelta per riparare");
    require(view.isBodyDrawn(0) && view.referenceBodyEligible(0) && !view.isBodyDrawn(2)
            && !view.referenceBodyEligible(2), "scelta usa i corpi prima della feature, anche senza anteprima valida");
    view.clearPreview();
    require(view.referenceBodyEligible(0), "azzerare i riferimenti non perde il contesto di scelta");
    view.cancelReferencePick();
    require(!view.isBodyDrawn(0) && view.isBodyDrawn(2), "Esc ripristina la visibilita' finale");
    for (int i = 0; i < original.extrusions.size(); ++i)
        require(view.extrusions_[i].visible == original.extrusions[i].visible
                && view.extrusions_[i].forgeBody == original.extrusions[i].forgeBody,
                "la scelta non modifica geometria o visibilita' salvata");
    QVector<GeometryRef> edges;
    const auto base = view.extrusions_[0].forgeBody;
    for (auto edge : base->edges()) {
        const auto &e = base->edge(edge);
        const Vec3 a = e.curve->point(e.range.lo), b = e.curve->point(e.range.hi);
        if (std::abs(a[2] - 2) > 1e-9 || std::abs(b[2] - 2) > 1e-9) continue;
        GeometryRef ref; ref.kind = 4; ref.index = 0; ref.featureId = view.extrusions_[0].featureId;
        ref.point = edgeReference(*base, edge, e.curve->point((e.range.lo + e.range.hi) / 2)); edges.append(ref);
    }
    require(edges.size() == 4, "quattro spigoli complanari per riparare");
    QMainWindow window;
    std::exception_ptr failure;
    QElapsedTimer watchdog; watchdog.start();
    int phase = 0;
    QTimer timer; timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>();
        if (!dialog || !dialog->isVisible()) return;
        try {
            require(watchdog.elapsed() < 5000, "timeout riparazione nel pannello");
            if (phase == 0) {
                QPushButton *clear = nullptr, *pick = nullptr;
                for (auto *button : dialog->findChildren<QPushButton *>()) {
                    if (button->text() == QStringLiteral("Azzera")) clear = button;
                    if (button->text() == QStringLiteral("Scegli i bordi nella vista")) pick = button;
                }
                require(clear && pick, "comandi di riparazione presenti");
                clear->click(); pick->click();
                require(view.referenceBodyEligible(0) && bool(view.refPickFinished_), "base selezionabile dal pannello fallito");
                for (const auto &ref : edges) view.refPickFinished_(true, ref);
                phase = 1;
            } else if (view.preview_.valid && !view.previewRunning_) {
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                require(dialog->result() == QDialog::Accepted, "la feature si ripara senza cancellarla");
                timer.stop();
            } else require(view.preview_.error.isEmpty(), qPrintable(view.preview_.error));
        } catch (...) { failure = std::current_exception(); timer.stop(); dialog->reject(); }
    });
    timer.start();
    planarDialog(&window, &view, QStringLiteral("Ripara superficie"), 1, view.extrusions_[1],
                 [&](const ExtrusionObject &d) { return view.updateBody(1, d); });
    timer.stop(); if (failure) std::rethrow_exception(failure);
    require(view.extrusions_.size() == 3 && view.extrusions_[1].featureId == feature
            && view.extrusions_[1].error.isEmpty() && view.extrusions_[1].forgeBody,
            "riparazione mantiene la feature e la storia");
    view.undo(); require(!view.extrusions_[1].error.isEmpty(), "Undo ripristina la feature fallita");
    view.redo(); require(view.extrusions_[1].error.isEmpty() && view.extrusions_[1].featureId == feature, "Redo della riparazione");
    std::cout << "PASS riparazione dal pannello e scelta storica" << std::endl;
}

// Un evento nella storia, piu' corpi indipendenti nella scena e nel codec.
static void patternSingleStory() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    for (bool sheet : {false, true}) {
        DocumentState initial;
        ExtrusionObject source; source.name = QStringLiteral("Base");
        source.feature = BodyFeature::Primitive; source.primitive.kind = PrimitiveKind::Box;
        source.primitive.origin[0] = 3;
        source.primitive.size[0] = source.primitive.size[1] = source.primitive.size[2] = 2;
        if (sheet) {
            source.feature = BodyFeature::Imported;
            ExchangeBody exchange; exchange.body = makePlaneSheet(Frame3({4,0,0},{0,0,1},{1,0,0}), 1);
            source.importData = QByteArray::fromStdString(writeStep({exchange}));
        }
        initial.extrusions = {source};
        CadViewport view; view.loadDocument(initial);
        ExtrusionObject mirror; mirror.feature = BodyFeature::Pattern; mirror.name = QStringLiteral("Specchio unico");
        mirror.firstBody = 0; mirror.pattern.kind = 2;
        GeometryRef plane; plane.kind = 1; plane.index = 2; mirror.pattern.refs = {plane};
        require(view.createBody(mirror, {0}).isEmpty(), "crea specchio separato");
        const auto check = [&] {
            const auto steps = view.storyboard();
            require(steps.size() == 2 && !steps[1].sketch && steps[1].index == 1,
                    "una sola feature specchio nella storyboard");
            require(view.storyboardIndex(false, 2) == 1, "corpo derivato appartiene allo stesso evento");
            require(view.resultBodiesBefore(-1).size() == 2, "due risultati indipendenti dello specchio");
            require(view.extrusions_[1].modelBodyId != view.extrusions_[2].modelBodyId,
                    "identita' dei corpi separate");
            require(storySketchDependencyError(view.sketches_, view.extrusions_, steps).isEmpty(),
                    "la sequenza logica include le dipendenze dei risultati");
        };
        check();
        view.setStoryboardPosition(1);
        require(view.historyPosition() == 1 && view.resultBodiesBefore(-1) == QVector<int>{0},
                "prima dello specchio resta solo la base");
        view.setStoryboardPosition(2); check();
        require(view.historyPosition() == 3 && view.isBodyDrawn(1) && view.isBodyDrawn(2),
                "dopo lo specchio entrambi i corpi esistono nello stesso passo");
        require(view.setFeatureSuppressed(2, true).isEmpty() && view.resultBodiesBefore(-1) == QVector<int>{0},
                "soppressione dal secondo corpo riguarda tutta la funzione");
        view.undo(); check(); view.redo();
        require(view.resultBodiesBefore(-1) == QVector<int>{0}, "Redo della soppressione di gruppo");
        require(view.setFeatureSuppressed(2, false).isEmpty(), "riattiva dal secondo corpo"); check();
        QByteArray bytes; require(encodeDocument(view.currentDocument(), bytes).isEmpty(), "salva evento e corpi");
        DocumentState decoded; require(decodeDocument(bytes, decoded).isEmpty(), "rilegge evento e corpi");
        view.loadDocument(decoded); require(view.rebuildAllFeatures() == 0, "rigenera evento e corpi"); check();
        {
            PdfWindow window; window.viewport_->loadDocument(view.currentDocument()); window.rebuildModelTree();
            QTreeWidgetItem *bodies = nullptr, *history = nullptr;
            for (int i = 0; i < window.modelTree_->topLevelItemCount(); ++i) {
                auto *item = window.modelTree_->topLevelItem(i);
                if (item->text(0) == QStringLiteral("Corpi")) bodies = item;
                if (item->text(0) == QStringLiteral("Storia delle feature")) history = item;
            }
            require(bodies && bodies->childCount() == 2 && history, "albero reale: due voci soltanto in Corpi");
            int mirrors = 0;
            for (int i = 0; i < history->childCount(); ++i)
                if (history->child(i)->text(0).startsWith(QStringLiteral("Specchio unico"))) ++mirrors;
            require(mirrors == 1, "albero reale: una sola voce dello specchio nella storia");
        }
        view.deleteFeature(2);
        require(view.extrusions_.size() == 1 && view.storyboard().size() == 1,
                "eliminare dal risultato rimuove una funzione e tutti i suoi risultati");
        view.undo(); check(); view.redo(); require(view.extrusions_.size() == 1, "Redo elimina il gruppo");
        view.undo(); check();
        ExtrusionObject scale; scale.feature = BodyFeature::Scale; scale.firstBody = 2; scale.scaleFactor = 1.1;
        scale.name = QStringLiteral("Scala del secondo corpo");
        require(view.createBody(scale, {2}).isEmpty(), "consumatore del secondo risultato");
        const quint64 consumer = view.extrusions_.last().featureId;
        const quint64 second = view.extrusions_[2].featureId;
        auto changed = view.extrusions_[1]; changed.pattern.kind = 0; changed.pattern.count = 3; changed.pattern.spacing = 7;
        GeometryRef axis; axis.kind = 2; axis.index = 0; changed.pattern.refs = {axis};
        view.setStoryboardPosition(2);
        require(view.updateBody(1, changed).isEmpty(), "aumenta copie durante rollback con consumatore futuro");
        require(view.storyboard().size() == 3 && view.historyPosition() == 4,
                "nuovi risultati precedono il consumatore senza aggiungere eventi");
        require(view.extrusions_[4].featureId == consumer && view.extrusions_[4].firstBody == 2
                && view.extrusions_[2].featureId == second, "riordino risultati conserva i riferimenti del consumatore");
        require(view.resultBodiesBefore(-1).size() == 3, "tutte le copie disponibili prima del consumatore");
        view.setStoryboardPosition(3);
        require(view.rebuildAllFeatures() == 0 && view.resultBodiesBefore(-1).size() == 3,
                "consumatore rimane valido dopo aggiunta componenti");
        view.undo(); // Undo della rigenerazione
        view.undo(); // Undo della modifica con riordino
        require(view.extrusions_.size() == 4 && view.extrusions_.last().featureId == consumer,
                "Undo ripristina ordine e identita'");
        view.redo(); require(view.extrusions_.size() == 5, "Redo ripristina le componenti aggiunte");
    }
    // Riordino e inserimento coinvolgono il gruppo, anche fra corpi diversi.
    DocumentState initial; ExtrusionObject base; base.feature = BodyFeature::Primitive;
    base.primitive.kind = PrimitiveKind::Box; base.primitive.origin[0] = 3;
    initial.extrusions = {base}; CadViewport view; view.loadDocument(initial);
    ExtrusionObject mirror; mirror.feature = BodyFeature::Pattern; mirror.firstBody = 0; mirror.pattern.kind = 2;
    GeometryRef plane; plane.kind = 1; plane.index = 2; mirror.pattern.refs = {plane};
    require(view.createBody(mirror, {0}).isEmpty(), "specchio per riordino");
    ExtrusionObject other = base; other.primitive.origin[1] = 10;
    require(view.createBody(other, {}).isEmpty(), "altro corpo indipendente");
    const quint64 root = view.extrusions_[1].featureId, child = view.extrusions_[2].featureId;
    require(view.moveFeatureTo(1, 3).isEmpty(), "sposta lo specchio oltre un corpo indipendente");
    require(view.extrusions_[2].featureId == root && view.extrusions_[3].featureId == child
            && view.extrusions_[3].firstBody == 2 && view.rebuildAllFeatures() == 0,
            "il gruppo si sposta intero con riferimenti corretti");
    require(view.moveFeatureTo(3, 1).isEmpty() && view.extrusions_[1].featureId == root,
            "riordino dal corpo derivato usa la funzione madre");
    view.deleteFeature(1);
    require(view.extrusions_.size() == 2, "rimuove il gruppo lasciando i corpi indipendenti");
    view.setStoryboardPosition(1);
    require(view.createBody(mirror, {0}).isEmpty(), "inserisce specchio al cursore prima di un corpo futuro");
    require(view.storyboard().size() == 3 && view.historyPosition() == 3 && view.extrusions_[2].firstBody == 1,
            "inserimento atomico dello specchio e dei risultati");
    view.setStoryboardPosition(3); require(view.rebuildAllFeatures() == 0, "storia completa dopo inserimento");
    // Documento vecchio con risultato in coda: definizioni e cache si muovono insieme.
    auto unordered = view.currentDocument();
    std::swap(unordered.extrusions[2], unordered.extrusions[3]);
    unordered.extrusions[3].firstBody = 1;
    const auto resultBody = unordered.extrusions[3].forgeBody;
    view.loadPreviewDocument(unordered);
    require(view.extrusions_[2].forgeBody == resultBody && view.extrusions_[2].firstBody == 1
            && view.storyboard().size() == 3, "lettura preview normalizza l'ordine mantenendo le cache associate");
    std::cout << "PASS specchio: una feature, corpi separati, cursore, GUI, modifica, riordino, codec e Undo/Redo" << std::endl;
}
