    static void documentConsultation(){
        using namespace ForgeCad;
        QTemporaryDir temporary;require(temporary.isValid(),"workspace consultazione");
        const auto path=temporary.filePath("parte.prt"),drawingPath=temporary.filePath("foglio.drw");
        clientOk(saveDocumentFile(path,clientModel()));
        PdfWindow owner,reader;
        require(owner.openDocumentPath(path)&&!owner.viewport_->documentReadOnly(),"prima apertura senza revisioni modificabile");
        require(reader.openDocumentPath(path)&&reader.viewport_->documentReadOnly()&&!reader.documentLock_,"seconda apertura in consultazione");
        const auto initialBytes=clientRead(path);
        auto changed=reader.viewport_->extrusions()[0];changed.primitive.size[0]=33;
        require(!reader.viewport_->updateBody(0,changed).isEmpty()&&!reader.saveDocumentPath(path),"lettore non modifica ne' salva");
        require(!reader.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"altro").isEmpty(),"check-out rifiutato mentre altra finestra modifica");
        require(clientRead(path)==initialBytes,"tentativi del lettore conservano byte");
        clientOk(owner.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"progettista"));
        WorkspaceRevisionStatus session;clientOk(workspaceRevisionStatus(path,owner.viewport_->currentDocument(),session));const auto token=session.session.checkout.token;
        reader.refreshDocumentEditAccess();require(reader.viewport_->documentReadOnly(),"sessione condivisa non abilita il lettore");
        require(!reader.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::CancelCheckout).isEmpty(),"lettore non annulla prenotazione di un editor attivo");
        PdfWindow manualReader;require(manualReader.openDocumentPath(path)&&manualReader.viewport_->documentReadOnly(),"consultazione da aggiornare esplicitamente");
        changed=owner.viewport_->extrusions()[0];changed.primitive.size[0]=14;clientOk(owner.viewport_->updateBody(0,changed));
        require(owner.saveDocumentPath(path),"salva nuovo contenuto sotto esclusiva");
        require(manualReader.openDocumentPath(path)&&manualReader.viewport_->documentReadOnly()&&!manualReader.documentLock_
                &&manualReader.viewport_->extrusions()[0].primitive.size[0]==14,"riapertura esplicita aggiorna consultazione senza prendere esclusiva");
        clientOk(owner.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkin,{},"prima"));
        require(!owner.documentLock_&&!QFileInfo::exists(path+".lock")&&owner.viewport_->documentReadOnly(),"check-in rilascia esclusiva mantenendo finestra aperta");
        PdfWindow third;require(third.openDocumentPath(path)&&third.viewport_->documentReadOnly()&&!QFileInfo::exists(path+".lock"),"due consultazioni di un check-in senza marker");
        require(reader.viewport_->extrusions()[0].primitive.size[0]==10,"lettore conserva snapshot precedente fino alla ripresa");
        clientOk(reader.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"altro"));
        require(reader.documentLock_&&!reader.viewport_->documentReadOnly()&&reader.viewport_->extrusions()[0].primitive.size[0]==14
                &&reader.viewport_->documentBaseCheckinId()==owner.viewport_->documentBaseCheckinId(),"promozione rilegge contenuto e base sotto esclusiva");
        third.refreshDocumentEditAccess();require(third.viewport_->documentReadOnly()&&!third.documentLock_,"nuovo check-out non promuove altri lettori");
        require(!third.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"altro").isEmpty(),"token e proprietario uguali non superano lock dell'editor");
        clientOk(reader.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::CancelCheckout));
        require(!reader.documentLock_&&reader.viewport_->documentReadOnly(),"annullamento su file revisionato rilascia lock");
        clientOk(owner.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"progettista"));
        clientOk(workspaceRevisionStatus(path,owner.viewport_->currentDocument(),session));const auto resumedToken=session.session.checkout.token;
        owner.newDocument();require(!QFileInfo::exists(path+".lock"),"chiusura copia libera lock conservando check-out");
        PdfWindow reopened;require(reopened.openDocumentPath(path)&&reopened.viewport_->documentReadOnly()&&!reopened.documentLock_,"check-out persistente si riapre in consultazione");
        require(reopened.findChild<QAction *>("revisionCheckoutAction")->isEnabled(),"ripresa esplicita disponibile");
        require(!reopened.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"estraneo").isEmpty()&&!QFileInfo::exists(path+".lock"),"titolare diverso non eredita sessione e non trattiene lock");
        clientOk(reopened.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"progettista"));
        clientOk(workspaceRevisionStatus(path,reopened.viewport_->currentDocument(),session));
        require(session.session.checkout.token==resumedToken&&resumedToken!=token&&!reopened.viewport_->documentReadOnly(),"ripresa conserva token e abilita un solo editor");
        // Le modifiche conservate dopo annullamento non devono cancellare un nuovo salvataggio altrui.
        reopened.viewport_->setLengthUnit(LengthUnit::Inch);
        clientOk(reopened.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::CancelCheckout));
        const auto retained=reopened.viewport_->currentDocument();
        clientOk(third.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"altro"));
        changed=third.viewport_->extrusions()[0];changed.primitive.size[0]=18;clientOk(third.viewport_->updateBody(0,changed));require(third.saveDocumentPath(path),"altro editor salva dopo rilascio");
        clientOk(third.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::CancelCheckout));const auto newerBytes=clientRead(path);
        require(!reopened.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"progettista").isEmpty()
                &&reopened.documentModified_&&reopened.viewport_->lengthUnit()==retained.lengthUnit&&clientRead(path)==newerBytes,"modifiche locali obsolete conservate senza sovrascrivere disco");
        require(reopened.saveDocumentPath(temporary.filePath("copia-locale.prt")),"modifiche obsolete salvabili come copia indipendente");
        // Consultazione ammessa anche quando il lock appartiene a un altro processo.
        {QProcess worker;worker.start(QCoreApplication::applicationFilePath(),{"--document-lock-worker",path});
         require(worker.waitForStarted(5000)&&worker.waitForReadyRead(5000)&&worker.readAllStandardOutput().contains("READY"),"processo indipendente in modifica");
         PdfWindow remote;require(remote.openDocumentPath(path)&&remote.viewport_->documentReadOnly(),"consultazione con editor in altro processo");
         require(!remote.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"altro").isEmpty(),"esclusione di scrittura fra processi");
         worker.write("\n");require(worker.waitForFinished(5000)&&worker.exitCode()==0,"rilascio processo indipendente");}
        // Stesso protocollo per le tavole e ricaricamento delle viste prima della modifica.
        DrawingWindow sheet;clientOk(sheet.createFromModel(path));clientOk(sheet.savePath(drawingPath));
        DrawingWindow sheetReader;clientOk(sheetReader.openPath(drawingPath));require(sheetReader.editor()->isReadOnly(),"seconda apertura tavola in consultazione");
        require(!sheetReader.savePath(drawingPath).isEmpty()&&!sheetReader.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"altro").isEmpty(),"lettore tavola non salva e non prenota copia occupata");
        clientOk(sheet.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));
        // Il check-in della tavola richiede una revisione esatta del modello.
        clientOk(third.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"altro"));
        clientOk(third.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkin,{},"seconda"));
        QVector<RevisionRecord> models;clientOk(workspaceDocumentHistory(path,third.viewport_->currentDocument(),models));clientOk(sheet.pinModelRevision(models.back().version));
        auto block=sheet.editor()->sheet().titleBlock;block.code="NUOVA";clientOk(sheet.editor()->setPageSettings("Foglio nuovo",{297,210},block));
        clientOk(sheet.savePath(drawingPath));clientOk(sheet.runRevisionAction(DrawingWindow::RevisionAction::Checkin,{},"prima tavola"));
        require(sheet.editor()->isReadOnly()&&!QFileInfo::exists(drawingPath+".lock"),"check-in tavola rilascia lock");
        DrawingWindow sheetThird;clientOk(sheetThird.openPath(drawingPath));require(sheetThird.editor()->isReadOnly()&&!QFileInfo::exists(drawingPath+".lock"),"consultazioni tavola revisionata senza marker");
        clientOk(sheetReader.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"altro"));
        require(!sheetReader.editor()->isReadOnly()&&sheetReader.document().sheet.title=="Foglio nuovo"
                &&sheetReader.document().sheet.titleBlock.code=="NUOVA"&&sheetReader.document().baseCheckinId==sheet.document().baseCheckinId,"ripresa tavola aggiorna foglio sorgente e base");
        require(!sheetThird.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"altro").isEmpty()&&sheetThird.editor()->isReadOnly(),"prenotazione condivisa tavola non autorizza secondo editor");
        clientOk(sheetReader.runRevisionAction(DrawingWindow::RevisionAction::CancelCheckout));require(!QFileInfo::exists(drawingPath+".lock"),"annullamento tavola rilascia lock");
        clientOk(sheetReader.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"altro"));
        auto localBlock=sheetReader.editor()->sheet().titleBlock;localBlock.code="LOCALE";
        clientOk(sheetReader.editor()->setPageSettings("Lavoro locale",{297,210},localBlock));
        clientOk(sheetReader.runRevisionAction(DrawingWindow::RevisionAction::CancelCheckout));
        clientOk(sheetThird.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));
        auto otherBlock=sheetThird.editor()->sheet().titleBlock;otherBlock.code="ALTRUI";
        clientOk(sheetThird.editor()->setPageSettings("Salvataggio altrui",{297,210},otherBlock));clientOk(sheetThird.savePath(drawingPath));
        clientOk(sheetThird.runRevisionAction(DrawingWindow::RevisionAction::CancelCheckout));const auto drawingBytes=clientRead(drawingPath);
        require(!sheetReader.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"altro").isEmpty()
                &&sheetReader.isModified()&&sheetReader.document().sheet.title=="Lavoro locale"&&clientRead(drawingPath)==drawingBytes,"tavola locale obsoleta conservata senza sovrascrittura");
        clientOk(sheetReader.savePath(temporary.filePath("foglio-locale.drw")));
        // Metadati non verificabili non impediscono la consultazione e non creano un editor.
        WorkspacePaths workspace;clientOk(locateDocumentWorkspace(path,workspace));
        const auto catalogPath=QDir(workspace.revisions).filePath("catalog.json");const auto catalogBytes=clientRead(catalogPath);
        clientWrite(catalogPath,"catalogo corrotto");
        {PdfWindow guarded;require(guarded.openDocumentPath(path)&&guarded.viewport_->documentReadOnly()&&!guarded.documentLock_,"catalogo corrotto consente sola consultazione del modello");
         DrawingWindow guardedSheet;clientOk(guardedSheet.openPath(drawingPath));require(guardedSheet.editor()->isReadOnly()&&!QFileInfo::exists(drawingPath+".lock"),"catalogo corrotto consente sola consultazione della tavola");}
        clientWrite(catalogPath,catalogBytes);
    }
