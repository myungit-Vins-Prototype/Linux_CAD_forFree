#ifndef FORGECAD2026_GUI_H
#define FORGECAD2026_GUI_H

#include <QMainWindow>

class CadViewport;
class QAction;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

class PdfWindow final : public QMainWindow {
public:
    explicit PdfWindow(QWidget *parent = nullptr);
private:
    void setDisplayMode(int mode);
    void setTheme(bool dark);
    void rebuildModelTree();
    void scheduleModelTreeRebuild();
    void updateUndoActions();
    void editBackground();

    CadViewport *viewport_ = nullptr;
    QLabel *modeStatus_ = nullptr;
    QTreeWidget *modelTree_ = nullptr;
    QAction *undoAction_ = nullptr;
    QAction *redoAction_ = nullptr;
    bool rebuildingTree_ = false;
    bool treeRebuildPending_ = false;
    bool suppressTreeClick_ = false;
};

#endif
