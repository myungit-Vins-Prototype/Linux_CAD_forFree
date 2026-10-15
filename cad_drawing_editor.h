#ifndef FORGECAD_DRAWING_EDITOR_H
#define FORGECAD_DRAWING_EDITOR_H
#include "cad_drawing_sheet.h"
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
    const std::vector<DrawingSection> &sections(const QUuid &viewId) const;
    DrawingDimensionResult dimensionResult(const QUuid &id) const;
    QString insertView(DrawingView view,DrawingViewLayout layout);
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
