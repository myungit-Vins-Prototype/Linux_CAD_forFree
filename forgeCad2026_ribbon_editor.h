#pragma once
#include <QJsonArray>
#include <QMap>
#include <QString>
class QWidget;
namespace ForgeCad {
// L'editor lavora su una copia: solo Accetta restituisce una nuova configurazione.
bool editRibbonLayout(QWidget *parent, QJsonArray &layout, const QJsonArray &defaults,
                      const QMap<QString, QString> &commands);
}
