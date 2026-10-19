#pragma once
#include "cad_drawing_file.h"
namespace ForgeCad {
struct LinkedDrawingEntry {QString path;DrawingFile drawing;};
// Cerca per UUID del modello nel workspace, escludendo archivi, cartelle nascoste e link a directory.
QString findLinkedDrawings(const QString &modelPath,const QUuid &modelId,QVector<LinkedDrawingEntry> &result);
QString chooseLinkedDrawing(QWidget *owner,const QString &modelPath,const QUuid &modelId);
}
