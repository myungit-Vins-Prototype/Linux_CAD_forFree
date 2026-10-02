#ifndef FORGECAD_HISTORY_GRAPH_H
#define FORGECAD_HISTORY_GRAPH_H

#include <functional>

#include <QDialog>
#include <QHash>
#include <QPointF>

#include "cad_types.h"

class QCheckBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QTextBrowser;

namespace ForgeCad {

// Vista diagnostica, sola lettura, del grafo parametrico. kind della callback:
// 0 schizzo, 1 feature.
class HistoryGraphDialog final : public QDialog {
public:
    explicit HistoryGraphDialog(QWidget *parent = nullptr);
    ~HistoryGraphDialog() override;

    void setDocument(const DocumentState &document);
    void setSelectionCallback(std::function<void(int, int)> callback) { selectionCallback_ = std::move(callback); }
    void zoomBy(double factor);
    double zoomFactor() const;

    int nodeCount() const { return nodeCount_; }
    int edgeCount() const { return edgeCount_; }
    int invalidDependencyCount() const { return invalidDependencies_; }

private:
    void rebuild();
    void fitGraph();
    void rebuildWhenPointerIdle();

    DocumentState document_;
    QGraphicsScene *scene_ = nullptr;
    QGraphicsView *view_ = nullptr;
    QTextBrowser *details_ = nullptr;
    QLabel *summary_ = nullptr;
    QCheckBox *showSketches_ = nullptr;
    QCheckBox *showLabels_ = nullptr;
    std::function<void(int, int)> selectionCallback_;
    int nodeCount_ = 0;
    int edgeCount_ = 0;
    int invalidDependencies_ = 0;
    bool firstFit_ = true;
    bool rebuildPending_ = false;
    QHash<QString, QPointF> manualPositions_;
};

}

#endif
