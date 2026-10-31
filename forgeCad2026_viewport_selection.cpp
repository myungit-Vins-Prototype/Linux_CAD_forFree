#include "forgeCad2026_viewport_internal.h"


SceneSelection CadViewport::selection() const { return selection_; }

int CadViewport::selectedFeatureIndex() const {
    if (selection_.kind != SceneObjectKind::Extrusion) return -1;
    return selectedFace_.body == selection_.index && selectedFeature_ >= 0 ? selectedFeature_ : selection_.index;
}

int CadViewport::sourceFeatureForFace(const GeometryRef &ref) const {
    if (ref.kind != 5 || ref.index < 0 || ref.index >= extrusions_.size() || !extrusions_.at(ref.index).forgeBody) return -1;
    QVector<int> chain = operandClosure(ref.index).values();
    if (!chain.contains(ref.index)) chain.append(ref.index);
    std::sort(chain.begin(), chain.end());
    std::vector<std::pair<int, ForgeCad::ForgeBody>> sources;
    for (int i : chain) if (!extrusions_.at(i).suppressed && extrusions_.at(i).forgeBody)
        sources.emplace_back(i, extrusions_.at(i).forgeBody);
    return ForgeCad::forgeFaceOwner(*extrusions_.at(ref.index).forgeBody, ref.point.subshape,
        {ref.point.x, ref.point.y, ref.point.z}, sources);
}

QVector<SceneSelection> CadViewport::selectedObjects() const { return selectedObjects_.isEmpty() && selection_.kind != SceneObjectKind::None
                                                             ? QVector<SceneSelection>{selection_} : selectedObjects_; }

void CadViewport::setSelectedObjects(const QVector<SceneSelection> &objects) {
    if (sketchMode_) return;
    selectedObjects_.clear();
    for (const SceneSelection &object : objects)
        if ((object.kind == SceneObjectKind::Sketch && object.index >= 0 && object.index < sketches_.size())
            || (object.kind == SceneObjectKind::Extrusion && object.index >= 0 && object.index < extrusions_.size()))
            if (!selectedObjects_.contains(object)) selectedObjects_.append(object);
    selection_ = selectedObjects_.isEmpty() ? SceneSelection() : selectedObjects_.last();
    if (selectedObjects_.size() < 2) selectedObjects_.clear();
    selectedFace_ = {};
    QVector<int> features;
    for (const SceneSelection &object : objects)
        if (object.kind == SceneObjectKind::Extrusion) features.append(object.index);
    setFeatureHighlights(features);
    if (objects.size() > 1) showStatus(QStringLiteral("%1 oggetti selezionati (Canc elimina)").arg(objects.size()));
    update();
}

void CadViewport::setMultiSelectionCallback(std::function<void(const QVector<SceneSelection> &)> callback) { multiSelectionCallback_ = std::move(callback); }

void CadViewport::selectObject(SceneObjectKind kind, int index, bool wholeBody ) {
    if (activeSketchObject() && (kind == SceneObjectKind::Plane
        || (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size() && ForgeCad::isDatumBody(extrusions_.at(index))))) {
        selectSketchReference(kind == SceneObjectKind::Plane ? 1 : 8, index);
        return;
    }
    selection_ = {kind, index, -1};
    selectedFace_ = {};
    selectedFeature_ = kind == SceneObjectKind::Extrusion ? index : -1;
    selectedObjects_.clear();
    if (kind == SceneObjectKind::Plane) selectedPlane_ = index;
    if (kind == SceneObjectKind::Extrusion && wholeBody) setBodyHighlight(index);
    else setFeatureHighlights(kind == SceneObjectKind::Extrusion ? QVector<int>{index} : QVector<int>());
    update();
}

void CadViewport::armBoxSelection(const QPoint &position, bool additive) {
    boxArmed_ = true;
    boxSelecting_ = false;
    boxAdditive_ = additive;
    boxStart_ = boxEnd_ = position;
}

QRectF CadViewport::boxRect() const { return QRectF(QPointF(boxStart_), QPointF(boxEnd_)).normalized(); }

bool CadViewport::boxWindowMode() const { return boxEnd_.x() >= boxStart_.x(); }

bool CadViewport::segmentTouchesRect(const QPointF &a, const QPointF &b, const QRectF &r) {
    if (r.contains(a) || r.contains(b)) return true;
    const QPointF corners[] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    const QLineF segment(a, b);
    for (int k = 0; k < 4; ++k)
        if (segment.intersects(QLineF(corners[k], corners[(k + 1) % 4]), nullptr) == QLineF::BoundedIntersection) return true;
    return false;
}

bool CadViewport::polylineInBox(const QVector<QPointF> &points, const QRectF &r, bool window) const {
    if (points.isEmpty()) return false;
    if (window) {
        for (const QPointF &p : points)
            if (!r.contains(p)) return false;
        return true;
    }
    if (points.size() == 1) return r.contains(points.first());
    for (int k = 1; k < points.size(); ++k)
        if (segmentTouchesRect(points.at(k - 1), points.at(k), r)) return true;
    return false;
}

bool CadViewport::polylinesInBox(const QVector<QVector<QPointF>> &polylines, const QRectF &r, bool window) const {
    bool any = false;
    for (const QVector<QPointF> &polyline : polylines) {
        if (polyline.isEmpty()) continue;
        const bool in = polylineInBox(polyline, r, window);
        if (window && !in) return false;
        any = any || in;
        if (!window && any) return true;
    }
    return any;
}

QVector<QPointF> CadViewport::projectSketchPoints(const QVector<QPointF> &points, const SketchObject &sketch) const {
    QVector<QPointF> screen;
    screen.reserve(points.size());
    for (const QPointF &p : points) screen.append(projectWorldPoint(mapSketchPoint(p, sketch)));
    return screen;
}

QVector<QVector<QPointF>> CadViewport::sketchScreenPolylines(const SketchObject &sketch) const {
    QVector<QVector<QPointF>> polylines;
    for (const SketchSegment &segment : sketch.segments) polylines.append(projectSketchPoints({segment.first, segment.second}, sketch));
    for (const CurveObject &curve : sketch.curves) polylines.append(projectSketchPoints(curve.samples, sketch));
    return polylines;
}

void CadViewport::finishBoxSelection() {
    const QRectF r = boxRect();
    const bool window = boxWindowMode();
    if (sketchMode_) {
        const SketchObject *sketch = activeSketchObject();
        if (!sketch) return;
        if (!boxAdditive_) {
            sketchSelections_.clear();
            selectedPoints_.clear();
            selectedConstraints_.clear();
        }
        const auto add = [&](const SketchElementSelection &element) {
            if (!sketchSelections_.contains(element)) sketchSelections_.append(element);
        };
        for (int index = 0; index < sketch->segments.size(); ++index)
            if (polylineInBox(projectSketchPoints({sketch->segments.at(index).first, sketch->segments.at(index).second}, *sketch), r, window))
                add({0, index});
        for (int index = 0; index < sketch->curves.size(); ++index)
            if (polylineInBox(projectSketchPoints(sketch->curves.at(index).samples, *sketch), r, window)) add({1, index});
        showStatus(QStringLiteral("%1 entita' selezionate (Canc elimina)").arg(sketchSelections_.size()));
        selectionChanged();
        return;
    }
    QVector<SceneSelection> found = boxAdditive_ ? selectedObjects_ : QVector<SceneSelection>();
    if (boxAdditive_ && found.isEmpty() && selection_.kind != SceneObjectKind::None && selection_.kind != SceneObjectKind::Plane) found.append(selection_);
    const auto add = [&](const SceneSelection &object) {
        if (!found.contains(object)) found.append(object);
    };
    for (int index = 0; index < sketches_.size(); ++index)
        if (isSketchDrawn(index) && polylinesInBox(sketchScreenPolylines(sketches_.at(index)), r, window)) add({SceneObjectKind::Sketch, index, -1});
    for (int index = 0; index < extrusions_.size(); ++index) {
        const ExtrusionObject &body = extrusions_.at(index);
        if (!isBodyDrawn(index)) continue;
        QVector<QVector<QPointF>> polylines;
        if (ForgeCad::isDatumBody(body)) {
            if (!body.datumValid) continue;
            QVector<QPointF> corners;
            for (const QVector3D &corner : datumCorners(body.datumFrame, body.datum.size)) corners.append(projectWorldPoint(corner));
            if (!corners.isEmpty()) corners.append(corners.first());
            polylines.append(corners);
        } else {
            for (const QVector<QVector3D> &edge : body.display.edges) {
                QVector<QPointF> screen;
                screen.reserve(edge.size());
                for (const QVector3D &p : edge) screen.append(projectWorldPoint(p));
                polylines.append(screen);
            }
            if (polylines.isEmpty() && !body.display.vertices.isEmpty()) {
                QVector<QPointF> screen;
                for (const QVector3D &p : body.display.vertices) screen.append(projectWorldPoint(p));
                polylines.append(screen);
            }
        }
        if (polylinesInBox(polylines, r, window)) add({SceneObjectKind::Extrusion, index, -1});
    }
    selectedObjects_ = found;
    selection_ = found.isEmpty() ? SceneSelection() : found.first();
    selectedFace_ = {};
    clearFeatureHighlights();
    if (selectionCallback_) selectionCallback_(selection_);
    if (multiSelectionCallback_ && found.size() > 1) multiSelectionCallback_(found);
    showStatus(found.size() > 1 ? QStringLiteral("%1 oggetti selezionati (Canc elimina)").arg(found.size())
                                : found.isEmpty() ? QStringLiteral("Nessun oggetto nel riquadro") : QStringLiteral("1 oggetto selezionato"));
}

void CadViewport::toggleObjectAt(const QPoint &position) {
    const SceneSelection hit = pickSceneObject(position);
    if (hit.kind != SceneObjectKind::Sketch && hit.kind != SceneObjectKind::Extrusion) return;
    if (selectedObjects_.isEmpty() && (selection_.kind == SceneObjectKind::Sketch || selection_.kind == SceneObjectKind::Extrusion))
        selectedObjects_.append(selection_);
    if (selectedObjects_.contains(hit)) selectedObjects_.removeAll(hit);
    else selectedObjects_.append(hit);
    selection_ = selectedObjects_.isEmpty() ? SceneSelection() : selectedObjects_.last();
    selectedFace_ = {};
    clearFeatureHighlights();
    if (selectionCallback_) selectionCallback_(selection_);
    if (multiSelectionCallback_ && selectedObjects_.size() > 1) multiSelectionCallback_(selectedObjects_);
    if (selectedObjects_.size() > 1) showStatus(QStringLiteral("%1 oggetti selezionati (Canc elimina)").arg(selectedObjects_.size()));
}

QString CadViewport::beginEdgePick(bool chamfer, bool deferPreview ) {
    edgePickEdit_ = -1;
    return startEdgePick(chamfer, false, false, deferPreview);
}

QString CadViewport::beginHelixPick() {
    edgePickEdit_ = -1;
    return startEdgePick(false, false, true);
}

void CadViewport::setHelixPickCallback(std::function<void(int, int, EdgePoint)> callback) { helixPickFinished_ = std::move(callback); }

QString CadViewport::beginExtendPick(bool linear) {
    edgePickEdit_ = -1;
    edgePickLinear_ = linear;
    return startEdgePick(false, true);
}

void CadViewport::setExtendPickCallback(std::function<void(int, QVector<EdgePoint>)> callback) { extendPickFinished_ = std::move(callback); }

void CadViewport::setEdgePickLinear(bool linear) {
    edgePickLinear_ = linear;
    if (edgePicking_ && edgePickExtend_) edgePicked();
}

void CadViewport::resumeExtendPick(int body, const QVector<EdgePoint> &edges, bool linear) {
    if (body < 0 || body >= extrusions_.size()) return;
    selection_ = {SceneObjectKind::Extrusion, body, -1};
    selectedFace_ = {};
    if (!beginExtendPick(linear).isEmpty()) return;
    pickReferences(body, edges);
    edgePicked();
}

QString CadViewport::beginBlendEdit(int blend, double size, bool chamfer) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (blend < 0 || blend >= extrusions_.size() || extrusions_.at(blend).feature != BodyFeature::Blend
        || extrusions_.at(blend).operation >= 0)
        return QStringLiteral("Il corpo non e' un raccordo o uno smusso.");
    const ExtrusionObject &body = extrusions_.at(blend);
    if (body.firstBody < 0 || body.firstBody >= blend) return QStringLiteral("Il corpo da raccordare non esiste piu'.");
    selection_ = {SceneObjectKind::Extrusion, body.firstBody, -1};
    selectedFace_ = {};
    edgePickSize_ = size;
    const QString error = startEdgePick(chamfer);
    if (!error.isEmpty()) return error;
    edgePickEdit_ = blend;
    pickReferences(body.firstBody, body.blendEdges,
                   body.blendBaseFeature != 0 && body.blendBaseFeature == extrusions_.at(body.firstBody).featureId);
    edgePicked();
    return {};
}

void CadViewport::setEdgeEditCallback(std::function<void(int, QVector<EdgePoint>, double, bool)> callback) { edgeEditFinished_ = std::move(callback); }

void CadViewport::setEdgePickSize(double size) {
    edgePickSize_ = size;
    if (edgePicking_) edgePicked();
}

double CadViewport::edgePickSize() const { return edgePickSize_; }

void CadViewport::setEdgePickChamferSpec(const ChamferSpec &spec) {
    edgePickSpec_ = spec;
    if (edgePicking_ && edgePickChamfer_) edgePicked();
}

const ChamferSpec &CadViewport::edgePickChamferSpec() const { return edgePickSpec_; }

QString CadViewport::startEdgePick(bool chamfer, bool extend , bool helix , bool deferPreview ) {
    if (sketchMode_) return QStringLiteral("Esci prima dalla modalita' schizzo.");
    edgePickChamfer_ = chamfer;
    edgePickExtend_ = extend;
    edgePickHelix_ = helix;
    edgePickPreviewEnabled_ = !deferPreview;
    edgePickBody_ = -1;
    // Il corpo selezionato vale anche se nascosto (la base di un raccordo da modificare).
    if (!helix && selection_.kind == SceneObjectKind::Extrusion && selection_.index >= 0 && selection_.index < extrusions_.size()) {
        const ExtrusionObject &selected = extrusions_.at(selection_.index);
        if (ForgeCad::isShapeBody(selected) && (!extend || !selected.solid)) edgePickBody_ = selection_.index;
    }
    bool any = edgePickBody_ >= 0;
    for (int index = 0; index < extrusions_.size() && !any; ++index) any = edgePickEligible(index);
    if (!any) return helix ? QStringLiteral("Nella scena non ci sono corpi visibili con spigoli circolari o facce cilindriche.")
                 : extend ? QStringLiteral("Nella scena non ci sono superfici (estrusioni di profili aperti) visibili da estendere.")
                          : QStringLiteral("Nella scena non ci sono solidi o superfici cucite visibili da raccordare.");
    edgePicking_ = true;
    pickedEdges_.clear();
    pickedFaces_.clear();
    hoverEdge_ = -1;
    hoverFaceEdges_.clear();
    // Con una faccia selezionata del corpo si parte dai suoi bordi.
    if (edgePickBody_ >= 0 && selectedFace_.body == edgePickBody_) {
        pickedEdges_ = faceDisplayEdges(edgePickBody_, selectedFace_.hit);
        ForgeCad::Kernel::Vec3 inside;
        if (!extend && !helix && faceLabelPoint(edgePickBody_, selectedFace_.hit.face, inside))
            pickedFaces_.append({selectedFace_.hit.face, faceBoundaryReference(edgePickBody_, selectedFace_.hit.face, inside), pickedEdges_});
    }
    setFocus();
    edgePicked();
    return {};
}

void CadViewport::resumeEdgePick(int body, const QVector<EdgePoint> &edges, bool chamfer, bool deferPreview) {
    if (body < 0 || body >= extrusions_.size()) return;
    selection_ = {SceneObjectKind::Extrusion, body, -1};
    selectedFace_ = {};
    if (!beginEdgePick(chamfer, deferPreview).isEmpty()) return;
    pickReferences(body, edges);
    edgePicked();
}

void CadViewport::setEdgePickCallbacks(std::function<void(const QString &)> status,
                              std::function<void(int, QVector<EdgePoint>, bool)> finished) {
    edgePickStatus_ = std::move(status);
    edgePickFinished_ = std::move(finished);
}

void CadViewport::setEdgePickChangedCallback(std::function<void(int, QVector<EdgePoint>)> callback) {
    edgePickChanged_ = std::move(callback);
}

void CadViewport::setEdgePickPanelKeyCallbacks(std::function<void()> accept, std::function<void()> cancel) {
    edgePickPanelAccept_ = std::move(accept);
    edgePickPanelCancel_ = std::move(cancel);
}

void CadViewport::enableEdgePickPreview() {
    if (!edgePicking_ || edgePickPreviewEnabled_) return;
    edgePickPreviewEnabled_ = true;
    edgePicked();
}

int CadViewport::edgePickBody() const { return edgePickBody_; }

QVector<EdgePoint> CadViewport::edgePickPoints() const { return pickedEdgePoints(); }

bool CadViewport::edgePickPanelActive() const { return bool(edgePickChanged_); }

void CadViewport::endEdgePick(bool keepPreview ) { cancelEdgePick(keepPreview); }

QString CadViewport::beginReferencePick(int roles, int owner) {
    // Nello schizzo solo con la vista sbloccata (estrusione, rivoluzione): i clic scelgono, non disegnano.
    if (sketchMode_ && !sketchViewUnlocked_) return QStringLiteral("Esci prima dalla modalita' schizzo.");
    if (edgePicking_) cancelEdgePick();
    refPicking_ = true;
    refPickRoles_ = roles;
    refPickOwner_ = owner;
    refPickContextBodies_.clear();
    if (owner >= 0 && owner < extrusions_.size()) {
        const auto include = [&](int index) {
            if (index >= 0 && index < owner && !extrusions_.at(index).suppressed
                && extrusions_.at(index).error.isEmpty() && ForgeCad::isShapeBody(extrusions_.at(index)))
                refPickContextBodies_.insert(index);
        };
        for (int index : resultBodiesBefore(owner)) include(index);
        // Un riferimento esistente puo' usare anche uno stadio intermedio.
        for (int index : ForgeCad::bodyOperands(extrusions_.at(owner))) include(index);
    }
    refHoverValid_ = false;
    setFocus();
    update();
    return {};
}

void CadViewport::setPickBodies(const QVector<int> &bodies) {
    if (pickBodies_ == bodies) return;
    pickBodies_ = bodies;
    update();
}

void CadViewport::cancelReferencePick() {
    if (!refPicking_) return;
    refPicking_ = false;
    refPickOwner_ = -1;
    refPickContextBodies_.clear();
    refHoverValid_ = false;
    update();
}

bool CadViewport::referencePicking() const { return refPicking_; }

void CadViewport::setReferencePickCallback(std::function<void(bool, GeometryRef)> callback) { refPickFinished_ = std::move(callback); }

void CadViewport::setTrimPartPickCallback(int firstBody, const QVector<SheetPiece> &firstPieces,
                                 int secondBody, const QVector<SheetPiece> &secondPieces,
                                 std::function<void(int, int, EdgePoint)> callback) {
    trimPartPickBodies_.clear();
    trimPartPickDisplays_.clear();
    trimPartPickSides_.clear();
    trimPartPickIndices_.clear();
    const auto append = [&](int body, const QVector<SheetPiece> &pieces, int side) {
        if (body < 0) return;
        for (int k = 0; k < pieces.size(); ++k) {
            trimPartPickBodies_.append(body);
            trimPartPickDisplays_.append(pieces.at(k).display);
            trimPartPickSides_.append(side);
            trimPartPickIndices_.append(k);
        }
    };
    if (callback) {
        append(firstBody, firstPieces, 0);
        append(secondBody, secondPieces, 1);
    }
    trimPartHover_ = -1;
    hover_ = {};
    unsetCursor();
    trimPartPickFinished_ = std::move(callback);
    update();
}

bool CadViewport::selectedFaceReference(int &body, EdgePoint &reference) const {
    if (selectedFace_.body < 0 || selectedFace_.body >= extrusions_.size() || selectedFace_.hit.face < 0) return false;
    const ForgeCad::ForgeBody &shape = extrusions_.at(selectedFace_.body).forgeBody;
    const ForgeCad::Kernel::FaceId face(selectedFace_.hit.face);
    if (!shape || !shape->contains(face)) return false;
    const ForgeCad::Kernel::Face &data = shape->face(face);
    ForgeCad::Kernel::Vec3 point;
    if (!data.loops.empty() && shape->loop(data.loops.front()).first.valid())
        point = shape->finPoint(shape->loop(data.loops.front()).first, 0.5);
    body = selectedFace_.body;
    reference = ForgeCad::faceReference(*shape, face, point);
    return true;
}

void CadViewport::updateHover(const QPoint &currentPosition) {
    if ((interactionLocked_ || documentReadOnly_) && !refPicking_ && !edgePicking_ && !trimPartPickFinished_
        && !sketchPickCallback_ && !sketchEntityPickCallback_ && !sketchOffsetSelectionCallback_) { hover_ = {}; return; }
    if (QApplication::mouseButtons() & Qt::LeftButton) return;
    if (trimPartPickFinished_) {
        QVector3D origin, direction;
        viewRay(currentPosition, origin, direction);
        int nearest = -1;
        float best = std::numeric_limits<float>::max();
        for (int k = 0; k < trimPartPickDisplays_.size(); ++k) {
            float distance = 0.0f;
            if (meshRayHit(trimPartPickDisplays_.at(k), origin, direction, distance) && distance < best)
                best = distance, nearest = k;
        }
        if (nearest != trimPartHover_) {
            trimPartHover_ = nearest;
            if (nearest >= 0) {
                setCursor(Qt::PointingHandCursor);
                showStatus(QStringLiteral("%1 corpo, parte %2: clic per mantenerla")
                               .arg(trimPartPickSides_.at(nearest) == 0 ? QStringLiteral("Primo") : QStringLiteral("Secondo"))
                               .arg(trimPartPickIndices_.at(nearest) + 1));
            } else unsetCursor();
            update();
        }
        hover_ = {};
        return;
    }
    if (refPicking_) {
        GeometryRef ref;
        FaceHit face;
        int faceBody = -1;
        const bool valid = pickReference(currentPosition, ref, &face, &faceBody);
        refHoverValid_ = valid;
        refHover_ = ref;
        refHoverFace_ = face;
        refHoverFaceBody_ = valid && ref.kind == 5 ? faceBody : -1;
        // Il filtro dei riferimenti vale anche per l'evidenziazione:
        // il picking generico della scena includerebbe di nuovo i piani.
        hover_ = SceneSelection();
        update();
        return;
    }
    if (edgePicking_) {
        int edgeBody = -1;
        const int edge = pickEdge(currentPosition, &edgeBody);
        // Senza uno spigolo vicino si evidenziano i bordi della faccia sotto il puntatore.
        QVector<int> faceEdges;
        FaceHit face;
        int hoverBody = edgeBody;
        if (edge < 0) {
            hoverBody = edgePickFaceBody(currentPosition);
            if (pickBodyFace(hoverBody, currentPosition, face)) faceEdges = faceDisplayEdges(hoverBody, face);
        }
        if (edge != hoverEdge_ || faceEdges != hoverFaceEdges_ || hoverBody != hoverEdgeBody_) {
            hoverEdge_ = edge;
            hoverFaceEdges_ = faceEdges;
            hoverEdgeBody_ = hoverBody;
            update();
        }
    }
    const SceneSelection hover = pickSceneObject(currentPosition, false);
    if (hover != hover_) {
        hover_ = hover;
        update();
    }
}

void CadViewport::warmPickCaches() {
    for (auto it = warmedBodies_.begin(); it != warmedBodies_.end();)
        it = it->expired() ? warmedBodies_.erase(it) : std::next(it);
    // I corpi visibili e la catena dei loro operandi: il clic cerca la
    // feature proprietaria della faccia anche sugli stadi precedenti
    // (faceOwnerFeature), che proiettano sulle loro superfici.
    QSet<int> chain;
    QVector<int> pending;
    for (int index = 0; index < extrusions_.size(); ++index)
        if (extrusions_.at(index).visible) pending.append(index), chain.insert(index);
    while (!pending.isEmpty()) {
        const int index = pending.takeLast();
        for (int operand : ForgeCad::bodyOperands(extrusions_.at(index)))
            if (operand >= 0 && operand < index && !chain.contains(operand)) chain.insert(operand), pending.append(operand);
    }
    for (int index : chain) {
        const ExtrusionObject &body = extrusions_.at(index);
        if (body.suppressed || !body.forgeBody || !ForgeCad::isShapeBody(body)) continue;
        const ForgeCad::ForgeBody forgeBody = body.forgeBody;
        const bool warmed = std::any_of(warmedBodies_.begin(), warmedBodies_.end(), [&](const std::weak_ptr<const ForgeCad::Kernel::Body> &known) {
            return !known.owner_before(forgeBody) && !forgeBody.owner_before(known);
        });
        if (warmed) continue;
        warmedBodies_.push_back(forgeBody);
        QThreadPool::globalInstance()->start([forgeBody] {
            try {
                ForgeCad::forgeWarmPickCaches(*forgeBody);
            } catch (const std::exception &) {
            }
        });
    }
}

SketchElementSelection CadViewport::findSketchElement(const QPointF &point) const {
    SketchElementSelection result;
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return result;
    double nearestDistance = pickTolerance();
    const SketchObject &sketch = sketches_.at(activeSketch_);
    const auto considerSegment = [&](const QPointF &first, const QPointF &second, int kind, int index) {
        const double distance = distanceToSegment(point, first, second);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            result = {kind, index};
        }
    };
    for (int index = 0; index < sketch.segments.size(); ++index) {
        const auto &segment = sketch.segments.at(index);
        considerSegment(segment.first, segment.second, 0, index);
    }
    for (int index = 0; index < sketch.curves.size(); ++index) {
        const auto &samples = sketch.curves.at(index).samples;
        if(sketch.curves.at(index).tool==DrawingTool::Point && samples.size()==1)
            considerSegment(samples[0],samples[0],1,index);
        for (int sample = 1; sample < samples.size(); ++sample) {
            considerSegment(samples.at(sample - 1), samples.at(sample), 1, index);
        }
    }
    return result;
}

void CadViewport::selectSketchElement(const QPointF &point, bool additive) {
    const SketchElementSelection hit = findSketchElement(point);
    if (!additive) selectedConstraints_.clear();
    if (hit.kind < 0) {
        if (!additive) {
            sketchSelections_.clear();
            selectedPoints_.clear();
        }
        selectionChanged();
        update();
        return;
    }
    if (!additive) {
        sketchSelections_.clear();
        selectedPoints_.clear();
    }
    for (int index = 0; index < sketchSelections_.size(); ++index) {
        if (sketchSelections_.at(index) == hit) {
            if (additive) sketchSelections_.removeAt(index);
            selectionChanged();
            update();
            return;
        }
    }
    sketchSelections_.append(hit);
    selectionChanged();
    update();
}

int CadViewport::segmentMidpointAt(const QPointF &point) const {
    const SketchObject *sketch = activeSketchObject();
    if (!sketch) return -1;
    double best = pickTolerance();
    int result = -1;
    for (int i = 0; i < sketch->segments.size(); ++i) {
        const auto &segment = sketch->segments.at(i);
        if (pointDistance(segment.first, segment.second) <= ForgeCad::kSketchConnectionTolerance) continue;
        const double distance = pointDistance(point, 0.5 * (segment.first + segment.second));
        if (distance < best) { best = distance; result = i; }
    }
    return result;
}

bool CadViewport::selectPointWithControl(const QPointF &point) {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return false;
    double nearestDistance = pickTolerance();
    SelectedPoint selected{-1, -1, -1};
    const auto &sketch = sketches_.at(activeSketch_);
    for (int segment = 0; segment < sketch.segments.size(); ++segment) {
        const QPointF endpoints[] = {sketch.segments.at(segment).first, sketch.segments.at(segment).second};
        for (int endpoint = 0; endpoint < 2; ++endpoint) {
            const double distance = pointDistance(point, endpoints[endpoint]);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                selected = {0, segment, endpoint};
            }
        }
    }
    for (int curve = 0; curve < sketch.curves.size(); ++curve) {
        for (int control = 0; control < sketch.curves.at(curve).controlPoints.size(); ++control) {
            // Curva derivata: i poli sono interni alla sua definizione,
            // per i vincoli valgono solo gli estremi.
            if (!isPickableCurvePoint(sketch.curves.at(curve), control)) continue;
            const double distance = pointDistance(point, sketch.curves.at(curve).controlPoints.at(control));
            if (distance < nearestDistance) {
                nearestDistance = distance;
                selected = {1, curve, control};
            }
        }
        // Le maniglie delle spline (estremi entrante e uscente di ogni punto).
        const CurveObject &object = sketch.curves.at(curve);
        if (object.tool == DrawingTool::Spline && object.tangentHandles.size() == object.controlPoints.size())
            for (int control = 0; control < object.tangentHandles.size(); ++control)
                for (int side = 0; side < 2; ++side) {
                    if (!visibleSplineHandle(object,control,side)) continue;
                    const QPointF handle = side == 0 ? object.tangentHandles.at(control).first : object.tangentHandles.at(control).second;
                    const double distance = pointDistance(point, handle);
                    if (distance < nearestDistance) {
                        nearestDistance = distance;
                        selected = {1, curve, handlePoint(control, side)};
                    }
                }
    }
    // L'origine del piano si sceglie come un punto (per i vincoli).
    if (pointLength(point) < nearestDistance) selected = {2, 0, -1};
    const int midpoint = segmentMidpointAt(point);
    if (midpoint >= 0 && pointDistance(point, 0.5 * (sketch.segments.at(midpoint).first
            + sketch.segments.at(midpoint).second)) < nearestDistance && selected.kind != 2) {
        const SketchElementSelection entity{0, midpoint};
        const int existing = sketchSelections_.indexOf(entity);
        if (existing >= 0) sketchSelections_.removeAt(existing);
        else sketchSelections_.append(entity);
        selectionChanged();
        update();
        return true;
    }
    if (selected.kind < 0) return false;
    // Ctrl+clic sceglie o toglie il punto (per i vincoli: la finestra Vincoli li propone).
    for (int k = 0; k < selectedPoints_.size(); ++k)
        if (selectedPoints_.at(k).kind == selected.kind && selectedPoints_.at(k).element == selected.element
            && selectedPoints_.at(k).point == selected.point) {
            selectedPoints_.removeAt(k);
            selectionChanged();
            update();
            return true;
        }
    for (int k = selectedPoints_.size() - 1; k >= 0; --k)
        if (!isValidSelectedPoint(selectedPoints_.at(k))) selectedPoints_.removeAt(k);
    if (selectedPoints_.size() + sketchSelections_.size() >= 3) selectedPoints_.clear();  // fino a tre (simmetria: due punti e l'asse)
    selectedPoints_.append(selected);
    selectionChanged();
    update();
    return true;
}

bool CadViewport::isValidSelectedPoint(const SelectedPoint &point) const {
    if (point.kind == 2) return point.element == 0;
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size() || point.element < 0) return false;
    const SketchObject &sketch = sketches_.at(activeSketch_);
    if (point.kind == 0) return point.element < sketch.segments.size() && (point.point == 0 || point.point == 1);
    if (point.kind != 1 || point.element >= sketch.curves.size() || point.point < 0) return false;
    const CurveObject &curve = sketch.curves.at(point.element);
    // Maniglia di una spline (per i vincoli).
    if (isHandlePoint(point.point))
        return curve.tool == DrawingTool::Spline && curve.tangentHandles.size() == curve.controlPoints.size()
            && (point.point - kHandlePoint) / 2 < curve.tangentHandles.size();
    return point.point < curve.controlPoints.size() && isPickableCurvePoint(curve, point.point);
}

bool CadViewport::isPickableCurvePoint(const CurveObject &curve, int control) {
    if(curve.tool==DrawingTool::Circle)return control==0;
    return curve.tool != DrawingTool::Converted || control == 0 || control == int(curve.controlPoints.size()) - 1;
}

QPointF CadViewport::selectedPointPosition(const SelectedPoint &point) const {
    if (point.kind == 2) return QPointF(0.0, 0.0);
    if (point.kind == 0) {
        const auto &segment = sketches_.at(activeSketch_).segments.at(point.element);
        return point.point == 0 ? segment.first : segment.second;
    }
    const CurveObject &curve = sketches_.at(activeSketch_).curves.at(point.element);
    if (isHandlePoint(point.point)) {
        const int h = point.point - kHandlePoint;
        return h % 2 == 0 ? curve.tangentHandles.at(h / 2).first : curve.tangentHandles.at(h / 2).second;
    }
    return curve.controlPoints.at(point.point);
}

void CadViewport::setSelectedPointPosition(const SelectedPoint &point, const QPointF &position) {
    if (point.kind == 0) {
        auto &segment = sketches_[activeSketch_].segments[point.element];
        if (point.point == 0) segment.first = position;
        else segment.second = position;
    } else {
        CurveObject &curve = sketches_[activeSketch_].curves[point.element];
        const QPointF delta = position - curve.controlPoints.at(point.point);
        curve.controlPoints[point.point] = position;
        if (curve.tool == DrawingTool::Spline && curve.tangentHandles.size() == curve.controlPoints.size()) {
            curve.tangentHandles[point.point].first += delta;
            curve.tangentHandles[point.point].second += delta;
        }
    }
}

QVector<CadViewport::TopologyLabelItem> CadViewport::topologyLabelItems() const {
    QVector<TopologyLabelItem> result;
    if (!topologyIdsVisible_ || sketchMode_) return result;
    int onlyBody = -1;
    if (selection_.kind == SceneObjectKind::Extrusion) onlyBody = selection_.index;
    else if (hover_.kind == SceneObjectKind::Extrusion) onlyBody = hover_.index;
    const QFont font = uiFont(8, QFont::DemiBold);
    const QFontMetrics metrics(font);
    // Raggio attraverso il punto 3D, senza arrotondarlo al pixel: sui bordi
    // di sagoma l'arrotondamento potrebbe passare fuori dal corpo.
    const QVector3D towardScene = sceneModelViewMatrix().inverted().mapVector(QVector3D(0, 0, -1)).normalized();
    const float rayReach = float(2.0 * sceneDepth());
    const double visibilityTolerance = qMax(1e-6, 1e-5 * double((sceneMax_ - sceneMin_).length()));
    const auto append = [&](const QVector3D &point, const QPointF &offset, const QString &text,
                            int body, int subshape, bool edge) {
        const QPointF anchor = projectWorldPoint(point);
        if (!QRectF(rect()).contains(anchor) || sectionRemoves(point)) return;
        QVector3D origin = point - rayReach * towardScene;
        double skipped = 0.0;
        if (!sectionRay(origin, towardScene, skipped)) return;
        // Anche un altro corpo visibile puo' coprire l'etichetta, compresi
        // quelli esclusi dal filtro delle etichette per selezione/hover.
        for (int index=0;index<extrusions_.size();++index)
            if (const auto &occluder=extrusions_.at(index);isBodyDrawn(index) && ForgeCad::isShapeBody(occluder)
                && edgeOccludedByMesh(occluder.display, origin, towardScene, point, visibilityTolerance)) return;
        QRectF box = metrics.boundingRect(QRect(0, 0, 10000, 10000), Qt::AlignCenter, text).adjusted(-3.0, -2.0, 3.0, 2.0);
        box.moveCenter(anchor + offset);
        // Le due righe restano leggibili anche lungo il bordo del viewport.
        box.moveLeft(qBound(4.0, box.left(), qMax(4.0, width() - box.width() - 4.0)));
        box.moveTop(qBound(4.0, box.top(), qMax(4.0, height() - box.height() - 4.0)));
        result.append({box, anchor, text, body, subshape, edge});
    };
    for (int bodyIndex = 0; bodyIndex < extrusions_.size(); ++bodyIndex) {
        const ExtrusionObject &body = extrusions_.at(bodyIndex);
        if (!isBodyDrawn(bodyIndex) || !ForgeCad::isShapeBody(body) || (onlyBody >= 0 && bodyIndex != onlyBody)) continue;
        const BodyDisplay &display = body.display;
        const auto names = body.forgeBody ? ForgeCad::elementNamesOf(*body.forgeBody) : nullptr;
        const auto labelText = [&](bool edge, int index) {
            const QString id = QStringLiteral("B%1:%2%3").arg(bodyIndex)
                .arg(edge ? QChar(u'E') : QChar(u'F')).arg(index);
            const QString name = names ? QString::fromStdString(names->primary(
                edge ? ForgeCad::Kernel::EntityKind::Edge : ForgeCad::Kernel::EntityKind::Face, index)) : QString();
            return name.isEmpty() ? id : id + QChar(u'\n') + name;
        };
        for (int k = 0; k < display.faceIds.size() && k < display.faceLabelPoints.size(); ++k)
            append(display.faceLabelPoints.at(k), QPointF(22.0, -18.0),
                   labelText(false, display.faceIds.at(k)),
                   bodyIndex, display.faceIds.at(k), false);
        for (int k = 0; k < display.edges.size(); ++k) {
            const QVector<QVector3D> &edge = display.edges.at(k);
            if (edge.isEmpty()) continue;
            const int edgeId = display.edgeIds.value(k, k);
            append(polylineMidpoint(edge), QPointF(0.0, 22.0),
                   labelText(true, edgeId), bodyIndex, edgeId, true);
        }
    }
    return result;
}

bool CadViewport::pickTopologyLabel(const QPoint &position, TopologyLabelItem &hit) const {
    const QVector<TopologyLabelItem> labels = topologyLabelItems();
    for (int k = labels.size() - 1; k >= 0; --k)
        if (labels.at(k).box.adjusted(-2.0, -2.0, 2.0, 2.0).contains(QPointF(position))) {
            hit = labels.at(k);
            return true;
        }
    return false;
}

QVector<QPointF> CadViewport::snapCandidates(const SketchObject &sketch) const {
    QVector<QPointF> points;
    for (const CurveObject &curve : sketch.curves) {
        if (curve.tool!=DrawingTool::Circle&&curve.samples.size() >= 2) {
            points.append(curve.samples.first());
            points.append(curve.samples.last());
        }
        if ((curve.tool == DrawingTool::Circle || curve.tool == DrawingTool::Arc
             || curve.tool == DrawingTool::Polygon) && !curve.controlPoints.isEmpty())
            points.append(curve.controlPoints.first());
        if (curve.tool == DrawingTool::Spline || curve.tool == DrawingTool::Nurbs || curve.tool == DrawingTool::Point)
            points += curve.controlPoints;
        if (curve.tool == DrawingTool::Ellipse && curve.controlPoints.size() >= 3) {
            const QPointF c = curve.controlPoints.at(0);
            points << c << curve.controlPoints.at(1) << curve.controlPoints.at(2) << 2.0 * c - curve.controlPoints.at(1)
                   << 2.0 * c - curve.controlPoints.at(2);
        }
    }
    for (const CurveObject &curve : sketch.curves) {
        if (curve.tool != DrawingTool::Polygon) continue;
        for (const ForgeCad::Kernel::ProfileSegment &side : ForgeCad::curveGeometry(curve)) {
            const ForgeCad::Kernel::Vec2 start = side.start();
            points.append(QPointF(start.x(), start.y()));
        }
    }
    return points;
}

QPointF CadViewport::snapPoint(const QPointF &point, bool snapToGrid ) {
    QVector<SketchSegment> segments;
    QVector<QPointF> points;
    SketchObject combined;
    lastSnapExternal_ = false;
    if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) {
        combined = sketches_.at(activeSketch_);
        if (geometrySnap_) {
            const auto &cache = sketchReferenceCache();
            combined.segments += cache.segments; combined.curves += cache.curves;
        }
        segments = combined.segments;
        points = snapCandidates(combined);
        if (drawingTool_ == DrawingTool::Spline && curveControlPoints_.size() > 1)
            points.append(curveControlPoints_.first());
        if (originSnap_) points.append(QPointF(0.0, 0.0));  // origine del piano (dove passano gli assi)
    }
    const double tolerance = pickTolerance(10.0);
    ForgeCad::SnapResult result = ForgeCad::snapSegments(point, segments, points, geometrySnap_, snapToGrid && gridSnap_, snapSpacing_, tolerance);
    lastSnapNote_.clear();
    lastSnapCurve_ = -1;
    // Curve (cerchi, archi, poligoni, spline, NURBS): i quadranti di cerchi
    // e archi come punti; poi il punto esatto piu' vicino sulla curva, se
    // e' piu' vicino del punto trovato su un segmento. I punti vincono.
    if (geometrySnap_ && activeSketch_ >= 0 && activeSketch_ < sketches_.size()
        && (result.kind == SnapKind::None || result.kind == SnapKind::Nearest)) {
        const SketchObject &sketch = combined;
        QPointF quadrant;
        if (nearestQuadrant(sketch, point, tolerance, quadrant)) {
            result = {SnapKind::Endpoint, quadrant};
            lastSnapNote_ = QStringLiteral("◆ Quadrante");
        } else {
            const double segmentDistance = result.kind == SnapKind::Nearest ? pointDistance(point, result.point) : tolerance;
            QPointF onCurve;
            const int curve = nearestCurvePoint(sketch, point, segmentDistance, onCurve);
            if (curve >= 0) {
                result = {SnapKind::Nearest, onCurve};
                lastSnapCurve_ = curve;
                lastSnapNote_ = QStringLiteral("∈ Su curva");
            }
        }
    }
    if (result.kind == SnapKind::Endpoint && originSnap_ && result.point == QPointF(0.0, 0.0)) lastSnapNote_ = QStringLiteral("✚ Origine");
    if (geometrySnap_ && activeSketchObject() && result.kind != SnapKind::None) {
        const auto &local = *activeSketchObject();
        // Un punto della cache resta esatto durante le inferenze di direzione.
        const auto &cache = sketchReferenceCache();
        for (const auto &segment : cache.segments)
            if (distanceToSegment(result.point, segment.first, segment.second) <= 1e-9) lastSnapExternal_ = true;
        if (lastSnapCurve_ >= local.curves.size()) { lastSnapCurve_ = -1; lastSnapExternal_ = true; }
        if (lastSnapExternal_ && lastSnapNote_.isEmpty()) lastSnapNote_ = QStringLiteral("Riferimento associativo");
    }
    lastSnapKind_ = result.kind;
    lastSnapPoint_ = result.point;
    return result.point;
}

bool CadViewport::nearestQuadrant(const SketchObject &sketch, const QPointF &point, double tolerance, QPointF &quadrant) const {
    double best = tolerance;
    bool found = false;
    for (const CurveObject &curve : sketch.curves) {
        for (const auto &candidate : ForgeCad::curveQuadrants(curve)) {
            const double d = pointDistance(point, candidate.second);
            if (d < best) {
                best = d;
                quadrant = candidate.second;
                found = true;
            }
        }
    }
    return found;
}

bool CadViewport::onArc(const CurveObject &arc, double angle) {
    if (arc.controlPoints.size() < 3) return false;
    const QPointF c = arc.controlPoints.at(0), a = arc.controlPoints.at(1), b = arc.controlPoints.at(2);
    const double start = std::atan2(a.y() - c.y(), a.x() - c.x());
    double sweep = std::atan2(b.y() - c.y(), b.x() - c.x()) - start;
    while (sweep <= 0.0) sweep += 2.0 * M_PI;
    double offset = std::fmod(angle - start, 2.0 * M_PI);
    if (offset < 0.0) offset += 2.0 * M_PI;
    return offset <= sweep + 1e-12;
}

int CadViewport::nearestCurvePoint(const SketchObject &sketch, const QPointF &point, double tolerance, QPointF &result) const {
    double best = tolerance;
    int found = -1;
    for (int index = 0; index < sketch.curves.size(); ++index) {
        const CurveObject &curve = sketch.curves.at(index);
        if ((curve.tool == DrawingTool::Circle || curve.tool == DrawingTool::Arc) && curve.controlPoints.size() >= 2) {
            const QPointF center = curve.controlPoints.at(0);
            const double r = pointDistance(center, curve.controlPoints.at(1)), d = pointDistance(center, point);
            if (r <= 0.0 || d <= 0.0) continue;
            if (curve.tool == DrawingTool::Arc && !onArc(curve, std::atan2(point.y() - center.y(), point.x() - center.x()))) continue;
            if (std::abs(d - r) < best) {
                best = std::abs(d - r);
                result = center + (point - center) * (r / d);
                found = index;
            }
            continue;
        }
        try {
            for (const ForgeCad::Kernel::ProfileSegment &piece : ForgeCad::curveGeometry(curve)) {
                const ForgeCad::Kernel::CurveProjection<2> projection =
                    ForgeCad::Kernel::projectPoint(*piece.curve, ForgeCad::Kernel::Vec2(point.x(), point.y()), piece.range);
                if (projection.distance < best) {
                    best = projection.distance;
                    result = QPointF(projection.point.x(), projection.point.y());
                    found = index;
                }
            }
        } catch (const std::exception &) {
        }
    }
    return found;
}

bool CadViewport::meshRayHit(const BodyDisplay &display, const QVector3D &origin, const QVector3D &direction, float &distance) {
    const QVector3D d = direction.normalized();
    bool found = false;
    float best = std::numeric_limits<float>::max();
    const QVector<QVector3D> &v = display.vertices;
    for (int k = 0; k + 2 < v.size(); k += 3) {
        // Moller-Trumbore, entrambe le facce del triangolo.
        const QVector3D e1 = v[k + 1] - v[k], e2 = v[k + 2] - v[k];
        const QVector3D p = QVector3D::crossProduct(d, e2);
        const float det = QVector3D::dotProduct(e1, p);
        if (std::fabs(det) < 1e-20f) continue;
        const float inverse = 1.0f / det;
        const QVector3D s = origin - v[k];
        const float u = QVector3D::dotProduct(s, p) * inverse;
        if (u < 0.0f || u > 1.0f) continue;
        const QVector3D q = QVector3D::crossProduct(s, e1);
        const float w = QVector3D::dotProduct(d, q) * inverse;
        if (w < 0.0f || u + w > 1.0f) continue;
        const float t = QVector3D::dotProduct(e2, q) * inverse;
        if (t >= 0.0f && t < best) {
            best = t;
            found = true;
        }
    }
    if (found) distance = best;
    return found;
}

int CadViewport::gpuPickBody(const QPoint &position) const {
    if (!context() || !context()->isValid() || section_.enabled || !preview_.key.isEmpty()) return -2;
    CadViewport *self = const_cast<CadViewport *>(this);
    self->makeCurrent();
    const qreal ratio = devicePixelRatioF();
    const QSize pixels(qMax(1, qRound(width() * ratio)), qMax(1, qRound(height() * ratio)));
    const int index = gpuPicker_.pick(position, pixels, ratio, renderedFrameSerial_, [this] {
        displayCache_.setMatrices(sceneProjectionMatrix(), sceneModelViewMatrix());
        for (int index = 0; index < extrusions_.size(); ++index) {
            const ExtrusionObject &body = extrusions_.at(index);
            if (!isBodyDrawn(index) || !ForgeCad::isShapeBody(body) || body.display.vertices.isEmpty()) continue;
            if (!pickBodies_.isEmpty() && !pickBodies_.contains(index)) continue;
            const unsigned id = unsigned(index + 1);
            const QVector4D identifier(float(id & 0xFFu) / 255.0f, float((id >> 8) & 0xFFu) / 255.0f,
                                       float((id >> 16) & 0xFFu) / 255.0f, 1.0f);
            if (body.display.instancedBase && !body.display.instanceTransforms.isEmpty())
                displayCache_.pickingInstancedFaces(*body.display.instancedBase, body.display.instanceTransforms, identifier);
            else displayCache_.pickingFaces(body.display, identifier);
        }
    });
    self->doneCurrent();
    return index >= 0 && index < extrusions_.size() ? index : index == -2 ? -2 : -1;
}

SceneSelection CadViewport::pickSceneObject(const QPoint &position, bool exact ) const {
    SceneSelection result;
    float nearestDistance = 7.0f;
    const auto considerSegment = [&](const QPointF &first, const QPointF &second, int sketchIndex) {
        const QVector2D line(second - first);
        const float lengthSquared = line.lengthSquared();
        const QVector2D offset(QPointF(position) - first);
        const float parameter = lengthSquared > 0.0f
            ? qBound(0.0f, QVector2D::dotProduct(offset, line) / lengthSquared, 1.0f) : 0.0f;
        const float distance = (offset - line * parameter).length();
        if (distance < nearestDistance) {
            nearestDistance = distance;
            result = {SceneObjectKind::Sketch, sketchIndex, -1};
        }
    };
    for (int sketchIndex = 0; sketchIndex < sketches_.size(); ++sketchIndex) {
        const SketchObject &sketch = sketches_.at(sketchIndex);
        if (!isSketchDrawn(sketchIndex)) continue;
        for (const auto &segment : sketch.segments) {
            considerSegment(projectWorldPoint(mapSketchPoint(segment.first, sketch)),
                            projectWorldPoint(mapSketchPoint(segment.second, sketch)), sketchIndex);
        }
        for (const CurveObject &curve : sketch.curves) {
            for (int sample = 1; sample < curve.samples.size(); ++sample) {
                considerSegment(projectWorldPoint(mapSketchPoint(curve.samples.at(sample - 1), sketch)),
                                projectWorldPoint(mapSketchPoint(curve.samples.at(sample), sketch)),
                                sketchIndex);
            }
        }
    }
    // Curve (eliche): vicinanza alla polilinea in pixel, come gli schizzi.
    for (int index = 0; index < extrusions_.size(); ++index) {
        const ExtrusionObject &body = extrusions_.at(index);
        if (!isBodyDrawn(index) || !ForgeCad::isCurveBody(body)) continue;
        for (const QVector<QVector3D> &polyline : body.display.edges)
            for (int k = 1; k < polyline.size(); ++k) {
                const float before = nearestDistance;
                considerSegment(projectWorldPoint(polyline.at(k - 1)), projectWorldPoint(polyline.at(k)), -1);
                if (nearestDistance < before) result = {SceneObjectKind::Extrusion, index, -1};
            }
    }
    if (result.kind != SceneObjectKind::None) return result;
    // Bordo di un piano di costruzione (come le linee degli schizzi).
    const int datumBorder = pickDatumPlane(position, true);
    if (datumBorder >= 0) return {SceneObjectKind::Extrusion, datumBorder, -1};

    // Selezione sulla forma esatta: intersezione del raggio con le facce B-rep.
    QVector3D origin, direction;
    viewRay(position, origin, direction);
    double nearest = std::numeric_limits<double>::max();
    // L'interno di un piano di costruzione, se sta davanti ai corpi.
    const int datum = pickDatumPlane(position, false, &nearest);
    if (datum >= 0) result = {SceneObjectKind::Extrusion, datum, -1};
    if (!exact && datum < 0) {
        const int body = gpuPickBody(position);
        if (body >= 0) return {SceneObjectKind::Extrusion, body, -1};
        if (body != -2) {
            const int plane = pickReferencePlane(position);
            return plane >= 0 ? SceneSelection{SceneObjectKind::Plane, plane, -1} : SceneSelection{};
        }
    }
    double skipped = 0.0;
    const bool reaches = sectionRay(origin, direction, skipped);
    for (int index = 0; index < extrusions_.size() && reaches; ++index) {
        const ExtrusionObject &extrusion = extrusions_.at(index);
        if (!isBodyDrawn(index) || !ForgeCad::isShapeBody(extrusion)) continue;
        FaceHit face;
        bool hit = false;
        double distance = 0.0;
        if (exact) {
            hit = cachedRayFace(index, origin, direction, face);
            distance = hit ? face.distance : 0.0;
        } else {
            float t = 0.0f;
            hit = meshRayHit(extrusion.display, origin, direction, t);
            distance = t;
        }
        if (hit && sectionRemoves(origin + float(distance) * direction.normalized())) hit = false;
        distance += skipped;
        if (hit && distance < nearest) {
            nearest = distance;
            result = {SceneObjectKind::Extrusion, index, -1};
        }
    }
    if (result.kind != SceneObjectKind::None) return result;

    const int plane = pickReferencePlane(position);
    if (plane >= 0) result = {SceneObjectKind::Plane, plane, -1};
    return result;
}

bool CadViewport::pickVisibleSketchEntity(const QPoint &position, int &sketchIndex, int &kind, int &entityIndex) const {
    double best = 7.0;
    bool found = false;
    const auto consider = [&](const SketchObject &sketch, int s, int k, int e, const QVector<QPointF> &points) {
        for (int i = 1; i < points.size(); ++i) {
            const double distance = distanceToSegment(QPointF(position),
                projectWorldPoint(mapSketchPoint(points.at(i - 1), sketch)),
                projectWorldPoint(mapSketchPoint(points.at(i), sketch)));
            if (distance >= best) continue;
            best = distance; sketchIndex = s; kind = k; entityIndex = e; found = true;
        }
    };
    for (int s = 0; s < sketches_.size(); ++s) {
        const SketchObject &sketch = sketches_.at(s);
        if (!isSketchDrawn(s)) continue;
        for (int e = 0; e < sketch.segments.size(); ++e) {
            if (sketch.isConstructionSegment(e)) continue;
            const SketchSegment &segment = sketch.segments.at(e);
            consider(sketch, s, 0, e, {segment.first, segment.second});
        }
        for (int e = 0; e < sketch.curves.size(); ++e)
            if (!sketch.curves.at(e).construction) consider(sketch, s, 1, e, sketch.curves.at(e).samples);
    }
    return found;
}

int CadViewport::pickReferencePlane(const QPoint &position) const {
    QVector3D origin, direction;
    viewRay(position, origin, direction);
    const QVector3D normals[] = {QVector3D(0, 0, 1), QVector3D(0, 1, 0), QVector3D(1, 0, 0)};
    int selected = -1;
    float nearestDistance = 1.0e9f;
    for (int plane = 0; plane < 3; ++plane) {
        if (!isPlaneShown(plane)) continue;
        const float denominator = QVector3D::dotProduct(normals[plane], direction);
        if (qAbs(denominator) < 0.001f) continue;
        const float distance = -QVector3D::dotProduct(normals[plane], origin) / denominator;
        const QVector3D hit = origin + direction * distance;
        const QSizeF size = referencePlaneExtents(plane) * 1.0003;
        const float u = plane == 2 ? hit.y() : hit.x();
        const float v = plane == 0 ? hit.y() : hit.z();
        if (distance > 0.0f && qAbs(u) <= size.width() && qAbs(v) <= size.height()
            && distance < nearestDistance) {
            nearestDistance = distance;
            selected = plane;
        }
    }
    return selected;
}

bool CadViewport::isPlaneShown(int plane) const {
    return referencePlanesVisible_ && plane >= 0 && plane < 3 && planeVisible_[plane];
}

bool CadViewport::resizablePlane(SceneSelection &target, QVector<QVector3D> &corners, QVector3D &center, double &half) const {
    if (sketchMode_ || refPicking_ || edgePicking_ || interactionLocked_) return false;
    target = selection_;
    if (target.kind == SceneObjectKind::Plane && isPlaneShown(target.index)) {
        corners = planeCorners(target.index);
        center = {};
        half = referencePlaneHalf(target.index);
        return true;
    }
    if (target.kind == SceneObjectKind::Extrusion && isDatumShown(target.index)) {
        const auto &body = extrusions_.at(target.index);
        corners = datumCorners(body.datumFrame, body.datum.size);
        center = (corners[0] + corners[2]) * 0.5f;
        half = datumHalf(body.datum.size);
        return true;
    }
    return false;
}

int CadViewport::planeResizeHandleAt(const QPoint &position) const {
    SceneSelection target;
    QVector<QVector3D> corners;
    QVector3D center;
    double half;
    if (!resizablePlane(target, corners, center, half)) return -1;
    for (int k = 0; k < corners.size(); ++k)
        if (pointDistance(projectWorldPoint(corners[k]), position) <= 9.0) return k;
    return -1;
}

bool CadViewport::beginPlaneResize(const QPoint &position) {
    const int corner = planeResizeHandleAt(position);
    if (corner < 0) return false;
    QVector<QVector3D> corners;
    QVector3D center;
    double half;
    if (!resizablePlane(planeResizeTarget_, corners, center, half)) return false;
    planeResizeDirection_ = (projectWorldPoint(corners[corner]) - projectWorldPoint(center)) / half;
    if (QPointF::dotProduct(planeResizeDirection_, planeResizeDirection_) < 1e-16) return false;
    planeResizeStart_ = position;
    planeResizeHalf_ = half;
    planeResizeOriginal_ = planeResizeTarget_.kind == SceneObjectKind::Plane
        ? referencePlaneScales_[planeResizeTarget_.index] : extrusions_.at(planeResizeTarget_.index).datum.size;
    planeResizeSnapshot_ = documentState();
    planeResizing_ = true;
    setCursor(Qt::SizeFDiagCursor);
    showStatus(QStringLiteral("Trascina per ridimensionare il piano; Esc annulla."));
    return true;
}

void CadViewport::finishPlaneResize(bool cancel) {
    if (!planeResizing_) return;
    const bool datum = planeResizeTarget_.kind == SceneObjectKind::Extrusion;
    const int index = planeResizeTarget_.index;
    double &value = datum ? extrusions_[index].datum.size : referencePlaneScales_[index];
    const bool changed = value != planeResizeOriginal_;
    if (cancel) {
        if (datum) value = planeResizeOriginal_;
        else for (double &planeScale : referencePlaneScales_) planeScale = planeResizeOriginal_;
    }
    planeResizing_ = false;
    unsetCursor();
    sceneBoundsDirty_ = true;
    if (!cancel && changed) {
        if (datum) { history_.record(planeResizeSnapshot_); documentChanged(); }
        else {
            QSettings settings;
            settings.setValue(QStringLiteral("view/planeScale"), value);
            // Mantiene coerenti anche le chiavi lette dalle versioni precedenti.
            for (int plane = 0; plane < 3; ++plane)
                settings.setValue(QStringLiteral("view/planeScale%1").arg(plane), value);
        }
    }
    planeResizeSnapshot_ = {};
    update();
}

bool CadViewport::isDatumShown(int index) const {
    if (index < 0 || index >= extrusions_.size()) return false;
    const ExtrusionObject &body = extrusions_.at(index);
    return ForgeCad::isDatumBody(body) && isBodyDrawn(index) && body.datumValid && index != datumPreviewIndex_;
}

int CadViewport::pickDatumPlane(const QPoint &position, bool border, double *distance ) const {
    int best = -1;
    double nearest = border ? 7.0 : std::numeric_limits<double>::max();
    QVector3D origin, direction;
    viewRay(position, origin, direction);
    for (int index = 0; index < extrusions_.size(); ++index) {
        if (!isDatumShown(index)) continue;
        const ExtrusionObject &body = extrusions_.at(index);
        const QVector<QVector3D> corners = datumCorners(body.datumFrame, body.datum.size);
        if (border) {
            for (int k = 0; k < 4; ++k) {
                const double d = distanceToSegment(QPointF(position), projectWorldPoint(corners.at(k)), projectWorldPoint(corners.at((k + 1) % 4)));
                if (d < nearest) nearest = d, best = index;
            }
            continue;
        }
        const SketchFrame &f = body.datumFrame;
        const QVector3D n(float(f.normal[0]), float(f.normal[1]), float(f.normal[2]));
        const QVector3D c(float(f.origin[0]), float(f.origin[1]), float(f.origin[2]));
        const float denominator = QVector3D::dotProduct(n, direction);
        if (qAbs(denominator) < 1e-4f) continue;
        const float t = QVector3D::dotProduct(c - origin, n) / denominator;
        if (t <= 0.0f) continue;
        const QVector3D local = origin + t * direction - c;
        const QVector3D x(float(f.xAxis[0]), float(f.xAxis[1]), float(f.xAxis[2]));
        const QVector3D y = QVector3D::crossProduct(n, x);
        const float h = float(datumHalf(body.datum.size));
        if (qAbs(QVector3D::dotProduct(local, x)) <= h && qAbs(QVector3D::dotProduct(local, y)) <= h && t < nearest) nearest = t, best = index;
    }
    if (distance) *distance = nearest;
    return best;
}

bool CadViewport::referenceBodyEligible(int index) const {
    if(sketchMode_ && !isBodyDrawn(index, false))return false;
    const int owner = refPickOwner_ < 0 ? int(extrusions_.size()) : refPickOwner_;
    if (!pickBodies_.isEmpty())
        return pickBodies_.contains(index) && index < owner && index < extrusions_.size() && ForgeCad::isShapeBody(extrusions_.at(index));
    return index >= 0 && index < owner && index < extrusions_.size() && isBodyDrawn(index, false) && ForgeCad::isShapeBody(extrusions_.at(index));
}

bool CadViewport::referenceSketchEligible(int index) const {
    if (index < 0 || index >= sketches_.size() || !isSketchDrawn(index)) return false;
    const int datum = sketches_.at(index).datumPlane;
    return datum < 0 || refPickOwner_ < 0 || datum < refPickOwner_;
}

bool CadViewport::curveBodyEndpoint(int index, int end, QVector3D &point, int entity) const {
    const int owner = refPickOwner_ < 0 ? int(extrusions_.size()) : refPickOwner_;
    if (index < 0 || index >= owner || index >= extrusions_.size()) return false;
    const ExtrusionObject &body = extrusions_.at(index);
    if (!isBodyDrawn(index) || !ForgeCad::isCurveBody(body) || !body.curve) return false;
    const auto curve=body.curves.isEmpty()?body.curve:body.curves.value(entity);
    if(!curve)return false;
    const ForgeCad::Kernel::Interval domain = curve->domain();
    const ForgeCad::Kernel::Vec3 p = curve->point(end == 1 ? domain.hi : domain.lo);
    point = QVector3D(float(p.x()), float(p.y()), float(p.z()));
    return true;
}

bool CadViewport::pickReference(const QPoint &position, GeometryRef &ref, FaceHit *faceHit , int *faceBody ) const {
    const int roles = refPickRoles_;
    const QPointF cursor(position);
    const int owner = refPickOwner_ < 0 ? int(extrusions_.size()) : refPickOwner_;
    if (roles & ForgeCad::DatumRolePoint) {
        double best = 10.0;
        bool found = false;
        const auto consider = [&](const QVector3D &p, const GeometryRef &candidate) {
            const double d = pointDistance(projectWorldPoint(p), cursor);
            if (d < best) best = d, ref = candidate, found = true;
        };
        GeometryRef origin;
        origin.kind = 0;
        consider(QVector3D(), origin);
        for (int s = 0; s < sketches_.size(); ++s) {
            if (!referenceSketchEligible(s)) continue;
            const SketchObject &sketch = sketches_.at(s);
            GeometryRef r;
            r.kind = 6;
            r.index = s;
            for (int i = 0; i < sketch.segments.size(); ++i) {
                r.element = {0, i, 0};
                consider(mapSketchPoint(sketch.segments.at(i).first, sketch), r);
                r.element = {0, i, 1};
                consider(mapSketchPoint(sketch.segments.at(i).second, sketch), r);
            }
            for (int i = 0; i < sketch.curves.size(); ++i)
                for (int k = 0; k < sketch.curves.at(i).controlPoints.size(); ++k) {
                    r.element = {1, i, k};
                    consider(mapSketchPoint(sketch.curves.at(i).controlPoints.at(k), sketch), r);
                }
        }
        for (int b = 0; b < extrusions_.size(); ++b) {
            if (!referenceBodyEligible(b)) continue;
            GeometryRef r;
            r.kind = 3;
            r.index = b;
            r.featureId = extrusions_.at(b).featureId;
            for (const QVector<QVector3D> &polyline : extrusions_.at(b).display.edges)
                for (const QVector3D &p : {polyline.first(), polyline.last()}) {
                    r.point = {p.x(), p.y(), p.z()};
                    if (extrusions_.at(b).forgeBody) {
                        const ForgeCad::Kernel::VertexId vertex = ForgeCad::resolveVertexReference(
                            *extrusions_.at(b).forgeBody, r.point, std::numeric_limits<double>::max());
                        if (vertex.valid()) r.point = ForgeCad::vertexReference(*extrusions_.at(b).forgeBody, vertex);
                    }
                    consider(p, r);
                }
        }
        for (int b = 0; b < extrusions_.size(); ++b)
            for(int entity=0;entity<qMax(1,int(extrusions_[b].curves.size()));++entity)
            for (int end = 0; end < 2; ++end) {
                QVector3D point;
                if (!curveBodyEndpoint(b, end, point,entity)) continue;
                GeometryRef r;
                r.kind = 10;
                r.index = b;
                r.featureId = extrusions_.at(b).featureId;
                r.element = {-1, -1, end};
                r.point = {point.x(), point.y(), point.z()};
                if(extrusions_[b].feature==BodyFeature::SpatialSketch && entity<extrusions_[b].spatialSketch.entities.size()) {
                    r.point.geometry=ForgeCad::kSpatialCurveReference;r.point.subshape=extrusions_[b].spatialSketch.entities[entity].id;
                }
                consider(point, r);
            }
        if (roles & ForgeCad::kMeasureSnapRole) {
            // Misura: anche punti medi e centri degli spigoli. A video il punto
            // medio della polilinea; la misura li calcola sulla curva esatta.
            for (int b = 0; b < extrusions_.size(); ++b) {
                if (!referenceBodyEligible(b) || !extrusions_.at(b).forgeBody) continue;
                const ExtrusionObject &body = extrusions_.at(b);
                for (int e = 0; e < body.display.edges.size(); ++e) {
                    const int edgeId = body.display.edgeIds.value(e, -1);
                    const QVector<QVector3D> &polyline = body.display.edges.at(e);
                    if (edgeId < 0 || polyline.size() < 2) continue;
                    // Il riferimento (contesto topologico) solo per il candidato che vince.
                    const auto offer = [&](const QVector3D &at, int kind) {
                        if (pointDistance(projectWorldPoint(at), cursor) >= best) return;
                        const QVector3D sample = polyline.at(polyline.size() / 2);
                        GeometryRef r;
                        r.kind = kind;
                        r.index = b;
                        r.featureId = body.featureId;
                        r.point = ForgeCad::edgeReference(*body.forgeBody, ForgeCad::Kernel::EdgeId(edgeId),
                                                          ForgeCad::Kernel::Vec3(sample.x(), sample.y(), sample.z()));
                        consider(at, r);
                    };
                    offer(polylineMidpoint(polyline), ForgeCad::kGeometryRefEdgeMidpoint);
                    ForgeCad::Kernel::Vec3 center;
                    if (ForgeCad::edgeCenter(*body.forgeBody, ForgeCad::Kernel::EdgeId(edgeId), center))
                        offer(QVector3D(float(center.x()), float(center.y()), float(center.z())), ForgeCad::kGeometryRefEdgeCenter);
                }
            }
        }
        if (found) return true;
    }
    if (roles & (ForgeCad::DatumRoleLine | ForgeCad::DatumRoleCurve)) {
        double best = 8.0;
        bool found = false;
        const auto considerPolyline = [&](const QVector<QVector3D> &polyline, GeometryRef candidate, bool withPoint) {
            for (int k = 1; k < polyline.size(); ++k) {
                const QPointF a = projectWorldPoint(polyline.at(k - 1)), b = projectWorldPoint(polyline.at(k));
                const double d = distanceToSegment(cursor, a, b);
                if (d >= best) continue;
                best = d;
                if (withPoint) {
                    // Un campione della polilinea (sta sulla curva esatta): il piu' vicino al puntatore.
                    const QVector3D p = pointDistance(a, cursor) <= pointDistance(b, cursor) ? polyline.at(k - 1) : polyline.at(k);
                    candidate.point.x = p.x();
                    candidate.point.y = p.y();
                    candidate.point.z = p.z();
                }
                ref = candidate;
                found = true;
            }
        };
        for (int b = 0; b < extrusions_.size(); ++b) {
            GeometryRef r;
            r.index = b;
            const ExtrusionObject &body = extrusions_.at(b);
            if (referenceBodyEligible(b)) {
                r.kind = 4;
                r.featureId = body.featureId;
                for (int edge = 0; edge < body.display.edges.size(); ++edge) {
                    GeometryRef candidate = r;
                    const QVector<QVector3D> &polyline = body.display.edges.at(edge);
                    const int edgeId = body.display.edgeIds.value(edge, -1);
                    if (edgeId >= 0 && body.forgeBody && !polyline.isEmpty()) {
                        const QVector3D sample = polyline.at(polyline.size() / 2);
                        candidate.point = ForgeCad::edgeReference(*body.forgeBody, ForgeCad::Kernel::EdgeId(edgeId),
                                                                   ForgeCad::Kernel::Vec3(sample.x(), sample.y(), sample.z()));
                    }
                    considerPolyline(polyline, candidate, true);
                }
            } else if (b < owner && isBodyDrawn(b) && ForgeCad::isCurveBody(body) && body.curve) {
                r.kind = 9;
                r.featureId = body.featureId;
                // Il punto sulla polilinea dice quale curva (ProjectedCurve ne ha piu' di una).
                for(int entity=0;entity<body.display.edges.size();++entity) {
                    auto candidate=r;
                    if(body.feature==BodyFeature::SpatialSketch && entity<body.spatialSketch.entities.size()) {
                        candidate.point.geometry=ForgeCad::kSpatialCurveReference;
                        candidate.point.subshape=body.spatialSketch.entities[entity].id;
                    }
                    considerPolyline(body.display.edges[entity],candidate,true);
                }
            }
        }
        for (int s = 0; s < sketches_.size(); ++s) {
            if (!referenceSketchEligible(s)) continue;
            const SketchObject &sketch = sketches_.at(s);
            GeometryRef r;
            r.kind = 7;
            r.index = s;
            for (int i = 0; i < sketch.segments.size(); ++i) {
                r.element = {0, i, -1};
                considerPolyline({mapSketchPoint(sketch.segments.at(i).first, sketch), mapSketchPoint(sketch.segments.at(i).second, sketch)}, r, false);
            }
            for (int i = 0; i < sketch.curves.size(); ++i) {
                r.element = {1, i, -1};
                QVector<QVector3D> polyline;
                for (const QPointF &p : sketch.curves.at(i).samples) polyline.append(mapSketchPoint(p, sketch));
                considerPolyline(polyline, r, false);
            }
        }
        if (axesVisible_)
            for (int axis = 0; axis < 3; ++axis) {
                QVector3D tip;
                tip[axis] = float(axisDisplayLength());
                GeometryRef r;
                r.kind = 2;
                r.index = axis;
                considerPolyline({QVector3D(), tip}, r, false);
            }
        if (found) return true;
    }
    if (roles & (ForgeCad::DatumRolePlane | ForgeCad::DatumRoleLine | ForgeCad::DatumRoleFace)) {
        QVector3D origin, direction;
        viewRay(position, origin, direction);
        double nearest = std::numeric_limits<double>::max();
        bool found = false;
        for (int b = 0; b < extrusions_.size(); ++b) {
            if (!referenceBodyEligible(b)) continue;
            FaceHit hit;
            if (!pickBodyFace(b, position, hit) || hit.distance >= nearest) continue;
            // Una faccia curva vale solo come retta (asse del cilindro o del cono).
            if (!hit.planar && !(roles & (ForgeCad::DatumRoleLine | ForgeCad::DatumRoleFace))) continue;
            nearest = hit.distance;
            const double length = double(direction.length());
            ref = GeometryRef();
            ref.kind = 5;
            ref.index = b;
            ref.featureId = extrusions_.at(b).featureId;
            ref.point = {double(origin.x()) + double(direction.x()) / length * hit.distance, double(origin.y()) + double(direction.y()) / length * hit.distance,
                         double(origin.z()) + double(direction.z()) / length * hit.distance};
            if (hit.face >= 0 && extrusions_.at(b).forgeBody)
                ref.point = ForgeCad::faceReference(*extrusions_.at(b).forgeBody, ForgeCad::Kernel::FaceId(hit.face),
                                                    ForgeCad::Kernel::Vec3(ref.point.x, ref.point.y, ref.point.z));
            if (faceHit) *faceHit = hit;
            if (faceBody) *faceBody = b;
            found = true;
        }
        if (pickBodies_.isEmpty() && (roles & ForgeCad::DatumRolePlane)) {
            double distance = 0.0;
            const int datum = pickDatumPlane(position, false, &distance);
            if (datum >= 0 && datum < owner && distance < nearest) {
                ref = GeometryRef();
                ref.kind = 8;
                ref.index = datum;
                ref.featureId = extrusions_.at(datum).featureId;
                if (faceBody) *faceBody = -1;
                found = true;
            }
            if (!found) {
                const int plane = pickReferencePlane(position);
                if (plane >= 0) {
                    ref = GeometryRef();
                    ref.kind = 1;
                    ref.index = plane;
                    found = true;
                }
            }
        }
        return found;
    }
    return false;
}

bool CadViewport::snapRefPosition(const GeometryRef &ref, QVector3D &position) const {
    if (ref.index < 0 || ref.index >= extrusions_.size() || !extrusions_.at(ref.index).forgeBody) return false;
    const ExtrusionObject &body = extrusions_.at(ref.index);
    const int edge = ref.point.subshape;
    if (edge < 0 || !body.forgeBody->contains(ForgeCad::Kernel::EdgeId(edge))) return false;
    if (ref.kind == ForgeCad::kGeometryRefEdgeCenter) {
        ForgeCad::Kernel::Vec3 c;
        if (!ForgeCad::edgeCenter(*body.forgeBody, ForgeCad::Kernel::EdgeId(edge), c)) return false;
        position = QVector3D(float(c.x()), float(c.y()), float(c.z()));
        return true;
    }
    const int polyline = body.display.edgeIds.indexOf(edge);
    if (polyline < 0) return false;
    position = polylineMidpoint(body.display.edges.at(polyline));
    return true;
}

QVector3D CadViewport::polylineMidpoint(const QVector<QVector3D> &polyline) {
    if (polyline.isEmpty()) return {};
    float total = 0.0f;
    for (int k = 1; k < polyline.size(); ++k) total += (polyline.at(k) - polyline.at(k - 1)).length();
    float walked = 0.0f;
    for (int k = 1; k < polyline.size(); ++k) {
        const float step = (polyline.at(k) - polyline.at(k - 1)).length();
        if (walked + step >= 0.5f * total && step > 0.0f) return polyline.at(k - 1) + (polyline.at(k) - polyline.at(k - 1)) * ((0.5f * total - walked) / step);
        walked += step;
    }
    return polyline.last();
}

QString CadViewport::edgePickMessage() const {
    if (edgePickHelix_) return QStringLiteral("Elica: clicca uno spigolo circolare o una faccia cilindrica o conica per la base, Esc annulla");
    QString preview;
    if (!pickedEdges_.isEmpty())
        preview = preview_.valid ? QStringLiteral(" - anteprima") : preview_.error.isEmpty() ? QStringLiteral(" - anteprima in calcolo...")
                                                                                      : QStringLiteral(" - anteprima non riuscita: ") + preview_.error;
    if (edgePickBody_ < 0)
        return QStringLiteral("%1: clicca %2, Esc annulla")
            .arg(edgePickExtend_ ? QStringLiteral("Estensione") : edgePickChamfer_ ? QStringLiteral("Smusso") : QStringLiteral("Raccordo"))
            .arg(edgePickExtend_ ? QStringLiteral("i bordi di una superficie") : QStringLiteral("gli spigoli di un solido o di superfici cucite (anche selezionando una faccia)"));
    if (edgePickExtend_)
        return QStringLiteral("Estensione: clicca i bordi di \"%1\" da estendere (%2 scelti), Invio conferma, Esc annulla%3")
            .arg(extrusions_.value(edgePickBody_).name)
            .arg(pickedEdges_.size())
            .arg(preview);
    return QStringLiteral("%1: clicca gli spigoli di \"%2\" o una faccia per tutti i suoi bordi (%3 scelti), Invio conferma, Esc annulla%4")
        .arg(edgePickChamfer_ ? QStringLiteral("Smusso") : QStringLiteral("Raccordo"))
        .arg(extrusions_.value(edgePickBody_).name)
        .arg(pickedEdges_.size())
        .arg(preview);
}

QVector<EdgePoint> CadViewport::pickedEdgePoints() const {
    QVector<EdgePoint> points;
    if (edgePickBody_ < 0 || edgePickBody_ >= extrusions_.size()) return points;
    const QVector<QVector<QVector3D>> &edges = extrusions_.at(edgePickBody_).display.edges;
    QSet<int> covered;
    if (!edgePickExtend_)
        for (const PickedFace &face : pickedFaces_) {
            bool all = !face.border.isEmpty();
            for (int index : face.border) all = all && pickedEdges_.contains(index);
            if (!all) continue;
            points.append(face.reference);
            for (int index : face.border) covered.insert(index);
        }
    for (int index : pickedEdges_) {
        if (covered.contains(index)) continue;
        if (index < 0 || index >= edges.size() || edges.at(index).size() < 2) continue;
        const QVector<QVector3D> &polyline = edges.at(index);
        const QVector3D p = polyline.size() >= 3 ? polyline.at(polyline.size() / 2) : 0.5f * (polyline.first() + polyline.last());
        const int edgeId = extrusions_.at(edgePickBody_).display.edgeIds.value(index, -1);
        if (edgeId >= 0 && extrusions_.at(edgePickBody_).forgeBody)
            points.append(ForgeCad::edgeReference(*extrusions_.at(edgePickBody_).forgeBody, ForgeCad::Kernel::EdgeId(edgeId),
                                                   ForgeCad::Kernel::Vec3(p.x(), p.y(), p.z())));
        else
            points.append({p.x(), p.y(), p.z()});
    }
    return points;
}

void CadViewport::edgePicked() {
    if (!edgePickPreviewEnabled_ || pickedEdges_.isEmpty() || edgePickHelix_) clearBlendPreview();
    else if (!edgePickExtend_ && edgePickEdit_ >= 0 && edgePickEdit_ < extrusions_.size()) {
        // Modifica degli spigoli di un raccordo esistente: niente calcolo a
        // ogni clic (si fa una volta, con la misura, alla conferma); resta
        // la patch del raccordo attuale, dal body che c'e' gia'.
        const ExtrusionObject &existing = extrusions_.at(edgePickEdit_);
        if (unchangedFeature(existing, edgePickEdit_)) requestPreview(existing, edgePickEdit_);
        else clearBlendPreview();
    } else if (edgePickExtend_) requestExtendPreview(edgePickBody_, pickedEdgePoints(), edgePickSize_, edgePickLinear_, edgePickEdit_);
    else requestBlendPreview(edgePickBody_, pickedEdgePoints(), edgePickSize_, edgePickChamfer_, edgePickEdit_, edgePickSpec_);
    if (edgePickStatus_) edgePickStatus_(edgePickMessage());
    if (edgePickChanged_) edgePickChanged_(edgePickBody_, pickedEdgePoints());
    update();
}

void CadViewport::cancelEdgePick(bool keepPreview ) {
    if (!keepPreview) clearBlendPreview();
    edgePickEdit_ = -1;
    edgePickBody_ = -1;
    edgePicking_ = false;
    edgePickHelix_ = false;
    hoverEdgeBody_ = -1;
    pickedEdges_.clear();
    pickedFaces_.clear();
    hoverEdge_ = -1;
    hoverFaceEdges_.clear();
    if (edgePickStatus_) edgePickStatus_(QString());
    update();
}

void CadViewport::finishEdgePick() {
    if (edgePickChanged_) return;  // con il pannello aperto si conferma con OK
    const int body = edgePickBody_;
    if (body < 0 || body >= extrusions_.size() || pickedEdges_.isEmpty()) {
        cancelEdgePick();
        return;
    }
    const QVector<EdgePoint> points = pickedEdgePoints();
    const bool chamfer = edgePickChamfer_, extend = edgePickExtend_;
    const int edited = edgePickEdit_;
    // L'anteprima resta per la finestra della misura (che la aggiorna e poi la toglie).
    cancelEdgePick(true);
    if (points.isEmpty()) {
        clearBlendPreview();
        return;
    }
    if (extend) {
        if (extendPickFinished_) extendPickFinished_(body, points);
    } else if (edited >= 0) {
        if (edgeEditFinished_) edgeEditFinished_(edited, points, edgePickSize_, chamfer);
    } else if (edgePickFinished_) {
        edgePickFinished_(body, points, chamfer);
    }
}

bool CadViewport::edgePickEligible(int index) const {
    if (index < 0 || index >= extrusions_.size() || (sketchMode_ && !isBodyDrawn(index))) return false;
    const ExtrusionObject &body = extrusions_.at(index);
    if (!ForgeCad::isShapeBody(body) || (!isBodyDrawn(index) && index != edgePickBody_)) return false;
    if (edgePickHelix_) return true;
    return !edgePickExtend_ || !body.solid;
}

const CadViewport::ProjectedEdges &CadViewport::projectedBodyEdges(int index) const {
    projectWorldPoint(QVector3D()); // aggiorna la chiave della vista
    if (projectedEdges_.size() != extrusions_.size()) projectedEdges_.resize(extrusions_.size());
    auto &cache = projectedEdges_[index];
    const auto &display = extrusions_.at(index).display;
    if (cache.valid && cache.view == camera_.projectionKey_ && cache.source.edges.constData() == display.edges.constData()) return cache;
    cache.source = display;
    cache.view = camera_.projectionKey_;
    cache.lines.clear(); cache.bounds.clear();
    for (const auto &edge : display.edges) {
        QVector<QPointF> points;
        points.reserve(edge.size());
        for (const auto &p : edge) points.append(projectWorldPoint(p));
        QRectF bounds;
        if (!points.isEmpty()) {
            double x0 = points[0].x(), x1 = x0, y0 = points[0].y(), y1 = y0;
            for (const auto &p : points) { x0 = std::min(x0, p.x()); x1 = std::max(x1, p.x()); y0 = std::min(y0, p.y()); y1 = std::max(y1, p.y()); }
            bounds = QRectF(QPointF(x0, y0), QPointF(x1, y1));
        }
        cache.lines.append(points); cache.bounds.append(bounds);
    }
    cache.valid = true;
    return cache;
}

bool CadViewport::edgeOccludedByMesh(const BodyDisplay &display, const QVector3D &origin, const QVector3D &direction,
                                   const QVector3D &edgePoint, double tolerance) {
    float surfaceDistance = 0.0f;
    if (!meshRayHit(display, origin, direction, surfaceDistance)) return false;
    const double edgeDistance = QVector3D::dotProduct(edgePoint - origin, direction.normalized());
    return edgeDistance > double(surfaceDistance) + tolerance;
}

int CadViewport::pickEdge(const QPoint &position, int *body ) const {
    int best = -1, bestBody = -1;
    double nearest = 8.0;
    const int frontBody = hiddenEdgesVisible_ ? -1 : gpuPickBody(position);
    QVector3D rayOrigin, rayDirection;
    if (!hiddenEdgesVisible_) viewRay(position, rayOrigin, rayDirection);
    for (int candidate = 0; candidate < extrusions_.size(); ++candidate) {
        if (!pickedEdges_.isEmpty() && candidate != edgePickBody_) continue;
        if (!edgePickEligible(candidate)) continue;
        // Un altro corpo opaco davanti impedisce di prendere gli spigoli
        // di questo. Sullo sfondo si prosegue: puo' essere una silhouette.
        if (!hiddenEdgesVisible_ && frontBody >= 0 && frontBody != candidate) continue;
        const auto &projected = projectedBodyEdges(candidate);
        const auto &edges = projected.lines;
        for (int index = 0; index < edges.size(); ++index) {
            if (!projected.bounds.at(index).adjusted(-9, -9, 9, 9).contains(position)) continue;
            const auto &polyline = edges.at(index);
            for (int k = 1; k < polyline.size(); ++k) {
                const QVector2D screenSegment(polyline.at(k) - polyline.at(k - 1));
                const float screenLength = screenSegment.lengthSquared();
                const float parameter = screenLength > 0.0f
                    ? qBound(0.0f, QVector2D::dotProduct(QVector2D(QPointF(position) - polyline.at(k - 1)), screenSegment)
                                            / screenLength, 1.0f)
                    : 0.0f;
                // A parita' di distanza vince il corpo gia' in scelta.
                const double d = distanceToSegment(QPointF(position), polyline.at(k - 1), polyline.at(k))
                               - (candidate == edgePickBody_ ? 0.5 : 0.0);
                if (d >= nearest) continue;
                if (!hiddenEdgesVisible_) {
                    const QVector<QVector3D> &world = extrusions_.at(candidate).display.edges.at(index);
                    const QVector3D edgePoint = world.at(k - 1) + parameter * (world.at(k) - world.at(k - 1));
                    // Due pixel di margine assorbono la discretizzazione
                    // della mesh senza far passare il bordo opposto di
                    // pareti o fori sottili.
                    const double tolerance = qMax(1e-5, 2.0 * double(camera_.zoom_) / qMax(1, height()));
                    if (edgeOccludedByMesh(extrusions_.at(candidate).display, rayOrigin, rayDirection, edgePoint, tolerance)) continue;
                }
                nearest = d;
                best = index;
                bestBody = candidate;
            }
        }
    }
    if (body) *body = bestBody;
    return best;
}

int CadViewport::edgePickFaceBody(const QPoint &position) const {
    if (edgePickBody_ >= 0 && !pickedEdges_.isEmpty()) return edgePickBody_;
    const SceneSelection hit = pickSceneObject(position);
    if (hit.kind == SceneObjectKind::Extrusion && edgePickEligible(hit.index)) return hit.index;
    return edgePickBody_;
}

bool CadViewport::cachedRayFace(int index, const QVector3D &origin, const QVector3D &direction, FaceHit &hit) const {
    const ExtrusionObject &body = extrusions_.at(index);
    if (!body.forgeBody) return false;
    if (raySelectionCache_.size() != extrusions_.size()) raySelectionCache_.resize(extrusions_.size());
    auto &cache = raySelectionCache_[index];
    if (!cache.valid || cache.body != body.forgeBody || cache.origin != origin || cache.direction != direction) {
        cache.body = body.forgeBody;
        cache.origin = origin;
        cache.direction = direction;
        // Prima la sola zona del punto colpito sulla tassellazione (poche
        // facce, esatte); se li' non c'e' nulla, tutte le facce.
        float meshT = 0.0f;
        cache.hit = false;
        if (body.display.rayIndex && meshRayHit(body.display, origin, direction, meshT)) {
            const double slack = std::max(1e-6, 0.01 * body.display.rayIndex->bounds.diagonal());
            const ForgeCad::Kernel::Interval window{double(meshT) - slack, double(meshT) + slack};
            cache.hit = ForgeCad::forgePickFace(*body.forgeBody, origin, direction, cache.face, body.display.rayIndex.get(), &window);
        }
        if (!cache.hit) cache.hit = ForgeCad::forgePickFace(*body.forgeBody, origin, direction, cache.face, body.display.rayIndex.get());
        cache.valid = true;
    }
    if (cache.hit) hit = cache.face;
    return cache.hit;
}

bool CadViewport::pickBodyFace(int index, const QPoint &position, FaceHit &hit) const {
    if (index < 0 || index >= extrusions_.size() || !ForgeCad::isShapeBody(extrusions_.at(index))) return false;
    QVector3D origin, direction;
    viewRay(position, origin, direction);
    double skipped = 0.0;
    if (!sectionRay(origin, direction, skipped)) return false;
    if (!cachedRayFace(index, origin, direction, hit)) return false;
    if (sectionRemoves(origin + float(hit.distance) * direction.normalized())) return false;
    hit.distance += skipped;
    return true;
}

int CadViewport::faceOwnerFeature(int index, const QPoint &position, const FaceHit &hit) const {
    if (index < 0 || index >= extrusions_.size() || hit.face < 0 || !extrusions_.at(index).forgeBody) return index;
    QVector3D origin, direction;
    viewRay(position, origin, direction);
    double skipped = 0.0;
    if (!sectionRay(origin, direction, skipped)) return index;
    // Lo stesso punto del raggio del kernel (cachedRayFace).
    const double t = hit.distance - skipped;
    const ForgeCad::Kernel::Vec3 point(double(origin.x()) + t * double(direction.x()), double(origin.y()) + t * double(direction.y()),
                                       double(origin.z()) + t * double(direction.z()));
    QSet<int> chain{index};
    QVector<int> pending{index};
    while (!pending.isEmpty()) {
        const int body = pending.takeLast();
        for (int operand : ForgeCad::bodyOperands(extrusions_.at(body)))
            if (operand >= 0 && operand < body && !chain.contains(operand)) {
                chain.insert(operand);
                pending.append(operand);
            }
    }
    QVector<int> ordered(chain.begin(), chain.end());
    std::sort(ordered.begin(), ordered.end());
    std::vector<std::pair<int, ForgeCad::ForgeBody>> bodies;
    for (int body : ordered) {
        const ExtrusionObject &feature = extrusions_.at(body);
        if (feature.suppressed || !ForgeCad::isShapeBody(feature) || !feature.forgeBody) continue;
        bodies.push_back({body, feature.forgeBody});
    }
    const int owner = ForgeCad::forgeFaceOwner(*extrusions_.at(index).forgeBody, hit.face, point, bodies);
    return owner >= 0 ? owner : index;
}

QSet<int> CadViewport::operandClosure(int body) const {
    QSet<int> chain{body};
    QVector<int> pending{body};
    while (!pending.isEmpty()) {
        const int current = pending.takeLast();
        for (int operand : ForgeCad::bodyOperands(extrusions_.at(current)))
            if (operand >= 0 && operand < current && !chain.contains(operand)) {
                chain.insert(operand);
                pending.append(operand);
            }
    }
    return chain;
}

void CadViewport::clearFeatureHighlights() {
    ++featureHighlightGeneration_;
    if (featureHighlights_.isEmpty() && bodyHighlight_ < 0) return;
    featureHighlights_.clear();
    bodyHighlight_ = -1;
    update();
}

void CadViewport::setBodyHighlight(int body) {
    clearFeatureHighlights();
    if (sketchMode_ || body < 0 || body >= extrusions_.size() || !ForgeCad::isShapeBody(extrusions_.at(body))) return;
    bodyHighlight_ = body;
    update();
}

void CadViewport::setFeatureHighlights(const QVector<int> &features) {
    clearFeatureHighlights();
    if (features.isEmpty() || sketchMode_) return;
    const quint64 generation = featureHighlightGeneration_;
    const QPointer<CadViewport> guard(this);
    // Corpi visibili con la loro catena di operandi (calcolata una volta).
    QVector<QPair<int, QSet<int>>> shown;
    for (int body = 0; body < extrusions_.size(); ++body) {
        const ExtrusionObject &candidate = extrusions_.at(body);
        if (candidate.visible && ForgeCad::isShapeBody(candidate) && candidate.forgeBody && !candidate.display.vertices.isEmpty())
            shown.append({body, operandClosure(body)});
    }
    for (int feature : features) {
        if (feature < 0 || feature >= extrusions_.size()) continue;
        const ExtrusionObject &definition = extrusions_.at(feature);
        if (definition.suppressed || !ForgeCad::isShapeBody(definition)) continue;
        int target = -1;
        for (const auto &entry : shown)
            if (entry.second.contains(feature)) { target = entry.first; break; }
        if (target < 0) continue;
        QVector<int> ordered;
        for (int body : operandClosure(feature)) ordered.append(body);
        std::sort(ordered.begin(), ordered.end());
        std::vector<std::pair<int, ForgeCad::ForgeBody>> chain;
        for (int body : ordered) {
            const ExtrusionObject &stage = extrusions_.at(body);
            if (!stage.suppressed && ForgeCad::isShapeBody(stage) && stage.forgeBody) chain.push_back({body, stage.forgeBody});
        }
        const ForgeCad::ForgeBody result = extrusions_.at(target).forgeBody;
        const BodyDisplay display = extrusions_.at(target).display;
        QThreadPool::globalInstance()->start([this, guard, generation, feature, target, result, display, chain] {
            BodyDisplay highlight;
            BodyDisplay fresh;
            QVector<int> faces;
            try {
                faces = ForgeCad::forgeFeatureFaces(*result, display.faceIds, display.faceLabelPoints, feature, chain);
                // I documenti salvati prima del formato 4 della cache non hanno
                // la faccia di ogni triangolo: si ritassella (stessa qualita').
                const bool known = !display.triangleFaces.isEmpty() && display.triangleFaces.size() * 3 == display.vertices.size();
                if (!known && !faces.isEmpty()) ForgeCad::forgeTessellate(*result, qMax(0, display.quality), fresh);
                const BodyDisplay &mesh = known ? display : fresh;
                const QSet<int> chosen(faces.begin(), faces.end());
                for (qsizetype t = 0; t < mesh.triangleFaces.size() && 3 * t + 2 < mesh.vertices.size(); ++t) {
                    if (!chosen.contains(mesh.triangleFaces.at(t))) continue;
                    for (int k = 0; k < 3; ++k) {
                        highlight.vertices.append(mesh.vertices.at(3 * t + k));
                        highlight.normals.append(mesh.normals.value(3 * t + k));
                    }
                }
                QSet<int> lines;
                for (int face : faces)
                    for (int line : mesh.faceEdges.value(face)) lines.insert(line);
                for (int line : lines)
                    if (line >= 0 && line < mesh.edges.size()) highlight.edges.append(mesh.edges.at(line));
                highlight.quality = display.quality;
            } catch (...) {
                faces.clear();
            }
            deliverResult(guard, [this, generation, feature, target, faces, previous = display.vertices,
                                                 highlight = std::move(highlight), fresh = std::move(fresh)]() mutable {
                if (generation != featureHighlightGeneration_) return;
                if (target >= extrusions_.size() || feature >= extrusions_.size()) return;
                // Mesh rifatta (cache senza triangleFaces): il corpo prende la stessa
                // tassellazione dell'evidenziazione, cosi' le facce coincidono.
                BodyDisplay &shown = extrusions_[target].display;
                if (!fresh.vertices.isEmpty() && shown.vertices.constData() == previous.constData()
                    && fresh.vertices.size() > 0) {
                    fresh.rayIndex = shown.rayIndex ? shown.rayIndex : fresh.rayIndex;
                    shown = std::move(fresh);
                    raySelectionCache_.clear();
                    projectedEdges_.clear();
                }
                if (faces.isEmpty()) {
                    showStatus(QStringLiteral("%1: nessuna faccia del modello visibile appartiene a questa feature").arg(extrusions_.at(feature).name));
                    return;
                }
                featureHighlights_.append({feature, target, std::move(highlight)});
                showStatus(QStringLiteral("%1: %2 %3 evidenziate su %4").arg(extrusions_.at(feature).name).arg(faces.size())
                               .arg(faces.size() == 1 ? QStringLiteral("faccia") : QStringLiteral("facce"), extrusions_.at(target).name));
                update();
            });
        });
    }
}

EdgePoint CadViewport::faceBoundaryReference(int body, int face, const ForgeCad::Kernel::Vec3 &point) const {
    EdgePoint reference{point.x(), point.y(), point.z()};
    if (body >= 0 && body < extrusions_.size() && extrusions_.at(body).forgeBody && face >= 0)
        reference = ForgeCad::faceReference(*extrusions_.at(body).forgeBody, ForgeCad::Kernel::FaceId(face), point);
    reference.role = kEdgePointFaceBoundary;
    return reference;
}

bool CadViewport::faceLabelPoint(int body, int face, ForgeCad::Kernel::Vec3 &point) const {
    using namespace ForgeCad::Kernel;
    if (body < 0 || body >= extrusions_.size() || face < 0) return false;
    const BodyDisplay &display = extrusions_.at(body).display;
    const int k = display.faceIds.indexOf(face);
    if (k >= 0 && k < display.faceLabelPoints.size()) {
        const QVector3D p = display.faceLabelPoints.at(k);
        point = Vec3(p.x(), p.y(), p.z());
        return true;
    }
    // Senza tassellazione (corpo nascosto): il punto medio di un bordo,
    // spostato verso l'interno (la faccia sta a sinistra della fin) e
    // riportato sulla superficie.
    const ForgeCad::ForgeBody &shape = extrusions_.at(body).forgeBody;
    if (!shape || !shape->contains(FaceId(face))) return false;
    const Face &data = shape->face(FaceId(face));
    for (LoopId loop : data.loops)
        for (FinId fin : shape->loopFins(loop)) {
            const Edge &edge = shape->edge(shape->fin(fin).edge);
            const double t = 0.5 * (edge.range.lo + edge.range.hi);
            Vec3 c[2];
            edge.curve->evaluate(t, 1, c);
            const bool forward = shape->edge(shape->fin(fin).edge).forward == fin;
            const Vec3 tangent = (forward ? 1.0 : -1.0) * c[1];
            const SurfaceProjection on = projectPoint(*data.surface, c[0]);
            const Vec3 n = data.surface->normal(on.u, on.v);
            const Vec3 inward = cross(data.sense ? n : -n, tangent);
            if (!(norm(inward) > 0.0)) continue;
            const double step = 1e-2 * std::max(1e-9, norm(c[1]) * edge.range.length());
            const Vec3 candidate = projectPoint(*data.surface, c[0] + inward * (step / norm(inward))).point;
            if (classifyPointOnFace(*shape, FaceId(face), candidate, 1e-9) == PointLocation::Inside) {
                point = candidate;
                return true;
            }
        }
    return false;
}

void CadViewport::pickReferences(int body, const QVector<EdgePoint> &references, bool sameState ) {
    pickedEdges_.clear();
    pickedFaces_.clear();
    if (body < 0 || body >= extrusions_.size()) return;
    const ExtrusionObject &object = extrusions_.at(body);
    const BodyDisplay &display = object.display;
    const auto add = [&](int index) {
        if (index >= 0 && !pickedEdges_.contains(index)) pickedEdges_.append(index);
    };
    const ForgeCad::ReferenceState state = sameState ? ForgeCad::ReferenceState::Same : ForgeCad::ReferenceState::Other;
    double reach = std::numeric_limits<double>::max();
    if (object.forgeBody) {
        ForgeCad::Kernel::Box box;
        for (ForgeCad::Kernel::VertexId v : object.forgeBody->vertices()) box.add(object.forgeBody->vertex(v).point);
        reach = 1e-3 * std::max(1.0, box.diagonal());
    }
    for (const EdgePoint &reference : references) {
        if (!object.forgeBody) {
            FaceHit points;
            points.edges = {reference};
            for (int index : faceDisplayEdges(body, points)) add(index);
            continue;
        }
        if (reference.role == kEdgePointFaceBoundary) {
            const ForgeCad::Kernel::FaceId face = ForgeCad::resolveFaceReference(*object.forgeBody, reference, reach, ForgeCad::ReferenceState::Other);
            if (!face.valid() || face.index >= display.faceEdges.size()) continue;
            const QVector<int> &border = display.faceEdges.at(face.index);
            for (int index : border) add(index);
            pickedFaces_.append({face.index, reference, border});
        } else {
            const ForgeCad::Kernel::EdgeId edge = ForgeCad::resolveEdgeReference(*object.forgeBody, reference, reach, state);
            if (edge.valid()) add(display.edgeIds.indexOf(edge.index));
        }
    }
}

QVector<int> CadViewport::faceDisplayEdges(int index, const FaceHit &hit) const {
    QVector<int> result;
    if (index < 0 || index >= extrusions_.size()) return result;
    const BodyDisplay &display = extrusions_.at(index).display;
    if (hit.face >= 0 && hit.face < display.faceEdges.size()) return display.faceEdges.at(hit.face);
    const QVector<QVector<QVector3D>> &edges = display.edges;
    for (const EdgePoint &point : hit.edges) {
        if (point.subshape >= 0) {
            const int exact = display.edgeIds.indexOf(point.subshape);
            if (exact >= 0) {
                if (!result.contains(exact)) result.append(exact);
                continue;
            }
        }
        const QVector3D p(float(point.x), float(point.y), float(point.z));
        int best = -1;
        float nearest = std::numeric_limits<float>::max();
        for (int e = 0; e < edges.size(); ++e)
            for (int k = 1; k < edges.at(e).size(); ++k) {
                const QVector3D a = edges.at(e).at(k - 1), segment = edges.at(e).at(k) - a;
                const float length = segment.lengthSquared();
                const float t = length > 0.0f ? qBound(0.0f, QVector3D::dotProduct(p - a, segment) / length, 1.0f) : 0.0f;
                const float d = (a + t * segment - p).length();
                if (d < nearest) {
                    nearest = d;
                    best = e;
                }
            }
        if (best >= 0 && !result.contains(best)) result.append(best);
    }
    return result;
}

bool CadViewport::sectionRay(QVector3D &origin, const QVector3D &direction, double &skipped) const {
    skipped = 0.0;
    if (!section_.enabled) return true;
    const QVector3D d = direction.normalized();
    const float side = QVector3D::dotProduct(origin - section_.point, section_.normal);
    if (side <= 0.0f) return true;
    const float along = QVector3D::dotProduct(d, section_.normal);
    if (along >= 0.0f) return false;
    skipped = double(-side / along);
    origin += float(skipped) * d;
    return true;
}

bool CadViewport::sectionRemoves(const QVector3D &point) const {
    return section_.enabled && QVector3D::dotProduct(point - section_.point, section_.normal) > 1e-6f * qMax(1.0f, float(sceneDepth()));
}
