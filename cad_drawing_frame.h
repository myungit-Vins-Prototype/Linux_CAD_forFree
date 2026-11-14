#pragma once
#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <QVector>
class QPainter;
namespace ForgeCad {
// Coordinate in mm rispetto all'angolo inferiore destro della cornice.
// Il cartiglio resta ancorato allo stesso angolo cambiando il formato.
enum class DrawingFrameKind { Text, Line, Rectangle, Image };
struct DrawingFrameItem {
    QUuid id = QUuid::createUuid();
    DrawingFrameKind kind = DrawingFrameKind::Text;
    QRectF rect{-100, -30, 60, 8};
    QString text;
    double textHeight = 2.6, lineWidth = .25;
    bool bold = false;
    int alignment = 0;   // sinistra, centro, destra
    QByteArray imagePng; // incorporata: nessun percorso locale nel documento
};
struct DrawingSheet;
struct DrawingPageContext;
QString validateDrawingFrame(const DrawingSheet &sheet);
QVector<DrawingFrameItem> standardDrawingFrame();
void paintDrawingFrame(QPainter &painter, const DrawingSheet &sheet, const DrawingPageContext &context);
QRectF drawingFrameBounds(const DrawingSheet &sheet);
} // namespace ForgeCad
