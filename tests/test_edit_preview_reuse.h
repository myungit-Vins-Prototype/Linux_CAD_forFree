// Riapertura: niente timer/worker se geometria e mesh sono gia' nella scena.
static void editPreviewReuse(const QString &path = {}, bool gl = false) {
    using namespace ForgeCad;
    const auto settle = [&](CadViewport &view) {
        QElapsedTimer timeout; timeout.start();
        while ((view.previewRunning_ || view.previewPending()) && timeout.elapsed() < 15000)
            QApplication::processEvents();
        require(view.preview_.valid && !view.previewRunning_, qPrintable("anteprima: " + view.preview_.error));
    };
    const auto exercise = [&](CadViewport &view) {
        const auto before = view.documentState();
        int started = 0;
        view.setWorkCallback([&](bool active, const QString &, bool) { if (active) ++started; });
        for (int i = 0; i < view.extrusions_.size(); ++i) {
            const auto existing = view.extrusions_.at(i);
            if (existing.suppressed || !existing.error.isEmpty() || !hasGeometry(existing)) continue;
            require(view.unchangedFeature(existing, i), "ogni tipo di funzione riconosce il risultato esistente");
            if (isDatumBody(existing)) {
                SketchFrame frame;
                QString error;
                require(view.previewDatum(existing.datum, i, frame, &error) && error.isEmpty(), "piano esistente riusato");
                for (int k = 0; k < 3; ++k)
                    require(frame.origin[k] == existing.datumFrame.origin[k] && frame.normal[k] == existing.datumFrame.normal[k],
                            "piano invariato conserva esattamente il suo riferimento");
                continue;
            }
            auto definition = existing;
            if (definition.feature == BodyFeature::Pattern && definition.patternComponent == 0)
                definition.patternComponent = -1;
            for (int repeat = 0; repeat < 3; ++repeat) {
                view.clearPreview();
                view.requestPreview(definition, i);
                require(view.preview_.valid && view.preview_.sceneResult && !view.previewRunning_
                        && (!view.previewTimer_ || !view.previewTimer_->isActive()),
                        qPrintable("riapertura immediata: " + existing.name));
                require(view.preview_.geometry == existing.forgeBody, "identita' del B-rep conservata");
                for (int operand : bodyOperands(existing))
                    if (operand >= 0 && operand < i && existing.modelBodyId
                        && view.extrusions_.at(operand).modelBodyId == existing.modelBodyId)
                        require(!view.isBodyVisibleInScene(operand), "risultato completo non sovrappone gli stadi dello stesso corpo");
                require(view.preview_.display.vertices.constData() == existing.display.vertices.constData()
                        && view.preview_.display.edges.constData() == existing.display.edges.constData(),
                        "buffer di scena condivisi senza ritassellazione o estrazione patch");
                if (definition.feature == BodyFeature::Pattern && definition.patternComponent < 0 && existing.patternComponent == 0) {
                    int expected = 0;
                    for (int k = i + 1; k < view.extrusions_.size(); ++k) {
                        const auto &part = view.extrusions_.at(k);
                        if (part.feature == BodyFeature::Pattern && part.patternComponent > 0 && part.firstBody == i && !part.suppressed) {
                            require(expected < view.preview_.additionalDisplays.size(), "ogni istanza presente nell'anteprima");
                            require(view.preview_.additionalDisplays.at(expected).vertices.constData() == part.display.vertices.constData(),
                                    "istanza disegnata dal buffer esistente");
                            ++expected;
                        }
                    }
                    require(expected == view.preview_.additionalDisplays.size(), "nessuna istanza duplicata o perduta");
                }
                // Anche una chiamata gia' accodata deve lasciare pronta la scena.
                view.startPreviewJob();
                require(!view.previewRunning_ && started == 0, "nessun lavoro avviato per un risultato pronto");
            }
            std::cout << "REUSE " << i << " " << existing.name.toStdString() << " parts="
                      << view.preview_.additionalDisplays.size() + 1 << '\n';
        }
        view.clearPreview(); view.setWorkCallback({});
        for (int i = 0; i < view.extrusions_.size(); ++i)
            require(view.extrusions_.at(i).forgeBody == before.extrusions.at(i).forgeBody
                    && view.extrusions_.at(i).curve == before.extrusions.at(i).curve,
                    "riaperture e annullamento non cambiano il documento");
    };
    DocumentState document;
    if (!path.isEmpty()) {
        require(loadDocumentFile(path, document).isEmpty(), "lettura copia documento reale");
    } else {
        SketchObject sketch; sketch.name = "Profilo";
        sketch.segments = {{{2,0},{4,0}},{{4,0},{4,2}},{{4,2},{2,2}},{{2,2},{2,0}}};
        document.sketches = {sketch}; auto xz = sketch; xz.plane = 1; document.sketches.append(xz);
        ExtrusionObject box; box.feature = BodyFeature::Primitive; box.name = "Base";
        box.primitive.size[0] = box.primitive.size[1] = box.primitive.size[2] = 10;
        ExtrusionObject blend; blend.feature = BodyFeature::Blend; blend.name = "Raccordo"; blend.firstBody = 0;
        blend.blendEdges = {{5,0,10}}; blend.blendSize = .5;
        ExtrusionObject extrusion; extrusion.name = "Estrusione"; extrusion.sketchIndex = 0; extrusion.distance = 5;
        ExtrusionObject revolution; revolution.name = "Rivoluzione"; revolution.feature = BodyFeature::Revolution;
        revolution.sketchIndex = 1; revolution.revolveAxis = -2; revolution.revolveAngle = 90;
        ExtrusionObject scale; scale.name = "Scala"; scale.feature = BodyFeature::Scale; scale.firstBody = 2; scale.scaleFactor = 1.1;
        ExtrusionObject planar; planar.name = "Superficie"; planar.feature = BodyFeature::PlanarSurface; planar.sketchIndex = 0;
        ExtrusionObject curve; curve.name = "Curva 3D"; curve.feature = BodyFeature::SpatialSketch;
        SpatialCurve3D line; line.id = 1; line.points = {{0,0,0},{10,0,3}}; curve.spatialSketch.entities = {line}; curve.spatialSketch.nextId = 2;
        ExtrusionObject datum; datum.name = "Piano"; datum.feature = BodyFeature::DatumPlane; datum.datum.refs = {{1,0,{},{}}}; datum.datum.distance = 3;
        document.extrusions = {box,blend,extrusion,revolution,scale,planar,curve,datum};
    }
    CadViewport view; view.tessellationQuality_ = 1; view.loadDocument(document);
    for (int i = 0; i < view.extrusions_.size(); ++i) {
        require(view.extrusions_.at(i).error.isEmpty(), qPrintable(view.extrusions_.at(i).name + ": " + view.extrusions_.at(i).error));
        if (!isDatumBody(view.extrusions_.at(i))) view.ensureBodyDisplay(i);
    }
    exercise(view);
    if(gl) {
        int blend=-1;
        for(int i=0;i<view.extrusions_.size();++i)if(view.extrusions_.at(i).feature==BodyFeature::Blend){blend=i;break;}
        if(blend>=0) {
            view.clearFeatureHighlights();const auto feature=view.extrusions_.at(blend);
            view.ensureBodyDisplay(feature.firstBody);view.requestPreview(feature,blend);
            require(view.preview_.sceneResult && !view.isBodyVisibleInScene(feature.firstBody),"base sostituita dal raccordo completo");
            view.resize(1200,900);view.setViewPreset(4);view.fitAll();view.show();QApplication::processEvents();
            require(view.isValid(),"contesto GL per raccordo riaperto");
            const auto clean=view.grabFramebuffer();const auto replaced=view.preview_.replaced;
            view.preview_.replaced.removeAll(feature.firstBody);const auto overlapped=view.grabFramebuffer();view.preview_.replaced=replaced;
            require(!clean.isNull() && clean!=overlapped,"regressione visiva rileva la sovrapposizione della base");
            const QString qa=qEnvironmentVariable("FORGECAD_EDIT_PREVIEW_QA");
            if(!qa.isEmpty()){QDir().mkpath(qa);require(clean.save(qa+"/raccordo-riaperto.png"),"cattura raccordo senza sovrapposizione");}
            view.hide();view.clearPreview();
        }
    }
    {
        PdfWindow owner;
        if(gl){owner.resize(1400,950);owner.show();QApplication::processEvents();}
        CadViewport *scene = nullptr;
        for (auto *widget : owner.findChildren<QWidget *>())
            if (auto *candidate = dynamic_cast<CadViewport *>(widget)) { scene = candidate; break; }
        require(scene && scene->editBodyCallback_, "percorso reale Modifica della finestra");
        scene->loadDocument(document);
        for (int i = 0; i < scene->extrusions_.size(); ++i)
            if (!isDatumBody(scene->extrusions_.at(i))) scene->ensureBodyDisplay(i);
        int started = 0;
        scene->setWorkCallback([&](bool active, const QString &, bool) { if (active) ++started; });
        const auto snapshot = scene->documentState();
        for (int i = 0; i < scene->extrusions_.size(); ++i) {
            const auto existing = scene->extrusions_.at(i);
            if (existing.patternComponent > 0 || existing.feature == BodyFeature::Imported) continue;
            bool inspected = false;
            std::exception_ptr failure;
            QTimer watchdog; watchdog.setSingleShot(true); watchdog.setInterval(10000);
            const auto close = [&] { for (auto *panel : owner.findChildren<QDialog *>()) panel->reject(); };
            QObject::connect(&watchdog, &QTimer::timeout, &owner, close); watchdog.start();
            QTimer::singleShot(30, &owner, [&] {
                try {
                    require(started == 0 && !scene->previewRunning_, qPrintable("nessun worker aprendo " + existing.name));
                    if (isDatumBody(existing)) require(scene->datumPreviewValid_, "piano pronto alla riapertura");
                    else {
                        require(scene->preview_.valid && scene->preview_.sceneResult,
                                qPrintable("risultato pronto nel pannello " + existing.name));
                        require(scene->preview_.geometry == existing.forgeBody, "pannello conserva il risultato originale");
                        if(existing.feature==BodyFeature::Blend) {
                            require(scene->edgePicking_ && scene->edgePickEligible(existing.firstBody),"base disponibile per scegliere i bordi nel pannello reale");
                            if(gl) {
                                const auto clean=scene->grabFramebuffer();const auto replaced=scene->preview_.replaced;
                                scene->preview_.replaced.removeAll(existing.firstBody);const auto overlap=scene->grabFramebuffer();scene->preview_.replaced=replaced;
                                require(!clean.isNull() && clean!=overlap,"il pannello raccordo non ridisegna la base selezionabile");
                                const QString qa=qEnvironmentVariable("FORGECAD_EDIT_PREVIEW_QA");
                                if(!qa.isEmpty()){QDir().mkpath(qa);require(clean.save(qa+"/pannello-raccordo.png"),"cattura pannello reale raccordo");}
                            }
                        }
                    }
                    inspected = true;
                } catch (...) { failure = std::current_exception(); }
                close();
            });
            scene->editBodyCallback_(i);
            if (failure) std::rethrow_exception(failure);
            require(inspected, "editor verificato prima della chiusura");
            std::cout << "PANEL " << existing.name.toStdString() << " no_worker\n";
        }
        scene->setWorkCallback({});
        for (int i = 0; i < scene->extrusions_.size(); ++i)
            require(scene->extrusions_.at(i).forgeBody == snapshot.extrusions.at(i).forgeBody
                    && scene->extrusions_.at(i).curve == snapshot.extrusions.at(i).curve, "Annulla lascia il documento identico");
    }
    if (path.isEmpty()) {
        // Corpo nuovo e funzione additiva, compresi risultati multipli/esclusioni.
        for (bool featureOnly : {false,true}) {
            auto state = document; state.extrusions.resize(3);
            if (featureOnly) { state.extrusions[2].mergeOperation = 1; state.extrusions[2].mergeBodies = {0}; }
            view.loadDocument(state);
            ExtrusionObject pattern; pattern.feature = BodyFeature::Pattern; pattern.name = "Ripetizione"; pattern.firstBody = 2;
            pattern.pattern.refs = {{2,0,{},{}}}; pattern.pattern.count = 4; pattern.pattern.spacing = 20.123456789012345;
            pattern.pattern.featureOnly = featureOnly; pattern.pattern.excluded = {2};
            require(view.createBody(pattern).isEmpty(), "ripetizione di prova");
            for (int i = 0; i < view.extrusions_.size(); ++i) view.ensureBodyDisplay(i);
            exercise(view);
        }
    }
    // Cambio reale: parte il worker; tornando ai valori originali si usa
    // subito la scena e il risultato superato non puo' rimpiazzarla.
    int target = -1;
    for (int i = 0; i < view.extrusions_.size(); ++i)
        if (view.extrusions_.at(i).feature == BodyFeature::Extrusion) { target = i; break; }
    if (target >= 0 && view.extrusions_.at(target).extent == 0) {
        const auto original = view.extrusions_.at(target);
        auto changed = original; changed.distance += 1;
        view.requestPreview(changed, target); require(view.previewPending(), "modifica effettiva richiede una nuova anteprima");
        view.startPreviewJob(); require(view.previewRunning_, "worker avviato solo dopo modifica");
        view.requestPreview(original, target);
        require(view.preview_.valid && view.preview_.geometry == original.forgeBody, "ritorno immediato al risultato originale");
        settle(view); require(view.preview_.geometry == original.forgeBody, "worker superato non sostituisce la scena riusata");
    }
    view.clearPreview();
    // Il pannello reale della ripetizione deve raggiungere lo stesso percorso.
    int patternIndex = -1;
    for (int i = 0; i < view.extrusions_.size(); ++i)
        if (view.extrusions_.at(i).feature == BodyFeature::Pattern && view.extrusions_.at(i).patternComponent <= 0) { patternIndex = i; break; }
    if (patternIndex >= 0) {
        QMainWindow owner; owner.resize(1200,900); view.resize(1100,800);
        if (gl) { view.show(); QApplication::processEvents(); view.setViewPreset(4); view.fitAll(); }
        int started = 0; bool inspected = false; std::exception_ptr failure;
        view.setWorkCallback([&](bool active, const QString &, bool) { if (active) ++started; });
        QTimer watchdog; watchdog.setSingleShot(true); watchdog.setInterval(10000);
        QObject::connect(&watchdog, &QTimer::timeout, &owner, [&] { for (auto *panel : owner.findChildren<QDialog *>()) panel->reject(); }); watchdog.start();
        QTimer::singleShot(0, &owner, [&] {
            auto panels = owner.findChildren<QDialog *>();
            try {
                require(!panels.isEmpty(), "pannello modifica aperto");
                require(view.preview_.valid && view.preview_.sceneResult && started == 0 && !view.previewRunning_, "pannello riusa la scena all'apertura");
                if (gl) {
                    const auto all = view.grabFramebuffer();
                    const auto parts = view.preview_.additionalDisplays; view.preview_.additionalDisplays.clear();
                    const auto first = view.grabFramebuffer(); view.preview_.additionalDisplays = parts;
                    require(!all.isNull() && (parts.isEmpty() || all != first), "le istanze esistenti sono visibili nel framebuffer");
                    const auto qa = qEnvironmentVariable("FORGECAD_EDIT_PREVIEW_QA");
                    if (!qa.isEmpty()) { QDir().mkpath(qa); require(all.save(qa + "/ripetizione-riaperta.png"), "cattura anteprima riusata"); }
                }
                inspected = true;
            } catch (...) { failure = std::current_exception(); }
            for (auto *panel : panels) panel->reject();
        });
        const auto existing = view.extrusions_.at(patternIndex);
        patternDialog(&owner, &view, "Modifica ripetizione", patternIndex, existing, [](const ExtrusionObject &) { return QString(); });
        view.setWorkCallback({});
        if (failure) std::rethrow_exception(failure);
        require(inspected && view.preview_.key.isEmpty(), "annullamento pulisce soltanto l'anteprima");
    }
    std::cout << "PASS riuso immediato anteprime in modifica\n";
}
