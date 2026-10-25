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
QSize FunctionMessage::sizeHint() const { return QSize(240, 72); }
QSize FunctionMessage::minimumSizeHint() const { return QSize(80, 48); }

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
    headerClose_->setFixedSize(24, 22);
    connect(headerClose_, &QToolButton::clicked, this, &QDialog::reject);
    captureTimer_.setSingleShot(true);
    connect(&captureTimer_, &QTimer::timeout, this, [this] { captureSceneBackdrop(); });
    updatePalette();
}

FunctionDialogPanel::~FunctionDialogPanel() {
    if (viewport_) viewport_->removeGlassPanel(reinterpret_cast<quintptr>(this));
}

void FunctionDialogPanel::setHeaderVisible(bool visible) {
    headerVisible_ = visible;
    setContentsMargins(0, visible ? kHeaderHeight : 0, 0, 0);
    if (headerClose_) headerClose_->setVisible(visible);
    update();
}

QFormLayout *FunctionDialogPanel::createScrollableForm() {
    if (form_) return form_;
    auto *root = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this);
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
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form_->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form_->setSizeConstraint(QLayout::SetMinAndMaxSize);
    scroll->setWidget(contents);
    root->addWidget(scroll, 1);
    return form_;
}

QColor FunctionDialogPanel::panelColor() const { return panelColor_; }

int FunctionDialogPanel::panelOpacity() const { return opacity_; }

int FunctionDialogPanel::backdropBlur() const { return blur_; }

int FunctionDialogPanel::cornerRadius() const { return cornerRadius_; }

bool FunctionDialogPanel::isEmbedded() const { return embedded_; }

void FunctionDialogPanel::beginEmbeddedMove(const QPoint &globalPosition) {
    if (!embedded_) return;
    dragOffset_ = globalPosition - mapToGlobal(QPoint());
    dragging_ = true;
    raise();
}

void FunctionDialogPanel::updateEmbeddedMove(const QPoint &globalPosition) {
    if (!dragging_ || !parentWidget()) return;
    move(parentWidget()->mapFromGlobal(globalPosition - dragOffset_));
    keepInsideViewport();
}

void FunctionDialogPanel::endEmbeddedMove() { dragging_ = false; }

void FunctionDialogPanel::placeAtLeft() {
    if (!embedded_ || !parentWidget()) return;
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

void FunctionDialogPanel::mousePressEvent(QMouseEvent *event) { raise(); event->accept(); }

void FunctionDialogPanel::mouseReleaseEvent(QMouseEvent *event) { event->accept(); }

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
    // Conferma e Annulla restano raggiungibili anche quando il form scorre.
    if (form_) for (auto *buttons : findChildren<QDialogButtonBox *>()) {
        int row = -1; QFormLayout::ItemRole role;
        form_->getWidgetPosition(buttons, &row, &role);
        if (row >= 0) {
            const auto taken = form_->takeRow(row);
            delete taken.labelItem; delete taken.fieldItem;
            static_cast<QVBoxLayout *>(layout())->addWidget(buttons);
        }
    }
    initializePanelSize();
    if (embedded_ && !placed_) placeAtLeft();
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
        if(panelColor_!=ForgeCad::uiGlassPalette(parentWidget()).color(QPalette::Window)) {
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
        headerClose_->move(width() - headerClose_->width() - 6, (kHeaderHeight - headerClose_->height()) / 2);
        headerClose_->raise();
    }
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
        painter.fillRect(QRect(0, kHeaderHeight - 2, width(), 2), QColor(255, 159, 28));
        QFont font = this->font();
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(readableText(panelColor_));
        const int right = headerClose_ && headerClose_->isVisible() ? headerClose_->x() - 6 : width() - 10;
        const QRect text(12, 0, qMax(0, right - 12), kHeaderHeight - 2);
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
                requested = requested.boundedTo(QSize(parentWidget()->width() - x(), parentWidget()->height() - y()));
            resize(requested);
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
    if (!embedded_ || !parentWidget()) return;
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
    QColor popup = panelColor_.darker(125);
    popup.setAlpha(245);
    QColor field = panelColor_.lighter(112);
    field.setAlpha(150);
    // Campi e popup hanno fondi propri: non devono ereditare il colore
    // scelto per il vetro, che puo' diventare nero sopra una scena chiara.
    const QColor fieldForeground = readableText(field);
    const QColor popupForeground = readableText(popup);
    const QString popupRgba = QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(popup.red()).arg(popup.green()).arg(popup.blue()).arg(popup.alpha());
    const QString fieldRgba = QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(field.red()).arg(field.green()).arg(field.blue()).arg(field.alpha());
    const auto sheet=QStringLiteral(
        "QDialog#functionDialogPanel, QScrollArea#functionDialogScroll, "
        "QScrollArea#functionDialogScroll > QWidget > QWidget, QWidget#functionDialogContents, "
        "QWidget#loftContents, QWidget#loftFooter, QAbstractItemView, QAbstractSpinBox, QPushButton { background: transparent; }"
        "QDialog#functionDialogPanel, QLabel, QPlainTextEdit, QCheckBox, QToolButton, QAbstractItemView, QAbstractSpinBox, QPushButton { color: %5; }"
        "QAbstractItemView::item { background: transparent; }"
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid rgba(135,170,205,150); }"
        "QComboBox { background-color: %1; color: %2; border: 1px solid rgba(135,170,205,150); padding: 2px 22px 2px 5px; }"
        "QComboBox QAbstractItemView { background-color: %3; color: %4; border: 1px solid #52708d; outline: 0; }"
        "QComboBox QAbstractItemView::item { background-color: %3; color: %4; min-height: 24px; padding: 2px 5px; }"
        "QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected { background-color: #ff9f1c; color: #101820; font-weight: 800; border: 2px solid #ffe0a3; }"
        "QSizeGrip#functionPanelResizeGrip { background: transparent; }"
        "QToolButton#functionPanelHeaderClose { color: %6; }")
        .arg(fieldRgba, fieldForeground.name(QColor::HexRgb), popupRgba, popupForeground.name(QColor::HexRgb),palette().color(QPalette::WindowText).name(QColor::HexRgb),readableText(panelColor_).name(QColor::HexRgb));
    if(styleSheet()!=sheet)setStyleSheet(sheet);
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
    preferredPanelSize_ = size();
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
    const auto ownerColor=ForgeCad::uiGlassPalette(parentWidget()).color(QPalette::Window);
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
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(group, QPalette::WindowText, foreground);
        p.setColor(group, QPalette::Text, foreground);
        p.setColor(group, QPalette::ButtonText, foreground);
        p.setColor(group, QPalette::ToolTipText, foreground);
        p.setColor(group, QPalette::HighlightedText, QColor(16, 24, 32));
        p.setColor(group, QPalette::Highlight, QColor(255, 159, 28));
        p.setColor(group, QPalette::Base, panelColor_.darker(125));
    }
    setPalette(p);
    applyTransparentStyle();
}

FeatureOperationDiagram::FeatureOperationDiagram(Kind kind, QWidget *parent ) : QWidget(parent), kind_(kind) {
    setMinimumSize(390, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FeatureOperationDiagram::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(27, 35, 45));
    const QColor profile(90, 205, 255), result(58, 164, 220, 95), arrow(255, 196, 80), text(225, 232, 240);
    p.setPen(text);
    QFont title = p.font(); title.setBold(true); title.setPixelSize(13); p.setFont(title);
    if (kind_ == Sweep) {
        p.drawText(QRectF(14, 9, width() - 28, 24), QStringLiteral("SWEEP — SEZIONE LUNGO UN PERCORSO"));
        QPainterPath path; path.moveTo(80, 125); path.cubicTo(150, 125, 150, 60, 290, 60);
        p.setPen(QPen(result, 30, Qt::SolidLine, Qt::RoundCap)); p.drawPath(path);
        p.setPen(QPen(arrow, 2.5, Qt::DashLine)); p.drawPath(path);
        p.setPen(QPen(profile, 2.5)); p.setBrush(QColor(90, 205, 255, 45));
        p.drawEllipse(QRectF(68, 108, 24, 34)); p.drawEllipse(QRectF(278, 43, 24, 34));
        p.setPen(text); QFont steps = p.font(); steps.setPixelSize(11); steps.setBold(false); p.setFont(steps);
        p.drawText(QRectF(14, 145, width() - 28, 30), Qt::AlignCenter, QStringLiteral("1 Sezione nella vista  →  2 Percorso  →  3 Orientamento e conferma"));
        return;
    }
    p.drawText(QRectF(14, 9, width() - 28, 24), kind_ == Extrusion ? QStringLiteral("ESTRUSIONE DEL PROFILO")
                                                                   : QStringLiteral("RIVOLUZIONE ATTORNO ALL'ASSE"));
    if (kind_ == Extrusion) {
        const QRectF front(75, 75, 95, 65), back(205, 45, 95, 65);
        p.setBrush(result); p.setPen(QPen(profile, 2));
        QPainterPath body; body.addRect(front); body.addRect(back);
        body.moveTo(front.topLeft()); body.lineTo(back.topLeft()); body.moveTo(front.topRight()); body.lineTo(back.topRight());
        body.moveTo(front.bottomLeft()); body.lineTo(back.bottomLeft()); body.moveTo(front.bottomRight()); body.lineTo(back.bottomRight());
        p.drawPath(body);
        p.setPen(QPen(arrow, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(165, 58), QPointF(238, 31));
        p.drawLine(QPointF(238, 31), QPointF(226, 29)); p.drawLine(QPointF(238, 31), QPointF(230, 40));
        p.setPen(text); p.drawText(QRectF(305, 73, width() - 318, 45), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("distanza\no riferimento"));
    } else {
        const qreal axisX = width() * 0.43;
        p.setPen(QPen(QColor(160, 174, 188), 2, Qt::DashLine)); p.drawLine(QPointF(axisX, 40), QPointF(axisX, 158));
        QPainterPath profilePath; profilePath.moveTo(axisX + 12, 137); profilePath.lineTo(axisX + 58, 137);
        profilePath.cubicTo(axisX + 80, 116, axisX + 75, 78, axisX + 32, 63); profilePath.lineTo(axisX + 12, 63);
        p.setBrush(QColor(90, 205, 255, 45)); p.setPen(QPen(profile, 2.5)); p.drawPath(profilePath);
        p.setBrush(Qt::NoBrush); p.setPen(QPen(arrow, 3, Qt::SolidLine, Qt::RoundCap));
        QRectF arc(axisX - 96, 47, 192, 100); p.drawArc(arc, 30 * 16, 285 * 16);
        const QPointF tip(axisX + 83, 63); p.drawLine(tip, tip + QPointF(-12, -1)); p.drawLine(tip, tip + QPointF(-6, 10));
        p.setPen(text); p.drawText(QRectF(16, 70, axisX - 115, 54), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("asse scelto\ndallo schizzo"));
    }
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
    const int body = ref.kind == 5 || (sketchStart_ >= 0 && ref.kind == 4) ? resultBodyForPick(viewport_, candidates_, ref.index) : -1;
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
    setMinimumSize(420, 230);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(QStringLiteral("1. Scegli e ordina le sezioni.  2. Aggiungi guide aperte che attraversano ogni sezione.  3. Regola la continuità alle estremità."));
}

void LoftSelectionDiagram::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(27, 35, 45));
    const QRectF area = rect().adjusted(12, 10, -12, -10);
    const QColor section(90, 205, 255), guide(255, 118, 205), text(225, 232, 240);
    const QColor secondary(145, 162, 178), tangent(255, 196, 80), card(35, 46, 58), border(66, 82, 98);
    const qreal gap = 8.0, cardWidth = (area.width() - 2.0 * gap) / 3.0;
    const QString titles[] = {QStringLiteral("SEZIONI"), QStringLiteral("CURVE GUIDA"), QStringLiteral("ESTREMITÀ")};
    const QString notes[] = {QStringLiteral("Spunta e ordina\ndall'inizio alla fine"),
                             QStringLiteral("Ogni guida aperta\nattraversa ogni sezione"),
                             QStringLiteral("G0 posizione\nG1 tangente · G2 curvatura")};
    QFont titleFont = p.font();
    titleFont.setBold(true);
    titleFont.setPixelSize(12);
    QFont noteFont = p.font();
    noteFont.setPixelSize(11);
    for (int cardIndex = 0; cardIndex < 3; ++cardIndex) {
        const QRectF cardRect(area.left() + cardIndex * (cardWidth + gap), area.top(), cardWidth, area.height());
        p.setPen(QPen(border, 1.0));
        p.setBrush(card);
        p.drawRoundedRect(cardRect, 7, 7);
        const QPointF badge(cardRect.left() + 19, cardRect.top() + 19);
        p.setPen(Qt::NoPen);
        p.setBrush(cardIndex == 0 ? section : cardIndex == 1 ? guide : tangent);
        p.drawEllipse(badge, 11, 11);
        p.setPen(QColor(20, 27, 34));
        p.setFont(titleFont);
        p.drawText(QRectF(badge.x() - 10, badge.y() - 10, 20, 20), Qt::AlignCenter, QString::number(cardIndex + 1));
        p.setPen(text);
        p.drawText(QRectF(cardRect.left() + 37, cardRect.top() + 9, cardRect.width() - 44, 22), Qt::AlignVCenter, titles[cardIndex]);
        p.setFont(noteFont);
        p.setPen(secondary);
        p.drawText(QRectF(cardRect.left() + 10, cardRect.bottom() - 48, cardRect.width() - 20, 39), Qt::AlignCenter, notes[cardIndex]);
    }

    // 1: tre profili distinti, con freccia che rende esplicito l'ordine.
    const QRectF first(area.left(), area.top(), cardWidth, area.height());
    const qreal cx1 = first.center().x() - 8;
    const qreal ys[] = {first.top() + 126, first.top() + 96, first.top() + 66};
    p.setBrush(QColor(90, 205, 255, 24));
    p.setPen(QPen(section, 2.0));
    for (int i = 0; i < 3; ++i) {
        const QRectF loop(cx1 - 38 + 5 * i, ys[i] - 10, 76 - 10 * i, 20);
        p.drawEllipse(loop);
        p.setBrush(section);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(loop.left(), loop.center().y()), 8, 8);
        p.setPen(QColor(20, 27, 34));
        p.setFont(noteFont);
        p.drawText(QRectF(loop.left() - 7, loop.center().y() - 7, 14, 14), Qt::AlignCenter, QString::number(i + 1));
        p.setBrush(QColor(90, 205, 255, 24));
        p.setPen(QPen(section, 2.0));
    }
    const qreal arrowX = first.right() - 25;
    p.drawLine(QPointF(arrowX, ys[0] + 9), QPointF(arrowX, ys[2] - 8));
    p.drawLine(QPointF(arrowX, ys[2] - 8), QPointF(arrowX - 5, ys[2] - 1));
    p.drawLine(QPointF(arrowX, ys[2] - 8), QPointF(arrowX + 5, ys[2] - 1));

    // 2: i punti magenta mostrano che una guida deve toccare tutte le sezioni.
    const QRectF second(first.right() + gap, area.top(), cardWidth, area.height());
    const qreal cx2 = second.center().x(), gy[] = {second.top() + 126, second.top() + 96, second.top() + 66};
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(section, 1.5));
    for (int i = 0; i < 3; ++i) p.drawEllipse(QRectF(cx2 - 38 + 5 * i, gy[i] - 9, 76 - 10 * i, 18));
    QPainterPath guidePath;
    guidePath.moveTo(cx2 - 25, gy[0]);
    guidePath.cubicTo(cx2 + 31, gy[0] - 15, cx2 - 31, gy[2] + 15, cx2 + 20, gy[2]);
    p.setPen(QPen(guide, 2.7, Qt::SolidLine, Qt::RoundCap));
    p.drawPath(guidePath);
    p.setPen(Qt::NoPen);
    p.setBrush(guide);
    for (int i = 0; i < 3; ++i) {
        // Intersezioni volutamente allineate sulla guida, non punti decorativi sui profili.
        const qreal t = i / 2.0;
        const QPointF hit = guidePath.pointAtPercent(t);
        p.drawEllipse(hit, 4.5, 4.5);
    }

    // 3: stessa estremita' con i tre livelli, indicati da frecce sempre piu' vincolanti.
    const QRectF third(second.right() + gap, area.top(), cardWidth, area.height());
    const QPointF end(third.center().x() - 12, third.top() + 105);
    QPainterPath profile;
    profile.moveTo(end.x() - 50, end.y() + 13);
    profile.cubicTo(end.x() - 22, end.y() + 4, end.x() - 10, end.y(), end.x(), end.y());
    p.setPen(QPen(section, 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawPath(profile);
    p.setPen(QPen(tangent, 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(end, end + QPointF(45, -28));
    p.drawLine(end + QPointF(45, -28), end + QPointF(37, -27));
    p.drawLine(end + QPointF(45, -28), end + QPointF(42, -20));
    p.setBrush(tangent);
    p.setPen(Qt::NoPen);
    p.drawEllipse(end, 5, 5);
    p.setFont(titleFont);
    p.setPen(tangent);
    p.drawText(QRectF(third.left() + 10, third.top() + 50, third.width() - 20, 18), Qt::AlignCenter,
               QStringLiteral("G0  →  G1  →  G2"));
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
    p.fillRect(rect(), QColor(24, 35, 47, 175));
    p.setPen(QPen(QColor(116, 155, 184), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 7, 7);
    const QColor ink(220, 234, 244), accent(255, 166, 70), material(105, 137, 174, 180);
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
    auto *headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(10, 4, 4, 4);
    auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), header_);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(label, 1);
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
