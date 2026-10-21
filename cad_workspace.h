#ifndef FORGECAD_WORKSPACE_H
#define FORGECAD_WORKSPACE_H

#include <QString>
#include <QUuid>

namespace ForgeCad {
struct WorkspacePaths {
    QString root, metadata, revisions, sessions, pending;
    QUuid workspaceId;
};
// Cerca il workspace piu' vicino risalendo dalla cartella del documento.
// Senza workspace esistente propone la cartella del documento, senza scrivere.
QString locateDocumentWorkspace(const QString &documentPath, WorkspacePaths &paths);
// Solo durante Salva: crea .forgecad e l'archivio al primo utilizzo.
// Gli altri salvataggi riusano il workspace anche dalle sottocartelle.
QString ensureDocumentWorkspace(const QString &documentPath, WorkspacePaths &paths);
}
#endif
