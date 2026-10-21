#pragma once
#include "cad_drawing_silhouette.h"
#include "cad_drawing_centers.h"
#include "cad_drawing_axes.h"
#include <QPainterPath>
#include <QImage>
#include <QRectF>
namespace ForgeCad {
// Sola presentazione: classificazione sulla mesh, con tagli parametrici esatti
// di cerchi/ellissi contro i triangoli; riferimenti delle quote invariati.
// Coordinate dei tracciati in mm sul foglio, relative all'origine della vista.
struct DrawingVisiblePath {QPainterPath visible,hidden;};
struct DrawingViewVisibility {
    std::vector<DrawingVisiblePath> curves,silhouettes;
    QPainterPath sectionHatch,sectionMaterial;
    QPainterPath visibleCylinderAxes,allCylinderAxes;
    QImage shadedImage;
    QRectF shadedBounds;
    bool meshPrepared=false;
    DrawingCenterGeometry visibleCenters,allCenters; // cache dei riferimenti grafici, senza valutazioni al toggle
    QStringList warnings;
};
// Le sezioni con proiezione richiedono il materiale di chiusura (anche vuoto sulle superfici).
QString drawingViewVisibility(const DrawingModelSnapshot &model,const DrawingView &view,
                             const std::vector<DrawingCurve> &curves,const DrawingSilhouetteResult &silhouettes,
                             DrawingSilhouetteCache &cache,DrawingViewVisibility &result,const QPainterPath *sectionMaterial=nullptr,bool includeMesh=false);
}
