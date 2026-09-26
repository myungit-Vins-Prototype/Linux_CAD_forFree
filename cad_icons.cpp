#include "cad_icons.h"

#include <QFont>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include <cmath>
#include <functional>
#include <map>

namespace ForgeCad {
namespace {

// Colori di un'icona: tratto principale, accento (l'entita' o la parte che il
// comando crea), costruzione, rimozione, conferma, e i toni delle facce dei solidi.
struct IconPalette {
    QColor ink, accent, construction, remove, confirm, faceLight, faceMid, faceDark;
};

IconPalette palette(QIcon::Mode mode) {
    if (mode == QIcon::Disabled) {
        const QColor grey(110, 120, 128);
        return {grey, grey, grey, grey, grey, QColor(90, 98, 105), QColor(75, 82, 88), QColor(62, 68, 74)};
    }
    return {QColor(216, 228, 234), QColor(255, 176, 74), QColor(96, 170, 255), QColor(255, 104, 96),
            QColor(96, 214, 138), QColor(150, 186, 212), QColor(104, 140, 168), QColor(72, 102, 128)};
}

using Draw = std::function<void(QPainter &, const IconPalette &)>;

// Strumenti di disegno comuni (coordinate in una griglia di 24 x 24).
QPen stroke(const QColor &color, double width = 1.6, Qt::PenStyle style = Qt::SolidLine) {
    QPen pen(color, width, style, Qt::RoundCap, Qt::RoundJoin);
    if (style == Qt::DashLine) pen.setDashPattern({2.2, 1.8});
    return pen;
}

void dot(QPainter &p, const QPointF &center, const QColor &color, double radius = 1.9) {
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(center, radius, radius);
    p.restore();
}

void square(QPainter &p, const QPointF &center, const QColor &color, double half = 1.6) {
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRect(QRectF(center.x() - half, center.y() - half, 2 * half, 2 * half));
    p.restore();
}

void line(QPainter &p, const QPointF &a, const QPointF &b, const QPen &pen) {
    p.setPen(pen);
    p.drawLine(a, b);
}

void arrowHead(QPainter &p, const QPointF &tip, const QPointF &from, const QColor &color, double size = 3.6) {
    const QPointF d = tip - from;
    const double l = std::hypot(d.x(), d.y());
    if (l <= 0.0) return;
    const QPointF u = d / l, n(-u.y(), u.x());
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(QPolygonF({tip, tip - u * size + n * size * 0.55, tip - u * size - n * size * 0.55}));
    p.restore();
}

void polygon(QPainter &p, const QPolygonF &points, const QColor &fill, const QPen &pen) {
    p.setPen(pen);
    p.setBrush(fill);
    p.drawPolygon(points);
}

// Cubo in assonometria: facce superiore, sinistra (frontale) e destra.
// `highlight` 0/1/2 colora una faccia con l'accento (-1 nessuna); `faces`
// falso = solo spigoli; `edges` falso = solo facce.
void cube(QPainter &p, const IconPalette &c, int highlight = -1, bool faces = true, bool edges = true, double scale = 1.0,
          const QPointF &offset = QPointF()) {
    const auto at = [&](double x, double y) { return offset + QPointF(12 + (x - 12) * scale, 12 + (y - 12) * scale); };
    const QPolygonF top({at(12, 3), at(20.5, 7.2), at(12, 11.4), at(3.5, 7.2)});
    const QPolygonF left({at(3.5, 7.2), at(12, 11.4), at(12, 21), at(3.5, 16.8)});
    const QPolygonF right({at(12, 11.4), at(20.5, 7.2), at(20.5, 16.8), at(12, 21)});
    const QPolygonF polys[3] = {top, left, right};
    const QColor tones[3] = {c.faceLight, c.faceMid, c.faceDark};
    const QPen edgePen = edges ? stroke(c.ink, 1.1) : QPen(Qt::NoPen);
    for (int k = 0; k < 3; ++k) {
        QColor fill = faces ? tones[k] : QColor(Qt::transparent);
        if (k == highlight) fill = c.accent;
        polygon(p, polys[k], fill, edgePen);
    }
}

std::map<QString, Draw> &registry() {
    static std::map<QString, Draw> icons;
    if (!icons.empty()) return icons;

    // --- Schizzo: disegno ---------------------------------------------------
    icons["select"] = [](QPainter &p, const IconPalette &c) {
        polygon(p, QPolygonF({{6, 3}, {6, 19}, {10, 15.2}, {13, 21}, {15.6, 19.8}, {12.7, 14.2}, {18, 14}}), c.ink,
                stroke(QColor(20, 30, 40), 1.0));
    };
    icons["line"] = [](QPainter &p, const IconPalette &c) {
        line(p, {4, 19}, {20, 5}, stroke(c.accent, 1.8));
        dot(p, {4, 19}, c.ink);
        dot(p, {20, 5}, c.ink);
    };
    icons["polyline"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.accent, 1.8));
        p.setBrush(Qt::NoBrush);
        const QPolygonF points({{3, 18}, {9, 6}, {15, 16}, {21, 5}});
        p.drawPolyline(points);
        for (const QPointF &point : points) dot(p, point, c.ink, 1.7);
    };
    icons["constructionLine"] = [](QPainter &p, const IconPalette &c) {
        line(p, {4, 19}, {20, 5}, stroke(c.construction, 1.8, Qt::DashLine));
        dot(p, {4, 19}, c.ink);
        dot(p, {20, 5}, c.ink);
    };
    icons["spline"] = [](QPainter &p, const IconPalette &c) {
        QPainterPath path({3, 17});
        path.cubicTo({7, 3}, {13, 23}, {21, 7});
        p.setPen(stroke(c.accent, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        for (const QPointF &point : {QPointF(3, 17), QPointF(10.5, 12.75), QPointF(21, 7)}) dot(p, point, c.ink, 1.7);
    };
    icons["nurbs"] = [](QPainter &p, const IconPalette &c) {
        const QPolygonF poles({{3, 19}, {7, 4}, {17, 20}, {21, 5}});
        p.setPen(stroke(c.construction, 1.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poles);
        QPainterPath path(poles.at(0));
        path.cubicTo(poles.at(1), poles.at(2), poles.at(3));
        p.setPen(stroke(c.accent, 1.8));
        p.drawPath(path);
        for (const QPointF &point : poles) square(p, point, c.ink, 1.5);
    };
    icons["circle"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.accent, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
        line(p, {12, 12}, {18, 6}, stroke(c.ink, 1.0, Qt::DashLine));
        dot(p, {12, 12}, c.ink, 1.7);
    };
    icons["arcCenter"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.accent, 1.8));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.arcMoveTo(QRectF(-3, 6, 22, 22), 0);
        path.arcTo(QRectF(-3, 6, 22, 22), 0, 90);
        p.drawPath(path);
        line(p, {8, 17}, {19, 17}, stroke(c.ink, 1.0, Qt::DashLine));
        line(p, {8, 17}, {8, 6}, stroke(c.ink, 1.0, Qt::DashLine));
        dot(p, {8, 17}, c.ink, 1.8);
        dot(p, {19, 17}, c.ink, 1.6);
        dot(p, {8, 6}, c.ink, 1.6);
    };
    icons["arcThreePoint"] = [](QPainter &p, const IconPalette &c) {
        QPainterPath path;
        const QRectF box(3, 8, 18, 18);
        path.arcMoveTo(box, 20);
        path.arcTo(box, 20, 140);
        p.setPen(stroke(c.accent, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        for (const QPointF &point : {QPointF(20.46, 13.92), QPointF(12, 8), QPointF(3.54, 13.92)}) dot(p, point, c.ink, 1.8);
    };
    icons["arcTangent"] = [](QPainter &p, const IconPalette &c) {
        line(p, {2.5, 18}, {11, 18}, stroke(c.ink, 1.8));
        QPainterPath path;
        const QRectF box(4, 4, 14, 14);
        path.arcMoveTo(box, -90);
        path.arcTo(box, -90, 180);
        p.setPen(stroke(c.accent, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        dot(p, {11, 18}, c.ink, 1.8);
        dot(p, {11, 4}, c.ink, 1.6);
    };
    icons["polygon"] = [](QPainter &p, const IconPalette &c) {
        QPolygonF hexagon;
        for (int k = 0; k < 6; ++k) hexagon << QPointF(12 + 9 * std::cos(M_PI / 3 * k + M_PI / 6), 12 + 9 * std::sin(M_PI / 3 * k + M_PI / 6));
        polygon(p, hexagon, Qt::NoBrush, stroke(c.accent, 1.8));
        dot(p, {12, 12}, c.ink, 1.6);
    };
    icons["rectangle"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.accent, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(4, 6, 16, 12));
        dot(p, {4, 18}, c.ink);
        dot(p, {20, 6}, c.ink);
    };
    icons["centerRectangle"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.accent, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(4, 6, 16, 12));
        line(p, {12, 12}, {20, 6}, stroke(c.ink, 1.0, Qt::DashLine));
        dot(p, {12, 12}, c.ink);
        dot(p, {20, 6}, c.ink);
    };
    icons["ellipse"] = [](QPainter &p, const IconPalette &c) {
        line(p, {2.5, 12}, {21.5, 12}, stroke(c.ink, 0.9, Qt::DashLine));
        line(p, {12, 5.5}, {12, 18.5}, stroke(c.ink, 0.9, Qt::DashLine));
        p.setPen(stroke(c.accent, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(12, 12), 9.5, 6);
        dot(p, {12, 12}, c.ink, 1.6);
    };

    // --- Schizzo: modifica -----------------------------------------------------
    icons["toggleConstruction"] = [](QPainter &p, const IconPalette &c) {
        line(p, {3, 14}, {12, 5}, stroke(c.accent, 1.8));
        line(p, {12, 19}, {21, 10}, stroke(c.construction, 1.8, Qt::DashLine));
        line(p, {8.5, 12.5}, {12.5, 15.5}, stroke(c.ink, 1.2));
        arrowHead(p, {13.5, 16.3}, {8.5, 12.5}, c.ink, 3.0);
        arrowHead(p, {7.5, 11.7}, {12.5, 15.5}, c.ink, 3.0);
    };
    icons["trim"] = [](QPainter &p, const IconPalette &c) {
        line(p, {12, 3}, {12, 21}, stroke(c.ink, 1.8));
        line(p, {3, 12}, {12, 12}, stroke(c.accent, 1.8));
        line(p, {12, 12}, {21, 12}, stroke(c.remove, 1.8, Qt::DashLine));
        line(p, {16, 8}, {20, 16}, stroke(c.remove, 1.2));
        line(p, {20, 8}, {16, 16}, stroke(c.remove, 1.2));
    };
    icons["extend"] = [](QPainter &p, const IconPalette &c) {
        line(p, {20, 3}, {20, 21}, stroke(c.ink, 1.8));
        line(p, {3, 12}, {10, 12}, stroke(c.accent, 1.8));
        line(p, {10, 12}, {16, 12}, stroke(c.accent, 1.4, Qt::DashLine));
        arrowHead(p, {19.2, 12}, {12, 12}, c.accent);
    };
    icons["split"] = [](QPainter &p, const IconPalette &c) {
        line(p, {3, 18}, {10.2, 13.2}, stroke(c.accent, 1.8));
        line(p, {13.8, 10.8}, {21, 6}, stroke(c.accent, 1.8));
        line(p, {8.5, 5.5}, {15.5, 18.5}, stroke(c.ink, 1.0, Qt::DashLine));
        dot(p, {10.2, 13.2}, c.ink, 1.7);
        dot(p, {13.8, 10.8}, c.ink, 1.7);
    };
    icons["sketchFillet"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.ink, 0.9, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({{5, 12}, {5, 5}, {12, 5}}));
        line(p, {5, 21}, {5, 12}, stroke(c.ink, 1.8));
        line(p, {12, 5}, {21, 5}, stroke(c.ink, 1.8));
        QPainterPath path;
        const QRectF box(5, 5, 14, 14);
        path.arcMoveTo(box, 180);
        path.arcTo(box, 180, -90);
        p.setPen(stroke(c.accent, 1.9));
        p.drawPath(path);
    };
    icons["sketchChamfer"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.ink, 0.9, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({{5, 12}, {5, 5}, {12, 5}}));
        line(p, {5, 21}, {5, 12}, stroke(c.ink, 1.8));
        line(p, {12, 5}, {21, 5}, stroke(c.ink, 1.8));
        line(p, {5, 12}, {12, 5}, stroke(c.accent, 1.9));
    };

    // --- Schizzo: vincoli e quote ------------------------------------------------
    icons["constraints"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.ink, 1.2));
        p.setBrush(QColor(255, 255, 255, 18));
        p.drawRoundedRect(QRectF(2.5, 3.5, 19, 17), 2.5, 2.5);
        line(p, {5.5, 16.5}, {11.5, 16.5}, stroke(c.accent, 1.6));
        line(p, {8.5, 16.5}, {8.5, 8}, stroke(c.accent, 1.6));
        line(p, {13.5, 16.5}, {15.5, 8}, stroke(c.accent, 1.6));
        line(p, {16.8, 16.5}, {18.8, 8}, stroke(c.accent, 1.6));
    };
    icons["dimension"] = [](QPainter &p, const IconPalette &c) {
        line(p, {4.5, 7}, {4.5, 20}, stroke(c.ink, 1.1));
        line(p, {19.5, 7}, {19.5, 20}, stroke(c.ink, 1.1));
        line(p, {6, 11}, {18, 11}, stroke(c.accent, 1.3));
        arrowHead(p, {4.8, 11}, {10, 11}, c.accent);
        arrowHead(p, {19.2, 11}, {14, 11}, c.accent);
        line(p, {4.5, 17}, {19.5, 17}, stroke(c.ink, 1.8));
    };
    const auto letter = [](const QString &text, bool framed) {
        return [text, framed](QPainter &p, const IconPalette &c) {
            if (framed) {
                p.setPen(stroke(c.ink, 1.1));
                p.setBrush(QColor(255, 255, 255, 18));
                p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
            }
            QFont font(QStringLiteral("Sans"));
            font.setPixelSize(13);
            font.setBold(true);
            p.setFont(font);
            p.setPen(c.accent);
            p.drawText(QRectF(3, 3, 18, 18), Qt::AlignCenter, text);
        };
    };
    icons["constraintAuto"] = letter(QStringLiteral("A"), true);
    icons["constraintFree"] = [](QPainter &p, const IconPalette &c) {
        line(p, {4, 19}, {20, 5}, stroke(c.accent, 1.8));
        p.setPen(stroke(c.ink, 1.1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(7, 7), 3.5, 3.5);
        line(p, {4.5, 9.5}, {9.5, 4.5}, stroke(c.ink, 1.1));
    };
    icons["constraintHorizontal"] = [](QPainter &p, const IconPalette &c) {
        line(p, {3, 15}, {21, 15}, stroke(c.accent, 1.8));
        dot(p, {3, 15}, c.ink, 1.6);
        dot(p, {21, 15}, c.ink, 1.6);
        QFont font(QStringLiteral("Sans"));
        font.setPixelSize(10);
        font.setBold(true);
        p.setFont(font);
        p.setPen(c.ink);
        p.drawText(QRectF(6, 1, 12, 11), Qt::AlignCenter, QStringLiteral("H"));
    };
    icons["constraintVertical"] = [](QPainter &p, const IconPalette &c) {
        line(p, {9, 3}, {9, 21}, stroke(c.accent, 1.8));
        dot(p, {9, 3}, c.ink, 1.6);
        dot(p, {9, 21}, c.ink, 1.6);
        QFont font(QStringLiteral("Sans"));
        font.setPixelSize(10);
        font.setBold(true);
        p.setFont(font);
        p.setPen(c.ink);
        p.drawText(QRectF(11, 6, 11, 12), Qt::AlignCenter, QStringLiteral("V"));
    };
    icons["constraintLength"] = [](QPainter &p, const IconPalette &c) {
        line(p, {3, 18}, {21, 18}, stroke(c.ink, 1.8));
        line(p, {5, 11}, {19, 11}, stroke(c.accent, 1.2));
        arrowHead(p, {3.5, 11}, {9, 11}, c.accent, 3.2);
        arrowHead(p, {20.5, 11}, {15, 11}, c.accent, 3.2);
        line(p, {3, 8}, {3, 16}, stroke(c.ink, 1.0));
        line(p, {21, 8}, {21, 16}, stroke(c.ink, 1.0));
        QFont font(QStringLiteral("Sans"));
        font.setPixelSize(8);
        font.setBold(true);
        p.setFont(font);
        p.setPen(c.ink);
        p.drawText(QRectF(4, 1, 16, 8), Qt::AlignCenter, QStringLiteral("L"));
    };
    icons["constraintAngle"] = [](QPainter &p, const IconPalette &c) {
        line(p, {3, 20}, {21, 20}, stroke(c.ink, 1.8));
        line(p, {3, 20}, {17, 5}, stroke(c.ink, 1.8));
        QPainterPath path;
        const QRectF box(-7, 10, 20, 20);
        path.arcMoveTo(box, 0);
        path.arcTo(box, 0, 47);
        p.setPen(stroke(c.accent, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    };
    icons["snap"] = [](QPainter &p, const IconPalette &c) {
        QPainterPath path({6, 4});
        path.lineTo(6, 12);
        path.arcTo(QRectF(6, 6, 12, 12), 180, 180);
        path.lineTo(18, 4);
        p.setPen(stroke(c.remove, 3.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        line(p, {6, 3}, {6, 6.5}, QPen(c.ink, 3.4, Qt::SolidLine, Qt::FlatCap));
        line(p, {18, 3}, {18, 6.5}, QPen(c.ink, 3.4, Qt::SolidLine, Qt::FlatCap));
        dot(p, {12, 21.5}, c.accent, 1.6);
    };
    icons["originSnap"] = [](QPainter &p, const IconPalette &c) {
        line(p, {12, 2.5}, {12, 21.5}, stroke(c.ink, 1.0));
        line(p, {2.5, 12}, {21.5, 12}, stroke(c.ink, 1.0));
        p.setPen(stroke(c.accent, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(12, 12), 5, 5);
        dot(p, {12, 12}, c.accent, 1.8);
    };
    icons["polygonSides"] = [](QPainter &p, const IconPalette &c) {
        QPolygonF pentagon;
        for (int k = 0; k < 5; ++k) pentagon << QPointF(10 + 7.5 * std::cos(2 * M_PI / 5 * k - M_PI / 2), 11 + 7.5 * std::sin(2 * M_PI / 5 * k - M_PI / 2));
        polygon(p, pentagon, Qt::NoBrush, stroke(c.accent, 1.6));
        QFont font(QStringLiteral("Sans"));
        font.setPixelSize(9);
        font.setBold(true);
        p.setFont(font);
        p.setPen(c.ink);
        p.drawText(QRectF(13, 13, 11, 11), Qt::AlignCenter, QStringLiteral("n"));
    };
    icons["exitSketch"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.confirm, 3.0));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({{4.5, 12.5}, {10, 18}, {20, 6}}));
    };

    // --- Documento e modifica -------------------------------------------------
    icons["new"] = [](QPainter &p, const IconPalette &c) {
        polygon(p, QPolygonF({{5, 2.5}, {14, 2.5}, {19, 7.5}, {19, 21.5}, {5, 21.5}}), QColor(255, 255, 255, 25), stroke(c.ink, 1.3));
        p.setPen(stroke(c.ink, 1.1));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({{14, 2.5}, {14, 7.5}, {19, 7.5}}));
        line(p, {12, 11}, {12, 18}, stroke(c.accent, 1.7));
        line(p, {8.5, 14.5}, {15.5, 14.5}, stroke(c.accent, 1.7));
    };
    icons["open"] = [](QPainter &p, const IconPalette &c) {
        polygon(p, QPolygonF({{2.5, 5}, {9, 5}, {11, 7.5}, {20, 7.5}, {20, 19}, {2.5, 19}}), c.faceMid, stroke(c.ink, 1.2));
        polygon(p, QPolygonF({{5, 11}, {22, 11}, {19.5, 19}, {2.5, 19}}), c.accent, stroke(c.ink, 1.2));
    };
    icons["save"] = [](QPainter &p, const IconPalette &c) {
        polygon(p, QPolygonF({{3, 3}, {18, 3}, {21, 6}, {21, 21}, {3, 21}}), c.faceMid, stroke(c.ink, 1.2));
        polygon(p, QPolygonF({{7, 3}, {16, 3}, {16, 9}, {7, 9}}), c.faceLight, QPen(Qt::NoPen));
        polygon(p, QPolygonF({{6, 13}, {18, 13}, {18, 21}, {6, 21}}), QColor(235, 240, 244), QPen(Qt::NoPen));
        square(p, {13.5, 6}, c.faceDark, 1.3);
        line(p, {8, 16}, {16, 16}, stroke(c.faceMid, 1.0));
        line(p, {8, 18.5}, {16, 18.5}, stroke(c.faceMid, 1.0));
    };
    const auto curvedArrow = [](bool redo) {
        return [redo](QPainter &p, const IconPalette &c) {
            p.save();
            if (redo) {
                p.translate(24, 0);
                p.scale(-1, 1);
            }
            QPainterPath path({8, 8});
            path.lineTo(14, 8);
            path.cubicTo({22, 8}, {22, 19}, {14, 19});
            path.lineTo(7, 19);
            p.setPen(stroke(c.ink, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            arrowHead(p, {3, 8}, {9, 8}, c.accent, 5.0);
            p.restore();
        };
    };
    icons["undo"] = curvedArrow(false);
    icons["redo"] = curvedArrow(true);
    icons["delete"] = [](QPainter &p, const IconPalette &c) {
        line(p, {4, 6}, {20, 6}, stroke(c.ink, 1.6));
        line(p, {9.5, 6}, {10, 3.5}, stroke(c.ink, 1.4));
        line(p, {10, 3.5}, {14, 3.5}, stroke(c.ink, 1.4));
        line(p, {14, 3.5}, {14.5, 6}, stroke(c.ink, 1.4));
        polygon(p, QPolygonF({{6, 8}, {18, 8}, {17, 21}, {7, 21}}), QColor(255, 104, 96, 60), stroke(c.remove, 1.4));
        line(p, {10, 11}, {10.3, 18}, stroke(c.remove, 1.1));
        line(p, {14, 11}, {13.7, 18}, stroke(c.remove, 1.1));
    };

    // --- Modellazione ---------------------------------------------------------
    icons["newSketch"] = [](QPainter &p, const IconPalette &c) {
        polygon(p, QPolygonF({{1.5, 19}, {7.5, 13}, {22.5, 13}, {16.5, 19}}), QColor(96, 170, 255, 70), stroke(c.construction, 1.2));
        p.save();
        p.translate(14, 11);
        p.rotate(-50);
        polygon(p, QPolygonF({{-1.8, -9}, {1.8, -9}, {1.8, 3}, {0, 6}, {-1.8, 3}}), c.accent, stroke(QColor(40, 30, 20), 0.8));
        p.restore();
        dot(p, {9.3, 15.5}, c.ink, 1.1);
    };
    icons["faceSketch"] = [](QPainter &p, const IconPalette &c) {
        cube(p, c, 0, true, true, 0.85, QPointF(-2, 2));
        p.save();
        p.translate(17, 7);
        p.rotate(-40);
        polygon(p, QPolygonF({{-1.6, -6.5}, {1.6, -6.5}, {1.6, 3}, {0, 5.5}, {-1.6, 3}}), c.ink, stroke(QColor(20, 30, 40), 0.8));
        p.restore();
    };
    icons["extrude"] = [](QPainter &p, const IconPalette &c) {
        polygon(p, QPolygonF({{3, 12}, {12, 12}, {12, 21}, {3, 21}}), c.faceMid, stroke(c.ink, 1.0));
        polygon(p, QPolygonF({{3, 12}, {7, 8}, {16, 8}, {12, 12}}), c.faceLight, stroke(c.ink, 1.0));
        polygon(p, QPolygonF({{12, 12}, {16, 8}, {16, 17}, {12, 21}}), c.faceDark, stroke(c.ink, 1.0));
        line(p, {3, 21}, {12, 21}, stroke(c.accent, 1.8));
        line(p, {20.5, 21}, {20.5, 7}, stroke(c.accent, 1.8));
        arrowHead(p, {20.5, 2.5}, {20.5, 8}, c.accent, 4.2);
    };
    icons["revolve"] = [](QPainter &p, const IconPalette &c) {
        line(p, {8, 1.5}, {8, 22.5}, stroke(c.construction, 1.1, Qt::DashLine));
        polygon(p, QPolygonF({{10.5, 6}, {15, 6}, {17, 11}, {15, 17}, {10.5, 17}}), c.faceMid, stroke(c.ink, 1.1));
        QPainterPath path;
        const QRectF box(0.5, 16, 15, 6);
        path.arcMoveTo(box, 200);
        path.arcTo(box, 200, 150);
        p.setPen(stroke(c.accent, 1.7));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        arrowHead(p, {15.6, 17.6}, {14, 21}, c.accent, 4.0);
    };
    icons["fillet"] = [](QPainter &p, const IconPalette &c) {
        QPainterPath body({3, 21});
        body.lineTo(3, 12);
        body.arcTo(QRectF(3, 3, 18, 18), 180, -90);
        body.lineTo(21, 3);
        body.lineTo(21, 21);
        body.closeSubpath();
        p.setPen(stroke(c.ink, 1.0));
        p.setBrush(c.faceMid);
        p.drawPath(body);
        p.setPen(stroke(c.ink, 1.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({{3, 11}, {3, 3}, {11, 3}}));
        QPainterPath arc;
        arc.arcMoveTo(QRectF(3, 3, 18, 18), 180);
        arc.arcTo(QRectF(3, 3, 18, 18), 180, -90);
        p.setPen(stroke(c.accent, 2.6));
        p.drawPath(arc);
    };
    icons["chamfer"] = [](QPainter &p, const IconPalette &c) {
        polygon(p, QPolygonF({{3, 21}, {3, 12}, {12, 3}, {21, 3}, {21, 21}}), c.faceMid, stroke(c.ink, 1.0));
        p.setPen(stroke(c.ink, 1.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({{3, 11}, {3, 3}, {11, 3}}));
        line(p, {3, 12}, {12, 3}, stroke(c.accent, 2.6));
    };
    const auto booleanIcon = [](int operation) {
        return [operation](QPainter &p, const IconPalette &c) {
            QPainterPath a, b;
            a.addEllipse(QPointF(9, 12), 6.8, 6.8);
            b.addEllipse(QPointF(15, 12), 6.8, 6.8);
            const QPainterPath result = operation == 0 ? a.united(b) : operation == 1 ? a.intersected(b) : a.subtracted(b);
            p.setPen(Qt::NoPen);
            p.setBrush(c.accent);
            p.drawPath(result);
            p.setBrush(Qt::NoBrush);
            p.setPen(stroke(c.ink, 1.1));
            p.drawPath(a);
            p.setPen(stroke(c.ink, 1.1, operation == 2 ? Qt::DashLine : Qt::SolidLine));
            p.drawPath(b);
        };
    };
    icons["union"] = booleanIcon(0);
    icons["intersection"] = booleanIcon(1);
    icons["difference"] = booleanIcon(2);
    icons["box"] = [](QPainter &p, const IconPalette &c) { cube(p, c); };
    icons["cylinder"] = [](QPainter &p, const IconPalette &c) {
        QPainterPath side({4, 6});
        side.lineTo(4, 18);
        side.arcTo(QRectF(4, 15, 16, 6), 180, 180);
        side.lineTo(20, 6);
        side.closeSubpath();
        p.setPen(stroke(c.ink, 1.0));
        p.setBrush(c.faceMid);
        p.drawPath(side);
        p.setBrush(c.faceLight);
        p.drawEllipse(QRectF(4, 3, 16, 6));
    };
    icons["sphere"] = [](QPainter &p, const IconPalette &c) {
        QRadialGradient gradient(QPointF(9, 8), 13);
        gradient.setColorAt(0, c.faceLight);
        gradient.setColorAt(1, c.faceDark);
        p.setPen(stroke(c.ink, 1.0));
        p.setBrush(gradient);
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.setBrush(Qt::NoBrush);
        p.setPen(stroke(c.ink, 0.7));
        p.drawEllipse(QRectF(3, 9.5, 18, 5));
    };
    icons["cone"] = [](QPainter &p, const IconPalette &c) {
        QPainterPath side({12, 2.5});
        side.lineTo(3.5, 18);
        side.arcTo(QRectF(3.5, 15, 17, 6), 180, 180);
        side.closeSubpath();
        p.setPen(stroke(c.ink, 1.0));
        p.setBrush(c.faceMid);
        p.drawPath(side);
        p.setBrush(Qt::NoBrush);
        QPainterPath back;
        back.arcMoveTo(QRectF(3.5, 15, 17, 6), 0);
        back.arcTo(QRectF(3.5, 15, 17, 6), 0, 180);
        p.setPen(stroke(c.ink, 0.7, Qt::DashLine));
        p.drawPath(back);
    };
    icons["torus"] = [](QPainter &p, const IconPalette &c) {
        QPainterPath ring;
        ring.addEllipse(QRectF(2, 5, 20, 14));
        QPainterPath hole;
        hole.addEllipse(QRectF(8, 10, 8, 4));
        p.setPen(stroke(c.ink, 1.0));
        p.setBrush(c.faceMid);
        p.drawPath(ring.subtracted(hole));
        p.setBrush(Qt::NoBrush);
        QPainterPath highlight;
        highlight.arcMoveTo(QRectF(4.5, 7, 15, 10), 20);
        highlight.arcTo(QRectF(4.5, 7, 15, 10), 20, 140);
        p.setPen(stroke(c.faceLight, 1.2));
        p.drawPath(highlight);
    };

    // --- Vista ------------------------------------------------------------------
    icons["zoomAll"] = [](QPainter &p, const IconPalette &c) {
        p.setPen(stroke(c.construction, 1.3));
        p.setBrush(Qt::NoBrush);
        for (const QPointF &corner : {QPointF(2.5, 2.5), QPointF(21.5, 2.5), QPointF(2.5, 21.5), QPointF(21.5, 21.5)}) {
            const double sx = corner.x() < 12 ? 1 : -1, sy = corner.y() < 12 ? 1 : -1;
            p.drawPolyline(QPolygonF({corner + QPointF(0, 5 * sy), corner, corner + QPointF(5 * sx, 0)}));
        }
        p.setPen(stroke(c.ink, 1.6));
        p.drawEllipse(QPointF(10.5, 10.5), 4.8, 4.8);
        line(p, {14, 14}, {18.5, 18.5}, stroke(c.accent, 2.6));
    };
    icons["viewFront"] = [](QPainter &p, const IconPalette &c) { cube(p, c, 1); };
    icons["viewTop"] = [](QPainter &p, const IconPalette &c) { cube(p, c, 0); };
    icons["viewRight"] = [](QPainter &p, const IconPalette &c) { cube(p, c, 2); };
    icons["viewIso"] = [](QPainter &p, const IconPalette &c) {
        cube(p, c);
        line(p, {12, 11.4}, {12, 21}, stroke(c.accent, 1.5));
        line(p, {12, 11.4}, {3.5, 7.2}, stroke(c.accent, 1.5));
        line(p, {12, 11.4}, {20.5, 7.2}, stroke(c.accent, 1.5));
    };
    icons["viewRear"] = [](QPainter &p, const IconPalette &c) {
        cube(p, c, -1, true, true);
        polygon(p, QPolygonF({{3.5, 7.2}, {12, 3}, {12, 12.6}, {3.5, 16.8}}), QColor(0, 0, 0, 0), stroke(c.accent, 1.2, Qt::DashLine));
    };
    icons["viewTrimetric"] = [](QPainter &p, const IconPalette &c) {
        p.save();
        p.translate(12, 12);
        p.rotate(-12);
        p.translate(-12, -12);
        cube(p, c);
        p.restore();
    };
    icons["displayWireframe"] = [](QPainter &p, const IconPalette &c) {
        cube(p, c, -1, false, true);
        p.setPen(stroke(c.ink, 0.8, Qt::DashLine));
        p.drawLine(QPointF(3.5, 16.8), QPointF(12, 12.6));
        p.drawLine(QPointF(12, 12.6), QPointF(20.5, 16.8));
        p.drawLine(QPointF(12, 12.6), QPointF(12, 3));
    };
    icons["displayShaded"] = [](QPainter &p, const IconPalette &c) { cube(p, c, -1, true, false); };
    icons["displayShadedEdges"] = [](QPainter &p, const IconPalette &c) { cube(p, c, -1, true, true); };
    return icons;
}

// Motore dell'icona: disegna a ogni dimensione richiesta (anche con il
// rapporto dei pixel dello schermo), niente immagini scalate.
class PaintedIconEngine : public QIconEngine {
public:
    explicit PaintedIconEngine(Draw draw) : draw_(std::move(draw)) {}
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);
        const double side = std::min(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 24.0, side / 24.0);
        draw_(*painter, palette(mode));
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override { return scaledPixmap(size, mode, state, 1.0); }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QImage image(size * scale, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            paint(&painter, QRect(QPoint(), image.size()), mode, state);
        }
        image.setDevicePixelRatio(scale);
        return QPixmap::fromImage(image);
    }
    QIconEngine *clone() const override { return new PaintedIconEngine(draw_); }

private:
    Draw draw_;
};

}

QIcon commandIcon(const QString &name) {
    const auto &icons = registry();
    const auto found = icons.find(name);
    if (found == icons.end()) return {};
    return QIcon(new PaintedIconEngine(found->second));
}

QStringList commandIconNames() {
    QStringList names;
    for (const auto &entry : registry()) names.append(entry.first);
    return names;
}

}
