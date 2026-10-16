#pragma once
#include "cad_drawing_silhouette.h"
#include <QPainterPath>
namespace ForgeCad {
// Sola presentazione: classificazione sulla mesh, riferimenti delle quote invariati.
// Coordinate dei tracciati in mm sul foglio, relative all'origine della vista.
struct DrawingVisiblePath {QPainterPath visible,hidden;};
struct DrawingViewVisibility {
    std::vector<DrawingVisiblePath> curves,silhouettes;
    QStringList warnings;
};
QString drawingViewVisibility(const DrawingModelSnapshot &model,const DrawingView &view,
                             const std::vector<DrawingCurve> &curves,const DrawingSilhouetteResult &silhouettes,
                             DrawingSilhouetteCache &cache,DrawingViewVisibility &result);
}
