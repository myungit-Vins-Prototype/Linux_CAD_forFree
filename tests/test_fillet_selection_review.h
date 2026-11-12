// Incluso nel friend ViewportInteractionTest.
static void filletSelectionReviewRegressions(bool openGl) {
    using namespace ForgeCad;
    const auto click = [](CadViewport &view, const QPoint &point) {
        QMouseEvent press(QEvent::MouseButtonPress, point, view.mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, point, view.mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &press); QApplication::sendEvent(&view, &release);
    };
    QMainWindow window;
    auto *view = new CadViewport(&window); window.setCentralWidget(view);
    view->resize(800, 600); window.resize(800, 600); view->tessellationQuality_ = 0;
    view->setReferencePlanesVisible(false); view->setGridVisible(false); view->setAxesVisible(false);
    if (openGl) { require(QGuiApplication::platformName() != QStringLiteral("offscreen"), "regressione raccordi GL richiede sessione grafica"); window.show(); QApplication::processEvents(); }
    PrimitiveParameters base; base.size[0] = base.size[1] = 10; base.size[2] = 2;
    require(view->createPrimitive(base, QStringLiteral("Base")).isEmpty(), "base della regressione selezione raccordi");
    auto front = base; front.origin[0] = front.origin[1] = 3; front.origin[2] = 4; front.size[0] = front.size[1] = 4;
    require(view->createPrimitive(front, QStringLiteral("Corpo anteriore")).isEmpty(), "corpo opaco davanti alla base");
    const auto setCamera = [&] {
        view->setViewFrame(QVector3D(1, 0, 0), QVector3D(0, 0, 1));
        view->camera_.autoFit_ = false; view->camera_.zoom_ = 14; view->camera_.panX_ = view->camera_.panY_ = -5;
    };
    setCamera();
    const auto shape = view->extrusions_[0].forgeBody;
    const auto edge = Kernel::nearestEdge(*shape, {0, 5, 2}, 1e-7);
    require(edge.valid(), "spigolo della base non coperto dal corpo anteriore");
    const auto reference = edgeReference(*shape, edge, {0, 5, 2});
    const auto covered = view->projectWorldPoint(QVector3D(5, 5, 5)).toPoint();
    const auto exposed = view->projectWorldPoint(QVector3D(1, 5, 2)).toPoint();
    // La preselezione deliberata dei bordi di una faccia resta disponibile.
    click(*view, exposed);
    require(view->selectedFace_.body == 0 && view->beginEdgePick(false, true).isEmpty()
                && view->pickedEdges_.size() == 4 && view->edgePickPoints().size() == 1
                && view->edgePickPoints().first().role == kEdgePointFaceBoundary,
            "faccia preselezionata conserva intenzionalmente i suoi quattro bordi");
    view->endEdgePick();
    view->resumeEdgePick(0, {reference}, false, true);
    if (openGl) { const auto frame = view->grabFramebuffer(); require(!frame.isNull(), "rendering scena occlusione raccordi"); }
    const auto initialEdges = view->pickedEdges_;
    require(view->pickSceneObject(covered).index == 1 && view->pickEdge(covered) == -1,
            "il clic della regressione colpisce una faccia del corpo anteriore, senza spigoli vicini");
    click(*view, covered);
    require(view->edgePickBody() == 0 && view->pickedEdges_ == initialEdges && view->pickedFaces_.isEmpty(),
            "clic su altro corpo opaco non seleziona i bordi della faccia nascosta dietro");
    click(*view, exposed);
    require(view->pickedEdges_.size() == 4 && view->pickedFaces_.size() == 1,
            "clic sulla faccia effettivamente visibile seleziona i suoi bordi");
    // La scelta X-ray e' esplicita e continua a consentire i bordi occultati.
    view->resumeEdgePick(0, {reference}, false, true); view->setHiddenEdgesVisible(true);
    click(*view, covered);
    require(view->pickedEdges_.size() == 4, "X-ray conserva la scelta esplicita attraverso i corpi");
    view->setHiddenEdgesVisible(false); view->endEdgePick();
    // Modifica di un raccordo: il risultato viene nascosto dall'anteprima,
    // mentre la base consumata e' mostrata. Anche il picker deve usare quella scena.
    DocumentState document; ExtrusionObject root; root.feature = BodyFeature::Primitive; root.primitive = base;
    document.extrusions = {root}; view->loadDocument(document); setCamera();
    const auto originalBase = view->extrusions_[0].forgeBody;
    const auto originalEdge = Kernel::nearestEdge(*originalBase, {0, 5, 2}, 1e-7);
    const auto originalReference = edgeReference(*originalBase, originalEdge, {0, 5, 2});
    require(view->createBlend(0, {originalReference}, .2, false, QStringLiteral("Raccordo")).isEmpty(), "raccordo da modificare");
    require(!view->isBodyDrawn(0) && view->isBodyDrawn(1), "base consumata e risultato visibile prima della modifica");
    const auto initialDocument = view->currentDocument();
    const auto initialGeometry = Kernel::writeBodyBinary(*view->extrusions_[1].forgeBody);
    view->resumeEdgePick(0, {originalReference}, false, true);
    view->requestBlendPreview(0, {originalReference}, .2, false, 1);
    QElapsedTimer previewWait; previewWait.start();
    while ((view->previewPending() || view->previewRunning_) && previewWait.elapsed() < 10000) QApplication::processEvents();
    require(view->preview_.valid && view->isBodyVisibleInScene(0) && !view->isBodyVisibleInScene(1),
            "anteprima modifica mostra la base e nasconde soltanto il risultato editato");
    const auto middle = view->projectWorldPoint(QVector3D(5, 5, 2)).toPoint();
    require(view->pickSceneObject(middle).index == 0 && view->edgePickFaceBody(middle) == 0,
            "picker segue la base effettivamente mostrata durante modifica raccordo");
    if (openGl) {
        const auto frame = view->grabFramebuffer(); require(!frame.isNull(), "scena reale della modifica raccordo");
        view->makeCurrent(); require(view->glGetError() == GL_NO_ERROR, "raccordo e scena di picking senza errori GL"); view->doneCurrent();
        const auto directory = qEnvironmentVariable("FORGECAD_GUI_REVIEW_QA_DIR");
        if (!directory.isEmpty()) require(QDir().mkpath(directory) && frame.save(QDir(directory).filePath(QStringLiteral("fillet-edit-base.png"))), "cattura base in modifica");
    }
    view->endEdgePick();
    require(!view->isBodyVisibleInScene(0) && view->isBodyVisibleInScene(1), "Annulla ripristina la scena finale");
    // Il vero pannello attraversa schede, scelta facce, Esc e tutti i modi
    // di chiusura. L'owner non GL rende queste prove eseguibili anche offscreen.
    QWidget plainOwner;
    for (int finish = 0; finish < 4; ++finish) {
        bool inspected = false, applied = false; std::exception_ptr failure;
        QTimer::singleShot(0, &window, [&] {
            QDialog *dialog = nullptr;
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *candidate = qobject_cast<QDialog *>(widget); candidate && candidate->findChild<QTabWidget *>(QStringLiteral("filletTabs"))) dialog = candidate;
                for (auto *candidate : widget->findChildren<QDialog *>())
                    if (candidate->findChild<QTabWidget *>(QStringLiteral("filletTabs"))) dialog = candidate;
            }
            try {
                require(dialog, "pannello raccordo reale aperto");
                auto *tabs = dialog->findChild<QTabWidget *>(QStringLiteral("filletTabs"));
                auto *automatic = dialog->findChild<QCheckBox *>(QStringLiteral("filletAutomaticPreview"));
                automatic->setChecked(false);
                auto *list = dialog->findChild<QListWidget *>(QStringLiteral("filletEdges0"));
                require(list && list->count() == 1 && view->pickedEdges_.size() == 1, "modifica riparte dal solo spigolo salvato");
                list->setCurrentRow(0);
                require(view->referenceListHighlighted(list), "voce scelta evidenziata nella vista");
                tabs->setCurrentIndex(3);
                require(!list->isVisible() && !view->referenceListHighlighted(list) && view->referenceListMarks_.isEmpty(),
                        "cambio scheda rimuove lo spigolo evidenziato dalla lista ora nascosta");
                dialog->findChild<QPushButton *>(QStringLiteral("filletPickFace0"))->click();
                require(view->referencePicking(), "scelta faccia attiva nella scheda dedicata");
                QKeyEvent escapeReference(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(view, &escapeReference);
                require(!view->referencePicking() && dialog->isVisible(), "Esc annulla la sola scelta del riferimento");
                tabs->setCurrentIndex(0);
                require(view->edgePickPoints().size() == 1 && view->edgePickPoints().first().subshape == originalReference.subshape,
                        "ritorno ai bordi non aggiunge spigoli dopo scelta facce annullata");
                inspected = true;
                if (finish == 0) {
                    QKeyEvent escapePanel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                    QApplication::sendEvent(view, &escapePanel);
                } else if (finish == 1) {
                    automatic->setChecked(true); // Chiudi anche con una richiesta pendente.
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
                } else if (finish == 2) dialog->close();
                else dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            } catch (...) {
                failure = std::current_exception();
                if (dialog) dialog->reject();
            }
        });
        const auto result = filletDialog(openGl ? static_cast<QWidget *>(&window) : &plainOwner, view,
            QStringLiteral("Modifica raccordo review"), 0, 1, {originalReference}, .25, ChamferSpec{},
            [&](int body, const QVector<EdgePoint> &edges, double radius, bool chamfer, const ChamferSpec &spec) {
                require(body == 0 && edges.size() == 1 && edges.first().subshape == originalReference.subshape,
                        "conferma usa soltanto lo spigolo intenzionalmente salvato");
                auto changed = view->extrusions_[1]; changed.blendEdges = edges; changed.blendSize = radius;
                changed.blendChamfer = chamfer; changed.chamferSpec = spec;
                const auto error = view->updateBody(1, changed); applied = error.isEmpty(); return error;
            });
        if (failure) std::rethrow_exception(failure);
        require(inspected && result.applied == (finish == 3) && applied == (finish == 3), "esito atteso della chiusura del pannello raccordo");
        QElapsedTimer stopped; stopped.start();
        while (view->previewRunning_ && stopped.elapsed() < 10000) QApplication::processEvents();
        require(!view->edgePicking_ && !view->referencePicking() && !view->edgePickChanged_ && !view->edgePickPanelAccept_
                    && !view->edgePickPanelCancel_ && !view->refPickFinished_ && !view->previewCallback_
                    && view->preview_.key.isEmpty() && view->referenceListMarks_.isEmpty() && view->pickedEdges_.isEmpty(),
                "chiusura ripulisce spigoli, evidenziazioni, timer e callback anche dopo anteprima pendente");
        if (finish == 3) {
            require(view->extrusions_.size() == 2 && view->extrusions_[1].blendEdges.size() == 1, "conferma non crea bordi o corpi aggiuntivi");
            const auto confirmed = Kernel::writeBodyBinary(*view->extrusions_[1].forgeBody);
            view->undo(); require(Kernel::writeBodyBinary(*view->extrusions_[1].forgeBody) == initialGeometry, "Undo della modifica raggio");
            view->redo(); require(Kernel::writeBodyBinary(*view->extrusions_[1].forgeBody) == confirmed, "Redo della modifica raggio");
            view->undo();
        }
        require(view->extrusions_.size() == initialDocument.extrusions.size(), "numero corpi invariato dopo modifica o annullamento");
        for (int i = 0; i < view->extrusions_.size(); ++i)
            require(view->extrusions_[i].visible == initialDocument.extrusions[i].visible, "visibilita' persistente non alterata da anteprima o chiusura");
        require(!view->isBodyVisibleInScene(0) && view->isBodyVisibleInScene(1), "nessun corpo finale lasciato nascosto dal pannello");
    }
    // Conferma anticipata: il gate rende deterministici timer pendente,
    // worker attivo e sostituzione di una richiesta gia' partita. Si verifica
    // il puntatore pubblicato dal worker, non solo l'equivalenza geometrica.
    auto *pool = QThreadPool::globalInstance();
    require(pool->waitForDone(10000), "pool libero prima della conferma anticipata");
    const int previousLimit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    for (bool embedded : {false, true}) for (int scenario = 0; scenario < 4; ++scenario) {
        const bool edit = scenario == 2;
        DocumentState before = initialDocument;
        if (!edit) { before.extrusions.resize(1); before.extrusions[0].visible = true; }
        view->loadDocument(before);
        const auto baseGeometry = view->extrusions_[0].forgeBody;
        const auto savedGeometry = edit ? view->extrusions_[1].forgeBody : ForgeBody{};
        QSemaphore entered, release;
        pool->start([&] { entered.release(); release.acquire(); });
        entered.acquire();
        bool released = false, inspected = false, hiddenWhilePending = false;
        int starts = 0, ends = 0, delivered = 0, applications = 0;
        ForgeBody published;
        std::exception_ptr failure;
        view->workCallback_ = [&](bool active, const QString &, bool background) {
            if (background) (active ? starts : ends)++;
        };
        QTimer::singleShot(0, &window, [&] {
            QDialog *dialog = nullptr;
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *candidate = qobject_cast<QDialog *>(widget); candidate && candidate->findChild<QTabWidget *>("filletTabs")) dialog = candidate;
                for (auto *candidate : widget->findChildren<QDialog *>())
                    if (candidate->findChild<QTabWidget *>("filletTabs")) dialog = candidate;
            }
            try {
                require(dialog && !dialog->isHidden() && (!openGl || dialog->isVisible()) && view->previewPending(),
                        "raccordo aperto con anteprima pendente");
                const auto statusCallback = view->previewCallback_;
                view->setPreviewCallback([&, statusCallback](const QString &error) {
                    published = view->preview_.geometry;
                    ++delivered;
                    if (statusCallback) statusCallback(error);
                });
                if (scenario != 0) {
                    view->startPreviewJob();
                    view->previewTimer_->stop();
                    require(view->previewRunning_, "worker raccordo attivo prima di Conferma");
                }
                if (edit) {
                    // Il risultato superato deve essere scartato, poi la
                    // conferma attende solo la nuova definizione.
                    dialog->findChild<QDoubleSpinBox *>("filletConstantRadius")->setValue(.4);
                }
                QTimer::singleShot(0, dialog, [&, dialog] {
                    hiddenWhilePending = dialog->isHidden() && view->previewPending();
                    if (!released) { released = true; release.release(); }
                });
                auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
                ok->click();
                require(hiddenWhilePending, "Conferma chiude il pannello prima della fine dell'anteprima");
                if (scenario == 3) {
                    auto *message = dialog->findChild<QPlainTextEdit *>("functionApplyError");
                    require(!dialog->isHidden() && (!openGl || dialog->isVisible()) && message && !message->isHidden()
                                && !view->previewError().isEmpty() && message->toPlainText().contains(view->previewError()),
                            "errore geometrico reale riapre il pannello con il messaggio dell'anteprima");
                    require(view->extrusions_.size() == before.extrusions.size()
                                && view->extrusions_[0].forgeBody == baseGeometry,
                            "anteprima fallita non modifica il documento");
                    dialog->findChild<QDoubleSpinBox *>("filletConstantRadius")->setValue(.35);
                    ok->click();
                }
                require(dialog->result() == QDialog::Accepted, "conferma anticipata completata senza errore di attesa");
                inspected = true;
            } catch (...) {
                failure = std::current_exception();
                if (dialog) dialog->reject();
            }
            if (!released) { released = true; release.release(); }
        });
        const auto result = filletDialog(embedded ? static_cast<QWidget *>(&window) : &plainOwner, view,
            QStringLiteral("Conferma raccordo in calcolo"), 0, edit ? 1 : -1, {originalReference},
            scenario == 3 ? 20. : .35, ChamferSpec{},
            [&](int body, const QVector<EdgePoint> &edges, double radius, bool chamfer, const ChamferSpec &spec) {
                ++applications;
                require(view->previewPending() && view->preview_.key == CadViewport::previewKey(view->preview_.definition, edit ? 1 : -1),
                        "apply entra con la stessa anteprima ancora pendente");
                if (!edit) return view->createBlend(body, edges, radius, chamfer, QStringLiteral("Raccordo anticipato"), spec);
                auto changed = view->extrusions_[1];
                changed.blendEdges = edges; changed.blendSize = radius; changed.chamferSpec = spec;
                return view->updateBody(1, changed);
            });
        require(pool->waitForDone(10000), "worker raccordo terminato");
        QApplication::processEvents();
        view->workCallback_ = {};
        if (failure) { pool->setMaxThreadCount(previousLimit); std::rethrow_exception(failure); }
        require(inspected && result.applied && view->extrusions_.size() == 2
                    && published && view->extrusions_[1].forgeBody == published,
                "creazione/modifica acquisisce esattamente il puntatore pubblicato dall'anteprima");
        const int expectedJobs = (edit || scenario == 3) ? 2 : 1;
        require(starts == expectedJobs && ends == starts && delivered == (scenario == 3 ? 2 : 1)
                    && applications == (scenario == 3 ? 2 : 1),
                "nessun secondo calcolo uguale, callback obsolete scartate e lavoro bilanciato");
        require(!view->extrusions_[1].display.vertices.isEmpty() && view->extrusions_[1].display.constructionCurves.isEmpty()
                    && !view->previewPending() && !view->previewRunning_ && view->preview_.key.isEmpty(),
                "risultato completo e pulizia dopo la conferma anticipata");
        view->undo();
        require(view->extrusions_.size() == before.extrusions.size() && view->extrusions_[0].forgeBody == baseGeometry
                    && (!edit || view->extrusions_[1].forgeBody == savedGeometry),
                "un solo Undo ripristina il documento prima della conferma anticipata");
        view->redo();
        require(view->extrusions_.size() == 2 && view->extrusions_[1].forgeBody == published,
                "Redo conserva il puntatore del raccordo confermato");
    }
    pool->setMaxThreadCount(previousLimit);
    std::cout << "PASS raccordi review: occlusione facce, X-ray, scena editata, evidenziazioni, Esc/Annulla/chiusura/conferma e Undo/Redo" << std::endl;
}
