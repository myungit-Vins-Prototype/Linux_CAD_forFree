#ifndef FORGECAD_DRAWING_WINDOW_H
#define FORGECAD_DRAWING_WINDOW_H
#include "cad_drawing_revisions.h"
#include <QMainWindow>
class QLabel;
class QAction;
class QMenu;
class QDockWidget;
class QTableWidget;
class QPushButton;
class QTabBar;
class QVBoxLayout;
namespace ForgeCad {
// Documento tavola autonomo: il modello viene letto dal file salvato.
class DrawingToolbarController;
class DrawingWindow final:public QMainWindow {
public:
    explicit DrawingWindow(QWidget *parent=nullptr);
    ~DrawingWindow() override;
    enum class RevisionAction {Checkout,Checkin,CompletePending,CancelCheckout};
    QString runRevisionAction(RevisionAction action,const QString &first={},const QString &second={});
    QString pinModelRevision(const DocumentVersion &version);
    QString useWorkingModel();
    QString openRevision(const QString &workingPath,const DocumentVersion &version);
    QString createFromModel(const QString &modelPath);
    QString openPath(const QString &path);
    QString savePath(const QString &path);
    QString exportPdfPath(const QString &path,DrawingPdfReport &report,bool allSheets=true)const;
    int sheetCount()const{return editor_?drawingSheetCount(drawing_):0;}
    int activeSheet()const{return activeSheet_;}
    QString setActiveSheet(int index);
    QString addSheet(const QString &name,const QSizeF &size);
    QString duplicateSheet(int index);
    QString renameSheet(int index,const QString &name);
    QString moveSheet(int from,int to);
    QString removeSheet(int index);
    QString updateFromSource(const QString &overridePath={});
    DrawingSheetEditor *editor()const{return editor_;}
    const DrawingFile &document()const{return drawing_;}
    QString filePath()const{return path_;}
    bool isModified()const{return modified_;}
protected:
    void closeEvent(QCloseEvent *event)override;
    bool event(QEvent *event)override;
private:
    void installEditor();
    void setupSheetActions();
    void installSheetTabs(QVBoxLayout *layout);
    void refreshSheetTabs();
    void showNewSheetDialog();
    void showManageSheetsDialog();
    void showPdfExportDialog();
    QString sheetMutationError();
    QString commitSheets(DrawingFile prepared,const QUuid &active,const QString &label);
    void sheetChanged(const DrawingSheet &sheet);
    void recordSheets(const QString &label);
    void resetSheetsHistory(bool clean);
    void markSheetsSaved();
    QString restoreSheets(bool redo);
    QString updateSheetsModel(DrawingFile prepared,DrawingEditorModel model);
    struct SheetHistory {QByteArray bytes;QUuid active;QString label;};
    QVector<SheetHistory> sheetHistory_;
    int sheetHistoryIndex_=0,activeSheet_=0;
    QByteArray savedDrawing_;
    bool restoringSheets_=false;
    QTabBar *sheetTabs_=nullptr;
    QPushButton *newSheetButton_=nullptr;
    DrawingToolbarController *toolbarController_=nullptr;
    void loadContents(const QString &path,DrawingFile drawing);
    QString applySource(DrawingFile drawing,DocumentState source);
    void addRevisionMenus(QMenu *file);
    void showModelRevisions();
    void showDrawingHistory();
    void installRevisionPanel();
    void refreshRevisionPanel();
    void showRevisionTableDialog();
    void refreshTitle();
    void refreshEditAccess();
    QString acquireWriteAccess();
    QString requireEditAccess();
    void refreshPageContext();
    void refreshModelUpdateNotice();
    QHash<QString,QString> revisionLabels_;
    bool save(bool askPath);
    void report(const QString &error);
    DrawingFile drawing_;
    DrawingEditorModel model_;
    QString path_,sourcePath_,sourceArchivePath_;
    std::unique_ptr<DocumentFileLock> lock_;
    QByteArray diskFingerprint_;
    DrawingSheetEditor *editor_=nullptr;
    QLabel *sourceStatus_=nullptr;
    QAction *updateAction_=nullptr;
    QWidget *modelUpdateNotice_=nullptr;
    QLabel *modelUpdateText_=nullptr;
    QPushButton *loadLatestModel_=nullptr;
    RevisionRecord availableModelRevision_;
    QMenu *editMenu_=nullptr,*insertMenu_=nullptr,*dimensionMenu_=nullptr,*viewMenu_=nullptr;
    QVector<QAction *> revisionMutationActions_;
    bool modified_=false,containerDirty_=false,sourceReady_=false;
    mutable bool busy_=false;
    QUuid modelBaseCheckinId_;
    QDockWidget *revisionDock_=nullptr;
    QLabel *revisionStatus_=nullptr;
    QTableWidget *revisionHistory_=nullptr,*projectHistory_=nullptr;
    QVector<RevisionRecord> projectRecords_;
    WorkspaceRevisionStatus revisionStatusCache_;
    bool revisionStatusKnown_=false;
    QString revisionStateText_;
    WorkspaceEditAccess editAccess_;
};
}
#endif
