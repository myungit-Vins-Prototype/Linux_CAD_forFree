static void buildDiagnosticFlow() {
    using namespace ForgeCad;
    CadViewport view;
    SketchObject sketch;
    sketch.name = QStringLiteral("Profilo");
    sketch.segments = {{{2, 3}, {7, 3}}, {{7, 3}, {7, 8}}, {{7, 8}, {2, 8}}, {{2, 8}, {2, 3}}};
    view.sketches_.append(sketch);
    ExtrusionObject sheet;
    sheet.feature = BodyFeature::PlanarSurface;
    sheet.sketchIndex = 0;
    sheet.name = QStringLiteral("Superficie di prova");
    require(view.createBody(sheet).isEmpty(), "crea una lamina aperta per il test");
    ExtrusionObject sew;
    sew.feature = BodyFeature::Sew;
    sew.name = QStringLiteral("Cucitura diagnostica");
    sew.firstBody = 0;
    sew.sewSolid = true;
    const auto checkFourEdges = [&] {
        require(!view.buildDiagnosticError_.isEmpty() && view.buildDiagnostic_.paths.size() == 4,
                "la diagnostica contiene i quattro bordi aperti effettivi");
        for (const auto &path : view.buildDiagnostic_.paths)
            require(path.size() >= 2 && path.first() != path.last(), "ogni bordo aperto ha un percorso nella vista");
    };
    view.requestPreview(sew);
    QElapsedTimer wait;
    wait.start();
    while (view.previewError().isEmpty() && wait.elapsed() < 10000) QApplication::processEvents();
    require(!view.previewError().isEmpty(), "errore restituito dal worker di anteprima");
    checkFourEdges();
    require(!view.createBody(sew).isEmpty() && view.extrusions().size() == 1,
            "la conferma fallisce senza aggiungere una funzione");
    checkFourEdges();
    view.clearPreview();
    require(view.buildDiagnosticError_.isEmpty() && view.buildDiagnostic_.paths.isEmpty(), "annulla le evidenziazioni");
    require(!view.createBody(sew).isEmpty(), "errore di costruzione anche senza anteprima");
    checkFourEdges();
    view.extrusions_.append(sew);
    ExtrusionObject independent;
    independent.feature = BodyFeature::Primitive;
    independent.name = QStringLiteral("Primitiva indipendente");
    view.extrusions_.append(independent);
    view.regenerateAll();
    view.documentChanged();
    require(!view.extrusions_[1].error.isEmpty() && view.extrusions_[2].error.isEmpty(), "errore a monte seguito da una funzione valida");
    checkFourEdges();
    view.selectObject(SceneObjectKind::Extrusion, 0);
    require(view.buildDiagnosticError_.isEmpty(), "la selezione valida pulisce la segnalazione storica");
    view.selectObject(SceneObjectKind::Extrusion, 1);
    checkFourEdges();
    view.clearPreview();
    auto fixed = sew;
    fixed.sewSolid = false;
    view.requestPreview(fixed);
    wait.restart();
    while (!view.preview_.valid && wait.elapsed() < 10000) QApplication::processEvents();
    require(view.preview_.valid && view.buildDiagnosticError_.isEmpty(), "la correzione non lascia bordi in errore");

    auto generic = featureDiagnosticContext(sew, view.sketches_, view.extrusions_);
    require(generic.context.contains(sheet.name) && !generic.paths.isEmpty(), "ripiego circoscritto all'ingresso nominato");
    auto unresolved = sew;
    TopologyReferenceResolution missed;
    missed.reference = {2, 3, 4, 6, 1};
    unresolved.referenceResolutions.append(missed);
    auto savedPick = featureDiagnosticContext(unresolved, {}, {});
    require(savedPick.paths == QVector<QVector<QVector3D>>{{QVector3D(2, 3, 4)}}, "ultima posizione del riferimento perduto");
    auto used = missed;
    used.sourceIndex = 0;
    used.method = TopologyResolutionMethod::Identity;
    used.kind = 4;
    used.resolvedSubshape = view.extrusions_.at(0).forgeBody->edges().front().index;
    unresolved.referenceResolutions = {used};
    const auto input = featureDiagnosticContext(unresolved, view.sketches_, view.extrusions_);
    require(input.paths.size() == 1 && input.paths.front().size() >= 2 && input.context.contains("Riferimenti"),
            "spigolo scelto mostrato come ingresso quando manca una causa puntuale");
    view.clearPreview();
    view.sketches_[0].supportError = QStringLiteral("Appoggio perso");
    view.selectObject(SceneObjectKind::Sketch, 0);
    require(view.buildDiagnosticError_ == view.sketches_[0].supportError, "selezione dello schizzo con appoggio perso");
    auto invalid = sketch;
    invalid.customFrame = true;
    for (int axis = 0; axis < 3; ++axis) invalid.frame.xAxis[axis] = invalid.frame.normal[axis];
    require(sketchReferenceDiagnostic(invalid).paths.isEmpty(), "piano degenere senza coordinate fittizie");
    QMainWindow window;
    CadViewport edited(&window);
    PrimitiveParameters box;
    box.size[0] = box.size[1] = box.size[2] = 3;
    require(edited.createPrimitive(box, QStringLiteral("Base")).isEmpty(), "crea la prima scatola");
    box.origin[0] = 1;
    require(edited.createPrimitive(box, QStringLiteral("Utensile")).isEmpty(), "crea la seconda scatola sovrapposta");
    require(edited.createBoolean(BooleanOperation::Intersection, 0, {1}, QStringLiteral("Intersezione")).isEmpty(),
            "intersezione valida prima della modifica");
    {
        QDialog panel(&window);
        auto *form = new QFormLayout(&panel);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &panel);
        form->addRow(buttons);
        PreviewScope preview(&edited, panel, form, 0);
        ExtrusionObject moved = edited.extrusions().at(0);
        moved.primitive.origin[0] = 10;
        preview.request(moved);
        QTimer::singleShot(0, &panel, [buttons] { buttons->button(QDialogButtonBox::Ok)->click(); });
        QTimer::singleShot(10000, &panel, &QDialog::reject);
        require(runUntilAppliedModeless(&window, &edited, panel, form, buttons,
                    [&] { return edited.updateBody(0, moved); }),
                "il pannello accetta la modifica valida dell'operando");
        require(!edited.extrusions().at(2).error.isEmpty()
                && edited.buildDiagnosticError_ == edited.extrusions().at(2).error,
                "il ricalcolo dopo la modifica segnala l'intersezione divenuta vuota");
    }
    require(edited.preview_.key.isEmpty() && edited.buildDiagnosticError_ == edited.extrusions().at(2).error,
            "la chiusura effettiva rimuove l'anteprima e conserva l'errore a valle");
    edited.clearPreview();
    require(edited.buildDiagnosticError_.isEmpty(), "la pulizia esplicita azzera anche l'errore storico");
    std::cout << "PASS build diagnostic flow\n";
}
