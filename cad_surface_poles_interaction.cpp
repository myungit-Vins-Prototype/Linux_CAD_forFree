#include "cad_surface_poles_interaction.h"
#include "cad_surface_poles_model.h"
#include "forgeCad2026_viewport.h"
#include <QApplication>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QRubberBand>
#include <QSet>
#include <QTableView>
#include <algorithm>
#include <cmath>
namespace ForgeCad {
SurfacePolesInteraction::SurfacePolesInteraction(CadViewport *view, QTableView *table, const QVector<int> &rows,
                                                 QObject *parent)
    : QObject(parent), view_(view), table_(table), displayedRows_(rows),
      band_(new QRubberBand(QRubberBand::Rectangle, view)) {
    setObjectName("surfacePolesInteraction");
    band_->setAttribute(Qt::WA_TransparentForMouseEvents);
    releaseCheck_.setInterval(60);
    connect(&releaseCheck_, &QTimer::timeout, this, [this] {
        if (interacting() && !(QApplication::mouseButtons() & Qt::LeftButton))
            finish(false);
    });
    qApp->installEventFilter(this);
}
SurfacePolesInteraction::~SurfacePolesInteraction() {
    qApp->removeEventFilter(this);
    active_ = boxing_ = false;
    if (QWidget::mouseGrabber() == view_)
        view_->releaseMouse();
    delete band_;
}
int SurfacePolesInteraction::coordinateColumn() const {
    return static_cast<SurfacePolesModel *>(table_->model())->coordinateColumn();
}
void SurfacePolesInteraction::cancel() { finish(true); }
void SurfacePolesInteraction::selectBox() {
    const QRect box = QRect(origin_, boxEnd_).normalized();
    QSet<int> selected;
    const bool toggle = modifiers_ & (Qt::ControlModifier | Qt::MetaModifier), add = modifiers_ & Qt::ShiftModifier;
    if (toggle || add)
        for (int row : initialSelection_)
            selected.insert(row);
    for (int i = 0; i < displayedRows_.size(); ++i)
        if (box.contains(view_->surfacePoleScreen(i).toPoint())) {
            const int row = displayedRows_[i];
            if (toggle && selected.contains(row))
                selected.remove(row);
            else
                selected.insert(row);
        }
    if (selected.size() > 4096) {
        if (message)
            message(QStringLiteral("Seleziona fino a 4096 poli per funzione."));
        return;
    }
    QVector<int> rows(selected.cbegin(), selected.cend());
    std::sort(rows.begin(), rows.end());
    QItemSelection selection;
    for (int row : rows)
        selection.select(table_->model()->index(row, 0),
                         table_->model()->index(row, table_->model()->columnCount() - 1));
    table_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (!rows.isEmpty())
        table_->selectionModel()->setCurrentIndex(table_->model()->index(rows[0], coordinateColumn()),
                                                  QItemSelectionModel::NoUpdate);
}
void SurfacePolesInteraction::finish(bool cancel) {
    if (!interacting())
        return;
    releaseCheck_.stop();
    const bool wasBox = boxing_, wasDrag = active_;
    active_ = boxing_ = false;
    band_->hide();
    if (cancel && wasDrag) {
        QSignalBlocker guard(table_->model());
        static_cast<SurfacePolesModel *>(table_->model())->restore(initialEdits_);
        static_cast<SurfacePolesModel *>(table_->model())->setAutomatic(initialAutomatic_);
    }
    if (!cancel && wasBox)
        selectBox();
    if (QWidget::mouseGrabber() == view_)
        view_->releaseMouse();
    table_->viewport()->update();
    if (finished)
        finished();
}
bool SurfacePolesInteraction::eventFilter(QObject *object, QEvent *event) {
    if (!enabled_)
        return false;
    if (interacting()) {
        // Un nuovo clic, anche su un controllo del pannello, chiude una
        // cattura rimasta aperta prima che il controllo cambi la griglia.
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)
            finish(false);
        if ((object == view_ && (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide)) ||
            event->type() == QEvent::ApplicationDeactivate ||
            (event->type() == QEvent::WindowDeactivate && (object == view_ || object == view_->window()))) {
            finish(true);
            return false;
        }
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            finish(true);
            return true;
        }
        if ((event->type() == QEvent::MouseButtonRelease ||
             event->type() == QEvent::NonClientAreaMouseButtonRelease) &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            boxEnd_ =
                object == view_ ? mouse->position().toPoint() : view_->mapFromGlobal(mouse->globalPosition().toPoint());
            finish(false);
            return object == view_;
        }
        if (event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const auto position =
                object == view_ ? mouse->position().toPoint() : view_->mapFromGlobal(mouse->globalPosition().toPoint());
            boxEnd_ = position;
            if (!(mouse->buttons() & Qt::LeftButton)) {
                finish(false);
                return object == view_;
            }
            if (boxing_) {
                band_->setGeometry(QRect(origin_, position).normalized());
                return object == view_;
            }
            // Il gizmo segue la griglia: la conversione pixel/mondo resta
            // quella del primo fotogramma, senza dipendere dalla nuova origine.
            Kernel::Vec3 delta;
            for(int c=0;c<3;++c)delta[c]=dragBasis_[0][c]*(position.x()-origin_.x())+
                                        dragBasis_[1][c]*(position.y()-origin_.y());
            {
                QSignalBlocker guard(table_->model());
                static_cast<SurfacePolesModel *>(table_->model())->restore(initialEdits_);
                static_cast<SurfacePolesModel *>(table_->model())->setAutomatic(initialAutomatic_);
                const auto error = static_cast<SurfacePolesModel *>(table_->model())->translatePoles(
                    rows_, delta, radiusU_, radiusV_);
                if (!error.isEmpty() && message) message(error);
            }
            table_->viewport()->update();
            if (moved)
                moved();
            return object == view_;
        }
    }
    if (object != view_ || view_->referencePicking())
        return false;
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        const auto position = mouse->position();
        modifiers_ = mouse->modifiers();
        const bool boxGesture = boxSelection_ || (modifiers_ & Qt::AltModifier);
        int handle = boxGesture ? -1 : view_->surfacePoleHandleAt(position);
        int row = -1;
        double nearest = 10 * 10;
        for (int i = 0; !boxGesture && i < displayedRows_.size(); ++i) {
            const auto d = view_->surfacePoleScreen(i) - position;
            const double distance = QPointF::dotProduct(d, d);
            if (distance < nearest) {
                nearest = distance;
                row = displayedRows_[i];
            }
        }
        const bool toggle = modifiers_ & (Qt::ControlModifier | Qt::MetaModifier);
        const bool add = modifiers_ & Qt::ShiftModifier;
        if (row >= 0 && (toggle || add || handle < 0 || handle == 3 ||
                         QLineF(position, view_->surfacePoleAxisScreen(handle)).length() > 12))
            handle = -1;
        if (handle < 0 || handle == 3) {
            if (row >= 0) {
                const auto index = table_->model()->index(row, 0);
                const bool selected = table_->selectionModel()->isRowSelected(row, {});
                if (toggle && selected) {
                    table_->selectionModel()->select(index, QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
                    return true;
                }
                if (!selected)
                    table_->selectionModel()->select(
                        index, (toggle || add ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect) |
                                   QItemSelectionModel::Rows);
                table_->selectionModel()->setCurrentIndex(table_->model()->index(row, coordinateColumn()),
                                                          QItemSelectionModel::NoUpdate);
                table_->scrollTo(table_->model()->index(row, coordinateColumn()));
            } else if (handle != 3) {
                if (!boxGesture)
                    return false; // Il trascinamento normale sullo sfondo ruota la vista.
                boxing_ = true;
                origin_ = position.toPoint();
                boxEnd_ = origin_;
                initialSelection_.clear();
                for (const auto &selected : table_->selectionModel()->selectedRows())
                    initialSelection_.append(selected.row());
                band_->setGeometry(QRect(origin_, QSize()));
                band_->show();
                if (started)
                    started();
                view_->grabMouse();
                if (mouse->spontaneous())
                    releaseCheck_.start();
                return true;
            }
            // I modificatori servono alla selezione; trascinare il gruppo
            // richiede un normale clic su uno dei poli gia' selezionati.
            if (toggle || add)
                return true;
        }
        rows_.clear();
        if (table_->selectionModel()->selectedRows().size() > 4096)
            return true;
        for (const auto &index : table_->selectionModel()->selectedRows()) {
            const int r = index.row();
            std::array<double, 3> values;
            for (int k = 0; k < 3; ++k) {
                bool valid = false;
                values[k] = QLocale().toDouble(
                    table_->model()->data(table_->model()->index(r, k + coordinateColumn()), Qt::EditRole).toString(),
                    &valid);
                if (!valid || !std::isfinite(values[k]))
                    return true;
            }
            rows_.append(r);
        }
        if (rows_.isEmpty())
            return true;
        initialEdits_=static_cast<SurfacePolesModel *>(table_->model())->snapshot();
        initialAutomatic_=static_cast<SurfacePolesModel *>(table_->model())->automaticEdits();
        axis_ = handle >= 0 && handle < 3 ? handle : -1;
        origin_ = position.toPoint();
        const auto x=view_->surfacePoleAxisDragVector(axis_,origin_,origin_+QPoint(1,0));
        const auto y=view_->surfacePoleAxisDragVector(axis_,origin_,origin_+QPoint(0,1));
        for(int c=0;c<3;++c){dragBasis_[0][c]=double(x[c]);dragBasis_[1][c]=double(y[c]);}
        active_ = true;
        if (started)
            started();
        view_->setFocus();
        view_->grabMouse();
        if (mouse->spontaneous())
            releaseCheck_.start();
        return true;
    }
    return false;
}
} // namespace ForgeCad
