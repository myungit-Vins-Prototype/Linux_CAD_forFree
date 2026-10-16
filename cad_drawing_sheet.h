#ifndef FORGECAD_DRAWING_SHEET_H
#define FORGECAD_DRAWING_SHEET_H
#include "cad_drawing.h"
#include <QPointF>
#include <QSizeF>
#include <QVector>

namespace ForgeCad {
enum class DrawingDerivedKind { None, Orthographic, Section };
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
};
struct DrawingDimensionLayout {
    QUuid dimensionId;
    QPointF position; // posizione del testo sul foglio, indipendente dalla misura
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
struct DrawingSheet {
    DrawingDocument drawing;
    QString title = QStringLiteral("Tavola");
    QSizeF size{297,210};
    QVector<DrawingViewLayout> views;
    QVector<DrawingDimensionLayout> dimensions;
    DrawingTitleBlock titleBlock;
    DrawingRevisionTable revisionTable;
};
// Solo metadati del foglio; nessuna duplicazione della geometria del modello.
QString encodeDrawingSheet(const DrawingSheet &sheet,QByteArray &bytes);
QString decodeDrawingSheet(const QByteArray &bytes,DrawingSheet &sheet);
}
#endif
