#ifndef FORGECAD_REVISION_CLIENT_H
#define FORGECAD_REVISION_CLIENT_H
#include "cad_native_revisions.h"
#include "cad_workspace.h"

namespace ForgeCad {
enum class WorkspaceCheckoutPhase { Reserving, Active, Canceling };
struct WorkspaceRevisionStatus {
    bool hasSession = false, hasPending = false;
    WorkspaceCheckoutPhase phase = WorkspaceCheckoutPhase::Active;
    NativeRevisionSession session;
};
// Anteprima della revisione del documento registrato, senza creare cartelle.
QString nextWorkspaceRevisionLabel(const QString &path,const QUuid &documentId,QString &label);
// Legge lo stato locale senza creare workspace o prenotazioni.
QString workspaceRevisionStatus(const QString &path, const DocumentState &working, WorkspaceRevisionStatus &status);
// Il documento deve essere gia' salvato nel workspace. Il token viene
// conservato prima di inviare la prenotazione e riusato al recupero.
QString checkoutWorkspaceDocument(const QString &path, const DocumentState &working, const QString &owner);
// Mantiene le modifiche locali; rifiuta l'annullamento con check-in pendente.
QString cancelWorkspaceCheckout(const QString &path, const DocumentState &working);
QString checkinWorkspaceDocument(const QString &path, DocumentState &working, const QString &revision, const QString &comment,
                                RevisionRecord &record, bool bodies = true, const DocumentFileLock *lock = nullptr);
// Completa esattamente l'operazione gia' preparata, anche se il modello locale
// e' cambiato. In quel caso conserva le modifiche e lo segnala al chiamante.
QString completeWorkspaceCheckin(const QString &path, DocumentState &working, RevisionRecord &record, bool &matchesWorking,
                                bool bodies = true, const DocumentFileLock *lock = nullptr);
QString workspaceDocumentHistory(const QString &path, const DocumentState &working, QVector<RevisionRecord> &records);
}
#endif
