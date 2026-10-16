#include "cad_drawing_revisions.h"
#include "cad_revision_client_internal.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <algorithm>

namespace ForgeCad {
namespace {
using namespace RevisionClientDetail;
constexpr int maximumOperation=10*1024*1024;
Identity identityOf(const DrawingFile &drawing){return {drawing.sheet.drawing.version,drawing.baseCheckinId};}
bool same(const DocumentVersion &a,const DocumentVersion &b){return a.documentId==b.documentId&&a.checkinId==b.checkinId;}
void validateDependency(RevisionStore &store,const DrawingFile &drawing){
    const auto model=drawing.sheet.drawing.model;
    require(!model.checkinId.isNull(),QStringLiteral("Collegare prima la tavola a un check-in preciso del modello."));
    QUuid archiveId;ok(store.archiveIdentity(archiveId));
    require(archiveId==drawing.modelArchiveId,QStringLiteral("Modello e tavola devono usare lo stesso archivio di workspace."));
    DocumentState snapshot;ok(readNativeRevision(store,model,snapshot));QByteArray hash;ok(drawingModelDefinitionHash(snapshot,hash));
    require(hash==drawing.modelDefinitionHash,QStringLiteral("Il modello archiviato non corrisponde alla definizione usata dalla tavola."));
}
DrawingFile validateOperation(const DrawingRevisionOperation &operation){
    const auto &request=operation.request;const auto &q=request.checkout;
    require(!operation.archiveId.isNull()&&!operation.workingFile.isEmpty()&&operation.workingFile.size()<=4096
            &&!QDir::isAbsolutePath(operation.workingFile)&&QDir::cleanPath(operation.workingFile)==operation.workingFile
            &&operation.workingFile!=".."&&!operation.workingFile.startsWith("../")&&!operation.workingFile.contains(QChar(0)),QStringLiteral("Percorso della copia di lavoro pendente non valido."));
    require(!q.documentId.isNull()&&!q.token.isNull()&&!request.checkinId.isNull()&&!q.owner.trimmed().isEmpty()&&q.owner.size()<=256
            &&q.createdUtc.isValid()&&q.createdUtc.offsetFromUtc()==0,QStringLiteral("Prenotazione della tavola non valida."));
    require(!request.technicalRevision.trimmed().isEmpty()&&request.technicalRevision.size()<=128&&request.comment.size()<=16384,
            QStringLiteral("Metadati del check-in della tavola non validi."));
    DrawingFile snapshot;ok(decodeDrawingFile(request.payload,snapshot));
    require(snapshot.sheet.drawing.version.documentId==q.documentId&&snapshot.sheet.drawing.version.checkinId==request.checkinId
            &&snapshot.baseCheckinId==q.baseCheckinId,QStringLiteral("Snapshot e richiesta della tavola non corrispondono."));
    require(!snapshot.sheet.drawing.model.checkinId.isNull()&&snapshot.modelArchiveId==operation.archiveId&&request.dependencies.size()==1
            &&same(request.dependencies[0],snapshot.sheet.drawing.model),QStringLiteral("Dipendenza del check-in della tavola non valida."));
    return snapshot;
}
bool finish(const Context &c,DrawingFile &drawing,RevisionRecord &record,const DocumentFileLock *lock){
    DrawingRevisionOperation operation;ok(decodeDrawingRevisionOperation(readFile(c.pendingPath,maximumOperation),operation));
    const auto &request=operation.request;
    require(operation.workingFile==c.relative&&request.checkout.documentId==drawing.sheet.drawing.version.documentId
            &&(drawing.baseCheckinId==request.checkout.baseCheckinId||drawing.baseCheckinId==request.checkinId),QStringLiteral("Il pendente appartiene a un'altra copia o base della tavola."));
    RevisionStore store(c.paths.revisions);QUuid archive;ok(store.archiveIdentity(archive));
    require(archive==operation.archiveId,QStringLiteral("Archivio diverso da quello del check-in pendente."));
    const auto status=readStatus(c,identityOf(drawing));
    if(status.hasSession)require(status.session.archiveId==archive&&status.session.checkout.token==request.checkout.token&&status.phase!=WorkspaceCheckoutPhase::Canceling,
                                 QStringLiteral("Sessione diversa dal check-in pendente."));
    const auto archived=validateOperation(operation);validateDependency(store,archived);
    auto current=drawing;current.sheet.drawing.version.checkinId=request.checkinId;current.baseCheckinId=request.checkout.baseCheckinId;
    QByteArray bytes;ok(encodeDrawingFile(current,bytes));const bool matches=bytes==request.payload;
    RevisionRecord receipt;ok(store.checkin(request,receipt));
    drawing.baseCheckinId=receipt.version.checkinId;record=receipt;
    const auto error=saveDrawingFile(c.file,drawing,lock);
    require(error.isEmpty(),QStringLiteral("Check-in archiviato; aggiornamento della tavola locale da completare: %1").arg(error));
    removeFile(c.sessionPath);removeFile(c.pendingPath);return matches;
}
}
QString encodeDrawingRevisionOperation(const DrawingRevisionOperation &operation,QByteArray &bytes){
    try{
        validateOperation(operation);const auto &r=operation.request;const auto &q=r.checkout;
        const auto metadata=QJsonDocument(QJsonObject{{"archiveId",text(operation.archiveId)},{"workingFile",operation.workingFile},
            {"documentId",text(q.documentId)},{"token",text(q.token)},{"base",text(q.baseCheckinId)},{"owner",q.owner},
            {"createdUtc",q.createdUtc.toUTC().toString(Qt::ISODateWithMs)},{"checkinId",text(r.checkinId)},
            {"technicalRevision",r.technicalRevision},{"comment",r.comment}}).toJson(QJsonDocument::Compact);
        require(metadata.size()<=128*1024,QStringLiteral("Metadati del pendente oltre i limiti."));
        QByteArray prepared;QDataStream out(&prepared,QIODevice::WriteOnly);
        out.writeRawData("FCDROP01",8);out<<quint32(metadata.size());out.writeRawData(metadata.constData(),metadata.size());
        out<<quint32(r.payload.size());out.writeRawData(r.payload.constData(),r.payload.size());
        require(out.status()==QDataStream::Ok&&prepared.size()+32<=maximumOperation,QStringLiteral("Pendente della tavola oltre i limiti."));
        prepared+=QCryptographicHash::hash(prepared,QCryptographicHash::Sha256);bytes=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString decodeDrawingRevisionOperation(const QByteArray &bytes,DrawingRevisionOperation &operation){
    try{
        require(bytes.startsWith("FCDROP01")&&bytes.size()>=48&&bytes.size()<=maximumOperation,QStringLiteral("Contenitore pendente della tavola non valido."));
        const auto content=bytes.left(bytes.size()-32);
        require(bytes.right(32)==QCryptographicHash::hash(content,QCryptographicHash::Sha256),QStringLiteral("Pendente della tavola danneggiato."));
        QDataStream in(content);in.skipRawData(8);quint32 size=0;in>>size;
        require(size<=128*1024&&qsizetype(size)<=content.size()-16,QStringLiteral("Metadati del pendente troncati."));
        QJsonParseError parse;const auto metadata=QJsonDocument::fromJson(content.mid(12,size),&parse);
        require(parse.error==QJsonParseError::NoError&&metadata.isObject(),QStringLiteral("Metadati del pendente non validi."));
        in.skipRawData(size);quint32 length=0;in>>length;
        require(in.status()==QDataStream::Ok&&length<=9*1024*1024&&qsizetype(length)==content.size()-16-size,QStringLiteral("Contenuto pendente troncato o eccedente."));
        const auto object=metadata.object();DrawingRevisionOperation prepared;
        prepared.archiveId=uuid(object["archiveId"]);prepared.workingFile=object["workingFile"].toString();auto &r=prepared.request;
        r.checkout={uuid(object["documentId"]),uuid(object["token"]),uuid(object["base"],true),object["owner"].toString(),QDateTime::fromString(object["createdUtc"].toString(),Qt::ISODateWithMs)};
        r.checkinId=uuid(object["checkinId"]);r.technicalRevision=object["technicalRevision"].toString();r.comment=object["comment"].toString();r.payload=content.right(length);
        require(object["comment"].isString(),QStringLiteral("Commento del pendente non valido."));
        DrawingFile snapshot;ok(decodeDrawingFile(r.payload,snapshot));r.dependencies={snapshot.sheet.drawing.model};validateOperation(prepared);
        operation=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString workspaceDrawingRevisionStatus(const QString &path,const DrawingFile &drawing,WorkspaceRevisionStatus &status){
    try{const auto c=context(path,identityOf(drawing));auto prepared=readStatus(c,identityOf(drawing));status=std::move(prepared);return {};}
    catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString checkoutWorkspaceDrawing(const QString &path,const DrawingFile &drawing,const QString &owner){return RevisionClientDetail::checkoutDocument(path,identityOf(drawing),owner,QStringLiteral("drawing"));}
QString cancelWorkspaceDrawingCheckout(const QString &path,const DrawingFile &drawing){return RevisionClientDetail::cancelDocumentCheckout(path,identityOf(drawing));}
QString checkinWorkspaceDrawing(const QString &path,DrawingFile &drawing,const QString &revision,const QString &comment,RevisionRecord &record,const DocumentFileLock *documentLock){
    try{
        const auto c=context(path,identityOf(drawing));QLockFile lock(c.lockPath);lock.setStaleLockTime(0);
        require(lock.tryLock(0),QStringLiteral("Operazione sulla tavola gia' in corso."));
        auto status=readStatus(c,identityOf(drawing));require(!status.hasPending,QStringLiteral("Completare prima il check-in pendente della tavola."));
        require(status.hasSession,QStringLiteral("Eseguire prima il check-out della tavola."));
        RevisionStore store(c.paths.revisions);reserve(c,identityOf(drawing),status,store);validateDependency(store,drawing);
        QString label=revision.trimmed();if(label.isEmpty())ok(store.nextTechnicalRevision(drawing.sheet.drawing.version.documentId,label));
        ok(store.validateTechnicalRevision(drawing.sheet.drawing.version.documentId,label));
        DrawingRevisionOperation operation;operation.archiveId=status.session.archiveId;operation.workingFile=c.relative;
        auto &request=operation.request;request.checkout=status.session.checkout;request.checkinId=QUuid::createUuid();request.technicalRevision=label;request.comment=comment;
        auto snapshot=drawing;snapshot.sheet.drawing.version.checkinId=request.checkinId;request.dependencies={snapshot.sheet.drawing.model};ok(encodeDrawingFile(snapshot,request.payload));
        QByteArray bytes;ok(encodeDrawingRevisionOperation(operation,bytes));writeAtomic(c.pendingPath,bytes);finish(c,drawing,record,documentLock);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString completeWorkspaceDrawingCheckin(const QString &path,DrawingFile &drawing,RevisionRecord &record,bool &matchesWorking,const DocumentFileLock *documentLock){
    try{
        const auto c=context(path,identityOf(drawing));QLockFile lock(c.lockPath);lock.setStaleLockTime(0);
        require(lock.tryLock(0),QStringLiteral("Operazione sulla tavola gia' in corso."));
        const bool matches=finish(c,drawing,record,documentLock);matchesWorking=matches;return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString workspaceDrawingHistory(const QString &path,const DrawingFile &drawing,QVector<RevisionRecord> &records){
    auto identity=identityOf(drawing);identity.version.checkinId={};return RevisionClientDetail::documentHistory(path,identity,records);
}
QString readDrawingRevision(RevisionStore &store,const DocumentVersion &version,DrawingFile &drawing){
    try{
        QByteArray bytes;ok(store.read(version,bytes));DrawingFile prepared;ok(decodeDrawingFile(bytes,prepared));
        require(same(prepared.sheet.drawing.version,version),QStringLiteral("Identita' della tavola diversa dal record archiviato."));
        QVector<RevisionRecord> records;ok(store.history(version.documentId,records));
        const auto found=std::find_if(records.begin(),records.end(),[&](const auto &record){return same(record.version,version);});
        require(found!=records.end()&&found->dependencies.size()==1&&same(found->dependencies[0],prepared.sheet.drawing.model),QStringLiteral("Dipendenze della tavola diverse dal record archiviato."));
        validateDependency(store,prepared);drawing=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString pinWorkspaceDrawingModel(const QString &path,DrawingFile &drawing,const DocumentVersion &modelVersion){
    try{
        const auto c=context(path,identityOf(drawing));
        require(modelVersion.documentId==drawing.sheet.drawing.model.documentId&&!modelVersion.checkinId.isNull(),QStringLiteral("Selezionare un check-in del modello della tavola."));
        RevisionStore store(c.paths.revisions);DocumentState snapshot;ok(readNativeRevision(store,modelVersion,snapshot));
        auto prepared=drawing;prepared.sheet.drawing.model=modelVersion;ok(store.archiveIdentity(prepared.modelArchiveId));
        prepared.modelArchivePath=QDir(QFileInfo(c.file).absolutePath()).relativeFilePath(c.paths.revisions);
        ok(drawingModelDefinitionHash(snapshot,prepared.modelDefinitionHash));drawing=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
}
