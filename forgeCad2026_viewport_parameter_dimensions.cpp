#include "forgeCad2026_viewport_internal.h"
#include "cad_pattern_variation.h"

void CadViewport::setParameterDimensions(const QVector<PatternVariation> &dimensions,
                                         const QVector<PatternVariation> &selected,
                                         std::function<void(const PatternVariation &)> picked) {
    parameterDimensions_ = dimensions;
    selectedParameterDimensions_ = selected;
    parameterDimensionPicked_ = std::move(picked);
    parameterDimensionHover_ = -1;
    if (dimensions.isEmpty()) unsetCursor();
    update();
}

bool CadViewport::parameterDimensionGraphic(int index, DimensionGraphic &graphic, int *sketchIndex,
                                            int *constraintIndex) const {
    if (sketchMode_ || index < 0 || index >= parameterDimensions_.size()) return false;
    const auto &ref = parameterDimensions_[index];
    if (!ref.sketch) return false;
    for (int s = 0; s < sketches_.size(); ++s) {
        const auto &sketch = sketches_[s];
        if (sketch.storyId != ref.ownerId) continue;
        for (int c = 0; c < sketch.geometricConstraints.size(); ++c) {
            const auto &constraint = sketch.geometricConstraints[c];
            if (constraint.parameterId != ref.parameterId ||
                ref.parameter != QString::number(int(constraint.type)) + "/value") continue;
            if (sketchIndex) *sketchIndex = s;
            if (constraintIndex) *constraintIndex = c;
            return dimensionGraphic(sketch, c, graphic);
        }
    }
    return false;
}

int CadViewport::parameterDimensionAt(const QPointF &position) const {
    // Prima le etichette: una linea di un'altra quota non deve rubare il clic.
    for (int textOnly = 1; textOnly >= 0; --textOnly)
        for (int i = parameterDimensions_.size() - 1; i >= 0; --i) {
            DimensionGraphic g;
            if (!parameterDimensionGraphic(i, g)) continue;
            if (textOnly) {
                if (g.textBox.containsPoint(position, Qt::OddEvenFill)) return i;
            } else {
                QPainterPathStroker stroke; stroke.setWidth(8.0);
                if (stroke.createStroke(g.lines).contains(position)) return i;
            }
        }
    return -1;
}

void CadViewport::drawParameterDimensions(QPainter &painter) const {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(uiFont(9, QFont::DemiBold));
    for (int i = 0; i < parameterDimensions_.size(); ++i) {
        DimensionGraphic g;
        int s = -1, c = -1;
        if (!parameterDimensionGraphic(i, g, &s, &c)) continue;
        const bool selected = std::any_of(selectedParameterDimensions_.begin(), selectedParameterDimensions_.end(),
            [&](const auto &ref) { return ForgeCad::samePatternParameter(ref, parameterDimensions_[i]); });
        const QColor color = i == parameterDimensionHover_ ? QColor(255,170,70)
                             : selected ? QColor(100,255,170) : QColor(255,225,130);
        highlightConstraintEntities(painter, sketches_[s], c, color);
        painter.setPen(QPen(color, selected ? 1.8 : 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(g.lines);
        painter.setPen(Qt::NoPen); painter.setBrush(color);
        for (const auto &arrow : g.arrows) painter.drawPolygon(arrow);
        painter.save();
        painter.translate(g.textCenter); painter.rotate(g.textAngle);
        const QFontMetricsF metrics(painter.font());
        const QRectF box(-metrics.horizontalAdvance(g.text)/2-3, -metrics.height()/2,
                         metrics.horizontalAdvance(g.text)+6, metrics.height());
        painter.setBrush(QColor(8,14,22,220)); painter.drawRect(box);
        painter.setPen(color); painter.drawText(box, Qt::AlignCenter, g.text);
        painter.restore();
    }
    painter.restore();
}
