#include "cad_revision_client_internal.h"

namespace ForgeCad {
namespace {
using namespace RevisionClientDetail;
bool finish(const Context &c,DocumentState &working,RevisionStore &store,RevisionRecord &record,bool bodies,
            const DocumentFileLock *lock) {
    NativeRevisionSession session;RevisionCheckinRequest request;
    ok(decodeNativeRevisionOperation(readFile(c.pendingPath,256*1024*1024+128*1024+16),session,request));
    require(request.checkout.documentId==working.version.documentId
            && (working.baseCheckinId==request.checkout.baseCheckinId || working.baseCheckinId==request.checkinId),
            QStringLiteral("Il check-in pendente appartiene a un'altra copia o base di lavoro."));
    session.archiveDirectory=c.paths.revisions;validateArchive(store,session);
    const auto status=readStatus(c,identityOf(working));
    if(status.hasSession) require(status.session.checkout.token==request.checkout.token
                                  && status.phase!=WorkspaceCheckoutPhase::Canceling,
                                  QStringLiteral("La sessione locale e' diversa dall'operazione pendente."));
    DocumentState archived;ok(decodeDocument(request.payload,archived));
    auto current=working;current.version.checkinId=request.checkinId;current.baseCheckinId=request.checkout.baseCheckinId;
    QByteArray currentDefinition,archivedDefinition;ok(encodeDocument(current,currentDefinition,false));ok(encodeDocument(archived,archivedDefinition,false));
    const bool matches=currentDefinition==archivedDefinition;
    archived.version.checkinId={};
    RevisionRecord receipt;ok(publishNativeCheckin(store,request,archived,session,receipt));
    // La ricevuta aggiorna soltanto la base. Modifiche successive restano locali.
    working.baseCheckinId=receipt.version.checkinId;record=receipt;
    const auto error=saveDocumentFile(c.file,working,bodies,lock);
    require(error.isEmpty(),QStringLiteral("Check-in archiviato; aggiornamento della copia di lavoro da completare: %1").arg(error));
    // La richiesta resta recuperabile anche se il processo si interrompe dopo
    // la rimozione della sessione e prima della rimozione del pendente.
    removeFile(c.sessionPath);removeFile(c.pendingPath);return matches;
}
}
QString workspaceRevisionStatus(const QString &path,const DocumentState &working,WorkspaceRevisionStatus &status) {
    try { const auto c=context(path,identityOf(working));auto prepared=readStatus(c,identityOf(working));status=std::move(prepared);return {}; }
    catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString checkoutWorkspaceDocument(const QString &path,const DocumentState &working,const QString &owner) {
    return RevisionClientDetail::checkoutDocument(path,RevisionClientDetail::identityOf(working),owner,QStringLiteral("model"));
}
QString cancelWorkspaceCheckout(const QString &path,const DocumentState &working) {
    return RevisionClientDetail::cancelDocumentCheckout(path,RevisionClientDetail::identityOf(working));
}
QString checkinWorkspaceDocument(const QString &path,DocumentState &working,const QString &revision,const QString &comment,
                                RevisionRecord &record,bool bodies,const DocumentFileLock *documentLock) {
    try {
        const auto c=context(path,identityOf(working));QLockFile lock(c.lockPath);lock.setStaleLockTime(0);
        require(lock.tryLock(0),QStringLiteral("Operazione sul documento gia' in corso; riprovare."));
        auto status=readStatus(c,identityOf(working));require(!status.hasPending,QStringLiteral("Esiste gia' un check-in pendente: completarlo prima di crearne un altro."));
        require(status.hasSession,QStringLiteral("Eseguire prima il check-out del documento."));
        RevisionStore store(c.paths.revisions);reserve(c,identityOf(working),status,store);
        RevisionCheckinRequest request;ok(prepareNativeCheckin(working,status.session,revision,comment,request));
        QByteArray bytes;ok(encodeNativeRevisionOperation(status.session,request,bytes));writeAtomic(c.pendingPath,bytes);
        finish(c,working,store,record,bodies,documentLock);return {};
    } catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString completeWorkspaceCheckin(const QString &path,DocumentState &working,RevisionRecord &record,bool &matchesWorking,
                                bool bodies,const DocumentFileLock *documentLock) {
    try {
        const auto c=context(path,identityOf(working));QLockFile lock(c.lockPath);lock.setStaleLockTime(0);
        require(lock.tryLock(0),QStringLiteral("Operazione sul documento gia' in corso; riprovare."));
        RevisionStore store(c.paths.revisions);const bool matches=finish(c,working,store,record,bodies,documentLock);
        matchesWorking=matches;return {};
    } catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString workspaceDocumentHistory(const QString &path,const DocumentState &working,QVector<RevisionRecord> &records) {
    return RevisionClientDetail::documentHistory(path,RevisionClientDetail::identityOf(working),records);
}
}
