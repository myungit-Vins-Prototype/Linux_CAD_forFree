#ifndef FORGECAD_NATIVE_REVISIONS_H
#define FORGECAD_NATIVE_REVISIONS_H

#include "cad_document_io.h"
#include "cad_drawing.h"
#include "cad_revision_store.h"

namespace ForgeCad {
struct NativeRevisionSession {
    QUuid archiveId;
    QString archiveDirectory;
    RevisionCheckout checkout;
};

// Documento nuovo o copia di lavoro gia' basata sul check-in corrente.
QString beginNativeCheckout(RevisionStore &store, const DocumentState &working, const QString &name,
                           const QString &owner, NativeRevisionSession &session);
QString resumeNativeCheckout(RevisionStore &store, const DocumentState &working, NativeRevisionSession &session);
QString readNativeRevision(RevisionStore &store, const DocumentVersion &version, DocumentState &archived);
// Legge e riserva esattamente questa versione. Se non e' piu' la testa, non
// crea una prenotazione; il ripristino storico resta un'operazione distinta.
QString checkoutNativeRevision(RevisionStore &store, const DocumentVersion &version, const QString &owner,
                               DocumentState &working, NativeRevisionSession &session);
QString prepareNativeCheckin(const DocumentState &working, const NativeRevisionSession &session,
                            const QString &technicalRevision, const QString &comment, RevisionCheckinRequest &request);
QString publishNativeCheckin(RevisionStore &store, const RevisionCheckinRequest &request,
                            DocumentState &working, NativeRevisionSession &session, RevisionRecord &record);

// La richiesta preparata va conservata finche' non arriva la ricevuta.
// Il contenitore include i byte originali dello snapshot, senza ricodificarli
// dopo un riavvio: il ritentativo resta identico anche per curve rilette.
QString encodeNativeRevisionOperation(const NativeRevisionSession &session, const RevisionCheckinRequest &request, QByteArray &bytes);
QString decodeNativeRevisionOperation(const QByteArray &bytes, NativeRevisionSession &session, RevisionCheckinRequest &request);

// I corpi sono scelti esplicitamente per ID feature: nessuna inclusione
// implicita di stadi consumati o nascosti. Mantiene B-rep e nomi condivisi.
QString drawingSnapshotOfDocument(const DocumentState &document, const QVector<quint64> &features, DrawingModelSnapshot &snapshot);
}
#endif
