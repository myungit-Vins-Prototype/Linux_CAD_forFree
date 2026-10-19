#pragma once

#include <QMenu>
#include <QApplication>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionMenuItem>
#include <QToolButton>

// Popup comuni: colori del tema e colonna delle spunte
// indipendente dalle icone, anche nelle scelte esclusive di Visualizza.
class CadMenu final : public QMenu {
public:
    explicit CadMenu(QWidget *parent = nullptr) : QMenu(parent) { configure(); }
    CadMenu(const QString &title, QWidget *parent) : QMenu(title, parent) { configure(); }

protected:
    void paintEvent(QPaintEvent *event) override {
        const auto colors=themePalette();
        QMenu::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipRect(rect().adjusted(6, 6, -6, -6));
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
            painter.fillRect(row, colors.color(QPalette::Window));
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
                painter.setPen(QPen(colors.color(action->isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText),
                                    2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(tick);
            }
        }
    }

private:
    QPalette themePalette() const {
        // Read the owning interface palette, also for nested popups.
        auto *anchor=parentWidget();while(anchor&&qobject_cast<QMenu *>(anchor))anchor=anchor->parentWidget();
        return anchor?anchor->palette():QApplication::palette();
    }
    void configure() {
        setStyleSheet(QStringLiteral(
            "QMenu { background: palette(window); color: palette(window-text); border: 1px solid palette(mid); padding: 6px; }"
            "QMenu::item { padding: 4px 14px 4px 26px; background: transparent; border-radius: 4px; }"
            "QMenu::item:selected:enabled { background: palette(highlight); color: palette(highlighted-text); }"
            "QMenu::item:disabled { color: palette(mid); }"
            "QMenu::separator { height: 2px; background: palette(mid); margin: 0px 8px; }"));
    }
};

inline QMenu *addCadMenu(QWidget *parent, const QString &title) {
    auto *menu = new CadMenu(title, parent);
    if (auto *bar = qobject_cast<QMenuBar *>(parent)) bar->addMenu(menu);
    else if (auto *popup = qobject_cast<QMenu *>(parent)) popup->addMenu(menu);
    return menu;
}
