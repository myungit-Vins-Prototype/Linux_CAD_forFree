#include "cad_drawing_window.h"
#include "cad_drawing_compute.h"
#include "cad_workspace.h"
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QStatusBar>
#include <stdexcept>
namespace ForgeCad {
namespace {
struct RestoreModel {DrawingEditorModel geometry;QUuid base;QString path;};
void restoreOk(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
}
QString DrawingWindow::restoreRevision(const DocumentVersion &version){
    if(!editor_||busy_||editor_->isComputing())return QStringLiteral("Attendere il calcolo in corso.");
    const auto access=requireEditAccess();if(!access.isEmpty())return access;
    if(path_.isEmpty()||!drawing_.sheet.drawing.version.checkinId.isNull()||!lock_||!lock_->owns(path_))return QStringLiteral("Eseguire il check-out della tavola di lavoro prima del ripristino.");
    WorkspaceRevisionStatus status;auto error=workspaceDrawingRevisionStatus(path_,drawing_,status);if(!error.isEmpty())return error;
    if(!status.hasSession||status.hasPending||status.phase!=WorkspaceCheckoutPhase::Active||drawing_.baseCheckinId.isNull())return QStringLiteral("Il ripristino richiede un check-out attivo senza check-in pendenti.");
    if(version.documentId!=drawing_.sheet.drawing.version.documentId||version.checkinId.isNull())return QStringLiteral("Selezionare una revisione della stessa tavola.");
    WorkspacePaths paths;error=locateDocumentWorkspace(path_,paths);if(!error.isEmpty())return error;
    RevisionStore store(paths.revisions);DrawingFile prepared;error=readDrawingRevision(store,version,prepared);if(!error.isEmpty())return error;
    // Recupera il contenuto storico, conservando la base corrente della prenotazione.
    prepared.sheet.drawing.version=drawing_.sheet.drawing.version;prepared.baseCheckinId=drawing_.baseCheckinId;
    if(prepared.sheet.drawing.model.documentId==drawing_.sheet.drawing.model.documentId)prepared.modelPath=drawing_.modelPath;
    prepared.modelArchivePath=QDir(QFileInfo(path_).absolutePath()).relativeFilePath(paths.revisions);synchronizeDrawingFileVersions(prepared);
    const auto active=drawingSheetId(drawing_,activeSheet_);int index=0;
    for(int i=0;i<drawingSheetCount(prepared);++i)if(drawingSheetId(prepared,i)==active){index=i;break;}
    return restoreDrawingState(std::move(prepared),index,QStringLiteral("Ripristina revisione %1").arg(version.checkinId.toString(QUuid::WithoutBraces).left(8)));
}
QString DrawingWindow::restoreDrawingState(DrawingFile prepared,int index,const QString &label,int historyTarget){
    const bool oldBusy=busy_,wasEnabled=isEnabled();busy_=true;setEnabled(false);refreshTitle();
    const auto finish=[&](QString error){busy_=oldBusy;setEnabled(wasEnabled);refreshEditAccess();refreshTitle();return error;};
    try{
        QByteArray bytes;restoreOk(encodeDrawingFile(prepared,bytes));
        if(index<0||index>=drawingSheetCount(prepared))return finish(QStringLiteral("Foglio da ripristinare non valido."));
        RestoreModel loaded;bool computing=false;const auto workingPath=path_;
        const auto error=runDrawingComputation(this,computing,QStringLiteral("Preparazione del ripristino"),loaded,[workingPath,prepared](auto phase){
            DocumentState source;RestoreModel result;bool changed=false;restoreOk(loadDrawingModel(workingPath,prepared,source,result.path,changed));
            if(changed)throw std::runtime_error("Il modello della copia da ripristinare è cambiato: ripristino non applicato.");
            phase(1);restoreOk(prepareDrawingEditorModel(source,result.geometry));result.base=source.baseCheckinId;phase(5);return result;
        });if(!error.isEmpty())return finish(error);
        // Anche un errore o annullamento su un foglio inattivo lascia tutto invariato.
        std::unique_ptr<DrawingSheetEditor> active;
        for(int i=0;i<drawingSheetCount(prepared);++i){auto page=std::make_unique<DrawingSheetEditor>(drawingSheetAt(prepared,i),loaded.geometry);if(i==index)active=std::move(page);}
        const auto history=sheetHistory_;const int cursor=sheetHistoryIndex_;const auto saved=savedDrawing_;
        editor_->setChangedCallback({});drawing_=std::move(prepared);model_=std::move(loaded.geometry);activeSheet_=index;
        modelBaseCheckinId_=loaded.base;sourcePath_=loaded.path;sourceArchivePath_=drawing_.modelArchivePath.isEmpty()?QString():QDir(QFileInfo(path_).absolutePath()).absoluteFilePath(drawing_.modelArchivePath);
        sourceReady_=true;modified_=true;containerDirty_=true;
        // Il vecchio editor resta vivo fino al ritorno dell'eventuale callback Undo.
        installEditor(active.release());sheetHistory_=history;sheetHistoryIndex_=cursor;savedDrawing_=saved;
        if(historyTarget>=0){sheetHistoryIndex_=historyTarget;modified_=bytes!=savedDrawing_;containerDirty_=modified_;}
        else recordSheets(label);
        const auto checkin=drawing_.sheet.drawing.model.checkinId;
        sourceStatus_->setText(checkin.isNull()?QStringLiteral("Sorgente: copia di lavoro salvata %1").arg(sourcePath_):QStringLiteral("Modello fissato al check-in %1").arg(checkin.toString(QUuid::WithoutBraces)));
        editor_->fitSheet();busy_=oldBusy;setEnabled(wasEnabled);refreshPageContext();refreshTitle();
        if(historyTarget<0)statusBar()->showMessage(QStringLiteral("Revisione ripristinata nella copia di lavoro. Undo annulla; Salva e un nuovo check-in archiviano il risultato."),15000);
        return {};
    }catch(const std::exception &error){return finish(QString::fromUtf8(error.what()));}
}
}
