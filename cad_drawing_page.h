#pragma once
#include "cad_drawing_sheet.h"
#include <QStringList>
class QPainter;
namespace ForgeCad {
struct DrawingPageContext {
    QString modelName,drawingRevision,modelRevision;
    DocumentVersion drawingVersion,modelVersion;
};
struct DrawingPdfReport {QStringList warnings;};
// Solo presentazione; le etichette storiche sono risolte dal contenitore per ID.
DrawingPageContext defaultDrawingPageContext(const DrawingSheet &sheet);
void paintDrawingPage(QPainter &painter,const DrawingSheet &sheet,const DrawingPageContext &context);
QString drawingPaperName(const QSizeF &size);
}
