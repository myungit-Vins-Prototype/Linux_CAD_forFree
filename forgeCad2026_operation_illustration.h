#pragma once

#include <QIcon>
#include <QPainter>
#include <QWidget>

namespace ForgeCad::Gui {
// Illustrazioni vettoriali dei pannelli: stessa proiezione, materiali e
// annotazioni del pannello Raggio approvato. Non sono anteprime del modello.
bool hasOperationIllustration(const QString &operation);
QIcon operationPanelIcon(const QString &operation);
void paintOperationIllustration(QPainter &painter, const QRectF &bounds,
                                const QString &operation);

class OperationIllustration final : public QWidget {
public:
  explicit OperationIllustration(const QString &operation,
                                 QWidget *parent = nullptr);
  void setOperation(const QString &operation);

protected:
  void paintEvent(QPaintEvent *) override;

private:
  QString operation_;
};
} // namespace ForgeCad::Gui
