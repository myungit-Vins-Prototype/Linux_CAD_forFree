#include "cad_drawing_browser.h"
#include "cad_drawing_window.h"
#include "cad_toolbar.h"
#include "cad_curve_extract.h"

#include "kernel/fk_cancel.h"
#include "cad_spatial_sketch.h"
#include <QTableWidget>
#include <QHeaderView>
#include "forgeCad2026_gui.h"
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
#include <QOpenGLContext>
#include <QQuaternion>
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
#include <QHelpEvent>
#include <QToolTip>
#include "cad_icons.h"
#include "cad_kernel.h"
#include "cad_sketch_edit.h"
#include "cad_sketch_offset.h"
#include "cad_snap.h"
#include "fk_parallel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QThread>
#include <QPointer>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QMap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QTextBrowser>
#include <QTextEdit>
#include <QFrame>
#include <QSizeGrip>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QShortcut>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QSlider>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QMenu>
#include "cad_menu.h"
#include <QMenuBar>
#include <QMessageBox>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QLinearGradient>
#include <QPainterPath>
#include <QPushButton>
#include <QProgressBar>
#include <QProgressDialog>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>
#include "toolbar_editor.h"
#include <QToolButton>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QStyleOptionSlider>
#include <QStyleOptionButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QThreadPool>
#include <QRunnable>
#include <QWheelEvent>
#include <QWindow>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QStandardItemModel>
#include <algorithm>
#include <cmath>
#include <exception>
#include <thread>
#include <cstdio>
#include <limits>
#include <functional>
#include <memory>
#include <optional>
#include <qnamespace.h>
#include <utility>

using ForgeCad::Gui::uiFont;
using ForgeCad::Gui::pointLength;
using ForgeCad::Gui::pointDistance;
using ForgeCad::Gui::kSelectionColor;
using ForgeCad::Gui::kHoverColor;
using ForgeCad::Gui::featureIconName;
using ForgeCad::Gui::wrappedNote;
using ForgeCad::Gui::runUntilApplied;

// I report dei file grandi possono contenere migliaia di righe: scorrono
// nell'area di testo senza aumentare la dimensione della finestra.
static void showImportReport(QWidget *parent, const QString &message) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Importa STEP/IGES"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QTextBrowser(&dialog);
    text->setPlainText(message);
    text->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout->addWidget(text, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    QSize size(720, 420);
    if (const QScreen *screen = dialog.screen()) size = size.boundedTo(screen->availableGeometry().size() * 0.85);
    dialog.setFixedSize(size);
    dialog.exec();
}



// Tipi delle voci dell'albero modello (Qt::UserRole); Qt::UserRole + 1 e' l'indice.
enum TreeItemType { kTreeInfo = -1, kTreeOrigin = 0, kTreePlane = 1, kTreeSketch = 3, kTreeExtrusion = 4, kTreeBody = 5 };

// Numero (badge) accanto al nome delle voci di gruppo dell'albero: il testo in
// kTreeBadgeRole, disegnato come una pillola dopo il nome.
constexpr int kTreeBadgeRole = Qt::UserRole + 3;

class TreeBadgeDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        QStyleOptionViewItem item = option;
        initStyleOption(&item, index);
        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
#ifdef Q_OS_MACOS
        // Il percorso nativo delle righe puo' omettere l'indicatore pur
        // mantenendo la sua area cliccabile. Disegna il normale checkbox
        // macOS nella geometria usata anche dal delegate per l'interazione.
        if (item.features.testFlag(QStyleOptionViewItem::HasCheckIndicator)) {
            QStyleOptionButton check;
            if (widget) check.initFrom(widget);
            check.rect = style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &item, widget);
            check.state = item.state & ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange);
            check.state |= item.checkState == Qt::Checked ? QStyle::State_On
                : item.checkState == Qt::PartiallyChecked ? QStyle::State_NoChange : QStyle::State_Off;
            check.palette = item.palette;
            painter->save();
            // Il controllo Cocoa viene dipinto rispetto all'origine del painter.
            painter->translate(check.rect.topLeft());
            check.rect.moveTopLeft(QPoint(0, 0));
            style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &check, painter, widget);
            painter->restore();
        }
#endif
        const QString badge = index.data(kTreeBadgeRole).toString();
        if (badge.isEmpty()) return;
        const QRect text = style->subElementRect(QStyle::SE_ItemViewItemText, &item, widget);
        const int textWidth = item.fontMetrics.horizontalAdvance(item.text);
        QFont font = item.font;
        font.setBold(true);
        font.setPointSizeF(std::max(6.0, font.pointSizeF() * 0.85));
        const QFontMetrics metrics(font);
        const int height = metrics.height() + 2, width = std::max(height, metrics.horizontalAdvance(badge) + 10);
        const QRect pill(text.left() + std::min(textWidth, text.width()) + 8, text.center().y() - height / 2 + 1, width, height);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(232, 137, 45));
        painter->drawRoundedRect(pill, height / 2.0, height / 2.0);
        painter->setPen(QColor(25, 25, 25));
        painter->setFont(font);
        painter->drawText(pill, Qt::AlignCenter, badge);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        const QString badge = index.data(kTreeBadgeRole).toString();
        if (!badge.isEmpty()) size.rwidth() += option.fontMetrics.horizontalAdvance(badge) + 24;
        return size;
    }
};

const QColor kStoryboardPositionColor(70, 205, 255);

// Use the live palette when painting the handle: stylesheet palette colors
// can remain cached after the application switches its appearance.
class StoryboardPositionSlider final : public QSlider {
public:
    using QSlider::QSlider;
    QVector<QPair<int, int>> checkpoints;
    int activePosition = 0;
    std::function<void(int)> checkpointSelected;
    QRect handleRect() const {
        QStyleOptionSlider option;
        initStyleOption(&option);
        return style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
    }
protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && std::abs(event->position().x() - width() / 2) <= 10)
            for (const auto &checkpoint : checkpoints)
                if (std::abs(event->position().y() - checkpoint.first) <= 6) {
                    event->accept();
                    if (checkpointSelected) checkpointSelected(checkpoint.second);
                    return;
                }
        QSlider::mousePressEvent(event);
    }
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        QPainter painter(this);
        const QWidget *owner = parentWidget();
        if (owner && !qobject_cast<const QAbstractScrollArea *>(owner)) owner = owner->parentWidget();
        const auto *tree = qobject_cast<const QAbstractScrollArea *>(owner);
        const QBrush background = tree ? tree->viewport()->palette().brush(QPalette::Base)
                                      : palette().brush(QPalette::Base);
        painter.fillRect(rect(), background);
        painter.setRenderHint(QPainter::Antialiasing);
        // Guida azzurra con bordo sfumato; piccoli intervalli isolano i checkpoint.
        const double centerX = width() / 2.0;
        QLinearGradient edge(centerX - 4, 0, centerX + 4, 0);
        for (const auto &stop : {qMakePair(0.0, 0), qMakePair(0.2, 30), qMakePair(0.4, 160),
                                 qMakePair(0.5, 255), qMakePair(0.6, 160), qMakePair(0.8, 30), qMakePair(1.0, 0)}) {
            QColor color = kStoryboardPositionColor;
            color.setAlpha(stop.second);
            edge.setColorAt(stop.first, color);
        }
        const auto paintSegment = [&](double begin, double end) {
            if (end <= begin) return;
            painter.fillRect(QRectF(centerX - 4, begin, 8, end - begin), edge);
            painter.setPen(QPen(kStoryboardPositionColor, 1.5, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(centerX, begin), QPointF(centerX, end));
        };
        const auto drawSegment = [&](double begin, double end) {
            // La guida resta appena staccata anche dalla maniglia bombata.
            paintSegment(begin, qMin(end, handle.top() - 3.0));
            paintSegment(qMax(begin, handle.bottom() + 3.0), end);
        };
        if (checkpoints.isEmpty()) {
            drawSegment(6, qMax(6, height() - 6));
        } else {
            for (int index = 1; index < checkpoints.size(); ++index)
                drawSegment(checkpoints.at(index - 1).first + 6.0, checkpoints.at(index).first - 6.0);
            if (handle.center().y() < checkpoints.first().first)
                drawSegment(handle.bottom() + 3.0, checkpoints.first().first - 6.0);
            if (handle.center().y() > checkpoints.last().first)
                drawSegment(checkpoints.last().first + 6.0, handle.top() - 3.0);
        }
        for (const auto &checkpoint : checkpoints) {
            const QColor color = kStoryboardPositionColor;
            painter.setPen(QPen(color, 1));
            painter.setBrush(checkpoint.second <= activePosition ? QBrush(color) : background);
            painter.drawEllipse(QPointF(width() / 2.0, checkpoint.first), 2.5, 2.5);
        }
        const QRectF cap = QRectF(handle).adjusted(1, 1, -1, -1);
        const QColor base = palette().color(QPalette::Highlight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 65));
        painter.drawRoundedRect(cap.translated(0, 1.5), 4, 4);
        QLinearGradient dome(cap.topLeft(), cap.bottomLeft());
        dome.setColorAt(0.0, base.lighter(145));
        dome.setColorAt(0.25, base.lighter(120));
        dome.setColorAt(0.5, base);
        dome.setColorAt(1.0, base.darker(165));
        painter.setBrush(dome);
        painter.setPen(QPen(hasFocus() ? palette().color(QPalette::HighlightedText) : base.darker(165), 1));
        painter.drawRoundedRect(cap, 4, 4);
        painter.setPen(QPen(QColor(255, 255, 255, 150), 1, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(cap.topLeft() + QPointF(4, 1.5), cap.topRight() + QPointF(-4, 1.5));
        for (int offset : {-2, 2}) {
            const QPointF start = QPointF(handle.center()) + QPointF(-4, offset);
            const QPointF end = start + QPointF(8, 0);
            painter.setPen(QPen(base.darker(190), 1, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(start, end);
            painter.setPen(QPen(QColor(255, 255, 255, 160), 1, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(start + QPointF(0, 1), end + QPointF(0, 1));
        }
    }
};

class StoryboardTree final : public QTreeWidget {
public:
    explicit StoryboardTree(QWidget *parent = nullptr) : QTreeWidget(parent) {
        // Anche i margini laterali seguono il colore di fondo delle righe.
        setBackgroundRole(QPalette::Base);
        setAutoFillBackground(true);
        setDragEnabled(true);
        setAcceptDrops(true);
        // L'indicatore Qt standard tende a evidenziare l'intera voce. Qui la
        // destinazione e' una posizione nella storia, quindi disegniamo una
        // riga esplicita fra le due feature.
        setDropIndicatorShown(false);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
        // Ctrl/Maiusc+clic: piu' feature e schizzi per le operazioni comuni (Elimina, Sopprimi...).
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        historyRailViewport_ = new QWidget(this);
        historyRailViewport_->setObjectName(QStringLiteral("storyboardRailViewport"));
        historyRailViewport_->hide();
        historyRail_ = new StoryboardPositionSlider(Qt::Vertical, historyRailViewport_);
        static_cast<StoryboardPositionSlider *>(historyRail_)->checkpointSelected = [this](int position) {
            if (historyPositionChanged_) historyPositionChanged_(position);
        };
        historyRail_->setObjectName(QStringLiteral("storyboardVerticalRail"));
        historyRail_->setStyleSheet(QStringLiteral(
            "QSlider::groove:vertical { background: transparent; width: 4px; border-radius: 2px; }"
            "QSlider::handle:vertical { background: palette(highlight); border: 1px solid palette(light);"
            " height: 12px; margin: 0 -6px; border-radius: 3px; }"
            "QSlider::handle:vertical:hover { border: 2px solid palette(highlighted-text); }"
            "QSlider::handle:vertical:focus { border: 2px solid palette(highlighted-text); }"));
        historyRail_->setInvertedAppearance(true);
        historyRail_->setInvertedControls(true);
        historyRail_->setToolTip(QStringLiteral("Trascina la maniglia per scegliere la posizione di calcolo e inserimento"));
        historyRail_->hide();
        connect(historyRail_, &QSlider::sliderPressed, this, [this] { pendingHistoryPosition_ = historyPosition_; });
        connect(historyRail_, &QSlider::sliderMoved, this, [this](int value) {
            pendingHistoryPosition_ = historyPositionAtY(railTop_ + value);
            setInsertionLine(railTop_ + value, 22);
        });
        connect(historyRail_, &QSlider::sliderReleased, this, [this] {
            clearInsertionLine();
            if (historyPositionChanged_) historyPositionChanged_(pendingHistoryPosition_);
        });
        connect(historyRail_, &QSlider::actionTriggered, this, [this](int action) {
            if (historyRail_->isSliderDown() || updatingRail_ || !historyPositionChanged_) return;
            int position = historyPosition_;
            if (action == QAbstractSlider::SliderToMinimum) position = 0;
            else if (action == QAbstractSlider::SliderToMaximum) position = historyCount_;
            else if (action == QAbstractSlider::SliderSingleStepAdd || action == QAbstractSlider::SliderPageStepAdd) ++position;
            else if (action == QAbstractSlider::SliderSingleStepSub || action == QAbstractSlider::SliderPageStepSub) --position;
            else position = historyPositionAtY(railTop_ + historyRail_->sliderPosition());
            historyPositionChanged_(qBound(0, position, historyCount_));
        });
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateHistoryRail(); });
    }
    void setMoveFeatureCallback(std::function<QString(int, int)> callback) { moveFeature_ = std::move(callback); }
    void setHistoryPositionCallback(std::function<void(int)> callback) { historyPositionChanged_ = std::move(callback); }
    enum class CursorMode { Horizontal, Left, Right };
    CursorMode cursorMode() const { return cursorMode_; }
    void setCursorMode(CursorMode mode) {
        cursorMode_ = mode;
        const QMargins margins = viewportMargins();
        setViewportMargins(mode == CursorMode::Left ? 30 : 0, margins.top(),
                           mode == CursorMode::Right ? 30 : 0, margins.bottom());
        updateHistoryRail();
        viewport()->update();
    }
    QSlider *historyRail() const { return historyRail_; }
    int historyPosition() const { return historyPosition_; }
    void setHistoryRange(int position, int count) {
        historyPosition_ = position; historyCount_ = count;
        updateHistoryRail();
        viewport()->update();
    }
    static bool isHistoryCursor(QTreeWidgetItem *item) {
        return item && item->data(0, Qt::UserRole + 2).toString() == QStringLiteral("historyCursor");
    }


protected:
    void updateGeometries() override {
        QTreeWidget::updateGeometries();
        const QMargins margins = viewportMargins();
        const int left = cursorMode_ == CursorMode::Left ? 30 : 0;
        const int right = cursorMode_ == CursorMode::Right ? 30 : 0;
        if (margins.left() != left || margins.right() != right)
            setViewportMargins(left, margins.top(), right, margins.bottom());
        if (historyRail_) updateHistoryRail();
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (cursorMode_ == CursorMode::Horizontal && event->button() == Qt::LeftButton
            && isHistoryCursor(itemAt(event->position().toPoint()))) {
            movingHistoryCursor_ = true;
            pendingHistoryPosition_ = historyPosition_;
            setCurrentItem(itemAt(event->position().toPoint()));
            event->accept();
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (movingHistoryCursor_) {
            // Snap to gaps between direct feature rows, including their details.
            int position = historyCount_;
            int lineY = -1;
            QTreeWidgetItem *history = nullptr;
            for (int i = 0; i < topLevelItemCount(); ++i)
                if (topLevelItem(i)->data(0, Qt::UserRole + 2).toString() == QStringLiteral("H")) history = topLevelItem(i);
            if (history) {
                for (int i = 0; i < history->childCount(); ++i) {
                    auto *item = history->child(i);
                    if (item->data(0, Qt::UserRole).toInt() != kTreeExtrusion) continue;
                    const QRect rect = visualItemRect(item);
                    if (!rect.isEmpty() && event->position().y() <= rect.center().y()) {
                        position = item->data(0, Qt::UserRole + 1).toInt();
                        lineY = rect.top();
                        break;
                    }
                }
                if (lineY < 0 && history->childCount()) {
                    auto *last = history->child(history->childCount() - 1);
                    while (last->isExpanded() && last->childCount()) last = last->child(last->childCount() - 1);
                    lineY = visualItemRect(last).bottom() + 1;
                }
            }
            pendingHistoryPosition_ = position;
            setInsertionLine(lineY, 22);
            if (event->position().y() < 20) verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
            if (event->position().y() > viewport()->height() - 20) verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
            event->accept();
            return;
        }
        QTreeWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (movingHistoryCursor_ && event->button() == Qt::LeftButton) {
            movingHistoryCursor_ = false;
            clearInsertionLine();
            const int position = pendingHistoryPosition_;
            event->accept();
            // The callback rebuilds the tree. Do not retain or use row pointers.
            if (historyPositionChanged_) historyPositionChanged_(position);
            return;
        }
        QTreeWidget::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (isHistoryCursor(currentItem()) && historyPositionChanged_) {
            int position = historyPosition_;
            if (event->key() == Qt::Key_Home) position = 0;
            else if (event->key() == Qt::Key_End) position = historyCount_;
            else if (event->key() == Qt::Key_Up) --position;
            else if (event->key() == Qt::Key_Down) ++position;
            else { QTreeWidget::keyPressEvent(event); return; }
            event->accept();
            historyPositionChanged_(qBound(0, position, historyCount_));
            return;
        }
        QTreeWidget::keyPressEvent(event);
    }
    void startDrag(Qt::DropActions actions) override {
        draggedItem_ = currentItem();
        if (!isMovableFeature(draggedItem_)) {
            draggedItem_ = nullptr;
            return;
        }
        QTreeWidget::startDrag(actions);
        draggedItem_ = nullptr;
        clearInsertionLine();
    }

    void dragEnterEvent(QDragEnterEvent *event) override {
        if (isMovableFeature(draggedItem_)) event->acceptProposedAction();
        else event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override {
        QTreeWidgetItem *target = targetFeature(event->position().toPoint());
        if (target && target != draggedItem_) {
            const int sourceIndex = draggedItem_->data(0, Qt::UserRole + 1).toInt();
            const int targetIndex = target->data(0, Qt::UserRole + 1).toInt();
            const QRect targetRect = visualItemRect(target);
            setInsertionLine(sourceIndex < targetIndex ? targetRect.bottom() + 1 : targetRect.top(), targetRect.left());
            event->acceptProposedAction();
        } else {
            clearInsertionLine();
            event->ignore();
        }
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override {
        clearInsertionLine();
        QTreeWidget::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override {
        QTreeWidgetItem *source = draggedItem_;
        QTreeWidgetItem *target = targetFeature(event->position().toPoint());
        clearInsertionLine();
        if (!isMovableFeature(source) || !target || source == target || !moveFeature_) {
            event->ignore();
            return;
        }
        const QString error = moveFeature_(source->data(0, Qt::UserRole + 1).toInt(), target->data(0, Qt::UserRole + 1).toInt());
        event->ignore();  // l'albero viene ricostruito dal documento, mai spostato solo graficamente
        if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Riordina storyboard"), error);
    }

    void paintEvent(QPaintEvent *event) override {
        updateHistoryRail();
        QTreeWidget::paintEvent(event);
        if (insertionLineY_ < 0) return;
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QColor color = kStoryboardPositionColor;
        painter.setPen(QPen(color, 2.0));
        const int right = viewport()->width() - 5;
        painter.drawLine(insertionLineLeft_, insertionLineY_, right, insertionLineY_);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(QPolygonF({QPointF(insertionLineLeft_, insertionLineY_ - 4),
                                       QPointF(insertionLineLeft_ + 6, insertionLineY_),
                                       QPointF(insertionLineLeft_, insertionLineY_ + 4)}));
    }

private:
    QTreeWidgetItem *historyRootItem() const {
        for (int i = 0; i < topLevelItemCount(); ++i)
            if (topLevelItem(i)->data(0, Qt::UserRole + 2).toString() == QStringLiteral("H")) return topLevelItem(i);
        return nullptr;
    }
    int historyPositionAtY(int y) const {
        auto *root = historyRootItem();
        if (!root) return historyPosition_;
        bool first = true;
        for (int row = 0; row < root->childCount(); ++row) {
            auto *item = root->child(row);
            if (item->data(0, Qt::UserRole).toInt() != kTreeExtrusion) continue;
            const QRect rect = visualItemRect(item);
            if (!rect.isEmpty() && y <= rect.center().y())
                return first ? 0 : item->data(0, Qt::UserRole + 1).toInt();
            first = false;
        }
        return historyCount_;
    }
    void updateHistoryRail() {
        auto *root = historyRootItem();
        if (cursorMode_ == CursorMode::Horizontal || !root || !root->isExpanded() || !root->childCount()) {
            historyRail_->hide();
            historyRailViewport_->hide();
            return;
        }
        auto *last = root->child(root->childCount() - 1);
        while (last->isExpanded() && last->childCount()) last = last->child(last->childCount() - 1);
        // Coordinate dell'intera storia: scorrono con le righe, senza fissare
        // la maniglia ai bordi quando la sua posizione e' fuori vista.
        const int top = visualItemRect(root->child(0)).top();
        const int bottom = visualItemRect(last).bottom();
        if (bottom <= top) { historyRail_->hide(); historyRailViewport_->hide(); return; }
        int cursorY = top;
        for (int row = 0; row < root->childCount(); ++row)
            if (isHistoryCursor(root->child(row))) cursorY = visualItemRect(root->child(row)).center().y();
        railTop_ = top;
        updatingRail_ = true;
        const QSignalBlocker blocker(historyRail_);
        auto *slider = static_cast<StoryboardPositionSlider *>(historyRail_);
        const int handleHeight = slider->handleRect().height();
        const int handleOffset = (handleHeight - 1) / 2;
        historyRailViewport_->setGeometry(cursorMode_ == CursorMode::Left ? 2 : viewport()->geometry().right() + 3,
                                          viewport()->geometry().top(), 26, viewport()->height());
        historyRail_->setGeometry(0, top - handleOffset, 26, bottom - top + handleHeight);
        slider->checkpoints.clear();
        slider->activePosition = historyPosition_;
        for (int row = 0; row < root->childCount(); ++row) {
            auto *item = root->child(row);
            if (item->data(0, Qt::UserRole).toInt() != kTreeExtrusion) continue;
            const QRect rect = visualItemRect(item);
            if (!rect.isEmpty() && rect.center().y() >= top && rect.center().y() <= bottom)
                slider->checkpoints.append({rect.center().y() - top + handleOffset, item->data(0, Qt::UserRole + 1).toInt() + 1});
        }
        historyRail_->setRange(0, bottom - top);
        if (!historyRail_->isSliderDown()) historyRail_->setValue(cursorY - top);
        historyRail_->show();
        historyRailViewport_->show();
        historyRailViewport_->raise();
        updatingRail_ = false;
    }
    void setInsertionLine(int y, int left) {
        if (insertionLineY_ == y && insertionLineLeft_ == left) return;
        insertionLineY_ = y;
        insertionLineLeft_ = qMax(2, left - 8);
        viewport()->update();
    }

    void clearInsertionLine() {
        if (insertionLineY_ < 0) return;
        insertionLineY_ = -1;
        viewport()->update();
    }

    static bool isMovableFeature(QTreeWidgetItem *item) {
        return item && item->data(0, Qt::UserRole).toInt() == kTreeExtrusion && item->parent()
            && item->parent()->data(0, Qt::UserRole + 2).toString() == QStringLiteral("H");
    }

    QTreeWidgetItem *targetFeature(const QPoint &position) const {
        if (!isMovableFeature(draggedItem_)) return nullptr;
        QTreeWidgetItem *body = draggedItem_->parent();
        QTreeWidgetItem *under = itemAt(position);
        for (QTreeWidgetItem *candidate = under; candidate && candidate != body; candidate = candidate->parent())
            if (candidate->parent() == body && candidate->data(0, Qt::UserRole).toInt() == kTreeExtrusion)
                return normalizedTarget(body, candidate);

        // Schizzi e dettagli possono stare tra due feature. In quel caso il
        // rilascio vale per la feature diretta piu' vicina nello stesso corpo.
        QTreeWidgetItem *best = nullptr;
        int distance = std::numeric_limits<int>::max();
        for (int row = 0; row < body->childCount(); ++row) {
            QTreeWidgetItem *candidate = body->child(row);
            if (candidate->data(0, Qt::UserRole).toInt() != kTreeExtrusion) continue;
            const QRect rect = visualItemRect(candidate);
            const int d = std::abs(rect.center().y() - position.y());
            if (d < distance) distance = d, best = candidate;
        }
        return normalizedTarget(body, best);
    }

    QTreeWidgetItem *normalizedTarget(QTreeWidgetItem *body, QTreeWidgetItem *target) const {
        if (!body || !target || target != firstFeature(body)) return target;
        // Il primo stadio resta fisso. Un rilascio su di esso significa
        // "porta subito dopo la radice".
        bool foundRoot = false;
        for (int row = 0; row < body->childCount(); ++row) {
            QTreeWidgetItem *candidate = body->child(row);
            if (candidate->data(0, Qt::UserRole).toInt() != kTreeExtrusion) continue;
            if (foundRoot) return candidate;
            foundRoot = true;
        }
        return target;
    }

    static QTreeWidgetItem *firstFeature(QTreeWidgetItem *body) {
        if (!body) return nullptr;
        for (int row = 0; row < body->childCount(); ++row)
            if (body->child(row)->data(0, Qt::UserRole).toInt() == kTreeExtrusion) return body->child(row);
        return nullptr;
    }

    QWidget *historyRailViewport_ = nullptr;
    QSlider *historyRail_ = nullptr;
    CursorMode cursorMode_ = CursorMode::Horizontal;
    int railTop_ = 0;
    bool updatingRail_ = false;
    std::function<void(int)> historyPositionChanged_;
    bool movingHistoryCursor_ = false;
    int pendingHistoryPosition_ = 0;
    int historyPosition_ = 0;
    int historyCount_ = 0;
    std::function<QString(int, int)> moveFeature_;
    QTreeWidgetItem *draggedItem_ = nullptr;
    int insertionLineY_ = -1;
    int insertionLineLeft_ = 2;
};

static const QStringList &planeNames() {
    static const QStringList names = {QStringLiteral("Piano XY - Superiore"),
                                      QStringLiteral("Piano XZ - Frontale"),
                                      QStringLiteral("Piano YZ - Destro")};
    return names;
}

// Anteprima del documento selezionato nella finestra Apri. Il file viene
// decodificato su QThreadPool; soltanto l'ultimo risultato selezionato arriva
// al piccolo viewport, senza toccare il documento della finestra principale.
class DocumentFilePreview final : public QFrame {
public:
    explicit DocumentFilePreview(QWidget *parent = nullptr) : QFrame(parent), timer_(this) {
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

    void setPath(const QString &path) {
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

protected:
    void resizeEvent(QResizeEvent *event) override {
        QFrame::resizeEvent(event);
        if (message_) message_->setGeometry(viewport_->rect());
    }

private:
    struct Loaded {
        DocumentState state;
        QString error;
        QString details;
        int cachedBodies = 0;
        bool hasSketches = false;
    };
    void clearPreview(const QString &message) {
        DocumentState empty;
        viewport_->loadPreviewDocument(std::move(empty));
        message_->setText(message);
        message_->setGeometry(viewport_->rect());
        message_->show();
        progress_->hide();
        details_->clear();
    }
    void startLoading() {
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

    CadViewport *viewport_ = nullptr;
    QLabel *message_ = nullptr;
    QLabel *details_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QTimer timer_;
    QString currentPath_;
    quint64 generation_ = 0;
};

// Aspetto comune delle finestre delle funzioni. Il fondale arriva direttamente
// dal framebuffer del viewport CAD, quindi non dipende da cio' che Wayland
// considera dietro la finestra e segue ogni nuovo frame della scena.
struct WindowLock {
    QList<QPointer<QWidget>> widgets;
    QList<QPointer<QAction>> actions;
    CadViewport *viewport = nullptr;
    bool previousLock = false;
    WindowLock(QMainWindow *window, CadViewport *target, QWidget *keep) : viewport(target), previousLock(target->interactionLocked()) {
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
    ~WindowLock() {
        for (const QPointer<QWidget> &widget : widgets)
            if (widget) widget->setEnabled(true);
        for (const QPointer<QAction> &action : actions)
            if (action) action->setEnabled(true);
        viewport->setInteractionLocked(previousLock);
    }
};

class FunctionDialogPanel : public QDialog {
public:
    explicit FunctionDialogPanel(QWidget *parent = nullptr, bool exclusive = true)
        : QDialog(overlayParent(parent)), exclusive_(exclusive), panelColor_(palette().color(QPalette::Window)), captureTimer_(this) {
        setAttribute(Qt::WA_NoMousePropagation);
        embedded_ = dynamic_cast<CadViewport *>(parentWidget()) != nullptr;
        if (embedded_) {
            setWindowFlags(Qt::Widget);
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
        resizeGrip_ = new QSizeGrip(this);
        resizeGrip_->setObjectName(QStringLiteral("functionPanelResizeGrip"));
        resizeGrip_->setFixedSize(22, 22);
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
    ~FunctionDialogPanel() override {
        if (viewport_) viewport_->removeGlassPanel(reinterpret_cast<quintptr>(this));
    }
    // FloatingPanel ha una barra del titolo propria.
    void setHeaderVisible(bool visible) {
        headerVisible_ = visible;
        setContentsMargins(0, visible ? kHeaderHeight : 0, 0, 0);
        if (headerClose_) headerClose_->setVisible(visible);
        update();
    }
    QFormLayout *createScrollableForm() {
        if (form_) return form_;
        auto *root = new QVBoxLayout(this);
        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("functionDialogScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    QColor panelColor() const { return panelColor_; }
    int panelOpacity() const { return opacity_; }
    int backdropBlur() const { return blur_; }
    int cornerRadius() const { return cornerRadius_; }
    bool isEmbedded() const { return embedded_; }
    void beginEmbeddedMove(const QPoint &globalPosition) {
        if (!embedded_) return;
        dragOffset_ = globalPosition - mapToGlobal(QPoint());
        dragging_ = true;
        raise();
    }
    void updateEmbeddedMove(const QPoint &globalPosition) {
        if (!dragging_ || !parentWidget()) return;
        move(parentWidget()->mapFromGlobal(globalPosition - dragOffset_));
        keepInsideViewport();
    }
    void endEmbeddedMove() { dragging_ = false; }
    void placeAtLeft() {
        if (!embedded_ || !parentWidget()) return;
        resize(size().boundedTo(parentWidget()->size() - QSize(32, 32)));
        move(16, qMax(16, (parentWidget()->height() - height()) / 2));
        keepInsideViewport();
        placed_ = true;
    }
    void setPanelOpacity(int percent) {
        opacity_ = qBound(25, percent, 100);
        updatePalette();
        syncGpuGlass();
        scheduleBackdropCapture(0);
        update();
    }
    void setBackdropBlur(int pixels) {
        blur_ = qBound(0, pixels, 40);
        syncGpuGlass();
        scheduleBackdropCapture(0);
        update();
    }
    void setCornerRadius(int pixels) {
        cornerRadius_ = qBound(0, pixels, 40);
        syncGpuGlass();
        updateRoundedMask();
        update();
    }
    void reloadAppearance() {
        QSettings settings;
        setPanelOpacity(settings.value(QStringLiteral("view/functionPanelOpacity"), 88).toInt());
        setBackdropBlur(settings.value(QStringLiteral("view/functionPanelBlur"), 12).toInt());
        setCornerRadius(settings.value(QStringLiteral("view/functionPanelCornerRadius"), 10).toInt());
    }
protected:
    void mousePressEvent(QMouseEvent *event) override { raise(); event->accept(); }
    void mouseReleaseEvent(QMouseEvent *event) override { event->accept(); }
    void mouseDoubleClickEvent(QMouseEvent *event) override { event->accept(); }
    void wheelEvent(QWheelEvent *event) override { event->accept(); }
    void contextMenuEvent(QContextMenuEvent *event) override { event->accept(); }
    void showEvent(QShowEvent *event) override {
        QDialog::showEvent(event);
        locateViewport();
        if (exclusive_ && viewport_ && !windowLock_)
            if (auto *window = qobject_cast<QMainWindow *>(viewport_->window()))
                windowLock_ = std::make_unique<WindowLock>(window, viewport_, this);
        initializePanelSize();
        if (embedded_ && !placed_) placeAtLeft();
        raise();
        syncGpuGlass();
        scheduleBackdropCapture(0);
        QTimer::singleShot(0, this, [this] { focusFirstEditor(); });
    }
    void hideEvent(QHideEvent *event) override {
        if (viewport_) viewport_->removeGlassPanel(reinterpret_cast<quintptr>(this));
        windowLock_.reset();
        QDialog::hideEvent(event);
    }
    void changeEvent(QEvent *event) override {
        QDialog::changeEvent(event);
        if (event->type() == QEvent::WindowTitleChange) update(0, 0, width(), kHeaderHeight);
    }
    void moveEvent(QMoveEvent *event) override {
        QDialog::moveEvent(event);
        syncGpuGlass();
        scheduleBackdropCapture(0);
    }
    void resizeEvent(QResizeEvent *event) override {
        QDialog::resizeEvent(event);
        if (embedded_ && placed_) keepInsideViewport();
        if (resizeGrip_) {
            resizeGrip_->move(width() - resizeGrip_->width(), height() - resizeGrip_->height());
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
    void paintEvent(QPaintEvent *event) override {
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
            // Intestazione: fondo scuro fisso (leggibile su qualsiasi scena), nome del comando, filo arancio.
            const QRect header(0, 0, width(), kHeaderHeight);
            painter.fillRect(header, QColor(24, 31, 40, 235));
            painter.fillRect(QRect(0, kHeaderHeight - 2, width(), 2), QColor(255, 159, 28));
            QFont font = this->font();
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(QColor(242, 246, 250));
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
    bool eventFilter(QObject *watched, QEvent *event) override {
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
        if (embedded_ && watched == parentWidget() && event->type() == QEvent::Resize) keepInsideViewport();
        return QDialog::eventFilter(watched, event);
    }
private:
    bool exclusive_ = true;
    std::unique_ptr<WindowLock> windowLock_;
    static QWidget *overlayParent(QWidget *requested) {
        if (!requested) return nullptr;
        QWidget *root = requested->window();
        if (!root) return requested;
        for (QOpenGLWidget *candidate : root->findChildren<QOpenGLWidget *>())
            if (dynamic_cast<CadViewport *>(candidate)) return candidate;
        return requested;
    }
    void keepInsideViewport() {
        if (!embedded_ || !parentWidget()) return;
        const QSize maximum(qMax(1, parentWidget()->width() - 16), qMax(1, parentWidget()->height() - 16));
        if (size().width() > maximum.width() || size().height() > maximum.height())
            resize(size().boundedTo(maximum));
        move(qBound(0, x(), qMax(0, parentWidget()->width() - width())),
             qBound(0, y(), qMax(0, parentWidget()->height() - height())));
    }
    void updateRoundedMask() {
        if (cornerRadius_ <= 0) {
            clearMask();
            return;
        }
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), cornerRadius_, cornerRadius_);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }
    static QColor readableText(const QColor &background) {
        // I pannelli mantengono un aspetto scuro/medio anche sopra scene
        // luminose. Il testo nero diventa utile soltanto su un fondo davvero
        // chiaro; una soglia piu' bassa faceva oscillare tutta la palette dopo
        // la prima cattura del framebuffer.
        const double luminance = 0.2126 * background.redF() + 0.7152 * background.greenF() + 0.0722 * background.blueF();
        return luminance > 0.70 ? QColor(20, 24, 29) : QColor(242, 246, 250);
    }
    void applyTransparentStyle() {
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
        setStyleSheet(QStringLiteral(
            "QDialog#functionDialogPanel, QScrollArea#functionDialogScroll, "
            "QScrollArea#functionDialogScroll > QWidget > QWidget, QWidget#functionDialogContents, "
            "QWidget#loftContents, QWidget#loftFooter, QAbstractItemView, QAbstractSpinBox, QPushButton { background: transparent; }"
            "QAbstractItemView::item { background: transparent; }"
            "QComboBox { background-color: %1; color: %2; border: 1px solid rgba(135,170,205,150); padding: 2px 22px 2px 5px; }"
            "QComboBox QAbstractItemView { background-color: %3; color: %4; border: 1px solid #52708d; outline: 0; }"
            "QComboBox QAbstractItemView::item { background-color: %3; color: %4; min-height: 24px; padding: 2px 5px; }"
            "QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected { background-color: #ff9f1c; color: #101820; font-weight: 800; border: 2px solid #ffe0a3; }"
            "QSizeGrip#functionPanelResizeGrip { background: transparent; }")
            .arg(fieldRgba, fieldForeground.name(QColor::HexRgb), popupRgba, popupForeground.name(QColor::HexRgb)));
    }
    QString panelSizeSettingsKey() const {
        QString id = objectName() != QLatin1String("functionDialogPanel") ? objectName() : windowTitle();
        if (id.isEmpty()) id = QStringLiteral("panel");
        id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
        return QStringLiteral("view/functionPanelSizes/") + id;
    }
    QSize naturalPanelSize() {
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
    QSize maximumPanelSize() const {
        if (embedded_ && parentWidget())
            return QSize(qMax(1, parentWidget()->width() - 32), qMax(1, parentWidget()->height() - 32));
        QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()));
        if (!screen) screen = QGuiApplication::primaryScreen();
        return screen ? screen->availableGeometry().size() - QSize(32, 32) : QSize(1600, 1000);
    }
    void initializePanelSize() {
        if (panelSizeInitialized_) return;
        panelSizeInitialized_ = true;
        const QSize stored = QSettings().value(panelSizeSettingsKey()).toSize();
        QSize requested = stored.isValid() ? stored : naturalPanelSize();
        const QSize maximum = maximumPanelSize();
        requested = requested.expandedTo(minimumSize()).boundedTo(maximum);
        resize(requested);
    }
    void storeManualSize() {
        if (!size().isValid()) return;
        QSettings().setValue(panelSizeSettingsKey(), size());
    }
    void focusFirstEditor() {
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
    void locateViewport() {
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
    void scheduleBackdropCapture(int delay) {
        if (!isVisible() || opacity_ >= 100) return;
        if (viewport_ && viewport_->gpuGlassAvailable()) {
            backdrop_ = QImage();
            return;
        }
        if (!captureTimer_.isActive() || delay == 0) captureTimer_.start(delay);
    }
    void syncGpuGlass() {
        if (!viewport_ || !embedded_) return;
        viewport_->setGlassPanel(reinterpret_cast<quintptr>(this), geometry(), blur_, cornerRadius_, isVisible() && opacity_ < 100);
    }
    static QImage boxBlurPass(const QImage &source, int radius, bool horizontal) {
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
    static QImage blurScene(QImage image, int radius) {
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
    void captureSceneBackdrop() {
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
    void updatePalette() {
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
        QPalette p = palette();
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
    QColor panelColor_;
    int opacity_ = 88;
    int blur_ = 12;
    int cornerRadius_ = 10;
    QPointer<CadViewport> viewport_;
    QImage backdrop_;
    QTimer captureTimer_;
    quint64 capturedFrameSerial_ = std::numeric_limits<quint64>::max();
    QFormLayout *form_ = nullptr;
    QSizeGrip *resizeGrip_ = nullptr;
    static constexpr int kHeaderHeight = 30;
    QToolButton *headerClose_ = nullptr;
    bool headerVisible_ = true;
    QPoint dragOffset_;
    QPoint resizeStartGlobal_;
    QSize resizeStartSize_;
    bool embedded_ = false, dragging_ = false, resizing_ = false, placed_ = false, panelSizeInitialized_ = false;
};

class FeatureOperationDiagram final : public QWidget {
public:
    enum Kind { Extrusion, Revolution, Sweep };
    explicit FeatureOperationDiagram(Kind kind, QWidget *parent = nullptr) : QWidget(parent), kind_(kind) {
        setMinimumSize(390, 180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
protected:
    void paintEvent(QPaintEvent *) override {
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
private:
    Kind kind_;
};

// Finestra di una funzione che si chiude solo se il comando riesce: la
// conferma esegue `apply`; se restituisce un errore, la finestra resta aperta
// con i valori inseriti e l'errore in rosso sopra i pulsanti (si esce con
// Annulla o Esc). Vero se il comando e' riuscito.
bool ForgeCad::Gui::runUntilApplied(QDialog &dialog, QFormLayout *form, QDialogButtonBox *buttons, const std::function<QString()> &apply) {
    auto *errorLabel = new QLabel(&dialog);
    errorLabel->setWordWrap(true);
    errorLabel->setStyleSheet(QStringLiteral("color: #ff7b72; font-weight: 600;"));
    errorLabel->setMaximumWidth(360);
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

// Come runUntilApplied, ma la finestra non e' modale (la vista resta attiva:
// si puo' ruotare) e il resto della finestra principale e' spento (WindowLock).
static bool runModeless(QMainWindow *window, CadViewport *viewport, QDialog &dialog);
static bool runUntilAppliedModeless(QMainWindow *window, CadViewport *viewport, QDialog &dialog, QFormLayout *form, QDialogButtonBox *buttons,
                                    const std::function<QString()> &apply) {
    auto *errorLabel = new QLabel(&dialog);
    errorLabel->setWordWrap(true);
    errorLabel->setStyleSheet(QStringLiteral("color: #ff7b72; font-weight: 600;"));
    errorLabel->setMaximumWidth(360);
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

// Anteprima dal vivo in una finestra di funzione: una riga "Anteprima" con lo
// stato, `request` a ogni cambio dei valori; alla chiusura l'anteprima sparisce.
struct PreviewScope {
    CadViewport *viewport = nullptr;
    QLabel *label = nullptr;
    int index = -1;  // corpo che l'anteprima sostituisce (modifica), -1 nuovo
    PreviewScope(CadViewport *target, QDialog &dialog, QFormLayout *form, int replacedIndex, QBoxLayout *outside = nullptr)
        : viewport(target), index(replacedIndex) {
        if (!viewport) return;
        label = new QLabel(QStringLiteral("in calcolo..."), &dialog);
        label->setWordWrap(true);
        label->setMaximumWidth(360);
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
        QLabel *status = label;
        viewport->setPreviewCallback([status](const QString &error) {
            status->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
        });
    }
    void request(const ExtrusionObject &definition) const {
        if (!viewport) return;
        label->setText(QStringLiteral("in calcolo..."));
        viewport->requestPreview(definition, index);
    }
    ~PreviewScope() {
        if (!viewport) return;
        viewport->setPreviewCallback({});
        viewport->clearPreview();
    }
    PreviewScope(const PreviewScope &) = delete;
    PreviewScope &operator=(const PreviewScope &) = delete;
};

// Dove e come mostrare l'anteprima di una finestra di funzione: il viewport
// (nullptr: niente anteprima), il corpo sostituito e la definizione dai valori.
template <class... Values>
struct PreviewSpec {
    CadViewport *viewport = nullptr;
    int index = -1;
    std::function<ExtrusionObject(Values...)> define;
};

// Selezionare una riga identifica il riferimento senza cambiare gli operandi
// della funzione o ricalcolarne l'anteprima. Vale anche per liste con checkbox.
static void connectReferenceList(QListWidget *list, QDialog &dialog, CadViewport *viewport,
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

class GeometryReferenceList final : public QListWidget {
public:
    GeometryReferenceList(QDialog &dialog, CadViewport *viewport, const QString &name)
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
    void setReferences(const QVector<GeometryRef> &refs) {
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
    void setEdges(int body, const QVector<EdgePoint> &edges) {
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
protected:
    void changeEvent(QEvent *event) override {
        QListWidget::changeEvent(event);
        if (event->type() == QEvent::EnabledChange && !isEnabled()) viewport_->clearReferenceListHighlight(this);
    }
private:
    static bool sameReference(const GeometryRef &a, const GeometryRef &b) {
        return a.kind == b.kind && a.index == b.index && a.element == b.element
            && a.point.subshape == b.point.subshape && a.point.geometry == b.point.geometry
            && a.point.role == b.point.role && a.point.x == b.point.x && a.point.y == b.point.y && a.point.z == b.point.z;
    }
    CadViewport *viewport_;
    QVector<GeometryRef> refs_;
};

// Finestra della misura di un raccordo o smusso con l'anteprima dal vivo
// (CadViewport::requestBlendPreview dopo Invio nel campo). `edgesLabel` descrive gli
// spigoli; `chamferBox` (facoltativa) permette di passare da raccordo a
// smusso; "Spigoli..." (se `reselect` non e' nullo) chiude la finestra e
// chiede di tornare alla scelta degli spigoli. `apply` fa il comando: la
// finestra resta aperta finche' non riesce o si annulla.
struct BlendDialogResult {
    bool applied = false, reselect = false;
    double size = 0.0;
    bool chamfer = false;
    ChamferSpec spec;
};
static BlendDialogResult blendDialog(QWidget *parent, CadViewport *viewport, const QString &title, int base, int hidden,
                                     const QVector<EdgePoint> &edges, double size, bool chamfer, bool allowKindChange, const ChamferSpec &initialSpec,
                                     const std::function<QString(int, const QVector<EdgePoint> &, double, bool, const ChamferSpec &)> &apply,
                                     bool pickInView = false) {
    BlendDialogResult result;
    int selectedBase = base;
    QVector<EdgePoint> selectedEdges = edges;
    bool previewStarted = !pickInView;
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *sizeBox = new ForgeCad::ExpressionSpinBox(&dialog);
    sizeBox->setDecimals(6);
    sizeBox->setRange(0.000001, 100000.0);
    sizeBox->setValue(size);
    QCheckBox *chamferBox = nullptr;
    auto *sizeLabel = new QLabel(&dialog);
    form->addRow(sizeLabel, sizeBox);
    if (allowKindChange) {
        chamferBox = new QCheckBox(QStringLiteral("Smusso (distanza) invece del raccordo (raggio)"), &dialog);
        chamferBox->setChecked(chamfer);
        form->addRow(QString(), chamferBox);
    }
    // Smusso: distanza uguale, due distanze o distanza e angolo; la prima distanza sulla
    // faccia di riferimento (normale piu' verso +Z, poi +X, poi +Y), o sull'altra con "Inverti".
    auto *modeBox = new QComboBox(&dialog);
    modeBox->addItems({QStringLiteral("Distanza uguale sulle due facce"), QStringLiteral("Due distanze"), QStringLiteral("Distanza e angolo")});
    modeBox->setCurrentIndex(qBound(0, initialSpec.mode, 2));
    auto *secondBox = new ForgeCad::ExpressionSpinBox(&dialog);
    secondBox->setDecimals(6);
    auto *secondLabel = new QLabel(&dialog);
    auto *flipBox = new QCheckBox(QStringLiteral("Inverti i lati (la prima distanza sull'altra faccia)"), &dialog);
    flipBox->setChecked(initialSpec.flip);
    flipBox->setToolTip(QStringLiteral("La prima distanza si misura sulla faccia con la normale piu' verso +Z (o +X, poi +Y):\n"
                                       "su uno spigolo in alto e' la faccia superiore, cioe' lungo X o Y; invertendo va sulla faccia laterale, lungo Z."));
    form->addRow(QStringLiteral("Smusso:"), modeBox);
    form->addRow(secondLabel, secondBox);
    form->addRow(QString(), flipBox);
    const auto currentChamfer = [chamferBox, chamfer] { return chamferBox ? chamferBox->isChecked() : chamfer; };
    double secondDistance = initialSpec.mode == 1 ? initialSpec.second : size, angle = initialSpec.mode == 2 ? initialSpec.second : 45.0;
    int shownMode = -1;
    const auto describe = [&] {
        const bool isChamfer = currentChamfer();
        const int mode = modeBox->currentIndex();
        sizeLabel->setText(!isChamfer ? QStringLiteral("Raggio del raccordo:")
                                      : mode == 0 ? QStringLiteral("Distanza dello smusso dallo spigolo:") : QStringLiteral("Prima distanza (faccia di riferimento):"));
        // La seconda misura: distanza o angolo (ognuna ricorda il suo valore).
        if (shownMode == 1) secondDistance = secondBox->value();
        if (shownMode == 2) angle = secondBox->value();
        const QSignalBlocker blocker(secondBox);
        if (mode == 2) {
            secondLabel->setText(QStringLiteral("Angolo con la faccia di riferimento (gradi):"));
            secondBox->setSuffix(QStringLiteral(" °"));
            secondBox->setRange(0.01, 179.99);
            secondBox->setValue(angle);
        } else {
            secondLabel->setText(QStringLiteral("Seconda distanza (altra faccia):"));
            secondBox->setLengthMeasurement(true);
            secondBox->setRange(0.000001, 100000.0);
            secondBox->setValue(secondDistance);
        }
        shownMode = mode;
        form->setRowVisible(modeBox, isChamfer);
        form->setRowVisible(secondBox, isChamfer && mode != 0);
        form->setRowVisible(flipBox, isChamfer && mode != 0);
        dialog.adjustSize();
    };
    const auto currentSpec = [&] {
        ChamferSpec spec;
        spec.mode = modeBox->currentIndex();
        spec.second = secondBox->value();
        spec.flip = flipBox->isChecked();
        return spec;
    };
    describe();
    auto *edgesLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("blendEdges"));
    form->addRow(QStringLiteral("Spigoli:"), edgesLabel);
    auto *previewLabel = new QLabel(pickInView ? QStringLiteral("seleziona almeno uno spigolo nella vista")
                                               : QStringLiteral("Anteprima in calcolo..."), &dialog);
    previewLabel->setWordWrap(true);
    previewLabel->setMaximumWidth(360);
    form->addRow(QStringLiteral("Anteprima:"), previewLabel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *edgesButton = buttons->addButton(QStringLiteral("Spigoli..."), QDialogButtonBox::ActionRole);
    edgesButton->setToolTip(QStringLiteral("Torna alla scelta degli spigoli (quelli di adesso restano scelti)"));
    QObject::connect(edgesButton, &QPushButton::clicked, &dialog, [&dialog, &result] {
        result.reselect = true;
        dialog.reject();
    });
    edgesButton->setVisible(!pickInView);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    const auto refresh = [&] {
        edgesLabel->setEdges(selectedBase, selectedEdges);
        if (!previewStarted || selectedBase < 0 || selectedEdges.isEmpty()) {
            previewLabel->setText(QStringLiteral("seleziona almeno uno spigolo nella vista"));
            viewport->clearBlendPreview();
            return;
        }
        previewLabel->setText(QStringLiteral("in calcolo..."));
        viewport->requestBlendPreview(selectedBase, selectedEdges, sizeBox->value(), currentChamfer(), hidden, currentSpec());
    };
    viewport->setPreviewCallback([previewLabel](const QString &error) {
        previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
    });
    sizeBox->setKeyboardTracking(false);
    secondBox->setKeyboardTracking(false);
    sizeBox->onReturn = refresh;
    secondBox->onReturn = refresh;
    QObject::connect(flipBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(modeBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        describe();
        refresh();
    });
    if (chamferBox) QObject::connect(chamferBox, &QCheckBox::toggled, &dialog, [&] {
        describe();
        refresh();
    });
    if (pickInView) {
        viewport->setEdgePickPanelKeyCallbacks(
            [buttons] {
                if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) ok->click();
            },
            [&dialog] { dialog.reject(); });
        viewport->setEdgePickChangedCallback([&](int body, QVector<EdgePoint> changed) {
            selectedBase = body;
            selectedEdges = std::move(changed);
            refresh();
        });
        // Il primo calcolo parte dal ciclo eventi successivo: il pannello e'
        // gia' visibile quando l'eventuale faccia preselezionata viene elaborata.
        QTimer::singleShot(0, &dialog, [viewport, &previewStarted] {
            previewStarted = true;
            viewport->enableEdgePickPreview();
        });
    }
    refresh();
    sizeBox->selectAll();
    const auto commit = [&] {
        if (selectedBase < 0 || selectedEdges.isEmpty()) return QStringLiteral("Seleziona almeno uno spigolo nella vista.");
        return apply(selectedBase, selectedEdges, sizeBox->value(), currentChamfer(), currentSpec());
    };
    if (pickInView && qobject_cast<QMainWindow *>(parent))
        result.applied = runUntilAppliedModeless(qobject_cast<QMainWindow *>(parent), viewport, dialog, form, buttons, commit);
    else
        result.applied = runUntilApplied(dialog, form, buttons, commit);
    if (pickInView) {
        viewport->setEdgePickPanelKeyCallbacks({}, {});
        viewport->setEdgePickChangedCallback({});
        viewport->endEdgePick(true);
    }
    viewport->setPreviewCallback({});
    viewport->clearBlendPreview();
    result.size = sizeBox->value();
    result.chamfer = currentChamfer();
    result.spec = currentSpec();
    return result;
}

static int resultBodyForPick(CadViewport *viewport, const QVector<int> &candidates, int picked);

// Scelta dei corpi nella vista per le finestre con un elenco di corpi: come per
// i raccordi, il corpo lo da' la faccia cliccata. Con un elenco a tendina il
// clic sceglie il corpo e la scelta finisce; con un elenco a spunte il clic
// accende o spegne il corpo e la scelta continua (Esc nella vista la chiude).
// `start(true)` (funzione nuova senza un corpo gia' selezionato) apre la
// finestra in attesa del clic: `waiting()` finche' non si sceglie un corpo,
// nella vista o nell'elenco, e intanto l'anteprima aspetta. Le finestre che
// scelgono anche altri riferimenti nella vista passano prima i clic a
// `handle`; le altre usano `install`. `changed` aggiorna la finestra.
class BodyPicker {
public:
    BodyPicker(QDialog &dialog, CadViewport *viewport, int replaced, const QVector<int> &candidates, QComboBox *combo,
               QListWidget *list = nullptr)
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
    ~BodyPicker() {
        if (active_) viewport_->cancelReferencePick();
        if (installed_) viewport_->setReferencePickCallback(nullptr);
    }
    BodyPicker(const BodyPicker &) = delete;
    BodyPicker &operator=(const BodyPicker &) = delete;

    // L'elenco con il pulsante accanto.
    QWidget *row(QWidget *field) const {
        auto *row = new QWidget(field->parentWidget());
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(field, 1);
        layout->addWidget(button_, 0, list_ ? Qt::AlignTop : Qt::Alignment());
        return row;
    }
    bool active() const { return active_; }
    bool waiting() const { return waiting_; }
    // L'elenco contiene anche gli schizzi da `start` in poi (nell'ordine del
    // documento): un clic su un'entita' di uno schizzo sceglie lo schizzo.
    void acceptSketches(int start) {
        sketchStart_ = start;
        button_->setToolTip(QStringLiteral("Clicca una faccia del corpo o un'entita' dello schizzo nella vista"));
    }
    // Dentro `handle`: il cambio dell'elenco viene dal clic, la finestra si
    // aggiorna una volta sola alla fine (`changed`).
    bool handling() const { return handling_; }
    QString hint() const {
        return list_ ? QStringLiteral("Clicca i corpi nella vista (di nuovo per toglierli); Esc nella vista termina.")
                     : sketchStart_ >= 0 ? QStringLiteral("Clicca nella vista una faccia del corpo o un'entita' dello schizzo.")
                                         : QStringLiteral("Clicca nella vista una faccia del corpo.");
    }
    void start(bool waiting = false) {
        if (!button_->isEnabled()) return;
        if (starting) starting();  // la finestra lascia la sua scelta nella vista
        const int roles = sketchStart_ >= 0 ? ForgeCad::DatumRoleFace | ForgeCad::DatumRoleCurve | ForgeCad::DatumRoleLine : ForgeCad::DatumRoleFace;
        const QString error = viewport_->beginReferencePick(roles, replaced_);
        active_ = error.isEmpty();
        waiting_ = active_ && waiting;
        button_->setChecked(active_);
        if (!active_ && message) message(error, true);
    }
    void stop() {
        if (active_) viewport_->cancelReferencePick();
        active_ = false;
        button_->setChecked(false);
    }
    // Il clic nella vista: vero se riguardava la scelta del corpo.
    bool handle(bool picked, const GeometryRef &ref) {
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
    // Per le finestre che non scelgono altro nella vista.
    void install() {
        installed_ = true;
        viewport_->setReferencePickCallback([this](bool picked, GeometryRef ref) { handle(picked, ref); });
    }

    std::function<void()> changed, starting;
    std::function<void(const QString &, bool)> message;

private:
    CadViewport *viewport_;
    int replaced_;
    QVector<int> candidates_;
    QComboBox *combo_;
    QListWidget *list_;
    QPushButton *button_ = nullptr;
    bool active_ = false, waiting_ = false, installed_ = false, handling_ = false;
    int sketchStart_ = -1;
};

// Finestra della scala: corpo, fattore uniforme e centro (origine, baricentro
// del solido o un punto), con l'anteprima. `definition` porta i valori
// iniziali; `replaced` e' il corpo modificato (-1 nuovo).
static QString logicalBodyLabel(CadViewport *viewport, int index);
static bool scaleDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &definition,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    if (candidates.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Nella scena non ci sono corpi da scalare."));
        return false;
    }
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int index : candidates) bodyBox->addItem(logicalBodyLabel(viewport, index));
    bodyBox->setCurrentIndex(qMax(0, int(candidates.indexOf(definition.firstBody))));
    bodyBox->setEnabled(replaced < 0);
    BodyPicker picker(dialog, viewport, replaced, candidates, bodyBox);
    auto *factorBox = new ForgeCad::ExpressionSpinBox(&dialog);
    factorBox->setLengthMeasurement(false);
    factorBox->setDecimals(6);
    factorBox->setRange(0.000001, 1000000.0);
    factorBox->setValue(definition.scaleFactor);
    auto *centerBox = new QComboBox(&dialog);
    centerBox->addItems({QStringLiteral("Origine"), QStringLiteral("Baricentro del solido"), QStringLiteral("Punto")});
    centerBox->setCurrentIndex(qBound(0, definition.scaleCenterMode, 2));
    QDoubleSpinBox *coordinates[3];
    auto *pointRow = new QWidget(&dialog);
    auto *pointLayout = new QHBoxLayout(pointRow);
    pointLayout->setContentsMargins(0, 0, 0, 0);
    const double initial[3] = {definition.scaleCenter.x, definition.scaleCenter.y, definition.scaleCenter.z};
    for (int k = 0; k < 3; ++k) {
        coordinates[k] = new ForgeCad::ExpressionSpinBox(pointRow);
        coordinates[k]->setDecimals(6);
        coordinates[k]->setRange(-1e6, 1e6);
        coordinates[k]->setValue(initial[k]);
        coordinates[k]->setPrefix(QStringList{QStringLiteral("X "), QStringLiteral("Y "), QStringLiteral("Z ")}.at(k));
        pointLayout->addWidget(coordinates[k]);
    }
    form->addRow(QStringLiteral("Corpo:"), picker.row(bodyBox));
    form->addRow(QStringLiteral("Fattore di scala:"), factorBox);
    form->addRow(QStringLiteral("Centro:"), centerBox);
    form->addRow(QStringLiteral("Punto:"), pointRow);
    const PreviewScope scope(viewport, dialog, form, replaced);
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.operation = -1;
        d.feature = BodyFeature::Scale;
        d.firstBody = candidates.at(bodyBox->currentIndex());
        d.scaleFactor = factorBox->value();
        d.scaleCenterMode = centerBox->currentIndex();
        d.scaleCenter = {coordinates[0]->value(), coordinates[1]->value(), coordinates[2]->value()};
        return d;
    };
    const auto refresh = [&] {
        pointRow->setEnabled(centerBox->currentIndex() == 2);
        if (picker.waiting()) {
            scope.label->setText(picker.hint());
            viewport->clearPreview();
            return;
        }
        scope.request(current());
    };
    picker.changed = refresh;
    picker.install();
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(centerBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(factorBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    for (QDoubleSpinBox *box : coordinates) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    // Senza un corpo selezionato prima del comando lo si sceglie nella vista.
    if (replaced < 0 && !candidates.contains(definition.firstBody)) picker.start(true);
    refresh();
    factorBox->selectAll();
    const auto run = [&] {
        if (picker.waiting()) return picker.hint();
        picker.stop();
        return apply(current());
    };
    if (auto *window = qobject_cast<QMainWindow *>(parent)) return runUntilAppliedModeless(window, viewport, dialog, form, buttons, run);
    return runUntilApplied(dialog, form, buttons, run);
}

// Elica e spirale: base (cerchio o arco di uno schizzo, o lo spigolo / la
// faccia scelti nella vista), tipo, modo (passo e giri, altezza e giri,
// altezza e passo), conicita', angolo di partenza, verso, con l'anteprima.
// "Scegli nella vista..." (se `allowPick`) chiude la finestra con `reselect`:
// la base si sceglie con un clic e la finestra si riapre con i valori.
struct HelixDialogResult {
    bool applied = false, reselect = false;
    ExtrusionObject definition;
};
static HelixDialogResult helixDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                                     bool allowPick, const std::function<QString(const ExtrusionObject &)> &apply) {
    HelixDialogResult result;
    result.definition = initial;
    const QVector<SketchObject> &sketches = viewport->sketches();
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    struct Base {
        int source = 0, sketch = -1, curve = -1, body = -1;
        EdgePoint point;
    };
    QVector<Base> bases;
    QStringList labels;
    for (int s = 0; s < sketches.size(); ++s)
        for (int c = 0; c < sketches.at(s).curves.size(); ++c) {
            const CurveObject &curve = sketches.at(s).curves.at(c);
            const bool circle = curve.tool == DrawingTool::Circle && curve.controlPoints.size() >= 2;
            const bool arc = curve.tool == DrawingTool::Arc && curve.controlPoints.size() >= 3;
            if (!circle && !arc) continue;
            const double r = std::hypot(curve.controlPoints.at(1).x() - curve.controlPoints.at(0).x(), curve.controlPoints.at(1).y() - curve.controlPoints.at(0).y());
            bases.append({0, s, c, -1, {}});
            labels.append(QStringLiteral("%1: %2 %3 (R %4)%5").arg(sketches.at(s).name, circle ? QStringLiteral("cerchio") : QStringLiteral("arco"))
                              .arg(c + 1).arg(ForgeCad::formatLength(r)).arg(curve.construction ? QStringLiteral(", costruzione") : QString()));
        }
    int current = -1;
    if (initial.helix.source != 0 && initial.firstBody >= 0 && initial.firstBody < bodies.size()) {
        bases.prepend({initial.helix.source, -1, -1, initial.firstBody, initial.helix.reference});
        labels.prepend(QStringLiteral("%1 di %2").arg(initial.helix.source == 1 ? QStringLiteral("Spigolo circolare") : QStringLiteral("Faccia cilindrica o conica"),
                                                      bodies.at(initial.firstBody).name));
        current = 0;
    } else {
        for (int k = 0; k < bases.size(); ++k)
            if (bases.at(k).sketch == initial.sketchIndex && (initial.helix.curve < 0 || bases.at(k).curve == initial.helix.curve)) {
                current = k;
                break;
            }
    }
    if (bases.isEmpty() && !allowPick) {
        QMessageBox::information(parent, title, QStringLiteral("Serve un cerchio o un arco in uno schizzo, o uno spigolo circolare o una faccia cilindrica."));
        return result;
    }
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *baseRow = new QWidget(&dialog);
    auto *baseLayout = new QHBoxLayout(baseRow);
    baseLayout->setContentsMargins(0, 0, 0, 0);
    auto *baseBox = new QComboBox(baseRow);
    baseBox->addItems(labels);
    baseBox->setCurrentIndex(qMax(0, current));
    baseLayout->addWidget(baseBox, 1);
    QPushButton *pickButton = nullptr;
    if (allowPick) {
        pickButton = new QPushButton(QStringLiteral("Scegli nella vista..."), baseRow);
        pickButton->setToolTip(QStringLiteral("Un clic su uno spigolo circolare o su una faccia cilindrica o conica di un corpo"));
        baseLayout->addWidget(pickButton);
    }
    baseBox->setEnabled(replaced < 0 || initial.helix.source == 0);
    auto *typeBox = new QComboBox(&dialog);
    typeBox->addItems({QStringLiteral("Elica"), QStringLiteral("Spirale piana (di Archimede)")});
    typeBox->setCurrentIndex(initial.helix.spiral ? 1 : 0);
    auto *modeBox = new QComboBox(&dialog);
    modeBox->addItems({QStringLiteral("Passo e giri"), QStringLiteral("Altezza e giri"), QStringLiteral("Altezza e passo")});
    modeBox->setCurrentIndex(qBound(0, initial.helix.mode, 2));
    const auto spin = [&dialog](double value, double lo, double hi, int decimals, const QString &suffix = QString()) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setDecimals(decimals);
        box->setRange(lo, hi);
        box->setValue(value);
        box->setSuffix(suffix);
        return box;
    };
    auto *pitchBox = spin(initial.helix.pitch, 1e-6, 1e6, 6);
    auto *turnsBox = spin(initial.helix.turns, 1e-3, 1e5, 4);
    turnsBox->setLengthMeasurement(false);
    auto *heightBox = spin(initial.helix.height, 1e-6, 1e6, 6);
    auto *taperBox = spin(initial.helix.taper, -89.0, 89.0, 4, QStringLiteral("°"));
    taperBox->setToolTip(QStringLiteral("Elica conica: positivo = il raggio cresce lungo l'asse. Sulla faccia di un cono vale quella della faccia."));
    auto *startBox = spin(initial.helix.startAngle, -360.0, 360.0, 4, QStringLiteral("°"));
    startBox->setToolTip(QStringLiteral("Base da schizzo: 0° verso X, 90° verso Y (quadrante superiore), anche invertendo l'asse. Spigolo o faccia: dall'asse X del modello proiettato."));
    auto *leftBox = new QCheckBox(QStringLiteral("Sinistrorsa (senso orario guardando lungo l'asse)"), &dialog);
    leftBox->setChecked(initial.helix.leftHanded);
    auto *reverseBox = new QCheckBox(QStringLiteral("Inverti la direzione dell'asse"), &dialog);
    reverseBox->setChecked(initial.helix.reverse);
    auto *pitchLabel = new QLabel(QStringLiteral("Passo:"), &dialog);
    auto *heightLabel = new QLabel(QStringLiteral("Altezza:"), &dialog);
    auto *modeLabel = new QLabel(QStringLiteral("Definizione:"), &dialog);
    auto *taperLabel = new QLabel(QStringLiteral("Conicita':"), &dialog);
    form->addRow(QStringLiteral("Base:"), baseRow);
    form->addRow(QStringLiteral("Tipo:"), typeBox);
    form->addRow(modeLabel, modeBox);
    form->addRow(pitchLabel, pitchBox);
    form->addRow(QStringLiteral("Giri:"), turnsBox);
    form->addRow(heightLabel, heightBox);
    form->addRow(taperLabel, taperBox);
    form->addRow(QStringLiteral("Angolo di partenza:"), startBox);
    form->addRow(QString(), leftBox);
    form->addRow(QString(), reverseBox);
    const PreviewScope scope(viewport, dialog, form, replaced);
    const auto currentDefinition = [&] {
        ExtrusionObject d = initial;
        d.operation = -1;
        d.feature = BodyFeature::Helix;
        if (baseBox->currentIndex() >= 0 && baseBox->currentIndex() < bases.size()) {
            const Base &b = bases.at(baseBox->currentIndex());
            d.helix.source = b.source;
            if (b.source == 0) {
                d.sketchIndex = b.sketch;
                d.helix.curve = b.curve;
                d.firstBody = -1;
                d.plane = sketches.value(b.sketch).plane;
            } else {
                d.firstBody = b.body;
                d.helix.reference = b.point;
                d.sketchIndex = -1;
            }
        }
        d.helix.spiral = typeBox->currentIndex() == 1;
        d.helix.mode = modeBox->currentIndex();
        d.helix.pitch = pitchBox->value();
        d.helix.turns = turnsBox->value();
        d.helix.height = heightBox->value();
        d.helix.taper = taperBox->value();
        d.helix.startAngle = startBox->value();
        d.helix.leftHanded = leftBox->isChecked();
        d.helix.reverse = reverseBox->isChecked();
        return d;
    };
    bool updating = false;
    const auto refresh = [&] {
        if (updating) return;
        updating = true;
        const bool spiral = typeBox->currentIndex() == 1;
        const int mode = spiral ? 0 : modeBox->currentIndex();
        modeBox->setEnabled(!spiral);
        taperBox->setEnabled(!spiral);
        heightBox->setEnabled(!spiral && mode != 0);
        turnsBox->setEnabled(spiral || mode != 2);
        pitchBox->setEnabled(spiral || mode != 1);
        pitchLabel->setText(spiral ? QStringLiteral("Passo (crescita del raggio per giro):") : QStringLiteral("Passo (per giro):"));
        // Il valore che il modo non usa si ricava dagli altri due.
        if (!spiral && mode == 0) heightBox->setValue(pitchBox->value() * turnsBox->value());
        if (!spiral && mode == 1) pitchBox->setValue(heightBox->value() / turnsBox->value());
        if (!spiral && mode == 2) turnsBox->setValue(heightBox->value() / pitchBox->value());
        updating = false;
        if (!bases.isEmpty()) scope.request(currentDefinition());
    };
    QObject::connect(baseBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(typeBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(modeBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    for (QDoubleSpinBox *box : {pitchBox, turnsBox, heightBox, taperBox, startBox}) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    for (QCheckBox *box : {leftBox, reverseBox}) QObject::connect(box, &QCheckBox::toggled, &dialog, refresh);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (pickButton)
        QObject::connect(pickButton, &QPushButton::clicked, &dialog, [&] {
            result.reselect = true;
            result.definition = currentDefinition();
            dialog.reject();
        });
    form->addRow(buttons);
    refresh();
    result.applied = runUntilApplied(dialog, form, buttons, [&] {
        if (bases.isEmpty()) return QStringLiteral("Scegli la base: \"Scegli nella vista...\".");
        return apply(currentDefinition());
    });
    if (!result.reselect) result.definition = currentDefinition();
    return result;
}

// Nome di un corpo risultante nelle finestre delle operazioni tra corpi: il
// corpo logico ("Corpo 2"), non la feature che ne e' l'ultimo stadio, con
// superficie/nascosto.
static QString resultBodyLabel(CadViewport *viewport, int index, bool markHidden = true) {
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

// Corpi da offrire come operandi a una funzione nel posto `replaced` (-1 se
// nuova): i corpi che esistono in quel punto della storia (resultBodiesBefore:
// l'ultimo stadio di ogni corpo, non le feature intermedie), piu' quelli gia'
// scelti in modifica; tenuti quelli che `accept` vuole.
static QVector<int> operandBodies(CadViewport *viewport, int replaced, const QVector<int> &used,
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

// Elenco dei solidi con cui fondere un'estrusione, una rivoluzione o una sweep
// (spunte, indice in Qt::UserRole): i corpi, non le feature. Restituisce gli indici.
static QVector<int> fillMergeBodyList(QListWidget *list, CadViewport *viewport, int replaced, const QVector<int> &used) {
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

// Sweep: profilo (uno schizzo), percorso (un altro schizzo o una curva, come
// un'elica) e orientamento del profilo lungo il percorso, con l'anteprima.
static bool sweepDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<SketchObject> &sketches = viewport->sketches();
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    const int limit = replaced >= 0 ? replaced : int(bodies.size());
    QVector<int> curves;
    for (int index = 0; index < limit; ++index)
        if (ForgeCad::isCurveBody(bodies.at(index)) && bodies.at(index).curve) curves.append(index);
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setObjectName(QStringLiteral("sweepDialog"));
    auto *form = dialog.createScrollableForm();
    form->addRow(new FeatureOperationDiagram(FeatureOperationDiagram::Sweep, &dialog));
    GeometryRef profileRef = initial.sweepProfileRef, pathRef = initial.sweepPathRef;
    auto *profileSource = new QComboBox(&dialog);
    profileSource->setObjectName(QStringLiteral("sweepProfileSource"));
    profileSource->addItems({QStringLiteral("Schizzo"), QStringLiteral("Dalla vista (bordo o curva estratta / 3D)")});
    profileSource->setCurrentIndex(replaced < 0 || profileRef.kind >= 0 || sketches.isEmpty() ? 1 : 0);
    auto *pickProfile = new QPushButton(QStringLiteral("Seleziona sezione nella vista"), &dialog);
    pickProfile->setObjectName(QStringLiteral("sweepPickProfile"));
    pickProfile->setCheckable(true);
    auto *profileStatus = new QLabel(&dialog); profileStatus->setWordWrap(true);
    auto *profileBox = new QComboBox(&dialog), *pathTypeBox = new QComboBox(&dialog), *pathSketchBox = new QComboBox(&dialog),
         *pathChainBox = new QComboBox(&dialog), *curveBox = new QComboBox(&dialog);
    for (const SketchObject &sketch : sketches) {
        profileBox->addItem(sketch.name);
        pathSketchBox->addItem(sketch.name);
    }
    for (int index : curves) curveBox->addItem(bodies.at(index).name);
    pathTypeBox->addItems({QStringLiteral("Schizzo (catena o contorno chiuso di entita' tangenti)"), QStringLiteral("Curva 3D (elica, estratta o schizzo 3D)"), QStringLiteral("Dalla vista (bordo di faccia o curva 3D)")});
    profileBox->setCurrentIndex(sketches.isEmpty() ? -1 : qBound(0, initial.sketchIndex, int(sketches.size()) - 1));
    int pathSketch = initial.pathSketch;
    if (pathSketch < 0 || pathSketch >= sketches.size()) pathSketch = profileBox->currentIndex() == 0 && sketches.size() > 1 ? 1 : 0;
    pathSketchBox->setCurrentIndex(pathSketch);
    pathTypeBox->setCurrentIndex(initial.sweepPath == 1 && !curves.isEmpty() ? 1 : (initial.sweepPath == 0 && sketches.size() > 1 ? 0 : (curves.isEmpty() ? 0 : 1)));
    if (replaced < 0 || pathRef.kind >= 0) pathTypeBox->setCurrentIndex(2);
    pathTypeBox->setObjectName(QStringLiteral("sweepPathSource"));
    curveBox->setCurrentIndex(qMax(0, int(curves.indexOf(initial.firstBody))));
    auto *modeBox = new QComboBox(&dialog);
    modeBox->addItems({QStringLiteral("Torsione minima (segue il percorso senza ruotare attorno)"),
                       QStringLiteral("Frenet (segue la curvatura: molle, filetti)"), QStringLiteral("Orientamento costante (il profilo trasla)")});
    modeBox->setCurrentIndex(qBound(0, initial.sweepMode, 2));
    auto *operationBox = new QComboBox(&dialog);
    operationBox->addItems({QStringLiteral("Corpo nuovo"), QStringLiteral("Unisci ai solidi"), QStringLiteral("Sottrai dai solidi")});
    operationBox->setCurrentIndex(qBound(0, initial.mergeOperation, 2));
    auto *surfaceBox = new QCheckBox(QStringLiteral("Solo superficie (senza coperchi)"), &dialog);
    surfaceBox->setChecked(initial.sweepSurface);
    surfaceBox->setToolTip(QStringLiteral("Una lamina: un profilo chiuso diventa un tubo aperto alle estremita', un profilo aperto una superficie; "
                                          "non si fonde con i solidi"));
    auto *autoBox = new QCheckBox(QStringLiteral("Automatico: i solidi che hanno punti in comune con la sweep"), &dialog);
    autoBox->setChecked(initial.mergeAuto);
    auto *bodyList = new QListWidget(&dialog);
    bodyList->setMinimumHeight(110);
    BodyPicker picker(dialog, viewport, replaced, fillMergeBodyList(bodyList, viewport, replaced, initial.mergeBodies), nullptr, bodyList);
    QWidget *bodyRow = picker.row(bodyList);
    auto *help = new QLabel(QStringLiteral("Il profilo resta dove e' disegnato e si muove con il percorso, che parte dal punto piu' vicino al profilo "
                                           "(di solito lo si disegna sul piano normale al percorso, all'inizio). I tratti del percorso devono essere tangenti."),
                            &dialog);
    help->setWordWrap(true);
    help->setMaximumWidth(360);
    help->setStyleSheet(QStringLiteral("color: #8aa0b4;"));
    auto *pickPath = new QPushButton(QStringLiteral("Seleziona percorso nella vista"), &dialog);
    auto *pathStatus = new QLabel(&dialog); pathStatus->setWordWrap(true);
    auto *pickStatus = new QLabel(QStringLiteral("Puoi scegliere una singola catena cliccando una sua entità nella scena."), &dialog);
    pickStatus->setWordWrap(true); pickStatus->setStyleSheet(QStringLiteral("color: #8aa0b4;"));
    pickPath->setObjectName(QStringLiteral("sweepPickPath"));
    pickPath->setCheckable(true);
    form->addRow(QStringLiteral("Sezione:"), profileSource);
    form->addRow(QStringLiteral("Schizzo della sezione:"), profileBox);
    form->addRow(QString(), pickProfile);
    form->addRow(QString(), profileStatus);
    form->addRow(QStringLiteral("Percorso:"), pathTypeBox);
    form->addRow(QStringLiteral("Schizzo del percorso:"), pathSketchBox);
    form->addRow(QStringLiteral("Catena nello schizzo:"), pathChainBox);
    form->addRow(QString(), pickPath);
    form->addRow(QString(), pathStatus);
    form->addRow(QString(), pickStatus);
    form->addRow(QStringLiteral("Curva del percorso:"), curveBox);
    form->addRow(QStringLiteral("Orientamento:"), modeBox);
    form->addRow(QString(), surfaceBox);
    form->addRow(QStringLiteral("Risultato:"), operationBox);
    form->addRow(QString(), autoBox);
    form->addRow(QStringLiteral("Solidi:"), bodyRow);
    form->addRow(help);
    const PreviewScope scope(viewport, dialog, form, replaced);
    QVector<SketchPathRef> pathComponents;
    const auto refillPaths = [&] {
        const int sketchIndex = pathSketchBox->currentIndex();
        pathComponents = sketchIndex >= 0 && sketchIndex < sketches.size() ? ForgeCad::sketchPathComponents(sketches.at(sketchIndex))
                                                                          : QVector<SketchPathRef>();
        for (SketchPathRef &path : pathComponents) path.sketch = sketchIndex;
        pathChainBox->clear();
        for (int k = 0; k < pathComponents.size(); ++k) {
            const SketchPathRef &path = pathComponents.at(k);
            pathChainBox->addItem(QStringLiteral("Percorso %1 — %2 entità").arg(k + 1).arg(path.segments.size() + path.curves.size()));
        }
        SketchPathRef wanted{initial.pathSketch, initial.pathSegments, initial.pathCurves};
        const int selected = pathComponents.indexOf(wanted);
        if (selected >= 0) pathChainBox->setCurrentIndex(selected);
    };
    refillPaths();
    const auto current = [&] {
        ExtrusionObject d = initial;
        d.operation = -1;
        d.feature = BodyFeature::Sweep;
        d.sweepProfileRef = profileSource->currentIndex() == 1 ? profileRef : GeometryRef{};
        d.sweepPathRef = pathTypeBox->currentIndex() == 2 ? pathRef : GeometryRef{};
        d.sketchIndex = d.sweepProfileRef.kind >= 0 ? -1 : profileBox->currentIndex();
        d.plane = sketches.value(d.sketchIndex).plane;
        d.sweepPath = pathTypeBox->currentIndex() == 1 ? 1 : 0;
        d.pathSketch = pathSketchBox->currentIndex();
        d.pathSegments.clear(); d.pathCurves.clear();
        if (d.sweepPath == 0 && pathChainBox->currentIndex() >= 0 && pathChainBox->currentIndex() < pathComponents.size()) {
            d.pathSegments = pathComponents.at(pathChainBox->currentIndex()).segments;
            d.pathCurves = pathComponents.at(pathChainBox->currentIndex()).curves;
        }
        d.firstBody = d.sweepPathRef.kind >= 0 && ForgeCad::isBodyRef(d.sweepPathRef) ? d.sweepPathRef.index
            : d.sweepPath == 1 && !curves.isEmpty() ? curves.at(qMax(0, curveBox->currentIndex())) : -1;
        d.sweepMode = modeBox->currentIndex();
        d.sweepSurface = surfaceBox->isChecked();
        d.mergeOperation = d.sweepSurface ? 0 : operationBox->currentIndex();
        d.mergeAuto = autoBox->isChecked();
        d.mergeBodies.clear();
        for (int row = 0; row < bodyList->count() && !d.sweepSurface; ++row)
            if (bodyList->item(row)->checkState() == Qt::Checked)
                d.mergeBodies.append(bodyList->item(row)->data(Qt::UserRole).toInt());
        return d;
    };
    const auto refresh = [&] {
        profileBox->setEnabled(profileSource->currentIndex() == 0 && !sketches.isEmpty());
        profileStatus->setText(profileSource->currentIndex() == 1
            ? profileRef.kind >= 0 ? ForgeCad::geometryRefText(profileRef, sketches, bodies) : QStringLiteral("Scegli una sezione piana nella vista.")
            : sketches.value(profileBox->currentIndex()).name);
        form->setRowVisible(profileBox, profileSource->currentIndex() == 0);
        form->setRowVisible(pathSketchBox, pathTypeBox->currentIndex() == 0);
        form->setRowVisible(pathChainBox, pathTypeBox->currentIndex() == 0);
        form->setRowVisible(curveBox, pathTypeBox->currentIndex() == 1);
        pathSketchBox->setEnabled(pathTypeBox->currentIndex() == 0);
        pathChainBox->setEnabled(pathTypeBox->currentIndex() == 0);
        pickPath->setEnabled(true);
        curveBox->setEnabled(pathTypeBox->currentIndex() == 1 && !curves.isEmpty());
        pathStatus->setText(pathTypeBox->currentIndex() == 2
            ? pathRef.kind >= 0 ? ForgeCad::geometryRefText(pathRef, sketches, bodies) : QStringLiteral("Scegli il percorso nella vista.")
            : pathTypeBox->currentIndex() == 1 ? curveBox->currentText() : pathSketchBox->currentText() + QStringLiteral(" / ") + pathChainBox->currentText());
        QVector<GeometryRef> marks;
        const auto definition = current();
        if (definition.sweepProfileRef.kind >= 0) marks.append(definition.sweepProfileRef);
        if (definition.sweepPathRef.kind >= 0) marks.append(definition.sweepPathRef);
        viewport->setReferenceMarks(marks);
        // La superficie non si fonde con i solidi.
        operationBox->setEnabled(!surfaceBox->isChecked());
        const bool merge = !surfaceBox->isChecked() && operationBox->currentIndex() != 0;
        form->setRowVisible(autoBox, merge);
        form->setRowVisible(bodyRow, merge);
        bodyRow->setEnabled(!autoBox->isChecked());
        if (!merge || autoBox->isChecked()) picker.stop();
        scope.request(viewport->withMergeCandidates(current(), replaced >= 0 ? replaced : int(bodies.size())));
    };
    QObject::connect(pathSketchBox, &QComboBox::currentIndexChanged, &dialog, [&](int) { refillPaths(); refresh(); });
    for (QComboBox *box : {profileSource, profileBox, pathTypeBox, pathChainBox, curveBox, modeBox, operationBox})
        QObject::connect(box, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(autoBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(surfaceBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(bodyList, &QListWidget::itemChanged, &dialog, [&] {
        if (!picker.handling()) refresh();
    });
    picker.changed = refresh;
    picker.message = [&](const QString &text, bool) { scope.label->setText(text); };
    int graphicalTarget = 0;  // 1 sezione, 2 percorso
    const auto armPick = [&](int target) {
        picker.stop();
        viewport->cancelReferencePick();
        graphicalTarget = target;
        pickProfile->setChecked(target == 1); pickPath->setChecked(target == 2);
        const QString reason = viewport->beginReferencePick(ForgeCad::DatumRoleCurve | ForgeCad::DatumRoleLine, replaced);
        if (!reason.isEmpty()) { graphicalTarget = 0; pickProfile->setChecked(false); pickPath->setChecked(false); }
        pickStatus->setText(reason.isEmpty() ? target == 1
            ? QStringLiteral("1. Clicca la sezione: schizzo, bordo o curva piana. Poi scegli il percorso.")
            : QStringLiteral("2. Clicca il percorso: bordo di faccia, curva estratta, curva o entità di schizzo 3D.") : reason);
    };
    picker.starting = [&] { graphicalTarget = 0; pickProfile->setChecked(false); pickPath->setChecked(false); };
    QObject::connect(pickProfile, &QPushButton::clicked, &dialog, [&] { armPick(1); });
    QObject::connect(pickPath, &QPushButton::clicked, &dialog, [&] { armPick(2); });
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picker.handle(picked, ref)) return;
        if (!picked) {
            graphicalTarget = 0; pickProfile->setChecked(false); pickPath->setChecked(false);
            return;
        }
        if (!graphicalTarget) return;
        std::vector<ForgeCad::Kernel::PathSegment> pieces; QString reason;
        if (!ForgeCad::geometryRefPath(ref, limit, sketches, bodies, pieces, &reason)) {
            pickStatus->setText(reason);
            viewport->beginReferencePick(ForgeCad::DatumRoleCurve | ForgeCad::DatumRoleLine, replaced);
            return;
        }
        if (graphicalTarget == 1) {
            if (ref.kind == 7) {
                profileRef = {};
                profileBox->setCurrentIndex(ref.index); profileSource->setCurrentIndex(0);
            } else {
                profileRef = ref; profileSource->setCurrentIndex(1);
            }
            refresh(); armPick(2);
        } else {
            if (ref.kind == 7) {
                pathRef = {}; pathTypeBox->setCurrentIndex(0); pathSketchBox->setCurrentIndex(ref.index);
                for (int k = 0; k < pathComponents.size(); ++k)
                    if ((ref.element.kind == 0 ? pathComponents.at(k).segments : pathComponents.at(k).curves).contains(ref.element.element)) {
                        pathChainBox->setCurrentIndex(k); break;
                    }
            } else { pathRef = ref; pathTypeBox->setCurrentIndex(2); }
            graphicalTarget = 0; pickProfile->setChecked(false); pickPath->setChecked(false);
            pickStatus->setText(QStringLiteral("Sezione e percorso selezionati. Regola l'orientamento e verifica l'anteprima."));
            refresh();
        }
    });
    QObject::connect(&dialog, &QDialog::finished, viewport, [viewport] {
        viewport->cancelReferencePick(); viewport->setReferencePickCallback({}); viewport->setReferenceMarks({});
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refresh();
    armPick(1);
    const auto validate = [&] {
        const ExtrusionObject d = current();
        if (d.sweepProfileRef.kind < 0 && (profileSource->currentIndex() == 1 || d.sketchIndex < 0)) return QStringLiteral("Seleziona la sezione nella vista o scegli uno schizzo.");
        if (pathTypeBox->currentIndex() == 2 && d.sweepPathRef.kind < 0) return QStringLiteral("Seleziona il percorso nella vista.");
        if (d.sweepPathRef.kind < 0 && d.sweepPath == 1 && d.firstBody < 0) return QStringLiteral("Nella scena non ci sono curve 3D da usare come percorso.");
        if (d.sweepPathRef.kind < 0 && d.sweepPath == 0 && d.pathSegments.isEmpty() && d.pathCurves.isEmpty()) return QStringLiteral("Seleziona una catena valida nello schizzo del percorso.");
        if (d.sweepPathRef.kind < 0 && d.sweepProfileRef.kind < 0 && d.sweepPath == 0 && d.pathSketch == d.sketchIndex) return QStringLiteral("Profilo e percorso devono stare in schizzi diversi.");
        if (d.mergeOperation != 0 && !d.mergeAuto && d.mergeBodies.isEmpty())
            return QStringLiteral("Spunta almeno un solido (o scegli Automatico).");
        return apply(d);
    };
    if (auto *window = qobject_cast<QMainWindow *>(parent)) return runUntilAppliedModeless(window, viewport, dialog, form, buttons, validate);
    return runUntilApplied(dialog, form, buttons, validate);
}

class LoftSelectionDiagram final : public QWidget {
public:
    explicit LoftSelectionDiagram(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(420, 230);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(QStringLiteral("1. Scegli e ordina le sezioni.  2. Aggiungi guide aperte che attraversano ogni sezione.  3. Regola la continuità alle estremità."));
    }
protected:
    void paintEvent(QPaintEvent *) override {
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
};

// Loft: sezioni ordinate, guide trasversali e condizioni delle estremita',
// con anteprima della stessa definizione che viene salvata nel documento.
// Controlli condivisi dai loft e dalle superfici tra due profili.
class SurfaceEndControls {
public:
    SurfaceEndControls(QDialog &dialog, QFormLayout &form, CadViewport *viewport, int owner, ExtrusionObject &definition)
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
        status_ = new QLabel(&dialog);
        status_->setWordWrap(true);
        form.addRow(status_);
        auto *note = new QLabel(QStringLiteral("La tangenza alle facce richiede che il profilo coincida con il loro bordo. "
                                              "Con tangenza attiva i collegamenti tra i profili diventano curvi. "
                                              "Per G1 alle facce l'influenza regola la lunghezza della tangente."), &dialog);
        note->setWordWrap(true); form.addRow(note);
        sync();
    }
    ~SurfaceEndControls() { viewport_->cancelReferencePick(); viewport_->setReferencePickCallback({}); viewport_->setReferenceMarks({}); }
    std::function<void()> changed;
    void stop() { viewport_->cancelReferencePick(); active_ = -1; }
    void copyTo(ExtrusionObject &d) const {
        d.loftStartContinuity = definition_.loftStartContinuity; d.loftEndContinuity = definition_.loftEndContinuity;
        d.loftStartInfluence = definition_.loftStartInfluence; d.loftEndInfluence = definition_.loftEndInfluence;
        d.loftStartFaces = definition_.loftStartFaces; d.loftEndFaces = definition_.loftEndFaces;
    }
private:
    QVector<GeometryRef> &refs(int side) { return side == 0 ? definition_.loftStartFaces : definition_.loftEndFaces; }
    void sync() {
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
    void start(int side) {
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
    CadViewport *viewport_;
    int owner_, active_ = -1;
    ExtrusionObject &definition_;
    QComboBox *modes_[2]{};
    QDoubleSpinBox *influences_[2]{};
    QLabel *faces_[2]{}, *status_ = nullptr;
    QPushButton *picks_[2]{};
};

static bool loftDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<SketchObject> &sketches = viewport->sketches();
    FunctionDialogPanel dialog(parent);
    dialog.setObjectName(QStringLiteral("loftDialog"));
    dialog.setWindowTitle(title);
    dialog.setMinimumSize(480, 360);
    dialog.setSizeGripEnabled(false);
    dialog.setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, false);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    auto *scroll = new QScrollArea(&dialog);
    scroll->setObjectName(QStringLiteral("loftScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    auto *contents = new QWidget(scroll);
    contents->setObjectName(QStringLiteral("loftContents"));
    contents->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    auto *form = new QFormLayout(contents);
    scroll->setWidget(contents);
    dialogLayout->addWidget(scroll, 1);
    auto *list = new QListWidget(&dialog);
    list->setMinimumHeight(160);
    list->setObjectName(QStringLiteral("loftSections"));
    QVector<GeometryRef> sectionRefs = initial.loftSections;
    QVector<GeometryRef> edgeGuides = initial.loftEdgeGuides;
    connectReferenceList(list, dialog, viewport, [&](QListWidgetItem *item) {
        const int pos = item->data(Qt::UserRole + 1).toInt();
        return pos >= 0 && pos < sectionRefs.size() ? QVector<GeometryRef>{sectionRefs.at(pos)} : QVector<GeometryRef>();
    });
    const auto sameRef = [](const GeometryRef &a,const GeometryRef &b) {
        if(a.kind!=b.kind || a.index!=b.index || a.element!=b.element) return false;
        if(a.kind!=4 && a.kind!=9) return true;
        if(a.point.subshape>=0 || b.point.subshape>=0) return a.point.subshape==b.point.subshape;
        return std::hypot(a.point.x-b.point.x,std::hypot(a.point.y-b.point.y,a.point.z-b.point.z))<1e-9;
    };
    const auto sectionText = [&](const GeometryRef &ref) {
        if(ref.kind==7 && ref.element.kind==-1) return sketches.value(ref.index).name;
        return ForgeCad::geometryRefText(ref,sketches,viewport->extrusions());
    };
    const auto appendSection = [&](const GeometryRef &ref,bool checked) {
        int pos=-1;
        for(int k=0;k<sectionRefs.size();++k) if(sameRef(sectionRefs.at(k),ref)) {pos=k;break;}
        if(pos<0) {pos=sectionRefs.size();sectionRefs.append(ref);}
        auto *item=new QListWidgetItem(sectionText(ref),list);
        item->setData(Qt::UserRole,ref.kind==7 && ref.element.kind==-1?ref.index:-1);
        item->setData(Qt::UserRole+1,pos);
        item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
        item->setCheckState(checked?Qt::Checked:Qt::Unchecked);
        return item;
    };
    for(const GeometryRef &ref : initial.loftSections) appendSection(ref,true);
    QVector<int> order = initial.loftSketches;
    for (int index = 0; index < sketches.size(); ++index)
        if (!order.contains(index)) order.append(index);
    for (int index : order) {
        if (index < 0 || index >= sketches.size()) continue;
        GeometryRef ref; ref.kind=7; ref.index=index; ref.element={-1,-1,-1};
        bool present=false;
        for(const auto &existing: initial.loftSections) if(sameRef(existing,ref)) present=true;
        if(!present) appendSection(ref,initial.loftSections.isEmpty() && initial.loftSketches.contains(index));
    }
    auto *removeSection=new QPushButton(QStringLiteral("Rimuovi sezione"),&dialog);
    removeSection->setObjectName(QStringLiteral("loftRemoveSection"));
    auto *moveRow = new QWidget(&dialog);
    auto *moveLayout = new QHBoxLayout(moveRow);
    moveLayout->setContentsMargins(0, 0, 0, 0);
    auto *up = new QPushButton(QStringLiteral("↑ Su"), moveRow), *down = new QPushButton(QStringLiteral("↓ Giu'"), moveRow);
    auto *pickSections = new QPushButton(QStringLiteral("Scegli sezioni nella vista"), moveRow);
    moveLayout->addWidget(up);
    moveLayout->addWidget(down);
    moveLayout->addWidget(pickSections);
    moveLayout->addWidget(removeSection);
    pickSections->setObjectName(QStringLiteral("loftPickSections"));
    moveLayout->addStretch(1);
    auto *ruledBox = new QCheckBox(QStringLiteral("Rigato (superfici rigate tra sezioni consecutive)"), &dialog);
    ruledBox->setChecked(initial.loftRuled);
    auto *surfaceBox = new QCheckBox(QStringLiteral("Solo superficie (senza coperchi)"), &dialog);
    surfaceBox->setChecked(initial.loftSurface);
    surfaceBox->setToolTip(QStringLiteral("Una lamina: con sezioni chiuse il tubo che passa per le sezioni, aperto alle estremita'"));
    auto *guides = new QListWidget(&dialog);
    guides->setObjectName(QStringLiteral("loftGuides"));
    guides->setMinimumHeight(160);
    QVector<SketchPathRef> partialSelections = initial.loftGuidePaths;
    connectReferenceList(guides, dialog, viewport, [&](QListWidgetItem *item) {
        const int kind = item->data(Qt::UserRole).toInt(), index = item->data(Qt::UserRole + 1).toInt();
        if (kind == 2 && index >= 0 && index < edgeGuides.size()) return QVector<GeometryRef>{edgeGuides.at(index)};
        QVector<GeometryRef> refs;
        if (kind == 0 && index >= 0 && index < sketches.size()) {
            GeometryRef ref; ref.kind = 7; ref.index = index; ref.element = {-1, -1, -1}; refs.append(ref);
        }
        if (kind == 1 && index >= 0 && index < partialSelections.size()) {
            const auto &path = partialSelections.at(index);
            for (int segment : path.segments) { GeometryRef ref; ref.kind = 7; ref.index = path.sketch; ref.element = {0, segment, -1}; refs.append(ref); }
            for (int curve : path.curves) { GeometryRef ref; ref.kind = 7; ref.index = path.sketch; ref.element = {1, curve, -1}; refs.append(ref); }
        }
        return refs;
    });
    const auto appendGuide = [&](int kind, int index, bool checked) {
        const QSignalBlocker block(guides);
        QString text;
        if (kind == 0) text = sketches.value(index).name;
        else if (kind == 1) {
            const auto &path = partialSelections.at(index);
            text = QStringLiteral("%1 — selezione parziale (%2 entità)")
                .arg(sketches.value(path.sketch).name).arg(path.segments.size() + path.curves.size());
        } else text = ForgeCad::geometryRefText(edgeGuides.at(index), sketches, viewport->extrusions());
        auto *item = new QListWidgetItem(text, guides);
        item->setData(Qt::UserRole, kind);
        item->setData(Qt::UserRole + 1, index);
        item->setData(Qt::UserRole + 3, initial.loftGuideContinuity);
        item->setData(Qt::UserRole + 4, initial.loftGuideInfluence);
        item->setData(Qt::UserRole + 5, text);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        item->setText(QStringLiteral("%1 — G%2 / %3 %").arg(text).arg(initial.loftGuideContinuity)
                      .arg(100 * initial.loftGuideInfluence, 0, 'g', 3));
        return item;
    };
    const auto guideLabel = [&](QListWidgetItem *item) {
        const QSignalBlocker blocker(guides);
        item->setText(QStringLiteral("%1 — G%2 / %3 %").arg(item->data(Qt::UserRole + 5).toString())
                      .arg(item->data(Qt::UserRole + 3).toInt()).arg(100 * item->data(Qt::UserRole + 4).toDouble(), 0, 'g', 3));
    };
    for (int index : ForgeCad::loftGuideSequence(initial)) {
        QListWidgetItem *item;
        if (index < initial.loftGuides.size()) item = appendGuide(0, initial.loftGuides.at(index), true);
        else if (index < initial.loftGuides.size() + partialSelections.size())
            item = appendGuide(1, index - initial.loftGuides.size(), true);
        else item = appendGuide(2, index - initial.loftGuides.size() - partialSelections.size(), true);
        item->setData(Qt::UserRole + 3, initial.loftGuideContinuities.value(index, initial.loftGuideContinuity));
        item->setData(Qt::UserRole + 4, initial.loftGuideInfluences.value(index, initial.loftGuideInfluence));
        guideLabel(item);
    }
    for (int index = 0; index < sketches.size(); ++index)
        if (!initial.loftGuides.contains(index)) appendGuide(0, index, false);
    auto *guideButtons = new QWidget(&dialog);
    auto *guideButtonsLayout = new QHBoxLayout(guideButtons);
    guideButtonsLayout->setContentsMargins(0,0,0,0);
    auto *guideUp = new QPushButton(QStringLiteral("↑ Su"), guideButtons);
    auto *guideDown = new QPushButton(QStringLiteral("↓ Giu'"), guideButtons);
    auto *removeGuide = new QPushButton(QStringLiteral("Rimuovi guida"), guideButtons);
    guideUp->setObjectName(QStringLiteral("loftGuideUp"));
    guideDown->setObjectName(QStringLiteral("loftGuideDown"));
    removeGuide->setObjectName(QStringLiteral("loftRemoveGuide"));
    guideButtonsLayout->addWidget(guideUp); guideButtonsLayout->addWidget(guideDown);
    guideButtonsLayout->addWidget(removeGuide); guideButtonsLayout->addStretch(1);
    auto *pickGuides = new QPushButton(QStringLiteral("Scegli guide nella vista"), &dialog);
    pickGuides->setObjectName(QStringLiteral("loftPickGuides"));
    auto *pickStatus = new QLabel(QStringLiteral("Spunta gli schizzi interi oppure scegli bordi e curve nella vista; un secondo clic li toglie."), &dialog);
    pickStatus->setWordWrap(true);
    const auto influence = [&](double value) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setRange(0.0, 100.0);
        spin->setSuffix(QStringLiteral(" %"));
        spin->setDecimals(0);
        spin->setValue(100.0 * value);
        return spin;
    };
    QDoubleSpinBox *guideInfluence = influence(initial.loftGuideInfluence);
    guideInfluence->setObjectName(QStringLiteral("loftGuideInfluence"));
    auto *guideContinuity = new QComboBox(&dialog);
    guideContinuity->setObjectName(QStringLiteral("loftGuideContinuity"));
    guideContinuity->addItems({QStringLiteral("G0 — solo attraversamento"), QStringLiteral("G1 — segue la tangente (consigliato)"),
                               QStringLiteral("G2 — segue tangente e curvatura")});
    guideContinuity->setCurrentIndex(qBound(0, initial.loftGuideContinuity, 2));
    auto *help = new QLabel(QStringLiteral("Le sezioni vanno ordinate. Ogni guida deve essere una catena aperta e attraversare una sola volta ogni sezione; "
                                           "non può essere usata anche come sezione. Seleziona una guida nell'elenco per regolarne continuita' e influenza. Per le guide, G1 segue la tangente senza trasferire la curvatura che può creare flessi; G2 la trasferisce."),
                            &dialog);
    help->setWordWrap(true);
    help->setMaximumWidth(570);
    form->addRow(new LoftSelectionDiagram(&dialog));
    form->addRow(QStringLiteral("Sezioni:"), list);
    form->addRow(QString(), moveRow);
    form->addRow(QStringLiteral("Curve guida:"), guides);
    form->addRow(QString(), pickGuides);
    form->addRow(QString(), guideButtons);
    form->addRow(QString(), pickStatus);
    form->addRow(QStringLiteral("Continuita' guida selezionata:"), guideContinuity);
    form->addRow(QStringLiteral("Influenza guida selezionata:"), guideInfluence);
    ExtrusionObject endpointDefinition = initial;
    SurfaceEndControls endControls(dialog, *form, viewport, replaced, endpointDefinition);
    form->addRow(QString(), ruledBox);
    form->addRow(QString(), surfaceBox);
    form->addRow(help);
    auto *footer = new QWidget(&dialog);
    footer->setObjectName(QStringLiteral("loftFooter"));
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    const PreviewScope scope(viewport, dialog, form, replaced, footerLayout);
    const auto current = [&] {
        ExtrusionObject d = initial;
        d.operation = -1;
        d.feature = BodyFeature::Loft;
        d.loftSketches.clear();
        d.loftSections.clear();
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->checkState() == Qt::Checked) {
                const GeometryRef &ref=sectionRefs.at(list->item(row)->data(Qt::UserRole+1).toInt());
                d.loftSections.append(ref);
                if(ref.kind==7 && ref.element.kind==-1) d.loftSketches.append(ref.index);
            }
        d.loftEdgeGuides.clear(); d.loftGuides.clear(); d.loftGuidePaths.clear(); d.loftGuideOrder.clear();
        QVector<QPair<int,int>> selected;
        QVector<int> modes; QVector<double> influences;
        for (int row = 0; row < guides->count(); ++row) {
            const auto *item = guides->item(row);
            if (item->checkState() != Qt::Checked) continue;
            modes.append(item->data(Qt::UserRole + 3).toInt());
            influences.append(item->data(Qt::UserRole + 4).toDouble());
            const int kind = item->data(Qt::UserRole).toInt(), index = item->data(Qt::UserRole+1).toInt();
            if (kind == 0) { selected.append({kind,d.loftGuides.size()}); d.loftGuides.append(index); }
            else if (kind == 1) { selected.append({kind,d.loftGuidePaths.size()}); d.loftGuidePaths.append(partialSelections.at(index)); }
            else { selected.append({kind,d.loftEdgeGuides.size()}); d.loftEdgeGuides.append(edgeGuides.at(index)); }
        }
        d.loftGuideContinuities.resize(selected.size()); d.loftGuideInfluences.resize(selected.size());
        for (int i = 0; i < selected.size(); ++i) {
            const auto &entry = selected[i];
            const int index = entry.second + (entry.first > 0 ? d.loftGuides.size() : 0)
                + (entry.first > 1 ? d.loftGuidePaths.size() : 0);
            d.loftGuideOrder.append(index);
            d.loftGuideContinuities[index] = modes[i]; d.loftGuideInfluences[index] = influences[i];
        }
        d.loftRuled = ruledBox->isChecked();
        d.loftSurface = surfaceBox->isChecked();
        endControls.copyTo(d);
        d.loftGuideContinuity = initial.loftGuideContinuity;
        d.loftGuideInfluence = initial.loftGuideInfluence;
        d.sketchIndex = d.loftSketches.value(0, -1);
        d.plane = sketches.value(d.sketchIndex).plane;
        return d;
    };
    const auto refresh = [&] {
        const ExtrusionObject d = current();
        if (d.loftSections.size() >= 2) scope.request(d);
        else viewport->clearPreview();
    };
    const auto syncGuideControls = [&] {
        const auto *item = guides->currentItem();
        const QSignalBlocker blockMode(guideContinuity), blockInfluence(guideInfluence);
        const bool editable = item && item->checkState() == Qt::Checked && !ruledBox->isChecked();
        guideContinuity->setEnabled(editable); guideInfluence->setEnabled(editable);
        if (item) {
            guideContinuity->setCurrentIndex(item->data(Qt::UserRole + 3).toInt());
            guideInfluence->setValue(100 * item->data(Qt::UserRole + 4).toDouble());
        }
    };
    const auto changeGuideCondition = [&] {
        if (auto *item = guides->currentItem()) {
            { const QSignalBlocker block(guides);
              item->setData(Qt::UserRole + 3, guideContinuity->currentIndex());
              item->setData(Qt::UserRole + 4, guideInfluence->value() / 100.0); }
            guideLabel(item); refresh();
        }
    };
    QObject::connect(guides, &QListWidget::currentRowChanged, &dialog, syncGuideControls);
    QObject::connect(guides, &QListWidget::itemChanged, &dialog, syncGuideControls);
    QObject::connect(ruledBox, &QCheckBox::toggled, &dialog, syncGuideControls);
    if (guides->count()) guides->setCurrentRow(0);
    syncGuideControls();
    endControls.changed = refresh;
    const auto move = [&](int step) {
        const int row = list->currentRow();
        if (row < 0 || row + step < 0 || row + step >= list->count()) return;
        QListWidgetItem *item = list->takeItem(row);
        list->insertItem(row + step, item);
        list->setCurrentRow(row + step);
        refresh();
    };
    QObject::connect(up, &QPushButton::clicked, &dialog, [&] { move(-1); });
    QObject::connect(down, &QPushButton::clicked, &dialog, [&] { move(1); });
    const auto moveGuide = [&](int step) {
        const int row = guides->currentRow();
        if (row < 0 || row + step < 0 || row + step >= guides->count()) return;
        auto *item = guides->takeItem(row);
        guides->insertItem(row + step, item); guides->setCurrentRow(row + step); refresh();
    };
    QObject::connect(guideUp, &QPushButton::clicked, &dialog, [&] { moveGuide(-1); });
    QObject::connect(guideDown, &QPushButton::clicked, &dialog, [&] { moveGuide(1); });
    QObject::connect(removeGuide, &QPushButton::clicked, &dialog, [&] {
        if (guides->currentItem()) guides->currentItem()->setCheckState(Qt::Unchecked);
    });
    QObject::connect(removeSection,&QPushButton::clicked,&dialog,[&] {
        if(list->currentRow()<0) return;
        delete list->takeItem(list->currentRow()); refresh();
    });
    int graphicalTarget = 0;  // 1 sezioni, 2 guide
    const auto armGraphicalPick = [&](int target) {
        endControls.stop();
        graphicalTarget = target;
        pickSections->setChecked(target == 1); pickGuides->setChecked(target == 2);
        viewport->cancelReferencePick();
        viewport->setReferencePickCallback([&](bool picked,GeometryRef ref) {
            if(!picked) {graphicalTarget=0;pickSections->setChecked(false);pickGuides->setChecked(false);return;}
            std::vector<ForgeCad::Kernel::PathSegment> path; QString reason;
            const int owner=replaced<0?viewport->extrusions().size():replaced;
            if(!ForgeCad::geometryRefPath(ref,owner,sketches,viewport->extrusions(),path,&reason)) {
                pickStatus->setText(reason);
            } else if(graphicalTarget==1) {
                QListWidgetItem *item=nullptr;
                for(int row=0;row<list->count();++row)
                    if(sameRef(sectionRefs.at(list->item(row)->data(Qt::UserRole+1).toInt()),ref)) {item=list->item(row);break;}
                if(item) item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);
                else item=appendSection(ref,true);
                list->setCurrentItem(item);
                refresh();
            } else if(graphicalTarget==2) {
                int existing=-1;
                for(int k=0;k<edgeGuides.size();++k) if(sameRef(edgeGuides.at(k),ref)) {existing=k;break;}
                if(existing<0) { existing=edgeGuides.size(); edgeGuides.append(ref); }
                QListWidgetItem *item=nullptr;
                for(int row=0;row<guides->count();++row)
                    if(guides->item(row)->data(Qt::UserRole).toInt()==2 && guides->item(row)->data(Qt::UserRole+1).toInt()==existing) {item=guides->item(row);break;}
                if(item) item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);
                else item=appendGuide(2,existing,true);
                guides->setCurrentItem(item); refresh();
            }
            if(graphicalTarget) viewport->beginReferencePick(ForgeCad::DatumRoleCurve|ForgeCad::DatumRoleLine,replaced);
        });
        const QString reason=viewport->beginReferencePick(ForgeCad::DatumRoleCurve|ForgeCad::DatumRoleLine,replaced);
        pickStatus->setText(reason.isEmpty()?QStringLiteral("Clicca un bordo o una curva; Esc termina la scelta. Gli schizzi interi si spuntano nell’elenco."):reason);
    };
    for(const QString &name : {QStringLiteral("surfaceStartFacesPick"),QStringLiteral("surfaceEndFacesPick")})
        QObject::connect(dialog.findChild<QPushButton *>(name),&QPushButton::clicked,&dialog,[&] {
            graphicalTarget=0; pickSections->setChecked(false);pickGuides->setChecked(false);
        });
    pickSections->setCheckable(true); pickGuides->setCheckable(true);
    QObject::connect(pickSections, &QPushButton::clicked, &dialog, [&] { armGraphicalPick(1); });
    QObject::connect(pickGuides, &QPushButton::clicked, &dialog, [&] { armGraphicalPick(2); });
    viewport->setSketchPickCallback([&](int sketch) {
        if (graphicalTarget != 1) return;
        QListWidget *target = list;
        for (int row = 0; row < target->count(); ++row) {
            QListWidgetItem *item = target->item(row);
            if (item->data(Qt::UserRole).toInt() != sketch) continue;
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            target->setCurrentRow(row);
            break;
        }
    });
    viewport->setSketchEntityPickCallback([&](int sketch, int kind, int entity) {
        if (graphicalTarget != 2) return;
        QVector<SketchPathRef> components = ForgeCad::sketchPathComponents(sketches.at(sketch));
        for (SketchPathRef &path : components) path.sketch = sketch;
        for (const SketchPathRef &path : components) {
            if (!(kind == 0 ? path.segments : path.curves).contains(entity)) continue;
            int existing = partialSelections.indexOf(path);
            if (existing < 0) { existing=partialSelections.size(); partialSelections.append(path); }
            QListWidgetItem *item=nullptr;
            for (int row=0;row<guides->count();++row) {
                auto *candidate=guides->item(row);
                if(candidate->data(Qt::UserRole).toInt()==1 && candidate->data(Qt::UserRole+1).toInt()==existing) item=candidate;
            }
            if(item) item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);
            else item=appendGuide(1,existing,true);
            if(item->checkState()==Qt::Checked)
                for(int row=0;row<guides->count();++row)
                    if(guides->item(row)->data(Qt::UserRole).toInt()==0 && guides->item(row)->data(Qt::UserRole+1).toInt()==sketch)
                        guides->item(row)->setCheckState(Qt::Unchecked);
            guides->setCurrentItem(item); refresh();
            pickStatus->setText(QStringLiteral("Elenco delle guide aggiornato."));
            break;
        }
    });
    QObject::connect(&dialog, &QDialog::finished, viewport, [viewport] {
        viewport->cancelReferencePick(); viewport->setReferencePickCallback({});
        viewport->setReferenceMarks({});
        viewport->setSketchPickCallback({}); viewport->setSketchEntityPickCallback({});
    });
    QObject::connect(list, &QListWidget::itemChanged, &dialog, refresh);
    QObject::connect(guides, &QListWidget::itemChanged, &dialog, refresh);
    QObject::connect(ruledBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(surfaceBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(guideContinuity, &QComboBox::currentIndexChanged, &dialog, changeGuideCondition);
    QObject::connect(guideInfluence, &QDoubleSpinBox::valueChanged, &dialog, changeGuideCondition);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, footer);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    footerLayout->addWidget(buttons);
    dialogLayout->addWidget(footer);
    QTimer::singleShot(0, &dialog, [&dialog, parent] {
        if (dialog.isEmbedded()) {
            dialog.placeAtLeft();
            return;
        }
        const QPoint parentCentre = parent ? parent->mapToGlobal(parent->rect().center()) : QCursor::pos();
        QScreen *screen = QGuiApplication::screenAt(parentCentre);
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        const QRect available = screen->availableGeometry();
        const int margin = 16;
        dialog.resize(qMin(dialog.width(), available.width() - 2 * margin),
                      qMin(dialog.height(), available.height() - 2 * margin));
        const int x = available.left() + margin;
        const int y = qBound(available.top() + margin, parentCentre.y() - dialog.height() / 2,
                             available.bottom() - dialog.height() - margin + 1);
        dialog.move(x, y);
    });
    scroll->ensureVisible(0, 0);
    refresh();
    const auto validate = [&] {
        const ExtrusionObject d = current();
        if (d.loftSections.size() < 2) return QStringLiteral("Scegli almeno due sezioni (schizzi o bordi).");
        for(const auto &section : d.loftSections)
            for(const auto &guide : d.loftEdgeGuides)
                if(sameRef(section,guide) || (section.kind==7 && section.element.kind==-1 && guide.kind==7 && section.index==guide.index))
                    return QStringLiteral("Una curva non può essere insieme sezione e guida.");
        for (int sketch : d.loftGuides)
            if (d.loftSketches.contains(sketch))
                return QStringLiteral("Uno schizzo non può essere insieme sezione e curva guida.");
        for (const SketchPathRef &path : d.loftGuidePaths)
            if (d.loftSketches.contains(path.sketch)) return QStringLiteral("Uno schizzo non può essere insieme sezione e curva guida.");
        return apply(d);
    };
    if (auto *window = qobject_cast<QMainWindow *>(parent)) return runUntilAppliedModeless(window, viewport, dialog, form, buttons, validate);
    return runUntilApplied(dialog, form, buttons, validate);
}

// Proprieta' di massa del corpo scelto (o di tutti i solidi visibili):
// volume, superficie, massa con la densita' del materiale, baricentro,
// tensore d'inerzia al baricentro e all'origine, momenti e assi principali.
// Blocco dell'interfaccia durante una finestra "leggera" (non modale, per
// lasciare la vista attiva alla scelta dei riferimenti): menu, barre, albero e
// tutte le azioni (anche le scorciatoie) spenti; la vista senza menu contestuali.

static bool runModeless(QMainWindow *window, CadViewport *viewport, QDialog &dialog) {
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

// Estrusione o rivoluzione dallo schizzo: finche' la finestra e' aperta la
// vista si puo' ruotare (per vedere il verso), poi torna quella dello schizzo.
struct SketchViewUnlock {
    CadViewport *viewport;
    explicit SketchViewUnlock(CadViewport *target) : viewport(target) { viewport->setSketchViewUnlocked(true); }
    ~SketchViewUnlock() { viewport->setSketchViewUnlocked(false); }
    SketchViewUnlock(const SketchViewUnlock &) = delete;
    SketchViewUnlock &operator=(const SketchViewUnlock &) = delete;
};

// Piano di costruzione (nuovo, o al posto del corpo `replaced`): tipo,
// riferimenti scelti nella vista (la finestra resta aperta e la vista attiva:
// un clic per riferimento, si passa da solo a quello che manca; il pulsante di
// un riferimento lo fa scegliere di nuovo), distanza o angolo, normale
// invertita, misura a video. Anteprima in arancio. La conferma chiama `apply`
// (l'errore resta nella finestra).
static bool datumDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ForgeCad::DatumMode> &modes = ForgeCad::datumModes();
    ExtrusionObject definition = initial;
    definition.feature = BodyFeature::DatumPlane;
    definition.operation = -1;
    DatumParameters &d = definition.datum;
    d.mode = qBound(0, d.mode, int(modes.size()) - 1);
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *modeBox = new QComboBox(&dialog);
    for (const ForgeCad::DatumMode &mode : modes) modeBox->addItem(mode.name);
    modeBox->setCurrentIndex(d.mode);
    form->addRow(QStringLiteral("Tipo:"), modeBox);
    QVector<QLabel *> refLabels;
    QVector<QPushButton *> refButtons;
    for (int k = 0; k < 3; ++k) {
        auto *label = new QLabel(&dialog);
        auto *button = new QPushButton(&dialog);
        button->setToolTip(QStringLiteral("Scegli di nuovo nella vista"));
        form->addRow(label, button);
        refLabels.append(label);
        refButtons.append(button);
    }
    const auto spin = [&dialog](double value, double lo, double hi, const QString &suffix) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setDecimals(6);
        box->setRange(lo, hi);
        box->setValue(value);
        box->setSuffix(suffix);
        return box;
    };
    QDoubleSpinBox *distanceBox = spin(d.distance, -1e6, 1e6, QString());
    QDoubleSpinBox *angleBox = spin(d.angle, -360.0, 360.0, QStringLiteral(" °"));
    QDoubleSpinBox *sizeBox = spin(d.size, 0.0, 1e6, QString());
    sizeBox->setSpecialValueText(QStringLiteral("automatica"));
    auto *onCurveBox = new QCheckBox(QStringLiteral("Il piano passa per il punto della curva piu' vicino"), &dialog);
    onCurveBox->setChecked(d.onCurve);
    auto *flipBox = new QCheckBox(QStringLiteral("Inverti la normale"), &dialog);
    flipBox->setChecked(d.flip);
    form->addRow(QStringLiteral("Distanza:"), distanceBox);
    form->addRow(QStringLiteral("Angolo:"), angleBox);
    form->addRow(QString(), onCurveBox);
    form->addRow(QString(), flipBox);
    form->addRow(QStringLiteral("Misura a video (mezzo lato):"), sizeBox);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(280);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);

    int active = -1;  // riferimento in scelta nella vista
    const auto roles = [&] { return modes.at(d.mode).roles; };
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    const auto refresh = [&] {
        const ForgeCad::DatumMode &mode = modes.at(d.mode);
        for (int k = 0; k < 3; ++k) {
            const bool used = k < mode.roles.size();
            form->setRowVisible(refButtons.at(k), used);
            if (!used) continue;
            refLabels.at(k)->setText(mode.labels.at(k));
            const GeometryRef &ref = d.refs.at(k);
            QString text = ref.kind >= 0 ? ForgeCad::geometryRefText(ref, viewport->sketches(), viewport->extrusions()) : QStringLiteral("(da scegliere)");
            if (k == active) text = QStringLiteral("▶ ") + text + QStringLiteral("  - clicca nella vista");
            refButtons.at(k)->setText(text);
        }
        form->setRowVisible(distanceBox, mode.distance);
        form->setRowVisible(angleBox, mode.angle);
        form->setRowVisible(onCurveBox, mode.onCurve);
        QVector<GeometryRef> marks;
        bool complete = true;
        for (int k = 0; k < mode.roles.size(); ++k) {
            if (d.refs.at(k).kind >= 0) marks.append(d.refs.at(k));
            else complete = false;
        }
        viewport->setReferenceMarks(marks);
        if (!complete) {
            viewport->setDatumPreview(false, {}, d.size, replaced);
            setStatus(active >= 0 ? QStringLiteral("Clicca nella vista: %1 (Esc nella vista: annulla la scelta; trascinando lontano dagli oggetti la vista ruota)")
                                        .arg(mode.labels.at(active).chopped(1).toLower())
                                  : QStringLiteral("Scegli i riferimenti che mancano (pulsanti qui sopra)."),
                      false);
            return;
        }
        SketchFrame frame;
        QString error;
        const bool ok = viewport->previewDatum(d, replaced, frame, &error);
        viewport->setDatumPreview(ok, frame, d.size, replaced);
        if (ok)
            setStatus(QStringLiteral("Piano per (%1, %2, %3), normale (%4, %5, %6).")
                          .arg(frame.origin[0], 0, 'g', 6).arg(frame.origin[1], 0, 'g', 6).arg(frame.origin[2], 0, 'g', 6)
                          .arg(frame.normal[0], 0, 'g', 6).arg(frame.normal[1], 0, 'g', 6).arg(frame.normal[2], 0, 'g', 6),
                      false);
        else setStatus(error, true);
    };
    const auto pick = [&](int slot) {
        active = slot;
        if (slot >= 0) {
            const QString error = viewport->beginReferencePick(roles().at(slot), replaced);
            if (!error.isEmpty()) {
                active = -1;
                setStatus(error, true);
                return;
            }
        } else {
            viewport->cancelReferencePick();
        }
        refresh();
    };
    const auto pickMissing = [&] {
        for (int k = 0; k < roles().size(); ++k)
            if (d.refs.at(k).kind < 0) return pick(k);
        pick(-1);
    };
    // I riferimenti che non valgono per il tipo nuovo si tolgono.
    const auto fitRefs = [&] {
        const QVector<int> r = roles();
        d.refs.resize(r.size());
        for (int k = 0; k < r.size(); ++k)
            if (d.refs.at(k).kind >= 0 && !(ForgeCad::geometryRefRoles(d.refs.at(k), viewport->sketches()) & r.at(k))) d.refs[k] = GeometryRef();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picked && active >= 0 && active < d.refs.size()) {
            d.refs[active] = ref;
            active = -1;
            pickMissing();
        } else {
            active = -1;
            refresh();
        }
    });
    for (int k = 0; k < 3; ++k) QObject::connect(refButtons.at(k), &QPushButton::clicked, &dialog, [&, k] { pick(k); });
    QObject::connect(modeBox, &QComboBox::currentIndexChanged, &dialog, [&](int mode) {
        d.mode = mode;
        fitRefs();
        pickMissing();
    });
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, [&](double v) { d.distance = v; refresh(); });
    QObject::connect(angleBox, &QDoubleSpinBox::valueChanged, &dialog, [&](double v) { d.angle = v; refresh(); });
    QObject::connect(sizeBox, &QDoubleSpinBox::valueChanged, &dialog, [&](double v) { d.size = v; refresh(); });
    QObject::connect(onCurveBox, &QCheckBox::toggled, &dialog, [&](bool on) { d.onCurve = on; refresh(); });
    QObject::connect(flipBox, &QCheckBox::toggled, &dialog, [&](bool on) { d.flip = on; refresh(); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        for (int k = 0; k < roles().size(); ++k)
            if (d.refs.at(k).kind < 0) return pick(k);
        const QString error = apply(definition);
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    fitRefs();
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        pickMissing();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->clearDatumPreview();
    return accepted;
}

// Ripetizione delle entita' selezionate nello schizzo attivo: lineare (una o
// due direzioni), circolare o specchio, con le copie in anteprima. Direzioni e
// retta dello specchio: gli assi del piano, un angolo o un segmento selezionato
// (che non si ripete); centro: origine, punto scelto con Ctrl+clic, centro di
// un cerchio o arco selezionato, o coordinate.
static bool sketchPatternDialog(QWidget *parent, CadViewport *viewport, int kind) {
    const SketchObject *sketch = viewport->activeSketchObject();
    const QString titles[3] = {QStringLiteral("Ripetizione lineare nello schizzo"), QStringLiteral("Ripetizione circolare nello schizzo"),
                               QStringLiteral("Specchio nello schizzo")};
    if (!sketch) {
        QMessageBox::information(parent, titles[kind], QStringLiteral("Entra in modalita' schizzo e seleziona le entita' da ripetere."));
        return false;
    }
    const QVector<SketchElementSelection> selection = viewport->sketchSelection();
    if (selection.isEmpty()) {
        QMessageBox::information(parent, titles[kind], QStringLiteral("Seleziona prima le entita' da ripetere (clic, Maiusc+clic per aggiungerne)."));
        return false;
    }
    QVector<int> segments;  // segmenti selezionati: possibili riferimenti
    QVector<QPointF> centers;
    QStringList centerNames;
    for (const SketchElementSelection &element : selection) {
        if (element.kind == 0) segments.append(element.index);
        if (element.kind == 1 && element.index < sketch->curves.size()) {
            const CurveObject &curve = sketch->curves.at(element.index);
            if ((curve.tool == DrawingTool::Circle || curve.tool == DrawingTool::Arc || curve.tool == DrawingTool::Polygon
                 || curve.tool == DrawingTool::Ellipse) && !curve.controlPoints.isEmpty()) {
                centers.append(curve.controlPoints.first());
                centerNames.append(QStringLiteral("Centro della curva %1").arg(element.index + 1));
            }
        }
    }
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(titles[kind]);
    auto *form = dialog.createScrollableForm();
    form->addRow(wrappedNote(QStringLiteral("%1 entita' selezionate. Un segmento scelto come riferimento non si ripete.").arg(selection.size()), &dialog));
    const auto spin = [&dialog](double value, double lo, double hi, const QString &suffix = QString()) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setDecimals(6);
        box->setRange(lo, hi);
        box->setValue(value);
        box->setSuffix(suffix);
        return box;
    };
    const auto countSpin = [&dialog](int value) {
        auto *box = new QSpinBox(&dialog);
        box->setRange(1, 1000);
        box->setValue(value);
        return box;
    };
    // Direzione: asse X, asse Y, angolo, o lungo un segmento selezionato.
    struct DirectionRow {
        QComboBox *box = nullptr;
        QDoubleSpinBox *angle = nullptr;
        QCheckBox *flip = nullptr;
    };
    const auto directionRow = [&](const QString &label, int initial) {
        DirectionRow row;
        row.box = new QComboBox(&dialog);
        row.box->addItems({QStringLiteral("Asse X del piano"), QStringLiteral("Asse Y del piano"), QStringLiteral("Angolo dall'asse X")});
        for (int segment : segments) row.box->addItem(QStringLiteral("Lungo il segmento %1").arg(segment + 1));
        row.box->setCurrentIndex(initial);
        row.angle = spin(0.0, -360.0, 360.0, QStringLiteral(" °"));
        row.flip = new QCheckBox(QStringLiteral("Inverti"), &dialog);
        auto *line = new QWidget(&dialog);
        auto *layout = new QHBoxLayout(line);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(row.box, 1);
        layout->addWidget(row.angle);
        layout->addWidget(row.flip);
        form->addRow(label, line);
        return row;
    };
    // Direzione e segmento di riferimento (-1 nessuno); `ref` la retta dello schizzo da cui viene (asse o segmento).
    const auto direction = [&](const DirectionRow &row, int &reference, ConstraintRef *ref = nullptr) {
        const int index = row.box->currentIndex();
        QPointF d(1.0, 0.0);
        reference = -1;
        if (ref) *ref = index == 0 ? ConstraintRef{2, 1, -1} : index == 1 ? ConstraintRef{2, 2, -1} : index >= 3 ? ConstraintRef{0, segments.at(index - 3), -1} : ConstraintRef();
        if (index == 1) d = QPointF(0.0, 1.0);
        else if (index == 2) d = QPointF(std::cos(row.angle->value() * M_PI / 180.0), std::sin(row.angle->value() * M_PI / 180.0));
        else if (index >= 3) {
            reference = segments.at(index - 3);
            SketchSegment segment;
            if (viewport->activeSegment(reference, segment)) d = segment.second - segment.first;
        }
        return row.flip->isChecked() ? -d : d;
    };
    DirectionRow first, second;
    QSpinBox *count = nullptr, *count2 = nullptr;
    QDoubleSpinBox *spacing = nullptr, *spacing2 = nullptr, *angle = nullptr;
    QCheckBox *useSecond = nullptr, *spread = nullptr;
    QComboBox *centerBox = nullptr, *axisBox = nullptr;
    QDoubleSpinBox *centerX = nullptr, *centerY = nullptr;
    const QVector<QPointF> points = viewport->selectedSketchPoints();
    const QVector<ConstraintRef> pointRefs = viewport->selectedSketchPointRefs();
    QVector<ConstraintRef> centerRefs;
    for (const SketchElementSelection &element : selection)
        if (element.kind == 1 && element.index < sketch->curves.size()) {
            const CurveObject &curve = sketch->curves.at(element.index);
            if ((curve.tool == DrawingTool::Circle || curve.tool == DrawingTool::Arc || curve.tool == DrawingTool::Polygon
                 || curve.tool == DrawingTool::Ellipse) && !curve.controlPoints.isEmpty())
                centerRefs.append({1, element.index, 0});
        }
    if (kind == 0) {
        first = directionRow(QStringLiteral("Direzione:"), 0);
        count = countSpin(3);
        spacing = spin(10.0, -1e6, 1e6);
        form->addRow(QStringLiteral("Istanze:"), count);
        form->addRow(QStringLiteral("Passo:"), spacing);
        useSecond = new QCheckBox(QStringLiteral("Seconda direzione (griglia)"), &dialog);
        form->addRow(QString(), useSecond);
        second = directionRow(QStringLiteral("Direzione 2:"), 1);
        count2 = countSpin(2);
        spacing2 = spin(10.0, -1e6, 1e6);
        form->addRow(QStringLiteral("Istanze 2:"), count2);
        form->addRow(QStringLiteral("Passo 2:"), spacing2);
    } else if (kind == 1) {
        centerBox = new QComboBox(&dialog);
        centerBox->addItem(QStringLiteral("Origine del piano"));
        for (int k = 0; k < points.size(); ++k) centerBox->addItem(QStringLiteral("Punto scelto %1 (Ctrl+clic)").arg(k + 1));
        centerBox->addItems(centerNames);
        centerBox->addItem(QStringLiteral("Coordinate"));
        centerBox->setCurrentIndex(!points.isEmpty() ? 1 : !centers.isEmpty() ? 1 : 0);
        centerX = spin(0.0, -1e6, 1e6);
        centerY = spin(0.0, -1e6, 1e6);
        centerX->setPrefix(QStringLiteral("X "));
        centerY->setPrefix(QStringLiteral("Y "));
        auto *line = new QWidget(&dialog);
        auto *layout = new QHBoxLayout(line);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(centerX);
        layout->addWidget(centerY);
        form->addRow(QStringLiteral("Centro:"), centerBox);
        form->addRow(QString(), line);
        count = countSpin(6);
        angle = spin(360.0, -360.0, 360.0, QStringLiteral(" °"));
        spread = new QCheckBox(QStringLiteral("Angolo totale (istanze distribuite; 360 = giro intero)"), &dialog);
        spread->setChecked(true);
        form->addRow(QStringLiteral("Istanze:"), count);
        form->addRow(QStringLiteral("Angolo:"), angle);
        form->addRow(QString(), spread);
    } else {
        axisBox = new QComboBox(&dialog);
        axisBox->addItems({QStringLiteral("Asse X del piano"), QStringLiteral("Asse Y del piano")});
        for (int segment : segments) axisBox->addItem(QStringLiteral("Segmento %1").arg(segment + 1));
        axisBox->setCurrentIndex(segments.isEmpty() ? 1 : 2);
        form->addRow(QStringLiteral("Retta di simmetria:"), axisBox);
    }
    auto *parametricBox = new QCheckBox(QStringLiteral("Parametrica: le copie seguono le entita' di partenza"), &dialog);
    parametricBox->setChecked(true);
    auto *dimensionBox = new QCheckBox(kind == 1 ? QStringLiteral("Quota l'angolo (definisce le copie)") : QStringLiteral("Quota i passi (definiscono le copie)"), &dialog);
    dimensionBox->setChecked(true);
    form->addRow(QString(), parametricBox);
    if (kind != 2) form->addRow(QString(), dimensionBox);
    else dimensionBox->hide();
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMaximumWidth(360);
    form->addRow(QStringLiteral("Anteprima:"), status);
    int excluded = -1;
    const auto current = [&] {
        ForgeCad::SketchPattern pattern;
        pattern.kind = kind;
        pattern.parametric = parametricBox->isChecked();
        pattern.dimensioned = dimensionBox->isChecked();
        excluded = -1;
        if (kind == 0) {
            int reference = -1, reference2 = -1;
            pattern.direction = direction(first, reference, &pattern.directionRef);
            pattern.count = count->value();
            pattern.spacing = spacing->value();
            pattern.count2 = useSecond->isChecked() ? count2->value() : 1;
            pattern.direction2 = direction(second, reference2, &pattern.direction2Ref);
            pattern.spacing2 = spacing2->value();
            excluded = reference >= 0 ? reference : useSecond->isChecked() ? reference2 : -1;
        } else if (kind == 1) {
            const int index = centerBox->currentIndex();
            if (index == 0) pattern.center = QPointF(0, 0), pattern.centerRef = {2, 0, -1};
            else if (index <= points.size()) pattern.center = points.at(index - 1), pattern.centerRef = pointRefs.value(index - 1);
            else if (index <= points.size() + centers.size())
                pattern.center = centers.at(index - 1 - points.size()), pattern.centerRef = centerRefs.value(index - 1 - points.size());
            else pattern.center = QPointF(centerX->value(), centerY->value());
            pattern.count = count->value();
            pattern.angle = angle->value();
            pattern.spread = spread->isChecked();
        } else {
            const int index = axisBox->currentIndex();
            pattern.axisPoint = QPointF(0, 0);
            pattern.axisDirection = index == 0 ? QPointF(1, 0) : QPointF(0, 1);
            pattern.axisRef = {2, index == 0 ? 1 : 2, -1};
            if (index >= 2) {
                excluded = segments.at(index - 2);
                pattern.axisRef = {0, excluded, -1};
                SketchSegment segment;
                if (viewport->activeSegment(excluded, segment)) {
                    pattern.axisPoint = segment.first;
                    pattern.axisDirection = segment.second - segment.first;
                }
            }
        }
        return pattern;
    };
    const auto refresh = [&] {
        if (kind == 0) {
            for (const DirectionRow *row : {&first, &second}) row->angle->setEnabled(row->box->currentIndex() == 2);
            for (QWidget *w : {static_cast<QWidget *>(second.box), static_cast<QWidget *>(second.angle), static_cast<QWidget *>(second.flip),
                               static_cast<QWidget *>(count2), static_cast<QWidget *>(spacing2)})
                w->setEnabled(useSecond->isChecked());
            if (useSecond->isChecked()) second.angle->setEnabled(second.box->currentIndex() == 2);
        }
        if (kind == 1) {
            const bool custom = centerBox->currentIndex() == centerBox->count() - 1;
            centerX->setEnabled(custom);
            centerY->setEnabled(custom);
        }
        const QString error = viewport->previewSketchPattern(current(), excluded);
        status->setStyleSheet(error.isEmpty() ? QStringLiteral("color: #9fc6e8;") : QStringLiteral("color: #ff7a6a;"));
        status->setText(error.isEmpty() ? QStringLiteral("le copie in arancio nella vista") : error);
    };
    for (QComboBox *box : {first.box, second.box, centerBox, axisBox})
        if (box) QObject::connect(box, &QComboBox::currentIndexChanged, &dialog, refresh);
    for (QDoubleSpinBox *box : {first.angle, second.angle, spacing, spacing2, angle, centerX, centerY})
        if (box) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    for (QSpinBox *box : {count, count2})
        if (box) QObject::connect(box, &QSpinBox::valueChanged, &dialog, refresh);
    for (QCheckBox *box : {first.flip, second.flip, useSecond, spread, parametricBox})
        if (box) QObject::connect(box, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(parametricBox, &QCheckBox::toggled, dimensionBox, &QWidget::setEnabled);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refresh();
    const bool applied = runUntilApplied(dialog, form, buttons, [&] { return viewport->applySketchPattern(current(), excluded); });
    viewport->clearSketchPatternPreview();
    return applied;
}

// Offset delle entita' selezionate dello schizzo attivo: distanza, verso, dalle
// due parti, archi negli angoli, originali di costruzione. Anteprima
// tratteggiata in arancio; un passo di Undo.
static bool sketchOffsetDialog(QWidget *parent, CadViewport *viewport) {
    const QString title = QStringLiteral("Offset entita'");
    if (!viewport->activeSketchObject()) {
        QMessageBox::information(parent, title, QStringLiteral("Entra in modalita' schizzo e seleziona le entita' da copiare a distanza."));
        return false;
    }
    static ForgeCad::SketchOffset last;
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *selectionLabel = wrappedNote(QString(), &dialog);
    form->addRow(selectionLabel);
    auto *distanceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    distanceBox->setDecimals(6);
    distanceBox->setRange(0.0, 100000.0);
    distanceBox->setValue(last.distance);
    distanceBox->setKeyboardTracking(false);
    auto *reverseButton = new QPushButton(ForgeCad::commandIcon(QStringLiteral("reverseDirection")), QStringLiteral("Inverti il verso"), &dialog);
    reverseButton->setCheckable(true);
    reverseButton->setChecked(last.reverse);
    reverseButton->setToolTip(QStringLiteral("Le catene chiuse vanno verso l'esterno, quelle aperte a sinistra: acceso, dall'altra parte"));
    auto *bothBox = new QCheckBox(QStringLiteral("Dalle due parti"), &dialog);
    bothBox->setChecked(last.bothSides);
    auto *roundBox = new QCheckBox(QStringLiteral("Archi negli angoli convessi (altrimenti spigoli vivi)"), &dialog);
    roundBox->setChecked(last.roundCorners);
    auto *constructionBox = new QCheckBox(QStringLiteral("Entita' di partenza di costruzione"), &dialog);
    constructionBox->setChecked(last.construction);
    auto *dimensionBox = new QCheckBox(QStringLiteral("Mantieni la quota di offset"), &dialog);
    dimensionBox->setChecked(true);
    form->addRow(QStringLiteral("Distanza:"), distanceBox);
    form->addRow(dimensionBox);
    form->addRow(QString(), reverseButton);
    form->addRow(QString(), bothBox);
    form->addRow(QString(), roundBox);
    form->addRow(QString(), constructionBox);
    form->addRow(wrappedNote(QStringLiteral("Segmenti e archi restano esatti (paralleli e concentrici); spline ed ellissi diventano "
                                           "curve approssimate entro 1e-7."), &dialog));
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    form->addRow(status);
    const auto current = [&] {
        ForgeCad::SketchOffset offset;
        offset.distance = distanceBox->value();
        offset.dimensioned = dimensionBox->isChecked();
        offset.reverse = reverseButton->isChecked();
        offset.bothSides = bothBox->isChecked();
        offset.roundCorners = roundBox->isChecked();
        offset.construction = constructionBox->isChecked();
        return offset;
    };
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    const auto refresh = [&] {
        const int count = viewport->sketchSelection().size();
        selectionLabel->setText(QStringLiteral("%1 entita' selezionate — clic su curve o bordi del modello per aggiungere; clic di nuovo per togliere. Riquadro per le entita' dello schizzo.").arg(count));
        reverseButton->setEnabled(!bothBox->isChecked());
        const QString error = count ? viewport->previewSketchOffset(current()) : QString();
        if (!count) viewport->clearSketchPatternPreview();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(count > 0 && error.isEmpty());
        status->setStyleSheet(error.isEmpty() ? QStringLiteral("color: #9fc6e8;") : QStringLiteral("color: #ff7a6a;"));
        status->setText(!count ? QStringLiteral("Seleziona curve dello schizzo o bordi del modello. I bordi saranno copiati come riferimenti di costruzione.")
                             : error.isEmpty() ? QStringLiteral("le copie in arancio nella vista") : error);
    };
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(reverseButton, &QPushButton::toggled, &dialog, refresh);
    for (QCheckBox *box : {bothBox, roundBox, constructionBox, dimensionBox}) QObject::connect(box, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    viewport->setDrawingTool(DrawingTool::Select);
    viewport->setSketchOffsetSelectionCallback(refresh);
    refresh();
    const bool applied = runUntilAppliedModeless(qobject_cast<QMainWindow *>(parent), viewport, dialog, form, buttons, [&] {
        const ForgeCad::SketchOffset offset = current();
        const QString error = viewport->applySketchOffset(offset);
        if (error.isEmpty()) last = offset;
        return error;
    });
    viewport->setSketchOffsetSelectionCallback({});
    viewport->clearSketchPatternPreview();
    return applied;
}

// Sposta / ruota / copia le entita' selezionate nello schizzo attivo:
// traslazione, rotazione attorno a un centro (il punto scelto con Ctrl+clic,
// il centro della selezione o l'origine) e copia, con l'anteprima arancio.
static bool sketchMoveDialog(QWidget *parent, CadViewport *viewport) {
    const QString title = QStringLiteral("Sposta entita'");
    const SketchObject *sketch = viewport->activeSketchObject();
    if (!sketch) {
        QMessageBox::information(parent, title, QStringLiteral("Entra in modalita' schizzo e seleziona le entita' da spostare."));
        return false;
    }
    const QVector<SketchElementSelection> selection = viewport->sketchSelection();
    if (selection.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Seleziona prima le entita' (clic, Maiusc+clic o il riquadro)."));
        return false;
    }
    // Centro del riquadro delle entita' scelte (punti esatti, non i campioni).
    QRectF box;
    const auto add = [&box](const QPointF &p) { box = box.isNull() ? QRectF(p, QSizeF(0, 0)) : box.united(QRectF(p, QSizeF(0, 0))); };
    for (const SketchElementSelection &element : selection) {
        if (element.kind == 0 && element.index >= 0 && element.index < sketch->segments.size()) {
            add(sketch->segments.at(element.index).first);
            add(sketch->segments.at(element.index).second);
        } else if (element.kind == 1 && element.index >= 0 && element.index < sketch->curves.size()) {
            for (const QPointF &p : sketch->curves.at(element.index).controlPoints) add(p);
        }
    }
    const QPointF selectionCenter = box.center();
    const QVector<QPointF> points = viewport->selectedSketchPoints();
    static ForgeCad::SketchMove last;
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    form->addRow(wrappedNote(QStringLiteral("%1 entita' selezionate. Prima la rotazione attorno al centro (antiorario), poi la traslazione. "
                                            "Le entita' spostate perdono i vincoli che non valgono piu' (verso le entita' rimaste ferme, fissi).")
                                 .arg(selection.size()), &dialog));
    const auto spin = [&dialog](double value, double lo, double hi, const QString &prefix, bool lengthMeasure = true) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setLengthMeasurement(lengthMeasure);
        box->setDecimals(6);
        box->setRange(lo, hi);
        box->setValue(value);
        box->setPrefix(prefix);
        box->setKeyboardTracking(false);
        return box;
    };
    const auto pair = [&dialog](QWidget *a, QWidget *b) {
        auto *row = new QWidget(&dialog);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(a);
        layout->addWidget(b);
        return row;
    };
    QDoubleSpinBox *dx = spin(last.translation.x(), -1e6, 1e6, QStringLiteral("ΔX ")), *dy = spin(last.translation.y(), -1e6, 1e6, QStringLiteral("ΔY "));
    QDoubleSpinBox *angleBox = spin(last.angle, -360.0, 360.0, QString(), false);
    angleBox->setSuffix(QStringLiteral(" °"));
    const QPointF center = points.isEmpty() ? selectionCenter : points.first();
    QDoubleSpinBox *cx = spin(center.x(), -1e6, 1e6, QStringLiteral("X ")), *cy = spin(center.y(), -1e6, 1e6, QStringLiteral("Y "));
    auto *centerBox = new QComboBox(&dialog);
    centerBox->addItem(QStringLiteral("Centro della selezione"));
    centerBox->addItem(QStringLiteral("Origine"));
    if (!points.isEmpty()) centerBox->addItem(QStringLiteral("Punto scelto (Ctrl+clic)"));
    centerBox->addItem(QStringLiteral("Coordinate"));
    centerBox->setCurrentIndex(points.isEmpty() ? 0 : 2);
    auto *copyBox = new QCheckBox(QStringLiteral("Copia (le entita' di partenza restano)"), &dialog);
    copyBox->setChecked(last.copy);
    form->addRow(QStringLiteral("Traslazione:"), pair(dx, dy));
    form->addRow(QStringLiteral("Rotazione:"), angleBox);
    form->addRow(QStringLiteral("Centro:"), centerBox);
    form->addRow(QString(), pair(cx, cy));
    form->addRow(QString(), copyBox);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    form->addRow(status);
    const auto current = [&] {
        ForgeCad::SketchMove move;
        move.translation = QPointF(dx->value(), dy->value());
        move.angle = angleBox->value();
        move.center = QPointF(cx->value(), cy->value());
        move.copy = copyBox->isChecked();
        return move;
    };
    const auto refresh = [&] {
        const QString error = viewport->previewSketchMove(current());
        status->setStyleSheet(error.isEmpty() ? QStringLiteral("color: #9fc6e8;") : QStringLiteral("color: #ff7a6a;"));
        status->setText(error.isEmpty() ? QStringLiteral("la posizione nuova in arancio nella vista") : error);
    };
    QObject::connect(centerBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        const QString choice = centerBox->currentText();
        const bool manual = choice == QStringLiteral("Coordinate");
        QPointF c(cx->value(), cy->value());
        if (choice == QStringLiteral("Centro della selezione")) c = selectionCenter;
        else if (choice == QStringLiteral("Origine")) c = QPointF();
        else if (!manual && !points.isEmpty()) c = points.first();
        {
            const QSignalBlocker bx(cx), by(cy);
            cx->setValue(c.x());
            cy->setValue(c.y());
        }
        cx->setEnabled(manual);
        cy->setEnabled(manual);
        refresh();
    });
    cx->setEnabled(false);
    cy->setEnabled(false);
    for (QDoubleSpinBox *box : {dx, dy, angleBox, cx, cy}) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(copyBox, &QCheckBox::toggled, &dialog, refresh);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refresh();
    const bool applied = runUntilApplied(dialog, form, buttons, [&] {
        for (QDoubleSpinBox *box : {dx, dy, angleBox, cx, cy}) box->interpretText();
        const ForgeCad::SketchMove move = current();
        const QString error = viewport->applySketchMove(move);
        if (error.isEmpty()) last = move;
        return error;
    });
    viewport->clearSketchPatternPreview();
    return applied;
}

// Ripetizione di un corpo o di una funzione (nuova, o al posto del corpo
// `replaced`): corpo, tipo (lineare, circolare, specchio), cosa si ripete,
// riferimenti scelti nella vista come per i piani di costruzione (direzione,
// asse o piano; la seconda direzione della griglia), istanze, passo o angolo.
// Anteprima in ambra; la conferma chiama `apply` (l'errore resta nella finestra).
static bool patternDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    const QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Nella scena non ci sono corpi da ripetere."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.feature = BodyFeature::Pattern;
    definition.operation = -1;
    PatternParameters &p = definition.pattern;
    const bool preselected = candidates.contains(definition.firstBody);
    if (!preselected) definition.firstBody = candidates.last();
    // Riferimenti di partenza: asse X (lineare), asse Z (circolare), piano YZ (specchio).
    const auto defaultRef = [](int kind) {
        GeometryRef ref;
        ref.kind = kind == 2 ? 1 : 2;
        ref.index = kind == 0 ? 0 : 2;
        return ref;
    };
    if (p.refs.isEmpty()) p.refs = {defaultRef(p.kind)};
    if (p.refs.size() < 2) {
        GeometryRef y;
        y.kind = 2;
        y.index = 1;
        p.refs.append(y);
    }
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int index : candidates) bodyBox->addItem(logicalBodyLabel(viewport, index));
    bodyBox->setCurrentIndex(int(candidates.indexOf(definition.firstBody)));
    bodyBox->setEnabled(replaced < 0);
    BodyPicker picker(dialog, viewport, replaced, candidates, bodyBox);
    auto *kindBox = new QComboBox(&dialog);
    kindBox->addItems({QStringLiteral("Lineare"), QStringLiteral("Circolare"), QStringLiteral("Specchio")});
    kindBox->setCurrentIndex(qBound(0, p.kind, 2));
    auto *whatBox = new QComboBox(&dialog);
    whatBox->addItems({QStringLiteral("Il corpo intero"), QStringLiteral("La funzione (lo strumento dell'unione o della differenza)")});
    whatBox->setCurrentIndex(p.featureOnly ? 1 : 0);
    form->addRow(QStringLiteral("Corpo:"), picker.row(bodyBox));
    form->addRow(QStringLiteral("Tipo:"), kindBox);
    form->addRow(QStringLiteral("Ripeti:"), whatBox);
    auto *refLabel = new QLabel(&dialog), *refLabel2 = new QLabel(QStringLiteral("Direzione 2:"), &dialog);
    auto *refButton = new QPushButton(&dialog), *refButton2 = new QPushButton(&dialog);
    refButton->setToolTip(QStringLiteral("Scegli nella vista"));
    refButton2->setToolTip(QStringLiteral("Scegli nella vista"));
    auto *flipBox = new QCheckBox(QStringLiteral("Inverti il verso"), &dialog);
    flipBox->setChecked(p.flip);
    const auto spin = [&dialog](double value, double lo, double hi, const QString &suffix = QString()) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setDecimals(6);
        box->setRange(lo, hi);
        box->setValue(value);
        box->setSuffix(suffix);
        return box;
    };
    auto *countBox = new QSpinBox(&dialog);
    countBox->setRange(1, 500);
    countBox->setValue(p.count);
    QDoubleSpinBox *spacingBox = spin(p.spacing, -1e6, 1e6);
    QDoubleSpinBox *angleBox = spin(p.angle, -360.0, 360.0, QStringLiteral(" °"));
    auto *spreadBox = new QCheckBox(QStringLiteral("Angolo totale (istanze distribuite; 360 = giro intero)"), &dialog);
    spreadBox->setChecked(p.spread);
    auto *keepBox = new QCheckBox(QStringLiteral("Tieni anche il corpo di partenza"), &dialog);
    keepBox->setChecked(p.keepOriginal);
    auto *secondBox = new QCheckBox(QStringLiteral("Seconda direzione (griglia)"), &dialog);
    secondBox->setChecked(p.count2 > 1);
    auto *flipBox2 = new QCheckBox(QStringLiteral("Inverti il verso 2"), &dialog);
    flipBox2->setChecked(p.flip2);
    auto *countBox2 = new QSpinBox(&dialog);
    countBox2->setRange(1, 500);
    countBox2->setValue(qMax(2, p.count2));
    QDoubleSpinBox *spacingBox2 = spin(p.spacing2, -1e6, 1e6);
    form->addRow(refLabel, refButton);
    form->addRow(QString(), flipBox);
    form->addRow(QStringLiteral("Istanze:"), countBox);
    form->addRow(QStringLiteral("Passo:"), spacingBox);
    form->addRow(QStringLiteral("Angolo:"), angleBox);
    form->addRow(QString(), spreadBox);
    form->addRow(QString(), keepBox);
    form->addRow(QString(), secondBox);
    form->addRow(refLabel2, refButton2);
    form->addRow(QString(), flipBox2);
    form->addRow(QStringLiteral("Istanze 2:"), countBox2);
    form->addRow(QStringLiteral("Passo 2:"), spacingBox2);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(280);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);

    int active = -1;  // riferimento in scelta nella vista
    const auto roles = [&](int slot) {
        if (slot == 1) return int(ForgeCad::DatumRoleLine | ForgeCad::DatumRolePlane);
        return p.kind == 0 ? int(ForgeCad::DatumRoleLine | ForgeCad::DatumRolePlane) : p.kind == 1 ? int(ForgeCad::DatumRoleLine) : int(ForgeCad::DatumRolePlane);
    };
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.firstBody = candidates.at(bodyBox->currentIndex());
        d.pattern.kind = kindBox->currentIndex();
        d.pattern.featureOnly = whatBox->currentIndex() == 1;
        d.pattern.flip = flipBox->isChecked();
        d.pattern.count = countBox->value();
        d.pattern.spacing = spacingBox->value();
        d.pattern.angle = angleBox->value();
        d.pattern.spread = spreadBox->isChecked();
        d.pattern.keepOriginal = keepBox->isChecked();
        const bool grid = d.pattern.kind == 0 && secondBox->isChecked();
        d.pattern.count2 = grid ? countBox2->value() : 1;
        d.pattern.flip2 = flipBox2->isChecked();
        d.pattern.spacing2 = spacingBox2->value();
        d.pattern.refs = p.refs;
        if (!grid) d.pattern.refs.resize(1);
        return d;
    };
    const auto refresh = [&] {
        const int kind = kindBox->currentIndex();
        p.kind = kind;
        const ExtrusionObject &base = bodies.at(candidates.at(bodyBox->currentIndex()));
        const bool feature = base.operation == 0 || base.operation == 2
            || (base.operation < 0 && (base.feature == BodyFeature::Extrusion || base.feature == BodyFeature::Revolution)
                && base.mergeOperation != 0 && !base.mergeBodies.isEmpty());
        if (!feature && whatBox->currentIndex() == 1) whatBox->setCurrentIndex(0);
        whatBox->setEnabled(feature);
        refLabel->setText(kind == 0 ? QStringLiteral("Direzione:") : kind == 1 ? QStringLiteral("Asse:") : QStringLiteral("Piano di simmetria:"));
        const auto text = [&](int slot) {
            const GeometryRef &ref = p.refs.at(slot);
            QString t = ref.kind >= 0 ? ForgeCad::geometryRefText(ref, viewport->sketches(), viewport->extrusions()) : QStringLiteral("(da scegliere)");
            if (slot == active) t = QStringLiteral("▶ ") + t + QStringLiteral("  - clicca nella vista");
            return t;
        };
        refButton->setText(text(0));
        refButton2->setText(text(1));
        const bool grid = kind == 0 && secondBox->isChecked();
        form->setRowVisible(flipBox, kind != 2);
        form->setRowVisible(countBox, kind != 2);
        form->setRowVisible(spacingBox, kind == 0);
        form->setRowVisible(angleBox, kind == 1);
        form->setRowVisible(spreadBox, kind == 1);
        form->setRowVisible(keepBox, kind == 2 && whatBox->currentIndex() == 0);
        form->setRowVisible(secondBox, kind == 0);
        for (QWidget *w : {static_cast<QWidget *>(refButton2), static_cast<QWidget *>(flipBox2), static_cast<QWidget *>(countBox2),
                           static_cast<QWidget *>(spacingBox2)})
            form->setRowVisible(w, grid);
        QVector<GeometryRef> marks{p.refs.at(0)};
        if (grid) marks.append(p.refs.at(1));
        viewport->setReferenceMarks(marks);
        if (active >= 0) setStatus(QStringLiteral("Clicca nella vista il riferimento (Esc nella vista: annulla la scelta; trascinando lontano dagli oggetti la vista ruota)."), false);
        else setStatus(picker.active() ? picker.hint() : QString(), false);
        if (picker.waiting()) {
            scope->label->setText(picker.hint());
            viewport->clearPreview();
            return;
        }
        scope->request(current());
    };
    picker.changed = refresh;
    picker.message = setStatus;
    picker.starting = [&] { active = -1; };
    const auto pick = [&](int slot) {
        picker.stop();
        active = slot;
        const QString error = viewport->beginReferencePick(roles(slot), replaced);
        if (!error.isEmpty()) {
            active = -1;
            setStatus(error, true);
            return;
        }
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picker.handle(picked, ref)) return;
        if (picked && active >= 0) {
            if (ForgeCad::geometryRefRoles(ref, viewport->sketches()) & roles(active)) p.refs[active] = ref;
            else setStatus(QStringLiteral("Questo riferimento non va bene qui."), true);
        }
        active = -1;
        refresh();
    });
    QObject::connect(refButton, &QPushButton::clicked, &dialog, [&] { pick(0); });
    QObject::connect(refButton2, &QPushButton::clicked, &dialog, [&] { pick(1); });
    QObject::connect(kindBox, &QComboBox::currentIndexChanged, &dialog, [&](int kind) {
        // Il riferimento che non vale per il tipo nuovo torna quello di partenza.
        if (!(ForgeCad::geometryRefRoles(p.refs.at(0), viewport->sketches()) & (kind == 0 ? int(ForgeCad::DatumRoleLine | ForgeCad::DatumRolePlane) : kind == 1 ? int(ForgeCad::DatumRoleLine) : int(ForgeCad::DatumRolePlane))))
            p.refs[0] = defaultRef(kind);
        if (active >= 0) viewport->cancelReferencePick();  // la scelta del corpo invece continua
        active = -1;
        refresh();
    });
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(whatBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    for (QCheckBox *box : {flipBox, spreadBox, keepBox, secondBox, flipBox2}) QObject::connect(box, &QCheckBox::toggled, &dialog, refresh);
    for (QSpinBox *box : {countBox, countBox2}) QObject::connect(box, &QSpinBox::valueChanged, &dialog, refresh);
    for (QDoubleSpinBox *box : {spacingBox, angleBox, spacingBox2}) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (picker.waiting()) return setStatus(picker.hint(), true);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(current());
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        if (replaced < 0 && !preselected) picker.start(true);
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    scope.reset();
    return accepted;
}

// Estrusione (nuova dello schizzo initial.sketchIndex, o al posto del corpo
// `replaced`): fine a distanza o fino a un punto, uno spigolo, una faccia o
// un piano (scelti nella vista: la finestra non e' modale), e fusione con
// altri solidi (unione o sottrazione) nei corpi scelti o in quelli che
// l'estrusione tocca. Anteprima in ambra; la conferma chiama `apply`.
static bool extrusionDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                            const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Extrusion;
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    form->addRow(new FeatureOperationDiagram(FeatureOperationDiagram::Extrusion, &dialog));
    auto *extentBox = new QComboBox(&dialog);
    extentBox->addItems({QStringLiteral("Distanza"), QStringLiteral("Fino a un punto"), QStringLiteral("Fino a uno spigolo"),
                         QStringLiteral("Fino a una faccia o a un piano")});
    extentBox->setCurrentIndex(qBound(0, definition.extent, 3));
    auto *distanceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    distanceBox->setDecimals(6);
    distanceBox->setRange(-100000.0, 100000.0);
    distanceBox->setValue(definition.distance);
    // Versi: uno solo, simmetrica (meta' per parte del piano dello schizzo) o
    // due versi con la seconda distanza dall'altra parte.
    auto *sidesBox = new QComboBox(&dialog);
    sidesBox->addItems({QStringLiteral("Un verso"), QStringLiteral("Simmetrica"), QStringLiteral("Due versi")});
    sidesBox->setCurrentIndex(qBound(0, definition.extrudeSides, 2));
    sidesBox->setToolTip(QStringLiteral("Simmetrica: la distanza in tutto, meta' per parte dello schizzo (solo con la fine a distanza).\n"
                                        "Due versi: la fine scelta da una parte e la seconda distanza dall'altra."));
    auto *distance2Box = new ForgeCad::ExpressionSpinBox(&dialog);
    distance2Box->setDecimals(6);
    distance2Box->setRange(0.0, 100000.0);
    distance2Box->setValue(definition.distance2);
    distance2Box->setToolTip(QStringLiteral("Distanza nel verso opposto alla prima (positiva)."));
    // Inverti il verso: la distanza cambia segno (nei due versi le due
    // distanze si scambiano); con la fine "fino a" il verso lo da' il riferimento.
    auto *flipButton = new QPushButton(ForgeCad::commandIcon(QStringLiteral("reverseDirection")), QStringLiteral("Inverti il verso"), &dialog);
    flipButton->setToolTip(QStringLiteral("Estrude dall'altra parte del piano dello schizzo"));
    // Come nei raccordi: l'anteprima si calcola con Invio, non a ogni carattere.
    distanceBox->setKeyboardTracking(false);
    distance2Box->setKeyboardTracking(false);
    auto *refButton = new QPushButton(&dialog);
    refButton->setToolTip(QStringLiteral("Scegli nella vista"));
    auto *operationBox = new QComboBox(&dialog);
    operationBox->addItems({QStringLiteral("Corpo nuovo"), QStringLiteral("Unisci ai solidi"), QStringLiteral("Sottrai dai solidi")});
    operationBox->setCurrentIndex(qBound(0, definition.mergeOperation, 2));
    auto *surfaceBox = new QCheckBox(QStringLiteral("Solo superficie (senza base e coperchio)"), &dialog);
    surfaceBox->setChecked(definition.extrudeSurface);
    surfaceBox->setToolTip(QStringLiteral("Una lamina: un contorno chiuso diventa un tubo aperto alle estremita', un profilo aperto una superficie; "
                                          "non si fonde con i solidi"));
    auto *autoBox = new QCheckBox(QStringLiteral("Automatico: i solidi che hanno punti in comune con l'estrusione"), &dialog);
    autoBox->setChecked(definition.mergeAuto);
    auto *bodyList = new QListWidget(&dialog);
    bodyList->setMinimumHeight(110);
    BodyPicker picker(dialog, viewport, replaced, fillMergeBodyList(bodyList, viewport, replaced, definition.mergeBodies), nullptr, bodyList);
    QWidget *bodyRow = picker.row(bodyList);
    form->addRow(QStringLiteral("Versi:"), sidesBox);
    form->addRow(QStringLiteral("Fine:"), extentBox);
    form->addRow(QStringLiteral("Distanza:"), distanceBox);
    form->addRow(QStringLiteral("Fino a:"), refButton);
    form->addRow(QStringLiteral("Secondo verso:"), distance2Box);
    form->addRow(QString(), flipButton);
    form->addRow(QString(), surfaceBox);
    form->addRow(QStringLiteral("Risultato:"), operationBox);
    form->addRow(QString(), autoBox);
    form->addRow(QStringLiteral("Solidi:"), bodyRow);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(280);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    bool picking = false;
    const auto roles = [&] {
        const int extent = extentBox->currentIndex();
        return extent == 1 ? int(ForgeCad::DatumRolePoint)
             : extent == 2 ? int(ForgeCad::DatumRoleLine | ForgeCad::DatumRoleCurve)
                           : int(ForgeCad::DatumRolePlane | ForgeCad::DatumRoleFace);
    };
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.extent = extentBox->currentIndex();
        d.distance = distanceBox->value();
        d.extrudeSides = sidesBox->currentIndex();
        d.distance2 = distance2Box->value();
        // La superficie non si fonde con i solidi.
        d.extrudeSurface = surfaceBox->isChecked();
        d.mergeOperation = d.extrudeSurface ? 0 : operationBox->currentIndex();
        d.mergeAuto = autoBox->isChecked();
        d.mergeBodies.clear();
        for (int row = 0; row < bodyList->count() && !d.extrudeSurface; ++row)
            if (bodyList->item(row)->checkState() == Qt::Checked) d.mergeBodies.append(bodyList->item(row)->data(Qt::UserRole).toInt());
        return d;
    };
    std::function<void()> refresh;
    refresh = [&] {
        const int extent = extentBox->currentIndex();
        // La simmetrica vale solo con la fine a distanza.
        if (auto *model = qobject_cast<QStandardItemModel *>(sidesBox->model()))
            if (QStandardItem *item = model->item(1)) item->setEnabled(extent == 0);
        if (extent != 0 && sidesBox->currentIndex() == 1) {
            const QSignalBlocker block(sidesBox);
            sidesBox->setCurrentIndex(2);
        }
        const int sides = sidesBox->currentIndex();
        form->setRowVisible(distanceBox, extent == 0);
        form->setRowVisible(refButton, extent != 0);
        form->setRowVisible(distance2Box, sides == 2);
        if (auto *label = qobject_cast<QLabel *>(form->labelForField(distanceBox)))
            label->setText(sides == 1 ? QStringLiteral("Distanza totale:") : QStringLiteral("Distanza:"));
        // La simmetrica non ha verso; con la fine "fino a" lo decide il riferimento.
        flipButton->setEnabled(extent == 0 && sides != 1);
        operationBox->setEnabled(!surfaceBox->isChecked());
        const bool merge = !surfaceBox->isChecked() && operationBox->currentIndex() != 0;
        form->setRowVisible(autoBox, merge);
        form->setRowVisible(bodyRow, merge);
        bodyRow->setEnabled(!autoBox->isChecked());
        if (!merge || autoBox->isChecked()) picker.stop();
        QString text = definition.extentRef.kind >= 0 ? ForgeCad::geometryRefText(definition.extentRef, viewport->sketches(), viewport->extrusions())
                                                      : QStringLiteral("(da scegliere)");
        if (picking) text = QStringLiteral("▶ ") + text + QStringLiteral("  - clicca nella vista");
        refButton->setText(text);
        viewport->setReferenceMarks(extent != 0 && definition.extentRef.kind >= 0 ? QVector<GeometryRef>{definition.extentRef} : QVector<GeometryRef>());
        if (picking) setStatus(QStringLiteral("Clicca nella vista il riferimento (Esc nella vista: annulla la scelta)."), false);
        else if (picker.active()) setStatus(picker.hint(), false);
        else if (extent != 0 && definition.extentRef.kind < 0) setStatus(QStringLiteral("Scegli nella vista dove finisce l'estrusione."), false);
        else setStatus(QString(), false);
        ExtrusionObject d = current();
        if (extent == 0 && !picking && !picker.active()) {
            viewport->setFeatureHandle(d, [&](double value) {
                const QSignalBlocker blocker(distanceBox);
                distanceBox->setValue(value);
                refresh();
            });
        } else viewport->clearFeatureHandle();
        if (extent != 0 && d.extentRef.kind < 0) return;
        scope->request(viewport->withMergeCandidates(d, replaced >= 0 ? replaced : int(bodies.size())));
    };
    picker.changed = refresh;
    picker.message = setStatus;
    picker.starting = [&] { picking = false; viewport->clearFeatureHandle(); };
    const auto pick = [&] {
        picker.stop();
        picking = true;
        const QString error = viewport->beginReferencePick(roles(), replaced);
        if (!error.isEmpty()) {
            picking = false;
            setStatus(error, true);
            return;
        }
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picker.handle(picked, ref)) return;
        if (picked && picking) {
            if (ForgeCad::geometryRefRoles(ref, viewport->sketches()) & roles()) definition.extentRef = ref;
            else setStatus(QStringLiteral("Questo riferimento non va bene qui."), true);
        }
        picking = false;
        refresh();
    });
    QObject::connect(refButton, &QPushButton::clicked, &dialog, pick);
    QObject::connect(extentBox, &QComboBox::currentIndexChanged, &dialog, [&](int extent) {
        if (picking) viewport->cancelReferencePick();
        picking = false;
        // Il riferimento che non vale per la fine nuova si sceglie di nuovo.
        if (definition.extentRef.kind >= 0 && !(ForgeCad::geometryRefRoles(definition.extentRef, viewport->sketches()) & roles())) definition.extentRef = GeometryRef();
        refresh();
        if (extent != 0 && definition.extentRef.kind < 0) pick();
    });
    QObject::connect(operationBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(surfaceBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(autoBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(bodyList, &QListWidget::itemChanged, &dialog, [&] {
        if (!picker.handling()) refresh();
    });
    QObject::connect(sidesBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(flipButton, &QPushButton::clicked, &dialog, [&] {
        distanceBox->interpretText();
        distance2Box->interpretText();
        const double d = distanceBox->value();
        if (sidesBox->currentIndex() == 2) {
            const double back = distance2Box->value();
            distanceBox->setValue(d >= 0.0 ? -back : back);
            distance2Box->setValue(std::fabs(d));
        } else {
            distanceBox->setValue(-d);
        }
        refresh();
    });
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(distance2Box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    distanceBox->onReturn = refresh;
    distance2Box->onReturn = refresh;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        // Il valore scritto e non ancora confermato con Invio vale comunque.
        distanceBox->interpretText();
        distance2Box->interpretText();
        const ExtrusionObject d = current();
        if (d.extrudeSides == 2 && !(d.distance2 > 1e-7)) {
            setStatus(QStringLiteral("La distanza nel secondo verso deve essere positiva."), true);
            return;
        }
        if (d.extent != 0 && d.extentRef.kind < 0) {
            setStatus(QStringLiteral("Scegli prima nella vista dove finisce l'estrusione."), true);
            return;
        }
        if (d.mergeOperation != 0 && !d.mergeAuto && d.mergeBodies.isEmpty()) {
            setStatus(QStringLiteral("Spunta almeno un solido (o scegli Automatico)."), true);
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(d);
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    viewport->clearFeatureHandle();
    scope.reset();
    return accepted;
}

// Spostamento e rotazione di un corpo (nuovo, o al posto del corpo
// `replaced`): corpo, traslazione X/Y/Z, angolo e asse della rotazione
// (assi del modello o una retta scelta nella vista), copia. Finestra non
// modale; anteprima in ambra; la conferma chiama `apply`.
static bool transformDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                            const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Nella scena non ci sono corpi da spostare."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Transform;
    const bool preselected = candidates.contains(definition.firstBody);
    if (!preselected) definition.firstBody = candidates.last();
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int index : candidates) bodyBox->addItem(logicalBodyLabel(viewport, index));
    bodyBox->setCurrentIndex(int(candidates.indexOf(definition.firstBody)));
    bodyBox->setEnabled(replaced < 0);
    BodyPicker picker(dialog, viewport, replaced, candidates, bodyBox);
    const auto spin = [&dialog](double value, double lo, double hi, const QString &prefix, const QString &suffix = QString()) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setDecimals(6);
        box->setRange(lo, hi);
        box->setValue(value);
        box->setPrefix(prefix);
        box->setSuffix(suffix);
        return box;
    };
    QDoubleSpinBox *dx = spin(definition.move.translation[0], -1e6, 1e6, QStringLiteral("X ")),
                   *dy = spin(definition.move.translation[1], -1e6, 1e6, QStringLiteral("Y ")),
                   *dz = spin(definition.move.translation[2], -1e6, 1e6, QStringLiteral("Z "));
    auto *translation = new QWidget(&dialog);
    auto *row = new QHBoxLayout(translation);
    row->setContentsMargins(0, 0, 0, 0);
    for (QDoubleSpinBox *box : {dx, dy, dz}) row->addWidget(box);
    QDoubleSpinBox *angleBox = spin(definition.move.angle, -360.0, 360.0, QString(), QStringLiteral(" °"));
    auto *axisButton = new QPushButton(&dialog);
    axisButton->setToolTip(QStringLiteral("Scegli nella vista una retta: asse, spigolo, faccia cilindrica, segmento"));
    auto *copyBox = new QCheckBox(QStringLiteral("Copia (il corpo di partenza resta)"), &dialog);
    copyBox->setChecked(definition.move.copy);
    form->addRow(QStringLiteral("Corpo:"), picker.row(bodyBox));
    form->addRow(QStringLiteral("Traslazione:"), translation);
    form->addRow(QStringLiteral("Rotazione:"), angleBox);
    form->addRow(QStringLiteral("Asse:"), axisButton);
    form->addRow(QString(), copyBox);
    form->addRow(wrappedNote(QStringLiteral("Prima la rotazione attorno all'asse (verso destrorso), poi la traslazione."), &dialog));
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(280);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    bool picking = false;
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.firstBody = candidates.at(bodyBox->currentIndex());
        d.move.translation[0] = dx->value();
        d.move.translation[1] = dy->value();
        d.move.translation[2] = dz->value();
        d.move.angle = angleBox->value();
        d.move.copy = copyBox->isChecked();
        return d;
    };
    const auto refresh = [&] {
        QString text = ForgeCad::geometryRefText(definition.move.axis, viewport->sketches(), viewport->extrusions());
        if (picking) text = QStringLiteral("▶ ") + text + QStringLiteral("  - clicca nella vista");
        axisButton->setText(text);
        viewport->setReferenceMarks(std::fabs(angleBox->value()) > 0.0 ? QVector<GeometryRef>{definition.move.axis} : QVector<GeometryRef>());
        setStatus(picking ? QStringLiteral("Clicca nella vista la retta dell'asse (Esc nella vista: annulla la scelta).")
                          : picker.active() ? picker.hint() : QString(), false);
        if (picker.waiting()) {
            scope->label->setText(picker.hint());
            viewport->clearPreview();
            return;
        }
        scope->request(current());
    };
    picker.changed = refresh;
    picker.message = setStatus;
    picker.starting = [&] { picking = false; };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picker.handle(picked, ref)) return;
        if (picked && picking) {
            if (ForgeCad::geometryRefRoles(ref, viewport->sketches()) & ForgeCad::DatumRoleLine) definition.move.axis = ref;
            else setStatus(QStringLiteral("Serve una retta."), true);
        }
        picking = false;
        refresh();
    });
    QObject::connect(axisButton, &QPushButton::clicked, &dialog, [&] {
        picker.stop();
        picking = true;
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleLine, replaced);
        if (!error.isEmpty()) {
            picking = false;
            setStatus(error, true);
            return;
        }
        refresh();
    });
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(copyBox, &QCheckBox::toggled, &dialog, refresh);
    for (QDoubleSpinBox *box : {dx, dy, dz, angleBox}) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (picker.waiting()) return setStatus(picker.hint(), true);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(current());
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        if (replaced < 0 && !preselected) picker.start(true);
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    scope.reset();
    return accepted;
}

// Nome del solo corpo logico per i pannelli che operano sui corpi interi.
static QString logicalBodyLabel(CadViewport *viewport, int index) {
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


// Il candidato che corrisponde al corpo cliccato nella vista: lo stesso stadio
// o lo stadio del suo corpo logico tra i candidati; -1 se non c'e'.
static int resultBodyForPick(CadViewport *viewport, const QVector<int> &candidates, int picked) {
    if (candidates.contains(picked)) return picked;
    if (picked < 0 || picked >= viewport->extrusions().size()) return -1;
    const quint64 body = viewport->extrusions().at(picked).modelBodyId;
    for (int candidate : candidates)
        if (body && viewport->extrusions().at(candidate).modelBodyId == body) return candidate;
    return -1;
}

class ThreadDiagram final : public QWidget {
public:
    enum Phase { Surface, Profile, Extent };
    explicit ThreadDiagram(Phase phase, QWidget *parent = nullptr) : QWidget(parent), phase_(phase) {
        setMinimumSize(280, 88);
        setMaximumWidth(360);
    }
    void setValues(bool internal, double angle, double pitch, double length, bool leftHanded) {
        internal_ = internal, angle_ = angle, pitch_ = pitch, length_ = length, leftHanded_ = leftHanded;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
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
                                      QStringLiteral("2  Profilo %1°   passo %2").arg(angle_, 0, 'f', 0).arg(ForgeCad::formatLength(pitch_)));
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
                                      QStringLiteral("3  Lunghezza %1   %2").arg(ForgeCad::formatLength(length_), leftHanded_ ? QStringLiteral("sinistrorsa") : QStringLiteral("destrorsa")));
        }
    }
private:
    Phase phase_;
    bool internal_ = false, leftHanded_ = false;
    double angle_ = 60.0, pitch_ = 1.5, length_ = 10.0;
};

struct ThreadChoice { const char *name; double diameter; double pitch; };

static QVector<ThreadChoice> threadChoices(int standard) {
    switch (standard) {
    case 0: return {{"M3 x 0,5",3,.5},{"M4 x 0,7",4,.7},{"M5 x 0,8",5,.8},{"M6 x 1",6,1},{"M8 x 1,25",8,1.25},{"M10 x 1,5",10,1.5},{"M12 x 1,75",12,1.75},{"M16 x 2",16,2},{"M20 x 2,5",20,2.5},{"M24 x 3",24,3},{"M30 x 3,5",30,3.5},{"M36 x 4",36,4}};
    case 1: return {{"1/4-20 UNC",6.35,25.4/20},{"5/16-18 UNC",7.9375,25.4/18},{"3/8-16 UNC",9.525,25.4/16},{"1/2-13 UNC",12.7,25.4/13},{"5/8-11 UNC",15.875,25.4/11},{"3/4-10 UNC",19.05,2.54},{"1-8 UNC",25.4,3.175}};
    case 2: return {{"1/4-28 UNF",6.35,25.4/28},{"5/16-24 UNF",7.9375,25.4/24},{"3/8-24 UNF",9.525,25.4/24},{"1/2-20 UNF",12.7,1.27},{"5/8-18 UNF",15.875,25.4/18},{"3/4-16 UNF",19.05,25.4/16},{"1-12 UNF",25.4,25.4/12}};
    case 3: case 4: return {{"G/R 1/8 - 28",9.728,25.4/28},{"G/R 1/4 - 19",13.157,25.4/19},{"G/R 3/8 - 19",16.662,25.4/19},{"G/R 1/2 - 14",20.955,25.4/14},{"G/R 3/4 - 14",26.441,25.4/14},{"G/R 1 - 11",33.249,25.4/11}};
    case 5: return {{"NPT 1/8 - 27",10.287,25.4/27},{"NPT 1/4 - 18",13.716,25.4/18},{"NPT 3/8 - 18",17.145,25.4/18},{"NPT 1/2 - 14",21.336,25.4/14},{"NPT 3/4 - 14",26.670,25.4/14},{"NPT 1 - 11,5",33.401,25.4/11.5}};
    case 6: return {{"Tr 8 x 1,5",8,1.5},{"Tr 10 x 2",10,2},{"Tr 12 x 3",12,3},{"Tr 16 x 4",16,4},{"Tr 20 x 4",20,4},{"Tr 24 x 5",24,5},{"Tr 30 x 6",30,6}};
    default: return {{"ACME 1/4-16",6.35,25.4/16},{"ACME 1/2-10",12.7,2.54},{"ACME 3/4-6",19.05,25.4/6},{"ACME 1-5",25.4,25.4/5},{"ACME 1 1/2-4",38.1,6.35}};
    }
}

static bool threadDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
                         const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    if (initial.firstBody >= 0 && (replaced < 0 || initial.firstBody < replaced) && !candidates.contains(initial.firstBody)) candidates.append(initial.firstBody);
    std::sort(candidates.begin(), candidates.end());
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Nella scena non ci sono solidi da filettare."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Thread;
    int selected = candidates.indexOf(definition.firstBody);
    if (selected < 0) selected = candidates.size() - 1, definition.firstBody = candidates.at(selected);

    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *surfaceDiagram = new ThreadDiagram(ThreadDiagram::Surface, &dialog);
    auto *profileDiagram = new ThreadDiagram(ThreadDiagram::Profile, &dialog);
    auto *extentDiagram = new ThreadDiagram(ThreadDiagram::Extent, &dialog);
    auto *bodyBox = new QComboBox(&dialog);
    for (int index : candidates) bodyBox->addItem(logicalBodyLabel(viewport, index));
    bodyBox->setCurrentIndex(selected);
    bodyBox->setEnabled(replaced < 0);
    auto *faceButton = new QPushButton(QStringLiteral("Scegli faccia nella vista…"), &dialog);
    faceButton->setCheckable(true);
    auto *faceInfoLabel = new QLabel(&dialog); faceInfoLabel->setWordWrap(true);
    auto *standardBox = new QComboBox(&dialog);
    standardBox->addItems({QStringLiteral("ISO metrico M (60°)"), QStringLiteral("Unified UNC (60°)"), QStringLiteral("Unified UNF (60°)"),
                           QStringLiteral("BSPP / G cilindrico (55°)"), QStringLiteral("BSPT / R conico (55°)"), QStringLiteral("NPT conico (60°)"),
                           QStringLiteral("ISO trapezoidale Tr (30°)"), QStringLiteral("ACME (29°)")});
    standardBox->setCurrentIndex(qBound(0, definition.thread.standard, 7));
    auto *sizeBox = new QComboBox(&dialog);
    auto *pitchBox = new ForgeCad::ExpressionSpinBox(&dialog);
    pitchBox->setDecimals(6); pitchBox->setRange(0.01, 100.0); pitchBox->setValue(definition.thread.pitch); pitchBox->setKeyboardTracking(false);
    auto *lengthBox = new ForgeCad::ExpressionSpinBox(&dialog);
    lengthBox->setDecimals(6); lengthBox->setRange(0.001, 1e6); lengthBox->setKeyboardTracking(false);
    auto *handBox = new QComboBox(&dialog); handBox->addItems({QStringLiteral("Destrorsa"), QStringLiteral("Sinistrorsa")}); handBox->setCurrentIndex(definition.thread.leftHanded ? 1 : 0);
    auto *reverseBox = new QCheckBox(QStringLiteral("Parti dall'altra estremita' della faccia"), &dialog); reverseBox->setChecked(definition.thread.reverse);
    auto *status = new QLabel(&dialog); status->setWordWrap(true);
    form->addRow(surfaceDiagram);
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Superficie:"), faceButton);
    form->addRow(faceInfoLabel);
    form->addRow(profileDiagram);
    form->addRow(QStringLiteral("Standard:"), standardBox);
    form->addRow(QStringLiteral("Misura nominale:"), sizeBox);
    form->addRow(QStringLiteral("Passo:"), pitchBox);
    form->addRow(extentDiagram);
    form->addRow(QStringLiteral("Lunghezza:"), lengthBox);
    form->addRow(QStringLiteral("Verso:"), handBox);
    form->addRow(reverseBox);
    form->addRow(wrappedNote(QStringLiteral("La geometria usa il profilo nominale. Tolleranze, giochi e maggiorazioni di lavorazione restano a carico del processo produttivo."), &dialog));
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); form->addRow(buttons);
    const PreviewScope scope(viewport, dialog, form, replaced);

    ForgeCad::ThreadFaceInfo faceInfo;
    bool faceValid = false, picking = false, initializing = true;
    const auto angle = [&] { const int s = standardBox->currentIndex(); return (s == 3 || s == 4) ? 55.0 : s == 6 ? 30.0 : s == 7 ? 29.0 : 60.0; };
    const auto setStatus = [status](const QString &text, bool error) { status->setStyleSheet(error ? QStringLiteral("color:#ff7a6a;") : QStringLiteral("color:#9fc6e8;")); status->setText(text); };
    const auto readFace = [&] {
        faceValid = false;
        const int body = candidates.at(bodyBox->currentIndex());
        const ForgeCad::ForgeBody &shape = viewport->extrusions().at(body).forgeBody;
        QString error;
        if (shape) faceValid = ForgeCad::forgeThreadFaceInfo(*shape, definition.thread.face, faceInfo, &error);
        if (faceValid) {
            faceInfoLabel->setText(QStringLiteral("%1 • diametro %2 • lunghezza disponibile %3%4")
                                   .arg(faceInfo.internal ? QStringLiteral("Foro interno") : QStringLiteral("Albero esterno"),
                                        ForgeCad::formatLength(faceInfo.diameter), ForgeCad::formatLength(faceInfo.length),
                                        faceInfo.conical ? QStringLiteral(" • conica") : QString()));
            lengthBox->setMaximum(qMax(0.001, faceInfo.length));
            if (initializing || lengthBox->value() > faceInfo.length) lengthBox->setValue(definition.thread.length > 0.0 ? qMin(definition.thread.length, faceInfo.length) : faceInfo.length);
        } else faceInfoLabel->setText(QStringLiteral("Nessuna faccia cilindrica o conica selezionata."));
        return error;
    };
    const auto populateSizes = [&] {
        const QVector<ThreadChoice> choices = threadChoices(standardBox->currentIndex());
        const QSignalBlocker blocker(sizeBox);
        sizeBox->clear();
        int best = 0, exact = -1; double delta = std::numeric_limits<double>::max();
        for (int k = 0; k < choices.size(); ++k) {
            const ThreadChoice &choice = choices.at(k);
            sizeBox->addItem(QString::fromLatin1(choice.name), choice.pitch);
            const double d = faceValid ? std::fabs(choice.diameter - faceInfo.diameter) : double(k);
            if (!definition.thread.designation.isEmpty() && QString::fromLatin1(choice.name) == definition.thread.designation) exact = k;
            if (d < delta) best = k, delta = d;
        }
        if (exact >= 0) best = exact;
        sizeBox->setCurrentIndex(best);
        if (initializing && definition.thread.pitch > 0.0) pitchBox->setValue(definition.thread.pitch);
        else if (!choices.isEmpty()) pitchBox->setValue(choices.at(best).pitch);
    };
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.firstBody = candidates.at(bodyBox->currentIndex());
        d.thread.standard = standardBox->currentIndex();
        d.thread.designation = sizeBox->currentText();
        d.thread.pitch = pitchBox->value();
        d.thread.length = lengthBox->value();
        d.thread.leftHanded = handBox->currentIndex() == 1;
        d.thread.reverse = reverseBox->isChecked();
        return d;
    };
    const auto refresh = [&] {
        const double a = angle(), p = pitchBox->value(), l = lengthBox->value();
        for (ThreadDiagram *diagram : {surfaceDiagram, profileDiagram, extentDiagram}) diagram->setValues(faceInfo.internal, a, p, l, handBox->currentIndex() == 1);
        faceButton->setChecked(picking);
        QVector<GeometryRef> marks;
        if (faceValid) marks.append(GeometryRef{5, candidates.at(bodyBox->currentIndex()), {}, definition.thread.face});
        viewport->setReferenceMarks(marks);
        if (picking) { setStatus(QStringLiteral("Clicca la faccia cilindrica o conica del foro o dell'albero; Esc annulla la scelta."), false); viewport->clearPreview(); return; }
        if (!faceValid) { setStatus(QStringLiteral("Seleziona una faccia cilindrica o conica."), true); viewport->clearPreview(); return; }
        setStatus(QString(), false);
        scope.request(current());
    };
    const auto beginPick = [&] {
        picking = true; refresh();
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
        if (!error.isEmpty()) { picking = false; setStatus(error, true); }
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        picking = false;
        if (picked && ref.kind == 5) {
            const int body = resultBodyForPick(viewport, candidates, ref.index);
            if (body >= 0) {
                bodyBox->setCurrentIndex(candidates.indexOf(body));
                definition.firstBody = body;
                definition.thread.face = ref.point;
                const QString error = readFace();
                populateSizes();
                if (!faceValid) setStatus(error, true);
            }
        }
        refresh();
    });
    QObject::connect(faceButton, &QPushButton::clicked, &dialog, [&] { if (picking) { picking = false; viewport->cancelReferencePick(); refresh(); } else beginPick(); });
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, [&] { definition.thread.face = {}; readFace(); populateSizes(); refresh(); });
    QObject::connect(standardBox, &QComboBox::currentIndexChanged, &dialog, [&] { definition.thread.designation.clear(); populateSizes(); refresh(); });
    QObject::connect(sizeBox, &QComboBox::currentIndexChanged, &dialog, [&] { if (sizeBox->currentIndex() >= 0) pitchBox->setValue(sizeBox->currentData().toDouble()); refresh(); });
    pitchBox->onReturn = refresh;
    lengthBox->onReturn = refresh;
    QObject::connect(handBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(reverseBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!faceValid) { setStatus(QStringLiteral("Seleziona prima una faccia cilindrica o conica."), true); return; }
        QApplication::setOverrideCursor(Qt::WaitCursor); const QString error = apply(current()); QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept(); else setStatus(error, true);
    });
    readFace(); populateSizes(); initializing = false; refresh();
    if (!faceValid && replaced < 0) QTimer::singleShot(0, &dialog, beginPick);
    bool accepted = false;
    { const WindowLock lock(window, viewport, &dialog); dialog.show(); refresh(); QEventLoop loop; QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit); loop.exec(); accepted = dialog.result() == QDialog::Accepted; }
    viewport->cancelReferencePick(); viewport->setReferencePickCallback(nullptr); viewport->setReferenceMarks({});
    return accepted;
}

// Booleana (nuova, o al posto del corpo `replaced`): operazione, il primo
// oggetto (A) e gli strumenti, anche piu' d'uno (A op B op C...: l'unione di
// tutti, l'intersezione di tutti, A meno tutti gli strumenti). Si offrono solo
// i corpi che esistono in quel punto della storia (resultBodiesBefore), non gli
// stadi intermedi; si scelgono anche cliccandoli nella vista (finestra non
// modale). Anteprima in ambra; la conferma chiama `apply` (l'errore resta nella
// finestra).
static bool booleanDialog(QMainWindow *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> indices = viewport->resultBodiesBefore(replaced);
    // In modifica gli operandi attuali restano scelti anche se non sono piu' l'ultimo stadio del loro corpo.
    for (int operand : QVector<int>{initial.firstBody, initial.secondBody} + initial.booleanTools)
        if (operand >= 0 && operand < viewport->extrusions().size() && (replaced < 0 || operand < replaced) && !indices.contains(operand))
            indices.append(operand);
    std::sort(indices.begin(), indices.end());
    if (indices.size() < 2) {
        QMessageBox::information(parent, title, QStringLiteral("Servono almeno due corpi nella scena (solidi, o un solido e una superficie)."));
        return false;
    }
    const auto label = [&](int index) {
        const bool used = index == initial.firstBody || index == initial.secondBody || initial.booleanTools.contains(index);
        return resultBodyLabel(viewport, index, !used);
    };
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *operationBox = new QComboBox(&dialog);
    operationBox->addItems({QStringLiteral("Unione"), QStringLiteral("Intersezione"), QStringLiteral("Differenza (A meno gli strumenti)")});
    operationBox->setCurrentIndex(qBound(0, initial.operation, 2));
    auto *firstBox = new QComboBox(&dialog);
    for (int index : indices) firstBox->addItem(label(index));
    int first = int(indices.indexOf(initial.firstBody));
    if (first < 0) first = 0;
    firstBox->setCurrentIndex(first);
    auto *toolList = new QListWidget(&dialog);
    toolList->setMinimumHeight(110);
    QVector<int> chosen;
    if (initial.secondBody >= 0) chosen = QVector<int>{initial.secondBody} + initial.booleanTools;
    for (int index : indices) {
        auto *item = new QListWidgetItem(label(index), toolList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(chosen.contains(index) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, index);
    }
    // Di partenza, con un solo candidato oltre ad A, e' quello.
    if (chosen.isEmpty() && indices.size() == 2) toolList->item(first == 0 ? 1 : 0)->setCheckState(Qt::Checked);
    auto *pickFirst = new QPushButton(QStringLiteral("A dalla vista"), &dialog);
    auto *pickTools = new QPushButton(QStringLiteral("Strumenti dalla vista"), &dialog);
    for (QPushButton *button : {pickFirst, pickTools}) {
        button->setCheckable(true);
        button->setToolTip(QStringLiteral("Clicca i corpi nella vista (di nuovo uno strumento per toglierlo); Esc nella vista termina"));
    }
    auto *pickRow = new QWidget(&dialog);
    auto *pickLayout = new QHBoxLayout(pickRow);
    pickLayout->setContentsMargins(0, 0, 0, 0);
    pickLayout->addWidget(pickFirst);
    pickLayout->addWidget(pickTools);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    form->addRow(QStringLiteral("Operazione:"), operationBox);
    form->addRow(QStringLiteral("Oggetto A:"), firstBox);
    form->addRow(QStringLiteral("Strumenti:"), toolList);
    form->addRow(QString(), pickRow);
    form->addRow(wrappedNote(QStringLiteral("Spunta uno o piu' strumenti: l'operazione li applica ad A uno dopo l'altro."), &dialog));
    form->addRow(status);
    const PreviewScope scope(viewport, dialog, form, replaced);
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    const auto current = [&] {
        ExtrusionObject body = initial;
        body.operation = operationBox->currentIndex();
        body.firstBody = indices.at(firstBox->currentIndex());
        QVector<int> tools;
        for (int row = 0; row < toolList->count(); ++row) {
            QListWidgetItem *item = toolList->item(row);
            const int index = item->data(Qt::UserRole).toInt();
            if (item->checkState() == Qt::Checked && index != body.firstBody) tools.append(index);
        }
        body.secondBody = tools.value(0, -1);
        body.booleanTools = tools.mid(1);
        return body;
    };
    // 0: nessuna scelta nella vista, 1: A, 2: strumenti. Durante la scelta
    // l'anteprima si toglie: nella vista restano i corpi da cliccare.
    int picking = 0;
    const auto refresh = [&] {
        // A non e' uno strumento.
        const int a = indices.at(firstBox->currentIndex());
        {
            const QSignalBlocker blocker(toolList);  // setFlags darebbe itemChanged
            for (int row = 0; row < toolList->count(); ++row) {
                QListWidgetItem *item = toolList->item(row);
                const bool self = item->data(Qt::UserRole).toInt() == a;
                item->setFlags(self ? item->flags() & ~Qt::ItemIsEnabled : item->flags() | Qt::ItemIsEnabled);
            }
        }
        pickFirst->setChecked(picking == 1);
        pickTools->setChecked(picking == 2);
        const ExtrusionObject body = current();
        viewport->setPickBodies(picking ? indices : QVector<int>());
        if (picking) {
            scope.label->setText(QStringLiteral("scelta dei corpi nella vista"));
            viewport->clearPreview();
            return;
        }
        if (body.secondBody < 0) {
            scope.label->setText(QStringLiteral("scegli almeno uno strumento"));
            viewport->clearPreview();
            return;
        }
        scope.request(body);
    };
    const auto startPick = [&](int mode) {
        if (picking == mode) {
            picking = 0;
            viewport->cancelReferencePick();
            setStatus(QString(), false);
            refresh();
            return;
        }
        picking = mode;
        refresh();  // via l'anteprima prima di scegliere
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
        if (!error.isEmpty()) {
            picking = 0;
            setStatus(error, true);
        } else {
            setStatus(mode == 1 ? QStringLiteral("Clicca il corpo A nella vista.")
                                : QStringLiteral("Clicca gli strumenti nella vista (di nuovo per toglierli); Esc nella vista termina."), false);
        }
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (!picked || !picking) {
            picking = 0;
            setStatus(QString(), false);
            refresh();
            return;
        }
        const int body = resultBodyForPick(viewport, indices, ref.index);
        if (body < 0) {
            setStatus(QStringLiteral("Quel corpo non e' tra quelli disponibili in questo punto della storia."), true);
        } else if (picking == 1) {
            const QSignalBlocker blocker(firstBox);
            firstBox->setCurrentIndex(int(indices.indexOf(body)));
            picking = 2;  // poi gli strumenti
            setStatus(QStringLiteral("A: %1. Ora clicca gli strumenti; Esc nella vista termina.").arg(resultBodyLabel(viewport, body, false)), false);
        } else if (body == indices.at(firstBox->currentIndex())) {
            setStatus(QStringLiteral("Quel corpo e' A: non puo' essere anche uno strumento."), true);
        } else {
            for (int row = 0; row < toolList->count(); ++row) {
                QListWidgetItem *item = toolList->item(row);
                if (item->data(Qt::UserRole).toInt() != body) continue;
                const QSignalBlocker blocker(toolList);
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            }
            setStatus(QString(), false);
        }
        if (picking) {
            const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);  // si continua a scegliere
            if (!error.isEmpty()) picking = 0;
        }
        refresh();
    });
    QObject::connect(pickFirst, &QPushButton::clicked, &dialog, [&] { startPick(1); });
    // Di default la scelta nella vista e' attiva: A se manca, poi gli strumenti.
    if (replaced < 0) startPick(indices.contains(initial.firstBody) ? 2 : 1);
    QObject::connect(pickTools, &QPushButton::clicked, &dialog, [&] { startPick(2); });
    QObject::connect(operationBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(firstBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(toolList, &QListWidget::itemChanged, &dialog, refresh);
    refresh();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    const bool accepted = runUntilAppliedModeless(parent, viewport, dialog, form, buttons, [&] {
        if (picking) {
            picking = 0;
            viewport->cancelReferencePick();
            viewport->setPickBodies({});
            refresh();
        }
        const ExtrusionObject body = current();
        if (body.secondBody < 0) return QStringLiteral("Scegli almeno uno strumento.");
        return apply(body);
    });
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setPickBodies({});
    return accepted;
}

// Offset di superficie (nuovo, o al posto del corpo `replaced`): corpo, facce
// scelte nella vista (ogni clic aggiunge o toglie una faccia; nessuna = tutte),
// distanza con il verso. Finestra non modale come lo spostamento: la vista
// resta attiva per scegliere le facce. Anteprima in ambra; la conferma chiama
// `apply` (l'errore resta nella finestra).
// Con initial.feature == DeleteFace e' la finestra di *Elimina facce*: le facce
// scelte (almeno una) si tolgono dal corpo, niente distanza. Il corpo e' quello
// della faccia cliccata e la finestra non forza la visibilita' di alcun corpo.
// Con initial.feature == Shell e' il *Guscio*: un solido svuotato con lo
// spessore verso l'interno, le facce scelte (anche nessuna) tolte per l'apertura.
// Sformo con selezione multipla e piano neutro associativo.
static bool draftDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
                        const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int i) { return !viewport->extrusions().at(i).forgeBody; }), candidates.end());
    if (initial.firstBody >= 0 && initial.firstBody < viewport->extrusions().size()
        && (replaced < 0 || initial.firstBody < replaced) && !candidates.contains(initial.firstBody)) candidates.append(initial.firstBody);
    if (candidates.isEmpty()) { QMessageBox::information(window, title, QStringLiteral("Crea prima un solido o una superficie da sformare.")); return false; }
    ExtrusionObject definition = initial;
    definition.feature = BodyFeature::Draft;
    definition.operation = -1;
    if (!candidates.contains(definition.firstBody)) definition.firstBody = candidates.last();
    if (definition.draftNeutral.kind < 0) { definition.draftNeutral.kind = 1; definition.draftNeutral.index = 0; }
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int i : candidates) bodyBox->addItem(logicalBodyLabel(viewport, i));
    bodyBox->setCurrentIndex(candidates.indexOf(definition.firstBody));
    bodyBox->setEnabled(replaced < 0);
    auto *faces = new QPushButton(QStringLiteral("Scegli facce"), &dialog); faces->setCheckable(true);
    auto *clear = new QPushButton(QStringLiteral("Azzera facce"), &dialog);
    auto *faceCount = new QLabel(&dialog);
    auto *neutralBox = new QComboBox(&dialog);
    neutralBox->addItems({QStringLiteral("XY"), QStringLiteral("XZ"), QStringLiteral("YZ"), QStringLiteral("Riferimento nella vista")});
    neutralBox->setCurrentIndex(definition.draftNeutral.kind == 1 ? qBound(0, definition.draftNeutral.index, 2) : 3);
    auto *neutralPick = new QPushButton(QStringLiteral("Scegli piano / faccia neutra"), &dialog); neutralPick->setCheckable(true);
    auto *neutralLabel = new QLabel(&dialog); neutralLabel->setWordWrap(true);
    auto *angle = new ForgeCad::ExpressionSpinBox(&dialog);
    angle->setDecimals(4); angle->setRange(-88.9999, 88.9999); angle->setSuffix(QStringLiteral("°"));
    angle->setValue(definition.draftAngle); angle->setKeyboardTracking(false);
    auto *reverse = new QCheckBox(QStringLiteral("Inverti direzione di estrazione"), &dialog); reverse->setChecked(definition.draftReverse);
    auto *status = new QLabel(&dialog); status->setWordWrap(true); status->setMaximumWidth(360);
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Facce:"), faces); form->addRow(faceCount); form->addRow(clear);
    form->addRow(QStringLiteral("Piano neutro:"), neutralBox); form->addRow(neutralPick); form->addRow(neutralLabel);
    form->addRow(QStringLiteral("Angolo:"), angle); form->addRow(reverse);
    form->addRow(wrappedNote(QStringLiteral("Il piano neutro resta fisso. L'estrazione segue la sua normale: un angolo positivo restringe il corpo in quella direzione, uno negativo lo allarga. Supportati solidi convessi e superfici cucite con facce piane e bordi rettilinei. I bordi aperti conservano la quota lungo l’estrazione."), &dialog));
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); form->addRow(buttons);
    PreviewScope scope(viewport, dialog, form, replaced);
    int picking = 0;  // 1 facce, 2 piano neutro
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.draftAngle = angle->value(); d.draftReverse = reverse->isChecked();
        return d;
    };
    const auto refresh = [&] {
        faceCount->setText(QStringLiteral("%1 facce scelte").arg(definition.offsetFaces.size()));
        neutralLabel->setText(ForgeCad::geometryRefText(definition.draftNeutral, viewport->sketches(), viewport->extrusions()));
        faces->setChecked(picking == 1); neutralPick->setChecked(picking == 2);
        QVector<GeometryRef> marks;
        for (const EdgePoint &point : definition.offsetFaces) { GeometryRef r; r.kind = 5; r.index = definition.firstBody; r.point = point; marks.append(r); }
        if (definition.draftNeutral.kind >= 0) marks.append(definition.draftNeutral);
        viewport->setReferenceMarks(marks);
        if (picking || definition.offsetFaces.isEmpty() || definition.draftNeutral.kind < 0) {
            scope.label->setText(picking ? QStringLiteral("termina la scelta per vedere l'anteprima")
                                        : QStringLiteral("scegli le facce e il piano neutro"));
            viewport->clearPreview();
            return;
        }
        scope.request(current());
    };
    const auto beginPick = [&](int mode) {
        viewport->cancelReferencePick();
        picking = mode;
        const QString error = viewport->beginReferencePick(mode == 1 ? ForgeCad::DatumRoleFace : ForgeCad::DatumRolePlane, replaced);
        if (!error.isEmpty()) { picking = 0; status->setText(error); }
        else status->setText(mode == 1 ? QStringLiteral("Clicca le facce; un secondo clic le toglie. Esc termina la scelta.") : QStringLiteral("Clicca una faccia piana o un piano di costruzione."));
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        const int mode = picking;
        picking = 0;
        if (!picked) { status->clear(); refresh(); return; }
        if (mode == 2) {
            ForgeCad::ResolvedRef resolved;
            QString error;
            if (ForgeCad::resolveGeometryRef(ref, replaced < 0 ? viewport->extrusions().size() : replaced, viewport->sketches(), viewport->extrusions(), resolved, &error) && resolved.hasPlane) {
                definition.draftNeutral = ref;
                const QSignalBlocker blocker(neutralBox); neutralBox->setCurrentIndex(ref.kind == 1 ? ref.index : 3);
                status->clear();
            } else status->setText(error.isEmpty() ? QStringLiteral("Il riferimento deve essere piano.") : error);
        } else if (mode == 1) {
            const int body = ref.kind == 5 ? resultBodyForPick(viewport, candidates, ref.index) : -1;
            if (body != definition.firstBody) status->setText(QStringLiteral("Scegli una faccia del corpo indicato nel pannello."));
            else {
                const auto &shape = *viewport->extrusions().at(body).forgeBody;
                const auto face = ForgeCad::resolveFaceReference(shape, ref.point, 1e-6);
                int existing = -1;
                for (int i = 0; i < definition.offsetFaces.size(); ++i)
                    if (ForgeCad::resolveFaceReference(shape, definition.offsetFaces.at(i), 1e-6) == face) existing = i;
                if (face.valid()) {
                    if (existing >= 0) definition.offsetFaces.remove(existing); else definition.offsetFaces.append(ref.point);
                }
            }
            // Selezione continua: l'anteprima parte alla fine della scelta.
            picking = 1;
            const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
            if (!error.isEmpty()) { picking = 0; status->setText(error); }
        }
        refresh();
    });
    QObject::connect(faces, &QPushButton::clicked, &dialog, [&] {
        if (picking == 1) { viewport->cancelReferencePick(); picking = 0; status->clear(); refresh(); } else beginPick(1);
    });
    QObject::connect(neutralPick, &QPushButton::clicked, &dialog, [&] {
        if (picking == 2) { viewport->cancelReferencePick(); picking = 0; refresh(); } else beginPick(2);
    });
    QObject::connect(clear, &QPushButton::clicked, &dialog, [&] { definition.offsetFaces.clear(); refresh(); });
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        viewport->cancelReferencePick(); picking = 0; definition.firstBody = candidates.at(bodyBox->currentIndex()); definition.offsetFaces.clear(); refresh();
    });
    QObject::connect(neutralBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        if (neutralBox->currentIndex() == 3) { beginPick(2); return; }
        viewport->cancelReferencePick(); picking = 0;
        definition.draftNeutral = {}; definition.draftNeutral.kind = 1; definition.draftNeutral.index = neutralBox->currentIndex(); refresh();
    });
    QObject::connect(angle, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(reverse, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        viewport->cancelReferencePick(); picking = 0;
        if (definition.offsetFaces.isEmpty()) { status->setText(QStringLiteral("Scegli almeno una faccia.")); refresh(); return; }
        const QString error = apply(current());
        if (error.isEmpty()) dialog.accept(); else status->setText(error);
    });
    QTimer::singleShot(0, &dialog, [&] { if (definition.offsetFaces.isEmpty()) beginPick(1); else refresh(); });
    const bool accepted = runModeless(window, viewport, dialog);
    viewport->cancelReferencePick(); viewport->setReferencePickCallback(nullptr); viewport->setReferenceMarks({});
    return accepted;
}

// Curve proiettate (ProjectedCurve): corpo, schizzo e verso. Il taglio con
// la proiezione di uno schizzo e' un metodo di Taglia superficie (trimDialog).
static bool projectionDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
                             const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int i) { return !viewport->extrusions().at(i).forgeBody; }), candidates.end());
    if (initial.firstBody >= 0 && initial.firstBody < viewport->extrusions().size() && viewport->extrusions().at(initial.firstBody).forgeBody
        && (replaced < 0 || initial.firstBody < replaced) && !candidates.contains(initial.firstBody))
        candidates.append(initial.firstBody);
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Crea prima un solido o una superficie su cui proiettare."));
        return false;
    }
    if (viewport->sketches().isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Crea prima lo schizzo da proiettare."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::ProjectedCurve;
    if (!candidates.contains(definition.firstBody)) definition.firstBody = candidates.last();
    if (definition.sketchIndex < 0 || definition.sketchIndex >= viewport->sketches().size()) definition.sketchIndex = viewport->sketches().size() - 1;
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int i : candidates) bodyBox->addItem(logicalBodyLabel(viewport, i));
    bodyBox->setCurrentIndex(candidates.indexOf(definition.firstBody));
    auto *sketchBox = new QComboBox(&dialog);
    for (const SketchObject &sketch : viewport->sketches()) sketchBox->addItem(sketch.name);
    sketchBox->setCurrentIndex(definition.sketchIndex);
    auto *reverse = new QCheckBox(QStringLiteral("Inverti il verso della proiezione"), &dialog);
    reverse->setChecked(definition.projectionReverse);
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Schizzo:"), sketchBox);
    form->addRow(reverse);
    form->addRow(wrappedNote(QStringLiteral("Ogni entita' dello schizzo (escluse quelle di costruzione) va lungo la normale del suo piano sulla "
                                            "prima faccia del corpo che incontra: la curva sta sulla superficie esatta entro 1e-8 mm. Le entita' "
                                            "collegate danno una sola curva per catena. Di default si proietta verso il corpo. Per tagliare una "
                                            "superficie con lo schizzo proiettato: Taglia superficie, strumento lo schizzo."), &dialog));
    auto *result = new QLabel(&dialog);
    result->setWordWrap(true);
    result->setMaximumWidth(360);
    result->setStyleSheet(QStringLiteral("color: #9fc6e8;"));
    form->addRow(QStringLiteral("Esito:"), result);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    PreviewScope scope(viewport, dialog, form, replaced);
    QLabel *previewLabel = scope.label;
    viewport->setPreviewCallback([previewLabel, result, viewport](const QString &error) {
        previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
        result->setText(error.isEmpty() ? viewport->previewNotice() : QString());
    });
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.firstBody = candidates.at(bodyBox->currentIndex());
        d.sketchIndex = sketchBox->currentIndex();
        d.projectionReverse = reverse->isChecked();
        return d;
    };
    const auto refresh = [&] { scope.request(current()); };
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(sketchBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(reverse, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString error = apply(current());
        if (error.isEmpty()) dialog.accept();
        else status->setText(error);
    });
    QTimer::singleShot(0, &dialog, refresh);
    return runModeless(window, viewport, dialog);
}

// Spessore (Thicken): superficie (o facce scelte di un corpo), spessore, lato
// e direzione (normale delle facce, un asse o un riferimento della vista).
static bool thickenDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int i) { return !viewport->extrusions().at(i).forgeBody; }), candidates.end());
    if (initial.firstBody >= 0 && initial.firstBody < viewport->extrusions().size() && viewport->extrusions().at(initial.firstBody).forgeBody
        && (replaced < 0 || initial.firstBody < replaced) && !candidates.contains(initial.firstBody))
        candidates.append(initial.firstBody);
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Crea prima una superficie da ispessire."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Thicken;
    if (!candidates.contains(definition.firstBody)) {
        // Di partenza l'ultima superficie, altrimenti l'ultimo corpo.
        definition.firstBody = candidates.last();
        for (int i : candidates)
            if (viewport->extrusions().at(i).forgeBody->isSheet()) definition.firstBody = i;
    }
    if (!(definition.distance > 0.0)) definition.distance = 1.0;
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int i : candidates) bodyBox->addItem(logicalBodyLabel(viewport, i));
    bodyBox->setCurrentIndex(candidates.indexOf(definition.firstBody));
    auto *faces = new QPushButton(QStringLiteral("Scegli facce"), &dialog);
    faces->setCheckable(true);
    auto *all = new QPushButton(QStringLiteral("Tutta la superficie"), &dialog);
    auto *faceCount = new QLabel(&dialog);
    auto *thickness = new ForgeCad::ExpressionSpinBox(&dialog);
    thickness->setDecimals(6);
    thickness->setRange(1e-6, 1e6);
    thickness->setValue(definition.distance);
    thickness->setKeyboardTracking(false);
    auto *sideBox = new QComboBox(&dialog);
    sideBox->addItems({QStringLiteral("Nel verso della direzione"), QStringLiteral("Nel verso opposto"), QStringLiteral("Meta' per parte")});
    sideBox->setCurrentIndex(qBound(0, definition.thickenSide, 2));
    auto *directionBox = new QComboBox(&dialog);
    directionBox->addItems({QStringLiteral("Normale della superficie"), QStringLiteral("Asse X"), QStringLiteral("Asse Y"), QStringLiteral("Asse Z"),
                            QStringLiteral("Riferimento nella vista")});
    const GeometryRef &startDirection = definition.thickenDirection;
    directionBox->setCurrentIndex(startDirection.kind < 0 ? 0 : startDirection.kind == 2 ? 1 + qBound(0, startDirection.index, 2) : 4);
    auto *directionPick = new QPushButton(QStringLiteral("Scegli retta, piano o faccia"), &dialog);
    directionPick->setCheckable(true);
    auto *directionLabel = new QLabel(&dialog);
    directionLabel->setWordWrap(true);
    directionLabel->setMaximumWidth(360);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMaximumWidth(360);
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Facce:"), faces);
    form->addRow(faceCount);
    form->addRow(all);
    form->addRow(QStringLiteral("Spessore:"), thickness);
    form->addRow(QStringLiteral("Direzione:"), directionBox);
    form->addRow(directionPick);
    form->addRow(directionLabel);
    form->addRow(QStringLiteral("Lato:"), sideBox);
    form->addRow(wrappedNote(QStringLiteral("Lungo la normale: superficie a distanza e pareti lungo la normale ai bordi; le facce devono essere unite da "
                                            "spigoli tangenti e lo spessore non puo' superare il raggio di curvatura. Lungo una direzione (asse, retta, "
                                            "normale di un piano o di una faccia piana): copia traslata e pareti estruse, esatte, anche con spigoli vivi; "
                                            "ogni retta parallela alla direzione deve incontrare la superficie una volta sola. Tutta la superficie diventa "
                                            "un solido nello stesso corpo; facce scelte di un corpo danno un corpo nuovo."), &dialog));
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    PreviewScope scope(viewport, dialog, form, replaced);
    int picking = 0;  // 1 facce, 2 direzione
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.firstBody = candidates.at(bodyBox->currentIndex());
        d.distance = thickness->value();
        d.thickenSide = sideBox->currentIndex();
        return d;
    };
    const auto refresh = [&] {
        faceCount->setText(definition.offsetFaces.isEmpty() ? QStringLiteral("tutta la superficie")
                                                            : QStringLiteral("%1 facce scelte").arg(definition.offsetFaces.size()));
        faces->setChecked(picking == 1);
        directionPick->setChecked(picking == 2);
        directionPick->setVisible(directionBox->currentIndex() == 4);
        directionLabel->setText(definition.thickenDirection.kind < 0 ? QStringLiteral("normale delle facce")
                                                                     : ForgeCad::geometryRefText(definition.thickenDirection, viewport->sketches(), viewport->extrusions()));
        QVector<GeometryRef> marks;
        for (const EdgePoint &point : definition.offsetFaces) {
            GeometryRef r;
            r.kind = 5;
            r.index = candidates.at(bodyBox->currentIndex());
            r.point = point;
            marks.append(r);
        }
        if (definition.thickenDirection.kind >= 0) marks.append(definition.thickenDirection);
        viewport->setReferenceMarks(marks);
        if (picking) {
            scope.label->setText(QStringLiteral("termina la scelta per vedere l'anteprima"));
            viewport->clearPreview();
            return;
        }
        if (directionBox->currentIndex() == 4 && definition.thickenDirection.kind < 0) {
            scope.label->setText(QStringLiteral("scegli la direzione nella vista"));
            viewport->clearPreview();
            return;
        }
        scope.request(current());
    };
    const auto beginPick = [&](int mode) {
        viewport->cancelReferencePick();
        picking = mode;
        const int roles = mode == 1 ? ForgeCad::DatumRoleFace : ForgeCad::DatumRoleLine | ForgeCad::DatumRolePlane | ForgeCad::DatumRoleFace;
        const QString error = viewport->beginReferencePick(roles, replaced);
        if (!error.isEmpty()) {
            picking = 0;
            status->setText(error);
        } else {
            status->setText(mode == 1 ? QStringLiteral("Clicca le facce; un secondo clic le toglie. Esc termina la scelta.")
                                      : QStringLiteral("Clicca una retta, un asse, un piano o una faccia piana: la direzione e' la retta o la normale."));
        }
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        const int mode = picking;
        picking = 0;
        if (!picked) {
            status->clear();
            refresh();
            return;
        }
        if (mode == 2) {
            ForgeCad::ResolvedRef resolved;
            QString error;
            if (ForgeCad::resolveGeometryRef(ref, replaced < 0 ? viewport->extrusions().size() : replaced, viewport->sketches(), viewport->extrusions(),
                                             resolved, &error)
                && (resolved.hasLine || resolved.hasPlane)) {
                definition.thickenDirection = ref;
                status->clear();
            } else {
                status->setText(error.isEmpty() ? QStringLiteral("Il riferimento deve essere una retta, un piano o una faccia piana.") : error);
            }
        } else if (mode == 1) {
            const int body = ref.kind == 5 ? resultBodyForPick(viewport, candidates, ref.index) : -1;
            if (body != candidates.at(bodyBox->currentIndex())) {
                status->setText(QStringLiteral("Scegli una faccia del corpo indicato nel pannello."));
            } else {
                const auto &shape = *viewport->extrusions().at(body).forgeBody;
                const auto face = ForgeCad::resolveFaceReference(shape, ref.point, 1e-6);
                int existing = -1;
                for (int i = 0; i < definition.offsetFaces.size(); ++i)
                    if (ForgeCad::resolveFaceReference(shape, definition.offsetFaces.at(i), 1e-6) == face) existing = i;
                if (face.valid()) {
                    if (existing >= 0) definition.offsetFaces.remove(existing);
                    else definition.offsetFaces.append(ref.point);
                }
            }
            // Scelta continua: l'anteprima parte alla fine.
            picking = 1;
            const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
            if (!error.isEmpty()) {
                picking = 0;
                status->setText(error);
            }
        }
        refresh();
    });
    QObject::connect(faces, &QPushButton::clicked, &dialog, [&] {
        if (picking == 1) {
            viewport->cancelReferencePick();
            picking = 0;
            status->clear();
            refresh();
        } else {
            beginPick(1);
        }
    });
    QObject::connect(directionPick, &QPushButton::clicked, &dialog, [&] {
        if (picking == 2) {
            viewport->cancelReferencePick();
            picking = 0;
            refresh();
        } else {
            beginPick(2);
        }
    });
    QObject::connect(all, &QPushButton::clicked, &dialog, [&] {
        definition.offsetFaces.clear();
        refresh();
    });
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        viewport->cancelReferencePick();
        picking = 0;
        definition.offsetFaces.clear();
        refresh();
    });
    QObject::connect(directionBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        const int index = directionBox->currentIndex();
        if (index == 4) {
            beginPick(2);
            return;
        }
        viewport->cancelReferencePick();
        picking = 0;
        definition.thickenDirection = {};
        if (index >= 1) {
            definition.thickenDirection.kind = 2;
            definition.thickenDirection.index = index - 1;
        }
        refresh();
    });
    QObject::connect(thickness, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(sideBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        viewport->cancelReferencePick();
        picking = 0;
        if (directionBox->currentIndex() == 4 && definition.thickenDirection.kind < 0) {
            status->setText(QStringLiteral("Scegli la direzione nella vista."));
            return;
        }
        const QString error = apply(current());
        if (error.isEmpty()) dialog.accept();
        else status->setText(error);
    });
    QTimer::singleShot(0, &dialog, refresh);
    const bool accepted = runModeless(window, viewport, dialog);
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    return accepted;
}

static bool offsetDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                         const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    const bool shell = initial.feature == BodyFeature::Shell;
    const bool deleteFace = initial.feature == BodyFeature::DeleteFace;
    // Eliminazione e guscio: le facce scelte si tolgono.
    const bool removal = deleteFace || shell;
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);  // solo i corpi esistenti, non gli stadi intermedi
    if (shell) candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int k) { return !bodies.at(k).solid; }), candidates.end());
    if (replaced >= 0 && initial.firstBody >= 0 && initial.firstBody < replaced && !candidates.contains(initial.firstBody)) {
        candidates.append(initial.firstBody);
        std::sort(candidates.begin(), candidates.end());
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, shell ? QStringLiteral("Nella scena non ci sono solidi da svuotare.")
                                                      : QStringLiteral("Nella scena non ci sono solidi o superfici."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = shell ? BodyFeature::Shell : removal ? BodyFeature::DeleteFace : BodyFeature::SurfaceOffset;
    // Senza un corpo selezionato prima del comando lo da' la prima faccia
    // cliccata (come per i raccordi): fino ad allora niente anteprima.
    bool waiting = replaced < 0 && !candidates.contains(definition.firstBody);
    if (waiting) definition.firstBody = candidates.last();
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    QComboBox *bodyBox = nullptr;
    if (!deleteFace) {
        bodyBox = new QComboBox(&dialog);
        for (int index : candidates) bodyBox->addItem(resultBodyLabel(viewport, index, false));  // i corpi, non le funzioni
        bodyBox->setCurrentIndex(int(candidates.indexOf(definition.firstBody)));
        bodyBox->setEnabled(replaced < 0);
    }
    auto *facesLabel = new QLabel(&dialog);
    facesLabel->setWordWrap(true);
    auto *pickButton = new QPushButton(QStringLiteral("Scegli le facce nella vista"), &dialog);
    pickButton->setCheckable(true);
    pickButton->setToolTip(QStringLiteral("Ogni clic su una faccia del corpo la aggiunge o la toglie; Esc nella vista termina la scelta"));
    auto *clearButton = new QPushButton(QStringLiteral("Tutte le facce"), &dialog);
    clearButton->setToolTip(QStringLiteral("Toglie la scelta: l'offset vale per tutte le facce del corpo"));
    auto *faceRow = new QWidget(&dialog);
    auto *faceLayout = new QHBoxLayout(faceRow);
    faceLayout->setContentsMargins(0, 0, 0, 0);
    faceLayout->addWidget(pickButton, 1);
    faceLayout->addWidget(clearButton);
    auto *distanceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    distanceBox->setDecimals(6);
    distanceBox->setRange(0.0, 100000.0);
    distanceBox->setObjectName(QStringLiteral("offsetDistance"));
    distanceBox->setValue(removal && definition.distance == 0.0 ? 1.0 : std::fabs(definition.distance));
    distanceBox->setKeyboardTracking(false);
    auto *flipButton = new QPushButton(ForgeCad::commandIcon(QStringLiteral("reverseDirection")), QStringLiteral("Inverti il verso"), &dialog);
    flipButton->setCheckable(true);
    flipButton->setChecked(definition.distance < 0.0);
    flipButton->setToolTip(QStringLiteral("Spento: verso l'esterno (lungo la normale uscente); acceso: verso l'interno"));
    if (bodyBox) form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Facce:"), facesLabel);
    form->addRow(QString(), faceRow);
    if (shell) {
        flipButton->hide();
        clearButton->setText(QStringLiteral("Azzera"));
        clearButton->setToolTip(QStringLiteral("Nessuna apertura: il solido resta chiuso con una cavita' interna"));
        form->addRow(QStringLiteral("Spessore:"), distanceBox);
        form->addRow(wrappedNote(QStringLiteral("Il solido si svuota lasciando pareti dello spessore dato verso l'interno; "
                                                "le facce scelte si tolgono e fanno l'apertura (nessuna: cavita' chiusa). "
                                                "Clicca un altro solido nella vista per cambiarlo."), &dialog));
    } else if (removal) {
        distanceBox->hide();
        flipButton->hide();
        clearButton->setText(QStringLiteral("Azzera"));
        clearButton->setToolTip(QStringLiteral("Toglie tutte le facce scelte"));
        form->addRow(wrappedNote(QStringLiteral("Le facce scelte si tolgono dal corpo: un solido diventa una superficie aperta "
                                                "(le facce restanti, cucite lungo gli spigoli comuni). "
                                                "Clicca un altro corpo nella vista per cambiarlo."), &dialog));
    } else {
        form->addRow(QStringLiteral("Distanza (0 = copia):"), distanceBox);
        form->addRow(QString(), flipButton);
        form->addRow(wrappedNote(QStringLiteral("A distanza zero si copiano esattamente le facce, conservando i bordi comuni. "
                                               "Il corpo originale resta disponibile. Per l'offset, con la cucitura attiva "
                                               "i bordi comuni vengono estesi e rifilati; disattivandola gli spigoli vivi si separano."), &dialog));
    }
    auto *sewBox = new QCheckBox(QStringLiteral("Mantieni la cucitura (estendi e rifila)"), &dialog);
    sewBox->setChecked(definition.offsetSew);
    sewBox->setVisible(!removal);
    if (!removal) form->addRow(sewBox);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(280);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    bool picking = false;
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    const auto current = [&] {
        ExtrusionObject d = definition;
        if (bodyBox) d.firstBody = candidates.at(bodyBox->currentIndex());
        d.offsetSew = sewBox->isChecked();
        d.distance = (flipButton->isChecked() ? -1.0 : 1.0) * distanceBox->value();
        return d;
    };
    // Faccia del corpo di un riferimento (per togliere una faccia gia' scelta).
    const auto faceOf = [&](const EdgePoint &point) {
        const ExtrusionObject &body = viewport->extrusions().at(definition.firstBody);
        if (!body.forgeBody) return ForgeCad::Kernel::FaceId();
        return ForgeCad::resolveFaceReference(*body.forgeBody, point, 1e-6);
    };
    const auto refresh = [&] {
        sewBox->setEnabled(removal || distanceBox->value() != 0.0);
        flipButton->setEnabled(removal || distanceBox->value() != 0.0);
        if (waiting) {
            if (shell) viewport->setPickBodies(picking ? candidates : QVector<int>());
            facesLabel->setText(QStringLiteral("clicca nella vista una faccia del corpo"));
            pickButton->setChecked(picking);
            viewport->setReferenceMarks({});
            if (picking) setStatus(QStringLiteral("Clicca una faccia: sceglie anche il corpo. Esc nella vista termina la scelta."), false);
            scope->label->setText(QStringLiteral("scegli il corpo cliccandone una faccia"));
            viewport->clearPreview();
            return;
        }
        facesLabel->setText(definition.offsetFaces.isEmpty()
                                ? (shell ? QStringLiteral("nessuna apertura") : removal ? QStringLiteral("nessuna faccia scelta") : QStringLiteral("tutte le facce del corpo"))
                                : QStringLiteral("%1 facce scelte").arg(definition.offsetFaces.size()));
        // Il guscio conserva il vecchio isolamento dei candidati. Elimina facce
        // lascia invece invariata la vista: i corpi nascosti restano nascosti e
        // non intercettano il picking di un corpo visibile racchiuso al loro interno.
        if (shell) viewport->setPickBodies(picking ? candidates : QVector<int>());
        pickButton->setChecked(picking);
        QVector<GeometryRef> marks;
        for (const EdgePoint &point : definition.offsetFaces) {
            GeometryRef ref;
            ref.kind = 5;
            ref.index = definition.firstBody;
            ref.point = point;
            marks.append(ref);
        }
        viewport->setReferenceMarks(marks);
        if (picking) setStatus(QStringLiteral("Clicca le facce del corpo (di nuovo per toglierle); Esc nella vista termina la scelta."), false);
        if (removal) {
            if (picking) {
                scope->label->setText(QStringLiteral("scelta delle facce (Esc nella vista per l'anteprima)"));
                viewport->clearPreview();
            } else if (definition.offsetFaces.isEmpty() && !shell) {
                scope->label->setText(QStringLiteral("scegli le facce da eliminare"));
                viewport->clearPreview();
            } else if (shell && !(distanceBox->value() > 0.0)) {
                setStatus(QStringLiteral("Lo spessore deve essere maggiore di zero."), true);
                viewport->clearPreview();
            } else {
                scope->request(current());
            }
            return;
        }
        scope->request(current());
    };
    const auto startPicking = [&] {
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
        picking = error.isEmpty();
        if (!picking) setStatus(error, true);
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (!picked) {
            picking = false;
            setStatus(QString(), false);
            refresh();
            return;
        }
        const int body = ref.kind == 5 ? resultBodyForPick(viewport, candidates, ref.index) : -1;
        if (body < 0) {
            setStatus(QStringLiteral("Serve una faccia di un corpo adatto (in questo punto della storia)."), true);
        } else {
            ref.index = body;
            if (waiting) definition.offsetFaces.clear();
            waiting = false;
            if (ref.index != definition.firstBody && replaced >= 0) {
                setStatus(QStringLiteral("In modifica il corpo resta quello della funzione."), true);
                startPicking();
                refresh();
                return;
            }
            if (ref.index != definition.firstBody) {
                // Le facce di un altro corpo: la scelta riparte da li'.
                definition.offsetFaces.clear();
                definition.firstBody = ref.index;
                if (bodyBox) {
                    const QSignalBlocker blocker(bodyBox);
                    bodyBox->setCurrentIndex(int(candidates.indexOf(ref.index)));
                }
            }
            const ForgeCad::Kernel::FaceId face = faceOf(ref.point);
            int existing = -1;
            for (int k = 0; k < definition.offsetFaces.size() && existing < 0; ++k)
                if (face.valid() && faceOf(definition.offsetFaces.at(k)) == face) existing = k;
            if (existing >= 0) definition.offsetFaces.remove(existing);
            else definition.offsetFaces.append(ref.point);
            setStatus(QString(), false);
        }
        startPicking();  // si continua a scegliere
        refresh();
    });
    QObject::connect(pickButton, &QPushButton::clicked, &dialog, [&] {
        if (picking) {
            viewport->cancelReferencePick();
            picking = false;
            setStatus(QString(), false);
        } else {
            startPicking();
        }
        refresh();
    });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [&] {
        definition.offsetFaces.clear();
        refresh();
    });
    if (bodyBox)
        QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, [&] {
            waiting = false;
            definition.firstBody = candidates.at(bodyBox->currentIndex());
            definition.offsetFaces.clear();
            refresh();
        });
    QObject::connect(sewBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(flipButton, &QPushButton::toggled, &dialog, refresh);
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (waiting) {
            setStatus(QStringLiteral("Clicca prima nella vista una faccia del corpo."), true);
            return;
        }
        if (removal && !shell && definition.offsetFaces.isEmpty()) {
            setStatus(QStringLiteral("Scegli almeno una faccia da eliminare."), true);
            return;
        }
        if (removal && picking) {
            viewport->cancelReferencePick();
            picking = false;
            refresh();
        }
        if (shell && !(distanceBox->value() > 0.0)) {
            setStatus(QStringLiteral("Lo spessore deve essere maggiore di zero."), true);
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(current());
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        // Senza facce scelte si parte subito con la scelta nella vista.
        // La scelta nella vista parte subito (corpi e facce).
        if (replaced < 0) startPicking();
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    viewport->setPickBodies({});
    scope.reset();
    return accepted;
}

// Stesso riferimento curva (per togliere un tratto gia' scelto con un secondo clic).
static bool sameCurveRef(const GeometryRef &a, const GeometryRef &b) {
    if (a.kind != b.kind || a.index != b.index || a.element != b.element) return false;
    if (a.point.subshape >= 0 || b.point.subshape >= 0) return a.point.subshape == b.point.subshape;
    return std::hypot(a.point.x - b.point.x, std::hypot(a.point.y - b.point.y, a.point.z - b.point.z)) < 1e-9;
}

// Scelta nella vista di catene di curve (spigoli dei corpi, entita' degli
// schizzi, eliche) per le superfici rigata e planare: ogni clic aggiunge o
// toglie un tratto della lista attiva; Esc nella vista termina la scelta.
// Valgono solo i riferimenti che danno tratti esatti limitati (geometryRefPath).
struct CurvePicker {
    CadViewport *viewport = nullptr;
    int owner = -1;
    QVector<GeometryRef> *active = nullptr;  // lista in scelta (nullptr: nessuna)
    std::function<void(const QString &, bool)> status;
    std::function<void()> changed;
    QCheckBox *wholeLoop = nullptr;  // acceso: un bordo libero di una superficie porta con se' tutto il suo loop
    QVector<GeometryRef> *faceList = nullptr;  // lista di facce (riferimenti kind 5) invece che di curve

    // Stessa faccia dello stesso corpo.
    bool sameFace(const GeometryRef &a, const GeometryRef &b) const {
        if (a.kind != 5 || b.kind != 5 || a.index != b.index) return false;
        if (a.index < 0 || a.index >= viewport->extrusions().size() || !viewport->extrusions().at(a.index).forgeBody) return false;
        const ForgeCad::Kernel::Body &body = *viewport->extrusions().at(a.index).forgeBody;
        const double far = std::numeric_limits<double>::max();
        return ForgeCad::resolveFaceReference(body, a.point, far) == ForgeCad::resolveFaceReference(body, b.point, far);
    }

    // Il loop di bordo libero dello spigolo `ref` (kind 4) di una lamina, con
    // almeno due tratti; vuoto altrimenti.
    QVector<GeometryRef> boundaryLoop(const GeometryRef &ref) const {
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

    QString text(const QVector<GeometryRef> &refs) const {
        if (refs.isEmpty()) return QStringLiteral("nessun tratto");
        QStringList names;
        for (const GeometryRef &ref : refs) names.append(ForgeCad::geometryRefText(ref, viewport->sketches(), viewport->extrusions()));
        if (names.size() > 3) return QStringLiteral("%1 tratti (%2, ...)").arg(names.size()).arg(names.mid(0, 2).join(QStringLiteral(", ")));
        return names.join(QStringLiteral(", "));
    }
    void start(QVector<GeometryRef> *list) {
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
    void stop() {
        viewport->cancelReferencePick();
        active = nullptr;
    }
    void install() {
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
};

// Curva 3D associativa e suo intervallo tagliato: la proiezione serve solo
// a individuare il taglio, mai a sostituire la geometria spaziale.
static bool spatialSketchDialog(QMainWindow *window,CadViewport *viewport,const QString &title,int replaced,
    const ExtrusionObject &initial,const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition=initial;definition.feature=BodyFeature::SpatialSketch;definition.operation=-1;definition.sketchIndex=-1;
    auto &p=definition.spatialSketch;
    if(p.entities.isEmpty()){SpatialCurve3D e;e.id=p.nextId++;p.entities.append(e);}
    FunctionDialogPanel dialog(window);dialog.setObjectName(QStringLiteral("spatialSketchDialog"));dialog.setWindowTitle(title);
    auto *form=dialog.createScrollableForm();
    auto *entities=new QComboBox(&dialog);entities->setObjectName(QStringLiteral("spatialEntities"));
    auto *newLine=new QPushButton(QStringLiteral("Nuova polilinea"),&dialog),*newSpline=new QPushButton(QStringLiteral("Nuova spline"),&dialog);
    newLine->setObjectName(QStringLiteral("spatialNewLine"));newSpline->setObjectName(QStringLiteral("spatialNewSpline"));
    auto *removeEntity=new QPushButton(QStringLiteral("Elimina entita'"),&dialog);
    auto *kind=new QComboBox(&dialog);kind->addItems({QStringLiteral("Polilinea"),QStringLiteral("Spline interpolante")});
    auto *closed=new QCheckBox(QStringLiteral("Chiusa"),&dialog);closed->setObjectName(QStringLiteral("spatialClosed"));
    auto *points=new QTableWidget(0,3,&dialog);points->setObjectName(QStringLiteral("spatialPoints"));
    points->setHorizontalHeaderLabels({QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")});
    points->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);points->setSelectionBehavior(QAbstractItemView::SelectRows);
    points->setSelectionMode(QAbstractItemView::SingleSelection);points->setMinimumHeight(150);
    std::array<ForgeCad::ExpressionSpinBox*,3> xyz;
    for(int i=0;i<3;++i){xyz[i]=new ForgeCad::ExpressionSpinBox(&dialog);xyz[i]->setDecimals(9);xyz[i]->setRange(-1e9,1e9);xyz[i]->setObjectName(QStringLiteral("spatial%1").arg(QStringLiteral("XYZ")[i]));}
    auto *addXYZ=new QPushButton(QStringLiteral("Aggiungi punto XYZ"),&dialog);addXYZ->setObjectName(QStringLiteral("spatialAddXYZ"));
    auto *changePoint=new QPushButton(QStringLiteral("Aggiorna punto selezionato"),&dialog);
    auto *removePoint=new QPushButton(QStringLiteral("Elimina punto selezionato"),&dialog);
    auto *plane=new QComboBox(&dialog);plane->setObjectName(QStringLiteral("spatialPlane"));
    plane->addItems({QStringLiteral("Vista, attraverso l'ultimo punto"),QStringLiteral("XY: profondita' Z"),QStringLiteral("XZ: profondita' Y"),QStringLiteral("YZ: profondita' X")});
    auto *depth=new ForgeCad::ExpressionSpinBox(&dialog);depth->setObjectName(QStringLiteral("spatialDepth"));depth->setDecimals(9);depth->setRange(-1e9,1e9);
    auto *snap=new QCheckBox(QStringLiteral("Aggancia ai punti esistenti"),&dialog);snap->setChecked(true);
    auto *draw=new QPushButton(QStringLiteral("Inserisci punti nella vista"),&dialog);draw->setObjectName(QStringLiteral("spatialDraw"));draw->setCheckable(true);
    auto *undo=new QPushButton(QStringLiteral("Annulla modifica"),&dialog),*redo=new QPushButton(QStringLiteral("Ripristina modifica"),&dialog);
    undo->setObjectName(QStringLiteral("spatialUndo"));redo->setObjectName(QStringLiteral("spatialRedo"));
    form->addRow(QStringLiteral("Entita':"),entities);form->addRow(newLine,newSpline);form->addRow(removeEntity);
    form->addRow(QStringLiteral("Tipo:"),kind);form->addRow(closed);form->addRow(points);
    for(int i=0;i<3;++i)form->addRow(QStringLiteral("%1:").arg(QStringLiteral("XYZ")[i]),xyz[i]);
    form->addRow(addXYZ);form->addRow(changePoint);form->addRow(removePoint);form->addRow(undo,redo);
    form->addRow(QStringLiteral("Piano temporaneo:"),plane);form->addRow(QStringLiteral("Profondita':"),depth);form->addRow(snap);form->addRow(draw);
    form->addRow(wrappedNote(QStringLiteral("Clic: aggiungi un punto. Trascina con il destro: ruota; tasto centrale: sposta la vista. "
        "Esc interrompe l'inserimento. Puoi cambiare piano e profondita' per ogni punto, oppure modificare XYZ nella tabella. "
        "Le curve non sono vincolate al piano temporaneo."),&dialog));
    auto *status=new QLabel(&dialog);status->setWordWrap(true);form->addRow(status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    auto scope=std::make_unique<PreviewScope>(viewport,dialog,form,replaced);
    QVector<SpatialSketchParameters> undos,redos;int active=0;
    std::function<void()> refresh;
    const auto remember=[&]{undos.append(p);redos.clear();};
    const auto configure=[&]{
        const QVector<SpatialPoint> nodes=active>=0 && active<p.entities.size()?p.entities[active].points:QVector<SpatialPoint>{};
        viewport->configureSpatialDrawing(plane->currentIndex(),depth->value(),nodes.isEmpty()?SpatialPoint{}:nodes.last(),nodes,snap->isChecked());
    };
    refresh=[&]{
        active=qBound(0,active,qMax(0,int(p.entities.size())-1));
        const QSignalBlocker be(entities),bk(kind),bc(closed),bp(points);
        entities->clear();
        for(const auto &e:p.entities)entities->addItem(QStringLiteral("%1 %2 (%3 punti)").arg(e.spline?QStringLiteral("Spline"):QStringLiteral("Polilinea")).arg(e.id).arg(e.points.size()));
        entities->setCurrentIndex(p.entities.isEmpty()?-1:active);points->setRowCount(0);
        if(!p.entities.isEmpty()){
            const auto &e=p.entities[active];kind->setCurrentIndex(e.spline?1:0);closed->setChecked(e.closed);points->setRowCount(e.points.size());
            for(int row=0;row<e.points.size();++row){const auto &q=e.points[row];const double v[3]={q.x,q.y,q.z};
                for(int col=0;col<3;++col)points->setItem(row,col,new QTableWidgetItem(ForgeCad::formatLength(v[col],9,false)));}
        }
        kind->setEnabled(!p.entities.isEmpty());closed->setEnabled(!p.entities.isEmpty());undo->setEnabled(!undos.isEmpty());redo->setEnabled(!redos.isEmpty());
        configure();
        auto preview=definition;
        // Un punto in corso resta visibile come nodo; non e' ancora una curva.
        for(int i=preview.spatialSketch.entities.size()-1;i>=0;--i)
            if(preview.spatialSketch.entities[i].points.size()<2)preview.spatialSketch.entities.removeAt(i);
        if(preview.spatialSketch.entities.isEmpty()){viewport->clearPreview();scope->label->setText(QStringLiteral("aggiungi almeno due punti"));}
        else scope->request(preview);
    };
    const auto addPoint=[&](SpatialPoint q){
        if(p.entities.isEmpty()){remember();SpatialCurve3D e;e.id=p.nextId++;p.entities.append(e);active=0;}
        else remember();
        const auto &old=p.entities[active].points;
        if(!old.isEmpty()){const auto &last=old.last();const double dx=q.x-last.x,dy=q.y-last.y,dz=q.z-last.z;
            if(dx*dx+dy*dy+dz*dz<1e-18){undos.removeLast();status->setText(QStringLiteral("Il nuovo punto coincide con l'ultimo."));return;}}
        p.entities[active].points.append(q);xyz[0]->setValue(q.x);xyz[1]->setValue(q.y);xyz[2]->setValue(q.z);status->clear();refresh();
    };
    QObject::connect(draw,&QPushButton::toggled,&dialog,[&](bool enabled){
        if(enabled){configure();viewport->setSpatialDrawing(addPoint,[&]{draw->setChecked(false);},replaced);}
        else viewport->setSpatialDrawing({},{});
    });
    const auto newEntity=[&](bool spline){remember();
        if(!p.entities.isEmpty() && p.entities[active].points.isEmpty())p.entities[active].spline=spline;
        else {SpatialCurve3D e;e.id=p.nextId++;e.spline=spline;p.entities.append(e);active=p.entities.size()-1;}
        refresh();draw->setChecked(true);
    };
    QObject::connect(newLine,&QPushButton::clicked,&dialog,[&]{newEntity(false);});
    QObject::connect(newSpline,&QPushButton::clicked,&dialog,[&]{newEntity(true);});
    QObject::connect(entities,&QComboBox::currentIndexChanged,&dialog,[&](int index){if(index>=0){active=index;refresh();}});
    QObject::connect(removeEntity,&QPushButton::clicked,&dialog,[&]{if(!p.entities.isEmpty()){remember();p.entities.removeAt(active);refresh();}});
    QObject::connect(kind,&QComboBox::currentIndexChanged,&dialog,[&](int index){if(!p.entities.isEmpty()){remember();p.entities[active].spline=index==1;refresh();}});
    QObject::connect(closed,&QCheckBox::toggled,&dialog,[&](bool value){if(!p.entities.isEmpty()){remember();p.entities[active].closed=value;refresh();}});
    QObject::connect(addXYZ,&QPushButton::clicked,&dialog,[&]{addPoint({xyz[0]->value(),xyz[1]->value(),xyz[2]->value()});});
    QObject::connect(points,&QTableWidget::itemSelectionChanged,&dialog,[&]{const int row=points->currentRow();
        if(active<p.entities.size() && row>=0 && row<p.entities[active].points.size()){const auto &q=p.entities[active].points[row];xyz[0]->setValue(q.x);xyz[1]->setValue(q.y);xyz[2]->setValue(q.z);}});
    QObject::connect(points,&QTableWidget::cellChanged,&dialog,[&](int row,int col){
        if(active>=p.entities.size() || row<0 || row>=p.entities[active].points.size())return;
        double value;if(!ForgeCad::evaluateExpression(points->item(row,col)->text(),value) || !std::isfinite(value)){
            status->setText(QStringLiteral("Coordinata non valida."));refresh();return;}
        remember();value*=ForgeCad::millimetersPerUnit(viewport->lengthUnit());auto &q=p.entities[active].points[row];
        if(col==0)q.x=value;else if(col==1)q.y=value;else q.z=value;status->clear();refresh();
    });
    QObject::connect(changePoint,&QPushButton::clicked,&dialog,[&]{int row=points->currentRow();if(active<p.entities.size() && row>=0 && row<p.entities[active].points.size()){
        remember();p.entities[active].points[row]={xyz[0]->value(),xyz[1]->value(),xyz[2]->value()};refresh();}});
    QObject::connect(removePoint,&QPushButton::clicked,&dialog,[&]{int row=points->currentRow();if(active<p.entities.size() && row>=0 && row<p.entities[active].points.size()){
        remember();p.entities[active].points.removeAt(row);refresh();}});
    QObject::connect(undo,&QPushButton::clicked,&dialog,[&]{if(!undos.isEmpty()){redos.append(p);p=undos.takeLast();refresh();}});
    QObject::connect(redo,&QPushButton::clicked,&dialog,[&]{if(!redos.isEmpty()){undos.append(p);p=redos.takeLast();refresh();}});
    QObject::connect(plane,&QComboBox::currentIndexChanged,&dialog,[&](int index){
        SpatialPoint anchor;if(active<p.entities.size() && !p.entities[active].points.isEmpty())anchor=p.entities[active].points.last();
        depth->setValue(index==1?anchor.z:index==2?anchor.y:index==3?anchor.x:0);configure();
    });
    QObject::connect(depth,&QDoubleSpinBox::valueChanged,&dialog,[&]{configure();});
    QObject::connect(snap,&QCheckBox::toggled,&dialog,[&]{configure();});
    auto *undoKey=new QShortcut(QKeySequence::Undo,&dialog),*redoKey=new QShortcut(QKeySequence::Redo,&dialog);
    undoKey->setContext(Qt::WidgetWithChildrenShortcut);redoKey->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(undoKey,&QShortcut::activated,undo,&QPushButton::click);QObject::connect(redoKey,&QShortcut::activated,redo,&QPushButton::click);
    viewport->setSpatialEditHistory([&]{undo->click();},[&]{redo->click();});
    refresh();draw->setChecked(true);
    const bool accepted=runUntilAppliedModeless(window,viewport,dialog,form,buttons,[&]{
        QString error;ForgeCad::buildSpatialSketch(p,&error);if(!error.isEmpty())return error;return apply(definition);
    });
    viewport->setSpatialDrawing({},{});viewport->setSpatialEditHistory({},{});scope.reset();return accepted;
}

static bool extractedCurveDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
    const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition=initial;
    definition.operation=-1;definition.feature=BodyFeature::ExtractedCurve;definition.sketchIndex=-1;
    auto &p=definition.curveExtraction;
    if(replaced<0) p.view=viewport->curveTrimView();
    FunctionDialogPanel dialog(window);dialog.setObjectName(QStringLiteral("extractedCurveDialog"));dialog.setWindowTitle(title);dialog.setModal(false);
    auto *form=dialog.createScrollableForm();
    auto *sourceLabel=new QLabel(&dialog),*toolLabel=new QLabel(&dialog);
    sourceLabel->setWordWrap(true);toolLabel->setWordWrap(true);
    auto *sourceButton=new QPushButton(QStringLiteral("Scegli bordo o curva"),&dialog);
    auto *toolButton=new QPushButton(QStringLiteral("Scegli segmento"),&dialog);
    sourceButton->setObjectName(QStringLiteral("extractSourceButton"));toolButton->setObjectName(QStringLiteral("extractToolButton"));
    sourceButton->setCheckable(true);toolButton->setCheckable(true);
    auto *joinBox=new QCheckBox(QStringLiteral("Unisci piu' bordi adiacenti in una curva"),&dialog);
    joinBox->setObjectName(QStringLiteral("extractJoinBox"));joinBox->setChecked(!p.sources.isEmpty());
    auto *sourceList=new GeometryReferenceList(dialog, viewport, QStringLiteral("extractSources"));
    auto *removeSource=new QPushButton(QStringLiteral("Rimuovi bordo selezionato"),&dialog);removeSource->setObjectName(QStringLiteral("extractRemoveSource"));
    auto *trimBox=new QCheckBox(QStringLiteral("Taglia la curva"),&dialog);trimBox->setChecked(p.trimmed);
    auto *modeBox=new QComboBox(&dialog);modeBox->addItems({QStringLiteral("Intersezione reale 3D"),QStringLiteral("Proiezione nella vista salvata")});modeBox->setCurrentIndex(p.projected?1:0);
    auto *viewButton=new QPushButton(QStringLiteral("Usa vista corrente"),&dialog);
    auto *sideBox=new QComboBox(&dialog);sideBox->addItems({QStringLiteral("Mantieni dall'inizio al taglio"),QStringLiteral("Mantieni dal taglio alla fine")});sideBox->setCurrentIndex(p.keepStart?0:1);
    auto *cutBox=new QComboBox(&dialog);cutBox->setObjectName(QStringLiteral("extractCutBox"));
    form->addRow(QStringLiteral("Sorgente:"),sourceLabel);form->addRow(sourceButton);
    form->addRow(joinBox);form->addRow(sourceList);form->addRow(removeSource);
    form->addRow(trimBox);form->addRow(QStringLiteral("Segmento:"),toolLabel);form->addRow(toolButton);
    form->addRow(QStringLiteral("Metodo:"),modeBox);form->addRow(viewButton);
    form->addRow(QStringLiteral("Intersezione:"),cutBox);form->addRow(QStringLiteral("Parte:"),sideBox);
    form->addRow(wrappedNote(QStringLiteral("La curva resta legata alla sorgente e conserva la sua forma 3D. "
        "Il segmento e la vista del taglio vengono conservati per la rigenerazione."),&dialog));
    auto *status=new QLabel(&dialog);status->setWordWrap(true);form->addRow(status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    auto scope=std::make_unique<PreviewScope>(viewport,dialog,form,replaced);
    int picking=0;
    const auto refresh=[&]{
        sourceLabel->setText(p.source.kind<0 ? QStringLiteral("nessuna curva") : ForgeCad::geometryRefText(p.source,viewport->sketches(),viewport->extrusions()));
        sourceList->setReferences(p.sources);
        sourceList->setVisible(joinBox->isChecked());removeSource->setVisible(joinBox->isChecked());
        sourceButton->setText(joinBox->isChecked()?QStringLiteral("Scegli o togli bordi adiacenti"):QStringLiteral("Scegli bordo o curva"));
        toolLabel->setText(p.tool.kind<0 ? QStringLiteral("nessun segmento") : ForgeCad::geometryRefText(p.tool,viewport->sketches(),viewport->extrusions()));
        sourceButton->setChecked(picking==1);toolButton->setChecked(picking==2);
        for(QWidget *w:QList<QWidget*>{toolButton,toolLabel,modeBox,cutBox,sideBox})w->setEnabled(p.trimmed);
        viewButton->setEnabled(p.trimmed && p.projected);
        auto marks=p.sources.isEmpty()?QVector<GeometryRef>{p.source}:p.sources;if(p.trimmed)marks.append(p.tool);viewport->setReferenceMarks(marks);
        if(p.source.kind<0 || (p.trimmed && p.tool.kind<0)){viewport->clearPreview();scope->label->setText(QStringLiteral("scegli i riferimenti"));return;}
        scope->request(definition);
    };
    const auto updateCuts=[&]{
        QVector<double> cuts;QString reason;
        if(p.trimmed && p.source.kind>=0 && p.tool.kind>=0)
            ForgeCad::extractCurves3D(p,replaced<0?viewport->extrusions().size():replaced,viewport->sketches(),viewport->extrusions(),&reason,&cuts);
        const QSignalBlocker blocked(cutBox);cutBox->clear();
        int selected=-1;double best=2;
        for(int k=0;k<cuts.size();++k){
            cutBox->addItem(QStringLiteral("Taglio %1").arg(k+1),cuts[k]);
            if(std::fabs(cuts[k]-p.cutHint)<best){best=std::fabs(cuts[k]-p.cutHint);selected=k;}
        }
        if(selected>=0){cutBox->setCurrentIndex(selected);p.cutHint=cuts[selected];}
        refresh();
    };
    const auto startPick=[&](int target){
        if(picking==target){viewport->cancelReferencePick();picking=0;refresh();return;}
        const QString error=viewport->beginReferencePick(ForgeCad::DatumRoleCurve|ForgeCad::DatumRoleLine,replaced);
        picking=error.isEmpty()?target:0;status->setText(error);refresh();
    };
    viewport->setReferencePickCallback([&](bool picked,GeometryRef ref){
        if(!picked){picking=0;refresh();return;}
        if(!picking)return;
        std::vector<ForgeCad::Kernel::PathSegment> path;QString reason;
        if(!ForgeCad::geometryRefPath(ref,replaced<0?viewport->extrusions().size():replaced,viewport->sketches(),viewport->extrusions(),path,&reason)){
            status->setText(reason);return;
        }
        if(picking==1&&joinBox->isChecked()){
            int existing=-1;
            for(int i=0;i<p.sources.size();++i){const auto &r=p.sources[i];if(r.kind==ref.kind&&r.index==ref.index&&r.point.subshape==ref.point.subshape&&r.element.kind==ref.element.kind&&r.element.element==ref.element.element){existing=i;break;}}
            if(existing>=0)p.sources.removeAt(existing);else p.sources.append(ref);
            p.source=p.sources.isEmpty()?GeometryRef{}:p.sources.first();
            status->clear();updateCuts();
            const QString nextError=viewport->beginReferencePick(ForgeCad::DatumRoleCurve|ForgeCad::DatumRoleLine,replaced);
            if(!nextError.isEmpty()){picking=0;status->setText(nextError);refresh();}
            return;
        }
        if(picking==1){p.source=ref;p.sources.clear();}else p.tool=ref;
        viewport->cancelReferencePick();picking=0;status->clear();updateCuts();
    });
    QObject::connect(joinBox,&QCheckBox::toggled,&dialog,[&](bool enabled){
        if(enabled&&p.sources.isEmpty()&&p.source.kind>=0)p.sources.append(p.source);
        if(!enabled){if(!p.sources.isEmpty())p.source=p.sources.first();p.sources.clear();}
        updateCuts();
    });
    QObject::connect(removeSource,&QPushButton::clicked,&dialog,[&]{const int row=sourceList->currentRow();if(row>=0&&row<p.sources.size())p.sources.removeAt(row);p.source=p.sources.isEmpty()?GeometryRef{}:p.sources.first();updateCuts();});
    QObject::connect(sourceButton,&QPushButton::clicked,&dialog,[&]{startPick(1);});
    QObject::connect(toolButton,&QPushButton::clicked,&dialog,[&]{startPick(2);});
    QObject::connect(trimBox,&QCheckBox::toggled,&dialog,[&](bool value){p.trimmed=value;updateCuts();});
    QObject::connect(modeBox,&QComboBox::currentIndexChanged,&dialog,[&](int index){p.projected=index==1;if(p.projected)p.view=viewport->curveTrimView();updateCuts();});
    QObject::connect(viewButton,&QPushButton::clicked,&dialog,[&]{p.view=viewport->curveTrimView();updateCuts();});
    QObject::connect(sideBox,&QComboBox::currentIndexChanged,&dialog,[&](int index){p.keepStart=index==0;refresh();});
    QObject::connect(cutBox,&QComboBox::currentIndexChanged,&dialog,[&](int index){if(index>=0)p.cutHint=cutBox->itemData(index).toDouble();refresh();});
    if(p.source.kind<0)QTimer::singleShot(0,&dialog,[&]{startPick(1);});
    updateCuts();
    const bool accepted=runUntilAppliedModeless(window,viewport,dialog,form,buttons,[&]{
        if(p.source.kind<0)return QStringLiteral("Scegli il bordo o la curva sorgente.");
        if(p.trimmed && p.tool.kind<0)return QStringLiteral("Scegli il segmento di taglio.");
        return apply(definition);
    });
    viewport->cancelReferencePick();viewport->setReferencePickCallback(nullptr);viewport->setReferenceMarks({});scope.reset();
    return accepted;
}

// Superficie rigata (nuova, o al posto del corpo `replaced`) tra due catene di
// curve scelte nella vista. Finestra non modale come l'offset; anteprima in ambra.
static bool ruledDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Ruled;
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *firstButton = new QPushButton(QStringLiteral("Scegli nella vista"), &dialog);
    auto *secondButton = new QPushButton(QStringLiteral("Scegli nella vista"), &dialog);
    for (QPushButton *button : {firstButton, secondButton}) button->setCheckable(true);
    auto *firstLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("ruledFirstEdges"));
    auto *secondLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("ruledSecondEdges"));
    auto *firstClear = new QPushButton(QStringLiteral("Azzera"), &dialog), *secondClear = new QPushButton(QStringLiteral("Azzera"), &dialog);
    const auto row = [&](QPushButton *pick, QPushButton *clear) {
        auto *widget = new QWidget(&dialog);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(pick, 1);
        layout->addWidget(clear);
        return widget;
    };
    form->addRow(QStringLiteral("Prima curva:"), firstLabel);
    form->addRow(QString(), row(firstButton, firstClear));
    form->addRow(QStringLiteral("Seconda curva:"), secondLabel);
    form->addRow(QString(), row(secondButton, secondClear));
    SurfaceEndControls endControls(dialog, *form, viewport, replaced, definition);
    auto *loopBox = new QCheckBox(QStringLiteral("Bordo libero intero delle superfici"), &dialog);
    loopBox->setChecked(true);
    loopBox->setToolTip(QStringLiteral("Un clic su un bordo libero di una superficie sceglie (o toglie) tutto il suo loop"));
    form->addRow(QString(), loopBox);
    form->addRow(wrappedNote(QStringLiteral("Ogni curva e' una catena di tratti consecutivi (spigoli dei corpi, entita' degli schizzi, "
                                           "eliche), aperta o chiusa come l'altra. Le rette uniscono i punti alla stessa frazione di lunghezza."), &dialog));
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(280);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    CurvePicker picker;
    picker.viewport = viewport;
    picker.owner = replaced;
    picker.status = setStatus;
    picker.wholeLoop = loopBox;
    const auto refresh = [&] {
        firstLabel->setReferences(definition.ruledFirst);
        secondLabel->setReferences(definition.ruledSecond);
        firstButton->setChecked(picker.active == &definition.ruledFirst);
        secondButton->setChecked(picker.active == &definition.ruledSecond);
        viewport->setReferenceMarks(definition.ruledFirst + definition.ruledSecond + definition.loftStartFaces + definition.loftEndFaces);
        if (definition.ruledFirst.isEmpty() || definition.ruledSecond.isEmpty()) {
            scope->label->setText(QStringLiteral("scegli le due curve"));
            viewport->clearPreview();
            return;
        }
        scope->request(definition);
    };
    picker.changed = refresh;
    endControls.changed = refresh;
    picker.install();
    for (int k = 0; k < 2; ++k) {
        QVector<GeometryRef> *list = k == 0 ? &definition.ruledFirst : &definition.ruledSecond;
        QObject::connect(k == 0 ? firstButton : secondButton, &QPushButton::clicked, &dialog, [&, list] {
            endControls.stop();
            picker.install();
            if (picker.active == list) picker.stop();
            else picker.start(list);
            refresh();
        });
        QObject::connect(k == 0 ? firstClear : secondClear, &QPushButton::clicked, &dialog, [&, list] {
            list->clear();
            refresh();
        });
    }
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (definition.ruledFirst.isEmpty() || definition.ruledSecond.isEmpty()) {
            setStatus(QStringLiteral("Scegli le due curve."), true);
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(definition);
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        if (definition.ruledFirst.isEmpty() && replaced < 0) picker.start(&definition.ruledFirst);
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    scope.reset();
    return accepted;
}

// Superficie tra curve (nuova, o al posto del corpo `replaced`): le curve di un
// contorno chiuso scelte nella vista (spigoli, entita' degli schizzi, eliche;
// con un clic tutto il bordo libero di una superficie). Non modale; anteprima in ambra.
static bool boundaryDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                           const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::BoundarySurface;
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *curvesLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("boundaryEdges"));
    auto *pickButton = new QPushButton(QStringLiteral("Scegli nella vista"), &dialog);
    pickButton->setCheckable(true);
    auto *clearButton = new QPushButton(QStringLiteral("Azzera"), &dialog);
    auto *pickRow = new QWidget(&dialog);
    auto *pickLayout = new QHBoxLayout(pickRow);
    pickLayout->setContentsMargins(0, 0, 0, 0);
    pickLayout->addWidget(pickButton, 1);
    pickLayout->addWidget(clearButton);
    auto *loopBox = new QCheckBox(QStringLiteral("Bordo libero intero delle superfici"), &dialog);
    loopBox->setChecked(true);
    loopBox->setToolTip(QStringLiteral("Un clic su un bordo libero di una superficie sceglie (o toglie) tutto il suo loop"));
    form->addRow(QStringLiteral("Contorno:"), curvesLabel);
    form->addRow(QString(), pickRow);
    form->addRow(QString(), loopBox);
    form->addRow(wrappedNote(QStringLiteral("Le curve, in qualsiasi ordine, devono formare un contorno chiuso anche sghembo. Gli angoli vivi "
                                           "dividono il contorno in 3 o 4 lati (un contorno liscio in 4 parti uguali); la superficie li "
                                           "raccorda come una patch di Coons."), &dialog));
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(280);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    CurvePicker picker;
    picker.viewport = viewport;
    picker.owner = replaced;
    picker.status = setStatus;
    picker.wholeLoop = loopBox;
    const auto refresh = [&] {
        curvesLabel->setReferences(definition.planarRefs);
        pickButton->setChecked(picker.active != nullptr);
        viewport->setReferenceMarks(definition.planarRefs);
        if (definition.planarRefs.isEmpty()) {
            scope->label->setText(QStringLiteral("scegli le curve del contorno"));
            viewport->clearPreview();
            return;
        }
        scope->request(definition);
    };
    picker.changed = refresh;
    picker.install();
    QObject::connect(pickButton, &QPushButton::clicked, &dialog, [&] {
        if (picker.active) picker.stop();
        else picker.start(&definition.planarRefs);
        refresh();
    });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [&] {
        definition.planarRefs.clear();
        refresh();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (definition.planarRefs.isEmpty()) {
            setStatus(QStringLiteral("Scegli le curve del contorno."), true);
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(definition);
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        if (definition.planarRefs.isEmpty() && replaced < 0) picker.start(&definition.planarRefs);
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    scope.reset();
    return accepted;
}

// Superficie di riempimento (nuova, o al posto del corpo `replaced`): un
// contorno chiuso di curve, curve guida interne e le facce adiacenti dei
// corpi da continuare in tangenza o curvatura (proposte da sole: le facce con
// un bordo libero sul contorno).
static bool fillDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::FillSurface;
    FunctionDialogPanel dialog(window);
    dialog.setObjectName(QStringLiteral("fillDialog"));
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    // Tre liste scelte nella vista: contorno, guide, facce adiacenti.
    struct ListRow {
        QWidget *label;
        QPushButton *pick;
        QPushButton *clear;
        QWidget *row;
    };
    const auto listRow = [&](const QString &pickText, const QString &listName = QString()) {
        QWidget *label;
        if (!listName.isEmpty()) label = new GeometryReferenceList(dialog, viewport, listName);
        else { auto *summary = new QLabel(&dialog); summary->setWordWrap(true); label = summary; }
        ListRow r{label, new QPushButton(pickText, &dialog), new QPushButton(QStringLiteral("Azzera"), &dialog), new QWidget(&dialog)};
        r.pick->setCheckable(true);
        auto *layout = new QHBoxLayout(r.row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(r.pick, 1);
        layout->addWidget(r.clear);
        return r;
    };
    const ListRow boundaryRow = listRow(QStringLiteral("Scegli il contorno"), QStringLiteral("fillBoundaryEdges")),
                  guideRow = listRow(QStringLiteral("Scegli le guide"), QStringLiteral("fillGuideEdges")),
                  faceRow = listRow(QStringLiteral("Scegli le facce"));
    auto *faceList = new QListWidget(&dialog);
    faceList->setObjectName(QStringLiteral("fillContactFaces"));
    faceList->setMinimumHeight(100);
    QVector<GeometryRef> previousFaces = definition.ruledSecond;
    QVector<double> previousInfluences;
    for (int i = 0; i < previousFaces.size(); ++i)
        previousInfluences.append(definition.fillFaceInfluences.value(i, definition.fillInfluence));
    faceRow.pick->setObjectName(QStringLiteral("fillPickFaces"));
    auto *detectButton = new QPushButton(QStringLiteral("Rileva"), &dialog);
    detectButton->setToolTip(QStringLiteral("Le facce dei corpi che hanno un bordo libero sul contorno"));
    faceRow.row->layout()->addWidget(detectButton);
    auto *loopBox = new QCheckBox(QStringLiteral("Bordo libero intero delle superfici"), &dialog);
    loopBox->setChecked(true);
    loopBox->setToolTip(QStringLiteral("Un clic su un bordo libero di una superficie sceglie (o toglie) tutto il suo loop"));
    auto *continuityBox = new QComboBox(&dialog);
    continuityBox->addItems({QStringLiteral("Contatto (G0)"), QStringLiteral("Tangenza (G1)"), QStringLiteral("Curvatura (G2)")});
    continuityBox->setCurrentIndex(qBound(0, definition.fillContinuity, 2));
    continuityBox->setToolTip(QStringLiteral("Raccordo con le facce adiacenti lungo i tratti del contorno che stanno sul loro bordo"));
    auto *influenceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    influenceBox->setObjectName(QStringLiteral("fillFaceInfluence"));
    influenceBox->setDecimals(3);
    influenceBox->setRange(0.05, 20.0);
    influenceBox->setSingleStep(0.1);
    influenceBox->setValue(definition.fillInfluence);
    influenceBox->setKeyboardTracking(false);
    influenceBox->setToolTip(QStringLiteral("Regola la distribuzione della curvatura e la forma del raccordo (1: naturale), mantenendo il piano tangente sul bordo"));
    auto *weightBox = new ForgeCad::ExpressionSpinBox(&dialog);
    weightBox->setDecimals(3);
    weightBox->setRange(0.0, 100.0);
    weightBox->setSingleStep(0.1);
    weightBox->setValue(definition.fillGuideWeight);
    weightBox->setKeyboardTracking(false);
    weightBox->setToolTip(QStringLiteral("1: la superficie passa per le guide; meno di 1: le guide la attirano soltanto; 0: ignorate"));
    form->addRow(QStringLiteral("Contorno:"), boundaryRow.label);
    form->addRow(QString(), boundaryRow.row);
    form->addRow(QString(), loopBox);
    form->addRow(QStringLiteral("Guide:"), guideRow.label);
    form->addRow(QString(), guideRow.row);
    form->addRow(QStringLiteral("Facce adiacenti:"), faceRow.label);
    form->addRow(QString(), faceRow.row);
    form->addRow(QString(), faceList);
    form->addRow(QStringLiteral("Continuita':"), continuityBox);
    form->addRow(QStringLiteral("Influenza faccia selezionata:"), influenceBox);
    form->addRow(QStringLiteral("Peso delle guide:"), weightBox);
    auto *shapeBox = new QCheckBox(QStringLiteral("Usa la faccia selezionata per guidare la forma"), &dialog);
    shapeBox->setObjectName(QStringLiteral("fillShapeFace"));
    shapeBox->setToolTip(QStringLiteral("Guida la curvatura interna dalla faccia scelta, mantenendo i contatti con le altre facce."));
    auto *shapeWeightBox = new ForgeCad::ExpressionSpinBox(&dialog);
    shapeWeightBox->setObjectName(QStringLiteral("fillShapeWeight"));shapeWeightBox->setRange(0,10);shapeWeightBox->setDecimals(3);
    shapeWeightBox->setValue(definition.fillShapeWeight);shapeWeightBox->setKeyboardTracking(false);
    form->addRow(shapeBox);form->addRow(QStringLiteral("Influenza sulla forma interna:"),shapeWeightBox);

    form->addRow(wrappedNote(QStringLiteral("Il contorno chiuso (in qualsiasi ordine) delimita la superficie, che passa per le curve guida "
                                           "interne. Dove il contorno sta sul bordo di una faccia adiacente la superficie la continua in "
                                           "tangenza o in curvatura; vicino a quei bordi le guide pesano meno, cosi' una guida che arriva "
                                           "con un'altra pendenza non crea una piega. In G1 la curvatura delle facce suggerisce la forma del "
                                           "raccordo; in G2 viene richiesta anche la continuita' di curvatura. L'influenza regola la forma."), &dialog));
    auto *result = new QLabel(&dialog);
    result->setWordWrap(true);
    result->setMaximumWidth(360);
    result->setStyleSheet(QStringLiteral("color: #9fc6e8;"));
    form->addRow(QStringLiteral("Esito:"), result);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(280);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    QLabel *previewLabel = scope->label;
    viewport->setPreviewCallback([previewLabel, result, viewport](const QString &error) {
        previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
        result->setText(error.isEmpty() ? viewport->previewNotice() : QString());
    });
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    CurvePicker picker;
    picker.viewport = viewport;
    picker.owner = replaced;
    picker.status = setStatus;
    picker.wholeLoop = loopBox;
    picker.faceList = &definition.ruledSecond;
    // Facce adiacenti proposte finche' non si scelgono a mano.
    bool autoFaces = replaced < 0 && definition.ruledSecond.isEmpty();
    const auto detect = [&] {
        definition.ruledSecond.clear();
        const int limit = replaced < 0 ? int(viewport->extrusions().size()) : replaced;
        std::vector<ForgeCad::Kernel::PathSegment> boundary;
        for (const GeometryRef &ref : definition.planarRefs) {
            std::vector<ForgeCad::Kernel::PathSegment> pieces;
            if (ForgeCad::geometryRefPath(ref, limit, viewport->sketches(), viewport->extrusions(), pieces, nullptr))
                boundary.insert(boundary.end(), pieces.begin(), pieces.end());
        }
        if (boundary.empty()) return;
        for (int b : viewport->resultBodiesBefore(limit)) {
            const ExtrusionObject &body = viewport->extrusions().at(b);
            if (!body.forgeBody) continue;
            for (const EdgePoint &point : ForgeCad::forgeFillContactFaces(boundary, *body.forgeBody)) {
                GeometryRef ref;
                ref.kind = 5;
                ref.index = b;
                ref.featureId = body.featureId;
                ref.point = point;
                definition.ruledSecond.append(ref);
            }
        }
    };
    const auto syncFaceControls = [&] {
        const QSignalBlocker block(influenceBox), shapeBlock(shapeBox);
        const int row = faceList->currentRow();
        shapeBox->setEnabled(definition.fillContinuity>0 && row>=0);
        shapeBox->setChecked(row>=0 && row==definition.fillShapeFace);
        shapeWeightBox->setEnabled(definition.fillShapeFace>=0 && definition.fillContinuity>0);
        influenceBox->setEnabled(definition.fillContinuity > 0 && row >= 0 && row < definition.fillFaceInfluences.size());
        if (row >= 0 && row < definition.fillFaceInfluences.size()) influenceBox->setValue(definition.fillFaceInfluences[row]);
    };
    const auto refresh = [&] {
        // Rileva/picking possono riordinare o rimuovere facce: conserva il
        // valore per identita' topologica, mai per la vecchia riga dell'elenco.
        const int oldRow = faceList->currentRow();
        const GeometryRef selected = previousFaces.value(oldRow);
        const int oldShape=definition.fillShapeFace;
        const GeometryRef shapeRef=previousFaces.value(oldShape);
        int mappedShape=-1;
        QVector<double> values; int selectedRow = -1;
        for (int i = 0; i < definition.ruledSecond.size(); ++i) {
            const auto &ref = definition.ruledSecond[i]; double value = definition.fillInfluence;
            for (int j = 0; j < previousFaces.size(); ++j) if (picker.sameFace(ref, previousFaces[j])) { value = previousInfluences.value(j, definition.fillInfluence); break; }
            if (oldRow >= 0 && picker.sameFace(ref, selected)) selectedRow = i;
            if(oldShape>=0 && picker.sameFace(ref,shapeRef))mappedShape=i;
            values.append(value);
        }
        definition.fillShapeFace=mappedShape;
        if(continuityBox->currentIndex()==0)definition.fillShapeFace=-1;
        definition.fillShapeWeight=shapeWeightBox->value();
        definition.fillFaceInfluences = values;
        previousFaces = definition.ruledSecond; previousInfluences = values;
        {
            const QSignalBlocker block(faceList);
            faceList->clear();
            for (int i = 0; i < previousFaces.size(); ++i) {
                const auto &ref = previousFaces[i];
                faceList->addItem(QStringLiteral("%1 — F%2 / influenza %3")
                    .arg(ForgeCad::geometryRefText(ref, viewport->sketches(), viewport->extrusions())).arg(ref.point.subshape)
                    .arg(values[i], 0, 'g', 3) + (i==definition.fillShapeFace ? QStringLiteral(" — guida della forma") : QString()));
            }
            faceList->setCurrentRow(selectedRow >= 0 ? selectedRow : faceList->count() ? 0 : -1);
        }
        static_cast<GeometryReferenceList *>(boundaryRow.label)->setReferences(definition.planarRefs);
        static_cast<GeometryReferenceList *>(guideRow.label)->setReferences(definition.ruledFirst);
        QStringList faceNames;
        for (const GeometryRef &ref : definition.ruledSecond) faceNames.append(ForgeCad::geometryRefText(ref, viewport->sketches(), viewport->extrusions()));
        static_cast<QLabel *>(faceRow.label)->setText(faceNames.isEmpty() ? QStringLiteral("nessuna (solo contatto)") : faceNames.join(QStringLiteral(", ")));
        boundaryRow.pick->setChecked(picker.active == &definition.planarRefs);
        guideRow.pick->setChecked(picker.active == &definition.ruledFirst);
        faceRow.pick->setChecked(picker.active == &definition.ruledSecond);
        viewport->setReferenceMarks(definition.planarRefs + definition.ruledFirst + definition.ruledSecond);
        definition.fillContinuity = continuityBox->currentIndex();

        definition.fillGuideWeight = weightBox->value();
        syncFaceControls();
        if (definition.planarRefs.isEmpty()) {
            previewLabel->setText(QStringLiteral("scegli le curve del contorno"));
            result->clear();
            viewport->clearPreview();
            return;
        }
        result->setText(QStringLiteral("in calcolo..."));
        scope->request(definition);
    };
    picker.changed = [&] {
        if (picker.active == &definition.planarRefs && autoFaces) detect();
        refresh();
    };
    picker.install();
    const auto toggle = [&](QVector<GeometryRef> *list) {
        if (picker.active == list) picker.stop();
        else {
            if (picker.active) picker.stop();
            picker.start(list);
        }
        refresh();
    };
    QObject::connect(boundaryRow.pick, &QPushButton::clicked, &dialog, [&] { toggle(&definition.planarRefs); });
    QObject::connect(guideRow.pick, &QPushButton::clicked, &dialog, [&] { toggle(&definition.ruledFirst); });
    QObject::connect(faceRow.pick, &QPushButton::clicked, &dialog, [&] {
        autoFaces = false;
        toggle(&definition.ruledSecond);
    });
    QObject::connect(boundaryRow.clear, &QPushButton::clicked, &dialog, [&] {
        definition.planarRefs.clear();
        if (autoFaces) definition.ruledSecond.clear();
        refresh();
    });
    QObject::connect(guideRow.clear, &QPushButton::clicked, &dialog, [&] {
        definition.ruledFirst.clear();
        refresh();
    });
    QObject::connect(faceRow.clear, &QPushButton::clicked, &dialog, [&] {
        autoFaces = false;
        definition.ruledSecond.clear();
        refresh();
    });
    QObject::connect(detectButton, &QPushButton::clicked, &dialog, [&] {
        autoFaces = true;
        detect();
        if (definition.ruledSecond.isEmpty()) setStatus(QStringLiteral("Nessuna faccia ha un bordo libero sul contorno."), false);
        refresh();
    });
    QObject::connect(continuityBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    const auto changeFaceInfluence = [&] {
        const int row = faceList->currentRow();
        if (row < 0 || row >= definition.fillFaceInfluences.size()) return;
        definition.fillFaceInfluences[row] = influenceBox->value();
        previousInfluences[row] = influenceBox->value();
        refresh();
    };
    QObject::connect(faceList, &QListWidget::currentRowChanged, &dialog, syncFaceControls);
    QObject::connect(influenceBox, &QDoubleSpinBox::valueChanged, &dialog, changeFaceInfluence);
    influenceBox->onReturn = changeFaceInfluence;
    QObject::connect(shapeBox,&QCheckBox::toggled,&dialog,[&](bool checked){
        if(checked)definition.fillShapeFace=faceList->currentRow();
        else if(definition.fillShapeFace==faceList->currentRow())definition.fillShapeFace=-1;
        refresh();
    });
    QObject::connect(shapeWeightBox,&QDoubleSpinBox::valueChanged,&dialog,refresh);
    shapeWeightBox->onReturn=refresh;
    weightBox->onReturn = refresh;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        influenceBox->interpretText();
        changeFaceInfluence();
        weightBox->interpretText();
        definition.fillContinuity = continuityBox->currentIndex();

        definition.fillGuideWeight = weightBox->value();
        if (definition.planarRefs.isEmpty()) {
            setStatus(QStringLiteral("Scegli le curve del contorno."), true);
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(definition);
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        if (definition.planarRefs.isEmpty() && replaced < 0) picker.start(&definition.planarRefs);
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    scope.reset();
    return accepted;
}

// Superficie planare (nuova, o al posto del corpo `replaced`): i contorni
// chiusi di uno schizzo o contorni chiusi di bordi scelti nella vista.
static bool planarDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                         const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::PlanarSurface;
    const QVector<SketchObject> &sketches = viewport->sketches();
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *sourceBox = new QComboBox(&dialog);
    for (const SketchObject &sketch : sketches) sourceBox->addItem(QStringLiteral("Schizzo: %1").arg(sketch.name));
    sourceBox->addItem(QStringLiteral("Bordi scelti nella vista"));
    const int edgesItem = int(sketches.size());
    sourceBox->setCurrentIndex(!definition.planarRefs.isEmpty() || definition.sketchIndex < 0 || definition.sketchIndex >= sketches.size()
                                   ? edgesItem
                                   : definition.sketchIndex);
    auto *edgesLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("planarEdges"));
    auto *pickButton = new QPushButton(QStringLiteral("Scegli i bordi nella vista"), &dialog);
    pickButton->setCheckable(true);
    auto *clearButton = new QPushButton(QStringLiteral("Azzera"), &dialog);
    auto *edgeRow = new QWidget(&dialog);
    auto *edgeLayout = new QHBoxLayout(edgeRow);
    edgeLayout->setContentsMargins(0, 0, 0, 0);
    edgeLayout->addWidget(pickButton, 1);
    edgeLayout->addWidget(clearButton);
    form->addRow(QStringLiteral("Sorgente:"), sourceBox);
    form->addRow(QStringLiteral("Bordi:"), edgesLabel);
    form->addRow(QString(), edgeRow);
    auto *loopBox = new QCheckBox(QStringLiteral("Bordo libero intero delle superfici"), &dialog);
    loopBox->setChecked(true);
    loopBox->setToolTip(QStringLiteral("Un clic su un bordo libero di una superficie sceglie (o toglie) tutto il suo loop"));
    form->addRow(QString(), loopBox);
    form->addRow(wrappedNote(QStringLiteral("I contorni chiusi complanari delimitano la superficie; quelli dentro un altro sono fori."), &dialog));
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(280);
    status->setMaximumWidth(360);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    CurvePicker picker;
    picker.viewport = viewport;
    picker.owner = replaced;
    picker.status = setStatus;
    picker.wholeLoop = loopBox;
    const auto current = [&] {
        ExtrusionObject d = definition;
        if (sourceBox->currentIndex() == edgesItem) {
            d.sketchIndex = -1;
        } else {
            d.sketchIndex = sourceBox->currentIndex();
            d.planarRefs.clear();
        }
        return d;
    };
    const auto refresh = [&] {
        const bool edges = sourceBox->currentIndex() == edgesItem;
        for (QWidget *w : {static_cast<QWidget *>(edgesLabel), static_cast<QWidget *>(pickButton), static_cast<QWidget *>(clearButton), static_cast<QWidget *>(loopBox)}) w->setEnabled(edges);
        if (!edges && picker.active) picker.stop();
        edgesLabel->setReferences(definition.planarRefs);
        pickButton->setChecked(picker.active != nullptr);
        viewport->setReferenceMarks(edges ? definition.planarRefs : QVector<GeometryRef>());
        if (edges && definition.planarRefs.isEmpty()) {
            scope->label->setText(QStringLiteral("scegli i bordi"));
            viewport->clearPreview();
            return;
        }
        scope->request(current());
    };
    picker.changed = refresh;
    picker.install();
    QObject::connect(sourceBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(pickButton, &QPushButton::clicked, &dialog, [&] {
        if (picker.active) picker.stop();
        else picker.start(&definition.planarRefs);
        refresh();
    });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [&] {
        definition.planarRefs.clear();
        refresh();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const ExtrusionObject d = current();
        if (d.sketchIndex < 0 && d.planarRefs.isEmpty()) {
            setStatus(QStringLiteral("Scegli uno schizzo o i bordi."), true);
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(d);
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        if (sourceBox->currentIndex() == edgesItem && definition.planarRefs.isEmpty() && replaced < 0) picker.start(&definition.planarRefs);
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    scope.reset();
    return accepted;
}

// Cucitura di superfici (nuova, o al posto del corpo `replaced`): le superfici
// spuntate (di partenza quelle scelte nella vista, o tutte le superfici
// visibili), la tolleranza e il solido se chiudono un volume.
static bool sewDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                      const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    QVector<int> indices = viewport->resultBodiesBefore(replaced);  // solo i corpi esistenti, non gli stadi intermedi
    for (int operand : QVector<int>{initial.firstBody} + initial.booleanTools)
        if (operand >= 0 && operand < bodies.size() && (replaced < 0 || operand < replaced) && !indices.contains(operand)) indices.append(operand);
    std::sort(indices.begin(), indices.end());
    if (indices.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Nella scena non ci sono superfici da cucire."));
        return false;
    }
    QVector<int> chosen;
    if (initial.firstBody >= 0) chosen = QVector<int>{initial.firstBody} + initial.booleanTools;
    if (chosen.isEmpty())
        for (const SceneSelection &selection : viewport->selectedObjects())
            if (selection.kind == SceneObjectKind::Extrusion && indices.contains(selection.index)) chosen.append(selection.index);
    if (chosen.size() < 2) {
        chosen.clear();
        for (int index : indices)
            if (bodies.at(index).visible && !bodies.at(index).solid) chosen.append(index);
    }
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *list = new QListWidget(&dialog);
    list->setMinimumHeight(160);
    for (int index : indices) {
        const ExtrusionObject &body = bodies.at(index);
        const QString text = resultBodyLabel(viewport, index, false) + (body.solid ? QStringLiteral(" (solido: le sue facce)") : QString());
        auto *item = new QListWidgetItem(text, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(chosen.contains(index) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, index);
    }
    auto *toleranceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    toleranceBox->setDecimals(8);
    toleranceBox->setRange(1e-8, 1.0);
    toleranceBox->setValue(initial.sewTolerance > 0.0 ? initial.sewTolerance : 1e-5);
    toleranceBox->setKeyboardTracking(false);
    toleranceBox->setToolTip(QStringLiteral("Distanza massima tra i bordi e i vertici da unire"));
    auto *solidBox = new QCheckBox(QStringLiteral("Crea un solido (le superfici devono chiudere un volume)"), &dialog);
    solidBox->setChecked(initial.sewSolid);
    BodyPicker picker(dialog, viewport, replaced, indices, nullptr, list);
    form->addRow(QStringLiteral("Superfici:"), picker.row(list));
    form->addRow(QStringLiteral("Tolleranza:"), toleranceBox);
    form->addRow(QString(), solidBox);
    form->addRow(wrappedNote(QStringLiteral("I bordi comuni (stessi estremi e stessa curva entro la tolleranza) si uniscono; un "
                                           "bordo su cui finisce il vertice di un'altra superficie si divide li'. Creando un solido, "
                                           "una singola superficie cilindrica puo' acquisire i trim dell'apertura che chiude."), &dialog));
    const PreviewScope scope(viewport, dialog, form, replaced);
    const auto current = [&] {
        ExtrusionObject body = initial;
        body.operation = -1;
        body.feature = BodyFeature::Sew;
        QVector<int> inputs;
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->checkState() == Qt::Checked) inputs.append(list->item(row)->data(Qt::UserRole).toInt());
        body.firstBody = inputs.value(0, -1);
        body.booleanTools = inputs.mid(1);
        body.sewTolerance = toleranceBox->value();
        body.sewSolid = solidBox->isChecked();
        return body;
    };
    const auto refresh = [&] {
        const ExtrusionObject body = current();
        if (body.firstBody < 0) {
            scope.label->setText(QStringLiteral("spunta le superfici da cucire"));
            viewport->clearPreview();
            return;
        }
        scope.request(body);
    };
    picker.changed = refresh;
    picker.message = [&](const QString &text, bool) { scope.label->setText(text); };
    picker.install();
    QObject::connect(list, &QListWidget::itemChanged, &dialog, [&] {
        if (!picker.handling()) refresh();
    });
    QObject::connect(toleranceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(solidBox, &QCheckBox::toggled, &dialog, refresh);
    refresh();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    const auto run = [&] {
        picker.stop();
        const ExtrusionObject body = current();
        if (body.firstBody < 0) return QStringLiteral("Spunta le superfici da cucire.");
        return apply(body);
    };
    if (auto *window = qobject_cast<QMainWindow *>(parent)) return runUntilAppliedModeless(window, viewport, dialog, form, buttons, run);
    return runUntilApplied(dialog, form, buttons, run);
}

// Misura nella vista (Analisi -> Misura): si cliccano una o due entita' con gli
// agganci della scelta dei riferimenti (vertici, punti medi e centri degli
// spigoli, punti degli schizzi, spigoli e curve, facce, piani e assi). Una
// entita': coordinate, lunghezza, raggio, area...; due: distanza minima con i
// punti piu' vicini (disegnata nella vista), angolo, interasse. Il clic su
// un'entita' gia' scelta la toglie, un terzo clic ricomincia; Esc nella vista
// azzera. Solo lettura: il documento non cambia.
static void measureDialog(QMainWindow *window, CadViewport *viewport) {
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(QStringLiteral("Misura"));
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    form->addRow(wrappedNote(QStringLiteral("Clicca nella vista uno o due oggetti: vertici, punti medi e centri degli spigoli, spigoli, "
                                            "facce, piani e assi. Il clic su un oggetto scelto lo toglie; Esc nella vista azzera."), &dialog));
    auto *first = new QLabel(&dialog), *second = new QLabel(&dialog);
    for (QLabel *label : {first, second}) {
        label->setWordWrap(true);
        label->setMaximumWidth(360);
    }
    form->addRow(QStringLiteral("A:"), first);
    form->addRow(QStringLiteral("B:"), second);
    auto *results = new QLabel(&dialog);
    results->setWordWrap(true);
    results->setMinimumWidth(300);
    results->setMaximumWidth(420);
    results->setTextInteractionFlags(Qt::TextSelectableByMouse);
    results->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    form->addRow(results);
    auto *buttons = new QDialogButtonBox(&dialog);
    QPushButton *clearButton = buttons->addButton(QStringLiteral("Azzera"), QDialogButtonBox::ResetRole);
    QPushButton *copyButton = buttons->addButton(QStringLiteral("Copia"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    form->addRow(buttons);

    QVector<GeometryRef> refs;
    QString report;
    const auto same = [](const GeometryRef &a, const GeometryRef &b) {
        if (a.kind != b.kind || a.index != b.index || !(a.element == b.element)) return false;
        if (a.kind == 3 || a.kind == 4 || a.kind == 5 || a.kind == ForgeCad::kGeometryRefEdgeMidpoint || a.kind == ForgeCad::kGeometryRefEdgeCenter)
            return a.point.subshape == b.point.subshape;
        return true;
    };
    const auto refresh = [&] {
        viewport->setReferenceMarks(refs);
        QVector<ForgeCad::MeasureEntity> entities;
        QStringList names;
        QString error;
        for (const GeometryRef &ref : refs) {
            ForgeCad::MeasureEntity entity;
            QString reason;
            if (!ForgeCad::measureEntity(ref, viewport->sketches(), viewport->extrusions(), entity, &reason)) {
                error = reason;
                names << QStringLiteral("(non valido)");
                continue;
            }
            names << entity.name;
            entities.append(entity);
        }
        first->setText(names.value(0, QStringLiteral("(clicca nella vista)")));
        second->setText(refs.size() >= 1 ? names.value(1, QStringLiteral("(facoltativo)")) : QStringLiteral("-"));
        viewport->setMeasureOverlay(false);
        report.clear();
        if (!error.isEmpty()) {
            results->setStyleSheet(QStringLiteral("color: #ff7a6a;"));
            results->setText(error);
            return;
        }
        results->setStyleSheet(QString());
        if (entities.isEmpty()) {
            results->clear();
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const ForgeCad::MeasureReport measure = ForgeCad::measureEntities(entities);
        QApplication::restoreOverrideCursor();
        if (!measure.ok) {
            results->setStyleSheet(QStringLiteral("color: #ff7a6a;"));
            results->setText(measure.error);
            return;
        }
        report = measure.lines.join(QLatin1Char('\n'));
        results->setText(report);
        if (measure.hasDistance) {
            const auto q = [](const ForgeCad::Kernel::Vec3 &p) { return QVector3D(float(p.x()), float(p.y()), float(p.z())); };
            viewport->setMeasureOverlay(true, q(measure.from), q(measure.to), measure.label);
        }
    };
    const auto pick = [&] {
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRolePoint | ForgeCad::DatumRoleLine | ForgeCad::DatumRoleCurve
                                                               | ForgeCad::DatumRolePlane | ForgeCad::DatumRoleFace | ForgeCad::kMeasureSnapRole,
                                                           -1);
        if (!error.isEmpty()) results->setText(error);
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picked) {
            int existing = -1;
            for (int k = 0; k < refs.size(); ++k)
                if (same(refs.at(k), ref)) existing = k;
            if (existing >= 0) refs.remove(existing);
            else if (refs.size() >= 2) refs = {ref};
            else refs.append(ref);
        } else {
            refs.clear();
        }
        refresh();
        pick();
    });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [&] {
        refs.clear();
        refresh();
        pick();
    });
    QObject::connect(copyButton, &QPushButton::clicked, &dialog, [&] {
        if (!report.isEmpty()) QGuiApplication::clipboard()->setText(first->text() + QStringLiteral("\n") + second->text() + QStringLiteral("\n") + report);
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Un oggetto selezionato prima del comando e' gia' la prima entita'? No: la
    // selezione della vista non dice quale spigolo o faccia; si parte da zero.
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        refresh();
        pick();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    viewport->setMeasureOverlay(false);
}

// Analisi -> Micro-geometrie: facce sottili, spigoli corti e quasi tangenti
// del corpo selezionato (o del primo solido visibile), segnati nella vista
// finche' la finestra resta aperta. Sono le geometrie di rumore che fanno
// fallire raccordi e offset: si correggono nella feature che le crea.
// Segni nella vista e righe del rapporto per le micro-geometrie del corpo `index`.
static QVector<GeometryRef> microFeatureMarks(const ExtrusionObject &body, int index, QStringList &lines) {
    QVector<GeometryRef> marks;
    const ForgeCad::Kernel::Body &brep = *body.forgeBody;
    for (const auto &feature : ForgeCad::Kernel::findMicroFeatures(brep)) {
        GeometryRef ref;
        ref.index = index;
        ref.featureId = body.featureId;
        if (feature.kind == ForgeCad::Kernel::MicroFeature::Kind::ThinFace) {
            // La faccia si segna con i suoi bordi (i segni di faccia valgono
            // solo per le facce piane e di rivoluzione).
            for (const ForgeCad::Kernel::EdgeId edge : ForgeCad::faceBoundaryEdges(brep, ForgeCad::Kernel::FaceId(feature.index))) {
                GeometryRef border = ref;
                border.kind = 4;
                const auto &geometry = brep.edge(edge);
                border.point = ForgeCad::edgeReference(brep, edge, geometry.curve->point(0.5 * (geometry.range.lo + geometry.range.hi)));
                marks.append(border);
            }
            lines << QStringLiteral("Faccia sottile F%1: larghezza circa %2 mm").arg(feature.index).arg(feature.measure, 0, 'g', 3);
            continue;
        } else {
            ref.kind = 4;
            ref.point = ForgeCad::edgeReference(brep, ForgeCad::Kernel::EdgeId(feature.index), feature.location);
            if (feature.kind == ForgeCad::Kernel::MicroFeature::Kind::ShortEdge)
                lines << QStringLiteral("Spigolo corto E%1: %2 mm").arg(feature.index).arg(feature.measure, 0, 'g', 3);
            else
                lines << QStringLiteral("Spigolo quasi tangente E%1: %2 gradi tra le facce").arg(feature.index)
                             .arg(feature.measure * 180.0 / M_PI, 0, 'g', 3);
        }
        marks.append(ref);
    }
    return marks;
}

static void shapeAnalysisDialog(QMainWindow *window, CadViewport *viewport) {
    QDialog dialog(window);
    dialog.setWindowTitle(QStringLiteral("Curvatura, zebra e continuità"));
    dialog.resize(660, 600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *body = new QComboBox(&dialog);
    body->setObjectName(QStringLiteral("shapeAnalysisBody"));
    const auto &objects = viewport->extrusions();
    QVector<int> candidates;
    for (int index : viewport->resultBodiesBefore(-1))
        if (objects[index].visible) {
            candidates.append(index);
            body->addItem(logicalBodyLabel(viewport,index),index);
        }
    // Le curve 3D sono riferimenti indipendenti, non stadi intermedi di un corpo.
    for (int i=0; i<objects.size(); ++i)
        if (objects[i].visible && !objects[i].modelBodyId && objects[i].curve)
            body->addItem(QStringLiteral("Curva 3D: ")+objects[i].name,i);
    const auto &sketches = viewport->sketches();
    for (int i=0; i<sketches.size(); ++i)
        if (sketches[i].visible) body->addItem(QStringLiteral("Schizzo: ") + sketches[i].name, -2-i);
    if (!body->count()) {
        QMessageBox::information(window, dialog.windowTitle(), QStringLiteral("Occorre un corpo, una superficie, una curva 3D o uno schizzo visibile."));
        return;
    }
    const auto selection = viewport->selection();
    if (selection.kind == SceneObjectKind::Extrusion) {
        const int mapped = resultBodyForPick(viewport,candidates,selection.index);
        const int row = body->findData(mapped>=0?mapped:selection.index); if (row>=0) body->setCurrentIndex(row);
    }
    if (selection.kind == SceneObjectKind::Sketch) {
        const int row = body->findData(-2-selection.index); if (row>=0) body->setCurrentIndex(row);
    }
    auto *face = new QComboBox(&dialog);
    face->setObjectName(QStringLiteral("shapeAnalysisFace"));
    auto *pickFace = new QPushButton(QStringLiteral("Scegli faccia nella vista"), &dialog);
    pickFace->setObjectName(QStringLiteral("shapeAnalysisPickFace")); pickFace->setCheckable(true);
    auto fillFaces = [&] {
        const QSignalBlocker blocker(face);
        face->clear(); face->addItem(QStringLiteral("Tutte le facce"),-1);
        const int index=body->currentData().toInt();
        const bool hasFaces=index>=0 && objects[index].forgeBody;
        if(hasFaces) for(auto id:objects[index].forgeBody->faces()) face->addItem(QStringLiteral("Faccia F%1").arg(id.index),id.index);
        face->setEnabled(hasFaces); pickFace->setEnabled(hasFaces);
    };
    fillFaces();
    auto *mode = new QComboBox(&dialog);
    mode->setObjectName(QStringLiteral("shapeAnalysisMode"));
    mode->addItems({QStringLiteral("Zebra — riflessi"), QStringLiteral("Curvatura — intensità"),
        QStringLiteral("Forma locale — sella / piatta / doppia curvatura"), QStringLiteral("Giunzioni — semaforo"),
        QStringLiteral("Curve e bordi — pettine"), QStringLiteral("Superfici — griglia U/V e pettini")});
    if (body->currentData().toInt()<0 || objects[body->currentData().toInt()].curve) mode->setCurrentIndex(4);
    auto spin = [&dialog](double lo, double hi, double value, int decimals) {
        auto *s = new QDoubleSpinBox(&dialog); s->setDecimals(decimals); s->setRange(lo,hi); s->setValue(value); return s;
    };
    auto *frequency = spin(1,100,12,0), *angle = spin(0,180,0,0);
    auto *limit = spin(.000001,1000000,.1,6), *gain = spin(.000001,1000000,100,6);
    auto *autoGain = new QCheckBox(QStringLiteral("Adatta i denti alla dimensione della curva"), &dialog);
    autoGain->setChecked(true);
    auto *uLines = spin(0,40,6,0), *vLines = spin(0,40,6,0), *isoSamples = spin(4,200,32,0);
    auto *normalOnly = new QCheckBox(QStringLiteral("Misura quanto piega la superficie"), &dialog);
    normalOnly->setChecked(true);
    auto *through = new QCheckBox(QStringLiteral("Mostra anche le parti nascoste dalla superficie"), &dialog);
    through->setChecked(true);
    through->setObjectName(QStringLiteral("shapeCombThrough"));
    uLines->setObjectName(QStringLiteral("shapeULines")); vLines->setObjectName(QStringLiteral("shapeVLines"));
    isoSamples->setObjectName(QStringLiteral("shapeIsoSamples"));
    normalOnly->setObjectName(QStringLiteral("shapeNormalCurvature"));
    auto *angleTol = spin(.001,45,.1,3), *curvTol = spin(.000001,100, .001,6);
    form->addRow(QStringLiteral("Corpo / riferimento"),body);
    form->addRow(QStringLiteral("Faccia da analizzare"),face); form->addRow(pickFace);
    form->addRow(QStringLiteral("Lettura"),mode);
    form->addRow(QStringLiteral("Densità zebra"),frequency); form->addRow(QStringLiteral("Direzione zebra (°)"),angle);
    form->addRow(QStringLiteral("Rosso da |k| (mm⁻¹)"),limit); form->addRow(QStringLiteral("Scala pettine (mm²)"),gain);
    form->addRow(QStringLiteral("Scala automatica pettine"),autoGain);
    form->addRow(QStringLiteral("Linee U costante (ciano)"),uLines);
    form->addRow(QStringLiteral("Linee V costante (magenta)"),vLines);
    form->addRow(QStringLiteral("Denti per curva U/V (circa)"),isoSamples);
    form->addRow(QStringLiteral("Curvatura normale"),normalOnly);
    form->addRow(QStringLiteral("Pettini sempre visibili"),through);
    form->addRow(QStringLiteral("Soglia tangenza (°)"),angleTol); form->addRow(QStringLiteral("Soglia curvatura (mm⁻¹)"),curvTol);
    layout->addLayout(form);
    auto *help = new QLabel(&dialog); help->setWordWrap(true); layout->addWidget(help);
    auto *report = new QTextBrowser(&dialog); layout->addWidget(report,1);
    auto *apply = new QPushButton(QStringLiteral("Aggiorna analisi"), &dialog); layout->addWidget(apply);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    const QStringList guides = {
        QStringLiteral("Ruota la vista e la direzione delle strisce. Una rottura del riflesso suggerisce un salto di tangenza; un cambio brusco di andamento suggerisce un cambio di curvatura. Zebra qualitative sulla tessellazione: non identificano da sole i flessi."),
        QStringLiteral("Blu = curvatura zero, verde = metà scala, rosso = limite o superiore; grigio = non valutabile. Si mostra max(|k1|,|k2|), in mm⁻¹: 0,1 corrisponde a un raggio di 10 mm. Non indica il segno. Le fasce triangolari dipendono dal campionamento."),
        QStringLiteral("Blu = sella (curvature di segno opposto); arancio = doppia curvatura dello stesso segno; verde = almeno una curvatura quasi nulla entro soglia. Un cilindro è verde senza essere piano. Il confine tra colori non è una certificazione di flesso; grigio = non valutabile."),
        QStringLiteral("Rosso = salto di tangenza o posizione; arancio = salto di curvatura; verde = regolare entro soglia nei campioni; grigio = bordo libero o non valutabile. Controlla 31 punti per bordo condiviso nello stesso corpo: non confronta corpi separati né nodi interni alle facce. Non certifica G1/G2 sull'intero bordo."),
        QStringLiteral("Denti blu: più lunghi dove la curva piega di più, disegnati verso l’esterno della piega. Inviluppo arancio: andamento del pettine. Su una curva piana, un flesso regolare passa per curvatura zero e cambia lato; in 3D non esiste un lato firmato unico. Pettine sulle curve dello schizzo, sui bordi del corpo o sulla curva 3D, campionato per tratti: non certifica assenza di salti."),
        QStringLiteral("Ciano: U costante; magenta: V costante. Griglia sulla superficie e pettini lungo le sue curve U/V. Con Curvatura normale si misura la piega della superficie in quella direzione; disattivandola si misura la curvatura completa della curva U/V, che può piegare anche su un piano. Denti più lunghi = piega più stretta; scala comune alle due famiglie. Le direzioni U/V non sono necessariamente quelle di curvatura massima e minima. Ritaglio grafico sui contorni e sui fori, approssimato dalla tessellazione; analisi campionata.")};
    auto refresh = [&] {
        viewport->clearShapeAnalysis();
        const int m=mode->currentIndex();
        help->setText(guides[m]);
        frequency->setEnabled(m==0); angle->setEnabled(m==0); limit->setEnabled(m==1);
        const bool comb = m==4 || m==5;
        autoGain->setEnabled(comb); gain->setEnabled(comb && !autoGain->isChecked()); angleTol->setEnabled(m==3); curvTol->setEnabled(m==2 || m==3);
        form->setRowVisible(frequency,m==0); form->setRowVisible(angle,m==0); form->setRowVisible(limit,m==1);
        form->setRowVisible(autoGain,comb); form->setRowVisible(gain,comb); form->setRowVisible(through,comb);
        form->setRowVisible(angleTol,m==3); form->setRowVisible(curvTol,m==2 || m==3);
        form->setRowVisible(uLines,m==5); form->setRowVisible(vLines,m==5);
        form->setRowVisible(isoSamples,m==5); form->setRowVisible(normalOnly,m==5);
        const int i=body->currentData().toInt();
        if (m!=4 && (i<0 || !objects[i].forgeBody)) { report->setPlainText(QStringLiteral("Per uno schizzo o una curva 3D scegli Pettine.")); return; }
        try {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const double scale=comb?(autoGain->isChecked()?0:gain->value()):limit->value(), at=angleTol->value(), kt=curvTol->value();
            const int faceId=face->currentData().toInt();
            ForgeCad::SurfaceCombOptions isoOptions; isoOptions.faceId=faceId;
            isoOptions.uLines=int(uLines->value()); isoOptions.vLines=int(vLines->value());
            isoOptions.samples=int(isoSamples->value()); isoOptions.normalOnly=normalOnly->isChecked();
            ForgeCad::ShapeAnalysis result;
            report->setPlainText(QStringLiteral("Calcolo in corso…"));
            viewport->calculateShapeAnalysis([&] {
                if(i>=0) result=m==5 ? ForgeCad::analyzeSurfaceComb(objects[i],scale,isoOptions) : ForgeCad::analyzeShape(objects[i],m,scale,at,kt,faceId);
                else {
                    const auto &sketch=sketches[-2-i];
                    ForgeCad::Kernel::Frame3 frame; double height;
                    ForgeCad::forgeSketchFrame(sketch,0,frame,height);
                    std::vector<ForgeCad::ForgeCurve> curves;
                    for(const auto &segment:ForgeCad::forgeSketchSegments(sketch))
                        curves.push_back(std::make_shared<ForgeCad::Kernel::TrimmedCurve<3>>(
                            ForgeCad::Kernel::embedCurve(segment.curve,frame),segment.range.lo,segment.range.hi));
                    double sketchScale=scale;
                    if(scale==0) {
                        ForgeCad::Kernel::Vec3 lo,hi; bool first=true; double maxK=0;
                        for(const auto &curve:curves) {
                            const auto breaks=curve->breakpoints(curve->domain());
                            for(std::size_t j=1;j<breaks.size();++j) for(int k=0;k<=24;++k) {
                                ForgeCad::Kernel::Vec3 p,c;
                                if(!ForgeCad::curveCurvature(*curve,breaks[j-1]+(breaks[j]-breaks[j-1])*(k+.001)/24.002,p,c)) continue;
                                if(first) { lo=hi=p; first=false; }
                                for(int axis=0;axis<3;++axis) { lo[axis]=std::min(lo[axis],p[axis]); hi[axis]=std::max(hi[axis],p[axis]); }
                                maxK=std::max(maxK,ForgeCad::Kernel::norm(c));
                            }
                        }
                        sketchScale=maxK>0?.15*ForgeCad::Kernel::distance(lo,hi)/maxK:1;
                    }
                    for(const auto &geometry:curves) {
                        ExtrusionObject curve; curve.curve=geometry;
                        const auto part=ForgeCad::analyzeShape(curve,4,sketchScale,at,kt);
                        result.lines+=part.lines; result.report+=part.report;
                    }
                }
            });
            viewport->shapeAnalysis_=std::move(result);
            viewport->shapeAnalysisFace_=faceId;
            if(i>=0 && faceId>=0) viewport->shapeAnalysisFaceDisplay_=ForgeCad::analysisFaceDisplay(objects[i].display,faceId);
            viewport->shapeAnalysis_.report.prepend(QStringLiteral("%1 — %2").arg(body->currentText(),face->currentText()));
            QApplication::restoreOverrideCursor();
            viewport->shapeAnalysisBody_=i; viewport->shapeAnalysisMode_=m; viewport->shapeAnalysisThrough_=through->isChecked();
            viewport->zebraFrequency_=float(frequency->value()); viewport->zebraAngle_=float(angle->value()*M_PI/180);
            report->setPlainText(viewport->shapeAnalysis_.report.join(QLatin1Char('\n')));
            viewport->update();
        } catch(const std::exception &e) { QApplication::restoreOverrideCursor(); report->setPlainText(QString::fromUtf8(e.what())); }
    };
    QObject::connect(autoGain,&QCheckBox::toggled,&dialog,[&] { gain->setEnabled((mode->currentIndex()==4 || mode->currentIndex()==5) && !autoGain->isChecked()); });
    QObject::connect(through,&QCheckBox::toggled,&dialog,[&](bool checked) { viewport->shapeAnalysisThrough_=checked; viewport->update(); });
    QObject::connect(apply,&QPushButton::clicked,&dialog,refresh);
    QObject::connect(mode,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,refresh);
    QObject::connect(face,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,refresh);
    QObject::connect(body,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&] {
        viewport->cancelReferencePick(); pickFace->setChecked(false); fillFaces(); refresh();
    });
    QObject::connect(pickFace,&QPushButton::clicked,&dialog,[&](bool checked) {
        if(!checked) { viewport->cancelReferencePick(); return; }
        const QString error=viewport->beginReferencePick(ForgeCad::DatumRoleFace,-1);
        if(!error.isEmpty()) { pickFace->setChecked(false); report->setPlainText(error); }
        else report->setPlainText(QStringLiteral("Clicca una faccia del corpo nella vista. Esc termina la scelta."));
    });
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        pickFace->setChecked(false); viewport->cancelReferencePick();
        if(!picked) return;
        // Gli ID di faccia devono riferirsi proprio alla geometria risultante.
        if(ref.kind!=5 || !candidates.contains(ref.index)) {
            report->setPlainText(QStringLiteral("Scegli una faccia di un corpo risultante visibile.")); return;
        }
        const int row=body->findData(ref.index);
        if(row<0) return;
        { const QSignalBlocker blocker(body); body->setCurrentIndex(row); }
        fillFaces();
        const int faceRow=face->findData(ref.point.subshape);
        if(faceRow<1) { report->setPlainText(QStringLiteral("Faccia non disponibile sul corpo risultante.")); refresh(); return; }
        face->setCurrentIndex(faceRow);
    });
    QObject::connect(frequency,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&] { if(mode->currentIndex()==0) refresh(); });
    QObject::connect(angle,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&] { if(mode->currentIndex()==0) refresh(); });
    refresh();
    runModeless(window,viewport,dialog);
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->clearShapeAnalysis();
}

static void microFeaturesDialog(QWidget *parent, CadViewport *viewport) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    int index = -1;
    const SceneSelection selection = viewport->selection();
    if (selection.kind == SceneObjectKind::Extrusion && selection.index >= 0 && selection.index < bodies.size()
        && bodies.at(selection.index).forgeBody)
        index = selection.index;
    for (int k = bodies.size() - 1; k >= 0 && index < 0; --k)
        if (bodies.at(k).visible && bodies.at(k).forgeBody) index = k;
    if (index < 0) {
        QMessageBox::information(parent, QStringLiteral("Micro-geometrie"), QStringLiteral("Nella scena non ci sono corpi."));
        return;
    }
    const ExtrusionObject &body = bodies.at(index);
    QStringList lines;
    QVector<GeometryRef> marks;
    try {
        marks = microFeatureMarks(body, index, lines);
    } catch (const std::exception &failure) {
        QMessageBox::warning(parent, QStringLiteral("Micro-geometrie"), QString::fromUtf8(failure.what()));
        return;
    }
    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("Micro-geometrie di %1").arg(body.name));
    if (lines.isEmpty()) {
        box.setText(QStringLiteral("Nessuna faccia sottile (sotto 0,05 mm), nessuno spigolo corto (sotto 0,05 mm) "
                                   "e nessuno spigolo quasi tangente (tra 0,06 e 10 gradi)."));
    } else {
        box.setText(QStringLiteral("%1 micro-geometrie, segnate nella vista. Di solito sono rumore di una feature a monte "
                                   "(loft, offset, estensioni) e fanno fallire raccordi e offset: conviene correggerle li'.")
                        .arg(lines.size()));
        box.setDetailedText(lines.join(QLatin1Char('\n')));
    }
    viewport->setReferenceMarks(marks);
    box.exec();
    viewport->setReferenceMarks({});
}

static void massPropertiesDialog(QWidget *parent, CadViewport *viewport) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    QVector<int> candidates;
    for (int index = 0; index < bodies.size(); ++index)
        if (bodies.at(index).curve || bodies.at(index).forgeBody) candidates.append(index);
    if (candidates.isEmpty()) {
        QMessageBox::information(parent, QStringLiteral("Proprieta' di massa"), QStringLiteral("Nella scena non ci sono corpi."));
        return;
    }
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Proprieta' di massa"));
    dialog.resize(620, 640);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *bodyBox = new QComboBox(&dialog);
    bodyBox->addItem(QStringLiteral("Tutti i solidi visibili"));
    for (int index : candidates)
        bodyBox->addItem(bodies.at(index).visible ? bodies.at(index).name : bodies.at(index).name + QStringLiteral(" (nascosto)"));
    const SceneSelection selection = viewport->selection();
    if (selection.kind == SceneObjectKind::Extrusion && candidates.contains(selection.index)) bodyBox->setCurrentIndex(int(candidates.indexOf(selection.index)) + 1);
    struct Material {
        const char *name;
        double density;  // g/cm^3
    };
    static const Material materials[] = {
        {"Acciaio", 7.85}, {"Acciaio inox AISI 304", 8.00}, {"Ghisa", 7.20}, {"Alluminio", 2.70}, {"Ottone", 8.50}, {"Rame", 8.96},
        {"Titanio", 4.51}, {"PP (polipropilene)", 0.905}, {"PE-HD", 0.955}, {"PE-LD", 0.92}, {"PET", 1.38}, {"PETG", 1.27},
        {"ABS", 1.05}, {"PC (policarbonato)", 1.20}, {"PA6", 1.13}, {"POM", 1.41}, {"PVC rigido", 1.40}, {"PS", 1.05}, {"PMMA", 1.18},
        {"Acqua", 1.00}};
    auto *materialBox = new QComboBox(&dialog);
    for (const Material &m : materials) materialBox->addItem(QString::fromUtf8(m.name));
    materialBox->addItem(QStringLiteral("Personalizzato"));
    auto *densityBox = new ForgeCad::ExpressionSpinBox(&dialog);
    densityBox->setDecimals(5);
    densityBox->setRange(1e-6, 100.0);
    densityBox->setSuffix(QStringLiteral(" g/cm³"));
    QSettings settings;
    materialBox->setCurrentIndex(qBound(0, settings.value(QStringLiteral("mass/material"), 0).toInt(), materialBox->count() - 1));
    densityBox->setValue(settings.value(QStringLiteral("mass/density"), materials[0].density).toDouble());
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Materiale:"), materialBox);
    form->addRow(QStringLiteral("Densita':"), densityBox);
    layout->addLayout(form);
    auto *text = new QTextBrowser(&dialog);
    text->setOpenLinks(false);
    layout->addWidget(text, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *copy = buttons->addButton(QStringLiteral("Copia"), QDialogButtonBox::ActionRole);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    // Proprieta' (densita' 1) per corpo, calcolate una volta sola.
    QMap<int, ForgeCad::MassReport> cache;
    const auto reportFor = [&](int index) {
        if (!cache.contains(index)) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            cache.insert(index, viewport->massReport(index));
            QApplication::restoreOverrideCursor();
        }
        return cache.value(index);
    };
    QString plain;
    const auto show = [&] {
        const double rho = densityBox->value() * 1e-3;  // g/mm^3
        ForgeCad::MassReport r;
        QString name;
        if (bodyBox->currentIndex() == 0) {
            QVector<ForgeCad::MassReport> parts;
            for (int index : candidates) {
                const ExtrusionObject &body = bodies.at(index);
                if (!body.visible || !body.solid || body.curve) continue;
                parts.append(reportFor(index));
            }
            if (parts.isEmpty()) {
                text->setHtml(QStringLiteral("<p>Nessun solido visibile.</p>"));
                plain.clear();
                return;
            }
            r = ForgeCad::combineMass(parts);
            name = QStringLiteral("Tutti i solidi visibili (%1; i volumi che si sovrappongono si contano due volte)").arg(parts.size());
        } else {
            const int index = candidates.at(bodyBox->currentIndex() - 1);
            r = reportFor(index);
            name = bodies.at(index).name;
        }
        const auto num = [](double v, int digits = 6) { return QString::number(v, 'g', digits); };
        const QString linearUnit = ForgeCad::lengthUnitSymbol(ForgeCad::displayLengthUnit());
        const double lengthScale = ForgeCad::millimetersPerUnit(ForgeCad::displayLengthUnit());
        const double inertiaScale = lengthScale * lengthScale;
        QStringList lines;
        QString html = QStringLiteral("<h3>%1</h3>").arg(name.toHtmlEscaped());
        const auto row = [&](const QString &label, const QString &value) {
            html += QStringLiteral("<tr><td style='padding-right:14px;color:#9fb3c8'>%1</td><td>%2</td></tr>").arg(label, value);
            lines.append(label + QStringLiteral("\t") + QString(value).remove(QRegularExpression(QStringLiteral("<[^>]*>"))));
        };
        if (!r.ok) {
            text->setHtml(html + QStringLiteral("<p style='color:#ff7b72'>Calcolo non riuscito: %1</p>").arg(r.error.toHtmlEscaped()));
            plain.clear();
            return;
        }
        html += QStringLiteral("<table>");
        if (r.kind == ForgeCad::MassReport::Kind::Curve) {
            row(QStringLiteral("Lunghezza"), ForgeCad::formatLength(r.length));
            row(QStringLiteral("Baricentro della curva"), QStringLiteral("X %1, Y %2, Z %3")
                .arg(ForgeCad::formatLength(r.centroid[0]), ForgeCad::formatLength(r.centroid[1]), ForgeCad::formatLength(r.centroid[2])));
        } else if (r.kind == ForgeCad::MassReport::Kind::Sheet) {
            row(QStringLiteral("Area della superficie"), ForgeCad::formatArea(r.area));
            if (r.hasCentroid)
                row(QStringLiteral("Baricentro dell'area"), QStringLiteral("X %1, Y %2, Z %3")
                    .arg(ForgeCad::formatLength(r.centroid[0]), ForgeCad::formatLength(r.centroid[1]), ForgeCad::formatLength(r.centroid[2])));
        } else {
            const double mass = rho * r.volume;
            row(QStringLiteral("Volume"), ForgeCad::formatVolume(r.volume));
            row(QStringLiteral("Area della superficie"), ForgeCad::formatArea(r.area));
            row(QStringLiteral("Densita'"), num(densityBox->value()) + QStringLiteral(" g/cm³"));
            row(QStringLiteral("Massa"), QStringLiteral("<b>%1 g</b> (%2 kg)").arg(num(mass, 9), num(mass * 1e-3, 9)));
            if (r.hasCentroid) {
                row(QStringLiteral("Baricentro"), QStringLiteral("X %1, Y %2, Z %3")
                    .arg(ForgeCad::formatLength(r.centroid[0]), ForgeCad::formatLength(r.centroid[1]), ForgeCad::formatLength(r.centroid[2])));
                // Momenti (Ixx = \\int (y^2 + z^2) dm) e prodotti d'inerzia (Pxy = \\int x y dm), in g mm^2.
                const auto tensorRows = [&](const double t[3][3], const QString &where) {
                    row(QStringLiteral("Momenti d'inerzia %1").arg(where),
                        QStringLiteral("Ixx %1, Iyy %2, Izz %3 g·%4²")
                            .arg(num(rho * t[0][0] / inertiaScale, 9), num(rho * t[1][1] / inertiaScale, 9),
                                 num(rho * t[2][2] / inertiaScale, 9), linearUnit));
                    row(QStringLiteral("Prodotti d'inerzia %1").arg(where),
                        QStringLiteral("Pxy %1, Pxz %2, Pyz %3 g·%4²")
                            .arg(num(-rho * t[0][1] / inertiaScale, 9), num(-rho * t[0][2] / inertiaScale, 9),
                                 num(-rho * t[1][2] / inertiaScale, 9), linearUnit));
                };
                tensorRows(r.inertia, QStringLiteral("al baricentro"));
                double moments[3], axes[3][3];
                ForgeCad::principalMoments(r.inertia, moments, axes);
                for (int k = 0; k < 3; ++k)
                    row(QStringLiteral("Momento principale I%1").arg(k + 1),
                        QStringLiteral("%1 g·%2² lungo (%3, %4, %5), raggio d'inerzia %6")
                            .arg(num(rho * moments[k] / inertiaScale, 9), linearUnit, num(axes[0][k], 6),
                                 num(axes[1][k], 6), num(axes[2][k], 6),
                                 ForgeCad::formatLength(r.volume > 0.0 ? std::sqrt(std::max(0.0, moments[k]) / r.volume) : 0.0, 6)));
                const double origin[3] = {0.0, 0.0, 0.0};
                double about[3][3];
                ForgeCad::inertiaAbout(r, origin, about);
                tensorRows(about, QStringLiteral("all'origine"));
            }
        }
        html += QStringLiteral("</table><p style='color:#8aa0b4'>Calcolo: %1.<br>Unita': %2, g. Prodotti d'inerzia con il segno positivo (Pxy = ∫ x y dm); "
                               "il tensore d'inerzia ha fuori diagonale -Pxy.</p>")
                    .arg(r.method.toHtmlEscaped(), linearUnit);
        text->setHtml(html);
        plain = name + QStringLiteral("\n") + lines.join(QLatin1Char('\n'));
    };
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, show);
    QObject::connect(materialBox, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        if (index >= 0 && index < int(std::size(materials))) densityBox->setValue(materials[index].density);
        QSettings().setValue(QStringLiteral("mass/material"), index);
    });
    QObject::connect(densityBox, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
        QSettings().setValue(QStringLiteral("mass/density"), value);
        show();
    });
    QObject::connect(copy, &QPushButton::clicked, &dialog, [&] { QGuiApplication::clipboard()->setText(plain); });
    show();
    dialog.exec();
}

// Finestra del taglio: corpo e strumento (corpo, superficie, piano o schizzo),
// parti da tenere scelte nell'elenco o direttamente con un clic nella vista.
// Le parti si ricalcolano a ogni cambio di superficie o di strumento; ognuna
// e' ricordata da un suo punto (trimKeep). `definition` porta i valori
// iniziali; `replaced` e' il corpo modificato (-1 nuovo): si scelgono solo i
// corpi che vengono prima. `apply` fa il comando.
static bool trimDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, ExtrusionObject definition,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    // Superficie e strumento: i corpi che esistono in quel punto della storia, non le feature.
    const QVector<int> used = {definition.firstBody, definition.secondBody};
    const QVector<int> sheets = operandBodies(viewport, replaced, used, [](const ExtrusionObject &) { return true; });
    QVector<int> tools = operandBodies(viewport, replaced, used, [](const ExtrusionObject &) { return true; });
    const int historyEnd = replaced >= 0 ? replaced : int(viewport->extrusions().size());
    for (int index = 0; index < historyEnd; ++index)
        if (viewport->extrusions().at(index).feature == BodyFeature::DatumPlane
            && viewport->extrusions().at(index).datumValid && !tools.contains(index)) tools.append(index);
    if (sheets.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Serve un corpo o una superficie da tagliare."));
        return false;
    }
    const auto label = [&](int index) { return resultBodyLabel(viewport, index); };
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *sheetBox = new QComboBox(&dialog), *toolBox = new QComboBox(&dialog), *partBox = new QComboBox(&dialog);
    auto *bothBox = new QCheckBox(QStringLiteral("Rifila entrambi i corpi"), &dialog);
    auto *toolPartBox = new QComboBox(&dialog);
    partBox->setObjectName(QStringLiteral("trimPart"));
    toolPartBox->setObjectName(QStringLiteral("trimToolPart"));
    bothBox->setObjectName(QStringLiteral("trimBoth"));
    bothBox->setChecked(definition.trimBoth);
    bothBox->setToolTip(QStringLiteral("Conserva una parte di ciascun corpo e rimuove da entrambi le porzioni oltre l'intersezione."));
    for (int index : sheets) sheetBox->addItem(label(index));
    for (int index : tools) toolBox->addItem(label(index));
    const int sketchStart = int(tools.size());
    for (const SketchObject &sketch : viewport->sketches()) toolBox->addItem(QStringLiteral("Schizzo: ") + sketch.name);
    const int planeStart = sketchStart + int(viewport->sketches().size());
    for (const QString &plane : planeNames()) toolBox->addItem(plane);
    sheetBox->setCurrentIndex(qMax(0, int(sheets.indexOf(definition.firstBody))));
    if (definition.secondBody >= 0 && tools.contains(definition.secondBody)) toolBox->setCurrentIndex(int(tools.indexOf(definition.secondBody)));
    else if (definition.sketchIndex >= 0 && definition.sketchIndex < viewport->sketches().size())
        toolBox->setCurrentIndex(sketchStart + definition.sketchIndex);
    else if (definition.secondBody < 0 && definition.firstBody >= 0)
        toolBox->setCurrentIndex(planeStart + qBound(0, definition.trimPlane, 2));
    else {
        // Il primo corpo che non e' la superficie.
        for (int k = 0; k < tools.size(); ++k)
            if (tools.at(k) != sheets.at(sheetBox->currentIndex())) {
                toolBox->setCurrentIndex(k);
                break;
            }
    }
    // Superficie e strumento si scelgono anche cliccandoli nella vista: per una
    // funzione nuova senza superficie selezionata prima la superficie, poi lo strumento.
    BodyPicker sheetPicker(dialog, viewport, replaced, sheets, sheetBox), toolPicker(dialog, viewport, replaced, tools, toolBox);
    toolPicker.acceptSketches(sketchStart);
    form->addRow(QStringLiteral("Corpo o superficie da tagliare:"), sheetPicker.row(sheetBox));
    form->addRow(QStringLiteral("Strumento (corpo, superficie, piano o schizzo):"), toolPicker.row(toolBox));
    // Strumento schizzo: proiezione sulla prima faccia incontrata (di default
    // nelle funzioni nuove) o taglio passante lungo la normale (file precedenti).
    auto *methodBox = new QComboBox(&dialog);
    methodBox->addItems({QStringLiteral("Proiezione sulla prima faccia incontrata"), QStringLiteral("Taglio passante lungo la normale (tutte le facce)")});
    methodBox->setCurrentIndex(definition.trimProject || replaced < 0 ? 0 : 1);
    auto *projectModeBox = new QComboBox(&dialog);
    projectModeBox->addItems({QStringLiteral("Togli le parti dentro il profilo"), QStringLiteral("Tieni solo le parti dentro il profilo"),
                              QStringLiteral("Dividi soltanto le facce (linea di divisione)")});
    projectModeBox->setCurrentIndex(qBound(0, definition.projectionMode, 2));
    auto *projectReverse = new QCheckBox(QStringLiteral("Inverti il verso della proiezione"), &dialog);
    projectReverse->setChecked(definition.projectionReverse);
    auto *projectNote = wrappedNote(QStringLiteral("Lo schizzo (senza le entita' di costruzione) va lungo la normale del suo piano, di default verso il corpo, "
                                                   "sulla prima faccia che incontra: le facce sotto restano intere. I tratti che ricalcano bordi "
                                                   "esistenti (anche la corda di un bordo curvo corto, entro 0,001 mm) delimitano la regione senza nuovi tagli."), &dialog);
    auto *projectResult = new QLabel(&dialog);
    projectResult->setWordWrap(true);
    projectResult->setMaximumWidth(360);
    projectResult->setStyleSheet(QStringLiteral("color: #9fc6e8;"));
    form->addRow(QStringLiteral("Metodo dello schizzo:"), methodBox);
    form->addRow(QStringLiteral("Taglio:"), projectModeBox);
    form->addRow(QString(), projectReverse);
    form->addRow(projectNote);
    form->addRow(QStringLiteral("Esito:"), projectResult);
    form->addRow(QStringLiteral("Parte da tenere:"), partBox);
    form->addRow(QString(), bothBox);
    form->addRow(QStringLiteral("Parte del secondo corpo:"), toolPartBox);
    auto *choiceHelp = new QLabel(QStringLiteral("Passa il mouse sulle regioni: quella verde verra' mantenuta con un clic."), &dialog);
    choiceHelp->setWordWrap(true);
    choiceHelp->setStyleSheet(QStringLiteral("color: #8ee8a2; font-weight: 600;"));
    choiceHelp->setMaximumWidth(360);
    form->addRow(QStringLiteral("Scelta nella vista:"), choiceHelp);
    const PreviewScope scope(viewport, dialog, form, replaced);
    {
        QLabel *previewLabel = scope.label;
        viewport->setPreviewCallback([previewLabel, projectResult, viewport](const QString &error) {
            previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
            projectResult->setText(error.isEmpty() ? viewport->previewNotice() : QString());
        });
    }
    QVector<SheetPiece> pieces, toolPieces;
    QString piecesKey, toolPiecesKey, piecesError, toolPiecesError;
    EdgePoint clickedKeep, clickedToolKeep;
    bool clickedKeepValid = false, clickedToolKeepValid = false;
    bool first = definition.firstBody >= 0 && replaced >= 0;
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.operation = -1;
        d.feature = BodyFeature::SheetTrim;
        d.firstBody = sheets.at(sheetBox->currentIndex());
        const int tool = toolBox->currentIndex();
        d.secondBody = tool < tools.size() ? tools.at(tool) : -1;
        d.sketchIndex = tool >= sketchStart && tool < planeStart ? tool - sketchStart : -1;
        d.trimPlane = tool >= planeStart ? tool - planeStart : 0;
        d.trimBoth = bothBox->isChecked() && d.secondBody >= 0;
        d.trimProject = d.sketchIndex >= 0 && methodBox->currentIndex() == 0;
        d.projectionMode = projectModeBox->currentIndex();
        d.projectionReverse = projectReverse->isChecked();
        if (clickedKeepValid) d.trimKeep = clickedKeep;
        else if (partBox->currentIndex() >= 0 && partBox->currentIndex() < pieces.size())
            d.trimKeep = pieces.at(partBox->currentIndex()).point;
        if (clickedToolKeepValid) d.trimToolKeep = clickedToolKeep;
        else if (toolPartBox->currentIndex() >= 0 && toolPartBox->currentIndex() < toolPieces.size())
            d.trimToolKeep = toolPieces.at(toolPartBox->currentIndex()).point;
        return d;
    };
    const auto refreshPreview = [&] {
        const ExtrusionObject d = current();
        const int firstPart = partBox->currentIndex();
        if (pieces.size() < 2 || firstPart < 0 || firstPart >= pieces.size()) return;
        ForgeCad::ForgeBody geometry = pieces.at(firstPart).geometry;
        BodyDisplay display = pieces.at(firstPart).display;
        if (d.trimBoth) {
            const int secondPart = toolPartBox->currentIndex();
            if (toolPieces.size() < 2 || secondPart < 0 || secondPart >= toolPieces.size()) return;
            QString error;
            geometry = ForgeCad::forgeSew({geometry, toolPieces.at(secondPart).geometry}, 1e-6, false, &error);
            if (!geometry) {
                scope.label->setText(error);
                return;
            }
            ForgeCad::forgeTessellate(*geometry, 0, display);
        }
        viewport->showTrimPreview(d, geometry, display, replaced);
    };
    bool chaining = false;  // superficie scelta nella vista, lo strumento segue
    std::function<void()> installPartPicker;
    const auto refreshPieces = [&] {
        if (chaining || sheetPicker.handling() || toolPicker.handling()) return;
        viewport->setTrimPartPickCallback(-1, {}, -1, {}, {});
        if (sheetPicker.active() || toolPicker.active() || sheetPicker.waiting() || toolPicker.waiting()) {
            viewport->clearPreview();
            scope.label->setText((sheetPicker.active() || sheetPicker.waiting()) ? QStringLiteral("Clicca nella vista la superficie da tagliare.")
                                                       : QStringLiteral("Clicca nella vista il corpo o lo schizzo che taglia (o scegli un piano nell'elenco)."));
            return;
        }
        const ExtrusionObject d = current();
        // Righe del pannello secondo lo strumento: con lo schizzo proiettato
        // niente parti da scegliere (decide il profilo).
        const bool sketchTool = d.sketchIndex >= 0;
        form->setRowVisible(methodBox, sketchTool);
        for (QWidget *row : {static_cast<QWidget *>(projectModeBox), static_cast<QWidget *>(projectReverse),
                             static_cast<QWidget *>(projectNote), static_cast<QWidget *>(projectResult)})
            form->setRowVisible(row, d.trimProject);
        for (QWidget *row : {static_cast<QWidget *>(partBox), static_cast<QWidget *>(bothBox), static_cast<QWidget *>(toolPartBox),
                             static_cast<QWidget *>(choiceHelp)})
            form->setRowVisible(row, !d.trimProject);
        if (d.trimProject) {
            clickedKeepValid = clickedToolKeepValid = false;
            projectResult->clear();
            scope.request(d);
            return;
        }
        clickedKeepValid = clickedToolKeepValid = false;
        const bool shapeTool = d.secondBody >= 0 && d.secondBody < viewport->extrusions().size()
            && viewport->extrusions().at(d.secondBody).forgeBody;
        bothBox->setEnabled(shapeTool);
        if (!shapeTool) {
            const QSignalBlocker blocker(bothBox);
            bothBox->setChecked(false);
        }
        toolPartBox->setEnabled(bothBox->isChecked());
        choiceHelp->setText(bothBox->isChecked()
            ? QStringLiteral("Passa il mouse sulle regioni di entrambi i corpi: il viewport riconosce automaticamente a quale corpo appartengono. Clicca in verde una parte per ciascun corpo.")
            : QStringLiteral("Passa il mouse sulle regioni del corpo: quella verde verra' mantenuta con un clic; la scelta corrente resta in ambra."));
        const int previousPart = partBox->currentIndex();
        const int previousToolPart = toolPartBox->currentIndex();
        const QString splitKey = QStringLiteral("%1/%2/%3/%4").arg(d.firstBody).arg(d.secondBody).arg(d.trimPlane).arg(d.sketchIndex);
        if (piecesKey != splitKey) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            piecesError.clear();
            pieces = viewport->sheetPieces(d.firstBody, d.secondBody, d.trimPlane, d.sketchIndex, &piecesError);
            QApplication::restoreOverrideCursor();
            piecesKey = splitKey;
        }
        if (bothBox->isChecked() && shapeTool) {
            const QString reverseKey = QStringLiteral("%1/%2").arg(d.secondBody).arg(d.firstBody);
            if (toolPiecesKey != reverseKey) {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                toolPiecesError.clear();
                toolPieces = viewport->sheetPieces(d.secondBody, d.firstBody, 0, -1, &toolPiecesError);
                QApplication::restoreOverrideCursor();
                toolPiecesKey = reverseKey;
            }
        }
        QStringList errors;
        if (pieces.size() < 2)
            errors.append(QStringLiteral("Primo corpo: %1").arg(piecesError.isEmpty()
                ? QStringLiteral("lo strumento non divide la superficie.") : piecesError));
        if (bothBox->isChecked() && toolPieces.size() < 2)
            errors.append(QStringLiteral("Secondo corpo: %1").arg(toolPiecesError.isEmpty()
                ? QStringLiteral("l'intersezione non divide la superficie.") : toolPiecesError));
        const QSignalBlocker blocker(partBox);
        const QSignalBlocker toolBlocker(toolPartBox);
        partBox->clear();
        toolPartBox->clear();
        int chosen = 0;
        double closest = 1e300;
        for (int k = 0; pieces.size() >= 2 && k < pieces.size(); ++k) {
            partBox->addItem(QStringLiteral("Parte %1 (area %2)").arg(k + 1).arg(pieces.at(k).area, 0, 'g', 6));
            // Modifica: la parte piu' vicina al punto di prima.
            const EdgePoint &p = pieces.at(k).point, &q = definition.trimKeep;
            const double d2 = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z);
            if (first && d2 < closest) closest = d2, chosen = k;
        }
        if (!first && previousPart >= 0 && previousPart < pieces.size()) chosen = previousPart;
        first = false;
        partBox->setCurrentIndex(chosen);
        int toolChosen = 0;
        double toolClosest = 1e300;
        for (int k = 0; bothBox->isChecked() && toolPieces.size() >= 2 && k < toolPieces.size(); ++k) {
            toolPartBox->addItem(QStringLiteral("Parte %1 (area %2)").arg(k + 1).arg(toolPieces.at(k).area, 0, 'g', 6));
            const EdgePoint &p = toolPieces.at(k).point, &q = definition.trimToolKeep;
            const double dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (definition.trimBoth && d2 < toolClosest) toolClosest = d2, toolChosen = k;
        }
        if (previousToolPart >= 0 && previousToolPart < toolPieces.size()) toolChosen = previousToolPart;
        toolPartBox->setCurrentIndex(toolChosen);
        installPartPicker();
        if (!errors.isEmpty()) {
            viewport->clearPreview();
            scope.label->setText(errors.join(QLatin1Char('\n')));
            choiceHelp->setText(QStringLiteral("Taglio non pronto: verifica che i corpi si intersechino e che l'intersezione divida ciascuno in almeno due regioni. Le eventuali regioni disponibili restano selezionabili."));
            return;
        }
        scope.label->setText(bothBox->isChecked()
            ? QStringLiteral("Clicca nella vista una parte da tenere per ciascun corpo.")
            : QStringLiteral("Clicca nella vista la parte da tenere, oppure sceglila nell'elenco."));
        refreshPreview();
    };
    installPartPicker = [&] {
        const ExtrusionObject d = current();
        const bool both = bothBox->isChecked();
        viewport->setTrimPartPickCallback(d.firstBody, pieces.size() >= 2 ? pieces : QVector<SheetPiece>{},
                                          both ? d.secondBody : -1, both && toolPieces.size() >= 2 ? toolPieces : QVector<SheetPiece>{},
                                          [&](int side, int piece, EdgePoint point) {
            QVector<SheetPiece> &selectedPieces = side == 1 ? toolPieces : pieces;
            QComboBox *selectedBox = side == 1 ? toolPartBox : partBox;
            if (piece < 0 || piece >= selectedPieces.size()) return;
            if (side == 1) clickedToolKeep = point, clickedToolKeepValid = true;
            else clickedKeep = point, clickedKeepValid = true;
            {
                const QSignalBlocker blocker(selectedBox);
                selectedBox->setCurrentIndex(piece);
            }
            if (pieces.size() < 2 || (bothBox->isChecked() && toolPieces.size() < 2)) return;
            scope.label->setText(QStringLiteral("Parte %1 del %2 corpo scelta nella vista.")
                                 .arg(piece + 1).arg(side == 1 ? QStringLiteral("secondo") : QStringLiteral("primo")));
            if (bothBox->isChecked() && clickedKeepValid && clickedToolKeepValid) {
                choiceHelp->setText(QStringLiteral("Entrambe le parti sono scelte. Le regioni mantenute sono mostrate in ambra; premi OK per applicare."));
            } else if (bothBox->isChecked()) {
                choiceHelp->setText(QStringLiteral("Parte scelta. Passa ora il mouse sulle regioni dell'altro corpo e clicca quella verde da mantenere."));
            }
            refreshPreview();
        });
    };
    for (BodyPicker *picker : {&sheetPicker, &toolPicker}) {
        picker->changed = refreshPieces;
        picker->message = [&](const QString &text, bool) { scope.label->setText(text); };
    }
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (sheetPicker.active()) {
            chaining = sheetPicker.waiting();
            sheetPicker.handle(picked, ref);
            if (chaining && picked && !sheetPicker.active()) toolPicker.start(true);  // poi lo strumento
            chaining = false;
            refreshPieces();
            return;
        }
        toolPicker.handle(picked, ref);
    });
    QObject::connect(sheetBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(toolBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(bothBox, &QCheckBox::toggled, &dialog, refreshPieces);
    QObject::connect(methodBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(projectModeBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(projectReverse, &QCheckBox::toggled, &dialog, refreshPieces);
    QObject::connect(partBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        clickedKeepValid = false;
        refreshPreview();
    });
    QObject::connect(toolPartBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        clickedToolKeepValid = false;
        refreshPreview();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (replaced < 0 && !sheets.contains(definition.firstBody)) sheetPicker.start(true);
    refreshPieces();
    const auto run = [&] {
        sheetPicker.stop();
        toolPicker.stop();
        if (current().trimProject) return apply(current());
        if (pieces.size() < 2) return QStringLiteral("Lo strumento non divide la superficie: scegline un altro.");
        if (current().trimBoth && toolPieces.size() < 2)
            return QStringLiteral("L'intersezione non divide il secondo corpo: non si puo' rifilare da entrambi i lati.");
        return apply(current());
    };
    bool applied = false;
    if (auto *window = qobject_cast<QMainWindow *>(parent)) applied = runUntilAppliedModeless(window, viewport, dialog, form, buttons, run);
    else applied = runUntilApplied(dialog, form, buttons, run);
    viewport->setTrimPartPickCallback(-1, {}, -1, {}, {});
    viewport->setReferencePickCallback(nullptr);
    return applied;
}

// Finestra dell'estensione di una superficie: distanza e tipo (stessa
// superficie o lineare), con l'anteprima; "Bordi..." (se `allowReselect`)
// torna alla scelta dei bordi.
static QVector<EdgePoint> completeExtensionContours(const ForgeCad::ForgeBody &body, const QVector<EdgePoint> &edges) {
    if (!body) return {};
    ForgeCad::Kernel::Box box;
    for (auto vertex : body->vertices()) box.add(body->vertex(vertex).point);
    const double reach = 1e-3 * std::max(1.0, box.diagonal());
    QVector<EdgePoint> result;
    QSet<int> included;
    for (const auto &reference : edges) {
        const auto edge = ForgeCad::resolveEdgeReference(*body, reference, reach);
        if (!edge.valid()) return {};
        const auto loop = ForgeCad::freeBoundaryLoop(*body, edge);
        if (loop.isEmpty()) return {};
        for (const auto &part : loop) {
            if (included.contains(part.subshape)) continue;
            included.insert(part.subshape);
            result.append(part);
        }
    }
    return result;
}

struct ExtendDialogResult {
    QVector<EdgePoint> edges;
    bool applied = false, reselect = false;
    double distance = 0.0;
    bool linear = false;
};
static ExtendDialogResult extendDialog(QWidget *parent, CadViewport *viewport, const QString &title, int base, int hidden, const QVector<EdgePoint> &edges,
                                       double distance, bool linear, bool allowReselect, const std::function<QString(const QVector<EdgePoint> &, double, bool)> &apply) {
    ExtendDialogResult result;
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *distanceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    distanceBox->setDecimals(6);
    distanceBox->setRange(0.000001, 100000.0);
    distanceBox->setValue(distance);
    auto *typeBox = new QComboBox(&dialog);
    typeBox->addItems({QStringLiteral("Stessa superficie (prolungata)"), QStringLiteral("Lineare (tangente)")});
    typeBox->setCurrentIndex(linear ? 1 : 0);
    typeBox->setToolTip(QStringLiteral("Stessa superficie: la superficie prosegue oltre il bordo (piani, cilindri, superfici estruse con la curva prolungata).\n"
                                       "Lineare: una striscia tangente alla superficie lungo il bordo."));
    form->addRow(QStringLiteral("Distanza lungo la superficie:"), distanceBox);
    form->addRow(QStringLiteral("Tipo:"), typeBox);
    const QVector<EdgePoint> complete = completeExtensionContours(viewport->extrusions().value(base).forgeBody, edges);
    auto *completeBox = new QCheckBox(QStringLiteral("Estendi i contorni completi"), &dialog);
    completeBox->setEnabled(!complete.isEmpty());
    completeBox->setToolTip(QStringLiteral("Include tutti i bordi collegati ai bordi scelti, anche quelli delle facce molto sottili."));
    form->addRow(QString(), completeBox);
    auto *edgeCount = new GeometryReferenceList(dialog, viewport, QStringLiteral("extendEdges"));
    form->addRow(QStringLiteral("Bordi:"), edgeCount);
    const auto selectedEdges = [&] { return completeBox->isChecked() ? complete : edges; };
    auto *previewLabel = new QLabel(QStringLiteral("in calcolo..."), &dialog);
    previewLabel->setWordWrap(true);
    previewLabel->setMaximumWidth(360);
    form->addRow(QStringLiteral("Anteprima:"), previewLabel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (allowReselect) {
        QPushButton *edgesButton = buttons->addButton(QStringLiteral("Bordi..."), QDialogButtonBox::ActionRole);
        edgesButton->setToolTip(QStringLiteral("Torna alla scelta dei bordi (quelli di adesso restano scelti)"));
        QObject::connect(edgesButton, &QPushButton::clicked, &dialog, [&dialog, &result] {
            result.reselect = true;
            dialog.reject();
        });
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    const auto refresh = [&] {
        const auto selected = selectedEdges();
        edgeCount->setEdges(base, selected);
        previewLabel->setText(QStringLiteral("in calcolo..."));
        viewport->requestExtendPreview(base, selected, distanceBox->value(), typeBox->currentIndex() == 1, hidden);
    };
    viewport->setPreviewCallback([previewLabel](const QString &error) {
        previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
    });
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(typeBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    refresh();
    distanceBox->selectAll();
    QObject::connect(completeBox, &QCheckBox::toggled, &dialog, refresh);
    result.applied = runUntilApplied(dialog, form, buttons, [&] { return apply(selectedEdges(), distanceBox->value(), typeBox->currentIndex() == 1); });
    result.edges = selectedEdges();
    viewport->setPreviewCallback({});
    viewport->clearPreview();
    result.distance = distanceBox->value();
    result.linear = typeBox->currentIndex() == 1;
    return result;
}

// Finestra della rivoluzione (anche per modificarne una: lo schizzo resta
// quello, `fixedSketch`): schizzo, asse (dall'elenco o scelto nella vista: un
// segmento o una linea di costruzione dello schizzo, gli assi del modello, uno
// spigolo rettilineo o l'asse di una faccia cilindrica, purche' stia nel piano
// dello schizzo), angolo con il verso, risultato (corpo nuovo, unione o
// sottrazione con i solidi, come l'estrusione) e l'anteprima. La conferma
// chiama `apply`; la finestra resta aperta se fallisce.
static bool revolutionDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                             bool fixedSketch, const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<SketchObject> &sketches = viewport->sketches();
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    form->addRow(new FeatureOperationDiagram(FeatureOperationDiagram::Revolution, &dialog));
    auto *sketchBox = new QComboBox(&dialog);
    for (const SketchObject &item : sketches) sketchBox->addItem(item.name);
    sketchBox->setCurrentIndex(qBound(0, initial.sketchIndex, int(sketches.size()) - 1));
    sketchBox->setEnabled(!fixedSketch);
    GeometryRef axisRef = initial.revolveAxis == kRevolveAxisReference ? initial.revolveAxisRef : GeometryRef();
    auto *axisBox = new QComboBox(&dialog);
    const auto fillAxes = [&, axisBox](int sketchIndex) {
        const QSignalBlocker blocker(axisBox);
        axisBox->clear();
        if (sketchIndex < 0 || sketchIndex >= sketches.size()) return;
        const SketchObject &item = sketches.at(sketchIndex);
        int construction = 0, ordinary = 0;
        for (int index = 0; index < item.segments.size(); ++index)
            if (item.isConstructionSegment(index)) axisBox->addItem(QStringLiteral("Linea di costruzione %1").arg(++construction), index);
        for (int index = 0; index < item.segments.size(); ++index)
            if (!item.isConstructionSegment(index)) axisBox->addItem(QStringLiteral("Segmento %1 del profilo").arg(++ordinary), index);
        axisBox->addItem(QStringLiteral("Asse X del piano"), -1);
        axisBox->addItem(QStringLiteral("Asse Y del piano"), -2);
        if (axisRef.kind >= 0)
            axisBox->addItem(QStringLiteral("Nella vista: ") + ForgeCad::geometryRefText(axisRef, sketches, bodies), kRevolveAxisReference);
    };
    fillAxes(sketchBox->currentIndex());
    {
        const int initialAxis = axisBox->findData(initial.revolveAxis);
        if (initialAxis >= 0 && (fixedSketch || initial.revolveAxis == kRevolveAxisReference)) axisBox->setCurrentIndex(initialAxis);
    }
    auto *pickButton = new QPushButton(QStringLiteral("Scegli l'asse nella vista"), &dialog);
    pickButton->setCheckable(true);
    pickButton->setToolTip(QStringLiteral("Clicca una retta: un segmento o una linea di costruzione dello schizzo, un asse del modello,\n"
                                          "uno spigolo rettilineo o una faccia cilindrica (il suo asse). Deve stare nel piano dello schizzo."));
    auto *angleBox = new ForgeCad::ExpressionSpinBox(&dialog);
    angleBox->setDecimals(6);
    angleBox->setRange(0.0, 360.0);
    angleBox->setValue(std::abs(initial.revolveAngle));
    angleBox->setSuffix(QStringLiteral(" °"));
    // Inverti il verso: pulsante a due stati (premuto = angolo negativo).
    auto *reverseBox = new QPushButton(ForgeCad::commandIcon(QStringLiteral("reverseDirection")), QStringLiteral("Inverti il verso"), &dialog);
    reverseBox->setCheckable(true);
    reverseBox->setToolTip(QStringLiteral("Ruota nel verso opposto attorno all'asse"));
    reverseBox->setChecked(initial.revolveAngle < 0.0);
    // Come nei raccordi: l'anteprima si calcola con Invio (o con le frecce), non a ogni carattere.
    angleBox->setKeyboardTracking(false);
    auto *operationBox = new QComboBox(&dialog);
    operationBox->addItems({QStringLiteral("Corpo nuovo"), QStringLiteral("Unisci ai solidi"), QStringLiteral("Sottrai dai solidi")});
    operationBox->setCurrentIndex(qBound(0, initial.mergeOperation, 2));
    auto *autoBox = new QCheckBox(QStringLiteral("Automatico: i solidi che hanno punti in comune con la rivoluzione"), &dialog);
    autoBox->setChecked(initial.mergeAuto);
    auto *bodyList = new QListWidget(&dialog);
    bodyList->setMinimumHeight(110);
    BodyPicker picker(dialog, viewport, replaced, fillMergeBodyList(bodyList, viewport, replaced, initial.mergeBodies), nullptr, bodyList);
    QWidget *bodyRow = picker.row(bodyList);
    auto *pickStatus = new QLabel(&dialog);
    pickStatus->setWordWrap(true);
    pickStatus->setMaximumWidth(360);
    pickStatus->setStyleSheet(QStringLiteral("color: #8aa0b4;"));
    form->addRow(QStringLiteral("Schizzo:"), sketchBox);
    form->addRow(QStringLiteral("Asse:"), axisBox);
    form->addRow(QString(), pickButton);
    form->addRow(QString(), pickStatus);
    form->addRow(QStringLiteral("Angolo:"), angleBox);
    form->addRow(QString(), reverseBox);
    form->addRow(QStringLiteral("Risultato:"), operationBox);
    form->addRow(QString(), autoBox);
    form->addRow(QStringLiteral("Solidi:"), bodyRow);
    const PreviewScope scope(viewport, dialog, form, replaced);
    const auto current = [&] {
        ExtrusionObject d = initial;
        d.operation = -1;
        d.feature = BodyFeature::Revolution;
        d.sketchIndex = sketchBox->currentIndex();
        d.plane = sketches.value(d.sketchIndex).plane;
        d.revolveAxis = axisBox->currentIndex() >= 0 ? axisBox->currentData().toInt() : -1;
        d.revolveAxisRef = d.revolveAxis == kRevolveAxisReference ? axisRef : GeometryRef();
        d.revolveAngle = reverseBox->isChecked() ? -angleBox->value() : angleBox->value();
        d.mergeOperation = operationBox->currentIndex();
        d.mergeAuto = autoBox->isChecked();
        d.mergeBodies.clear();
        for (int row = 0; row < bodyList->count(); ++row)
            if (bodyList->item(row)->checkState() == Qt::Checked) d.mergeBodies.append(bodyList->item(row)->data(Qt::UserRole).toInt());
        return d;
    };
    // L'asse scelto, evidenziato nella vista.
    const auto markAxis = [&] {
        const int axis = axisBox->currentIndex() >= 0 ? axisBox->currentData().toInt() : -1;
        GeometryRef mark;
        if (axis >= 0) {
            mark.kind = 7;
            mark.index = sketchBox->currentIndex();
            mark.element = {0, axis, -1};
        } else if (axis == kRevolveAxisReference) {
            mark = axisRef;
        }
        viewport->setReferenceMarks(mark.kind >= 0 ? QVector<GeometryRef>{mark} : QVector<GeometryRef>());
    };
    std::function<void()> refresh;
    refresh = [&] {
        const bool merge = operationBox->currentIndex() != 0;
        form->setRowVisible(autoBox, merge);
        form->setRowVisible(bodyRow, merge);
        bodyRow->setEnabled(!autoBox->isChecked());
        if (!merge || autoBox->isChecked()) picker.stop();
        markAxis();
        if (axisBox->currentIndex() < 0) { viewport->clearFeatureHandle(); return; }
        if (!pickButton->isChecked() && !picker.active()) {
            viewport->setFeatureHandle(current(), [&](double value) {
                const QSignalBlocker angleBlocker(angleBox), reverseBlocker(reverseBox);
                angleBox->setValue(std::abs(value));
                reverseBox->setChecked(value < 0);
                refresh();
            });
        } else viewport->clearFeatureHandle();
        scope.request(viewport->withMergeCandidates(current(), replaced >= 0 ? replaced : int(bodies.size())));
    };
    picker.changed = refresh;
    picker.message = [&](const QString &text, bool) { pickStatus->setText(text); };
    picker.starting = [&] {
        viewport->clearFeatureHandle();
        const QSignalBlocker blocker(pickButton);
        pickButton->setChecked(false);
        pickStatus->setText(picker.hint());
    };
    const auto setPicking = [&](bool on) {
        const QSignalBlocker blocker(pickButton);
        pickButton->setChecked(on);
        if (on) { picker.stop(); viewport->clearFeatureHandle(); }
        if (!on) {
            viewport->cancelReferencePick();
            pickStatus->setText(QString());
            refresh();
            return;
        }
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleLine, replaced);
        if (!error.isEmpty()) {
            pickButton->setChecked(false);
            pickStatus->setText(error);
            return;
        }
        pickStatus->setText(QStringLiteral("Clicca l'asse nella vista (Esc nella vista: annulla la scelta; lontano dagli oggetti il trascinamento ruota la vista)."));
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picker.handle(picked, ref)) return;
        const QSignalBlocker blocker(pickButton);
        pickButton->setChecked(false);
        pickStatus->setText(QString());
        if (!picked) return;
        // Un segmento dello schizzo stesso: l'asse e' quel segmento (segue lo schizzo).
        if (ref.kind == 7 && ref.index == sketchBox->currentIndex() && ref.element.kind == 0) {
            const int row = axisBox->findData(ref.element.element);
            if (row >= 0) {
                axisBox->setCurrentIndex(row);
                return;
            }
        }
        if (!(ForgeCad::geometryRefRoles(ref, sketches) & ForgeCad::DatumRoleLine)) {
            pickStatus->setText(QStringLiteral("Questo riferimento non e' una retta."));
            return;
        }
        axisRef = ref;
        fillAxes(sketchBox->currentIndex());
        axisBox->setCurrentIndex(axisBox->findData(kRevolveAxisReference));
        refresh();
    });
    QObject::connect(pickButton, &QPushButton::toggled, &dialog, setPicking);
    QObject::connect(sketchBox, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        // Un asse dell'elenco vale per lo schizzo; quello della vista resta.
        fillAxes(index);
        if (axisRef.kind >= 0) axisBox->setCurrentIndex(axisBox->findData(kRevolveAxisReference));
        refresh();
    });
    QObject::connect(axisBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(angleBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    angleBox->onReturn = refresh;
    QObject::connect(reverseBox, &QPushButton::toggled, &dialog, refresh);
    QObject::connect(operationBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(autoBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(bodyList, &QListWidget::itemChanged, &dialog, [&] {
        if (!picker.handling()) refresh();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refresh();
    const auto run = [&] {
        angleBox->interpretText();  // il valore scritto vale anche senza Invio
        if (axisBox->currentIndex() < 0) return QStringLiteral("Scegli l'asse della rivoluzione.");
        const ExtrusionObject d = current();
        if (d.mergeOperation != 0 && !d.mergeAuto && d.mergeBodies.isEmpty()) return QStringLiteral("Spunta almeno un solido (o scegli Automatico).");
        return apply(d);
    };
    // La finestra non e' modale: la vista si puo' ruotare per vedere il verso e l'asse si sceglie cliccandolo.
    bool applied = false;
    if (auto *window = qobject_cast<QMainWindow *>(parent)) applied = runUntilAppliedModeless(window, viewport, dialog, form, buttons, run);
    else applied = runUntilApplied(dialog, form, buttons, run);
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback({});
    viewport->setReferenceMarks({});
    viewport->clearFeatureHandle();
    return applied;
}

static QString primitiveTitle(PrimitiveKind kind) {
    switch (kind) {
    case PrimitiveKind::Box: return QStringLiteral("Parallelepipedo");
    case PrimitiveKind::Cylinder: return QStringLiteral("Cilindro");
    case PrimitiveKind::Sphere: return QStringLiteral("Sfera");
    case PrimitiveKind::Cone: return QStringLiteral("Cono");
    case PrimitiveKind::Torus: return QStringLiteral("Toro");
    }
    return {};
}

// Finestra dei parametri di una primitiva (il tipo non cambia): valori
// iniziali e risultato in `parameters`.
static bool primitiveDialog(QWidget *parent, PrimitiveParameters &parameters,
                            const std::function<QString(const PrimitiveParameters &)> &apply = {},
                            const PreviewSpec<const PrimitiveParameters &> &preview = {}) {
    struct SizeField { QString label; double minimum; };
    QVector<SizeField> fields;
    switch (parameters.kind) {
    case PrimitiveKind::Box:
        fields = {{QStringLiteral("Lunghezza (X):"), 1e-6}, {QStringLiteral("Larghezza (Y):"), 1e-6}, {QStringLiteral("Altezza (Z):"), 1e-6}};
        break;
    case PrimitiveKind::Cylinder: fields = {{QStringLiteral("Raggio:"), 1e-6}, {QStringLiteral("Altezza:"), 1e-6}}; break;
    case PrimitiveKind::Sphere: fields = {{QStringLiteral("Raggio:"), 1e-6}}; break;
    case PrimitiveKind::Cone:
        fields = {{QStringLiteral("Raggio alla base:"), 0.0}, {QStringLiteral("Raggio in cima:"), 0.0}, {QStringLiteral("Altezza:"), 1e-6}};
        break;
    case PrimitiveKind::Torus: fields = {{QStringLiteral("Raggio maggiore:"), 1e-6}, {QStringLiteral("Raggio minore:"), 1e-6}}; break;
    }
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(primitiveTitle(parameters.kind));
    auto *form = dialog.createScrollableForm();
    auto *planeBox = new QComboBox(&dialog);
    planeBox->addItems(planeNames());
    planeBox->setCurrentIndex(qBound(0, parameters.plane, 2));
    form->addRow(QStringLiteral("Piano di base (asse Z = normale):"), planeBox);
    const auto makeSpin = [&dialog](double value, double minimum) {
        auto *spin = new ForgeCad::ExpressionSpinBox(&dialog);
        spin->setDecimals(6);
        spin->setRange(minimum, 100000.0);
        spin->setValue(value);
        return spin;
    };
    QDoubleSpinBox *origin[3];
    const QStringList originLabels = {QStringLiteral("Origine X:"), QStringLiteral("Origine Y:"), QStringLiteral("Origine Z:")};
    for (int axis = 0; axis < 3; ++axis) {
        origin[axis] = makeSpin(parameters.origin[axis], -100000.0);
        form->addRow(originLabels.at(axis), origin[axis]);
    }
    QVector<QDoubleSpinBox *> sizes;
    for (int index = 0; index < fields.size(); ++index) {
        sizes.append(makeSpin(parameters.size[index], fields.at(index).minimum));
        form->addRow(fields.at(index).label, sizes.last());
    }
    const PreviewScope scope(preview.define ? preview.viewport : nullptr, dialog, form, preview.index);
    const auto current = [&, planeBox] {
        PrimitiveParameters values = parameters;
        values.plane = planeBox->currentIndex();
        for (int axis = 0; axis < 3; ++axis) values.origin[axis] = origin[axis]->value();
        for (int index = 0; index < sizes.size(); ++index) values.size[index] = sizes.at(index)->value();
        return values;
    };
    if (preview.define) {
        const auto refresh = [&scope, &preview, current] { scope.request(preview.define(current())); };
        QObject::connect(planeBox, &QComboBox::currentIndexChanged, &dialog, refresh);
        for (QDoubleSpinBox *spin : origin) QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, refresh);
        for (QDoubleSpinBox *spin : sizes) QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, refresh);
        refresh();
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    const auto read = [&] {
        parameters.plane = planeBox->currentIndex();
        for (int axis = 0; axis < 3; ++axis) parameters.origin[axis] = origin[axis]->value();
        for (int index = 0; index < sizes.size(); ++index) parameters.size[index] = sizes.at(index)->value();
    };
    if (apply)
        return runUntilApplied(dialog, form, buttons, [&] {
            read();
            return apply(parameters);
        });
    if (dialog.exec() != QDialog::Accepted) return false;
    read();
    return true;
}

// Pannello fluttuante dentro la finestra principale (la finestra dei vincoli).
// Su Wayland un'applicazione non puo' scegliere la posizione delle sue
// finestre: il compositor (niri) apre ogni finestra fluttuante al centro. Il
// pannello invece e' un figlio della finestra principale, si trascina dalla
// barra del titolo e si ridimensiona dall'angolo in basso a destra;
// posizione e dimensione restano in QSettings (`settingsKey`) e si
// riprendono alla riapertura, anche dopo il riavvio. Resta dentro la finestra
// quando questa cambia dimensione.
class FloatingPanel final : public FunctionDialogPanel {
public:
    FloatingPanel(QWidget *window, const QString &title, const QString &settingsKey, const QSize &defaultSize)
        : FunctionDialogPanel(window, false), settingsKey_(settingsKey), defaultSize_(defaultSize) {
        setHeaderVisible(false);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(1, 1, 1, 1);
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
    QWidget *content() const { return content_; }

protected:
    void showEvent(QShowEvent *event) override {
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
    void resizeEvent(QResizeEvent *event) override {
        FunctionDialogPanel::resizeEvent(event);
        if (placed_ && isVisible()) store();
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
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

private:
    void keepInside() {
        const QRect area = parentWidget()->rect();
        QSize size = this->size().boundedTo(area.size());
        size = size.expandedTo(minimumSizeHint().boundedTo(area.size()));
        if (size != this->size()) resize(size);
        move(qBound(0, x(), qMax(0, area.width() - width())), qBound(0, y(), qMax(0, area.height() - header_->height())));
    }
    void store() { QSettings().setValue(settingsKey_, geometry()); }

    QString settingsKey_;
    QSize defaultSize_;
    QWidget *header_ = nullptr, *content_ = nullptr;
    bool draggingHeader_ = false, placed_ = false;
};

// In una lista: il tasto Canc va alla lista (elimina gli elementi scelti) e
// non alla scorciatoia Canc della finestra principale.
class DeleteKeyFilter final : public QObject {
public:
    DeleteKeyFilter(QObject *parent, std::function<void()> action) : QObject(parent), action_(std::move(action)) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Delete) {
            event->accept();
            if (event->type() == QEvent::KeyPress) action_();
            return event->type() == QEvent::KeyPress;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> action_;
};

// Finestre di dialogo spostabili trascinandone lo sfondo (o un'etichetta):
// su Wayland i compositor che non disegnano le barre del titolo (niri, e
// altri con "prefer-no-csd") le lasciano senza un punto da cui trascinarle, e
// un'applicazione non puo' spostare da se' una sua finestra; puo' pero'
// chiedere al compositor di iniziare lo spostamento (startSystemMove).
class DialogMover final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
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

private:
    QPointer<FunctionDialogPanel> embeddedPanel_;
};

static AxesOrientation defaultAxesOrientation();
static void saveDefaultAxesOrientation(const AxesOrientation &o);
static LengthUnit defaultLengthUnit() {
    const int value = QSettings().value(QStringLiteral("units/defaultLength"), int(LengthUnit::Millimeter)).toInt();
    return LengthUnit(qBound(int(LengthUnit::Millimeter), value, int(LengthUnit::Foot)));
}

PdfWindow::PdfWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QGuiApplication::applicationDisplayName());
    qApp->installEventFilter(new DialogMover(this));
    resize(1280, 820);
    setMinimumSize(900, 600);
    viewport_ = new CadViewport(this);
    auto *viewport = viewport_;
    viewport->setOrientation(defaultAxesOrientation());  // assi del documento nuovo (Opzioni, di default Z in alto)
    viewport->setLengthUnit(defaultLengthUnit());
    setCentralWidget(viewport);
    recovery_ = new ForgeCad::DocumentRecovery([this](DocumentState &state,QString &path) {
        if (foregroundProgressDepth_ || loadingDocument_ || !viewport_->recoverySnapshotReady()) return false;
        state=viewport_->recoveryDocument(); path=documentPath_; return true;
    },this);

    auto *modelDock = new QDockWidget(QStringLiteral("Albero modello"), this);
    modelDock->setObjectName(QStringLiteral("modelDock"));  // per saveState/restoreState
    auto *modelTree = new StoryboardTree(modelDock);
    modelTree_ = modelTree;
    modelTree->setItemDelegate(new TreeBadgeDelegate(modelTree));
    modelTree->setMoveFeatureCallback([viewport](int feature, int target) { return viewport->moveFeatureTo(feature, target); });
    modelTree->setHeaderLabel(QStringLiteral("Oggetti scena"));
    modelTree->setMinimumWidth(220);
    modelTree->setContextMenuPolicy(Qt::CustomContextMenu);
    modelTree->setHistoryPositionCallback([viewport](int position) { viewport->setHistoryPosition(position); });
    modelTree->setCursorMode(static_cast<StoryboardTree::CursorMode>(qBound(0,
        QSettings().value(QStringLiteral("view/storyboardCursorMode"), 0).toInt(), 2)));
    modelDock->setWidget(modelTree);
    addDockWidget(Qt::LeftDockWidgetArea, modelDock);
    rebuildModelTree();
    auto createSketchOnPlane = std::make_shared<std::function<void(int)>>();
    // Modifica dei parametri di un corpo: la finestra della sua funzione con
    // i valori attuali, poi CadViewport::updateBody (un passo di Undo).
    // Modifica di un raccordo o smusso: misura (con l'anteprima), tipo e spigoli.
    // "Spigoli..." torna alla scelta sulla base con gli spigoli attuali; Invio
    // riapre questa finestra con quelli nuovi.
    auto editBlend = std::make_shared<std::function<void(int, QVector<EdgePoint>, double, bool)>>(
        [this, viewport](int index, QVector<EdgePoint> edges, double size, bool chamfer) {
            if (index < 0 || index >= viewport->extrusions().size()) return;
            const ExtrusionObject original = viewport->extrusions().at(index);
            const QString title = QStringLiteral("Modifica ") + (original.blendChamfer ? QStringLiteral("smusso") : QStringLiteral("raccordo"));
            const BlendDialogResult result = blendDialog(this, viewport, title, original.firstBody, index, edges, size, chamfer, true, original.chamferSpec,
                                                         [&](int, const QVector<EdgePoint> &selectedEdges, double newSize, bool newChamfer, const ChamferSpec &spec) {
                ExtrusionObject body = original;
                const QString oldPrefix = body.blendChamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo");
                const QString newPrefix = newChamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo");
                if (body.name.startsWith(oldPrefix)) body.name.replace(0, oldPrefix.size(), newPrefix);
                body.blendSize = newSize;
                body.blendChamfer = newChamfer;
                body.chamferSpec = spec;
                // Spigoli scelti di nuovo: sono stati presi sulla base attuale.
                const auto sameReferences = [](const QVector<EdgePoint> &a, const QVector<EdgePoint> &b) {
                    if (a.size() != b.size()) return false;
                    for (int k = 0; k < a.size(); ++k)
                        if (a.at(k).x != b.at(k).x || a.at(k).y != b.at(k).y || a.at(k).z != b.at(k).z || a.at(k).subshape != b.at(k).subshape
                            || a.at(k).role != b.at(k).role)
                            return false;
                    return true;
                };
                if (!sameReferences(selectedEdges, original.blendEdges) && original.firstBody >= 0 && original.firstBody < viewport->extrusions().size())
                    body.blendBaseFeature = viewport->extrusions().at(original.firstBody).featureId;
                body.blendEdges = selectedEdges;
                const QString error = viewport->updateBody(index, body);
                return error.isEmpty() ? error : error + QStringLiteral("\nIl corpo resta com'era.");
            });
            if (!result.reselect) return;
            pickSizeBox_->setValue(result.size);
            const QString error = viewport->beginBlendEdit(index, result.size, result.chamfer);
            if (!error.isEmpty()) QMessageBox::information(this, title, error);
        });
    viewport->setEdgeEditCallback([editBlend](int index, QVector<EdgePoint> edges, double size, bool chamfer) {
        (*editBlend)(index, edges, size, chamfer);
    });
    auto editBody = std::make_shared<std::function<void(int)>>([this, viewport, editBlend](int index) {
        if (viewport->interactionLocked()||viewport->documentReadOnly()) return;
        const QVector<ExtrusionObject> &bodies = viewport->extrusions();
        if (index < 0 || index >= bodies.size()) return;
        // Ogni finestra si chiude solo se il corpo si rigenera con i valori nuovi
        // (altrimenti mostra l'errore e resta aperta; il corpo resta com'era).
        const ExtrusionObject original = bodies.at(index);
        const auto update = [viewport, index](const ExtrusionObject &changed) {
            const QString error = viewport->updateBody(index, changed);
            return error.isEmpty() ? error : error + QStringLiteral("\nIl corpo resta com'era.");
        };
        if (original.operation >= 0) {
            booleanDialog(this, viewport, QStringLiteral("Modifica booleana"), index, original, [&](const ExtrusionObject &values) {
                static const QStringList names = {QStringLiteral("Unione"), QStringLiteral("Intersezione"), QStringLiteral("Differenza")};
                ExtrusionObject body = values;
                // Il nome segue l'operazione se era quello automatico.
                if (body.name.startsWith(names.value(original.operation)))
                    body.name.replace(0, names.value(original.operation).size(), names.value(values.operation));
                return update(body);
            });
        } else if (original.feature == BodyFeature::Extrusion) {
            extrusionDialog(this, viewport, QStringLiteral("Modifica estrusione"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Revolution) {
            revolutionDialog(this, viewport, QStringLiteral("Modifica rivoluzione"), index, original, true,
                             [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Primitive) {
            PrimitiveParameters parameters = original.primitive;
            const auto define = [&](const PrimitiveParameters &values) {
                ExtrusionObject body = original;
                body.primitive = values;
                body.plane = values.plane;
                return body;
            };
            primitiveDialog(this, parameters, [&](const PrimitiveParameters &values) { return update(define(values)); },
                            {viewport, index, define});
        } else if (original.feature == BodyFeature::Blend) {
            (*editBlend)(index, original.blendEdges, original.blendSize, original.blendChamfer);
        } else if (original.feature == BodyFeature::Helix) {
            helixDialog(this, viewport, original.helix.spiral ? QStringLiteral("Modifica spirale") : QStringLiteral("Modifica elica"), index, original, false,
                        [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Sweep) {
            sweepDialog(this, viewport, QStringLiteral("Modifica sweep"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Loft) {
            loftDialog(this, viewport, QStringLiteral("Modifica loft"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Scale) {
            scaleDialog(this, viewport, QStringLiteral("Modifica scala"), index, original, [&](const ExtrusionObject &values) {
                ExtrusionObject body = original;
                body.scaleFactor = values.scaleFactor;
                body.scaleCenterMode = values.scaleCenterMode;
                body.scaleCenter = values.scaleCenter;
                return update(body);
            });
        } else if (original.feature == BodyFeature::SheetTrim) {
            trimDialog(this, viewport, QStringLiteral("Modifica taglio"), index, original, [&](const ExtrusionObject &values) {
                ExtrusionObject body = original;
                body.firstBody = values.firstBody;
                body.secondBody = values.secondBody;
                body.sketchIndex = values.sketchIndex;
                body.trimPlane = values.trimPlane;
                body.trimKeep = values.trimKeep;
                body.trimBoth = values.trimBoth;
                body.trimProject = values.trimProject;
                body.projectionMode = values.projectionMode;
                body.projectionReverse = values.projectionReverse;
                body.trimToolKeep = values.trimToolKeep;
                return update(body);
            });
        } else if (original.feature == BodyFeature::Transform) {
            transformDialog(this, viewport, QStringLiteral("Modifica spostamento"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Pattern) {
            patternDialog(this, viewport, QStringLiteral("Modifica ripetizione"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::DatumPlane) {
            datumDialog(this, viewport, QStringLiteral("Modifica piano di costruzione"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Imported) {
            QMessageBox::information(this, original.name, QStringLiteral("Corpo importato da %1: non ha parametri da modificare.\n"
                                                                         "Si puo' usare nelle altre funzioni (booleane, raccordi, schizzi sulle facce...).")
                                                              .arg(original.importSource));
        } else if (original.feature == BodyFeature::SurfaceOffset) {
            offsetDialog(this, viewport, original.distance == 0.0 ? QStringLiteral("Modifica copia facce") : QStringLiteral("Modifica offset"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::BoundarySurface) {
            boundaryDialog(this, viewport, QStringLiteral("Modifica superficie tra curve"), index, original,
                           [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::FillSurface) {
            fillDialog(this, viewport, QStringLiteral("Modifica riempimento"), index, original,
                       [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Draft) {
            draftDialog(this, viewport, QStringLiteral("Modifica sformo"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Thicken) {
            thickenDialog(this, viewport, QStringLiteral("Modifica spessore"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::SpatialSketch) {
            spatialSketchDialog(this,viewport,QStringLiteral("Modifica schizzo 3D"),index,original,
                [viewport,index](const ExtrusionObject &d){return viewport->updateBody(index,d);});
        } else if (original.feature == BodyFeature::ExtractedCurve) {
            extractedCurveDialog(this, viewport, QStringLiteral("Modifica curva estratta 3D"), index, original,
                                 [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::ProjectedCurve) {
            projectionDialog(this, viewport, QStringLiteral("Modifica curva proiettata"), index, original,
                             [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Shell) {
            offsetDialog(this, viewport, QStringLiteral("Modifica svuota"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Thread) {
            threadDialog(this, viewport, QStringLiteral("Modifica filettatura"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::DeleteFace) {
            offsetDialog(this, viewport, QStringLiteral("Modifica eliminazione facce"), index, original,
                         [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Ruled) {
            ruledDialog(this, viewport, QStringLiteral("Modifica superficie rigata"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::PlanarSurface) {
            planarDialog(this, viewport, QStringLiteral("Modifica superficie planare"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Sew) {
            sewDialog(this, viewport, QStringLiteral("Modifica cucitura"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::SheetExtend) {
            extendDialog(this, viewport, QStringLiteral("Modifica estensione"), original.firstBody, index, original.blendEdges, original.blendSize,
                         original.extendLinear, false, [&](const QVector<EdgePoint> &selected, double distance, bool linear) {
                             ExtrusionObject body = original;
                             body.blendEdges = selected;
                             body.blendSize = distance;
                             body.extendLinear = linear;
                             return update(body);
                         });
        }
    });
    viewport->setEditBodyCallback([editBody](int index) { (*editBody)(index); });
    connect(modelTree, &QTreeWidget::itemDoubleClicked, this, [this, viewport, editBody](QTreeWidgetItem *item, int) {
        if (suppressTreeClick_) return;
        const int type = item->data(0, Qt::UserRole).toInt();
        if (type == kTreeExtrusion) (*editBody)(item->data(0, Qt::UserRole + 1).toInt());
        else if (type == kTreeSketch) viewport->selectSketch(item->data(0, Qt::UserRole + 1).toInt());  // doppio clic: apre lo schizzo
    });
    viewport->setSelectionCallback([this](SceneSelection selection) {
        for (QTreeWidgetItemIterator it(modelTree_); *it; ++it) {
            QTreeWidgetItem *item = *it;
            const int type = item->data(0, Qt::UserRole).toInt();
            const int itemIndex = item->data(0, Qt::UserRole + 1).toInt();
            const bool matches = itemIndex == selection.index
                && ((type == kTreePlane && selection.kind == SceneObjectKind::Plane)
                    || (type == kTreeSketch && selection.kind == SceneObjectKind::Sketch)
                    || (type == kTreeExtrusion && selection.kind == SceneObjectKind::Extrusion));
            if (matches) {
                modelTree_->setCurrentItem(item);
                return;
            }
        }
        modelTree_->clearSelection();
    });
    // Selezione multipla nella vista (riquadro, Ctrl/Maiusc+clic): le stesse voci nell'albero.
    viewport->setMultiSelectionCallback([this](const QVector<SceneSelection> &objects) {
        modelTree_->clearSelection();
        for (QTreeWidgetItemIterator it(modelTree_); *it; ++it) {
            QTreeWidgetItem *item = *it;
            const int type = item->data(0, Qt::UserRole).toInt();
            const int itemIndex = item->data(0, Qt::UserRole + 1).toInt();
            const SceneObjectKind kind = type == kTreeSketch ? SceneObjectKind::Sketch
                                       : type == kTreeExtrusion ? SceneObjectKind::Extrusion : SceneObjectKind::None;
            if (kind != SceneObjectKind::None && objects.contains(SceneSelection{kind, itemIndex, -1})) item->setSelected(true);
        }
    });
    viewport->setDocumentChangedCallback([this] {
        if (!loadingDocument_ && !viewport_->documentReadOnly()) recovery_->changed();
        if (!loadingDocument_ && !viewport_->documentReadOnly() && !documentModified_) {
            documentModified_ = true;
            updateWindowTitle();
        }
        scheduleModelTreeRebuild();
        updateUndoActions();
        if (historyGraphDialog_)
            static_cast<ForgeCad::HistoryGraphDialog *>(historyGraphDialog_)->setDocument(viewport_->currentDocument());
    });
    viewport->setPlaneContextCallback([this, viewport, createSketchOnPlane](int plane) {
        CadMenu menu(this);
        QAction *newSketch = menu.addAction(QStringLiteral("Nuovo schizzo su questo piano"));
        QAction *normalView = menu.addAction(QStringLiteral("Vista normale al piano"));
        const QAction *chosen = menu.exec(QCursor::pos());
        if (chosen == newSketch && *createSketchOnPlane) (*createSketchOnPlane)(plane);
        else if (chosen == normalView) viewport->setViewNormal(plane);
    });
    connect(modelTree, &QTreeWidget::itemChanged, this, [this, viewport](QTreeWidgetItem *item, int) {
        if (rebuildingTree_) return;
        if(viewport->documentReadOnly()){scheduleModelTreeRebuild();return;}
        // Il clic sulla casella emette anche itemClicked: non deve aprire lo schizzo.
        suppressTreeClick_ = true;
        QTimer::singleShot(0, this, [this] { suppressTreeClick_ = false; });
        const int type = item->data(0, Qt::UserRole).toInt();
        const int index = item->data(0, Qt::UserRole + 1).toInt();
        const bool visible = item->checkState(0) == Qt::Checked;
        if (type == kTreePlane) viewport->setPlaneVisible(index, visible);
        else if (type == kTreeSketch) viewport->setObjectVisible(SceneObjectKind::Sketch, index, visible);
        else if (type == kTreeExtrusion) viewport->setObjectVisible(SceneObjectKind::Extrusion, index, visible);
        else if (type == kTreeBody) viewport->setModelBodyVisible(index, visible);
    });
    connect(modelTree, &QTreeWidget::itemClicked, this, [this, viewport, modelTree](QTreeWidgetItem *item, int) {
        if (suppressTreeClick_) return;
        const int type = item->data(0, Qt::UserRole).toInt();
        const int index = item->data(0, Qt::UserRole + 1).toInt();
        // Piu' voci scelte (Ctrl/Maiusc+clic): schizzi e feature insieme nella vista.
        const QList<QTreeWidgetItem *> chosen = modelTree->selectedItems();
        if (chosen.size() > 1 || (QGuiApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            QVector<SceneSelection> objects;
            for (QTreeWidgetItem *selected : chosen) {
                const int selectedType = selected->data(0, Qt::UserRole).toInt();
                const int selectedIndex = selected->data(0, Qt::UserRole + 1).toInt();
                if (selectedType == kTreeSketch) objects.append({SceneObjectKind::Sketch, selectedIndex, -1});
                else if (selectedType == kTreeExtrusion) objects.append({SceneObjectKind::Extrusion, selectedIndex, -1});
            }
            viewport->setSelectedObjects(objects);
            return;
        }
        if (type == kTreePlane) viewport->selectPlane(index);
        // Un clic seleziona lo schizzo; si apre con il doppio clic.
        if (type == kTreeSketch) viewport->setSelectedObjects({{SceneObjectKind::Sketch, index, -1}});
        if (type == kTreeExtrusion) viewport->selectObject(SceneObjectKind::Extrusion, index);
        if (type == kTreeBody) {
            const int tip = viewport->modelBodyTip(index);
            if (tip >= 0) viewport->selectObject(SceneObjectKind::Extrusion, tip, true);  // tutto il corpo
        }
    });
    // Rinomina (F2 o menu contestuale): schizzi e corpi.
    auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), modelTree);
    renameShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(renameShortcut, &QShortcut::activated, this, [this, modelTree] {
        if (QTreeWidgetItem *item = modelTree->currentItem()) renameTreeItem(item);
    });
    connect(modelTree, &QTreeWidget::customContextMenuRequested, this,
            [this, modelTree, viewport, createSketchOnPlane, editBody](const QPoint &position) {
        if(viewport->documentReadOnly())return;
        auto *item = modelTree->itemAt(position);
        if (!item) return;
        const int type = item->data(0, Qt::UserRole).toInt();
        const int index = item->data(0, Qt::UserRole + 1).toInt();
        CadMenu menu(this);
        const QPoint globalPosition = modelTree->viewport()->mapToGlobal(position);
        // Piu' voci scelte: le operazioni comuni a tutte.
        const QList<QTreeWidgetItem *> chosenItems = modelTree->selectedItems();
        if (chosenItems.size() > 1 && item->isSelected()) {
            QVector<SceneSelection> objects;
            QVector<int> features;
            bool anySuppressed = false, anyActive = false, anyVisible = false;
            for (QTreeWidgetItem *selected : chosenItems) {
                const int selectedType = selected->data(0, Qt::UserRole).toInt();
                const int selectedIndex = selected->data(0, Qt::UserRole + 1).toInt();
                if (selectedType == kTreeSketch && selectedIndex >= 0 && selectedIndex < viewport->sketches().size()) {
                    objects.append({SceneObjectKind::Sketch, selectedIndex, -1});
                    anyVisible = anyVisible || viewport->isObjectVisible(SceneObjectKind::Sketch, selectedIndex);
                } else if (selectedType == kTreeExtrusion && selectedIndex >= 0 && selectedIndex < viewport->extrusions().size()) {
                    objects.append({SceneObjectKind::Extrusion, selectedIndex, -1});
                    const ExtrusionObject &feature = viewport->extrusions().at(selectedIndex);
                    if (feature.modelBodyId != 0) {
                        features.append(selectedIndex);
                        anySuppressed = anySuppressed || feature.suppressed;
                        anyActive = anyActive || !feature.suppressed;
                    } else {
                        anyVisible = anyVisible || feature.visible;
                    }
                }
            }
            if (objects.isEmpty()) return;
            viewport->setSelectedObjects(objects);
            QAction *suppress = anyActive ? menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")),
                                                           QStringLiteral("Sopprimi %1 feature").arg(features.size())) : nullptr;
            QAction *activate = anySuppressed ? menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")),
                                                               QStringLiteral("Riattiva %1 feature").arg(features.size())) : nullptr;
            // La visibilita' vale per schizzi e corpi fuori dalla storia (le feature seguono il loro corpo).
            QVector<SceneSelection> visibilityTargets;
            for (const SceneSelection &object : objects)
                if (object.kind == SceneObjectKind::Sketch || !features.contains(object.index)) visibilityTargets.append(object);
            QAction *hide = !visibilityTargets.isEmpty() && anyVisible
                ? menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")), QStringLiteral("Nascondi")) : nullptr;
            QAction *show = !visibilityTargets.isEmpty() && !anyVisible
                ? menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")), QStringLiteral("Mostra")) : nullptr;
            menu.addSeparator();
            QAction *remove = menu.addAction(ForgeCad::commandIcon(QStringLiteral("delete")),
                                             QStringLiteral("Elimina %1 elementi").arg(objects.size()));
            const QAction *chosen = menu.exec(globalPosition);
            if (!chosen) return;
            if (chosen == suppress || chosen == activate) {
                const QString error = viewport->setFeaturesSuppressed(features, chosen == suppress);
                if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Storyboard"), error);
            } else if (chosen == hide || chosen == show) {
                viewport->setObjectsVisible(visibilityTargets, chosen == show);
            } else if (chosen == remove) {
                viewport->deleteObjectsOrFeatures(objects);
            }
            return;
        }
        if (type == kTreePlane) {
            QAction *newSketch = menu.addAction(QStringLiteral("Nuovo schizzo su questo piano"));
            QAction *normalView = menu.addAction(QStringLiteral("Vista normale al piano"));
            const QAction *chosen = menu.exec(globalPosition);
            if (chosen == newSketch && *createSketchOnPlane) (*createSketchOnPlane)(index);
            if (chosen == normalView) { item->setSelected(true); viewport->setViewNormal(index); }
        } else if (type == kTreeBody) {
            const bool visible = index >= 0 && index < viewport->modelBodies().size() && viewport->modelBodies().at(index).visible;
            QAction *toggle = menu.addAction(visible ? QStringLiteral("Nascondi corpo") : QStringLiteral("Mostra corpo"));
            QAction *rename = menu.addAction(QStringLiteral("Rinomina corpo..."));
            QAction *meshColor = menu.addAction(QStringLiteral("Colore mesh..."));
            toggle->setIcon(ForgeCad::commandIcon(QStringLiteral("visibility")));
            rename->setIcon(ForgeCad::commandIcon(QStringLiteral("rename")));
            meshColor->setIcon(ForgeCad::commandIcon(QStringLiteral("meshColor")));
            rename->setShortcut(QKeySequence(Qt::Key_F2));
            menu.addSeparator();
            QAction *remove = menu.addAction(QStringLiteral("Elimina corpo e storyboard"));
            remove->setIcon(ForgeCad::commandIcon(QStringLiteral("delete")));
            const QAction *chosen = menu.exec(globalPosition);
            if (chosen == rename) renameTreeItem(item);
            else if (chosen == toggle) viewport->setModelBodyVisible(index, !visible);
            else if (chosen == meshColor) {
                const QColor color = QColorDialog::getColor(viewport->modelBodyMeshColor(index), this,
                                                            QStringLiteral("Colore mesh del corpo"));
                if (color.isValid()) viewport->setModelBodyMeshColor(index, color);
            }
            else if (chosen == remove) viewport->deleteModelBody(index);
        } else if (type == kTreeSketch || type == kTreeExtrusion) {
            const SceneObjectKind kind = type == kTreeSketch ? SceneObjectKind::Sketch : SceneObjectKind::Extrusion;
            const bool visible = viewport->isObjectVisible(kind, index);
            QAction *editSketch = type == kTreeSketch ? menu.addAction(QStringLiteral("Modifica schizzo")) : nullptr;
            QAction *editParameters = type == kTreeExtrusion ? menu.addAction(QStringLiteral("Modifica parametri...")) : nullptr;
            if (editSketch) editSketch->setIcon(ForgeCad::commandIcon(QStringLiteral("edit")));
            if (editParameters && index >= 0 && index < viewport->extrusions().size())
                editParameters->setIcon(ForgeCad::commandIcon(featureIconName(viewport->extrusions().at(index))));
            const bool storyboardFeature = type == kTreeExtrusion && index >= 0 && index < viewport->extrusions().size()
                                        && viewport->extrusions().at(index).modelBodyId != 0;
            QAction *suppress = storyboardFeature
                ? menu.addAction(viewport->extrusions().at(index).suppressed ? QStringLiteral("Riattiva feature")
                                                                            : QStringLiteral("Sopprimi feature"))
                : nullptr;
            if (suppress) suppress->setIcon(ForgeCad::commandIcon(QStringLiteral("visibility")));
            QAction *moveEarlier = storyboardFeature ? menu.addAction(QStringLiteral("Sposta prima nella storia")) : nullptr;
            QAction *moveLater = storyboardFeature ? menu.addAction(QStringLiteral("Sposta dopo nella storia")) : nullptr;
            if (moveEarlier) moveEarlier->setIcon(ForgeCad::commandIcon(QStringLiteral("move")));
            if (moveLater) moveLater->setIcon(ForgeCad::commandIcon(QStringLiteral("move")));
            const bool datum = type == kTreeExtrusion && index >= 0 && index < viewport->extrusions().size()
                            && viewport->extrusions().at(index).feature == BodyFeature::DatumPlane;
            QAction *datumSketch = datum ? menu.addAction(QStringLiteral("Nuovo schizzo sul piano")) : nullptr;
            if (datumSketch) datumSketch->setIcon(ForgeCad::commandIcon(QStringLiteral("newSketch")));
            if (datumSketch) datumSketch->setEnabled(viewport->extrusions().at(index).datumValid);
            QAction *toggle = storyboardFeature ? nullptr : menu.addAction(visible ? QStringLiteral("Nascondi") : QStringLiteral("Mostra"));
            QAction *rename = menu.addAction(QStringLiteral("Rinomina..."));
            if (toggle) toggle->setIcon(ForgeCad::commandIcon(QStringLiteral("visibility")));
            rename->setIcon(ForgeCad::commandIcon(QStringLiteral("rename")));
            rename->setShortcut(QKeySequence(Qt::Key_F2));
            menu.addSeparator();
            QAction *remove = menu.addAction(QStringLiteral("Elimina"));
            remove->setIcon(ForgeCad::commandIcon(QStringLiteral("delete")));
            const QAction *chosen = menu.exec(globalPosition);
            if (chosen && chosen == rename) {
                renameTreeItem(item);
                return;
            }
            if (chosen && chosen == editSketch) viewport->selectSketch(index);
            else if (chosen && chosen == editParameters) (*editBody)(index);
            else if (chosen && chosen == suppress) {
                const bool value = !viewport->extrusions().at(index).suppressed;
                const QString error = viewport->setFeatureSuppressed(index, value);
                if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Storyboard"), error);
            }
            else if (chosen && (chosen == moveEarlier || chosen == moveLater)) {
                const QString error = viewport->moveFeature(index, chosen == moveEarlier ? -1 : 1);
                if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Riordina storyboard"), error);
            }
            else if (chosen && chosen == datumSketch) viewport->createDatumSketch(index, QStringLiteral("Schizzo %1").arg(viewport->sketches().size() + 1));
            else if (chosen && chosen == remove) viewport->deleteObject(kind, index);
            else if (toggle && chosen == toggle) viewport->setObjectVisible(kind, index, !visible);
        }
    });

    auto *fileMenu = addCadMenu(menuBar(), QStringLiteral("File"));
    QAction *newAction = fileMenu->addAction(QStringLiteral("Nuovo"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &PdfWindow::newDocument);
    QAction *openAction = fileMenu->addAction(QStringLiteral("Apri..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &PdfWindow::openDocument);
    QAction *saveAction = fileMenu->addAction(QStringLiteral("Salva"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this] { saveDocument(false); });
    QAction *saveAsAction = fileMenu->addAction(QStringLiteral("Salva con nome..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    saveAsAction->setToolTip(QStringLiteral("Salva una copia indipendente del documento."));
    connect(saveAsAction, &QAction::triggered, this, [this] { saveDocument(true); });
    auto *recoverAction=fileMenu->addAction(QStringLiteral("Recupera documenti..."));
    recoverAction->setObjectName(QStringLiteral("documentRecoveryAction"));
    recoverAction->setIcon(ForgeCad::commandIcon(QStringLiteral("open")));
    recoverAction->setToolTip(QStringLiteral("Apri come copia un backup automatico rimasto dopo una chiusura imprevista"));
    connect(recoverAction,&QAction::triggered,this,[this]{showRecoveryDocuments();});
    QMenu *drawingMenu = addCadMenu(fileMenu, QStringLiteral("Tavole 2D"));
    auto *newDrawingAction = drawingMenu->addAction(QStringLiteral("Nuova tavola dal modello..."));
    newDrawingAction->setObjectName(QStringLiteral("newDrawingAction"));
    newDrawingAction->setIconVisibleInMenu(true);newDrawingAction->setToolTip(QStringLiteral("Crea una tavola 2D associata al modello salvato."));
    connect(newDrawingAction,&QAction::triggered,this,&PdfWindow::newDrawing);
    auto *openDrawingAction = drawingMenu->addAction(QStringLiteral("Apri tavola..."));
    openDrawingAction->setObjectName(QStringLiteral("openDrawingAction"));
    openDrawingAction->setIconVisibleInMenu(true);openDrawingAction->setToolTip(QStringLiteral("Apre le tavole collegate al modello: scelta con anteprima se sono più di una; altrimenti consente di sfogliare i file .drw."));
    connect(openDrawingAction,&QAction::triggered,this,&PdfWindow::openDrawing);
    QMenu *revisionMenu = addCadMenu(fileMenu, QStringLiteral("Revisioni"));
    revisionMenu->setObjectName(QStringLiteral("documentRevisionMenu"));
    const auto reportRevisionError = [this](const QString &error) {
        if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Revisioni"), error);
    };
    QAction *checkoutAction = revisionMenu->addAction(QStringLiteral("Esegui check-out..."));
    checkoutAction->setObjectName(QStringLiteral("revisionCheckoutAction"));
    connect(checkoutAction, &QAction::triggered, this, [this, reportRevisionError] {
        bool accepted = false;
        const auto fallback = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
        const QString owner = QInputDialog::getText(this, QStringLiteral("Check-out"), QStringLiteral("Proprietario della copia di lavoro:"),
            QLineEdit::Normal, QSettings().value(QStringLiteral("revisions/owner"),fallback).toString(), &accepted).trimmed();
        if (!accepted || owner.isEmpty()) return;
        if ((documentPath_.isEmpty()||(documentModified_&&!documentEditAccess_.readOnly))&&!saveDocument(false)) return;
        const auto error = runDocumentRevisionAction(DocumentRevisionAction::Checkout,owner);
        if (error.isEmpty()) QSettings().setValue(QStringLiteral("revisions/owner"),owner);
        reportRevisionError(error);
    });
    QAction *checkinAction = revisionMenu->addAction(QStringLiteral("Esegui check-in..."));
    checkinAction->setObjectName(QStringLiteral("revisionCheckinAction"));
    connect(checkinAction, &QAction::triggered, this, [this, reportRevisionError] {
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Check-in"));
        dialog.setObjectName(QStringLiteral("revisionCheckinDialog"));
        auto *layout = new QFormLayout(&dialog);
        QString proposed;
        const auto proposalError=ForgeCad::nextWorkspaceRevisionLabel(documentPath_,viewport_->currentDocument().version.documentId,proposed);
        if(!proposalError.isEmpty()){reportRevisionError(proposalError);return;}
        auto *revision = new QLineEdit(proposed,&dialog);revision->setReadOnly(true);
        revision->setObjectName(QStringLiteral("revisionLabel")); revision->setMaxLength(128);
        auto *comment = new QTextEdit(&dialog); comment->setAcceptRichText(false); comment->setObjectName(QStringLiteral("revisionComment"));
        layout->addRow(QStringLiteral("Revisione tecnica:"),revision); layout->addRow(QStringLiteral("Commento:"),comment);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Esegui check-in"));
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(revision,&QLineEdit::textChanged,&dialog,[buttons](const QString &value) {
            buttons->button(QDialogButtonBox::Ok)->setEnabled(!value.trimmed().isEmpty());
        });
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!revision->text().trimmed().isEmpty());
        layout->addRow(buttons); dialog.resize(440,250);
        if (dialog.exec()!=QDialog::Accepted || !saveDocument(false)) return;
        const auto error = runDocumentRevisionAction(DocumentRevisionAction::Checkin,{},comment->toPlainText());
        reportRevisionError(error);
    });
    QAction *completeAction = revisionMenu->addAction(QStringLiteral("Completa check-in pendente"));
    completeAction->setObjectName(QStringLiteral("revisionCompleteAction"));
    connect(completeAction,&QAction::triggered,this,[this,reportRevisionError] {
        reportRevisionError(runDocumentRevisionAction(DocumentRevisionAction::CompletePending));
    });
    QAction *cancelCheckoutAction = revisionMenu->addAction(QStringLiteral("Annulla check-out..."));
    cancelCheckoutAction->setObjectName(QStringLiteral("revisionCancelAction"));
    connect(cancelCheckoutAction,&QAction::triggered,this,[this,reportRevisionError] {
        if (QMessageBox::question(this,QStringLiteral("Annulla check-out"),
            QStringLiteral("Liberare la prenotazione del documento? Le modifiche locali vengono mantenute."),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        reportRevisionError(runDocumentRevisionAction(DocumentRevisionAction::CancelCheckout));
    });
    revisionMenu->addSeparator();
    QAction *historyAction = revisionMenu->addAction(QStringLiteral("Storico dei check-in..."));
    historyAction->setObjectName(QStringLiteral("revisionHistoryAction"));
    connect(historyAction,&QAction::triggered,this,[this] { showDocumentRevisions(); });
    recentDocumentsMenu_ = addCadMenu(fileMenu, QStringLiteral("File recenti"));
    recentDocumentsMenu_->setObjectName(QStringLiteral("recentDocumentsMenu"));
    connect(recentDocumentsMenu_, &QMenu::aboutToShow, this, &PdfWindow::refreshRecentDocuments);
    refreshRecentDocuments();
    fileMenu->addSeparator();
    // Importazione da altri CAD (cad_import): STEP e IGES, B-rep esatti in mm, un corpo per solido.
    QAction *importAction = fileMenu->addAction(QStringLiteral("Importa STEP/IGES..."));
    importAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    importAction->setToolTip(QStringLiteral("Solidi e superfici da un file STEP o IGES (geometria esatta, un corpo per solido)"));
    connect(importAction, &QAction::triggered, this, [this, viewport] {
        QSettings settings;
        const QString directory = settings.value(QStringLiteral("import/lastDirectory"), QFileInfo(documentPath_).absolutePath()).toString();
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Importa STEP/IGES"), directory,
                                                          QStringLiteral("STEP e IGES (*.step *.stp *.iges *.igs *.STEP *.STP *.IGES *.IGS);;"
                                                                         "STEP (*.step *.stp *.STEP *.STP);;IGES (*.iges *.igs *.IGES *.IGS)"));
        if (path.isEmpty()) return;
        settings.setValue(QStringLiteral("import/lastDirectory"), QFileInfo(path).absolutePath());
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        beginForegroundProgress(QStringLiteral("Importazione di %1...").arg(QFileInfo(path).fileName()));
        QVector<ForgeCad::ImportedPart> parts;
        QStringList notes;
        QString error;
        QStringList failures;
        try {
            error = viewport->readImportFile(path, parts, notes);
            if (error.isEmpty()) failures = viewport->importParts(parts, QFileInfo(path).fileName(), [this](int done, int total) {
                updateForegroundProgress(QStringLiteral("Preparazione della vista: %1 / %2 corpi").arg(done).arg(total), done, total);
            });
        } catch (const std::exception &failure) {
            error = QStringLiteral("Importazione non riuscita: %1").arg(QString::fromUtf8(failure.what()));
        }
        endForegroundProgress();
        if (!error.isEmpty()) {
            showImportReport(this, error);
            return;
        }
        int solids = 0;
        for (const ForgeCad::ImportedPart &part : parts) solids += part.solid ? 1 : 0;
        const QString summary = QStringLiteral("Importati da %1: %2 solidi, %3 superfici.").arg(QFileInfo(path).fileName()).arg(solids).arg(parts.size() - solids);
        statusBar()->showMessage(summary, 10000);
        if (!notes.isEmpty() || !failures.isEmpty()) {
            QString text = summary;
            if (!failures.isEmpty()) text += QStringLiteral("\n\nNon costruiti:\n  ") + failures.join(QStringLiteral("\n  "));
            if (!notes.isEmpty()) text += QStringLiteral("\n\nAvvisi:\n  ") + notes.join(QStringLiteral("\n  "));
            showImportReport(this, text);
        }
    });
    // Esportazione per altri CAD (cad_export): corpi visibili, B-rep esatti in mm.
    auto *exportMenu = addCadMenu(fileMenu, QStringLiteral("Esporta"));
    const auto exportMesh = [this, viewport](bool quadrangular) {
        const QVector<ForgeCad::ExportBody> bodies = viewport->exportableBodies();
        bool hasBody = false;
        for (const ForgeCad::ExportBody &body : bodies) hasBody = hasBody || bool(body.body);
        if (!hasBody) {
            QMessageBox::information(this, quadrangular ? QStringLiteral("Esporta OBJ") : QStringLiteral("Esporta STL"),
                                     QStringLiteral("Non ci sono solidi o superfici visibili da esportare."));
            return;
        }

        const ForgeCad::StlExportOptions suggested = viewport->suggestedStlOptions();
        QSettings settings;
        QDialog dialog(this);
        dialog.setWindowTitle(quadrangular ? QStringLiteral("Qualita' mesh OBJ quadrangolare")
                                           : QStringLiteral("Qualita' mesh STL"));
        dialog.setMinimumWidth(470);
        auto *layout = new QVBoxLayout(&dialog);
        auto *description = new QLabel(quadrangular
            ? QStringLiteral("OBJ conserva i quadrilateri validi per Blender; vicino a poli, fori e transizioni restano alcuni triangoli.\n"
                             "Dimensione, scarto cordale e angolo controllano la densita' e l'approssimazione delle curve.")
            : QStringLiteral("La dimensione massima controlla la densita' anche sulle facce piane.\n"
                             "Lo scarto cordale e l'angolo controllano l'approssimazione di raggi e superfici curve."), &dialog);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *automaticMesh = new QCheckBox(QStringLiteral("Calcola automaticamente il raffinamento migliore"), &dialog);
        automaticMesh->setChecked(settings.value(QStringLiteral("export/automaticMeshRefinement"), true).toBool());
        automaticMesh->setToolTip(QStringLiteral("Parte dalle dimensioni del modello e cerca parametri che triangolino tutte le facce. I valori riusciti sono mostrati nel risultato."));
        layout->addWidget(automaticMesh);
        auto *form = new QFormLayout;
        const auto distanceBox = [&](double value, bool allowZero) {
            auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
            box->setDecimals(6);
            box->setRange(allowZero ? 0.0 : 0.000001, 1.0e9);
            box->setSingleStep(qMax(0.000001, value / 10.0));
            box->setValue(value);
            if (allowZero) box->setSpecialValueText(QStringLiteral("Nessun limite"));
            return box;
        };
        auto *edgeSize = distanceBox(automaticMesh->isChecked() ? suggested.maxEdgeLength : settings.value(QStringLiteral("export/stlMaxEdge"), suggested.maxEdgeLength).toDouble(), true);
        auto *deflection = distanceBox(automaticMesh->isChecked() ? suggested.deflection : settings.value(QStringLiteral("export/stlDeflection"), suggested.deflection).toDouble(), false);
        auto *angle = new QDoubleSpinBox(&dialog);
        angle->setDecimals(2); angle->setRange(0.1, 90.0); angle->setSingleStep(1.0); angle->setSuffix(QStringLiteral(" °"));
        angle->setValue(automaticMesh->isChecked() ? suggested.angle : settings.value(QStringLiteral("export/stlAngle"), suggested.angle).toDouble());
        edgeSize->setToolTip(QStringLiteral("Lunghezza massima di ogni lato dei triangoli; zero lascia decidere allo scarto e all'angolo"));
        deflection->setToolTip(QStringLiteral("Distanza massima ammessa tra la mesh e la geometria esatta"));
        angle->setToolTip(QStringLiteral("Variazione massima della normale tra punti vicini sulle superfici curve"));
        form->addRow(QStringLiteral("Lato massimo triangolo:"), edgeSize);
        form->addRow(QStringLiteral("Scarto cordale massimo:"), deflection);
        form->addRow(QStringLiteral("Angolo massimo superfici:"), angle);
        edgeSize->setEnabled(!automaticMesh->isChecked());
        deflection->setEnabled(!automaticMesh->isChecked());
        angle->setEnabled(!automaticMesh->isChecked());
        auto *count = new QLabel(QStringLiteral("Non ancora calcolato"), &dialog);
        count->setWordWrap(true);
        form->addRow(QStringLiteral("Risultato:"), count);
        layout->addLayout(form);
        auto *showPreview = new QCheckBox(QStringLiteral("Mostra anteprima della mesh nella vista"), &dialog);
        showPreview->setChecked(settings.value(QStringLiteral("export/showMeshPreview"), true).toBool());
        showPreview->setToolTip(QStringLiteral("Riutilizza la mesh calcolata: non esegue una seconda tassellazione"));
        layout->addWidget(showPreview);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Esporta..."));
        auto *calculate = buttons->addButton(QStringLiteral("Calcola mesh e anteprima"), QDialogButtonBox::ActionRole);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        ForgeCad::StlBuildResult built;
        ForgeCad::ObjBuildResult builtObj;
        ForgeCad::StlExportOptions builtOptions;
        bool cacheValid = false;
        const auto currentOptions = [&] {
            ForgeCad::StlExportOptions options;
            options.maxEdgeLength = edgeSize->value();
            options.deflection = deflection->value();
            options.angle = angle->value();
            options.automaticRefinement = automaticMesh->isChecked();
            return options;
        };
        const auto sameOptions = [](const ForgeCad::StlExportOptions &a, const ForgeCad::StlExportOptions &b) {
            return a.maxEdgeLength == b.maxEdgeLength && a.deflection == b.deflection && a.angle == b.angle
                && a.automaticRefinement == b.automaticRefinement;
        };
        const auto build = [&] {
            builtOptions = currentOptions();
            count->setStyleSheet(QString());
            count->setText(QStringLiteral("Calcolo in corso..."));
            calculate->setEnabled(false);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (quadrangular) builtObj = ForgeCad::buildQuadObj(bodies, builtOptions);
            else built = ForgeCad::buildBinaryStl(bodies, builtOptions);
            QApplication::restoreOverrideCursor();
            calculate->setEnabled(true);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
            const QString buildError = quadrangular ? builtObj.error : built.error;
            cacheValid = buildError.isEmpty();
            if (!cacheValid) {
                viewport->clearExportMeshPreview();
                count->setStyleSheet(QStringLiteral("color:#ff806e"));
                count->setText(buildError);
            } else {
                const bool limited = quadrangular ? builtObj.previewLimited : built.previewLimited;
                if (quadrangular) {
                    count->setText(QStringLiteral("%1 quadrilateri + %2 triangoli — %3%4")
                        .arg(QLocale().toString(qlonglong(builtObj.quadCount)),
                             QLocale().toString(qlonglong(builtObj.triangleCount)),
                             QLocale().formattedDataSize(builtObj.data.size()),
                             limited ? QStringLiteral(" — anteprima alleggerita") : QString()));
                } else {
                    count->setText(QStringLiteral("%1 triangoli — %2%3")
                        .arg(QLocale().toString(qlonglong(built.triangleCount)),
                             QLocale().formattedDataSize(built.data.size()),
                             limited ? QStringLiteral(" — anteprima alleggerita") : QString()));
                }
                const auto &used = quadrangular ? builtObj.usedOptions : built.usedOptions;
                if (automaticMesh->isChecked()) {
                    const QSignalBlocker edgeBlock(edgeSize), deflectionBlock(deflection), angleBlock(angle);
                    edgeSize->setValue(used.maxEdgeLength);
                    deflection->setValue(used.deflection);
                    angle->setValue(used.angle);
                    builtOptions = currentOptions();
                }
                count->setText(count->text() + QStringLiteral("\nLato %1 mm · scarto %2 mm · angolo %3° · %4 tentativi")
                    .arg(QLocale().toString(used.maxEdgeLength, 'g', 6), QLocale().toString(used.deflection, 'g', 6),
                         QLocale().toString(used.angle, 'g', 4))
                    .arg(quadrangular ? builtObj.tessellationAttempts : built.tessellationAttempts));
                if (showPreview->isChecked())
                    viewport->setExportMeshPreview(quadrangular ? builtObj.preview : built.preview, quadrangular, limited);
                else viewport->clearExportMeshPreview();
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return cacheValid;
        };
        connect(calculate, &QPushButton::clicked, &dialog, [&, build] { build(); });
        const auto invalidate = [&] {
            cacheValid = false;
            viewport->clearExportMeshPreview();
            count->setStyleSheet(QString());
            count->setText(QStringLiteral("Parametri modificati — ricalcolare"));
        };
        connect(automaticMesh, &QCheckBox::toggled, &dialog, [&](bool automatic) {
            settings.setValue(QStringLiteral("export/automaticMeshRefinement"), automatic);
            edgeSize->setEnabled(!automatic);
            deflection->setEnabled(!automatic);
            angle->setEnabled(!automatic);
            if (automatic) {
                edgeSize->setValue(suggested.maxEdgeLength);
                deflection->setValue(suggested.deflection);
                angle->setValue(suggested.angle);
            }
            invalidate();
        });
        connect(edgeSize, &QDoubleSpinBox::valueChanged, &dialog, [invalidate](double) { invalidate(); });
        connect(deflection, &QDoubleSpinBox::valueChanged, &dialog, [invalidate](double) { invalidate(); });
        connect(angle, &QDoubleSpinBox::valueChanged, &dialog, [invalidate](double) { invalidate(); });
        connect(showPreview, &QCheckBox::toggled, &dialog, [&](bool visible) {
            settings.setValue(QStringLiteral("export/showMeshPreview"), visible);
            if (!visible || !cacheValid) viewport->clearExportMeshPreview();
            else viewport->setExportMeshPreview(quadrangular ? builtObj.preview : built.preview, quadrangular,
                                                quadrangular ? builtObj.previewLimited : built.previewLimited);
        });
        const bool accepted = runModeless(this, viewport, dialog);
        viewport->clearExportMeshPreview();
        if (!accepted) return;

        const ForgeCad::StlExportOptions requested = currentOptions();
        settings.setValue(QStringLiteral("export/stlMaxEdge"), requested.maxEdgeLength);
        settings.setValue(QStringLiteral("export/stlDeflection"), requested.deflection);
        settings.setValue(QStringLiteral("export/stlAngle"), requested.angle);
        const QString base = documentPath_.isEmpty() ? QStringLiteral("Senza nome") : QFileInfo(documentPath_).completeBaseName();
        const QString directory = documentPath_.isEmpty() ? QDir::homePath() : QFileInfo(documentPath_).absolutePath();
        const QString suffix = quadrangular ? QStringLiteral("obj") : QStringLiteral("stl");
        QString path = QFileDialog::getSaveFileName(this,
                                                    quadrangular ? QStringLiteral("Esporta OBJ quadrangolare") : QStringLiteral("Esporta STL"),
                                                    directory + QStringLiteral("/") + base + QStringLiteral(".") + suffix,
                                                    quadrangular ? QStringLiteral("Wavefront OBJ (*.obj)") : QStringLiteral("STL binario (*.stl)"));
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) != 0) path += QStringLiteral(".") + suffix;
        if (!cacheValid || !sameOptions(requested, builtOptions)) {
            beginForegroundProgress(quadrangular ? QStringLiteral("Tassellazione OBJ quadrangolare...")
                                                 : QStringLiteral("Tassellazione STL..."));
            builtOptions = requested;
            if (quadrangular) builtObj = ForgeCad::buildQuadObj(bodies, requested);
            else built = ForgeCad::buildBinaryStl(bodies, requested);
            cacheValid = quadrangular ? builtObj.error.isEmpty() : built.error.isEmpty();
            endForegroundProgress();
        }
        if (!cacheValid) {
            QMessageBox::warning(this, quadrangular ? QStringLiteral("Esporta OBJ") : QStringLiteral("Esporta STL"),
                                 quadrangular ? builtObj.error : built.error);
            return;
        }
        beginForegroundProgress(quadrangular ? QStringLiteral("Scrittura OBJ...") : QStringLiteral("Scrittura STL..."));
        const QString error = quadrangular ? ForgeCad::saveQuadObj(path, builtObj.data)
                                           : ForgeCad::saveBinaryStl(path, built.data);
        endForegroundProgress();
        if (!error.isEmpty()) QMessageBox::warning(this, quadrangular ? QStringLiteral("Esporta OBJ") : QStringLiteral("Esporta STL"), error);
        else if (quadrangular)
            statusBar()->showMessage(QStringLiteral("Esportati %1 quadrilateri e %2 triangoli in %3")
                .arg(QLocale().toString(qlonglong(builtObj.quadCount)),
                     QLocale().toString(qlonglong(builtObj.triangleCount)), path), 10000);
        else
            statusBar()->showMessage(QStringLiteral("Esportati %1 triangoli in %2")
                                     .arg(QLocale().toString(qlonglong(built.triangleCount)), path), 10000);
    };
    connect(exportMenu->addAction(QStringLiteral("STL mesh triangolare...")), &QAction::triggered, this,
            [exportMesh] { exportMesh(false); });
    connect(exportMenu->addAction(QStringLiteral("OBJ mesh quadrangolare (Blender)...")), &QAction::triggered, this,
            [exportMesh] { exportMesh(true); });
    exportMenu->addSeparator();
    const QList<QPair<QString, ForgeCad::ExportFormat>> exportFormats = {
        {QStringLiteral("STEP AP242..."), ForgeCad::ExportFormat::StepAP242},
        {QStringLiteral("STEP AP214..."), ForgeCad::ExportFormat::StepAP214},
        {QStringLiteral("STEP AP203..."), ForgeCad::ExportFormat::StepAP203},
        {QStringLiteral("IGES 5.3 solidi (186)..."), ForgeCad::ExportFormat::IgesSolids},
        {QStringLiteral("IGES 5.3 superfici (144)..."), ForgeCad::ExportFormat::IgesSurfaces}};
    for (const auto &entry : exportFormats) {
        const ForgeCad::ExportFormat format = entry.second;
        const QString title = QString(entry.first).remove(QStringLiteral("..."));
        connect(exportMenu->addAction(entry.first), &QAction::triggered, this, [this, format, title] {
            const QVector<ForgeCad::ExportBody> bodies = viewport_->exportableBodies();
            if (bodies.isEmpty()) {
                QMessageBox::information(this, QStringLiteral("Esporta"), QStringLiteral("Non ci sono corpi visibili da esportare."));
                return;
            }
            const QString suffix = ForgeCad::exportSuffix(format);
            const QString base = documentPath_.isEmpty() ? QStringLiteral("Senza nome") : QFileInfo(documentPath_).completeBaseName();
            const QString filter = suffix == QLatin1String("step") ? QStringLiteral("STEP (*.step *.stp)") : QStringLiteral("IGES (*.igs *.iges)");
            QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Esporta %1").arg(title),
                QFileInfo(documentPath_).absolutePath() + QStringLiteral("/") + base + QStringLiteral(".") + suffix, filter);
            if (path.isEmpty()) return;
            const QString extension = QFileInfo(path).suffix().toLower();
            const bool known = suffix == QLatin1String("step") ? (extension == QLatin1String("step") || extension == QLatin1String("stp"))
                                                               : (extension == QLatin1String("igs") || extension == QLatin1String("iges"));
            if (!known) path += QStringLiteral(".") + suffix;
            beginForegroundProgress(QStringLiteral("Esportazione %1...").arg(title));
            const QString error = ForgeCad::exportBodies(path, bodies, format);
            endForegroundProgress();
            if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Esporta"), error);
            else statusBar()->showMessage(QStringLiteral("Esportati %1 corpi in %2 (%3)").arg(bodies.size()).arg(path, title), 8000);
        });
    }
    fileMenu->addSeparator();
    auto *editMenu = addCadMenu(menuBar(), QStringLiteral("Modifica"));
    undoAction_ = editMenu->addAction(QStringLiteral("Annulla"));
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = editMenu->addAction(QStringLiteral("Ripeti"));
    redoAction_->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    connect(undoAction_, &QAction::triggered, this, [viewport] { viewport->undo(); });
    connect(redoAction_, &QAction::triggered, this, [viewport] { viewport->redo(); });
    editMenu->addSeparator();
    // Elimina: il corpo o lo schizzo selezionato (con le funzioni che ne
    // dipendono, dopo conferma); in modalita' schizzo le entita' selezionate.
    QAction *deleteAction = editMenu->addAction(QStringLiteral("Elimina"));
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, [viewport] { viewport->deleteSelection(); });
    // Rigenera tutto: l'intera storia ricalcolata con il kernel attuale, senza
    // usare i corpi salvati nel documento.
    QAction *rebuildAction = editMenu->addAction(QStringLiteral("Rigenera tutto"));
    rebuildAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    rebuildAction->setToolTip(QStringLiteral("Ricalcola tutte le feature della storia con il kernel attuale (anche quelle lette dal documento)"));
    connect(rebuildAction, &QAction::triggered, this, [this, viewport] {
        const int failed = viewport->rebuildAllFeatures();
        statusBar()->showMessage(failed == 0 ? QStringLiteral("Storia rigenerata.")
                                             : QStringLiteral("Storia rigenerata: %1 feature con errori (⚠ nell'albero).").arg(failed),
                                 8000);
    });
    updateUndoActions();
    auto *viewMenu = addCadMenu(menuBar(), QStringLiteral("Visualizza"));
    auto *storyboardCursorMenu = addCadMenu(viewMenu, QStringLiteral("Barra di posizione storyboard"));
    auto *storyboardCursorGroup = new QActionGroup(this);
    storyboardCursorGroup->setExclusive(true);
    const QStringList cursorModes = {QStringLiteral("Orizzontale nell'albero"), QStringLiteral("Verticale a sinistra"), QStringLiteral("Verticale a destra")};
    for (int mode = 0; mode < cursorModes.size(); ++mode) {
        auto *action = storyboardCursorMenu->addAction(cursorModes.at(mode));
        action->setObjectName(QStringLiteral("storyboardCursorMode%1").arg(mode));
        action->setCheckable(true);
        action->setChecked(int(modelTree->cursorMode()) == mode);
        storyboardCursorGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, modelTree, mode] {
            QSettings().setValue(QStringLiteral("view/storyboardCursorMode"), mode);
            modelTree->setCursorMode(static_cast<StoryboardTree::CursorMode>(mode));
            rebuildModelTree();
        });
    }
    auto *functionPanelMenu = addCadMenu(viewMenu, QStringLiteral("Pannelli delle funzioni"));
    auto *functionPanelOpacityAction = functionPanelMenu->addAction(QString());
    auto *functionPanelBlurAction = functionPanelMenu->addAction(QString());
    auto *functionPanelRadiusAction = functionPanelMenu->addAction(QString());
    functionPanelOpacityAction->setIcon(ForgeCad::commandIcon(QStringLiteral("panelOpacity")));
    functionPanelBlurAction->setIcon(ForgeCad::commandIcon(QStringLiteral("panelBlur")));
    functionPanelRadiusAction->setIcon(ForgeCad::commandIcon(QStringLiteral("panelCorners")));
    const auto updateFunctionPanelActions = [functionPanelOpacityAction, functionPanelBlurAction, functionPanelRadiusAction] {
        QSettings settings;
        functionPanelOpacityAction->setText(QStringLiteral("Trasparenza... (%1%)")
                                                .arg(settings.value(QStringLiteral("view/functionPanelOpacity"), 88).toInt()));
        functionPanelBlurAction->setText(QStringLiteral("Sfocatura scena... (%1 px)")
                                             .arg(settings.value(QStringLiteral("view/functionPanelBlur"), 12).toInt()));
        functionPanelRadiusAction->setText(QStringLiteral("Raggio degli angoli... (%1 px)")
                                               .arg(settings.value(QStringLiteral("view/functionPanelCornerRadius"), 10).toInt()));
    };
    const auto refreshFunctionPanels = [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *panel = dynamic_cast<FunctionDialogPanel *>(widget)) panel->reloadAppearance();
            for (QDialog *dialog : widget->findChildren<QDialog *>())
                if (auto *panel = dynamic_cast<FunctionDialogPanel *>(dialog)) panel->reloadAppearance();
        }
    };
    connect(functionPanelOpacityAction, &QAction::triggered, this, [this, updateFunctionPanelActions, refreshFunctionPanels] {
        QSettings settings;
        bool accepted = false;
        const int value = QInputDialog::getInt(this, QStringLiteral("Trasparenza dei pannelli"), QStringLiteral("Opacità dello sfondo (%):"),
                                               settings.value(QStringLiteral("view/functionPanelOpacity"), 88).toInt(), 25, 100, 1, &accepted);
        if (!accepted) return;
        settings.setValue(QStringLiteral("view/functionPanelOpacity"), value);
        updateFunctionPanelActions();
        refreshFunctionPanels();
    });
    connect(functionPanelBlurAction, &QAction::triggered, this, [this, updateFunctionPanelActions, refreshFunctionPanels] {
        QSettings settings;
        bool accepted = false;
        const int value = QInputDialog::getInt(this, QStringLiteral("Sfocatura dei pannelli"),
                                               QStringLiteral("Raggio della sfocatura della scena (px):"),
                                               settings.value(QStringLiteral("view/functionPanelBlur"), 12).toInt(),
                                               0, 40, 1, &accepted);
        if (!accepted) return;
        settings.setValue(QStringLiteral("view/functionPanelBlur"), value);
        updateFunctionPanelActions();
        refreshFunctionPanels();
    });
    connect(functionPanelRadiusAction, &QAction::triggered, this, [this, updateFunctionPanelActions, refreshFunctionPanels] {
        QSettings settings;
        bool accepted = false;
        const int value = QInputDialog::getInt(this, QStringLiteral("Angoli dei pannelli"),
                                               QStringLiteral("Raggio degli angoli (px):"),
                                               settings.value(QStringLiteral("view/functionPanelCornerRadius"), 10).toInt(),
                                               0, 40, 1, &accepted);
        if (!accepted) return;
        settings.setValue(QStringLiteral("view/functionPanelCornerRadius"), value);
        updateFunctionPanelActions();
        refreshFunctionPanels();
    });
    updateFunctionPanelActions();
    auto *functionsMenu = addCadMenu(menuBar(), QStringLiteral("Funzioni"));
    QAction *extrudeAction = functionsMenu->addAction(QStringLiteral("Estrusione..."));
    extrudeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    auto *booleanMenu = addCadMenu(functionsMenu, QStringLiteral("Booleane"));
    QAction *unionAction = booleanMenu->addAction(QStringLiteral("Unione..."));
    QAction *intersectionAction = booleanMenu->addAction(QStringLiteral("Intersezione..."));
    QAction *differenceAction = booleanMenu->addAction(QStringLiteral("Differenza A - B..."));
    QAction *revolveAction = functionsMenu->addAction(QStringLiteral("Rivoluzione..."));
     revolveAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    auto *primitiveMenu = addCadMenu(functionsMenu, QStringLiteral("Primitive"));
    QAction *filletAction = functionsMenu->addAction(QStringLiteral("Raccordo spigoli..."));
    QAction *chamferAction = functionsMenu->addAction(QStringLiteral("Smusso spigoli..."));
    QAction *draftAction = functionsMenu->addAction(QStringLiteral("Sformo facce..."));
    draftAction->setToolTip(QStringLiteral("Inclina le facce piane di solidi convessi o superfici rispetto a un piano neutro"));
    QAction *shellAction = functionsMenu->addAction(QStringLiteral("Svuota..."));
    shellAction->setToolTip(QStringLiteral("Svuota un solido lasciando pareti di spessore costante; le facce scelte si tolgono e fanno l'apertura"));
    QAction *threadAction = functionsMenu->addAction(QStringLiteral("Filettatura automatica..."));
    threadAction->setToolTip(QStringLiteral("Crea una filettatura reale scegliendo la faccia cilindrica o conica di un foro o di un albero"));
    QAction *datumAction = functionsMenu->addAction(QStringLiteral("Piano di costruzione..."));
    datumAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    datumAction->setToolTip(QStringLiteral("Piano parallelo a distanza, per tre punti, normale a una curva, per una retta e un punto, ad angolo, medio"));
    auto *patternMenu = addCadMenu(functionsMenu, QStringLiteral("Ripetizione"));
    QAction *linearPatternAction = patternMenu->addAction(QStringLiteral("Ripetizione lineare..."));
    QAction *circularPatternAction = patternMenu->addAction(QStringLiteral("Ripetizione circolare..."));
    QAction *mirrorAction = patternMenu->addAction(QStringLiteral("Specchio..."));
    linearPatternAction->setToolTip(QStringLiteral("Copie di un corpo solido, di superficie o di una funzione (foro, sporgenza) lungo una o due direzioni"));
    circularPatternAction->setToolTip(QStringLiteral("Copie di un corpo solido, di superficie o di una funzione attorno a un asse"));
    mirrorAction->setToolTip(QStringLiteral("Immagine speculare di un corpo solido, di superficie o di una funzione rispetto a un piano"));
    QAction *moveAction = functionsMenu->addAction(QStringLiteral("Sposta / ruota corpo..."));
    moveAction->setToolTip(QStringLiteral("Traslazione e rotazione esatte del corpo selezionato (anche come copia)"));
    QAction *scaleAction = functionsMenu->addAction(QStringLiteral("Scala..."));
    scaleAction->setToolTip(QStringLiteral("Scala uniforme del corpo selezionato attorno all'origine, al baricentro o a un punto"));
    functionsMenu->addSeparator();
    QAction *helixAction = functionsMenu->addAction(QStringLiteral("Elica e spirale..."));
    helixAction->setToolTip(QStringLiteral("Elica (cilindrica o conica) o spirale piana da un cerchio di uno schizzo, uno spigolo circolare o una faccia cilindrica"));
    QAction *sweepAction = functionsMenu->addAction(QStringLiteral("Sweep (estrusione lungo un percorso)..."));
    sweepAction->setToolTip(QStringLiteral("Sezione piana trascinata lungo un percorso: schizzo, bordo di faccia o curva 3D"));
    QAction *loftAction = functionsMenu->addAction(QStringLiteral("Loft..."));
    loftAction->setToolTip(QStringLiteral("Solido o superficie che passa per le sezioni di piu' schizzi"));
    functionsMenu->addSeparator();
    auto *surfaceMenu = addCadMenu(functionsMenu, QStringLiteral("Superfici"));
    QAction *trimSurfaceAction = surfaceMenu->addAction(QStringLiteral("Taglia superficie..."));
    trimSurfaceAction->setToolTip(QStringLiteral("Taglia una superficie con un corpo (superficie o solido) o un piano e ne tiene una parte"));
    QAction *extendSurfaceAction = surfaceMenu->addAction(QStringLiteral("Estendi superficie..."));
    extendSurfaceAction->setToolTip(QStringLiteral("Estende i bordi scelti di una superficie: clic sui bordi, Invio, poi la distanza"));
    QAction *copyFacesAction = surfaceMenu->addAction(QStringLiteral("Copia facce..."));
    copyFacesAction->setToolTip(QStringLiteral("Copia una faccia, piu' facce o tutta una superficie cucita, mantenendo il corpo originale"));
    QAction *offsetSurfaceAction = surfaceMenu->addAction(QStringLiteral("Offset superficie..."));
    offsetSurfaceAction->setToolTip(QStringLiteral("Superficie a distanza costante dalle facce scelte di un solido o di una superficie "
                                                   "(con estensione e rifilo opzionali dei bordi comuni)"));
    QAction *deleteFaceAction = surfaceMenu->addAction(QStringLiteral("Elimina facce..."));
    deleteFaceAction->setToolTip(QStringLiteral("Toglie le facce scelte da un solido o da una superficie: un solido diventa una superficie aperta"));
    QAction *ruledSurfaceAction = surfaceMenu->addAction(QStringLiteral("Superficie rigata..."));
    ruledSurfaceAction->setToolTip(QStringLiteral("Superficie di rette tra due curve scelte nella vista (spigoli, entita' degli schizzi, eliche)"));
    QAction *boundarySurfaceAction = surfaceMenu->addAction(QStringLiteral("Superficie tra curve..."));
    boundarySurfaceAction->setToolTip(QStringLiteral("Superficie delimitata da un contorno chiuso di curve, anche sghembo (3 o 4 lati: patch di Coons)"));
    QAction *fillSurfaceAction = surfaceMenu->addAction(QStringLiteral("Superficie di riempimento..."));
    fillSurfaceAction->setToolTip(QStringLiteral("Superficie in un contorno chiuso che passa per curve guida e continua in tangenza o "
                                                 "curvatura le facce adiacenti"));
    QAction *thickenAction = surfaceMenu->addAction(QStringLiteral("Spessore..."));
    thickenAction->setToolTip(QStringLiteral("Da' spessore a una superficie (o a facce scelte) lungo la normale o lungo una direzione scelta"));
    QAction *extractedCurveAction = surfaceMenu->addAction(QStringLiteral("Estrai bordo 3D..."));
    extractedCurveAction->setToolTip(QStringLiteral("Curva esatta associativa dal bordo originale, senza proiezione su un piano"));
    QAction *trimCurve3DAction = surfaceMenu->addAction(QStringLiteral("Taglia curva 3D..."));
    trimCurve3DAction->setToolTip(QStringLiteral("Taglia una curva con un segmento mantenendo il legame con la sorgente"));
    QAction *projectedCurveAction = surfaceMenu->addAction(QStringLiteral("Curva proiettata..."));
    projectedCurveAction->setToolTip(QStringLiteral("Curve 3D: lo schizzo proiettato lungo la normale del suo piano sulla prima faccia del corpo"));
    QAction *planarSurfaceAction = surfaceMenu->addAction(QStringLiteral("Superficie planare..."));
    planarSurfaceAction->setToolTip(QStringLiteral("Superficie piana delimitata dai contorni chiusi di uno schizzo o da bordi complanari"));
    QAction *loftSurfaceAction = surfaceMenu->addAction(QStringLiteral("Loft di superfici..."));
    loftSurfaceAction->setToolTip(QStringLiteral("Loft che resta una superficie: con sezioni chiuse un tubo senza coperchi"));
    QAction *sweepSurfaceAction = surfaceMenu->addAction(QStringLiteral("Sweep di superfici..."));
    sweepSurfaceAction->setToolTip(QStringLiteral("Sweep che resta una superficie: con un profilo chiuso un tubo senza coperchi"));
    QAction *sewSurfacesAction = surfaceMenu->addAction(QStringLiteral("Cuci superfici..."));
    sewSurfacesAction->setToolTip(QStringLiteral("Unisce le superfici lungo i bordi comuni; se chiudono un volume crea un solido"));
    auto *analysisMenu = addCadMenu(menuBar(), QStringLiteral("Analisi"));
    auto *shapeAction = analysisMenu->addAction(QStringLiteral("Curvatura, zebra e continuità..."));
    connect(shapeAction, &QAction::triggered, this, [this, viewport] { shapeAnalysisDialog(this, viewport); });
    QAction *massAction = analysisMenu->addAction(QStringLiteral("Proprieta' di massa..."));
    massAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    massAction->setToolTip(QStringLiteral("Volume, massa, baricentro e momenti d'inerzia del corpo selezionato"));
    QAction *microAction = analysisMenu->addAction(QStringLiteral("Micro-geometrie..."));
    microAction->setToolTip(QStringLiteral("Facce sottili, spigoli corti e quasi tangenti del corpo selezionato: il rumore che fa fallire raccordi e offset"));
    connect(microAction, &QAction::triggered, this, [this, viewport] { microFeaturesDialog(this, viewport); });
    QAction *measureAction = analysisMenu->addAction(QStringLiteral("Misura..."));
    measureAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    measureAction->setToolTip(QStringLiteral("Misura nella vista: distanze, angoli, lunghezze, raggi e aree di punti, spigoli e facce"));
    auto *modeMenu = addCadMenu(viewMenu, QStringLiteral("Stile visualizzazione"));
    auto *qualityMenu = addCadMenu(viewMenu, QStringLiteral("Qualita tessellazione"));
    auto *qualityGroup = new QActionGroup(this); qualityGroup->setExclusive(true);
    QAction *lowQuality = qualityMenu->addAction(QStringLiteral("Bassa"));
    QAction *mediumQuality = qualityMenu->addAction(QStringLiteral("Media"));
    QAction *highQuality = qualityMenu->addAction(QStringLiteral("Alta"));
    lowQuality->setCheckable(true); mediumQuality->setCheckable(true); highQuality->setCheckable(true);
    mediumQuality->setChecked(true);
    qualityGroup->addAction(lowQuality); qualityGroup->addAction(mediumQuality); qualityGroup->addAction(highQuality);
    auto *lightingMenu = addCadMenu(viewMenu, QStringLiteral("Luci scena"));
    QAction *backgroundAction = viewMenu->addAction(QStringLiteral("Sfondo e luce ambiente..."));
    connect(backgroundAction, &QAction::triggered, this, &PdfWindow::editBackground);
    QAction *historyGraphAction = viewMenu->addAction(QStringLiteral("Debug history / storyboard..."));
    historyGraphAction->setToolTip(QStringLiteral("Grafo di feature, schizzi, operandi, riferimenti topologici ed errori di rigenerazione"));
    connect(historyGraphAction, &QAction::triggered, this, [this, viewport] {
        auto *dialog = static_cast<ForgeCad::HistoryGraphDialog *>(historyGraphDialog_);
        if (!dialog) {
            dialog = new ForgeCad::HistoryGraphDialog(this);
            historyGraphDialog_ = dialog;
            dialog->setSelectionCallback([viewport](int kind, int index) {
                if (kind == 0) viewport->selectSketch(index);
                else if (kind == 1) viewport->selectObject(SceneObjectKind::Extrusion, index);
            });
            connect(dialog, &QObject::destroyed, this, [this] { historyGraphDialog_ = nullptr; });
        }
        dialog->setDocument(viewport->currentDocument());
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    });
    // Sfondo dell'albero modello (QSettings view/treeBackground; vuoto = quello del tema).
    auto *treeColorMenu = addCadMenu(viewMenu, QStringLiteral("Sfondo dell'albero"));
    QAction *treeColorAction = treeColorMenu->addAction(QStringLiteral("Scegli il colore..."));
    QAction *treeColorReset = treeColorMenu->addAction(QStringLiteral("Colore del tema"));
    connect(treeColorAction, &QAction::triggered, this, [this] {
        QSettings settings;
        const QColor current = settings.value(QStringLiteral("view/treeBackground")).value<QColor>();
        const QColor color = QColorDialog::getColor(current.isValid() ? current : modelTree_->palette().color(QPalette::Base), this,
                                                    QStringLiteral("Sfondo dell'albero modello"));
        if (!color.isValid()) return;
        settings.setValue(QStringLiteral("view/treeBackground"), color);
        applyTreeBackground(color);
    });
    connect(treeColorReset, &QAction::triggered, this, [this] {
        QSettings().remove(QStringLiteral("view/treeBackground"));
        applyTreeBackground(QColor());
    });
    applyTreeBackground(QSettings().value(QStringLiteral("view/treeBackground")).value<QColor>());
    // Colore delle facce della feature scelta (QSettings view/featureHighlightColor; di
    // default quello della selezione degli spigoli).
    auto *highlightColorMenu = addCadMenu(viewMenu, QStringLiteral("Colore evidenziazione feature"));
    QAction *highlightColorAction = highlightColorMenu->addAction(QStringLiteral("Scegli il colore..."));
    QAction *highlightColorReset = highlightColorMenu->addAction(QStringLiteral("Colore della selezione (predefinito)"));
    connect(highlightColorAction, &QAction::triggered, this, [this, viewport] {
        const QColor color = QColorDialog::getColor(viewport->featureHighlightColor(), this,
                                                    QStringLiteral("Colore delle facce della feature scelta"));
        if (!color.isValid()) return;
        QSettings().setValue(QStringLiteral("view/featureHighlightColor"), color);
        viewport->setFeatureHighlightColor(color);
    });
    connect(highlightColorReset, &QAction::triggered, this, [viewport] {
        QSettings().remove(QStringLiteral("view/featureHighlightColor"));
        viewport->setFeatureHighlightColor(QColor());
    });
    viewport->setFeatureHighlightColor(QSettings().value(QStringLiteral("view/featureHighlightColor")).value<QColor>());
    QAction *showAllAction = viewMenu->addAction(QStringLiteral("Mostra tutti gli oggetti"));
    showAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    connect(showAllAction, &QAction::triggered, this, [viewport] { viewport->showAllObjects(); });
    auto *planesAction = viewMenu->addAction(QStringLiteral("Piani di riferimento"));
    planesAction->setCheckable(true); planesAction->setChecked(true);
    QSettings viewSettings;
    auto *gridAction = viewMenu->addAction(QStringLiteral("Griglia"));
    gridAction->setCheckable(true); gridAction->setChecked(viewSettings.value(QStringLiteral("view/grid"), true).toBool());
    viewport->setGridVisible(gridAction->isChecked());
    connect(gridAction, &QAction::toggled, this, [viewport](bool visible) {
        viewport->setGridVisible(visible);
        QSettings().setValue(QStringLiteral("view/grid"), visible);
    });
    // Assi: visibili o no, sopra gli oggetti o sotto, lunghezza.
    auto *axesMenu = addCadMenu(viewMenu, QStringLiteral("Assi"));
    auto *axesVisibleAction = axesMenu->addAction(QStringLiteral("Mostra gli assi"));
    axesVisibleAction->setCheckable(true); axesVisibleAction->setChecked(viewSettings.value(QStringLiteral("view/axes"), true).toBool());
    viewport->setAxesVisible(axesVisibleAction->isChecked());
    connect(axesVisibleAction, &QAction::toggled, this, [viewport](bool visible) {
        viewport->setAxesVisible(visible);
        QSettings().setValue(QStringLiteral("view/axes"), visible);
    });
    auto *axesOnTopAction = axesMenu->addAction(QStringLiteral("Sempre in primo piano (sopra solidi, superfici e schizzi)"));
    axesOnTopAction->setCheckable(true); axesOnTopAction->setChecked(viewSettings.value(QStringLiteral("view/axesOnTop"), true).toBool());
    viewport->setAxesOnTop(axesOnTopAction->isChecked());
    connect(axesOnTopAction, &QAction::toggled, this, [viewport](bool onTop) {
        viewport->setAxesOnTop(onTop);
        QSettings().setValue(QStringLiteral("view/axesOnTop"), onTop);
    });
    auto *axisLengthAction = axesMenu->addAction(QStringLiteral("Dimensione degli assi..."));
    viewport->setAxisLength(viewSettings.value(QStringLiteral("view/axisLength"), CadViewport::kDefaultAxisLength).toDouble());
    connect(axisLengthAction, &QAction::triggered, this, [this, viewport] {
        bool accepted = false;
        const double length = ForgeCad::getDouble(this, QStringLiteral("Dimensione degli assi"),
            QStringLiteral("Scala degli assi a video (2 = dimensione standard, adattata allo zoom):"),
            viewport->axisLength(), 0.1, 100.0, 3, &accepted, false);
        if (!accepted) return;
        viewport->setAxisLength(length);
        QSettings().setValue(QStringLiteral("view/axisLength"), viewport->axisLength());
    });
    // Trasparenza dei corpi in modalita' schizzo: lo schizzo dietro (o dentro) i solidi resta leggibile.
    auto *sketchOpacityAction = viewMenu->addAction(QStringLiteral("Trasparenza dei corpi nello schizzo..."));
    viewport->setSketchBodyOpacity(viewSettings.value(QStringLiteral("view/sketchBodyOpacity"), 0.35).toDouble());
    connect(sketchOpacityAction, &QAction::triggered, this, [this, viewport] {
        const double previous = viewport->sketchBodyOpacity();
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Trasparenza dei corpi nello schizzo"));
        auto *form = new QFormLayout(&dialog);
        form->addRow(wrappedNote(QStringLiteral("In modalita' schizzo le facce dei corpi diventano trasparenti\n"
                                               "e non nascondono lo schizzo (100% = opache, come fuori dallo schizzo)."), &dialog));
        auto *slider = new QSlider(Qt::Horizontal, &dialog);
        slider->setRange(5, 100);
        slider->setValue(int(std::lround(previous * 100.0)));
        auto *percent = new QSpinBox(&dialog);
        percent->setRange(5, 100);
        percent->setSuffix(QStringLiteral(" %"));
        percent->setValue(slider->value());
        auto *row = new QWidget(&dialog);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(percent);
        form->addRow(QStringLiteral("Opacita':"), row);
        connect(slider, &QSlider::valueChanged, percent, &QSpinBox::setValue);
        connect(percent, &QSpinBox::valueChanged, slider, &QSlider::setValue);
        connect(slider, &QSlider::valueChanged, &dialog, [viewport](int value) { viewport->setSketchBodyOpacity(value / 100.0); });  // dal vivo
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted) {
            viewport->setSketchBodyOpacity(previous);
            return;
        }
        QSettings().setValue(QStringLiteral("view/sketchBodyOpacity"), viewport->sketchBodyOpacity());
    });
    // Antialiasing (MSAA) delle linee degli schizzi, degli spigoli e dei contorni dei solidi.
    auto *antialiasingMenu = addCadMenu(viewMenu, QStringLiteral("Antialiasing"));
    auto *antialiasingGroup = new QActionGroup(this); antialiasingGroup->setExclusive(true);
    const int savedSamples = viewSettings.value(QStringLiteral("view/antialiasing"), 4).toInt();
    viewport->setAntialiasing(savedSamples);
    for (int samples : {0, 2, 4, 8, 16}) {
        auto *action = antialiasingMenu->addAction(samples == 0 ? QStringLiteral("Spento") : QStringLiteral("%1x MSAA").arg(samples));
        action->setCheckable(true); action->setChecked(samples == savedSamples); antialiasingGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, viewport, samples] {
            viewport->setAntialiasing(samples);
            QSettings().setValue(QStringLiteral("view/antialiasing"), samples);
            const int maximum = viewport->maxAntialiasing();
            if (samples > 0 && maximum > 0 && samples > maximum)
                statusBar()->showMessage(QStringLiteral("Antialiasing: la scheda arriva a %1x, uso %1x").arg(maximum), 6000);
            else
                statusBar()->showMessage(samples == 0 ? QStringLiteral("Antialiasing spento") : QStringLiteral("Antialiasing %1x").arg(samples), 4000);
        });
    }
    auto *wheelZoomAction = viewMenu->addAction(QStringLiteral("Zoom con rotella"));
    wheelZoomAction->setCheckable(true); wheelZoomAction->setChecked(true);
    auto *zoomInAction = viewMenu->addAction(QStringLiteral("Aumenta zoom"));
    auto *zoomOutAction = viewMenu->addAction(QStringLiteral("Riduci zoom"));
    auto *resetZoomAction = viewMenu->addAction(QStringLiteral("Zoom tutto (inquadra la scena)"));
    zoomInAction->setShortcut(QKeySequence(Qt::Key_Plus));
    zoomOutAction->setShortcut(QKeySequence(Qt::Key_Minus));
    resetZoomAction->setShortcut(QKeySequence(Qt::Key_0));
    auto *modeGroup = new QActionGroup(this); modeGroup->setExclusive(true);
    auto addMode = [this, modeMenu, modeGroup, viewport](const QString &text, int mode) {
        auto *action = modeMenu->addAction(text); action->setCheckable(true); action->setChecked(mode == 2); modeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, viewport, mode] { viewport->setDisplayMode(mode); setDisplayMode(mode); });
    };
    addMode(QStringLiteral("Solo linee esterne"), 0);
    addMode(QStringLiteral("Mesh"), 1);
    addMode(QStringLiteral("Mesh + linee esterne"), 2);
    modeMenu->addSeparator();
    QAction *hiddenEdgesAction = modeMenu->addAction(QStringLiteral("Mostra e seleziona i bordi nascosti"));
    hiddenEdgesAction->setCheckable(true);
    hiddenEdgesAction->setToolTip(QStringLiteral("Visualizza gli spigoli attraverso le facce e ne consente la selezione; disattivato facilita la scelta dei contorni esterni"));
    hiddenEdgesAction->setChecked(viewSettings.value(QStringLiteral("view/hiddenEdges"), false).toBool());
    viewport->setHiddenEdgesVisible(hiddenEdgesAction->isChecked());
    connect(hiddenEdgesAction, &QAction::toggled, this, [viewport](bool visible) {
        viewport->setHiddenEdgesVisible(visible);
        QSettings().setValue(QStringLiteral("view/hiddenEdges"), visible);
    });
    QAction *topologyIdsAction = modeMenu->addAction(QStringLiteral("Mostra nomi e ID topologici (facce e bordi)"));
    topologyIdsAction->setCheckable(true);
    topologyIdsAction->setToolTip(QStringLiteral("Mostra il nome persistente e l’ID di facce e bordi con una freccia; clicca un’etichetta per illuminarne il bordo o il contorno"));
    topologyIdsAction->setChecked(viewSettings.value(QStringLiteral("view/topologyIds"), false).toBool());
    viewport->setTopologyIdsVisible(topologyIdsAction->isChecked());
    connect(topologyIdsAction, &QAction::toggled, this, [viewport](bool visible) {
        viewport->setTopologyIdsVisible(visible);
        QSettings().setValue(QStringLiteral("view/topologyIds"), visible);
    });
    QAction *studio = lightingMenu->addAction(QStringLiteral("Studio"));
    QAction *soft = lightingMenu->addAction(QStringLiteral("Morbida"));
    QAction *inspection = lightingMenu->addAction(QStringLiteral("Ispezione"));
    connect(studio, &QAction::triggered, this, [viewport] { viewport->setLightingPreset(0); });
    connect(soft, &QAction::triggered, this, [viewport] { viewport->setLightingPreset(1); });
    connect(inspection, &QAction::triggered, this, [viewport] { viewport->setLightingPreset(2); });
    connect(planesAction, &QAction::toggled, this, [viewport](bool visible) { viewport->setReferencePlanesVisible(visible); });
    connect(wheelZoomAction, &QAction::toggled, this, [viewport](bool enabled) {
        viewport->setWheelZoomEnabled(enabled);
    });
    connect(zoomInAction, &QAction::triggered, this, [viewport] { viewport->zoomIn(); });
    connect(zoomOutAction, &QAction::triggered, this, [viewport] { viewport->zoomOut(); });
    connect(resetZoomAction, &QAction::triggered, this, [viewport] { viewport->resetZoom(); });

    // Vista in sezione: pannello nella finestra (resta aperto mentre si lavora):
    // piano (di riferimento, o un piano di costruzione o una faccia piana scelti
    // nella vista), posizione lungo la normale con il cursore (la sezione si
    // muove dal vivo), lato, riempimento e piano visibile.
    viewMenu->addSeparator();
    // La sezione resta attiva anche chiudendo la finestra dei parametri: si
    // spegne da qui (menu, barra, Ctrl+Shift+X); i parametri si riaprono con l'altra voce.
    QAction *sectionAction = viewMenu->addAction(QStringLiteral("Vista in sezione"));
    sectionAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    sectionAction->setCheckable(true);
    sectionAction->setToolTip(QStringLiteral("Vista in sezione accesa/spenta (i parametri nella finestra Sezione; il piano si trascina dalla freccia)"));
    QAction *sectionParameters = viewMenu->addAction(QStringLiteral("Parametri della sezione..."));
    {
        // Finestra fluttuante come le altre (non modale: la vista resta attiva; si sposta trascinandola).
        auto *panel = new QDialog(this);
        panel->setWindowTitle(QStringLiteral("Sezione"));
        panel->setModal(false);
        auto *form = new QFormLayout(panel);
        auto *planeBox = new QComboBox(panel);
        planeBox->addItems({QStringLiteral("Piano XY"), QStringLiteral("Piano XZ"), QStringLiteral("Piano YZ"), QStringLiteral("Scelto nella vista")});
        auto *pickButton = new QPushButton(QStringLiteral("Scegli nella vista..."), panel);
        pickButton->setToolTip(QStringLiteral("Un piano di riferimento o di costruzione, o una faccia piana"));
        auto *slider = new QSlider(Qt::Horizontal, panel);
        slider->setRange(0, 2000);
        auto *offsetBox = new ForgeCad::ExpressionSpinBox(panel);
        offsetBox->setDecimals(4);
        offsetBox->setRange(-1e6, 1e6);
        offsetBox->setKeyboardTracking(false);
        auto *flipBox = new QCheckBox(QStringLiteral("Inverti il lato"), panel);
        auto *capsBox = new QCheckBox(QStringLiteral("Riempi le sezioni dei solidi"), panel);
        capsBox->setChecked(true);
        auto *planeVisible = new QCheckBox(QStringLiteral("Mostra il piano"), panel);
        planeVisible->setChecked(true);
        auto *info = new QLabel(panel);
        info->setWordWrap(true);
        info->setStyleSheet(QStringLiteral("color: #9fc6e8;"));
        form->addRow(QStringLiteral("Piano:"), planeBox);
        form->addRow(QString(), pickButton);
        form->addRow(QStringLiteral("Posizione:"), slider);
        form->addRow(QString(), offsetBox);
        form->addRow(QString(), flipBox);
        form->addRow(QString(), capsBox);
        form->addRow(QString(), planeVisible);
        form->addRow(info);
        auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, panel);
        QObject::connect(closeButtons, &QDialogButtonBox::rejected, panel, &QDialog::hide);
        form->addRow(closeButtons);
        struct SectionPanelState {
            GeometryRef picked;
            double lo = -10.0, hi = 10.0;
            bool updating = false;
            bool active = false;
        };
        auto state = std::make_shared<SectionPanelState>();
        // Il piano di base (punto e normale) della scelta attuale.
        const auto basePlane = [viewport, planeBox, state](QVector3D &point, QVector3D &normal) {
            const int index = planeBox->currentIndex();
            if (index < 3) {
                point = QVector3D();
                normal = index == 0 ? QVector3D(0, 0, 1) : index == 1 ? QVector3D(0, 1, 0) : QVector3D(1, 0, 0);
                return true;
            }
            if (state->picked.kind < 0) return false;
            ForgeCad::ResolvedRef resolved;
            if (!ForgeCad::resolveGeometryRef(state->picked, int(viewport->extrusions().size()), viewport->sketches(), viewport->extrusions(), resolved, nullptr)
                || !resolved.hasPlane)
                return false;
            point = QVector3D(float(resolved.point.x()), float(resolved.point.y()), float(resolved.point.z()));
            normal = QVector3D(float(resolved.direction.x()), float(resolved.direction.y()), float(resolved.direction.z()));
            return true;
        };
        const auto apply = [viewport, basePlane, offsetBox, flipBox, capsBox, planeVisible, info, planeBox, state] {
            QVector3D point, normal;
            if (!state->active || !basePlane(point, normal)) {
                viewport->setSection(false);
                info->setText(state->active && planeBox->currentIndex() == 3 ? QStringLiteral("Scegli il piano nella vista.") : QString());
                return;
            }
            normal.normalize();
            if (flipBox->isChecked()) normal = -normal;
            const QVector3D at = point + float(offsetBox->value()) * (flipBox->isChecked() ? -normal : normal);
            viewport->setSection(true, at, normal, capsBox->isChecked(), planeVisible->isChecked());
            info->setText(planeBox->currentIndex() == 3 ? ForgeCad::geometryRefText(state->picked, viewport->sketches(), viewport->extrusions())
                                                        : QString());
        };
        // L'intervallo del cursore: la geometria lungo la normale del piano. Per
        // un piano nuovo si toglie la parte verso l'osservatore (la sezione si vede).
        const auto updateRange = [viewport, basePlane, slider, offsetBox, flipBox, state, apply](bool newPlane = false) {
            QVector3D point, normal;
            if (basePlane(point, normal)) {
                if (newPlane) {
                    const QSignalBlocker blocker(flipBox);
                    flipBox->setChecked(QVector3D::dotProduct(normal, viewport->viewerDirection()) < 0.0f);
                }
                viewport->sceneRangeAlong(point, normal, state->lo, state->hi);
                const double margin = 0.02 * std::max(1e-3, state->hi - state->lo);
                state->lo -= margin;
                state->hi += margin;
            }
            state->updating = true;
            const double value = std::clamp(offsetBox->value(), state->lo, state->hi);
            offsetBox->setSingleStep(std::max(1e-4, (state->hi - state->lo) / 100.0));
            offsetBox->setValue(value);
            slider->setValue(int(std::lround((value - state->lo) / std::max(1e-12, state->hi - state->lo) * slider->maximum())));
            state->updating = false;
            apply();
        };
        QObject::connect(slider, &QSlider::valueChanged, panel, [slider, offsetBox, state, apply](int value) {
            if (state->updating) return;
            state->updating = true;
            offsetBox->setValue(state->lo + (state->hi - state->lo) * value / double(slider->maximum()));
            state->updating = false;
            apply();
        });
        QObject::connect(offsetBox, &QDoubleSpinBox::valueChanged, panel, [slider, state, apply](double value) {
            if (state->updating) return;
            state->updating = true;
            slider->setValue(int(std::lround((value - state->lo) / std::max(1e-12, state->hi - state->lo) * slider->maximum())));
            state->updating = false;
            apply();
        });
        const auto pick = [viewport, state, planeBox, info, updateRange] {
            info->setText(QStringLiteral("Clicca un piano o una faccia piana (Esc: annulla)."));
            const QString error = viewport->beginReferencePick(ForgeCad::DatumRolePlane, -1);
            if (!error.isEmpty()) {
                info->setText(error);
                return;
            }
            viewport->setReferencePickCallback([viewport, state, planeBox, updateRange](bool picked, GeometryRef ref) {
                viewport->setReferencePickCallback(nullptr);
                if (picked && (ForgeCad::geometryRefRoles(ref, viewport->sketches()) & ForgeCad::DatumRolePlane)) {
                    state->picked = ref;
                    const QSignalBlocker blocker(planeBox);
                    planeBox->setCurrentIndex(3);
                }
                updateRange(true);
            });
        };
        QObject::connect(pickButton, &QPushButton::clicked, panel, pick);
        QObject::connect(planeBox, &QComboBox::currentIndexChanged, panel, [state, updateRange, pick](int index) {
            if (index == 3 && state->picked.kind < 0) pick();
            else updateRange(true);
        });
        for (QCheckBox *box : {flipBox, capsBox, planeVisible}) QObject::connect(box, &QCheckBox::toggled, panel, apply);
        QObject::connect(sectionAction, &QAction::toggled, panel, [panel, updateRange, offsetBox, state, viewport](bool on) {
            state->active = on;
            if (on) {
                // La prima volta la sezione passa per il centro della geometria e si aprono i parametri.
                static bool first = true;
                updateRange(first);
                if (first) offsetBox->setValue(0.5 * (state->lo + state->hi));
                first = false;
                panel->show();
                panel->raise();
            } else {
                viewport->cancelReferencePick();
                viewport->setSection(false);
                panel->hide();
            }
        });
        QObject::connect(sectionParameters, &QAction::triggered, panel, [panel, sectionAction] {
            if (!sectionAction->isChecked()) sectionAction->setChecked(true);
            panel->show();
            panel->raise();
        });
        auto *offButton = closeButtons->addButton(QStringLiteral("Spegni la sezione"), QDialogButtonBox::DestructiveRole);
        QObject::connect(offButton, &QPushButton::clicked, panel, [sectionAction] { sectionAction->setChecked(false); });
        viewport->setSectionRefreshCallback(apply);  // un piano scelto nella vista segue il suo corpo
        // Trascinando il piano nella vista cambia la posizione (lungo la normale della sezione).
        viewport->setSectionDragCallback([offsetBox, flipBox](double delta) {
            offsetBox->setValue(offsetBox->value() + (flipBox->isChecked() ? -delta : delta));
        });
    }
    connect(lowQuality, &QAction::triggered, this, [viewport] { viewport->setTessellationQuality(0); });
    connect(mediumQuality, &QAction::triggered, this, [viewport] { viewport->setTessellationQuality(1); });
    connect(highQuality, &QAction::triggered, this, [viewport] { viewport->setTessellationQuality(2); });
    connect(extrudeAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->activeSketchIndex() < 0) return;
        const QString name = QStringLiteral("Estrusione %1").arg(viewport->extrusions().size() + 1);
        ExtrusionObject initial;
        initial.sketchIndex = viewport->activeSketchIndex();
        initial.distance = 1.0;
        // Uno schizzo su una faccia di un solido: di default si unisce a lui.
        initial.mergeOperation = viewport->sketches().at(initial.sketchIndex).plane == kFacePlane ? 1 : 0;
        const SketchViewUnlock unlock(viewport);  // la vista si ruota per vedere il verso dell'estrusione
        extrusionDialog(this, viewport, QStringLiteral("Estrusione"), -1, initial,
                        [viewport, name](const ExtrusionObject &values) { return viewport->createExtrusion(values, name); });
    });
    const auto runBoolean = [this, viewport](BooleanOperation operation, const QString &title) {
        ExtrusionObject initial;
        initial.operation = int(operation);
        const SceneSelection selection = viewport->selection();
        initial.firstBody = selection.kind == SceneObjectKind::Extrusion ? selection.index : -1;
        const QString resultName = title + QStringLiteral(" %1").arg(viewport->extrusions().size() + 1);
        booleanDialog(this, viewport, title, -1, initial, [&](const ExtrusionObject &values) {
            return viewport->createBoolean(BooleanOperation(values.operation), values.firstBody, QVector<int>{values.secondBody} + values.booleanTools, resultName);
        });
    };
    // Rivoluzione: schizzo (quello attivo o selezionato), asse (linee di
    // costruzione per prime, poi gli altri segmenti e gli assi del piano), angolo.
    connect(revolveAction, &QAction::triggered, this, [this, viewport] {
        const QVector<SketchObject> &sketches = viewport->sketches();
        if (sketches.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Rivoluzione"), QStringLiteral("Disegna prima uno schizzo con un profilo (chiuso: solido, aperto: superficie) e l'asse (linea di costruzione)."));
            return;
        }
        const SceneSelection selection = viewport->selection();
        ExtrusionObject initial;
        initial.feature = BodyFeature::Revolution;
        initial.sketchIndex = viewport->activeSketchIndex();
        if (selection.kind == SceneObjectKind::Sketch) initial.sketchIndex = selection.index;
        initial.revolveAxis = 0;
        initial.revolveAngle = 360.0;
        // Come lo sweep: con un solido visibile nella scena si parte unendo
        // (automatico: solo quelli toccati; senza contatto resta un corpo nuovo).
        for (const ExtrusionObject &body : viewport->extrusions())
            if (body.visible && body.solid && body.forgeBody) {
                initial.mergeOperation = 1;
                break;
            }
        const QString name = QStringLiteral("Rivoluzione %1").arg(viewport->extrusions().size() + 1);
        const SketchViewUnlock unlock(viewport);  // la vista si ruota per vedere il verso della rivoluzione
        revolutionDialog(this, viewport, QStringLiteral("Rivoluzione"), -1, initial, false, [viewport, name](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = name;
            return viewport->createBody(body);
        });
    });

    // Primitive: piano di riferimento (orientamento), origine e dimensioni.
    const auto runPrimitive = [this, viewport](PrimitiveKind kind, const QString &title) {
        PrimitiveParameters parameters;
        parameters.kind = kind;
        const double defaults[5][3] = {{2.0, 2.0, 2.0}, {1.0, 2.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 0.0, 2.0}, {2.0, 0.5, 0.0}};
        for (int index = 0; index < 3; ++index) parameters.size[index] = defaults[int(kind)][index];
        const QString name = title + QStringLiteral(" %1").arg(viewport->extrusions().size() + 1);
        primitiveDialog(this, parameters, [viewport, name](const PrimitiveParameters &values) { return viewport->createPrimitive(values, name); },
                        {viewport, -1, [](const PrimitiveParameters &values) {
                             ExtrusionObject body;
                             body.feature = BodyFeature::Primitive;
                             body.primitive = values;
                             return body;
                         }});
    };
    const QList<QPair<QString, PrimitiveKind>> primitives = {
        {QStringLiteral("Parallelepipedo"), PrimitiveKind::Box}, {QStringLiteral("Cilindro"), PrimitiveKind::Cylinder},
        {QStringLiteral("Sfera"), PrimitiveKind::Sphere}, {QStringLiteral("Cono"), PrimitiveKind::Cone},
        {QStringLiteral("Toro"), PrimitiveKind::Torus}};
    QList<QAction *> primitiveActions;
    for (const auto &entry : primitives) {
        QAction *action = primitiveMenu->addAction(entry.first + QStringLiteral("..."));
        primitiveActions.append(action);
        const QString title = entry.first;
        const PrimitiveKind kind = entry.second;
        connect(action, &QAction::triggered, this, [runPrimitive, kind, title] { runPrimitive(kind, title); });
    }

    // Raccordi e smussi: il pannello della misura appare subito; mentre resta
    // aperto i clic nella vista aggiornano selezione e anteprima.
    auto *edgePickStatus = new QLabel(this);
    auto *pickSizeLabel = new QLabel(QStringLiteral("Misura:"), this);
    pickSizeBox_ = new ForgeCad::ExpressionSpinBox(this);
    pickSizeBox_->setDecimals(4);
    pickSizeBox_->setRange(0.0001, 100000.0);
    pickSizeBox_->setValue(blendSize_);
    pickSizeBox_->setToolTip(QStringLiteral("Raggio del raccordo o distanza dello smusso per l'anteprima"));
    pickSizeLabel->hide();
    pickSizeBox_->hide();
    connect(pickSizeBox_, &QDoubleSpinBox::valueChanged, this, [viewport](double value) { viewport->setEdgePickSize(value); });
    viewport->setEdgePickCallbacks(
        [edgePickStatus, pickSizeLabel, this, viewport](const QString &text) {
            edgePickStatus->setText(text);
            const bool statusSize = !text.isEmpty() && !viewport->edgePickPanelActive();
            pickSizeLabel->setVisible(statusSize);
            pickSizeBox_->setVisible(statusSize);
        },
        {});
    for (const auto &[action, chamfer] : {std::pair<QAction *, bool>{filletAction, false}, {chamferAction, true}}) {
        const bool isChamfer = chamfer;
        connect(action, &QAction::triggered, this, [this, viewport, isChamfer] {
            viewport->setEdgePickSize(pickSizeBox_->value());
            // Il calcolo resta sospeso fino al primo ciclo eventi del pannello,
            // quindi la finestra compare prima di qualsiasi anteprima.
            const QString error = viewport->beginEdgePick(isChamfer, true);
            if (!error.isEmpty()) {
                QMessageBox::information(this, isChamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo"), error);
                return;
            }
            const QString title = isChamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo");
            const QString name = title + QStringLiteral(" %1").arg(viewport->extrusions().size() + 1);
            const BlendDialogResult result = blendDialog(
                this, viewport, title, viewport->edgePickBody(), -1, viewport->edgePickPoints(), viewport->edgePickSize(), isChamfer, false,
                viewport->edgePickChamferSpec(),
                [viewport, name](int body, const QVector<EdgePoint> &edges, double size, bool chamfer, const ChamferSpec &spec) {
                    return viewport->createBlend(body, edges, size, chamfer, name, spec);
                }, true);
            viewport->setEdgePickChamferSpec(result.spec);
            blendSize_ = result.size;
            viewport->setEdgePickSize(result.size);
            if (pickSizeBox_) pickSizeBox_->setValue(result.size);
        });
    }
    statusBar()->addWidget(edgePickStatus);
    statusBar()->addWidget(pickSizeLabel);
    statusBar()->addWidget(pickSizeBox_);

    for (int kind = 0; kind < 3; ++kind) {
        QAction *action = kind == 0 ? linearPatternAction : kind == 1 ? circularPatternAction : mirrorAction;
        connect(action, &QAction::triggered, this, [this, viewport, kind] {
            ExtrusionObject definition;
            definition.feature = BodyFeature::Pattern;
            definition.pattern.kind = kind;
            const SceneSelection selection = viewport->selection();
            if (selection.kind == SceneObjectKind::Extrusion) definition.firstBody = selection.index;
            patternDialog(this, viewport, kind == 2 ? QStringLiteral("Specchio") : QStringLiteral("Ripetizione"), -1, definition,
                          [viewport](const ExtrusionObject &d) {
                ExtrusionObject body = d;
                body.name = QStringLiteral("%1 %2").arg(d.pattern.kind == 2 ? QStringLiteral("Specchio") : QStringLiteral("Ripetizione"))
                                .arg(viewport->extrusions().size() + 1);
                return viewport->createBody(body, {d.firstBody});
            });
        });
    }
    connect(moveAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        const SceneSelection selection = viewport->selection();
        if (selection.kind == SceneObjectKind::Extrusion) definition.firstBody = selection.index;
        transformDialog(this, viewport, QStringLiteral("Sposta / ruota corpo"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = (d.move.copy ? QStringLiteral("Copia %1") : QStringLiteral("Spostamento %1")).arg(viewport->extrusions().size() + 1);
            body.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(body, d.move.copy ? QVector<int>() : QVector<int>{d.firstBody});
        });
    });
    connect(scaleAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        const SceneSelection selection = viewport->selection();
        if (selection.kind == SceneObjectKind::Extrusion) definition.firstBody = selection.index;
        definition.scaleFactor = 2.0;
        const QString name = QStringLiteral("Scala %1").arg(viewport->extrusions().size() + 1);
        scaleDialog(this, viewport, QStringLiteral("Scala"), -1, definition, [viewport, name](const ExtrusionObject &d) {
            return viewport->createScale(d.firstBody, d.scaleFactor, d.scaleCenterMode, d.scaleCenter, name);
        });
    });
    connect(threadAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        definition.feature = BodyFeature::Thread;
        int selectedBody = -1;
        EdgePoint selectedFace;
        if (viewport->selectedFaceReference(selectedBody, selectedFace)) {
            definition.firstBody = selectedBody;
            definition.thread.face = selectedFace;
        } else {
            const SceneSelection selection = viewport->selection();
            if (selection.kind == SceneObjectKind::Extrusion) definition.firstBody = selection.index;
        }
        threadDialog(this, viewport, QStringLiteral("Filettatura automatica"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Thread ? 1 : 0;
            body.name = QStringLiteral("Filettatura %1").arg(count);
            body.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(body, {d.firstBody});
        });
    });
    // Elica e spirale: la base da un cerchio di uno schizzo nella finestra, o da
    // uno spigolo o una faccia scelti con un clic ("Scegli nella vista...").
    auto pendingHelix = std::make_shared<ExtrusionObject>();
    auto openHelix = std::make_shared<std::function<void(const ExtrusionObject &)>>();
    *openHelix = [this, viewport, pendingHelix](const ExtrusionObject &definition) {
        const HelixDialogResult result = helixDialog(this, viewport, QStringLiteral("Elica e spirale"), -1, definition, true, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = QStringLiteral("%1 %2").arg(d.helix.spiral ? QStringLiteral("Spirale") : QStringLiteral("Elica")).arg(viewport->extrusions().size() + 1);
            return viewport->createBody(body);
        });
        if (!result.reselect) return;
        *pendingHelix = result.definition;
        const QString error = viewport->beginHelixPick();
        if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Elica e spirale"), error);
    };
    viewport->setHelixPickCallback([pendingHelix, openHelix](int body, int source, EdgePoint point) {
        ExtrusionObject definition = *pendingHelix;
        definition.firstBody = body;
        definition.helix.source = source;
        definition.helix.reference = point;
        (*openHelix)(definition);
    });
    connect(helixAction, &QAction::triggered, this, [this, viewport, openHelix, pendingHelix] {
        ExtrusionObject definition;
        definition.feature = BodyFeature::Helix;
        const SceneSelection selection = viewport->selection();
        definition.sketchIndex = selection.kind == SceneObjectKind::Sketch ? selection.index : viewport->activeSketchIndex();
        bool circles = false;
        for (const SketchObject &sketch : viewport->sketches())
            for (const CurveObject &curve : sketch.curves) circles = circles || curve.tool == DrawingTool::Circle || curve.tool == DrawingTool::Arc;
        if (circles) {
            (*openHelix)(definition);
            return;
        }
        // Nessun cerchio negli schizzi: la base si sceglie subito nella vista.
        *pendingHelix = definition;
        const QString error = viewport->beginHelixPick();
        if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Elica e spirale"), error + QStringLiteral("\nOppure disegna un cerchio in uno schizzo."));
    });
    connect(sweepAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        definition.feature = BodyFeature::Sweep;
        const SceneSelection selection = viewport->selection();
        definition.sketchIndex = selection.kind == SceneObjectKind::Sketch ? selection.index : qMax(0, viewport->activeSketchIndex());
        // Una nuova sweep parte come funzione additiva se nella scena esiste
        // gia' un solido visibile. Il probe conserva soltanto quelli realmente
        // toccati; se non ce ne sono il risultato resta un corpo separato.
        for (const ExtrusionObject &body : viewport->extrusions())
            if (body.visible && body.solid && body.forgeBody) {
                definition.mergeOperation = 1;
                break;
            }
        if (selection.kind == SceneObjectKind::Extrusion && selection.index >= 0 && selection.index < viewport->extrusions().size()
            && viewport->extrusions().at(selection.index).feature == BodyFeature::Helix) {
            definition.sweepPath = 1;
            definition.firstBody = selection.index;
        }
        sweepDialog(this, viewport, QStringLiteral("Sweep"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = QStringLiteral("Sweep %1").arg(viewport->extrusions().size() + 1);
            return viewport->createBody(body, d.sweepPathRef.kind < 0 && d.sweepPath == 1 ? QVector<int>{d.firstBody} : QVector<int>{});
        });
    });
    connect(loftAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        definition.feature = BodyFeature::Loft;
        loftDialog(this, viewport, QStringLiteral("Loft"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = QStringLiteral("Loft %1").arg(viewport->extrusions().size() + 1);
            return viewport->createBody(body);
        });
    });
    connect(massAction, &QAction::triggered, this, [this, viewport] { massPropertiesDialog(this, viewport); });
    connect(measureAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        measureDialog(this, viewport);
    });
    connect(datumAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::DatumPlane;
        definition.datum.distance = viewport->suggestedDatumOffset();
        // Con un piano (o un piano di costruzione) selezionato si parte da un parallelo a quello.
        const SceneSelection selection = viewport->selection();
        GeometryRef ref;
        if (selection.kind == SceneObjectKind::Plane && selection.index >= 0 && selection.index < 3) ref.kind = 1, ref.index = selection.index;
        if (selection.kind == SceneObjectKind::Extrusion && selection.index >= 0 && selection.index < viewport->extrusions().size()
            && viewport->extrusions().at(selection.index).feature == BodyFeature::DatumPlane)
            ref.kind = 8, ref.index = selection.index;
        if (ref.kind >= 0) definition.datum.refs = {ref};
        datumDialog(this, viewport, QStringLiteral("Piano di costruzione"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::DatumPlane ? 1 : 0;
            body.name = QStringLiteral("Piano %1").arg(count);
            return viewport->createBody(body);
        });
    });
    // Superfici: taglio (superficie, strumento, parte da tenere) ed estensione
    // (bordi scelti nella vista come gli spigoli dei raccordi, poi la distanza).
    connect(trimSurfaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        // Di partenza il corpo e lo schizzo selezionati: lo schizzo diventa lo strumento (proiettato).
        QVector<SceneSelection> selected = viewport->selectedObjects();
        selected.prepend(viewport->selection());
        for (const SceneSelection &object : selected) {
            if (object.kind == SceneObjectKind::Extrusion && definition.firstBody < 0) definition.firstBody = object.index;
            if (object.kind == SceneObjectKind::Sketch && definition.sketchIndex < 0) {
                definition.sketchIndex = object.index;
                definition.trimProject = true;
            }
        }
        const QString name = QStringLiteral("Taglio %1").arg(viewport->extrusions().size() + 1);
        trimDialog(this, viewport, QStringLiteral("Taglia superficie"), -1, definition, [viewport, name](const ExtrusionObject &d) {
            return viewport->createSheetTrim(d, name);
        });
    });
    auto extendLinear = std::make_shared<bool>(false);
    connect(extendSurfaceAction, &QAction::triggered, this, [this, viewport, extendLinear] {
        viewport->setEdgePickSize(pickSizeBox_->value());
        const QString error = viewport->beginExtendPick(*extendLinear);
        if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Estendi superficie"), error);
    });
    viewport->setExtendPickCallback([this, viewport, extendLinear](int body, QVector<EdgePoint> edges) {
        const QString name = QStringLiteral("Estensione %1").arg(viewport->extrusions().size() + 1);
        const ExtendDialogResult result = extendDialog(this, viewport, QStringLiteral("Estendi superficie"), body, -1, edges, viewport->edgePickSize(),
                                                       *extendLinear, true, [&](const QVector<EdgePoint> &selected, double distance, bool linear) {
                                                           return viewport->createSheetExtend(body, selected, distance, linear, name);
                                                       });
        *extendLinear = result.linear;
        viewport->setEdgePickSize(result.distance);
        if (pickSizeBox_) pickSizeBox_->setValue(result.distance);
        if (result.reselect) viewport->resumeExtendPick(body, result.edges, result.linear);
    });
    for (QAction *action : {copyFacesAction, offsetSurfaceAction}) {
        connect(action, &QAction::triggered, this, [this, viewport, action, copyFacesAction] {
            if (viewport->sketchModeActive()) viewport->endSketchMode();
            ExtrusionObject definition;
            definition.feature = BodyFeature::SurfaceOffset;
            definition.distance = action == copyFacesAction ? 0.0 : 1.0;
            // Di partenza il corpo e la faccia selezionati nella vista.
            int body = -1;
            EdgePoint face;
            if (viewport->selectedFaceReference(body, face)) {
                definition.firstBody = body;
                definition.offsetFaces = {face};
            } else if (viewport->selection().kind == SceneObjectKind::Extrusion) {
                definition.firstBody = viewport->selection().index;
            }
            offsetDialog(this, viewport, action == copyFacesAction ? QStringLiteral("Copia facce") : QStringLiteral("Offset superficie"), -1, definition, [viewport](const ExtrusionObject &d) {
                ExtrusionObject body = d;
                body.name = (d.distance == 0.0 ? QStringLiteral("Copia facce %1") : QStringLiteral("Offset %1")).arg(viewport->extrusions().size() + 1);
                body.plane = viewport->extrusions().value(d.firstBody).plane;
                return viewport->createBody(body);
            });
        });
    }
    connect(draftAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::Draft;
        definition.draftNeutral.kind = 1;
        definition.draftNeutral.index = 0;
        int body = -1;
        EdgePoint face;
        if (viewport->selectedFaceReference(body, face)) { definition.firstBody = body; definition.offsetFaces = {face}; }
        else if (viewport->selection().kind == SceneObjectKind::Extrusion) definition.firstBody = viewport->selection().index;
        draftDialog(this, viewport, QStringLiteral("Sformo facce"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Draft ? 1 : 0;
            result.name = QStringLiteral("Sformo %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(result);
        });
    });
    connect(thickenAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::Thicken;
        definition.distance = 1.0;
        // Di partenza la superficie selezionata (tutta) o il corpo e la faccia selezionati.
        int body = -1;
        EdgePoint face;
        if (viewport->selectedFaceReference(body, face)) {
            definition.firstBody = body;
            const ExtrusionObject &chosen = viewport->extrusions().at(body);
            if (!(chosen.forgeBody && chosen.forgeBody->isSheet())) definition.offsetFaces = {face};
        } else if (viewport->selection().kind == SceneObjectKind::Extrusion) {
            definition.firstBody = viewport->selection().index;
        }
        thickenDialog(this, viewport, QStringLiteral("Spessore"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Thicken ? 1 : 0;
            result.name = QStringLiteral("Spessore %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(result);
        });
    });
    const auto createExtractedCurve = [this, viewport](bool trim) {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition; definition.feature=BodyFeature::ExtractedCurve; definition.sketchIndex=-1;
        definition.curveExtraction.trimmed=trim; definition.curveExtraction.projected=true;
        const auto selection=viewport->selection();
        if(selection.kind==SceneObjectKind::Extrusion && selection.index>=0 && selection.index<viewport->extrusions().size()
            && ForgeCad::isCurveBody(viewport->extrusions().at(selection.index))) {
            auto &source=definition.curveExtraction.source;source.kind=9;source.index=selection.index;
            source.featureId=viewport->extrusions().at(selection.index).featureId;
        }
        extractedCurveDialog(this,viewport,trim?QStringLiteral("Taglia curva 3D"):QStringLiteral("Estrai bordo 3D"),-1,definition,[viewport,trim](const ExtrusionObject &d){
            auto result=d;int count=1;
            for(const auto &body:viewport->extrusions()) count+=body.feature==BodyFeature::ExtractedCurve?1:0;
            result.name=QStringLiteral("%1 %2").arg(trim?QStringLiteral("Curva tagliata 3D"):QStringLiteral("Curva estratta 3D")).arg(count);
            return viewport->createBody(result);
        });
    };
    connect(extractedCurveAction,&QAction::triggered,this,[createExtractedCurve]{createExtractedCurve(false);});
    connect(trimCurve3DAction,&QAction::triggered,this,[createExtractedCurve]{createExtractedCurve(true);});
    connect(projectedCurveAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::ProjectedCurve;
        definition.sketchIndex = -1;
        // Di partenza il corpo e lo schizzo selezionati.
        QVector<SceneSelection> selected = viewport->selectedObjects();
        selected.prepend(viewport->selection());
        for (const SceneSelection &object : selected) {
            if (object.kind == SceneObjectKind::Extrusion) definition.firstBody = object.index;
            if (object.kind == SceneObjectKind::Sketch) definition.sketchIndex = object.index;
        }
        projectionDialog(this, viewport, QStringLiteral("Curva proiettata"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::ProjectedCurve ? 1 : 0;
            result.name = QStringLiteral("Curva proiettata %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(result);
        });
    });
    connect(shellAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::Shell;
        definition.distance = 1.0;
        // Di partenza il solido e la faccia selezionati nella vista (la faccia: l'apertura).
        int body = -1;
        EdgePoint face;
        if (viewport->selectedFaceReference(body, face)) {
            definition.firstBody = body;
            definition.offsetFaces = {face};
        } else if (viewport->selection().kind == SceneObjectKind::Extrusion) {
            definition.firstBody = viewport->selection().index;
        }
        offsetDialog(this, viewport, QStringLiteral("Svuota"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Shell ? 1 : 0;
            result.name = QStringLiteral("Svuota %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(result);
        });
    });
    connect(boundarySurfaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        boundaryDialog(this, viewport, QStringLiteral("Superficie tra curve"), -1, ExtrusionObject(), [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::BoundarySurface ? 1 : 0;
            body.name = QStringLiteral("Superficie tra curve %1").arg(count);
            return viewport->createBody(body);
        });
    });
    connect(fillSurfaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        fillDialog(this, viewport, QStringLiteral("Superficie di riempimento"), -1, ExtrusionObject(), [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::FillSurface ? 1 : 0;
            body.name = QStringLiteral("Riempimento %1").arg(count);
            return viewport->createBody(body);
        });
    });
    connect(deleteFaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::DeleteFace;
        // Di partenza il corpo e la faccia selezionati nella vista.
        int body = -1;
        EdgePoint face;
        if (viewport->selectedFaceReference(body, face)) {
            definition.firstBody = body;
            definition.offsetFaces = {face};
        } else if (viewport->selection().kind == SceneObjectKind::Extrusion) {
            definition.firstBody = viewport->selection().index;
        }
        offsetDialog(this, viewport, QStringLiteral("Elimina facce"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::DeleteFace ? 1 : 0;
            result.name = QStringLiteral("Elimina facce %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createDeleteFaces(result);
        });
    });
    connect(ruledSurfaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ruledDialog(this, viewport, QStringLiteral("Superficie rigata"), -1, ExtrusionObject(), [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Ruled ? 1 : 0;
            body.name = QStringLiteral("Rigata %1").arg(count);
            return viewport->createBody(body);
        });
    });
    connect(planarSurfaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        const SceneSelection selection = viewport->selection();
        definition.sketchIndex = selection.kind == SceneObjectKind::Sketch ? selection.index : viewport->activeSketchIndex();
        planarDialog(this, viewport, QStringLiteral("Superficie planare"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::PlanarSurface ? 1 : 0;
            body.name = QStringLiteral("Superficie planare %1").arg(count);
            return viewport->createBody(body);
        });
    });
    connect(loftSurfaceAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        definition.feature = BodyFeature::Loft;
        definition.loftSurface = true;
        loftDialog(this, viewport, QStringLiteral("Loft di superfici"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = QStringLiteral("Loft %1").arg(viewport->extrusions().size() + 1);
            return viewport->createBody(body);
        });
    });
    connect(sweepSurfaceAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        definition.feature = BodyFeature::Sweep;
        definition.sweepSurface = true;
        const SceneSelection selection = viewport->selection();
        definition.sketchIndex = selection.kind == SceneObjectKind::Sketch ? selection.index : qMax(0, viewport->activeSketchIndex());
        if (selection.kind == SceneObjectKind::Extrusion && selection.index >= 0 && selection.index < viewport->extrusions().size()
            && viewport->extrusions().at(selection.index).feature == BodyFeature::Helix) {
            definition.sweepPath = 1;
            definition.firstBody = selection.index;
        }
        sweepDialog(this, viewport, QStringLiteral("Sweep di superfici"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = QStringLiteral("Sweep %1").arg(viewport->extrusions().size() + 1);
            return viewport->createBody(body, d.sweepPathRef.kind < 0 && d.sweepPath == 1 ? QVector<int>{d.firstBody} : QVector<int>{});
        });
    });
    connect(sewSurfacesAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        sewDialog(this, viewport, QStringLiteral("Cuci superfici"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = QStringLiteral("%1 %2").arg(d.sewSolid ? QStringLiteral("Solido cucito") : QStringLiteral("Cucitura"))
                            .arg(viewport->extrusions().size() + 1);
            body.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(body);
        });
    });

    connect(unionAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Union, QStringLiteral("Unione")); });
    connect(intersectionAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Intersection, QStringLiteral("Intersezione")); });
    connect(differenceAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Difference, QStringLiteral("Differenza")); });

    // Opzioni: kernel geometrico con cui si costruiscono i corpi. La scelta
    // resta per gli avvii successivi (QSettings).
    auto *optionsMenu = addCadMenu(menuBar(), QStringLiteral("Opzioni"));
    auto *unitsMenu = addCadMenu(optionsMenu, QStringLiteral("Unita' di misura del documento"));
    auto *unitsGroup = new QActionGroup(unitsMenu);
    unitsGroup->setExclusive(true);
    const QList<LengthUnit> units = {LengthUnit::Millimeter, LengthUnit::Centimeter, LengthUnit::Meter,
                                     LengthUnit::Inch, LengthUnit::Foot};
    for (LengthUnit unit : units) {
        QAction *action = unitsMenu->addAction(QStringLiteral("%1 (%2)")
            .arg(ForgeCad::lengthUnitName(unit), ForgeCad::lengthUnitSymbol(unit)));
        action->setCheckable(true);
        action->setData(int(unit));
        unitsGroup->addAction(action);
    }
    connect(unitsMenu, &QMenu::aboutToShow, this, [viewport, unitsGroup] {
        for (QAction *action : unitsGroup->actions()) action->setChecked(action->data().toInt() == int(viewport->lengthUnit()));
    });
    connect(unitsGroup, &QActionGroup::triggered, this, [this, viewport](QAction *action) {
        const LengthUnit unit = LengthUnit(action->data().toInt());
        viewport->setLengthUnit(unit);
        QSettings().setValue(QStringLiteral("units/defaultLength"), int(unit));
        statusBar()->showMessage(QStringLiteral("Unita' del documento: %1. Geometria e storia convertite in visualizzazione senza ricalcolo.")
                                     .arg(ForgeCad::lengthUnitName(unit)), 7000);
    });
    optionsMenu->addSeparator();
    // Orientamento degli assi del documento: quale asse sta in alto e quale guarda
    // l'osservatore nella vista frontale (o la vista corrente come frontale).
    QAction *orientationAction = optionsMenu->addAction(QStringLiteral("Orientamento degli assi..."));
    connect(orientationAction, &QAction::triggered, this, [this, viewport] {
        static const QStringList axisNames = {QStringLiteral("+X"), QStringLiteral("-X"), QStringLiteral("+Y"),
                                              QStringLiteral("-Y"), QStringLiteral("+Z"), QStringLiteral("-Z")};
        const auto axisVector = [](int index, double out[3]) {
            for (int k = 0; k < 3; ++k) out[k] = 0.0;
            out[index / 2] = index % 2 ? -1.0 : 1.0;
        };
        const auto axisIndex = [](const double v[3]) {
            int best = 0;
            for (int k = 1; k < 3; ++k)
                if (std::abs(v[k]) > std::abs(v[best])) best = k;
            return 2 * best + (v[best] < 0.0 ? 1 : 0);
        };
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Orientamento degli assi"));
        auto *form = new QFormLayout(&dialog);
        auto *upBox = new QComboBox(&dialog), *towardBox = new QComboBox(&dialog);
        upBox->addItems(axisNames);
        form->addRow(QStringLiteral("Asse in alto sullo schermo:"), upBox);
        form->addRow(QStringLiteral("Asse verso l'osservatore (vista frontale):"), towardBox);
        auto *rightLabel = new QLabel(&dialog);
        form->addRow(QStringLiteral("Asse a destra:"), rightLabel);
        const auto fillToward = [=](int keep) {
            const QSignalBlocker blocker(towardBox);
            towardBox->clear();
            for (int k = 0; k < 6; ++k)
                if (k / 2 != upBox->currentIndex() / 2) towardBox->addItem(axisNames.at(k), k);
            const int found = towardBox->findData(keep);
            towardBox->setCurrentIndex(found >= 0 ? found : 0);
        };
        const auto current = [=] {
            AxesOrientation o;
            axisVector(upBox->currentIndex(), o.up);
            axisVector(towardBox->currentData().toInt(), o.toward);
            const double *u = o.up, *t = o.toward;
            o.right[0] = u[1] * t[2] - u[2] * t[1];
            o.right[1] = u[2] * t[0] - u[0] * t[2];
            o.right[2] = u[0] * t[1] - u[1] * t[0];
            return o;
        };
        const auto describe = [=] { rightLabel->setText(axisNames.at(axisIndex(current().right))); };
        const auto load = [=](const AxesOrientation &o) {
            upBox->setCurrentIndex(axisIndex(o.up));
            fillToward(axisIndex(o.toward));
            describe();
        };
        connect(upBox, &QComboBox::currentIndexChanged, &dialog, [=] {
            fillToward(towardBox->currentData().toInt());
            describe();
        });
        connect(towardBox, &QComboBox::currentIndexChanged, &dialog, describe);
        load(viewport->orientation());
        auto *presets = new QHBoxLayout;
        auto *zUp = new QPushButton(QStringLiteral("Z in alto"), &dialog), *yUp = new QPushButton(QStringLiteral("Y in alto"), &dialog);
        auto *fromView = new QPushButton(QStringLiteral("La vista corrente come frontale"), &dialog);
        fromView->setToolTip(QStringLiteral("Gli assi restano girati come li vedi ora: questa diventa la vista frontale"));
        presets->addWidget(zUp);
        presets->addWidget(yUp);
        presets->addWidget(fromView);
        form->addRow(presets);
        AxesOrientation yUpOrientation;
        yUpOrientation.up[0] = 0.0, yUpOrientation.up[1] = 1.0, yUpOrientation.up[2] = 0.0;
        yUpOrientation.toward[0] = 0.0, yUpOrientation.toward[1] = 0.0, yUpOrientation.toward[2] = 1.0;
        connect(zUp, &QPushButton::clicked, &dialog, [=] { load(AxesOrientation()); });
        connect(yUp, &QPushButton::clicked, &dialog, [=] { load(yUpOrientation); });
        bool useView = false;
        connect(fromView, &QPushButton::clicked, &dialog, [&dialog, &useView] {
            useView = true;
            dialog.accept();
        });
        auto *asDefault = new QCheckBox(QStringLiteral("Predefinito per i documenti nuovi"), &dialog);
        asDefault->setChecked(true);
        form->addRow(QString(), asDefault);
        form->addRow(wrappedNote(QStringLiteral("Cambia solo come si vedono gli assi (viste standard, orbita, griglia) e gli assi\n"
                                               "degli schizzi nuovi sui piani; la geometria del modello resta la stessa."), &dialog));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted) return;
        const AxesOrientation chosen = useView ? viewport->orientationFromCurrentView() : current();
        viewport->setOrientation(chosen);
        if (useView) viewport->setViewPreset(0);
        if (asDefault->isChecked()) saveDefaultAxesOrientation(chosen);
        documentModified_ = true;
        updateWindowTitle();
    });
    optionsMenu->addSeparator();
    // Copia dei corpi calcolati nei documenti (formato 14): file piu' grandi,
    // apertura senza ricalcolare le funzioni. In QSettings, di default si'.
    QAction *saveBodiesAction = optionsMenu->addAction(QStringLiteral("Salva nel documento i corpi calcolati"));
    saveBodiesAction->setCheckable(true);
    saveBodiesAction->setChecked(QSettings().value(QStringLiteral("document/saveBodies"), true).toBool());
    saveBodiesAction->setToolTip(QStringLiteral("Il file e' piu' grande, ma all'apertura i corpi non si ricalcolano"));
    connect(saveBodiesAction, &QAction::toggled, this, [](bool enabled) { QSettings().setValue(QStringLiteral("document/saveBodies"), enabled); });
    // Tassellazione di display con CUDA (cad_cuda_tessellation): i punti delle
    // superfici B-spline si valutano sulla GPU; vale dalla prossima tassellazione.
    QAction *cudaTessellationAction = optionsMenu->addAction(QStringLiteral("Tassellazione con CUDA"));
    cudaTessellationAction->setCheckable(true);
    cudaTessellationAction->setEnabled(ForgeCad::cudaTessellationAvailable());
    cudaTessellationAction->setChecked(ForgeCad::cudaTessellationAvailable() &&
                                       QSettings().value(QStringLiteral("view/cudaTessellation"), true).toBool());
    cudaTessellationAction->setToolTip(ForgeCad::cudaTessellationAvailable()
        ? QStringLiteral("Valuta sulla GPU i punti delle superfici B-spline durante la tassellazione (solo visualizzazione)")
        : QStringLiteral("Nessuna GPU CUDA disponibile: la tassellazione resta sulla CPU"));
    ForgeCad::setCudaTessellationEnabled(cudaTessellationAction->isChecked());
    connect(cudaTessellationAction, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("view/cudaTessellation"), enabled);
        ForgeCad::setCudaTessellationEnabled(enabled);
        if (auto *label = findChild<QLabel *>(QStringLiteral("gpuStatus"))) label->setText(gpuStatusText());
    });
    // Tasto per il pan (da tenere premuto trascinando con il sinistro), in QSettings.
    QAction *panKeyAction = optionsMenu->addAction(QStringLiteral("Tasto per il pan..."));
    const auto panKeyName = [](int key) { return QKeySequence(key).toString(QKeySequence::NativeText); };
    viewport->setPanKey(QSettings().value(QStringLiteral("view/panKey"), int(Qt::Key_Space)).toInt());
    connect(panKeyAction, &QAction::triggered, this, [this, viewport, panKeyName] {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Tasto per il pan"));
        auto *form = new QFormLayout(&dialog);
        form->addRow(wrappedNote(QStringLiteral("Tieni premuto il tasto e trascina con il tasto sinistro per spostare la vista\n"
                                               "(anche con il tasto centrale del mouse). Attuale: %1").arg(panKeyName(viewport->panKey())), &dialog));
        auto *edit = new QKeySequenceEdit(&dialog);
        edit->setMaximumSequenceLength(1);
        form->addRow(QStringLiteral("Nuovo tasto:"), edit);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog,
                [edit] { edit->setKeySequence(QKeySequence(Qt::Key_Space)); });
        form->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted || edit->keySequence().isEmpty()) return;
        // Solo il tasto, senza modificatori (si tiene premuto mentre si trascina).
        const int key = edit->keySequence()[0].key();
        viewport->setPanKey(key);
        QSettings().setValue(QStringLiteral("view/panKey"), key);
        statusBar()->showMessage(QStringLiteral("Pan: tieni premuto %1 e trascina").arg(panKeyName(key)), 5000);
    });
    auto *sketchMenu = addCadMenu(menuBar(), QStringLiteral("Schizzo"));
    QAction *newSketchAction = sketchMenu->addAction(QStringLiteral("Nuovo schizzo..."));
    QAction *spatialSketchAction=sketchMenu->addAction(QStringLiteral("Nuovo schizzo 3D..."));
    spatialSketchAction->setToolTip(QStringLiteral("Disegna polilinee e spline nello spazio con punti XYZ liberi"));
    connect(spatialSketchAction,&QAction::triggered,this,[this,viewport]{
        if(viewport->sketchModeActive())viewport->endSketchMode();
        ExtrusionObject definition;definition.feature=BodyFeature::SpatialSketch;
        spatialSketchDialog(this,viewport,QStringLiteral("Nuovo schizzo 3D"),-1,definition,[viewport](const ExtrusionObject &d){
            auto result=d;int count=1;for(const auto &b:viewport->extrusions())count+=b.feature==BodyFeature::SpatialSketch?1:0;
            result.name=QStringLiteral("Schizzo 3D %1").arg(count);return viewport->createBody(result);
        });
    });
    QAction *faceSketchAction = sketchMenu->addAction(QStringLiteral("Nuovo schizzo sulla faccia selezionata"));
    faceSketchAction->setToolTip(QStringLiteral("Clic su una faccia piana di un corpo, poi questo comando (anche dal menu contestuale della faccia)"));
    connect(faceSketchAction, &QAction::triggered, this, [this, viewport] {
        const QString error = viewport->createSketchOnSelectedFace(QStringLiteral("Schizzo %1").arg(viewport->sketches().size() + 1));
        if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Schizzo sulla faccia"), error);
    });
    QAction *sketchAction = sketchMenu->addAction(QStringLiteral("Disegna segmenti"));
    auto *toolMenu = addCadMenu(sketchMenu, QStringLiteral("Strumento geometrico"));
    auto *toolGroup = new QActionGroup(this); toolGroup->setExclusive(true);
    auto addTool = [this, toolMenu, toolGroup, viewport](const QString &text, DrawingTool tool, bool checked) {
        auto *action = toolMenu->addAction(text); action->setCheckable(true); action->setChecked(checked); toolGroup->addAction(action);
        action->setData(int(tool));
        connect(action, &QAction::triggered, this, [viewport, tool] { viewport->setDrawingTool(tool); });
        return action;
    };
    QAction *selectTool = addTool(QStringLiteral("Selezione"), DrawingTool::Select, true);
    selectTool->setToolTip(QStringLiteral("Selezione (Esc): clic seleziona, Maiusc+clic aggiunge, trascina i punti delle curve"));
    viewport->setToolChangedCallback([toolGroup](DrawingTool tool) {
        for (QAction *action : toolGroup->actions())
            if (action->data().isValid() && action->data().toInt() == int(tool)) action->setChecked(true);
    });
    QAction *lineTool = addTool(QStringLiteral("Linea"), DrawingTool::Line, false);
    QAction *polylineTool = addTool(QStringLiteral("Polilinea"), DrawingTool::Polyline, false);
    QAction *splineTool = addTool(QStringLiteral("Spline"), DrawingTool::Spline, false);
    auto *splineOptionsMenu = addCadMenu(toolMenu, QStringLiteral("Opzioni spline"));
    auto *uniformSpline = splineOptionsMenu->addAction(QStringLiteral("Uniforma la curvatura (C2)"));
    auto *relaxedSpline = splineOptionsMenu->addAction(QStringLiteral("Rilassa la curva"));
    auto *limitedSpline = splineOptionsMenu->addAction(QStringLiteral("Limita le oscillazioni"));
    uniformSpline->setToolTip(QStringLiteral("Curvatura continua tra i tratti; i punti di passaggio restano fissi"));
    relaxedSpline->setToolTip(QStringLiteral("Accorcia le tangenti per ridurre le gobbe; combinabile con le altre spunte, senza garanzia C2"));
    limitedSpline->setToolTip(QStringLiteral("Evita inversioni di X/Y nei singoli tratti; non elimina flessi necessari e non garantisce C2"));
    for (QAction *option : {uniformSpline, relaxedSpline, limitedSpline}) {
        option->setCheckable(true);
        connect(option, &QAction::toggled, this, [=] {
            viewport->setSplineShapeOptions({uniformSpline->isChecked(), relaxedSpline->isChecked(), limitedSpline->isChecked()});
        });
    }
    uniformSpline->setChecked(true);
    splineTool->setToolTip(QStringLiteral("Spline per punti, continuita' C2 automatica. Spunte in Schizzo > Strumento geometrico > Opzioni spline; "
                                        "per curve esistenti: Selezione, clic destro > Modifica punti e maniglie"));
    QAction *nurbsTool = addTool(QStringLiteral("NURBS"), DrawingTool::Nurbs, false);
    QAction *circleTool = addTool(QStringLiteral("Cerchio"), DrawingTool::Circle, false);
    QAction *arcTool = addTool(QStringLiteral("Arco (centro, inizio, fine)"), DrawingTool::Arc, false);
    QAction *threePointArcTool = addTool(QStringLiteral("Arco per tre punti"), DrawingTool::ThreePointArc, false);
    threePointArcTool->setToolTip(QStringLiteral("Arco per tre punti: inizio, fine, poi un punto dell'arco"));
    QAction *tangentArcTool = addTool(QStringLiteral("Arco tangente"), DrawingTool::TangentArc, false);
    tangentArcTool->setToolTip(QStringLiteral("Arco tangente: clic sull'estremo di un segmento o di un arco, poi la fine"));
    QAction *polygonTool = addTool(QStringLiteral("Poligono"), DrawingTool::Polygon, false);
    QAction *rectangleTool = addTool(QStringLiteral("Rettangolo (due angoli)"), DrawingTool::Rectangle, false);
    rectangleTool->setToolTip(QStringLiteral("Rettangolo: due angoli opposti (Maiusc: quadrato); quattro segmenti orizzontali e verticali"));
    QAction *centerRectangleTool = addTool(QStringLiteral("Rettangolo dal centro"), DrawingTool::CenterRectangle, false);
    centerRectangleTool->setToolTip(QStringLiteral("Rettangolo: centro e un angolo (Maiusc: quadrato)"));
    QAction *ellipseTool = addTool(QStringLiteral("Ellisse"), DrawingTool::Ellipse, false);
    ellipseTool->setToolTip(QStringLiteral("Ellisse: centro, estremo del primo semiasse, punto per il secondo semiasse"));
    QAction *constructionTool = addTool(QStringLiteral("Linea di costruzione"), DrawingTool::ConstructionLine, false);
    QAction *polygonSidesAction = toolMenu->addAction(QStringLiteral("Numero lati poligono..."));
    auto *editToolMenu = addCadMenu(sketchMenu, QStringLiteral("Modifica entita'"));
    auto addEditTool = [this, editToolMenu, toolGroup, viewport](const QString &text, DrawingTool tool, Qt::Key key) {
        auto *action = editToolMenu->addAction(text); action->setCheckable(true); toolGroup->addAction(action);
        action->setData(int(tool));
        action->setShortcut(QKeySequence(key));
        connect(action, &QAction::triggered, this, [viewport, tool] { viewport->setDrawingTool(tool); });
        return action;
    };
    QAction *trimTool = addEditTool(QStringLiteral("Taglia"), DrawingTool::Trim, Qt::Key_T);
    QAction *extendTool = addEditTool(QStringLiteral("Estendi"), DrawingTool::Extend, Qt::Key_E);
    QAction *splitTool = addEditTool(QStringLiteral("Spezza"), DrawingTool::Split, Qt::Key_S);
    QAction *sketchFilletTool = addEditTool(QStringLiteral("Raccordo..."), DrawingTool::Fillet, Qt::Key_R);
    QAction *sketchChamferTool = addEditTool(QStringLiteral("Smusso..."), DrawingTool::Chamfer, Qt::Key_M);
    // Raggio o distanza chiesti quando si sceglie lo strumento.
    auto askBlendSize = [this, viewport](bool chamfer) {
        bool accepted = false;
        const double size = ForgeCad::getDouble(this, chamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo"),
            chamfer ? QStringLiteral("Distanza dello smusso dallo spigolo:") : QStringLiteral("Raggio del raccordo:"),
            viewport->sketchBlendSize(chamfer), 0.000001, 100000.0, 6, &accepted);
        if (accepted) viewport->setSketchBlendSize(chamfer, size);
        statusBar()->showMessage(chamfer
            ? QStringLiteral("Smusso: clic su uno spigolo tra due segmenti, oppure sui due segmenti (sulla parte da tenere)")
            : QStringLiteral("Raccordo: clic su uno spigolo tra due segmenti, oppure sui due segmenti (sulla parte da tenere)"), 8000);
    };
    connect(sketchFilletTool, &QAction::triggered, this, [askBlendSize] { askBlendSize(false); });
    connect(sketchChamferTool, &QAction::triggered, this, [askBlendSize] { askBlendSize(true); });
    viewport->setStatusCallback([this](const QString &message) {
        if (message.isEmpty()) statusBar()->clearMessage();
        else statusBar()->showMessage(message, 6000);
    });
    sketchAction->setCheckable(true); sketchAction->setChecked(true);
    QAction *snapAction = sketchMenu->addAction(QStringLiteral("Snap alla geometria"));
    snapAction->setCheckable(true); snapAction->setChecked(QSettings().value(QStringLiteral("sketch/geometrySnap"), true).toBool());
    viewport->setGeometrySnap(snapAction->isChecked());
    QAction *gridSnapAction = sketchMenu->addAction(QStringLiteral("Snap alla griglia"));
    gridSnapAction->setCheckable(true); gridSnapAction->setChecked(QSettings().value(QStringLiteral("sketch/gridSnap"), true).toBool());
    viewport->setGridSnap(gridSnapAction->isChecked());
    QAction *originSnapAction = sketchMenu->addAction(QStringLiteral("Snap all'origine"));
    originSnapAction->setCheckable(true); originSnapAction->setChecked(QSettings().value(QStringLiteral("sketch/originSnap"), true).toBool());
    viewport->setOriginSnap(originSnapAction->isChecked());
    connect(originSnapAction, &QAction::toggled, this, [viewport](bool enabled) {
        viewport->setOriginSnap(enabled);
        QSettings().setValue(QStringLiteral("sketch/originSnap"), enabled);
    });
    QAction *automaticConstraint = sketchMenu->addAction(QStringLiteral("Vincolo automatico"));
    QAction *freeConstraint = sketchMenu->addAction(QStringLiteral("Nessun vincolo (linea libera)"));
    QAction *horizontalConstraint = sketchMenu->addAction(QStringLiteral("Vincolo orizzontale"));
    QAction *verticalConstraint = sketchMenu->addAction(QStringLiteral("Vincolo verticale"));
    QAction *lengthConstraint = sketchMenu->addAction(QStringLiteral("Quota lunghezza..."));
    QAction *angleConstraint = sketchMenu->addAction(QStringLiteral("Quota angolare..."));
    // Quota: prima si scelgono le entita', poi si mette la quota (orizzontale,
    // verticale o obliqua secondo il puntatore, o con H/V/O) e se ne da' il valore.
    QAction *dimensionAction = sketchMenu->addAction(QStringLiteral("Quota (scegli le entita', poi mettila)"));
    dimensionAction->setShortcut(QKeySequence(Qt::Key_D));
    dimensionAction->setCheckable(true);
    dimensionAction->setData(int(DrawingTool::Dimension));
    toolGroup->addAction(dimensionAction);
    dimensionAction->setToolTip(QStringLiteral("Quota (D): clic sulle entita' da quotare (punti, segmenti, cerchi, archi), poi clic nel vuoto per metterla;\n"
                                               "tra due punti e' orizzontale, verticale o obliqua secondo il puntatore (H, V, O fissano l'orientamento, A automatico)"));
    connect(dimensionAction, &QAction::triggered, this, [this, viewport] {
        const QString error = viewport->beginDimensionTool();
        if (!error.isEmpty()) statusBar()->showMessage(error, 5000);
    });
    QAction *editDimensionAction = sketchMenu->addAction(QStringLiteral("Modifica quota del segmento, cerchio, arco, poligono... (doppio clic)"));
    connect(editDimensionAction, &QAction::triggered, this, [this, viewport] {
        const QString error = viewport->editSegmentDimension();
        if (!error.isEmpty()) statusBar()->showMessage(error, 5000);
    });
    QAction *sketchOffsetAction = sketchMenu->addAction(QStringLiteral("Offset entita'..."));
    sketchOffsetAction->setToolTip(QStringLiteral("Copia a distanza costante le entita' selezionate (le catene restano collegate)"));
    connect(sketchOffsetAction, &QAction::triggered, this, [this, viewport] { sketchOffsetDialog(this, viewport); });
    QAction *sketchMoveAction = sketchMenu->addAction(QStringLiteral("Sposta / ruota / copia entita'..."));
    sketchMoveAction->setToolTip(QStringLiteral("Sposta, ruota o copia le entita' selezionate dello schizzo"));
    connect(sketchMoveAction, &QAction::triggered, this, [this, viewport] { sketchMoveDialog(this, viewport); });
    auto *sketchPatternMenu = addCadMenu(sketchMenu, QStringLiteral("Ripetizione"));
    QAction *sketchLinearPattern = sketchPatternMenu->addAction(QStringLiteral("Ripetizione lineare..."));
    QAction *sketchCircularPattern = sketchPatternMenu->addAction(QStringLiteral("Ripetizione circolare..."));
    QAction *sketchMirror = sketchPatternMenu->addAction(QStringLiteral("Specchio..."));
    sketchLinearPattern->setToolTip(QStringLiteral("Copie delle entita' selezionate lungo una o due direzioni"));
    sketchCircularPattern->setToolTip(QStringLiteral("Copie delle entita' selezionate attorno a un centro"));
    sketchMirror->setToolTip(QStringLiteral("Immagine speculare delle entita' selezionate rispetto a un asse o a un segmento"));
    for (int kind = 0; kind < 3; ++kind) {
        QAction *action = kind == 0 ? sketchLinearPattern : kind == 1 ? sketchCircularPattern : sketchMirror;
        connect(action, &QAction::triggered, this, [this, viewport, kind] { sketchPatternDialog(this, viewport, kind); });
    }
    // Riferimenti esterni: spigoli e curve dei corpi, sezioni dei solidi nel piano dello schizzo.
    auto *referenceMenu = addCadMenu(sketchMenu, QStringLiteral("Riferimenti esterni"));
    auto *axisReferences = addCadMenu(referenceMenu, QStringLiteral("Assi del modello"));
    for (int axis = 0; axis < 3; ++axis) {
        QAction *action = axisReferences->addAction(QStringLiteral("Asse %1").arg(QStringLiteral("XYZ").at(axis)));
        connect(action, &QAction::triggered, this, [viewport, axis] { viewport->selectSketchReference(2, axis); });
    }
    auto *planeReferences = addCadMenu(referenceMenu, QStringLiteral("Piani di riferimento"));
    for (int plane = 0; plane < 3; ++plane) {
        QAction *action = planeReferences->addAction(plane == 0 ? QStringLiteral("XY") : plane == 1 ? QStringLiteral("XZ") : QStringLiteral("YZ"));
        connect(action, &QAction::triggered, this, [viewport, plane] { viewport->selectSketchReference(1, plane); });
    }
    referenceMenu->addSeparator();
    QAction *convertTool = referenceMenu->addAction(QStringLiteral("Converti spigoli e curve"));
    convertTool->setCheckable(true);
    toolGroup->addAction(convertTool);
    convertTool->setData(int(DrawingTool::ConvertEdges));
    convertTool->setToolTip(QStringLiteral("Clic su uno spigolo, una curva o un'entita' di un altro schizzo visibile: proietta lungo la normale al piano dello schizzo attivo"));
    connect(convertTool, &QAction::triggered, this, [viewport] { viewport->setDrawingTool(DrawingTool::ConvertEdges); });
    QAction *sectionReferences = referenceMenu->addAction(QStringLiteral("Sezione di solidi e superfici nel piano dello schizzo"));
    sectionReferences->setToolTip(QStringLiteral("Le curve esatte in cui il piano dello schizzo interseca i solidi e le superfici visibili"));
    connect(sectionReferences, &QAction::triggered, this, [this, viewport] {
        int added = 0;
        const QString error = viewport->addSectionReferences(&added);
        statusBar()->showMessage(error.isEmpty() ? QStringLiteral("Sezione aggiunta allo schizzo: %1 entita'.").arg(added) : error, 8000);
    });
    QAction *sketchContacts = referenceMenu->addAction(QStringLiteral("Contatti con gli schizzi visibili"));
    sketchContacts->setToolTip(QStringLiteral("Aggiunge punti fissi dove gli altri schizzi attraversano il piano; se sono complanari, aggiunge le loro curve di costruzione"));
    connect(sketchContacts, &QAction::triggered, this, [this, viewport] {
        int added = 0;
        const QString error = viewport->addSketchContactReferences(&added);
        statusBar()->showMessage(error.isEmpty() ? QStringLiteral("Riferimenti di contatto aggiunti: %1 entita'.").arg(added) : error, 8000);
    });
    referenceMenu->addSeparator();
    QAction *referencesConstruction = referenceMenu->addAction(QStringLiteral("Riferimenti come entita' di costruzione"));
    referencesConstruction->setCheckable(true);
    referencesConstruction->setChecked(true);
    referencesConstruction->setToolTip(QStringLiteral("Spento: i riferimenti entrano nei profili (per estruderli)"));
    connect(referencesConstruction, &QAction::toggled, this, [viewport](bool on) { viewport->setReferencesConstruction(on); });
    QAction *symmetryAxisAction = sketchMenu->addAction(QStringLiteral("Asse di simmetria on/off per i segmenti selezionati"));
    symmetryAxisAction->setToolTip(QStringLiteral("Linea d'asse per le simmetrie e le quote di raggio e diametro (le linee di costruzione lo diventano anche quando le si usa come asse di una quota)"));
    connect(symmetryAxisAction, &QAction::triggered, this, [this, viewport] {
        const QString error = viewport->toggleSymmetryAxis();
        if (!error.isEmpty()) statusBar()->showMessage(error, 5000);
    });
    QAction *toggleConstruction = sketchMenu->addAction(QStringLiteral("Costruzione on/off per le entita' selezionate"));
    toggleConstruction->setShortcut(QKeySequence(Qt::Key_C));
    connect(toggleConstruction, &QAction::triggered, this, [this, viewport] {
        const QString error = viewport->toggleConstruction();
        if (!error.isEmpty()) statusBar()->showMessage(error, 5000);
    });
    // Finestra fluttuante dei vincoli: quelli possibili per le entita' scelte
    // (clic = vincolo) e l'elenco dei vincoli dello schizzo o della selezione.
    // Pannello dentro la finestra: ricorda posizione e dimensione (FloatingPanel).
    auto *constraintPanel = new FloatingPanel(this, QStringLiteral("Vincoli"), QStringLiteral("view/constraintPanel"), QSize(380, 520));
    auto *panelLayout = new QVBoxLayout(constraintPanel->content());
    auto *selectionLabel = new QLabel(constraintPanel);
    selectionLabel->setWordWrap(true);
    panelLayout->addWidget(selectionLabel);
    auto *addBox = new QWidget(constraintPanel);
    auto *addLayout = new QGridLayout(addBox);
    addLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->addWidget(addBox);
    auto *panelError = new QLabel(constraintPanel);
    panelError->setWordWrap(true);
    panelError->setStyleSheet(QStringLiteral("color: #ff7b72;"));
    panelError->hide();
    panelLayout->addWidget(panelError);
    auto *onlySelection = new QCheckBox(QStringLiteral("Solo i vincoli delle entita' scelte"), constraintPanel);
    panelLayout->addWidget(onlySelection);
    auto *constraintList = new QListWidget(constraintPanel);
    constraintList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    constraintList->setToolTip(QStringLiteral("Selezione = vincolo evidenziato nella vista; doppio clic su una quota = valore; Canc = elimina"));
    panelLayout->addWidget(constraintList, 1);
    auto *panelButtons = new QHBoxLayout;
    auto *editValue = new QPushButton(QStringLiteral("Valore..."), constraintPanel);
    auto *removeConstraint = new QPushButton(QStringLiteral("Elimina"), constraintPanel);
    panelButtons->addWidget(editValue);
    panelButtons->addStretch(1);
    panelButtons->addWidget(removeConstraint);
    panelLayout->addLayout(panelButtons);
    auto *showGlyphs = new QCheckBox(QStringLiteral("Mostra i vincoli nella vista"), constraintPanel);
    showGlyphs->setChecked(true);
    panelLayout->addWidget(showGlyphs);
    auto *panelHelp = new QLabel(QStringLiteral("Scegli le entita' con il clic (Maiusc: aggiunge) e i punti con Ctrl+clic (anche l'origine), "
                                                "poi il vincolo. Un clic sul simbolo di un vincolo nella vista lo seleziona, Canc lo elimina."),
                                 constraintPanel);
    panelHelp->setWordWrap(true);
    panelHelp->setStyleSheet(QStringLiteral("color: #8aa0b4;"));
    panelLayout->addWidget(panelHelp);
    auto refreshingPanel = std::make_shared<bool>(false);
    auto refreshPanel = std::make_shared<std::function<void()>>();
    *refreshPanel = [=] {
        if (*refreshingPanel) return;
        *refreshingPanel = true;
        const SketchObject *sketch = viewport->activeSketchObject();
        while (QLayoutItem *item = addLayout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        constraintList->clear();
        const bool active = sketch != nullptr;
        addBox->setEnabled(active);
        constraintList->setEnabled(active);
        if (!active) {
            selectionLabel->setText(QStringLiteral("Entra in uno schizzo per definirne i vincoli."));
            *refreshingPanel = false;
            return;
        }
        const QVector<ConstraintRef> refs = viewport->constraintSelection();
        QStringList names;
        for (const ConstraintRef &ref : refs) names.append(ForgeCad::describeRef(*sketch, ref));
        const ForgeCad::SketchAnalysis &analysis = viewport->sketchAnalysis();
        const QString freedom = analysis.fullyDefined()
            ? QStringLiteral("<b>Schizzo completamente definito</b> (0 gradi di liberta')")
            : QStringLiteral("<b>Gradi di liberta': %1</b> (sotto definito: le entita' definite sono bianche)").arg(analysis.degreesOfFreedom);
        selectionLabel->setText(freedom + QStringLiteral("<br>") + (refs.isEmpty() ? QStringLiteral("Nessuna entita' scelta.")
                                                                                   : QStringLiteral("Scelti: ") + names.join(QStringLiteral(", ")).toHtmlEscaped()));
        const QVector<ConstraintType> types = ForgeCad::applicableConstraints(*sketch, refs);
        int column = 0, row = 0;
        for (ConstraintType type : types) {
            auto *button = new QPushButton(ForgeCad::constraintSymbol(type) + QLatin1Char(' ') + ForgeCad::constraintName(type), addBox);
            connect(button, &QPushButton::clicked, constraintPanel, [=] {
                const QString error = viewport->addConstraint(type);
                panelError->setText(error);
                panelError->setVisible(!error.isEmpty());
                viewport->setFocus();
            });
            addLayout->addWidget(button, row, column);
            if (++column == 2) column = 0, ++row;
        }
        if (types.isEmpty() && !refs.isEmpty()) {
            auto *none = new QLabel(QStringLiteral("Nessun vincolo per queste entita'."), addBox);
            addLayout->addWidget(none, 0, 0, 1, 2);
        }
        // Elenco: tutti i vincoli o solo quelli delle entita' scelte.
        const QVector<int> selected = viewport->selectedConstraints();
        for (int index = 0; index < sketch->geometricConstraints.size(); ++index) {
            const SketchConstraint &c = sketch->geometricConstraints.at(index);
            if (onlySelection->isChecked() && !refs.isEmpty()) {
                bool related = false;
                for (const ConstraintRef &ref : refs)
                    related = related || (ref.kind == 2 ? (c.first == ref || c.second == ref) : ForgeCad::refersTo(c, ref.kind, ref.element));
                if (!related) continue;
            }
            auto *item = new QListWidgetItem(ForgeCad::describeConstraint(*sketch, c), constraintList);
            item->setData(Qt::UserRole, index);
            if (ForgeCad::constraintError(*sketch, c) > 1e-7) item->setForeground(QColor(255, 140, 90));
            item->setSelected(selected.contains(index));
        }
        *refreshingPanel = false;
    };
    viewport->setConstraintPanelCallback([refreshPanel] { (*refreshPanel)(); });
    const auto listSelection = [constraintList] {
        QVector<int> indices;
        for (QListWidgetItem *item : constraintList->selectedItems()) indices.append(item->data(Qt::UserRole).toInt());
        return indices;
    };
    connect(constraintList, &QListWidget::itemSelectionChanged, constraintPanel, [=] {
        if (*refreshingPanel) return;
        *refreshingPanel = true;
        viewport->setSelectedConstraints(listSelection());
        *refreshingPanel = false;
    });
    connect(constraintList, &QListWidget::itemDoubleClicked, constraintPanel, [viewport](QListWidgetItem *item) {
        viewport->editConstraintValue(item->data(Qt::UserRole).toInt());
    });
    connect(editValue, &QPushButton::clicked, constraintPanel, [=] {
        const QVector<int> indices = listSelection();
        if (!indices.isEmpty()) viewport->editConstraintValue(indices.first());
    });
    connect(removeConstraint, &QPushButton::clicked, constraintPanel, [=] { viewport->deleteConstraints(listSelection()); });
    constraintList->installEventFilter(new DeleteKeyFilter(constraintList, [=] { viewport->deleteConstraints(listSelection()); }));
    connect(onlySelection, &QCheckBox::toggled, constraintPanel, [refreshPanel] { (*refreshPanel)(); });
    connect(showGlyphs, &QCheckBox::toggled, constraintPanel, [viewport](bool visible) { viewport->setConstraintsVisible(visible); });
    QAction *constraintsAction = sketchMenu->addAction(QStringLiteral("Vincoli..."));
    constraintsAction->setShortcut(QKeySequence(Qt::Key_K));
    constraintsAction->setToolTip(QStringLiteral("Finestra dei vincoli: quelli possibili per le entita' scelte e l'elenco (K)"));
    connect(constraintsAction, &QAction::triggered, this, [constraintPanel, refreshPanel] {
        (*refreshPanel)();
        constraintPanel->show();
        constraintPanel->raise();
    });
    auto *pointConstraints = addCadMenu(sketchMenu, QStringLiteral("Vincoli di punto"));
    auto *pierceAction=pointConstraints->addAction(QStringLiteral("Pierce su superficie..."));
    pierceAction->setToolTip(QStringLiteral("Seleziona un punto con Ctrl+clic, poi scegli la faccia: il punto resta sulla sua intersezione con il piano dello schizzo e segue la superficie."));
    connect(pierceAction,&QAction::triggered,this,[this,viewport] {
        const QString error=viewport->beginSurfacePierce();
        if (!error.isEmpty()) statusBar()->showMessage(error,8000);
    });
    for (ConstraintType type : {ConstraintType::Midpoint, ConstraintType::Quadrant}) {
        auto *action = pointConstraints->addAction(ForgeCad::constraintName(type));
        const QString help = type == ConstraintType::Midpoint
            ? QStringLiteral("Seleziona un punto con Ctrl+clic e il punto medio di un segmento con Ctrl+clic (oppure il segmento con Maiusc+clic), poi applica Punto medio.")
            : QStringLiteral("Seleziona un punto con Ctrl+clic e un cerchio/arco con Maiusc+clic: viene scelto il quadrante presente piu' vicino.");
        action->setToolTip(help);
        connect(action, &QAction::triggered, this, [this, viewport, type, help] {
            const QString error = viewport->addConstraint(type);
            if (!error.isEmpty()) statusBar()->showMessage(error + QLatin1Char(' ') + help, 10000);
        });
    }
    constraintPanel_ = constraintPanel;
    QAction *exitSketch = sketchMenu->addAction(QStringLiteral("Esci dalla modalita schizzo"));
    // Nello schizzo il tasto destro trascinato ruota la vista; questo la rimette perpendicolare al piano.
    QAction *sketchNormalView = sketchMenu->addAction(QStringLiteral("Vista normale allo schizzo"));
    sketchNormalView->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_8));
    sketchNormalView->setToolTip(QStringLiteral("Rimette la vista perpendicolare al piano dello schizzo (il tasto destro trascinato la ruota)"));
    connect(sketchNormalView, &QAction::triggered, this, [viewport] { viewport->alignViewToSketch(); });
    automaticConstraint->setShortcut(QKeySequence(Qt::Key_A));
    freeConstraint->setShortcut(QKeySequence(Qt::Key_O));
    horizontalConstraint->setShortcut(QKeySequence(Qt::Key_H));
    verticalConstraint->setShortcut(QKeySequence(Qt::Key_V));
    lengthConstraint->setShortcut(QKeySequence(Qt::Key_L));
    angleConstraint->setShortcut(QKeySequence(Qt::Key_G));

    *createSketchOnPlane = [viewport](int plane) {
        if (plane < 0 || plane >= planeNames().size()) return;
        // La voce nell'albero viene creata (e resa corrente) dalla ricostruzione differita.
        viewport->createSketch(plane, QStringLiteral("Schizzo %1").arg(viewport->sketches().size() + 1));
    };
    connect(newSketchAction, &QAction::triggered, this, [this, viewport, createSketchOnPlane] {
        // Piano di costruzione selezionato: lo schizzo va li'.
        const SceneSelection selection = viewport->selection();
        if (selection.kind == SceneObjectKind::Extrusion && selection.index >= 0 && selection.index < viewport->extrusions().size()
            && viewport->extrusions().at(selection.index).feature == BodyFeature::DatumPlane) {
            viewport->createDatumSketch(selection.index, QStringLiteral("Schizzo %1").arg(viewport->sketches().size() + 1));
            return;
        }
        const QStringList names = {QStringLiteral("Piano XY - Superiore"), QStringLiteral("Piano XZ - Frontale"), QStringLiteral("Piano YZ - Destro")};
        bool accepted = false;
        const QString selected = QInputDialog::getItem(this, QStringLiteral("Nuovo schizzo"), QStringLiteral("Seleziona il piano:"), names, 0, false, &accepted);
        if (accepted) (*createSketchOnPlane)(names.indexOf(selected));
    });
    connect(sketchAction, &QAction::triggered, this, [viewport] { viewport->setDrawingTool(DrawingTool::Line); });
    connect(polygonSidesAction, &QAction::triggered, this, [this, viewport] {
        bool accepted = false;
        const int sides = QInputDialog::getInt(this, QStringLiteral("Poligono"),
            QStringLiteral("Numero di lati:"), 6, 3, 64, 1, &accepted);
        if (accepted) viewport->setPolygonSides(sides);
    });
    connect(snapAction, &QAction::toggled, this, [viewport](bool enabled) {
        viewport->setGeometrySnap(enabled);
        QSettings().setValue(QStringLiteral("sketch/geometrySnap"), enabled);
    });
    connect(gridSnapAction, &QAction::toggled, this, [viewport](bool enabled) {
        viewport->setGridSnap(enabled);
        QSettings().setValue(QStringLiteral("sketch/gridSnap"), enabled);
    });
    connect(automaticConstraint, &QAction::triggered, this, [viewport] { viewport->setConstraintMode(0); });
    connect(freeConstraint, &QAction::triggered, this, [viewport] { viewport->setConstraintMode(-1); });
    connect(horizontalConstraint, &QAction::triggered, this, [viewport] { viewport->setConstraintMode(1); });
    connect(verticalConstraint, &QAction::triggered, this, [viewport] { viewport->setConstraintMode(2); });
    connect(lengthConstraint, &QAction::triggered, this, [this, viewport] {
        bool accepted = false;
        const double length = ForgeCad::getDouble(
            this, QStringLiteral("Quota lunghezza"), QStringLiteral("Lunghezza del prossimo segmento:"),
            1.0, 0.000001, 100000.0, 6, &accepted);
        if (accepted) viewport->setLineLength(length);
    });
    connect(angleConstraint, &QAction::triggered, this, [this, viewport] {
        bool accepted = false;
        const double angle = ForgeCad::getDouble(
            this, QStringLiteral("Quota angolare"),
            QStringLiteral("Angolo rispetto all'asse X del piano (gradi):"),
            0.0, -360.0, 360.0, 6, &accepted, false);
        if (accepted) viewport->setLineAngle(angle);
    });
    connect(exitSketch, &QAction::triggered, this, [viewport] { viewport->endSketchMode(); });

    auto viewActions = std::make_shared<QList<QAction *>>();
    auto *quit = fileMenu->addAction(QStringLiteral("Esci")); connect(quit, &QAction::triggered, this, &QWidget::close);
    const QList<QPair<QString, int>> views = {{QStringLiteral("Frontale"),0},{QStringLiteral("Posteriore"),1},{QStringLiteral("Destra"),2},{QStringLiteral("Superiore"),3},{QStringLiteral("Isometrica"),4},{QStringLiteral("Trimetrica"),5}};
    for (int index = 0; index < views.size(); ++index) {
        auto *action = new QAction(QStringLiteral("Vista ") + views.at(index).first.toLower(), this); action->setShortcut(QKeySequence(Qt::Key_1 + index)); addAction(action); viewActions->append(action);
        connect(action, &QAction::triggered, this, [viewport, index] { viewport->setViewPreset(index); });
    }

    // Barre a icone. Le icone (cad_icons) valgono anche nei menu; il
    // suggerimento dice il comando e la scorciatoia. I gruppi di strumenti
    // simili stanno in un pulsante con il menu (la freccia): il pulsante
    // mostra l'ultimo scelto (o quello attivo, per gli strumenti dello schizzo).
    const auto decorate = [](QAction *action, const QString &icon) {
        action->setIcon(ForgeCad::commandIcon(icon));
        QString tip = action->toolTip();
        const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
        if (!shortcut.isEmpty() && !tip.contains(QStringLiteral("(") + shortcut + QStringLiteral(")"))) tip += QStringLiteral(" (") + shortcut + QStringLiteral(")");
        action->setToolTip(tip);
    };
    const QList<QPair<QAction *, QString>> iconActions = {
        {newAction, QStringLiteral("new")}, {openAction, QStringLiteral("open")}, {saveAction, QStringLiteral("save")},
        {newDrawingAction, QStringLiteral("drawingNew")}, {openDrawingAction, QStringLiteral("drawingOpen")},
        {undoAction_, QStringLiteral("undo")}, {redoAction_, QStringLiteral("redo")}, {deleteAction, QStringLiteral("delete")},
        {rebuildAction, QStringLiteral("rebuild")},
        {newSketchAction, QStringLiteral("newSketch")}, {spatialSketchAction, QStringLiteral("newSketch3D")}, {faceSketchAction, QStringLiteral("faceSketch")},
        {extrudeAction, QStringLiteral("extrude")}, {revolveAction, QStringLiteral("revolve")},
        {filletAction, QStringLiteral("fillet")}, {chamferAction, QStringLiteral("chamfer")}, {draftAction, QStringLiteral("draft")}, {thickenAction, QStringLiteral("thicken")}, {projectedCurveAction, QStringLiteral("projectedCurve")}, {extractedCurveAction, QStringLiteral("extractEdge3D")}, {trimCurve3DAction, QStringLiteral("trimEdge3D")}, {shellAction, QStringLiteral("shell")}, {threadAction, QStringLiteral("thread")},
        {trimSurfaceAction, QStringLiteral("trimSurface")}, {extendSurfaceAction, QStringLiteral("extendSurface")},
        {copyFacesAction, QStringLiteral("copyFaces")}, {offsetSurfaceAction, QStringLiteral("offsetSurface")}, {sewSurfacesAction, QStringLiteral("sewSurfaces")},
        {deleteFaceAction, QStringLiteral("deleteFace")}, {boundarySurfaceAction, QStringLiteral("boundarySurface")}, {fillSurfaceAction, QStringLiteral("fillSurface")},
        {ruledSurfaceAction, QStringLiteral("ruledSurface")}, {planarSurfaceAction, QStringLiteral("planarSurface")},
        {loftSurfaceAction, QStringLiteral("loftSurface")}, {sweepSurfaceAction, QStringLiteral("sweepSurface")}, {scaleAction, QStringLiteral("scale")}, {moveAction, QStringLiteral("move")},
        {datumAction, QStringLiteral("datumPlane")}, {importAction, QStringLiteral("import")},
        {linearPatternAction, QStringLiteral("patternLinear")}, {circularPatternAction, QStringLiteral("patternCircular")},
        {mirrorAction, QStringLiteral("mirror")},
        {helixAction, QStringLiteral("helix")}, {sweepAction, QStringLiteral("sweep")}, {loftAction, QStringLiteral("loft")}, {massAction, QStringLiteral("massProperties")}, {measureAction, QStringLiteral("measure")},
        {unionAction, QStringLiteral("union")}, {intersectionAction, QStringLiteral("intersection")}, {differenceAction, QStringLiteral("difference")},
        {resetZoomAction, QStringLiteral("zoomAll")}, {sectionAction, QStringLiteral("section")},
        {modeMenu->actions().at(0), QStringLiteral("displayWireframe")}, {modeMenu->actions().at(1), QStringLiteral("displayShaded")},
        {modeMenu->actions().at(2), QStringLiteral("displayShadedEdges")},
        {selectTool, QStringLiteral("select")}, {lineTool, QStringLiteral("line")}, {polylineTool, QStringLiteral("polyline")},
        {constructionTool, QStringLiteral("constructionLine")}, {splineTool, QStringLiteral("spline")}, {nurbsTool, QStringLiteral("nurbs")},
        {circleTool, QStringLiteral("circle")}, {arcTool, QStringLiteral("arcCenter")}, {threePointArcTool, QStringLiteral("arcThreePoint")},
        {tangentArcTool, QStringLiteral("arcTangent")}, {polygonTool, QStringLiteral("polygon")}, {polygonSidesAction, QStringLiteral("polygonSides")},
        {rectangleTool, QStringLiteral("rectangle")}, {centerRectangleTool, QStringLiteral("centerRectangle")}, {ellipseTool, QStringLiteral("ellipse")},
        {trimTool, QStringLiteral("trim")}, {extendTool, QStringLiteral("extend")}, {splitTool, QStringLiteral("split")},
        {sketchFilletTool, QStringLiteral("sketchFillet")}, {sketchChamferTool, QStringLiteral("sketchChamfer")},
        {toggleConstruction, QStringLiteral("toggleConstruction")}, {symmetryAxisAction, QStringLiteral("symmetryAxis")}, {constraintsAction, QStringLiteral("constraints")},
        {sketchLinearPattern, QStringLiteral("sketchPatternLinear")}, {sketchCircularPattern, QStringLiteral("sketchPatternCircular")},
        {sketchMirror, QStringLiteral("sketchMirror")}, {convertTool, QStringLiteral("convertEdges")}, {sketchOffsetAction, QStringLiteral("sketchOffset")}, {sketchMoveAction, QStringLiteral("sketchMove")},
        {sectionReferences, QStringLiteral("sectionCurves")},
        {dimensionAction, QStringLiteral("dimension")}, {automaticConstraint, QStringLiteral("constraintAuto")},
        {freeConstraint, QStringLiteral("constraintFree")}, {horizontalConstraint, QStringLiteral("constraintHorizontal")},
        {verticalConstraint, QStringLiteral("constraintVertical")}, {lengthConstraint, QStringLiteral("constraintLength")},
        {angleConstraint, QStringLiteral("constraintAngle")}, {snapAction, QStringLiteral("snap")}, {gridSnapAction, QStringLiteral("gridSnap")}, {originSnapAction, QStringLiteral("originSnap")},
        {exitSketch, QStringLiteral("exitSketch")}, {sketchNormalView, QStringLiteral("viewFront")}};
    for (const auto &entry : iconActions) decorate(entry.first, entry.second);
    const QList<QPair<QAction *, QString>> viewIconActions = {
        {viewMenu->menuAction(), "viewOptions"}, {storyboardCursorMenu->menuAction(), "storyboard"},
        {storyboardCursorMenu->actions().at(0), "storyboardHorizontal"},
        {storyboardCursorMenu->actions().at(1), "storyboardLeft"}, {storyboardCursorMenu->actions().at(2), "storyboardRight"},
        {functionPanelMenu->menuAction(), "panelCorners"}, {modeMenu->menuAction(), "displayShadedEdges"},
        {qualityMenu->menuAction(), "viewQuality"}, {lightingMenu->menuAction(), "viewLight"},
        {studio, "viewLight"}, {soft, "panelBlur"}, {inspection, "viewInspect"},
        {backgroundAction, "viewBackground"}, {historyGraphAction, "storyboard"},
        {treeColorMenu->menuAction(), "viewColor"}, {treeColorAction, "viewColor"}, {treeColorReset, "viewTheme"},
        {highlightColorMenu->menuAction(), "viewHighlight"}, {highlightColorAction, "viewHighlight"}, {highlightColorReset, "viewTheme"},
        {showAllAction, "viewOptions"}, {planesAction, "datumPlane"}, {gridAction, "viewGrid"},
        {axesMenu->menuAction(), "viewAxes"}, {axesVisibleAction, "viewAxes"}, {axesOnTopAction, "viewLayers"}, {axisLengthAction, "dimension"},
        {sketchOpacityAction, "panelOpacity"}, {antialiasingMenu->menuAction(), "viewAntialias"},
        {wheelZoomAction, "viewWheel"}, {zoomInAction, "zoomIn"}, {zoomOutAction, "zoomOut"},
        {hiddenEdgesAction, "displayWireframe"}, {topologyIdsAction, "viewTopology"}, {sectionParameters, "section"}
    };
    for (const auto &entry : viewIconActions) decorate(entry.first, entry.second);
    // Dynamic choices inherit the meaningful icon of their submenu.
    const std::function<void(QMenu *)> completeViewIcons = [&](QMenu *menu) {
        for (QAction *action : menu->actions()) {
            if (action->isSeparator()) continue;
            action->setIconVisibleInMenu(true);
            if (action->icon().isNull()) action->setIcon(menu->menuAction()->icon());
            if (action->menu()) completeViewIcons(action->menu());
        }
    };
    completeViewIcons(viewMenu);
    // Il menu generale resta testuale; le voci interne conservano le icone.
    viewMenu->menuAction()->setIcon(QIcon());
    const QStringList primitiveIcons = {QStringLiteral("box"), QStringLiteral("cylinder"), QStringLiteral("sphere"), QStringLiteral("cone"), QStringLiteral("torus")};
    for (int k = 0; k < primitiveActions.size() && k < primitiveIcons.size(); ++k) decorate(primitiveActions.at(k), primitiveIcons.at(k));
    const QStringList viewIcons = {QStringLiteral("viewFront"), QStringLiteral("viewRear"), QStringLiteral("viewRight"),
                                   QStringLiteral("viewTop"), QStringLiteral("viewIso"), QStringLiteral("viewTrimetric")};
    for (int k = 0; k < viewActions->size(); ++k) decorate(viewActions->at(k), viewIcons.at(k));
    exitSketch->setToolTip(QStringLiteral("Chiudi lo schizzo"));
    const auto flyout = [](QToolBar *bar, const QList<QAction *> &actions, const QString &tip) {
        auto *button = new QToolButton(bar);
        auto *menu = new CadMenu(button);
        for (QAction *action : actions) menu->addAction(action);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::MenuButtonPopup);
        button->setAutoRaise(true);
        button->setIconSize(bar->iconSize());
        button->setDefaultAction(actions.first());
        const auto show = [button, tip](QAction *action) {
            button->setDefaultAction(action);
            button->setToolTip(action->toolTip() + QStringLiteral("\n") + tip);
        };
        show(actions.first());
        for (QAction *action : actions) {
            connect(action, &QAction::triggered, button, [show, action] { show(action); });
            if (action->isCheckable()) connect(action, &QAction::toggled, button, [show, action](bool on) { if (on) show(action); });
        }
        connect(bar, &QToolBar::iconSizeChanged, button, &QToolButton::setIconSize);
        bar->addWidget(button);
        return button;
    };
    const auto iconBar = [this](const QString &title, const QString &name) {
        auto *bar = addToolBar(title);
        bar->setObjectName(name);
        bar->setProperty("toolbarManagedVisibility", true);
        ForgeCad::configureCommandBar(bar);
        return bar;
    };

    auto *toolbar = iconBar(QStringLiteral("Modellazione"), QStringLiteral("modelingIconBar"));
    toolbar->addAction(newAction); toolbar->addAction(openAction); toolbar->addAction(saveAction); toolbar->addSeparator();
    toolbar->addAction(newDrawingAction);toolbar->addAction(openDrawingAction);toolbar->addSeparator();
    toolbar->addAction(undoAction_); toolbar->addAction(redoAction_); toolbar->addAction(deleteAction); toolbar->addAction(rebuildAction);
    toolbar->addSeparator();
    toolbar->addAction(importAction); toolbar->addSeparator();
    toolbar->addAction(newSketchAction); toolbar->addAction(spatialSketchAction); toolbar->addAction(faceSketchAction); toolbar->addAction(datumAction); toolbar->addSeparator();
    toolbar->addAction(extrudeAction); toolbar->addAction(revolveAction);
    flyout(toolbar, primitiveActions, QStringLiteral("Primitive: la freccia per le altre"));
    toolbar->addAction(filletAction); toolbar->addAction(chamferAction); toolbar->addAction(draftAction); toolbar->addAction(shellAction); toolbar->addAction(threadAction);
    toolbar->addAction(moveAction);
    toolbar->addAction(scaleAction);
    flyout(toolbar, {linearPatternAction, circularPatternAction, mirrorAction}, QStringLiteral("Ripetizioni: la freccia per le altre"));
    toolbar->addSeparator();
    toolbar->addAction(helixAction); toolbar->addAction(sweepAction); toolbar->addAction(loftAction);
    toolbar->addSeparator();
    toolbar->addAction(unionAction); toolbar->addAction(intersectionAction); toolbar->addAction(differenceAction); toolbar->addSeparator();
    toolbar->addAction(massAction); toolbar->addAction(measureAction); toolbar->addSeparator();
    toolbar->addAction(resetZoomAction);
    toolbar->addAction(sectionAction);
    flyout(toolbar, *viewActions, QStringLiteral("Viste standard: la freccia per le altre"));
    flyout(toolbar, {modeMenu->actions().at(2), modeMenu->actions().at(1), modeMenu->actions().at(0)}, QStringLiteral("Stile di visualizzazione"));
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("  %1 / Part Studio  ").arg(QGuiApplication::applicationDisplayName()), this));

    // Schizzo: in modalita' schizzo prende il posto della barra di modellazione
    // (come le schede del CommandManager di SolidWorks), cosi' ci sta anche in
    // una finestra stretta.
    // Modalita' della prossima linea: una sola attiva.
    auto *lineModeGroup = new QActionGroup(this);
    for (QAction *action : {automaticConstraint, freeConstraint, horizontalConstraint, verticalConstraint}) {
        action->setCheckable(true);
        lineModeGroup->addAction(action);
    }
    automaticConstraint->setChecked(true);
    // Superfici: tutti i comandi come pulsanti diretti (prima erano dietro la
    // freccia di un solo pulsante nella barra di modellazione).
    auto *surfaceToolbar = iconBar(QStringLiteral("Superfici"), QStringLiteral("surfaceIconBar"));
    for (QAction *action : {trimSurfaceAction, extendSurfaceAction, copyFacesAction, offsetSurfaceAction, thickenAction, deleteFaceAction, sewSurfacesAction})
        surfaceToolbar->addAction(action);
    surfaceToolbar->addSeparator();
    for (QAction *action : {ruledSurfaceAction, boundarySurfaceAction, fillSurfaceAction, planarSurfaceAction, loftSurfaceAction, sweepSurfaceAction})
        surfaceToolbar->addAction(action);
    surfaceToolbar->addSeparator();
    surfaceToolbar->addAction(extractedCurveAction);
    surfaceToolbar->addAction(trimCurve3DAction);
    surfaceToolbar->addAction(projectedCurveAction);
    // Barra personalizzata: i comandi scelti in Opzioni -> Personalizza barra
    // degli strumenti (chiavi "Menu/Sottomenu/Comando" in QSettings).
    auto *customToolbar = iconBar(QStringLiteral("Personalizzata"), QStringLiteral("customIconBar"));
    customToolbar->setProperty("toolbarHideWhenEmpty", true);
    const auto menuCommands = [this] {
        QVector<QPair<QString, QAction *>> commands;
        const std::function<void(QMenu *, const QString &)> visit = [&](QMenu *menu, const QString &path) {
            for (QAction *action : menu->actions()) {
                if (action->isSeparator() || action->property("toolbarConfiguration").toBool()) continue;
                QString text = action->text();
                text.remove(QLatin1Char('&'));
                if (action->menu()) { visit(action->menu(), path + text + QLatin1Char('/')); continue; }
                if (!text.isEmpty()) commands.append({path + text, action});
            }
        };
        for (QAction *top : menuBar()->actions())
            if (top->menu()) visit(top->menu(), top->text().remove(QLatin1Char('&')) + QLatin1Char('/'));
        return commands;
    };
    const auto fillCustomToolbar = [customToolbar, menuCommands, flyout] {
        // clear() removes widget actions but does not destroy them.
        const auto oldActions = customToolbar->actions();
        customToolbar->clear();
        for (QAction *action : oldActions)
            if (action->parent() == customToolbar) delete action;
        const QStringList keys = QSettings().value(QStringLiteral("toolbar/custom")).toStringList();
        const auto commands = menuCommands();
        QList<QAction *> selected;
        for (const QString &key : keys)
            for (const auto &command : commands)
                if (command.first == key) { selected.append(command.second); break; }
        if (!selected.isEmpty() && QSettings().value(QStringLiteral("toolbar/customDropdown"), false).toBool()) {
            auto *button = flyout(customToolbar, selected, QStringLiteral("Comandi personalizzati"));
            button->setObjectName(QStringLiteral("customToolbarDropdown"));
        } else {
            for (QAction *action : selected) customToolbar->addAction(action);
        }
        ToolbarEditor::refreshVisibility(customToolbar);
    };
    auto *toolbarsMenu = addCadMenu(optionsMenu, QStringLiteral("Barre degli strumenti"));
    toolbarsMenu->setObjectName(QStringLiteral("toolbarsMenu"));
    toolbarsMenu->menuAction()->setProperty("toolbarConfiguration", true);
    QAction *customizeAction = toolbarsMenu->addAction(QStringLiteral("Personalizza barra Personalizzata..."));
    customizeAction->setProperty("toolbarConfiguration", true);
    customizeAction->setToolTip(QStringLiteral("Sceglie i comandi dei menu da mettere nella barra Personalizzata, e il loro ordine"));
    connect(customizeAction, &QAction::triggered, this, [this, menuCommands, fillCustomToolbar] {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Personalizza barra degli strumenti"));
        auto *layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(QStringLiteral("Spunta i comandi da mettere nella barra Personalizzata; Su e Giu' cambiano l'ordine."), &dialog));
        auto *dropdown = new QCheckBox(QStringLiteral("Raggruppa i comandi in un menu a discesa"), &dialog);
        dropdown->setChecked(QSettings().value(QStringLiteral("toolbar/customDropdown"), false).toBool());
        layout->addWidget(dropdown);
        auto *hint = new QLabel(QStringLiteral("Per spostare la barra, trascina la maniglia sul bordo.\nIn Opzioni puoi bloccare posizione e personalizzazione delle barre."), &dialog);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *filter = new QLineEdit(&dialog);
        filter->setPlaceholderText(QStringLiteral("Cerca un comando..."));
        layout->addWidget(filter);
        auto *list = new QListWidget(&dialog);
        list->setObjectName(QStringLiteral("customToolbarCommands"));
        list->setIconSize(QSize(20, 20));
        layout->addWidget(list, 1);
        const QStringList chosen = QSettings().value(QStringLiteral("toolbar/custom")).toStringList();
        const auto commands = menuCommands();
        // Prima i comandi scelti nel loro ordine, poi gli altri nell'ordine dei menu.
        for (const QString &key : chosen)
            for (const auto &command : commands)
                if (command.first == key) {
                    auto *item = new QListWidgetItem(command.second->icon(), command.first, list);
                    item->setCheckState(Qt::Checked);
                    break;
                }
        for (const auto &command : commands) {
            if (chosen.contains(command.first)) continue;
            auto *item = new QListWidgetItem(command.second->icon(), command.first, list);
            item->setCheckState(Qt::Unchecked);
        }
        auto *moves = new QHBoxLayout;
        auto *up = new QPushButton(QStringLiteral("Su"), &dialog), *down = new QPushButton(QStringLiteral("Giu'"), &dialog);
        moves->addWidget(up);
        moves->addWidget(down);
        moves->addStretch(1);
        layout->addLayout(moves);
        const auto move = [list](int step) {
            const int row = list->currentRow(), target = row + step;
            if (row < 0 || target < 0 || target >= list->count()) return;
            QListWidgetItem *item = list->takeItem(row);
            list->insertItem(target, item);
            list->setCurrentRow(target);
        };
        connect(up, &QPushButton::clicked, &dialog, [move] { move(-1); });
        connect(down, &QPushButton::clicked, &dialog, [move] { move(1); });
        connect(filter, &QLineEdit::textChanged, &dialog, [list](const QString &text) {
            for (int row = 0; row < list->count(); ++row)
                list->item(row)->setHidden(!text.isEmpty() && !list->item(row)->text().contains(text, Qt::CaseInsensitive));
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(520, 560);
        if (dialog.exec() != QDialog::Accepted) return;
        QStringList keys;
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->checkState() == Qt::Checked) keys.append(list->item(row)->text());
        QSettings().setValue(QStringLiteral("toolbar/custom"), keys);
        QSettings().remove(QStringLiteral("toolbar/layout/customIconBar"));
        QSettings().setValue(QStringLiteral("toolbar/customDropdown"), dropdown->isChecked());
        fillCustomToolbar();
    });
    auto *drawingToolbar = iconBar(QStringLiteral("Strumenti schizzo"), QStringLiteral("sketchIconBar"));
    drawingToolbar->setProperty("toolbarModeHidden", true);
    drawingToolbar->setVisible(false);
    drawingToolbar->addAction(exitSketch); drawingToolbar->addAction(sketchNormalView); drawingToolbar->addSeparator();
    drawingToolbar->addAction(undoAction_); drawingToolbar->addAction(redoAction_); drawingToolbar->addSeparator();
    drawingToolbar->addAction(selectTool); drawingToolbar->addSeparator();
    flyout(drawingToolbar, {lineTool, constructionTool}, QStringLiteral("Linee: la freccia per le altre"));
    drawingToolbar->addAction(polylineTool);
    flyout(drawingToolbar, {rectangleTool, centerRectangleTool}, QStringLiteral("Rettangoli: la freccia per gli altri"));
    drawingToolbar->addAction(circleTool);
    flyout(drawingToolbar, {arcTool, threePointArcTool, tangentArcTool}, QStringLiteral("Archi: la freccia per gli altri"));
    flyout(drawingToolbar, {polygonTool, polygonSidesAction}, QStringLiteral("Poligono: la freccia per il numero di lati"));
    drawingToolbar->addAction(ellipseTool);
    flyout(drawingToolbar, {splineTool, nurbsTool}, QStringLiteral("Curve: la freccia per le altre"));
    drawingToolbar->addSeparator();
    flyout(drawingToolbar, {trimTool, extendTool, splitTool}, QStringLiteral("Taglia, estendi, spezza: la freccia per gli altri"));
    flyout(drawingToolbar, {sketchFilletTool, sketchChamferTool}, QStringLiteral("Raccordo e smusso: la freccia per l'altro"));
    drawingToolbar->addAction(toggleConstruction); drawingToolbar->addAction(symmetryAxisAction);
    drawingToolbar->addAction(sketchOffsetAction);
    drawingToolbar->addAction(sketchMoveAction);
    flyout(drawingToolbar, {sketchLinearPattern, sketchCircularPattern, sketchMirror}, QStringLiteral("Ripetizioni: la freccia per le altre"));
    flyout(drawingToolbar, {convertTool, sectionReferences}, QStringLiteral("Riferimenti dai corpi: la freccia per la sezione"));
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(constraintsAction); drawingToolbar->addAction(dimensionAction);
    flyout(drawingToolbar, {automaticConstraint, freeConstraint, horizontalConstraint, verticalConstraint},
           QStringLiteral("Vincolo della prossima linea: la freccia per gli altri"));
    flyout(drawingToolbar, {lengthConstraint, angleConstraint}, QStringLiteral("Quote della prossima linea: la freccia per l'altra"));
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(snapAction); drawingToolbar->addAction(gridSnapAction); drawingToolbar->addAction(originSnapAction);
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(extrudeAction); drawingToolbar->addAction(revolveAction);
    // In modalita' schizzo si spengono solo le viste standard (la vista resta normale
    // al piano); il resto del menu Visualizza (trasparenza dei corpi, zoom...) resta attivo.
    viewport->setSketchModeCallback([this, viewActions, drawingToolbar, toolbar, surfaceToolbar](bool active) {
        for (QAction *action : *viewActions) action->setEnabled(!active);
        toolbar->setProperty("toolbarModeHidden", active);
        surfaceToolbar->setProperty("toolbarModeHidden", active);
        drawingToolbar->setProperty("toolbarModeHidden", !active);
        for (auto *bar : {toolbar, surfaceToolbar, drawingToolbar}) ToolbarEditor::refreshVisibility(bar);
        // La finestra dei vincoli accompagna la modalita' schizzo.
        if (constraintPanel_) constraintPanel_->setVisible(active);
    });
    modeStatus_ = new QLabel(QStringLiteral("Mesh + linee esterne")); statusBar()->addWidget(modeStatus_);
    backgroundProgressLabel_ = new QLabel(this);
    backgroundProgressLabel_->setObjectName(QStringLiteral("backgroundProgressLabel"));
    backgroundProgress_ = new QProgressBar(this);
    backgroundProgress_->setObjectName(QStringLiteral("backgroundProgressBar"));
    backgroundProgress_->setRange(0, 0);
    backgroundProgress_->setTextVisible(false);
    backgroundProgress_->setFixedWidth(150);
    backgroundProgressLabel_->hide();
    backgroundProgress_->hide();
    statusBar()->addPermanentWidget(backgroundProgressLabel_);
    statusBar()->addPermanentWidget(backgroundProgress_);
    viewport->setWorkCallback([this](bool begin, const QString &message, bool background) {
        if (background) {
            if (begin) beginBackgroundProgress(message);
            else endBackgroundProgress();
        } else {
            if (begin) beginForegroundProgress(message);
            else endForegroundProgress();
        }
    });
    auto *gpuStatus = new QLabel(gpuStatusText());
    gpuStatus->setObjectName(QStringLiteral("gpuStatus"));
    auto *sceneCounts = new QLabel(QStringLiteral("Solidi: 0 · Superfici: 0"));
    sceneCounts->setObjectName(QStringLiteral("sceneCounts"));
    sceneCounts->setToolTip(QStringLiteral("Solidi e superfici separati nei corpi visibili: dopo un'unione riuscita i solidi uniti ne fanno uno"));
    statusBar()->addPermanentWidget(sceneCounts);
    statusBar()->addPermanentWidget(gpuStatus);
    viewport->setRendererCallback([this, gpuStatus](const QString &renderer) {
        glRenderer_ = renderer;
        gpuStatus->setText(gpuStatusText());
    });
    installDocumentRevisionPanel(viewMenu);
    // Impostazioni dell'interfaccia (Opzioni): finestra e pannelli, stile di
    // visualizzazione, luci, qualita', sfondo, piani, griglia, zoom con la
    // rotella. Si salvano a richiesta nel gruppo "interface" di QSettings e
    // si riprendono all'avvio. Tasto del pan, kernel, antialiasing, griglia e
    // assi si salvano gia' quando si cambiano.
    const QList<QAction *> modeActions = modeGroup->actions(), qualityActions = qualityGroup->actions();
    const QList<QAction *> lightingActions = {studio, soft, inspection};
    optionsMenu->addSeparator();
    QAction *saveInterfaceAction = optionsMenu->addAction(QStringLiteral("Salva impostazioni dell'interfaccia"));
    QAction *resetInterfaceAction = optionsMenu->addAction(QStringLiteral("Ripristina impostazioni predefinite dell'interfaccia..."));
    connect(saveInterfaceAction, &QAction::triggered, this, [this, viewport] {
        QSettings settings;
        settings.beginGroup(QStringLiteral("interface"));
        settings.setValue(QStringLiteral("geometry"), saveGeometry());
        settings.setValue(QStringLiteral("state"), saveState());
        settings.setValue(QStringLiteral("displayMode"), viewport->displayMode());
        settings.setValue(QStringLiteral("hiddenEdges"), viewport->hiddenEdgesVisible());
        settings.setValue(QStringLiteral("lighting"), viewport->lightingPreset());
        settings.setValue(QStringLiteral("quality"), viewport->tessellationQuality());
        settings.setValue(QStringLiteral("wheelZoom"), viewport->wheelZoomEnabled());
        settings.setValue(QStringLiteral("planes"), viewport->referencePlanesVisible());
        settings.setValue(QStringLiteral("grid"), viewport->gridVisible());
        settings.setValue(QStringLiteral("axisLength"), viewport->axisLength());
        settings.setValue(QStringLiteral("axes"), viewport->axesVisible());
        settings.setValue(QStringLiteral("axesOnTop"), viewport->axesOnTop());
        settings.setValue(QStringLiteral("originSnap"), viewport->originSnap());
        settings.setValue(QStringLiteral("antialiasing"), viewport->antialiasing());
        const BackgroundSettings &background = viewport->background();
        settings.setValue(QStringLiteral("background/gradient"), background.gradient);
        settings.setValue(QStringLiteral("background/start"), background.startColor);
        settings.setValue(QStringLiteral("background/end"), background.endColor);
        settings.setValue(QStringLiteral("background/angle"), background.angle);
        settings.setValue(QStringLiteral("background/position"), background.position);
        settings.setValue(QStringLiteral("background/affectsLighting"), background.affectsLighting);
        settings.setValue(QStringLiteral("background/lightingStrength"), background.lightingStrength);
        settings.endGroup();
        settings.sync();
        statusBar()->showMessage(settings.status() == QSettings::NoError
            ? QStringLiteral("Impostazioni dell'interfaccia salvate in %1").arg(settings.fileName())
            : QStringLiteral("Impossibile salvare le impostazioni in %1").arg(settings.fileName()), 6000);
    });
    connect(resetInterfaceAction, &QAction::triggered, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Impostazioni dell'interfaccia"),
                QStringLiteral("Tornare alle impostazioni predefinite dell'interfaccia al prossimo avvio?")) != QMessageBox::Yes)
            return;
        QSettings settings;
        settings.remove(QStringLiteral("interface"));
        for (const QString &key : {QStringLiteral("view/grid"), QStringLiteral("view/axisLength"), QStringLiteral("view/axes"), QStringLiteral("view/axesOnTop"),
                                   QStringLiteral("view/antialiasing"), QStringLiteral("view/panKey"), QStringLiteral("sketch/originSnap"),
                                   QStringLiteral("sketch/geometrySnap"), QStringLiteral("sketch/gridSnap"),
                                   QStringLiteral("view/constraintPanel"), QStringLiteral("document/saveBodies"),
                                   QStringLiteral("view/sketchBodyOpacity"), QStringLiteral("view/hiddenEdges"),
                                   QStringLiteral("view/featureHighlightColor")})
            settings.remove(key);
        statusBar()->showMessage(QStringLiteral("Le impostazioni predefinite valgono dal prossimo avvio."), 6000);
    });
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("interface"));
        const auto pick = [](const QList<QAction *> &actions, int index) {
            if (index >= 0 && index < actions.size()) actions.at(index)->trigger();
        };
        if (settings.contains(QStringLiteral("displayMode"))) pick(modeActions, settings.value(QStringLiteral("displayMode")).toInt());
        if (settings.contains(QStringLiteral("hiddenEdges"))) hiddenEdgesAction->setChecked(settings.value(QStringLiteral("hiddenEdges")).toBool());
        if (settings.contains(QStringLiteral("lighting"))) pick(lightingActions, settings.value(QStringLiteral("lighting")).toInt());
        if (settings.contains(QStringLiteral("quality"))) pick(qualityActions, settings.value(QStringLiteral("quality")).toInt());
        if (settings.contains(QStringLiteral("wheelZoom"))) wheelZoomAction->setChecked(settings.value(QStringLiteral("wheelZoom")).toBool());
        if (settings.contains(QStringLiteral("planes"))) planesAction->setChecked(settings.value(QStringLiteral("planes")).toBool());
        if (settings.contains(QStringLiteral("background/start"))) {
            BackgroundSettings background = viewport->background();
            background.gradient = settings.value(QStringLiteral("background/gradient"), background.gradient).toBool();
            background.startColor = settings.value(QStringLiteral("background/start"), background.startColor).value<QColor>();
            background.endColor = settings.value(QStringLiteral("background/end"), background.endColor).value<QColor>();
            background.angle = settings.value(QStringLiteral("background/angle"), background.angle).toFloat();
            background.position = settings.value(QStringLiteral("background/position"), background.position).toFloat();
            background.affectsLighting = settings.value(QStringLiteral("background/affectsLighting"), background.affectsLighting).toBool();
            background.lightingStrength = settings.value(QStringLiteral("background/lightingStrength"), background.lightingStrength).toFloat();
            viewport->setBackground(background);
        }
        if (settings.contains(QStringLiteral("geometry"))) restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
        if (settings.contains(QStringLiteral("state"))) restoreState(settings.value(QStringLiteral("state")).toByteArray());
        // La visibilita' delle barre dipende dalla modalita', non dallo stato
        // salvato (che puo' provenire da una sessione in schizzo).
        toolbar->setVisible(true);
        surfaceToolbar->setVisible(true);
        drawingToolbar->setVisible(false);  // compare solo in modalita' schizzo
        settings.endGroup();
    }
    auto *lockToolbars = toolbarsMenu->addAction(QStringLiteral("Blocca barre degli strumenti"));
    lockToolbars->setObjectName(QStringLiteral("lockToolbarsAction"));
    lockToolbars->setProperty("toolbarConfiguration", true);
    lockToolbars->setCheckable(true);
    lockToolbars->setToolTip(QStringLiteral("Blocca lo spostamento delle barre e la modifica dei comandi personalizzati"));
    const auto applyToolbarLock = [this, customizeAction](bool locked) {
        for (auto *bar : findChildren<QToolBar *>()) {
            bar->setMovable(!locked);
            bar->setFloatable(!locked);
        }
        customizeAction->setEnabled(!locked);
        for (auto *action : findChildren<QAction *>())
            if (action->property("toolbarEditorAction").toBool()) action->setEnabled(!locked);
    };
    lockToolbars->setChecked(QSettings().value(QStringLiteral("toolbar/locked"), false).toBool());
    applyToolbarLock(lockToolbars->isChecked());
    connect(lockToolbars, &QAction::toggled, this, [this, applyToolbarLock](bool locked) {
        QSettings().setValue(QStringLiteral("toolbar/locked"), locked);
        applyToolbarLock(locked);
        if (locked) QSettings().setValue(QStringLiteral("interface/state"), saveState());
    });
    fillCustomToolbar();  // dopo lo stato salvato: visibile se ha dei comandi
    const auto toolbarCommands = menuCommands();
    auto *editBarsMenu = addCadMenu(toolbarsMenu, QStringLiteral("Personalizza pulsanti delle barre"));
    editBarsMenu->menuAction()->setProperty("toolbarConfiguration", true);
    toolbarsMenu->addSeparator();
    for (auto *bar : {toolbar, surfaceToolbar, customToolbar, drawingToolbar}) {
        const auto saveCustomOrder = [customToolbar, toolbarCommands] {
            if (QSettings().value(QStringLiteral("toolbar/customDropdown"), false).toBool()) return;
            QStringList keys;
            for (auto *action : customToolbar->actions())
                for (const auto &command : toolbarCommands)
                    if (command.second == action) { keys.append(command.first); break; }
            QSettings().setValue(QStringLiteral("toolbar/custom"), keys);
        };
        auto *editor = new ToolbarEditor(bar, toolbarCommands, [lockToolbars] { return lockToolbars->isChecked(); },
                                        bar == customToolbar ? std::function<void()>(saveCustomOrder) : std::function<void()>());
        auto *editAction = editBarsMenu->addAction(bar->windowTitle() + QStringLiteral("..."));
        editAction->setObjectName(bar->objectName() + QStringLiteral("EditorAction"));
        editAction->setProperty("toolbarEditorAction", true);
        editAction->setProperty("toolbarConfiguration", true);
        editAction->setEnabled(!lockToolbars->isChecked());
        connect(editAction, &QAction::triggered, editor, [editor] { editor->edit(); });
        auto *visibleAction = toolbarsMenu->addAction(bar->windowTitle());
        visibleAction->setObjectName(bar->objectName() + QStringLiteral("VisibleAction"));
        visibleAction->setProperty("toolbarConfiguration", true);
        visibleAction->setCheckable(true);
        const QString visibilityKey = QStringLiteral("toolbar/visible/") + bar->objectName();
        visibleAction->setChecked(QSettings().value(visibilityKey, true).toBool());
        if (bar == drawingToolbar) visibleAction->setToolTip(QStringLiteral("Mostra la barra quando è attiva la modalità schizzo"));
        connect(visibleAction, &QAction::toggled, bar, [bar, visibilityKey](bool visible) {
            QSettings().setValue(visibilityKey, visible);
            ToolbarEditor::refreshVisibility(bar);
        });
        ToolbarEditor::refreshVisibility(bar);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(bar, &QWidget::customContextMenuRequested, this, [this, bar, lockToolbars, editAction, toolbarsMenu](const QPoint &point) {
            CadMenu menu(this);
            menu.addAction(editAction);
            menu.addSeparator();
            menu.addAction(lockToolbars);
            menu.addSeparator();
            menu.addMenu(toolbarsMenu);
            menu.exec(bar->mapToGlobal(point));
        });
    }
    const auto markMutation=[this](QAction *action){if(!documentMutationActions_.contains(action))documentMutationActions_.append(action);};
    const std::function<void(QMenu *)> markMenu=[&](QMenu *menu){for(auto *action:menu->actions()){if(action->menu())markMenu(action->menu());else if(!action->isSeparator())markMutation(action);}};
    for(auto *menu:{editMenu,functionsMenu,sketchMenu,unitsMenu})markMenu(menu);
    markMutation(saveAction);markMutation(importAction);markMutation(orientationAction);
    saveAction->setObjectName("documentSaveAction");importAction->setObjectName("documentImportAction");
    documentRevisionStatus_=new QLabel(this);documentRevisionStatus_->setObjectName("documentRevisionStatus");
    statusBar()->addPermanentWidget(documentRevisionStatus_);
    auto *revisionTimer=new QTimer(this);revisionTimer->setInterval(5000);
    connect(revisionTimer,&QTimer::timeout,this,[this]{
        if(foregroundProgressDepth_||viewport_->interactionLocked())return;
        refreshDocumentEditAccess();updateWindowTitle();
    });revisionTimer->start();
    documentRevisionDock_->setVisible(QSettings().value("revisions/modelPanelVisible",true).toBool());
    refreshDocumentEditAccess();updateWindowTitle();
    setTheme(QApplication::palette().color(QPalette::Window).lightness() < 128);
    connect(recovery_,&ForgeCad::DocumentRecovery::checkpointFinished,this,[this](quint64,bool,const QString &error){
        if(!error.isEmpty())statusBar()->showMessage(QStringLiteral("Backup automatico non riuscito: %1").arg(error),12000);
    });
    connect(recovery_,&ForgeCad::DocumentRecovery::cleared,this,[this](quint64 revision,const QString &error){
        if(!error.isEmpty())statusBar()->showMessage(QStringLiteral("Pulizia backup: %1").arg(error),8000);
        if(recoveryClosing_&&revision==recoveryCloseRevision_)recovery_->shutdown();
    });
    connect(recovery_,&ForgeCad::DocumentRecovery::stopped,this,[this]{if(recoveryClosing_){recoveryCloseReady_=true;close();}});
    connect(recovery_,&ForgeCad::DocumentRecovery::discarded,this,[this](const QString &error){
        if(!error.isEmpty())statusBar()->showMessage(error,10000);
    });
    connect(recovery_,&ForgeCad::DocumentRecovery::restored,this,[this](DocumentState state,const ForgeCad::RecoveryEntry &entry,const QString &error){
        if(!error.isEmpty()){QMessageBox::warning(this,QStringLiteral("Recupero documento"),error);return;}
        ForgeCad::forkDocumentIdentity(state);
        auto *window=new PdfWindow;window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();window->beginForegroundProgress(QStringLiteral("Ricostruzione del modello recuperato..."));
        window->loadingDocument_=true;window->viewport_->loadDocument(std::move(state));window->loadingDocument_=false;
        window->endForegroundProgress();
        window->documentModified_=true;window->recoveredSource_=entry;window->recovery_->changed();
        window->refreshDocumentEditAccess();window->updateWindowTitle();window->show();
        window->statusBar()->showMessage(QStringLiteral("Recuperato %1. Salva con nome per conservare la copia.").arg(entry.title),15000);
    });
    connect(recovery_,&ForgeCad::DocumentRecovery::entriesFound,this,[this](const QVector<ForgeCad::RecoveryEntry> &entries,const QString &error){
        if(!recoveryScanPending_)return;recoveryScanPending_=false;
        if(recoveryClosing_)return;
        if(foregroundProgressDepth_||loadingDocument_||viewport_->interactionLocked()||QApplication::activeModalWidget()){
            if(!entries.isEmpty())statusBar()->showMessage(QStringLiteral("Backup disponibili in File → Recupera documenti..."),10000);
            return;
        }
        if(!error.isEmpty()){statusBar()->showMessage(error,10000);return;}
        if(entries.isEmpty()){
            if(!recoveryScanAutomatic_)statusBar()->showMessage(QStringLiteral("Nessun documento da recuperare."),6000);
            return;
        }
        QDialog dialog(this);dialog.setObjectName(QStringLiteral("documentRecoveryDialog"));dialog.setWindowTitle(QStringLiteral("Documenti da recuperare"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *hint=new QLabel(QStringLiteral("Sono disponibili copie automatiche di sessioni interrotte. Il recupero apre una nuova copia e lascia intatto il file originale."),&dialog);
        hint->setWordWrap(true);layout->addWidget(hint);
        auto *table=new QTableWidget(entries.size(),3,&dialog);table->setObjectName(QStringLiteral("documentRecoveryTable"));
        table->setHorizontalHeaderLabels({QStringLiteral("Documento"),QStringLiteral("Ultimo backup"),QStringLiteral("Dimensione")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);
        for(int row=0;row<entries.size();++row){const auto &entry=entries[row];
            auto *name=new QTableWidgetItem(entry.originalPath.isEmpty()?entry.title:entry.originalPath);name->setToolTip(entry.originalPath);table->setItem(row,0,name);
            table->setItem(row,1,new QTableWidgetItem(entry.savedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
            table->setItem(row,2,new QTableWidgetItem(QStringLiteral("%1 KiB").arg((entry.bytes+1023)/1024)));
        }
        table->setTextElideMode(Qt::ElideMiddle);table->resizeColumnsToContents();table->setColumnWidth(2,qMax(110,table->columnWidth(2)));
        table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table->selectRow(0);layout->addWidget(table);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
        buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Chiudi"));
        auto *restore=buttons->addButton(QStringLiteral("Recupera come copia"),QDialogButtonBox::AcceptRole);restore->setObjectName(QStringLiteral("restoreRecoveryButton"));
        auto *remove=buttons->addButton(QStringLiteral("Elimina backup"),QDialogButtonBox::DestructiveRole);remove->setObjectName(QStringLiteral("discardRecoveryButton"));
        connect(restore,&QPushButton::clicked,&dialog,[&]{const int row=table->currentRow();if(row>=0){recovery_->restore(entries[row]);dialog.accept();}});
        connect(remove,&QPushButton::clicked,&dialog,[&]{const int row=table->currentRow();if(row>=0){recovery_->discard(entries[row]);dialog.accept();}});
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);dialog.resize(800,340);dialog.exec();
    });
    // Solo la prima finestra dell'app propone il recupero all'avvio. Il menu
    // File resta disponibile anche dopo aver chiuso la proposta.
    static bool startupRecoveryScheduled=false;
    if(!startupRecoveryScheduled){startupRecoveryScheduled=true;QTimer::singleShot(0,this,[this]{if(isVisible())showRecoveryDocuments(true);});}
}

bool PdfWindow::event(QEvent *event) {
    const bool handled = QMainWindow::event(event);
    if (event->type() == QEvent::WindowActivate&&viewport_&&documentRevisionStatus_) {refreshDocumentEditAccess();updateWindowTitle();}
    if (event->type() == QEvent::ApplicationPaletteChange) {
        setTheme(QApplication::palette().color(QPalette::Window).lightness() < 128);
        // Le icone vettoriali leggono la palette corrente al ridisegno.
        for (auto *bar : findChildren<QToolBar *>()) bar->update();
    }
    return handled;
}

void PdfWindow::refreshDocumentEditAccess(){
    if(!viewport_||foregroundProgressDepth_||viewport_->interactionLocked())return;
    ForgeCad::workspaceDocumentEditAccess(documentPath_,viewport_->documentVersion(),viewport_->documentBaseCheckinId(),documentEditAccess_);
    if(documentEditAccess_.readOnly)documentLock_.reset();
    if(!documentPath_.isEmpty()&&!documentEditAccess_.readOnly&&(!documentLock_||!documentLock_->owns(documentPath_))){
        documentLock_.reset();documentEditAccess_.readOnly=true;
        documentEditAccess_.reason=QStringLiteral("Copia aperta in consultazione. Eseguire check-out per passare alla modifica; la copia potrebbe essere in uso da un'altra sessione.");
    }
    viewport_->setDocumentReadOnly(documentEditAccess_.readOnly);
    if(documentEditAccess_.readOnly){
        for(const auto &action:documentMutationActions_)if(action&&action->isEnabled()){
            action->setEnabled(false);if(!revisionDisabledActions_.contains(action))revisionDisabledActions_.append(action);
        }
    }else{
        for(const auto &action:revisionDisabledActions_)if(action)action->setEnabled(true);
        revisionDisabledActions_.clear();
    }
    if(documentRevisionStatus_){documentRevisionStatus_->setText(!viewport_->documentVersion().checkinId.isNull()?QStringLiteral("Revisione storica · Sola lettura"):documentEditAccess_.readOnly?QStringLiteral("Sola lettura · Consultazione"):QString());documentRevisionStatus_->setToolTip(documentEditAccess_.reason);}
    updateUndoActions();
    refreshDocumentRevisionPanel();
}
void PdfWindow::updateWindowTitle() {
    const QString name = documentPath_.isEmpty() ? QStringLiteral("Senza nome") : QFileInfo(documentPath_).fileName();
    setWindowTitle(QStringLiteral("%1%2%3 - %4").arg(name, documentModified_ ? QStringLiteral(" *") : QString(),
        documentEditAccess_.readOnly?QStringLiteral(" [sola lettura]"):QString(),QGuiApplication::applicationDisplayName()));
}

void PdfWindow::rememberRecentDocument(const QString &path) {
    QSettings settings;
    QStringList paths = settings.value(QStringLiteral("document/recentFiles")).toStringList();
    const QString canonical = ForgeCad::canonicalDocumentPath(path);
    for (int i = paths.size() - 1; i >= 0; --i)
        if (ForgeCad::canonicalDocumentPath(paths.at(i)) == canonical) paths.removeAt(i);
    paths.prepend(canonical);
    while (paths.size() > 10) paths.removeLast();
    settings.setValue(QStringLiteral("document/recentFiles"), paths);
    refreshRecentDocuments();
}

void PdfWindow::refreshRecentDocuments() {
    if (!recentDocumentsMenu_) return;
    recentDocumentsMenu_->clear();
    const QStringList paths = QSettings().value(QStringLiteral("document/recentFiles")).toStringList();
    if (paths.isEmpty()) {
        recentDocumentsMenu_->addAction(QStringLiteral("Nessun file recente"))->setEnabled(false);
        return;
    }
    for (int i = 0; i < qMin(10, int(paths.size())); ++i) {
        const QString path = paths.at(i);
        auto *action = recentDocumentsMenu_->addAction(QStringLiteral("&%1 %2").arg(i + 1).arg(QString(path).replace('&', QStringLiteral("&&"))));
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] { openDocumentPath(path); });
    }
    recentDocumentsMenu_->addSeparator();
    auto *clear = recentDocumentsMenu_->addAction(QStringLiteral("Svuota elenco"));
    clear->setObjectName(QStringLiteral("clearRecentDocuments"));
    connect(clear, &QAction::triggered, this, [this] {
        QSettings().remove(QStringLiteral("document/recentFiles"));
        refreshRecentDocuments();
    });
}

// Chiede se salvare le modifiche; false se l'utente annulla (o il salvataggio fallisce).
bool PdfWindow::maybeSaveChanges() {
    if (!documentModified_) return true;
    const auto answer = QMessageBox::question(this, QGuiApplication::applicationDisplayName(),
        QStringLiteral("Il documento e' stato modificato. Salvare le modifiche?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveDocument(false);
    return true;
}

// Orientamento degli assi per i documenti nuovi (Opzioni), di default Z in alto.
static AxesOrientation defaultAxesOrientation() {
    const QVariantList values = QSettings().value(QStringLiteral("view/axesOrientation")).toList();
    AxesOrientation o;
    if (values.size() != 9) return o;
    double *axes[3] = {o.right, o.up, o.toward};
    for (int k = 0; k < 9; ++k) axes[k / 3][k % 3] = values.at(k).toDouble();
    return o;
}
static void saveDefaultAxesOrientation(const AxesOrientation &o) {
    QVariantList values;
    for (const double *axis : {o.right, o.up, o.toward})
        for (int k = 0; k < 3; ++k) values.append(axis[k]);
    QSettings().setValue(QStringLiteral("view/axesOrientation"), values);
}

void PdfWindow::beginForegroundProgress(const QString &message, int maximum) {
    const bool first = foregroundProgressDepth_ == 0;
    ++foregroundProgressDepth_;
    if (!foregroundProgress_) {
        foregroundProgress_ = new QProgressDialog(this);
        foregroundProgress_->setObjectName(QStringLiteral("foregroundProgressDialog"));
        foregroundProgress_->setWindowTitle(QStringLiteral("%1 - operazione in corso").arg(QGuiApplication::applicationDisplayName()));
        foregroundProgress_->setCancelButton(nullptr);
        foregroundProgress_->setAutoClose(false);
        foregroundProgress_->setAutoReset(false);
        foregroundProgress_->setMinimumDuration(0);
        foregroundProgress_->setWindowModality(Qt::WindowModal);
        foregroundProgress_->setMinimumWidth(430);
    }
    foregroundProgress_->setLabelText(message);
    if (first) {
        foregroundProgress_->setRange(0, maximum > 0 ? maximum : 0);
        if (maximum > 0) foregroundProgress_->setValue(0);
        foregroundProgress_->show();
        foregroundProgress_->raise();
    }
    // Il calcolo partira' subito sul thread GUI: completa il disegno della
    // finestra prima di bloccare il ciclo eventi, senza accettare altri input.
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void PdfWindow::updateForegroundProgress(const QString &message, int value, int maximum) {
    if (!foregroundProgress_ || foregroundProgressDepth_ <= 0) return;
    if (maximum > 0 && (foregroundProgress_->minimum() != 0 || foregroundProgress_->maximum() != maximum))
        foregroundProgress_->setRange(0, maximum);
    foregroundProgress_->setLabelText(message);
    foregroundProgress_->setValue(qBound(0, value, qMax(0, maximum)));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void PdfWindow::endForegroundProgress() {
    if (foregroundProgressDepth_ <= 0) return;
    if (--foregroundProgressDepth_ == 0 && foregroundProgress_) foregroundProgress_->hide();
}

void PdfWindow::beginBackgroundProgress(const QString &message) {
    ++backgroundProgressDepth_;
    if (backgroundProgressLabel_) {
        backgroundProgressLabel_->setText(message);
        backgroundProgressLabel_->show();
    }
    if (backgroundProgress_) backgroundProgress_->show();
}

void PdfWindow::endBackgroundProgress() {
    if (backgroundProgressDepth_ <= 0) return;
    if (--backgroundProgressDepth_ != 0) return;
    if (backgroundProgressLabel_) backgroundProgressLabel_->hide();
    if (backgroundProgress_) backgroundProgress_->hide();
}

void PdfWindow::newDrawing() {
    if (foregroundProgressDepth_>0 || viewport_->interactionLocked()) return;
    if ((documentPath_.isEmpty()||documentModified_)&&!saveDocument(false)) return;
    auto *drawing = new ForgeCad::DrawingWindow(this);
    const auto error = drawing->createFromModel(documentPath_);
    if (!error.isEmpty()) { delete drawing; QMessageBox::warning(this,QStringLiteral("Tavola 2D"),error); return; }
    drawing->setAttribute(Qt::WA_DeleteOnClose); drawing->show();
}

void PdfWindow::openDrawing() {
    const auto path=ForgeCad::chooseLinkedDrawing(this,documentPath_,viewport_->currentDocument().version.documentId);
    if (path.isEmpty()) return;
    for (auto *child:children()) if (auto *drawing=dynamic_cast<ForgeCad::DrawingWindow *>(child))
        if (drawing->filePath()==ForgeCad::canonicalDocumentPath(path) && drawing->document().sheet.drawing.version.checkinId.isNull()) { drawing->show(); drawing->raise(); drawing->activateWindow(); return; }
    auto *drawing=new ForgeCad::DrawingWindow(this); const auto error=drawing->openPath(path);
    if (!error.isEmpty()) { delete drawing; QMessageBox::warning(this,QStringLiteral("Apri tavola 2D"),error); return; }
    drawing->setAttribute(Qt::WA_DeleteOnClose); drawing->show();
}

void PdfWindow::newDocument() {
    if (!maybeSaveChanges()) return;
    clearDocumentRecovery();
    loadingDocument_ = true;
    viewport_->setDocumentReadOnly(false);
    DocumentState empty;
    empty.orientation = defaultAxesOrientation();
    empty.orientationSet = true;
    empty.lengthUnit = defaultLengthUnit();
    empty.lengthUnitSet = true;
    viewport_->loadDocument(empty);
    loadingDocument_ = false;
    documentLock_.reset();
    documentDiskFingerprint_.clear();
    documentPath_.clear();
    documentModified_ = false;
    refreshDocumentEditAccess();
    updateWindowTitle();
}

void PdfWindow::openDocument() {
    QFileDialog dialog(this, QStringLiteral("Apri"), QFileInfo(documentPath_).absolutePath(),
                       QStringLiteral("Documenti %1 (*.prt);;Tutti i file (*)").arg(QGuiApplication::applicationDisplayName()));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    // I dialoghi nativi non espongono un'area portabile per un widget
    // accessorio; il dialogo Qt consente la stessa anteprima su tutti i SO.
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    auto *preview = new DocumentFilePreview(&dialog);
    if (auto *grid = qobject_cast<QGridLayout *>(dialog.layout()))
        grid->addWidget(preview, 0, grid->columnCount(), grid->rowCount(), 1);
    else
        dialog.layout()->addWidget(preview);
    connect(&dialog, &QFileDialog::currentChanged, preview, &DocumentFilePreview::setPath);
    dialog.resize(1050, 650);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    const QString path = dialog.selectedFiles().first();
    if (path.isEmpty()) return;
    openDocumentPath(path);
}

QString PdfWindow::gpuStatusText() const {
    QString cuda;
    if (!forgecad_cuda_available()) cuda = FORGECAD_HAS_CUDA ? QStringLiteral("CUDA compilato, GPU runtime non disponibile")
                                                              : QString::fromUtf8(forgecad_cuda_backend());
    else if (ForgeCad::cudaTessellationEnabled()) cuda = QStringLiteral("CUDA: tassellazione su GPU");
    else cuda = QStringLiteral("CUDA disponibile (tassellazione su CPU)");
    return QStringLiteral("%1 | OpenGL: %2").arg(cuda, glRenderer_);
}

bool PdfWindow::openDocumentPath(const QString &path) {
    const QString resolved = ForgeCad::canonicalDocumentPath(path);
    if (documentLock_ && documentLock_->owns(resolved)) {
        rememberRecentDocument(resolved);
        return true;
    }
    if (!maybeSaveChanges()) return false;
    std::unique_ptr<ForgeCad::DocumentFileLock> candidate;
    const QString fileName = QFileInfo(path).fileName();
    beginForegroundProgress(QStringLiteral("Lettura di %1... 0%").arg(fileName), 100);
    updateForegroundProgress(QStringLiteral("Lettura del file %1... 5%").arg(fileName), 5);
    DocumentState state;
    QString error = ForgeCad::loadDocumentFile(resolved, state);
    QByteArray fingerprint;
    if(error.isEmpty()){
        ForgeCad::WorkspaceEditAccess access;
        ForgeCad::workspaceDocumentEditAccess(resolved,state.version,state.baseCheckinId,access);
        ForgeCad::WorkspaceRevisionStatus status;
        const auto statusError=ForgeCad::workspaceRevisionStatus(resolved,state,status);
        // Un check-out persistente viene ripreso soltanto su comando esplicito.
        if(!access.readOnly&&(statusError.isEmpty()?!status.hasSession:state.baseCheckinId.isNull())){
            candidate=std::make_unique<ForgeCad::DocumentFileLock>(resolved);
            if(!candidate->acquire().isEmpty())candidate.reset();
            else{
                error=ForgeCad::loadDocumentFile(resolved,state);
                if(error.isEmpty())error=ForgeCad::documentFileFingerprint(resolved,fingerprint);
            }
        }
    }
    if (!error.isEmpty()) {
        endForegroundProgress();
        QMessageBox::warning(this, QStringLiteral("Apri"), error);
        return false;
    }
    clearDocumentRecovery();
    updateForegroundProgress(QStringLiteral("Definizione caricata, preparazione della geometria... 15%"), 15);
    // I file senza orientamento (versioni vecchie) prendono quello predefinito.
    if (!state.orientationSet) {
        state.orientation = defaultAxesOrientation();
        state.orientationSet = true;
    }
    loadingDocument_ = true;
    viewport_->setDocumentReadOnly(false);
    viewport_->loadDocument(std::move(state), [this](int completed, int total, const QString &name) {
        const int percent = 15 + (total > 0 ? 80 * completed / total : 80);
        const QString detail = name.isEmpty() ? QStringLiteral("Costruzione della scena")
                                               : QStringLiteral("Costruzione di %1").arg(name);
        updateForegroundProgress(QStringLiteral("%1... %2%").arg(detail).arg(percent), percent);
    });
    loadingDocument_ = false;
    updateForegroundProgress(QStringLiteral("Completamento della vista... 98%"), 98);
    updateForegroundProgress(QStringLiteral("Documento caricato. 100%"), 100);
    endForegroundProgress();
    documentLock_ = std::move(candidate);
    documentDiskFingerprint_=fingerprint;
    documentPath_ = resolved;
    documentModified_ = false;
    refreshDocumentEditAccess();
    rememberRecentDocument(resolved);
    updateWindowTitle();
    int failed = 0;
    for (const ExtrusionObject &body : viewport_->extrusions()) failed += body.error.isEmpty() ? 0 : 1;
    statusBar()->showMessage(failed == 0 ? QStringLiteral("Aperto %1").arg(path)
                                         : QStringLiteral("Aperto %1: %2 corpi non si rigenerano (vedi l'albero)").arg(path).arg(failed), 6000);
    return true;
}

bool PdfWindow::saveDocument(bool askPath) {
    QString path = documentPath_;
    if (askPath || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Salva con nome"),
                                            path.isEmpty() ? QStringLiteral("Senza nome.prt") : path,
                                            QStringLiteral("Documenti %1 (*.prt)").arg(QGuiApplication::applicationDisplayName()));
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().compare(QLatin1String(ForgeCad::kDocumentSuffix), Qt::CaseInsensitive) != 0)
            path += QStringLiteral(".") + QLatin1String(ForgeCad::kDocumentSuffix);
    }
    return saveDocumentPath(path);
}

bool PdfWindow::saveDocumentPath(const QString &path) {
    const QString resolved = ForgeCad::canonicalDocumentPath(path);
    refreshDocumentEditAccess();
    if(!documentPath_.isEmpty()&&resolved==documentPath_&&documentEditAccess_.readOnly){
        statusBar()->showMessage(documentEditAccess_.reason,10000);return false;
    }
    std::unique_ptr<ForgeCad::DocumentFileLock> candidate;
    const ForgeCad::DocumentFileLock *lock = documentLock_.get();
    if (!lock || lock->path() != resolved) {
        candidate = std::make_unique<ForgeCad::DocumentFileLock>(resolved);
        const QString error = candidate->acquire();
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Salva — accesso esclusivo"), error);
            return false;
        }
        lock = candidate.get();
    }
    beginForegroundProgress(QStringLiteral("Salvataggio di %1...").arg(QFileInfo(path).fileName()));
    DocumentState saved = viewport_->currentDocument();
    if (!documentPath_.isEmpty() && ForgeCad::canonicalDocumentPath(documentPath_) != resolved)
        ForgeCad::forkDocumentIdentity(saved);
    const QString error = ForgeCad::saveDocumentFile(resolved, saved,
                                                     QSettings().value(QStringLiteral("document/saveBodies"), true).toBool(), lock);
    endForegroundProgress();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Salva"), error);
        return false;
    }
    if (candidate) documentLock_ = std::move(candidate);
    documentDiskFingerprint_.clear();
    ForgeCad::documentFileFingerprint(resolved,documentDiskFingerprint_);
    viewport_->adoptDocumentIdentity(saved.version, saved.baseCheckinId);
    documentPath_ = resolved;
    documentModified_ = false;
    refreshDocumentEditAccess();
    rememberRecentDocument(resolved);
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("Salvato %1 (%2 byte)").arg(path).arg(QFileInfo(path).size()), 6000);
    clearDocumentRecovery();
    return true;
}

QString PdfWindow::acquireDocumentWriteAccess() {
    if(documentLock_&&documentLock_->owns(documentPath_))return {};
    auto candidate=std::make_unique<ForgeCad::DocumentFileLock>(documentPath_);
    auto error=candidate->acquire();if(!error.isEmpty())return error;
    QByteArray fingerprint;error=ForgeCad::documentFileFingerprint(documentPath_,fingerprint);
    if(!error.isEmpty())return error;
    if(documentModified_){
        if(documentDiskFingerprint_.isEmpty()||fingerprint!=documentDiskFingerprint_)
            return QStringLiteral("La copia su disco e' cambiata. Salvare le modifiche locali con un altro nome prima di riprendere il check-out.");
    }else if(documentDiskFingerprint_.isEmpty()||fingerprint!=documentDiskFingerprint_){
        DocumentState current;error=ForgeCad::loadDocumentFile(documentPath_,current);
        if(!error.isEmpty())return error;
        if(current.version.documentId!=viewport_->documentVersion().documentId||!current.version.checkinId.isNull())
            return QStringLiteral("Il documento su disco ha un'altra identita'. Riaprirlo prima di modificarlo.");
        // La rilettura avviene sotto esclusiva, prima di prenotare la base.
        beginForegroundProgress(QStringLiteral("Aggiornamento della copia di lavoro..."));
        loadingDocument_=true;viewport_->setDocumentReadOnly(false);viewport_->loadDocument(std::move(current));loadingDocument_=false;
        endForegroundProgress();
    }
    documentDiskFingerprint_=fingerprint;documentLock_=std::move(candidate);return {};
}

QString PdfWindow::runDocumentRevisionAction(DocumentRevisionAction action, const QString &first, const QString &second) {
    if (foregroundProgressDepth_>0 || viewport_->interactionLocked()) return QStringLiteral("Attendere la conclusione dell'operazione in corso.");
    if (documentPath_.isEmpty()) return QStringLiteral("Salvare prima il documento nel workspace.");
    if(!viewport_->documentVersion().checkinId.isNull())return QStringLiteral("La revisione storica e' in sola lettura.");
    const bool owned=documentLock_&&documentLock_->owns(documentPath_);
    if(!owned&&action==DocumentRevisionAction::Checkin)return QStringLiteral("Riprendere il check-out prima di pubblicare le modifiche.");
    const auto lockError=acquireDocumentWriteAccess();
    if(!lockError.isEmpty()){refreshDocumentEditAccess();return lockError;}
    auto working = viewport_->currentDocument();
    const auto oldBase = working.baseCheckinId;
    const bool bodies = QSettings().value(QStringLiteral("document/saveBodies"),true).toBool();
    ForgeCad::RevisionRecord record; bool matchesWorking = true;
    viewport_->setInteractionLocked(true);
    beginForegroundProgress(QStringLiteral("Aggiornamento delle revisioni..."));
    QString error;
    switch (action) {
    case DocumentRevisionAction::Checkout:
        error = ForgeCad::checkoutWorkspaceDocument(documentPath_,working,first); break;
    case DocumentRevisionAction::Checkin:
        error = ForgeCad::checkinWorkspaceDocument(documentPath_,working,first,second,record,bodies,documentLock_.get()); break;
    case DocumentRevisionAction::CompletePending:
        error = ForgeCad::completeWorkspaceCheckin(documentPath_,working,record,matchesWorking,bodies,documentLock_.get()); break;
    case DocumentRevisionAction::CancelCheckout:
        error = ForgeCad::cancelWorkspaceCheckout(documentPath_,working); break;
    }
    endForegroundProgress(); viewport_->setInteractionLocked(false);
    viewport_->adoptDocumentIdentity(working.version,working.baseCheckinId);
    if (working.baseCheckinId!=oldBase && !error.isEmpty()) documentModified_=true;
    if (error.isEmpty()) {
        if (action==DocumentRevisionAction::Checkin || action==DocumentRevisionAction::CompletePending) {
            documentModified_=false;
            if(matchesWorking)clearDocumentRecovery();
            statusBar()->showMessage(matchesWorking
                ? QStringLiteral("Check-in %1, revisione %2, completato.").arg(record.sequence).arg(record.technicalRevision)
                : QStringLiteral("Check-in pendente completato. Le modifiche successive restano nella copia di lavoro."),10000);
        } else statusBar()->showMessage(action==DocumentRevisionAction::Checkout
                ? QStringLiteral("Check-out attivo: %1.").arg(first)
                : QStringLiteral("Check-out annullato; modifiche locali mantenute."),6000);
    }
    if(error.isEmpty()){
        documentDiskFingerprint_.clear();ForgeCad::documentFileFingerprint(documentPath_,documentDiskFingerprint_);
    }
    if(!owned&&!error.isEmpty())documentLock_.reset();
    refreshDocumentEditAccess();updateWindowTitle(); return error;
}

void PdfWindow::installDocumentRevisionPanel(QMenu *viewMenu) {
    documentRevisionDock_=new QDockWidget(QStringLiteral("Revisioni"),this);documentRevisionDock_->setObjectName("documentRevisionDock");
    documentRevisionDock_->setMinimumWidth(300);auto *body=new QWidget(documentRevisionDock_);auto *layout=new QVBoxLayout(body);
    documentRevisionPanelStatus_=new QLabel(body);documentRevisionPanelStatus_->setObjectName("documentRevisionPanelStatus");documentRevisionPanelStatus_->setWordWrap(true);documentRevisionPanelStatus_->setTextFormat(Qt::PlainText);layout->addWidget(documentRevisionPanelStatus_);
    auto *commands=new QHBoxLayout;
    for(const auto &entry:QList<QPair<QString,QString>>{{"revisionCheckoutAction","open"},{"revisionCheckinAction","save"},{"revisionCompleteAction","rebuild"},{"revisionCancelAction","undo"}}){
        auto *action=findChild<QAction *>(entry.first);action->setIcon(ForgeCad::commandIcon(entry.second));auto *button=new QToolButton(body);button->setDefaultAction(action);button->setToolTip(action->text());commands->addWidget(button);
    }
    layout->addLayout(commands);
    documentRevisionHistory_=new QTableWidget(0,5,body);documentRevisionHistory_->setObjectName("documentRevisionHistory");
    documentRevisionHistory_->setHorizontalHeaderLabels({QStringLiteral("N."),QStringLiteral("Rev."),QStringLiteral("Autore"),QStringLiteral("Data"),QStringLiteral("Commento")});
    documentRevisionHistory_->setEditTriggers(QAbstractItemView::NoEditTriggers);documentRevisionHistory_->setSelectionBehavior(QAbstractItemView::SelectRows);documentRevisionHistory_->setSelectionMode(QAbstractItemView::SingleSelection);documentRevisionHistory_->horizontalHeader()->setStretchLastSection(true);layout->addWidget(documentRevisionHistory_,1);
    documentOpenRevision_=new QPushButton(ForgeCad::commandIcon("open"),QStringLiteral("Apri revisione selezionata"),body);documentOpenRevision_->setObjectName("documentOpenSelectedRevision");documentOpenRevision_->setToolTip(QStringLiteral("Apre il modello archiviato in una finestra separata in sola lettura."));documentOpenRevision_->setEnabled(false);layout->addWidget(documentOpenRevision_);
    connect(documentOpenRevision_,&QPushButton::clicked,this,&PdfWindow::openSelectedDocumentRevision);
    connect(documentRevisionHistory_,&QTableWidget::cellDoubleClicked,this,[this](int,int){openSelectedDocumentRevision();});
    connect(documentRevisionHistory_,&QTableWidget::itemSelectionChanged,this,[this]{documentOpenRevision_->setEnabled(documentRevisionHistory_->currentRow()>=0&&!foregroundProgressDepth_&&!viewport_->interactionLocked());});
    auto *refresh=new QPushButton(QStringLiteral("Aggiorna stato"),body);refresh->setObjectName("documentRefreshRevisions");layout->addWidget(refresh);connect(refresh,&QPushButton::clicked,this,[this]{refreshDocumentEditAccess();updateWindowTitle();});
    documentRevisionDock_->setWidget(body);addDockWidget(Qt::RightDockWidgetArea,documentRevisionDock_);
    auto *toggle=documentRevisionDock_->toggleViewAction();toggle->setObjectName("documentRevisionPanelAction");toggle->setText(QStringLiteral("Pannello revisioni"));viewMenu->addAction(toggle);
    documentRevisionDock_->setVisible(QSettings().value("revisions/modelPanelVisible",true).toBool());
    connect(documentRevisionDock_,&QDockWidget::visibilityChanged,this,[this](bool visible){if(isVisible())QSettings().setValue("revisions/modelPanelVisible",visible);});
}
void PdfWindow::refreshDocumentRevisionPanel() {
    if(!documentRevisionDock_||!viewport_||foregroundProgressDepth_||viewport_->interactionLocked())return;
    DocumentState identity;identity.version=viewport_->documentVersion();identity.baseCheckinId=viewport_->documentBaseCheckinId();
    const bool historical=!identity.version.checkinId.isNull();QVector<ForgeCad::RevisionRecord> records;ForgeCad::WorkspaceRevisionStatus status;
    QString message,error;bool known=false;
    if(documentPath_.isEmpty())message=QStringLiteral("Nuovo modello: salva il documento per gestire le revisioni.");
    else{
        error=ForgeCad::workspaceDocumentHistory(documentPath_,identity,records);
        if(historical)message=QStringLiteral("Copia storica in sola lettura.");
        else{
            const auto statusError=ForgeCad::workspaceRevisionStatus(documentPath_,identity,status);known=statusError.isEmpty();
            if(!known)message=QStringLiteral("Stato non verificato: %1").arg(statusError);
            else if(status.hasPending)message=QStringLiteral("Check-in pendente: completa l'operazione.");
            else if(status.hasSession)message=QStringLiteral("Check-out: %1\n%2").arg(status.session.checkout.owner,status.phase==ForgeCad::WorkspaceCheckoutPhase::Active?QStringLiteral("Copia di lavoro prenotata"):QStringLiteral("Prenotazione da completare"));
            else message=documentEditAccess_.readOnly?documentEditAccess_.reason:QStringLiteral("Nessun check-out attivo.");
            QString next;if(ForgeCad::nextWorkspaceRevisionLabel(documentPath_,identity.version.documentId,next).isEmpty())message+=QStringLiteral("\nProssima revisione automatica: %1").arg(next);
        }
    }
    const auto base=historical?identity.version.checkinId:identity.baseCheckinId;
    for(const auto &record:records)if(record.version.checkinId==base)message+=QStringLiteral("\n%1: %2").arg(historical?QStringLiteral("Revisione"):QStringLiteral("Base"),record.technicalRevision);
    if(documentModified_)message+=QStringLiteral("\nModifiche locali da salvare.");
    documentRevisionPanelStatus_->setText(message);documentRevisionPanelStatus_->setToolTip(error);
    const auto selected=documentRevisionHistory_->currentRow()>=0&&documentRevisionHistory_->item(documentRevisionHistory_->currentRow(),0)?documentRevisionHistory_->item(documentRevisionHistory_->currentRow(),0)->data(Qt::UserRole).toUuid():QUuid();
    QSignalBlocker lock(documentRevisionHistory_);documentRevisionHistory_->setRowCount(records.size());int selection=records.size()-1;
    for(int row=0;row<records.size();++row){const auto &record=records[row];const QStringList cells{QString::number(record.sequence),record.technicalRevision,record.author,record.createdUtc.toLocalTime().toString("yyyy-MM-dd HH:mm"),record.comment};for(int col=0;col<cells.size();++col)documentRevisionHistory_->setItem(row,col,new QTableWidgetItem(cells[col]));auto *item=documentRevisionHistory_->item(row,0);item->setData(Qt::UserRole,record.version.checkinId);item->setData(Qt::UserRole+1,record.version.documentId);if(record.version.checkinId==selected)selection=row;}
    documentRevisionHistory_->resizeColumnsToContents();documentRevisionHistory_->setColumnWidth(4,200);documentRevisionHistory_->resizeRowsToContents();if(selection>=0)documentRevisionHistory_->selectRow(selection);
    documentOpenRevision_->setEnabled(selection>=0);
    const bool writableVersion=!historical&&!documentPath_.isEmpty();
    findChild<QAction *>("revisionCheckoutAction")->setEnabled(writableVersion&&known&&!status.hasPending&&(!status.hasSession||status.phase==ForgeCad::WorkspaceCheckoutPhase::Reserving||!documentLock_));
    findChild<QAction *>("revisionCheckinAction")->setEnabled(writableVersion&&known&&documentLock_&&!documentEditAccess_.readOnly&&status.hasSession&&!status.hasPending&&status.phase==ForgeCad::WorkspaceCheckoutPhase::Active);
    findChild<QAction *>("revisionCompleteAction")->setEnabled(writableVersion&&known&&status.hasPending);
    findChild<QAction *>("revisionCancelAction")->setEnabled(writableVersion&&known&&status.hasSession&&!status.hasPending);
}
void PdfWindow::openSelectedDocumentRevision() {
    if(foregroundProgressDepth_||viewport_->interactionLocked()||!documentRevisionHistory_)return;
    const int row=documentRevisionHistory_->currentRow();auto *item=row>=0?documentRevisionHistory_->item(row,0):nullptr;if(!item)return;
    const ForgeCad::DocumentVersion version{item->data(Qt::UserRole+1).toUuid(),item->data(Qt::UserRole).toUuid()};
    ForgeCad::WorkspacePaths paths;auto error=ForgeCad::locateDocumentWorkspace(documentPath_,paths);DocumentState source;
    if(error.isEmpty()){ForgeCad::RevisionStore store(paths.revisions);error=ForgeCad::readNativeRevision(store,version,source);}
    if(!error.isEmpty()){QMessageBox::warning(this,QStringLiteral("Revisioni"),error);return;}
    auto *window=new PdfWindow;window->loadingDocument_=true;window->viewport_->loadDocument(source);window->documentPath_=documentPath_;window->documentModified_=false;window->loadingDocument_=false;
    window->refreshDocumentEditAccess();window->updateWindowTitle();window->rebuildModelTree();window->setAttribute(Qt::WA_DeleteOnClose);window->show();
}

void PdfWindow::showDocumentRevisions() {
    QVector<ForgeCad::RevisionRecord> records;
    const auto error = ForgeCad::workspaceDocumentHistory(documentPath_,viewport_->currentDocument(),records);
    if (!error.isEmpty()) { QMessageBox::warning(this,QStringLiteral("Revisioni"),error); return; }
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Storico dei check-in"));
    dialog.setObjectName(QStringLiteral("documentRevisionHistoryDialog"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(int(records.size()),5,&dialog);
    table->setObjectName(QStringLiteral("documentRevisionHistoryTable"));
    table->setHorizontalHeaderLabels({QStringLiteral("Check-in"),QStringLiteral("Revisione"),QStringLiteral("Autore"),QStringLiteral("Data"),QStringLiteral("Commento")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    for (int row=0;row<records.size();++row) {
        const auto &record=records[records.size()-1-row];
        const QStringList values{QString::number(record.sequence),record.technicalRevision,record.author,
            record.createdUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),record.comment};
        for (int column=0;column<values.size();++column) table->setItem(row,column,new QTableWidgetItem(values[column]));
    }
    table->resizeColumnsToContents();table->horizontalHeader()->setStretchLastSection(true);layout->addWidget(table);
    if (records.isEmpty()) layout->addWidget(new QLabel(QStringLiteral("Nessun check-in archiviato per questo documento."),&dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);
    dialog.resize(760,360);dialog.exec();
}

void PdfWindow::closeEvent(QCloseEvent *event) {
    if(recoveryCloseReady_){documentLock_.reset();event->accept();return;}
    if(recoveryClosing_){event->ignore();return;}
    // Durante un calcolo il ciclo di eventi gira ancora (runWhileResponsive):
    // la chiusura aspetta la fine.
    if (foregroundProgressDepth_ > 0 || (viewport_ && viewport_->interactionLocked())) {
        event->ignore();
        return;
    }
    if (maybeSaveChanges()) {
        const auto attached=children();
        for (auto *child:attached) if (auto *drawing=dynamic_cast<ForgeCad::DrawingWindow *>(child)) {
            if (!drawing->close()) { event->ignore(); return; }
        }
        // La coda elimina anche una scrittura gia' in corso prima di uscire.
        // Il ciclo eventi continua a rispondere; nessun wait/join sul disco.
        recoveryClosing_=true;viewport_->setInteractionLocked(true);setEnabled(false);
        recoveryCloseRevision_=clearDocumentRecovery();event->ignore();
    }
    else event->ignore();
}

quint64 PdfWindow::clearDocumentRecovery() {
    if(!recoveredSource_.directory.isEmpty()){recovery_->discard(recoveredSource_);recoveredSource_={};}
    return recovery_->reset();
}
void PdfWindow::showRecoveryDocuments(bool automatic) {
    if(recoveryScanPending_||foregroundProgressDepth_||viewport_->interactionLocked())return;
    recoveryScanPending_=true;recoveryScanAutomatic_=automatic;recovery_->scan();
}

// La ricostruzione dell'albero cancella e ricrea le voci: non si puo' fare
// dentro un segnale dell'albero stesso (per esempio itemChanged del clic sulla
// casella di visibilita'), perche' Qt continua a usare la voce dopo il segnale.
// Per questo viene sempre rimandata al giro successivo del ciclo di eventi.
void PdfWindow::scheduleModelTreeRebuild() {
    if (treeRebuildPending_) return;
    treeRebuildPending_ = true;
    QTimer::singleShot(0, this, [this] {
        treeRebuildPending_ = false;
        rebuildModelTree();
    });
}

// Ricostruisce l'albero dallo stato del viewport: origine e piani restano fissi,
// schizzi ed estrusioni seguono il documento (anche dopo Undo/Redo).
void PdfWindow::renameTreeItem(QTreeWidgetItem *item) {
    if(viewport_->documentReadOnly())return;
    if (!item) return;
    const int type = item->data(0, Qt::UserRole).toInt();
    const int index = item->data(0, Qt::UserRole + 1).toInt();
    if (type != kTreeSketch && type != kTreeExtrusion && type != kTreeBody) return;
    if (type == kTreeBody) {
        const QString current = viewport_->modelBodies().value(index).name;
        for (;;) {
            bool accepted = false;
            const QString name = QInputDialog::getText(this, QStringLiteral("Rinomina corpo"), QStringLiteral("Nome:"),
                                                       QLineEdit::Normal, current, &accepted);
            if (!accepted) return;
            const QString error = viewport_->renameModelBody(index, name);
            if (error.isEmpty()) return;
            QMessageBox::warning(this, QStringLiteral("Rinomina corpo"), error);
        }
    }
    const SceneObjectKind kind = type == kTreeSketch ? SceneObjectKind::Sketch : SceneObjectKind::Extrusion;
    const QString current = kind == SceneObjectKind::Sketch ? viewport_->sketches().value(index).name : viewport_->extrusions().value(index).name;
    for (;;) {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rinomina"), QStringLiteral("Nome:"), QLineEdit::Normal, current, &accepted);
        if (!accepted) return;
        const QString error = viewport_->renameObject(kind, index, name);
        if (error.isEmpty()) return;
        QMessageBox::warning(this, QStringLiteral("Rinomina"), error);
    }
}

// Sfondo dell'albero modello (colore scelto; il testo chiaro o scuro per restare leggibile).
void PdfWindow::applyTreeBackground(const QColor &color) {
    if (!modelTree_) return;
    if (!color.isValid()) {
        modelTree_->setStyleSheet(QString());
        return;
    }
    const double luminance = 0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
    modelTree_->setStyleSheet(QStringLiteral("QTreeWidget { background-color: %1; alternate-background-color: %1; color: %2; }")
                                  .arg(color.name(), luminance > 0.55 ? QStringLiteral("#10161c") : QStringLiteral("#e6eef5")));
}

void PdfWindow::rebuildModelTree() {
    // Ricreare le voci azzera temporaneamente i range e puo' far scorrere
    // l'albero verso la selezione: conserva la posizione prima di cancellarle.
    const int verticalPosition=modelTree_->verticalScrollBar()->value();
    const int horizontalPosition=modelTree_->horizontalScrollBar()->value();
    const int previousHistoryPosition=static_cast<StoryboardTree *>(modelTree_)->historyPosition();
    rebuildingTree_ = true;
    const auto addObject = [this](const QString &name, int type, int index, int checkState, QTreeWidgetItem *parent = nullptr) {
        auto *item = parent ? new QTreeWidgetItem(parent, {name}) : new QTreeWidgetItem(modelTree_, {name});
        item->setData(0, Qt::UserRole, type);
        item->setData(0, Qt::UserRole + 1, index);
        item->setFlags(item->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
        if (checkState >= 0) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, checkState ? Qt::Checked : Qt::Unchecked);
        }
        return item;
    };
    if (modelTree_->topLevelItemCount() == 0) {
        addObject(QStringLiteral("Origine (0, 0, 0)"), kTreeOrigin, 0, -1);
        for (int plane = 0; plane < planeNames().size(); ++plane)
            addObject(planeNames().at(plane), kTreePlane, plane, 1);
    }
    const int fixedItems = 1 + planeNames().size();
    const SceneSelection selection = viewport_->selection();
    // Stato aperto/chiuso delle voci, ripreso dopo la ricostruzione (chiave in UserRole + 2).
    const bool cursorSelected = StoryboardTree::isHistoryCursor(modelTree_->currentItem());
    QHash<QString, bool> expanded;
    const std::function<void(QTreeWidgetItem *)> remember = [&](QTreeWidgetItem *item) {
        const QString key = item->data(0, Qt::UserRole + 2).toString();
        if (!key.isEmpty()) expanded.insert(key, item->isExpanded());
        for (int child = 0; child < item->childCount(); ++child) remember(item->child(child));
    };
    for (int top = fixedItems; top < modelTree_->topLevelItemCount(); ++top) remember(modelTree_->topLevelItem(top));
    const auto restore = [&](QTreeWidgetItem *item, const QString &key, bool open) {
        item->setData(0, Qt::UserRole + 2, key);
        item->setExpanded(expanded.value(key, open));
    };
    while (modelTree_->topLevelItemCount() > fixedItems) delete modelTree_->takeTopLevelItem(fixedItems);
    const QVector<SketchObject> &sketches = viewport_->sketches();
    QSet<int> shownSketches;
    const auto addSketch = [&](int index, QTreeWidgetItem *parent) {
        if (index < 0 || index >= sketches.size() || shownSketches.contains(index)) return;
        shownSketches.insert(index);
        const SketchObject &sketch = sketches.at(index);
        const QString plane = sketch.plane != kFacePlane ? planeNames().value(sketch.plane)
                            : sketch.datumPlane >= 0 && sketch.datumPlane < viewport_->extrusions().size() ? viewport_->extrusions().at(sketch.datumPlane).name
                                                                                                         : QStringLiteral("Faccia di %1").arg(sketch.faceSource);
        QTreeWidgetItem *item = addObject(sketch.name + QStringLiteral(" [") + plane + QStringLiteral("]"), kTreeSketch, index,
                                          sketch.visible, parent);
        item->setIcon(0, ForgeCad::commandIcon(QStringLiteral("newSketch")));
        if ((selection.kind == SceneObjectKind::Sketch && selection.index == index)
            || viewport_->activeSketchIndex() == index) modelTree_->setCurrentItem(item);
    };
    const QVector<ExtrusionObject> &extrusions = viewport_->extrusions();
    const QVector<ModelBody> &modelBodies = viewport_->modelBodies();
    QTreeWidgetItem *referenceRoot = nullptr;
    int referenceCount = 0;
    for (const ExtrusionObject &feature : extrusions)
        if (feature.modelBodyId == 0) ++referenceCount;
    if (referenceCount > 0) {
        referenceRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Geometria di riferimento")});
        referenceRoot->setData(0, Qt::UserRole, kTreeInfo);
        referenceRoot->setData(0, kTreeBadgeRole, QString::number(referenceCount));
        referenceRoot->setToolTip(0, QStringLiteral("%1 piani di costruzione e curve di riferimento").arg(referenceCount));
        restore(referenceRoot, QStringLiteral("R"), false);  // chiusa di default
    }
    // Storia: tutte le feature dei corpi nell'ordine di calcolo, ognuna
    // preceduta dai suoi schizzi. I corpi stanno in un gruppo a parte.
    QTreeWidgetItem *historyRoot = nullptr, *bodiesRoot = nullptr;
    const QHash<quint64, quint64> roots = viewport_->storyboardRoots();
    QHash<quint64, int> bodyIndex;
    for (int index = 0; index < modelBodies.size(); ++index) bodyIndex.insert(modelBodies.at(index).id, index);
    if (!modelBodies.isEmpty()) {
        // Corpi subito sotto la geometria di riferimento, chiusi di default.
        bodiesRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Corpi")});
        bodiesRoot->setData(0, Qt::UserRole, kTreeInfo);
        restore(bodiesRoot, QStringLiteral("C"), false);
        historyRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Storia delle feature")});
        historyRoot->setData(0, Qt::UserRole, kTreeInfo);
        historyRoot->setToolTip(0, QStringLiteral("Le feature nell'ordine in cui sono calcolate, dall'alto verso il basso"));
        restore(historyRoot, QStringLiteral("H"), true);
        // Solo i corpi che esistono nella posizione corrente: quelli consumati da
        // una booleana, una fusione o una cucitura fanno parte di un altro.
        for (int index = 0; index < modelBodies.size(); ++index) {
            const ModelBody &body = modelBodies.at(index);
            if (roots.value(body.id, body.id) != body.id) continue;
            QString label = body.name;
            const int tip = viewport_->modelBodyTip(index);
            if (viewport_->historyPosition() < extrusions.size() && tip < 0) continue;
            if (tip >= 0) {
                const QPair<int, int> parts = viewport_->bodyComponents(tip);
                if (parts.first > 1) label += QStringLiteral("  (%1 solidi)").arg(parts.first);
                if (parts.second > 1) label += QStringLiteral("  (%1 superfici)").arg(parts.second);
            }
            QTreeWidgetItem *item = addObject(label, kTreeBody, index, body.visible, bodiesRoot);
            item->setIcon(0, ForgeCad::commandIcon(QStringLiteral("box")));
            item->setToolTip(0, tip >= 0 ? QStringLiteral("Risultato di \"%1\" nella storia").arg(extrusions.at(tip).name)
                                         : QStringLiteral("Tutte le feature del corpo sono soppresse"));
            if (tip >= 0 && !extrusions.at(tip).error.isEmpty()) item->setForeground(0, QColor(255, 150, 90));
        }
        bodiesRoot->setData(0, kTreeBadgeRole, QString::number(bodiesRoot->childCount()));
        bodiesRoot->setToolTip(0, QStringLiteral("%1 corpi alla fine della storia").arg(bodiesRoot->childCount()));
    }
    for (int index = 0; index < extrusions.size(); ++index) {
        const ExtrusionObject &body = extrusions.at(index);
        QString label = body.name;
        const bool reference = body.modelBodyId == 0;
        QTreeWidgetItem *parent = reference || !historyRoot ? referenceRoot : historyRoot;
        for (int sketch : viewport_->featureSketches(index)) addSketch(sketch, parent);
        // Il corpo in cui finisce la feature (anche attraverso una booleana).
        const quint64 root = roots.value(body.modelBodyId, body.modelBodyId);
        const ModelBody owner = modelBodies.value(bodyIndex.value(root, -1));
        const bool tip = !reference && body.visible && index < viewport_->historyPosition();
        // Le feature sono passi della storia, non solidi: il numero di solidi
        // o superfici separati sta nel gruppo dei corpi.
        // Con piu' corpi si dice in quale finisce la feature.
        if (!reference && !owner.name.isEmpty() && bodiesRoot && bodiesRoot->childCount() > 1) label += QStringLiteral("   \u2192 %1").arg(owner.name);
        if (tip) label += QStringLiteral("  \u25C0 risultato");
        // Lo stadio precedente dello stesso ramo e' implicito nella storia: non si ripete tra i dettagli.
        const auto previousStage = [&](int operand) {
            return !reference && operand >= 0 && operand < extrusions.size() && extrusions.at(operand).modelBodyId == body.modelBodyId;
        };
        const QString featureKey = QStringLiteral("F%1").arg(body.featureId);
        QTreeWidgetItem *item = addObject(body.error.isEmpty() ? label : label + QStringLiteral("  \u26A0"),
                                          kTreeExtrusion, index, reference ? body.visible : -1, parent);
        bool hasPreviousStage = false;
        for (int previous = 0; previous < index; ++previous)
            if (extrusions.at(previous).modelBodyId == body.modelBodyId) { hasPreviousStage = true; break; }
        if (!reference) {
            item->setFlags(item->flags() | Qt::ItemIsDropEnabled);
            if (hasPreviousStage) item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        }
        const QString icon = featureIconName(body);
        if (!icon.isEmpty()) item->setIcon(0, ForgeCad::commandIcon(icon));
        if (!tip && !reference && body.error.isEmpty()) item->setForeground(0, QColor(135, 150, 165));
        if (body.suppressed) {
            QFont font = item->font(0);
            font.setStrikeOut(true);
            item->setFont(0, font);
            item->setForeground(0, QColor(115, 125, 135));
        }
        if (!body.error.isEmpty()) {
            item->setForeground(0, QColor(255, 150, 90));
            item->setToolTip(0, QStringLiteral("Rigenerazione non riuscita: ") + body.error);
        }
        if (body.operation < 0 && body.error.isEmpty()) {
            if (body.feature == BodyFeature::Revolution) {
                item->setToolTip(0, QStringLiteral("Rivoluzione di %1 di %2\u00B0").arg(sketches.value(body.sketchIndex).name).arg(body.revolveAngle));
            } else if (body.feature == BodyFeature::Blend) {
                QString size = ForgeCad::formatLength(body.blendSize);
                if (body.blendChamfer && body.chamferSpec.mode == 1) size += QStringLiteral(" x %1").arg(ForgeCad::formatLength(body.chamferSpec.second));
                if (body.blendChamfer && body.chamferSpec.mode == 2) size += QStringLiteral(" a %1\u00B0").arg(body.chamferSpec.second);
                item->setToolTip(0, QStringLiteral("%1 %2 su %3 spigoli")
                    .arg(body.blendChamfer ? QStringLiteral("Smusso di") : QStringLiteral("Raccordo di raggio"))
                    .arg(size).arg(body.blendEdges.size()));
            } else if (body.feature == BodyFeature::Scale) {
                static const QStringList centers = {QStringLiteral("all'origine"), QStringLiteral("al baricentro"), QStringLiteral("attorno al punto")};
                item->setToolTip(0, QStringLiteral("Scala %1 %2").arg(body.scaleFactor).arg(centers.value(body.scaleCenterMode)));
            } else if (body.feature == BodyFeature::SheetTrim) {
                const QString tool = body.secondBody >= 0 ? extrusions.value(body.secondBody).name
                    : body.sketchIndex >= 0 ? sketches.value(body.sketchIndex).name : planeNames().value(body.trimPlane);
                item->setToolTip(0, QStringLiteral("Superficie tagliata da %1%2").arg(tool, body.notice.isEmpty() ? QString() : QStringLiteral("\n") + body.notice));
            } else if (body.feature == BodyFeature::SurfaceOffset) {
                const QString operation = body.distance == 0.0 ? QStringLiteral("Copia facce")
                                                               : QStringLiteral("Offset di %1").arg(ForgeCad::formatLength(body.distance));
                item->setToolTip(0, QStringLiteral("%1 da %2 (%3)").arg(operation, extrusions.value(body.firstBody).name)
                    .arg(body.offsetFaces.isEmpty() ? QStringLiteral("tutte le facce") : QStringLiteral("%1 facce").arg(body.offsetFaces.size())));
            } else if (body.feature == BodyFeature::Draft) {
                item->setToolTip(0, QStringLiteral("Sformo di %1° su %2 facce").arg(body.draftAngle).arg(body.offsetFaces.size()));
            } else if (body.feature == BodyFeature::Thicken) {
                static const char *sides[] = {"nel verso", "nel verso opposto", "meta' per parte"};
                item->setToolTip(0, QStringLiteral("Spessore %1 %2 lungo %3").arg(ForgeCad::formatLength(body.distance), QString::fromLatin1(sides[qBound(0, body.thickenSide, 2)]),
                    body.thickenDirection.kind < 0 ? QStringLiteral("la normale") : ForgeCad::geometryRefText(body.thickenDirection, sketches, extrusions)));
            } else if (body.feature == BodyFeature::SpatialSketch) {
                item->setToolTip(0,QStringLiteral("Schizzo 3D libero: %1 entita'").arg(body.spatialSketch.entities.size()));
            } else if (body.feature == BodyFeature::ExtractedCurve) {
                item->setToolTip(0, QStringLiteral("Curva 3D associativa da %1%2").arg(
                    ForgeCad::geometryRefText(body.curveExtraction.source, sketches, extrusions),
                    body.curveExtraction.trimmed ? QStringLiteral("; taglio tramite segmento") : QString()));
            } else if (body.feature == BodyFeature::ProjectedCurve) {
                item->setToolTip(0, QStringLiteral("Proiezione di %1 su %2%3").arg(sketches.value(body.sketchIndex).name, extrusions.value(body.firstBody).name,
                                                                                    body.notice.isEmpty() ? QString() : QStringLiteral("\n") + body.notice));
            } else if (body.feature == BodyFeature::Shell) {
                item->setToolTip(0, QStringLiteral("Guscio di spessore %1, %2").arg(ForgeCad::formatLength(body.distance))
                    .arg(body.offsetFaces.isEmpty() ? QStringLiteral("chiuso") : QStringLiteral("%1 facce aperte").arg(body.offsetFaces.size())));
            } else if (body.feature == BodyFeature::Thread) {
                item->setToolTip(0, QStringLiteral("%1: passo %2, lunghezza %3%4")
                    .arg(body.thread.designation.isEmpty() ? QStringLiteral("Filettatura") : body.thread.designation,
                         ForgeCad::formatLength(body.thread.pitch), ForgeCad::formatLength(body.thread.length),
                         body.thread.leftHanded ? QStringLiteral(", sinistrorsa") : QString()));
            } else if (body.feature == BodyFeature::BoundarySurface) {
                item->setToolTip(0, QStringLiteral("Superficie tra %1 curve").arg(body.planarRefs.size()));
            } else if (body.feature == BodyFeature::FillSurface) {
                item->setToolTip(0, QStringLiteral("Riempimento di %1 curve, %2 guide, %3 facce adiacenti (%4)%5")
                    .arg(body.planarRefs.size()).arg(body.ruledFirst.size()).arg(body.ruledSecond.size())
                    .arg(QStringList{QStringLiteral("contatto"), QStringLiteral("tangenza"), QStringLiteral("curvatura")}.value(body.fillContinuity))
                    .arg(body.notice.isEmpty() ? QString() : QStringLiteral("\n") + body.notice));
            } else if (body.feature == BodyFeature::DeleteFace) {
                item->setToolTip(0, QStringLiteral("%1 facce tolte da %2").arg(body.offsetFaces.size()).arg(extrusions.value(body.firstBody).name));
            } else if (body.feature == BodyFeature::Ruled) {
                item->setToolTip(0, QStringLiteral("Superficie %1 tra %2 e %3 tratti")
                    .arg(body.loftStartContinuity || body.loftEndContinuity ? QStringLiteral("raccordata") : QStringLiteral("rigata"))
                    .arg(body.ruledFirst.size()).arg(body.ruledSecond.size()));
            } else if (body.feature == BodyFeature::PlanarSurface) {
                item->setToolTip(0, body.planarRefs.isEmpty() ? QStringLiteral("Superficie planare dello schizzo %1").arg(sketches.value(body.sketchIndex).name)
                                                              : QStringLiteral("Superficie planare da %1 bordi").arg(body.planarRefs.size()));
            } else if (body.feature == BodyFeature::Sew) {
                item->setToolTip(0, QStringLiteral("%1 superfici cucite (tolleranza %2)%3").arg(1 + body.booleanTools.size()).arg(ForgeCad::formatLength(body.sewTolerance))
                    .arg(body.solid ? QStringLiteral(": solido") : QString()));
            } else if (body.feature == BodyFeature::SheetExtend) {
                item->setToolTip(0, QStringLiteral("%1 bordi estesi di %2 (%3)").arg(body.blendEdges.size()).arg(ForgeCad::formatLength(body.blendSize))
                    .arg(body.extendLinear ? QStringLiteral("lineare") : QStringLiteral("stessa superficie")));
            } else if (body.feature == BodyFeature::Helix) {
                double pitch, turns, height;
                ForgeCad::helixDimensions(body.helix, pitch, turns, height);
                item->setToolTip(0, body.helix.spiral ? QStringLiteral("Spirale: passo %1, %2 giri").arg(ForgeCad::formatLength(pitch)).arg(turns)
                                                      : QStringLiteral("Elica%1: passo %2, %3 giri, altezza %4%5")
                                                            .arg(body.helix.taper != 0.0 ? QStringLiteral(" conica") : QString())
                                                            .arg(ForgeCad::formatLength(pitch)).arg(turns).arg(ForgeCad::formatLength(height))
                                                            .arg(body.helix.leftHanded ? QStringLiteral(", sinistrorsa") : QString()));
            } else if (body.feature == BodyFeature::Sweep) {
                static const QStringList modes = {QStringLiteral("torsione minima"), QStringLiteral("Frenet"), QStringLiteral("orientamento costante")};
                item->setToolTip(0, QStringLiteral("Sweep (%1)").arg(modes.value(body.sweepMode)));
            } else if (body.feature == BodyFeature::Loft) {
                const int guideCount = body.loftGuides.size() + body.loftGuidePaths.size() + body.loftEdgeGuides.size();
                QStringList guideModes;
                for (int index : ForgeCad::loftGuideSequence(body))
                    guideModes << QStringLiteral("G%1 (%2 %)").arg(body.loftGuideContinuities.value(index, body.loftGuideContinuity))
                        .arg(100 * body.loftGuideInfluences.value(index, body.loftGuideInfluence), 0, 'g', 3);
                item->setToolTip(0, QStringLiteral("Loft %1: %2 sezioni, %3 guide, estremità G%4/G%5, guide %6")
                                        .arg(body.loftRuled && !body.loftStartContinuity && !body.loftEndContinuity ? QStringLiteral("rigato") : QStringLiteral("liscio"))
                                        .arg(body.loftSections.isEmpty()?body.loftSketches.size():body.loftSections.size()).arg(guideCount)
                                        .arg(body.loftStartContinuity == 3 ? QStringLiteral("1 (facce)") : QString::number(body.loftStartContinuity))
                                        .arg(body.loftEndContinuity == 3 ? QStringLiteral("1 (facce)") : QString::number(body.loftEndContinuity))
                                        .arg(guideModes.join(QStringLiteral(", "))));
            } else if (body.feature == BodyFeature::Imported) {
                item->setToolTip(0, QStringLiteral("%1 importato da %2").arg(body.solid ? QStringLiteral("Solido") : QStringLiteral("Superficie"), body.importSource));
            } else if (body.feature == BodyFeature::Pattern) {
                const PatternParameters &p = body.pattern;
                QString tip = p.kind == 0 ? QStringLiteral("Ripetizione lineare: %1 x passo %2").arg(p.count).arg(ForgeCad::formatLength(p.spacing))
                              : p.kind == 1 ? QStringLiteral("Ripetizione circolare: %1 istanze, %2 %3\u00B0").arg(p.count)
                                                  .arg(p.spread ? QStringLiteral("angolo totale") : QStringLiteral("passo")).arg(p.angle)
                                            : QStringLiteral("Specchio%1").arg(p.keepOriginal || p.featureOnly ? QString() : QStringLiteral(" (solo l'immagine)"));
                if (p.kind == 0 && p.count2 > 1) tip += QStringLiteral(", %1 x passo %2 nella seconda direzione").arg(p.count2).arg(ForgeCad::formatLength(p.spacing2));
                if (p.featureOnly) tip += QStringLiteral(" (della funzione)");
                item->setToolTip(0, tip);
            } else if (body.feature == BodyFeature::DatumPlane) {
                const ForgeCad::DatumMode mode = ForgeCad::datumModes().value(body.datum.mode);
                QString tip = QStringLiteral("Piano di costruzione: %1").arg(mode.name.toLower());
                if (mode.distance) tip += QStringLiteral(", distanza %1").arg(ForgeCad::formatLength(body.datum.distance));
                if (mode.angle) tip += QStringLiteral(", angolo %1\u00B0").arg(body.datum.angle);
                item->setToolTip(0, tip);
            } else if (body.feature == BodyFeature::Primitive) {
                item->setToolTip(0, QStringLiteral("Origine (%1, %2, %3), %4")
                    .arg(ForgeCad::formatLength(body.primitive.origin[0])).arg(ForgeCad::formatLength(body.primitive.origin[1]))
                    .arg(ForgeCad::formatLength(body.primitive.origin[2]))
                    .arg(planeNames().value(body.primitive.plane)));
            }
        }
        // Estrusione fino a un riferimento, oppure estrusione/sweep fusa con altri solidi.
        if (body.operation < 0
            && ((body.feature == BodyFeature::Extrusion && (body.extent != 0 || body.extrudeSides != 0))
                || (body.feature == BodyFeature::Revolution && body.revolveAxis == kRevolveAxisReference)
                || ((body.feature == BodyFeature::Extrusion || body.feature == BodyFeature::Revolution || body.feature == BodyFeature::Sweep)
                    && body.mergeOperation != 0 && !body.mergeBodies.isEmpty()))) {
            QStringList children;
            if (body.feature == BodyFeature::Extrusion && body.extrudeSides == 1)
                children.append(QStringLiteral("Simmetrica: %1 in tutto").arg(ForgeCad::formatLength(body.distance)));
            if (body.feature == BodyFeature::Extrusion && body.extent != 0)
                children.append(QStringLiteral("Fino a: ") + ForgeCad::geometryRefText(body.extentRef, sketches, extrusions));
            if (body.feature == BodyFeature::Revolution && body.revolveAxis == kRevolveAxisReference)
                children.append(QStringLiteral("Asse: ") + ForgeCad::geometryRefText(body.revolveAxisRef, sketches, extrusions));
            if (body.feature == BodyFeature::Extrusion && body.extrudeSides == 2)
                children.append(QStringLiteral("Secondo verso: %1").arg(ForgeCad::formatLength(body.distance2)));
            if (body.mergeOperation != 0)
                for (int other : body.mergeBodies)
                    if (!previousStage(other))
                        children.append((body.mergeOperation == 1 ? QStringLiteral("Unita a: ") : QStringLiteral("Sottratta da: ")) + extrusions.value(other).name);
            for (const QString &text : children) {
                auto *child = new QTreeWidgetItem(item, {text});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            restore(item, featureKey, false);
        }
        const bool withChildren = body.feature == BodyFeature::Blend || body.feature == BodyFeature::SheetTrim || body.feature == BodyFeature::SheetExtend
                               || body.feature == BodyFeature::Scale || body.feature == BodyFeature::Helix || body.feature == BodyFeature::Sweep
                               || body.feature == BodyFeature::Loft || body.feature == BodyFeature::DatumPlane || body.feature == BodyFeature::Imported
                               || body.feature == BodyFeature::Pattern || body.feature == BodyFeature::Transform
                               || body.feature == BodyFeature::SurfaceOffset || body.feature == BodyFeature::Sew
                               || body.feature == BodyFeature::Ruled || body.feature == BodyFeature::PlanarSurface
                               || body.feature == BodyFeature::DeleteFace || body.feature == BodyFeature::BoundarySurface
                               || body.feature == BodyFeature::FillSurface
                               || body.feature == BodyFeature::Draft || body.feature == BodyFeature::Shell || body.feature == BodyFeature::Thread
                               || body.feature == BodyFeature::ProjectedCurve || body.feature == BodyFeature::Thicken || body.feature == BodyFeature::ExtractedCurve || body.feature == BodyFeature::SpatialSketch;
        if (body.operation < 0 && withChildren) {
            QStringList children;
            if (body.feature == BodyFeature::SurfaceOffset) {
                if (!previousStage(body.firstBody)) children.append(QStringLiteral("Corpo: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Distanza: %1").arg(ForgeCad::formatLength(body.distance)));
            } else if (body.feature == BodyFeature::Thicken) {
                if (!body.offsetFaces.isEmpty() || !previousStage(body.firstBody))
                    children.append(QStringLiteral("Corpo: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Spessore: %1").arg(ForgeCad::formatLength(body.distance)));
                children.append(body.offsetFaces.isEmpty() ? QStringLiteral("Facce: tutta la superficie") : QStringLiteral("Facce: %1").arg(body.offsetFaces.size()));
                children.append(QStringLiteral("Direzione: ") + (body.thickenDirection.kind < 0 ? QStringLiteral("normale della superficie")
                                                                   : ForgeCad::geometryRefText(body.thickenDirection, sketches, extrusions)));
                static const char *sides[] = {"nel verso", "nel verso opposto", "meta' per parte"};
                children.append(QStringLiteral("Lato: ") + QString::fromLatin1(sides[qBound(0, body.thickenSide, 2)]));
            } else if (body.feature == BodyFeature::SpatialSketch) {
                for(const auto &e:body.spatialSketch.entities)children.append(QStringLiteral("%1 %2: %3 punti%4").arg(e.spline?QStringLiteral("Spline"):QStringLiteral("Polilinea")).arg(e.id).arg(e.points.size()).arg(e.closed?QStringLiteral(" (chiusa)"):QString()));
            } else if (body.feature == BodyFeature::ExtractedCurve) {
                children.append(QStringLiteral("Sorgente: ") + ForgeCad::geometryRefText(body.curveExtraction.source, sketches, extrusions));
                if(!body.curveExtraction.sources.isEmpty())for(const auto &ref:body.curveExtraction.sources)children.append(QStringLiteral("Bordo unificato: ")+ForgeCad::geometryRefText(ref,sketches,extrusions));
                if (body.curveExtraction.trimmed) {
                    children.append(QStringLiteral("Segmento: ") + ForgeCad::geometryRefText(body.curveExtraction.tool, sketches, extrusions));
                    children.append(body.curveExtraction.projected ? QStringLiteral("Taglio: vista salvata") : QStringLiteral("Taglio: intersezione 3D"));
                    children.append(body.curveExtraction.keepStart ? QStringLiteral("Parte: inizio") : QStringLiteral("Parte: fine"));
                }
            } else if (body.feature == BodyFeature::ProjectedCurve) {
                children.append(QStringLiteral("Corpo: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Schizzo: ") + sketches.value(body.sketchIndex).name);
                children.append(QStringLiteral("Curve: %1").arg(body.curves.size()));
                if (body.projectionReverse) children.append(QStringLiteral("Verso: invertito"));
            } else if (body.feature == BodyFeature::Draft) {
                children.append(QStringLiteral("Angolo: %1°").arg(body.draftAngle));
                children.append(QStringLiteral("Facce: %1").arg(body.offsetFaces.size()));
                children.append(QStringLiteral("Piano neutro: ") + ForgeCad::geometryRefText(body.draftNeutral, sketches, extrusions));
                children.append(body.draftReverse ? QStringLiteral("Estrazione: verso invertito") : QStringLiteral("Estrazione: normale del piano"));
            } else if (body.feature == BodyFeature::Shell) {
                if (!previousStage(body.firstBody)) children.append(QStringLiteral("Solido: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Spessore: %1").arg(ForgeCad::formatLength(body.distance)));
                children.append(QStringLiteral("Facce aperte: %1").arg(body.offsetFaces.size()));
            } else if (body.feature == BodyFeature::Thread) {
                if (!previousStage(body.firstBody)) children.append(QStringLiteral("Solido: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Standard: ") + body.thread.designation);
                children.append(QStringLiteral("Passo: %1").arg(ForgeCad::formatLength(body.thread.pitch)));
                children.append(QStringLiteral("Lunghezza: %1").arg(ForgeCad::formatLength(body.thread.length)));
            } else if (body.feature == BodyFeature::BoundarySurface) {
                for (const GeometryRef &ref : body.planarRefs) children.append(QStringLiteral("Curva: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
            } else if (body.feature == BodyFeature::FillSurface) {
                for (const GeometryRef &ref : body.planarRefs) children.append(QStringLiteral("Contorno: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
                for (const GeometryRef &ref : body.ruledFirst) children.append(QStringLiteral("Guida: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
                for (int i = 0; i < body.ruledSecond.size(); ++i)
                    children.append(QStringLiteral("Faccia adiacente: %1 — influenza %2")
                        .arg(ForgeCad::geometryRefText(body.ruledSecond[i], sketches, extrusions))
                        .arg(body.fillFaceInfluences.value(i, body.fillInfluence), 0, 'g', 3));
            } else if (body.feature == BodyFeature::DeleteFace) {
                if (!previousStage(body.firstBody)) children.append(QStringLiteral("Corpo: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Facce tolte: %1").arg(body.offsetFaces.size()));
            } else if (body.feature == BodyFeature::Ruled) {
                for (const GeometryRef &ref : body.ruledFirst) children.append(QStringLiteral("Prima curva: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
                for (const GeometryRef &ref : body.ruledSecond) children.append(QStringLiteral("Seconda curva: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
            } else if (body.feature == BodyFeature::PlanarSurface) {
                if (body.planarRefs.isEmpty()) children.append(QStringLiteral("Schizzo: ") + sketches.value(body.sketchIndex).name);
                for (const GeometryRef &ref : body.planarRefs) children.append(QStringLiteral("Bordo: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
            } else if (body.feature == BodyFeature::Sew) {
                for (int input : QVector<int>{body.firstBody} + body.booleanTools)
                    if (!previousStage(input)) children.append(QStringLiteral("Cucita con: ") + extrusions.value(input).name);
            } else if (body.feature == BodyFeature::DatumPlane) {
                for (const GeometryRef &ref : body.datum.refs) children.append(ForgeCad::geometryRefText(ref, sketches, extrusions));
            } else if (body.feature == BodyFeature::Imported) {
                children.append(QStringLiteral("File: ") + body.importSource);
            } else if (body.feature == BodyFeature::Transform) {
                const TransformParameters &m = body.move;
                if (m.copy || !previousStage(body.firstBody))
                    children.append((m.copy ? QStringLiteral("Copia di: ") : QStringLiteral("Corpo: ")) + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Traslazione: (%1, %2, %3)")
                    .arg(ForgeCad::formatLength(m.translation[0]), ForgeCad::formatLength(m.translation[1]), ForgeCad::formatLength(m.translation[2])));
                if (std::fabs(m.angle) > 0.0)
                    children.append(QStringLiteral("Rotazione: %1\u00B0 attorno a %2").arg(m.angle).arg(ForgeCad::geometryRefText(m.axis, sketches, extrusions)));
            } else if (body.feature == BodyFeature::Pattern) {
                if (body.pattern.featureOnly || !previousStage(body.firstBody))
                    children.append((body.pattern.featureOnly ? QStringLiteral("Funzione: ") : QStringLiteral("Corpo: ")) + extrusions.value(body.firstBody).name);
                const QStringList roles = {body.pattern.kind == 0 ? QStringLiteral("Direzione: ") : body.pattern.kind == 1 ? QStringLiteral("Asse: ")
                                                                                                                             : QStringLiteral("Piano: "),
                                           QStringLiteral("Direzione 2: ")};
                for (int k = 0; k < body.pattern.refs.size() && k < 2; ++k)
                    children.append(roles.at(k) + ForgeCad::geometryRefText(body.pattern.refs.at(k), sketches, extrusions));
            } else if (body.feature == BodyFeature::Helix) {
                children.append(body.helix.source == 0 ? QStringLiteral("Base: ") + sketches.value(body.sketchIndex).name
                                : QStringLiteral("Base: %1 di %2").arg(body.helix.source == 1 ? QStringLiteral("spigolo") : QStringLiteral("faccia"),
                                                                       extrusions.value(body.firstBody).name));
            } else if (body.feature == BodyFeature::Sweep) {
                children.append(QStringLiteral("Profilo: ") + (body.sweepProfileRef.kind >= 0
                    ? ForgeCad::geometryRefText(body.sweepProfileRef, sketches, extrusions) : sketches.value(body.sketchIndex).name));
                const int entities = body.pathSegments.size() + body.pathCurves.size();
                children.append(QStringLiteral("Percorso: ") + (body.sweepPathRef.kind >= 0
                    ? ForgeCad::geometryRefText(body.sweepPathRef, sketches, extrusions) : body.sweepPath == 0
                    ? sketches.value(body.pathSketch).name + (entities ? QStringLiteral(" (%1 entità)").arg(entities) : QString())
                    : extrusions.value(body.firstBody).name));
            } else if (body.feature == BodyFeature::Loft) {
                if(body.loftSections.isEmpty()) {
                    for (int k = 0; k < body.loftSketches.size(); ++k)
                        children.append(QStringLiteral("Sezione %1: %2").arg(k + 1).arg(sketches.value(body.loftSketches.at(k)).name));
                } else for(int k=0;k<body.loftSections.size();++k) {
                    const auto &ref=body.loftSections.at(k);
                    children.append(QStringLiteral("Sezione %1: %2").arg(k+1).arg(ref.kind==7 && ref.element.kind==-1?sketches.value(ref.index).name:ForgeCad::geometryRefText(ref,sketches,extrusions)));
                }
                const auto guideOrder = ForgeCad::loftGuideSequence(body);
                for (int k=0;k<guideOrder.size();++k) {
                    int index=guideOrder.at(k); QString text;
                    if(index<body.loftGuides.size()) text=sketches.value(body.loftGuides.at(index)).name;
                    else if((index-=body.loftGuides.size())<body.loftGuidePaths.size()) {
                        const auto &path=body.loftGuidePaths.at(index);
                        text=QStringLiteral("%1 (%2 entità)").arg(sketches.value(path.sketch).name).arg(path.segments.size()+path.curves.size());
                    } else text=ForgeCad::geometryRefText(body.loftEdgeGuides.at(index-body.loftGuidePaths.size()),sketches,extrusions);
                    children.append(QStringLiteral("Guida %1: %2 — G%3 / %4 %").arg(k+1).arg(text)
                        .arg(body.loftGuideContinuities.value(guideOrder[k], body.loftGuideContinuity))
                        .arg(100 * body.loftGuideInfluences.value(guideOrder[k], body.loftGuideInfluence), 0, 'g', 3));
                }
            } else {
                const QString role = body.feature == BodyFeature::Blend ? QStringLiteral("Base: ") : body.feature == BodyFeature::Scale ? QStringLiteral("Corpo: ")
                                                                                                                                      : QStringLiteral("Superficie: ");
                if (!previousStage(body.firstBody)) children.append(role + extrusions.value(body.firstBody).name);
            }
            if (body.feature == BodyFeature::SheetTrim) {
                const QString tool = body.secondBody >= 0 ? extrusions.value(body.secondBody).name
                    : body.sketchIndex >= 0 ? sketches.value(body.sketchIndex).name : planeNames().value(body.trimPlane);
                children.append(QStringLiteral("Strumento: ") + tool);
                if (body.secondBody < 0 && body.sketchIndex >= 0) {
                    static const char *modes[] = {"toglie le parti dentro il profilo", "tiene solo le parti dentro il profilo", "divide le facce"};
                    children.append(body.trimProject ? QStringLiteral("Proiezione sulla prima faccia: ") + QString::fromLatin1(modes[qBound(0, body.projectionMode, 2)])
                                                     : QStringLiteral("Taglio passante lungo la normale"));
                    if (body.trimProject && body.projectionReverse) children.append(QStringLiteral("Verso: invertito"));
                }
            }
            for (const QString &text : children) {
                auto *child = new QTreeWidgetItem(item, {text});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            restore(item, featureKey, false);
        }
        if (body.operation >= 0) {
            // Risultato booleano: gli operandi sono mostrati come voci figlie.
            static const QStringList symbols = {QStringLiteral("A \u222A B"), QStringLiteral("A \u2229 B"),
                                                QStringLiteral("A \u2212 B")};
            const QString firstName = extrusions.value(body.firstBody).name;
            const QString secondName = extrusions.value(body.secondBody).name;
            if (body.error.isEmpty())
                item->setToolTip(0, QStringLiteral("%1   (A = %2, B = %3)")
                    .arg(symbols.value(body.operation), firstName, secondName));
            // Nella storia il primo operando e' lo stadio precedente; restano gli
            // strumenti (i rami assorbiti, che compaiono sopra nella storia).
            QStringList operands;
            if (!previousStage(body.firstBody)) operands.append(QStringLiteral("Corpo: ") + firstName);
            for (int tool : QVector<int>{body.secondBody} + body.booleanTools)
                operands.append(QStringLiteral("Strumento: ") + extrusions.value(tool).name);
            for (const QString &operand : operands) {
                auto *child = new QTreeWidgetItem(item, {operand});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            restore(item, featureKey, false);
        }
        if (selection.kind == SceneObjectKind::Extrusion && selection.index == index) modelTree_->setCurrentItem(item);
    }
    QTreeWidgetItem *sketchRoot = nullptr;
    if (!historyRoot) {
        historyRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Storia delle feature")});
        historyRoot->setData(0, Qt::UserRole, kTreeInfo);
        restore(historyRoot, QStringLiteral("H"), true);
    }
    const int position = viewport_->historyPosition();
    int cursorRow = historyRoot->childCount();
    for (int row = 0; row < historyRoot->childCount(); ++row) {
        auto *item = historyRoot->child(row);
        if (item->data(0, Qt::UserRole).toInt() != kTreeExtrusion) continue;
        if (item->data(0, Qt::UserRole + 1).toInt() >= position) {
            cursorRow = row;
            // Keep the next feature's profile together with that feature.
            while (cursorRow > 0 && historyRoot->child(cursorRow - 1)->data(0, Qt::UserRole).toInt() == kTreeSketch) --cursorRow;
            break;
        }
    }
    const bool horizontalCursor = static_cast<StoryboardTree *>(modelTree_)->cursorMode() == StoryboardTree::CursorMode::Horizontal;
    auto *cursor = new QTreeWidgetItem(QStringList{(horizontalCursor
        ? QStringLiteral("━━━━  Inserisci qui  %1 / %2  ━━━━")
        : QStringLiteral("Inserisci qui  %1 / %2")).arg(position).arg(extrusions.size())});
    cursor->setData(0, Qt::UserRole, kTreeInfo);
    cursor->setData(0, Qt::UserRole + 2, QStringLiteral("historyCursor"));
    cursor->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    cursor->setForeground(0, kStoryboardPositionColor);
    if (horizontalCursor) cursor->setBackground(0, QColor(25, 65, 85));
    QFont cursorFont = cursor->font(0);
    cursorFont.setBold(true);
    cursor->setFont(0, cursorFont);
    cursor->setToolTip(0, horizontalCursor
        ? QStringLiteral("Trascina la barra tra le feature per scegliere la posizione di calcolo e inserimento. Frecce su/giù: un passo; Home: inizio; End: fine.")
        : QStringLiteral("Trascina la maniglia laterale per scegliere la posizione di calcolo e inserimento. Frecce su/giù: un passo; Home: inizio; End: fine."));
    historyRoot->insertChild(cursorRow, cursor);
    static_cast<StoryboardTree *>(modelTree_)->setHistoryRange(position, extrusions.size());
    for (int row = cursorRow + 1; row < historyRoot->childCount(); ++row) {
        auto *item = historyRoot->child(row);
        item->setForeground(0, QColor(110, 120, 130));
        QFont font = item->font(0);
        font.setItalic(true);
        item->setFont(0, font);
    }
    for (int index = 0; index < sketches.size(); ++index) {
        if (shownSketches.contains(index)) continue;
        if (!sketchRoot) {
            sketchRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Schizzi non utilizzati")});
            sketchRoot->setData(0, Qt::UserRole, kTreeInfo);
            restore(sketchRoot, QStringLiteral("S"), true);
        }
        addSketch(index, sketchRoot);
    }
    // Solidi e superfici separati dei corpi visibili (barra di stato).
    if (auto *counts = statusBar()->findChild<QLabel *>(QStringLiteral("sceneCounts"))) {
        int solids = 0, sheets = 0, visible = 0;
        for (int index = 0; index < extrusions.size(); ++index) {
            if (!extrusions.at(index).visible) continue;
            const QPair<int, int> parts = viewport_->bodyComponents(index);
            if (parts.first + parts.second == 0) continue;
            ++visible;
            solids += parts.first;
            sheets += parts.second;
        }
        counts->setText(QStringLiteral("Solidi: %1 · Superfici: %2 · Corpi visibili: %3").arg(solids).arg(sheets).arg(visible));
    }
    // Aggiorna prima il layout completo: ripristinare mentre il range e'
    // ancora vuoto troncherebbe lo scorrimento a zero.
    // Gli schizzi non utilizzati sono aggiunti dopo il cursore: ripristina
    // la sua selezione per ultima, cosi' non viene sostituita da uno schizzo.
    if(cursorSelected)modelTree_->setCurrentItem(cursor);
    modelTree_->doItemsLayout();
    if(cursorSelected && position!=previousHistoryPosition)modelTree_->scrollToItem(cursor);
    else modelTree_->verticalScrollBar()->setValue(verticalPosition);
    modelTree_->horizontalScrollBar()->setValue(horizontalPosition);
    rebuildingTree_ = false;
}

void PdfWindow::updateUndoActions() {
    if (undoAction_) undoAction_->setEnabled(viewport_->canUndo());
    if (redoAction_) redoAction_->setEnabled(viewport_->canRedo());
}

// Dialogo per lo sfondo: colori, direzione (angolo) e punto di mescolanza
// (posizione) della sfumatura, e quanto lo sfondo illumina gli oggetti.
// Le modifiche sono applicate subito come anteprima; Annulla le ripristina.
void PdfWindow::editBackground() {
    const BackgroundSettings original = viewport_->background();
    auto settings = std::make_shared<BackgroundSettings>(original);
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Sfondo e luce ambiente"));
    auto *layout = new QFormLayout(&dialog);

    const auto makeColorButton = [&dialog](const QColor &color) {
        auto *button = new QPushButton(&dialog);
        button->setMinimumWidth(120);
        button->setText(color.name());
        button->setStyleSheet(QStringLiteral("background: %1; color: %2;")
            .arg(color.name(), color.lightness() > 128 ? QStringLiteral("#000") : QStringLiteral("#fff")));
        return button;
    };
    auto *gradientCheck = new QCheckBox(QStringLiteral("Sfondo sfumato"), &dialog);
    gradientCheck->setChecked(settings->gradient);
    auto *startButton = makeColorButton(settings->startColor);
    auto *endButton = makeColorButton(settings->endColor);
    auto *angleSpin = new QSpinBox(&dialog);
    angleSpin->setRange(0, 359);
    angleSpin->setWrapping(true);
    angleSpin->setSuffix(QStringLiteral(" deg"));
    angleSpin->setValue(qRound(settings->angle));
    angleSpin->setToolTip(QStringLiteral("Direzione dal colore iniziale al finale: 0 = da sinistra a destra, 90 = dal basso in alto"));
    auto *positionSpin = new QSpinBox(&dialog);
    positionSpin->setRange(0, 100);
    positionSpin->setSuffix(QStringLiteral(" %"));
    positionSpin->setValue(qRound(settings->position * 100.0f));
    positionSpin->setToolTip(QStringLiteral("Punto in cui i due colori si mescolano al 50%"));
    auto *lightingCheck = new QCheckBox(QStringLiteral("Lo sfondo illumina gli oggetti"), &dialog);
    lightingCheck->setChecked(settings->affectsLighting);
    auto *strengthSpin = new QSpinBox(&dialog);
    strengthSpin->setRange(0, 100);
    strengthSpin->setSuffix(QStringLiteral(" %"));
    strengthSpin->setValue(qRound(settings->lightingStrength * 100.0f));

    layout->addRow(gradientCheck);
    layout->addRow(QStringLiteral("Colore iniziale:"), startButton);
    layout->addRow(QStringLiteral("Colore finale:"), endButton);
    layout->addRow(QStringLiteral("Angolo sfumatura:"), angleSpin);
    layout->addRow(QStringLiteral("Posizione sfumatura:"), positionSpin);
    layout->addRow(lightingCheck);
    layout->addRow(QStringLiteral("Intensita luce ambiente:"), strengthSpin);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);

    const auto refresh = [=] {
        endButton->setEnabled(settings->gradient);
        angleSpin->setEnabled(settings->gradient);
        positionSpin->setEnabled(settings->gradient);
        strengthSpin->setEnabled(settings->affectsLighting);
        for (auto pair : {std::make_pair(startButton, settings->startColor),
                          std::make_pair(endButton, settings->endColor)}) {
            pair.first->setText(pair.second.name());
            pair.first->setStyleSheet(QStringLiteral("background: %1; color: %2;")
                .arg(pair.second.name(), pair.second.lightness() > 128 ? QStringLiteral("#000") : QStringLiteral("#fff")));
        }
        viewport_->setBackground(*settings);
    };
    refresh();
    connect(gradientCheck, &QCheckBox::toggled, &dialog, [=](bool on) { settings->gradient = on; refresh(); });
    connect(lightingCheck, &QCheckBox::toggled, &dialog, [=](bool on) { settings->affectsLighting = on; refresh(); });
    connect(angleSpin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
            [=](int value) { settings->angle = float(value); refresh(); });
    connect(positionSpin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
            [=](int value) { settings->position = float(value) / 100.0f; refresh(); });
    connect(strengthSpin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
            [=](int value) { settings->lightingStrength = float(value) / 100.0f; refresh(); });
    connect(startButton, &QPushButton::clicked, &dialog, [=, &dialog] {
        const QColor color = QColorDialog::getColor(settings->startColor, &dialog, QStringLiteral("Colore iniziale"));
        if (color.isValid()) { settings->startColor = color; refresh(); }
    });
    connect(endButton, &QPushButton::clicked, &dialog, [=, &dialog] {
        const QColor color = QColorDialog::getColor(settings->endColor, &dialog, QStringLiteral("Colore finale"));
        if (color.isValid()) { settings->endColor = color; refresh(); }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) viewport_->setBackground(original);
}

void PdfWindow::setDisplayMode(int mode) {
    modeStatus_->setText(mode == 0 ? QStringLiteral("Solo linee esterne") : mode == 1 ? QStringLiteral("Mesh") : QStringLiteral("Mesh + linee esterne"));
}

void PdfWindow::setTheme(bool dark) {
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!application) return;
    const QString sheet = dark ? QStringLiteral("QMainWindow { background: #111820; color: #d8e4ea; }QMenuBar,QToolBar,QStatusBar { background: #1b2730; color: #d8e4ea; }QMenu { background: #202d36; color: #d8e4ea; }"
        "QToolBar { spacing: 1px; padding: 2px 4px; border: none; }"
        "QToolBar::separator { background: #34444f; width: 1px; margin: 5px 3px; }"
        "QToolBar QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 2px; }"
        "QToolBar QToolButton:checked { background: #24495f; border-color: #4f9fd0; }"
        "QToolButton:enabled:hover, QToolBar QToolButton:enabled:hover { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QToolButton:enabled:pressed, QToolBar QToolButton:enabled:pressed { background: #1f4258; border-color: #80c8f0; }"
        "QToolBar QToolButton[popupMode=\"1\"] { padding-right: 11px; }"
        "QToolBar QToolButton::menu-button { border: none; width: 10px; }"
        "QToolButton::menu-button:enabled:hover { background: #5089ad; }"
        "QToolButton::menu-button:enabled:pressed { background: #1f4258; }"
        "QPushButton:enabled:hover { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QPushButton:enabled:pressed { background: #1f4258; border-color: #80c8f0; }"
        "QMenuBar::item:selected:enabled { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QMenuBar::item:pressed:enabled { background: #1f4258; border-color: #80c8f0; color: #ffffff; }"
        "QMenu::item:selected:enabled { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QMenu::item:disabled { color: #788995; }") : QStringLiteral(
        "QMainWindow { background: palette(window); color: palette(window-text); }"
        "QMenuBar,QToolBar,QStatusBar,QMenu { background: palette(window); color: palette(window-text); }"
        "QToolBar { spacing: 1px; padding: 2px 4px; border: none; }"
        "QToolBar::separator { background: #c3cbd2; width: 1px; margin: 5px 3px; }"
        "QToolBar QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; }"
        "QToolBar QToolButton:checked { background: #d6eafb; border-color: #659dca; }"
        "QToolButton:enabled:hover, QToolBar QToolButton:enabled:hover { background: #e2eff9; border-color: #659dca; }"
        "QToolButton:enabled:pressed, QToolBar QToolButton:enabled:pressed { background: #c1dcf1; border-color: #427fab; }"
        "QToolBar QToolButton[popupMode=\"1\"] { padding-right: 11px; }"
        "QToolBar QToolButton::menu-button { border: none; width: 10px; }"
        "QToolButton::menu-button:enabled:hover { background: #c1dcf1; }"
        "QToolButton::menu-button:enabled:pressed { background: #a9cee9; }"
        "QMenuBar::item:selected:enabled,QMenu::item:selected:enabled { background: #d6eafb; color: palette(window-text); }"
        "QMenuBar::item:pressed:enabled { background: #c1dcf1; }"
        "QMenu::item:disabled { color: palette(disabled, window-text); }");
    if (application->styleSheet() != sheet) application->setStyleSheet(sheet);
}
