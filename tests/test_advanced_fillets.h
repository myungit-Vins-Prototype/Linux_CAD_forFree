static void advancedFillets() {
    using namespace ForgeCad;
    using namespace Kernel;
    QMainWindow window;
    CadViewport view(&window);
    window.setCentralWidget(&view);
    window.resize(1200, 1000);
    window.show();
    QApplication::processEvents();
    PrimitiveParameters primitive;
    primitive.size[0] = 20;
    primitive.size[1] = 16;
    primitive.size[2] = 30;
    for (int mode = 0; mode < 9; ++mode) {
        view.loadDocument(DocumentState{});
        require(view.createPrimitive(primitive, "Base").isEmpty(), "base raggi avanzati");
        const auto base = view.extrusions().first().forgeBody;
        const auto edge = nearestEdge(*base, {0, 0, 15}, 1e-8);
        QVector<EdgePoint> refs{edgeReference(*base, edge, {0, 0, 15})};
        ChamferSpec spec;
        spec.fillet.mode = mode < 3 ? 1 : mode == 3 ? 2 : mode == 4 ? 3 : 0;
        spec.fillet.conicType = qMin(mode, 2);
        spec.fillet.rho = mode == 0 ? .4 : mode == 1 ? .5 : .65;
        spec.fillet.propagate = false;
        spec.fillet.radii = {2, 3, 4, 3, 2};
        if (mode >= 6) {
            spec.fillet.mode = 1;
            spec.fillet.conicType = mode - 6;
            spec.fillet.rho = mode == 6 ? .4 : mode == 7 ? .5 : .65;
            spec.fillet.conicControl = 1;
            spec.fillet.conicRadius = mode == 6 ? 3 : mode == 7 ? 3 * std::sqrt(.5) : 1;
        }
        if (mode == 4) {
            const auto &e = base->edge(edge);
            refs = {faceReference(*base, base->finFace(e.forward), {0, 0, 15}),
                    faceReference(*base, base->finFace(e.backward), {0, 0, 15})};
            for (auto &r : refs)
                r.role = kEdgePointFaceBoundary;
        }
        bool checked = false;
        QString applyError;
        QTimer timer;
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            auto *tabs = window.findChild<QTabWidget *>("filletTabs");
            if (!tabs || !tabs->isVisible())
                return;
            timer.stop();
            auto *automatic = window.findChild<QCheckBox *>("filletAutomaticPreview");
            automatic->setChecked(false);
            checked = tabs->count() == 4 && tabs->currentIndex() == spec.fillet.mode;
            for (int i = 0; i < 4; ++i)
                checked = checked && !tabs->tabIcon(i).isNull();
            if (mode == 3) {
                auto *count = window.findChild<QSpinBox *>("filletPointCount");
                count->setValue(7);
                auto *table = window.findChild<QTableWidget *>("filletRadiusTable");
                checked = checked && table->rowCount() == 7 && table->item(0, 1)->text().startsWith("0") &&
                          table->item(6, 1)->text().startsWith("100");
            }
            if (mode == 4)
                for (int k = 0; k < 2; ++k) {
                    auto *pick = window.findChild<QPushButton *>(QStringLiteral("filletPickFace%1").arg(k));
                    pick->click();
                    GeometryRef ref;
                    ref.kind = 5;
                    ref.index = 0;
                    ref.point = refs[k];
                    auto callback = view.refPickFinished_;
                    checked = checked && bool(callback);
                    if (callback)
                        callback(true, ref);
                }
            auto *buttons = tabs->parentWidget()->findChild<QDialogButtonBox *>();
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        timer.start();
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &window, [&] {
            for (auto *d : window.findChildren<QDialog *>())
                if (d->isVisible())
                    d->reject();
        });
        watchdog.start(5000);
        const auto result =
            filletDialog(&window, &view, "Raggio", 0, -1, refs, 3, spec,
                         [&](int body, const auto &selected, double radius, bool chamfer, const auto &definition) {
                             applyError =
                                 view.createBlend(body, selected, radius, chamfer, "Raggio avanzato", definition);
                             return applyError;
                         });
        watchdog.stop();
        timer.stop();
        if (!applyError.isEmpty())
            std::cerr << "mode=" << mode << " " << applyError.toStdString() << std::endl;
        require(result.applied && checked, "quattro schede reali e conferma del relativo calcolo");
        const auto &built = view.extrusions().last();
        require(built.forgeBody && checkBody(*built.forgeBody, {true, true}).empty(),
                "corpo reale valido dopo conferma");
        require(built.chamferSpec.fillet.mode == spec.fillet.mode && !built.chamferSpec.fillet.propagate,
                "parametri scelti raggiungono il modello");
        if (mode == 3)
            require(built.chamferSpec.fillet.radii.size() == 7, "spinner persistente sul modello");
        const double volume = massProperties(*built.forgeBody).volume;
        for (bool cache : {false, true}) {
            QByteArray bytes;
            DocumentState restored;
            require(encodeDocument(view.currentDocument(), bytes, cache).isEmpty() &&
                        decodeDocument(bytes, restored).isEmpty(),
                    "salvataggio e riapertura nuovi raccordi");
            const auto &saved = restored.extrusions.last().chamferSpec.fillet;
            require(saved.mode == spec.fillet.mode && saved.conicControl == built.chamferSpec.fillet.conicControl &&
                        saved.conicType == built.chamferSpec.fillet.conicType &&
                        saved.rho == built.chamferSpec.fillet.rho &&
                        saved.conicRadius == built.chamferSpec.fillet.conicRadius && !saved.propagate,
                    "tutti i parametri del raccordo conservati");
            if (mode == 3)
                require(restored.extrusions.last().chamferSpec.fillet.radii == built.chamferSpec.fillet.radii,
                        "tutti i raggi variabili conservati");
            CadViewport reopened;
            reopened.loadDocument(restored);
            require(reopened.extrusions().last().forgeBody &&
                        std::abs(massProperties(*reopened.extrusions().last().forgeBody).volume - volume) < 1e-5,
                    "rigenerazione equivalente dopo riapertura");
        }
        view.undo();
        require(view.extrusions().size() == 1, "undo raccordo avanzato");
        view.redo();
        require(view.extrusions().size() == 2 && view.extrusions().last().chamferSpec.fillet.mode == spec.fillet.mode,
                "redo mantiene il tipo di raccordo");
        QThreadPool::globalInstance()->waitForDone();
        QApplication::processEvents();
    }
    for (int mode : {1, 2, 3}) {
        view.loadDocument(DocumentState{});
        require(view.createPrimitive(primitive, "Base anteprima").isEmpty(), "base promozione");
        const auto base = view.extrusions().first().forgeBody;
        const auto e = nearestEdge(*base, {0, 0, 15}, 1e-8);
        QVector<EdgePoint> refs{edgeReference(*base, e, {0, 0, 15})};
        ChamferSpec spec;
        spec.fillet.mode = mode;
        spec.fillet.propagate = false;
        spec.fillet.radii = {2, 3, 4, 3, 2};
        if (mode == 3) {
            const auto &edge = base->edge(e);
            refs = {faceReference(*base, base->finFace(edge.forward), {0, 0, 15}),
                    faceReference(*base, base->finFace(edge.backward), {0, 0, 15})};
            for (auto &r : refs)
                r.role = kEdgePointFaceBoundary;
        }
        view.requestBlendPreview(0, refs, 3, false, -1, spec);
        view.startPreviewJob();
        QElapsedTimer elapsed;
        elapsed.start();
        while (view.previewRunning_ && elapsed.elapsed() < 3000) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        if (!view.preview_.error.isEmpty())
            std::cerr << view.preview_.error.toStdString() << std::endl;
        require(view.preview_.valid && view.preview_.geometry, "anteprima reale dei nuovi raccordi");
        const auto geometry = view.preview_.geometry;
        require(view.createBlend(0, refs, 3, false, "Raggio da anteprima", spec).isEmpty() &&
                    view.extrusions().last().forgeBody == geometry,
                "promozione della geometria avanzata senza rifare il calcolo");
        view.clearPreview();
    }
    view.loadDocument(DocumentState{});
    require(view.createPrimitive(primitive, "Base cancellazione").isEmpty(), "base cancellazione");
    const auto base = view.extrusions().first().forgeBody;
    const auto edge = nearestEdge(*base, {0, 0, 15}, 1e-8);
    const QVector<EdgePoint> refs{edgeReference(*base, edge, {0, 0, 15})};
    struct Gate {
        std::mutex mutex;
        std::condition_variable changed;
        bool ready = false, released = false;
    };
    auto gate = std::make_shared<Gate>();
    auto *pool = QThreadPool::globalInstance();
    const int threads = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    struct Release {
        std::shared_ptr<Gate> gate;
        QThreadPool *pool;
        int threads;
        ~Release() {
            {
                std::lock_guard<std::mutex> lock(gate->mutex);
                gate->released = true;
            }
            gate->changed.notify_all();
            pool->waitForDone();
            pool->setMaxThreadCount(threads);
            QApplication::processEvents();
        }
    } release{gate, pool, threads};
    pool->start([gate] {
        std::unique_lock<std::mutex> lock(gate->mutex);
        gate->ready = true;
        gate->changed.notify_all();
        gate->changed.wait(lock, [&] { return gate->released; });
    });
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        require(gate->changed.wait_for(lock, std::chrono::seconds(2), [&] { return gate->ready; }),
                "worker occupato per il test di cancellazione");
    }
    bool closedPending = false;
    std::shared_ptr<std::atomic<bool>> cancellation;
    QTimer closer;
    closer.setInterval(20);
    QObject::connect(&closer, &QTimer::timeout, &window, [&] {
        if (!view.previewRunning_)
            return;
        auto *close = window.findChild<QToolButton *>("functionPanelHeaderClose");
        if (!close || !close->isVisible())
            return;
        closedPending = true;
        cancellation = view.previewCancellation_;
        closer.stop();
        close->click();
    });
    closer.start();
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &window, [&] {
        for (auto *d : window.findChildren<QDialog *>())
            if (d->isVisible())
                d->reject();
    });
    watchdog.start(3000);
    const auto result = filletDialog(&window, &view, "Raggio", 0, -1, refs, 3, {},
                                     [](int, const auto &, double, bool, const auto &) { return QString(); });
    watchdog.stop();
    closer.stop();
    require(!result.applied && closedPending && cancellation && cancellation->load() && !view.previewPending(),
            "chiusura immediata e cancellazione mentre l'anteprima e' in calcolo");
    require(view.extrusions().size() == 1, "annullamento non crea geometria");
    std::cout << "PASS nuovi raggi: schede, geometria, salvataggio, rigenerazione, undo/redo e cancellazione"
              << std::endl;
}
