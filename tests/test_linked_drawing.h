#pragma once
#include "../cad_drawing_window.h"
#include "test_drawing_workflow.h"
namespace {
void linkedDrawingTests(){
    drawingWorkflowTests();
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    QTemporaryDir temporary;require(temporary.isValid(),"workspace temporaneo tavole");
    const auto sourcePath=temporary.filePath("parte.prt"),drawingPath=temporary.filePath("foglio.drw");
    auto working=clientModel();clientOk(saveDocumentFile(sourcePath,working));
    DrawingFile original;QUuid dimensionId;
    {
        DrawingWindow window;clientOk(window.createFromModel(sourcePath));auto *editor=window.editor();
        DrawingView view;view.plane=Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));
        const auto feature=working.extrusions[0].featureId;
        clientOk(editor->insertView(view,{view.id,"Sezione A-A",QPointF(140,100),{feature}}));
        const auto &pieces=editor->sections(view.id);int chosen=-1;
        for(std::size_t i=0;i<pieces.size();++i)if(std::abs(arcLength(*pieces[i].curve,pieces[i].range)-10)<1e-7){chosen=int(i);break;}
        require(chosen>=0,"curva da 10 mm nel modello collegato");
        for(auto *item:editor->canvas()->scene()->items())if(item->data(0).toString()=="section"&&item->data(2).toInt()==chosen)item->setSelected(true);
        clientOk(editor->addSelectedDimension(DrawingMeasure::EndpointDistance));dimensionId=editor->sheet().drawing.dimensions[0].id;
        clientOk(window.savePath(drawingPath));original=window.document();
        require(!window.isModified()&&original.modelPath=="parte.prt","salva tavola con riferimento relativo");
        const auto bytes=clientRead(drawingPath);require(bytes.size()<4096&&!bytes.contains("forgeBody")&&!bytes.contains("importData"),"file tavola leggero senza snapshot 3D");
        DrawingFile restored;clientOk(decodeDrawingFile(bytes,restored));const auto retained=restored.sheet.drawing.version.documentId;
        require(!decodeDrawingFile(bytes.left(bytes.size()-1),restored).isEmpty()&&restored.sheet.drawing.version.documentId==retained,"tavola troncata rifiutata atomicamente");
        DrawingWindow second;clientOk(second.openPath(drawingPath));require(second.editor()->isReadOnly(),"seconda apertura della stessa tavola in consultazione");
        require(!second.savePath(drawingPath).isEmpty(),"consultazione non sovrascrive la tavola dell'editor");
        const auto copy=temporary.filePath("copia.drw");clientOk(window.savePath(copy));
        require(window.document().sheet.drawing.version.documentId!=retained&&window.document().sheet.drawing.model.documentId==working.version.documentId,"Salva con nome crea tavola indipendente sullo stesso 3D");
        const auto copiedId=window.document().sheet.drawing.version.documentId;
        require(!window.savePath(temporary.filePath("assente/copia.drw")).isEmpty()&&window.document().sheet.drawing.version.documentId==copiedId&&window.filePath()==canonicalDocumentPath(copy),"salvataggio fallito conserva identita' e percorso canonico");
        require(!window.savePath(sourcePath).isEmpty(),"la tavola non puo' sostituire il modello");
    }
    {
        DrawingWindow window;clientOk(window.openPath(drawingPath));
        require(!window.editor()->isReadOnly()&&std::abs(window.editor()->dimensionResult(dimensionId).value-10)<1e-7,"apertura tavola da sorgente esterna");
        const auto preview=qEnvironmentVariable("FORGECAD_LINKED_DRAWING_PREVIEW");if(!preview.isEmpty()){window.show();QApplication::processEvents();require(window.grab().save(preview),"anteprima finestra tavola");}
    }
    // La cache 3D non influisce sull'impronta e puo' essere ricostruita da parametri.
    clientOk(saveDocumentFile(sourcePath,working,false));
    {DrawingWindow window;clientOk(window.openPath(drawingPath));require(std::abs(window.editor()->dimensionResult(dimensionId).value-10)<1e-7,"modello senza cache ricostruito senza tassellazione");}
    auto other=working;forkDocumentIdentity(other);const auto otherPath=temporary.filePath("altro.prt");clientOk(saveDocumentFile(otherPath,other));
    auto changed=working;changed.extrusions[0].primitive.size[0]=14;changed.extrusions[0].cachedGeometry=false;
    {CadViewport viewport;viewport.loadDocument(changed);changed=viewport.currentDocument();}clientOk(saveDocumentFile(sourcePath,changed));
    {
        DrawingWindow window;clientOk(window.openPath(drawingPath));
        require(window.editor()->isReadOnly()&&std::isnan(window.editor()->dimensionResult(dimensionId).value),"modello cambiato non aggiorna quote silenziosamente");
        require(!window.updateFromSource(otherPath).isEmpty()&&window.editor()->isReadOnly(),"modello con UUID diverso rifiutato");
        clientOk(window.updateFromSource());require(window.isModified()&&!window.editor()->isReadOnly()&&std::abs(window.editor()->dimensionResult(dimensionId).value-14)<1e-7,"aggiornamento esplicito a 14 mm");
        clientOk(window.savePath(drawingPath));
    }
    require(QFile::rename(sourcePath,temporary.filePath("rinominata.prt")),"sposta solo modello");
    {
        DrawingWindow window;clientOk(window.openPath(drawingPath));require(window.editor()->isReadOnly(),"metadati consultabili con sorgente assente");
        require(std::isnan(window.editor()->dimensionResult(dimensionId).value),"nessuna misura precedente mostrata come valida");
        clientOk(window.updateFromSource(temporary.filePath("rinominata.prt")));clientOk(window.savePath(drawingPath));
        require(window.document().modelPath=="rinominata.prt","rilocalizzazione conserva identita' e salva nuovo percorso");
    }
    const auto moved=temporary.path()+"-trasferito";require(QDir().rename(temporary.path(),moved),"sposta workspace intero");
    {
        DrawingWindow window;clientOk(window.openPath(QDir(moved).filePath("foglio.drw")));
        require(std::abs(window.editor()->dimensionResult(dimensionId).value-14)<1e-7,"modello e tavola si riaprono dopo spostamento cartella");
    }
    // Revisione del modello fissata: l'attuale parte da 14 non sostituisce il check-in da 10.
    const auto movedSource=QDir(moved).filePath("rinominata.prt");WorkspacePaths paths;clientOk(locateDocumentWorkspace(movedSource,paths));
    RevisionStore store(paths.revisions);NativeRevisionSession session;clientOk(beginNativeCheckout(store,working,"Parte","progettista",session));
    RevisionCheckinRequest request;clientOk(prepareNativeCheckin(working,session,"A","",request));RevisionRecord record;
    clientOk(publishNativeCheckin(store,request,working,session,record));DocumentState archived;clientOk(readNativeRevision(store,record.version,archived));
    auto pinned=original;pinned.modelPath="rinominata.prt";pinned.sheet.drawing.model=record.version;pinned.modelArchivePath=".forgecad/revisions";
    clientOk(store.archiveIdentity(pinned.modelArchiveId));clientOk(drawingModelDefinitionHash(archived,pinned.modelDefinitionHash));
    const auto pinnedPath=QDir(moved).filePath("storica.drw");clientOk(saveDrawingFile(pinnedPath,pinned));
    {DrawingWindow window;clientOk(window.openPath(pinnedPath));require(std::abs(window.editor()->dimensionResult(dimensionId).value-10)<1e-7,"tavola fissata legge check-in preciso dall'archivio");}
    auto wrongArchive=pinned;wrongArchive.modelArchiveId=QUuid::createUuid();DocumentState output;QString resolved;bool different=false;
    require(!loadDrawingModel(pinnedPath,wrongArchive,output,resolved,different).isEmpty(),"archivio con identita' diversa rifiutato");
    auto historical=pinned;historical.sheet.drawing.version.checkinId=QUuid::createUuid();QByteArray bytes;clientOk(encodeDrawingFile(historical,bytes));
    const auto archivedPath=QDir(moved).filePath("archiviata.drw");clientWrite(archivedPath,bytes);
    {DrawingWindow window;clientOk(window.openPath(archivedPath));require(window.editor()->isReadOnly()&&!window.savePath(archivedPath).isEmpty(),"tavola archiviata in sola lettura");
     clientOk(window.savePath(QDir(moved).filePath("copia-storica.drw")));require(!window.editor()->isReadOnly()&&window.document().sheet.drawing.version.checkinId.isNull(),"copia indipendente di tavola archiviata modificabile");}
    const auto marker=QDir(paths.metadata).filePath("workspace.json");const auto before=clientRead(QDir(moved).filePath("foglio.drw"));
    const auto markerBytes=clientRead(marker);clientWrite(marker,"corrotto");DrawingFile current;clientOk(loadDrawingFile(QDir(moved).filePath("foglio.drw"),current));
    require(!saveDrawingFile(QDir(moved).filePath("foglio.drw"),current).isEmpty()&&clientRead(QDir(moved).filePath("foglio.drw"))==before,"workspace corrotto non sostituisce tavola esistente");
    clientWrite(marker,markerBytes);require(QDir(moved).removeRecursively(),"pulizia solo fixture temporanee");
}
}
