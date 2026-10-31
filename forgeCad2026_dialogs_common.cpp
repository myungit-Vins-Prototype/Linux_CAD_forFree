#include "forgeCad2026_dialogs.h"
#include "cad_drawing_browser.h"
#include "cad_drawing_window.h"
#include "cad_toolbar.h"
#include "cad_curve_extract.h"

#include "kernel/fk_cancel.h"
#include "cad_spatial_sketch.h"
#include "forgeCad2026_viewport.h"
#include "cuda_support.h"
#include "cad_cuda_tessellation.h"
#include "cad_constraints.h"
#include "cad_curve_solver.h"
#include "cad_document_io.h"
#include "cad_revision_client.h"
#include "cad_export.h"
#include "cad_expression.h"
#include "cad_datum.h"
#include "cad_features.h"
#include "cad_feature_dependencies.h"
#include "cad_feature_build.h"
#include "cad_import.h"
#include "cad_mass.h"
#include "cad_measure.h"
#include "cad_pattern.h"
#include "cad_extrude.h"
#include "cad_sketch_refs.h"
#include "cad_forge.h"
#include "cad_display_cache.h"
#include "cad_shape_analysis.h"
#include "fk_curve_ops.h"
#include "cad_overlay_renderer.h"
#include <array>
#include <atomic>
#include <set>
#include "fk_classify.h"
#include "fk_surface_algo.h"
#include "fk_curve_algo.h"
#include "fk_topology.h"
#include "fk_micro.h"
#include "cad_history.h"
#include "cad_history_graph.h"
#include "cad_model_history.h"
#include "cad_topology_ref.h"
#include "cad_element_names.h"
#include "cad_icons.h"
#include "cad_kernel.h"
#include "cad_sketch_edit.h"
#include "cad_sketch_offset.h"
#include "cad_snap.h"
#include "fk_parallel.h"

#include "cad_menu.h"
#include "forgeCad2026_ui_glass.h"
#include "forgeCad2026_operation_illustration.h"
#include <QScopedValueRollback>
#include "toolbar_editor.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <thread>
#include <cstdio>
#include <limits>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

using ForgeCad::Gui::uiFont;
using ForgeCad::Gui::pointLength;
using ForgeCad::Gui::pointDistance;
using ForgeCad::Gui::kSelectionColor;
using ForgeCad::Gui::kHoverColor;
using ForgeCad::Gui::featureIconName;
using ForgeCad::Gui::wrappedNote;
using ForgeCad::Gui::runUntilApplied;



const QStringList &planeNames() {
    static const QStringList names = {QStringLiteral("Piano XY - Superiore"),
                                      QStringLiteral("Piano XZ - Frontale"),
                                      QStringLiteral("Piano YZ - Destro")};
    return names;
}

DocumentFilePreview::DocumentFilePreview(QWidget *parent ) : QFrame(parent), timer_(this) {
    setObjectName(QStringLiteral("documentFilePreview"));
    setFrameShape(QFrame::StyledPanel);
    setMinimumWidth(330);
    setMaximumWidth(390);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *title = new QLabel(QStringLiteral("<b>ANTEPRIMA</b>"), this);
    viewport_ = new CadViewport(this);
    viewport_->setObjectName(QStringLiteral("documentPreviewViewport"));
    viewport_->setMinimumSize(300, 230);
    viewport_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewport_->setReferencePlanesVisible(false);
    viewport_->setGridVisible(false);
    viewport_->setAxesVisible(false);
    viewport_->setDisplayMode(2);
    message_ = new QLabel(QStringLiteral("Seleziona un documento %1").arg(QGuiApplication::applicationDisplayName()), viewport_);
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    message_->setStyleSheet(QStringLiteral("background:rgba(20,29,39,220); color:#d7e4ec; padding:12px;"));
    details_ = new QLabel(this);
    details_->setObjectName(QStringLiteral("documentPreviewDetails"));
    details_->setWordWrap(true);
    details_->setTextFormat(Qt::RichText);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("documentPreviewProgress"));
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->hide();
    layout->addWidget(title);
    layout->addWidget(viewport_, 1);
    layout->addWidget(progress_);
    layout->addWidget(details_);
    timer_.setSingleShot(true);
    timer_.setInterval(160);
    connect(&timer_, &QTimer::timeout, this, [this] { startLoading(); });
}

void DocumentFilePreview::setPath(const QString &path) {
    currentPath_ = path;
    ++generation_;
    timer_.stop();
    const QFileInfo info(path);
    if (!info.isFile() || info.suffix().compare(QLatin1String(ForgeCad::kDocumentSuffix), Qt::CaseInsensitive) != 0) {
        clearPreview(info.isDir() ? QStringLiteral("Seleziona un file .prt")
                                  : QStringLiteral("Anteprima disponibile per i documenti .prt"));
        return;
    }
    message_->setText(QStringLiteral("Caricamento anteprima..."));
    message_->show();
    progress_->show();
    details_->setText(QStringLiteral("<b>%1</b>").arg(info.fileName().toHtmlEscaped()));
    timer_.start();
}

void DocumentFilePreview::resizeEvent(QResizeEvent *event) {
    QFrame::resizeEvent(event);
    if (message_) message_->setGeometry(viewport_->rect());
}

void DocumentFilePreview::clearPreview(const QString &message) {
    DocumentState empty;
    viewport_->loadPreviewDocument(std::move(empty));
    message_->setText(message);
    message_->setGeometry(viewport_->rect());
    message_->show();
    progress_->hide();
    details_->clear();
}

void DocumentFilePreview::startLoading() {
    const QString path = currentPath_;
    const quint64 request = generation_;
    QPointer<DocumentFilePreview> guard(this);
    QThreadPool::globalInstance()->start(QRunnable::create([guard, path, request] {
        Loaded loaded;
        // La cache puo' provenire da una build precedente: qui serve solo
        // a disegnare il riquadro e non viene mai promossa nel documento.
        loaded.error = ForgeCad::loadDocumentFile(path, loaded.state, true);
        if (loaded.error.isEmpty()) {
            loaded.hasSketches = !loaded.state.sketches.isEmpty();
            for (const ExtrusionObject &body : loaded.state.extrusions)
                loaded.cachedBodies += body.visible && body.forgeBody ? 1 : 0;
            const QFileInfo info(path);
            loaded.details = QStringLiteral("<b>%1</b><br>%2<br>%3 schizzi · %4 corpi")
                .arg(info.fileName().toHtmlEscaped())
                .arg(QLocale().formattedDataSize(info.size()))
                .arg(loaded.state.sketches.size())
                .arg(loaded.state.extrusions.size());
            if (loaded.cachedBodies == 0 && !loaded.state.extrusions.isEmpty())
                loaded.details += QStringLiteral("<br><span style='color:#ffb84d'>Il file non contiene l'anteprima 3D</span>");
        }
        QMetaObject::invokeMethod(qApp, [guard, request, loaded = std::move(loaded)]() mutable {
            if (!guard || request != guard->generation_) return;
            if (!loaded.error.isEmpty()) {
                guard->clearPreview(loaded.error);
                return;
            }
            guard->progress_->hide();
            guard->viewport_->loadPreviewDocument(std::move(loaded.state));
            guard->message_->setVisible(loaded.cachedBodies == 0 && !loaded.hasSketches);
            if (loaded.cachedBodies == 0 && !loaded.hasSketches)
                guard->message_->setText(QStringLiteral("Nessuna geometria visibile"));
            guard->details_->setText(loaded.details);
        }, Qt::QueuedConnection);
    }));
}

WindowLock::WindowLock(QMainWindow *window, CadViewport *target, QWidget *keep) : viewport(target), previousLock(target->interactionLocked()) {
    QList<QWidget *> candidates{window->menuBar()};
    for (QToolBar *bar : window->findChildren<QToolBar *>()) candidates.append(bar);
    for (QDockWidget *dock : window->findChildren<QDockWidget *>()) candidates.append(dock);
    for (QWidget *widget : candidates)
        if (widget && widget->isEnabled()) {
            widget->setEnabled(false);
            widgets.append(widget);
        }
    for (QDialog *dialog : window->findChildren<QDialog *>())
        if (dialog != keep && dialog->isVisible() && !dialog->isAncestorOf(keep) && !keep->isAncestorOf(dialog)
            && dialog->isEnabled()) {
            dialog->setEnabled(false);
            widgets.append(dialog);
        }
    const auto inside = [keep](const QObject *object) {
        for (; object; object = object->parent())
            if (object == keep) return true;
        return false;
    };
    for (QAction *action : window->findChildren<QAction *>())
        if (action->isEnabled() && !inside(action)) {
            action->setEnabled(false);
            actions.append(action);
        }
    viewport->setInteractionLocked(true);
}

WindowLock::~WindowLock() {
    for (const QPointer<QWidget> &widget : widgets)
        if (widget) widget->setEnabled(true);
    for (const QPointer<QAction> &action : actions)
        if (action) action->setEnabled(true);
    viewport->setInteractionLocked(previousLock);
}

namespace {
// Indicatore esplicito: alcuni temi non disegnano CE_SizeGrip nei pannelli figli.
class PanelResizeGrip final : public QSizeGrip {
public:
    explicit PanelResizeGrip(QWidget *parent):QSizeGrip(parent) {}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        auto color=palette().color(QPalette::WindowText);color.setAlpha(180);
        painter.setPen(QPen(color,1.5,Qt::SolidLine,Qt::RoundCap));
        for(int offset:{6,11,16})
            painter.drawLine(QPointF(width()-offset,height()-4),QPointF(width()-4,height()-offset));
    }
};
}

FunctionMessage::FunctionMessage(QWidget *parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(0);
    setMaximumHeight(96);
    setAccessibleName(QStringLiteral("Messaggio della funzione"));
}
FunctionMessage::FunctionMessage(const QString &text, QWidget *parent) : FunctionMessage(parent) { setText(text); }
void FunctionMessage::setText(const QString &text) { if (text != toPlainText()) setPlainText(text); }
QSize FunctionMessage::sizeHint() const { return QSize(240, objectName() == QStringLiteral("functionPreviewMessage") ? 36 : 72); }
QSize FunctionMessage::minimumSizeHint() const { return QSize(80, objectName() == QStringLiteral("functionPreviewMessage") ? 22 : 48); }

namespace {
QColor functionPanelColor(const QWidget *owner) {
    return ForgeCad::uiGlassPalette(owner).color(QPalette::Window).lightness() < 128
        ? QColor(28, 39, 53) : QColor(230, 238, 246);
}
}

FunctionDialogPanel::FunctionDialogPanel(QWidget *parent , bool exclusive )
        : QDialog(overlayParent(parent)), exclusive_(exclusive), panelColor_(ForgeCad::uiGlassPalette(parent).color(QPalette::Window)), captureTimer_(this) {
    setAttribute(Qt::WA_NoMousePropagation);
    embedded_ = dynamic_cast<CadViewport *>(parentWidget()) != nullptr;
    if (embedded_) {
        // Sottofinestra incorporata: QSizeGrip deve riferirsi al pannello,
        // non alla finestra principale (dove si nasconderebbe se massimizzata).
        setWindowFlags(Qt::SubWindow);
        parentWidget()->installEventFilter(this);
    }
    setObjectName(QStringLiteral("functionDialogPanel"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setSizeGripEnabled(false);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, false);
    setMinimumSize(400, 280);
    QSettings settings;
    dockSide_ = qBound(0, settings.value(QStringLiteral("view/functionPanelDockSide"), 0).toInt(), 2);
    fullHeight_ = settings.value(QStringLiteral("view/functionPanelFullHeight"), false).toBool();
    if (!settings.contains(QStringLiteral("view/functionPanelOpacity")))
        settings.setValue(QStringLiteral("view/functionPanelOpacity"), settings.value(QStringLiteral("loft/dialogOpacity"), 88));
    opacity_ = qBound(25, settings.value(QStringLiteral("view/functionPanelOpacity"),
                                         settings.value(QStringLiteral("loft/dialogOpacity"), 88)).toInt(), 100);
    blur_ = qBound(0, settings.value(QStringLiteral("view/functionPanelBlur"), 12).toInt(), 40);
    cornerRadius_ = qBound(0, settings.value(QStringLiteral("view/functionPanelCornerRadius"), 10).toInt(), 40);
    resizeGrip_ = new PanelResizeGrip(this);
    resizeGrip_->setObjectName(QStringLiteral("functionPanelResizeGrip"));
    resizeGrip_->setFixedSize(22, 22);
    resizeGrip_->setToolTip(QStringLiteral("Trascina per ridimensionare il pannello"));
    resizeGrip_->installEventFilter(this);
    // Barra d'intestazione con il nome del comando (windowTitle), disegnata
    // in paintEvent: i layout delle finestre stanno sotto grazie al margine.
    setContentsMargins(0, kHeaderHeight, 0, 0);
    headerClose_ = new QToolButton(this);
    headerClose_->setObjectName(QStringLiteral("functionPanelHeaderClose"));
    headerClose_->setText(QStringLiteral("\u2715"));
    headerClose_->setAutoRaise(true);
    headerClose_->setCursor(Qt::ArrowCursor);
    headerClose_->setFocusPolicy(Qt::NoFocus);
    headerClose_->setToolTip(QStringLiteral("Chiudi (Esc)"));
    headerClose_->setFixedSize(24, 24);
    QFont closeFont = headerClose_->font(); closeFont.setPixelSize(18); headerClose_->setFont(closeFont);
    connect(headerClose_, &QToolButton::clicked, this, &QDialog::reject);
    headerDock_ = createDockButton(this);
    captureTimer_.setSingleShot(true);
    connect(&captureTimer_, &QTimer::timeout, this, [this] { captureSceneBackdrop(); });
    updatePalette();
    qApp->installEventFilter(this);
}

FunctionDialogPanel::~FunctionDialogPanel() {
    if (viewport_) viewport_->removeGlassPanel(reinterpret_cast<quintptr>(this));
}

void FunctionDialogPanel::setHeaderVisible(bool visible) {
    headerVisible_ = visible;
    setContentsMargins(0, visible ? kHeaderHeight : 0, 0, 0);
    if (headerClose_) headerClose_->setVisible(visible);
    if (headerDock_) headerDock_->setVisible(visible && embedded_);
    update();
}

QFormLayout *FunctionDialogPanel::createScrollableForm() {
    if (form_) return form_;
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 0, 20, 16);
    root->setSpacing(13);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("functionPanelTabs"));
    tabs->setDocumentMode(true);
    tabs->setIconSize(QSize(21, 21));
    auto *page = new QWidget(tabs);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 15, 0, 0);
    pageLayout->setSpacing(12);
    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("functionDialogScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto *contents = new QWidget(scroll);
    contents->setObjectName(QStringLiteral("functionDialogContents"));
    contents->setAutoFillBackground(false);
    contents->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    scroll->viewport()->setAutoFillBackground(false);
    form_ = new QFormLayout(contents);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setHorizontalSpacing(10);
    form_->setVerticalSpacing(10);
    form_->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form_->setFormAlignment(Qt::AlignTop);
    form_->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form_->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form_->setSizeConstraint(QLayout::SetMinAndMaxSize);
    scroll->setWidget(contents);
    pageLayout->addWidget(scroll, 1);
    tabs->addTab(page, ForgeCad::Gui::operationPanelIcon(sizeIdentity_), sizeIdentity_ == QStringLiteral("fillet") ? QStringLiteral("Costante")
        : sizeIdentity_ == QStringLiteral("chamfer") ? QStringLiteral("Smusso") : QStringLiteral("Parametri"));
    root->addWidget(tabs, 1);
    return form_;
}

QColor FunctionDialogPanel::panelColor() const { return panelColor_; }

int FunctionDialogPanel::panelOpacity() const { return opacity_; }

int FunctionDialogPanel::backdropBlur() const { return blur_; }

int FunctionDialogPanel::cornerRadius() const { return cornerRadius_; }

bool FunctionDialogPanel::isEmbedded() const { return embedded_; }

QToolButton *FunctionDialogPanel::createDockButton(QWidget *parent) {
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("functionPanelDockMenu"));
    button->setText(QStringLiteral("⋮"));
    button->setFixedSize(24, 24);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(QStringLiteral("Aggancio e altezza del pannello"));
    button->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(button);
    auto *group = new QActionGroup(menu);
    const QStringList labels{QStringLiteral("Pannello libero"), QStringLiteral("Aggancia a sinistra"), QStringLiteral("Aggancia a destra")};
    const QStringList names{"functionPanelDockFree", "functionPanelDockLeft", "functionPanelDockRight"};
    for (int side = 0; side < 3; ++side) {
        auto *action = menu->addAction(labels[side]);
        action->setObjectName(names[side]); action->setCheckable(true); action->setData(side); group->addAction(action);
        connect(action, &QAction::triggered, this, [this, side] { setDockSide(side); });
    }
    menu->addSeparator();
    auto *height = menu->addAction(QStringLiteral("Usa tutta l’altezza disponibile"));
    height->setObjectName("functionPanelFullHeight"); height->setCheckable(true);
    connect(height, &QAction::triggered, this, [this](bool checked) { setFullHeight(checked); });
    connect(menu, &QMenu::aboutToShow, this, [this, group, height] {
        for (auto *action : group->actions()) action->setChecked(action->data().toInt() == dockSide_);
        height->setChecked(fullHeight_);
    });
    button->setMenu(menu); button->setVisible(embedded_);
    return button;
}

void FunctionDialogPanel::setDockSide(int side) {
    dockSide_ = qBound(0, side, 2);
    QSettings().setValue(QStringLiteral("view/functionPanelDockSide"), dockSide_);
    applyDockLayout();
}

void FunctionDialogPanel::setFullHeight(bool enabled) {
    if (fullHeight_ == enabled) return;
    if (enabled && !preferredPanelSize_.isValid()) preferredPanelSize_ = size();
    fullHeight_ = enabled;
    QSettings().setValue(QStringLiteral("view/functionPanelFullHeight"), enabled);
    if (!enabled && preferredPanelSize_.isValid()) resize(preferredPanelSize_.boundedTo(maximumPanelSize()));
    applyDockLayout();
}

void FunctionDialogPanel::applyDockLayout() {
    if (!embedded_ || !parentWidget() || applyingDock_) return;
    applyingDock_ = true;
    const int margin = 8;
    QSize target = size().boundedTo(parentWidget()->size() - QSize(2*margin, 2*margin));
    if (fullHeight_) target.setHeight(qMax(1, parentWidget()->height() - 2*margin));
    if (target != size()) resize(target);
    const int px = dockSide_ == 1 ? margin : dockSide_ == 2 ? qMax(margin, parentWidget()->width()-width()-margin)
                                                        : qBound(0, x(), qMax(0, parentWidget()->width()-width()));
    const int py = fullHeight_ ? margin : qBound(margin, y(), qMax(margin, parentWidget()->height()-height()-margin));
    if (pos() != QPoint(px,py)) move(px,py);
    applyingDock_ = false;
}

void FunctionDialogPanel::beginEmbeddedMove(const QPoint &globalPosition) {
    if (!embedded_) return;
    dragOffset_ = globalPosition - mapToGlobal(QPoint());
    moveStartGlobal_ = globalPosition; movedPanel_ = false;
    dragging_ = true; raise();
}

void FunctionDialogPanel::updateEmbeddedMove(const QPoint &globalPosition) {
    if (!dragging_ || !parentWidget()) return;
    if (!movedPanel_ && (globalPosition-moveStartGlobal_).manhattanLength() < QApplication::startDragDistance()) return;
    movedPanel_ = true; dockSide_ = 0;
    move(parentWidget()->mapFromGlobal(globalPosition - dragOffset_));
    keepInsideViewport();
}

void FunctionDialogPanel::endEmbeddedMove() {
    dragging_ = false;
    if (!movedPanel_ || !parentWidget()) return;
    const int gap = parentWidget()->width()-x()-width();
    setDockSide(x() <= 24 ? 1 : gap <= 24 ? 2 : 0);
    movedPanel_ = false;
}

void FunctionDialogPanel::placeAtLeft() {
    if (!embedded_ || !parentWidget()) return;
    if (dockSide_ || fullHeight_) { applyDockLayout(); placed_ = true; return; }
    resize(size().boundedTo(parentWidget()->size() - QSize(32, 32)));
    move(16, qMax(16, (parentWidget()->height() - height()) / 2));
    keepInsideViewport();
    placed_ = true;
}

void FunctionDialogPanel::setPanelOpacity(int percent) {
    opacity_ = qBound(25, percent, 100);
    updatePalette();
    syncGpuGlass();
    scheduleBackdropCapture(0);
    update();
}

void FunctionDialogPanel::setBackdropBlur(int pixels) {
    blur_ = qBound(0, pixels, 40);
    syncGpuGlass();
    scheduleBackdropCapture(0);
    update();
}

void FunctionDialogPanel::setCornerRadius(int pixels) {
    cornerRadius_ = qBound(0, pixels, 40);
    if(resizeGrip_) {
        const int inset=qCeil(cornerRadius_*(1.0-1.0/std::sqrt(2.0)));
        resizeGrip_->move(width()-resizeGrip_->width()-inset,height()-resizeGrip_->height()-inset);
    }
    syncGpuGlass();
    updateRoundedMask();
    update();
}

void FunctionDialogPanel::reloadAppearance() {
    const auto appearance=ForgeCad::readUiGlassAppearance();
    setPanelOpacity(appearance.opacity);
    setBackdropBlur(appearance.blur);
    setCornerRadius(appearance.radius);
}

void FunctionDialogPanel::mousePressEvent(QMouseEvent *event) {
    raise();
    if (headerVisible_ && event->button() == Qt::LeftButton && event->position().y() < kHeaderHeight)
        beginEmbeddedMove(event->globalPosition().toPoint());
    event->accept();
}
void FunctionDialogPanel::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_) updateEmbeddedMove(event->globalPosition().toPoint());
    event->accept();
}
void FunctionDialogPanel::mouseReleaseEvent(QMouseEvent *event) {
    if (dragging_ && event->button() == Qt::LeftButton) endEmbeddedMove();
    event->accept();
}

void FunctionDialogPanel::mouseDoubleClickEvent(QMouseEvent *event) { event->accept(); }

void FunctionDialogPanel::wheelEvent(QWheelEvent *event) { event->accept(); }

void FunctionDialogPanel::contextMenuEvent(QContextMenuEvent *event) { event->accept(); }

void FunctionDialogPanel::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    updatePalette();
    locateViewport();
    if (exclusive_ && viewport_ && !windowLock_)
        if (auto *window = qobject_cast<QMainWindow *>(viewport_->window()))
            windowLock_ = std::make_unique<WindowLock>(window, viewport_, this);
    if (form_ && ForgeCad::Gui::hasOperationIllustration(sizeIdentity_)
        && !findChild<QWidget *>(QStringLiteral("operationIllustration"))) {
        bool existing = false;
        for (QWidget *widget : findChildren<QWidget *>())
            existing = existing || dynamic_cast<FeatureOperationDiagram *>(widget) || dynamic_cast<LoftSelectionDiagram *>(widget);
        if (!existing) {
            const QString presentation = property("operationIllustrationId").toString();
            form_->insertRow(0, new ForgeCad::Gui::OperationIllustration(presentation.isEmpty() ? sizeIdentity_ : presentation, this));
        }
    }
    // Conferma e Annulla restano raggiungibili anche quando il form scorre.
    if (form_) for (auto *buttons : findChildren<QDialogButtonBox *>()) {
        int row = -1; QFormLayout::ItemRole role;
        form_->getWidgetPosition(buttons, &row, &role);
        if (auto *ok = buttons->button(QDialogButtonBox::Ok)) ok->setText(QStringLiteral("Conferma"));
        if (auto *cancel = buttons->button(QDialogButtonBox::Cancel)) cancel->setText(QStringLiteral("Annulla"));
        if (row >= 0) {
            const auto taken = form_->takeRow(row);
            delete taken.labelItem; delete taken.fieldItem;
            static_cast<QVBoxLayout *>(layout())->addWidget(buttons);
        }
    }
    if (form_) if (auto *preview = dynamic_cast<FunctionMessage *>(findChild<QPlainTextEdit *>(QStringLiteral("functionPreviewMessage")))) {
        int row = -1; QFormLayout::ItemRole role;
        form_->getWidgetPosition(preview, &row, &role);
        if (row >= 0) {
            auto taken = form_->takeRow(row);
            auto *note = new QWidget(this); auto *line = new QHBoxLayout(note);
            line->setContentsMargins(0, 10, 0, 0);
            if (taken.labelItem) line->addWidget(taken.labelItem->widget());
            line->addWidget(preview, 1);
            delete taken.labelItem; delete taken.fieldItem;
            auto *root = static_cast<QVBoxLayout *>(layout());
            root->insertWidget(qMax(0, root->count() - 1), note);
        }
    }
    dockSide_ = qBound(0, QSettings().value(QStringLiteral("view/functionPanelDockSide"), 0).toInt(), 2);
    fullHeight_ = QSettings().value(QStringLiteral("view/functionPanelFullHeight"), false).toBool();
    initializePanelSize();
    if (embedded_ && !placed_) placeAtLeft();
    applyDockLayout();
    raise();
    syncGpuGlass();
    scheduleBackdropCapture(0);
    QTimer::singleShot(0, this, [this] { focusFirstEditor(); });
}

void FunctionDialogPanel::hideEvent(QHideEvent *event) {
    if (viewport_) viewport_->removeGlassPanel(reinterpret_cast<quintptr>(this));
    windowLock_.reset();
    QDialog::hideEvent(event);
}

void FunctionDialogPanel::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::WindowTitleChange) update(0, 0, width(), kHeaderHeight);
    if (event->type()==QEvent::PaletteChange || event->type()==QEvent::ApplicationPaletteChange || event->type()==QEvent::StyleChange) {
        // La palette locale serve al contrasto sul vetro. Il colore del pannello
        // viene invece dalla finestra, senza riusare la copia del tema precedente.
        // Il confronto evita ricorsione quando updatePalette imposta la palette locale.
        if(panelColor_!=functionPanelColor(parentWidget())) {
            updatePalette();scheduleBackdropCapture(0);update();
        }
    }
}

void FunctionDialogPanel::moveEvent(QMoveEvent *event) {
    QDialog::moveEvent(event);
    syncGpuGlass();
    scheduleBackdropCapture(0);
}

void FunctionDialogPanel::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    if (embedded_ && placed_) keepInsideViewport();
    if (resizeGrip_) {
        const int inset=qCeil(cornerRadius_*(1.0-1.0/std::sqrt(2.0)));
        resizeGrip_->move(width() - resizeGrip_->width() - inset, height() - resizeGrip_->height() - inset);
        resizeGrip_->raise();
    }
    if (headerClose_) {
        headerClose_->move(width() - headerClose_->width() - 10, (kHeaderHeight - headerClose_->height()) / 2);
        headerClose_->raise();
    }
    if (headerDock_) { headerDock_->move(width()-64, (kHeaderHeight-24)/2); headerDock_->raise(); }
    updateRoundedMask();
    syncGpuGlass();
    scheduleBackdropCapture(0);
}

void FunctionDialogPanel::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF panelRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath panelPath;
    panelPath.addRoundedRect(panelRect, cornerRadius_, cornerRadius_);
    painter.setClipPath(panelPath);
    if ((!viewport_ || !viewport_->gpuGlassAvailable()) && !backdrop_.isNull())
        painter.drawImage(rect(), backdrop_);
    QColor color = panelColor_;
    color.setAlpha(qRound(255.0 * opacity_ / 100.0));
    painter.fillRect(rect(), color);
    if (headerVisible_) {
        // Intestazione coerente con il tema; il fondo quasi opaco mantiene
        // leggibili nome e pulsante di chiusura anche sopra una scena contrastata.
        const QRect header(0, 0, width(), kHeaderHeight);
        auto headerColor=panelColor_;headerColor.setAlpha(235);
        painter.fillRect(header,headerColor);
        painter.fillRect(QRect(18, kHeaderHeight - 1, qMax(0, width() - 36), 1), palette().color(QPalette::Mid));
        QFont font = this->font();
        font.setPixelSize(17);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(readableText(panelColor_));
        const int right = headerDock_ && headerDock_->isVisible() ? headerDock_->x() - 6
            : headerClose_ && headerClose_->isVisible() ? headerClose_->x() - 6 : width() - 10;
        const QRect text(20, 0, qMax(0, right - 20), kHeaderHeight - 2);
        painter.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(font).elidedText(windowTitle(), Qt::ElideRight, text.width()));
    }
    painter.setClipping(false);
    painter.setPen(QPen(QColor(120, 165, 205, 150), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(panelPath);
    QDialog::paintEvent(event);
}

bool FunctionDialogPanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::ApplicationPaletteChange
        && (watched == qApp || watched == this || (parentWidget() && watched == parentWidget()->window()))) {
        // La palette esplicita e il foglio stile impediscono a Qt di inviare
        // PaletteChange al pannello. Attende l'aggiornamento della finestra.
        QTimer::singleShot(0, this, [this] { updatePalette(); scheduleBackdropCapture(0); update(); });
    }
    if (watched == resizeGrip_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                resizing_ = true;
                resizeStartGlobal_ = mouse->globalPosition().toPoint();
                resizeStartSize_ = size();
                raise();
            }
            return true;
        }
        if (event->type() == QEvent::MouseMove && resizing_) {
            const QPoint delta = static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - resizeStartGlobal_;
            QSize requested = resizeStartSize_ + QSize(delta.x(), delta.y());
            requested = requested.expandedTo(minimumSize());
            if (embedded_ && parentWidget())
                requested = requested.boundedTo(dockSide_ ? maximumPanelSize() : QSize(parentWidget()->width() - x(), parentWidget()->height() - y()));
            if (fullHeight_) requested.setHeight(height());
            resize(requested); applyDockLayout();
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            if (resizing_) storeManualSize();
            resizing_ = false;
            return true;
        }
    }
    if (embedded_ && watched == parentWidget() && isVisible() && isEnabled()) {
        QPoint position;
        bool pointer = false;
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseMove) {
            position = static_cast<QMouseEvent *>(event)->position().toPoint();
            pointer = true;
        } else if (event->type() == QEvent::Wheel) {
            position = static_cast<QWheelEvent *>(event)->position().toPoint();
            pointer = true;
        }
        if (pointer && geometry().contains(position)) { raise(); event->accept(); return true; }
    }
    if (embedded_ && watched == parentWidget() && event->type() == QEvent::Resize) {
        if (!sizeIdentity_.isEmpty() && preferredPanelSize_.isValid() && !resizing_) resize(preferredPanelSize_.boundedTo(maximumPanelSize()));
        keepInsideViewport();
    }
    return QDialog::eventFilter(watched, event);
}

QWidget *FunctionDialogPanel::overlayParent(QWidget *requested) {
    if (!requested) return nullptr;
    QWidget *root = requested->window();
    if (!root) return requested;
    for (QOpenGLWidget *candidate : root->findChildren<QOpenGLWidget *>())
        if (dynamic_cast<CadViewport *>(candidate)) return candidate;
    return requested;
}

void FunctionDialogPanel::keepInsideViewport() {
    if (!embedded_ || !parentWidget() || applyingDock_) return;
    if (dockSide_ || fullHeight_) { applyDockLayout(); return; }
    const QSize maximum(qMax(1, parentWidget()->width() - 16), qMax(1, parentWidget()->height() - 16));
    if (size().width() > maximum.width() || size().height() > maximum.height())
        resize(size().boundedTo(maximum));
    move(qBound(0, x(), qMax(0, parentWidget()->width() - width())),
         qBound(0, y(), qMax(0, parentWidget()->height() - height())));
}

void FunctionDialogPanel::updateRoundedMask() {
    if (cornerRadius_ <= 0) {
        clearMask();
        return;
    }
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), cornerRadius_, cornerRadius_);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

QColor FunctionDialogPanel::readableText(const QColor &background) {
    // I pannelli mantengono un aspetto scuro/medio anche sopra scene
    // luminose. Il testo nero diventa utile soltanto su un fondo davvero
    // chiaro; una soglia piu' bassa faceva oscillare tutta la palette dopo
    // la prima cattura del framebuffer.
    const double luminance = 0.2126 * background.redF() + 0.7152 * background.greenF() + 0.0722 * background.blueF();
    return luminance > 0.70 ? QColor(20, 24, 29) : QColor(242, 246, 250);
}

void FunctionDialogPanel::applyTransparentStyle() {
    // Trasferisce lo stile della prima anteprima Raggio ai contenitori reali;
    // Fusion conserva frecce, spinner, spunte e stati nativi dei controlli.
    const auto color = [this](QPalette::ColorRole role) { return palette().color(QPalette::Active, role).name(QColor::HexRgb); };
    const QString sheet = QStringLiteral(R"(
        QDialog#functionDialogPanel, QScrollArea#functionDialogScroll,
        QScrollArea#functionDialogScroll > QWidget > QWidget, QWidget#functionDialogContents,
        QWidget#loftContents, QWidget#loftFooter { background: transparent; }
        QDialog#functionDialogPanel, QLabel, QCheckBox, QRadioButton, QToolButton { color: %1; }
        QAbstractSpinBox, QLineEdit { background: %4; color: %2; }
        QPushButton { background: transparent; color: %2; }
        QAbstractSpinBox, QComboBox { min-height: 27px; }
        QPushButton { min-height: 29px; }
        QAbstractSpinBox:disabled, QLineEdit:disabled, QComboBox:disabled, QPushButton:disabled,
        QToolButton:disabled, QCheckBox:disabled, QRadioButton:disabled { color: %11; }
        QPlainTextEdit { background: transparent; color: %1; border: none; }
        QPlainTextEdit#functionPreviewMessage, QLabel[functionHint="true"] { color: %3; font-size: 11px; }
        QComboBox { background: %4; color: %2; border: 1px solid %5; padding: 2px 22px 2px 5px; }
        QComboBox QAbstractItemView { background: %4; color: %2; border: 1px solid %5; outline: 0; }
        QComboBox QAbstractItemView::item { min-height: 24px; padding: 2px 5px; }
        QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected { background: %6; color: %7; }
        QAbstractItemView { color: %2; selection-background-color: %6; selection-color: %7; }
        QListWidget, QTreeWidget { background: transparent; border: 1px solid %5; border-radius: 5px; }
        QTableWidget { background: %4; alternate-background-color: %8; gridline-color: %5; }
        QHeaderView::section { background: %9; color: %2; border: 0; padding: 7px; }
        QTableCornerButton::section { background: %9; border: 0; }
        QTabBar::tab { padding: 10px 8px; color: %3; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { color: %7; background: %9; border-bottom: 2px solid %10; }
        QTabBar::tab:hover:!selected { background: %8; }
        QTabWidget::pane { border: 0; background: transparent; }
        QGroupBox { font-weight: 600; }
        QToolButton#functionPanelHeaderClose { color: %3; border: none; background: transparent; }
        QToolButton#functionPanelHeaderClose:hover { color: %2; background: %9; border-radius: 4px; }
        QScrollBar:vertical { background: transparent; width: 9px; margin: 0; }
        QScrollBar:horizontal { background: transparent; height: 9px; margin: 0; }
        QScrollBar::handle { background: %5; border-radius: 4px; min-width: 20px; min-height: 20px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QSizeGrip#functionPanelResizeGrip { background: transparent; }
    )").arg(color(QPalette::WindowText), color(QPalette::Text), color(QPalette::PlaceholderText),
           color(QPalette::Base), color(QPalette::Mid), color(QPalette::Highlight),
           color(QPalette::HighlightedText), color(QPalette::AlternateBase), color(QPalette::Midlight))
           .arg(color(QPalette::Link), palette().color(QPalette::Disabled, QPalette::Text).name(QColor::HexRgb));
    if (styleSheet() != sheet) setStyleSheet(sheet);
}

void FunctionDialogPanel::setSizeIdentity(const QString &identity, const QStringList &legacyNames) {
    Q_ASSERT(!panelSizeInitialized_);
    sizeIdentity_ = identity;
    legacySizeNames_ = legacyNames;
}
QString FunctionDialogPanel::panelSizeSettingsKey() const {
    if (sizeIdentity_.isEmpty()) return {}; // nessuna preferenza condivisa per pannelli senza identità
    return QStringLiteral("view/functionPanelSizes/v2/") + sizeIdentity_;
}

QSize FunctionDialogPanel::naturalPanelSize() {
    const QList<QScrollArea *> scrolls = findChildren<QScrollArea *>();
    for (QScrollArea *scroll : scrolls) {
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        if (scroll->widget()) {
            if (scroll->widget()->layout()) scroll->widget()->layout()->activate();
            scroll->widget()->adjustSize();
        }
        scroll->updateGeometry();
    }
    if (layout()) layout()->activate();
    const QMargins margins = contentsMargins();
    return (layout() ? layout()->sizeHint() + QSize(margins.left() + margins.right(), margins.top() + margins.bottom()) : sizeHint())
        .expandedTo(minimumSize());
}

QSize FunctionDialogPanel::maximumPanelSize() const {
    if (embedded_ && parentWidget())
        return QSize(qMax(1, parentWidget()->width() - 32), qMax(1, parentWidget()->height() - 32));
    QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()));
    if (!screen) screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry().size() - QSize(32, 32) : QSize(1600, 1000);
}

void FunctionDialogPanel::initializePanelSize() {
    if (panelSizeInitialized_) return;
    panelSizeInitialized_ = true;
    QSettings settings;
    const auto key = panelSizeSettingsKey();
    QSize stored = key.isEmpty() ? QSize() : settings.value(key).toSize();
    if (!stored.isValid() && !key.isEmpty()) {
        // Solo alias espliciti: il vecchio nome condiviso surfaceToolDialog
        // non viene trasferito alle tre operazioni distinte.
        QSize migrated;
        bool conflict = false;
        for (auto name : legacySizeNames_) {
            name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
            const auto value = settings.value(QStringLiteral("view/functionPanelSizes/") + name).toSize();
            if (!value.isValid()) continue;
            if (migrated.isValid() && migrated != value) conflict = true;
            migrated = value;
        }
        if (migrated.isValid() && !conflict) { stored = migrated; settings.setValue(key, stored); }
    }
    preferredPanelSize_ = (stored.isValid() ? stored : naturalPanelSize()).expandedTo(minimumSize());
    resize(preferredPanelSize_.boundedTo(maximumPanelSize()));
}

void FunctionDialogPanel::storeManualSize() {
    if (!size().isValid()) return;
    preferredPanelSize_ = fullHeight_ && preferredPanelSize_.isValid() ? QSize(width(), preferredPanelSize_.height()) : size();
    const auto key = panelSizeSettingsKey();
    if (!key.isEmpty()) QSettings().setValue(key, preferredPanelSize_);
}

void FunctionDialogPanel::focusFirstEditor() {
    if (embedded_ && parentWidget() && parentWidget()->window())
        parentWidget()->window()->activateWindow();
    // Nei pannelli incorporati il contenuto passa attraverso il viewport
    // di una QScrollArea: su alcuni compositor la focus chain della
    // QDialog non entra nel widget dello scroll. Cerca prima gli editor
    // reali nell'ordine dei figli, poi usa la catena come ripiego.
    for (QAbstractSpinBox *spin : findChildren<QAbstractSpinBox *>())
        if (spin->isVisibleTo(this) && spin->isEnabled()) {
            spin->setFocus(Qt::OtherFocusReason);
            spin->selectAll();
            return;
        }
    for (QLineEdit *line : findChildren<QLineEdit *>())
        if (line->isVisibleTo(this) && line->isEnabled() && !line->isReadOnly()
            && !qobject_cast<QAbstractSpinBox *>(line->parentWidget())) {
            line->setFocus(Qt::OtherFocusReason);
            line->selectAll();
            return;
        }
    QWidget *fallback = nullptr;
    QWidget *candidate = nextInFocusChain();
    while (candidate && candidate != this) {
        if (candidate->isVisibleTo(this) && candidate->isEnabled()
            && candidate->focusPolicy() != Qt::NoFocus) {
            if (auto *spin = qobject_cast<QAbstractSpinBox *>(candidate)) {
                spin->setFocus(Qt::OtherFocusReason);
                spin->selectAll();
                return;
            }
            if (auto *line = qobject_cast<QLineEdit *>(candidate)) {
                if (!line->isReadOnly() && !qobject_cast<QAbstractSpinBox *>(line->parentWidget())) {
                    line->setFocus(Qt::OtherFocusReason);
                    line->selectAll();
                    return;
                }
            }
            if (!fallback && qobject_cast<QComboBox *>(candidate)) fallback = candidate;
        }
        candidate = candidate->nextInFocusChain();
    }
    if (fallback) fallback->setFocus(Qt::OtherFocusReason);
}

void FunctionDialogPanel::locateViewport() {
    if (viewport_) return;
    QWidget *root = parentWidget() ? parentWidget()->window() : nullptr;
    if (!root) return;
    for (QOpenGLWidget *candidate : root->findChildren<QOpenGLWidget *>()) {
        if (auto *cad = dynamic_cast<CadViewport *>(candidate)) {
            viewport_ = cad;
            connect(viewport_, &QOpenGLWidget::frameSwapped, this,
                    [this] {
                        if (viewport_ && viewport_->renderedFrameSerial() != capturedFrameSerial_)
                            scheduleBackdropCapture(66);
                    }, Qt::QueuedConnection);
            break;
        }
    }
}

void FunctionDialogPanel::scheduleBackdropCapture(int delay) {
    if (!isVisible() || opacity_ >= 100) return;
    if (viewport_ && viewport_->gpuGlassAvailable()) {
        backdrop_ = QImage();
        return;
    }
    if (!captureTimer_.isActive() || delay == 0) captureTimer_.start(delay);
}

void FunctionDialogPanel::syncGpuGlass() {
    if (!viewport_ || !embedded_) return;
    viewport_->setGlassPanel(reinterpret_cast<quintptr>(this), geometry(), blur_, cornerRadius_, isVisible() && opacity_ < 100);
}

QImage FunctionDialogPanel::boxBlurPass(const QImage &source, int radius, bool horizontal) {
    if (radius <= 0 || source.isNull()) return source;
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.setDevicePixelRatio(source.devicePixelRatio());
    const int width = source.width(), height = source.height();
    const int count = 2 * radius + 1;
    if (horizontal) {
        for (int y = 0; y < height; ++y) {
            const QRgb *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
            QRgb *output = reinterpret_cast<QRgb *>(result.scanLine(y));
            int a = 0, r = 0, g = 0, b = 0;
            for (int k = -radius; k <= radius; ++k) {
                const QRgb pixel = input[qBound(0, k, width - 1)];
                a += qAlpha(pixel); r += qRed(pixel); g += qGreen(pixel); b += qBlue(pixel);
            }
            for (int x = 0; x < width; ++x) {
                output[x] = qRgba(r / count, g / count, b / count, a / count);
                const QRgb removed = input[qBound(0, x - radius, width - 1)];
                const QRgb added = input[qBound(0, x + radius + 1, width - 1)];
                a += qAlpha(added) - qAlpha(removed);
                r += qRed(added) - qRed(removed);
                g += qGreen(added) - qGreen(removed);
                b += qBlue(added) - qBlue(removed);
            }
        }
    } else {
        for (int x = 0; x < width; ++x) {
            int a = 0, r = 0, g = 0, b = 0;
            for (int k = -radius; k <= radius; ++k) {
                const QRgb pixel = reinterpret_cast<const QRgb *>(source.constScanLine(qBound(0, k, height - 1)))[x];
                a += qAlpha(pixel); r += qRed(pixel); g += qGreen(pixel); b += qBlue(pixel);
            }
            for (int y = 0; y < height; ++y) {
                reinterpret_cast<QRgb *>(result.scanLine(y))[x] = qRgba(r / count, g / count, b / count, a / count);
                const QRgb removed = reinterpret_cast<const QRgb *>(source.constScanLine(qBound(0, y - radius, height - 1)))[x];
                const QRgb added = reinterpret_cast<const QRgb *>(source.constScanLine(qBound(0, y + radius + 1, height - 1)))[x];
                a += qAlpha(added) - qAlpha(removed);
                r += qRed(added) - qRed(removed);
                g += qGreen(added) - qGreen(removed);
                b += qBlue(added) - qBlue(removed);
            }
        }
    }
    return result;
}

QImage FunctionDialogPanel::blurScene(QImage image, int radius) {
    if (radius <= 0) return image;
    // Tre filtri box separabili approssimano una gaussiana senza ridurre
    // la risoluzione: linee e silhouette restano morbide ma non a blocchi.
    const int passRadius = qMax(1, radius / 2);
    for (int pass = 0; pass < 3; ++pass) {
        image = boxBlurPass(image, passRadius, true);
        image = boxBlurPass(image, passRadius, false);
    }
    return image;
}

void FunctionDialogPanel::captureSceneBackdrop() {
    if (!isVisible() || opacity_ >= 100) {
        backdrop_ = QImage();
        return;
    }
    locateViewport();
    if (!viewport_ || !viewport_->isVisible() || size().isEmpty()) return;
    QImage frame = viewport_->panelBackdropFrame();
    if (frame.isNull()) return;
    // Il pannello e' figlio del viewport: pos() e size() sono nello stesso
    // sistema di coordinate della scena e identificano esattamente cio'
    // che si trova sotto il vetro, anche durante il trascinamento.
    frame.setDevicePixelRatio(1.0);
    const qreal viewportDpr = viewport_->devicePixelRatioF();
    const QSize viewportPixels(qMax(1, qRound(viewport_->width() * viewportDpr)),
                               qMax(1, qRound(viewport_->height() * viewportDpr)));
    if (frame.size() != viewportPixels)
        frame = frame.scaled(viewportPixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // Un pixel di lavoro per pixel logico e' sufficiente per un'immagine
    // volutamente sfocata e riduce fino a quattro volte il costo su HiDPI.
    const QSize scenePixels(qMax(1, width()), qMax(1, height()));
    const QRectF source(x() * viewportDpr, y() * viewportDpr,
                        width() * viewportDpr, height() * viewportDpr);
    QImage scene(scenePixels, QImage::Format_ARGB32_Premultiplied);
    scene.setDevicePixelRatio(1.0);
    scene.fill(Qt::transparent);
    {
        QPainter painter(&scene);
        painter.drawImage(QRectF(QPointF(0, 0), size()), frame, source);
    }
    backdrop_ = blurScene(std::move(scene), blur_);
    capturedFrameSerial_ = viewport_->renderedFrameSerial();
    updatePalette();
    update();
}

void FunctionDialogPanel::updatePalette() {
    if(updatingPalette_)return;
    QScopedValueRollback<bool> guard(updatingPalette_,true);
    // Stesse finiture della bozza Raggio nei due temi dell'applicazione.
    const QColor ownerColor = functionPanelColor(parentWidget());
    const bool dark = ownerColor.lightness() < 128;
    if(panelColor_!=ownerColor){panelColor_=ownerColor;backdrop_=QImage();}
    QColor behind = panelColor_;
    if (!backdrop_.isNull()) {
        // Un solo pixel (prima quello centrale) rendeva il testo instabile:
        // bastava che sotto il centro passasse una faccia chiara per
        // convertire in nero tutte le scritte. Campiona l'intera area con
        // una griglia regolare, abbastanza economica da rifare a ogni frame.
        constexpr int samples = 7;
        int red = 0, green = 0, blue = 0;
        for (int y = 0; y < samples; ++y)
            for (int x = 0; x < samples; ++x) {
                const QColor pixel = backdrop_.pixelColor((2 * x + 1) * backdrop_.width() / (2 * samples),
                                                          (2 * y + 1) * backdrop_.height() / (2 * samples));
                red += pixel.red(); green += pixel.green(); blue += pixel.blue();
            }
        behind = QColor(red / (samples * samples), green / (samples * samples), blue / (samples * samples));
    }
    const double alpha = opacity_ / 100.0;
    const QColor visible = QColor::fromRgbF(alpha * panelColor_.redF() + (1.0 - alpha) * behind.redF(),
                                            alpha * panelColor_.greenF() + (1.0 - alpha) * behind.greenF(),
                                            alpha * panelColor_.blueF() + (1.0 - alpha) * behind.blueF());
    const QColor foreground = readableText(visible);
    QPalette p = ForgeCad::uiGlassPalette(parentWidget());
    const QColor text(dark ? "#dce7f2" : "#203246");
    const QColor muted(dark ? "#a8bbcd" : "#536a80");
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(group, QPalette::Window, ownerColor);
        p.setColor(group, QPalette::WindowText, foreground);
        p.setColor(group, QPalette::Text, text);
        p.setColor(group, QPalette::ButtonText, text);
        p.setColor(group, QPalette::ToolTipText, text);
        p.setColor(group, QPalette::ToolTipBase, QColor(dark ? "#182330" : "#f4f8fc"));
        p.setColor(group, QPalette::HighlightedText, QColor(dark ? "#e5f4ff" : "#16334b"));
        p.setColor(group, QPalette::Highlight, QColor(dark ? "#305472" : "#c6e4f5"));
        p.setColor(group, QPalette::AlternateBase, QColor(dark ? "#1d2b3b" : "#e8f0f8"));
        p.setColor(group, QPalette::Button, QColor(dark ? "#1f2e3f" : "#edf3f9"));
        p.setColor(group, QPalette::Base, QColor(dark ? "#182330" : "#f4f8fc"));
        p.setColor(group, QPalette::Mid, QColor(dark ? "#3b5067" : "#aebfd0"));
        p.setColor(group, QPalette::Midlight, QColor(dark ? "#243447" : "#dbe6f1"));
        p.setColor(group, QPalette::Light, QColor(dark ? "#2a4156" : "#d9eaf6"));
        p.setColor(group, QPalette::Dark, QColor(dark ? "#304253" : "#bfcedc"));
        p.setColor(group, QPalette::Link, QColor(dark ? "#63d3ff" : "#237ea4"));
        p.setColor(group, QPalette::PlaceholderText, muted);
    }
    const QColor disabled(dark ? "#758b9f" : "#63798e");
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    setPalette(p);
    applyTransparentStyle();
}

FeatureOperationDiagram::FeatureOperationDiagram(Kind kind, QWidget *parent ) : QWidget(parent), kind_(kind) {
    setMinimumSize(280, 211);
    setFixedHeight(211);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FeatureOperationDiagram::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    ForgeCad::Gui::paintOperationIllustration(painter, rect(), kind_ == Extrusion ? QStringLiteral("extrusion")
        : kind_ == Revolution ? QStringLiteral("revolution") : QStringLiteral("sweep"));
}

bool ForgeCad::Gui::runUntilApplied(QDialog &dialog, QFormLayout *form, QDialogButtonBox *buttons, const std::function<QString()> &apply) {
    auto *errorLabel = new FunctionMessage(&dialog);
    errorLabel->setObjectName(QStringLiteral("functionApplyError"));
    errorLabel->setStyleSheet(QStringLiteral("color: #ff7b72; font-weight: 600;"));
    errorLabel->hide();
    int row = -1;
    QFormLayout::ItemRole role;
    form->getWidgetPosition(buttons, &row, &role);
    if (row >= 0) form->insertRow(row, errorLabel);
    else if (auto *box = dynamic_cast<QBoxLayout *>(buttons->parentWidget()->layout()))
        box->insertWidget(qMax(0, box->indexOf(buttons)), errorLabel);
    else
        form->addRow(errorLabel);
    QObject::disconnect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, errorLabel, apply] {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply();
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) {
            dialog.accept();
            return;
        }
        QString visible = error;
        visible.remove(QRegularExpression(QStringLiteral("\\s*\\[\\[loft-section=\\d+\\]\\]")));
        errorLabel->setText(visible + QStringLiteral("\nCorreggi i valori e riprova, oppure Annulla."));
        errorLabel->show();
        if (!dynamic_cast<FunctionDialogPanel *>(&dialog)) dialog.adjustSize();
    });
    return dialog.exec() == QDialog::Accepted;
}

bool runUntilAppliedModeless(QMainWindow *window, CadViewport *viewport, QDialog &dialog, QFormLayout *form, QDialogButtonBox *buttons,
                                    const std::function<QString()> &apply) {
    auto *errorLabel = new FunctionMessage(&dialog);
    errorLabel->setObjectName(QStringLiteral("functionApplyError"));
    errorLabel->setStyleSheet(QStringLiteral("color: #ff7b72; font-weight: 600;"));
    errorLabel->hide();
    int row = -1;
    QFormLayout::ItemRole role;
    form->getWidgetPosition(buttons, &row, &role);
    if (row >= 0) form->insertRow(row, errorLabel);
    else if (auto *box = dynamic_cast<QBoxLayout *>(buttons->parentWidget()->layout()))
        box->insertWidget(qMax(0, box->indexOf(buttons)), errorLabel);
    else
        form->addRow(errorLabel);
    QObject::disconnect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, errorLabel, apply] {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply();
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) {
            dialog.accept();
            return;
        }
        QString visible = error;
        visible.remove(QRegularExpression(QStringLiteral("\\s*\\[\\[loft-section=\\d+\\]\\]")));
        errorLabel->setText(visible + QStringLiteral("\nCorreggi i valori e riprova, oppure Annulla."));
        errorLabel->show();
        if (!dynamic_cast<FunctionDialogPanel *>(&dialog)) dialog.adjustSize();
    });
    return runModeless(window, viewport, dialog);
}

PreviewScope::PreviewScope(CadViewport *target, QDialog &dialog, QFormLayout *form, int replacedIndex, QBoxLayout *outside )
        : viewport(target), index(replacedIndex) {
    if (!viewport) return;
    label = new FunctionMessage(QStringLiteral("in calcolo..."), &dialog);
    label->setObjectName(QStringLiteral("functionPreviewMessage"));
    if (outside) {
        auto *row = new QWidget(&dialog);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(new QLabel(QStringLiteral("Anteprima:"), row));
        layout->addWidget(label, 1);
        outside->addWidget(row);
    } else {
        form->addRow(QStringLiteral("Anteprima:"), label);
    }
    FunctionMessage *status = label;
    viewport->setPreviewCallback([status](const QString &error) {
        status->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
    });
}

void PreviewScope::request(const ExtrusionObject &definition) const {
    if (!viewport) return;
    label->setText(QStringLiteral("in calcolo..."));
    viewport->requestPreview(definition, index);
}

PreviewScope::~PreviewScope() {
    if (!viewport) return;
    viewport->setPreviewCallback({});
    viewport->clearPreview();
}

void connectReferenceList(QListWidget *list, QDialog &dialog, CadViewport *viewport,
                                 std::function<QVector<GeometryRef>(QListWidgetItem *)> references) {
    const auto highlight = [list, viewport, references] {
        QVector<GeometryRef> refs;
        if (list->isEnabled())
            for (QListWidgetItem *item : list->selectedItems()) refs += references(item);
        viewport->setReferenceListHighlight(list, refs);
    };
    QObject::connect(list, &QListWidget::itemSelectionChanged, &dialog, highlight);
    QObject::connect(list, &QListWidget::itemClicked, &dialog, [highlight](QListWidgetItem *) { highlight(); });
    QObject::connect(&dialog, &QDialog::finished, viewport, [viewport, list] { viewport->clearReferenceListHighlight(list); });
    QObject::connect(list, &QObject::destroyed, viewport, [viewport, list] { viewport->clearReferenceListHighlight(list); });
}

GeometryReferenceList::GeometryReferenceList(QDialog &dialog, CadViewport *viewport, const QString &name)
        : QListWidget(&dialog), viewport_(viewport) {
    setObjectName(name);
    setMinimumHeight(60);
    setMaximumHeight(120);
    setToolTip(QStringLiteral("Seleziona una voce per evidenziarla nella vista"));
    connectReferenceList(this, dialog, viewport, [this](QListWidgetItem *item) {
        const int index = row(item);
        return index >= 0 && index < refs_.size() ? QVector<GeometryRef>{refs_.at(index)} : QVector<GeometryRef>();
    });
}

void GeometryReferenceList::setReferences(const QVector<GeometryRef> &refs) {
    const bool highlighted = viewport_->referenceListHighlighted(this);
    const int oldRow = currentRow();
    const bool selected = oldRow >= 0 && oldRow < refs_.size() && currentItem()->isSelected();
    const GeometryRef previous = selected ? refs_.at(oldRow) : GeometryRef();
    const QSignalBlocker blocker(this);
    refs_ = refs;
    clear();
    int selectedRow = -1;
    for (int i = 0; i < refs.size(); ++i) {
        const GeometryRef &ref = refs.at(i);
        QString label = ForgeCad::geometryRefText(ref, viewport_->sketches(), viewport_->extrusions());
        if (ref.kind == 4 && ref.point.subshape >= 0) label += QStringLiteral(" — E%1").arg(ref.point.subshape);
        addItem(QStringLiteral("%1. %2").arg(i + 1).arg(label));
        if (selected && sameReference(previous, ref)) selectedRow = i;
    }
    setCurrentRow(selectedRow);
    if (highlighted)
        viewport_->setReferenceListHighlight(this, isEnabled() && selectedRow >= 0
            ? QVector<GeometryRef>{refs.at(selectedRow)} : QVector<GeometryRef>());
}

void GeometryReferenceList::setEdges(int body, const QVector<EdgePoint> &edges) {
    QVector<GeometryRef> refs;
    for (const EdgePoint &point : edges) {
        GeometryRef ref;
        ref.kind = point.role == kEdgePointFaceBoundary ? 5 : 4;
        ref.index = body;
        ref.point = point;
        refs.append(ref);
    }
    setReferences(refs);
}

void GeometryReferenceList::changeEvent(QEvent *event) {
    QListWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange && !isEnabled()) viewport_->clearReferenceListHighlight(this);
}

bool GeometryReferenceList::sameReference(const GeometryRef &a, const GeometryRef &b) {
    return a.kind == b.kind && a.index == b.index && a.element == b.element
        && a.point.subshape == b.point.subshape && a.point.geometry == b.point.geometry
        && a.point.role == b.point.role && a.point.x == b.point.x && a.point.y == b.point.y && a.point.z == b.point.z;
}

BodyPicker::BodyPicker(QDialog &dialog, CadViewport *viewport, int replaced, const QVector<int> &candidates, QComboBox *combo,
               QListWidget *list )
        : viewport_(viewport), replaced_(replaced), candidates_(candidates), combo_(combo), list_(list) {
    button_ = new QPushButton(QStringLiteral("Dalla vista"), &dialog);
    button_->setCheckable(true);
    button_->setToolTip(list ? QStringLiteral("Clicca i corpi nella vista per spuntarli o toglierli; Esc nella vista termina")
                             : QStringLiteral("Clicca una faccia del corpo nella vista"));
    QObject::connect(button_, &QPushButton::clicked, &dialog, [this] {
        if (active_) stop();
        else start();
        if (changed) changed();
    });
    if (combo_) QObject::connect(combo_, &QComboBox::currentIndexChanged, &dialog, [this] { waiting_ = false; });
    if (list_) QObject::connect(list_, &QListWidget::itemChanged, &dialog, [this] { waiting_ = false; });
    if (replaced_ >= 0 && combo_ && !combo_->isEnabled()) button_->setEnabled(false);
}

BodyPicker::~BodyPicker() {
    if (active_) viewport_->cancelReferencePick();
    if (installed_) viewport_->setReferencePickCallback(nullptr);
}

QWidget *BodyPicker::row(QWidget *field) const {
    auto *row = new QWidget(field->parentWidget());
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(field, 1);
    layout->addWidget(button_, 0, list_ ? Qt::AlignTop : Qt::Alignment());
    return row;
}

bool BodyPicker::active() const { return active_; }

bool BodyPicker::waiting() const { return waiting_; }

void BodyPicker::acceptSketches(int start) {
    sketchStart_ = start;
    button_->setToolTip(QStringLiteral("Clicca una faccia del corpo o un'entita' dello schizzo nella vista"));
}

bool BodyPicker::handling() const { return handling_; }

QString BodyPicker::hint() const {
    return list_ ? QStringLiteral("Clicca i corpi nella vista (di nuovo per toglierli); Esc nella vista termina.")
                 : sketchStart_ >= 0 ? QStringLiteral("Clicca nella vista una faccia del corpo o un'entita' dello schizzo.")
                                     : QStringLiteral("Clicca nella vista una faccia del corpo.");
}

void BodyPicker::start(bool waiting ) {
    if (!button_->isEnabled()) return;
    if (starting) starting();  // la finestra lascia la sua scelta nella vista
    const int roles = sketchStart_ >= 0 ? ForgeCad::DatumRoleFace | ForgeCad::DatumRoleCurve | ForgeCad::DatumRoleLine : ForgeCad::DatumRoleFace;
    const QString error = viewport_->beginReferencePick(roles, replaced_);
    active_ = error.isEmpty();
    waiting_ = active_ && waiting;
    button_->setChecked(active_);
    if (!active_ && message) message(error, true);
}

void BodyPicker::stop() {
    if (active_) viewport_->cancelReferencePick();
    active_ = false;
    button_->setChecked(false);
}

bool BodyPicker::handle(bool picked, const GeometryRef &ref) {
    if (!active_) return false;
    if (!picked) {  // Esc nella vista
        stop();
        if (changed) changed();
        return true;
    }
    // Entita' (o punto) di uno schizzo, se l'elenco ha gli schizzi.
    if (sketchStart_ >= 0 && combo_ && (ref.kind == 6 || ref.kind == 7) && ref.index >= 0 && sketchStart_ + ref.index < combo_->count()) {
        handling_ = true;
        waiting_ = false;
        combo_->setCurrentIndex(sketchStart_ + ref.index);
        stop();
        handling_ = false;
        if (changed) changed();
        return true;
    }
    // Uno spigolo (con gli schizzi si scelgono anche le curve): il suo corpo.
    const int body = resolve ? resolve(ref) :
        (ref.kind == 5 || (sketchStart_ >= 0 && ref.kind == 4) ? resultBodyForPick(viewport_, candidates_, ref.index) : -1);
    handling_ = true;
    if (body < 0) {
        if (message) message(QStringLiteral("Quel corpo non si puo' usare qui (o non esiste in questo punto della storia)."), true);
    } else if (combo_) {
        waiting_ = false;
        combo_->setCurrentIndex(int(candidates_.indexOf(body)));
        stop();
    } else {
        waiting_ = false;
        for (int row = 0; row < list_->count(); ++row) {
            QListWidgetItem *item = list_->item(row);
            if (item->data(Qt::UserRole).toInt() == body && (item->flags() & Qt::ItemIsEnabled))
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    }
    handling_ = false;
    if (active_) start();  // l'elenco a spunte (o un corpo non valido): si continua
    if (changed) changed();
    return true;
}

void BodyPicker::install() {
    installed_ = true;
    viewport_->setReferencePickCallback([this](bool picked, GeometryRef ref) { handle(picked, ref); });
}

QString resultBodyLabel(CadViewport *viewport, int index, bool markHidden ) {
    const ExtrusionObject &body = viewport->extrusions().at(index);
    QString text = body.name;
    for (const ModelBody &model : viewport->modelBodies())
        if (model.id == body.modelBodyId) {
            text = model.name;
            if (!body.solid) text += QStringLiteral(" (superficie)");
            if (markHidden && !model.visible) text += QStringLiteral(" (nascosto)");
            return text;
        }
    if (!body.solid) text += QStringLiteral(" (superficie)");
    if (markHidden && !body.visible) text += QStringLiteral(" (nascosto)");
    return text;
}

QVector<int> operandBodies(CadViewport *viewport, int replaced, const QVector<int> &used,
                                  const std::function<bool(const ExtrusionObject &)> &accept) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    QVector<int> indices;
    for (int index : viewport->resultBodiesBefore(replaced))
        if (accept(bodies.at(index))) indices.append(index);
    for (int index : used)
        if (index >= 0 && index < bodies.size() && (replaced < 0 || index < replaced) && !indices.contains(index) && bodies.at(index).forgeBody
            && accept(bodies.at(index)))
            indices.append(index);
    std::sort(indices.begin(), indices.end());
    return indices;
}

QVector<int> fillMergeBodyList(QListWidget *list, CadViewport *viewport, int replaced, const QVector<int> &used) {
    const auto solid = [](const ExtrusionObject &body) { return body.forgeBody && body.solid; };
    const QVector<int> indices = operandBodies(viewport, replaced, used, solid);
    for (int index : indices) {
        const bool chosen = used.contains(index);
        auto *item = new QListWidgetItem(resultBodyLabel(viewport, index, !chosen), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(chosen ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, index);
    }
    return indices;
}

LoftSelectionDiagram::LoftSelectionDiagram(QWidget *parent ) : QWidget(parent) {
    setMinimumSize(280, 211);
    setFixedHeight(211);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(QStringLiteral("1. Scegli e ordina le sezioni.  2. Aggiungi guide aperte che attraversano ogni sezione.  3. Regola la continuità alle estremità."));
}

void LoftSelectionDiagram::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    ForgeCad::Gui::paintOperationIllustration(painter, rect(), QStringLiteral("loft"));
}


SurfaceEndControls::SurfaceEndControls(QDialog &dialog, QFormLayout &form, CadViewport *viewport, int owner, ExtrusionObject &definition)
        : viewport_(viewport), owner_(owner), definition_(definition) {
    const QStringList modes{QStringLiteral("G0 — libero"), QStringLiteral("G1 — normale al profilo"),
                            QStringLiteral("G2 — normale, curvatura nulla"), QStringLiteral("G1 — tangente a facce adiacenti")};
    for (int side = 0; side < 2; ++side) {
        modes_[side] = new QComboBox(&dialog);
        modes_[side]->setObjectName(side == 0 ? QStringLiteral("surfaceStartContinuity") : QStringLiteral("surfaceEndContinuity"));
        modes_[side]->addItems(modes);
        modes_[side]->setCurrentIndex(qBound(0, side == 0 ? definition.loftStartContinuity : definition.loftEndContinuity, 3));
        influences_[side] = new QDoubleSpinBox(&dialog);
        influences_[side]->setRange(0.0, 100.0);
        influences_[side]->setSuffix(QStringLiteral(" %"));
        influences_[side]->setValue(100.0 * (side == 0 ? definition.loftStartInfluence : definition.loftEndInfluence));
        faces_[side] = new QLabel(&dialog);
        faces_[side]->setWordWrap(true);
        picks_[side] = new QPushButton(QStringLiteral("Scegli facce nella vista"), &dialog);
        picks_[side]->setObjectName(side == 0 ? QStringLiteral("surfaceStartFacesPick") : QStringLiteral("surfaceEndFacesPick"));
        picks_[side]->setCheckable(true);
        auto *clear = new QPushButton(QStringLiteral("Azzera facce"), &dialog);
        auto *row = new QWidget(&dialog);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(picks_[side]); layout->addWidget(clear);
        form.addRow(side == 0 ? QStringLiteral("Primo profilo:") : QStringLiteral("Ultimo profilo:"), modes_[side]);
        form.addRow(QStringLiteral("Influenza:"), influences_[side]);
        form.addRow(QStringLiteral("Facce adiacenti:"), faces_[side]);
        form.addRow(row);
        QObject::connect(modes_[side], &QComboBox::currentIndexChanged, &dialog, [this] { sync(); });
        QObject::connect(influences_[side], &QDoubleSpinBox::valueChanged, &dialog, [this] { sync(); });
        QObject::connect(picks_[side], &QPushButton::clicked, &dialog, [this, side] {
            if (active_ == side) stop(); else start(side);
            sync();
        });
        QObject::connect(clear, &QPushButton::clicked, &dialog, [this, side] { refs(side).clear(); sync(); });
    }
    status_ = new FunctionMessage(&dialog);
    form.addRow(status_);
    auto *note = new QLabel(QStringLiteral("La tangenza alle facce richiede che il profilo coincida con il loro bordo. "
                                          "Con tangenza attiva i collegamenti tra i profili diventano curvi. "
                                          "Per G1 alle facce l'influenza regola la lunghezza della tangente."), &dialog);
    note->setWordWrap(true); form.addRow(note);
    sync();
}

SurfaceEndControls::~SurfaceEndControls() { viewport_->cancelReferencePick(); viewport_->setReferencePickCallback({}); viewport_->setReferenceMarks({}); }

void SurfaceEndControls::stop() { viewport_->cancelReferencePick(); active_ = -1; }

void SurfaceEndControls::copyTo(ExtrusionObject &d) const {
    d.loftStartContinuity = definition_.loftStartContinuity; d.loftEndContinuity = definition_.loftEndContinuity;
    d.loftStartInfluence = definition_.loftStartInfluence; d.loftEndInfluence = definition_.loftEndInfluence;
    d.loftStartFaces = definition_.loftStartFaces; d.loftEndFaces = definition_.loftEndFaces;
}

QVector<GeometryRef> &SurfaceEndControls::refs(int side) { return side == 0 ? definition_.loftStartFaces : definition_.loftEndFaces; }

void SurfaceEndControls::sync() {
    definition_.loftStartContinuity = modes_[0]->currentIndex(); definition_.loftEndContinuity = modes_[1]->currentIndex();
    for (int side = 0; side < 2; ++side) {
        const bool contact = modes_[side]->currentIndex() == 3;
        influences_[side]->setMinimum(contact ? 1.0 : 0.0);
        influences_[side]->setEnabled(modes_[side]->currentIndex() > 0);
        picks_[side]->setEnabled(contact); picks_[side]->setChecked(active_ == side);
        QStringList names;
        for (const auto &ref : refs(side)) names.append(ForgeCad::geometryRefText(ref, viewport_->sketches(), viewport_->extrusions()));
        faces_[side]->setText(names.isEmpty() ? QStringLiteral("nessuna faccia") : names.join(QStringLiteral(", ")));
    }
    definition_.loftStartInfluence = influences_[0]->value() / 100.0; definition_.loftEndInfluence = influences_[1]->value() / 100.0;
    if (changed) changed();
}

void SurfaceEndControls::start(int side) {
    active_ = side;
    viewport_->setReferencePickCallback([this](bool picked, GeometryRef ref) {
        if (!picked || active_ < 0) { active_ = -1; sync(); return; }
        if (ref.kind != 5) { status_->setText(QStringLiteral("Scegli una faccia.")); return; }
        auto &list = refs(active_);
        int existing = -1;
        for (int k = 0; k < list.size(); ++k)
            if (list[k].index == ref.index && list[k].point.subshape == ref.point.subshape) existing = k;
        if (existing < 0) list.append(ref); else list.removeAt(existing);
        viewport_->setReferenceMarks(definition_.loftStartFaces + definition_.loftEndFaces);
        sync();
        viewport_->beginReferencePick(ForgeCad::DatumRoleFace, owner_);
    });
    const QString error = viewport_->beginReferencePick(ForgeCad::DatumRoleFace, owner_);
    status_->setText(error.isEmpty() ? QStringLiteral("Clicca le facce adiacenti; un secondo clic le toglie. Esc termina la scelta.") : error);
    if (!error.isEmpty()) active_ = -1;
}

bool runModeless(QMainWindow *window, CadViewport *viewport, QDialog &dialog) {
    // Le opzioni non incorporate (STL/OBJ) restano sopra la finestra madre
    // anche quando il viewport riceve il focus per ruotare la vista.
    if (dialog.isWindow()) dialog.setWindowFlag(Qt::Tool, true);
    dialog.setModal(false);
    const WindowLock lock(window, viewport, &dialog);
    dialog.show();
    QEventLoop loop;
    QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
    loop.exec();
    return dialog.result() == QDialog::Accepted;
}

SketchViewUnlock::SketchViewUnlock(CadViewport *target) : viewport(target) { viewport->setSketchViewUnlocked(true); }

SketchViewUnlock::~SketchViewUnlock() { viewport->setSketchViewUnlocked(false); }

QString logicalBodyLabel(CadViewport *viewport, int index) {
    const ExtrusionObject &body = viewport->extrusions().at(index);
    QString text = body.name;
    for (const ModelBody &model : viewport->modelBodies())
        if (model.id == body.modelBodyId) {
            text = model.name;
            break;
        }
    if (!body.solid) text += QStringLiteral(" (superficie)");
    return text;
}

int resultBodyForPick(CadViewport *viewport, const QVector<int> &candidates, int picked) {
    if (candidates.contains(picked)) return picked;
    if (picked < 0 || picked >= viewport->extrusions().size()) return -1;
    const quint64 body = viewport->extrusions().at(picked).modelBodyId;
    for (int candidate : candidates)
        if (body && viewport->extrusions().at(candidate).modelBodyId == body) return candidate;
    return -1;
}

ThreadDiagram::ThreadDiagram(Phase phase, QWidget *parent ) : QWidget(parent), phase_(phase) {
    setMinimumSize(280, 88);
    setMaximumWidth(360);
}

void ThreadDiagram::setValues(bool internal, double angle, double pitch, double length, bool leftHanded) {
    internal_ = internal, angle_ = angle, pitch_ = pitch, length_ = length, leftHanded_ = leftHanded;
    update();
}

void ThreadDiagram::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(palette().color(QPalette::Window).lightness()>=128 ? QColor(Qt::white) : QColor(21, 29, 40));
    p.drawRoundedRect(QRectF(rect()), 9, 9);
    p.setPen(QPen(QColor(116, 155, 184), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 7, 7);
    const bool light=palette().color(QPalette::Window).lightness()>=128;
    const QColor ink=light?QColor(64,84,102):QColor(220,234,244), accent=light?QColor(146,87,0):QColor(255,166,70), material(105,137,174,180);
    p.setFont(uiFont(8, QFont::DemiBold));
    if (phase_ == Surface) {
        p.setPen(QPen(ink, 1.3)); p.setBrush(material);
        p.drawRoundedRect(QRectF(45, 17, 190, 54), 24, 24);
        if (internal_) { p.setBrush(QColor(24, 35, 47)); p.drawRoundedRect(QRectF(83, 28, 114, 32), 15, 15); }
        p.setBrush(Qt::NoBrush); p.setPen(QPen(accent, 4.0));
        p.drawLine(internal_ ? QPointF(84, 31) : QPointF(46, 20), internal_ ? QPointF(84, 57) : QPointF(46, 68));
        p.setPen(ink); p.drawText(QRectF(12, 3, width() - 24, 18), Qt::AlignLeft, QStringLiteral("1  Seleziona la faccia %1").arg(internal_ ? QStringLiteral("del foro") : QStringLiteral("dell'albero")));
    } else if (phase_ == Profile) {
        p.setPen(QPen(ink, 1.2)); p.drawLine(30, 67, width() - 25, 67);
        QPainterPath teeth(QPointF(45, 67));
        for (int k = 0; k < 5; ++k) { teeth.lineTo(58 + 38 * k, 35); teeth.lineTo(77 + 38 * k, 67); }
        p.setPen(QPen(accent, 2.1)); p.drawPath(teeth);
        p.setPen(ink); p.drawText(QRectF(12, 3, width() - 24, 18), Qt::AlignLeft,
                                  QStringLiteral("2  Profilo %1°   passo %2").arg(angle_, 0, 'f', 0).arg(ForgeCad::formatLength(pitch_, 6, true, this)));
        p.setPen(QPen(QColor(130, 205, 255), 1.0));
        p.drawLine(77, 29, 115, 29); p.drawLine(77, 25, 77, 33); p.drawLine(115, 25, 115, 33);
        p.drawText(QRectF(77, 12, 38, 15), Qt::AlignCenter, QStringLiteral("P"));
    } else {
        p.setPen(QPen(ink, 1.2)); p.setBrush(material); p.drawRoundedRect(QRectF(45, 25, 190, 40), 18, 18);
        p.setBrush(Qt::NoBrush); p.setPen(QPen(accent, 1.8));
        QPainterPath helix(QPointF(leftHanded_ ? 220 : 60, 29));
        for (int k = 0; k < 9; ++k) {
            const qreal x = leftHanded_ ? 220 - 18 * k : 60 + 18 * k;
            helix.lineTo(x + (leftHanded_ ? -9 : 9), 61); helix.lineTo(x + (leftHanded_ ? -18 : 18), 29);
        }
        p.drawPath(helix);
        p.setPen(QPen(QColor(130, 205, 255), 1.0)); p.drawLine(55, 73, 225, 73); p.drawLine(55, 69, 55, 78); p.drawLine(225, 69, 225, 78);
        p.setPen(ink); p.drawText(QRectF(12, 3, width() - 24, 18), Qt::AlignLeft,
                                  QStringLiteral("3  Lunghezza %1   %2").arg(ForgeCad::formatLength(length_, 6, true, this), leftHanded_ ? QStringLiteral("sinistrorsa") : QStringLiteral("destrorsa")));
    }
}

bool sameCurveRef(const GeometryRef &a, const GeometryRef &b) {
    if (a.kind != b.kind || a.index != b.index || a.element != b.element) return false;
    if (a.point.subshape >= 0 || b.point.subshape >= 0) return a.point.subshape == b.point.subshape;
    return std::hypot(a.point.x - b.point.x, std::hypot(a.point.y - b.point.y, a.point.z - b.point.z)) < 1e-9;
}

bool CurvePicker::sameFace(const GeometryRef &a, const GeometryRef &b) const {
    if (a.kind != 5 || b.kind != 5 || a.index != b.index) return false;
    if (a.index < 0 || a.index >= viewport->extrusions().size() || !viewport->extrusions().at(a.index).forgeBody) return false;
    const ForgeCad::Kernel::Body &body = *viewport->extrusions().at(a.index).forgeBody;
    const double far = std::numeric_limits<double>::max();
    return ForgeCad::resolveFaceReference(body, a.point, far) == ForgeCad::resolveFaceReference(body, b.point, far);
}

QVector<GeometryRef> CurvePicker::boundaryLoop(const GeometryRef &ref) const {
    QVector<GeometryRef> refs;
    if (!wholeLoop || !wholeLoop->isChecked() || ref.kind != 4) return refs;
    if (ref.index < 0 || ref.index >= viewport->extrusions().size()) return refs;
    const ExtrusionObject &body = viewport->extrusions().at(ref.index);
    if (!body.forgeBody) return refs;
    const ForgeCad::Kernel::EdgeId edge = ForgeCad::resolveEdgeReference(*body.forgeBody, ref.point, std::numeric_limits<double>::max());
    if (!edge.valid()) return refs;
    const QVector<EdgePoint> loop = ForgeCad::freeBoundaryLoop(*body.forgeBody, edge);
    if (loop.size() < 2) return refs;
    for (const EdgePoint &point : loop) {
        GeometryRef r = ref;
        r.point = point;
        refs.append(r);
    }
    return refs;
}

QString CurvePicker::text(const QVector<GeometryRef> &refs) const {
    if (refs.isEmpty()) return QStringLiteral("nessun tratto");
    QStringList names;
    for (const GeometryRef &ref : refs) names.append(ForgeCad::geometryRefText(ref, viewport->sketches(), viewport->extrusions()));
    if (names.size() > 3) return QStringLiteral("%1 tratti (%2, ...)").arg(names.size()).arg(names.mid(0, 2).join(QStringLiteral(", ")));
    return names.join(QStringLiteral(", "));
}

void CurvePicker::start(QVector<GeometryRef> *list) {
    active = list;
    const bool faces = list && list == faceList;
    const QString error = viewport->beginReferencePick(faces ? int(ForgeCad::DatumRoleFace) : ForgeCad::DatumRoleCurve | ForgeCad::DatumRoleLine, owner);
    if (!error.isEmpty()) {
        active = nullptr;
        status(error, true);
    } else if (faces) {
        status(QStringLiteral("Clicca le facce (di nuovo per toglierle); Esc nella vista termina."), false);
    } else {
        status(QStringLiteral("Clicca gli spigoli o le entita' degli schizzi (di nuovo per toglierli); Esc nella vista termina."), false);
    }
}

void CurvePicker::stop() {
    viewport->cancelReferencePick();
    active = nullptr;
}

void CurvePicker::install() {
    viewport->setReferencePickCallback([this](bool picked, GeometryRef ref) {
        if (!picked || !active) {
            active = nullptr;
            status(QString(), false);
            changed();
            return;
        }
        if (active == faceList) {
            if (ref.kind != 5) {
                status(QStringLiteral("Scegli una faccia di un corpo."), true);
            } else {
                int existing = -1;
                for (int k = 0; k < active->size(); ++k)
                    if (sameFace(active->at(k), ref)) existing = k;
                if (existing >= 0) active->remove(existing);
                else active->append(ref);
                status(QString(), false);
            }
            QVector<GeometryRef> *list = active;
            start(list);
            changed();
            return;
        }
        std::vector<ForgeCad::Kernel::PathSegment> pieces;
        QString reason;
        const int limit = owner < 0 ? int(viewport->extrusions().size()) : owner;
        if (!ForgeCad::geometryRefPath(ref, limit, viewport->sketches(), viewport->extrusions(), pieces, &reason)) {
            status(QStringLiteral("Non e' una curva limitata: %1.").arg(reason), true);
        } else {
            const auto find = [&](const GeometryRef &r) {
                for (int k = 0; k < active->size(); ++k)
                    if (sameCurveRef(active->at(k), r)) return k;
                return -1;
            };
            const QVector<GeometryRef> loop = boundaryLoop(ref);
            if (!loop.isEmpty()) {
                // Bordo libero intero: se lo spigolo c'era gia' si toglie tutto il loop.
                const bool remove = find(ref) >= 0;
                for (const GeometryRef &r : loop) {
                    const int existing = find(r);
                    if (remove && existing >= 0) active->remove(existing);
                    else if (!remove && existing < 0) active->append(r);
                }
            } else {
                const int existing = find(ref);
                if (existing >= 0) active->remove(existing);
                else active->append(ref);
            }
            status(QString(), false);
        }
        QVector<GeometryRef> *list = active;
        start(list);  // si continua a scegliere
        changed();
    });
}

FloatingPanel::FloatingPanel(QWidget *window, const QString &title, const QString &settingsKey, const QSize &defaultSize)
        : FunctionDialogPanel(window, false), settingsKey_(settingsKey), defaultSize_(defaultSize) {
    setHeaderVisible(false);
    auto *outer = new QVBoxLayout(this);
    // Area libera per la maniglia anche con il raggio massimo degli angoli.
    outer->setContentsMargins(1, 1, 1, 36);
    outer->setSpacing(0);
    header_ = new QWidget(this);
    header_->setObjectName(QStringLiteral("floatingPanelHeader"));
    header_->setCursor(Qt::SizeAllCursor);
    header_->setMinimumHeight(36);
    auto *headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(12, 3, 8, 3);
    auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), header_);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont heading = label->font(); heading.setPixelSize(17);
    label->setFont(heading);
    headerLayout->addWidget(label, 1);
    headerLayout->addWidget(createDockButton(header_));
    auto *close = new QToolButton(header_);
    close->setText(QStringLiteral("\u2715"));
    close->setAutoRaise(true);
    close->setCursor(Qt::ArrowCursor);
    close->setToolTip(QStringLiteral("Chiudi"));
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    headerLayout->addWidget(close);
    outer->addWidget(header_);
    content_ = new QWidget(this);
    outer->addWidget(content_, 1);
    header_->installEventFilter(this);
    hide();
}

QWidget *FloatingPanel::content() const { return content_; }

void FloatingPanel::showEvent(QShowEvent *event) {
    FunctionDialogPanel::showEvent(event);
    if (!placed_) {
        placed_ = true;
        const QRect stored = QSettings().value(settingsKey_).toRect();
        if (stored.isValid()) setGeometry(stored);
        else {
            resize(defaultSize_);
            // La prima volta in alto a destra nella finestra, sotto le barre.
            move(qMax(0, parentWidget()->width() - width() - 24), 90);
        }
    }
    keepInside();
    raise();
}

void FloatingPanel::resizeEvent(QResizeEvent *event) {
    FunctionDialogPanel::resizeEvent(event);
    if (placed_ && isVisible()) store();
}

bool FloatingPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == header_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                beginEmbeddedMove(mouse->globalPosition().toPoint());
                draggingHeader_ = true;
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && draggingHeader_) {
            updateEmbeddedMove(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
            keepInside();
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && draggingHeader_) {
            endEmbeddedMove();
            draggingHeader_ = false;
            store();
            return true;
        }
    }
    return FunctionDialogPanel::eventFilter(watched, event);
}

void FloatingPanel::keepInside() {
    const QRect area = parentWidget()->rect();
    QSize size = this->size().boundedTo(area.size());
    size = size.expandedTo(minimumSizeHint().boundedTo(area.size()));
    if (size != this->size()) resize(size);
    move(qBound(0, x(), qMax(0, area.width() - width())), qBound(0, y(), qMax(0, area.height() - header_->height())));
    keepInsideViewport();
}

void FloatingPanel::store() { QSettings().setValue(settingsKey_, geometry()); }

DeleteKeyFilter::DeleteKeyFilter(QObject *parent, std::function<void()> action) : QObject(parent), action_(std::move(action)) {}

bool DeleteKeyFilter::eventFilter(QObject *watched, QEvent *event) {
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Delete) {
        event->accept();
        if (event->type() == QEvent::KeyPress) action_();
        return event->type() == QEvent::KeyPress;
    }
    return QObject::eventFilter(watched, event);
}

bool DialogMover::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        QWidget *widget = qobject_cast<QWidget *>(watched);
        FunctionDialogPanel *panel = nullptr;
        for (QWidget *candidate = widget; candidate; candidate = candidate->parentWidget()) {
            panel = dynamic_cast<FunctionDialogPanel *>(candidate);
            if (panel) break;
        }
        if (panel && panel->isEmbedded()
            && (widget == panel || qobject_cast<QLabel *>(widget))) {
            panel->beginEmbeddedMove(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
            embeddedPanel_ = panel;
            return true;
        }
        auto *dialog = qobject_cast<QDialog *>(watched);
        if (dialog && dialog->isWindow() && dialog->windowHandle() && dialog->windowHandle()->startSystemMove()) return true;
    } else if (event->type() == QEvent::MouseMove && embeddedPanel_) {
        embeddedPanel_->updateEmbeddedMove(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
        return true;
    } else if (event->type() == QEvent::MouseButtonRelease && embeddedPanel_) {
        embeddedPanel_->endEmbeddedMove();
        embeddedPanel_.clear();
        return true;
    }
    return QObject::eventFilter(watched, event);
}
