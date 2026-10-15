#include "cad_revision_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace ForgeCad {
namespace {
constexpr qint64 kMaxCatalogBytes = 16 * 1024 * 1024;
constexpr qint64 kMaxPayloadBytes = 256 * 1024 * 1024;
constexpr qsizetype kMaxDocuments = 10000, kMaxRevisions = 100000, kMaxDependencies = 10000;
struct StoredDocument { RevisionDocumentInfo info; RevisionCheckout checkout; };
struct StoredRevision { RevisionRecord record; QByteArray requestHash; };
struct Catalog {
    QUuid archiveId;
    QVector<StoredDocument> documents;
    QVector<StoredRevision> revisions;
};

void require(bool ok, const QString &message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
QString text(const QUuid &id) { return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces); }
QUuid uuid(const QJsonValue &value, bool optional = false) {
    require(value.isString(), QStringLiteral("Identita' dell'archivio non valida."));
    const QString s = value.toString();
    if (optional && s.isEmpty()) return {};
    const QUuid id(s);
    require(!id.isNull(), QStringLiteral("Identita' nulla o malformata nell'archivio."));
    return id;
}
QString string(const QJsonValue &value, qsizetype limit, bool nonempty = false) {
    require(value.isString(), QStringLiteral("Metadato testuale dell'archivio non valido."));
    const auto s = value.toString();
    require(s.size() <= limit && (!nonempty || !s.trimmed().isEmpty()), QStringLiteral("Metadato testuale vuoto o troppo lungo."));
    return s;
}
QByteArray digest(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
QByteArray hashValue(const QJsonValue &value) {
    const QByteArray hex = string(value, 64, true).toLatin1();
    require(hex.size() == 64 && std::all_of(hex.begin(), hex.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }), QStringLiteral("Impronta SHA256 dell'archivio non valida."));
    return QByteArray::fromHex(hex);
}
QDateTime date(const QJsonValue &value) {
    const auto result = QDateTime::fromString(string(value, 40, true), Qt::ISODateWithMs);
    require(result.isValid() && result.offsetFromUtc() == 0, QStringLiteral("Data UTC dell'archivio non valida."));
    return result.toUTC();
}
QJsonObject versionJson(const DocumentVersion &version) {
    return {{"documentId", text(version.documentId)}, {"checkinId", text(version.checkinId)}};
}
QJsonArray dependenciesJson(const QVector<DocumentVersion> &dependencies) {
    QJsonArray result;
    for (const auto &d : dependencies) result.append(versionJson(d));
    return result;
}
QJsonObject checkoutJson(const RevisionCheckout &checkout) {
    return {{"token", text(checkout.token)}, {"base", text(checkout.baseCheckinId)}, {"owner", checkout.owner},
            {"createdUtc", checkout.createdUtc.toUTC().toString(Qt::ISODateWithMs)}};
}
QJsonObject catalogJson(const Catalog &catalog) {
    QJsonArray documents, revisions;
    for (const auto &d : catalog.documents) {
        QJsonObject entry{{"id", text(d.info.documentId)}, {"name", d.info.name}, {"kind", d.info.kind}, {"head", text(d.info.headCheckinId)}};
        entry["checkout"] = d.checkout.token.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(checkoutJson(d.checkout));
        documents.append(entry);
    }
    for (const auto &stored : catalog.revisions) {
        const auto &r = stored.record;
        revisions.append(QJsonObject{{"version", versionJson(r.version)}, {"previous", text(r.previousCheckinId)},
            {"sequence", QString::number(r.sequence)}, {"technicalRevision", r.technicalRevision}, {"author", r.author},
            {"comment", r.comment}, {"createdUtc", r.createdUtc.toUTC().toString(Qt::ISODateWithMs)},
            {"sha256", QString::fromLatin1(r.sha256.toHex())}, {"size", QString::number(r.payloadSize)},
            {"dependencies", dependenciesJson(r.dependencies)}, {"requestHash", QString::fromLatin1(stored.requestHash.toHex())}});
    }
    return {{"format", "ForgeCAD.RevisionArchive"}, {"schema", 1}, {"archiveId", text(catalog.archiveId)},
            {"documents", documents}, {"revisions", revisions}};
}

Catalog loadCatalog(const QString &directory) {
    require(!directory.isEmpty(), QStringLiteral("Cartella dell'archivio non specificata."));
    QFile file(QDir(directory).filePath("catalog.json"));
    require(file.open(QIODevice::ReadOnly) && file.size() > 0 && file.size() <= kMaxCatalogBytes,
            QStringLiteral("Catalogo revisioni assente, illeggibile o troppo grande."));
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && json.isObject(), QStringLiteral("Catalogo revisioni danneggiato."));
    const auto root = json.object();
    require(root["format"].toString() == QLatin1String("ForgeCAD.RevisionArchive") && root["schema"].toDouble(-1) == 1,
            QStringLiteral("Formato del catalogo revisioni non supportato."));
    require(root["documents"].isArray() && root["revisions"].isArray(), QStringLiteral("Elenchi del catalogo assenti."));
    const auto documents = root["documents"].toArray(), revisions = root["revisions"].toArray();
    require(documents.size() <= kMaxDocuments && revisions.size() <= kMaxRevisions, QStringLiteral("Archivio oltre i limiti del prototipo."));
    Catalog catalog; catalog.archiveId = uuid(root["archiveId"]);
    QSet<QUuid> documentIds, tokens;
    QHash<QUuid, QUuid> heads, knownRevisions;
    QHash<QUuid, quint64> sequences;
    for (const auto &value : documents) {
        require(value.isObject(), QStringLiteral("Documento del catalogo non valido."));
        const auto o = value.toObject(); StoredDocument d;
        d.info.documentId = uuid(o["id"]);
        require(!documentIds.contains(d.info.documentId), QStringLiteral("Documento duplicato nell'archivio."));
        documentIds.insert(d.info.documentId);
        d.info.name = string(o["name"], 4096, true); d.info.kind = string(o["kind"], 128, true);
        d.info.headCheckinId = uuid(o["head"], true);
        require(o["checkout"].isNull() || o["checkout"].isObject(), QStringLiteral("Prenotazione dell'archivio non valida."));
        if (o["checkout"].isObject()) {
            const auto c = o["checkout"].toObject();
            d.checkout = {d.info.documentId, uuid(c["token"]), uuid(c["base"], true), string(c["owner"], 256, true), date(c["createdUtc"])};
            require(!tokens.contains(d.checkout.token) && d.checkout.baseCheckinId == d.info.headCheckinId,
                    QStringLiteral("Prenotazione duplicata o base incoerente."));
            tokens.insert(d.checkout.token); d.info.checkedOut = true; d.info.checkoutOwner = d.checkout.owner;
        }
        catalog.documents.append(std::move(d));
    }
    for (const auto &value : revisions) {
        require(value.isObject(), QStringLiteral("Record di check-in non valido."));
        const auto o = value.toObject(); StoredRevision stored; auto &r = stored.record;
        require(o["version"].isObject(), QStringLiteral("Versione del check-in assente."));
        const auto version = o["version"].toObject(); r.version = {uuid(version["documentId"]), uuid(version["checkinId"])};
        require(documentIds.contains(r.version.documentId) && !knownRevisions.contains(r.version.checkinId),
                QStringLiteral("Documento assente o check-in duplicato nel catalogo."));
        r.previousCheckinId = uuid(o["previous"], true);
        bool ok = false; r.sequence = string(o["sequence"], 20, true).toULongLong(&ok);
        require(ok && r.sequence == sequences.value(r.version.documentId) + 1
                && r.previousCheckinId == heads.value(r.version.documentId), QStringLiteral("Catena dei check-in incoerente."));
        r.technicalRevision = string(o["technicalRevision"], 128, true);
        r.author = string(o["author"], 256, true); r.comment = string(o["comment"], 16384);
        r.createdUtc = date(o["createdUtc"]); r.sha256 = hashValue(o["sha256"]); stored.requestHash = hashValue(o["requestHash"]);
        r.payloadSize = string(o["size"], 20, true).toLongLong(&ok);
        require(ok && r.payloadSize >= 0 && r.payloadSize <= kMaxPayloadBytes, QStringLiteral("Dimensione del contenuto non valida."));
        require(o["dependencies"].isArray() && o["dependencies"].toArray().size() <= kMaxDependencies,
                QStringLiteral("Dipendenze del check-in non valide."));
        QSet<QUuid> dependencies;
        for (const auto &dep : o["dependencies"].toArray()) {
            require(dep.isObject(), QStringLiteral("Dipendenza del check-in non valida."));
            const auto d = dep.toObject(); const DocumentVersion v{uuid(d["documentId"]), uuid(d["checkinId"])};
            require(knownRevisions.contains(v.checkinId) && knownRevisions.value(v.checkinId) == v.documentId
                    && !dependencies.contains(v.checkinId), QStringLiteral("Dipendenza assente, futura o duplicata."));
            dependencies.insert(v.checkinId); r.dependencies.append(v);
        }
        heads[r.version.documentId] = r.version.checkinId; sequences[r.version.documentId] = r.sequence;
        knownRevisions[r.version.checkinId] = r.version.documentId;
        catalog.revisions.append(std::move(stored));
    }
    for (const auto &d : catalog.documents)
        require(d.info.headCheckinId == heads.value(d.info.documentId), QStringLiteral("Testa del documento incoerente con i check-in."));
    return catalog;
}

void saveCatalog(const QString &directory, const Catalog &catalog) {
    require(catalog.documents.size() <= kMaxDocuments && catalog.revisions.size() <= kMaxRevisions,
            QStringLiteral("Archivio oltre i limiti del prototipo."));
    const auto bytes = QJsonDocument(catalogJson(catalog)).toJson(QJsonDocument::Compact);
    require(bytes.size() <= kMaxCatalogBytes, QStringLiteral("Catalogo oltre il limite del prototipo."));
    QSaveFile file(QDir(directory).filePath("catalog.json"));
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            QStringLiteral("Pubblicazione del catalogo revisioni fallita."));
}

QString transaction(const QString &directory, const std::function<bool(Catalog &)> &operation) {
    try {
        require(!directory.isEmpty(), QStringLiteral("Cartella dell'archivio non specificata."));
        QLockFile lock(QDir(directory).filePath("catalog.lock")); lock.setStaleLockTime(0);
        require(lock.tryLock(0), QStringLiteral("Archivio occupato o non scrivibile; riprovare."));
        auto catalog = loadCatalog(directory);
        if (operation(catalog)) saveCatalog(directory, catalog);
        return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}
StoredDocument &findDocument(Catalog &catalog, const QUuid &id) {
    const auto it = std::find_if(catalog.documents.begin(), catalog.documents.end(), [&](const auto &d) { return d.info.documentId == id; });
    require(it != catalog.documents.end(), QStringLiteral("Documento non presente nell'archivio."));
    return *it;
}
bool owns(const StoredDocument &document, const RevisionCheckout &checkout) {
    return !checkout.token.isNull() && document.info.documentId == checkout.documentId
        && document.checkout.token == checkout.token && document.checkout.owner == checkout.owner
        && document.checkout.baseCheckinId == checkout.baseCheckinId && document.info.headCheckinId == checkout.baseCheckinId;
}
QString objectPath(const QString &directory, const QByteArray &sha256) {
    return QDir(directory).filePath("objects/" + QString::fromLatin1(sha256.toHex()) + ".bin");
}
QByteArray readObject(const QString &directory, const RevisionRecord &record) {
    QFile file(objectPath(directory, record.sha256));
    require(file.open(QIODevice::ReadOnly) && file.size() == record.payloadSize, QStringLiteral("Contenuto del check-in assente o troncato."));
    const auto bytes = file.readAll();
    require(bytes.size() == record.payloadSize && digest(bytes) == record.sha256, QStringLiteral("Contenuto del check-in danneggiato."));
    return bytes;
}
QByteArray fingerprint(const RevisionCheckinRequest &r, const QByteArray &sha256) {
    return digest(QJsonDocument(QJsonObject{{"documentId", text(r.checkout.documentId)}, {"token", text(r.checkout.token)},
        {"base", text(r.checkout.baseCheckinId)}, {"owner", r.checkout.owner}, {"checkinId", text(r.checkinId)},
        {"sha256", QString::fromLatin1(sha256.toHex())}, {"technicalRevision", r.technicalRevision}, {"comment", r.comment},
        {"dependencies", dependenciesJson(r.dependencies)}}).toJson(QJsonDocument::Compact));
}
}

RevisionStore::RevisionStore(QString directory)
    : directory_(directory.trimmed().isEmpty() ? QString() : QDir(directory).absolutePath()) {}

QString RevisionStore::initialize() const {
    try {
        require(!directory_.isEmpty(), QStringLiteral("Cartella dell'archivio non specificata."));
        require(QDir().mkpath(directory_), QStringLiteral("Impossibile creare la cartella dell'archivio."));
        QLockFile lock(QDir(directory_).filePath("catalog.lock")); lock.setStaleLockTime(0);
        require(lock.tryLock(0), QStringLiteral("Archivio occupato o non scrivibile; riprovare."));
        if (QFileInfo::exists(QDir(directory_).filePath("catalog.json"))) {
            loadCatalog(directory_);
            require(QDir(QDir(directory_).filePath("objects")).exists(), QStringLiteral("Cartella dei contenuti dell'archivio mancante."));
            return {};
        }
        const auto entries = QDir(directory_).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const auto &entry : entries)
            require(entry == QLatin1String("catalog.lock") || entry == QLatin1String("objects"),
                    QStringLiteral("Cartella non vuota priva di catalogo: inizializzazione annullata."));
        const QDir objects(QDir(directory_).filePath("objects"));
        require(!objects.exists() || objects.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty(),
                QStringLiteral("Contenuti presenti ma catalogo mancante: ripristinare il catalogo, senza inizializzare di nuovo."));
        require(QDir(directory_).mkpath("objects"), QStringLiteral("Impossibile creare la cartella dei contenuti."));
        Catalog catalog; catalog.archiveId = QUuid::createUuid(); saveCatalog(directory_, catalog);
        return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString RevisionStore::createDocument(const QString &name, const QString &kind, QUuid &documentId) const {
    QUuid prepared;
    const auto error = transaction(directory_, [&](Catalog &catalog) {
        string(QJsonValue(name), 4096, true); string(QJsonValue(kind), 128, true);
        StoredDocument d; d.info.documentId = QUuid::createUuid(); d.info.name = name; d.info.kind = kind;
        prepared = d.info.documentId; catalog.documents.append(std::move(d)); return true;
    });
    if (error.isEmpty()) documentId = prepared;
    return error;
}

QString RevisionStore::registerDocument(const QUuid &documentId, const QString &name, const QString &kind) const {
    return transaction(directory_, [&](Catalog &catalog) {
        require(!documentId.isNull(), QStringLiteral("Identita' del documento assente."));
        string(QJsonValue(name), 4096, true); string(QJsonValue(kind), 128, true);
        for (const auto &d : catalog.documents) if (d.info.documentId == documentId) {
            require(d.info.kind == kind, QStringLiteral("Il documento e' gia' registrato con un altro tipo."));
            return false;
        }
        StoredDocument d; d.info.documentId = documentId; d.info.name = name; d.info.kind = kind;
        catalog.documents.append(std::move(d)); return true;
    });
}

QString RevisionStore::archiveIdentity(QUuid &archiveId) const {
    try { const auto catalog = loadCatalog(directory_); archiveId = catalog.archiveId; return {}; }
    catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString RevisionStore::documents(QVector<RevisionDocumentInfo> &result) const {
    try {
        const auto catalog = loadCatalog(directory_); QVector<RevisionDocumentInfo> prepared;
        for (const auto &d : catalog.documents) prepared.append(d.info);
        result = std::move(prepared); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString RevisionStore::history(const QUuid &documentId, QVector<RevisionRecord> &result) const {
    try {
        auto catalog = loadCatalog(directory_); findDocument(catalog, documentId); QVector<RevisionRecord> prepared;
        for (const auto &r : catalog.revisions) if (r.record.version.documentId == documentId) prepared.append(r.record);
        result = std::move(prepared); return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

namespace {
QString reserveDocument(const QString &directory, const QUuid &documentId, const QString &owner,
                        const QUuid *expectedBase, RevisionCheckout &result) {
    RevisionCheckout prepared;
    const auto error = transaction(directory, [&](Catalog &catalog) {
        string(QJsonValue(owner), 256, true);
        auto &d = findDocument(catalog, documentId);
        require(!expectedBase || d.info.headCheckinId == *expectedBase, QStringLiteral("Il documento e' cambiato prima del check-out."));
        require(d.checkout.token.isNull(), QStringLiteral("Documento gia' in check-out da %1.").arg(d.checkout.owner));
        prepared = {documentId, QUuid::createUuid(), d.info.headCheckinId, owner, QDateTime::currentDateTimeUtc()};
        d.checkout = prepared; return true;
    });
    if (error.isEmpty()) result = prepared;
    return error;
}
}

QString RevisionStore::checkout(const QUuid &documentId, const QString &owner, RevisionCheckout &result) const {
    return reserveDocument(directory_, documentId, owner, nullptr, result);
}

QString RevisionStore::checkoutAtVersion(const DocumentVersion &base, const QString &owner, RevisionCheckout &result) const {
    return reserveDocument(directory_, base.documentId, owner, &base.checkinId, result);
}

QString RevisionStore::checkoutWithToken(const RevisionCheckout &request, RevisionCheckout &result) const {
    RevisionCheckout prepared;
    const auto error = transaction(directory_, [&](Catalog &catalog) {
        require(!request.documentId.isNull() && !request.token.isNull() && request.createdUtc.isValid()
                && request.createdUtc.offsetFromUtc() == 0, QStringLiteral("Richiesta di check-out incompleta."));
        string(QJsonValue(request.owner), 256, true);
        auto &d = findDocument(catalog, request.documentId);
        require(d.info.headCheckinId == request.baseCheckinId, QStringLiteral("La base del check-out non e' piu' corrente."));
        if (owns(d, request)) {
            require(d.checkout.createdUtc == request.createdUtc, QStringLiteral("Token di check-out riutilizzato con metadati diversi."));
            prepared = d.checkout; return false;
        }
        require(d.checkout.token.isNull(), QStringLiteral("Documento gia' in check-out da %1.").arg(d.checkout.owner));
        for (const auto &document : catalog.documents)
            require(document.checkout.token != request.token, QStringLiteral("Token di check-out gia' utilizzato."));
        d.checkout = request; prepared = request; return true;
    });
    if (error.isEmpty()) result = prepared;
    return error;
}

QString RevisionStore::resumeCheckout(const QUuid &documentId, const QUuid &token, RevisionCheckout &result) const {
    try {
        auto catalog = loadCatalog(directory_); const auto &d = findDocument(catalog, documentId);
        require(!token.isNull() && d.checkout.token == token, QStringLiteral("Prenotazione non piu' valida."));
        result = d.checkout; return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString RevisionStore::cancelCheckout(const RevisionCheckout &checkout) const {
    return transaction(directory_, [&](Catalog &catalog) {
        auto &d = findDocument(catalog, checkout.documentId);
        require(owns(d, checkout), QStringLiteral("Prenotazione persa o copia di lavoro obsoleta."));
        d.checkout = {}; return true;
    });
}

QString RevisionStore::checkin(const RevisionCheckinRequest &request, RevisionRecord &result) const {
    RevisionRecord prepared;
    const auto error = transaction(directory_, [&](Catalog &catalog) {
        require(!request.checkinId.isNull() && !request.checkout.documentId.isNull() && !request.checkout.token.isNull(),
                QStringLiteral("Identita' del check-in o della prenotazione assente."));
        require(request.payload.size() <= kMaxPayloadBytes && request.dependencies.size() <= kMaxDependencies,
                QStringLiteral("Contenuti o dipendenze oltre il limite del prototipo."));
        string(QJsonValue(request.technicalRevision), 128, true); string(QJsonValue(request.comment), 16384);
        const auto sha256 = digest(request.payload), requestHash = fingerprint(request, sha256);
        for (const auto &stored : catalog.revisions) if (stored.record.version.checkinId == request.checkinId) {
            require(stored.requestHash == requestHash, QStringLiteral("Identita' del check-in gia' usata per un'altra operazione."));
            readObject(directory_, stored.record); prepared = stored.record; return false;
        }
        auto &d = findDocument(catalog, request.checkout.documentId);
        require(owns(d, request.checkout), QStringLiteral("Prenotazione persa o copia di lavoro obsoleta."));
        QSet<QUuid> dependencies;
        for (const auto &dep : request.dependencies) {
            require(!dep.documentId.isNull() && !dep.checkinId.isNull() && !dependencies.contains(dep.checkinId),
                    QStringLiteral("Dipendenza senza versione precisa o duplicata."));
            const auto found = std::find_if(catalog.revisions.begin(), catalog.revisions.end(), [&](const auto &r) {
                return r.record.version.documentId == dep.documentId && r.record.version.checkinId == dep.checkinId;
            });
            require(found != catalog.revisions.end(), QStringLiteral("Dipendenza non presente nell'archivio."));
            readObject(directory_, found->record); dependencies.insert(dep.checkinId);
        }
        prepared.version = {d.info.documentId, request.checkinId}; prepared.previousCheckinId = d.info.headCheckinId;
        prepared.sequence = 1;
        for (const auto &r : catalog.revisions) if (r.record.version.documentId == d.info.documentId) ++prepared.sequence;
        prepared.technicalRevision = request.technicalRevision; prepared.author = d.checkout.owner; prepared.comment = request.comment;
        prepared.createdUtc = QDateTime::currentDateTimeUtc(); prepared.sha256 = sha256; prepared.payloadSize = request.payload.size();
        prepared.dependencies = request.dependencies;
        const auto path = objectPath(directory_, sha256);
        if (QFileInfo::exists(path)) readObject(directory_, prepared);
        else {
            QSaveFile object(path);
            require(object.open(QIODevice::WriteOnly) && object.write(request.payload) == request.payload.size() && object.commit(),
                    QStringLiteral("Scrittura del contenuto del check-in fallita."));
        }
        catalog.revisions.append({prepared, requestHash}); d.info.headCheckinId = request.checkinId; d.checkout = {};
        return true;
    });
    if (error.isEmpty()) result = prepared;
    return error;
}

QString RevisionStore::read(const DocumentVersion &version, QByteArray &payload) const {
    try {
        const auto catalog = loadCatalog(directory_);
        const auto found = std::find_if(catalog.revisions.begin(), catalog.revisions.end(), [&](const auto &r) {
            return r.record.version.documentId == version.documentId && r.record.version.checkinId == version.checkinId;
        });
        require(found != catalog.revisions.end(), QStringLiteral("Check-in non presente nell'archivio."));
        const auto prepared = readObject(directory_, found->record); payload = prepared; return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}
}
