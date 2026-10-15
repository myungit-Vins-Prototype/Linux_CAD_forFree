#include "cad_revision_store.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

using namespace ForgeCad;
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void ok(const QString &error) { if (!error.isEmpty()) throw std::runtime_error(error.toStdString()); }
QByteArray readFile(const QString &path) {
    QFile f(path); require(f.open(QIODevice::ReadOnly), "lettura fixture"); return f.readAll();
}
void writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path); require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "scrittura fixture");
}
RevisionCheckinRequest request(RevisionStore &store, const QUuid &document, const QByteArray &payload,
                             const QString &revision = QStringLiteral("A")) {
    RevisionCheckinRequest r;
    ok(store.checkout(document, QStringLiteral("progettista"), r.checkout));
    r.checkinId = QUuid::createUuid(); r.payload = payload; r.technicalRevision = revision; r.comment = QStringLiteral("modifica");
    return r;
}
QVector<RevisionRecord> history(RevisionStore &store, const QUuid &id) {
    QVector<RevisionRecord> result; ok(store.history(id, result)); return result;
}
void samePayload(RevisionStore &store, const DocumentVersion &version, const QByteArray &expected) {
    QByteArray bytes; ok(store.read(version, bytes)); require(bytes == expected, "contenuto storico esatto");
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 5 && args[1] == QLatin1String("--checkout")) {
        RevisionStore store(args[2]); RevisionCheckout checkout;
        if (!store.checkout(QUuid(args[3]), args[4], checkout).isEmpty()) return 10;
        std::cout << checkout.token.toString(QUuid::WithoutBraces).toStdString(); return 0;
    }
    try {
        QTemporaryDir temp; require(temp.isValid(), "directory temporanea");
        const auto path = temp.filePath("archivio"); RevisionStore store(path);
        require(!RevisionStore(QString()).initialize().isEmpty(), "archivio senza percorso rifiutato");
        ok(store.initialize()); ok(store.initialize());
        QUuid modelId; ok(store.createDocument(QStringLiteral("Particolare"), QStringLiteral("model"), modelId));
        const QByteArray firstPayload = QByteArrayLiteral("definizione-1\0B-rep-1");
        auto firstRequest = request(store, modelId, firstPayload);
        RevisionRecord first; ok(store.checkin(firstRequest, first));
        require(first.sequence == 1 && first.previousCheckinId.isNull() && first.version.documentId == modelId
                && first.version.checkinId == firstRequest.checkinId, "primo check-in con identita' e base");
        samePayload(store, first.version, firstPayload);
        RevisionRecord repeated; ok(store.checkin(firstRequest, repeated));
        require(repeated.version.checkinId == first.version.checkinId && history(store,modelId).size() == 1,
                "risposta persa: ritentativo idempotente");
        auto conflicting = firstRequest; conflicting.comment = QStringLiteral("altra operazione");
        require(!store.checkin(conflicting, repeated).isEmpty(), "riuso ID per un'altra operazione rifiutato");
        auto secondRequest = request(store, modelId, firstPayload);
        RevisionRecord second; ok(store.checkin(secondRequest, second));
        require(second.sequence == 2 && second.previousCheckinId == first.version.checkinId
                && second.sha256 == first.sha256 && second.technicalRevision == first.technicalRevision,
                "due check-in distinti nella stessa revisione, anche a contenuto identico");
        require(QDir(path+"/objects").entryList({"*.bin"},QDir::Files).size() == 1, "contenuti identici condivisi senza perdere check-in");
        require(history(store,modelId).size() == 2, "tutti i check-in conservati");
        std::cout << "PASS copie storiche, deduplicazione e ritentativi\n";

        // La prenotazione sopravvive all'istanza e puo' essere ripresa col token.
        auto pending = request(store, modelId, QByteArray("definizione-3"), QStringLiteral("B"));
        RevisionStore reopened(path); RevisionCheckout resumed;
        ok(reopened.resumeCheckout(modelId, pending.checkout.token, resumed));
        require(resumed.baseCheckinId == second.version.checkinId && resumed.owner == pending.checkout.owner, "prenotazione persistente");
        RevisionCheckout blocked;
        require(!reopened.checkout(modelId, QStringLiteral("altro"), blocked).isEmpty(), "secondo proprietario rifiutato");
        ok(store.checkin(firstRequest,repeated));
        ok(store.resumeCheckout(modelId,pending.checkout.token,resumed));
        samePayload(reopened, first.version, firstPayload); // consultazione senza check-out
        auto obsolete = pending; obsolete.checkout.baseCheckinId = first.version.checkinId;
        require(!store.checkin(obsolete, repeated).isEmpty() && history(store,modelId).size() == 2, "base obsoleta rifiutata");
        ok(store.cancelCheckout(pending.checkout));
        auto replacement = request(store, modelId, pending.payload, QStringLiteral("B"));
        require(!store.checkin(pending,repeated).isEmpty(), "vecchio titolare non puo' pubblicare dopo nuova prenotazione");
        require(!store.cancelCheckout(pending.checkout).isEmpty(), "vecchio token non cancella la prenotazione nuova");
        QVector<RevisionDocumentInfo> infos; ok(store.documents(infos));
        require(infos.size() == 1 && infos[0].checkedOut && infos[0].checkoutOwner == QStringLiteral("progettista"), "stato check-out consultabile");
        std::cout << "PASS prenotazione persistente, revoca e copie obsolete\n";

        // Un lock del catalogo non blocca la lettura dello storico.
        const auto originalCatalog = readFile(path+"/catalog.json");
        QLockFile catalogLock(path+"/catalog.lock"); catalogLock.setStaleLockTime(0);
        require(catalogLock.tryLock(0), "lock catalogo di prova");
        require(!store.checkin(replacement, repeated).isEmpty(), "pubblicazione concorrente rifiutata senza attesa UI");
        samePayload(store, first.version, firstPayload);
        catalogLock.unlock();
        require(readFile(path+"/catalog.json") == originalCatalog, "errore di prenotazione senza modifica del catalogo");
        // La scrittura del contenuto fallisce dopo aver letto il catalogo.
        require(QDir().rename(path+"/objects",path+"/objects-retained"), "spostamento fixture contenuti");
        writeFile(path+"/objects", "ostacolo");
        require(!store.checkin(replacement,repeated).isEmpty(), "errore di scrittura contenuto rilevato");
        require(readFile(path+"/catalog.json") == originalCatalog, "nessun check-in parziale dopo errore di scrittura");
        require(QFile::remove(path+"/objects") && QDir().rename(path+"/objects-retained",path+"/objects"), "ripristino fixture contenuti");
        ok(store.resumeCheckout(modelId,replacement.checkout.token,resumed));
        // Simula un contenuto completato prima di una pubblicazione interrotta.
        const auto orphanHash = QCryptographicHash::hash(replacement.payload,QCryptographicHash::Sha256).toHex();
        writeFile(path+"/objects/"+QString::fromLatin1(orphanHash)+".bin", replacement.payload);
        RevisionRecord third; ok(store.checkin(replacement,third));
        require(third.sequence == 3 && third.previousCheckinId == second.version.checkinId, "ritentativo dopo contenuto non pubblicato");
        ok(store.checkin(replacement,repeated));
        require(history(store,modelId).size() == 3, "nessun duplicato dopo risposta persa");
        samePayload(store,first.version,firstPayload); samePayload(store,third.version,replacement.payload);
        // Il ripristino e' un discendente nuovo, non riscrive lo storico.
        auto restoreRequest = request(store,modelId,firstPayload,QStringLiteral("C"));
        RevisionRecord restored; ok(store.checkin(restoreRequest,restored));
        require(restored.sequence == 4 && restored.previousCheckinId == third.version.checkinId, "ripristino senza sovrascrivere il passato");
        std::cout << "PASS errori atomici, contenuti preparati e ripristino\n";

        QUuid drawingId; ok(store.createDocument(QStringLiteral("Tavola"),QStringLiteral("drawing"),drawingId));
        auto drawingRequest = request(store,drawingId,QByteArray("tavola della prima versione"));
        drawingRequest.dependencies = {first.version};
        RevisionRecord drawingRecord; ok(store.checkin(drawingRequest,drawingRecord));
        const auto drawingHistory = history(store,drawingId);
        require(drawingHistory.size() == 1 && drawingHistory[0].dependencies.size() == 1
                && drawingHistory[0].dependencies[0].checkinId == first.version.checkinId, "dipendenza della tavola fissata al vecchio modello");
        samePayload(store,drawingHistory[0].dependencies[0],firstPayload);
        auto badDependency = request(store,drawingId,QByteArray("dipendenza non archiviata"));
        badDependency.dependencies = {{modelId,{}}};
        require(!store.checkin(badDependency,repeated).isEmpty(), "dipendenza dalla copia di lavoro rifiutata");
        badDependency.dependencies = {{modelId,QUuid::createUuid()}};
        require(!store.checkin(badDependency,repeated).isEmpty(), "dipendenza inesistente rifiutata");
        badDependency.dependencies = {first.version,first.version};
        require(!store.checkin(badDependency,repeated).isEmpty(), "dipendenze duplicate rifiutate");
        ok(store.cancelCheckout(badDependency.checkout));
        std::cout << "PASS dipendenze della tavola fissate ai check-in esatti\n";

        // Un file esistente mantiene il proprio UUID nell'archivio.
        const QUuid nativeId = QUuid::createUuid();
        ok(store.registerDocument(nativeId,QStringLiteral("Nativo"),QStringLiteral("model")));
        ok(store.registerDocument(nativeId,QStringLiteral("Altro percorso"),QStringLiteral("model")));
        require(!store.registerDocument(nativeId,QStringLiteral("Tavola"),QStringLiteral("drawing")).isEmpty(), "tipo incompatibile non sovrascrive il documento");
        require(history(store,nativeId).isEmpty(), "identita' adottata senza duplicare lo storico");
        ok(store.checkoutAtVersion({nativeId,{}},QStringLiteral("progettista"),resumed));
        ok(store.cancelCheckout(resumed));
        require(!store.checkoutAtVersion(first.version,QStringLiteral("progettista"),resumed).isEmpty(), "base letta prima del check-out gia' obsoleta");
        ok(store.checkoutAtVersion(restored.version,QStringLiteral("progettista"),resumed)); ok(store.cancelCheckout(resumed));
        QUuid archiveId; ok(store.archiveIdentity(archiveId)); require(!archiveId.isNull(), "identita' persistente dell'archivio");
        std::cout << "PASS identita' adottate e base controllata prima del check-out\n";

        // Due processi non possono ottenere entrambi la stessa prenotazione.
        QProcess a,b; const auto executable = QCoreApplication::applicationFilePath();
        const auto id = modelId.toString(QUuid::WithoutBraces);
        a.start(executable,{"--checkout",path,id,"utente-A"}); b.start(executable,{"--checkout",path,id,"utente-B"});
        require(a.waitForFinished(10000) && b.waitForFinished(10000), "due processi concorrenti terminati");
        require(a.exitStatus() == QProcess::NormalExit && b.exitStatus() == QProcess::NormalExit,
                "processi concorrenti senza crash");
        require((a.exitCode() == 0 && b.exitCode() == 10) || (a.exitCode() == 10 && b.exitCode() == 0), "un solo check-out vincente tra processi");
        const auto token = QUuid(QString::fromUtf8(a.exitCode() == 0 ? a.readAllStandardOutput() : b.readAllStandardOutput()));
        ok(store.resumeCheckout(modelId,token,resumed)); ok(store.cancelCheckout(resumed));
        std::cout << "PASS esclusione tra processi\n";

        // Danni a contenuti/catalogo: rifiuto, senza riparazioni distruttive.
        const auto objectPath = path+"/objects/"+QString::fromLatin1(first.sha256.toHex())+".bin";
        const auto originalObject = readFile(objectPath); auto corrupted = originalObject; corrupted[0] = char(corrupted[0]^1);
        writeFile(objectPath,corrupted);
        QByteArray retained("mantieni output");
        require(!store.read(first.version,retained).isEmpty() && retained == "mantieni output", "corruzione SHA256 e output atomico");
        require(!store.checkin(firstRequest,repeated).isEmpty(), "ritentativo non nasconde contenuti corrotti");
        writeFile(objectPath,originalObject);
        const auto catalogBytes = readFile(path+"/catalog.json");
        auto invalid = QJsonDocument::fromJson(catalogBytes).object(); invalid["schema"] = 2;
        writeFile(path+"/catalog.json",QJsonDocument(invalid).toJson());
        require(!store.initialize().isEmpty(), "schema sconosciuto non azzera l'archivio");
        const auto infoCount = infos.size();
        require(!store.documents(infos).isEmpty() && infos.size() == infoCount, "catalogo invalido senza output parziale");
        writeFile(path+"/catalog.json",catalogBytes);
        invalid = QJsonDocument::fromJson(catalogBytes).object(); auto records = invalid["revisions"].toArray();
        auto invalidRecord = records[0].toObject(); invalidRecord["previous"] = QUuid::createUuid().toString();
        records[0] = invalidRecord; invalid["revisions"] = records;
        writeFile(path+"/catalog.json",QJsonDocument(invalid).toJson());
        QVector<RevisionRecord> invalidHistory;
        require(!store.history(modelId,invalidHistory).isEmpty(), "catena storica incoerente rifiutata");
        writeFile(path+"/catalog.json",catalogBytes);
        require(QFile::remove(path+"/catalog.json"), "rimozione simulata del catalogo");
        require(!store.initialize().isEmpty() && !QFileInfo::exists(path+"/catalog.json"),
                "catalogo perso non sostituito con un archivio vuoto");
        writeFile(path+"/catalog.json",catalogBytes);
        // Spostare la cartella non cambia le identita' delle copie storiche.
        const auto movedPath = temp.filePath("archivio-spostato"); require(QDir().rename(path,movedPath), "spostamento archivio");
        RevisionStore moved(movedPath); ok(moved.initialize());
        QUuid movedId; ok(moved.archiveIdentity(movedId)); require(movedId == archiveId, "identita' archivio dopo spostamento");
        require(history(moved,modelId).size() == 4, "tutti i check-in dopo spostamento e riapertura");
        samePayload(moved,first.version,firstPayload); samePayload(moved,third.version,replacement.payload);
        samePayload(moved,drawingRecord.version,drawingRequest.payload);
        std::cout << "PASS integrita', catene e archivio spostato\n";
    } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
