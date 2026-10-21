#ifndef FORGECAD_DRAWING_REVISIONS_H
#define FORGECAD_DRAWING_REVISIONS_H
#include "cad_drawing_file.h"
#include "cad_revision_client.h"
namespace ForgeCad {
struct DrawingRevisionOperation {
    QUuid archiveId;
    QString workingFile; // relativo al workspace, verificato al recupero
    RevisionCheckinRequest request;
};
QString encodeDrawingRevisionOperation(const DrawingRevisionOperation &operation,QByteArray &bytes);
QString decodeDrawingRevisionOperation(const QByteArray &bytes,DrawingRevisionOperation &operation);
QString workspaceDrawingRevisionStatus(const QString &path,const DrawingFile &drawing,WorkspaceRevisionStatus &status);
QString checkoutWorkspaceDrawing(const QString &path,const DrawingFile &drawing,const QString &owner);
QString cancelWorkspaceDrawingCheckout(const QString &path,const DrawingFile &drawing);
QString checkinWorkspaceDrawing(const QString &path,DrawingFile &drawing,const QString &revision,const QString &comment,
                               RevisionRecord &record,const DocumentFileLock *lock=nullptr);
QString completeWorkspaceDrawingCheckin(const QString &path,DrawingFile &drawing,RevisionRecord &record,bool &matchesWorking,
                                       const DocumentFileLock *lock=nullptr);
QString workspaceDrawingHistory(const QString &path,const DrawingFile &drawing,QVector<RevisionRecord> &records);
QString readDrawingRevision(RevisionStore &store,const DocumentVersion &version,DrawingFile &drawing);
// Fissa la sorgente a un modello gia' archiviato nello stesso workspace.
QString pinWorkspaceDrawingModel(const QString &path,DrawingFile &drawing,const DocumentVersion &modelVersion);
}
#endif
