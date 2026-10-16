#pragma once
#include "../cad_revision_client.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>

namespace {
void clientOk(const QString &error) { if(!error.isEmpty())throw std::runtime_error(error.toStdString()); }
QByteArray clientRead(const QString &path) { QFile file(path);require(file.open(QIODevice::ReadOnly),"leggi fixture client");return file.readAll(); }
void clientWrite(const QString &path,const QByteArray &bytes) {
    QFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size(),"scrivi fixture client");
}
DocumentState clientModel() {
    DocumentState document;ExtrusionObject box;box.feature=BodyFeature::Primitive;box.featureId=81001;
    box.primitive.kind=PrimitiveKind::Box;box.primitive.size[0]=10;box.primitive.size[1]=6;box.primitive.size[2]=4;
    document.extrusions={box};CadViewport viewport;viewport.loadDocument(document);return viewport.currentDocument();
}
void workspaceRevisionTests() {
    using namespace ForgeCad;
    QTemporaryDir temporary;require(temporary.isValid(),"directory client");
    auto path=temporary.filePath("parte.prt");auto working=clientModel();const auto docId=working.version.documentId;
    WorkspaceRevisionStatus status;WorkspaceEditAccess access;
    clientOk(workspaceDocumentEditAccess({},working.version,{},access));require(!access.readOnly,"nuovo documento modificabile senza workspace");
    clientOk(workspaceDocumentEditAccess(path,working.version,{},access));require(!access.readOnly&&!QDir(temporary.filePath(".forgecad")).exists(),"lettura accesso non crea cartelle");
    require(!workspaceRevisionStatus(path,working,status).isEmpty() && !QDir(temporary.filePath(".forgecad")).exists(),"nessuna creazione prima di Salva");
    clientOk(saveDocumentFile(path,working));WorkspacePaths workspace;clientOk(locateDocumentWorkspace(path,workspace));
    const auto stem=docId.toString(QUuid::WithoutBraces);
    auto sessionPath=QDir(workspace.sessions).filePath(stem+".json");auto pendingPath=QDir(workspace.pending).filePath(stem+".op");
    RevisionRecord record;require(!checkinWorkspaceDocument(path,working,"R00","senza check-out",record).isEmpty(),"check-in richiede prenotazione");
    clientOk(checkoutWorkspaceDocument(path,working,"progettista"));clientOk(workspaceRevisionStatus(path,working,status));
    require(status.hasSession && !status.hasPending && status.phase==WorkspaceCheckoutPhase::Active,"sessione attiva persistente");
    const auto firstCheckout=status.session.checkout;
    clientOk(workspaceDocumentEditAccess(path,working.version,working.baseCheckinId,access));require(!access.readOnly,"check-out verificato sblocca modifica");
    auto object=QJsonDocument::fromJson(clientRead(sessionPath)).object();
    require(object["workingFile"].toString()=="parte.prt" && !object.contains("directory"),"sessione con percorso relativo portabile");
    DocumentState reopened;clientOk(loadDocumentFile(path,reopened));clientOk(checkoutWorkspaceDocument(path,reopened,"progettista"));
    clientOk(workspaceRevisionStatus(path,reopened,status));require(status.session.checkout.token==firstCheckout.token,"ripresa dopo chiusura conserva token");
    require(!checkoutWorkspaceDocument(path,reopened,"altro").isEmpty(),"titolare locale diverso rifiutato");
    const auto copy=temporary.filePath("copia-manuale.prt");require(QFile::copy(path,copy),"copia manuale fixture");
    require(!workspaceRevisionStatus(copy,reopened,status).isEmpty(),"stesso UUID su altro percorso non eredita prenotazione");
    // Risposta persa fra prenotazione e registrazione locale della fase attiva.
    object["phase"]="reserving";clientWrite(sessionPath,QJsonDocument(object).toJson(QJsonDocument::Compact));
    clientOk(workspaceDocumentEditAccess(path,reopened.version,reopened.baseCheckinId,access));require(access.readOnly,"prenotazione incompleta non abilita modifiche");
    clientOk(checkoutWorkspaceDocument(path,reopened,"progettista"));clientOk(workspaceRevisionStatus(path,reopened,status));
    require(status.session.checkout.token==firstCheckout.token && status.phase==WorkspaceCheckoutPhase::Active,"richiesta di check-out ritentata con stesso token");
    auto changed=reopened;changed.lengthUnit=LengthUnit::Inch;
    clientOk(cancelWorkspaceCheckout(path,changed));require(changed.lengthUnit==LengthUnit::Inch && !QFileInfo::exists(sessionPath),"annullamento conserva modifiche locali");
    clientOk(checkoutWorkspaceDocument(path,working,"progettista"));clientOk(workspaceRevisionStatus(path,working,status));
    require(status.session.checkout.token!=firstCheckout.token,"nuovo check-out con nuovo token");
    RevisionStore store(workspace.revisions);require(!store.cancelCheckout(firstCheckout).isEmpty(),"vecchio token non annulla nuova sessione");
    clientOk(checkinWorkspaceDocument(path,working,"R00","prima versione",record));const auto first=record;
    require(first.sequence==1 && first.technicalRevision=="R00" && working.baseCheckinId==first.version.checkinId
            && !QFileInfo::exists(sessionPath) && !QFileInfo::exists(pendingPath),"check-in aggiorna file e pulisce sessione/pendente");
    clientOk(loadDocumentFile(path,reopened));require(reopened.baseCheckinId==first.version.checkinId,"base su disco dopo check-in");
    const auto checkedInBytes=clientRead(path);
    clientOk(workspaceDocumentEditAccess(path,reopened.version,reopened.baseCheckinId,access));require(access.readOnly&&!access.reason.isEmpty(),"riapertura dopo check-in in sola lettura");
    clientOk(workspaceDocumentEditAccess(path,first.version,{},access));require(access.readOnly,"snapshot storico sempre protetto");
    QTemporaryDir detached;const auto detachedPath=detached.filePath("parte.prt");require(QFile::copy(path,detachedPath),"copia di file archiviato senza workspace");
    require(!workspaceDocumentEditAccess(detachedPath,reopened.version,reopened.baseCheckinId,access).isEmpty()&&access.readOnly&&!QDir(detached.filePath(".forgecad")).exists(),"workspace mancante non sblocca una revisione");
    auto unrelated=reopened;forkDocumentIdentity(unrelated);const auto unrelatedPath=temporary.filePath("indipendente.prt");clientOk(saveDocumentFile(unrelatedPath,unrelated));
    clientOk(workspaceDocumentEditAccess(unrelatedPath,unrelated.version,{},access));require(!access.readOnly,"copia indipendente senza check-in modificabile");
    clientOk(checkoutWorkspaceDocument(path,working,"progettista"));clientOk(workspaceRevisionStatus(path,working,status));
    const auto validSession=clientRead(sessionPath);auto forged=QJsonDocument::fromJson(validSession).object();forged["token"]=QUuid::createUuid().toString(QUuid::WithoutBraces);clientWrite(sessionPath,QJsonDocument(forged).toJson());
    require(!workspaceDocumentEditAccess(path,working.version,working.baseCheckinId,access).isEmpty()&&access.readOnly,"token locale non verificato non sblocca modifiche");clientWrite(sessionPath,validSession);
    clientOk(store.cancelCheckout(status.session.checkout));require(!workspaceDocumentEditAccess(path,working.version,working.baseCheckinId,access).isEmpty()&&access.readOnly,"prenotazione cancellata esternamente non eredita diritti");clientOk(cancelWorkspaceCheckout(path,working));
    const auto catalogPath=QDir(workspace.revisions).filePath("catalog.json"),catalog=QString::fromUtf8(clientRead(catalogPath));clientWrite(catalogPath,"corrotto");
    require(!workspaceDocumentEditAccess(path,working.version,working.baseCheckinId,access).isEmpty()&&access.readOnly,"catalogo corrotto impone sola lettura");clientWrite(catalogPath,catalog.toUtf8());
    require(clientRead(path)==checkedInBytes,"controlli accesso non riscrivono il modello");

    const auto preparePending=[&](const QString &revision) {
        clientOk(checkoutWorkspaceDocument(path,working,"progettista"));clientOk(workspaceRevisionStatus(path,working,status));
        RevisionCheckinRequest request;clientOk(prepareNativeCheckin(working,status.session,revision,"interruzione simulata",request));
        QByteArray pending;clientOk(encodeNativeRevisionOperation(status.session,request,pending));clientWrite(pendingPath,pending);return request;
    };
    auto pending=preparePending("R00");const auto exactBytes=clientRead(pendingPath);bool matches=false;
    clientOk(workspaceDocumentEditAccess(path,working.version,working.baseCheckinId,access));require(access.readOnly,"pendente congela modifiche prima del completamento");
    require(!cancelWorkspaceCheckout(path,working).isEmpty(),"annullamento bloccato con operazione pendente");
    require(!checkinWorkspaceDocument(path,working,"nuova","",record).isEmpty() && clientRead(pendingPath)==exactBytes,"nuovo check-in non sovrascrive il pendente");
    clientOk(completeWorkspaceCheckin(path,working,record,matches));
    require(matches && record.sequence==2 && record.technicalRevision=="2" && record.version.checkinId==pending.checkinId,"recupero legacy conserva snapshot/UUID e assegna numero libero");
    pending=preparePending("R02");RevisionRecord published;clientOk(store.checkin(pending,published));
    working.extrusions[0].primitive.size[0]=15;working.extrusions[0].cachedGeometry=false;
    CadViewport viewport;viewport.loadDocument(working);working=viewport.currentDocument();
    clientOk(completeWorkspaceCheckin(path,working,record,matches));
    require(!matches && record.sequence==3 && working.baseCheckinId==published.version.checkinId
            && working.extrusions[0].primitive.size[0]==15,"risposta persa con nuove modifiche: base aggiornata e geometria conservata");
    DocumentState archived;clientOk(readNativeRevision(store,published.version,archived));
    require(archived.extrusions[0].primitive.size[0]==10,"check-in pendente conserva snapshot originale");
    // Interruzione dopo aggiornamento file e rimozione sessione, prima della pulizia pendente.
    QByteArray receiptPending;clientOk(encodeNativeRevisionOperation(status.session,pending,receiptPending));clientWrite(pendingPath,receiptPending);
    clientOk(completeWorkspaceCheckin(path,working,record,matches));
    QVector<RevisionRecord> history;clientOk(store.history(docId,history));require(history.size()==3,"ricevuta ritentata senza doppio record");

    pending=preparePending("R03");const auto moved=temporary.filePath("trasferito");
    require(QDir().mkpath(moved),"destinazione workspace");
    require(QFile::rename(path,QDir(moved).filePath("parte.prt")) && QDir().rename(workspace.metadata,QDir(moved).filePath(".forgecad")),"sposta intero workspace");
    path=QDir(moved).filePath("parte.prt");clientOk(locateDocumentWorkspace(path,workspace));
    sessionPath=QDir(workspace.sessions).filePath(stem+".json");pendingPath=QDir(workspace.pending).filePath(stem+".op");
    clientOk(workspaceRevisionStatus(path,working,status));
    clientOk(completeWorkspaceCheckin(path,working,record,matches));require(matches && record.sequence==4,"ripresa dopo spostamento workspace");
    RevisionStore movedStore(workspace.revisions);
    clientOk(checkoutWorkspaceDocument(path,working,"progettista"));
    {QLockFile catalogLock(QDir(workspace.revisions).filePath("catalog.lock"));require(catalogLock.tryLock(0),"contesa catalogo");
     require(!checkinWorkspaceDocument(path,working,"R04","catalogo occupato",record).isEmpty() && QFileInfo::exists(pendingPath),"errore pubblicazione conserva richiesta");}
    clientOk(completeWorkspaceCheckin(path,working,record,matches));require(record.sequence==5,"ritenta pubblicazione senza duplicati");
    clientOk(checkoutWorkspaceDocument(path,working,"progettista"));
    {DocumentFileLock fileLock(path);clientOk(fileLock.acquire());
     require(!checkinWorkspaceDocument(path,working,"R05","errore file locale",record).isEmpty() && QFileInfo::exists(pendingPath),"risposta archiviata conservata se file locale non scrivibile");}
    const auto committed=record.version.checkinId;clientOk(loadDocumentFile(path,reopened));
    require(reopened.baseCheckinId!=committed,"errore file non sostituisce base su disco");
    clientOk(completeWorkspaceCheckin(path,reopened,record,matches));working=reopened;
    require(working.baseCheckinId==committed && record.sequence==6,"riapertura recupera aggiornamento del file senza nuovo check-in");
    clientOk(checkoutWorkspaceDocument(path,working,"progettista"));clientOk(workspaceRevisionStatus(path,working,status));
    object=QJsonDocument::fromJson(clientRead(sessionPath)).object();object["phase"]="canceling";
    clientWrite(sessionPath,QJsonDocument(object).toJson(QJsonDocument::Compact));clientOk(movedStore.cancelCheckout(status.session.checkout));
    clientOk(cancelWorkspaceCheckout(path,working));require(!QFileInfo::exists(sessionPath),"annullamento ripreso dopo risposta persa");
    pending=preparePending("R06");const auto correct=clientRead(pendingPath);auto corrupt=correct;corrupt[corrupt.size()-1]=char(corrupt.back()^1);
    clientWrite(pendingPath,corrupt);const auto oldBase=working.baseCheckinId;
    require(!completeWorkspaceCheckin(path,working,record,matches).isEmpty() && working.baseCheckinId==oldBase,"pendente corrotto conserva stato e modello");
    clientWrite(pendingPath,correct);clientOk(completeWorkspaceCheckin(path,working,record,matches));
    clientOk(workspaceDocumentHistory(path,working,history));require(history.size()==7,"tutte le copie conservate nel workspace");
}
}
