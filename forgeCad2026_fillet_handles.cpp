#include "cad_expression.h"
#include "forgeCad2026_viewport_internal.h"
#include <cmath>
void CadViewport::setFilletRadiusHandles(
    const QVector<FilletRadiusHandle> &handles,
    std::function<void(int, int, double, bool)> changed) {
  filletRadiusHandles_ = handles;
  filletRadiusChanged_ = std::move(changed);
  if (filletRadiusDrag_ >= handles.size())
    filletRadiusDrag_ = -1;
  update();
}
void CadViewport::clearFilletRadiusHandles() {
  filletRadiusHandles_.clear();
  filletRadiusChanged_ = {};
  filletRadiusDrag_ = -1;
  unsetCursor();
  update();
}
int CadViewport::filletRadiusHandleAt(const QPointF &position) const {
  if (!filletRadiusChanged_)
    return -1;
  int best = -1;
  double distance = 12;
  for (int i = 0; i < filletRadiusHandles_.size(); ++i) {
    const auto &handle = filletRadiusHandles_[i];
    const double d = pointDistance(
        position, projectWorldPoint(handle.point +
                                    handle.direction * float(handle.radius)));
    if (d < distance) {
      distance = d;
      best = i;
    }
  }
  return best;
}
bool CadViewport::beginFilletRadiusDrag(const QPointF &position) {
  const int index = filletRadiusHandleAt(position);
  if (index < 0)
    return false;
  filletRadiusDrag_ = index;
  filletRadiusPress_ = position;
  const auto &handle = filletRadiusHandles_[index];
  filletRadiusInitial_ = handle.radius;
  filletRadiusScreenDirection_ =
      projectWorldPoint(handle.point + handle.direction) -
      projectWorldPoint(handle.point);
  if (QPointF::dotProduct(filletRadiusScreenDirection_,
                          filletRadiusScreenDirection_) < 1e-4)
    filletRadiusScreenDirection_ =
        QPointF(0, -1.0 / qMax(1e-12, pickTolerance(1)));
  setCursor(Qt::ClosedHandCursor);
  update();
  return true;
}
void CadViewport::dragFilletRadius(const QPointF &position, bool finished) {
  if (filletRadiusDrag_ < 0 || filletRadiusDrag_ >= filletRadiusHandles_.size())
    return;
  const auto handle = filletRadiusHandles_[filletRadiusDrag_];
  const double delta = QPointF::dotProduct(position - filletRadiusPress_,
                                           filletRadiusScreenDirection_) /
                       QPointF::dotProduct(filletRadiusScreenDirection_,
                                           filletRadiusScreenDirection_);
  const double radius =
      qBound(0.000001, filletRadiusInitial_ + delta, 100000.0);
  filletRadiusHandles_[filletRadiusDrag_].radius = radius;
  if (finished) {
    filletRadiusDrag_ = -1;
    unsetCursor();
  }
  // Copia prima della chiamata: il pannello puo' aggiornare i marker o
  // chiudersi.
  const auto changed = filletRadiusChanged_;
  if (changed)
    changed(handle.path, handle.station, radius, finished);
  update();
}
void CadViewport::drawFilletRadiusHandles(QPainter &painter) {
  if (filletRadiusHandles_.isEmpty())
    return;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  const QColor color(255, 183, 69);
  QFont font = painter.font();
  font.setPixelSize(11);
  painter.setFont(font);
  for (int i = 0; i < filletRadiusHandles_.size(); ++i) {
    const auto &handle = filletRadiusHandles_[i];
    const auto origin = projectWorldPoint(handle.point);
    const auto tip = projectWorldPoint(handle.point +
                                       handle.direction * float(handle.radius));
    painter.setPen(QPen(color, 1.4, Qt::DashLine));
    painter.drawLine(origin, tip);
    painter.setBrush(QColor(20, 37, 50));
    painter.setPen(QPen(color, 1.5));
    painter.drawEllipse(origin, 2.5, 2.5);
    painter.setBrush(color);
    painter.setPen(QPen(QColor(28, 38, 48), 1.5));
    const double size = i == filletRadiusDrag_ ? 7 : 5;
    painter.drawEllipse(tip, size, size);
    const QString text =
        QStringLiteral("R%1 · %2")
            .arg(handle.station + 1)
            .arg(ForgeCad::formatLength(handle.radius, 3, true, this));
    const QRectF label(
        tip + QPointF(9, -22),
        QSizeF(QFontMetricsF(font).horizontalAdvance(text) + 10, 20));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 29, 40, 225));
    painter.drawRoundedRect(label, 3, 3);
    painter.setPen(color);
    painter.drawText(label, Qt::AlignCenter, text);
  }
  painter.restore();
}
