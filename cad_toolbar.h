#pragma once
#include <QSettings>
#include <QToolBar>

namespace ForgeCad {
// Stessa densita' e preferenza di blocco per modellazione e tavole.
inline void configureCommandBar(QToolBar *bar) {
    bar->setMovable(!QSettings().value(QStringLiteral("toolbar/locked"), false).toBool());
    bar->setFloatable(bar->isMovable());
    bar->setIconSize(QSize(24, 24));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
}
}
