#include "cad_drawing_local_section_depth.h"
#include "fk_tessellate.h"
#include <QComboBox>
#include <QEvent>
#include <QGraphicsItemGroup>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ForgeCad {
namespace {
using namespace Kernel;
bool perpendicular(const DrawingView &a, const DrawingView &b) {
    return std::abs(dot(a.plane.zDir(), b.plane.zDir())) < 1e-10;
}
QRectF profileBounds(const DrawingLocalSectionDepthView &reference) {
    auto bounds = reference.profile.boundingRect();
    if (bounds.width() < 1)
        bounds.adjust(-.5, 0, .5, 0);
    if (bounds.height() < 1)
        bounds.adjust(0, -.5, 0, .5);
    return bounds;
}
QPointF paperPoint(const DrawingView &view, Vec3 world) {
    const auto p = view.plane.toLocal(world);
    return {view.scale * p.x(), -view.scale * p.y()};
}
struct DepthCanvas final : QGraphicsView {
    using QGraphicsView::QGraphicsView;
    void fit() { fitInView(scene()->sceneRect(), Qt::KeepAspectRatio); }
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        fit();
    }
};
QGraphicsItemGroup *paintGuide(QGraphicsScene *scene, const DrawingLocalSectionDepthView &reference,
                              const DrawingLocalSectionDepthTrace &trace, double depth, double uiScale = 1, bool cosmetic = false) {
    const QColor blue("#1476b8"), orange("#b85300");
    auto *group = new QGraphicsItemGroup;
    scene->addItem(group);
    group->setData(0, "localSectionDepthGuide");
    group->setData(1, reference.view.id);
    group->setZValue(37);
    group->setAcceptedMouseButtons(Qt::NoButton);
    const auto line = [&](const QLineF &segment, QColor color, const char *kind, Qt::PenStyle style) {
        auto *item = new QGraphicsLineItem(segment, group);
        QPen pen(color, cosmetic ? 1.4 : .35, style);
        pen.setCosmetic(cosmetic);
        item->setPen(pen);
        item->setData(0, kind);
        item->setData(1, reference.view.id);
        item->setData(2, depth);
        item->setAcceptedMouseButtons(Qt::NoButton);
    };
    auto *band = new QGraphicsPolygonItem(trace.band, group);
    band->setPen(Qt::NoPen);
    band->setBrush(QColor(230, 134, 37, 25));
    band->setAcceptedMouseButtons(Qt::NoButton);
    line(trace.zero, blue, "localSectionZeroPlane", Qt::DashLine);
    line(trace.cut, orange, "localSectionCutPlane", Qt::SolidLine);
    auto dimension = trace.dimension;
    const auto tangentDirection = trace.zero.unitVector().p2() - trace.zero.unitVector().p1();
    dimension.translate(tangentDirection * (7 * (uiScale - 1)));
    line(dimension, orange, "localSectionDepthDimension", Qt::SolidLine);
    const auto unit = [](QPointF p) { return p / std::hypot(p.x(), p.y()); };
    const auto tangent = unit(trace.zero.p2() - trace.zero.p1());
    const auto inward = unit(dimension.p2() - dimension.p1());
    for (bool second : {false, true}) {
        const auto tip = second ? dimension.p2() : dimension.p1();
        const auto back = (second ? -inward : inward) * (1.4 * uiScale);
        QPainterPath arrow;
        arrow.moveTo(tip);
        arrow.lineTo(tip + back + tangent * (.55 * uiScale));
        arrow.lineTo(tip + back - tangent * (.55 * uiScale));
        arrow.closeSubpath();
        auto *item = new QGraphicsPathItem(arrow, group);
        item->setPen(Qt::NoPen);
        item->setBrush(orange);
        item->setAcceptedMouseButtons(Qt::NoButton);
    }
    const auto label = [&](QString text, QPointF position, QColor color) {
        auto *item = new QGraphicsSimpleTextItem(text, group);
        item->setBrush(color);
        item->setScale(2.4 * uiScale / item->boundingRect().height());
        item->setPos(position);
        item->setAcceptedMouseButtons(Qt::NoButton);
    };
    label(QStringLiteral("0"), trace.zero.p2() + tangent * (1.5 * uiScale) - inward * (3 * uiScale), blue);
    label(QStringLiteral("Taglio"), trace.cut.p2() + tangent * (1.5 * uiScale) + inward * uiScale, orange);
    label(QStringLiteral("%1 mm").arg(depth, 0, 'f', 2), dimension.center() + tangent * (2 * uiScale), orange);
    return group;
}
} // namespace

std::vector<DrawingLocalSectionDepthView> drawingLocalSectionDepthViews(
    const DrawingSheet &sheet, const DrawingView &target, const DrawingModelSnapshot &source,
    const std::function<QPainterPath(const QUuid &)> &profile,
    const std::function<std::vector<DrawingCurve>(const QUuid &)> &geometry) {
    const auto edited = std::find_if(sheet.views.begin(), sheet.views.end(),
                                    [&](const auto &layout) { return layout.viewId == target.id; });
    if (edited == sheet.views.end())
        throw std::invalid_argument("Vista della sezione locale assente.");
    auto features = edited->features;
    std::sort(features.begin(), features.end());
    std::vector<DrawingLocalSectionDepthView> result;
    for (const auto &view : sheet.drawing.views) {
        if (view.id == target.id || view.kind != DrawingViewKind::ProjectedEdges || !perpendicular(target, view))
            continue;
        const auto layout = std::find_if(sheet.views.begin(), sheet.views.end(),
                                        [&](const auto &value) { return value.viewId == view.id; });
        if (layout == sheet.views.end() || layout->interruption || layout->relation.kind == DrawingDerivedKind::Detail)
            continue;
        auto candidate = layout->features;
        std::sort(candidate.begin(), candidate.end());
        if (features != candidate)
            continue;
        auto path = profile(view.id);
        if (!path.isEmpty())
            result.push_back({view, *layout, std::move(path), true,
                geometry ? drawingLocalSectionSnapGeometry(view, geometry(view.id)) : nullptr, {}});
    }
    std::stable_sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
        const auto related = [&](const auto &r) {
            return r.view.id == edited->relation.parentId || r.layout.relation.parentId == target.id;
        };
        return related(a) && !related(b);
    });
    if (!result.empty())
        return result;
    // Vista d'appoggio solo nel pannello: una proiezione B-rep, senza sezioni, mesh o visibilita'.
    DrawingView view;
    view.kind = DrawingViewKind::ProjectedEdges;
    view.plane = Frame3::fromAxes(target.plane.origin(), target.plane.xDir(), target.plane.zDir(),
                                   -target.plane.yDir());
    std::vector<DrawingCurve> curves;
    auto selected = source;
    selected.bodies.erase(std::remove_if(selected.bodies.begin(), selected.bodies.end(),
        [&](const auto &body) { return !features.contains(body.featureId); }), selected.bodies.end());
    const auto error = drawingViewCurves(selected, view, curves);
    if (!error.isEmpty())
        throw std::runtime_error(error.toStdString());
    QPainterPath path;
    TessellationOptions options;
    options.deflection = .02;
    options.angle = .08;
    for (const auto &curve : curves) {
        const auto samples = sampleCurve(*curve.curve, curve.range, options);
        bool first = true;
        for (const auto parameter : samples) {
            const auto point = paperPoint(view, curve.curve->point(parameter));
            if (first)
                path.moveTo(point);
            else
                path.lineTo(point);
            first = false;
        }
    }
    DrawingViewLayout layout;
    layout.viewId = view.id;
    layout.title = QStringLiteral("Vista ortogonale d'appoggio");
    layout.features = features;
    result.push_back({view, layout, std::move(path), false, drawingLocalSectionSnapGeometry(view, curves), {}});
    return result;
}

double drawingLocalSectionDepthAt(const DrawingView &target,
    const DrawingLocalSectionDepthView &reference, QPointF paper) {
    if (!perpendicular(target, reference.view) || !std::isfinite(reference.view.scale) || reference.view.scale <= 0)
        throw std::invalid_argument("La vista di riferimento deve essere ortogonale alla vista sezionata.");
    const auto world = reference.view.plane.toGlobal(Vec3(paper.x() / reference.view.scale,
                                                        -paper.y() / reference.view.scale, 0));
    return dot(world - target.plane.origin(), target.plane.zDir());
}
DrawingLocalSectionDepthTrace drawingLocalSectionDepthTrace(const DrawingView &target,
    const DrawingLocalSectionDepthView &reference, double front, double depth, const QPolygonF &outline) {
    if (!std::isfinite(front) || !std::isfinite(depth) || depth <= 0)
        throw std::invalid_argument("Piano iniziale o profondita' non validi.");
    const double offset = drawingLocalSectionDepthAt(target, reference, {});
    const QPointF normal(dot(target.plane.zDir(), reference.view.plane.xDir()),
                         -dot(target.plane.zDir(), reference.view.plane.yDir()));
    const QPointF tangent(-normal.y(), normal.x());
    const auto bounds = profileBounds(reference);
    const auto scalar = [](QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); };
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (auto point : {bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(), bounds.bottomRight()}) {
        low = std::min(low, scalar(point, tangent));
        high = std::max(high, scalar(point, tangent));
    }
    double regionLow = low, regionHigh = high;
    if (!outline.isEmpty()) {
        regionLow = std::numeric_limits<double>::infinity();
        regionHigh = -regionLow;
        for (auto p : outline) {
            const auto point = paperPoint(reference.view, target.plane.toGlobal(Vec3(p.x(), p.y(), 0)));
            regionLow = std::min(regionLow, scalar(point, tangent));
            regionHigh = std::max(regionHigh, scalar(point, tangent));
        }
    }
    const auto point = [&](double level, double along) {
        return normal * ((level - offset) * reference.view.scale) + tangent * along;
    };
    DrawingLocalSectionDepthTrace trace;
    trace.zero = {point(front, low - 2), point(front, high + 2)};
    trace.cut = {point(front - depth, low - 2), point(front - depth, high + 2)};
    trace.dimension = {point(front, high + 7), point(front - depth, high + 7)};
    trace.band = {point(front, regionLow), point(front, regionHigh),
                  point(front - depth, regionHigh), point(front - depth, regionLow)};
    return trace;
}

DrawingLocalSectionDepthGuide::DrawingLocalSectionDepthGuide(QWidget *parent, QGraphicsScene *sheetScene, QGraphicsView *sheetCanvas,
    DrawingView target, std::vector<DrawingLocalSectionDepthView> references,
    std::function<void(bool, double)> picked)
    : QWidget(parent), target_(std::move(target)), references_(std::move(references)),
      picked_(std::move(picked)), sheetScene_(sheetScene), sheetCanvas_(sheetCanvas) {
    auto *form = new QVBoxLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSizeConstraint(QLayout::SetMinimumSize);
    selector_ = new QComboBox(this);
    selector_->setObjectName("drawingLocalSectionDepthView");
    for (const auto &reference : references_)
        selector_->addItem(reference.layout.title, reference.view.id);
    form->addWidget(new QLabel(QStringLiteral("Vista per la profondita':"), this));
    form->addWidget(selector_);
    previewScene_ = new QGraphicsScene(this);
    preview_ = new DepthCanvas(previewScene_, this);
    preview_->setObjectName("drawingLocalSectionDepthPreview");
    preview_->setMinimumHeight(190);
    preview_->setMinimumWidth(340);
    preview_->setMaximumHeight(240);
    preview_->setBackgroundBrush(Qt::white);
    preview_->setRenderHint(QPainter::Antialiasing);
    preview_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview_->setMouseTracking(true);
    preview_->viewport()->setMouseTracking(true);
    preview_->viewport()->installEventFilter(this);
    preview_->installEventFilter(this);
    form->addWidget(preview_);
    values_ = new QLabel(this);
    values_->setObjectName("drawingLocalSectionDepthValues");
    values_->setWordWrap(true);
    values_->setMinimumHeight(34);
    form->addWidget(values_);
    auto *row = new QHBoxLayout;
    zero_ = new QPushButton(QStringLiteral("Indica zero"), this);
    cut_ = new QPushButton(QStringLiteral("Indica taglio"), this);
    zero_->setObjectName("drawingLocalSectionPickZero");
    cut_->setObjectName("drawingLocalSectionPickCut");
    for (auto *button : {zero_, cut_}) {
        button->setCheckable(true);
        row->addWidget(button);
    }
    form->addLayout(row);
    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    form->addWidget(hint_);
    connect(selector_, &QComboBox::currentIndexChanged, this, [this] { selectPick(Pick::None); redraw(); });
    connect(zero_, &QPushButton::clicked, this, [this](bool checked) { selectPick(checked ? Pick::Zero : Pick::None); });
    connect(cut_, &QPushButton::clicked, this, [this](bool checked) { selectPick(checked ? Pick::Cut : Pick::None); });
    const bool available = !references_.empty();
    selector_->setEnabled(available);
    zero_->setEnabled(available);
    cut_->setEnabled(available);
    selectPick(Pick::None);
}
DrawingLocalSectionDepthGuide::~DrawingLocalSectionDepthGuide() { cancel(); }
void DrawingLocalSectionDepthGuide::cancel() {
    active_ = false;
    clearSnap();
    delete sheetGuide_;
    sheetGuide_ = nullptr;
    previewScene_->clear();
}
void DrawingLocalSectionDepthGuide::selectPick(Pick pick) {
    clearSnap();
    pick_ = pick;
    zero_->setChecked(pick == Pick::Zero);
    cut_->setChecked(pick == Pick::Cut);
    preview_->setCursor(pick == Pick::None ? Qt::ArrowCursor : Qt::CrossCursor);
    if (references_.empty())
        hint_->setText(QStringLiteral("Vista ortogonale non disponibile."));
    else if (pick == Pick::None)
        hint_->setText(QStringLiteral("Blu: zero della profondita'. Arancio: piano di taglio."));
    else
        hint_->setText(QStringLiteral("Clicca %1 nell'anteprima%2.")
            .arg(pick == Pick::Zero ? QStringLiteral("il piano iniziale") : QStringLiteral("il punto di taglio"),
                 references_[std::size_t(selector_->currentIndex())].onSheet
                    ? QStringLiteral(" o sulla vista selezionata nella tavola") : QString())
            + QStringLiteral(" Snap attivi; Tab cambia, Alt esclude."));
}
void DrawingLocalSectionDepthGuide::setParameters(double front, double depth, const QPolygonF &outline) {
    if (!active_ || (front == front_ && depth == depth_ && outline == outline_))
        return;
    front_ = front;
    depth_ = depth;
    outline_ = outline;
    redraw();
}
void DrawingLocalSectionDepthGuide::redraw() {
    clearSnap();
    delete sheetGuide_;
    sheetGuide_ = nullptr;
    previewScene_->clear();
    if (!active_ || references_.empty() || depth_ <= 0)
        return;
    const auto &reference = references_.at(std::size_t(selector_->currentIndex()));
    const auto trace = drawingLocalSectionDepthTrace(target_, reference, front_, depth_, outline_);
    QPen profilePen(QColor("#3d4852"), 1);
    profilePen.setCosmetic(true);
    auto *profile = previewScene_->addPath(reference.profile, profilePen);
    profile->setAcceptedMouseButtons(Qt::NoButton);
    auto bounds = profileBounds(reference);
    for (const auto &segment : {trace.zero, trace.cut})
        bounds = bounds.united(QRectF(segment.p1(), segment.p2()).normalized());
    const double zoom = std::min(preview_->viewport()->width() / std::max(1., bounds.width()),
                                 preview_->viewport()->height() / std::max(1., bounds.height()));
    paintGuide(previewScene_, reference, trace, depth_, std::max(1., 8 / zoom), true);
    previewScene_->setSceneRect(previewScene_->itemsBoundingRect().adjusted(-4, -4, 4, 4));
    static_cast<DepthCanvas *>(preview_)->fit();
    if (reference.onSheet) {
        sheetGuide_ = paintGuide(sheetScene_, reference, trace, depth_);
        sheetGuide_->setPos(reference.layout.position);
    }
    values_->setText(QStringLiteral("Zero: %1 mm dal piano della vista. Taglio: %2 mm. Profondita': %3 mm.")
        .arg(front_, 0, 'f', 2).arg(front_ - depth_, 0, 'f', 2).arg(depth_, 0, 'f', 2));
}
bool DrawingLocalSectionDepthGuide::pick(QPointF localPaper) {
    if (pick_ == Pick::None || references_.empty())
        return false;
    const auto &reference = references_.at(std::size_t(selector_->currentIndex()));
    const auto level = drawingLocalSectionDepthAt(target_, reference, localPaper);
    if (!std::isfinite(level) || std::abs(level) > 1e7 ||
        (pick_ == Pick::Cut && (front_ - level < 1e-7 || front_ - level > 1e7))) {
        hint_->setText(QStringLiteral("Il taglio deve essere all'interno rispetto allo zero. Scegli un altro punto."));
        return true;
    }
    const bool zero = pick_ == Pick::Zero;
    selectPick(Pick::None);
    picked_(zero, level);
    return true;
}
void DrawingLocalSectionDepthGuide::clearSnap() {
    delete snapMark_;
    snapMark_ = nullptr;
    snapChoices_.clear();
    snapCanvas_ = nullptr;
    snapCycle_ = 0;
}
QPointF DrawingLocalSectionDepthGuide::snap(QPointF localPaper, QGraphicsView *canvas, QPointF offset,
                                          Qt::KeyboardModifiers modifiers) {
    const double zoom = std::hypot(canvas->transform().m11(), canvas->transform().m12());
    if (canvas != snapCanvas_ || QLineF(localPaper, snapCursor_).length() * zoom > 3 || modifiers != snapModifiers_)
        snapCycle_ = 0;
    delete snapMark_;
    snapMark_ = nullptr;
    snapCanvas_ = canvas;
    snapCursor_ = localPaper;
    snapOffset_ = offset;
    snapModifiers_ = modifiers;
    const auto &reference = references_.at(std::size_t(selector_->currentIndex()));
    snapChoices_ = reference.snaps && !(modifiers & Qt::AltModifier)
        ? drawingLocalSectionSnapChoices(*reference.snaps, reference.view, localPaper, zoom, reference.snapVisible)
        : std::vector<DrawingLocalSectionSnapPoint>();
    if (snapChoices_.empty())
        return localPaper;
    const auto &point = snapChoices_[snapCycle_ % snapChoices_.size()];
    QPainterPath symbol;
    if (point.label == QStringLiteral("Punto medio")) {
        symbol.moveTo(0, -5); symbol.lineTo(5, 4); symbol.lineTo(-5, 4); symbol.closeSubpath();
    } else if (point.label == QStringLiteral("Centro")) {
        symbol.addEllipse(QPointF(), 4, 4); symbol.moveTo(-6, 0); symbol.lineTo(6, 0);
        symbol.moveTo(0, -6); symbol.lineTo(0, 6);
    } else if (point.label == QStringLiteral("Estremo"))
        symbol.addRect(-4, -4, 8, 8);
    else
        symbol.addEllipse(QPointF(), 5, 5);
    snapMark_ = canvas->scene()->addPath(symbol, QPen(QColor("#109750"), 1.5), QColor(255, 255, 255, 220));
    snapMark_->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    snapMark_->setData(0, "localSectionDepthSnap");
    snapMark_->setData(1, reference.view.id);
    snapMark_->setData(2, point.label);
    snapMark_->setData(3, drawingLocalSectionDepthAt(target_, reference, point.paper));
    snapMark_->setPos(point.paper + offset);
    snapMark_->setZValue(55);
    snapMark_->setAcceptedMouseButtons(Qt::NoButton);
    auto *label = new QGraphicsSimpleTextItem(point.label + (snapChoices_.size() > 1
        ? QStringLiteral(" (%1/%2) · Tab").arg(snapCycle_ % snapChoices_.size() + 1).arg(snapChoices_.size()) : QString()), snapMark_);
    label->setBrush(QColor("#087144"));
    label->setPos(8, -22);
    label->setAcceptedMouseButtons(Qt::NoButton);
    return point.paper;
}
bool DrawingLocalSectionDepthGuide::key(int key) {
    if (!active_ || pick_ == Pick::None)
        return false;
    if (key == Qt::Key_Clear) { clearSnap(); return true; }
    if (key == Qt::Key_Alt) { clearSnap(); return false; }
    if (key == Qt::Key_Tab) {
        if (snapCanvas_) { ++snapCycle_; snap(snapCursor_, snapCanvas_, snapOffset_, snapModifiers_); }
        return true;
    }
    return false;
}
bool DrawingLocalSectionDepthGuide::pointer(QPointF sheetPoint, bool click, Qt::KeyboardModifiers modifiers) {
    if (!active_ || pick_ == Pick::None || references_.empty())
        return false;
    const auto &reference = references_.at(std::size_t(selector_->currentIndex()));
    const auto local = sheetPoint - reference.layout.position;
    if (reference.onSheet && profileBounds(reference).adjusted(-8, -8, 8, 8).contains(local)) {
        const auto picked = snap(local, sheetCanvas_, reference.layout.position, modifiers);
        if (click) pick(picked);
    } else clearSnap();
    return true;
}
bool DrawingLocalSectionDepthGuide::eventFilter(QObject *watched, QEvent *event) {
    if (active_ && (watched == preview_ || watched == preview_->viewport()) && pick_ != Pick::None) {
        if (event->type() == QEvent::Leave) clearSnap();
        if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Tab) {
            event->accept(); return true;
        }
        if (event->type() == QEvent::KeyPress && key(static_cast<QKeyEvent *>(event)->key()))
            return true;
        if (watched == preview_->viewport() && (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress)) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (event->type() == QEvent::MouseMove || mouse->button() == Qt::LeftButton) {
                const auto picked = snap(preview_->mapToScene(mouse->position().toPoint()), preview_, {}, mouse->modifiers());
                if (event->type() == QEvent::MouseButtonPress) pick(picked);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace ForgeCad
