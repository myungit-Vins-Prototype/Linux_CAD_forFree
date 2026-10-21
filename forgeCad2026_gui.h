#ifndef FORGECAD2026_GUI_H
#define FORGECAD2026_GUI_H

#include <QMainWindow>
#include "cad_document_io.h"
#include "cad_document_recovery.h"
#include "cad_revision_client.h"
#include <QPointer>

class CadViewport;
class QAction;
class QMenu;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QProgressDialog;
class QWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QColor;
class QCloseEvent;
class QDockWidget;
class QTableWidget;
class QPushButton;

class PdfWindow final : public QMainWindow {
public:
    explicit PdfWindow(QWidget *parent = nullptr);
    // Apre un documento .prt (errori in una finestra di messaggio).
    bool openDocumentPath(const QString &path);
protected:
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;
private:
    friend class ViewportInteractionTest;
    // Documento su file (.prt, vedi cad_document_io).
    void newDocument();
    void newDrawing();
    void openDrawing();
    void openDocument();
    bool saveDocument(bool askPath);
    bool saveDocumentPath(const QString &path);
    enum class DocumentRevisionAction { Checkout, Checkin, CompletePending, CancelCheckout };
    QString runDocumentRevisionAction(DocumentRevisionAction action, const QString &first = {}, const QString &second = {});
    void showDocumentRevisions();
    void installDocumentRevisionPanel(QMenu *viewMenu);
    void refreshDocumentRevisionPanel();
    void openSelectedDocumentRevision();
    QDockWidget *documentRevisionDock_=nullptr;
    QTableWidget *documentRevisionHistory_=nullptr;
    QLabel *documentRevisionPanelStatus_=nullptr;
    QPushButton *documentOpenRevision_=nullptr;
    void rememberRecentDocument(const QString &path);
    void refreshRecentDocuments();
    bool maybeSaveChanges();
    void showRecoveryDocuments(bool automatic = false);
    quint64 clearDocumentRecovery();
    ForgeCad::DocumentRecovery *recovery_ = nullptr;
    ForgeCad::RecoveryEntry recoveredSource_;
    bool recoveryScanPending_ = false, recoveryScanAutomatic_ = false;
    bool recoveryClosing_ = false, recoveryCloseReady_ = false;
    quint64 recoveryCloseRevision_ = 0;
    void updateWindowTitle();
    void refreshDocumentEditAccess();
    ForgeCad::WorkspaceEditAccess documentEditAccess_;
    QVector<QPointer<QAction>> documentMutationActions_,revisionDisabledActions_;
    QLabel *documentRevisionStatus_=nullptr;
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
    std::unique_ptr<ForgeCad::DocumentFileLock> documentLock_;
    QMenu *recentDocumentsMenu_ = nullptr;
    QString glRenderer_ = QStringLiteral("in avvio");  // GL_RENDERER per la barra di stato
    QString gpuStatusText() const;
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
