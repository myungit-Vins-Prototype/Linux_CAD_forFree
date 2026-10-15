#include "cad_workspace.h"
#include "cad_revision_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <exception>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace ForgeCad {
namespace {
void requireWorkspace(bool ok, const QString &error) {
    if (!ok) throw std::runtime_error(error.toStdString());
}
WorkspacePaths pathsAt(const QString &root) {
    WorkspacePaths paths;
    paths.root = root;
    paths.metadata = QDir(root).filePath(".forgecad");
    paths.revisions = QDir(paths.metadata).filePath("revisions");
    paths.sessions = QDir(paths.metadata).filePath("sessions");
    paths.pending = QDir(paths.metadata).filePath("pending");
    return paths;
}
void validateDirectory(const QString &path) {
    const QFileInfo info(path);
    requireWorkspace(info.isDir() && !info.isSymLink(),
                     QStringLiteral("Cartella interna del workspace non valida: %1").arg(path));
}
void readIdentity(WorkspacePaths &paths) {
    QFile file(QDir(paths.metadata).filePath("workspace.json"));
    requireWorkspace(file.open(QIODevice::ReadOnly) && file.size() <= 4096,
                     QStringLiteral("Identita' del workspace assente o illeggibile: %1").arg(paths.metadata));
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &error);
    const auto object = json.object();
    paths.workspaceId = QUuid(object["workspaceId"].toString());
    requireWorkspace(error.error == QJsonParseError::NoError && json.isObject()
                     && object["format"].toString() == QLatin1String("ForgeCAD.Workspace")
                     && object["schema"].toInt(-1) == 1 && !paths.workspaceId.isNull(),
                     QStringLiteral("Formato del workspace non valido o non supportato: %1").arg(paths.metadata));
}
void ensureLayout(const WorkspacePaths &paths) {
    const RevisionStore store(paths.revisions);
    const auto error = store.initialize();
    requireWorkspace(error.isEmpty(), error);
    for (const auto &directory : {paths.sessions, paths.pending}) {
        requireWorkspace(QDir().mkpath(directory), QStringLiteral("Impossibile creare %1").arg(directory));
        validateDirectory(directory);
    }
}
}
QString locateDocumentWorkspace(const QString &documentPath, WorkspacePaths &paths) {
    try {
        requireWorkspace(!documentPath.trimmed().isEmpty(), QStringLiteral("Percorso del documento assente."));
        const QFileInfo requested(documentPath);
        // Gli alias di un file esistente devono usare il workspace del file reale.
        const QFileInfo document(requested.exists() ? requested.canonicalFilePath() : requested.absoluteFilePath());
        const QString parent = document.dir().canonicalPath();
        requireWorkspace(!parent.isEmpty(), QStringLiteral("Cartella del documento inesistente."));
        QDir cursor(parent);
        do {
            requireWorkspace(cursor.dirName() != QLatin1String(".forgecad"),
                             QStringLiteral("Salvare i documenti fuori dalla cartella interna .forgecad."));
            const QFileInfo metadata(cursor.filePath(".forgecad"));
            if (metadata.exists() || metadata.isSymLink()) {
                auto prepared = pathsAt(cursor.canonicalPath());
                validateDirectory(prepared.metadata);
                // L'identita' puo' ancora mancare dopo un'inizializzazione interrotta.
                if (QFileInfo::exists(QDir(prepared.metadata).filePath("workspace.json"))) readIdentity(prepared);
                paths = std::move(prepared);
                return {};
            }
        } while (cursor.cdUp());
        paths = pathsAt(parent);
        return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}
QString ensureDocumentWorkspace(const QString &documentPath, WorkspacePaths &paths) {
    WorkspacePaths prepared;
    const auto error = locateDocumentWorkspace(documentPath, prepared);
    if (!error.isEmpty()) return error;
    try {
        requireWorkspace(QDir().mkpath(prepared.metadata), QStringLiteral("Impossibile creare %1").arg(prepared.metadata));
        validateDirectory(prepared.metadata);
#ifdef Q_OS_WIN
        const auto nativePath = QDir::toNativeSeparators(prepared.metadata);
        const auto windowsPath = reinterpret_cast<LPCWSTR>(nativePath.utf16());
        const DWORD attributes = GetFileAttributesW(windowsPath);
        requireWorkspace(attributes != INVALID_FILE_ATTRIBUTES
                         && ((attributes & FILE_ATTRIBUTE_HIDDEN) || SetFileAttributesW(windowsPath, attributes | FILE_ATTRIBUTE_HIDDEN)),
                         QStringLiteral("Impossibile nascondere la cartella interna del workspace."));
#endif
        QLockFile lock(QDir(prepared.metadata).filePath("workspace.lock"));
        lock.setStaleLockTime(0);
        requireWorkspace(lock.tryLock(0), QStringLiteral("Workspace occupato o non scrivibile; riprovare."));
        const auto marker = QDir(prepared.metadata).filePath("workspace.json");
        if (QFileInfo::exists(marker)) {
            readIdentity(prepared);
            // Salva non rilegge l'intero storico: l'archivio e' verificato dalle
            // operazioni di revisione. Qui controlla solo la struttura necessaria.
            for (const auto &directory : {prepared.revisions, prepared.sessions, prepared.pending}) validateDirectory(directory);
            requireWorkspace(QFileInfo(QDir(prepared.revisions).filePath("catalog.json")).isFile()
                             && QFileInfo(QDir(prepared.revisions).filePath("objects")).isDir(),
                             QStringLiteral("Archivio del workspace incompleto: ripristinare i dati mancanti."));
        } else {
            const auto entries = QDir(prepared.metadata).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
            for (const auto &entry : entries)
                requireWorkspace(entry == QLatin1String("workspace.lock") || entry == QLatin1String("revisions")
                                 || entry == QLatin1String("sessions") || entry == QLatin1String("pending"),
                                 QStringLiteral("La cartella .forgecad contiene dati non riconosciuti: inizializzazione annullata."));
            for (const auto &directory : {prepared.revisions, prepared.sessions, prepared.pending})
                if (QFileInfo::exists(directory) || QFileInfo(directory).isSymLink()) validateDirectory(directory);
            ensureLayout(prepared);
            prepared.workspaceId = QUuid::createUuid();
            const auto bytes = QJsonDocument(QJsonObject{{"format", "ForgeCAD.Workspace"}, {"schema", 1},
                {"workspaceId", prepared.workspaceId.toString(QUuid::WithoutBraces)}}).toJson(QJsonDocument::Compact);
            QSaveFile file(marker);
            requireWorkspace(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
                             QStringLiteral("Impossibile pubblicare l'identita' del workspace."));
        }
        paths = std::move(prepared);
        return {};
    } catch (const std::exception &exception) { return QString::fromUtf8(exception.what()); }
}
}
