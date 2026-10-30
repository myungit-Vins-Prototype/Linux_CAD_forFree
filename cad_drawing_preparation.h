#pragma once
#include "cad_drawing_sheet.h"
#include "cad_drawing_dimension.h"
#include "cad_drawing_visibility.h"
#include <QHash>
#include <functional>
#include <map>

namespace ForgeCad {
struct DrawingFeatureChoice { quint64 id=0; QString name; };
struct DrawingEditorModel {
    DrawingModelSnapshot snapshot;
    QVector<DrawingFeatureChoice> choices;
    QHash<quint64,quint64> replacements;
    Kernel::Frame3 frontFrame{Kernel::Vec3(),Kernel::Vec3(0,-1,0),Kernel::Vec3(1,0,0)};
    // Solo corpi realmente letti dalla cache; mai ricostruiti per indice.
    QHash<quint64,std::shared_ptr<const Kernel::ElementNames>> legacySnapshotNames;
};
// Risultati geometrici trasferibili: nessun widget o stato dell'editor.
struct PreparedDrawingView {
    std::vector<DrawingCurve> curves;
    DrawingSilhouetteResult contours;
    std::shared_ptr<DrawingViewVisibility> display;
    DrawingSilhouetteCache cache;
};
struct PreparedDrawingSheet {
    DrawingSheet sheet;
    DrawingEditorModel model;
    std::map<QUuid,std::vector<DrawingCurve>> curves, sections;
    std::map<QUuid,DrawingSilhouetteResult> silhouettes;
    std::map<QUuid,std::shared_ptr<const DrawingViewVisibility>> visibility;
    std::map<QUuid,DrawingDimensionResult> dimensions;
    DrawingSilhouetteCache cache;
};
PreparedDrawingView prepareDrawingView(const DrawingModelSnapshot &source,const DrawingView &definition,
    const DrawingViewLayout &layout,DrawingSilhouetteCache seeds,const std::function<void(int)> &phase,
    const std::vector<DrawingCurve> *reuseCurves=nullptr,const DrawingSilhouetteResult *reuseContours=nullptr);
void validateDrawingViewPaths(const DrawingView &view,const DrawingViewLayout &layout,
    const std::vector<DrawingCurve> &curves,const DrawingSilhouetteResult &contours);
// Eccezioni di calcolo/annullamento: nessun argomento o stato vivo viene modificato.
PreparedDrawingSheet prepareDrawingSheet(DrawingSheet sheet,DrawingEditorModel model,const std::function<void(int)> &phase);
// 0: automatico, al massimo 4 worker; 1: sequenziale; richieste esplicite
// limitate a 8. Solo dati nei worker, callback di fase serializzate.
PreparedDrawingSheet prepareDrawingSheet(DrawingSheet sheet,DrawingEditorModel model,const std::function<void(int)> &phase,int threads);
DrawingSheet retargetDrawingSheetModel(DrawingSheet sheet,const DrawingEditorModel &model);
}
