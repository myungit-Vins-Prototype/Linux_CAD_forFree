#pragma once
#include "cad_drawing_silhouette.h"
#include <QPainterPath>
#include <QImage>
#include <QRectF>
namespace ForgeCad {
// Sola presentazione: classificazione sulla mesh, riferimenti delle quote invariati.
// Coordinate dei tracciati in mm sul foglio, relative all'origine della vista.
struct DrawingVisiblePath {QPainterPath visible,hidden;};
struct DrawingViewVisibility {
    std::vector<DrawingVisiblePath> curves,silhouettes;
    QPainterPath sectionHatch,sectionMaterial;
    QImage shadedImage;
    QRectF shadedBounds;
    bool meshPrepared=false;
    QStringList warnings;
};
// Le sezioni con proiezione richiedono il materiale di chiusura (anche vuoto sulle superfici).
QString drawingViewVisibility(const DrawingModelSnapshot &model,const DrawingView &view,
                             const std::vector<DrawingCurve> &curves,const DrawingSilhouetteResult &silhouettes,
                             DrawingSilhouetteCache &cache,DrawingViewVisibility &result,const QPainterPath *sectionMaterial=nullptr,bool includeMesh=false);
}
