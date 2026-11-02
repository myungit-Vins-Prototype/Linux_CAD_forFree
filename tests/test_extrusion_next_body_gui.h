// Incluso nel friend ViewportInteractionTest.
static void extrusionNextBodyGuiRegressions(bool openGl) {
    using namespace ForgeCad;
    const auto profile = [] {
        SketchObject sketch;
        sketch.name = QStringLiteral("Profilo fra i corpi");
        sketch.segments = {{{0, 0}, {2, 0}}, {{2, 0}, {2, 2}}, {{2, 2}, {0, 2}}, {{0, 2}, {0, 0}}};
        return sketch;
    };
    const auto target = [](double z, const QString &name) {
        ExtrusionObject body; body.feature = BodyFeature::Primitive; body.name = name;
        body.primitive.origin[0] = body.primitive.origin[1] = -1;
        body.primitive.origin[2] = z;
        body.primitive.size[0] = body.primitive.size[1] = 4;
        body.primitive.size[2] = 2;
        return body;
    };
    const auto zBounds = [](const ForgeBody &body) {
        require(bool(body), "B-rep disponibile per controllare il limite prossimo corpo");
        double low = 1e100, high = -1e100;
        for (auto vertex : body->vertices()) {
            const double z = body->vertex(vertex).point.z();
            low = std::min(low, z); high = std::max(high, z);
        }
        return QPair<double, double>{low, high};
    };
    QMainWindow window;
    auto *view = new CadViewport(&window);
    window.setCentralWidget(view);
    view->tessellationQuality_ = 0;
    DocumentState state;
    state.sketches = {profile()};
    state.extrusions = {target(4, QStringLiteral("Limite positivo")), target(-7, QStringLiteral("Limite invertito"))};
    view->loadDocument(state);
    if (openGl) {
        require(QGuiApplication::platformName() != QStringLiteral("offscreen"), "QA estrusione GL richiede una sessione grafica");
        window.resize(1100, 800); window.show();
        view->setReferencePlanesVisible(false); view->setAxesVisible(false); view->setGridVisible(false);
        view->setViewPreset(4); view->fitAll(); QApplication::processEvents();
    }
    const auto findPanel = [&]() -> QDialog * {
        for (auto *dialog : window.findChildren<QDialog *>())
            if (dialog->findChild<QComboBox *>(QStringLiteral("extrusionExtent"))) return dialog;
        return nullptr;
    };
    const auto waitPreview = [&] {
        QElapsedTimer wait; wait.start();
        while ((view->previewPending() || view->previewRunning_ || (view->previewTimer_ && view->previewTimer_->isActive()))
               && wait.elapsed() < 15000) QApplication::processEvents();
        require(view->preview_.valid && !view->previewRunning_, qPrintable(QStringLiteral("anteprima prossimo corpo: ") + view->previewError()));
    };
    ExtrusionObject definition; definition.sketchIndex = 0; definition.distance = 2;
    definition.extrudeSides = 2; definition.distance2 = 1; definition.extrudeSurface = true;
    bool inspected = false, applied = false;
    std::exception_ptr failure;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = findPanel();
        try {
            require(dialog, "pannello estrusione aperto");
            auto *extent = dialog->findChild<QComboBox *>(QStringLiteral("extrusionExtent"));
            auto *direction = dialog->findChild<QComboBox *>(QStringLiteral("extrusionNextBodyDirection"));
            auto *sides = dialog->findChild<QComboBox *>(QStringLiteral("extrusionSides"));
            auto *surface = dialog->findChild<QCheckBox *>(QStringLiteral("extrusionSurface"));
            auto *distance = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("extrusionDistance"));
            auto *reference = dialog->findChild<QPushButton *>(QStringLiteral("extrusionExtentReference"));
            require(extent && direction && sides && surface && distance && reference && extent->count() == 5
                        && extent->itemText(0) == QStringLiteral("Distanza")
                        && extent->itemText(3) == QStringLiteral("Fino a una faccia o a un piano")
                        && extent->itemText(4) == QStringLiteral("Fino al prossimo corpo"),
                    "nuova fine appesa senza cambiare gli indici esistenti");
            extent->setCurrentIndex(4);
            for (int pass = 0; pass < 4; ++pass) QApplication::processEvents();
            auto *sidesModel = qobject_cast<QStandardItemModel *>(sides->model());
            auto *scroll = dialog->findChild<QScrollArea *>(QStringLiteral("functionDialogScroll"));
            require(direction->isVisibleTo(dialog) && !reference->isVisibleTo(dialog) && !distance->isVisibleTo(dialog)
                        && !view->referencePicking() && sides->currentIndex() == 0 && sidesModel
                        && !sidesModel->item(1)->isEnabled() && !sidesModel->item(2)->isEnabled()
                        && !surface->isChecked() && !surface->isEnabled(),
                    "prossimo corpo usa verso esplicito, nessun picker, un verso solido");
            if (openGl)
                require(scroll && scroll->viewport()->rect().contains(QRect(direction->mapTo(scroll->viewport(), QPoint()), direction->size())),
                        "direzione subito visibile anche nel pannello compatto");
            waitPreview();
            auto bounds = zBounds(view->preview_.geometry);
            require(std::abs(bounds.first) < 1e-7 && std::abs(bounds.second - 4) < 1e-7,
                    "anteprima positiva termina sul corpo superiore");
            direction->setCurrentIndex(1);
            waitPreview();
            bounds = zBounds(view->preview_.geometry);
            require(std::abs(bounds.first + 5) < 1e-7 && std::abs(bounds.second) < 1e-7,
                    "anteprima invertita termina sul corpo inferiore");
            if (openGl) {
                const auto directory = qEnvironmentVariable("FORGECAD_GUI_REVIEW_QA_DIR");
                const auto frame = view->grabFramebuffer();
                require(!frame.isNull(), "rendering della nuova estrusione in anteprima");
                if (!directory.isEmpty())
                    require(QDir().mkpath(directory) && frame.save(QDir(directory).filePath(QStringLiteral("next-body-preview.png")))
                                && dialog->grab().save(QDir(directory).filePath(QStringLiteral("next-body-dialog.png"))),
                            "catture anteprima e pannello prossimo corpo");
                view->makeCurrent(); require(view->glGetError() == GL_NO_ERROR, "anteprima prossimo corpo senza errori GL"); view->doneCurrent();
            }
            inspected = true;
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            require(applied, "conferma della nuova modalita' raggiunge il callback senza riferimento");
        } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
    });
    const bool accepted = extrusionDialog(&window, view, QStringLiteral("Prossimo corpo"), -1, definition,
        [&](const ExtrusionObject &d) {
            require(d.extent == 4 && d.distance < 0 && d.extentRef.kind < 0 && d.extrudeSides == 0 && !d.extrudeSurface,
                    "parametri GUI della nuova estrusione coerenti");
            const auto error = view->createExtrusion(d, QStringLiteral("Fino al prossimo corpo"));
            applied = error.isEmpty();
            return error;
        });
    if (failure) std::rethrow_exception(failure);
    require(accepted && inspected && applied && view->extrusions_.size() == 3 && view->preview_.key.isEmpty()
                && !view->refPickFinished_, "conferma pulisce anteprima e callback del pannello");
    const auto created = Kernel::writeBodyBinary(*view->extrusions_[2].forgeBody);
    view->undo(); require(view->extrusions_.size() == 2, "Undo unico della nuova estrusione");
    view->redo(); require(view->extrusions_.size() == 3 && view->extrusions_[2].extent == 4
                             && Kernel::writeBodyBinary(*view->extrusions_[2].forgeBody) == created,
                         "Redo conserva la nuova modalita' e la geometria esatta");
    // Nessun candidato: il messaggio viene dal calcolo, non da una richiesta
    // di scegliere un riferimento che questa modalita' non richiede.
    state.extrusions.clear(); view->loadDocument(state);
    definition.extent = 4; definition.extrudeSides = 0; definition.extrudeSurface = false;
    bool missingTargetSeen = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = findPanel();
        try {
            require(dialog && !view->referencePicking(), "nuova modalita' senza candidati apre senza picker");
            QElapsedTimer wait; wait.start();
            while ((view->previewPending() || view->previewRunning_) && wait.elapsed() < 15000) QApplication::processEvents();
            require(view->previewError().contains(QStringLiteral("corp"), Qt::CaseInsensitive),
                    "assenza di corpo limite segnalata chiaramente nell'anteprima");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            auto *message = dialog->findChild<QPlainTextEdit *>(QStringLiteral("extrusionStatus"));
            missingTargetSeen = message && message->toPlainText().contains(QStringLiteral("corp"), Qt::CaseInsensitive)
                && !message->toPlainText().contains(QStringLiteral("Scegli prima nella vista")) && view->extrusions_.isEmpty();
            require(missingTargetSeen, "nessun corpo valido produce errore chiaro e nessuna modifica");
            dialog->reject();
        } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
    });
    require(!extrusionDialog(&window, view, QStringLiteral("Nessun corpo prossimo"), -1, definition,
                            [&](const ExtrusionObject &d) { return view->createExtrusion(d, QStringLiteral("Non creato")); }),
            "pannello senza corpo limite annullato");
    if (failure) std::rethrow_exception(failure);
    require(missingTargetSeen && view->extrusions_.isEmpty() && view->preview_.key.isEmpty(), "errore e Annulla lasciano intatto il documento");
    std::cout << "PASS estrusione prossimo corpo GUI: opzione, versi, anteprima, conferma, Undo/Redo ed errore senza limite" << std::endl;
}
