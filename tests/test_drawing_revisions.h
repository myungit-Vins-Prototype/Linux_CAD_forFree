#pragma once
#include "test_drawing_model_update.h"
#include "../cad_drawing_revisions.h"
namespace {
ForgeCad::DrawingFile revisionDrawingFixture(const QString &path,const QString &sourcePath,QUuid &dimensionId){
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    DrawingWindow window;clientOk(window.createFromModel(sourcePath));DocumentState source;clientOk(loadDocumentFile(sourcePath,source));
    DrawingView view;view.plane=Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));view.scale=4;
    clientOk(window.editor()->insertView(view,{view.id,"Sezione A-A",QPointF(140,100),{source.extrusions[0].featureId}}));
    const auto &pieces=window.editor()->sections(view.id);int index=-1;
    for(std::size_t i=0;i<pieces.size();++i)if(std::abs(arcLength(*pieces[i].curve,pieces[i].range)-10)<1e-7){index=int(i);break;}
    require(index>=0,"curva associativa nella fixture delle revisioni");
    for(auto *item:window.editor()->canvas()->scene()->items())if(item->data(0).toString()=="section"&&item->data(2).toInt()==index)item->setSelected(true);
    clientOk(window.editor()->addSelectedDimension(DrawingMeasure::EndpointDistance));dimensionId=window.editor()->sheet().drawing.dimensions[0].id;
    clientOk(window.savePath(path));return window.document();
}
void drawingRevisionTests(){
    drawingModelUpdateTests();
    using namespace ForgeCad;
    QTemporaryDir temporary;auto sourcePath=temporary.filePath("parte.prt"),path=temporary.filePath("tavola.drw");
    auto source=clientModel();clientOk(saveDocumentFile(sourcePath,source));WorkspacePaths paths;clientOk(locateDocumentWorkspace(sourcePath,paths));
    clientOk(checkoutWorkspaceDocument(sourcePath,source,"modellatore"));RevisionRecord modelA;
    clientOk(checkinWorkspaceDocument(sourcePath,source,"A","modello originale",modelA));
    QUuid dimension;auto drawing=revisionDrawingFixture(path,sourcePath,dimension);const auto id=drawing.sheet.drawing.version.documentId;
    const auto stem=id.toString(QUuid::WithoutBraces);const auto sessionPath=QDir(paths.sessions).filePath(stem+".json");auto pendingPath=QDir(paths.pending).filePath(stem+".op");
    RevisionRecord record;require(!checkinWorkspaceDrawing(path,drawing,"A","",record).isEmpty(),"tavola senza check-out rifiutata");
    clientOk(checkoutWorkspaceDrawing(path,drawing,"disegnatore"));WorkspaceRevisionStatus status;clientOk(workspaceDrawingRevisionStatus(path,drawing,status));const auto token=status.session.checkout.token;
    require(!checkinWorkspaceDrawing(path,drawing,"A","",record).isEmpty()&&!QFileInfo::exists(pendingPath),"check-in tavola richiede modello fissato prima di creare pendenti");
    clientOk(pinWorkspaceDrawingModel(path,drawing,modelA.version));clientOk(saveDrawingFile(path,drawing));
    DrawingFile reopened;clientOk(loadDrawingFile(path,reopened));clientOk(checkoutWorkspaceDrawing(path,reopened,"disegnatore"));clientOk(workspaceDrawingRevisionStatus(path,reopened,status));
    require(status.session.checkout.token==token,"prenotazione tavola ripresa dopo riapertura con stesso token");
    require(!checkoutWorkspaceDrawing(path,drawing,"altro").isEmpty(),"proprietario diverso rifiutato");
    const auto manual=temporary.filePath("copia-manuale.drw");require(QFile::copy(path,manual),"copia fixture tavola");
    require(!checkoutWorkspaceDrawing(manual,drawing,"disegnatore").isEmpty(),"copia manuale non eredita sessione");
    auto object=QJsonDocument::fromJson(clientRead(sessionPath)).object();object["phase"]="reserving";clientWrite(sessionPath,QJsonDocument(object).toJson());
    clientOk(checkoutWorkspaceDrawing(path,drawing,"disegnatore"));clientOk(workspaceDrawingRevisionStatus(path,drawing,status));require(status.session.checkout.token==token,"riserva ritentata senza duplicazione");
    clientOk(checkinWorkspaceDrawing(path,drawing,"A","prima tavola",record));const auto first=record;
    require(first.sequence==1&&first.dependencies.size()==1&&first.dependencies[0].checkinId==modelA.version.checkinId&&drawing.baseCheckinId==first.version.checkinId,"check-in tavola conserva dipendenza esatta e aggiorna base");
    require(!QFileInfo::exists(sessionPath)&&!QFileInfo::exists(pendingPath),"sessione e pendente rimossi dopo ricevuta");
    RevisionStore store(paths.revisions);DrawingFile archived;clientOk(readDrawingRevision(store,first.version,archived));
    require(archived.sheet.drawing.version.checkinId==first.version.checkinId&&archived.sheet.drawing.model.checkinId==modelA.version.checkinId,"copia storica della tavola autonoma nei metadati");
    auto stale=drawing;stale.baseCheckinId={};require(!checkoutWorkspaceDrawing(path,stale,"disegnatore").isEmpty(),"base obsoleta non prenota tavola");clientOk(cancelWorkspaceDrawingCheckout(path,stale));
    clientOk(checkoutWorkspaceDrawing(path,drawing,"disegnatore"));const auto unsaved=drawing.sheet.title+" modificata";drawing.sheet.title=unsaved;
    clientOk(cancelWorkspaceDrawingCheckout(path,drawing));require(drawing.sheet.title==unsaved,"annullamento check-out conserva modifiche locali");
    const auto makePending=[&](const QString &label){
        clientOk(checkoutWorkspaceDrawing(path,drawing,"disegnatore"));
        QLockFile lock(QDir(paths.revisions).filePath("catalog.lock"));require(lock.tryLock(0),"lock archivio fixture");
        require(!checkinWorkspaceDrawing(path,drawing,label,"interruzione simulata",record).isEmpty()&&QFileInfo::exists(pendingPath),"errore pubblicazione conserva pendente");
        DrawingRevisionOperation operation;clientOk(decodeDrawingRevisionOperation(clientRead(pendingPath),operation));return operation;
    };
    auto pending=makePending("B");pending.request.technicalRevision="A";QByteArray legacyPending;clientOk(encodeDrawingRevisionOperation(pending,legacyPending));clientWrite(pendingPath,legacyPending);const auto originalPending=clientRead(pendingPath);
    require(!cancelWorkspaceDrawingCheckout(path,drawing).isEmpty(),"pendente impedisce cancellazione prenotazione");
    require(!checkinWorkspaceDrawing(path,drawing,"nuovo","",record).isEmpty()&&clientRead(pendingPath)==originalPending,"nuovo tentativo non sovrascrive pendente");
    RevisionRecord published;clientOk(store.checkin(pending.request,published));require(published.technicalRevision=="2","vecchio pendente tavola rinumerato senza perdere UUID o contenuto");drawing.sheet.title="modifica successiva al pendente";bool matches=true;
    clientOk(completeWorkspaceDrawingCheckin(path,drawing,record,matches));require(!matches&&drawing.sheet.title=="modifica successiva al pendente"&&record.version.checkinId==published.version.checkinId,"risposta persa conserva modifiche successive e pubblica snapshot originale");
    clientOk(readDrawingRevision(store,record.version,archived));require(archived.sheet.title==unsaved,"contenuto originale del pendente immutato");
    // Interruzione dopo la rimozione della sessione, prima di eliminare il pendente.
    clientWrite(pendingPath,originalPending);clientOk(completeWorkspaceDrawingCheckin(path,drawing,record,matches));
    QVector<RevisionRecord> history;clientOk(workspaceDrawingHistory(path,drawing,history));require(history.size()==2,"ritentativo senza doppio check-in");
    pending=makePending("C");const auto intact=clientRead(pendingPath);auto damaged=intact;damaged[20]=char(damaged[20]^1);clientWrite(pendingPath,damaged);
    const auto retainedBase=drawing.baseCheckinId;require(!completeWorkspaceDrawingCheckin(path,drawing,record,matches).isEmpty()&&drawing.baseCheckinId==retainedBase,"metadati pendenti corrotti rifiutati prima della pubblicazione");
    clientWrite(pendingPath,intact);clientOk(completeWorkspaceDrawingCheckin(path,drawing,record,matches));
    clientOk(checkoutWorkspaceDrawing(path,drawing,"disegnatore"));
    const auto beforeLocalFailure=drawing.baseCheckinId;
    {DocumentFileLock lock(path);clientOk(lock.acquire());require(!checkinWorkspaceDrawing(path,drawing,"D","ricevuta locale interrotta",record).isEmpty()&&!record.version.checkinId.isNull()&&drawing.baseCheckinId==record.version.checkinId&&drawing.baseCheckinId!=beforeLocalFailure&&QFileInfo::exists(pendingPath),"ricevuta resta recuperabile se il file locale non si puo' salvare");}
    clientOk(completeWorkspaceDrawingCheckin(path,drawing,record,matches));
    pending=makePending("E");const auto moved=temporary.path()+"-spostato";require(QDir().rename(temporary.path(),moved),"sposta workspace con pendente");
    path=QDir(moved).filePath("tavola.drw");sourcePath=QDir(moved).filePath("parte.prt");clientOk(locateDocumentWorkspace(path,paths));
    clientOk(completeWorkspaceDrawingCheckin(path,drawing,record,matches));require(record.version.checkinId==pending.request.checkinId,"pendente recuperato dopo spostamento del workspace");
    RevisionStore movedStore(paths.revisions);clientOk(workspaceDrawingHistory(path,drawing,history));require(history.size()==5,"tutte le copie di check-in conservate");
    // La modifica del 3D non cambia la sorgente dei vecchi check-in della tavola.
    source.extrusions[0].primitive.size[0]=14;source.extrusions[0].cachedGeometry=false;{CadViewport viewport;viewport.loadDocument(source);source=viewport.currentDocument();}
    clientOk(saveDocumentFile(sourcePath,source));clientOk(checkoutWorkspaceDocument(sourcePath,source,"modellatore"));RevisionRecord modelB;clientOk(checkinWorkspaceDocument(sourcePath,source,"B","allungamento",modelB));
    {DrawingWindow historical;clientOk(historical.openRevision(path,first.version));require(historical.editor()->isReadOnly()&&std::abs(historical.editor()->dimensionResult(dimension).value-10)<1e-7,"storico tavola mantiene modello A dopo B");}
    const auto before=drawing.modelDefinitionHash;auto wrong=modelB.version;wrong.documentId=QUuid::createUuid();require(!pinWorkspaceDrawingModel(path,drawing,wrong).isEmpty()&&drawing.modelDefinitionHash==before,"altro modello rifiutato senza mutazioni");
    auto forged=archived;forged.sheet.drawing.version={QUuid::createUuid(),{}};forged.baseCheckinId={};
    clientOk(movedStore.registerDocument(forged.sheet.drawing.version.documentId,"incoerente","drawing"));RevisionCheckinRequest forgedRequest;
    clientOk(movedStore.checkout(forged.sheet.drawing.version.documentId,"test",forgedRequest.checkout));forgedRequest.checkinId=QUuid::createUuid();forged.sheet.drawing.version.checkinId=forgedRequest.checkinId;
    forgedRequest.technicalRevision="X";forgedRequest.dependencies={modelB.version};clientOk(encodeDrawingFile(forged,forgedRequest.payload));RevisionRecord forgedRecord;clientOk(movedStore.checkin(forgedRequest,forgedRecord));
    require(!readDrawingRevision(movedStore,forgedRecord.version,reopened).isEmpty(),"dipendenza del catalogo diversa dal contenuto rifiutata");
    require(QDir(moved).removeRecursively(),"pulizia solo workspace fixture delle revisioni");
}
void drawingRevisionUiTests(){
    using namespace ForgeCad;
    QTemporaryDir temporary;const auto sourcePath=temporary.filePath("parte.prt"),path=temporary.filePath("foglio.drw");
    auto source=clientModel();clientOk(saveDocumentFile(sourcePath,source));clientOk(checkoutWorkspaceDocument(sourcePath,source,"modellatore"));RevisionRecord modelA;
    clientOk(checkinWorkspaceDocument(sourcePath,source,"A","",modelA));QUuid dimension;revisionDrawingFixture(path,sourcePath,dimension);
    DrawingWindow window;clientOk(window.openPath(path));
    bool pinSeen=false;QTimer::singleShot(0,[&]{auto *dialog=window.findChild<QDialog *>("drawingModelRevisionsDialog");if(dialog){dialog->findChild<QTableWidget *>("drawingRevisionTable")->selectRow(0);pinSeen=true;dialog->accept();}});
    window.findChild<QAction *>("drawingPinModelAction")->trigger();require(pinSeen&&window.document().sheet.drawing.model.checkinId==modelA.version.checkinId,"selezione reale della revisione sorgente");
    bool checkoutSeen=false;QTimer::singleShot(0,[&]{auto *dialog=window.findChild<QInputDialog *>();if(dialog){dialog->setTextValue("disegnatore");checkoutSeen=true;dialog->accept();}});
    window.findChild<QAction *>("drawingCheckoutAction")->trigger();require(checkoutSeen,"dialogo reale proprietario check-out tavola");
    bool checkinSeen=false;QTimer::singleShot(0,[&]{auto *dialog=window.findChild<QDialog *>("drawingCheckinDialog");if(dialog){require(dialog->findChild<QLineEdit *>("drawingRevisionLabel")->isReadOnly()&&dialog->findChild<QLineEdit *>("drawingRevisionLabel")->text()=="1","revisione automatica non modificabile nel dialogo tavola");dialog->findChild<QTextEdit *>("drawingRevisionComment")->setPlainText("tavola iniziale");checkinSeen=true;dialog->accept();}});
    window.findChild<QAction *>("drawingCheckinAction")->trigger();require(checkinSeen&&!window.isModified()&&!window.document().baseCheckinId.isNull(),"check-in GUI salva ricevuta della tavola");
    QVector<RevisionRecord> records;clientOk(workspaceDrawingHistory(path,window.document(),records));require(records.size()==1&&records[0].technicalRevision=="1"&&records[0].comment=="tavola iniziale","campi GUI conservati in catalogo");
    source.extrusions[0].primitive.size[0]=14;source.extrusions[0].cachedGeometry=false;{CadViewport viewport;viewport.loadDocument(source);source=viewport.currentDocument();}
    clientOk(saveDocumentFile(sourcePath,source));clientOk(checkoutWorkspaceDocument(sourcePath,source,"modellatore"));RevisionRecord modelB;clientOk(checkinWorkspaceDocument(sourcePath,source,"B","",modelB));
    require(window.editor()->isReadOnly()&&!window.savePath(path).isEmpty(),"check-in tavola blocca salvataggio originale");
    clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));
    clientOk(window.pinModelRevision(modelB.version));require(std::abs(window.editor()->dimensionResult(dimension).value-14)<1e-7,"scelta modello B aggiorna quota a 14");
    clientOk(window.savePath(path));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,{},"tavola allungata"));
    bool historySeen=false;QTimer::singleShot(0,[&]{auto *dialog=window.findChild<QDialog *>("drawingHistoryDialog");if(dialog){auto *table=dialog->findChild<QTableWidget *>("drawingRevisionTable");historySeen=table&&table->rowCount()==2&&table->editTriggers()==QAbstractItemView::NoEditTriggers;table->selectRow(0);dialog->accept();}});
    window.findChild<QAction *>("drawingHistoryAction")->trigger();DrawingWindow *historical=nullptr;
    for(auto *widget:QApplication::topLevelWidgets())if(auto *drawing=dynamic_cast<DrawingWindow *>(widget))if(drawing!=&window&&!drawing->document().sheet.drawing.version.checkinId.isNull())historical=drawing;
    require(historySeen&&historical&&historical->editor()->isReadOnly()&&std::abs(historical->editor()->dimensionResult(dimension).value-10)<1e-7,"apertura GUI storico 1 con modello A");
    require(!historical->findChild<QAction *>("drawingCheckinAction")->isEnabled()&&!historical->savePath(path).isEmpty(),"storico non sovrascrive copia di lavoro");
    const auto preview=qEnvironmentVariable("FORGECAD_DRAWING_REVISIONS_PREVIEW");if(!preview.isEmpty()){QApplication::processEvents();require(historical->grab().save(preview),"anteprima storico tavola");}
    clientOk(historical->savePath(temporary.filePath("copia-storica.drw")));require(!historical->editor()->isReadOnly()&&historical->document().baseCheckinId.isNull(),"Salva con nome storico crea copia indipendente");delete historical;
    require(window.editor()->isReadOnly()&&!window.findChild<QAction *>("drawingWorkingModelAction")->isEnabled(),"sorgente bloccata dopo secondo check-in");
    clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));
    window.findChild<QAction *>("drawingWorkingModelAction")->trigger();require(window.document().sheet.drawing.model.checkinId.isNull()&&window.isModified(),"comando ritorno alla copia di lavoro");
    clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));bool cancelSeen=false;
    QTimer::singleShot(0,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox *>(widget)){box->button(QMessageBox::Yes)->click();cancelSeen=true;}});
    window.findChild<QAction *>("drawingCancelCheckoutAction")->trigger();WorkspaceRevisionStatus status;clientOk(workspaceDrawingRevisionStatus(path,window.document(),status));
    require(cancelSeen&&!status.hasSession&&window.isModified()&&window.editor()->isReadOnly(),"annullamento GUI conserva modifiche non salvate e blocca nuova modifica");
    // La protezione deve sopravvivere a chiusura/riapertura, senza dipendere dal pannello.
    const auto protectedPath=temporary.filePath("protetta.drw");
    {DrawingWindow emitted;clientOk(emitted.createFromModel(sourcePath));clientOk(emitted.savePath(protectedPath));clientOk(emitted.pinModelRevision(modelB.version));
     clientOk(emitted.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));clientOk(emitted.runRevisionAction(DrawingWindow::RevisionAction::Checkin,{},"protetta"));require(emitted.editor()->isReadOnly(),"tavola bloccata subito dopo check-in");}
    {DrawingWindow reopened;clientOk(reopened.openPath(protectedPath));const auto before=clientRead(protectedPath);
     require(reopened.editor()->isReadOnly()&&reopened.windowTitle().contains("sola lettura"),"riapertura tavola check-in in sola lettura");
     require(!reopened.findChild<QAction *>("drawingSaveAction")->isEnabled()&&reopened.findChild<QAction *>("drawingCheckoutAction")->isEnabled(),"sola lettura conserva comando check-out");
     auto block=reopened.editor()->sheet().titleBlock;block.code="vietato";
     require(!reopened.editor()->setPageSettings("vietato",{297,210},block).isEmpty()&&!reopened.editor()->undo().isEmpty()&&!reopened.savePath(protectedPath).isEmpty(),"pagina undo e salvataggio bloccati dopo riapertura");
     require(!reopened.pinModelRevision(modelA.version).isEmpty()&&!reopened.useWorkingModel().isEmpty()&&!reopened.updateFromSource().isEmpty(),"aggiornamento sorgente protetto anche da API");
     DrawingPdfReport report;clientOk(reopened.exportPdfPath(temporary.filePath("protetta.pdf"),report));require(clientRead(protectedPath)==before,"esportazione non modifica tavola protetta");
     auto *dock=reopened.findChild<QDockWidget *>("drawingRevisionDock");dock->hide();clientOk(reopened.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));
     require(!reopened.editor()->isReadOnly()&&reopened.findChild<QAction *>("drawingSaveAction")->isEnabled(),"check-out sblocca senza dipendere dalla visibilità del pannello");
     clientOk(reopened.editor()->setPageSettings("modificata",{297,210},block));clientOk(reopened.savePath(protectedPath));
     clientOk(reopened.runRevisionAction(DrawingWindow::RevisionAction::CancelCheckout));require(reopened.editor()->isReadOnly(),"annulla check-out ripristina blocco");}

}
}
