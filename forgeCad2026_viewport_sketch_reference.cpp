#include "forgeCad2026_viewport_internal.h"
#include "cad_sketch_apparent.h"

GeometryRef CadViewport::sketchReferenceSourceForHit(const SketchReferenceHit &hit, QString &error) const {
    GeometryRef ref;
    if (hit.sketch >= 0) {
        ref.kind = 7; ref.index = hit.sketch; ref.featureId = sketches_[hit.sketch].storyId;
        ref.element = {hit.entityKind, hit.entity, -1};
    } else {
        const auto &body = extrusions_[hit.body];
        ref.index = hit.body; ref.featureId = body.featureId;
        const ForgeCad::Kernel::Vec3 point(hit.point.x(), hit.point.y(), hit.point.z());
        if (ForgeCad::isCurveBody(body)) {
            ref.kind = 9; ref.point.x = point.x(); ref.point.y = point.y(); ref.point.z = point.z();
            ForgeCad::setCurveReferenceComponent(ref,body,hit.polyline);
        } else if (body.forgeBody) {
            ref.kind = 4;
            const int id = body.display.edgeIds.value(hit.polyline, -1);
            ForgeCad::Kernel::EdgeId edge;
            for (auto candidate : body.forgeBody->edges()) if (candidate.index == id) { edge = candidate; break; }
            if (!edge.valid()) { error = QStringLiteral("Identita' dello spigolo non disponibile."); return {}; }
            ref.point = ForgeCad::edgeReference(*body.forgeBody, edge, point);
        } else { error = QStringLiteral("La geometria sorgente non e' disponibile."); return {}; }
    }
    return ref;
}

namespace {
QString referenceProjectionKey(const GeometryRef &source, const SketchObject &sketch, int owner, bool contact) {
    QStringList key{QString::number(owner), QString::number(sketch.storyId), QString::number(sketch.plane),
        QString::number(sketch.customFrame), QString::number(source.kind), QString::number(source.index),
        QString::number(source.featureId), QString::number(source.point.subshape), source.point.name,
        QString::number(source.element.kind), QString::number(source.element.element), QString::number(source.element.point),
        QString::number(contact)};
    for (int i = 0; i < 3; ++i)
        key << QString::number(sketch.frame.origin[i], 'g', 17) << QString::number(sketch.frame.normal[i], 'g', 17)
            << QString::number(sketch.frame.xAxis[i], 'g', 17);
    return key.join('|');
}
}

void CadViewport::updateSketchReferenceHover(const QPoint &position, bool pointMode) {
    using namespace ForgeCad;
    const auto *active = activeSketchObject();
    const auto previousKey = sketchReferenceHoverKey_;
    const auto previousLabels = sketchReferenceHoverLabels_;
    const auto previousTarget = sketchReferenceHoverTarget_;
    sketchReferenceHoverLabels_.clear();
    sketchReferenceHoverSource_ = {};
    if (!active || sketchViewUnlocked_ || sketchOffsetSelectionCallback_
        || (drawingTool_ != DrawingTool::Select && drawingTool_ != DrawingTool::Dimension)) return;
    const QPointF raw = screenToSketchPoint(position);
    const auto local = findSketchElement(raw);
    // Le entita' dello schizzo realmente visibili mantengono la priorita'.
    if (local.kind >= 0 && !isHiddenSketchReference(*active, local.kind, local.index)) return;
    GeometryRef source;
    bool contact = false;
    QString error;
    if (pointMode) {
        // Ctrl sceglie punti notevoli, anche centri senza bordo sotto il mouse.
        // Non includere l'origine/piani/assi della cache nella scelta esterna.
        const auto &cache = sketchReferenceCache();
        double nearest = pickTolerance();
        const auto consider = [&](const QPointF &p, const std::shared_ptr<const SketchLinkedReference> &link) {
            const double d = pointDistance(raw, p);
            if (d < nearest) { nearest = d; source = link->source; contact = link->contact; }
        };
        for (const auto &link : cache.linkedReferences) {
            if (!link || !(link->source.kind == 3 || link->source.kind == 4 || link->source.kind == 6
                || link->source.kind == 7 || link->source.kind == 9 || isSketchApparentReference(link->source))) continue;
            for (const auto &entity : link->entities) {
                if (entity.kind == 0) {
                    const auto &segment = cache.segments.at(entity.element);
                    consider(segment.first, link); consider(segment.second, link);
                } else {
                    const auto &curve = cache.curves.at(entity.element);
                    for (int k = 0; k < curve.controlPoints.size(); ++k) {
                        if ((curve.tool == DrawingTool::Circle || curve.tool == DrawingTool::Polygon) && k > 0) continue;
                        if (curve.tool == DrawingTool::Converted && k != 0 && k != curve.controlPoints.size() - 1) continue;
                        consider(curve.controlPoints.at(k), link);
                    }
                }
            }
        }
    }
    if (source.kind < 0) {
        SketchReferenceHit hit;
        if (pickSketchReference(position, hit)) source = sketchReferenceSourceForHit(hit, error);
    }
    if (source.kind < 0) {
        const auto &cache = sketchReferenceCache();
        double nearest = pickTolerance();
        for (const auto &link : cache.linkedReferences) {
            if (!link || !isSketchApparentReference(link->source)) continue;
            for (const auto &entity : link->entities) {
                double d = std::numeric_limits<double>::max();
                if (entity.kind == 0) {
                    const auto &line = cache.segments.at(entity.element);
                    d = distanceToSegment(raw,line.first,line.second);
                } else for (const auto &span : curveGeometry(cache.curves.at(entity.element)))
                    d = std::min(d,Kernel::projectPoint(*span.curve,{raw.x(),raw.y()},span.range).distance);
                if (d < nearest) { nearest=d;source=link->source;contact=false; }
            }
        }
    }
    if (source.kind < 0) {
        double nearest = std::numeric_limits<double>::max();
        for (int i = 0; i < extrusions_.size(); ++i) {
            if (!referenceBodyEligible(i) || !extrusions_.at(i).forgeBody) continue;
            FaceHit hit;
            if (!pickBodyFace(i, position, hit) || hit.face < 0 || hit.distance >= nearest) continue;
            nearest = hit.distance;
            source = {}; source.kind = 5; source.index = i; source.featureId = extrusions_.at(i).featureId;
            QVector3D origin, direction;
            viewRay(position, origin, direction);
            const double t = hit.distance / double(direction.length());
            source.point = faceReference(*extrusions_.at(i).forgeBody, Kernel::FaceId(hit.face),
                {double(origin.x()) + t * double(direction.x()), double(origin.y()) + t * double(direction.y()),
                 double(origin.z()) + t * double(direction.z())});
            contact = true; // intersezione esatta della faccia con il piano attivo
        }
    }
    if (source.kind < 0 || !allowedSketchReference(source)) return;
    sketchReferenceHoverSource_ = source;
    const QString key = referenceProjectionKey(source, *active, activeSketch_, contact);
    if (key != sketchReferenceHoverKey_) {
        sketchReferenceHoverGeometry_ = {};
        auto &geometry = sketchReferenceHoverGeometry_;
        geometry.plane = active->plane; geometry.frame = active->frame; geometry.customFrame = active->customFrame;
        sketchReferenceHoverError_ = appendLinkedSketchReference(geometry, source, sketches_, extrusions_, contact, true);
        sketchReferenceHoverKey_ = key;
    }
    sketchReferenceHoverContact_ = contact;
    const QString label = sketchReferenceLabel(source, sketches_, extrusions_);
    sketchReferenceHoverLabels_.append((contact ? QStringLiteral("Sezione associativa proposta · ")
                                               : QStringLiteral("Proiezione associativa proposta · ")) + label);
    sketchReferenceHoverTarget_ = {};
    if (!sketchReferenceHoverError_.isEmpty()) {
        sketchReferenceHoverLabels_.append(sketchReferenceHoverError_);
        return;
    }
    const auto &geometry = sketchReferenceHoverGeometry_;
    // Scegliere un bordo intero non usa la priorita' di aggancio di un punto.
    // Un contatto/vertice sovrapposto non deve sostituire la sua proiezione.
    double nearest = std::numeric_limits<double>::max();
    for (int i = 0; i < geometry.segments.size(); ++i) {
        const auto &segment = geometry.segments.at(i);
        const double d = distanceToSegment(raw, segment.first, segment.second);
        if (d < nearest) {
            nearest = d;
            sketchReferenceHoverTarget_ = {0, i, segment.first == segment.second ? 0 : -1};
        }
    }
    for (int i = 0; i < geometry.curves.size(); ++i)
        for (const auto &span : curveGeometry(geometry.curves.at(i))) {
            const double d = Kernel::projectPoint(*span.curve, {raw.x(), raw.y()}, span.range).distance;
            if (d < nearest) { nearest = d; sketchReferenceHoverTarget_ = {1, i, -1}; }
        }
    if (pointMode) {
        const auto point = pointAttachment(raw, {}, geometry, pickTolerance(), false);
        if (point.target.kind >= 0 && point.type == ConstraintType::Coincident) sketchReferenceHoverTarget_ = point.target;
    }
    if (sketchReferenceHoverTarget_.kind < 0) return;
    if (sketchReferenceHoverTarget_.point >= 0)
        sketchReferenceHoverLabels_.append(describeRef(geometry, sketchReferenceHoverTarget_));
    auto selection = constraintSelection();
    if (key == previousKey && sketchReferenceHoverTarget_ == previousTarget
        && selection == sketchReferenceHoverSelection_ && !previousLabels.isEmpty()) {
        sketchReferenceHoverLabels_ = previousLabels;
        return;
    }
    sketchReferenceHoverSelection_ = selection;
    if (selection.isEmpty()) return;
    if (std::all_of(selection.begin(), selection.end(), [&](const auto &ref) {
        return ref.kind == 2 || isLinkedSketchEntity(*active, ref.kind, ref.element);
    })) {
        sketchReferenceHoverLabels_.append(QStringLiteral("Nessun vincolo applicabile alla selezione"));
        return;
    }
    SketchObject proposed = *active;
    const int segments = proposed.segments.size(), curves = proposed.curves.size();
    proposed.segments += geometry.segments; proposed.curves += geometry.curves;
    for (int i = 0; i < geometry.segments.size(); ++i) proposed.constructionSegments.append(segments + i);
    auto target = sketchReferenceHoverTarget_;
    target.element += target.kind == 0 ? segments : curves;
    selection.append(target);
    QStringList names;
    for (const auto type : applicableConstraints(proposed, selection)) names.append(constraintName(type));
    sketchReferenceHoverLabels_.append(names.isEmpty() ? QStringLiteral("Nessun vincolo applicabile alla selezione")
        : QStringLiteral("Vincoli disponibili: ") + names.join(QStringLiteral(" · ")));
}

bool CadViewport::selectSketchReferenceAt(const QPoint &position, bool pointMode, bool dimension) {
    using namespace ForgeCad;
    updateSketchReferenceHover(position, pointMode);
    if (sketchReferenceHoverSource_.kind < 0) return false;
    if (!sketchReferenceHoverError_.isEmpty() || sketchReferenceHoverTarget_.kind < 0) {
        showStatus(sketchReferenceHoverError_); return true;
    }
    const auto target = sketchReferenceHoverTarget_;
    SketchObject work = *activeSketchObject();
    QVector<SketchEntity> created;
    const QString error = appendLinkedSketchReference(work, sketchReferenceHoverSource_, sketches_, extrusions_,
                                                       sketchReferenceHoverContact_, true, 100.0, &created);
    if (!error.isEmpty()) { showStatus(error); return true; }
    int sourceEntity = -1;
    for (const auto &link : sketchReferenceHoverGeometry_.linkedReferences) if (link)
        for (int k = 0; k < link->entities.size(); ++k)
            if (link->entities[k].kind == target.kind && link->entities[k].element == target.element) sourceEntity = k;
    if (sourceEntity < 0 || sourceEntity >= created.size()) return false;
    const ConstraintRef selected{created[sourceEntity].kind, created[sourceEntity].index, target.point};
    sketches_[activeSketch_] = std::move(work);
    if (dimension) dimensionToolClick(screenToSketchPoint(position), selected);
    else {
        selectedConstraints_.clear();
        if (selected.point >= 0) {
            const auto found = std::find_if(selectedPoints_.begin(), selectedPoints_.end(), [&](const auto &point) {
                return point.kind == selected.kind && point.element == selected.element && point.point == selected.point;
            });
            if (found == selectedPoints_.end()) selectedPoints_.append({selected.kind, selected.element, selected.point});
            else selectedPoints_.erase(found);
        } else {
            const SketchElementSelection entity{selected.kind, selected.element};
            if (sketchSelections_.contains(entity)) sketchSelections_.removeAll(entity);
            else sketchSelections_.append(entity);
        }
        selectionChanged();
        const auto refs = constraintSelection();
        showStatus(refs.isEmpty() ? QStringLiteral("Riferimento deselezionato.")
            : QStringLiteral("Riferimento selezionato: %1. Scegli il vincolo nel pannello Vincoli.")
                .arg(describeRef(*activeSketchObject(), refs.last())));
    }
    update();
    return true;
}

void CadViewport::drawSketchReferenceHover(QPainter &painter) const {
    if (sketchReferenceHoverSource_.kind < 0 || sketchReferenceHoverLabels_.isEmpty()
        || drawingTool_ != DrawingTool::Dimension) return;
    drawGeometryRef(painter, sketchReferenceHoverSource_, kHoverColor);
    const auto &geometry = sketchReferenceHoverGeometry_;
    const auto &target = sketchReferenceHoverTarget_;
    if (target.kind < 0) return;
    painter.setPen(QPen(kHoverColor, 2.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    QPointF point;
    if (target.point >= 0 && ForgeCad::refPoint(geometry, target, point)) {
        painter.drawEllipse(projectWorldPoint(mapSketchPoint(point, geometry)), 6.0, 6.0);
    } else if (target.kind == 0) {
        const auto &segment = geometry.segments.at(target.element);
        painter.drawLine(projectWorldPoint(mapSketchPoint(segment.first, geometry)),
                         projectWorldPoint(mapSketchPoint(segment.second, geometry)));
    } else if (target.kind == 1) {
        const auto points = projectSketchPolyline(geometry.curves.at(target.element).samples, geometry);
        painter.drawPolyline(points.constData(), points.size());
    }
}
