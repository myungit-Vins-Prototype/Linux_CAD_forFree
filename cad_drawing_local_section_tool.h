#pragma once
#include "cad_drawing_local_section_depth.h"
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QVector>
#include <functional>

class QWidget;
class QGraphicsScene;
class QGraphicsPathItem;
class QGraphicsView;
class QDialog;
class QPushButton;
class QLabel;
class QDoubleSpinBox;
namespace ForgeCad {
class DrawingLocalSectionTool final : public QObject {
  public:
    DrawingLocalSectionTool(QWidget *parent, QGraphicsScene *scene, QGraphicsView *canvas,
                            const DrawingView &view, double front, double depth,
                            const std::function<QPointF(QPointF)> &toModel,
                            const std::function<QPointF(QPointF)> &toPaper,
                            std::function<void(DrawingLocalSection)> apply,
                            std::vector<DrawingLocalSectionDepthView> references = {});
    ~DrawingLocalSectionTool() override;
    bool active() const { return active_; }
    bool pointer(QPointF point, bool click, Qt::KeyboardModifiers modifiers = {});
    bool key(int key);
    void cancel();

  private:
    void preview(QPointF cursor);
    void finish();
    void updateDepthGuide();
    DrawingView definition_;
    QGraphicsScene *scene_;
    QGraphicsView *canvas_;
    QGraphicsPathItem *preview_ = nullptr;
    QPointer<QDialog> panel_;
    QDoubleSpinBox *front_ = nullptr, *depth_ = nullptr;
    QPushButton *apply_ = nullptr;
    QLabel *message_ = nullptr;
    DrawingLocalSectionDepthGuide *depthGuide_ = nullptr;
    QVector<QPointF> points_;
    std::function<QPointF(QPointF)> toModel_, toPaper_;
    std::function<void(DrawingLocalSection)> applyCallback_;
    bool active_ = true, drafting_ = true;
};
} // namespace ForgeCad
