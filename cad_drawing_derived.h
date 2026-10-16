#pragma once
#include "cad_drawing_sheet.h"
#include <QPainterPath>
#include <QStringList>
namespace ForgeCad {
// Risolve prima i genitori; nessuna geometria o cronologia 3D viene modificata.
QString synchronizeDrawingViews(DrawingSheet &sheet);
QPointF constrainDrawingViewPosition(const DrawingSheet &sheet,const DrawingViewLayout &layout,QPointF proposed);
// Usa gli assi gia' presenti nel formato; i frame legacy restano leggibili.
QString alignDrawingSectionOrientation(DrawingSheet &sheet,const QUuid &sectionId);
QString validateDrawingRelations(const DrawingSheet &sheet);
struct DrawingSectionHatch { QPainterPath material,lines; QStringList warnings; };
// Presentazione campionata delle regioni solide. Mai usata per misurare o agganciare quote.
QString drawingSectionHatch(const DrawingModelSnapshot &model,const DrawingView &view,
                           const std::vector<DrawingCurve> &curves,DrawingSectionHatch &result);
}
