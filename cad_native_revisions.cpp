#include "cad_native_revisions.h"
#include "cad_element_names.h"
#include "cad_feature_dependencies.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace ForgeCad {
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::invalid_argument(message); }
void ok(const QString &error) { if (!error.isEmpty()) throw std::runtime_error(error.toStdString()); }
bool sameCheckout(const RevisionCheckout &a, const RevisionCheckout &b) {
    return a.documentId == b.documentId && a.token == b.token && a.baseCheckinId == b.baseCheckinId && a.owner == b.owner;
}
void validateWorking(const DocumentState &working, const RevisionCheckout &checkout) {
    require(!checkout.documentId.isNull() && !checkout.token.isNull() && !checkout.owner.trimmed().isEmpty(), "Prenotazione nativa non valida.");
    require(working.version.documentId == checkout.documentId && working.version.checkinId.isNull()
            && working.baseCheckinId == checkout.baseCheckinId, "Documento e prenotazione non corrispondono.");
}
void validateArchive(RevisionStore &store, const NativeRevisionSession &session) {
    QUuid id; ok(store.archiveIdentity(id));
    require(!session.archiveId.isNull() && session.archiveId == id, "La sessione appartiene a un altro archivio.");
}
void validateSnapshot(const DocumentState &document) {
    for (const auto &body : document.extrusions)
        require(isCurveBody(body) || isDatumBody(body) || body.suppressed || body.forgeBody,
                "Geometria del documento non disponibile: rigenerare prima del check-in.");
}
QByteArray definitionPart(const QByteArray &bytes) {
    QDataStream in(bytes); in.skipRawData(4); quint16 version = 0; in >> version;
    if (version != kDocumentFormatVersion) {
        // Un pendente conserva il payload immutabile della versione che lo
        // ha preparato. Confronta la sua definizione migrata con la copia
        // corrente, senza riscrivere payload, UUID o impronta della richiesta.
        DocumentState archived;
        ok(decodeDocument(bytes, archived));
        QByteArray definition;
        ok(encodeDocument(archived, definition, false));
        return definition;
    }
    in.skipRawData(1); quint32 size = 0; in >> size;
    require(in.status() == QDataStream::Ok && bytes.size() >= 11 && qsizetype(size) <= bytes.size()-11,
            "Snapshot nativo incompleto.");
    return bytes.left(11+qsizetype(size));
}
QString uuidText(const QUuid &id) { return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces); }
QUuid readUuid(const QJsonValue &value, bool optional = false) {
    require(value.isString(), "Identita' dell'operazione assente.");
    if (optional && value.toString().isEmpty()) return {};
    const QUuid id(value.toString()); require(!id.isNull(), "Identita' dell'operazione non valida."); return id;
}
void validatePrepared(const NativeRevisionSession &session, const RevisionCheckinRequest &request) {
    require(!session.archiveId.isNull() && !session.archiveDirectory.isEmpty() && session.archiveDirectory.size() <= 4096,
            "Identita' o percorso dell'archivio non valido.");
    require(!request.checkinId.isNull() && sameCheckout(session.checkout,request.checkout), "Richiesta di un'altra prenotazione.");
    require(request.technicalRevision.size() <= 128 && !request.technicalRevision.trimmed().isEmpty()
            && request.comment.size() <= 16384 && request.checkout.owner.size() <= 256,
            "Metadati della richiesta non validi.");
    require(request.dependencies.isEmpty(), "Dipendenze native esterne non ancora supportate dal ponte.");
    DocumentState archived; ok(decodeDocument(request.payload,archived));
    require(archived.version.documentId == request.checkout.documentId && archived.version.checkinId == request.checkinId
            && archived.baseCheckinId == request.checkout.baseCheckinId, "Snapshot e richiesta di check-in non corrispondono.");
    require(!request.checkout.token.isNull() && !request.checkout.owner.trimmed().isEmpty(), "Prenotazione incompleta.");
    require(request.checkout.createdUtc.isValid() && request.checkout.createdUtc.offsetFromUtc() == 0, "Data della prenotazione non valida.");
    validateSnapshot(archived);
}
}

QString beginNativeCheckout(RevisionStore &store, const DocumentState &working, const QString &name,
                           const QString &owner, NativeRevisionSession &session) {
    try {
        require(!working.version.documentId.isNull() && working.version.checkinId.isNull(), "Serve una copia di lavoro del documento.");
        NativeRevisionSession prepared;
        ok(store.archiveIdentity(prepared.archiveId)); prepared.archiveDirectory = store.directory();
        ok(store.registerDocument(working.version.documentId,name,QStringLiteral("model")));
        ok(store.checkoutAtVersion({working.version.documentId,working.baseCheckinId},owner,prepared.checkout));
        session = std::move(prepared); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString resumeNativeCheckout(RevisionStore &store, const DocumentState &working, NativeRevisionSession &session) {
    try {
        validateArchive(store,session); validateWorking(working,session.checkout);
        RevisionCheckout checkout; ok(store.resumeCheckout(session.checkout.documentId,session.checkout.token,checkout));
        require(sameCheckout(checkout,session.checkout), "La prenotazione archiviata e' diversa da quella della copia di lavoro.");
        session.checkout = checkout; session.archiveDirectory = store.directory(); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString readNativeRevision(RevisionStore &store, const DocumentVersion &version, DocumentState &archived) {
    try {
        QByteArray bytes; ok(store.read(version,bytes)); DocumentState prepared; ok(decodeDocument(bytes,prepared));
        require(prepared.version.documentId == version.documentId && prepared.version.checkinId == version.checkinId,
                "Identita' nativa diversa dal record dell'archivio.");
        validateSnapshot(prepared); archived = std::move(prepared); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString checkoutNativeRevision(RevisionStore &store, const DocumentVersion &version, const QString &owner,
                               DocumentState &working, NativeRevisionSession &session) {
    DocumentState prepared; const auto error = readNativeRevision(store,version,prepared);
    if (!error.isEmpty()) return error;
    prepared.version.checkinId = {}; prepared.baseCheckinId = version.checkinId;
    NativeRevisionSession reserved;
    const auto checkoutError = beginNativeCheckout(store,prepared,QStringLiteral("Modello"),owner,reserved);
    if (!checkoutError.isEmpty()) return checkoutError;
    working = std::move(prepared); session = std::move(reserved); return {};
}

QString prepareNativeCheckin(const DocumentState &working, const NativeRevisionSession &session,
                            const QString &technicalRevision, const QString &comment, RevisionCheckinRequest &request) {
    try {
        validateWorking(working,session.checkout); validateSnapshot(working);
        RevisionCheckinRequest prepared; prepared.checkout = session.checkout; prepared.checkinId = QUuid::createUuid();
        prepared.technicalRevision = technicalRevision; prepared.comment = comment;
        DocumentState archived = working; archived.version.checkinId = prepared.checkinId;
        ok(encodeDocument(archived,prepared.payload,true)); validatePrepared(session,prepared);
        request = std::move(prepared); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString publishNativeCheckin(RevisionStore &store, const RevisionCheckinRequest &request,
                            DocumentState &working, NativeRevisionSession &session, RevisionRecord &record) {
    try {
        validateArchive(store,session);
        require(working.version.documentId == request.checkout.documentId && working.version.checkinId.isNull(), "Documento diverso dalla richiesta pendente.");
        // Ricevuta gia' applicata: non toccare una prenotazione successiva.
        if (working.baseCheckinId == request.checkinId) {
            RevisionRecord receipt; ok(store.checkin(request,receipt)); record = std::move(receipt); return {};
        }
        validateWorking(working,session.checkout); validatePrepared(session,request);
        DocumentState expected = working; expected.version.checkinId = request.checkinId;
        QByteArray encoded; ok(encodeDocument(expected,encoded,false));
        require(encoded == definitionPart(request.payload), "Il documento e' cambiato dopo la preparazione del check-in.");
        RevisionRecord receipt; ok(store.checkin(request,receipt));
        require(receipt.version.documentId == working.version.documentId && receipt.version.checkinId == request.checkinId,
                "Ricevuta del check-in incoerente.");
        working.baseCheckinId = receipt.version.checkinId;
        session.checkout = {}; session.archiveDirectory = store.directory(); record = std::move(receipt); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString encodeNativeRevisionOperation(const NativeRevisionSession &session, const RevisionCheckinRequest &request, QByteArray &bytes) {
    try {
        validatePrepared(session,request);
        const auto metadata = QJsonDocument(QJsonObject{{"archiveId",uuidText(session.archiveId)}, {"directory",session.archiveDirectory},
            {"documentId",uuidText(request.checkout.documentId)}, {"token",uuidText(request.checkout.token)},
            {"base",uuidText(request.checkout.baseCheckinId)}, {"owner",request.checkout.owner},
            {"createdUtc",request.checkout.createdUtc.toUTC().toString(Qt::ISODateWithMs)},
            {"checkinId",uuidText(request.checkinId)}, {"technicalRevision",request.technicalRevision}, {"comment",request.comment},
            {"sha256",QString::fromLatin1(QCryptographicHash::hash(request.payload,QCryptographicHash::Sha256).toHex())}}).toJson(QJsonDocument::Compact);
        require(metadata.size() <= 128*1024 && request.payload.size() <= 256*1024*1024, "Operazione oltre i limiti del prototipo.");
        QByteArray prepared; QDataStream out(&prepared,QIODevice::WriteOnly);
        out.writeRawData("FCADOP01",8); out << quint32(metadata.size()); out.writeRawData(metadata.constData(),int(metadata.size()));
        out << quint32(request.payload.size()); out.writeRawData(request.payload.constData(),int(request.payload.size()));
        require(out.status() == QDataStream::Ok, "Codifica dell'operazione fallita.");
        bytes = std::move(prepared); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString decodeNativeRevisionOperation(const QByteArray &bytes, NativeRevisionSession &session, RevisionCheckinRequest &request) {
    try {
        require(bytes.startsWith("FCADOP01") && bytes.size() >= 16 && bytes.size() <= 256*1024*1024+128*1024+16,
                "Contenitore dell'operazione non valido.");
        QDataStream in(bytes); in.skipRawData(8); quint32 metadataSize = 0; in >> metadataSize;
        require(metadataSize <= 128*1024 && qsizetype(metadataSize) <= bytes.size()-16, "Metadati dell'operazione troncati.");
        const auto metadata = QJsonDocument::fromJson(bytes.mid(12,metadataSize)); require(metadata.isObject(), "Metadati dell'operazione non validi.");
        in.skipRawData(int(metadataSize)); quint32 payloadSize = 0; in >> payloadSize;
        require(in.status() == QDataStream::Ok && payloadSize <= 256*1024*1024 && qsizetype(payloadSize) == bytes.size()-16-metadataSize,
                "Contenuto dell'operazione troncato o eccedente.");
        const auto o = metadata.object(); NativeRevisionSession prepared; RevisionCheckinRequest operation;
        prepared.archiveId = readUuid(o["archiveId"]);
        require(o["directory"].isString() && o["owner"].isString() && o["technicalRevision"].isString()
                && o["comment"].isString() && o["createdUtc"].isString() && o["sha256"].isString(), "Metadati dell'operazione incompleti.");
        prepared.archiveDirectory = o["directory"].toString();
        prepared.checkout = {readUuid(o["documentId"]),readUuid(o["token"]),readUuid(o["base"],true),o["owner"].toString(),
            QDateTime::fromString(o["createdUtc"].toString(),Qt::ISODateWithMs)};
        require(prepared.checkout.createdUtc.isValid() && prepared.checkout.createdUtc.offsetFromUtc() == 0, "Data dell'operazione non valida.");
        operation.checkout = prepared.checkout; operation.checkinId = readUuid(o["checkinId"]);
        operation.technicalRevision = o["technicalRevision"].toString(); operation.comment = o["comment"].toString();
        operation.payload = bytes.right(payloadSize);
        require(o["sha256"].toString().toLatin1() == QCryptographicHash::hash(operation.payload,QCryptographicHash::Sha256).toHex(),
                "Contenuto dell'operazione danneggiato.");
        validatePrepared(prepared,operation); session = std::move(prepared); request = std::move(operation); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString drawingSnapshotOfDocument(const DocumentState &document, const QVector<quint64> &features, DrawingModelSnapshot &snapshot) {
    try {
        require(!document.version.documentId.isNull() && !features.isEmpty(), "Selezionare le feature del documento da rappresentare.");
        // Anche i B-rep letti dallo snapshot devono avere la stessa genealogia
        // della rigenerazione: registrare gli operandi prima del risultato.
        for (int i = 0; i < document.extrusions.size(); ++i)
            registerFeatureGenealogy(document.extrusions[i], i, document.sketches, document.extrusions);
        DrawingModelSnapshot prepared; prepared.version = document.version; QSet<quint64> selected;
        for (quint64 id : features) {
            require(id && !selected.contains(id), "Feature selezionata nulla o duplicata."); selected.insert(id);
            int found = -1;
            for (int i=0;i<document.extrusions.size();++i) if (document.extrusions[i].featureId == id) {
                require(found < 0, "Identita' feature duplicata nel documento."); found = i;
            }
            require(found >= 0 && document.extrusions[found].forgeBody, "Feature selezionata senza B-rep.");
            const auto &feature = document.extrusions[found];
            registerFeatureGenealogy(feature,found,document.sketches,document.extrusions);
            const auto names = elementNamesOf(*feature.forgeBody); require(bool(names), "Nomi della feature non disponibili.");
            prepared.bodies.push_back({id,feature.forgeBody,names});
        }
        snapshot = std::move(prepared); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}
}
