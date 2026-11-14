#pragma once
#include "cad_drawing_sheet.h"
#include "cad_drawing_local_section_snap.h"
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QWidget>
#include <functional>
#include <vector>

class QComboBox;
class QGraphicsItemGroup;
class QGraphicsPathItem;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QPushButton;
namespace ForgeCad {
// Profilo gia' pronto: le variazioni di profondita' non ricalcolano la geometria.
struct DrawingLocalSectionDepthView {
    DrawingView view;
    DrawingViewLayout layout;
    QPainterPath profile; // mm carta, relativo all'origine della vista
    bool onSheet = true;
    std::shared_ptr<const DrawingLocalSectionSnapGeometry> snaps = {};
    std::function<bool(std::size_t, QPointF, bool)> snapVisible = {};
};
std::vector<DrawingLocalSectionDepthView> drawingLocalSectionDepthViews(
    const DrawingSheet &sheet, const DrawingView &target, const DrawingModelSnapshot &source,
    const std::function<QPainterPath(const QUuid &)> &profile,
    const std::function<std::vector<DrawingCurve>(const QUuid &)> &geometry = {});
struct DrawingLocalSectionDepthTrace {
    QLineF zero, cut, dimension;
    QPolygonF band;
};
DrawingLocalSectionDepthTrace drawingLocalSectionDepthTrace(const DrawingView &target,
    const DrawingLocalSectionDepthView &reference, double front, double depth, const QPolygonF &outline);
double drawingLocalSectionDepthAt(const DrawingView &target,
    const DrawingLocalSectionDepthView &reference, QPointF paper);

class DrawingLocalSectionDepthGuide final : public QWidget {
  public:
    DrawingLocalSectionDepthGuide(QWidget *parent, QGraphicsScene *sheetScene, QGraphicsView *sheetCanvas, DrawingView target,
        std::vector<DrawingLocalSectionDepthView> references, std::function<void(bool, double)> picked);
    ~DrawingLocalSectionDepthGuide() override;
    void setParameters(double front, double depth, const QPolygonF &outline);
    bool pointer(QPointF sheetPoint, bool click, Qt::KeyboardModifiers modifiers = {});
    bool key(int key);
    void cancel();
  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
  private:
    enum class Pick { None, Zero, Cut };
    void selectPick(Pick pick);
    void redraw();
    bool pick(QPointF localPaper);
    void clearSnap();
    QPointF snap(QPointF localPaper, QGraphicsView *canvas, QPointF offset, Qt::KeyboardModifiers modifiers);
    DrawingView target_;
    std::vector<DrawingLocalSectionDepthView> references_;
    std::function<void(bool, double)> picked_;
    QGraphicsScene *sheetScene_, *previewScene_;
    QGraphicsView *preview_, *sheetCanvas_;
    QGraphicsPathItem *snapMark_ = nullptr;
    std::vector<DrawingLocalSectionSnapPoint> snapChoices_;
    std::size_t snapCycle_ = 0;
    QPointF snapCursor_, snapOffset_;
    QGraphicsView *snapCanvas_ = nullptr;
    Qt::KeyboardModifiers snapModifiers_ = {};
    QGraphicsItemGroup *sheetGuide_ = nullptr;
    QComboBox *selector_;
    QLabel *values_, *hint_;
    QPushButton *zero_, *cut_;
    double front_ = 0, depth_ = -1;
    QPolygonF outline_;
    Pick pick_ = Pick::None;
    bool active_ = true;
};
} // namespace ForgeCad
