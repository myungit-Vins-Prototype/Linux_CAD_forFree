#pragma once
#include <QObject>
#include "cad_types.h"
#include <QPoint>
#include <QMap>
#include <QString>
#include <QVector>
#include <QTimer>
#include <array>
#include <functional>
class CadViewport;
class QTableView;
class QRubberBand;
namespace ForgeCad {
class SurfacePolesInteraction final : public QObject {
  public:
    SurfacePolesInteraction(CadViewport *view, QTableView *table, const QVector<int> &displayedRows, QObject *parent);
    ~SurfacePolesInteraction() override;
    std::function<void()> started, moved, finished;
    std::function<void(const QString &)> message;
    void cancel();
    void setEnabled(bool enabled) {
        enabled_ = enabled;
        if (!enabled)
            cancel();
    }
    void setBoxSelection(bool enabled) { cancel(); boxSelection_ = enabled; }
    const QVector<int> &draggedRows() const { return rows_; }
    bool interacting() const { return active_ || boxing_; }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    void finish(bool cancel);
    void selectBox();
    int coordinateColumn() const;
    CadViewport *view_;
    QTableView *table_;
    const QVector<int> &displayedRows_;
    QRubberBand *band_;
    bool enabled_ = true, active_ = false, boxing_ = false, boxSelection_ = false;
    QTimer releaseCheck_;
    int axis_ = -1;
    QPoint origin_, boxEnd_;
    Qt::KeyboardModifiers modifiers_{};
    QVector<int> rows_, initialSelection_;
    QVector<std::array<double, 3>> initial_;
    QMap<int,std::array<QString,4>> initialEdits_;
    QVector<QVector<SurfacePoleEdit>> initialAutomatic_;
};
} // namespace ForgeCad
