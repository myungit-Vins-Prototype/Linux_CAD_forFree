#pragma once

#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionMenuItem>
#include <QToolButton>

// Popup comuni: margine trasparente per l'ombra e colonna delle spunte
// indipendente dalle icone, anche nelle scelte esclusive di Visualizza.
class CadMenu final : public QMenu {
public:
    explicit CadMenu(QWidget *parent = nullptr) : QMenu(parent) { configure(); }
    CadMenu(const QString &title, QWidget *parent) : QMenu(title, parent) { configure(); }

protected:
    void paintEvent(QPaintEvent *event) override {
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            const QRectF panel = QRectF(rect()).adjusted(16.5, 16.5, -16.5, -16.5);
            QPainterPath outline;
            if (qobject_cast<QToolButton *>(parentWidget())) {
                outline.moveTo(panel.topLeft());
                outline.lineTo(panel.topRight());
                outline.lineTo(panel.right(), panel.bottom() - 10);
                outline.quadTo(panel.bottomRight(), QPointF(panel.right() - 10, panel.bottom()));
                outline.lineTo(panel.left() + 10, panel.bottom());
                outline.quadTo(panel.bottomLeft(), QPointF(panel.left(), panel.bottom() - 10));
                outline.closeSubpath();
            } else {
                outline.addRoundedRect(panel, 10, 10);
            }
            painter.setPen(Qt::NoPen);
            for (int spread = 14; spread > 0; --spread) {
                painter.setBrush(QColor(0, 0, 0, 3 + (14 - spread) / 3));
                painter.drawRoundedRect(panel.adjusted(-spread, -spread + 3, spread, spread + 3),
                                        10 + spread, 10 + spread);
            }
            painter.fillPath(outline, QColor(32, 45, 54, 250));
            painter.setPen(QPen(QColor(120, 165, 205, 150), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(outline.simplified());
        }
        // Conserva anche scorrimento e indicatori dei menu lunghi gestiti da Qt.
        QMenu::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipRect(rect().adjusted(22, 22, -22, -22));
        for (QAction *action : actions()) {
            const QRect row = actionGeometry(action);
            if (!action->isVisible() || row.isEmpty() || !row.intersects(event->rect())) continue;
            QStyleOptionMenuItem option;
            initStyleOption(&option, action);
            option.rect = row;
            // Qt usa la stessa colonna per icona e spunta: ridisegna la voce
            // senza l'indicatore nativo e disegna la spunta nel margine dedicato.
            option.checked = false;
            option.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange);
            option.checkType = QStyleOptionMenuItem::NotCheckable;
            option.icon = QIcon();
            painter.fillRect(row, QColor(32, 45, 54));
            style()->drawControl(QStyle::CE_MenuItem, &option, &painter, this);
            if (!action->icon().isNull()) {
                const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize, &option, this);
                const int x = layoutDirection() == Qt::RightToLeft
                    ? row.right() - 22 - iconSize : row.left() + 22;
                const QRect iconRect(x, row.center().y() - iconSize / 2, iconSize, iconSize);
                action->icon().paint(&painter, iconRect, Qt::AlignCenter,
                                    action->isEnabled() ? QIcon::Normal : QIcon::Disabled, QIcon::Off);
            }
            if (action->isCheckable() && action->isChecked()) {
                const qreal x = layoutDirection() == Qt::RightToLeft ? row.right() - 17 : row.left() + 6;
                const qreal y = row.center().y();
                QPainterPath tick;
                tick.moveTo(x, y);
                tick.lineTo(x + 4, y + 4);
                tick.lineTo(x + 11, y - 4);
                painter.setPen(QPen(action->isEnabled() ? QColor(142, 232, 162) : QColor(120, 137, 149),
                                    2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(tick);
            }
        }
    }

private:
    void configure() {
        setWindowFlag(Qt::FramelessWindowHint);
        setWindowFlag(Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet(QStringLiteral(
            "QMenu { background: transparent; color: #d8e4ea; border: none; padding: 22px; }"
            "QMenu::item { padding: 4px 14px 4px 26px; background: transparent; border-radius: 4px; }"
            "QMenu::item:selected:enabled { background: #365d78; color: white; }"
            "QMenu::item:disabled { color: #788995; }"
            "QMenu::separator { height: 2px; background: #445966; margin: 0px 8px; }"));
    }
};

inline QMenu *addCadMenu(QWidget *parent, const QString &title) {
    auto *menu = new CadMenu(title, parent);
    if (auto *bar = qobject_cast<QMenuBar *>(parent)) bar->addMenu(menu);
    else if (auto *popup = qobject_cast<QMenu *>(parent)) popup->addMenu(menu);
    return menu;
}
