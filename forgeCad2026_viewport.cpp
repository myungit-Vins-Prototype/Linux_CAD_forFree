#include "forgeCad2026_viewport_internal.h"


void CadViewport::calculateShapeAnalysis(const std::function<void()> &work) { runWhileResponsive(work); }

void CadViewport::clearShapeAnalysis() { shapeAnalysisBody_ = -1; shapeAnalysisMode_ = -1; shapeAnalysisFace_ = -1; shapeAnalysisFaceDisplay_ = {}; shapeAnalysis_ = {}; update(); }

CadViewport::CadViewport(QWidget *parent ) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(640, 420);
    // I tre piani standard hanno sempre la stessa misura. Per migrare le
    // impostazioni precedenti usa la scala del piano frontale, che era il
    // riferimento visivo principale, quando la nuova chiave non esiste.
    QSettings settings;
    const double sharedPlaneScale = qBound(
        0.05, settings.value(QStringLiteral("view/planeScale"),
                             settings.value(QStringLiteral("view/planeScale0"), 1.0)).toDouble(), 20.0);
    for (double &scale : referencePlaneScales_) scale = sharedPlaneScale;
}

CadViewport::~CadViewport() {
    clearFeatureHandle();
    if (previewCancellation_) previewCancellation_->store(true, std::memory_order_relaxed);
    // I pannelli figli si deregistrano dal compositore: devono sparire
    // mentre i membri del viewport esistono ancora, prima della classe base.
    qDeleteAll(findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly));
    // I framebuffer vanno distrutti con il loro contesto corrente.
    // Il contesto puo' emettere aboutToBeDestroyed dal distruttore della
    // classe base, quando i nostri membri non esistono piu'.
    if (context()) disconnect(context(), nullptr, this, nullptr);
    makeCurrent();
    displayCache_.clear();
    overlayRenderer_.clear();
    glassCompositor_.clearResources();
    sceneRenderer_.clearResources();
    gpuPicker_.clearResources();
    doneCurrent();
}

void CadViewport::setDisplayMode(int mode) { displayMode_ = mode; update(); }

void CadViewport::setHiddenEdgesVisible(bool visible) { hiddenEdgesVisible_ = visible; update(); }

void CadViewport::setTopologyIdsVisible(bool visible) {
    topologyIdsVisible_ = visible;
    if (!visible) {
        topologyHighlightBody_ = topologyHighlightEdge_ = topologyHighlightFace_ = -1;
    }
    update();
}

void CadViewport::setLightingPreset(int preset) { lightingPreset_ = preset; update(); }

void CadViewport::setGeometrySnap(bool enabled) { geometrySnap_ = enabled; update(); }

bool CadViewport::geometrySnap() const { return geometrySnap_; }

void CadViewport::setGridSnap(bool enabled) { gridSnap_ = enabled; update(); }

bool CadViewport::gridSnap() const { return gridSnap_; }

void CadViewport::setConstraintMode(int mode) { constraintMode_ = mode; }

void CadViewport::setLineLength(double length) { lineLength_ = qMax(0.0, length); update(); }

void CadViewport::setLineAngle(double angle) { lineAngle_ = angle; update(); }

void CadViewport::setPolygonSides(int sides) { polygonSides_ = qBound(3, sides, 64); }

void CadViewport::setTessellationQuality(int quality) {
    ScopedWork work(workCallback_, QStringLiteral("Aggiornamento della qualita' della scena..."));
    tessellationQuality_ = qBound(0, quality, 2);
    for (SketchObject &sketch : sketches_) {
        for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
    }
    for (int index = 0; index < extrusions_.size(); ++index) tessellateBody(extrusions_[index], index);
    update();
}

void CadViewport::setWheelZoomEnabled(bool enabled) { wheelZoomEnabled_ = enabled; }

quint64 CadViewport::renderedFrameSerial() const { return renderedFrameSerial_; }

void CadViewport::setGlassPanel(quintptr id, const QRect &rect, int blur, int radius, bool visible) {
    glassCompositor_.setPanel(id, rect, blur, radius, visible);
    update();
}

void CadViewport::removeGlassPanel(quintptr id) {
    glassCompositor_.removePanel(id); update();
}

void CadViewport::setExportMeshPreview(const BodyDisplay &display, bool quadrangular, bool limited) {
    exportMeshPreview_ = display;
    exportMeshPreviewQuadrangular_ = quadrangular;
    exportMeshPreviewLimited_ = limited;
    update();
}

void CadViewport::clearExportMeshPreview() {
    exportMeshPreview_ = {};
    exportMeshPreviewLimited_ = false;
    update();
}

bool CadViewport::gpuGlassAvailable() const {
    return glassCompositor_.available();
}

QImage CadViewport::panelBackdropFrame() {
    if (panelBackdropSerial_ != renderedFrameSerial_ || panelBackdropCache_.isNull()) {
        panelBackdropCache_ = grabFramebuffer();
        // grabFramebuffer puo' provocare un paintGL: registra il seriale
        // dopo la lettura per non considerare la cattura un nuovo frame.
        panelBackdropSerial_ = renderedFrameSerial_;
    }
    return panelBackdropCache_;
}

void CadViewport::zoomIn() { setZoom(camera_.zoom_ * 0.85f); }

void CadViewport::zoomOut() { setZoom(camera_.zoom_ / 0.85f); }

void CadViewport::resetZoom() { fitAll(); }

void CadViewport::fitAll() {
    fitView(sceneGeometryPoints());
    camera_.autoFit_ = true;
}

void CadViewport::setPanKey(int key) { panKey_ = key; panKeyHeld_ = false; }

int CadViewport::panKey() const { return panKey_; }

void CadViewport::setReferencePlanesVisible(bool visible) { referencePlanesVisible_ = visible; update(); }

void CadViewport::setPlaneVisible(int plane, bool visible) {
    if (plane < 0 || plane >= 3) return;
    planeVisible_[plane] = visible;
    update();
}

const BackgroundSettings &CadViewport::background() const { return background_; }

void CadViewport::setBackground(const BackgroundSettings &background) { background_ = background; update(); }

void CadViewport::setStatusCallback(std::function<void(const QString &)> callback) { statusCallback_ = std::move(callback); }

void CadViewport::setAntialiasing(int samples) {
    sceneRenderer_.setAntialiasing(samples);
    update();
}

int CadViewport::antialiasing() const { return sceneRenderer_.antialiasing(); }

int CadViewport::maxAntialiasing() const { return sceneRenderer_.maxAntialiasing(); }

void CadViewport::setGridVisible(bool visible) { gridVisible_ = visible; update(); }

void CadViewport::setAxisLength(double length) { axisLength_ = qBound(0.1, length, 100.0); update(); }

double CadViewport::axisLength() const { return axisLength_; }

double CadViewport::axisDisplayLength() const {
    return double(camera_.zoom_) * 0.22 * axisLength_ / kDefaultAxisLength;
}

void CadViewport::setAxesVisible(bool visible) { axesVisible_ = visible; update(); }

bool CadViewport::axesVisible() const { return axesVisible_; }

void CadViewport::setAxesOnTop(bool onTop) { axesOnTop_ = onTop; update(); }

bool CadViewport::axesOnTop() const { return axesOnTop_; }

QColor CadViewport::featureHighlightColor() const { return featureHighlightColor_; }

void CadViewport::setFeatureHighlightColor(const QColor &color) {
    featureHighlightColor_ = color.isValid() ? color : kSelectionColor;
    update();
}

void CadViewport::setSketchBodyOpacity(double opacity) { sketchBodyOpacity_ = qBound(0.05, opacity, 1.0); update(); }

double CadViewport::sketchBodyOpacity() const { return sketchBodyOpacity_; }

void CadViewport::setSketchViewUnlocked(bool unlocked) {
    if (unlocked == sketchViewUnlocked_) return;
    if (unlocked) {
        if (!sketchMode_) return;
        sketchCamera_ = {camera_.yaw_, camera_.pitch_, camera_.roll_, camera_.zoom_, camera_.panX_, camera_.panY_, sketchViewRotated_};
        sketchViewUnlocked_ = true;
        hasPendingPoint_ = false;
        curveControlPoints_.clear();
        showStatus(QStringLiteral("Trascina nella vista per ruotarla e vedere da che parte va la funzione; alla chiusura torna la vista dello schizzo"));
    } else {
        sketchViewUnlocked_ = false;
        if (sketchMode_) {
            camera_.yaw_ = sketchCamera_.yaw;
            camera_.pitch_ = sketchCamera_.pitch;
            camera_.roll_ = sketchCamera_.roll;
            camera_.zoom_ = sketchCamera_.zoom;
            camera_.panX_ = sketchCamera_.panX;
            camera_.panY_ = sketchCamera_.panY;
            sketchViewRotated_ = sketchCamera_.rotated;
        }
    }
    update();
}

void CadViewport::alignViewToSketch() {
    const SketchObject *sketch = activeSketchObject();
    if (!sketch) return;
    const QPoint center(width() / 2, height() / 2);
    QPointF middle;
    const bool keep = !sketchViewRotated_ || rayToSketchPlane(center, middle);
    if (!sketchViewRotated_) middle = screenToSketchPoint(center);
    const ForgeCad::Kernel::Frame3 axes = ForgeCad::sketchAxes(*sketch);
    setViewFrame(QVector3D(float(axes.xDir().x()), float(axes.xDir().y()), float(axes.xDir().z())),
                 QVector3D(float(axes.zDir().x()), float(axes.zDir().y()), float(axes.zDir().z())));
    sketchViewRotated_ = false;
    if (keep) {
        // Nella vista allineata il centro dello schermo e' il punto -pan dello schizzo.
        camera_.panX_ = float(-middle.x());
        camera_.panY_ = float(-middle.y());
    } else {
        const QVector<QVector3D> points = sketchGeometryPoints(*sketch);
        fitView(points.isEmpty() ? sceneGeometryPoints() : points);
    }
    update();
}

void CadViewport::setOriginSnap(bool enabled) { originSnap_ = enabled; }

bool CadViewport::originSnap() const { return originSnap_; }

void CadViewport::setSelectionCallback(std::function<void(SceneSelection)> callback) {
    selectionCallback_ = std::move(callback);
}

void CadViewport::setSketchPickCallback(std::function<void(int)> callback) { sketchPickCallback_ = std::move(callback); }

void CadViewport::setSketchEntityPickCallback(std::function<void(int, int, int)> callback) {
    sketchEntityPickCallback_ = std::move(callback);
}

void CadViewport::setDocumentChangedCallback(std::function<void()> callback) {
    documentChangedCallback_ = std::move(callback);
}

void CadViewport::setPlaneContextCallback(std::function<void(int)> callback) {
    planeContextCallback_ = std::move(callback);
}

void CadViewport::setToolChangedCallback(std::function<void(DrawingTool)> callback) { toolChangedCallback_ = std::move(callback); }

void CadViewport::setSketchModeCallback(std::function<void(bool)> callback) {
    sketchModeCallback_ = std::move(callback);
}

void CadViewport::setRendererCallback(std::function<void(const QString &)> callback) {
    rendererCallback_ = std::move(callback);
}

void CadViewport::setWorkCallback(std::function<void(bool, const QString &, bool)> callback) {
    workCallback_ = std::move(callback);
}

int CadViewport::activeSketchIndex() const { return activeSketch_; }

bool CadViewport::sketchModeActive() const { return sketchMode_; }

void CadViewport::setEditBodyCallback(std::function<void(int)> callback) { editBodyCallback_ = std::move(callback); }

void CadViewport::setReferenceMarks(const QVector<GeometryRef> &marks) {
    refMarks_ = marks;
    update();
}

void CadViewport::setReferenceListHighlight(QObject *owner, const QVector<GeometryRef> &refs) {
    if (refs.isEmpty()) { clearReferenceListHighlight(owner); return; }
    referenceListOwner_ = owner;
    referenceListMarks_ = refs;
    update();
}

void CadViewport::clearReferenceListHighlight(QObject *owner) {
    if (referenceListOwner_ && referenceListOwner_ != owner) return;
    referenceListOwner_.clear();
    referenceListMarks_.clear();
    update();
}

bool CadViewport::referenceListHighlighted(QObject *owner) const { return referenceListOwner_ == owner; }

void CadViewport::setMeasureOverlay(bool valid, const QVector3D &from , const QVector3D &to , const QString &label ) {
    measureValid_ = valid;
    measureFrom_ = from;
    measureTo_ = to;
    measureLabel_ = label;
    update();
}

void CadViewport::setDatumPreview(bool valid, const SketchFrame &frame, double size, int replaced) {
    datumPreviewValid_ = valid;
    datumPreviewFrame_ = frame;
    datumPreviewSize_ = size;
    datumPreviewIndex_ = replaced;
    update();
}

void CadViewport::clearDatumPreview() {
    datumPreviewValid_ = false;
    datumPreviewIndex_ = -1;
    refMarks_.clear();
    update();
}

double CadViewport::suggestedDatumOffset() const { return double(niceCeiling(0.25f * planeHalf())); }

void CadViewport::setInteractionLocked(bool locked) { interactionLocked_ = locked; }

bool CadViewport::interactionLocked() const { return interactionLocked_; }

void CadViewport::setSection(bool enabled, const QVector3D &point , const QVector3D &normal , bool caps , bool showPlane ) {
    section_.enabled = enabled && normal.lengthSquared() > 0.0f;
    section_.point = point;
    section_.normal = normal.lengthSquared() > 0.0f ? normal.normalized() : QVector3D(0, 0, 1);
    section_.caps = caps;
    section_.showPlane = showPlane;
    update();
}

bool CadViewport::sectionEnabled() const { return section_.enabled; }

void CadViewport::clearFeatureHandle() {
    featureHandleChanged_ = {};
    featureHandleDragging_ = false;
    featureHandlePath_.clear();
    if (featureHandleBox_) featureHandleBox_->hide();
    unsetCursor();
    update();
}

void CadViewport::setFeatureHandle(const ExtrusionObject &definition, std::function<void(double)> changed) {
    featureHandleDefinition_ = definition;
    featureHandleChanged_ = std::move(changed);
    if (!featureHandleBox_) {
        featureHandleBox_ = new ForgeCad::ExpressionSpinBox(this);
        featureHandleBox_->setObjectName(QStringLiteral("featureHandleValue"));
        featureHandleBox_->setDecimals(6);
        featureHandleBox_->setKeyboardTracking(false);
        featureHandleBox_->setFixedWidth(150);
        featureHandleBox_->setStyleSheet(QStringLiteral("QDoubleSpinBox { background: #202b38; color: #ffcf86; border: 1px solid #eea048; border-radius: 4px; padding: 4px; }"));
        featureHandleBox_->setToolTip(QStringLiteral("Quota con segno: un valore negativo inverte il verso. Invio applica il valore all'anteprima."));
        featureHandleBox_->onReturn = [this] { commitFeatureHandleValue(); };
        QObject::connect(featureHandleBox_, &QDoubleSpinBox::editingFinished, this, [this] { commitFeatureHandleValue(); });
        QObject::connect(featureHandleBox_, &QDoubleSpinBox::valueChanged, this, [this](double) {
            commitFeatureHandleValue();
        });
    }
    const bool angular = definition.feature == BodyFeature::Revolution;
    const QSignalBlocker blocker(featureHandleBox_);
    featureHandleBox_->setRange(angular ? -360.0 : -100000.0, angular ? 360.0 : 100000.0);
    featureHandleBox_->setLengthMeasurement(!angular);
    if (angular) featureHandleBox_->setSuffix(QStringLiteral(" °"));
    if (!featureHandleBox_->hasFocus()) featureHandleBox_->setValue(featureHandleValue());
    update();
}

double CadViewport::featureHandleValue() const {
    return featureHandleDefinition_.feature == BodyFeature::Revolution
        ? featureHandleDefinition_.revolveAngle : featureHandleDefinition_.distance;
}

void CadViewport::commitFeatureHandleValue() {
    if (!featureHandleChanged_ || !featureHandleBox_) return;
    const double value = featureHandleBox_->value();
    if (std::abs(value - featureHandleValue()) < 1e-10) return;
    const auto changed = featureHandleChanged_;
    changed(value);
}

QVector3D CadViewport::featureHandleSketchCenter(const SketchObject &sketch) const {
    // Centro dell'ingombro del profilo nel suo piano, senza riferimenti
    // di costruzione che possono estendersi molto oltre il profilo.
    QPointF minimum, maximum;
    bool hasPoint = false;
    const auto include = [&](const QPointF &p) {
        if (!hasPoint) { minimum = maximum = p; hasPoint = true; }
        else {
            minimum.setX(qMin(minimum.x(), p.x()));
            minimum.setY(qMin(minimum.y(), p.y()));
            maximum.setX(qMax(maximum.x(), p.x()));
            maximum.setY(qMax(maximum.y(), p.y()));
        }
    };
    for (int i = 0; i < sketch.segments.size(); ++i) {
        if (sketch.isConstructionSegment(i)) continue;
        include(sketch.segments.at(i).first);
        include(sketch.segments.at(i).second);
    }
    for (const auto &curve : sketch.curves) {
        if (curve.construction) continue;
        for (const auto &p : curve.samples) include(p);
    }
    return ForgeCad::sketchToDisplay(hasPoint ? (minimum + maximum) * 0.5 : QPointF(), sketch);
}

bool CadViewport::featureHandleGeometry(QVector3D &origin, QVector3D &axis, QVector3D &radial) const {
    const auto &d = featureHandleDefinition_;
    if (!featureHandleChanged_ || d.sketchIndex < 0 || d.sketchIndex >= sketches_.size()) return false;
    const auto &sketch = sketches_.at(d.sketchIndex);
    origin = featureHandleSketchCenter(sketch);
    const auto display = [](const ForgeCad::Kernel::Vec3 &p) { return QVector3D(p.x(), p.y(), p.z()); };
    if (d.feature == BodyFeature::Extrusion) {
        axis = display(ForgeCad::extrusionVector(sketch, 1.0)).normalized();
        radial = {};
        return true;
    }
    QPointF point, direction;
    QString error;
    if (d.revolveAxis == kRevolveAxisReference) {
        ForgeCad::ResolvedRef reference;
        if (!ForgeCad::resolveGeometryRef(d.revolveAxisRef, extrusions_.size(), sketches_, extrusions_, reference, &error)
            || !reference.hasLine || !ForgeCad::sketchLineFromWorld(sketch, reference.point, reference.direction, point, direction, &error)) return false;
    } else if (!ForgeCad::sketchRevolutionAxis(sketch, d.revolveAxis, point, direction, &error)) return false;
    axis = (ForgeCad::sketchToDisplay(point + direction, sketch) - ForgeCad::sketchToDisplay(point, sketch)).normalized();
    const auto axisPoint = ForgeCad::sketchToDisplay(point, sketch);
    const auto foot = axisPoint + axis * QVector3D::dotProduct(origin - axisPoint, axis);
    radial = origin - foot;
    if (radial.lengthSquared() < 1e-10f) {
        int side = ForgeCad::revolutionProfileSide(sketch, point, direction, &error);
        const QPointF perpendicular(-direction.y(), direction.x());
        radial = (ForgeCad::sketchToDisplay(point + perpendicular, sketch) - axisPoint) * (side < 0 ? -1.0f : 1.0f);
    }
    radial.normalize();
    origin = foot;
    return true;
}

QVector3D CadViewport::featureHandleRadialAt(double degrees) const {
    return QQuaternion::fromAxisAndAngle(featureHandleAxis_, float(degrees)).rotatedVector(featureHandleRadial_);
}

bool CadViewport::featureHandleAt(const QPointF &position) const {
    if (!featureHandleChanged_ || refPicking_ || edgePicking_ || documentReadOnly_) return false;
    for (int k = 1; k < featureHandlePath_.size(); ++k)
        if (distanceToSegment(position, featureHandlePath_[k - 1], featureHandlePath_[k]) <= 10) return true;
    return false;
}

void CadViewport::dragFeatureHandle(const QPointF &position) {
    if (!featureHandleChanged_) return;
    const QPointF delta = position - featureHandleLastMouse_;
    double value = featureHandleDragValue_;
    if (featureHandleDefinition_.feature == BodyFeature::Revolution) {
        // Intersezione raggio/piano dell'arco: angolo incrementale senza salto a +/-180.
        QVector3D a, ray;
        viewRay(position.toPoint(), a, ray);
        const double denominator = QVector3D::dotProduct(ray, featureHandleAxis_);
        if (std::abs(denominator) > 0.08) {
            const QVector3D hit = a + ray * (QVector3D::dotProduct(featureHandleOrigin_ - a, featureHandleAxis_) / denominator);
            const QVector3D radial = hit - featureHandleOrigin_;
            if (radial.lengthSquared() > 1e-12f) {
                const double angle = std::atan2(QVector3D::dotProduct(featureHandleAxis_, QVector3D::crossProduct(featureHandleRadial_, radial)),
                                                QVector3D::dotProduct(featureHandleRadial_, radial)) * 180.0 / M_PI;
                if (featureHandleMouseAngleValid_) value += std::remainder(angle - featureHandleMouseAngle_, 360.0);
                featureHandleMouseAngle_ = angle;
                featureHandleMouseAngleValid_ = true;
            }
        } else {
            const QPointF tip = projectWorldPoint(featureHandleOrigin_ + featureHandleRadius_ * featureHandleRadialAt(value));
            QPointF tangent = projectWorldPoint(featureHandleOrigin_ + featureHandleRadius_ * featureHandleRadialAt(value + 1)) - tip;
            const double squared = QPointF::dotProduct(tangent, tangent);
            value += squared > 0.05 ? QPointF::dotProduct(delta, tangent) / squared : -delta.y();
        }
        value = qBound(-360.0, value, 360.0);
    } else {
        const float length = 90.0f * camera_.zoom_ / qMax(1, height());
        const QPointF screen = projectWorldPoint(featureHandleOrigin_ + length * featureHandleAxis_) - projectWorldPoint(featureHandleOrigin_);
        const double squared = QPointF::dotProduct(screen, screen);
        const double movement = squared > 144.0 ? QPointF::dotProduct(delta, screen) * length / squared
                                               : -delta.y() * camera_.zoom_ / qMax(1, height());
        value += movement * (featureHandleDefinition_.extrudeSides == 1 ? 2 : 1);
        value = qBound(-100000.0, value, 100000.0);
    }
    featureHandleLastMouse_ = position;
    featureHandleDragValue_ = value;
    const auto changed = featureHandleChanged_;
    changed(value);
}

void CadViewport::setSectionDragCallback(std::function<void(double)> callback) { sectionDragged_ = std::move(callback); }

QPolygonF CadViewport::sectionHandleShape(bool *disc ) const {
    const QVector<QVector3D> quad = sectionQuad();
    QVector3D center;
    for (const QVector3D &p : quad) center += 0.25f * p;
    const QPointF a = projectWorldPoint(center), b = projectWorldPoint(center + sectionArrowLength() * section_.normal);
    const double length = pointDistance(a, b);
    if (disc) *disc = length < 18.0;
    if (length < 18.0) {
        QPolygonF circle;
        for (int k = 0; k < 24; ++k) circle << a + 10.0 * QPointF(std::cos(k * M_PI / 12.0), std::sin(k * M_PI / 12.0));
        return circle;
    }
    const QPointF u = (b - a) / length, w(-u.y(), u.x());
    const double head = std::min(20.0, 0.45 * length), shaft = 4.5, wing = 11.0;
    const QPointF neck = b - head * u;
    return QPolygonF({a + shaft * w, neck + shaft * w, neck + wing * w, b, neck - wing * w, neck - shaft * w, a - shaft * w});
}

bool CadViewport::sectionHandleAt(const QPoint &position) const {
    if (!section_.enabled || !section_.showPlane || !sectionDragged_) return false;
    const QPointF cursor(position);
    const QPolygonF shape = sectionHandleShape();
    if (shape.containsPoint(cursor, Qt::OddEvenFill)) return true;
    for (int k = 0; k < shape.size(); ++k)
        if (distanceToSegment(cursor, shape.at(k), shape.at((k + 1) % shape.size())) <= 5.0) return true;
    const QVector<QVector3D> quad = sectionQuad();
    for (int k = 0; k < 4; ++k)
        if (distanceToSegment(cursor, projectWorldPoint(quad.at(k)), projectWorldPoint(quad.at((k + 1) % 4))) <= 6.0) return true;
    return false;
}

float CadViewport::sectionArrowLength() const { return 90.0f * camera_.zoom_ / float(qMax(1, height())); }

double CadViewport::sectionDragDelta(const QPoint &mouse) const {
    const float length = sectionArrowLength();
    const QPointF a = projectWorldPoint(section_.point), b = projectWorldPoint(section_.point + length * section_.normal);
    const QPointF screen = b - a;
    const double pixels = std::hypot(screen.x(), screen.y());
    const double unit = double(camera_.zoom_) / double(qMax(1, height()));  // unita' del modello per pixel
    if (pixels < 12.0) return -double(mouse.y()) * unit;
    return (screen.x() * mouse.x() + screen.y() * mouse.y()) / (pixels * pixels) * double(length);
}

void CadViewport::setSectionRefreshCallback(std::function<void()> callback) { sectionRefresh_ = std::move(callback); }

void CadViewport::sceneRangeAlong(const QVector3D &point, const QVector3D &normal, double &lo, double &hi) const {
    updateSceneBounds();
    lo = std::numeric_limits<double>::max();
    hi = -lo;
    const QVector3D n = normal.normalized();
    for (int k = 0; k < 8; ++k) {
        const QVector3D corner(k & 1 ? sceneMax_.x() : sceneMin_.x(), k & 2 ? sceneMax_.y() : sceneMin_.y(), k & 4 ? sceneMax_.z() : sceneMin_.z());
        const double d = double(QVector3D::dotProduct(corner - point, n));
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
}

QVector<QPointF> CadViewport::projectSketchPolyline(const QVector<QPointF> &points, const SketchObject &sketch) const {
    QVector<QPointF> projected;
    projected.reserve(points.size());
    for (const QPointF &point : points) projected.append(projectWorldPoint(mapSketchPoint(point, sketch)));
    return projected;
}

bool CadViewport::isSketchDrawn(int index) const {
    return sketches_.at(index).visible || (sketchMode_ && index == activeSketch_);
}

QVector<QVector3D> CadViewport::sectionQuad(float scale ) const {
    const QVector3D n = section_.normal;
    const QVector3D helper = qAbs(n.x()) < 0.9f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
    const QVector3D u = QVector3D::crossProduct(n, helper).normalized(), v = QVector3D::crossProduct(n, u);
    updateSceneBounds();
    const QVector3D middle = 0.5f * (sceneMin_ + sceneMax_);
    const QVector3D center = middle - QVector3D::dotProduct(middle - section_.point, n) * n;
    const float half = scale * qMax(0.6f * (sceneMax_ - sceneMin_).length(), 1.0f);
    return {center - half * u - half * v, center + half * u - half * v, center + half * u + half * v, center - half * u + half * v};
}

void CadViewport::enableSectionClip() {
    QVector4D plane;
    if (section_.enabled) {
        const QVector3D n = section_.normal;
        plane = QVector4D(-n.x(), -n.y(), -n.z(), QVector3D::dotProduct(n, section_.point));
    }
    sceneRenderer_.setSectionClip(displayCache_, overlayRenderer_, plane, section_.enabled);
}

void CadViewport::disableSectionClip() {
    sceneRenderer_.setSectionClip(displayCache_, overlayRenderer_, QVector4D(), false);
}
