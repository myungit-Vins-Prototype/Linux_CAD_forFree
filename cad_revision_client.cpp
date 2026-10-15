#include "cad_revision_client.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <stdexcept>

namespace ForgeCad {
namespace {
void require(bool condition, const QString &message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}
void ok(const QString &error) { require(error.isEmpty(),error); }
QString text(const QUuid &id) { return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces); }
QUuid uuid(const QJsonValue &value, bool optional = false) {
    require(value.isString(),QStringLiteral("Identita' della sessione assente."));
    if (optional && value.toString().isEmpty()) return {};
    const QUuid id(value.toString());require(!id.isNull(),QStringLiteral("Identita' della sessione non valida."));return id;
}
struct Context {
    WorkspacePaths paths;
    QString file, relative, sessionPath, pendingPath, lockPath;
};
Context context(const QString &path, const DocumentState &working) {
    require(!working.version.documentId.isNull() && working.version.checkinId.isNull(),QStringLiteral("Serve una copia di lavoro del documento."));
    Context c;c.file=canonicalDocumentPath(path);ok(locateDocumentWorkspace(c.file,c.paths));
    require(!c.paths.workspaceId.isNull() && QFileInfo(c.file).isFile(),QStringLiteral("Salvare prima il documento nel workspace."));
    c.relative=QDir(c.paths.root).relativeFilePath(c.file);
    const auto name=text(working.version.documentId);
    c.sessionPath=QDir(c.paths.sessions).filePath(name+".json");
    c.pendingPath=QDir(c.paths.pending).filePath(name+".op");
    c.lockPath=QDir(c.paths.sessions).filePath(name+".lock");return c;
}
void writeAtomic(const QString &path,const QByteArray &bytes) {
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit(),
            QStringLiteral("Impossibile conservare lo stato dell'operazione: %1").arg(path));
}
QByteArray readFile(const QString &path, qint64 maximum) {
    QFile file(path);require(file.open(QIODevice::ReadOnly) && file.size()<=maximum,
                             QStringLiteral("Stato locale assente, illeggibile o oltre i limiti: %1").arg(path));
    const auto bytes=file.readAll();require(bytes.size()==file.size(),QStringLiteral("Lettura dello stato locale incompleta."));return bytes;
}
void removeFile(const QString &path) {
    require(!QFileInfo::exists(path) || QFile::remove(path),QStringLiteral("Operazione completata; pulizia dello stato locale da riprovare: %1").arg(path));
}
QString phaseName(WorkspaceCheckoutPhase phase) {
    if(phase==WorkspaceCheckoutPhase::Reserving)return QStringLiteral("reserving");
    if(phase==WorkspaceCheckoutPhase::Canceling)return QStringLiteral("canceling");
    return QStringLiteral("active");
}
void saveSession(const Context &c,const WorkspaceRevisionStatus &status) {
    const auto &s=status.session;const auto &q=s.checkout;
    const auto bytes=QJsonDocument(QJsonObject{{"format","ForgeCAD.WorkspaceSession"},{"schema",1},
        {"phase",phaseName(status.phase)},{"archiveId",text(s.archiveId)},{"documentId",text(q.documentId)},
        {"token",text(q.token)},{"base",text(q.baseCheckinId)},{"owner",q.owner},
        {"createdUtc",q.createdUtc.toUTC().toString(Qt::ISODateWithMs)},{"workingFile",c.relative}}).toJson(QJsonDocument::Compact);
    writeAtomic(c.sessionPath,bytes);
}
WorkspaceRevisionStatus readStatus(const Context &c,const DocumentState &working) {
    WorkspaceRevisionStatus result;result.hasPending=QFileInfo::exists(c.pendingPath);
    if(!QFileInfo::exists(c.sessionPath))return result;
    const auto json=QJsonDocument::fromJson(readFile(c.sessionPath,16*1024));const auto o=json.object();
    require(json.isObject() && o["format"].toString()==QLatin1String("ForgeCAD.WorkspaceSession") && o["schema"].toInt(-1)==1,
            QStringLiteral("Formato della sessione di lavoro non valido."));
    require(o["workingFile"].isString() && o["workingFile"].toString()==c.relative,
            QStringLiteral("La prenotazione appartiene a un'altra copia di lavoro. Usare Salva con nome per creare una copia indipendente."));
    const auto phase=o["phase"].toString();
    require(phase==QLatin1String("reserving") || phase==QLatin1String("active") || phase==QLatin1String("canceling"),
            QStringLiteral("Fase della sessione di lavoro non valida."));
    result.phase=phase==QLatin1String("reserving") ? WorkspaceCheckoutPhase::Reserving
                 : phase==QLatin1String("canceling") ? WorkspaceCheckoutPhase::Canceling : WorkspaceCheckoutPhase::Active;
    result.session.archiveId=uuid(o["archiveId"]);result.session.archiveDirectory=c.paths.revisions;
    result.session.checkout={uuid(o["documentId"]),uuid(o["token"]),uuid(o["base"],true),o["owner"].toString(),
                            QDateTime::fromString(o["createdUtc"].toString(),Qt::ISODateWithMs)};
    const auto &q=result.session.checkout;
    require(q.documentId==working.version.documentId && !q.owner.trimmed().isEmpty() && q.owner.size()<=256
            && q.createdUtc.isValid() && q.createdUtc.offsetFromUtc()==0,QStringLiteral("Metadati della sessione non validi."));
    result.hasSession=true;return result;
}
void validateArchive(RevisionStore &store,const NativeRevisionSession &session) {
    QUuid id;ok(store.archiveIdentity(id));require(id==session.archiveId,QStringLiteral("La sessione appartiene a un altro archivio."));
}
void reserve(const Context &c,const DocumentState &working,WorkspaceRevisionStatus &status,RevisionStore &store) {
    require(status.phase!=WorkspaceCheckoutPhase::Canceling,QStringLiteral("Completare prima l'annullamento del check-out."));
    validateArchive(store,status.session);
    if(status.phase==WorkspaceCheckoutPhase::Reserving) {
        require(status.session.checkout.baseCheckinId==working.baseCheckinId,QStringLiteral("La base della copia di lavoro e' cambiata."));
        RevisionCheckout confirmed;ok(store.checkoutWithToken(status.session.checkout,confirmed));status.session.checkout=confirmed;
        status.phase=WorkspaceCheckoutPhase::Active;saveSession(c,status);
    } else ok(resumeNativeCheckout(store,working,status.session));
}
bool finish(const Context &c,DocumentState &working,RevisionStore &store,RevisionRecord &record,bool bodies,
            const DocumentFileLock *lock) {
    NativeRevisionSession session;RevisionCheckinRequest request;
    ok(decodeNativeRevisionOperation(readFile(c.pendingPath,256*1024*1024+128*1024+16),session,request));
    require(request.checkout.documentId==working.version.documentId
            && (working.baseCheckinId==request.checkout.baseCheckinId || working.baseCheckinId==request.checkinId),
            QStringLiteral("Il check-in pendente appartiene a un'altra copia o base di lavoro."));
    session.archiveDirectory=c.paths.revisions;validateArchive(store,session);
    const auto status=readStatus(c,working);
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
    try { const auto c=context(path,working);auto prepared=readStatus(c,working);status=std::move(prepared);return {}; }
    catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString checkoutWorkspaceDocument(const QString &path,const DocumentState &working,const QString &owner) {
    try {
        const auto c=context(path,working);QLockFile lock(c.lockPath);lock.setStaleLockTime(0);
        require(lock.tryLock(0),QStringLiteral("Operazione sul documento gia' in corso; riprovare."));
        auto status=readStatus(c,working);require(!status.hasPending,QStringLiteral("Completare prima il check-in pendente."));
        RevisionStore store(c.paths.revisions);
        if(!status.hasSession) {
            require(!owner.trimmed().isEmpty() && owner.size()<=256,QStringLiteral("Specificare il proprietario del check-out (massimo 256 caratteri)."));
            ok(store.registerDocument(working.version.documentId,QFileInfo(c.file).completeBaseName(),QStringLiteral("model")));
            status.hasSession=true;status.phase=WorkspaceCheckoutPhase::Reserving;
            ok(store.archiveIdentity(status.session.archiveId));status.session.archiveDirectory=c.paths.revisions;
            status.session.checkout={working.version.documentId,QUuid::createUuid(),working.baseCheckinId,owner,QDateTime::currentDateTimeUtc()};
            saveSession(c,status);
        }
        require(status.session.checkout.owner==owner,QStringLiteral("La sessione locale appartiene a %1.").arg(status.session.checkout.owner));
        reserve(c,working,status,store);return {};
    } catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString cancelWorkspaceCheckout(const QString &path,const DocumentState &working) {
    try {
        const auto c=context(path,working);QLockFile lock(c.lockPath);lock.setStaleLockTime(0);
        require(lock.tryLock(0),QStringLiteral("Operazione sul documento gia' in corso; riprovare."));
        auto status=readStatus(c,working);require(!status.hasPending,QStringLiteral("Completare prima il check-in pendente."));
        require(status.hasSession,QStringLiteral("Nessuna sessione locale da annullare."));
        RevisionStore store(c.paths.revisions);validateArchive(store,status.session);
        status.phase=WorkspaceCheckoutPhase::Canceling;saveSession(c,status);
        RevisionCheckout live;
        const auto error=store.resumeCheckout(working.version.documentId,status.session.checkout.token,live);
        if(error.isEmpty())ok(store.cancelCheckout(live));
        else { // Verifica che l'errore non nasconda un archivio corrotto/assente.
            QVector<RevisionDocumentInfo> documents;ok(store.documents(documents));
        }
        removeFile(c.sessionPath);return {};
    } catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString checkinWorkspaceDocument(const QString &path,DocumentState &working,const QString &revision,const QString &comment,
                                RevisionRecord &record,bool bodies,const DocumentFileLock *documentLock) {
    try {
        const auto c=context(path,working);QLockFile lock(c.lockPath);lock.setStaleLockTime(0);
        require(lock.tryLock(0),QStringLiteral("Operazione sul documento gia' in corso; riprovare."));
        auto status=readStatus(c,working);require(!status.hasPending,QStringLiteral("Esiste gia' un check-in pendente: completarlo prima di crearne un altro."));
        require(status.hasSession,QStringLiteral("Eseguire prima il check-out del documento."));
        RevisionStore store(c.paths.revisions);reserve(c,working,status,store);
        RevisionCheckinRequest request;ok(prepareNativeCheckin(working,status.session,revision,comment,request));
        QByteArray bytes;ok(encodeNativeRevisionOperation(status.session,request,bytes));writeAtomic(c.pendingPath,bytes);
        finish(c,working,store,record,bodies,documentLock);return {};
    } catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString completeWorkspaceCheckin(const QString &path,DocumentState &working,RevisionRecord &record,bool &matchesWorking,
                                bool bodies,const DocumentFileLock *documentLock) {
    try {
        const auto c=context(path,working);QLockFile lock(c.lockPath);lock.setStaleLockTime(0);
        require(lock.tryLock(0),QStringLiteral("Operazione sul documento gia' in corso; riprovare."));
        RevisionStore store(c.paths.revisions);const bool matches=finish(c,working,store,record,bodies,documentLock);
        matchesWorking=matches;return {};
    } catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString workspaceDocumentHistory(const QString &path,const DocumentState &working,QVector<RevisionRecord> &records) {
    try {
        const auto c=context(path,working);RevisionStore store(c.paths.revisions);QVector<RevisionDocumentInfo> documents;ok(store.documents(documents));
        for(const auto &document:documents)if(document.documentId==working.version.documentId)return store.history(working.version.documentId,records);
        records.clear();return {};
    } catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
}
