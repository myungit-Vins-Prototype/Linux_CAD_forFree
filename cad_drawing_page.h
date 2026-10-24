#pragma once
#include "cad_drawing_sheet.h"
#include <QStringList>
#include <QDateTime>
#include <QRectF>
class QPainter;
namespace ForgeCad {
struct DrawingProjectRevision {
    DocumentVersion version;
    quint64 sequence=0;
    QString label,author,comment;
    QDateTime createdUtc;
};
struct DrawingPageContext {
    QString modelName,drawingRevision,modelRevision;
    int sheetNumber=1,sheetCount=1;
    DocumentVersion drawingVersion,modelVersion;
    QVector<DrawingProjectRevision> projectRevisions;
};
struct DrawingPdfReport {QStringList warnings;};
// Solo presentazione; le etichette storiche sono risolte dal contenitore per ID.
DrawingPageContext defaultDrawingPageContext(const DrawingSheet &sheet);
void paintDrawingPage(QPainter &painter,const DrawingSheet &sheet,const DrawingPageContext &context);
QString drawingPaperName(const QSizeF &size);
struct DrawingRevisionTableLayout {QRectF rect;QVector<double> rowHeights;double fontHeight=2.5;};
// Non tronca righe o testi: se il foglio non basta richiede un formato maggiore.
QString layoutDrawingRevisionTable(const DrawingSheet &sheet,const DrawingPageContext &context,DrawingRevisionTableLayout &result);
}
