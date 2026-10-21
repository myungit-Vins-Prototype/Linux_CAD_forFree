#ifndef FORGECAD_DRAWING_FILE_H
#define FORGECAD_DRAWING_FILE_H
#include "cad_document_io.h"
#include "cad_drawing_editor.h"

namespace ForgeCad {
struct DrawingFile {
    DrawingSheet sheet;
    QString modelPath; // riferimento relativo alla tavola, non identita' del modello
    QByteArray modelDefinitionHash;
    QUuid baseCheckinId;
    QUuid modelArchiveId;
    QString modelArchivePath; // facoltativo: versione precisa del modello nell'archivio
};
QString encodeDrawingFile(const DrawingFile &drawing,QByteArray &bytes);
QString decodeDrawingFile(const QByteArray &bytes,DrawingFile &drawing);
QString loadDrawingFile(const QString &path,DrawingFile &drawing);
QString saveDrawingFile(const QString &path,const DrawingFile &drawing,const DocumentFileLock *lock=nullptr);
QString drawingModelDefinitionHash(const DocumentState &document,QByteArray &hash);
QString prepareDrawingEditorModel(DocumentState &document,DrawingEditorModel &model);
// Legge il modello esatto richiesto; non ripiega sull'ultima versione di una
// copia di lavoro quando la tavola riferisce un check-in preciso.
QString loadDrawingModel(const QString &drawingPath,const DrawingFile &drawing,DocumentState &model,
                         QString &resolvedPath,bool &definitionChanged,const QString &overridePath={});
void forkDrawingIdentity(DrawingFile &drawing);
}
#endif
