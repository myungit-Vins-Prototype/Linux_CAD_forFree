#ifndef FORGECAD_REVISION_STORE_H
#define FORGECAD_REVISION_STORE_H

#include "cad_document_version.h"
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

namespace ForgeCad {

struct RevisionCheckout {
    QUuid documentId, token, baseCheckinId;
    QString owner; // metadato dichiarato, senza autenticazione
    QDateTime createdUtc;
};
struct RevisionDocumentInfo {
    QUuid documentId, headCheckinId;
    QString name, kind;
    bool checkedOut = false;
    QString checkoutOwner;
};
struct RevisionRecord {
    DocumentVersion version;
    QUuid previousCheckinId;
    quint64 sequence = 0;
    QString technicalRevision, author, comment;
    QDateTime createdUtc;
    QByteArray sha256; // digest binario di 32 byte
    qint64 payloadSize = 0;
    QVector<DocumentVersion> dependencies;
};
struct RevisionCheckinRequest {
    RevisionCheckout checkout;
    // Generato prima del primo tentativo e riusato soltanto per ritentare
    // la stessa operazione. E' anche l'identita' del check-in da pubblicare.
    QUuid checkinId;
    QByteArray payload;
    QString technicalRevision, comment;
    QVector<DocumentVersion> dependencies;
};

struct RevisionReleaseRequest {
    QUuid releaseId; // generato una volta e riusato per ritentare la stessa pubblicazione
    QString name,author,comment;
    DocumentVersion model;
    QVector<DocumentVersion> drawings; // ordine di consegna; un check-in per documento
};
struct RevisionReleaseMember {
    DocumentVersion version;
    QString name,kind,technicalRevision;
    quint64 sequence=0;
    QByteArray sha256;
    qint64 payloadSize=0;
};
struct RevisionReleaseManifest {
    QUuid releaseId,archiveId;
    QString name,author,comment;
    QDateTime createdUtc;
    RevisionReleaseMember model;
    QVector<RevisionReleaseMember> drawings;
};
RevisionReleaseRequest revisionReleaseRequest(const RevisionReleaseManifest &manifest);

// Archivio cooperativo su filesystem locale. Il lock breve protegge il
// catalogo; le prenotazioni persistono nel catalogo anche dopo la chiusura.
// Non e' un sistema di autenticazione. I filesystem di rete vanno validati.
class RevisionStore {
public:
    explicit RevisionStore(QString directory);
    QString initialize() const;
    QString createDocument(const QString &name, const QString &kind, QUuid &documentId) const;
    // Adotta l'identita' gia' contenuta nel file, senza creare un duplicato.
    // Se esiste, conserva nome/storico e verifica il tipo di documento.
    QString registerDocument(const QUuid &documentId, const QString &name, const QString &kind) const;
    QString archiveIdentity(QUuid &archiveId) const;
    QString documents(QVector<RevisionDocumentInfo> &result) const;
    QString history(const QUuid &documentId, QVector<RevisionRecord> &result) const;
    QString checkout(const QUuid &documentId, const QString &owner, RevisionCheckout &result) const;
    // Confronta la base sotto lock prima di creare la prenotazione.
    QString checkoutAtVersion(const DocumentVersion &base, const QString &owner, RevisionCheckout &result) const;
    // Token scelto e conservato dal client prima della richiesta: ritentativi
    // identici riprendono la prenotazione senza crearne una seconda.
    QString checkoutWithToken(const RevisionCheckout &request, RevisionCheckout &result) const;
    QString resumeCheckout(const QUuid &documentId, const QUuid &token, RevisionCheckout &result) const;
    QString cancelCheckout(const RevisionCheckout &checkout) const;
    // Pubblica contenuto + record e rilascia la prenotazione soltanto al
    // successo. I ritentativi identici restituiscono il medesimo record.
    QString checkin(const RevisionCheckinRequest &request, RevisionRecord &result) const;
    // Anteprima senza prenotare: l'unicita' viene verificata nuovamente sotto lock.
    QString nextTechnicalRevision(const QUuid &documentId, QString &label) const;
    QString validateTechnicalRevision(const QUuid &documentId, const QString &label) const;
    QString read(const DocumentVersion &version, QByteArray &payload) const;
    // Il backend verifica record, dipendenze e hash; il ponte CAD valida anche i contenuti nativi.
    QString inspectRelease(const RevisionReleaseRequest &request,RevisionReleaseManifest &manifest) const;
    QString publishRelease(const RevisionReleaseRequest &request,RevisionReleaseManifest &manifest) const;
    QString readRelease(const QUuid &id,RevisionReleaseManifest &manifest) const;
    // Elenco dei manifesti; la verifica dei contenuti avviene con readRelease.
    QString releases(QVector<RevisionReleaseManifest> &manifests) const;
    QString directory() const { return directory_; }
private:
    QString directory_;
};
}
#endif
