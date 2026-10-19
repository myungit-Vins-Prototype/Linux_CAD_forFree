#ifndef FORGECAD_DRAWING_EDITOR_H
#define FORGECAD_DRAWING_EDITOR_H
#include "cad_drawing_sheet.h"
#include "cad_drawing_dimension.h"
#include "cad_drawing_page.h"
#include <QHash>
#include <QWidget>
#include <functional>
#include <memory>

class QGraphicsView;
namespace ForgeCad {
struct DrawingFeatureChoice { quint64 id=0; QString name; };
struct DrawingEditorModel {
    DrawingModelSnapshot snapshot;
    QVector<DrawingFeatureChoice> choices;
    QHash<quint64,quint64> replacements;
};
class DrawingSheetEditor final : public QWidget {
public:
    DrawingSheetEditor(DrawingSheet sheet,DrawingEditorModel model,QWidget *parent=nullptr);
    ~DrawingSheetEditor() override;
    const DrawingSheet &sheet() const;
    QGraphicsView *canvas() const;
    const std::vector<DrawingCurve> &curves(const QUuid &viewId) const;
    const std::vector<DrawingSection> &sections(const QUuid &viewId) const;
    DrawingDimensionResult dimensionResult(const QUuid &id) const;
    QString insertView(DrawingView view,DrawingViewLayout layout);
    // Anteprima transitoria: segue il mouse, clic inserisce, Esc annulla.
    QString beginViewPlacement(DrawingView view,DrawingViewLayout layout);
    QString beginDerivedViewPlacement(const QUuid &parent,DrawingProjectionSide side,DrawingProjectionMethod method);
    QString beginDetailCircle(const QUuid &parent,const QString &label,double scale);
    QString beginDetailViewPlacement(const QUuid &parent,QPointF center,double radius,double scale,const QString &label);
    QString updateDetailView(const QUuid &id,QPointF center,double radius,double scale,const QString &label);
    QString beginSectionLine(const QUuid &parent,const QString &label,bool reverse=false,const QUuid &existing={},DrawingSectionStyle style=DrawingSectionStyle::Removed);
    QString setSectionLine(const QUuid &section,QPointF start,QPointF end);
    QString beginSectionPlacement(const QUuid &parent,QPointF start,QPointF end,const QString &label,bool reverse=false,DrawingSectionStyle style=DrawingSectionStyle::Removed);
    bool hasSectionLine()const;
    QString detachView(const QUuid &id);
    QString setViewAlignment(const QUuid &id,bool aligned);
    QString setSectionHatch(const QUuid &id,bool visible);
    QString reverseSection(const QUuid &id);
    QString alignSectionOrientation(const QUuid &id);
    QString setSectionStyle(const QUuid &id,DrawingSectionStyle style);
    bool hasViewPlacement() const;
    void cancelViewPlacement();
    // Modifica atomica: ID e quote conservati, ricalcolo delle viste dipendenti il cui piano cambia.
    QString updateView(DrawingView view,DrawingViewLayout layout);
    enum class ViewAlignment { SameRow, SameColumn };
    QString moveView(const QUuid &id,const QPointF &position);
    QString moveViewTitle(const QUuid &id,const QPointF &position);
    QString resetViewTitle(const QUuid &id);
    QString setDrawingStyle(const DrawingStyle &style);
    QString moveDimension(const QUuid &id,const QPointF &position);
    // Centri sul foglio: posizionamento una tantum, senza vincoli persistenti.
    QString alignView(const QUuid &id,const QUuid &reference,ViewAlignment alignment);
    QString removeView(const QUuid &id);
    QString setHiddenLinesVisible(const QUuid &id,bool visible);
    QString setShadedFacesVisible(const QUuid &id,bool visible);
    QString setCenterMarksVisible(const QUuid &id,bool visible);
    QString setCenterLinesVisible(const QUuid &id,bool visible);
    QString setCylinderAxesVisible(const QUuid &id,bool visible);
    QString beginAxis(DrawingAxisKind kind,const QUuid &existing={});
    QString addAxis(DrawingAxis axis);
    QString updateAxis(DrawingAxis axis);
    QString removeAxis(const QUuid &id);
    DrawingAxisResult axisResult(const QUuid &id)const;
    QString beginAnnotation(bool leader,const QUuid &existing={});
    QString addAnnotation(DrawingAnnotation note);
    QString updateAnnotation(DrawingAnnotation note);
    QString moveAnnotation(const QUuid &id,const QPointF &position);
    QString removeAnnotation(const QUuid &id);
    DrawingResolvedAttachment annotationResult(const QUuid &id)const;
    QString setRevisionTable(const DrawingRevisionTable &table);
    bool isComputing()const;
    QString setPageSettings(const QString &title,const QSizeF &size,const DrawingTitleBlock &block);
    void setPageContext(DrawingPageContext context);
    DrawingPageContext pageContext()const;
    QString exportPdf(const QString &path,DrawingPdfReport &report)const;
    QString undo();
    QString redo();
    bool canUndo() const;
    bool canRedo() const;
    void markSaved();
    bool isModified() const;
    QString addDimension(DrawingDimension dimension,const QPointF &position);
    QString updateDimension(DrawingDimension dimension);
    QString updateDimension(DrawingDimension dimension,DrawingDimensionPresentation presentation);
    QString addSelectedDimension(DrawingMeasure measure);
    QString updateModel(DrawingEditorModel model);
    void setChangedCallback(std::function<void(const DrawingSheet &)> callback);
    void setModelProvider(std::function<DrawingEditorModel()> provider);
    void fitSheet();
    void setReadOnly(bool readOnly);
    bool isReadOnly() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
