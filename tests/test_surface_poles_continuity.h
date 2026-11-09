static void surfacePolesContinuity(bool gl = false) {
    using namespace ForgeCad;
    using namespace Kernel;
    QMainWindow window;
    auto *view = new CadViewport(&window);
    window.setCentralWidget(view);
    view->tessellationQuality_ = 0;
    if (gl) {
        window.resize(1600, 1050);
        window.show();
        QApplication::processEvents();
    }
    ExtrusionObject imported;
    imported.feature = BodyFeature::Imported;
    imported.name = "Continuita tre facce";
    imported.importData =
        QByteArray::fromStdString(writeStep({{"Strip", surface_fixture::strip(), false, {0, 0, 0}, nullptr, {}}}));
    DocumentState document;
    document.extrusions = {imported};
    view->loadDocument(document);
    const auto source = view->extrusions_[0].forgeBody;
    const auto before = writeBodyBinary(*source);
    view->setReferencePlanesVisible(false);
    view->setAxesVisible(false);
    view->setGridVisible(false);
    view->setViewPreset(4);
    view->fitAll();
    ExtrusionObject definition;
    definition.feature = BodyFeature::SurfaceTools;
    definition.firstBody = 0;
    definition.surfaceTool.mode = 2;
    std::exception_ptr failure;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            auto *list = dialog->findChild<QListWidget *>("surfacePolesFaces");
            auto *table = dialog->findChild<QTableView *>("surfacePolesTable");
            auto *model = static_cast<SurfacePolesModel *>(table->model());
            auto *g0 = dialog->findChild<QCheckBox *>("surfacePolesKeepConnected"),
                 *g1 = dialog->findChild<QCheckBox *>("surfacePolesKeepTangent");
            require(g0 && g1 && !g0->isChecked() && !g1->isChecked(),
                    "vincoli opzionali disattivati nei documenti precedenti");
            model->setData(model->index(21, model->coordinateColumn() + 2), QLocale().toString(.05, 'g', 17));
            list->item(2)->setCheckState(Qt::Unchecked);
            list->item(1)->setCheckState(Qt::Unchecked);
            require(model->rowCount() == 16, "una sola faccia scelta prima dei vincoli");
            g1->setChecked(true);
            require(g0->isChecked() && model->rowCount() == 48, "G1 include G0 e facce NURBS adiacenti");
            const auto wait = [&](int ms) {
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < ms)
                    QApplication::processEvents();
            };
            require(model->snapshot().contains(21) && std::abs(model->values(21)[2] - .05) < 1e-12,
                    "inclusione automatica conserva i comandi delle facce deselezionate");
            model->restore({});
            wait(100);
            const int driver = 13, other = 17;
            model->setData(model->index(driver, model->coordinateColumn() + 2), QLocale().toString(.2, 'g', 17));
            wait(150);
            require(model->automaticRows().size() == 3 && model->automaticRows().contains(other),
                    "tre poli non selezionati seguono il giunto G1");
            require(std::abs(model->values(other)[2] - .2) < 1e-12 && std::abs(model->values(21)[2] - .2) < 1e-12,
                    "bordo e fascia tangente della seconda faccia si muovono");
            require(model->snapshot().size() == 1 && view->surfacePoleFollowers_.size() == 3,
                    "poli automatici distinti dai comandi manuali ed evidenziati");
            const auto state = model->snapshot();
            table->selectRow(driver);
            auto *interaction =
                static_cast<SurfacePolesInteraction *>(dialog->findChild<QObject *>("surfacePolesInteraction"));
            for (int pass = 0; pass < 2; ++pass) {
                const auto point = view->surfacePoleScreen(driver);
                const auto send = [&](QEvent::Type type, QPointF pos) {
                    QMouseEvent e(type, pos, view->mapToGlobal(pos.toPoint()),
                                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(view, &e);
                };
                send(QEvent::MouseButtonPress, point);
                send(QEvent::MouseMove, point + QPointF(12, 4));
                const auto once = model->values(other);
                send(QEvent::MouseMove, point + QPointF(12, 4));
                require(model->values(other) == once, "movimenti ripetuti senza accumulo");
                require(interaction->interacting(), "trascinamento vincolato avviato");
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(dialog, &escape);
                require(!interaction->interacting() && model->snapshot() == state &&
                            std::abs(model->values(other)[2] - .2) < 1e-12,
                        "Esc ripristina comandi e poli automatici senza accumulo");
            }
            g1->setChecked(false);
            require(g0->isChecked(), "G1 disattivabile mantenendo G0");
            wait(100);
            require(std::abs(model->values(21)[2] - .2) < 1e-12,
                    "disattivare G1 conserva la forma attuale senza imporla ai gesti futuri");
            g1->setChecked(true);
            wait(100);
            model->setData(model->index(other, model->coordinateColumn() + 2), QLocale().toString(.3, 'g', 17));
            wait(120);
            require(std::abs(model->values(driver)[2] - .3) < 1e-12 && std::abs(model->values(other)[2] - .3) < 1e-12,
                    "un polo modificato in precedenza segue il nuovo comando anche se non selezionato");
            model->setData(model->index(other, model->coordinateColumn() + 2), QLocale().toString(0., 'g', 17));
            wait(120);
            wait(100);
            require(std::abs(model->values(driver)[2]) < 1e-12 && std::abs(model->values(other)[2]) < 1e-12,
                    "ancoraggio a spostamento zero stabile senza deriva");
            model->restore(state);
            wait(120);
            {
                QSignalBlocker guard(model);
                model->setData(model->index(other, model->coordinateColumn() + 2), QLocale().toString(.3, 'g', 17));
            }
            model->dataChanged(model->index(driver, 0), model->index(other, model->columnCount() - 1));
            wait(120);
            auto *ok = dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok);
            require(!ok->isEnabled(), "comandi incompatibili sui bordi impediscono conferma");
            model->restore(state);
            wait(150);
            QElapsedTimer ready;
            ready.start();
            while ((!ok->isEnabled() || view->previewPending()) && ready.elapsed() < 8000)
                QApplication::processEvents();
            require(ok->isEnabled() && view->previewError().isEmpty(), "anteprima geometrica valida con G1");
            auto *info = dialog->findChild<QPlainTextEdit *>("surfacePolesContinuityInfo");
            require(info->toPlainText().contains("scarto G1"),
                    "controllo di continuita della geometria nell'anteprima");
            if (gl) {
                dialog->resize(650, 970);
                for (auto *area : dialog->findChildren<QScrollArea *>())
                    area->verticalScrollBar()->setValue(700);
                view->repaint();
                QApplication::processEvents();
                view->grabFramebuffer().save("/tmp/forgecad-poles-continuity/cage.png");
                window.grab().save("/tmp/forgecad-poles-continuity/window.png");
            }
            ok->click();
        } catch (...) {
            failure = std::current_exception();
            dialog->reject();
        }
    });
    require(surfaceToolDialog(&window, view, "Poli con continuita G1", -1, definition,
                              [&](const auto &d) { return view->createBody(d); }) ||
                bool(failure),
            "funzione vincolata confermata");
    if (failure)
        std::rethrow_exception(failure);
    const auto result = view->extrusions_.back();
    int stored = 0;
    for (const auto &face : result.surfaceTool.poleFaces)
        stored += face.edits.size();
    require(result.surfaceTool.poleContinuity == 2 && stored == 4, "salvati opzione e stato risolto completo");
    auto report = checkSurfacePoleContinuity(*source, *result.forgeBody, source->faces(), 1e-6);
    require(report.gap < 1e-9 && report.angle < 1e-9, "G0 e G1 verificati sulla geometria finale");
    require(checkBody(*result.forgeBody, {true, true}).empty() && writeBodyBinary(*source) == before,
            "B-rep valido e sorgente immutabile");
    auto free = result;
    free.surfaceTool.poleContinuity = 0;
    require(CadViewport::previewKey(free, 1) != CadViewport::previewKey(result, 1),
            "continuita nella chiave di anteprima");
    view->undo();
    require(view->extrusions_.size() == 1, "Undo della propagazione");
    view->redo();
    require(view->extrusions_.size() == 2, "Redo della propagazione");
    QByteArray bytes;
    clientOk(encodeDocument(view->documentState(), bytes));
    DocumentState decoded;
    clientOk(decodeDocument(bytes, decoded));
    require(decoded.extrusions.back().surfaceTool.poleContinuity == 2, "codec13 conserva G1");
    auto extras = QJsonDocument::fromJson(surfaceToolsExtras(view->extrusions_)).object();
    require(extras["schema"].toInt() == 13, "schema13 per i vincoli");
    auto records = extras["features"].toArray();
    auto record = records.last().toObject();
    record["continuity"] = 3;
    records[records.size() - 1] = record;
    extras["features"] = records;
    auto retained = view->extrusions_;
    require(!applySurfaceToolsExtras(QJsonDocument(extras).toJson(), retained).isEmpty(),
            "codec rifiuta continuita sconosciuta");
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            auto *table = dialog->findChild<QTableView *>("surfacePolesTable");
            auto *model = static_cast<SurfacePolesModel *>(table->model());
            require(dialog->findChild<QCheckBox *>("surfacePolesKeepTangent")->isChecked() &&
                        std::abs(model->values(17)[2] - .2) < 1e-12 && std::abs(model->values(21)[2] - .2) < 1e-12,
                    "riapertura ricostruisce la stessa griglia vincolata");
        } catch (...) {
            failure = std::current_exception();
        }
        dialog->reject();
    });
    surfaceToolDialog(&window, view, "Riapertura continuita", 1, result, [](const auto &) { return QString(); });
    if (failure)
        std::rethrow_exception(failure);
    for (auto &body : decoded.extrusions) {
        body.forgeBody.reset();
        body.cachedGeometry = false;
        body.display = {};
    }
    CadViewport rebuilt;
    rebuilt.tessellationQuality_ = 0;
    rebuilt.loadDocument(decoded);
    require(rebuilt.extrusions_.back().forgeBody && rebuilt.extrusions_.back().error.isEmpty(),
            "continuita rigenerata senza cache");
    std::cout << "PASS continuita poli: G0/G1 opzionali, propagazione verde, Esc, conflitti, anteprima, Undo/Redo, "
                 "codec13 e rigenerazione"
              << std::endl;
}
