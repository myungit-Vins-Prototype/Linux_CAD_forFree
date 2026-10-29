#include "forgeCad2026_storyboard.h"
#include <QtWidgets>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

void TreeBadgeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
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

QSize TreeBadgeDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const QString badge = index.data(kTreeBadgeRole).toString();
    if (!badge.isEmpty()) size.rwidth() += option.fontMetrics.horizontalAdvance(badge) + 24;
    return size;
}

QRect StoryboardPositionSlider::handleRect() const {
    QStyleOptionSlider option;
    initStyleOption(&option);
    return style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
}

void StoryboardPositionSlider::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && std::abs(event->position().x() - width() / 2) <= 10)
        for (const auto &checkpoint : checkpoints)
            if (std::abs(event->position().y() - checkpoint.first) <= 6) {
                event->accept();
                if (checkpointSelected) checkpointSelected(checkpoint.second);
                return;
            }
    QSlider::mousePressEvent(event);
}

void StoryboardPositionSlider::paintEvent(QPaintEvent *event) {
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

StoryboardTree::StoryboardTree(QWidget *parent ) : QTreeWidget(parent) {
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

namespace {
int storyItemPosition(QTreeWidgetItem *item) {
    return item->data(0, Qt::UserRole + 4).isValid() ? item->data(0, Qt::UserRole + 4).toInt()
                                                   : item->data(0, Qt::UserRole + 1).toInt();
}
bool storyObject(QTreeWidgetItem *item) {
    const int type = item->data(0, Qt::UserRole).toInt();
    return type == kTreeExtrusion || type == kTreeSketch;
}
}

void StoryboardTree::setMoveFeatureCallback(std::function<QString(int, int)> callback) { moveFeature_ = std::move(callback); }

void StoryboardTree::setHistoryPositionCallback(std::function<void(int)> callback) { historyPositionChanged_ = std::move(callback); }

StoryboardTree::CursorMode StoryboardTree::cursorMode() const { return cursorMode_; }

void StoryboardTree::setCursorMode(CursorMode mode) {
    cursorMode_ = mode;
    const QMargins margins = viewportMargins();
    setViewportMargins(mode == CursorMode::Left ? 30 : 0, margins.top(),
                       mode == CursorMode::Right ? 30 : 0, margins.bottom());
    updateHistoryRail();
    viewport()->update();
}

QSlider *StoryboardTree::historyRail() const { return historyRail_; }

int StoryboardTree::historyPosition() const { return historyPosition_; }

void StoryboardTree::setHistoryRange(int position, int count) {
    historyPosition_ = position; historyCount_ = count;
    updateHistoryRail();
    viewport()->update();
}

bool StoryboardTree::isHistoryCursor(QTreeWidgetItem *item) {
    return item && item->data(0, Qt::UserRole + 2).toString() == QStringLiteral("historyCursor");
}

void StoryboardTree::updateGeometries() {
    QTreeWidget::updateGeometries();
    const QMargins margins = viewportMargins();
    const int left = cursorMode_ == CursorMode::Left ? 30 : 0;
    const int right = cursorMode_ == CursorMode::Right ? 30 : 0;
    if (margins.left() != left || margins.right() != right)
        setViewportMargins(left, margins.top(), right, margins.bottom());
    if (historyRail_) updateHistoryRail();
}

void StoryboardTree::setSourcePickCallback(std::function<void(int, bool)> callback) {
    sourcePicked_ = std::move(callback);
    historyRail_->setEnabled(!sourcePicked_);
}

void StoryboardTree::chooseSource(QTreeWidgetItem *item) {
    if (!sourcePicked_ || !item) return;
    const int type = item->data(0, Qt::UserRole).toInt();
    if (type == kTreeExtrusion || type == kTreeBody) {
        setCurrentItem(item);
        sourcePicked_(item->data(0, Qt::UserRole + 1).toInt(), type == kTreeBody);
    } else item->setExpanded(!item->isExpanded());
}

void StoryboardTree::mouseDoubleClickEvent(QMouseEvent *event) {
    if (sourcePicked_) { event->accept(); return; }
    QTreeWidget::mouseDoubleClickEvent(event);
}

void StoryboardTree::contextMenuEvent(QContextMenuEvent *event) {
    if (sourcePicked_) { event->accept(); return; }
    QTreeWidget::contextMenuEvent(event);
}

void StoryboardTree::mousePressEvent(QMouseEvent *event) {
    if (sourcePicked_) {
        if (event->button() == Qt::LeftButton) chooseSource(itemAt(event->position().toPoint()));
        event->accept(); return;
    }
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

void StoryboardTree::mouseMoveEvent(QMouseEvent *event) {
    if (sourcePicked_) { event->accept(); return; }
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
                if (!storyObject(item)) continue;
                const QRect rect = visualItemRect(item);
                if (!rect.isEmpty() && event->position().y() <= rect.center().y()) {
                    position = storyItemPosition(item);
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

void StoryboardTree::mouseReleaseEvent(QMouseEvent *event) {
    if (sourcePicked_) { event->accept(); return; }
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

void StoryboardTree::keyPressEvent(QKeyEvent *event) {
    if (sourcePicked_) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) chooseSource(currentItem());
        else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down || event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
            QTreeWidget::keyPressEvent(event);
        event->accept(); return;
    }
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

void StoryboardTree::startDrag(Qt::DropActions actions) {
    if (sourcePicked_) return;
    draggedItem_ = currentItem();
    if (!isMovableFeature(draggedItem_)) {
        draggedItem_ = nullptr;
        return;
    }
    QTreeWidget::startDrag(actions);
    draggedItem_ = nullptr;
    clearInsertionLine();
}

void StoryboardTree::dragEnterEvent(QDragEnterEvent *event) {
    if (isMovableFeature(draggedItem_)) event->acceptProposedAction();
    else event->ignore();
}

void StoryboardTree::dragMoveEvent(QDragMoveEvent *event) {
    QTreeWidgetItem *target = targetFeature(event->position().toPoint());
    if (target && target != draggedItem_) {
        const int sourceIndex = storyItemPosition(draggedItem_);
        const int targetIndex = storyItemPosition(target);
        const QRect targetRect = visualItemRect(target);
        setInsertionLine(sourceIndex < targetIndex ? targetRect.bottom() + 1 : targetRect.top(), targetRect.left());
        event->acceptProposedAction();
    } else {
        clearInsertionLine();
        event->ignore();
    }
}

void StoryboardTree::dragLeaveEvent(QDragLeaveEvent *event) {
    clearInsertionLine();
    QTreeWidget::dragLeaveEvent(event);
}

void StoryboardTree::dropEvent(QDropEvent *event) {
    QTreeWidgetItem *source = draggedItem_;
    QTreeWidgetItem *target = targetFeature(event->position().toPoint());
    clearInsertionLine();
    if (!isMovableFeature(source) || !target || source == target || !moveFeature_) {
        event->ignore();
        return;
    }
    const QString error = moveFeature_(storyItemPosition(source), storyItemPosition(target));
    event->ignore();  // l'albero viene ricostruito dal documento, mai spostato solo graficamente
    if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Riordina storyboard"), error);
}

void StoryboardTree::paintEvent(QPaintEvent *event) {
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

QTreeWidgetItem *StoryboardTree::historyRootItem() const {
    for (int i = 0; i < topLevelItemCount(); ++i)
        if (topLevelItem(i)->data(0, Qt::UserRole + 2).toString() == QStringLiteral("H")) return topLevelItem(i);
    return nullptr;
}

int StoryboardTree::historyPositionAtY(int y) const {
    auto *root = historyRootItem();
    if (!root) return historyPosition_;
    bool first = true;
    for (int row = 0; row < root->childCount(); ++row) {
        auto *item = root->child(row);
        if (!storyObject(item)) continue;
        const QRect rect = visualItemRect(item);
        if (!rect.isEmpty() && y <= rect.center().y())
            return first ? 0 : storyItemPosition(item);
        first = false;
    }
    return historyCount_;
}

void StoryboardTree::updateHistoryRail() {
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
        if (!storyObject(item)) continue;
        const QRect rect = visualItemRect(item);
        if (!rect.isEmpty() && rect.center().y() >= top && rect.center().y() <= bottom)
            slider->checkpoints.append({rect.center().y() - top + handleOffset, storyItemPosition(item) + 1});
    }
    historyRail_->setRange(0, bottom - top);
    if (!historyRail_->isSliderDown()) historyRail_->setValue(cursorY - top);
    historyRail_->show();
    historyRailViewport_->show();
    historyRailViewport_->raise();
    updatingRail_ = false;
}

void StoryboardTree::setInsertionLine(int y, int left) {
    if (insertionLineY_ == y && insertionLineLeft_ == left) return;
    insertionLineY_ = y;
    insertionLineLeft_ = qMax(2, left - 8);
    viewport()->update();
}

void StoryboardTree::clearInsertionLine() {
    if (insertionLineY_ < 0) return;
    insertionLineY_ = -1;
    viewport()->update();
}

bool StoryboardTree::isMovableFeature(QTreeWidgetItem *item) {
    return item && storyObject(item) && item->parent()
        && item->parent()->data(0, Qt::UserRole + 2).toString() == QStringLiteral("H");
}

QTreeWidgetItem *StoryboardTree::targetFeature(const QPoint &position) const {
    if (!isMovableFeature(draggedItem_)) return nullptr;
    QTreeWidgetItem *body = draggedItem_->parent();
    QTreeWidgetItem *under = itemAt(position);
    for (QTreeWidgetItem *candidate = under; candidate && candidate != body; candidate = candidate->parent())
        if (candidate->parent() == body && storyObject(candidate))
            return normalizedTarget(body, candidate);

    // Schizzi e dettagli possono stare tra due feature. In quel caso il
    // rilascio vale per la feature diretta piu' vicina nello stesso corpo.
    QTreeWidgetItem *best = nullptr;
    int distance = std::numeric_limits<int>::max();
    for (int row = 0; row < body->childCount(); ++row) {
        QTreeWidgetItem *candidate = body->child(row);
        if (!storyObject(candidate)) continue;
        const QRect rect = visualItemRect(candidate);
        const int d = std::abs(rect.center().y() - position.y());
        if (d < distance) distance = d, best = candidate;
    }
    return normalizedTarget(body, best);
}

QTreeWidgetItem *StoryboardTree::normalizedTarget(QTreeWidgetItem *, QTreeWidgetItem *target) const {
    // Le dipendenze vengono validate sul documento, compresi gli schizzi.
    return target;
}

QTreeWidgetItem *StoryboardTree::firstFeature(QTreeWidgetItem *body) {
    if (!body) return nullptr;
    for (int row = 0; row < body->childCount(); ++row)
        if (body->child(row)->data(0, Qt::UserRole).toInt() == kTreeExtrusion) return body->child(row);
    return nullptr;
}
