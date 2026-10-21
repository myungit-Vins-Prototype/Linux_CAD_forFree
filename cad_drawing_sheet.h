#ifndef FORGECAD_DRAWING_SHEET_H
#define FORGECAD_DRAWING_SHEET_H
#include "cad_drawing.h"
#include "cad_drawing_tolerances.h"
#include "cad_drawing_axes.h"
#include "cad_drawing_style.h"
#include <QPointF>
#include <QSizeF>
#include <QVector>
#include <optional>

namespace ForgeCad {
enum class DrawingDerivedKind { None, Orthographic, Section, Detail, Auxiliary };
enum class DrawingProjectionSide { Left, Right, Above, Below };
enum class DrawingProjectionMethod { FirstAngle, ThirdAngle };
struct DrawingViewRelation {
    QUuid parentId;
    DrawingDerivedKind kind=DrawingDerivedKind::None;
    DrawingProjectionSide side=DrawingProjectionSide::Right;
    DrawingProjectionMethod method=DrawingProjectionMethod::FirstAngle;
    bool aligned=true;
    QPointF cutStart,cutEnd; // coordinate del piano madre, mm modello, Y verso l’alto
    bool reverse=false,hatch=true;
    QString label;
    QPointF detailCenter; // piano madre, mm modello
    double detailRadius=0; // ritaglio grafico, mai riferimento di quota
    double auxiliaryAngle=0; // lato d'osservazione nel piano madre, gradi da +X verso +Y
};
struct DrawingViewLayout {
    QUuid viewId;
    QString title;
    QPointF position; // origine del piano sul foglio, in mm (Y verso il basso)
    QVector<quint64> features;
    bool showHiddenLines=false; // presentazione della sola vista, salvata nel .drw
    DrawingViewRelation relation{};
    bool showMesh=false; // facce ombreggiate dalla mesh, mai riferimenti di quota
    bool showCenterMarks=false,showCenterLines=false; // cerchi/ellissi esatti della vista
    bool showCylinderAxes=false; // asse delle superfici cilindriche, sola presentazione
    std::optional<QPointF> titleOffset={}; // origine del titolo rispetto alla vista, mm sul foglio
};
struct DrawingDimensionLayout {
    QUuid dimensionId;
    QPointF position; // posizione del testo sul foglio, indipendente dalla misura
    DrawingDimensionPresentation presentation{};
};
struct DrawingTitleBlock {
    bool visible=false;
    QString code,organization,author,date; // data dichiarata, non aggiornata all’apertura
};
struct DrawingRevisionNote {
    QUuid checkinId;
    QString description,verifiedBy;
};
struct DrawingRevisionTable {
    bool visible=false;
    QVector<DrawingRevisionNote> notes;
};
struct DrawingAnnotation {
    QUuid id=QUuid::createUuid(),viewId; // vista nulla per una nota libera sul foglio
    QString text=QStringLiteral("Nota");
    QPointF position{20,20}; // mm sul foglio, angolo superiore sinistro
    double width=45,height=3; // larghezza di impaginazione e altezza nominale del testo, mm
    std::optional<DrawingAttachment> reference; // punta del richiamo, mai coordinate congelate
};
struct DrawingSheet {
    DrawingDocument drawing;
    QString title = QStringLiteral("Tavola");
    QSizeF size{297,210};
    QVector<DrawingViewLayout> views;
    QVector<DrawingDimensionLayout> dimensions;
    DrawingTitleBlock titleBlock;
    DrawingRevisionTable revisionTable;
    QVector<DrawingAxis> axes;
    QVector<DrawingAnnotation> annotations;
    DrawingStyle style;
};
// Solo metadati del foglio; nessuna duplicazione della geometria del modello.
DrawingAttachmentResolver drawingSheetAttachmentResolver(const DrawingSheet &sheet,const DrawingView &view,
    const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves);
QString encodeDrawingSheet(const DrawingSheet &sheet,QByteArray &bytes);
QString decodeDrawingSheet(const QByteArray &bytes,DrawingSheet &sheet);
}
#endif
