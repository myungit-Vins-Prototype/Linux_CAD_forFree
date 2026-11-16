#ifndef FORGECAD_DOCUMENT_VERSION_H
#define FORGECAD_DOCUMENT_VERSION_H
#include <QUuid>

namespace ForgeCad {
// Identita' logica indipendente da nome/percorso. Il check-in nullo indica
// la copia di lavoro; le dipendenze archiviate richiedono un check-in preciso.
struct DocumentVersion {
    QUuid documentId;
    QUuid checkinId;
};
}
#endif
