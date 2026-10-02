#include "cad_history_graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QCheckBox>
#include <QApplication>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "cad_datum.h"
#include "cad_constraints.h"
#include "cad_model_history.h"
#include "fk_topology.h"

namespace ForgeCad {
namespace {

constexpr int kNodeKindRole = 0;
constexpr int kNodeIndexRole = 1;
constexpr int kNodeDetailsRole = 2;
constexpr double kMinReadableZoom = 0.72;
constexpr double kMaxZoom = 8.0;

class HistoryGraphEdge;

class HistoryGraphNode final : public QGraphicsRectItem {
public:
    HistoryGraphNode(QString key, const QRectF &rect, std::function<void(const QString &, const QPointF &)> moved,
                     std::function<void()> released)
        : QGraphicsRectItem(rect), key_(std::move(key)), moved_(std::move(moved)), released_(std::move(released)) {
        setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable
                 | QGraphicsItem::ItemSendsGeometryChanges | QGraphicsItem::ItemClipsChildrenToShape);
        setCursor(Qt::OpenHandCursor);
    }

    void addEdge(HistoryGraphEdge *edge) { edges_.append(edge); }
    void removeEdge(HistoryGraphEdge *edge) { edges_.removeAll(edge); }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        setCursor(Qt::ClosedHandCursor);
        QGraphicsRectItem::mousePressEvent(event);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override {
        setCursor(Qt::OpenHandCursor);
        QGraphicsRectItem::mouseReleaseEvent(event);
        if (released_) released_();
    }

private:
    QString key_;
    QVector<HistoryGraphEdge *> edges_;
    std::function<void(const QString &, const QPointF &)> moved_;
    std::function<void()> released_;
};

class HistoryGraphEdge final : public QGraphicsPathItem {
public:
    HistoryGraphEdge(HistoryGraphNode *source, HistoryGraphNode *target, QString text,
                     const QColor &color, bool valid, bool showLabel)
        : source_(source), target_(target), head_(new QGraphicsPolygonItem(this)),
          label_(new QGraphicsSimpleTextItem(std::move(text), this)), showLabel_(showLabel) {
        QPen edgePen(color, valid ? 1.8 : 3.0);
        if (!valid) edgePen.setStyle(Qt::DashLine);
        setPen(edgePen);
        setZValue(-2.0);
        setAcceptHoverEvents(true);
        head_->setPen(QPen(color));
        head_->setBrush(QBrush(color));
        head_->setZValue(2.0);
        label_->setBrush(color.lighter(145));
        label_->setPen(QPen(QColor(22, 27, 34), 2.5));
        QFont font = label_->font();
        font.setPointSizeF(10.0);
        font.setBold(true);
        label_->setFont(font);
        label_->setZValue(3.0);
        label_->setVisible(showLabel_);
        source_->addEdge(this);
        target_->addEdge(this);
        updateGeometry();
    }

    ~HistoryGraphEdge() override {
        if (source_) source_->removeEdge(this);
        if (target_ && target_ != source_) target_->removeEdge(this);
    }

    void updateGeometry() {
        const QRectF a = source_->sceneBoundingRect(), b = target_->sceneBoundingRect();
        QPointF start, end;
        if (std::fabs(a.center().x() - b.center().x()) < a.width() * 0.5) {
            const bool downward = a.center().y() < b.center().y();
            start = QPointF(a.center().x(), downward ? a.bottom() : a.top());
            end = QPointF(b.center().x(), downward ? b.top() : b.bottom());
        } else {
            const bool leftToRight = a.center().x() < b.center().x();
            start = QPointF(leftToRight ? a.right() : a.left(), a.center().y());
            end = QPointF(leftToRight ? b.left() : b.right(), b.center().y());
        }
        QPainterPath route(start);
        const qreal dx = end.x() - start.x();
        if (std::fabs(dx) > 20.0) {
            const qreal middle = start.x() + dx * 0.5;
            route.lineTo(middle, start.y());
            route.lineTo(middle, end.y());
        }
        route.lineTo(end);
        setPath(route);
        const QPointF before = route.pointAtPercent(0.96);
        const double angle = std::atan2(end.y() - before.y(), end.x() - before.x());
        QPolygonF arrow;
        arrow << end << end - QPointF(std::cos(angle - 0.48) * 11.0, std::sin(angle - 0.48) * 11.0)
              << end - QPointF(std::cos(angle + 0.48) * 11.0, std::sin(angle + 0.48) * 11.0);
        head_->setPolygon(arrow);
        label_->setPos(route.pointAtPercent(0.52) + QPointF(6.0, -label_->boundingRect().height() - 3.0));
        setToolTip(label_->text());
    }

protected:
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(14.0);
        return stroker.createStroke(path());
    }
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override {
        label_->setVisible(true);
        QPen highlighted = pen(); highlighted.setWidthF(pen().widthF() + 1.5); setPen(highlighted);
        QGraphicsPathItem::hoverEnterEvent(event);
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override {
        label_->setVisible(showLabel_);
        QPen normal = pen(); normal.setWidthF(std::max(1.0, pen().widthF() - 1.5)); setPen(normal);
        QGraphicsPathItem::hoverLeaveEvent(event);
    }

private:
    HistoryGraphNode *source_ = nullptr;
    HistoryGraphNode *target_ = nullptr;
    QGraphicsPolygonItem *head_ = nullptr;
    QGraphicsSimpleTextItem *label_ = nullptr;
    bool showLabel_ = false;
};

QVariant HistoryGraphNode::itemChange(GraphicsItemChange change, const QVariant &value) {
    const QVariant result = QGraphicsRectItem::itemChange(change, value);
    if (change == QGraphicsItem::ItemPositionHasChanged) {
        for (HistoryGraphEdge *edge : std::as_const(edges_)) edge->updateGeometry();
        if (scene() && moved_) moved_(key_, value.toPointF());
    }
    return result;
}

void clearHistoryGraphScene(QGraphicsScene *scene) {
    QVector<HistoryGraphEdge *> edges;
    for (QGraphicsItem *item : scene->items())
        if (!item->parentItem())
            if (auto *edge = dynamic_cast<HistoryGraphEdge *>(item)) edges.append(edge);
    for (HistoryGraphEdge *edge : std::as_const(edges)) delete edge;
    scene->clear();
}

class HistoryGraphView final : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;

protected:
    void wheelEvent(QWheelEvent *event) override {
        if (event->angleDelta().y() == 0) {
            QGraphicsView::wheelEvent(event);
            return;
        }
        const double requested = std::pow(1.0015, event->angleDelta().y());
        const double current = transform().m11();
        const double factor = std::clamp(requested, kMinReadableZoom / current, kMaxZoom / current);
        scale(factor, factor);
        event->accept();
    }
};

QString legendItem(const QColor &color, const QString &text) {
    return QStringLiteral("<span style='color:%1;font-size:18px'>■</span>&nbsp;%2")
        .arg(color.name(), text.toHtmlEscaped());
}

QString featureType(const ExtrusionObject &feature) {
    if (feature.operation >= 0)
        return QStringList{QStringLiteral("Unione"), QStringLiteral("Intersezione"), QStringLiteral("Differenza")}.value(feature.operation,
                                                                                                                            QStringLiteral("Booleana"));
    switch (feature.feature) {
    case BodyFeature::Extrusion: return QStringLiteral("Estrusione");
    case BodyFeature::Revolution: return QStringLiteral("Rivoluzione");
    case BodyFeature::Primitive: return QStringLiteral("Primitiva");
    case BodyFeature::Blend: return feature.blendChamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo");
    case BodyFeature::SheetTrim: return QStringLiteral("Taglio superficie");
    case BodyFeature::SheetExtend: return QStringLiteral("Estensione superficie");
    case BodyFeature::Scale: return QStringLiteral("Scala");
    case BodyFeature::Helix: return feature.helix.spiral ? QStringLiteral("Spirale") : QStringLiteral("Elica");
    case BodyFeature::Sweep: return QStringLiteral("Sweep");
    case BodyFeature::Loft: return QStringLiteral("Loft");
    case BodyFeature::Imported: return QStringLiteral("Importato");
    case BodyFeature::DatumPlane: return QStringLiteral("Piano datum");
    case BodyFeature::Pattern: return QStringLiteral("Ripetizione");
    case BodyFeature::Transform: return QStringLiteral("Trasformazione");
    }
    return QStringLiteral("Feature");
}

QString geometryKind(int kind) {
    switch (kind) {
    case 0: return QStringLiteral("origine");
    case 1: return QStringLiteral("piano globale");
    case 2: return QStringLiteral("asse globale");
    case 3: return QStringLiteral("vertice");
    case 4: return QStringLiteral("spigolo");
    case 5: return QStringLiteral("faccia");
    case 6: return QStringLiteral("punto di schizzo");
    case 7: return QStringLiteral("entita' di schizzo");
    case 8: return QStringLiteral("piano datum");
    case 9: return QStringLiteral("curva 3D");
    case 10: return QStringLiteral("estremo curva 3D");
    default: return QStringLiteral("non definito");
    }
}

struct Dependency {
    enum class Source { Feature, Sketch, Global, Missing } source = Source::Missing;
    int index = -1;
    QString key;
    QString label;
    QString detail;
    bool valid = true;
};

void addBodyDependency(QVector<Dependency> &result, const QVector<ExtrusionObject> &features, int owner, int index, const QString &label) {
    Dependency dependency;
    dependency.source = index >= 0 && index < features.size() ? Dependency::Source::Feature : Dependency::Source::Missing;
    dependency.index = index;
    dependency.key = dependency.source == Dependency::Source::Feature ? QStringLiteral("f:%1").arg(index)
                                                                      : QStringLiteral("missing:f:%1:%2").arg(owner).arg(index);
    dependency.label = label;
    dependency.valid = index >= 0 && index < owner && index < features.size();
    dependency.detail = dependency.valid ? QStringLiteral("feature #%1, ID %2").arg(index).arg(features.at(index).featureId)
                                         : QStringLiteral("indice #%1 non precedente alla feature #%2").arg(index).arg(owner);
    result.append(dependency);
}

void addSketchDependency(QVector<Dependency> &result, const QVector<SketchObject> &sketches, int owner, int index, const QString &label) {
    Dependency dependency;
    dependency.source = index >= 0 && index < sketches.size() ? Dependency::Source::Sketch : Dependency::Source::Missing;
    dependency.index = index;
    dependency.key = dependency.source == Dependency::Source::Sketch ? QStringLiteral("s:%1").arg(index)
                                                                      : QStringLiteral("missing:s:%1:%2").arg(owner).arg(index);
    dependency.label = label;
    dependency.valid = index >= 0 && index < sketches.size();
    dependency.detail = dependency.valid ? sketches.at(index).name : QStringLiteral("schizzo #%1 mancante").arg(index);
    result.append(dependency);
}

int featureOwner(const GeometryRef &ref, const QVector<ExtrusionObject> &features) {
    if (ref.featureId)
        for (int index = 0; index < features.size(); ++index)
            if (features.at(index).featureId == ref.featureId) return index;
    return ref.index;
}

void addGeometryDependency(QVector<Dependency> &result, const GeometryRef &ref, const QVector<SketchObject> &sketches,
                           const QVector<ExtrusionObject> &features, int owner, const QString &label) {
    Dependency dependency;
    dependency.label = label + QStringLiteral(" • ") + geometryKind(ref.kind);
    dependency.detail = geometryRefText(ref, sketches, features)
        + QStringLiteral("\nkind=%1, featureId=%2, subshape=%3, geometry=%4, context=%5")
              .arg(ref.kind).arg(ref.featureId).arg(ref.point.subshape).arg(ref.point.geometry).arg(ref.point.context);
    if ((ref.kind >= 3 && ref.kind <= 5) || (ref.kind >= 8 && ref.kind <= 10)) {
        const int index = featureOwner(ref, features);
        dependency.source = index >= 0 && index < features.size() ? Dependency::Source::Feature : Dependency::Source::Missing;
        dependency.index = index;
        dependency.key = dependency.source == Dependency::Source::Feature ? QStringLiteral("f:%1").arg(index)
                                                                          : QStringLiteral("missing:r:%1:%2").arg(owner).arg(ref.featureId);
        dependency.valid = index >= 0 && index < owner && index < features.size();
    } else if (ref.kind == 6 || ref.kind == 7) {
        dependency.source = ref.index >= 0 && ref.index < sketches.size() ? Dependency::Source::Sketch : Dependency::Source::Missing;
        dependency.index = ref.index;
        dependency.key = dependency.source == Dependency::Source::Sketch ? QStringLiteral("s:%1").arg(ref.index)
                                                                          : QStringLiteral("missing:rs:%1:%2").arg(owner).arg(ref.index);
        dependency.valid = ref.index >= 0 && ref.index < sketches.size();
    } else if (ref.kind >= 0 && ref.kind <= 2) {
        dependency.source = Dependency::Source::Global;
        dependency.index = ref.index;
        dependency.key = QStringLiteral("g:%1:%2").arg(ref.kind).arg(ref.index);
        dependency.valid = true;
    } else {
        dependency.source = Dependency::Source::Missing;
        dependency.key = QStringLiteral("missing:r:%1:invalid").arg(owner);
        dependency.valid = false;
    }
    result.append(dependency);
}

QVector<Dependency> dependencies(int index, const DocumentState &document) {
    const QVector<ExtrusionObject> &features = document.extrusions;
    const QVector<SketchObject> &sketches = document.sketches;
    const ExtrusionObject &feature = features.at(index);
    QVector<Dependency> result;
    if (feature.operation >= 0) {
        addBodyDependency(result, features, index, feature.firstBody, QStringLiteral("operando A"));
        addBodyDependency(result, features, index, feature.secondBody, QStringLiteral("operando B"));
        for (int k = 0; k < feature.booleanTools.size(); ++k)
            addBodyDependency(result, features, index, feature.booleanTools.at(k), QStringLiteral("strumento %1").arg(k + 1));
        return result;
    }
    switch (feature.feature) {
    case BodyFeature::Extrusion:
        addSketchDependency(result, sketches, index, feature.sketchIndex, QStringLiteral("profilo"));
        for (int body : feature.mergeBodies) addBodyDependency(result, features, index, body, QStringLiteral("fusione"));
        if (feature.extent != 0) addGeometryDependency(result, feature.extentRef, sketches, features, index, QStringLiteral("fine estrusione"));
        break;
    case BodyFeature::Revolution:
        addSketchDependency(result, sketches, index, feature.sketchIndex, QStringLiteral("profilo"));
        break;
    case BodyFeature::Blend:
        addBodyDependency(result, features, index, feature.firstBody,
                          QStringLiteral("base • %1 spigoli").arg(feature.blendEdges.size()));
        break;
    case BodyFeature::SheetExtend:
        addBodyDependency(result, features, index, feature.firstBody,
                          QStringLiteral("superficie • %1 bordi").arg(feature.blendEdges.size()));
        break;
    case BodyFeature::Scale:
        addBodyDependency(result, features, index, feature.firstBody, QStringLiteral("corpo"));
        break;
    case BodyFeature::SheetTrim:
        addBodyDependency(result, features, index, feature.firstBody, QStringLiteral("superficie"));
        if (feature.secondBody >= 0) addBodyDependency(result, features, index, feature.secondBody, QStringLiteral("strumento"));
        break;
    case BodyFeature::Helix:
        if (feature.helix.source == 0) addSketchDependency(result, sketches, index, feature.sketchIndex, QStringLiteral("base"));
        else addBodyDependency(result, features, index, feature.firstBody,
                               feature.helix.source == 1 ? QStringLiteral("spigolo base") : QStringLiteral("faccia base"));
        break;
    case BodyFeature::Sweep:
        addSketchDependency(result, sketches, index, feature.sketchIndex, QStringLiteral("profilo"));
        if (feature.sweepPath == 0) addSketchDependency(result, sketches, index, feature.pathSketch, QStringLiteral("percorso"));
        else addBodyDependency(result, features, index, feature.firstBody, QStringLiteral("curva percorso"));
        for (int body : feature.mergeBodies) addBodyDependency(result, features, index, body, QStringLiteral("fusione"));
        break;
    case BodyFeature::Loft:
        for (int k = 0; k < feature.loftSketches.size(); ++k)
            addSketchDependency(result, sketches, index, feature.loftSketches.at(k), QStringLiteral("sezione %1").arg(k + 1));
        for (int k = 0; k < feature.loftGuides.size(); ++k)
            addSketchDependency(result, sketches, index, feature.loftGuides.at(k), QStringLiteral("guida %1").arg(k + 1));
        for (int k = 0; k < feature.loftGuidePaths.size(); ++k)
            addSketchDependency(result, sketches, index, feature.loftGuidePaths.at(k).sketch,
                                QStringLiteral("guida parziale %1").arg(k + 1));
        break;
    case BodyFeature::DatumPlane:
        for (int k = 0; k < feature.datum.refs.size(); ++k)
            addGeometryDependency(result, feature.datum.refs.at(k), sketches, features, index, QStringLiteral("riferimento %1").arg(k + 1));
        break;
    case BodyFeature::Pattern:
        addBodyDependency(result, features, index, feature.firstBody, feature.pattern.featureOnly ? QStringLiteral("feature") : QStringLiteral("corpo"));
        for (int k = 0; k < feature.pattern.refs.size(); ++k)
            addGeometryDependency(result, feature.pattern.refs.at(k), sketches, features, index, QStringLiteral("direzione %1").arg(k + 1));
        break;
    case BodyFeature::Transform:
        addBodyDependency(result, features, index, feature.firstBody, feature.move.copy ? QStringLiteral("corpo copiato") : QStringLiteral("corpo"));
        if (std::fabs(feature.move.angle) > 0.0)
            addGeometryDependency(result, feature.move.axis, sketches, features, index, QStringLiteral("asse rotazione"));
        break;
    default: break;
    }
    return result;
}

QString featureDetails(int index, const DocumentState &document, const QVector<Dependency> &deps) {
    const ExtrusionObject &feature = document.extrusions.at(index);
    QStringList text{
        QStringLiteral("%1 (#%2)").arg(feature.name).arg(index),
        QStringLiteral("Tipo: %1").arg(featureType(feature)),
        QStringLiteral("Feature ID: %1").arg(feature.featureId),
        QStringLiteral("Model body ID: %1").arg(feature.modelBodyId),
        QStringLiteral("Stato: %1").arg(feature.suppressed ? QStringLiteral("soppressa")
                                                            : feature.error.isEmpty() ? QStringLiteral("calcolata") : QStringLiteral("ERRORE"))
    };
    if (feature.forgeBody) {
        const Kernel::TopologyCounts count = feature.forgeBody->counts();
        text << QStringLiteral("B-rep: %1 vertici, %2 spigoli, %3 facce, %4 shell")
                    .arg(count.vertices).arg(count.edges).arg(count.faces).arg(count.shells);
    } else if (feature.curve) {
        text << QStringLiteral("Geometria: curva 3D");
    } else if (feature.datumValid) {
        text << QStringLiteral("Geometria: piano datum");
    } else {
        text << QStringLiteral("Geometria: nessun risultato");
    }
    if (!feature.error.isEmpty()) text << QStringLiteral("\nErrore:\n%1").arg(feature.error);
    text << QStringLiteral("\nDipendenze:");
    if (deps.isEmpty()) text << QStringLiteral("- nessuna");
    for (const Dependency &dependency : deps)
        text << QStringLiteral("- %1: %2%3").arg(dependency.label, dependency.detail,
                                                  dependency.valid ? QString() : QStringLiteral("  [NON VALIDA]"));
    if (!feature.blendEdges.isEmpty()) {
        text << QStringLiteral("\nSotto-entita' selezionate:");
        for (int k = 0; k < feature.blendEdges.size(); ++k) {
            const EdgePoint &edge = feature.blendEdges.at(k);
            text << QStringLiteral("- bordo %1: subshape=%2, tipo=%3, contesto=%4, punto=(%5, %6, %7)")
                        .arg(k + 1).arg(edge.subshape).arg(edge.geometry).arg(edge.context).arg(edge.x).arg(edge.y).arg(edge.z);
        }
    }
    return text.join(QLatin1Char('\n'));
}

QColor nodeColor(const ExtrusionObject &feature, bool tip) {
    if (!feature.error.isEmpty()) return QColor(126, 37, 42);
    if (feature.suppressed) return QColor(68, 73, 82);
    if (feature.modelBodyId == 0) return QColor(82, 55, 120);
    if (tip) return QColor(32, 103, 82);
    return QColor(38, 68, 96);
}

}

HistoryGraphDialog::HistoryGraphDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Debug history / storyboard"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setSizeGripEnabled(true);
    setMinimumSize(700, 450);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (!restoreGeometry(QSettings().value(QStringLiteral("view/historyGraphGeometry")).toByteArray()))
        resize(1200, 760);
    auto *layout = new QVBoxLayout(this);
    auto *tools = new QHBoxLayout;
    auto *refresh = new QPushButton(QStringLiteral("Aggiorna"), this);
    auto *fit = new QPushButton(QStringLiteral("Adatta leggibile"), this);
    auto *resetLayout = new QPushButton(QStringLiteral("Ripristina disposizione"), this);
    fit->setToolTip(QStringLiteral("Adatta il grafo senza rendere il testo troppo piccolo"));
    auto *zoomOut = new QPushButton(QStringLiteral("−"), this);
    auto *zoomIn = new QPushButton(QStringLiteral("+"), this);
    zoomOut->setToolTip(QStringLiteral("Riduci il grafo (anche con la rotella del mouse)"));
    zoomIn->setToolTip(QStringLiteral("Ingrandisci il grafo (anche con la rotella del mouse)"));
    zoomOut->setMaximumWidth(34);
    zoomIn->setMaximumWidth(34);
    showSketches_ = new QCheckBox(QStringLiteral("Mostra schizzi"), this);
    showLabels_ = new QCheckBox(QStringLiteral("Mostra tutte le etichette"), this);
    showLabels_->setToolTip(QStringLiteral("Se disattivato, il nome della dipendenza appare passando sulla freccia"));
    showSketches_->setChecked(true);
    showLabels_->setChecked(false);
    summary_ = new QLabel(this);
    tools->addWidget(refresh);
    tools->addWidget(fit);
    tools->addWidget(resetLayout);
    tools->addWidget(zoomOut);
    tools->addWidget(zoomIn);
    tools->addWidget(showSketches_);
    tools->addWidget(showLabels_);
    tools->addStretch();
    tools->addWidget(summary_);
    layout->addLayout(tools);

    auto *legend = new QLabel(this);
    legend->setWordWrap(true);
    legend->setText(QStringList{
        legendItem(QColor(32, 103, 82), QStringLiteral("risultato del corpo")),
        legendItem(QColor(38, 68, 96), QStringLiteral("lavorazione intermedia")),
        legendItem(QColor(82, 55, 120), QStringLiteral("geometria di riferimento")),
        legendItem(QColor(37, 92, 108), QStringLiteral("schizzo")),
        legendItem(QColor(126, 37, 42), QStringLiteral("errore o riferimento non valido")),
        legendItem(QColor(68, 73, 82), QStringLiteral("feature soppressa"))
    }.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")));
    layout->addWidget(legend);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    scene_ = new QGraphicsScene(this);
    view_ = new HistoryGraphView(scene_, splitter);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setBackgroundBrush(QColor(22, 27, 34));
    details_ = new QTextBrowser(splitter);
    details_->setMinimumWidth(330);
    details_->setPlaceholderText(QStringLiteral("Seleziona una feature o uno schizzo per vedere ID, dipendenze, riferimenti e messaggi del kernel."));
    splitter->addWidget(view_);
    splitter->addWidget(details_);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter);

    connect(refresh, &QPushButton::clicked, this, &HistoryGraphDialog::rebuild);
    connect(fit, &QPushButton::clicked, this, &HistoryGraphDialog::fitGraph);
    connect(resetLayout, &QPushButton::clicked, this, [this] {
        manualPositions_.clear();
        rebuild();
        fitGraph();
    });
    connect(zoomOut, &QPushButton::clicked, this, [this] { zoomBy(0.8); });
    connect(zoomIn, &QPushButton::clicked, this, [this] { zoomBy(1.25); });
    connect(showSketches_, &QCheckBox::toggled, this, &HistoryGraphDialog::rebuild);
    connect(showLabels_, &QCheckBox::toggled, this, &HistoryGraphDialog::rebuild);
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
        const QList<QGraphicsItem *> selected = scene_->selectedItems();
        if (selected.isEmpty()) return;
        QGraphicsItem *item = selected.first();
        details_->setPlainText(item->data(kNodeDetailsRole).toString());
        const int kind = item->data(kNodeKindRole).toInt();
        const int index = item->data(kNodeIndexRole).toInt();
        if (selectionCallback_ && (kind == 0 || kind == 1) && index >= 0) selectionCallback_(kind, index);
    });
}

HistoryGraphDialog::~HistoryGraphDialog() {
    QSettings().setValue(QStringLiteral("view/historyGraphGeometry"), saveGeometry());
    clearHistoryGraphScene(scene_);
}

void HistoryGraphDialog::setDocument(const DocumentState &document) {
    document_ = document;
    normalizeModelHistory(document_);
    rebuildWhenPointerIdle();
}

void HistoryGraphDialog::rebuildWhenPointerIdle() {
    if (QApplication::mouseButtons() == Qt::NoButton) {
        rebuildPending_ = false;
        rebuild();
        return;
    }
    if (rebuildPending_) return;
    rebuildPending_ = true;
    QTimer::singleShot(40, this, [this] {
        rebuildPending_ = false;
        rebuildWhenPointerIdle();
    });
}

void HistoryGraphDialog::fitGraph() {
    if (scene_->items().isEmpty()) return;
    const QRectF bounds = scene_->itemsBoundingRect().adjusted(-40, -40, 40, 40);
    view_->fitInView(bounds, Qt::KeepAspectRatio);
    const double fitted = view_->transform().m11();
    if (fitted < kMinReadableZoom) {
        view_->scale(kMinReadableZoom / fitted, kMinReadableZoom / fitted);
        // Nelle cronologie larghe mostra per prima la parte iniziale; il resto
        // resta raggiungibile trascinando o con la barra orizzontale.
        const double visibleWidth = view_->viewport()->width() / kMinReadableZoom;
        view_->centerOn(bounds.left() + visibleWidth * 0.5, bounds.center().y());
    }
}

void HistoryGraphDialog::zoomBy(double factor) {
    if (!view_ || !std::isfinite(factor) || factor <= 0.0) return;
    const double current = view_->transform().m11();
    const double limited = std::clamp(factor, kMinReadableZoom / current, kMaxZoom / current);
    view_->scale(limited, limited);
}

double HistoryGraphDialog::zoomFactor() const {
    return view_ ? view_->transform().m11() : 1.0;
}

void HistoryGraphDialog::rebuild() {
    clearHistoryGraphScene(scene_);
    details_->clear();
    nodeCount_ = edgeCount_ = invalidDependencies_ = 0;
    const QVector<ExtrusionObject> &features = document_.extrusions;
    const QVector<SketchObject> &sketches = document_.sketches;
    QHash<quint64, int> lanes;
    int nextLane = 1;
    for (const ExtrusionObject &feature : features)
        if (feature.modelBodyId && !lanes.contains(feature.modelBodyId)) lanes.insert(feature.modelBodyId, nextLane++);
    const int sketchLane = nextLane + 1;
    const qreal width = 285.0, height = 112.0, xStep = 350.0, yStep = 185.0;
    QHash<QString, HistoryGraphNode *> nodes;

    const auto addNode = [&](const QString &key, const QPointF &position, const QString &title, const QString &subtitle,
                             const QString &details, const QColor &fill, int kind, int index) {
        if (nodes.contains(key)) return nodes.value(key);
        auto *rect = new HistoryGraphNode(key, QRectF(QPointF(), QSizeF(width, height)),
                                          [this](const QString &nodeKey, const QPointF &nodePosition) {
                                              manualPositions_.insert(nodeKey, nodePosition);
                                          },
                                          [this] {
                                              QTimer::singleShot(0, this, [this] {
                                                  if (scene_) scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-80, -80, 80, 80));
                                              });
                                          });
        rect->setPen(QPen(fill.lighter(155), 2.0));
        rect->setBrush(QBrush(fill));
        rect->setPos(manualPositions_.value(key, position));
        scene_->addItem(rect);
        rect->setData(kNodeKindRole, kind);
        rect->setData(kNodeIndexRole, index);
        rect->setData(kNodeDetailsRole, details);
        rect->setToolTip(details);
        auto *text = new QGraphicsTextItem(rect);
        text->setDefaultTextColor(QColor(238, 243, 248));
        text->setTextWidth(width - 18.0);
        text->setHtml(QStringLiteral("<div style='font-size:12pt;font-weight:650'>%1</div>"
                                     "<div style='font-size:10.5pt;color:#d1deea;line-height:120%'>%2</div>")
                          .arg(title.toHtmlEscaped(), subtitle.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
        text->setPos(8.0, 5.0);
        text->setAcceptedMouseButtons(Qt::NoButton);
        nodes.insert(key, rect);
        ++nodeCount_;
        return rect;
    };

    // Etichette delle corsie.
    auto *referenceLabel = scene_->addSimpleText(QStringLiteral("GEOMETRIA DI RIFERIMENTO"));
    QFont laneFont = referenceLabel->font();
    laneFont.setPointSizeF(11.0);
    laneFont.setBold(true);
    referenceLabel->setFont(laneFont);
    referenceLabel->setBrush(QColor(196, 167, 235)); referenceLabel->setPos(0, -34);
    for (auto it = lanes.cbegin(); it != lanes.cend(); ++it) {
        QString name = QStringLiteral("CORPO %1").arg(it.key());
        for (const ModelBody &body : document_.modelBodies) if (body.id == it.key()) { name = body.name.toUpper(); break; }
        auto *label = scene_->addSimpleText(name);
        label->setFont(laneFont);
        label->setBrush(QColor(150, 190, 220)); label->setPos(0, -34 + it.value() * yStep);
    }

    QVector<QVector<Dependency>> allDependencies(features.size());
    for (int index = 0; index < features.size(); ++index) {
        const ExtrusionObject &feature = features.at(index);
        allDependencies[index] = dependencies(index, document_);
        const int lane = feature.modelBodyId ? lanes.value(feature.modelBodyId, 1) : 0;
        bool tip = false;
        for (const ModelBody &body : document_.modelBodies)
            if (body.id == feature.modelBodyId) { tip = body.tipFeatureId == feature.featureId; break; }
        QString state = feature.suppressed ? QStringLiteral("SOPPRESSA")
                      : !feature.error.isEmpty() ? QStringLiteral("ERRORE: %1").arg(feature.error.left(64))
                      : tip ? QStringLiteral("RISULTATO DEL CORPO") : QStringLiteral("stadio intermedio");
        const QString operation = featureType(feature);
        const QString featureName = feature.name.trimmed().isEmpty()
            ? QStringLiteral("%1 %2").arg(operation).arg(index + 1) : feature.name;
        addNode(QStringLiteral("f:%1").arg(index), QPointF(index * xStep, lane * yStep),
                QStringLiteral("#%1  %2").arg(index).arg(featureName),
                QStringLiteral("Lavorazione: %1\n%2").arg(operation, state),
                featureDetails(index, document_, allDependencies.at(index)), nodeColor(feature, tip), 1, index);
    }

    if (showSketches_->isChecked()) {
        for (int index = 0; index < sketches.size(); ++index) {
            int firstUse = features.size();
            for (int feature = 0; feature < allDependencies.size(); ++feature)
                for (const Dependency &dependency : allDependencies.at(feature))
                    if (dependency.source == Dependency::Source::Sketch && dependency.index == index) firstUse = std::min(firstUse, feature);
            const SketchObject &sketch = sketches.at(index);
            QStringList details{
                QStringLiteral("%1 (#%2)").arg(sketch.name).arg(index),
                QStringLiteral("Segmenti: %1").arg(sketch.segments.size()),
                QStringLiteral("Curve: %1").arg(sketch.curves.size()),
                QStringLiteral("Vincoli geometrici: %1").arg(sketch.geometricConstraints.size()),
                QStringLiteral("Piano: %1").arg(sketch.plane),
                QStringLiteral("Datum: %1").arg(sketch.datumPlane)
            };
            if (!sketch.geometricConstraints.isEmpty()) {
                details << QStringLiteral("\nVincoli:");
                for (int constraint = 0; constraint < sketch.geometricConstraints.size(); ++constraint) {
                    const SketchConstraint &value = sketch.geometricConstraints.at(constraint);
                    details << QStringLiteral("- #%1 %2 • residuo %3")
                                   .arg(constraint)
                                   .arg(describeConstraint(sketch, value))
                                   .arg(constraintError(sketch, value), 0, 'g', 6);
                }
            }
            addNode(QStringLiteral("s:%1").arg(index), QPointF((firstUse == features.size() ? index : firstUse) * xStep, sketchLane * yStep),
                    QStringLiteral("Schizzo #%1  %2").arg(index).arg(sketch.name),
                    QStringLiteral("%1 segmenti • %2 curve • %3 vincoli")
                        .arg(sketch.segments.size()).arg(sketch.curves.size()).arg(sketch.geometricConstraints.size()),
                    details.join(QLatin1Char('\n')), QColor(37, 92, 108), 0, index);
        }
        auto *sketchLabel = scene_->addSimpleText(QStringLiteral("SCHIZZI"));
        sketchLabel->setFont(laneFont);
        sketchLabel->setBrush(QColor(115, 205, 220)); sketchLabel->setPos(0, -34 + sketchLane * yStep);
    }

    // Crea nodi globali o mancanti richiesti dalle dipendenze.
    int globalAuxiliary = 0;
    int missingAuxiliary = 0;
    for (int owner = 0; owner < allDependencies.size(); ++owner)
        for (const Dependency &dependency : allDependencies.at(owner)) {
            if (nodes.contains(dependency.key)) continue;
            if (dependency.source == Dependency::Source::Global) {
                addNode(dependency.key, QPointF(-400.0, globalAuxiliary++ * 150.0), dependency.detail,
                        QStringLiteral("riferimento globale"), dependency.detail, QColor(70, 65, 105), -1, -1);
            } else if (dependency.source == Dependency::Source::Missing
                       || (!showSketches_->isChecked() && dependency.source == Dependency::Source::Sketch)) {
                if (dependency.source == Dependency::Source::Sketch && !showSketches_->isChecked()) continue;
                addNode(dependency.key, QPointF(owner * xStep, -180.0 - missingAuxiliary++ * 150.0), QStringLiteral("RIFERIMENTO MANCANTE"),
                        dependency.detail, dependency.detail, QColor(125, 35, 38), -1, -1);
            }
        }

    // Archi dietro ai blocchi.
    for (int owner = 0; owner < allDependencies.size(); ++owner) {
        const QString targetKey = QStringLiteral("f:%1").arg(owner);
        HistoryGraphNode *target = nodes.value(targetKey, nullptr);
        if (!target) continue;
        for (const Dependency &dependency : allDependencies.at(owner)) {
            HistoryGraphNode *source = nodes.value(dependency.key, nullptr);
            if (!source) continue;
            const QColor color = dependency.valid ? QColor(104, 165, 207) : QColor(255, 76, 76);
            auto *edge = new HistoryGraphEdge(source, target, dependency.label, color,
                                              dependency.valid, showLabels_->isChecked());
            scene_->addItem(edge);
            edge->updateGeometry();
            if (!dependency.valid) ++invalidDependencies_;
            ++edgeCount_;
        }
    }
    int failures = 0, suppressed = 0;
    for (const ExtrusionObject &feature : features) {
        failures += feature.error.isEmpty() ? 0 : 1;
        suppressed += feature.suppressed ? 1 : 0;
    }
    summary_->setText(QStringLiteral("%1 feature • %2 dipendenze • %3 non valide • %4 errori • %5 soppresse")
                          .arg(features.size()).arg(edgeCount_).arg(invalidDependencies_).arg(failures).arg(suppressed));
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-50, -50, 50, 50));
    if (firstFit_) {
        firstFit_ = false;
        QTimer::singleShot(0, this, &HistoryGraphDialog::fitGraph);
    }
}

}
