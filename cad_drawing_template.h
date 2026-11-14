#pragma once
#include "cad_drawing_sheet.h"
namespace ForgeCad {
struct DrawingSheetTemplate {
    QSizeF size{297, 210};
    DrawingTitleBlock titleBlock;
    DrawingStyle style;
    double scale = 1;
};
DrawingSheetTemplate drawingTemplateOf(const DrawingSheet &sheet);
void applyDrawingTemplate(DrawingSheet &sheet, const DrawingSheetTemplate &appearance);
QString encodeDrawingTemplate(const DrawingSheetTemplate &appearance, QByteArray &bytes);
QString decodeDrawingTemplate(const QByteArray &bytes, DrawingSheetTemplate &appearance);
QString saveDrawingTemplate(const QString &path, const DrawingSheetTemplate &appearance);
QString loadDrawingTemplate(const QString &path, DrawingSheetTemplate &appearance);
QString saveDefaultDrawingTemplate(const DrawingSheetTemplate &appearance, bool preferredFormat);
QString removeDefaultDrawingTemplate(const QSizeF &size);
QString loadDefaultDrawingTemplate(const QSizeF &size, DrawingSheetTemplate &appearance, bool &found);
QSizeF preferredDrawingTemplateSize();
} // namespace ForgeCad
