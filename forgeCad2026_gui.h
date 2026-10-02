#ifndef FORGECAD2026_GUI_H
#define FORGECAD2026_GUI_H

#include <QMainWindow>

class CadViewport;
class QAction;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QProgressDialog;
class QWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QColor;
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
    void beginForegroundProgress(const QString &message, int maximum = 0);
    void updateForegroundProgress(const QString &message, int value, int maximum = 100);
    void endForegroundProgress();
    void beginBackgroundProgress(const QString &message);
    void endBackgroundProgress();

    void setDisplayMode(int mode);
    void setTheme(bool dark);
    void rebuildModelTree();
    void renameTreeItem(QTreeWidgetItem *item);
    void applyTreeBackground(const QColor &color);
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
    QProgressDialog *foregroundProgress_ = nullptr;
    QLabel *backgroundProgressLabel_ = nullptr;
    QProgressBar *backgroundProgress_ = nullptr;
    int foregroundProgressDepth_ = 0;
    int backgroundProgressDepth_ = 0;
    double blendSize_ = 0.5;  // ultima misura di raccordo o smusso (proposta la volta dopo)
    QDoubleSpinBox *pickSizeBox_ = nullptr;
    QWidget *constraintPanel_ = nullptr;  // finestra fluttuante dei vincoli (modalita' schizzo)  // misura dell'anteprima durante la scelta degli spigoli
    QWidget *historyGraphDialog_ = nullptr;  // diagnostica non modale della storyboard
};

#endif
