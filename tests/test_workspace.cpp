#include "cad_workspace.h"
#include "cad_revision_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void ok(const QString &error) { if (!error.isEmpty()) throw std::runtime_error(error.toStdString()); }
void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path); require(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size(), "scrittura fixture");
}
QByteArray readFile(const QString &path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "lettura fixture"); return file.readAll();
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    try {
        using namespace ForgeCad;
        QTemporaryDir directory; require(directory.isValid(),"directory temporanea");
        const auto requestedRoot=directory.filePath("progetto");require(QDir().mkpath(requestedRoot),"cartella progetto");
        const auto root=QFileInfo(requestedRoot).canonicalFilePath();
        const auto file=QDir(root).filePath("parte.prt");WorkspacePaths paths;
        ok(locateDocumentWorkspace(file,paths));
        require(paths.root==root && paths.workspaceId.isNull() && !QFileInfo::exists(paths.metadata),"ricerca senza creazione");
        ok(ensureDocumentWorkspace(file,paths));const auto identity=paths.workspaceId;
        require(!identity.isNull() && QDir(paths.revisions).exists() && QDir(paths.sessions).exists() && QDir(paths.pending).exists(),"struttura iniziale");
        require(QFileInfo(paths.metadata).isHidden(),"metadati workspace nascosti");
        const auto marker=QDir(paths.metadata).filePath("workspace.json");const auto markerBytes=readFile(marker);
        const auto catalog=QDir(paths.revisions).filePath("catalog.json");const auto catalogBytes=readFile(catalog);
        ok(ensureDocumentWorkspace(file,paths));
        require(paths.workspaceId==identity && readFile(marker)==markerBytes && readFile(catalog)==catalogBytes,"inizializzazione idempotente");
        const auto nested=QDir(root).filePath("sotto/cartella");require(QDir().mkpath(nested),"sottocartelle");
        ok(ensureDocumentWorkspace(QDir(nested).filePath("altra.prt"),paths));
        require(paths.root==root && paths.workspaceId==identity && !QDir(QDir(nested).filePath(".forgecad")).exists(),"riuso dal sottoalbero");
        {QLockFile lock(QDir(paths.metadata).filePath("workspace.lock"));require(lock.tryLock(0),"lock fixture");
         WorkspacePaths retained=paths;require(!ensureDocumentWorkspace(file,retained).isEmpty() && retained.workspaceId==identity,"concorrenza senza stato parziale");}
        const auto archivePath=paths.revisions;RevisionStore store(archivePath);QUuid documentId;
        ok(store.createDocument("Parte","model",documentId));const auto populated=readFile(catalog);
        writeFile(marker,"{}");require(!ensureDocumentWorkspace(file,paths).isEmpty() && readFile(catalog)==populated,"marker corrotto non azzera archivio");
        writeFile(marker,markerBytes);require(QFile::remove(catalog),"rimuovi catalogo fixture");
        require(!ensureDocumentWorkspace(file,paths).isEmpty() && !QFileInfo::exists(catalog),"catalogo mancante non ricreato");
        writeFile(catalog,populated);
        require(!ensureDocumentWorkspace(QDir(paths.pending).filePath("errato.prt"),paths).isEmpty(),"documenti esclusi dalle cartelle interne");
#ifndef Q_OS_WIN
        const auto alias=directory.filePath("alias");require(QFile::link(root,alias),"alias directory");
        ok(ensureDocumentWorkspace(QDir(alias).filePath("parte.prt"),paths));require(paths.root==root && paths.workspaceId==identity,"alias usa workspace reale");
#endif
        const auto moved=directory.filePath("spostato");require(QDir().rename(root,moved),"spostamento workspace");
        ok(ensureDocumentWorkspace(QDir(moved).filePath("parte.prt"),paths));require(paths.workspaceId==identity,"spostamento conserva identita'");
        RevisionStore movedStore(paths.revisions);QVector<RevisionDocumentInfo> docs;ok(movedStore.documents(docs));
        require(docs.size()==1 && docs[0].documentId==documentId,"spostamento conserva catalogo");
        const auto other=directory.filePath("altro");require(QDir().mkpath(other),"altro progetto");
        ok(ensureDocumentWorkspace(QDir(other).filePath("nuovo.prt"),paths));require(paths.workspaceId!=identity,"progetti indipendenti");
        const auto collision=directory.filePath("collisione");require(QDir().mkpath(collision),"collisione");writeFile(QDir(collision).filePath(".forgecad"),"dati utente");
        require(!ensureDocumentWorkspace(QDir(collision).filePath("parte.prt"),paths).isEmpty(),"collisione file interno rifiutata");
        const auto unknown=directory.filePath("non-nostro");require(QDir().mkpath(QDir(unknown).filePath(".forgecad")),"cartella preesistente");
        writeFile(QDir(unknown).filePath(".forgecad/estraneo"),"preserva");
        require(!ensureDocumentWorkspace(QDir(unknown).filePath("parte.prt"),paths).isEmpty()
                && readFile(QDir(unknown).filePath(".forgecad/estraneo"))=="preserva","cartella sconosciuta preservata");
        const auto interrupted=directory.filePath("interrotto");require(QDir().mkpath(QDir(interrupted).filePath(".forgecad")),"inizializzazione interrotta");
        RevisionStore interruptedStore(QDir(interrupted).filePath(".forgecad/revisions"));ok(interruptedStore.initialize());
        QUuid archiveId;ok(interruptedStore.archiveIdentity(archiveId));
        ok(ensureDocumentWorkspace(QDir(interrupted).filePath("parte.prt"),paths));QUuid afterRecovery;ok(interruptedStore.archiveIdentity(afterRecovery));
        require(archiveId==afterRecovery,"ripresa inizializzazione conserva archivio");
        std::cout << "PASS workspace\n";return 0;
    } catch(const std::exception &error) { std::cerr << error.what() << '\n';return 1; }
}
