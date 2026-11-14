#include "cad_drawing_local_section_tool.h"
#include "cad_drawing_local_section.h"
#include "cad_numeric_input.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineF>
#include <QPen>
#include <QPushButton>

namespace ForgeCad {
DrawingLocalSectionTool::DrawingLocalSectionTool(QWidget *parent, QGraphicsScene *scene,
                                                 QGraphicsView *canvas, const DrawingView &view, double front,
                                                 double depth, const std::function<QPointF(QPointF)> &toModel,
                                                 const std::function<QPointF(QPointF)> &toPaper,
                                                 std::function<void(DrawingLocalSection)> apply,
                                                 std::vector<DrawingLocalSectionDepthView> references)
    : QObject(parent), definition_(view), scene_(scene), canvas_(canvas), toModel_(toModel),
      toPaper_(toPaper), applyCallback_(std::move(apply)) {
    auto *panel = new QDialog(parent);
    panel_ = panel;
    panel->setObjectName("drawingLocalSectionDialog");
    panel->setWindowTitle(QStringLiteral("Sezione locale"));
    panel->setModal(false);
    auto *form = new QFormLayout(panel);
    auto *instruction =
        new QLabel(view.localSection
                       ? QStringLiteral("Modifica zero e profondita', oppure usa Ridisegna contorno per cambiare il ritaglio.")
                       : QStringLiteral("Clicca i vertici del contorno sulla vista. Clic sul primo punto o Invio "
                                        "chiude e applica; Backspace toglie l'ultimo punto; Esc annulla."),
                   panel);
    instruction->setWordWrap(true);
    form->addRow(instruction);
    front_ = new NumericInput(panel);
    front_->setObjectName("drawingLocalSectionFront");
    front_->setDecimals(8);
    front_->setRange(-1e7, 1e7);
    front_->setValue(front);
    front_->setSuffix(" mm");
    depth_ = new NumericInput(panel);
    depth_->setObjectName("drawingLocalSectionDepth");
    depth_->setDecimals(8);
    depth_->setRange(1e-7, 1e7);
    depth_->setValue(depth);
    depth_->setSuffix(" mm");
    form->addRow(QStringLiteral("Zero profondita' (piano iniziale):"), front_);
    form->addRow(QStringLiteral("Profondita' verso l'interno:"), depth_);
    auto *reference =
        new QLabel(QStringLiteral("La profondita' si misura dal piano blu (0), lungo la normale verso l'interno."),
                   panel);
    reference->setWordWrap(true);
    form->addRow(reference);
    depthGuide_ = new DrawingLocalSectionDepthGuide(panel, scene_, canvas_, view, std::move(references),
        [this](bool zero, double level) {
            if (zero)
                front_->setValue(level);
            else
                depth_->setValue(front_->value() - level);
        });
    form->addRow(depthGuide_);
    connect(front_, &QDoubleSpinBox::valueChanged, this, [this] { updateDepthGuide(); });
    connect(depth_, &QDoubleSpinBox::valueChanged, this, [this] { updateDepthGuide(); });
    auto *reset = new QPushButton(QStringLiteral("Ridisegna contorno"), panel);
    reset->setObjectName("drawingLocalSectionReset");
    form->addRow(reset);
    message_ = new QLabel(panel);
    message_->setObjectName("drawingLocalSectionError");
    message_->setWordWrap(true);
    form->addRow(message_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, panel);
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setObjectName("drawingLocalSectionApply");
    apply_->setText(QStringLiteral("Applica"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annulla"));
    form->addRow(buttons);
    connect(apply_, &QPushButton::clicked, this, [this] { finish(); });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { cancel(); });
    connect(panel, &QDialog::rejected, this, [this] { cancel(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        points_.clear();
        drafting_ = true;
        preview({});
        canvas_->setFocus();
    });
    drafting_ = !view.localSection;
    if (view.localSection)
        for (auto p : view.localSection->outline)
            points_.append(toPaper_(QPointF(p.x(), p.y())));
    QPen pen(QColor("#1469c0"), .2, Qt::DashLine);
    preview_ = scene_->addPath({}, pen, QColor(20, 105, 192, 24));
    preview_->setData(0, "localSectionPreview");
    preview_->setZValue(38);
    preview_->setAcceptedMouseButtons(Qt::NoButton);
    panel->resize(420, 680);
    panel->show();
    preview(points_.isEmpty() ? QPointF() : points_.back());
    canvas_->setCursor(Qt::CrossCursor);
    canvas_->setFocus();
}
DrawingLocalSectionTool::~DrawingLocalSectionTool() {
    cancel();
    if (panel_)
        delete panel_.data();
}
void DrawingLocalSectionTool::cancel() {
    active_ = false;
    if (depthGuide_)
        depthGuide_->cancel();
    if (preview_) {
        delete preview_;
        preview_ = nullptr;
    }
    if (panel_) {
        panel_->blockSignals(true);
        panel_->hide();
    }
    canvas_->setCursor(Qt::ArrowCursor);
}
void DrawingLocalSectionTool::preview(QPointF cursor) {
    if (!preview_)
        return;
    QPainterPath path;
    for (int i = 0; i < points_.size(); ++i) {
        if (i)
            path.lineTo(points_[i]);
        else
            path.moveTo(points_[i]);
    }
    if (!points_.isEmpty()) {
        if (drafting_ && cursor != points_.back())
            path.lineTo(cursor);
        if (points_.size() >= 3)
            path.closeSubpath();
    }
    preview_->setPath(path);
    apply_->setEnabled(points_.size() >= 3);
    message_->setText(QStringLiteral("%1 vertici scelti").arg(points_.size()));
    updateDepthGuide();
}
void DrawingLocalSectionTool::updateDepthGuide() {
    QPolygonF outline;
    for (auto point : points_)
        outline.append(toModel_(point));
    depthGuide_->setParameters(front_->value(), depth_->value(), outline);
}
bool DrawingLocalSectionTool::pointer(QPointF point, bool click, Qt::KeyboardModifiers modifiers) {
    if (!active_)
        return false;
    if (depthGuide_->pointer(point, click, modifiers))
        return true;
    if (click) {
        const double zoom = std::max(.01, std::abs(canvas_->transform().m11()));
        if (points_.size() >= 3 && QLineF(points_.first(), point).length() * zoom <= 9) {
            finish();
            return true;
        }
        if (points_.size() >= 4096) {
            message_->setText(QStringLiteral("Numero massimo di vertici raggiunto."));
            return true;
        }
        if (points_.isEmpty() || QLineF(points_.last(), point).length() * zoom >= 2)
            points_.append(point);
    }
    preview(point);
    return true;
}
bool DrawingLocalSectionTool::key(int key) {
    if (!active_)
        return false;
    if (depthGuide_->key(key))
        return true;
    if (key == Qt::Key_Escape) {
        cancel();
        return true;
    }
    if (key == Qt::Key_Backspace) {
        if (!points_.isEmpty())
            points_.removeLast();
        preview(points_.isEmpty() ? QPointF() : points_.last());
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        finish();
        return true;
    }
    return false;
}
void DrawingLocalSectionTool::finish() {
    if (!active_)
        return;
    front_->interpretText();
    depth_->interpretText();
    DrawingLocalSection section;
    section.front = front_->value();
    section.depth = depth_->value();
    for (auto p : points_) {
        const auto q = toModel_(p);
        section.outline.push_back({q.x(), q.y()});
    }
    auto candidate = definition_;
    candidate.localSection = section;
    const auto error = validateDrawingLocalSection(candidate);
    if (!error.isEmpty()) {
        message_->setText(error);
        return;
    }
    cancel();
    applyCallback_(std::move(section));
}
} // namespace ForgeCad
