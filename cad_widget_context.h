#pragma once
#include <QVariant>
#include <QWidget>
namespace ForgeCad {
// I documenti incorporati condividono window(), ma non il contesto dei comandi.
inline QWidget *documentWorkspaceWidget(QWidget *widget) {
    for (auto *owner = widget; owner; owner = owner->parentWidget())
        if (owner->property("documentWorkspace").toBool()) return owner;
    return widget ? widget->window() : nullptr;
}
}
