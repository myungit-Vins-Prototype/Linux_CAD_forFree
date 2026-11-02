// Incluso nel friend ViewportInteractionTest: non cambia permessi o file utente.
static void revisionAccessGuiRegressions() {
    using namespace ForgeCad;
    const auto ok = [](const QString &error) { require(error.isEmpty(), qPrintable(error)); };
    const auto read = [](const QString &path) {
        QFile file(path); require(file.open(QIODevice::ReadOnly), "lettura catalogo temporaneo frontend");
        return file.readAll();
    };
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path); require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "scrittura catalogo temporaneo frontend");
    };
    QTemporaryDir temporary; require(temporary.isValid(), "workspace temporaneo frontend revisioni");
    const auto modelPath = temporary.filePath(QStringLiteral("parte.prt"));
    const auto drawingPath = temporary.filePath(QStringLiteral("tavola.drw"));
    DocumentState model;
    ExtrusionObject box; box.feature = BodyFeature::Primitive;
    box.primitive.size[0] = 10; box.primitive.size[1] = 6; box.primitive.size[2] = 4;
    model.extrusions = {box};
    CadViewport builder; builder.loadDocument(model); ok(saveDocumentFile(modelPath, builder.currentDocument()));
    PdfWindow owner, reader;
    require(owner.openDocumentPath(modelPath), "editor modello della regressione revisioni aperto");
    ok(owner.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout, QStringLiteral("progettista")));
    require(reader.openDocumentPath(modelPath) && reader.viewport_->documentReadOnly() && !reader.documentLock_,
            "secondo frontend modello in consultazione");
    DrawingWindow sheet, sheetReader;
    ok(sheet.createFromModel(modelPath)); ok(sheet.savePath(drawingPath));
    ok(sheet.runRevisionAction(DrawingWindow::RevisionAction::Checkout, QStringLiteral("disegnatore")));
    ok(sheetReader.openPath(drawingPath));
    require(!sheet.editor()->isReadOnly() && sheetReader.editor()->isReadOnly(), "editor e lettore tavola distinti");
    WorkspacePaths workspace; ok(locateDocumentWorkspace(modelPath, workspace));
    RevisionStore archive(workspace.revisions);
    const auto catalogPath = QDir(workspace.revisions).filePath(QStringLiteral("catalog.json"));
    const auto before = read(catalogPath);
    QUuid extra; ok(archive.createDocument(QStringLiteral("Altro documento"), QStringLiteral("model"), extra));
    const auto committed = read(catalogPath);
    const auto *ownedLock = owner.documentLock_.get();
    require(ownedLock && ownedLock->owns(modelPath) && !owner.viewport_->documentReadOnly(), "editor possiede il lock del modello");
    const auto modelReason = owner.documentEditAccess_.reason, readerReason = reader.documentEditAccess_.reason;
    {
        QLockFile publication(QDir(workspace.revisions).filePath(QStringLiteral("catalog.lock")));
        publication.setStaleLockTime(0);
        require(publication.tryLock(0), "lock di pubblicazione del catalogo frontend");
        write(catalogPath, before);
        owner.refreshDocumentEditAccess(); reader.refreshDocumentEditAccess();
        require(owner.documentLock_.get() == ownedLock && owner.documentLock_->owns(modelPath)
                    && !owner.viewport_->documentReadOnly() && owner.documentEditAccess_.reason == modelReason,
                "catalogo transitorio conserva lock e modifica del frontend modello");
        require(reader.viewport_->documentReadOnly() && !reader.documentLock_ && reader.documentEditAccess_.reason == readerReason,
                "catalogo transitorio conserva il frontend modello in consultazione");
        auto changed = owner.viewport_->extrusions_[0]; changed.primitive.size[0] = 11;
        ok(owner.viewport_->updateBody(0, changed));
        require(!reader.viewport_->updateBody(0, changed).isEmpty(), "lettore modello non promosso durante la pubblicazione");
        ok(sheet.renameSheet(0, QStringLiteral("Modifica locale durante pubblicazione")));
        require(!sheet.editor()->isReadOnly() && sheet.document().sheet.title == QStringLiteral("Modifica locale durante pubblicazione"),
                "catalogo transitorio conserva modifica del frontend tavola");
        require(!sheetReader.renameSheet(0, QStringLiteral("Non consentito")).isEmpty() && sheetReader.editor()->isReadOnly(),
                "catalogo transitorio non promuove il lettore tavola");
        DocumentFileLock competingModel(modelPath), competingDrawing(drawingPath);
        require(!competingModel.acquire().isEmpty() && !competingDrawing.acquire().isEmpty(),
                "entrambi i frontend conservano l'esclusiva effettiva durante la pubblicazione");
        write(catalogPath, committed);
    }
    owner.refreshDocumentEditAccess();
    ok(sheet.renameSheet(0, QStringLiteral("Modifica dopo pubblicazione")));
    require(owner.documentLock_.get() == ownedLock && !owner.viewport_->documentReadOnly() && !sheet.editor()->isReadOnly(),
            "ritorno a catalogo coerente conserva gli editor attivi");
    // Un danno stabile resta un errore definitivo: la guardia del transitorio
    // non deve mantenere la modifica quando la verifica del catalogo fallisce.
    write(catalogPath, QByteArrayLiteral("catalogo corrotto"));
    owner.refreshDocumentEditAccess(); reader.refreshDocumentEditAccess();
    require(owner.viewport_->documentReadOnly() && !owner.documentLock_ && reader.viewport_->documentReadOnly(),
            "catalogo corrotto porta i frontend modello alla sola lettura e rilascia il lock");
    require(!sheet.renameSheet(0, QStringLiteral("Modifica vietata")).isEmpty() && sheet.editor()->isReadOnly(),
            "catalogo corrotto blocca il frontend tavola");
    require(!sheetReader.renameSheet(0, QStringLiteral("Modifica vietata")).isEmpty() && sheetReader.editor()->isReadOnly(),
            "catalogo corrotto mantiene il lettore tavola in sola lettura");
    {
        DocumentFileLock releasedModel(modelPath), releasedDrawing(drawingPath);
        ok(releasedModel.acquire()); ok(releasedDrawing.acquire());
    }
    write(catalogPath, committed);
    std::cout << "PASS frontend revisioni: transitorio conserva lock/editor/lettori, corruzione stabile blocca modello e tavola" << std::endl;
}
