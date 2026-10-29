#ifndef FORGECAD2026_STORYBOARD_H
#define FORGECAD2026_STORYBOARD_H

// Albero modello, badge e cursore della posizione nella storia.
#include <QTreeWidget>
#include <QSlider>
#include <QColor>
#include <QStyledItemDelegate>
#include <functional>

// Tipi delle voci dell'albero modello (Qt::UserRole); Qt::UserRole + 1 e' l'indice.
enum TreeItemType { kTreeInfo = -1, kTreeOrigin = 0, kTreePlane = 1, kTreeSketch = 3, kTreeExtrusion = 4, kTreeBody = 5 };

// Numero (badge) accanto al nome delle voci di gruppo dell'albero: il testo in
// kTreeBadgeRole, disegnato come una pillola dopo il nome.
constexpr int kTreeBadgeRole = Qt::UserRole + 3;

class TreeBadgeDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

inline const QColor kStoryboardPositionColor(70, 205, 255);

// Use the live palette when painting the handle: stylesheet palette colors
// can remain cached after the application switches its appearance.
class StoryboardPositionSlider final : public QSlider {
public:
    using QSlider::QSlider;
    QVector<QPair<int, int>> checkpoints;
    int activePosition = 0;
    std::function<void(int)> checkpointSelected;
    QRect handleRect() const;
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
};

class StoryboardTree final : public QTreeWidget {
public:
    explicit StoryboardTree(QWidget *parent = nullptr);
    void setSourcePickCallback(std::function<void(int, bool)> callback);
    void setMoveFeatureCallback(std::function<QString(int, int)> callback);
    void setHistoryPositionCallback(std::function<void(int)> callback);
    enum class CursorMode { Horizontal, Left, Right };
    CursorMode cursorMode() const;
    void setCursorMode(CursorMode mode);
    QSlider *historyRail() const;
    int historyPosition() const;
    void setHistoryRange(int position, int count);
    static bool isHistoryCursor(QTreeWidgetItem *item);


protected:
    void updateGeometries() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void startDrag(Qt::DropActions actions) override;

    void dragEnterEvent(QDragEnterEvent *event) override;

    void dragMoveEvent(QDragMoveEvent *event) override;

    void dragLeaveEvent(QDragLeaveEvent *event) override;

    void dropEvent(QDropEvent *event) override;

    void paintEvent(QPaintEvent *event) override;

private:
    std::function<void(int, bool)> sourcePicked_;
    void chooseSource(QTreeWidgetItem *item);
    QTreeWidgetItem *historyRootItem() const;
    int historyPositionAtY(int y) const;
    void updateHistoryRail();
    void setInsertionLine(int y, int left);

    void clearInsertionLine();

    static bool isMovableFeature(QTreeWidgetItem *item);

    QTreeWidgetItem *targetFeature(const QPoint &position) const;

    QTreeWidgetItem *normalizedTarget(QTreeWidgetItem *body, QTreeWidgetItem *target) const;

    static QTreeWidgetItem *firstFeature(QTreeWidgetItem *body);

    QWidget *historyRailViewport_ = nullptr;
    QSlider *historyRail_ = nullptr;
    CursorMode cursorMode_ = CursorMode::Horizontal;
    int railTop_ = 0;
    bool updatingRail_ = false;
    std::function<void(int)> historyPositionChanged_;
    bool movingHistoryCursor_ = false;
    int pendingHistoryPosition_ = 0;
    int historyPosition_ = 0;
    int historyCount_ = 0;
    std::function<QString(int, int)> moveFeature_;
    QTreeWidgetItem *draggedItem_ = nullptr;
    int insertionLineY_ = -1;
    int insertionLineLeft_ = 2;
};

#endif
