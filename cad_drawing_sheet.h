#ifndef FORGECAD_DRAWING_SHEET_H
#define FORGECAD_DRAWING_SHEET_H
#include "cad_drawing.h"
#include <QPointF>
#include <QSizeF>
#include <QVector>

namespace ForgeCad {
struct DrawingViewLayout {
    QUuid viewId;
    QString title;
    QPointF position; // origine del piano sul foglio, in mm (Y verso il basso)
    QVector<quint64> features;
};
struct DrawingDimensionLayout {
    QUuid dimensionId;
    QPointF position; // posizione del testo sul foglio, indipendente dalla misura
};
struct DrawingSheet {
    DrawingDocument drawing;
    QString title = QStringLiteral("Tavola");
    QSizeF size{297,210};
    QVector<DrawingViewLayout> views;
    QVector<DrawingDimensionLayout> dimensions;
};
// Solo metadati del foglio; nessuna duplicazione della geometria del modello.
QString encodeDrawingSheet(const DrawingSheet &sheet,QByteArray &bytes);
QString decodeDrawingSheet(const QByteArray &bytes,DrawingSheet &sheet);
}
#endif
