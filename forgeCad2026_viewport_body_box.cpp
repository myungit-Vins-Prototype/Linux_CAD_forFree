#include "forgeCad2026_viewport_internal.h"

void CadViewport::setBodyBoxPickCallback(std::function<void(const QVector<int> &)> callback) {
    if (!callback && bodyBoxPickFinished_) {
        boxArmed_ = boxSelecting_ = false;
        update();
    }
    bodyBoxPickFinished_ = std::move(callback);
}

QVector<int> CadViewport::referenceBodiesInBox(const QRectF &rect, bool window) const {
    QVector<int> found;
    for (int index = 0; index < extrusions_.size(); ++index) {
        if (!referenceBodyEligible(index) || !isBodyVisibleInScene(index)) continue;
        const auto &body = extrusions_.at(index);
        if (body.suppressed || !body.error.isEmpty()) continue;
        ensureBodyDisplay(index);
        const auto &display = extrusions_.at(index).display;
        bool any = false, matches = window;
        // La mesh serve solo alla selezione a schermo, mai alla booleana.
        // In modalita finestra deve essere incluso l'intero corpo, anche se
        // contiene piu solidi o facce senza bordi visibili (sfera, toro).
        if (window) {
            for (const auto &point : display.vertices) {
                any = true;
                if (!rect.contains(projectWorldPoint(point))) { matches = false; break; }
            }
            if (matches) for (const auto &edge : display.edges) {
                for (const auto &point : edge) {
                    any = true;
                    if (!rect.contains(projectWorldPoint(point))) { matches = false; break; }
                }
                if (!matches) break;
            }
        } else {
            for (const auto &edge : display.edges) {
                QVector<QPointF> screen;
                screen.reserve(edge.size());
                for (const auto &point : edge) screen.append(projectWorldPoint(point));
                if (polylineInBox(screen, rect, false)) { matches = true; break; }
            }
            // Un riquadro interamente dentro una faccia non attraversa alcun
            // bordo: considerare anche l'area dei triangoli visualizzati.
            for (int k = 0; !matches && k + 2 < display.vertices.size(); k += 3) {
                QPolygonF triangle;
                for (int j = 0; j < 3; ++j) triangle.append(projectWorldPoint(display.vertices.at(k + j)));
                triangle.append(triangle.first());
                matches = polylineInBox(triangle, rect, false);
                if (!matches) for (const auto &corner : {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()})
                    if (triangle.containsPoint(corner, Qt::OddEvenFill)) { matches = true; break; }
            }
            any = matches;
        }
        if (any && matches) found.append(index);
    }
    return found;
}
