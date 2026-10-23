#pragma once

#include <QMenu>
#include <QApplication>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionMenuItem>
#include <QToolButton>
#include <QSettings>
#include <QtMath>
#include <algorithm>
#include <memory>

// Popup comuni: colori del tema e colonna delle spunte
// indipendente dalle icone, anche nelle scelte esclusive di Visualizza.
class CadMenu final : public QMenu {
public:
    explicit CadMenu(QWidget *parent=nullptr);
    CadMenu(const QString &title,QWidget *parent);
    ~CadMenu() override;
protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

inline QMenu *addCadMenu(QWidget *parent, const QString &title) {
    auto *menu = new CadMenu(title, parent);
    if (auto *bar = qobject_cast<QMenuBar *>(parent)) bar->addMenu(menu);
    else if (auto *popup = qobject_cast<QMenu *>(parent)) popup->addMenu(menu);
    return menu;
}
