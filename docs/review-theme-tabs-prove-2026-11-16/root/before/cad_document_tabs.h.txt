#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QVector>
class QTabWidget;
class QCloseEvent;
class PdfWindow;
namespace ForgeCad {
class DocumentTabsWindow final : public QMainWindow {
public:
    explicit DocumentTabsWindow(QWidget *parent = nullptr);
    PdfWindow *newModel();
    bool openPath(const QString &path);
    void chooseDocument();
    void addDocument(QMainWindow *document, bool drawing);
    void activateDocument(QWidget *document);
    void refreshDocumentActions(QMainWindow *document);
    void documentClosed(QWidget *document);
    void closeDocument(QWidget *document);
    QWidget *findDocument(const QString &path) const;
    bool closingAll() const { return closingAll_; }
    bool tabsAtBottom() const;
    void setTabsAtBottom(bool bottom);
    QTabWidget *documentTabs() const { return tabs_; }
protected:
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;
private:
    void refreshTab(QWidget *document);
    void activateCurrent();
    void continueClosing();
    QTabWidget *tabs_ = nullptr;
    QPointer<QWidget> previous_;
    QVector<QPointer<QWidget>> hiddenPanels_;
    bool preparingClose_ = false, closingAll_ = false;
};
DocumentTabsWindow *documentTabsWindow(QWidget *widget);
// Pubblica la pagina nel contenitore esistente; i runner autonomi restano finestre.
void showDocumentWorkspace(QMainWindow *document, QWidget *origin, bool drawing);
}
