#ifndef FORGECAD2026_GUI_COMMON_H
#define FORGECAD2026_GUI_COMMON_H

#include "cad_types.h"
#include <QColor>
#include <QFont>
#include <functional>

class QDialog;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QWidget;

// Helper interni condivisi dalla finestra e dal viewport.
namespace ForgeCad::Gui {
QFont uiFont(int pointSize, QFont::Weight weight);
double pointLength(const QPointF &point);
double pointDistance(const QPointF &a, const QPointF &b);
inline const QColor kSelectionColor(255, 225, 70);
inline const QColor kHoverColor(90, 205, 255);
QString featureIconName(const ExtrusionObject &body);
QLabel *wrappedNote(const QString &text, QWidget *parent);
bool runUntilApplied(QDialog &dialog, QFormLayout *form, QDialogButtonBox *buttons,
                     const std::function<QString()> &apply);
}

#endif
