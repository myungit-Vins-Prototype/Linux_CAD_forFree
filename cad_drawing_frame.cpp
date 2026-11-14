#include "cad_drawing_frame.h"
#include "cad_drawing_page.h"
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QSet>
#include <cmath>
namespace ForgeCad {
QString validateDrawingFrame(const DrawingSheet &sheet) {
    const auto &b = sheet.titleBlock;
    if (!b.customLayout && !b.items.isEmpty())
        return QStringLiteral("Elementi di cartiglio senza layout personalizzato.");
    if (b.items.size() > 256)
        return QStringLiteral("Troppi elementi nel template (massimo 256).");
    QSet<QUuid> ids;
    qint64 images = 0, pixels = 0;
    for (const auto &item : b.items) {
        if (item.id.isNull() || ids.contains(item.id) || int(item.kind) < 0 || int(item.kind) > 3)
            return QStringLiteral("Identita' o tipo di elemento del cartiglio non valido.");
        ids.insert(item.id);
        const auto box = item.rect;
        for (double v : {box.x(), box.y(), box.width(), box.height(), item.textHeight, item.lineWidth})
            if (!std::isfinite(v))
                return QStringLiteral("Coordinate del cartiglio non finite.");
        if (box.x() < 10 - sheet.size.width() || box.y() < 10 - sheet.size.height() || box.right() > 10 ||
            box.bottom() > 10 || box.width() < 0 || box.height() < 0 || box.width() + box.height() <= 0)
            return QStringLiteral("Elemento del template esterno al foglio o privo di dimensioni.");
        if (item.kind != DrawingFrameKind::Line && (box.width() < .1 || box.height() < .1))
            return QStringLiteral("Riquadro del template troppo piccolo.");
        if (item.textHeight < .5 || item.textHeight > 30 || item.lineWidth < .05 || item.lineWidth > 3 ||
            item.alignment < 0 || item.alignment > 2 || item.text.size() > 4096)
            return QStringLiteral("Stile o testo del cartiglio non valido.");
        for (const auto ch : item.text)
            if (!ch.isPrint() && ch != '\n' && ch != '\t')
                return QStringLiteral("Testo del cartiglio non stampabile.");
        images += item.imagePng.size();
        if (images > 4 * 1024 * 1024)
            return QStringLiteral("Immagini del template troppo grandi (massimo totale 4 MiB).");
        if (item.kind == DrawingFrameKind::Image) {
            QBuffer buffer;
            buffer.setData(item.imagePng);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "PNG");
            const auto size = reader.size();
            pixels += qint64(size.width()) * size.height();
            if (!size.isValid() || size.width() > 4096 || size.height() > 4096 || pixels > 16 * 1024 * 1024 ||
                reader.read().isNull())
                return QStringLiteral("Logo PNG non valido o troppo grande (massimo 4096 pixel per lato).");
        } else if (!item.imagePng.isEmpty())
            return QStringLiteral("Immagine associata a un elemento che non e' un logo.");
    }
    return {};
}
QVector<DrawingFrameItem> standardDrawingFrame() {
    QVector<DrawingFrameItem> items;
    const auto shape = [&](DrawingFrameKind kind, QRectF rect) {
        DrawingFrameItem i;
        i.kind = kind;
        i.rect = rect;
        items.append(i);
    };
    shape(DrawingFrameKind::Rectangle, {-175, -42, 175, 42});
    for (double y : {-30., -19., -9.})
        shape(DrawingFrameKind::Line, {-175, y, 175, 0});
    shape(DrawingFrameKind::Line, {-70, -42, 0, 23});
    for (double x : {-115., -58.})
        shape(DrawingFrameKind::Line, {x, -19, 0, 19});
    const auto cell = [&](double x, double y, double w, double h, const QString &label, const QString &value) {
        DrawingFrameItem i;
        i.rect = {x + 2, y + .6, w - 4, 3};
        i.text = label;
        i.textHeight = 1.7;
        items.append(i);
        i.id = QUuid::createUuid();
        i.rect = {x + 2, y + 3.5, w - 4, h - 4};
        i.text = value;
        i.textHeight = 2.6;
        items.append(i);
    };
    cell(-175, -42, 105, 12, "TITOLO", "{titolo}");
    cell(-70, -42, 70, 12, "CODICE TAVOLA", "{codice}");
    cell(-175, -30, 105, 11, "MODELLO", "{modello}");
    cell(-70, -30, 70, 11, "ORGANIZZAZIONE", "{organizzazione}");
    cell(-175, -19, 60, 10, "DISEGNATO DA", "{autore}");
    cell(-115, -19, 57, 10, "DATA", "{data}");
    cell(-58, -19, 58, 10, "SCALA / UNITA'", "{scala} / mm");
    cell(-175, -9, 60, 9, "REVISIONE TAVOLA", "{revisione_tavola}");
    cell(-115, -9, 57, 9, "REVISIONE MODELLO", "{revisione_modello}");
    cell(-58, -9, 58, 9, "FORMATO / FOGLIO", "{formato} / {foglio} di {fogli}");
    return items;
}
QRectF drawingFrameBounds(const DrawingSheet &sheet) {
    const auto origin = QPointF(sheet.size.width() - 10, sheet.size.height() - 10);
    if (!sheet.titleBlock.customLayout)
        return QRectF(origin - QPointF(175, 42), QSizeF(175, 42));
    if (sheet.titleBlock.items.empty())
        return QRectF(origin, QSizeF(0,0));
    double left = 0, top = 0, right = 0, bottom = 0;
    bool first = true;
    for (const auto &i : sheet.titleBlock.items) {
        const auto b = i.rect.translated(origin);
        if (first) {
            left = b.left();
            top = b.top();
            right = b.right();
            bottom = b.bottom();
            first = false;
        } else {
            left = std::min(left, b.left());
            top = std::min(top, b.top());
            right = std::max(right, b.right());
            bottom = std::max(bottom, b.bottom());
        }
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}
void paintDrawingFrame(QPainter &p, const DrawingSheet &sheet, const DrawingPageContext &context) {
    QString scale = drawingScaleText(drawingSheetScale(sheet));
    if (!drawingUsesSheetScale(sheet) && !sheet.drawing.views.empty())
        for (const auto &v : sheet.drawing.views)
            if (v.scale != sheet.drawing.views.front().scale) {
                scale = QStringLiteral("Vedi viste");
                break;
            }
    const QMap<QString, QString> fields = {{"titolo", sheet.title},
                                           {"codice", sheet.titleBlock.code},
                                           {"modello", context.modelName},
                                           {"organizzazione", sheet.titleBlock.organization},
                                           {"autore", sheet.titleBlock.author},
                                           {"data", sheet.titleBlock.date},
                                           {"scala", scale},
                                           {"formato", drawingPaperName(sheet.size)},
                                           {"foglio", QString::number(context.sheetNumber)},
                                           {"fogli", QString::number(context.sheetCount)},
                                           {"revisione_tavola", context.drawingRevision},
                                           {"revisione_modello", context.modelRevision}};
    const QPointF origin(sheet.size.width() - 10, sheet.size.height() - 10);
    p.save();
    p.setBrush(Qt::NoBrush);
    for (const auto &i : sheet.titleBlock.items) {
        const auto rect = i.rect.translated(origin);
        p.setPen(QPen(QColor("#222222"), i.lineWidth));
        if (i.kind == DrawingFrameKind::Line) {
            p.drawLine(rect.topLeft(), rect.bottomRight());
            continue;
        }
        if (i.kind == DrawingFrameKind::Rectangle) {
            p.drawRect(rect);
            continue;
        }
        if (i.kind == DrawingFrameKind::Image) {
            const auto image = QImage::fromData(i.imagePng, "PNG");
            if (!image.isNull()) {
                auto size = QSizeF(image.size());
                size.scale(rect.size(), Qt::KeepAspectRatio);
                p.drawImage(QRectF(rect.center() - QPointF(size.width() / 2, size.height() / 2), size), image);
            }
            continue;
        }
        QString value = i.text;
        for (auto it = fields.cbegin(); it != fields.cend(); ++it)
            value.replace("{" + it.key() + "}", it.value());
        p.save();
        QFont font(sheet.style.fontFamily);
        font.setPixelSize(100);
        font.setBold(i.bold);
        p.setFont(font);
        p.setClipRect(rect);
        const double factor = i.textHeight / 100;
        p.translate(rect.topLeft());
        p.scale(factor, factor);
        p.drawText(QRectF(QPointF(), rect.size() / factor),
                   (i.alignment == 1   ? Qt::AlignHCenter
                    : i.alignment == 2 ? Qt::AlignRight
                                       : Qt::AlignLeft) |
                       Qt::AlignVCenter | Qt::TextWordWrap,
                   value);
        p.restore();
    }
    p.restore();
}
} // namespace ForgeCad
