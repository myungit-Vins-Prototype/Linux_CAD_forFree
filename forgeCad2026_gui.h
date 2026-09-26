#ifndef FORGECAD2026_GUI_H
#define FORGECAD2026_GUI_H

#include <QMainWindow>

class CadViewport;
class QAction;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QCloseEvent;

class PdfWindow final : public QMainWindow {
public:
    explicit PdfWindow(QWidget *parent = nullptr);
    // Apre un documento .prt (errori in una finestra di messaggio).
    bool openDocumentPath(const QString &path);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    // Documento su file (.prt, vedi cad_document_io).
    void newDocument();
    void openDocument();
    bool saveDocument(bool askPath);
    bool maybeSaveChanges();
    void updateWindowTitle();

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
    QString documentPath_;
    bool documentModified_ = false;
    bool loadingDocument_ = false;
};

#endif
