static void surfacePolesAnalytic(bool gl = false) {
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
    imported.name = "Piano analitico";
    imported.importData =
        QByteArray::fromStdString(writeStep({{"Piano", makePlaneSheet(Frame3(), 10), false, {0, 0, 0}, nullptr, {}}}));
    DocumentState document;
    document.extrusions = {imported};
    view->loadDocument(document);
    const auto source = view->extrusions_[0].forgeBody;
    const auto face = source->faces().front();
    require(source->face(face).surface->type() == SurfaceType::Plane, "fixture analitica, senza poli originali");
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
            require(dialog, "pannello aperto sul piano analitico");
            auto *table = dialog->findChild<QTableView *>("surfacePolesTable");
            auto *model = static_cast<SurfacePolesModel *>(table->model());
            require(model->rowCount() == 16 && view->surfacePoleOverlay_.size() == 16,
                    "conversione automatica in griglia 4x4");
            require(dialog->findChild<QLabel *>("surfacePolesGridInfo")->text().contains("analitica convertita"),
                    "conversione indicata nel pannello");
            const auto wait = [&](int ms) {
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < ms)
                    QApplication::processEvents();
            };
            model->setData(model->index(5, model->coordinateColumn() + 2), QLocale().toString(.2, 'g', 17));
            wait(150);
            auto *refit = dialog->findChild<QCheckBox *>("surfacePolesRefitEnabled");
            dialog->findChild<QSpinBox *>("surfacePolesRefitU")->setValue(6);
            dialog->findChild<QSpinBox *>("surfacePolesRefitV")->setValue(8);
            refit->setChecked(true);
            require(model->rowCount() == 48, "spinner sulla superficie convertita");
            wait(250);
            refit->setChecked(false);
            require(model->rowCount() == 16, "ritorno alla griglia analitica convertita");
            dialog->findChild<QCheckBox *>("surfacePolesKeepTangent")->setChecked(true);
            model->setData(model->index(5, model->coordinateColumn() + 2), QLocale().toString(.2, 'g', 17));
            wait(150);
            auto *ok = dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok);
            QElapsedTimer ready;
            ready.start();
            while ((!ok->isEnabled() || view->previewPending()) && ready.elapsed() < 10000)
                QApplication::processEvents();
            require(ok->isEnabled() && view->previewError().isEmpty(), "anteprima deformata del piano valida");
            if (gl) {
                dialog->resize(650, 970);
                view->repaint();
                QApplication::processEvents();
                window.grab().save("/tmp/forgecad-poles-analytic/window.png");
                view->grabFramebuffer().save("/tmp/forgecad-poles-analytic/cage.png");
            }
            ok->click();
        } catch (...) {
            failure = std::current_exception();
            if (dialog)
                dialog->reject();
        }
    });
    const auto accepted = surfaceToolDialog(&window, view, "Poli del piano analitico", -1, definition,
                                            [&](const auto &d) { return view->createBody(d); });
    if (failure)
        std::rethrow_exception(failure);
    require(accepted, "piano convertito e modificato confermato");
    const auto result = view->extrusions_.back();
    require(result.error.isEmpty() && result.forgeBody && checkBody(*result.forgeBody, {true, true}).empty(),
            "risultato NURBS valido");
    auto support = std::dynamic_pointer_cast<const BSplineSurface>(result.forgeBody->face(face).surface);
    require(support && std::abs(support->pole(1, 1).z() - .2) < 1e-12, "deformazione salvata sul piano convertito");
    require(result.surfaceTool.poleFaces.size() == 1 &&
                result.surfaceTool.poleFaces[0].face.geometry == int(SurfaceType::Plane),
            "identita del piano originale conservata");
    require(writeBodyBinary(*source) == before, "corpo sorgente analitico immutato");
    view->undo();
    require(view->extrusions_.size() == 1, "Undo conversione e deformazione");
    view->redo();
    require(view->extrusions_.size() == 2, "Redo conversione e deformazione");
    QByteArray bytes;
    clientOk(encodeDocument(view->documentState(), bytes));
    DocumentState decoded;
    clientOk(decodeDocument(bytes, decoded));
    auto extras = QJsonDocument::fromJson(surfaceToolsExtras(view->extrusions_)).object();
    require(extras["schema"].toInt() == 16, "schema16 per superfici analitiche e G1 completo");
    extras["schema"] = 13;
    auto prior = view->extrusions_;
    require(!applySurfaceToolsExtras(QJsonDocument(extras).toJson(), prior).isEmpty(),
            "rifiuto di conversioni su schema precedente");
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            auto *table = dialog->findChild<QTableView *>("surfacePolesTable");
            auto *model = static_cast<SurfacePolesModel *>(table->model());
            require(model->rowCount() == 16 && std::abs(model->values(5)[2] - .2) < 1e-12,
                    "riapertura della griglia convertita");
        } catch (...) {
            failure = std::current_exception();
        }
        dialog->reject();
    });
    surfaceToolDialog(&window, view, "Riapertura piano", 1, result, [](const auto &) { return QString(); });
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
            "conversione rigenerata senza cache");
    std::cout << "PASS poli analitici: piano 4x4, spinner, deformazione, anteprima, Undo/Redo, codec14, riapertura e "
                 "rigenerazione"
              << std::endl;
}
