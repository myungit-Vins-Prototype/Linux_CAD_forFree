#include "cad_document_tabs.h"
#include "cad_widget_context.h"
#include "forgeCad2026_gui.h"
#include "cad_drawing_window.h"
#include "cad_document_io.h"
#include "cad_icons.h"
#include "cad_menu.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

namespace ForgeCad {
DocumentTabsWindow *documentTabsWindow(QWidget *widget) {
    for (auto *parent = widget; parent; parent = parent->parentWidget())
        if (auto *host = dynamic_cast<DocumentTabsWindow *>(parent)) return host;
    return nullptr;
}
void showDocumentWorkspace(QMainWindow *document, QWidget *origin, bool drawing) {
    if (auto *host = documentTabsWindow(origin)) host->addDocument(document, drawing);
    else { document->setAttribute(Qt::WA_DeleteOnClose); document->show(); }
}
DocumentTabsWindow::DocumentTabsWindow(QWidget *parent) : QMainWindow(parent) {
    setObjectName("documentTabsWindow");
    resize(1280, 860);
    const auto geometry=QSettings().value(QStringLiteral("interface/geometry")).toByteArray();
    if(!geometry.isEmpty())restoreGeometry(geometry);
    setMinimumSize(900, 640);
    setWindowTitle(QGuiApplication::applicationDisplayName());
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("documentTabs");
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setElideMode(Qt::ElideMiddle);
    tabs_->setTabPosition(tabsAtBottom() ? QTabWidget::South : QTabWidget::North);
    tabs_->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    setCentralWidget(tabs_);
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { activateCurrent(); });
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { closeDocument(tabs_->widget(index)); });
    connect(tabs_->tabBar(), &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        QMenu menu(this);
        auto *top = menu.addAction(QStringLiteral("Schede in alto"));
        auto *bottom = menu.addAction(QStringLiteral("Schede in basso"));
        const auto *chosen = menu.exec(tabs_->tabBar()->mapToGlobal(point));
        if (chosen == top) setTabsAtBottom(false);
        else if (chosen == bottom) setTabsAtBottom(true);
    });
}
bool DocumentTabsWindow::tabsAtBottom() const {
    return QSettings().value(QStringLiteral("interface/documentTabsBottom"), false).toBool();
}
void DocumentTabsWindow::setTabsAtBottom(bool bottom) {
    QSettings().setValue(QStringLiteral("interface/documentTabsBottom"), bottom);
    tabs_->setTabPosition(bottom ? QTabWidget::South : QTabWidget::North);
    for (int i = 0; i < tabs_->count(); ++i) {
        auto *page = tabs_->widget(i);
        for (auto *action : page->findChildren<QAction *>("documentTabsTopAction")) action->setChecked(!bottom);
        for (auto *action : page->findChildren<QAction *>("documentTabsBottomAction")) action->setChecked(bottom);
    }
}
PdfWindow *DocumentTabsWindow::newModel() {
    if (preparingClose_ || closingAll_) return nullptr;
    auto *model = new PdfWindow(tabs_);
    addDocument(model, false);
    return model;
}
QWidget *DocumentTabsWindow::findDocument(const QString &path) const {
    const auto canonical = canonicalDocumentPath(path);
    for (int i = 0; i < tabs_->count(); ++i) {
        auto *page = tabs_->widget(i);
        if (auto *model = dynamic_cast<PdfWindow *>(page)) {
            if (!model->isHistoricalDocument() && model->documentFilePath() == canonical) return page;
        } else if (auto *drawing = dynamic_cast<DrawingWindow *>(page)) {
            if (drawing->document().sheet.drawing.version.checkinId.isNull() && drawing->filePath() == canonical) return page;
        }
    }
    return nullptr;
}
bool DocumentTabsWindow::openPath(const QString &path) {
    if (path.isEmpty() || preparingClose_ || closingAll_) return false;
    if (auto *existing = findDocument(path)) { activateDocument(existing); return true; }
    if (QFileInfo(path).suffix().compare(QStringLiteral("drw"), Qt::CaseInsensitive) == 0) {
        auto *drawing = new DrawingWindow(tabs_);
        const auto error = drawing->openPath(path);
        if (!error.isEmpty()) { delete drawing; QMessageBox::warning(this, QStringLiteral("Apri tavola 2D"), error); return false; }
        addDocument(drawing, true);
        return true;
    }
    // La prima scheda vuota viene riutilizzata; tutti gli altri documenti restano aperti.
    auto *model = dynamic_cast<PdfWindow *>(tabs_->currentWidget());
    const bool reuse = model && model->isEmptyDocument();
    if (!reuse) model = new PdfWindow(tabs_);
    if (!model->openDocumentPathInPlace(path)) { if (!reuse) delete model; return false; }
    if (!reuse) addDocument(model, false);
    else activateDocument(model);
    return true;
}
void DocumentTabsWindow::chooseDocument() {
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Apri documento"), {},
        QStringLiteral("Documenti ForgeCAD (*.prt *.drw);;Modelli 3D (*.prt);;Tavole 2D (*.drw)"));
    if (!path.isEmpty()) openPath(path);
}
void DocumentTabsWindow::addDocument(QMainWindow *document, bool drawing) {
    if (tabs_->indexOf(document) >= 0) { activateDocument(document); return; }
    document->setAttribute(Qt::WA_DeleteOnClose);
    document->setWindowFlags(Qt::Widget);
    document->menuBar()->setNativeMenuBar(false);
    document->setProperty("drawingWorkspace", drawing);
    QMenu *file = nullptr;
    for (auto *action : document->menuBar()->actions()) if (auto *menu = action->menu()) {
        if (action->text().remove('&') == QStringLiteral("File")) file = menu;
    }
    if (drawing && file) {
        auto *first = file->actions().value(0);
        auto *create = new QAction(commandIcon("new"), QStringLiteral("Nuovo modello 3D"), document);
        create->setShortcut(QKeySequence::New); create->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(create, &QAction::triggered, this, [this] { newModel(); }); file->insertAction(first, create);
        auto *open = new QAction(commandIcon("open"), QStringLiteral("Apri documento..."), document);
        open->setShortcut(QKeySequence::Open); open->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(open, &QAction::triggered, this, [this] { chooseDocument(); }); file->insertAction(first, open);
    }
    if (file) {
        file->addSeparator();
        auto *close = file->addAction(QStringLiteral("Chiudi scheda")); close->setObjectName("closeDocumentTabAction");
        close->setShortcut(QKeySequence::Close); close->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(close, &QAction::triggered, this, [this, document] { closeDocument(document); });
        if (drawing) { auto *quit = file->addAction(QStringLiteral("Esci")); connect(quit, &QAction::triggered, this, [this] { this->close(); }); }
    }
    refreshDocumentActions(document);
    connect(document, &QWidget::windowTitleChanged, this, [this, document] { refreshTab(document); });
    tabs_->addTab(document, commandIcon(drawing ? "drawingNew" : "box"), QString());
    refreshTab(document); activateDocument(document);
}
void DocumentTabsWindow::refreshDocumentActions(QMainWindow *document) {
    for (auto *action : document->findChildren<QAction *>()) {
        if (action->shortcuts().isEmpty()) continue;
        if (action->shortcutContext() == Qt::WindowShortcut || action->shortcutContext() == Qt::WidgetWithChildrenShortcut) {
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            document->addAction(action);
        }
    }
    QMenu *view = nullptr;
    for (auto *action : document->menuBar()->actions())
        if (action->menu() && action->text().remove('&') == QStringLiteral("Visualizza")) view = action->menu();
    if (view) for (auto *action : view->actions())
        if (action->menu() && action->menu()->objectName() == QStringLiteral("documentTabsPositionMenu")) return;
    if (view) {
        view->addSeparator(); auto *position = addCadMenu(view, QStringLiteral("Schede documenti"));
        position->setObjectName("documentTabsPositionMenu");
        auto *group = new QActionGroup(position);
        for (const bool bottom : {false, true}) {
            auto *action = position->addAction(bottom ? QStringLiteral("In basso") : QStringLiteral("In alto"));
            action->setObjectName(bottom ? "documentTabsBottomAction" : "documentTabsTopAction");
            action->setCheckable(true); group->addAction(action); action->setChecked(bottom == tabsAtBottom());
            connect(action, &QAction::triggered, this, [this, bottom] { setTabsAtBottom(bottom); });
        }
    }
}
void DocumentTabsWindow::refreshTab(QWidget *document) {
    const int index = tabs_->indexOf(document); if (index < 0) return;
    QString title = document->windowTitle();
    const QString app = QStringLiteral(" - ") + QGuiApplication::applicationDisplayName();
    if (title.endsWith(app)) title.chop(app.size());
    if (title.endsWith(QStringLiteral(" — ForgeCAD"))) title.chop(QStringLiteral(" — ForgeCAD").size());
    const auto kind = document->property("drawingWorkspace").toBool() ? QStringLiteral("2D · ") : QStringLiteral("3D · ");
    tabs_->setTabText(index, kind + title);
    tabs_->setTabToolTip(index, document->windowTitle());
    if (document == tabs_->currentWidget()) setWindowTitle(document->windowTitle());
}
void DocumentTabsWindow::activateDocument(QWidget *document) {
    if(tabs_->currentWidget()==document)activateCurrent();
    else tabs_->setCurrentWidget(document);
}
void DocumentTabsWindow::activateCurrent() {
    auto *current = tabs_->currentWidget();
    if (previous_ != current) {
        if (previous_) for (auto *panel : previous_->findChildren<QWidget *>())
            if (panel->isWindow() && !panel->isHidden()) { hiddenPanels_.append(panel); panel->hide(); }
        previous_ = current;
        for (int i = hiddenPanels_.size() - 1; i >= 0; --i) {
            auto panel = hiddenPanels_[i];
            if (!panel) hiddenPanels_.removeAt(i);
            else if (current && documentWorkspaceWidget(panel)==current) { panel->show(); hiddenPanels_.removeAt(i); }
        }
    }
    if (!current) return;
    refreshTab(current);
    QEvent activated(QEvent::WindowActivate); QApplication::sendEvent(current, &activated);
    // Evita il ricalcolo o la ricreazione dei viewport durante il cambio scheda.
    if(auto *focus=current->focusWidget())focus->setFocus();
    else if(auto *window=dynamic_cast<QMainWindow *>(current))if(window->centralWidget())window->centralWidget()->setFocus();
}
void DocumentTabsWindow::closeDocument(QWidget *document) {
    if (!document || preparingClose_ || closingAll_) return;
    activateDocument(document); document->close();
}
void DocumentTabsWindow::documentClosed(QWidget *document) {
    const int index = tabs_->indexOf(document); if (index < 0) return;
    tabs_->removeTab(index);
    QTimer::singleShot(0, this, [this] {
        if (closingAll_) continueClosing();
        else if (tabs_->count() == 0) close();
    });
}
void DocumentTabsWindow::continueClosing() {
    if (!closingAll_) return;
    if (tabs_->count() == 0) { close(); return; }
    auto *document = tabs_->widget(0);
    // Il 3D chiude asincronamente il recupero su disco e richiama documentClosed.
    if (!document->close()) {
        const auto *model = dynamic_cast<PdfWindow *>(document);
        if (!model || !model->isClosingDocument()) { closingAll_ = false; tabs_->setEnabled(true); }
    }
}
void DocumentTabsWindow::closeEvent(QCloseEvent *event) {
    if (tabs_->count() == 0) { event->accept(); return; }
    event->ignore();
    if (preparingClose_ || closingAll_) return;
    preparingClose_ = true;
    // Prima tutti i consensi/salvataggi: Annulla lascia aperte anche le altre pagine.
    for (int i = 0; i < tabs_->count(); ++i) {
        auto *page = tabs_->widget(i); activateDocument(page);
        bool ready = true;
        if (auto *model = dynamic_cast<PdfWindow *>(page)) ready = model->prepareToClose();
        else if (auto *drawing = dynamic_cast<DrawingWindow *>(page)) ready = drawing->prepareToClose();
        if (!ready) { preparingClose_ = false; return; }
    }
    preparingClose_ = false; closingAll_ = true; tabs_->setEnabled(false); continueClosing();
}
bool DocumentTabsWindow::event(QEvent *event) {
    const bool result = QMainWindow::event(event);
    if (event->type() == QEvent::WindowActivate && tabs_) activateCurrent();
    return result;
}
}
