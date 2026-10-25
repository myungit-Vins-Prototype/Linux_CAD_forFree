#ifndef FORGECAD_DRAWING_FILE_H
#define FORGECAD_DRAWING_FILE_H
#include "cad_document_io.h"
#include "cad_drawing_editor.h"

namespace ForgeCad {
struct DrawingFile {
    DrawingSheet sheet; // primo foglio; identita' e modello comuni alla tavola
    QVector<DrawingSheet> otherSheets;
    QVector<QUuid> sheetIds; // vuoto solo nei file legacy a un foglio
    QString modelPath; // riferimento relativo alla tavola, non identita' del modello
    QByteArray modelDefinitionHash;
    QUuid baseCheckinId;
    QUuid modelArchiveId;
    QString modelArchivePath; // facoltativo: versione precisa del modello nell'archivio
};
int drawingSheetCount(const DrawingFile &drawing);
DrawingSheet &drawingSheetAt(DrawingFile &drawing,int index);
const DrawingSheet &drawingSheetAt(const DrawingFile &drawing,int index);
QUuid drawingSheetId(const DrawingFile &drawing,int index);
void synchronizeDrawingFileVersions(DrawingFile &drawing);
QString cloneDrawingSheet(const DrawingSheet &source,DrawingSheet &copy);
QString encodeDrawingFile(const DrawingFile &drawing,QByteArray &bytes);
QString decodeDrawingFile(const QByteArray &bytes,DrawingFile &drawing);
QString loadDrawingFile(const QString &path,DrawingFile &drawing);
QString saveDrawingFile(const QString &path,const DrawingFile &drawing,const DocumentFileLock *lock=nullptr);
QString drawingModelDefinitionHash(const DocumentState &document,QByteArray &hash);
// Accetta anche le impronte del formato 53 soltanto se il modello non ha
// nuovi nomi topologici degli schizzi che quel formato non rappresentava.
QString drawingModelDefinitionMatches(const DocumentState &document,const QByteArray &expected,bool &matches);
QString prepareDrawingEditorModel(DocumentState &document,DrawingEditorModel &model);
// Legge il modello esatto richiesto; non ripiega sull'ultima versione di una
// copia di lavoro quando la tavola riferisce un check-in preciso.
QString loadDrawingModel(const QString &drawingPath,const DrawingFile &drawing,DocumentState &model,
                         QString &resolvedPath,bool &definitionChanged,const QString &overridePath={});
void forkDrawingIdentity(DrawingFile &drawing);
}
#endif
