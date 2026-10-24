static void surfaceTools() {
    using namespace ForgeCad;
    using namespace Kernel;
    const auto imported = [](const Body &shape, const QString &name) {
        ExtrusionObject b;
        b.feature = BodyFeature::Imported;
        b.name = name;
        b.importData = QByteArray::fromStdString(writeStep({{name.toStdString(), shape, false, {0, 0, 0}, nullptr, {}}}));
        return b;
    };
    const auto wall = [](double x) { return makePlaneSheet(Frame3(Vec3(x, 5, 0), Vec3(1, 0, 0), Vec3(0, 0, 1)), 20); };
    DocumentState document;
    document.extrusions = {imported(surface_fixture::patch(true), "NURBS con foro"), imported(wall(14), "Riferimento finale")};
    ExtrusionObject grid;
    grid.feature = BodyFeature::SpatialSketch;
    grid.name = "Curve della rete";
    grid.spatialSketch.nextId = 7;
    for (int i = 0; i < 3; ++i) {
        SpatialCurve3D e;
        e.id = i + 1;
        e.points = {{0., 5. * i, 0.}, {10., 5. * i, 1. * i}};
        grid.spatialSketch.entities.append(e);
    }
    for (int j = 0; j < 3; ++j) {
        SpatialCurve3D e;
        e.id = j + 4;
        e.points = {{5. * j, 0., 0.}, {5. * j, 10., 1. * j}};
        grid.spatialSketch.entities.append(e);
    }
    document.extrusions.append(grid);
    CadViewport view;
    view.tessellationQuality_ = 0;
    view.loadDocument(document);
    require(view.extrusions_[0].forgeBody && !surfaceBasisHash(view.extrusions_[0].forgeBody).isEmpty(), "sorgente STEP mantiene NURBS");
    ExtrusionObject network;
    network.feature = BodyFeature::SurfaceTools;
    network.surfaceTool.mode = 1;
    network.name = "Rete 3 x 3";
    for (int i = 0; i < 6; ++i) {
        GeometryRef r;
        r.kind = 9;
        r.index = 2;
        r.featureId = view.extrusions_[2].featureId;
        r.point.subshape = i + 1;
        r.point.geometry = kSpatialCurveReference;
        (i < 3 ? network.ruledFirst : network.ruledSecond).append(r);
    }
    clientOk(view.createBody(network));
    require(view.extrusions_[3].forgeBody->faces().size() == 4, "rete creata con quattro facce");
    require(bodyOperands(view.extrusions_[3]) == QVector<int>{2}, "dipendenza della rete sulle curve spaziali");
    ExtrusionObject extension;
    extension.feature = BodyFeature::SurfaceTools;
    extension.surfaceTool.mode = 0;
    extension.firstBody = 0;
    extension.name = "Estesa fino al piano";
    const auto base = view.extrusions_[0].forgeBody;
    const auto edge = surface_fixture::edgeNear(*base, Vec3(10, 5, 0));
    extension.blendEdges = {edgeReference(*base, edge, Vec3(10, 5, 0))};
    extension.extentRef.kind = 5;
    extension.extentRef.index = 1;
    extension.extentRef.featureId = view.extrusions_[1].featureId;
    const auto target = view.extrusions_[1].forgeBody;
    extension.extentRef.point = faceReference(*target, target->faces().front(), Vec3(14, 5, 0));
    extension.surfaceTool.maximum = 10;
    clientOk(view.createBody(extension));
    require(view.extrusions_[4].forgeBody && bodyOperands(view.extrusions_[4]) == QVector<int>({0, 1}),
            "estensione associativa con base e riferimento");
    require(view.extrusions_[4].modelBodyId == view.extrusions_[0].modelBodyId, "estensione continua lo stesso corpo logico");
    ExtrusionObject poles;
    poles.feature = BodyFeature::SurfaceTools;
    poles.surfaceTool.mode = 2;
    poles.firstBody = 4;
    poles.name = "Poli e pesi";
    poles.surfaceTool.basis = surfaceBasisHash(view.extrusions_[4].forgeBody);
    poles.surfaceTool.edits = {{1, 1, 0, 0, .2, 1.2}};
    clientOk(view.createBody(poles));
    require(view.extrusions_[5].forgeBody && view.extrusions_[5].modelBodyId == view.extrusions_[4].modelBodyId,
            "poli come stadio del corpo");
    const auto changed = writeBodyBinary(*view.extrusions_[5].forgeBody);
    view.undo();
    require(view.extrusions_.size() == 5, "un solo Undo per modifica poli");
    view.redo();
    require(writeBodyBinary(*view.extrusions_[5].forgeBody) == changed, "Redo esatto dei poli");
    auto keyChanged = poles;
    keyChanged.surfaceTool.edits[0].weightFactor = 1.3;
    require(CadViewport::previewKey(poles, 5) != CadViewport::previewKey(keyChanged, 5), "peso invalida anteprima");
    keyChanged = poles;
    keyChanged.surfaceTool.tolerance = 1e-5;
    require(CadViewport::previewKey(poles, 5) != CadViewport::previewKey(keyChanged, 5), "tolleranza invalida anteprima");
    auto invalid = view.extrusions_[5];
    invalid.surfaceTool.edits[0].weightFactor = -1;
    require(!view.updateBody(5, invalid).isEmpty() && writeBodyBinary(*view.extrusions_[5].forgeBody) == changed,
            "peso invalido rifiutato atomicamente");
    invalid = view.extrusions_[5];
    invalid.surfaceTool.basis = QByteArray(32, 'x');
    require(!view.updateBody(5, invalid).isEmpty(), "griglia diversa richiede riselezione esplicita");
    require(!view.moveFeatureTo(3, 0).isEmpty(), "riordino non precede dipendenze della rete");
    clientOk(view.setFeatureSuppressed(5, true));
    require(view.extrusions_[5].suppressed && view.modelBodyTip(view.modelBodyIndexForFeature(4)) == 4,
            "soppressione espone stadio precedente");
    view.undo();
    require(!view.extrusions_[5].suppressed, "Undo della soppressione");
    auto shifted = imported(wall(16), "Riferimento finale");
    clientOk(view.updateBody(1, shifted));
    require(view.extrusions_[4].forgeBody && view.extrusions_[5].forgeBody, "rigenerazione dei poli dopo modifica del riferimento");
    require(view.extrusions_[5].surfaceTool.edits[0].dz == .2, "delta dei poli conservato durante rigenerazione");
    view.undo();
    document = view.currentDocument();
    QByteArray encoded;
    clientOk(encodeDocument(document, encoded, true));
    DocumentState restored;
    clientOk(decodeDocument(encoded, restored));
    require(restored.extrusions[5].surfaceTool.basis == document.extrusions[5].surfaceTool.basis &&
                restored.extrusions[5].surfaceTool.edits[0].weightFactor == 1.2,
            "parametri poli e pesi persistono");
    require(restored.extrusions[4].surfaceTool.maximum == 10 && restored.extrusions[3].surfaceTool.mode == 1,
            "parametri estensione e rete persistono");
    for (auto &b : restored.extrusions)
        b.cachedGeometry = false;
    CadViewport rebuilt;
    rebuilt.tessellationQuality_ = 0;
    rebuilt.loadDocument(restored);
    for (const auto &b : rebuilt.extrusions_)
        require(b.error.isEmpty() && hasGeometry(b), "tutte le feature rigenerano senza cache");
    require(checkBody(*rebuilt.extrusions_[5].forgeBody).empty(), "risultato rigenerato valido");
    QDataStream header(encoded);
    header.skipRawData(7);
    quint32 size = 0;
    header >> size;
    const auto payload = qUncompress(encoded.mid(11, size));
    const auto tag = QByteArray::fromHex("53524631");
    const int offset = payload.lastIndexOf(tag);
    require(offset > 0, "blocco estensioni presente");
    const auto extras = QJsonDocument::fromJson(surfaceToolsExtras(document.extrusions)).object();
    const auto malformed = [&](const QJsonObject &root) {
        QByteArray suffix;
        QDataStream out(&suffix, QIODevice::WriteOnly);
        out << quint32(0x53524631) << QJsonDocument(root).toJson(QJsonDocument::Compact);
        return nativeFixture(payload.left(offset) + suffix, 53);
    };
    auto bad = extras;
    auto records = bad["features"].toArray();
    auto record = records.last().toObject();
    auto edits = record["edits"].toArray();
    edits.append(edits.first());
    record["edits"] = edits;
    records[records.size() - 1] = record;
    bad["features"] = records;
    const auto retained = restored.version.documentId;
    require(!decodeDocument(malformed(bad), restored).isEmpty() && restored.version.documentId == retained,
            "poli duplicati rifiutati senza stato parziale");
    bad = extras;
    records = bad["features"].toArray();
    records.append(records.first());
    bad["features"] = records;
    require(!decodeDocument(malformed(bad), restored).isEmpty(), "estensioni duplicate rifiutate");
    require(!decodeDocument(nativeFixture(payload.left(offset), 53), restored).isEmpty(), "tipo nuovo senza estensioni rifiutato");
    require(!decodeDocument(nativeFixture(payload, 52), restored).isEmpty(), "declassamento del formato rifiutato");
    QByteArray retainedBytes = "intatto";
    auto invalidDocument = document;
    invalidDocument.extrusions[5].surfaceTool.edits[0].weightFactor = 0;
    require(!encodeDocument(invalidDocument, retainedBytes).isEmpty() && retainedBytes == "intatto",
            "codifica invalida non produce output parziale");
    const auto qa = QStringLiteral("/tmp/forgecad-surface-tools-qa");
    QDir().mkpath(qa);
    QMainWindow window;
    window.resize(1200, 900);
    view.resize(1000, 700);
    view.setViewNormal(0);
    view.fitAll();
    std::exception_ptr failure;
    ExtrusionObject submitted;
    // Tabella XYZ/peso, gabbia, mouse e annullamento sono esercitati sul vero pannello.
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            require(dialog, "pannello poli aperto");
            auto *table = dialog->findChild<QTableWidget *>("surfacePolesTable");
            require(table && table->rowCount() > 0 && view.surfacePoleOverlay_.size() == table->rowCount(), "gabbia e tabella concordano");
            require(!dialog->findChild<QComboBox *>("surfaceToolEdge")->isVisible(), "controlli degli altri modi nascosti");
            const int row = table->rowCount() / 2;
            const auto start = view.surfacePoleScreen(row);
            const double x = QLocale().toDouble(table->item(row, 2)->text());
            QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&view, &press);
            QMouseEvent move(QEvent::MouseMove, start + QPointF(14, 5), start + QPointF(14, 5), Qt::NoButton, Qt::LeftButton,
                             Qt::NoModifier);
            QApplication::sendEvent(&view, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, start + QPointF(14, 5), start + QPointF(14, 5), Qt::LeftButton, Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(&view, &release);
            require(QLocale().toDouble(table->item(row, 2)->text()) != x, "trascinamento modifica coordinate");
            table->item(row, 5)->setText("0");
            QElapsedTimer validation;
            validation.start();
            while (validation.elapsed() < 90)
                QApplication::processEvents();
            require(!dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->isEnabled(),
                    "peso zero disabilita conferma");
            table->item(row, 5)->setText(QLocale().toString(1.4));
            validation.restart();while(validation.elapsed()<90)QApplication::processEvents();
            dialog->grab().save(qa + "/poli-pesi.png");
            dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->click();
        } catch (...) {
            failure = std::current_exception();
            if (dialog)
                dialog->reject();
        }
    });
    require(surfaceToolDialog(&window, &view, "Modifica poli NURBS", 5, view.extrusions_[5],
                              [&](const ExtrusionObject &d) {
                                  submitted = d;
                                  return QString();
                              }),
            "pannello poli confermato");
    if (failure)
        std::rethrow_exception(failure);
    require(!submitted.surfaceTool.edits.isEmpty() && view.surfacePoleOverlay_.isEmpty(), "parametri acquisiti e gabbia rimossa");
    require(writeBodyBinary(*view.extrusions_[5].forgeBody) == changed, "anteprima sola non modifica modello");
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Cancel)->click();
    });
    require(!surfaceToolDialog(&window, &view, "Annulla poli", 5, view.extrusions_[5],
                               [&](const ExtrusionObject &) {
                                   throw std::runtime_error("annulla non applica");
                                   return QString();
                               }),
            "annullamento pannello senza applicazione");
    // Picking ordinato nelle due famiglie del pannello rete.
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            require(dialog, "pannello rete aperto");
            auto *u = dialog->findChild<QListWidget *>("surfaceNetworkU"), *v = dialog->findChild<QListWidget *>("surfaceNetworkV");
            require(u && v && u->count() == 3 && v->count() == 3, "famiglie mostrate in ordine");
            dialog->grab().save(qa + "/rete-curve.png");
            dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->click();
        } catch (...) {
            failure = std::current_exception();
            if (dialog)
                dialog->reject();
        }
    });
    require(surfaceToolDialog(&window, &view, "Rete di curve", 3, view.extrusions_[3],
                              [&](const ExtrusionObject &d) {
                                  require(d.ruledFirst.size() == 3 && d.ruledSecond.size() == 3, "due famiglie inviate");
                                  return QString();
                              }),
            "pannello rete confermato");
    if (failure)
        std::rethrow_exception(failure);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            require(dialog, "pannello estensione aperto");
            require(dialog->findChild<QComboBox *>("surfaceToolEdge")->count() >= 4, "bordi liberi disponibili");
            dialog->findChild<QPushButton *>("surfaceToolPickTarget")->click();
            auto callback = view.refPickFinished_;
            require(bool(callback), "picker riferimento attivo");
            callback(true, view.extrusions_[4].extentRef);
            dialog->grab().save(qa + "/estendi-riferimento.png");
            dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->click();
        } catch (...) {
            failure = std::current_exception();
            if (dialog)
                dialog->reject();
        }
    });
    require(surfaceToolDialog(&window, &view, "Estendi fino a riferimento", 4, view.extrusions_[4],
                              [&](const ExtrusionObject &d) {
                                  require(d.extentRef.kind == 5 && d.extentRef.index == 1, "faccia scelta conserva destinazione");
                                  require(resolveEdgeReference(*view.extrusions_[d.firstBody].forgeBody,d.blendEdges.front(),1e-5)==edge,"pannello mantiene il bordo esatto anche con alias comuni");
                                  return view.updateBody(4,d);
                              }),
            "pannello estensione confermato");
    if (failure)
        std::rethrow_exception(failure);
    QTemporaryDir dir;
    auto saved = document;
    const auto path = dir.filePath("superfici.prt");
    clientOk(saveDocumentFile(path, saved));
    clientOk(checkoutWorkspaceDocument(path, saved, "qa"));
    RevisionRecord revision;
    clientOk(checkinWorkspaceDocument(path, saved, {}, "Superfici", revision));
    DocumentState historical;
    WorkspacePaths workspace;
    clientOk(locateDocumentWorkspace(path, workspace));
    RevisionStore store(workspace.revisions);
    clientOk(readNativeRevision(store, revision.version, historical));
    require(historical.extrusions[5].surfaceTool.edits[0].weightFactor == 1.2 && historical.extrusions[3].ruledFirst.size() == 3,
            "revisioni immutabili conservano nuove funzioni");
    PdfWindow shell;
    shell.viewport_->loadDocument(historical);
    shell.refreshDocumentEditAccess();
    for (const char *name : {"surfaceExtendToAction", "surfaceNetworkAction", "surfacePolesAction"}) {
        const auto *action = shell.findChild<QAction *>(name);
        require(action && !action->isEnabled(), "nuovi comandi protetti in consultazione");
    }
    const int count = shell.viewport_->extrusions_.size();
    require(!shell.viewport_->createBody(poles).isEmpty() && shell.viewport_->extrusions_.size() == count,
            "API mutazione protetta readonly");
    individualFillControls();
    std::cout << "PASS superfici: rete/estensione/poli, dipendenze, rigenerazione, Undo/Redo, UI/mouse, formato53, errori atomici, "
                 "revisioni e sola lettura\n";
}
