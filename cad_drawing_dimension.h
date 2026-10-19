#pragma once
#include "cad_drawing.h"
#include <functional>
namespace ForgeCad {
struct DrawingPrimitive {
    enum class Kind { Other, Line, Conic } kind=Kind::Other;
    Kernel::Vec2 center{},firstAxis{},secondAxis{};
    bool circular=false,linearProjection=false;
    Kernel::Vec2 lineFirst{},lineSecond{};
};
DrawingPrimitive drawingPrimitive(const DrawingView &view,const DrawingCurve &curve);
bool drawingDatum(DrawingPointRole role);
QString validateDrawingAttachments(const DrawingDimension &dimension,const DrawingView &view);
// Crea un aggancio semantico: vertice/bordo di rifilatura, frazione di lunghezza
// o angolo polare nel piano per le coniche. Non salva indici transitori.
DrawingAttachment drawingAttachment(const DrawingView &view,const DrawingCurve &curve,const DrawingModelSnapshot &model,
                                    DrawingPointRole role,double parameter=std::numeric_limits<double>::quiet_NaN());
struct DrawingResolvedAttachment {
    DrawingDimensionStatus status=DrawingDimensionStatus::Invalid;
    QString message;
    Kernel::Vec2 point{},first{},second{},center{};
    double radius=0;
    bool isPoint=false,isLine=false,isCircle=false,hasCenter=false;
    const DrawingCurve *curve=nullptr;
};
using DrawingAttachmentResolver=std::function<DrawingResolvedAttachment(const DrawingAttachment &)>;
DrawingResolvedAttachment resolveDrawingAttachment(const DrawingView &view,const DrawingModelSnapshot &model,
    const std::vector<DrawingCurve> &curves,const DrawingAttachment &reference,const DrawingAttachmentResolver &axes={});
// Riusa le curve della vista gia' calcolate; nessuna lettura del workspace.
DrawingDimensionResult evaluateDrawingAttachments(const DrawingDimension &dimension,const DrawingView &view,
    const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,const DrawingAttachmentResolver &axes={});
DrawingDimension drawingDimensionBetween(const DrawingView &view,const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,
    std::vector<DrawingAttachment> references,DrawingMeasure measure,const Kernel::Vec2 &cursor,bool automatic=false,const DrawingAttachmentResolver &axes={});
int drawingAngleSector(const DrawingDimensionResult &base,const Kernel::Vec2 &cursor);
QString drawingDimensionName(DrawingMeasure measure);
QString drawingDimensionText(const DrawingDimension &dimension,double value);
}
