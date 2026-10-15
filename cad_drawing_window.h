#ifndef FORGECAD_DRAWING_WINDOW_H
#define FORGECAD_DRAWING_WINDOW_H
#include "cad_drawing_file.h"
#include <QMainWindow>
class QLabel;
class QAction;
namespace ForgeCad {
// Documento tavola autonomo: il modello viene letto dal file salvato.
class DrawingWindow final:public QMainWindow {
public:
    explicit DrawingWindow(QWidget *parent=nullptr);
    QString createFromModel(const QString &modelPath);
    QString openPath(const QString &path);
    QString savePath(const QString &path);
    QString updateFromSource(const QString &overridePath={});
    DrawingSheetEditor *editor()const{return editor_;}
    const DrawingFile &document()const{return drawing_;}
    QString filePath()const{return path_;}
    bool isModified()const{return modified_;}
protected:
    void closeEvent(QCloseEvent *event)override;
private:
    void installEditor();
    void refreshTitle();
    bool save(bool askPath);
    void report(const QString &error);
    DrawingFile drawing_;
    DrawingEditorModel model_;
    QString path_,sourcePath_,sourceArchivePath_;
    std::unique_ptr<DocumentFileLock> lock_;
    DrawingSheetEditor *editor_=nullptr;
    QLabel *sourceStatus_=nullptr;
    QAction *updateAction_=nullptr;
    bool modified_=false,sourceReady_=false;
};
}
#endif
