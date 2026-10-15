#ifndef FORGECAD_DRAWING_EDITOR_H
#define FORGECAD_DRAWING_EDITOR_H
#include "cad_drawing_sheet.h"
#include "cad_drawing_dimension.h"
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
    bool hasViewPlacement() const;
    void cancelViewPlacement();
    // Modifica atomica: ID di vista e quote conservati; ricalcolo della sola vista.
    QString updateView(DrawingView view,DrawingViewLayout layout);
    enum class ViewAlignment { SameRow, SameColumn };
    QString moveView(const QUuid &id,const QPointF &position);
    QString moveDimension(const QUuid &id,const QPointF &position);
    // Centri sul foglio: posizionamento una tantum, senza vincoli persistenti.
    QString alignView(const QUuid &id,const QUuid &reference,ViewAlignment alignment);
    QString removeView(const QUuid &id);
    QString undo();
    QString redo();
    bool canUndo() const;
    bool canRedo() const;
    void markSaved();
    bool isModified() const;
    QString addDimension(DrawingDimension dimension,const QPointF &position);
    QString updateDimension(DrawingDimension dimension);
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
