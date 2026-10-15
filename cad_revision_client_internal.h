#ifndef FORGECAD_REVISION_CLIENT_INTERNAL_H
#define FORGECAD_REVISION_CLIENT_INTERNAL_H
#include "cad_revision_client.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <stdexcept>
// Protocollo comune dei documenti: nessuna costruzione della geometria.
namespace ForgeCad::RevisionClientDetail {
struct Identity {DocumentVersion version;QUuid baseCheckinId;};
inline Identity identityOf(const DocumentState &document){return {document.version,document.baseCheckinId};}
inline void require(bool condition, const QString &message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}
inline void ok(const QString &error) { require(error.isEmpty(),error); }
inline QString text(const QUuid &id) { return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces); }
inline QUuid uuid(const QJsonValue &value, bool optional = false) {
    require(value.isString(),QStringLiteral("Identita' della sessione assente."));
    if (optional && value.toString().isEmpty()) return {};
    const QUuid id(value.toString());require(!id.isNull(),QStringLiteral("Identita' della sessione non valida."));return id;
}
struct Context {
    WorkspacePaths paths;
    QString file, relative, sessionPath, pendingPath, lockPath;
};
inline Context context(const QString &path, const Identity &working) {
    require(!working.version.documentId.isNull() && working.version.checkinId.isNull(),QStringLiteral("Serve una copia di lavoro del documento."));
    Context c;c.file=canonicalDocumentPath(path);ok(locateDocumentWorkspace(c.file,c.paths));
    require(!c.paths.workspaceId.isNull() && QFileInfo(c.file).isFile(),QStringLiteral("Salvare prima il documento nel workspace."));
    c.relative=QDir(c.paths.root).relativeFilePath(c.file);
    const auto name=text(working.version.documentId);
    c.sessionPath=QDir(c.paths.sessions).filePath(name+".json");
    c.pendingPath=QDir(c.paths.pending).filePath(name+".op");
    c.lockPath=QDir(c.paths.sessions).filePath(name+".lock");return c;
}
inline void writeAtomic(const QString &path,const QByteArray &bytes) {
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit(),
            QStringLiteral("Impossibile conservare lo stato dell'operazione: %1").arg(path));
}
inline QByteArray readFile(const QString &path, qint64 maximum) {
    QFile file(path);require(file.open(QIODevice::ReadOnly) && file.size()<=maximum,
                             QStringLiteral("Stato locale assente, illeggibile o oltre i limiti: %1").arg(path));
    const auto bytes=file.readAll();require(bytes.size()==file.size(),QStringLiteral("Lettura dello stato locale incompleta."));return bytes;
}
inline void removeFile(const QString &path) {
    require(!QFileInfo::exists(path) || QFile::remove(path),QStringLiteral("Operazione completata; pulizia dello stato locale da riprovare: %1").arg(path));
}
inline QString phaseName(WorkspaceCheckoutPhase phase) {
    if(phase==WorkspaceCheckoutPhase::Reserving)return QStringLiteral("reserving");
    if(phase==WorkspaceCheckoutPhase::Canceling)return QStringLiteral("canceling");
    return QStringLiteral("active");
}
inline void saveSession(const Context &c,const WorkspaceRevisionStatus &status) {
    const auto &s=status.session;const auto &q=s.checkout;
    const auto bytes=QJsonDocument(QJsonObject{{"format","ForgeCAD.WorkspaceSession"},{"schema",1},
        {"phase",phaseName(status.phase)},{"archiveId",text(s.archiveId)},{"documentId",text(q.documentId)},
        {"token",text(q.token)},{"base",text(q.baseCheckinId)},{"owner",q.owner},
        {"createdUtc",q.createdUtc.toUTC().toString(Qt::ISODateWithMs)},{"workingFile",c.relative}}).toJson(QJsonDocument::Compact);
    writeAtomic(c.sessionPath,bytes);
}
inline WorkspaceRevisionStatus readStatus(const Context &c,const Identity &working) {
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
inline void validateArchive(RevisionStore &store,const NativeRevisionSession &session) {
    QUuid id;ok(store.archiveIdentity(id));require(id==session.archiveId,QStringLiteral("La sessione appartiene a un altro archivio."));
}
inline void reserve(const Context &c,const Identity &working,WorkspaceRevisionStatus &status,RevisionStore &store) {
    require(status.phase!=WorkspaceCheckoutPhase::Canceling,QStringLiteral("Completare prima l'annullamento del check-out."));
    validateArchive(store,status.session);
    if(status.phase==WorkspaceCheckoutPhase::Reserving) {
        require(status.session.checkout.baseCheckinId==working.baseCheckinId,QStringLiteral("La base della copia di lavoro e' cambiata."));
        RevisionCheckout confirmed;ok(store.checkoutWithToken(status.session.checkout,confirmed));status.session.checkout=confirmed;
        status.phase=WorkspaceCheckoutPhase::Active;saveSession(c,status);
    } else {
        require(working.version.documentId==status.session.checkout.documentId && working.baseCheckinId==status.session.checkout.baseCheckinId,
                QStringLiteral("Documento e prenotazione non corrispondono."));
        RevisionCheckout live;ok(store.resumeCheckout(working.version.documentId,status.session.checkout.token,live));
        const auto &expected=status.session.checkout;
        require(live.documentId==expected.documentId && live.token==expected.token && live.baseCheckinId==expected.baseCheckinId && live.owner==expected.owner,
                QStringLiteral("La prenotazione archiviata e' diversa da quella della copia di lavoro."));
        status.session.checkout=live;
    }
}

inline QString checkoutDocument(const QString &path,const Identity &working,const QString &owner,const QString &kind) {
    try {
        const auto c=context(path,working);QLockFile lock(c.lockPath);lock.setStaleLockTime(0);
        require(lock.tryLock(0),QStringLiteral("Operazione sul documento gia' in corso; riprovare."));
        auto status=readStatus(c,working);require(!status.hasPending,QStringLiteral("Completare prima il check-in pendente."));
        RevisionStore store(c.paths.revisions);
        if(!status.hasSession) {
            require(!owner.trimmed().isEmpty() && owner.size()<=256,QStringLiteral("Specificare il proprietario del check-out (massimo 256 caratteri)."));
            ok(store.registerDocument(working.version.documentId,QFileInfo(c.file).completeBaseName(),kind));
            status.hasSession=true;status.phase=WorkspaceCheckoutPhase::Reserving;
            ok(store.archiveIdentity(status.session.archiveId));status.session.archiveDirectory=c.paths.revisions;
            status.session.checkout={working.version.documentId,QUuid::createUuid(),working.baseCheckinId,owner,QDateTime::currentDateTimeUtc()};
            saveSession(c,status);
        }
        require(status.session.checkout.owner==owner,QStringLiteral("La sessione locale appartiene a %1.").arg(status.session.checkout.owner));
        reserve(c,working,status,store);return {};
    } catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
inline QString cancelDocumentCheckout(const QString &path,const Identity &working) {
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
inline QString documentHistory(const QString &path,const Identity &working,QVector<RevisionRecord> &records) {
    try {
        const auto c=context(path,working);RevisionStore store(c.paths.revisions);QVector<RevisionDocumentInfo> documents;ok(store.documents(documents));
        for(const auto &document:documents)if(document.documentId==working.version.documentId)return store.history(working.version.documentId,records);
        records.clear();return {};
    } catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
}
#endif
