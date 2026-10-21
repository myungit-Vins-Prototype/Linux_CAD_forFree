#include "forgeCad2026_viewport.h"
#include "cad_curve_extract.h"

#include "kernel/fk_cancel.h"
#include "cad_spatial_sketch.h"
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
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QThread>
#include <QPointer>
#include <QDoubleSpinBox>
#include <QRegularExpression>
#include <QFormLayout>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include "cad_menu.h"
#include <QMessageBox>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QThreadPool>
#include <QWheelEvent>
#include <QGuiApplication>
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

namespace {
// Notifica uniforme per i calcoli del viewport. Quelli sincroni vengono
// mostrati in una finestra dalla PdfWindow; quelli in background nella barra
// di stato. La copia della callback rende la guardia sicura anche se durante
// il lavoro cambia il gestore installato nel viewport.
class ScopedWork final {
public:
    ScopedWork(const std::function<void(bool, const QString &, bool)> &callback, const QString &message, bool background = false)
        : callback_(callback), message_(message), background_(background) {
        if (callback_) callback_(true, message_, background_);
    }
    ~ScopedWork() { if (callback_) callback_(false, message_, background_); }
    ScopedWork(const ScopedWork &) = delete;
    ScopedWork &operator=(const ScopedWork &) = delete;
private:
    std::function<void(bool, const QString &, bool)> callback_;
    QString message_;
    bool background_ = false;
};

}

template<class Callback>
void CadViewport::deliverResult(QPointer<CadViewport> guard, Callback callback) {
    // Il worker non usa il viewport. Solo il thread GUI puo' controllare
    // la sua esistenza e applicare il risultato, anche dopo una chiusura.
    QMetaObject::invokeMethod(qApp, [guard, callback = std::move(callback)]() mutable {
        if (guard) callback();
    }, Qt::QueuedConnection);
}

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

void CadViewport::setSplineShapeOptions(const ForgeCad::SplineShapeOptions &options) { splineShapeOptions_ = options; update(); }

void CadViewport::setDrawingTool(DrawingTool tool) {
    const bool changed = drawingTool_ != tool;
    drawingTool_ = tool;
    if (tool == DrawingTool::Select) lastSnapKind_ = SnapKind::None;
    if (changed && toolChangedCallback_) toolChangedCallback_(tool);
    hasPendingPoint_ = false;
    curveControlPoints_.clear();
    tangentStarts_.clear();
    blendFirst_ = -1;
    trimPreview_.clear();
    convertHover_ = {};
    dimensionPreviewValid_ = false;
    dimensionHover_ = {};
    if (!panKeyHeld_) unsetCursor();
    update();
}

void CadViewport::setSketchBlendSize(bool chamfer, double size) { (chamfer ? sketchChamferDistance_ : sketchFilletRadius_) = size; update(); }

double CadViewport::sketchBlendSize(bool chamfer) const { return chamfer ? sketchChamferDistance_ : sketchFilletRadius_; }

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

DocumentState CadViewport::currentDocument() const { return documentState(); }

DocumentState CadViewport::recoveryDocument() const { return documentState(false); }

bool CadViewport::recoverySnapshotReady() const {
    return !interactionLocked_ && !responsiveWork_ && !pointDragActive_ && !draggingControlPoint_
        && bodyDragSegment_<0 && circleRadiusDrag_<0 && !featureHandleDragging_ && !planeResizing_;
}

bool CadViewport::hasPendingSketchChanges() const { return !deferredSketches_.isEmpty(); }

void CadViewport::adoptDocumentIdentity(const ForgeCad::DocumentVersion &identity, const QUuid &base) {
    documentVersion_ = identity; documentBaseCheckinId_ = base;
}

int CadViewport::rebuildAllFeatures() {
    if(documentReadOnly_)return -1;
    if (extrusions_.isEmpty()) return 0;
    recordUndo();
    for (ExtrusionObject &body : extrusions_) body.cachedGeometry = false;
    regenerateAll();
    documentChanged();
    int failed = 0;
    for (const ExtrusionObject &body : extrusions_) failed += !body.suppressed && !body.error.isEmpty() ? 1 : 0;
    return failed;
}

LengthUnit CadViewport::lengthUnit() const { return lengthUnit_; }

void CadViewport::setLengthUnit(LengthUnit unit) {
    if(documentReadOnly_)return;
    if (unit == lengthUnit_) return;
    lengthUnit_ = unit;
    ForgeCad::setDisplayLengthUnit(unit);
    documentChanged();
}

void CadViewport::loadDocument(DocumentState state,
                      const std::function<void(int, int, const QString &)> &progress ) {
    ScopedWork work(workCallback_, QStringLiteral("Caricamento e rigenerazione del documento..."));
    if (sketchMode_) endSketchMode();
    deferredSketches_.clear();
    if(!state.pendingSketchChanges.isEmpty())for(auto &body:state.extrusions)body.cachedGeometry=false;
    activeSketch_ = -1;
    selection_ = {};
    history_.clear();
    adoptDocumentIdentity(state.version, state.baseCheckinId);
    if (state.orientationSet) camera_.orientation_ = state.orientation;
    lengthUnit_ = state.lengthUnitSet ? state.lengthUnit : LengthUnit::Millimeter;
    ForgeCad::setDisplayLengthUnit(lengthUnit_);
    ForgeCad::normalizeModelHistory(state);
    sketches_ = std::move(state.sketches);
    historyPosition_ = -1;
    insertionSourceSize_ = -1;
    extrusions_ = std::move(state.extrusions);
    modelBodies_ = std::move(state.modelBodies);
    std::vector<CurveObject *> curveJobs;
    for (SketchObject &sketch : sketches_) {
        CurveObject *curves = sketch.curves.data();  // detach dei QVector prima dei worker
        for (int i = 0; i < sketch.curves.size(); ++i) curveJobs.push_back(curves + i);
    }
    ForgeCad::Kernel::parallelFor(curveJobs.size(), ForgeCad::Kernel::threadCount(0), [&](std::size_t i) {
        ForgeCad::recalculateCurve(*curveJobs[i], tessellationQuality_);
    });
    const int totalBodies = qMax(1, int(extrusions_.size()));
    if (progress) progress(0, totalBodies, QStringLiteral("Preparazione degli schizzi..."));
    regenerateAll(progress);
    if (progress && extrusions_.isEmpty()) progress(1, 1, QStringLiteral("Preparazione della scena..."));
    restoreDocument(documentState());
    fitAll();
}

void CadViewport::loadPreviewDocument(DocumentState state) {
    activeSketch_ = -1;
    selection_ = {};
    adoptDocumentIdentity(state.version, state.baseCheckinId);
    if (state.orientationSet) camera_.orientation_ = state.orientation;
    lengthUnit_ = state.lengthUnitSet ? state.lengthUnit : LengthUnit::Millimeter;
    ForgeCad::normalizeModelHistory(state);
    sketches_ = std::move(state.sketches);
    historyPosition_ = -1;
    insertionSourceSize_ = -1;
    extrusions_ = std::move(state.extrusions);
    modelBodies_ = std::move(state.modelBodies);
    for (SketchObject &sketch : sketches_)
        for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, 0);
    for (ExtrusionObject &body : extrusions_) {
        if (body.forgeBody) {
            body.solid = !body.forgeBody->isSheet();
            if (body.display.quality != 0) ForgeCad::tessellateGeometry(body, 0, body.display);
        } else {
            body.display = {};
        }
    }
    sceneBoundsDirty_ = true;
    setViewPreset(4);
    fitAll();
    update();
}

QVector<ForgeCad::ExportBody> CadViewport::exportableBodies() const {
    QVector<ForgeCad::ExportBody> result;
    for (int index = 0; index < extrusions_.size(); ++index) {
        const ExtrusionObject &body = extrusions_.at(index);
        if (body.visible && body.error.isEmpty() && (body.forgeBody || body.curve))
            result.append({body.name, body.forgeBody, body.curve, meshColorForFeature(index)});
        // Curve proiettate: le curve oltre la prima come voci a parte.
        for (int k = 1; body.visible && body.error.isEmpty() && k < body.curves.size(); ++k)
            result.append({QStringLiteral("%1 (%2)").arg(body.name).arg(k + 1), nullptr, body.curves.at(k), meshColorForFeature(index)});
    }
    return result;
}

ForgeCad::StlExportOptions CadViewport::suggestedStlOptions() const {
    QVector3D minimum, maximum;
    bool found = false;
    const auto add = [&](const QVector3D &point) {
        if (!found) minimum = maximum = point, found = true;
        else {
            minimum.setX(qMin(minimum.x(), point.x())); minimum.setY(qMin(minimum.y(), point.y())); minimum.setZ(qMin(minimum.z(), point.z()));
            maximum.setX(qMax(maximum.x(), point.x())); maximum.setY(qMax(maximum.y(), point.y())); maximum.setZ(qMax(maximum.z(), point.z()));
        }
    };
    for (const ExtrusionObject &body : extrusions_) {
        if (!body.visible || !body.error.isEmpty() || !body.forgeBody) continue;
        for (const QVector3D &point : body.display.vertices) add(point);
        for (const QVector<QVector3D> &edge : body.display.edges)
            for (const QVector3D &point : edge) add(point);
    }
    const double diagonal = found ? qMax(1e-6, double((maximum - minimum).length())) : 100.0;
    ForgeCad::StlExportOptions options;
    options.maxEdgeLength = diagonal / 50.0;
    options.deflection = diagonal / 2000.0;
    options.angle = 10.0;
    return options;
}

int CadViewport::displayMode() const { return displayMode_; }

bool CadViewport::hiddenEdgesVisible() const { return hiddenEdgesVisible_; }

int CadViewport::lightingPreset() const { return lightingPreset_; }

int CadViewport::tessellationQuality() const { return tessellationQuality_; }

bool CadViewport::wheelZoomEnabled() const { return wheelZoomEnabled_; }

bool CadViewport::referencePlanesVisible() const { return referencePlanesVisible_; }

bool CadViewport::gridVisible() const { return gridVisible_; }

const QVector<SketchObject> &CadViewport::sketches() const { return sketches_; }

const QVector<ExtrusionObject> &CadViewport::extrusions() const { return extrusions_; }

const QVector<ModelBody> &CadViewport::modelBodies() const { return modelBodies_; }

int CadViewport::historyPosition() const { return historyPosition_ < 0 ? int(extrusions_.size()) : historyPosition_; }

void CadViewport::applyHistoryPosition() {
    if (historyPosition_ >= 0) {
        QVector<ExtrusionObject> prefix = extrusions_.mid(0, historyPosition());
        QVector<ModelBody> bodies = modelBodies_;
        ForgeCad::normalizeModelHistory(prefix, bodies);
        for (int i = 0; i < extrusions_.size(); ++i)
            extrusions_[i].visible = i < prefix.size() && prefix.at(i).visible;
    }
    // Un operando consumato resta nella storia, ma non e' un corpo
    // indipendente nella posizione corrente. Nasconderlo anche se l'utente
    // lo aveva mostrato durante il rollback, senza perdere la preferenza.
    const auto roots = storyboardRoots();
    consumedBodies_.clear();
    for (ExtrusionObject &feature : extrusions_)
        if (feature.modelBodyId && roots.value(feature.modelBodyId, feature.modelBodyId) != feature.modelBodyId) {
            consumedBodies_.insert(feature.modelBodyId);
            feature.visible = false;
        }
}

void CadViewport::setHistoryPosition(int position) {
    for (ExtrusionObject &feature : extrusions_)
        for (const ModelBody &body : modelBodies_)
            if (feature.modelBodyId == body.id) feature.visible = feature.featureId == body.tipFeatureId && body.visible;
    historyPosition_ = position >= extrusions_.size() ? -1 : qMax(0, position);
    selection_ = {};
    documentChanged();
}

QVector<int> CadViewport::resultBodiesBefore(int position) const {
    const int end = position < 0 ? historyPosition() : std::min(position, historyPosition());
    QHash<quint64, int> last;
    QSet<quint64> consumed;
    for (int index = 0; index < end; ++index) {
        const ExtrusionObject &feature = extrusions_.at(index);
        if (!feature.modelBodyId || feature.suppressed || !feature.error.isEmpty() || !feature.forgeBody) continue;
        for (int operand : ForgeCad::hiddenOperands(feature))
            if (operand >= 0 && operand < extrusions_.size() && extrusions_.at(operand).modelBodyId != feature.modelBodyId)
                consumed.insert(extrusions_.at(operand).modelBodyId);
        last.insert(feature.modelBodyId, index);
    }
    QVector<int> result;
    for (auto it = last.cbegin(); it != last.cend(); ++it)
        if (!consumed.contains(it.key()) && extrusions_.at(it.value()).forgeBody && ForgeCad::isShapeBody(extrusions_.at(it.value())))
            result.append(it.value());
    std::sort(result.begin(), result.end());
    return result;
}

QHash<quint64, quint64> CadViewport::storyboardRoots() const {
    QHash<quint64, quint64> parent;
    for (int index = 0; index < historyPosition(); ++index) {
        const ExtrusionObject &feature = extrusions_.at(index);
        if (!feature.modelBodyId || feature.suppressed || !feature.error.isEmpty() || !feature.forgeBody) continue;
        for (int operand : ForgeCad::hiddenOperands(feature)) {
            if (operand < 0 || operand >= extrusions_.size()) continue;
            const quint64 consumed = extrusions_.at(operand).modelBodyId;
            if (consumed && consumed != feature.modelBodyId && !parent.contains(consumed)) parent.insert(consumed, feature.modelBodyId);
        }
    }
    QHash<quint64, quint64> roots;
    for (const ModelBody &body : modelBodies_) {
        quint64 root = body.id;
        for (int guard = 0; parent.contains(root) && guard <= modelBodies_.size(); ++guard) root = parent.value(root);
        roots.insert(body.id, root);
    }
    return roots;
}

QVector<int> CadViewport::featureSketches(int index) const {
    return index >= 0 && index < extrusions_.size() ? ForgeCad::sketchesOf(extrusions_.at(index)) : QVector<int>();
}

void CadViewport::setModelBodyVisible(int index, bool visible) {
    if(documentReadOnly_)return;
    if (index < 0 || index >= modelBodies_.size() || modelBodies_.at(index).visible == visible) return;
    recordUndo();
    ModelBody &model = modelBodies_[index];
    model.visible = visible;
    for (ExtrusionObject &feature : extrusions_)
        if (feature.modelBodyId == model.id) feature.visible = feature.featureId == model.tipFeatureId && visible;
    if (!visible && selection_.kind == SceneObjectKind::Extrusion
        && selection_.index >= 0 && selection_.index < extrusions_.size()
        && extrusions_.at(selection_.index).modelBodyId == model.id) selection_ = {};
    documentChanged();
}

QColor CadViewport::modelBodyMeshColor(int index) const {
    if (index < 0 || index >= modelBodies_.size()) return QColor();
    const ModelBody &model = modelBodies_.at(index);
    if (model.meshColor.isValid()) return model.meshColor;
    const int tip = modelBodyTip(index);
    return tip >= 0 && !extrusions_.at(tip).solid ? QColor::fromRgbF(0.20, 0.80, 0.95)
                                                  : QColor::fromRgbF(0.25, 0.65, 0.90);
}

void CadViewport::setModelBodyMeshColor(int index, const QColor &color) {
    if(documentReadOnly_)return;
    if (index < 0 || index >= modelBodies_.size() || !color.isValid()) return;
    const QColor opaque(color.red(), color.green(), color.blue());
    if (modelBodies_.at(index).meshColor == opaque) return;
    recordUndo();
    modelBodies_[index].meshColor = opaque;
    documentChanged();
}

int CadViewport::modelBodyIndexForFeature(int feature) const {
    if (feature < 0 || feature >= extrusions_.size()) return -1;
    const quint64 id = extrusions_.at(feature).modelBodyId;
    for (int index = 0; index < modelBodies_.size(); ++index)
        if (modelBodies_.at(index).id == id) return index;
    return -1;
}

QColor CadViewport::meshColorForFeature(int feature) const {
    const int model = modelBodyIndexForFeature(feature);
    if (model >= 0) return modelBodyMeshColor(model);
    return feature >= 0 && feature < extrusions_.size() && !extrusions_.at(feature).solid
        ? QColor::fromRgbF(0.20, 0.80, 0.95) : QColor::fromRgbF(0.25, 0.65, 0.90);
}

QString CadViewport::renameModelBody(int index, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const QString clean = name.trimmed();
    if (index < 0 || index >= modelBodies_.size()) return QStringLiteral("Corpo non valido.");
    if (clean.isEmpty()) return QStringLiteral("Il nome non puo' essere vuoto.");
    if (modelBodies_.at(index).name == clean) return {};
    recordUndo();
    modelBodies_[index].name = clean;
    documentChanged();
    return {};
}

int CadViewport::modelBodyTip(int index) const {
    if (index < 0 || index >= modelBodies_.size()) return -1;
    if (historyPosition_ >= 0) {
        const quint64 bodyId = modelBodies_.at(index).id;
        for (int feature = historyPosition() - 1; feature >= 0; --feature) {
            const ExtrusionObject &stage = extrusions_.at(feature);
            if (stage.modelBodyId == bodyId && !stage.suppressed && stage.error.isEmpty() && stage.forgeBody) return feature;
        }
        return -1;
    }
    const quint64 featureId = modelBodies_.at(index).tipFeatureId;
    for (int feature = 0; feature < extrusions_.size(); ++feature)
        if (extrusions_.at(feature).featureId == featureId) return feature;
    return -1;
}

void CadViewport::deleteModelBody(int index) {
    if (index < 0 || index >= modelBodies_.size()) return;
    const quint64 id = modelBodies_.at(index).id;
    QVector<SceneSelection> features;
    for (int feature = 0; feature < extrusions_.size(); ++feature)
        if (extrusions_.at(feature).modelBodyId == id) features.append({SceneObjectKind::Extrusion, feature, -1});
    deleteObjects(features);
}

QString CadViewport::setFeatureSuppressed(int index, bool suppressed) {
    if (index < 0 || index >= extrusions_.size()) return QStringLiteral("Feature non valida.");
    if (!extrusions_.at(index).modelBodyId) return QStringLiteral("Questa geometria di riferimento non appartiene a una storyboard.");
    return setFeaturesSuppressed({index}, suppressed);
}

QString CadViewport::setFeaturesSuppressed(const QVector<int> &indices, bool suppressed) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    QVector<int> list;
    for (int index : indices)
        if (index >= 0 && index < extrusions_.size() && extrusions_.at(index).modelBodyId
            && extrusions_.at(index).suppressed != suppressed && !list.contains(index))
            list.append(index);
    if (list.isEmpty()) return {};
    std::sort(list.begin(), list.end());
    QHash<quint64, bool> bodyVisible;
    for (int index : list) {
        const quint64 bodyId = extrusions_.at(index).modelBodyId;
        bool visible = true;
        for (const ModelBody &body : modelBodies_)
            if (body.id == bodyId) { visible = body.visible; break; }
        bodyVisible.insert(bodyId, visible);
    }
    recordUndo();
    for (int index : list) {
        ExtrusionObject &feature = extrusions_[index];
        const quint64 bodyId = feature.modelBodyId;
        const QVector<int> consumed = ForgeCad::hiddenOperands(feature);
        feature.suppressed = suppressed;
        for (int operand : consumed) {
            if (operand < 0 || operand >= extrusions_.size() || extrusions_.at(operand).modelBodyId == bodyId) continue;
            bool consumedElsewhere = false;
            if (suppressed) {
                for (int other = 0; other < extrusions_.size() && !consumedElsewhere; ++other)
                    consumedElsewhere = other != index && !extrusions_.at(other).suppressed
                                     && ForgeCad::hiddenOperands(extrusions_.at(other)).contains(operand);
            }
            extrusions_[operand].visible = suppressed && !consumedElsewhere;
        }
    }
    if (list.size() == 1) {
        rebuildBody(extrusions_[list.first()], list.first());
        regenerateAfter(list.first());
    } else {
        regenerateAll();
    }
    int lastTip = -1;
    for (auto it = bodyVisible.cbegin(); it != bodyVisible.cend(); ++it) {
        int tip = -1;
        for (int candidate = extrusions_.size() - 1; candidate >= 0; --candidate)
            if (extrusions_.at(candidate).modelBodyId == it.key() && !extrusions_.at(candidate).suppressed) { tip = candidate; break; }
        for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
            if (extrusions_.at(candidate).modelBodyId == it.key()) extrusions_[candidate].visible = candidate == tip && it.value();
        if (tip >= 0) lastTip = tip;
    }
    selection_ = lastTip >= 0 ? SceneSelection{SceneObjectKind::Extrusion, lastTip, -1} : SceneSelection{};
    documentChanged();
    return {};
}

void CadViewport::deleteFeature(int index) { deleteFeatures({index}); }

void CadViewport::deleteFeatures(const QVector<int> &indices) {
    if(documentReadOnly_)return;
    QVector<int> list;
    for (int index : indices)
        if (index >= 0 && index < extrusions_.size() && extrusions_.at(index).modelBodyId && !list.contains(index)) list.append(index);
    if (list.isEmpty()) return;
    std::sort(list.begin(), list.end(), std::greater<int>());  // dall'ultima: gli indici prima non cambiano
    QHash<quint64, bool> bodyVisible;
    for (int index : list) {
        const quint64 bodyId = extrusions_.at(index).modelBodyId;
        bool visible = true;
        for (const ModelBody &body : modelBodies_)
            if (body.id == bodyId) { visible = body.visible; break; }
        bodyVisible.insert(bodyId, visible);
    }
    recordUndo();
    for (int index : list) {
        const quint64 bodyId = extrusions_.at(index).modelBodyId;
        int previous = -1;
        for (int candidate = index - 1; candidate >= 0; --candidate)
            if (extrusions_.at(candidate).modelBodyId == bodyId) { previous = candidate; break; }
        // La base implicita del ramo prosegue dallo stadio precedente. I
        // riferimenti geometrici espliciti restano invece invalidi e vengono
        // segnalati dalla rigenerazione, senza cancellare le feature dipendenti.
        for (int candidate = index + 1; candidate < extrusions_.size(); ++candidate) {
            ExtrusionObject &dependent = extrusions_[candidate];
            if (dependent.modelBodyId != bodyId) continue;
            if (dependent.firstBody == index) dependent.firstBody = previous;
            for (int &merged : dependent.mergeBodies)
                if (merged == index) merged = previous;
        }
        removeBodies({index});
    }
    for (auto it = bodyVisible.cbegin(); it != bodyVisible.cend(); ++it) {
        int tip = -1;
        for (int candidate = extrusions_.size() - 1; candidate >= 0; --candidate)
            if (extrusions_.at(candidate).modelBodyId == it.key() && !extrusions_.at(candidate).suppressed) { tip = candidate; break; }
        for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
            if (extrusions_.at(candidate).modelBodyId == it.key()) extrusions_[candidate].visible = candidate == tip && it.value();
    }
    regenerateAll();
    selection_ = {};
    documentChanged();
}

QString CadViewport::moveFeature(int index, int direction) {
    if (index < 0 || index >= extrusions_.size() || !extrusions_.at(index).modelBodyId)
        return QStringLiteral("Feature non valida.");
    // Un passo nella storia (le feature di tutti i corpi, nell'ordine di calcolo).
    QVector<int> chain;
    for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
        if (extrusions_.at(candidate).modelBodyId) chain.append(candidate);
    const int position = chain.indexOf(index), destination = position + (direction < 0 ? -1 : 1);
    if (position < 0 || destination < 0 || destination >= chain.size()) return QStringLiteral("Non ci sono altre feature in quella direzione.");
    return moveFeatureTo(index, chain.at(destination));
}

QString CadViewport::moveFeatureTo(int index, int target) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    // La storia e' una sola sequenza: il bersaglio puo' essere una feature
    // di un altro corpo, conta solo che le dipendenze restino prima.
    if (index < 0 || index >= extrusions_.size() || target < 0 || target >= extrusions_.size()
        || !extrusions_.at(index).modelBodyId || !extrusions_.at(target).modelBodyId)
        return QStringLiteral("Feature non valida.");
    if (index == target) return {};
    const quint64 bodyId = extrusions_.at(index).modelBodyId;
    int first = -1;
    for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
        if (extrusions_.at(candidate).modelBodyId == bodyId) { first = candidate; break; }
    if (index == first) return QStringLiteral("La feature che crea il corpo non si sposta: sposta le feature che la seguono.");
    bool bodyVisible = true;
    for (const ModelBody &body : modelBodies_)
        if (body.id == bodyId) { bodyVisible = body.visible; break; }

    QVector<int> order;
    order.reserve(extrusions_.size());
    for (int old = 0; old < extrusions_.size(); ++old)
        if (old != index) order.append(old);
    // `target` e' una feature, non una posizione nel vettore: gli stadi
    // di corpi diversi possono essere intercalati. Salendo si inserisce
    // prima del bersaglio, scendendo dopo, come indica il drag nell'albero.
    int insertion = order.indexOf(target);
    if (insertion < 0) return QStringLiteral("La feature di destinazione non esiste piu'.");
    if (index < target) ++insertion;
    order.insert(insertion, index);
    QVector<int> map(extrusions_.size(), -1);
    QVector<ExtrusionObject> reordered;
    reordered.reserve(extrusions_.size());
    for (int old : order) {
        map[old] = reordered.size();
        reordered.append(extrusions_.at(old));
    }
    const auto mapped = [&](int old) { return old >= 0 ? map.value(old, -1) : old; };
    const auto remapRef = [&](GeometryRef &ref) {
        if (ForgeCad::isBodyRef(ref)) ref.index = mapped(ref.index);
    };
    for (ExtrusionObject &feature : reordered) {
        feature.firstBody = mapped(feature.firstBody);
        feature.secondBody = mapped(feature.secondBody);
        for (int &tool : feature.booleanTools) tool = mapped(tool);
        for (int &merged : feature.mergeBodies) merged = mapped(merged);
        for (GeometryRef &ref : feature.datum.refs) remapRef(ref);
        for (GeometryRef &ref : feature.pattern.refs) remapRef(ref);
        for (QVector<GeometryRef> *refs : {&feature.ruledFirst, &feature.ruledSecond, &feature.planarRefs, &feature.loftStartFaces, &feature.loftEndFaces, &feature.loftSections, &feature.loftEdgeGuides})
            for (GeometryRef &ref : *refs) remapRef(ref);
        remapRef(feature.extentRef);
        remapRef(feature.move.axis);
        remapRef(feature.revolveAxisRef);
        remapRef(feature.draftNeutral);
        remapRef(feature.thickenDirection);
        remapRef(feature.curveExtraction.source);
        remapRef(feature.curveExtraction.tool);
    }

    for (const ExtrusionObject &feature : reordered)
        if (feature.modelBodyId == bodyId) {
            if (feature.featureId != extrusions_.at(first).featureId)
                return QStringLiteral("Spostamento non consentito: \"%1\" verrebbe prima di \"%2\", che crea il suo corpo.")
                    .arg(extrusions_.at(index).name, extrusions_.at(first).name);
            break;
        }
    // Le basi interne del corpo sono implicite nella storyboard: dopo il
    // riordino ogni modificatore prende lo stadio immediatamente precedente.
    int previous = -1;
    for (int current = 0; current < reordered.size(); ++current) {
        ExtrusionObject &feature = reordered[current];
        if (feature.modelBodyId != bodyId) continue;
        if (previous >= 0) {
            if (feature.operation >= 0 || feature.feature == BodyFeature::Blend || feature.feature == BodyFeature::SheetTrim
                || feature.feature == BodyFeature::SheetExtend || feature.feature == BodyFeature::Scale
                || (feature.feature == BodyFeature::Transform && !feature.move.copy) || feature.feature == BodyFeature::Pattern
                || feature.feature == BodyFeature::Sew || feature.feature == BodyFeature::DeleteFace
                || feature.feature == BodyFeature::Draft || feature.feature == BodyFeature::Shell || feature.feature == BodyFeature::Thread
                || (feature.feature == BodyFeature::Thicken && feature.offsetFaces.isEmpty())) {
                feature.firstBody = previous;
            } else if (ForgeCad::mergingFeature(feature) && feature.mergeOperation != 0) {
                bool replaced = false;
                for (int &merged : feature.mergeBodies)
                    if (merged >= 0 && reordered.at(merged).modelBodyId == bodyId) { merged = previous; replaced = true; break; }
                if (!replaced) feature.mergeBodies.prepend(previous);
            }
        }
        previous = current;
    }
    for (int current = 0; current < reordered.size(); ++current)
        for (int operand : ForgeCad::bodyOperands(reordered.at(current)))
            if (operand >= current)
                return QStringLiteral("Spostamento non consentito: \"%1\" deve restare dopo una feature da cui dipende.")
                    .arg(reordered.at(current).name);

    recordUndo();
    extrusions_ = std::move(reordered);
    int tip = -1;
    for (int candidate = extrusions_.size() - 1; candidate >= 0; --candidate)
        if (extrusions_.at(candidate).modelBodyId == bodyId && !extrusions_.at(candidate).suppressed) { tip = candidate; break; }
    for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
        if (extrusions_.at(candidate).modelBodyId == bodyId) extrusions_[candidate].visible = candidate == tip && bodyVisible;
    for (SketchObject &sketch : sketches_)
        if (sketch.datumPlane >= 0) sketch.datumPlane = mapped(sketch.datumPlane);
    regenerateAll();
    const int moved = map.value(index, -1);
    selection_ = moved >= 0 ? SceneSelection{SceneObjectKind::Extrusion, moved, -1} : SceneSelection{};
    documentChanged();
    return {};
}

bool CadViewport::canUndo() const { return !documentReadOnly_&&history_.canUndo(); }

bool CadViewport::canRedo() const { return !documentReadOnly_&&history_.canRedo(); }

void CadViewport::undo() {
    if(documentReadOnly_)return;
    ScopedWork work(workCallback_, QStringLiteral("Ripristino dello stato precedente..."));
    DocumentState state = documentState();
    if (history_.undo(state)) {state.version=documentVersion_;state.baseCheckinId=documentBaseCheckinId_;restoreDocument(std::move(state));}
}

void CadViewport::redo() {
    if(documentReadOnly_)return;
    ScopedWork work(workCallback_, QStringLiteral("Ripristino dello stato successivo..."));
    DocumentState state = documentState();
    if (history_.redo(state)) {state.version=documentVersion_;state.baseCheckinId=documentBaseCheckinId_;restoreDocument(std::move(state));}
}

bool CadViewport::isObjectVisible(SceneObjectKind kind, int index) const {
    if (kind == SceneObjectKind::Sketch && index >= 0 && index < sketches_.size())
        return sketches_.at(index).visible;
    if (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size())
        return extrusions_.at(index).visible;
    return false;
}

void CadViewport::setObjectVisible(SceneObjectKind kind, int index, bool visible) {
    if(documentReadOnly_)return;
    if (isObjectVisible(kind, index) == visible) return;
    if (kind != SceneObjectKind::Sketch && kind != SceneObjectKind::Extrusion) return;
    if ((kind == SceneObjectKind::Sketch && (index < 0 || index >= sketches_.size()))
        || (kind == SceneObjectKind::Extrusion && (index < 0 || index >= extrusions_.size()))) return;
    recordUndo();
    if (kind == SceneObjectKind::Sketch) sketches_[index].visible = visible;
    else extrusions_[index].visible = visible;
    if (!visible) {
        if (selection_ == SceneSelection{kind, index, -1}) selection_ = {};
        if (hover_ == SceneSelection{kind, index, -1}) hover_ = {};
    }
    documentChanged();
}

void CadViewport::setObjectsVisible(const QVector<SceneSelection> &objects, bool visible) {
    if(documentReadOnly_)return;
    QVector<SceneSelection> changed;
    for (const SceneSelection &object : objects)
        if ((object.kind == SceneObjectKind::Sketch && object.index >= 0 && object.index < sketches_.size())
            || (object.kind == SceneObjectKind::Extrusion && object.index >= 0 && object.index < extrusions_.size()))
            if (isObjectVisible(object.kind, object.index) != visible) changed.append(object);
    if (changed.isEmpty()) return;
    recordUndo();
    for (const SceneSelection &object : changed) {
        if (object.kind == SceneObjectKind::Sketch) sketches_[object.index].visible = visible;
        else extrusions_[object.index].visible = visible;
    }
    if (!visible) {
        if (changed.contains(selection_)) selection_ = {};
        if (changed.contains(hover_)) hover_ = {};
    }
    documentChanged();
}

void CadViewport::showAllObjects() {
    if(documentReadOnly_)return;
    bool anyHidden = false;
    for (const SketchObject &sketch : sketches_) anyHidden = anyHidden || !sketch.visible;
    for (const ExtrusionObject &extrusion : extrusions_) anyHidden = anyHidden || !extrusion.visible;
    if (!anyHidden) return;
    recordUndo();
    for (SketchObject &sketch : sketches_) sketch.visible = true;
    for (ExtrusionObject &extrusion : extrusions_) extrusion.visible = true;
    documentChanged();
}

SceneSelection CadViewport::selection() const { return selection_; }

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
    selectedObjects_.clear();
    if (kind == SceneObjectKind::Plane) selectedPlane_ = index;
    if (kind == SceneObjectKind::Extrusion && wholeBody) setBodyHighlight(index);
    else setFeatureHighlights(kind == SceneObjectKind::Extrusion ? QVector<int>{index} : QVector<int>());
    update();
}

QStringList CadViewport::extrusionNames() const {
    QStringList names;
    for (const ExtrusionObject &extrusion : extrusions_) names.append(extrusion.name);
    return names;
}

QString CadViewport::createBoolean(BooleanOperation operation, int firstIndex, const QVector<int> &tools, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (firstIndex < 0 || firstIndex >= extrusions_.size() || tools.isEmpty()) return QStringLiteral("Scegli il primo oggetto e almeno uno strumento.");
    for (int tool : tools) {
        if (tool < 0 || tool >= extrusions_.size()) return QStringLiteral("Oggetti non validi.");
        if (tool == firstIndex) return QStringLiteral("Il primo oggetto non puo' essere anche uno strumento.");
        if (tools.count(tool) > 1) return QStringLiteral("Uno strumento e' scelto due volte.");
    }
    const ExtrusionObject &first = extrusions_.at(firstIndex);
    for (int tool : tools) {
        const QString invalid = booleanOperandsError(operation, first, extrusions_.at(tool));
        if (!invalid.isEmpty()) return invalid;
    }
    ExtrusionObject result;
    result.name = name;
    result.plane = first.plane;
    result.operation = int(operation);
    result.firstBody = firstIndex;
    result.secondBody = tools.first();
    result.booleanTools = tools.mid(1);
    rebuildBody(result, int(extrusions_.size()));
    if (!ForgeCad::hasGeometry(result)) return result.error;
    recordUndo();
    extrusions_[firstIndex].visible = false;
    for (int tool : tools) extrusions_[tool].visible = false;
    extrusions_.append(result);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QVector<int> CadViewport::mergeCandidates(int index) const {
    QSet<quint64> hidden;
    for (const ModelBody &model : modelBodies_)
        if (!model.visible) hidden.insert(model.id);
    QVector<int> candidates;
    for (int body : resultBodiesBefore(index)) {
        const ExtrusionObject &other = extrusions_.at(body);
        if (!other.solid) continue;
        if (other.modelBodyId ? hidden.contains(other.modelBodyId) : !other.visible) continue;
        candidates.append(body);
    }
    return candidates;
}

ExtrusionObject CadViewport::withMergeCandidates(ExtrusionObject definition, int index) const {
    definition.mergeProbe = false;
    if (ForgeCad::mergingFeature(definition) && definition.mergeOperation != 0 && definition.mergeAuto) {
        definition.mergeBodies = mergeCandidates(index);
        definition.mergeProbe = true;
    }
    return definition;
}

bool CadViewport::unchangedFeature(const ExtrusionObject &definition, int index) const {
    if (index < 0 || index >= extrusions_.size()) return false;
    const ExtrusionObject &existing = extrusions_.at(index);
    if (existing.suppressed || !existing.error.isEmpty() || !existing.forgeBody || ForgeCad::isCurveBody(existing) || ForgeCad::isDatumBody(existing))
        return false;
    ExtrusionObject normalized = definition;
    if (normalized.mergeProbe) normalized.mergeBodies = existing.mergeBodies;
    normalized.mergeProbe = false;
    ExtrusionObject reference = existing;
    reference.mergeProbe = false;
    if (previewKey(normalized, index) == previewKey(reference, index)) return true;
    // Raccordi: gli spigoli scelti di nuovo nella vista hanno punti diversi
    // da quelli salvati anche quando sono gli stessi spigoli; conta lo
    // spigolo (e la faccia) che indicano sulla base.
    if (definition.operation >= 0 || definition.feature != BodyFeature::Blend || existing.feature != BodyFeature::Blend) return false;
    normalized.blendEdges.clear();
    reference.blendEdges.clear();
    if (previewKey(normalized, index) != previewKey(reference, index)) return false;
    return sameBlendEdges(definition.blendEdges, existing);
}

bool CadViewport::sameBlendEdges(const QVector<EdgePoint> &edges, const ExtrusionObject &blend) const {
    if (blend.firstBody < 0 || blend.firstBody >= extrusions_.size() || !extrusions_.at(blend.firstBody).forgeBody) return false;
    const ExtrusionObject &base = extrusions_.at(blend.firstBody);
    ForgeCad::Kernel::Box box;
    for (ForgeCad::Kernel::VertexId v : base.forgeBody->vertices()) box.add(base.forgeBody->vertex(v).point);
    const double reach = 1e-3 * std::max(1.0, box.diagonal());
    const bool sameState = blend.blendBaseFeature != 0 && blend.blendBaseFeature == base.featureId;
    std::vector<ForgeCad::Kernel::EdgeId> picked, stored;
    if (!ForgeCad::resolveBlendEdges(*base.forgeBody, edges, reach, picked, ForgeCad::ReferenceState::Same)
        || !ForgeCad::resolveBlendEdges(*base.forgeBody, blend.blendEdges, reach, stored,
                                        sameState ? ForgeCad::ReferenceState::Same : ForgeCad::ReferenceState::Other))
        return false;
    const auto indices = [](const std::vector<ForgeCad::Kernel::EdgeId> &ids) {
        std::set<int> result;
        for (ForgeCad::Kernel::EdgeId id : ids) result.insert(id.index);
        return result;
    };
    return !picked.empty() && indices(picked) == indices(stored);
}

QString CadViewport::booleanOperandsError(BooleanOperation operation, const ExtrusionObject &first, const ExtrusionObject &second) {
    if (first.solid && second.solid) return {};
    if (!first.solid && !second.solid) return QStringLiteral("Serve almeno un solido chiuso: tra due superfici non si fanno booleane.");
    if (operation == BooleanOperation::Union) return QStringLiteral("L'unione richiede due solidi chiusi.");
    if (operation == BooleanOperation::Difference && first.solid)
        return QStringLiteral("Da un solido si puo' sottrarre solo un solido (una superficie non ha volume).");
    return {};
}

QString CadViewport::updateBody(int index, const ExtrusionObject &definition) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (index < 0 || index >= extrusions_.size()) return QStringLiteral("Corpo non valido.");
    if (definition.operation >= 0) {
        if (definition.firstBody < 0 || definition.secondBody < 0 || definition.firstBody >= index || definition.secondBody >= index)
            return QStringLiteral("Operandi della booleana non validi.");
        for (int tool : QVector<int>{definition.secondBody} + definition.booleanTools) {
            if (tool < 0 || tool >= index || tool == definition.firstBody) return QStringLiteral("Operandi della booleana non validi.");
            const QString invalid = booleanOperandsError(BooleanOperation(definition.operation), extrusions_.at(definition.firstBody), extrusions_.at(tool));
            if (!invalid.isEmpty()) return invalid;
        }
    }
    ExtrusionObject candidate = withMergeCandidates(definition, index);
    candidate.visible = extrusions_.at(index).visible;
    const bool sameBlendPreview = candidate.operation < 0 && candidate.feature == BodyFeature::Blend
        && preview_.key == previewKey(candidate, index);
    if (sameBlendPreview && !unchangedFeature(candidate, index) && (!preview_.valid || !preview_.geometry))
        return preview_.error.isEmpty() ? QStringLiteral("Attendi che l'anteprima sia pronta.") : preview_.error;
    const bool unchanged = unchangedFeature(candidate, index);
    if (unchanged) {
        // Nessun valore cambiato: il corpo e quelli che ne dipendono restano come sono.
        const ExtrusionObject &existing = extrusions_.at(index);
        candidate.mergeBodies = existing.mergeBodies;
        candidate.forgeBody = existing.forgeBody;
        candidate.solid = existing.solid;
        candidate.display = existing.display;
        candidate.error.clear();
    } else if (sameBlendPreview) {
        candidate.forgeBody = preview_.geometry;
        candidate.solid = true;
        // Il candidato e' una copia della feature: se prima era fallita ne
        // conserverebbe il messaggio, e la storyboard continuerebbe a
        // mostrare lo stadio precedente come risultato del corpo.
        candidate.error.clear();
        candidate.display = preview_.resultDisplay;
        if (candidate.display.vertices.isEmpty() && candidate.display.edges.isEmpty())
            ForgeCad::tessellateGeometry(candidate, tessellationQuality_, candidate.display);
        candidate.display.constructionCurves.clear();
    } else {
        // La stessa definizione dell'anteprima (guscio, offset...): il suo risultato.
        QString previewError;
        if (takePreviewResult(candidate, index, &previewError)) {
            if (!previewError.isEmpty()) return previewError;
        } else {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            rebuildBody(candidate, index);
            QApplication::restoreOverrideCursor();
        }
    }
    candidate.mergeProbe = false;
    if (!ForgeCad::hasGeometry(candidate)) return candidate.error;
    recordUndo();
    // Gli operandi che la funzione non usa piu' tornano visibili, quelli nuovi si nascondono.
    const QVector<int> before = ForgeCad::hiddenOperands(extrusions_.at(index)), after = ForgeCad::hiddenOperands(candidate);
    for (int body : before)
        if (!after.contains(body) && body >= 0 && body < extrusions_.size()) extrusions_[body].visible = true;
    for (int body : after)
        if (!before.contains(body) && body >= 0 && body < index) extrusions_[body].visible = false;
    // La modifica stacca la feature dal corpo di cui era uno stadio (una
    // sweep o un'estrusione che non si fonde piu', una cucitura con
    // un'altra prima superficie): il corpo logico si riassegna, altrimenti
    // il corpo di prima resterebbe nascosto come stadio intermedio.
    {
        const quint64 before = ForgeCad::inheritedModelBody(extrusions_, extrusions_.at(index));
        if (before != 0 && before != ForgeCad::inheritedModelBody(extrusions_, candidate)) candidate.modelBodyId = 0;
    }
    extrusions_[index] = candidate;
    if (!unchanged) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        regenerateAfter(index);
        QApplication::restoreOverrideCursor();
    }
    selection_ = {SceneObjectKind::Extrusion, index, -1};
    documentChanged();
    return {};
}

QString CadViewport::editSegmentDimension(int index ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (!sketchMode_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return QStringLiteral("Entra in modalita' schizzo.");
    if (index < 0)
        for (const SketchElementSelection &element : sketchSelections_)
            if (element.kind == 0) {
                index = element.index;
                break;
            }
    // Senza segmenti scelti: la quota di un cerchio, arco o poligono selezionato.
    if (index < 0)
        for (const SketchElementSelection &element : sketchSelections_)
            if (element.kind == 1) return editCurveDimension(element.index);
    SketchObject &sketch = sketches_[activeSketch_];
    if (index < 0 || index >= sketch.segments.size()) return QStringLiteral("Seleziona prima un segmento dello schizzo.");
    const SketchSegment segment = sketch.segments.at(index);
    const QPointF delta = segment.second - segment.first;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Quota del segmento"));
    auto *form = new QFormLayout(&dialog);
    auto *lengthBox = new ForgeCad::ExpressionSpinBox(&dialog);
    lengthBox->setDecimals(6);
    lengthBox->setRange(1e-6, 100000.0);
    lengthBox->setValue(pointLength(delta));
    auto *angleBox = new ForgeCad::ExpressionSpinBox(&dialog);
    angleBox->setDecimals(6);
    angleBox->setRange(-360.0, 360.0);
    const double oldAngle = std::atan2(delta.y(), delta.x()) * 180.0 / M_PI;
    angleBox->setValue(oldAngle);
    angleBox->setSuffix(QStringLiteral(" \u00B0"));
    form->addRow(QStringLiteral("Lunghezza:"), lengthBox);
    form->addRow(QStringLiteral("Angolo rispetto all'asse X:"), angleBox);
    form->addRow(wrappedNote(QStringLiteral("Il primo estremo resta fermo; la lunghezza diventa una quota (vincolo)\n"
                                           "e gli altri vincoli dello schizzo restano soddisfatti."), &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return {};
    const double angle = angleBox->value() * M_PI / 180.0, length = lengthBox->value();
    const QPointF second = segment.first + QPointF(std::cos(angle), std::sin(angle)) * length;
    const DocumentState snapshot = documentState();
    const SketchObject before = sketch;
    moveSketchPoint(sketch, index, segment.second, second - segment.second, segment.first);
    // La lunghezza e' una quota: il vincolo Distanza del segmento (nuovo o aggiornato).
    const ConstraintRef self{0, index, -1};
    bool found = false;
    for (SketchConstraint &c : sketch.geometricConstraints)
        if (c.type == ConstraintType::Distance && c.first == self && c.second.kind < 0) {
            c.value = length;
            found = true;
        }
    if (!found) {
        SketchConstraint c = ForgeCad::makeConstraint(sketch, ConstraintType::Distance, {self});
        c.value = length;
        sketch.geometricConstraints.append(c);
    }
    // Le quote d'angolo del segmento seguono la direzione scelta.
    for (SketchConstraint &c : sketch.geometricConstraints)
        if (c.type == ConstraintType::Angle && (c.first == self || c.second == self)) c.value = ForgeCad::currentMeasure(sketch, c);
    QVector<ForgeCad::PointTarget> targets = targetsAt(sketch, segment.first);
    targets += targetsAt(sketch, second);
    QString failure;
    if (!solveActive(targets, before, &failure)) return failure;
    history_.record(snapshot);
    sketchEdited();
    return {};
}

QString CadViewport::beginDimensionTool() {
    if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo.");
    const bool preselected = !constraintSelection().isEmpty();
    selectedConstraints_.clear();
    dimensionOrientation_ = 0;
    setDrawingTool(DrawingTool::Dimension);
    refreshDimensionPreview(screenToSketchPoint(mapFromGlobal(QCursor::pos())));
    showStatus(preselected && dimensionPreviewValid_
                   ? QStringLiteral("Quota: clic nel vuoto per metterla (H/V/O/A: orientamento), clic su altre entita' per cambiarle")
                   : QStringLiteral("Quota: scegli le entita' da quotare (punti, segmenti, cerchi, archi), poi clic nel vuoto per metterla"));
    return {};
}

void CadViewport::setDimensionOrientation(int orientation) {
    dimensionOrientation_ = qBound(0, orientation, 3);
    refreshDimensionPreview(screenToSketchPoint(mapFromGlobal(QCursor::pos())));
    static const char *names[] = {"automatica", "obliqua (allineata)", "orizzontale", "verticale"};
    showStatus(QStringLiteral("Quota %1").arg(QLatin1String(names[dimensionOrientation_])));
    update();
}

bool CadViewport::dimensionRefAt(const QPointF &point, ConstraintRef &ref) const {
    const SketchObject *sketch = activeSketchObject();
    if (!sketch) return false;
    double best = pickTolerance();
    bool found = false;
    const auto consider = [&](const QPointF &candidate, const ConstraintRef &candidateRef) {
        const double d = pointDistance(point, candidate);
        if (d < best) {
            best = d;
            ref = candidateRef;
            found = true;
        }
    };
    for (int s = 0; s < sketch->segments.size(); ++s) {
        consider(sketch->segments.at(s).first, {0, s, 0});
        consider(sketch->segments.at(s).second, {0, s, 1});
    }
    for (int c = 0; c < sketch->curves.size(); ++c) {
        const CurveObject &curve = sketch->curves.at(c);
        for (int k = 0; k < curve.controlPoints.size(); ++k) {
            // Il punto del raggio di un cerchio non e' un punto notevole.
            if (curve.tool == DrawingTool::Circle && k == 1) continue;
            if (curve.tool == DrawingTool::Polygon && k > 0) continue;
            consider(curve.controlPoints.at(k), {1, c, k});
        }
    }
    consider(QPointF(0.0, 0.0), {2, 0, -1});
    if (found) return true;
    const SketchElementSelection hit = findSketchElement(point);
    if (hit.kind < 0) return false;
    ref = {hit.kind, hit.index, -1};
    return true;
}

bool CadViewport::buildDimension(const QVector<ConstraintRef> &input, const QPointF &cursor, SketchConstraint &result) {
    const SketchObject *sketch = activeSketchObject();
    if (!sketch || input.isEmpty() || input.size() > 2) return false;
    const auto isCircle = [&](const ConstraintRef &r) {
        if (r.kind != 1 || r.point >= 0 || r.element < 0 || r.element >= sketch->curves.size()) return false;
        const DrawingTool tool = sketch->curves.at(r.element).tool;
        return tool == DrawingTool::Circle || tool == DrawingTool::Arc || tool == DrawingTool::Polygon || tool == DrawingTool::Ellipse;
    };
    const auto isLine = [](const ConstraintRef &r) { return (r.kind == 0 && r.point < 0) || (r.kind == 2 && (r.element == 1 || r.element == 2)); };
    const auto isPoint = [](const ConstraintRef &r) { return r.point >= 0 || (r.kind == 2 && r.element == 0); };
    QVector<ConstraintRef> refs = input;
    // Con un'altra entita' un cerchio o un arco vale per il suo centro.
    if (refs.size() == 2)
        for (ConstraintRef &r : refs)
            if (isCircle(r)) r.point = 0;
    const auto make = [&](ConstraintType type) {
        if (!ForgeCad::applicableConstraints(*sketch, refs).contains(type)) return false;
        result = ForgeCad::makeConstraint(*sketch, type, refs);
        result.placement = cursor;
        result.placed = true;
        return true;
    };
    // Quota lineare tra due punti (o gli estremi di un segmento): l'orientamento.
    const auto linear = [&](const QPointF &p, const QPointF &q) {
        int orientation = dimensionOrientation_;
        if (orientation == 0) {
            const double x0 = std::min(p.x(), q.x()), x1 = std::max(p.x(), q.x());
            const double y0 = std::min(p.y(), q.y()), y1 = std::max(p.y(), q.y());
            const bool insideX = cursor.x() >= x0 && cursor.x() <= x1, insideY = cursor.y() >= y0 && cursor.y() <= y1;
            orientation = insideX && !insideY ? 2 : insideY && !insideX ? 3 : 1;
        }
        const double tiny = 1e-9 * std::max(1.0, pointDistance(p, q));
        if (orientation == 2 && std::fabs(q.x() - p.x()) <= tiny) orientation = 1;
        if (orientation == 3 && std::fabs(q.y() - p.y()) <= tiny) orientation = 1;
        return make(orientation == 2 ? ConstraintType::HorizontalDistance : orientation == 3 ? ConstraintType::VerticalDistance : ConstraintType::Distance);
    };
    if (refs.size() == 1) {
        const ConstraintRef &r = refs.first();
        if (r.kind == 0 && r.point < 0) {
            const SketchSegment &segment = sketch->segments.at(r.element);
            return linear(segment.first, segment.second);
        }
        if (isCircle(r)) {
            const bool arc = sketch->curves.at(r.element).tool == DrawingTool::Arc;
            return make(arc ? ConstraintType::Radius : ConstraintType::Diameter) || make(ConstraintType::Radius);
        }
        return false;
    }
    const ConstraintRef &a = refs.at(0), &b = refs.at(1);
    if (isPoint(a) && isPoint(b)) {
        QPointF p, q;
        if (!ForgeCad::refPoint(*sketch, a, p) || !ForgeCad::refPoint(*sketch, b, q)) return false;
        return linear(p, q);
    }
    if ((isPoint(a) && isLine(b)) || (isLine(a) && isPoint(b))) {
        const ConstraintRef &line = isLine(a) ? a : b;
        if (ForgeCad::isAxisReference(*sketch, line) && make(ConstraintType::AxisRadius)) {
            placeAxisDimension(result, cursor);
            return true;
        }
        return make(ConstraintType::Distance);
    }
    if (isLine(a) && isLine(b)) {
        const auto direction = [&](const ConstraintRef &r) {
            if (r.kind == 2) return r.element == 1 ? QPointF(1.0, 0.0) : QPointF(0.0, 1.0);
            const SketchSegment &s = sketch->segments.at(r.element);
            return s.second - s.first;
        };
        const QPointF d = direction(a), e = direction(b);
        const double sine = std::fabs(d.x() * e.y() - d.y() * e.x()) / std::max(pointLength(d) * pointLength(e), 1e-300);
        if (sine < 1e-9) return make(ConstraintType::Distance);
        if (!make(ConstraintType::Angle)) return false;
        // Angolo interno o supplementare: quello del settore dove sta il puntatore.
        ForgeCad::chooseAngleSector(*sketch, result, cursor);
        return true;
    }
    return make(ConstraintType::Distance);
}

void CadViewport::refreshDimensionPreview(const QPointF &cursor) {
    dimensionPreviewValid_ = drawingTool_ == DrawingTool::Dimension && buildDimension(constraintSelection(), cursor, dimensionPreview_);
}

void CadViewport::dimensionToolClick(const QPointF &point) {
    ConstraintRef ref;
    if (dimensionRefAt(point, ref)) {
        QVector<ConstraintRef> refs = constraintSelection();
        if (refs.contains(ref)) refs.removeAll(ref);
        else if (refs.size() >= 2) refs = {ref};
        else refs.append(ref);
        selectedPoints_.clear();
        sketchSelections_.clear();
        for (const ConstraintRef &r : refs) {
            if (r.point >= 0 || r.kind == 2) selectedPoints_.append({r.kind, r.element, r.point});
            else sketchSelections_.append({r.kind, r.element});
        }
        refreshDimensionPreview(point);
        showStatus(dimensionPreviewValid_ ? QStringLiteral("%1: clic nel vuoto per mettere la quota (H/V/O/A: orientamento)")
                                                .arg(ForgeCad::constraintName(dimensionPreview_.type))
                                          : refs.isEmpty() ? QStringLiteral("Quota: scegli le entita' da quotare")
                                                           : QStringLiteral("Quota: scegli un'altra entita'"));
        selectionChanged();
        update();
        return;
    }
    refreshDimensionPreview(point);
    if (!dimensionPreviewValid_) {
        showStatus(QStringLiteral("Quota: scegli prima le entita' da quotare (punti, segmenti, cerchi, archi)"));
        return;
    }
    SketchConstraint constraint = dimensionPreview_;
    dimensionPreviewValid_ = false;
    const QString error = placeDimension(constraint);
    if (!error.isEmpty()) showStatus(error);
    selectedPoints_.clear();
    sketchSelections_.clear();
    selectionChanged();
    update();
}

double CadViewport::signedAngleInput(double input, double current) {
    return current < 0.0 ? -input : input;
}

QString CadViewport::placeDimension(SketchConstraint constraint) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const SketchObject *active = activeSketchObject();
    if (!active) return QStringLiteral("Entra in modalita' schizzo.");
    for (const SketchConstraint &other : active->geometricConstraints)
        if (other.type == constraint.type
            && ((other.first == constraint.first && other.second == constraint.second) || (other.first == constraint.second && other.second == constraint.first)))
            return QStringLiteral("La quota c'e' gia'.");
    using T = ConstraintType;
    const QVector<ConstraintRef> refs = constraint.second.kind >= 0 ? QVector<ConstraintRef>{constraint.first, constraint.second}
                                                                    : QVector<ConstraintRef>{constraint.first};
    const QVector<T> applicable = ForgeCad::applicableConstraints(*active, refs);
    QVector<T> variants;
    QStringList labels;
    const bool linear = constraint.type == T::Distance || constraint.type == T::HorizontalDistance || constraint.type == T::VerticalDistance;
    if (linear && applicable.contains(T::HorizontalDistance)) {
        variants = {T::Distance, T::HorizontalDistance, T::VerticalDistance};
        labels = {QStringLiteral("Obliqua (allineata ai punti)"), QStringLiteral("Orizzontale (lungo X)"), QStringLiteral("Verticale (lungo Y)")};
    } else if (constraint.type == T::Radius || constraint.type == T::Diameter) {
        variants = {T::Radius, T::Diameter};
        labels = {QStringLiteral("Raggio"), QStringLiteral("Diametro")};
    } else if (constraint.type == T::AxisRadius || constraint.type == T::AxisDiameter) {
        variants = {T::AxisRadius, T::AxisDiameter};
        labels = {QStringLiteral("Raggio dall'asse"), QStringLiteral("Diametro dall'asse")};
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Quota"));
    auto *form = new QFormLayout(&dialog);
    form->addRow(wrappedNote(ForgeCad::describeConstraint(*active, constraint), &dialog));
    auto *kindBox = new QComboBox(&dialog);
    kindBox->addItems(labels);
    kindBox->setCurrentIndex(qMax(0, int(variants.indexOf(constraint.type))));
    if (!variants.isEmpty()) form->addRow(QStringLiteral("Tipo:"), kindBox);
    auto *valueBox = new ForgeCad::ExpressionSpinBox(&dialog);
    valueBox->setDecimals(6);
    valueBox->setRange(constraint.type == T::Angle ? -360.0 : 1e-9, 1e6);
    if (constraint.type == T::Angle) valueBox->setSuffix(QStringLiteral(" °"));
    // Angolo: il valore come appare nella quota (senza segno); il verso resta quello attuale.
    valueBox->setValue(constraint.type == T::Angle ? std::fabs(constraint.value) : constraint.value);
    form->addRow(QStringLiteral("Valore:"), valueBox);
    const auto measureOf = [&](T type) {
        SketchConstraint probe = constraint;
        probe.type = type;
        return ForgeCad::currentMeasure(*active, probe);
    };
    connect(kindBox, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        if (index < 0 || index >= variants.size()) return;
        constraint.type = variants.at(index);
        valueBox->setValue(measureOf(constraint.type));
        valueBox->selectAll();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    valueBox->setFocus();
    valueBox->selectAll();
    QString result;
    runUntilApplied(dialog, form, buttons, [&] {
        valueBox->interpretText();
        SketchConstraint placed = constraint;
        placed.value = constraint.type == T::Angle ? signedAngleInput(valueBox->value(), constraint.value) : valueBox->value();
        const DocumentState snapshot = documentState();
        SketchObject &sketch = sketches_[activeSketch_];
        const SketchObject before = sketch;
        sketch.geometricConstraints.append(placed);
        markSymmetryAxis(sketch, placed);
        QString failure;
        if (!solveActive({}, before, &failure)) return failure.isEmpty() ? QStringLiteral("La quota e' in conflitto con i vincoli.") : failure;
        history_.record(snapshot);
        selectedConstraints_ = {int(sketch.geometricConstraints.size()) - 1};
        sketchEdited();
        return QString();
    });
    return result;
}

QSet<int> CadViewport::withDependentBodies(QSet<int> removed) const {
    for (int index = 0; index < extrusions_.size(); ++index) {
        const ExtrusionObject &body = extrusions_.at(index);
        bool depends = false;
        for (int operand : ForgeCad::bodyOperands(body)) depends = depends || removed.contains(operand);
        if (depends) removed.insert(index);
    }
    return removed;
}

void CadViewport::deleteSelection() {
    if(documentReadOnly_)return;
    if (sketchMode_) {
        if (!selectedConstraints_.isEmpty()) deleteConstraints(selectedConstraints_);
        else deleteSketchElements();
        return;
    }
    // Piu' oggetti scelti con il riquadro (o Maiusc/Ctrl+clic): tutti insieme.
    if (selectedObjects_.size() > 1) {
        deleteObjectsOrFeatures(selectedObjects_);
        return;
    }
    if (selection_.kind == SceneObjectKind::Sketch || selection_.kind == SceneObjectKind::Extrusion)
        deleteObject(selection_.kind, selection_.index);
}

void CadViewport::deleteObjectsOrFeatures(const QVector<SceneSelection> &objects) {
    if(documentReadOnly_)return;
    QVector<int> features;
    bool allFeatures = !objects.isEmpty();
    for (const SceneSelection &object : objects) {
        if (object.kind == SceneObjectKind::Extrusion && object.index >= 0 && object.index < extrusions_.size()
            && extrusions_.at(object.index).modelBodyId != 0)
            features.append(object.index);
        else allFeatures = false;
    }
    if (allFeatures) deleteFeatures(features);
    else deleteObjects(objects);
}

void CadViewport::deleteObject(SceneObjectKind kind, int index) {
    if (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size()
        && extrusions_.at(index).modelBodyId != 0) {
        deleteFeature(index);
        return;
    }
    deleteObjects({{kind, index, -1}});
}

void CadViewport::deleteObjects(const QVector<SceneSelection> &objects) {
    if(documentReadOnly_)return;
    QSet<int> sketchSet, chosenBodies;
    QStringList chosenNames;
    for (const SceneSelection &object : objects) {
        if (object.kind == SceneObjectKind::Sketch && object.index >= 0 && object.index < sketches_.size()) {
            if (!sketchSet.contains(object.index)) chosenNames.append(sketches_.at(object.index).name);
            sketchSet.insert(object.index);
        } else if (object.kind == SceneObjectKind::Extrusion && object.index >= 0 && object.index < extrusions_.size()) {
            if (!chosenBodies.contains(object.index)) chosenNames.append(extrusions_.at(object.index).name);
            chosenBodies.insert(object.index);
        }
    }
    if (sketchSet.isEmpty() && chosenBodies.isEmpty()) return;
    QSet<int> bodies = chosenBodies;
    for (int body = 0; body < extrusions_.size(); ++body)
        for (int sketch : ForgeCad::sketchesOf(extrusions_.at(body)))
            if (sketchSet.contains(sketch)) bodies.insert(body);
    bodies = withDependentBodies(bodies);
    QSet<int> dependents = bodies;
    for (int body : chosenBodies) dependents.remove(body);
    if (!dependents.isEmpty()) {
        QList<int> sorted(dependents.begin(), dependents.end());
        std::sort(sorted.begin(), sorted.end());
        QStringList names;
        for (int body : sorted) names.append(QStringLiteral("  \u2022 ") + extrusions_.at(body).name);
        const QString what = chosenNames.size() == 1 ? QStringLiteral("\"%1\"").arg(chosenNames.first())
                                                     : QStringLiteral("%1 oggetti scelti").arg(chosenNames.size());
        const auto answer = QMessageBox::question(this, QStringLiteral("Elimina"),
            QStringLiteral("Da %1 dipendono:\n%2\n\nEliminare anche questi?").arg(what, names.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }
    if (sketchMode_ && sketchSet.contains(activeSketch_)) endSketchMode();
    recordUndo();
    removeBodies(bodies);
    // Gli schizzi dall'ultimo: gli indici di quelli prima non cambiano.
    QList<int> sketchList(sketchSet.begin(), sketchSet.end());
    std::sort(sketchList.begin(), sketchList.end(), std::greater<int>());
    for (int index : sketchList) removeSketchAt(index);
    selection_ = {};
    selectedObjects_.clear();
    hover_ = {};
    documentChanged();
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
        if (!body.visible) continue;
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

void CadViewport::drawSelectionBox(QPainter &painter) const {
    if (!boxSelecting_) return;
    const bool window = boxWindowMode();
    const QColor color = window ? QColor(90, 170, 255) : QColor(110, 230, 140);
    painter.save();
    painter.setPen(QPen(color, 1.2, window ? Qt::SolidLine : Qt::DashLine));
    painter.setBrush(QColor(color.red(), color.green(), color.blue(), 40));
    painter.drawRect(boxRect());
    painter.restore();
}

void CadViewport::removeSketchAt(int index) {
    sketches_.removeAt(index);
    const auto renumber = [index](int &sketch) {
        if (sketch > index) --sketch;
        else if (sketch == index) sketch = -1;
    };
    for (ExtrusionObject &body : extrusions_) {
        renumber(body.sketchIndex);
        renumber(body.pathSketch);
        for (int &section : body.loftSketches) renumber(section);
        for (int &guide : body.loftGuides) renumber(guide);
        for (SketchPathRef &guide : body.loftGuidePaths) renumber(guide.sketch);
        for (QVector<GeometryRef> *refs : {&body.datum.refs, &body.pattern.refs, &body.ruledFirst, &body.ruledSecond, &body.planarRefs, &body.loftStartFaces, &body.loftEndFaces, &body.loftSections, &body.loftEdgeGuides})
            for (GeometryRef &ref : *refs)
                if (ForgeCad::isSketchRef(ref)) renumber(ref.index);
        if (ForgeCad::isSketchRef(body.extentRef)) renumber(body.extentRef.index);
        if (ForgeCad::isSketchRef(body.move.axis)) renumber(body.move.axis.index);
        if (ForgeCad::isSketchRef(body.revolveAxisRef)) renumber(body.revolveAxisRef.index);
        if (ForgeCad::isSketchRef(body.draftNeutral)) renumber(body.draftNeutral.index);
        if (ForgeCad::isSketchRef(body.thickenDirection)) renumber(body.thickenDirection.index);
        for (GeometryRef *ref : {&body.sweepProfileRef, &body.sweepPathRef})
            if (ForgeCad::isSketchRef(*ref)) renumber(ref->index);
        if (ForgeCad::isSketchRef(body.curveExtraction.source)) renumber(body.curveExtraction.source.index);
        if (ForgeCad::isSketchRef(body.curveExtraction.tool)) renumber(body.curveExtraction.tool.index);
    }
    if (activeSketch_ == index) activeSketch_ = -1;
    else if (activeSketch_ > index) --activeSketch_;
}

void CadViewport::setEditBodyCallback(std::function<void(int)> callback) { editBodyCallback_ = std::move(callback); }

QString CadViewport::createExtrusion(double distance, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    ExtrusionObject extrusion;
    extrusion.distance = distance;
    extrusion.sketchIndex = activeSketch_;
    return createExtrusion(extrusion, name);
}

QString CadViewport::createExtrusion(const ExtrusionObject &definition, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const int sketch = definition.sketchIndex >= 0 ? definition.sketchIndex : activeSketch_;
    if (sketch < 0 || sketch >= sketches_.size())
        return QStringLiteral("Nessuno schizzo attivo.");
    ExtrusionObject extrusion = withMergeCandidates(definition, int(extrusions_.size()));
    extrusion.operation = -1;
    extrusion.feature = BodyFeature::Extrusion;
    extrusion.name = name;
    extrusion.sketchIndex = sketch;
    extrusion.plane = sketches_.at(sketch).plane;
    rebuildBody(extrusion, int(extrusions_.size()));
    extrusion.mergeProbe = false;
    if (!ForgeCad::hasGeometry(extrusion)) return extrusion.error;
    recordUndo();
    for (int body : ForgeCad::hiddenOperands(extrusion))
        if (body >= 0 && body < extrusions_.size()) extrusions_[body].visible = false;
    extrusions_.append(extrusion);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    documentChanged();
    return {};
}

QString CadViewport::createRevolution(int sketchIndex, int axis, double angle, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (sketchIndex < 0 || sketchIndex >= sketches_.size()) return QStringLiteral("Nessuno schizzo scelto.");
    ExtrusionObject revolution;
    revolution.name = name;
    revolution.feature = BodyFeature::Revolution;
    revolution.sketchIndex = sketchIndex;
    revolution.plane = sketches_.at(sketchIndex).plane;
    revolution.revolveAxis = axis;
    revolution.revolveAngle = angle;
    rebuildBody(revolution, int(extrusions_.size()));
    if (!ForgeCad::hasGeometry(revolution)) return revolution.error;
    recordUndo();
    extrusions_.append(revolution);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    documentChanged();
    return {};
}

QString CadViewport::createPrimitive(const PrimitiveParameters &parameters, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    ExtrusionObject primitive;
    primitive.name = name;
    primitive.feature = BodyFeature::Primitive;
    primitive.plane = parameters.plane;
    primitive.primitive = parameters;
    rebuildBody(primitive, int(extrusions_.size()));
    if (!ForgeCad::hasGeometry(primitive)) return primitive.error;
    recordUndo();
    extrusions_.append(primitive);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    documentChanged();
    return {};
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

bool CadViewport::preparePreviewCacheContext() {
    const bool changed = previewCacheContextValid_
        && (previewCacheSketches_.constData()!=sketches_.constData()
            || previewCacheBodies_.constData()!=extrusions_.constData()
            || previewCacheQuality_!=tessellationQuality_);
    if (!previewCacheContextValid_ || changed) {
        previewCache_.clear();
        previewCacheSketches_=sketches_;
        previewCacheBodies_=extrusions_;
        previewCacheQuality_=tessellationQuality_;
        previewCacheContextValid_=true;
    }
    return changed;
}

void CadViewport::refreshPreviewDragSource() {
    if (preview_.valid && preview_.definition.feature==BodyFeature::Extrusion
        && preview_.definition.mergeOperation==0 && preview_.definition.extent==0) {
        previewDragSource_=std::make_shared<BodyDisplay>(preview_.display);
        previewDragDefinition_=preview_.definition;
        previewDragKey_=preview_.key;
    } else previewDragSource_.reset();
}

void CadViewport::rememberPreview() {
    if (!preview_.valid || !preview_.geometry) return;
    const auto bytes=[](const BodyDisplay &display) {
        std::size_t points=std::size_t(display.vertices.size()+display.normals.size());
        for (const auto &line:display.edges) points+=std::size_t(line.size());
        for (const auto &line:display.constructionCurves) points+=std::size_t(line.size());
        return points*sizeof(QVector3D);
    };
    const auto size=[&](const Preview &preview) {
        return bytes(preview.display)+bytes(preview.retainedDisplay)+bytes(preview.resultDisplay);
    };
    constexpr std::size_t budget=64*1024*1024;
    if (size(preview_)>budget) return;
    previewCache_.erase(std::remove_if(previewCache_.begin(),previewCache_.end(),
        [&](const Preview &entry){return entry.key==preview_.key;}),previewCache_.end());
    previewCache_.push_back(preview_);
    std::size_t total=0;for(const auto &entry:previewCache_)total+=size(entry);
    while(previewCache_.size()>4 || total>budget) {
        total-=size(previewCache_.front());previewCache_.erase(previewCache_.begin());
    }
}

void CadViewport::updateDragPreview(const ExtrusionObject &definition, int index) {
    if (!featureHandleDragging_) return;
    if (preview_.valid) {
        previewDragDisplay_ = preview_.display;
        previewDragVisible_ = true;
    }
    if (!previewDragSource_ || definition.feature != BodyFeature::Extrusion
        || definition.operation >= 0 || definition.extent != 0
        || definition.mergeOperation != 0 || definition.extrudeSides == 2
        || definition.sketchIndex < 0 || definition.sketchIndex >= sketches_.size()
        || std::abs(definition.distance) < 1e-7
        || std::abs(previewDragDefinition_.distance) < 1e-7) return;
    auto same = definition;
    same.distance = previewDragDefinition_.distance;
    if (previewKey(same, index) != previewDragKey_) return;
    const auto &sketch = sketches_.at(definition.sketchIndex);
    const auto base = ForgeCad::sketchToDisplay(QPointF(), sketch);
    const auto vector = ForgeCad::extrusionVector(sketch, 1.0);
    const QVector3D axis = QVector3D(vector.x(), vector.y(), vector.z()).normalized();
    const float ratio = float(definition.distance / previewDragDefinition_.distance);
    previewDragDisplay_ = *previewDragSource_;
    const auto transform = [&](QVector3D &point) {
        point += axis * (QVector3D::dotProduct(point - base, axis) * (ratio - 1));
    };
    for (auto &point : previewDragDisplay_.vertices) transform(point);
    for (auto &line : previewDragDisplay_.edges) for (auto &point : line) transform(point);
    for (auto &point : previewDragDisplay_.faceLabelPoints) transform(point);
    for (auto &normal : previewDragDisplay_.normals)
        normal = (normal + axis * (QVector3D::dotProduct(normal, axis) * (1 / ratio - 1))).normalized();
    if (ratio < 0) {
        for (int i = 0; i + 2 < previewDragDisplay_.vertices.size(); i += 3) {
            qSwap(previewDragDisplay_.vertices[i+1], previewDragDisplay_.vertices[i+2]);
            if (i + 2 < previewDragDisplay_.normals.size())
                qSwap(previewDragDisplay_.normals[i+1], previewDragDisplay_.normals[i+2]);
        }
    }
    for (auto &line : previewDragDisplay_.constructionCurves) for (auto &point : line) transform(point);
    previewDragDisplay_.rayIndex.reset();
    previewDragVisible_ = true;
    update();
}

void CadViewport::requestPreview(const ExtrusionObject &definition, int index ) {
    const bool contextChanged=preparePreviewCacheContext();
    if (contextChanged) { previewDragSource_.reset();previewDragVisible_=false; }
    updateDragPreview(definition, index);
    const bool firstRequest=preview_.key.isEmpty();
    const QString key = previewKey(definition, index);
    if (!contextChanged && key == preview_.key) {
        previewHandlePending_ = false;
        if ((preview_.valid || !preview_.error.isEmpty()) && previewCallback_) previewCallback_(preview_.error);
        return;
    }
    // Durante il drag lascia finire il fotogramma in corso: cancellarlo
    // a ogni movimento impedirebbe di vedere qualunque risultato.
    if (featureHandleDragging_ && previewRunning_) {
        previewHandlePending_ = true;
        previewHandleDefinition_ = definition;
        previewHandleIndex_ = index;
        return;
    }
    previewHandlePending_ = false;
    if (previewCancellation_) previewCancellation_->store(true, std::memory_order_relaxed);
    preview_.key = key;
    preview_.notice.clear();
    preview_.definition = definition;
    preview_.index = index;
    preview_.replaced.clear();
    preview_.later.clear();
    preview_.restored.clear();
    if (index >= 0 && index < extrusions_.size()) {
        preview_.later = withDependentBodies({index});
        preview_.later.remove(index);
        for (int body : preview_.later)
            for (int operand : ForgeCad::bodyOperands(extrusions_.at(body)))
                if (operand != index && !preview_.later.contains(operand) && operand >= 0 && operand < extrusions_.size()
                    && !extrusions_.at(operand).suppressed)
                    preview_.restored.insert(operand);
    }
    // Nel taglio entrambi gli originali restano visibili: si puo' cliccare
    // anche una regione diversa da quella attualmente mostrata in ambra.
    if (definition.operation < 0 && definition.feature == BodyFeature::SheetTrim && !definition.trimProject) {
        preview_.replaced.clear();
    }
    else if (ForgeCad::isCurveBody(definition) || ForgeCad::isDatumBody(definition)) preview_.replaced.clear();  // la base dell'elica resta
    else if (definition.operation < 0 && definition.feature == BodyFeature::Blend) preview_.replaced.clear(); // la base opaca resta sotto la patch
    else if (definition.operation < 0 && definition.feature == BodyFeature::SurfaceOffset) preview_.replaced.clear();  // il corpo di partenza resta
    else if (definition.operation < 0 && (definition.feature == BodyFeature::Loft || definition.feature == BodyFeature::Ruled || definition.feature == BodyFeature::PlanarSurface
                                          || definition.feature == BodyFeature::BoundarySurface
                                          || definition.feature == BodyFeature::FillSurface))
        preview_.replaced.clear();  // curve e bordi scelti restano visibili
    else if (definition.mergeProbe) preview_.replaced.clear();  // i corpi fusi si sanno a calcolo finito
    else preview_.replaced = ForgeCad::bodyOperands(definition);
    // Nell'unione la base resta opaca sotto la patch aggiunta. Nella
    // sottrazione viene invece sostituita dalle facce conservate del
    // risultato, gia' forate/tagliate, mentre la superficie di taglio e'
    // la sola patch trasparente. Lo sweep conserva per ora la sostituzione
    // completa del risultato.
    if (localMergePreview(definition)) {
        preview_.replaced.clear();
        if (!definition.mergeProbe) {
            if (definition.mergeOperation == 2) preview_.replaced = definition.mergeBodies;
            else if (definition.mergeOperation == 1)
                for (int body : definition.mergeBodies) preview_.restored.insert(body);
        }
    } else if (definition.operation < 0 && definition.feature == BodyFeature::Sweep && !definition.mergeProbe) {
        preview_.replaced = definition.mergeOperation != 0 ? definition.mergeBodies : QVector<int>();
    }
    preview_.valid = false;
    preview_.error.clear();
    preview_.geometry.reset();
    if (!previewDragVisible_) preview_.retainedDisplay = {};
    preview_.resultDisplay = {};
    previewErrorSketch_ = -1;
    ++preview_.generation;
    const auto cached=std::find_if(previewCache_.begin(),previewCache_.end(),
        [&](const Preview &entry){return entry.key==key;});
    if(cached!=previewCache_.end()) {
        const quint64 generation=preview_.generation;
        Preview result=*cached;
        previewCache_.erase(cached);previewCache_.push_back(result);
        preview_=std::move(result);preview_.generation=generation;
        previewDragVisible_=false;refreshPreviewDragSource();
        if(previewTimer_)previewTimer_->stop();
        if(previewCallback_)previewCallback_(preview_.error);
        update();return;
    }
    if (!previewTimer_) {
        previewTimer_ = new QTimer(this);
        previewTimer_->setSingleShot(true);
        previewTimer_->setInterval(120);
        previewTimer_->callOnTimeout([this] { startPreviewJob(); });
    }
    // Il timer scandisce il movimento: non rimandarlo a ogni evento mouse.
    if (!featureHandleDragging_ || !previewTimer_->isActive()) previewTimer_->start(firstRequest?0:120);
    update();
}

void CadViewport::showTrimPreview(const ExtrusionObject &definition, const ForgeCad::ForgeBody &geometry,
                         const BodyDisplay &display, int index ) {
    requestPreview(definition, index);
    if (previewTimer_) previewTimer_->stop();
    ++preview_.generation;
    preview_.valid = bool(geometry);
    preview_.error = geometry ? QString() : QStringLiteral("La parte scelta non ha geometria valida.");
    preview_.geometry = geometry;
    preview_.display = display;
    preview_.retainedDisplay = {};
    preview_.resultDisplay = {};
    if (previewCallback_) previewCallback_(preview_.error);
    update();
}

void CadViewport::requestBlendPreview(int base, const QVector<EdgePoint> &edges, double size, bool chamfer, int hidden , const ChamferSpec &spec ) {
    if (base < 0 || base >= extrusions_.size() || edges.isEmpty() || !(size > 0.0)) {
        clearPreview();
        return;
    }
    ExtrusionObject blend = hidden >= 0 && hidden < extrusions_.size() ? extrusions_.at(hidden) : ExtrusionObject();
    blend.operation = -1;
    blend.feature = BodyFeature::Blend;
    blend.firstBody = base;
    blend.blendEdges = edges;
    blend.blendSize = size;
    blend.blendChamfer = chamfer;
    blend.chamferSpec = spec;
    requestPreview(blend, hidden);
}

void CadViewport::clearPreview() {
    // Il risultato viene invalidato subito; il worker esce al prossimo
    // checkpoint e solo allora libera previewRunning_ e la barra di lavoro.
    if (previewTimer_) previewTimer_->stop();
    if (previewCancellation_) previewCancellation_->store(true, std::memory_order_relaxed);
    previewRerun_ = false;
    previewHandlePending_ = false;
    previewCache_.clear();previewCacheSketches_.clear();previewCacheBodies_.clear();previewCacheContextValid_=false;
    previewDragVisible_ = false;
    previewDragSource_.reset();
    previewDragDisplay_ = {};
    previewDragKey_.clear();
    ++preview_.generation;
    preview_.key.clear();
    preview_.index = -1;
    preview_.replaced.clear();
    preview_.later.clear();
    preview_.restored.clear();
    preview_.valid = false;
    preview_.error.clear();
    preview_.geometry.reset();
    previewErrorSketch_ = -1;
    preview_.display = {};
    preview_.retainedDisplay = {};
    preview_.resultDisplay = {};
    update();
}

void CadViewport::clearBlendPreview() { clearPreview(); }

QString CadViewport::previewError() const { return preview_.error; }

void CadViewport::setPreviewCallback(std::function<void(const QString &)> callback) { previewCallback_ = std::move(callback); }

const QString &CadViewport::previewNotice() const { return preview_.notice; }

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

void CadViewport::resumeEdgePick(int body, const QVector<EdgePoint> &edges, bool chamfer) {
    if (body < 0 || body >= extrusions_.size()) return;
    selection_ = {SceneObjectKind::Extrusion, body, -1};
    selectedFace_ = {};
    if (!beginEdgePick(chamfer).isEmpty()) return;
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

QString CadViewport::createBlend(int baseIndex, const QVector<EdgePoint> &edges, double size, bool chamfer, const QString &name, const ChamferSpec &spec ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (baseIndex < 0 || baseIndex >= extrusions_.size()) return QStringLiteral("Corpo non valido.");
    ExtrusionObject blend;
    blend.featureId = 1;
    for (const auto &feature : extrusions_) blend.featureId = std::max(blend.featureId, feature.featureId + 1);
    blend.name = name;
    blend.feature = BodyFeature::Blend;
    blend.plane = extrusions_.at(baseIndex).plane;
    blend.firstBody = baseIndex;
    blend.blendChamfer = chamfer;
    blend.blendSize = size;
    blend.blendEdges = edges;
    blend.blendBaseFeature = extrusions_.at(baseIndex).featureId;
    blend.chamferSpec = spec;
    // La stessa definizione e' gia' stata costruita dall'anteprima: il
    // B-rep immutabile si promuove direttamente. Durante l'anteprima si
    // tassella soltanto la patch locale; la mesh completa si prepara una
    // sola volta, quando l'utente conferma.
    const QString key = previewKey(blend, -1);
    if (preview_.key == key) {
        if (!preview_.valid || !preview_.geometry)
            return preview_.error.isEmpty() ? QStringLiteral("Attendi che l'anteprima sia pronta.") : preview_.error;
        blend.forgeBody = preview_.geometry;
        blend.solid = !blend.forgeBody->isSheet();
        blend.display = preview_.resultDisplay;
        if (blend.display.vertices.isEmpty() && blend.display.edges.isEmpty())
            ForgeCad::tessellateGeometry(blend, tessellationQuality_, blend.display);
        blend.display.constructionCurves.clear();
    } else {
        rebuildBody(blend, int(extrusions_.size()));
        if (!ForgeCad::hasGeometry(blend)) return blend.error;
    }
    // Anche il B-rep promosso dall'anteprima riceve il tag persistente,
    // conservando le dichiarazioni gia' preparate dal kernel.
    ForgeCad::registerFeatureGenealogy(blend, int(extrusions_.size()), sketches_, extrusions_);
    recordUndo();
    extrusions_[baseIndex].visible = false;
    extrusions_.append(blend);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QString CadViewport::createSheetTrim(int sheet, int tool, int plane, const EdgePoint &keep, const QString &name, int sketch ,
                            bool both , const EdgePoint &toolKeep ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    ExtrusionObject definition;
    definition.firstBody = sheet;
    definition.secondBody = tool;
    definition.trimPlane = plane;
    definition.sketchIndex = sketch;
    definition.trimKeep = keep;
    definition.trimBoth = both;
    definition.trimToolKeep = toolKeep;
    return createSheetTrim(definition, name);
}

QString CadViewport::createSheetTrim(const ExtrusionObject &definition, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const int sheet = definition.firstBody, tool = definition.secondBody;
    const bool both = definition.trimBoth;
    if (sheet < 0 || sheet >= extrusions_.size() || tool >= extrusions_.size() || tool == sheet)
        return QStringLiteral("Corpo da tagliare o strumento non validi.");
    ExtrusionObject body;
    body.name = name;
    body.feature = BodyFeature::SheetTrim;
    body.plane = extrusions_.at(sheet).plane;
    body.firstBody = sheet;
    body.secondBody = tool;
    body.trimPlane = definition.trimPlane;
    body.sketchIndex = definition.sketchIndex;
    body.trimKeep = definition.trimKeep;
    body.trimBoth = both;
    body.trimToolKeep = definition.trimToolKeep;
    body.trimProject = definition.trimProject && tool < 0 && definition.sketchIndex >= 0;
    body.projectionMode = definition.projectionMode;
    body.projectionReverse = definition.projectionReverse;
    const QString key = previewKey(body, -1);
    if (preview_.key == key && preview_.valid && preview_.geometry) {
        body.forgeBody = preview_.geometry;
        body.display = preview_.display;
        body.solid = !preview_.geometry->isSheet();
        body.notice = preview_.notice;
    } else {
        rebuildBody(body, int(extrusions_.size()));
    }
    if (!ForgeCad::hasGeometry(body)) return body.error;
    clearPreview();
    recordUndo();
    extrusions_[sheet].visible = false;
    if (both && tool >= 0) extrusions_[tool].visible = false;
    extrusions_.append(body);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QVector<SheetPiece> CadViewport::sheetPieces(int sheet, int tool, int plane, int sketch, QString *error) const {
    ScopedWork work(workCallback_, QStringLiteral("Calcolo delle parti della superficie..."));
    if (sheet < 0 || sheet >= extrusions_.size() || tool >= extrusions_.size() || tool == sheet) {
        if (error) *error = QStringLiteral("Superficie o strumento non validi.");
        return {};
    }
    const ExtrusionObject &target = extrusions_.at(sheet);
    const ExtrusionObject *cutter = tool >= 0 ? &extrusions_.at(tool) : nullptr;
    ForgeCad::ForgeBody geometry;
    if (cutter && cutter->forgeBody) geometry = cutter->forgeBody;
    else if (cutter && cutter->feature == BodyFeature::DatumPlane && cutter->datumValid) {
        const SketchFrame &f = cutter->datumFrame;
        const ForgeCad::Kernel::Frame3 frame(
            ForgeCad::Kernel::Vec3(f.origin[0], f.origin[1], f.origin[2]),
            ForgeCad::Kernel::Vec3(f.normal[0], f.normal[1], f.normal[2]),
            ForgeCad::Kernel::Vec3(f.xAxis[0], f.xAxis[1], f.xAxis[2]));
        geometry = ForgeCad::forgeTrimPlaneTool(target.forgeBody, frame, error);
    }
    else if (sketch >= 0 && sketch < sketches_.size())
        geometry = ForgeCad::forgeTrimSketchTool(target.forgeBody, sketches_.at(sketch), error);
    if ((cutter || sketch >= 0) && !geometry) return {};
    return ForgeCad::forgeSheetPieces(target.forgeBody, geometry, plane, error);
}

QString CadViewport::createSheetExtend(int sheet, const QVector<EdgePoint> &edges, double distance, bool linear, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (sheet < 0 || sheet >= extrusions_.size()) return QStringLiteral("Superficie non valida.");
    ExtrusionObject body;
    body.name = name;
    body.feature = BodyFeature::SheetExtend;
    body.plane = extrusions_.at(sheet).plane;
    body.firstBody = sheet;
    body.blendEdges = edges;
    body.blendSize = distance;
    body.extendLinear = linear;
    // Se il pannello sta mostrando proprio questa estensione, promuove il
    // B-rep e la mesh dell'anteprima. In questo modo non resta un fotogramma
    // in cui la sorgente e' gia' nascosta ma il risultato e' ancora escluso
    // dal disegno perche' appartiene all'anteprima attiva.
    const QString key = previewKey(body, -1);
    if (preview_.key == key) {
        if (!preview_.valid || !preview_.geometry)
            return preview_.error.isEmpty() ? QStringLiteral("Attendi che l'anteprima sia pronta.") : preview_.error;
        body.forgeBody = preview_.geometry;
        body.display = preview_.display;
        body.solid = false;
        if (body.display.vertices.isEmpty() && body.display.edges.isEmpty())
            ForgeCad::tessellateGeometry(body, tessellationQuality_, body.display);
    } else {
        rebuildBody(body, int(extrusions_.size()));
    }
    if (!ForgeCad::hasGeometry(body)) return body.error;
    clearPreview();
    recordUndo();
    extrusions_[sheet].visible = false;
    extrusions_.append(body);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QString CadViewport::createScale(int base, double factor, int mode, const EdgePoint &center, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (base < 0 || base >= extrusions_.size()) return QStringLiteral("Corpo non valido.");
    ExtrusionObject body;
    body.name = name;
    body.feature = BodyFeature::Scale;
    body.plane = extrusions_.at(base).plane;
    body.firstBody = base;
    body.scaleFactor = factor;
    body.scaleCenterMode = mode;
    body.scaleCenter = center;
    rebuildBody(body, int(extrusions_.size()));
    if (!ForgeCad::hasGeometry(body)) return body.error;
    recordUndo();
    extrusions_[base].visible = false;
    extrusions_.append(body);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

bool CadViewport::takePreviewResult(ExtrusionObject &body, int previewIndex, QString *error) {
    if (body.mergeProbe || preview_.key.isEmpty() || preview_.key != previewKey(body, previewIndex)) return false;
    if (!preview_.valid && preview_.error.isEmpty()) {
        ScopedWork work(workCallback_, QStringLiteral("Completamento del calcolo dell'anteprima..."));
        const QString key = preview_.key;
        while (preview_.key == key && !preview_.valid && preview_.error.isEmpty())
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents | QEventLoop::ExcludeUserInputEvents, 50);
        if (preview_.key != key) return false;
    }
    if (!preview_.valid || !preview_.geometry) {
        if (error) *error = preview_.error;
        return !preview_.error.isEmpty();
    }
    body.forgeBody = preview_.geometry;
    body.solid = !preview_.geometry->isSheet();
    body.display = preview_.resultDisplay.vertices.isEmpty() && preview_.resultDisplay.edges.isEmpty() ? preview_.display : preview_.resultDisplay;
    // Per un'estrusione fusa/sottratta preview_.display e' volutamente la
    // sola patch locale. Alla conferma serve invece la mesh completa del
    // risultato definitivo (senza rifare il calcolo B-rep).
    if (localMergePreview(body) && body.mergeOperation != 0
        && preview_.resultDisplay.vertices.isEmpty() && preview_.resultDisplay.edges.isEmpty())
        ForgeCad::tessellateGeometry(body, tessellationQuality_, body.display);
    body.display.constructionCurves.clear();
    body.error.clear();
    body.notice = preview_.notice;
    return true;
}

QString CadViewport::createBody(ExtrusionObject body, const QVector<int> &hide ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    body = withMergeCandidates(body, int(extrusions_.size()));
    QString previewError;
    if (takePreviewResult(body, -1, &previewError)) {
        if (!previewError.isEmpty()) return previewError;
    } else {
        rebuildBody(body, int(extrusions_.size()));
    }
    body.mergeProbe = false;
    if (!ForgeCad::hasGeometry(body)) return body.error;
    recordUndo();
    QVector<int> hidden = hide + ForgeCad::hiddenOperands(body);
    for (int index : hidden)
        if (index >= 0 && index < extrusions_.size()) extrusions_[index].visible = false;
    extrusions_.append(body);
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QString CadViewport::createDeleteFaces(ExtrusionObject definition) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (definition.firstBody < 0 || definition.firstBody >= extrusions_.size()
        || !extrusions_.at(definition.firstBody).forgeBody)
        return QStringLiteral("Il corpo di partenza non ha geometria.");
    QString error;
    const QVector<ForgeCad::ForgeBody> components = ForgeCad::forgeDeleteFacesSeparated(
        extrusions_.at(definition.firstBody).forgeBody, definition.offsetFaces, &error);
    if (components.isEmpty()) return error;

    recordUndo();
    quint64 nextBodyId = 1;
    for (const ExtrusionObject &feature : extrusions_) nextBodyId = qMax(nextBodyId, feature.modelBodyId + 1);
    for (const ModelBody &model : modelBodies_) nextBodyId = qMax(nextBodyId, model.id + 1);
    const quint64 inherited = extrusions_.at(definition.firstBody).modelBodyId;
    const QString commonName = definition.name;
    const int firstNew = extrusions_.size();
    for (int component = 0; component < components.size(); ++component) {
        ExtrusionObject result = definition;
        result.deleteComponent = components.size() > 1 ? component : -1;
        result.forgeBody = components.at(component);
        result.solid = false;
        result.error.clear();
        result.cachedGeometry = false;
        result.featureId = 0;
        result.modelBodyId = component == 0 && inherited ? inherited : nextBodyId++;
        if (components.size() > 1) result.name = QStringLiteral("%1 (%2)").arg(commonName).arg(component + 1);
        tessellateBody(result, extrusions_.size());
        extrusions_.append(std::move(result));
    }
    selection_ = {SceneObjectKind::Extrusion, firstNew, -1};
    hover_ = {};
    documentChanged();
    return {};
}

QString CadViewport::beginReferencePick(int roles, int owner) {
    // Nello schizzo solo con la vista sbloccata (estrusione, rivoluzione): i clic scelgono, non disegnano.
    if (sketchMode_ && !sketchViewUnlocked_) return QStringLiteral("Esci prima dalla modalita' schizzo.");
    if (edgePicking_) cancelEdgePick();
    refPicking_ = true;
    refPickRoles_ = roles;
    refPickOwner_ = owner;
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

void CadViewport::drawMeasureOverlay(QPainter &painter) const {
    if (!measureValid_) return;
    const QPointF a = projectWorldPoint(measureFrom_), b = projectWorldPoint(measureTo_);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent(255, 170, 60);
    painter.setPen(QPen(QColor(0, 0, 0, 150), 4.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(a, b);
    painter.setPen(QPen(accent, 2.0, Qt::DashLine, Qt::RoundCap));
    painter.drawLine(a, b);
    painter.setPen(QPen(Qt::black, 1.0));
    painter.setBrush(accent);
    painter.drawEllipse(a, 4.5, 4.5);
    painter.drawEllipse(b, 4.5, 4.5);
    if (!measureLabel_.isEmpty()) {
        QFont font = uiFont(10, QFont::DemiBold);
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        const QPointF middle = 0.5 * (a + b) + QPointF(10.0, -12.0);
        QRectF box(middle, QSizeF(metrics.horizontalAdvance(measureLabel_) + 12.0, metrics.height() + 6.0));
        box.moveTo(qBound(2.0, box.left(), qMax(2.0, width() - box.width() - 2.0)), qBound(2.0, box.top(), qMax(2.0, height() - box.height() - 2.0)));
        painter.setPen(QPen(accent, 1.2));
        painter.setBrush(QColor(25, 28, 34, 225));
        painter.drawRoundedRect(box, 4.0, 4.0);
        painter.setPen(QColor(255, 225, 180));
        painter.drawText(box, Qt::AlignCenter, measureLabel_);
    }
    painter.restore();
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

bool CadViewport::documentReadOnly() const { return documentReadOnly_; }

const ForgeCad::DocumentVersion &CadViewport::documentVersion() const { return documentVersion_; }

const QUuid &CadViewport::documentBaseCheckinId() const { return documentBaseCheckinId_; }

void CadViewport::setDocumentReadOnly(bool readOnly) {
    if(documentReadOnly_==readOnly)return;
    if(readOnly&&sketchMode_)endSketchMode();
    documentReadOnly_=readOnly;update();
}

bool CadViewport::previewDatum(const DatumParameters &parameters, int index, SketchFrame &frame, QString *error) const {
    return ForgeCad::computeDatum(parameters, index < 0 ? int(extrusions_.size()) : index, sketches_, extrusions_, frame, error);
}

int CadViewport::createDatumSketch(int body, const QString &name) {
    if(documentReadOnly_)return -1;
    if (body < 0 || body >= extrusions_.size() || !ForgeCad::isDatumBody(extrusions_.at(body)) || !extrusions_.at(body).datumValid) return -1;
    if (sketchMode_) endSketchMode();
    if (edgePicking_) cancelEdgePick();
    SketchObject sketch;
    sketch.name = name;
    sketch.plane = kFacePlane;
    sketch.frame = datumSketchFrame(extrusions_.at(body).datumFrame, nullptr);
    sketch.faceSource = extrusions_.at(body).name;
    sketch.datumPlane = body;
    recordUndo();
    sketches_.append(sketch);
    activeSketch_ = sketches_.size() - 1;
    selection_ = {};
    documentChanged();
    beginSketchMode(kFacePlane);
    return activeSketch_;
}

QString CadViewport::readImportFile(const QString &path, QVector<ForgeCad::ImportedPart> &parts, QStringList &notes) {
    QString error;
    runWhileResponsive([&] { error = ForgeCad::importCadFile(path, parts, &notes); });
    return error;
}

QStringList CadViewport::importParts(const QVector<ForgeCad::ImportedPart> &parts, const QString &source,
                            const std::function<void(int, int)> &progress ) {
    if(documentReadOnly_)return {QStringLiteral("Documento in sola lettura: eseguire il check-out.")};
    ScopedWork work(workCallback_, QStringLiteral("Preparazione dei corpi importati..."));
    QStringList failures;
    if (parts.isEmpty()) return failures;
    QVector<ExtrusionObject> prepared;
    prepared.reserve(parts.size());
    const int quality = tessellationQuality_;
    quint64 nextFeatureId=1;
    for(const auto &existing:extrusions_)nextFeatureId=std::max(nextFeatureId,existing.featureId+1);
    std::atomic<int> completed{0};
    QTimer reporter;
    if (progress) {
        progress(0, int(parts.size()));
        QObject::connect(&reporter, &QTimer::timeout, this, [&] { progress(completed.load(), int(parts.size())); });
        reporter.start(100);
    }
    runWhileResponsive([&] {
        for (const ForgeCad::ImportedPart &part : parts) {
            ExtrusionObject body;
            body.name = part.name;
            body.feature = BodyFeature::Imported;
            body.featureId=nextFeatureId++;
            body.importData = part.data;
            body.importSource = source;
            body.forgeBody = part.body;
            body.solid = part.solid;
            if (!body.forgeBody) body.error = QStringLiteral("Il corpo non ha geometria.");
            else {
                // Un secondo inserimento delle stesse parti deve avere
                // nomi propri; la registrazione dei B-rep e' immutabile.
                if(ForgeCad::featureHistoryOf(*body.forgeBody))
                    body.forgeBody=std::make_shared<const ForgeCad::Kernel::Body>(*body.forgeBody);
                ForgeCad::registerImportedHistory(body.forgeBody);
                ForgeCad::registerFeatureGenealogy(body,int(extrusions_.size()+prepared.size()),sketches_,extrusions_);
            }
            ForgeCad::tessellateGeometry(body, quality, body.display);
            prepared.append(std::move(body));
            ++completed;
        }
    });
    reporter.stop();
    if (progress) progress(int(parts.size()), int(parts.size()));
    recordUndo();
    for (ExtrusionObject &body : prepared) {
        if (!body.error.isEmpty()) failures.append(QStringLiteral("%1: %2").arg(body.name, body.error));
        extrusions_.append(std::move(body));
    }
    selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
    hover_ = {};
    documentChanged();
    fitAll();
    return failures;
}

ForgeCad::MassReport CadViewport::massReport(int index) const {
    ScopedWork work(workCallback_, QStringLiteral("Calcolo delle proprieta' di massa..."));
    ForgeCad::MassReport report;
    if (index < 0 || index >= extrusions_.size() || !ForgeCad::hasGeometry(extrusions_.at(index))) {
        report.error = QStringLiteral("il corpo non ha geometria");
        return report;
    }
    const ExtrusionObject &body = extrusions_.at(index);
    if (ForgeCad::isDatumBody(body)) {
        report.error = QStringLiteral("e' un piano di costruzione");
        return report;
    }
    if (body.curve) return ForgeCad::curveMassProperties(*body.curve);
    return ForgeCad::forgeMassProperties(*body.forgeBody);
}

void CadViewport::requestExtendPreview(int base, const QVector<EdgePoint> &edges, double distance, bool linear, int hidden ) {
    if (base < 0 || base >= extrusions_.size() || edges.isEmpty() || !(distance > 0.0)) {
        clearPreview();
        return;
    }
    ExtrusionObject body = hidden >= 0 && hidden < extrusions_.size() ? extrusions_.at(hidden) : ExtrusionObject();
    body.operation = -1;
    body.feature = BodyFeature::SheetExtend;
    body.firstBody = base;
    body.blendEdges = edges;
    body.blendSize = distance;
    body.extendLinear = linear;
    requestPreview(body, hidden);
}

QString CadViewport::toggleConstruction() {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (!sketchMode_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size())
        return QStringLiteral("Entra in modalita' schizzo e seleziona le entita' (clic o Ctrl+clic).");
    if (sketchSelections_.isEmpty()) return QStringLiteral("Seleziona prima le entita' dello schizzo (clic o Ctrl+clic).");
    const SketchObject &before = sketches_.at(activeSketch_);
    bool allConstruction = true;
    for (const SketchElementSelection &element : sketchSelections_) {
        if (element.kind == 0 && element.index < before.segments.size()) allConstruction &= before.isConstructionSegment(element.index);
        if (element.kind == 1 && element.index < before.curves.size()) allConstruction &= before.curves.at(element.index).construction;
    }
    recordUndo();
    SketchObject &sketch = sketches_[activeSketch_];
    for (const SketchElementSelection &element : sketchSelections_) {
        if (element.kind == 0 && element.index >= 0 && element.index < sketch.segments.size()) {
            sketch.constructionSegments.removeAll(element.index);
            if (!allConstruction) sketch.constructionSegments.append(element.index);
            else sketch.symmetryAxes.removeAll(element.index);  // un asse di simmetria e' sempre di costruzione
        } else if (element.kind == 1 && element.index >= 0 && element.index < sketch.curves.size()) {
            sketch.curves[element.index].construction = !allConstruction;
        }
    }
    std::sort(sketch.constructionSegments.begin(), sketch.constructionSegments.end());
    sketchEdited();
    return {};
}

QString CadViewport::toggleSymmetryAxis() {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo e seleziona i segmenti.");
    QVector<int> segments;
    for (const SketchElementSelection &element : sketchSelections_)
        if (element.kind == 0 && element.index >= 0 && element.index < sketches_.at(activeSketch_).segments.size()) segments.append(element.index);
    if (segments.isEmpty()) return QStringLiteral("Seleziona prima uno o piu' segmenti dello schizzo.");
    const SketchObject &before = sketches_.at(activeSketch_);
    bool all = true;
    for (int index : segments) all = all && before.symmetryAxes.contains(index);
    recordUndo();
    SketchObject &sketch = sketches_[activeSketch_];
    for (int index : segments) {
        sketch.symmetryAxes.removeAll(index);
        if (all) continue;
        sketch.symmetryAxes.append(index);
        if (!sketch.isConstructionSegment(index)) sketch.constructionSegments.append(index);
    }
    std::sort(sketch.constructionSegments.begin(), sketch.constructionSegments.end());
    sketchEdited();
    return {};
}

void CadViewport::markSymmetryAxis(SketchObject &sketch, const SketchConstraint &c) {
    const ConstraintRef axis = c.type == ConstraintType::Symmetric ? c.third
                             : c.type == ConstraintType::AxisRadius || c.type == ConstraintType::AxisDiameter ? c.second : ConstraintRef();
    if (axis.kind == 0 && axis.point < 0 && sketch.isConstructionSegment(axis.element) && !sketch.symmetryAxes.contains(axis.element))
        sketch.symmetryAxes.append(axis.element);
}

void CadViewport::placeAxisDimension(SketchConstraint &c, const QPointF &pointer) {
    if (c.type != ConstraintType::AxisRadius && c.type != ConstraintType::AxisDiameter) return;
    const SketchObject &sketch = sketches_.at(activeSketch_);
    QPointF p, q, a0, a1;
    if (!ForgeCad::dimensionPoints(sketch, c, p, q)) return;
    if (c.second.kind == 2) {
        a0 = QPointF(0, 0);
        a1 = c.second.element == 1 ? QPointF(1, 0) : QPointF(0, 1);
    } else if (c.second.kind == 0 && c.second.element >= 0 && c.second.element < sketch.segments.size()) {
        a0 = sketch.segments.at(c.second.element).first;
        a1 = sketch.segments.at(c.second.element).second;
    } else {
        return;
    }
    const auto side = [&](const QPointF &x) {
        const QPointF d = a1 - a0, r = x - a0;
        return d.x() * r.y() - d.y() * r.x();
    };
    const bool sameSide = side(pointer) * side(p) >= 0.0;
    const ConstraintType wanted = sameSide ? ConstraintType::AxisRadius : ConstraintType::AxisDiameter;
    if (wanted != c.type) {
        c.value = wanted == ConstraintType::AxisDiameter ? 2.0 * c.value : 0.5 * c.value;
        c.type = wanted;
    }
    c.placement = pointer;
    c.placed = true;
}

QPair<int, int> CadViewport::bodyComponents(int index) const {
    if (index < 0 || index >= extrusions_.size() || !ForgeCad::isShapeBody(extrusions_.at(index))) return {0, 0};
    const ForgeCad::ForgeBody &body = extrusions_.at(index).forgeBody;
    const auto found = componentCache_.find(body.get());
    if (found != componentCache_.end() && found->second.first.lock() == body) return found->second.second;
    int solids = 0, sheets = 0;
    ForgeCad::forgeComponentCounts(body, solids, sheets);
    if (componentCache_.size() > 512) componentCache_.clear();
    componentCache_[body.get()] = {body, {solids, sheets}};
    return {solids, sheets};
}

QString CadViewport::renameObject(SceneObjectKind kind, int index, const QString &name) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("Il nome non puo' essere vuoto.");
    if (kind == SceneObjectKind::Sketch && index >= 0 && index < sketches_.size()) {
        if (sketches_.at(index).name == trimmed) return {};
        recordUndo();
        sketches_[index].name = trimmed;
    } else if (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size()) {
        if (extrusions_.at(index).name == trimmed) return {};
        recordUndo();
        extrusions_[index].name = trimmed;
    } else {
        return QStringLiteral("Oggetto non valido.");
    }
    documentChanged();
    return {};
}

void CadViewport::selectSketchReference(int kind, int index) {
    if (!activeSketchObject()) { showStatus(QStringLiteral("Entra in modalita' schizzo.")); return; }
    SketchObject work = *activeSketchObject();
    QPointF point, direction;
    QString error, name;
    if (kind == 2 && index >= 0 && index < 3) {
        ForgeCad::Kernel::Vec3 axis;
        axis[index] = 1.0;
        error = ForgeCad::sketchAxisReference(work, {}, axis, point, direction);
        name = QStringLiteral("Asse %1").arg(QStringLiteral("XYZ").at(index));
    } else {
        SketchFrame plane;
        if (kind == 1 && index >= 0 && index < 3) {
            plane = ForgeCad::referenceSketchFrame(index, camera_.orientation_);
            name = QStringLiteral("Piano %1").arg(index == 0 ? QStringLiteral("XY") : index == 1 ? QStringLiteral("XZ") : QStringLiteral("YZ"));
        } else if (kind == 8 && index >= 0 && index < extrusions_.size() && ForgeCad::isDatumBody(extrusions_.at(index)) && extrusions_.at(index).datumValid) {
            plane = extrusions_.at(index).datumFrame;
            name = extrusions_.at(index).name;
        } else return;
        error = ForgeCad::sketchPlaneReference(work, plane, point, direction);
    }
    if (!error.isEmpty()) { showStatus(error); return; }
    const int before = int(work.segments.size());
    const int segment = ForgeCad::appendFixedReferenceLine(work, point, direction, std::max(1e-4, double(camera_.zoom_) * 0.35));
    if (work.segments.size() != before) {
        recordUndo();
        sketches_[activeSketch_] = work;
        documentChanged();
    }
    const SketchElementSelection selected{0, segment};
    const auto existing = sketchSelections_.indexOf(selected);
    if (existing >= 0) sketchSelections_.removeAt(existing);
    else {
        if (sketchSelections_.size() + selectedPoints_.size() >= 3) { sketchSelections_.clear(); selectedPoints_.clear(); }
        sketchSelections_.append(selected);
    }
    selectedConstraints_.clear();
    refreshDimensionPreview(cursorSketchPoint_);
    selectionChanged();
    showStatus(name + QStringLiteral(": riferimento di costruzione fisso; scegli il vincolo nella finestra Vincoli."));
    update();
}

bool CadViewport::pickSketchReferencePlaneAxis(const QPoint &position, int &kind, int &index) const {
    double best = 7.0;
    bool found = false;
    const auto consider = [&](const QVector<QVector3D> &corners, int candidateKind, int candidateIndex, bool closed) {
        for (int k = 0; k < corners.size() - (closed ? 0 : 1); ++k) {
            const double d = distanceToSegment(position, projectWorldPoint(corners[k]), projectWorldPoint(corners[(k + 1) % corners.size()]));
            if (d < best) { best = d; kind = candidateKind; index = candidateIndex; found = true; }
        }
    };
    for (int p = 0; p < 3; ++p) if (isPlaneShown(p)) consider(planeCorners(p), 1, p, true);
    for (int b = 0; b < extrusions_.size(); ++b)
        if (isDatumShown(b)) consider(datumCorners(extrusions_.at(b).datumFrame, extrusions_.at(b).datum.size), 8, b, true);
    if (axesVisible_)
        for (int a = 0; a < 3; ++a) {
            QVector3D tip;
            tip[a] = float(axisDisplayLength());
            if (pointDistance(projectWorldPoint(QVector3D()), projectWorldPoint(tip)) >= 2.0)
                consider({QVector3D(), tip}, 2, a, false);
        }
    return found;
}

bool CadViewport::pickSketchReference(const QPoint &position, SketchReferenceHit &hit) const {
    hit = {};
    double best = 8.0;
    for (int b = 0; b < extrusions_.size(); ++b) {
        const ExtrusionObject &body = extrusions_.at(b);
        if (!body.visible || !(ForgeCad::isShapeBody(body) || (ForgeCad::isCurveBody(body) && body.curve))) continue;
        const QVector<QVector<QVector3D>> &edges = body.display.edges;
        for (int e = 0; e < edges.size(); ++e)
            for (int k = 1; k < edges.at(e).size(); ++k) {
                const QPointF a = projectWorldPoint(edges.at(e).at(k - 1)), c = projectWorldPoint(edges.at(e).at(k));
                const double d = distanceToSegment(QPointF(position), a, c);
                if (d >= best) continue;
                best = d;
                hit.body = b;
                hit.polyline = e;
                hit.sketch = hit.entityKind = hit.entity = -1;
                // Un punto interno del tratto (non un estremo: gli estremi della
                // polilinea sono vertici comuni a piu' spigoli, e lo spigolo piu'
                // vicino a un vertice sarebbe uno qualsiasi di quelli).
                const QPointF ac = c - a;
                const double l2 = ac.x() * ac.x() + ac.y() * ac.y();
                double t = l2 > 0.0 ? ((QPointF(position) - a).x() * ac.x() + (QPointF(position) - a).y() * ac.y()) / l2 : 0.5;
                t = qBound(0.2, t, 0.8);
                hit.point = edges.at(e).at(k - 1) + float(t) * (edges.at(e).at(k) - edges.at(e).at(k - 1));
            }
    }
    // Anche le entita' degli altri schizzi visibili sono riferimenti
    // grafici. Si usano le loro coordinate 3D, quindi funziona fra piani
    // diversi e non soltanto nella vista ortogonale allo schizzo attivo.
    const auto consider = [&](int sketchIndex, int kind, int entityIndex, const QVector<QPointF> &points) {
        const SketchObject &source = sketches_.at(sketchIndex);
        for (int k = 1; k < points.size(); ++k) {
            const QVector3D wa = ForgeCad::sketchToDisplay(points.at(k - 1), source);
            const QVector3D wb = ForgeCad::sketchToDisplay(points.at(k), source);
            const double d = distanceToSegment(QPointF(position), projectWorldPoint(wa), projectWorldPoint(wb));
            if (d >= best) continue;
            best = d;
            hit.body = hit.polyline = -1;
            hit.sketch = sketchIndex;
            hit.entityKind = kind;
            hit.entity = entityIndex;
            hit.point = 0.5f * (wa + wb);
        }
    };
    for (int s = 0; s < sketches_.size(); ++s) {
        if (s == activeSketch_ || !sketches_.at(s).visible) continue;
        const SketchObject &source = sketches_.at(s);
        for (int index = 0; index < source.segments.size(); ++index) {
            const SketchSegment &segment = source.segments.at(index);
            consider(s, 0, index, {segment.first, segment.second});
        }
        for (int index = 0; index < source.curves.size(); ++index)
            consider(s, 1, index, source.curves.at(index).samples);
    }
    return hit.body >= 0 || hit.sketch >= 0;
}

QString CadViewport::appendReferenceAt(const QPoint &position, SketchObject &work, bool construction) {
    SketchReferenceHit hit;
    if (!pickSketchReference(position, hit)) return QStringLiteral("Clicca uno spigolo del modello o una curva visibile.");
    if (hit.sketch >= 0) {
        const SketchObject &source = sketches_.at(hit.sketch);
        const auto targetAxes = ForgeCad::sketchAxes(work), sourceAxes = ForgeCad::sketchAxes(source);
        if (ForgeCad::Kernel::norm(ForgeCad::Kernel::cross(targetAxes.zDir(), sourceAxes.zDir())) > 1e-10)
            return ForgeCad::appendSketchContactReference(work, source, {hit.entityKind, hit.entity});
        return ForgeCad::appendProjectedSketchEntity(work, source, {hit.entityKind, hit.entity}, construction);
    }
    const ExtrusionObject &body = extrusions_.at(hit.body);
    ForgeCad::Kernel::CurvePtr<3> curve;
    ForgeCad::Kernel::Interval range;
    if (ForgeCad::isCurveBody(body)) {
        EdgePoint near; near.x = hit.point.x(); near.y = hit.point.y(); near.z = hit.point.z();
        curve = ForgeCad::bodyCurveNear(body, near); range = curve->domain();
    }
    else {
        if(body.forgeBody) {
            // La polilinea serve al picking; la copia usa lo spigolo
            // esatto identificato dalla tessellazione, anche su curve
            // molto incurvate con corde lontane dalla geometria vera.
            const int pickedId=body.display.edgeIds.value(hit.polyline,-1);
            for(const auto id:body.forgeBody->edges()) if(id.index==pickedId) {
                const auto &edge=body.forgeBody->edge(id);curve=edge.curve;range=edge.range;break;
            }
        }
        if(!curve && (!body.forgeBody || !ForgeCad::nearestBodyEdge(*body.forgeBody,
                ForgeCad::Kernel::Vec3(hit.point.x(), hit.point.y(), hit.point.z()), curve, range)))
            return QStringLiteral("Lo spigolo non si trova sul corpo.");
    }
    return ForgeCad::appendProjectedCurve(work,curve,range,construction,true);
}

QString CadViewport::convertReferenceAt(const QPoint &position) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo.");
    SketchObject work=sketches_.at(activeSketch_);
    const QString error=appendReferenceAt(position,work,referencesConstruction_);
    if(!error.isEmpty()) return error;
    recordUndo();sketches_[activeSketch_]=work;sketchEdited();return {};
}

bool CadViewport::selectOffsetReferenceAt(const QPoint &position) {
    if(!sketchOffsetOriginal_ || !activeSketchObject()) return false;
    auto work=sketches_.at(activeSketch_);
    const int first=work.curves.size(), firstSegment=work.segments.size();
    if(!appendReferenceAt(position,work,true).isEmpty()
        || (work.curves.size()==first && work.segments.size()==firstSegment)) return false;
    for(int i=firstSegment;i<work.segments.size();++i) {
        if(!work.constructionSegments.contains(i)) work.constructionSegments.append(i);
        sketchSelections_.append({0,i});
    }
    for(int i=first;i<work.curves.size();++i) {
        work.curves[i].construction=true;
        ForgeCad::recalculateCurve(work.curves[i],tessellationQuality_);
        sketchSelections_.append({1,i});
    }
    sketches_[activeSketch_]=work;
    selectionChanged();update();return true;
}

QString CadViewport::addSectionReferences(int *added ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    ScopedWork progressWork(workCallback_, QStringLiteral("Calcolo delle sezioni sul piano di schizzo..."));
    if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo.");
    const int entitiesBefore = sketches_.at(activeSketch_).segments.size() + sketches_.at(activeSketch_).curves.size();
    SketchObject work = sketches_.at(activeSketch_);
    int cut = 0;
    QString last;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const ExtrusionObject &body : extrusions_) {
        if (!body.visible || !ForgeCad::isShapeBody(body)) continue;
        SketchObject attempt = work;
        const QString error = ForgeCad::appendSectionCurves(attempt, *body.forgeBody, referencesConstruction_, true);
        if (error.isEmpty()) {
            work = attempt;
            ++cut;
        } else {
            last = body.name + QStringLiteral(": ") + error;
        }
    }
    QApplication::restoreOverrideCursor();
    if (cut == 0) return last.isEmpty() ? QStringLiteral("Nessun solido o superficie visibile.") : last;
    if (added) *added = work.segments.size() + work.curves.size() - entitiesBefore;
    recordUndo();
    sketches_[activeSketch_] = work;
    sketchEdited();
    return {};
}

QString CadViewport::addSketchContactReferences(int *added ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    ScopedWork progressWork(workCallback_, QStringLiteral("Calcolo dei contatti con gli altri schizzi..."));
    if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo.");
    SketchObject work = sketches_.at(activeSketch_);
    const int before = work.segments.size() + work.curves.size();
    int sources = 0;
    QString last;
    for (int index = 0; index < sketches_.size(); ++index) {
        if (index == activeSketch_ || !sketches_.at(index).visible) continue;
        SketchObject attempt = work;
        const QString error = ForgeCad::appendSketchContactReferences(attempt, sketches_.at(index));
        if (error.isEmpty()) {
            work = std::move(attempt);
            ++sources;
        } else {
            last = sketches_.at(index).name + QStringLiteral(": ") + error;
        }
    }
    if (sources == 0) return last.isEmpty() ? QStringLiteral("Nessun altro schizzo visibile.") : last;
    if (added) *added = work.segments.size() + work.curves.size() - before;
    recordUndo();
    sketches_[activeSketch_] = std::move(work);
    sketchEdited();
    return {};
}

void CadViewport::setReferencesConstruction(bool construction) { referencesConstruction_ = construction; }

QVector<SketchElementSelection> CadViewport::sketchSelection() const { return sketchMode_ ? sketchSelections_ : QVector<SketchElementSelection>(); }

QVector<QPointF> CadViewport::selectedSketchPoints() const {
    QVector<QPointF> points;
    if (!activeSketchObject()) return points;
    for (const SelectedPoint &point : selectedPoints_)
        if (isValidSelectedPoint(point)) points.append(selectedPointPosition(point));
    return points;
}

QVector<ConstraintRef> CadViewport::selectedSketchPointRefs() const {
    QVector<ConstraintRef> refs;
    if (!activeSketchObject()) return refs;
    for (const SelectedPoint &point : selectedPoints_)
        if (isValidSelectedPoint(point)) refs.append({point.kind, point.element, point.kind == 2 ? -1 : point.point});
    return refs;
}

bool CadViewport::activeSegment(int index, SketchSegment &segment) const {
    const SketchObject *sketch = activeSketchObject();
    if (!sketch || index < 0 || index >= sketch->segments.size()) return false;
    segment = sketch->segments.at(index);
    return true;
}

QVector<ForgeCad::SketchEntity> CadViewport::patternEntities(int excluded) const {
    QVector<ForgeCad::SketchEntity> entities;
    for (const SketchElementSelection &element : sketchSelections_)
        if (!(element.kind == 0 && element.index == excluded)) entities.append({element.kind, element.index});
    return entities;
}

QString CadViewport::previewSketchEdit(const SketchEdit &edit) {
    sketchPatternPreview_.clear();
    const SketchObject *active = activeSketchObject();
    if (!active) return QStringLiteral("Entra in modalita' schizzo.");
    SketchObject work = *active;
    QVector<ForgeCad::SketchEntity> created;
    const ForgeCad::SketchEditResult result = edit(work, &created);
    if (result.error.isEmpty())
        for (const ForgeCad::SketchEntity &entity : created) {
            if (entity.kind == 0) {
                const SketchSegment &segment = work.segments.at(entity.index);
                sketchPatternPreview_.append({segment.first, segment.second});
            } else {
                CurveObject curve = work.curves.at(entity.index);
                ForgeCad::recalculateCurve(curve, tessellationQuality_);
                sketchPatternPreview_.append(curve.samples);
            }
        }
    update();
    return result.error;
}

QString CadViewport::applySketchEdit(const SketchEdit &edit) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo.");
    SketchObject work = sketches_.at(activeSketch_);
    QVector<ForgeCad::SketchEntity> created;
    const ForgeCad::SketchEditResult result = edit(work, &created);
    if (!result.error.isEmpty()) return result.error;
    recordUndo();
    for (CurveObject &curve : work.curves)
        if (curve.samples.isEmpty()) ForgeCad::recalculateCurve(curve, tessellationQuality_);
    sketches_[activeSketch_] = work;
    sketchSelections_.clear();
    for (const ForgeCad::SketchEntity &entity : created) sketchSelections_.append({entity.kind, entity.index});
    sketchPatternPreview_.clear();
    sketchEdited();
    return {};
}

QString CadViewport::previewSketchPattern(const ForgeCad::SketchPattern &pattern, int excluded) {
    return previewSketchEdit([&](SketchObject &work, QVector<ForgeCad::SketchEntity> *created) {
        return ForgeCad::patternSketchEntities(work, patternEntities(excluded), pattern, created);
    });
}

QString CadViewport::applySketchPattern(const ForgeCad::SketchPattern &pattern, int excluded) {
    return applySketchEdit([&](SketchObject &work, QVector<ForgeCad::SketchEntity> *created) {
        return ForgeCad::patternSketchEntities(work, patternEntities(excluded), pattern, created);
    });
}

void CadViewport::setSketchOffsetSelectionCallback(std::function<void()> callback) {
    if(callback && !sketchOffsetSelectionCallback_) {
        sketchOffsetOriginal_=sketches_.at(activeSketch_);
        sketchOffsetOriginalSelection_=sketchSelections_;
        sketchOffsetViewUnlocked_=sketchViewUnlocked_;
        setSketchViewUnlocked(false);
    } else if(!callback && sketchOffsetSelectionCallback_) {
        if(sketchOffsetOriginal_) {
            sketches_[activeSketch_]=*sketchOffsetOriginal_;
            sketchSelections_=sketchOffsetOriginalSelection_;
            sketchOffsetOriginal_.reset();
        }
        setSketchViewUnlocked(sketchOffsetViewUnlocked_);
    }
    sketchOffsetSelectionCallback_ = std::move(callback);
    update();
}

QString CadViewport::previewSketchOffset(const ForgeCad::SketchOffset &offset) {
    return previewSketchEdit([&](SketchObject &work, QVector<ForgeCad::SketchEntity> *created) {
        return ForgeCad::offsetSketchEntities(work, patternEntities(-1), offset, created);
    });
}

QString CadViewport::applySketchOffset(const ForgeCad::SketchOffset &offset) {
    if(!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo.");
    const auto staged=sketches_.at(activeSketch_);
    auto committed=staged;
    auto sources=patternEntities(-1);
    if(sketchOffsetOriginal_) {
        QSet<int> unused;
        for(int i=sketchOffsetOriginal_->curves.size();i<staged.curves.size();++i) {
            bool selected=false;
            for(const auto &e:sources) selected=selected || (e.kind==1 && e.index==i);
            if(!selected) unused.insert(i);
        }
        QSet<int> unusedSegments;
        for(int i=sketchOffsetOriginal_->segments.size();i<staged.segments.size();++i) {
            bool selected=false;
            for(const auto &e:sources) selected=selected || (e.kind==0 && e.index==i);
            if(!selected) unusedSegments.insert(i);
        }
        ForgeCad::removeSketchEntities(committed,unusedSegments,unused);
        for(auto &e:sources) if(e.kind==0) {
            int removed=0;for(int i:unusedSegments) removed+=i<e.index;
            e.index-=removed;
        }
        for(auto &e:sources) if(e.kind==1) {
            int removed=0;for(int i:unused) removed+=i<e.index;
            e.index-=removed;
        }
    }
    // Un solo Undo comprende la copia dei bordi e l'offset. I bordi
    // provvisori non rimangono nel documento quando si annulla.
    if(sketchOffsetOriginal_) sketches_[activeSketch_]=*sketchOffsetOriginal_;
    const auto error=applySketchEdit([&](SketchObject &work, QVector<ForgeCad::SketchEntity> *created) {
        work=committed;
        return ForgeCad::offsetSketchEntities(work, sources, offset, created);
    });
    if(!error.isEmpty()) sketches_[activeSketch_]=staged;
    else sketchOffsetOriginal_.reset();
    return error;
}

QString CadViewport::previewSketchMove(const ForgeCad::SketchMove &move) {
    return previewSketchEdit([&](SketchObject &work, QVector<ForgeCad::SketchEntity> *created) {
        return ForgeCad::moveSketchEntities(work, patternEntities(-1), move, created);
    });
}

QString CadViewport::applySketchMove(const ForgeCad::SketchMove &move) {
    return applySketchEdit([&](SketchObject &work, QVector<ForgeCad::SketchEntity> *created) {
        return ForgeCad::moveSketchEntities(work, patternEntities(-1), move, created);
    });
}

void CadViewport::clearSketchPatternPreview() {
    sketchPatternPreview_.clear();
    update();
}

const SketchObject *CadViewport::activeSketchObject() const {
    return sketchMode_ && activeSketch_ >= 0 && activeSketch_ < sketches_.size() ? &sketches_.at(activeSketch_) : nullptr;
}

QVector<ConstraintRef> CadViewport::constraintSelection() const {
    QVector<ConstraintRef> refs;
    for (const SelectedPoint &point : selectedPoints_)
        if (isValidSelectedPoint(point)) refs.append({point.kind, point.element, point.kind == 2 ? -1 : point.point});
    for (const SketchElementSelection &element : sketchSelections_) refs.append({element.kind, element.index, -1});
    return refs;
}

int CadViewport::pierceSourceIndex(const GeometryRef &source) const {
    if (source.featureId) {
        for (int i=0;i<extrusions_.size();++i) if (extrusions_.at(i).featureId==source.featureId) return i;
        return -1;
    }
    return source.index>=0 && source.index<extrusions_.size() ? source.index : -1;
}

QString CadViewport::addSurfacePierce(const ConstraintRef &point, GeometryRef source) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const SketchObject *active=activeSketchObject();
    if (!active || !ForgeCad::applicableConstraints(*active,{point}).contains(ConstraintType::Pierce))
        return QStringLiteral("Seleziona un punto dello schizzo con Ctrl+clic.");
    ForgeCad::normalizeModelHistory(extrusions_,modelBodies_);
    const int owner=pierceSourceIndex(source);
    if (source.kind!=5 || owner<0 || !extrusions_.at(owner).forgeBody)
        return QStringLiteral("Scegli una faccia della superficie o del solido.");
    source.index=owner; source.featureId=extrusions_.at(owner).featureId;
    // Un corpo generato da questo schizzo creerebbe un ciclo. Seguire
    // anche le dipendenze Pierce degli schizzi intermedi.
    QSet<int> visited;
    std::function<bool(int)> cyclic=[&](int index) {
        if (index<0 || index>=extrusions_.size() || visited.contains(index)) return false;
        visited.insert(index);
        for (int sketch : ForgeCad::sketchesOf(extrusions_.at(index))) {
            if (sketch==activeSketch_) return true;
            if (sketch<0 || sketch>=sketches_.size()) continue;
            for (const auto &c : sketches_.at(sketch).geometricConstraints)
                if (c.type==ConstraintType::Pierce && c.pierce && cyclic(pierceSourceIndex(c.pierce->surface))) return true;
        }
        for (int operand : ForgeCad::bodyOperands(extrusions_.at(index))) if (cyclic(operand)) return true;
        return false;
    };
    if (cyclic(owner)) return QStringLiteral("Pierce creerebbe una dipendenza circolare con lo schizzo.");
    for (int i=0;i<=owner;++i) if (ForgeCad::sketchesOf(extrusions_.at(i)).contains(activeSketch_))
        return QStringLiteral("La superficie Pierce deve precedere le funzioni che usano questo schizzo.");
    for (const auto &c : active->geometricConstraints)
        if (c.type==ConstraintType::Pierce && c.first==point && c.pierce
            && c.pierce->surface.featureId==source.featureId && c.pierce->surface.point.subshape==source.point.subshape)
            return QStringLiteral("Il vincolo Pierce su questa faccia c'e' gia'.");
    SketchObject work=*active;
    SketchConstraint constraint; constraint.type=ConstraintType::Pierce; constraint.first=point;
    const QString error=ForgeCad::refreshSurfacePierce(work,constraint,*extrusions_.at(owner).forgeBody,source,extrusions_.at(owner).name);
    if (!error.isEmpty()) return error;
    work.geometricConstraints.append(constraint);
    const auto solved=ForgeCad::solveSketch(work);
    if (!solved.ok) return solved.error;
    recordUndo();
    sketches_[activeSketch_]=std::move(work);
    selectedConstraints_={int(sketches_.at(activeSketch_).geometricConstraints.size()-1)};
    sketchEdited();
    return {};
}

QString CadViewport::beginSurfacePierce() {
    const auto refs=constraintSelection();
    if (!activeSketchObject() || refs.size()!=1
        || !ForgeCad::applicableConstraints(*activeSketchObject(),refs).contains(ConstraintType::Pierce))
        return QStringLiteral("Seleziona un solo punto dello schizzo con Ctrl+clic, poi Pierce su superficie.");
    if (refPicking_ || edgePicking_ || sketchViewUnlocked_)
        return QStringLiteral("Termina prima la scelta corrente.");
    const ConstraintRef point=refs.first();
    const int sketch=activeSketch_;
    setSketchViewUnlocked(true);
    setReferencePickCallback([this,point,sketch](bool picked,GeometryRef ref) {
        setReferencePickCallback(nullptr);
        setSketchViewUnlocked(false);
        if (!picked) { showStatus(QStringLiteral("Pierce annullato.")); return; }
        if (activeSketch_!=sketch) { showStatus(QStringLiteral("Lo schizzo attivo e' cambiato.")); return; }
        const QString error=addSurfacePierce(point,ref);
        showStatus(error.isEmpty() ? QStringLiteral("Vincolo Pierce sulla superficie aggiunto.") : error);
    });
    const QString error=beginReferencePick(ForgeCad::DatumRoleFace,-1);
    if (!error.isEmpty()) { setReferencePickCallback(nullptr); setSketchViewUnlocked(false); return error; }
    showStatus(QStringLiteral("Pierce: scegli una faccia; trascina per ruotare la vista, Esc annulla."));
    return {};
}

QString CadViewport::addConstraint(ConstraintType type, double value ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (type==ConstraintType::Pierce) return beginSurfacePierce();
    const SketchObject *active = activeSketchObject();
    if (!active) return QStringLiteral("Entra in modalita' schizzo.");
    const QVector<ConstraintRef> refs = constraintSelection();
    if (!ForgeCad::applicableConstraints(*active, refs).contains(type))
        return QStringLiteral("Il vincolo %1 non si applica alle entita' scelte.").arg(ForgeCad::constraintName(type));
    SketchConstraint constraint = ForgeCad::makeConstraint(*active, type, refs);
    if (ForgeCad::isDimension(type) && !std::isnan(value)) constraint.value = value;
    for (const SketchConstraint &other : active->geometricConstraints)
        if (other.type == constraint.type
            && ((other.first == constraint.first && other.second == constraint.second) || (other.first == constraint.second && other.second == constraint.first)))
            return QStringLiteral("Il vincolo c'e' gia'.");
    const DocumentState snapshot = documentState();
    SketchObject &sketch = sketches_[activeSketch_];
    const SketchObject before = sketch;
    sketch.geometricConstraints.append(constraint);
    markSymmetryAxis(sketch, constraint);
    QString failure;
    if (!solveActive({}, before, &failure)) return failure;
    history_.record(snapshot);
    selectedConstraints_ = {int(sketch.geometricConstraints.size()) - 1};
    if (type == ConstraintType::AxisRadius || type == ConstraintType::AxisDiameter) {
        dimensionPlacing_ = int(sketch.geometricConstraints.size()) - 1;
        setFocus();
    }
    sketchEdited();
    selectionChanged();
    return {};
}

QString CadViewport::deleteConstraints(QVector<int> indices) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (!activeSketchObject() || indices.isEmpty()) return {};
    recordUndo();
    SketchObject &sketch = sketches_[activeSketch_];
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    dimensionPlacing_ = -1;
    for (int k = indices.size() - 1; k >= 0; --k)
        if (indices.at(k) >= 0 && indices.at(k) < sketch.geometricConstraints.size()) sketch.geometricConstraints.removeAt(indices.at(k));
    selectedConstraints_.clear();
    constraintHover_ = -1;
    sketchEdited();
    selectionChanged();
    return {};
}

QString CadViewport::setConstraintValue(int index, double value, std::optional<ConstraintType> type ) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (!activeSketchObject() || index < 0 || index >= activeSketchObject()->geometricConstraints.size()) return QStringLiteral("Vincolo non valido.");
    if (!ForgeCad::isDimension(activeSketchObject()->geometricConstraints.at(index).type)) return QStringLiteral("Il vincolo non ha un valore.");
    const DocumentState snapshot = documentState();
    SketchObject &sketch = sketches_[activeSketch_];
    const SketchObject before = sketch;
    SketchConstraint &constraint = sketch.geometricConstraints[index];
    const bool axial = constraint.type == ConstraintType::AxisRadius || constraint.type == ConstraintType::AxisDiameter;
    if (type && axial && (*type == ConstraintType::AxisRadius || *type == ConstraintType::AxisDiameter)) constraint.type = *type;
    constraint.value = value;
    QString failure;
    if (!solveActive({}, before, &failure)) return failure;
    history_.record(snapshot);
    sketchEdited();
    selectionChanged();
    return {};
}

QString CadViewport::setSketchPatternValues(int index, const SketchPatternData &values) {
    if(documentReadOnly_)return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    const SketchObject *active = activeSketchObject();
    if (!active || index < 0 || index >= active->geometricConstraints.size() || active->geometricConstraints.at(index).type != ConstraintType::Pattern)
        return QStringLiteral("La ripetizione non esiste piu'.");
    const DocumentState snapshot = documentState();
    const SketchObject before = *active;
    SketchObject work = before;
    const ForgeCad::SketchEditResult result = ForgeCad::editSketchPattern(work, index, values);
    if (!result.error.isEmpty()) return result.error;
    QVector<ForgeCad::PointTarget> targets;
    const SketchPatternData &data = (result.segmentMap.isEmpty() ? work.geometricConstraints.at(index) : work.geometricConstraints.last()).pattern;
    for (const ConstraintRef &source : data.sources) {
        if (source.kind == 0) {
            targets.append({{0, source.element, 0}, work.segments.at(source.element).first});
            targets.append({{0, source.element, 1}, work.segments.at(source.element).second});
        } else {
            for (int k = 0; k < work.curves.at(source.element).controlPoints.size(); ++k)
                targets.append({{1, source.element, k}, work.curves.at(source.element).controlPoints.at(k)});
        }
    }
    sketches_[activeSketch_] = work;
    QString failure;
    if (!solveActive(targets, work, &failure) && !solveActive({}, work, &failure)) {
        sketches_[activeSketch_] = before;
        for (CurveObject &curve : sketches_[activeSketch_].curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
        return failure;
    }
    if (!result.segmentMap.isEmpty()) {
        remapSketchReferences(activeSketch_, result.segmentMap);
        sketchSelections_.clear();
        selectedPoints_.clear();
        selectedConstraints_.clear();
        sketchHover_ = {};
    }
    history_.record(snapshot);
    sketchEdited();
    selectionChanged();
    return {};
}

void CadViewport::editSketchPatternValues(int index) {
    if(documentReadOnly_)return;
    const SketchObject *active = activeSketchObject();
    if (!active || index < 0 || index >= active->geometricConstraints.size()) return;
    SketchPatternData values = active->geometricConstraints.at(index).pattern;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Ripetizione"));
    auto *form = new QFormLayout(&dialog);
    form->addRow(wrappedNote(ForgeCad::describeConstraint(*active, active->geometricConstraints.at(index)), &dialog));
    const auto spin = [&dialog](double value, double lo, double hi, const QString &suffix = QString()) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setDecimals(6);
        box->setRange(lo, hi);
        box->setValue(value);
        box->setSuffix(suffix);
        return box;
    };
    auto *count = new QSpinBox(&dialog), *count2 = new QSpinBox(&dialog);
    count->setRange(1, 1000);
    count->setValue(values.count);
    count2->setRange(1, 1000);
    count2->setValue(values.count2);
    QDoubleSpinBox *spacing = spin(values.spacing, -1e6, 1e6), *spacing2 = spin(values.spacing2, -1e6, 1e6);
    QDoubleSpinBox *angle = spin(values.angle, -360.0, 360.0, QStringLiteral(" °"));
    auto *spread = new QCheckBox(QStringLiteral("Angolo totale (360 = giro intero)"), &dialog);
    spread->setChecked(values.spread);
    auto *dimensioned = new QCheckBox(values.kind == 1 ? QStringLiteral("Angolo quotato") : QStringLiteral("Passo quotato"), &dialog);
    dimensioned->setChecked(values.dimensioned);
    auto *dimensioned2 = new QCheckBox(QStringLiteral("Passo 2 quotato"), &dialog);
    dimensioned2->setChecked(values.dimensioned2);
    if (values.kind == 0) {
        form->addRow(QStringLiteral("Istanze:"), count);
        form->addRow(QStringLiteral("Passo:"), spacing);
        form->addRow(QString(), dimensioned);
        form->addRow(QStringLiteral("Istanze 2:"), count2);
        form->addRow(QStringLiteral("Passo 2:"), spacing2);
        form->addRow(QString(), dimensioned2);
    } else if (values.kind == 1) {
        form->addRow(QStringLiteral("Istanze:"), count);
        form->addRow(QStringLiteral("Angolo:"), angle);
        form->addRow(QString(), spread);
        form->addRow(QString(), dimensioned);
    } else {
        form->addRow(wrappedNote(QStringLiteral("Lo specchio non ha parametri: la retta e' quella scelta."), &dialog));
    }
    form->addRow(wrappedNote(QStringLiteral("Un parametro non quotato resta libero: il risolutore lo cambia\n"
                                           "(trascinando una copia) e lo schizzo ha un grado di liberta' in piu'."), &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    runUntilApplied(dialog, form, buttons, [&] {
        values.count = count->value();
        values.count2 = count2->value();
        values.spacing = spacing->value();
        values.spacing2 = spacing2->value();
        values.angle = angle->value();
        values.spread = spread->isChecked();
        values.dimensioned = dimensioned->isChecked();
        values.dimensioned2 = dimensioned2->isChecked();
        return setSketchPatternValues(index, values);
    });
}

void CadViewport::editConstraintValue(int index) {
    const SketchObject *active = activeSketchObject();
    if (!active || index < 0 || index >= active->geometricConstraints.size()) return;
    const SketchConstraint constraint = active->geometricConstraints.at(index);
    if (constraint.type == ConstraintType::Pattern) {
        editSketchPatternValues(index);
        return;
    }
    if (!ForgeCad::isDimension(constraint.type)) return;
    // Valore (a espressioni); per le quote dall'asse un tasto passa da raggio a diametro e viceversa.
    const bool axial = constraint.type == ConstraintType::AxisRadius || constraint.type == ConstraintType::AxisDiameter;
    ConstraintType type = constraint.type;
    QDialog dialog(this);
    dialog.setWindowTitle(ForgeCad::constraintName(type));
    auto *form = new QFormLayout(&dialog);
    auto *description = new QLabel(ForgeCad::describeConstraint(*active, constraint), &dialog);
    form->addRow(description);
    auto *valueBox = new ForgeCad::ExpressionSpinBox(&dialog);
    valueBox->setDecimals(6);
    valueBox->setRange(type == ConstraintType::Angle ? -360.0 : 1e-9, 1e6);
    if (type == ConstraintType::Angle) valueBox->setSuffix(QStringLiteral(" °"));
    valueBox->setValue(type == ConstraintType::Angle ? std::fabs(constraint.value) : constraint.value);
    form->addRow(QStringLiteral("Valore:"), valueBox);
    QPushButton *swapButton = nullptr;
    if (axial) {
        swapButton = new QPushButton(&dialog);
        const auto label = [&] {
            swapButton->setText(type == ConstraintType::AxisRadius ? QStringLiteral("Raggio → diametro") : QStringLiteral("Diametro → raggio"));
            dialog.setWindowTitle(ForgeCad::constraintName(type));
        };
        label();
        connect(swapButton, &QPushButton::clicked, &dialog, [&, label] {
            valueBox->interpretText();
            const bool toDiameter = type == ConstraintType::AxisRadius;
            type = toDiameter ? ConstraintType::AxisDiameter : ConstraintType::AxisRadius;
            valueBox->setValue(toDiameter ? 2.0 * valueBox->value() : 0.5 * valueBox->value());
            label();
        });
        form->addRow(QStringLiteral("Tipo:"), swapButton);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    valueBox->setFocus();
    valueBox->selectAll();
    runUntilApplied(dialog, form, buttons, [&] {
        valueBox->interpretText();
        const double value = type == ConstraintType::Angle ? signedAngleInput(valueBox->value(), constraint.value) : valueBox->value();
        return setConstraintValue(index, value, type);
    });
}

const ForgeCad::SketchAnalysis &CadViewport::sketchAnalysis() const {
    if (analysisDirty_ || analysisSketch_ != activeSketch_) {
        analysis_ = activeSketchObject() ? ForgeCad::analyzeSketch(*activeSketchObject()) : ForgeCad::SketchAnalysis();
        analysisSketch_ = activeSketch_;
        analysisDirty_ = false;
    }
    return analysis_;
}

QVector<int> CadViewport::selectedConstraints() const { return selectedConstraints_; }

void CadViewport::setSelectedConstraints(const QVector<int> &indices) {
    selectedConstraints_ = indices;
    update();
}

void CadViewport::setConstraintsVisible(bool visible) {
    constraintsVisible_ = visible;
    update();
}

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

void CadViewport::drawFeatureHandle(QPainter &painter) {
    if (!featureHandleGeometry(featureHandleOrigin_, featureHandleAxis_, featureHandleRadial_)) {
        featureHandlePath_.clear();
        if (featureHandleBox_) featureHandleBox_->hide();
        return;
    }
    const bool angular = featureHandleDefinition_.feature == BodyFeature::Revolution;
    const double value = featureHandleValue();
    const float unit = camera_.zoom_ / qMax(1, height());
    featureHandleRadius_ = 90.0f * unit;
    featureHandlePath_.clear();
    const QPointF sketchCenter = projectWorldPoint(featureHandleSketchCenter(sketches_.at(featureHandleDefinition_.sketchIndex)));
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(255, 194, 100), 1.5, Qt::DashLine));
    if (angular) {
        const int steps = qMax(2, int(std::abs(value) / 3.0));
        for (int k = 0; k <= steps; ++k)
            featureHandlePath_ << projectWorldPoint(featureHandleOrigin_ + featureHandleRadius_ * featureHandleRadialAt(value * k / steps));
        painter.drawLine(sketchCenter, featureHandlePath_.first());
        painter.drawLine(projectWorldPoint(featureHandleOrigin_), featureHandlePath_.last());
    } else {
        // Nella simmetrica il parametro rappresenta la distanza totale.
        const double length = featureHandleDefinition_.extrudeSides == 1 ? value * 0.5 : value;
        featureHandlePath_ << projectWorldPoint(featureHandleOrigin_) << projectWorldPoint(featureHandleOrigin_ + float(length) * featureHandleAxis_);
    }
    painter.setPen(QPen(QColor(255, 174, 70), featureHandleDragging_ ? 4.0 : 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawPolyline(featureHandlePath_);
    const QPointF tip = featureHandlePath_.last();
    QPointF tangent = tip - featureHandlePath_.at(featureHandlePath_.size() - 2);
    if (std::hypot(tangent.x(), tangent.y()) < 0.01) tangent = QPointF(0, value < 0 ? 1 : -1);
    tangent /= std::hypot(tangent.x(), tangent.y());
    const QPointF normal(-tangent.y(), tangent.x());
    painter.setBrush(QColor(255, 194, 100));
    painter.drawPolygon(QPolygonF({tip, tip - 14 * tangent + 6 * normal, tip - 14 * tangent - 6 * normal}));
    painter.drawEllipse(featureHandlePath_.first(), 3, 3);
    painter.setBrush(QColor(255, 174, 70));
    painter.drawEllipse(tip, 5, 5);
    painter.restore();
    const int maxX = qMax(0, width() - featureHandleBox_->width());
    const int maxY = qMax(0, height() - featureHandleBox_->height());
    QPoint boxPosition(qRound(qBound(0.0, tip.x() + 18, double(maxX))),
                       qRound(qBound(0.0, tip.y() - 16, double(maxY))));
    for (auto *panel : findChildren<QDialog *>(QStringLiteral("functionDialogPanel"), Qt::FindDirectChildrenOnly)) {
        if (panel->isVisible() && panel->geometry().intersects(QRect(boxPosition, featureHandleBox_->size()))) {
            boxPosition.setX(qBound(0, panel->x() - featureHandleBox_->width() - 12, maxX));
        }
    }
    featureHandleBox_->move(boxPosition);
    featureHandleBox_->show();
    featureHandleBox_->raise();
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

void CadViewport::drawSectionHandle(QPainter &painter) const {
    if (!section_.enabled || !section_.showPlane || !sectionDragged_ || sketchMode_) return;
    bool disc = false;
    const QPolygonF shape = sectionHandleShape(&disc);
    const bool active = sectionHover_ || sectionDragging_;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QColor halo = kSelectionColor;
    halo.setAlpha(active ? 120 : 80);
    painter.setPen(QPen(halo, active ? 8.0 : 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(shape);
    painter.setPen(QPen(kSelectionColor, active ? 2.4 : 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(active ? QColor(255, 175, 95) : QColor(245, 140, 60));
    painter.drawPolygon(shape);
    if (disc) {
        painter.setBrush(kSelectionColor);
        painter.drawEllipse(shape.boundingRect().center(), 2.5, 2.5);
    }
    painter.restore();
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

QVector3D CadViewport::viewerDirection() const {
    QVector3D origin, direction;
    viewRay(QPoint(width() / 2, height() / 2), origin, direction);
    return -direction.normalized();
}

SketchFrame CadViewport::curveTrimView() const {
    QVector3D a, b, d;
    viewRay(QPoint(width()/2, height()/2), a, d);
    viewRay(QPoint(width()/2+100, height()/2), b, d);
    const QVector3D x=(b-a).normalized(), n=-d.normalized();
    SketchFrame frame;
    for (int k=0;k<3;++k) {frame.origin[k]=0;frame.xAxis[k]=x[k];frame.normal[k]=n[k];}
    return frame;
}

void CadViewport::setSpatialEditHistory(std::function<void()> undo,std::function<void()> redo) {spatialUndo_=std::move(undo);spatialRedo_=std::move(redo);}

void CadViewport::setSpatialDrawing(std::function<void(SpatialPoint)> add, std::function<void()> finish, int owner) {
    spatialAdd_=std::move(add);spatialFinish_=std::move(finish);spatialOwner_=owner;
    spatialPressed_=false;spatialHoverValid_=false;selectionPending_=false;
    if(spatialAdd_){cancelReferencePick();cancelEdgePick();setCursor(Qt::CrossCursor);}
    else {spatialNodes_.clear();unsetCursor();}
    update();
}

void CadViewport::configureSpatialDrawing(int plane,double depth,const SpatialPoint &anchor,const QVector<SpatialPoint> &nodes,bool snap) {
    spatialPlane_=plane;spatialDepth_=depth;spatialAnchor_=anchor;spatialNodes_=nodes;spatialSnap_=snap;update();
}

bool CadViewport::spatialPointAt(const QPoint &position,SpatialPoint &point,bool allowSnap) const {
    using namespace ForgeCad::Kernel;
    double best=9;bool found=false;
    const auto consider=[&](const Vec3 &q){
        const double d=pointDistance(projectWorldPoint(QVector3D(float(q.x()),float(q.y()),float(q.z()))),QPointF(position));
        if(d<best){best=d;point={q.x(),q.y(),q.z()};found=true;}
    };
    if(allowSnap && spatialSnap_) {
        for(const auto &p:spatialNodes_)consider({p.x,p.y,p.z});
        for(const auto &sketch:sketches_)if(sketch.visible && (spatialOwner_<0 || sketch.datumPlane<spatialOwner_)) {
            for(const auto &s:sketch.segments){consider(ForgeCad::sketchToWorld(s.first,sketch));consider(ForgeCad::sketchToWorld(s.second,sketch));}
            for(const auto &c:sketch.curves)for(const auto &p:c.controlPoints)consider(ForgeCad::sketchToWorld(p,sketch));
        }
        for(int i=0;i<extrusions_.size();++i) {
            const auto &body=extrusions_[i];
            if(!body.visible || body.suppressed || (spatialOwner_>=0 && i>=spatialOwner_))continue;
            if(body.forgeBody)for(auto id:body.forgeBody->vertices())consider(body.forgeBody->vertex(id).point);
            if(body.curve) {
                const auto curves=body.curves.isEmpty()?QVector<ForgeCad::ForgeCurve>{body.curve}:body.curves;
                for(const auto &c:curves)if(c){consider(c->point(c->domain().lo));consider(c->point(c->domain().hi));}
            }
            if(body.feature==BodyFeature::SpatialSketch)
                for(const auto &e:body.spatialSketch.entities)for(const auto &p:e.points)consider({p.x,p.y,p.z});
        }
        if(found)return true;
    }
    QVector3D rayOrigin,rayDirection;viewRay(position,rayOrigin,rayDirection);
    Vec3 origin(rayOrigin.x(),rayOrigin.y(),rayOrigin.z()),direction(rayDirection.x(),rayDirection.y(),rayDirection.z());
    Vec3 normal,anchor(spatialAnchor_.x,spatialAnchor_.y,spatialAnchor_.z);
    if(spatialPlane_==1){normal={0,0,1};anchor={0,0,spatialDepth_};}
    else if(spatialPlane_==2){normal={0,1,0};anchor={0,spatialDepth_,0};}
    else if(spatialPlane_==3){normal={1,0,0};anchor={spatialDepth_,0,0};}
    else {const auto frame=curveTrimView();normal={frame.normal[0],frame.normal[1],frame.normal[2]};anchor=anchor+spatialDepth_*normal;}
    const double denominator=dot(direction,normal);
    if(std::fabs(denominator)<1e-8)return false;
    const Vec3 q=origin+(dot(anchor-origin,normal)/denominator)*direction;
    if(!std::isfinite(norm(q)))return false;
    point={q.x(),q.y(),q.z()};return true;
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

bool CadViewport::constraintsVisible() const { return constraintsVisible_; }

void CadViewport::setConstraintPanelCallback(std::function<void()> callback) { constraintPanelCallback_ = std::move(callback); }

void CadViewport::selectPlane(int plane) { selectObject(SceneObjectKind::Plane, plane); }

int CadViewport::createSketch(int plane, const QString &name) {
    if(documentReadOnly_)return -1;
    if(sketchMode_)endSketchMode();
    recordUndo();
    SketchObject sketch;
    sketch.name = name;
    sketch.plane = plane;
    // Gli assi dello schizzo sono quelli dello schermo nella vista normale al piano.
    sketch.frame = ForgeCad::referenceSketchFrame(plane, camera_.orientation_);
    sketch.customFrame = true;
    sketches_.append(sketch);
    activeSketch_ = sketches_.size() - 1;
    documentChanged();
    beginSketchMode(plane);
    return activeSketch_;
}

int CadViewport::createFaceSketch(int body, const FaceHit &face, const QString &name) {
    if(documentReadOnly_)return -1;
    if (!face.planar || body < 0 || body >= extrusions_.size()) return -1;
    if (sketchMode_) endSketchMode();
    if (edgePicking_) cancelEdgePick();
    SketchObject sketch;
    sketch.name = name;
    sketch.plane = kFacePlane;
    sketch.frame = ForgeCad::faceSketchFrame(ForgeCad::Kernel::Vec3(face.point[0], face.point[1], face.point[2]),
                                             normalized(ForgeCad::Kernel::Vec3(face.normal[0], face.normal[1], face.normal[2])),
                                             ForgeCad::Kernel::Vec3(camera_.orientation_.up[0], camera_.orientation_.up[1], camera_.orientation_.up[2]));
    sketch.faceSource = extrusions_.at(body).name;
    recordUndo();
    sketches_.append(sketch);
    activeSketch_ = sketches_.size() - 1;
    selection_ = {};
    documentChanged();
    beginSketchMode(kFacePlane);
    return activeSketch_;
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

QString CadViewport::createSketchOnSelectedFace(const QString &name) {
    if (selectedFace_.body < 0 || selectedFace_.body >= extrusions_.size())
        return QStringLiteral("Seleziona prima una faccia piana di un corpo (clic sulla faccia nella vista).");
    if (!selectedFace_.hit.planar) return QStringLiteral("La faccia selezionata non e' piana.");
    const SelectedFace face = selectedFace_;
    return createFaceSketch(face.body, face.hit, name) >= 0 ? QString() : QStringLiteral("Schizzo sulla faccia non riuscito.");
}

void CadViewport::selectSketch(int index) {
    if(documentReadOnly_)return;
    if (index < 0 || index >= sketches_.size()) return;
    if(sketchMode_&&index!=activeSketch_)endSketchMode();
    activeSketch_ = index;
    beginSketchMode(sketches_.at(index).plane);
}

void CadViewport::beginSketchMode(int plane) {
    if(documentReadOnly_)return;
    activePlane_ = plane;
    selectedPlane_ = plane < 3 ? plane : -1;
    selectedFace_ = {};
    clearFeatureHighlights();
    sketchMode_ = true;
    sketchViewUnlocked_ = false;
    sketchViewRotated_ = false;
    sceneBoundsDirty_ = true;
    blendFirst_ = -1;
    trimPreview_.clear();
    sketchCameraLocked_ = true;
    hasPendingPoint_ = false;
    curveControlPoints_.clear();
    sketchSelections_.clear();
    // Le selezioni di punti (e dei vincoli) si riferiscono allo schizzo precedente.
    selectedPoints_.clear();
    selectedConstraints_.clear();
    constraintHover_ = -1;
    hover_ = {};
    sketchHover_ = {};
    // Vista allineata agli assi dello schizzo (X a destra, Y in alto): screenToSketchPoint lo richiede.
    if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) {
        const ForgeCad::Kernel::Frame3 axes = ForgeCad::sketchAxes(sketches_.at(activeSketch_));
        setViewFrame(QVector3D(float(axes.xDir().x()), float(axes.xDir().y()), float(axes.xDir().z())),
                     QVector3D(float(axes.zDir().x()), float(axes.zDir().y()), float(axes.zDir().z())));
    } else {
        setViewNormal(plane);
    }
    // Lo schizzo in modifica il piu' grande possibile (se e' vuoto, tutta la scena).
    QVector<QVector3D> sketchPoints;
    if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) sketchPoints = sketchGeometryPoints(sketches_.at(activeSketch_));
    fitView(sketchPoints.isEmpty() ? sceneGeometryPoints() : sketchPoints);
    if (sketchModeCallback_) sketchModeCallback_(true);
    selectionChanged();
    setDrawingTool(DrawingTool::Select);
    update();
}

void CadViewport::endSketchMode() {
    circleRadiusDrag_=-1;
    dimensionPlacing_ = -1;
    sketchViewUnlocked_ = false;
    sketchViewRotated_ = false;
    sketchMode_ = false;
    sceneBoundsDirty_ = true;
    blendFirst_ = -1;
    trimPreview_.clear();
    sketchCameraLocked_ = false;
    hasPendingPoint_ = false;
    lastSnapKind_ = SnapKind::None;
    sketchSelections_.clear();
    selectedPoints_.clear();
    selectedConstraints_.clear();
    constraintHover_ = -1;
    curveControlPoints_.clear();
    sketchHover_ = {};
    if(!deferredSketches_.isEmpty()){
        QSet<int> changed;
        for(int index:deferredSketches_)if(index>=0&&index<sketches_.size())changed.insert(index);
        deferredSketches_.clear();
        if(!changed.isEmpty())regenerateFrom(0,QVector<bool>(extrusions_.size(),false),changed);
        documentChanged();
    }
    if (sketchModeCallback_) sketchModeCallback_(false);
    selectionChanged();
    update();
}

QMatrix4x4 CadViewport::basisMatrix() const {
    return camera_.basisMatrix();
}

QMatrix4x4 CadViewport::sceneProjectionMatrix() const {
    return camera_.projectionMatrix(size(), sceneDepth());
}

QMatrix4x4 CadViewport::sceneModelViewMatrix() const {
    return camera_.modelViewMatrix();
}

void CadViewport::setRenderMatrices(const QMatrix4x4 &projection, const QMatrix4x4 &modelView) {
    displayCache_.setMatrices(projection, modelView);
    overlayRenderer_.setMatrices(projection, modelView);
}

void CadViewport::setViewPreset(int preset) {
    camera_.setPreset(preset);
    update();
}

void CadViewport::setViewFrame(const QVector3D &worldX, const QVector3D &worldNormal) {
    camera_.setFrame(worldX, worldNormal);
    update();
}

void CadViewport::setViewNormal(int plane) {
    const SketchFrame frame = ForgeCad::referenceSketchFrame(plane, camera_.orientation_);
    setViewFrame(QVector3D(float(frame.xAxis[0]), float(frame.xAxis[1]), float(frame.xAxis[2])),
                 QVector3D(float(frame.normal[0]), float(frame.normal[1]), float(frame.normal[2])));
}

float CadViewport::normalizedViewAngle(float degrees) {
    return ForgeCad::Gui::ViewportCamera::normalizedViewAngle(degrees);
}

QVector3D CadViewport::orbitPointAt(const QPoint &position) const {
    QVector3D origin, direction;
    viewRay(position, origin, direction);
    direction.normalize();
    double skipped = 0.0;
    const bool reaches = sectionRay(origin, direction, skipped);
    float nearest = std::numeric_limits<float>::max();
    if (reaches) for (const ExtrusionObject &body : extrusions_) {
        if (!body.visible || !ForgeCad::isShapeBody(body)) continue;
        float distance = 0.0f;
        if (meshRayHit(body.display, origin, direction, distance) && distance < nearest
            && !sectionRemoves(origin + distance*direction)) nearest = distance;
    }
    if (nearest < std::numeric_limits<float>::max()) return origin + nearest*direction;
    // Sullo sfondo usa il piano parallelo allo schermo che passa per
    // il centro del box della scena, alla stessa posizione del mouse.
    updateSceneBounds();
    const QVector3D center = (sceneMin_ + sceneMax_)*0.5f;
    return origin + QVector3D::dotProduct(center-origin, direction)*direction;
}

void CadViewport::orbitView(const QPoint &delta) {
    if (delta.isNull()) return;
    if (!camera_.orbitPivotValid_) {
        camera_.orbitPivot_ = orbitPointAt(lastMousePosition_);
        camera_.orbitPivotValid_ = true;
    }
    camera_.orbitView(delta);
}

const AxesOrientation &CadViewport::orientation() const { return camera_.orientation_; }

void CadViewport::setOrientation(const AxesOrientation &orientation) {
    if(documentReadOnly_)return;
    camera_.orientation_ = orientation;
    setViewPreset(4);
    fitAll();
    update();
}

AxesOrientation CadViewport::orientationFromCurrentView() const {
    return camera_.orientationFromCurrentView();
}

void CadViewport::initializeGL() {
    initializeOpenGLFunctions();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        makeCurrent();
        displayCache_.clear();
        overlayRenderer_.clear();
        sceneRenderer_.clearResources();
        gpuPicker_.clearResources();
        doneCurrent();
    }, Qt::DirectConnection);
    const QString renderer = QString::fromLatin1(reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    const bool modernRenderer = displayCache_.shaderAvailable() && overlayRenderer_.available();
    if (rendererCallback_)
        rendererCallback_(renderer + (modernRenderer ? QStringLiteral(" | shader 3.3, VAO/VBO solidi e overlay, picking GPU")
                                                      : QStringLiteral(" | renderer Core non disponibile")));
    sceneRenderer_.initialize();
    initializeGlassShader();
}

void CadViewport::resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
    if (camera_.autoFit_ && painted_ && !sketchMode_) fitView(sceneGeometryPoints());
}

void CadViewport::paintGL() {
    displayCache_.beginFrame();
    if (!painted_) {
        painted_ = true;
        if (!pendingFit_.isEmpty()) fitView(pendingFit_);
        pendingFit_.clear();
    }
    const QSize pixels = sceneRenderer_.render(size(), [this] {
        drawBackgroundGradient();
        const QMatrix4x4 projection = sceneProjectionMatrix();
        const QMatrix4x4 modelView = sceneModelViewMatrix();
        setRenderMatrices(projection, modelView);
        drawReferencePlanes();
        drawDatumPlanes();
        drawGrid();
        if (!axesOnTop_) drawAxes();
        configureLighting();
        drawExtrusions();
        drawExportMeshPreview();
        drawPickedEdges();
        drawTopologyHighlight();
        drawSketch();
        drawSketchCurvature();
        drawSnapMarkers();
        if (axesOnTop_) drawAxes();
    });
    drawReferenceLabels();
    drawSelectionHighlight();
    drawPlaneResizeHandles();
    // Il vetro copre anche etichette e selezioni: niente overlay della
    // scena viene disegnato sopra il fondale del pannello.
    // QPainter puo' lasciare scissor/stencil attivi dopo gli overlay.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    drawGpuGlassPanels(pixels, nullptr);
    ++renderedFrameSerial_;
}

void CadViewport::keyPressEvent(QKeyEvent *event) {
    if(spatialUndo_ && event->matches(QKeySequence::Undo)){auto undo=spatialUndo_;undo();return;}
    if(spatialRedo_ && event->matches(QKeySequence::Redo)){auto redo=spatialRedo_;redo();return;}
    if(spatialAdd_ && event->key()==Qt::Key_Escape){auto finish=spatialFinish_;if(finish)finish();return;}
    if (planeResizing_ && event->key() == Qt::Key_Escape) { finishPlaneResize(true); return; }
    if (event->key() == panKey_) {
        panKeyHeld_ = true;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (refPicking_ && event->key() == Qt::Key_Escape) {
        cancelReferencePick();
        if (refPickFinished_) {
            const auto finished = refPickFinished_;
            finished(false, {});
        }
        return;
    }
    if (edgePicking_) {
        if (event->key() == Qt::Key_Escape) {
            if (edgePickPanelCancel_) {
                edgePickPanelCancel_();
                return;
            }
            cancelEdgePick();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (edgePickPanelAccept_) {
                edgePickPanelAccept_();
                return;
            }
            finishEdgePick();
            return;
        }
    }
    if ((interactionLocked_ || documentReadOnly_)) {
        if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) zoomIn();
        else if (event->key() == Qt::Key_Minus) zoomOut();
        else if (event->key() == Qt::Key_0) resetZoom();
        else if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_6) setViewPreset(event->key() - Qt::Key_1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && dimensionPlacing_ >= 0) {
        dimensionPlacing_ = -1;  // la quota resta dove e'
        documentChanged();
        return;
    }
    if (sketchMode_ && sketchViewUnlocked_ && event->key() == Qt::Key_Escape) return;  // la finestra della funzione e' aperta
    if (sketchMode_ && drawingTool_ == DrawingTool::Dimension && event->modifiers() == Qt::NoModifier) {
        switch (event->key()) {
        case Qt::Key_A: setDimensionOrientation(0); return;
        case Qt::Key_O: setDimensionOrientation(1); return;
        case Qt::Key_H: setDimensionOrientation(2); return;
        case Qt::Key_V: setDimensionOrientation(3); return;
        case Qt::Key_Escape:
            // Prima si tolgono le entita' scelte, poi si torna alla selezione.
            if (!selectedPoints_.isEmpty() || !sketchSelections_.isEmpty()) {
                selectedPoints_.clear();
                sketchSelections_.clear();
                dimensionPreviewValid_ = false;
                selectionChanged();
                update();
                return;
            }
            break;
        default: break;
        }
    }
    if (event->key() == Qt::Key_Escape && sketchMode_ && blendFirst_ >= 0) {
        blendFirst_ = -1;
        update();
        return;
    }
    if (event->key() == Qt::Key_Escape && sketchMode_) {
        // Prima annulla quello che si sta disegnando, poi torna alla
        // selezione, infine esce dallo schizzo.
        if (hasPendingPoint_ || !curveControlPoints_.isEmpty()) {
            hasPendingPoint_ = false;
            curveControlPoints_.clear();
            update();
        } else if (drawingTool_ != DrawingTool::Select) {
            setDrawingTool(DrawingTool::Select);
        } else if (!selectedConstraints_.isEmpty() || !selectedPoints_.isEmpty() || !sketchSelections_.isEmpty()) {
            selectedConstraints_.clear();
            selectedPoints_.clear();
            sketchSelections_.clear();
            selectionChanged();
            update();
        } else {
            endSketchMode();
        }
        return;
    }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) {
        zoomIn();
        return;
    }
    if (event->key() == Qt::Key_Minus) {
        zoomOut();
        return;
    }
    if (event->key() == Qt::Key_0) {
        resetZoom();
        return;
    }
    if (sketchMode_) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            finalizeCurve();
            return;
        }
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_1: setViewPreset(0); break;
    case Qt::Key_2: setViewPreset(1); break;
    case Qt::Key_3: setViewPreset(2); break;
    case Qt::Key_4: setViewPreset(3); break;
    case Qt::Key_5: setViewPreset(4); break;
    case Qt::Key_6: setViewPreset(5); break;
    default: QOpenGLWidget::keyPressEvent(event); break;
    }
}

void CadViewport::mousePressEvent(QMouseEvent *event) {
    camera_.orbitPivotValid_ = false;
    lastMousePosition_ = event->position().toPoint();
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && panKeyHeld_)) {
        panning_ = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::LeftButton && featureHandleAt(event->position())) {
        featureHandleDragging_ = true;
        featureHandleDragValue_ = featureHandleValue();
        featureHandleLastMouse_ = event->position();
        featureHandleMouseAngleValid_ = false;
        dragFeatureHandle(event->position());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if(spatialAdd_) {
        if(event->button()==Qt::LeftButton){spatialPressed_=true;spatialDragged_=false;spatialPress_=lastMousePosition_;}
        event->accept();return;
    }
    if ((interactionLocked_ || documentReadOnly_) && !refPicking_ && !edgePicking_ && !trimPartPickFinished_
        && !sketchPickCallback_ && !sketchEntityPickCallback_ && !sketchOffsetSelectionCallback_) {
        if (event->button() == Qt::LeftButton && !releasingSelection_) {
            selectionPress_ = lastMousePosition_;
            selectionPending_ = true;
            selectionDragged_ = false;
        }
        event->accept();
        return;
    }
    // Nello schizzo il tasto destro trascinato ruota la vista (un clic resta il menu / la chiusura della spline).
    if (sketchMode_ && event->button() == Qt::RightButton) {
        rightPressPosition_ = lastMousePosition_;
        rightDragged_ = false;
        contextPending_ = false;
        return;
    }
    if (!sketchMode_ && !edgePicking_ && !refPicking_ && !trimPartPickFinished_ && event->button() == Qt::LeftButton
        && event->modifiers() == Qt::NoModifier) {
        TopologyLabelItem item;
        if (pickTopologyLabel(lastMousePosition_, item)) {
            const bool alreadyHighlighted = topologyHighlightBody_ == item.body
                && (item.edge ? topologyHighlightEdge_ == item.subshape : topologyHighlightFace_ == item.subshape);
            topologyHighlightBody_ = alreadyHighlighted ? -1 : item.body;
            topologyHighlightEdge_ = !alreadyHighlighted && item.edge ? item.subshape : -1;
            topologyHighlightFace_ = !alreadyHighlighted && !item.edge ? item.subshape : -1;
            showStatus(alreadyHighlighted
                ? QStringLiteral("Evidenziazione topologica rimossa")
                : QStringLiteral("%1 evidenziato").arg(QString(item.text).replace(QChar(u'\n'), QStringLiteral(" — "))));
            update();
            return;
        }
    }
    if (!(interactionLocked_ || documentReadOnly_) && !trimPartPickFinished_ && event->button() == Qt::LeftButton && beginPlaneResize(lastMousePosition_)) return;
    // Il piano di sezione si trascina dalla maniglia o dal bordo.
    if (!(interactionLocked_ || documentReadOnly_) && !sketchMode_ && !refPicking_ && !edgePicking_ && !trimPartPickFinished_ && event->button() == Qt::LeftButton && sectionHandleAt(lastMousePosition_)) {
        sectionDragging_ = true;
        setCursor(Qt::SizeAllCursor);
        return;
    }
    if (!releasingSelection_ && event->button() == Qt::LeftButton
        && (!sketchMode_ || (sketchViewUnlocked_ && refPicking_))
        && (edgePicking_ || refPicking_ || sketchMode_ || !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))) {
        selectionPress_ = lastMousePosition_;
        selectionPending_ = true;
        selectionDragged_ = false;
        return;
    }
    if ((!sketchMode_ || sketchViewUnlocked_) && refPicking_ && event->button() == Qt::LeftButton) {
        // Riferimento di un piano di costruzione; lontano da tutto il trascinamento ruota la vista.
        GeometryRef ref;
        if (pickReference(lastMousePosition_, ref)) {
            cancelReferencePick();
            if (refPickFinished_) {
                const auto finished = refPickFinished_;  // il callback puo' sostituirsi (o togliersi) mentre gira
                finished(true, ref);
            }
        }
        return;
    }
    if (!sketchMode_ && edgePicking_ && event->button() == Qt::LeftButton) {
        // Scelta degli spigoli per raccordo/smusso: il clic li accende o li spegne.
        // Clic su una faccia: tutti i suoi bordi (o nessuno, se c'erano gia' tutti).
        int edgeBody = -1;
        const int edge = pickEdge(lastMousePosition_, &edgeBody);
        FaceHit face;
        const int faceBody = edge >= 0 ? -1 : edgePickFaceBody(lastMousePosition_);
        if (edgePickHelix_) {
            // Base dell'elica: il primo clic decide (spigolo o faccia).
            int body = -1, source = 0;
            EdgePoint point;
            if (edge >= 0) {
                const QVector<QVector3D> &polyline = extrusions_.at(edgeBody).display.edges.at(edge);
                const QVector3D p = polyline.size() >= 3 ? polyline.at(polyline.size() / 2) : 0.5f * (polyline.first() + polyline.last());
                body = edgeBody, source = 1;
                const int edgeId = extrusions_.at(edgeBody).display.edgeIds.value(edge, -1);
                point = edgeId >= 0 && extrusions_.at(edgeBody).forgeBody
                    ? ForgeCad::edgeReference(*extrusions_.at(edgeBody).forgeBody, ForgeCad::Kernel::EdgeId(edgeId),
                                              ForgeCad::Kernel::Vec3(p.x(), p.y(), p.z()))
                    : EdgePoint{p.x(), p.y(), p.z()};
            } else if (pickBodyFace(faceBody, lastMousePosition_, face)) {
                QVector3D origin, direction;
                viewRay(lastMousePosition_, origin, direction);
                const QVector3D p = origin + direction.normalized() * float(face.distance);
                body = faceBody, source = 2, point = {p.x(), p.y(), p.z()};
                // Il punto esatto lungo il raggio (in double).
                const double length = double(direction.length());
                point = {double(origin.x()) + double(direction.x()) / length * face.distance, double(origin.y()) + double(direction.y()) / length * face.distance,
                         double(origin.z()) + double(direction.z()) / length * face.distance};
                if (face.face >= 0 && extrusions_.at(faceBody).forgeBody)
                    point = ForgeCad::faceReference(*extrusions_.at(faceBody).forgeBody, ForgeCad::Kernel::FaceId(face.face),
                                                    ForgeCad::Kernel::Vec3(point.x, point.y, point.z));
            }
            if (body < 0) return;
            cancelEdgePick();
            if (helixPickFinished_) helixPickFinished_(body, source, point);
            return;
        }
        if (edge >= 0) {
            // Il primo spigolo sceglie il corpo.
            if (edgeBody != edgePickBody_) {
                edgePickBody_ = edgeBody;
                pickedEdges_.clear();
                pickedFaces_.clear();
            }
            if (pickedEdges_.contains(edge)) pickedEdges_.removeAll(edge);
            else pickedEdges_.append(edge);
        } else if (pickBodyFace(faceBody, lastMousePosition_, face)) {
            if (faceBody != edgePickBody_) {
                edgePickBody_ = faceBody;
                pickedEdges_.clear();
                pickedFaces_.clear();
            }
            const QVector<int> border = faceDisplayEdges(edgePickBody_, face);
            bool all = !border.isEmpty();
            for (int index : border) all = all && pickedEdges_.contains(index);
            for (int index : border) {
                pickedEdges_.removeAll(index);
                if (!all) pickedEdges_.append(index);
            }
            for (int k = int(pickedFaces_.size()) - 1; k >= 0; --k)
                if (pickedFaces_.at(k).face == face.face) pickedFaces_.removeAt(k);
            if (!all && face.face >= 0 && !edgePickExtend_) {
                // La faccia stessa, per il punto esatto lungo il raggio.
                QVector3D origin, direction;
                viewRay(lastMousePosition_, origin, direction);
                const double length = double(direction.length());
                const ForgeCad::Kernel::Vec3 point(double(origin.x()) + double(direction.x()) / length * face.distance,
                                                   double(origin.y()) + double(direction.y()) / length * face.distance,
                                                   double(origin.z()) + double(direction.z()) / length * face.distance);
                pickedFaces_.append({face.face, faceBoundaryReference(edgePickBody_, face.face, point), border});
            }
        }
        edgePicked();
        return;
    }
    if (!sketchMode_ && event->button() == Qt::LeftButton && trimPartPickFinished_) {
        // Il clic usa le regioni, anche senza un precedente mouseMove e
        // quando il corpo originale e' nascosto dall'anteprima.
        if (event->modifiers() == Qt::NoModifier) {
            QVector3D origin, direction;
            viewRay(lastMousePosition_, origin, direction);
            int nearest = -1;
            float best = std::numeric_limits<float>::max();
            for (int k = 0; k < trimPartPickDisplays_.size(); ++k) {
                float distance = 0.0f;
                if (meshRayHit(trimPartPickDisplays_.at(k), origin, direction, distance) && distance < best)
                    best = distance, nearest = k;
            }
            if (nearest >= 0) {
                const QVector3D hit = origin + best * direction.normalized();
                trimPartPickFinished_(trimPartPickSides_.at(nearest), trimPartPickIndices_.at(nearest),
                                      EdgePoint{hit.x(), hit.y(), hit.z()});
            }
        }
        // Nessuna regione disponibile: non ricadere nella selezione scena.
        return;
    }
    if (!sketchMode_ && event->button() == Qt::LeftButton) {
        // Maiusc o Ctrl: riquadro trascinando, oppure un clic che aggiunge o toglie l'oggetto.
        if (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
            armBoxSelection(lastMousePosition_, true);
            return;
        }
        selectedObjects_.clear();
        selection_ = pickSceneObject(lastMousePosition_);
        if (selection_.kind == SceneObjectKind::Plane) selectedPlane_ = selection_.index;
        // Sul corpo si seleziona anche la faccia colpita (bordi evidenziati).
        selectedFace_ = {};
        selectedFeature_ = selection_.kind == SceneObjectKind::Extrusion ? selection_.index : -1;
        if (selection_.kind == SceneObjectKind::Extrusion && pickBodyFace(selection_.index, lastMousePosition_, selectedFace_.hit)) {
            selectedFace_.body = selection_.index;
            // La parte cliccata seleziona la funzione che l'ha creata (nell'albero e per il menu).
            selectedFeature_ = faceOwnerFeature(selection_.index, lastMousePosition_, selectedFace_.hit);
            QStringList edgeIds;
            for (const EdgePoint &edge : selectedFace_.hit.edges)
                if (edge.subshape >= 0) edgeIds.append(QStringLiteral("E%1").arg(edge.subshape));
            showStatus(QStringLiteral("%1: B%2:F%3%4 (%5), creata da %6")
                           .arg(extrusions_.at(selection_.index).name)
                           .arg(selection_.index)
                           .arg(selectedFace_.hit.face)
                           .arg(selectedFace_.hit.planar ? QStringLiteral(" piana") : QString())
                           .arg(edgeIds.isEmpty() ? QStringLiteral("nessun bordo") : edgeIds.join(QStringLiteral(", ")))
                           .arg(extrusions_.at(selectedFeature_).name));
        }
        // La faccia cliccata seleziona la sua feature (anche nell'albero) e se ne
        // colorano le facce; il corpo intero si colora solo dalla storyboard.
        if (selectedFeature_ >= 0 && selectedFace_.body >= 0) setFeatureHighlights({selectedFeature_});
        else clearFeatureHighlights();
        if (selectionCallback_)
            selectionCallback_(selectedFeature_ >= 0 ? SceneSelection{SceneObjectKind::Extrusion, selectedFeature_, -1} : selection_);
        if (sketchPickCallback_ && selection_.kind == SceneObjectKind::Sketch) sketchPickCallback_(selection_.index);
        if (sketchEntityPickCallback_ && selection_.kind == SceneObjectKind::Sketch) {
            int sketch = -1, kind = -1, entity = -1;
            if (pickVisibleSketchEntity(lastMousePosition_, sketch, kind, entity)) sketchEntityPickCallback_(sketch, kind, entity);
        }
        update();
        return;
    }
    if (sketchMode_ && event->button() == Qt::LeftButton) {
        // Vista sbloccata (estrusione o rivoluzione in corso): il trascinamento ruota la vista.
        if (sketchViewUnlocked_) return;
        const QPointF rawPoint = screenToSketchPoint(lastMousePosition_);
        if (dimensionPlacing_ >= 0) {
            dimensionPlacing_ = -1;
            documentChanged();
            return;
        }
        if (drawingTool_ == DrawingTool::Dimension) {
            int kind = -1, index = -1;
            ConstraintRef ref;
            if (!dimensionRefAt(rawPoint, ref) && pickSketchReferencePlaneAxis(lastMousePosition_, kind, index)) {
                selectSketchReference(kind, index);
                return;
            }
            dimensionToolClick(rawPoint);
            return;
        }
        if (drawingTool_ == DrawingTool::ConvertEdges) {
            const QString error = convertReferenceAt(lastMousePosition_);
            showStatus(error.isEmpty() ? QStringLiteral("Riferimento aggiunto allo schizzo.") : error);
            return;
        }
        if (isEditTool()) {
            applyEditTool(rawPoint);
            return;
        }
        if (drawingTool_ == DrawingTool::Select) {
            if (sketchOffsetSelectionCallback_) {
                // Nel pannello Offset si scelgono entita', senza muovere
                // poli, estremi o simboli dei vincoli.
                const auto hit = findSketchElement(rawPoint);
                if (hit.kind >= 0) selectSketchElement(rawPoint, true);
                else if(!selectOffsetReferenceAt(lastMousePosition_))
                    armBoxSelection(lastMousePosition_, event->modifiers() & Qt::ShiftModifier);
                return;
            }
            // Ctrl indica in modo esplicito la scelta di un punto per i
            // vincoli. Deve vincere sulle etichette delle quote che
            // possono coprire il centro di cerchi e poligoni.
            if ((event->modifiers() & Qt::ControlModifier) && selectPointWithControl(rawPoint)) return;
            // Un clic sul simbolo di un vincolo lo seleziona (Maiusc aggiunge o toglie).
            QPointF endpoint;int editCurve=-1,editControl=-1;EditablePointKind editKind;
            const bool onEditablePoint=segmentEndpointAt(rawPoint,endpoint)||findCurveEditPoint(rawPoint,editCurve,editControl,editKind);
            const int glyph = onEditablePoint?-1:constraintAt(lastMousePosition_);
            if (glyph >= 0) {
                // Una quota si sposta trascinandola.
                if (ForgeCad::isDimension(sketches_.at(activeSketch_).geometricConstraints.at(glyph).type)) {
                    dimensionDrag_ = glyph;
                    dragSnapshot_ = documentState();
                    dragRecorded_ = false;
                }
                if (event->modifiers() & Qt::ShiftModifier) {
                    if (selectedConstraints_.contains(glyph)) selectedConstraints_.removeAll(glyph);
                    else selectedConstraints_.append(glyph);
                } else {
                    selectedConstraints_ = {glyph};
                    sketchSelections_.clear();
                    selectedPoints_.clear();
                }
                selectionChanged();
                update();
                return;
            }
            int referenceKind = -1, referenceIndex = -1, pointCurve=-1,pointControl=-1;EditablePointKind pointKind;
            const bool editablePoint=findCurveEditPoint(rawPoint,pointCurve,pointControl,pointKind);
            if (!editablePoint&&findSketchElement(rawPoint).kind < 0 && pickSketchReferencePlaneAxis(lastMousePosition_, referenceKind, referenceIndex)) {
                selectSketchReference(referenceKind, referenceIndex);
                return;
            }
            // Selezione: Ctrl+clic su punti ed elementi come prima, il
            // trascinamento muove i punti delle curve, il clic seleziona
            // (Maiusc aggiunge o toglie).
            // Un estremo di segmento (anche comune a piu' entita') si
            // trascina con tutti i punti coincidenti; l'interno di un
            // segmento lo seleziona e, trascinando, lo sposta.
            if (event->modifiers() & Qt::ControlModifier) {
                selectSketchElement(rawPoint, true);
            } else if (segmentEndpointAt(rawPoint, pointDragPosition_)) {
                pointDragActive_ = true;
                dragSnapshot_ = documentState();
                dragRecorded_ = false;
            } else if (findCurveEditPoint(rawPoint, draggingCurveIndex_, draggingControlIndex_, draggingPointKind_)) {
                draggingControlPoint_ = true;
                dragSnapshot_ = documentState();
                dragRecorded_ = false;
            } else {
                selectSketchElement(rawPoint, event->modifiers() & Qt::ShiftModifier);
                const SketchElementSelection hit = findSketchElement(rawPoint);
                // Dal vuoto il trascinamento e' un riquadro di selezione.
                if (hit.kind < 0) armBoxSelection(lastMousePosition_, event->modifiers() & Qt::ShiftModifier);
                if(hit.kind==1&&sketches_[activeSketch_].curves[hit.index].tool==DrawingTool::Circle){
                    const auto &circle=sketches_[activeSketch_].curves[hit.index];
                    circleRadiusDrag_=hit.index;circleInitialRadius_=pointDistance(circle.controlPoints[0],circle.controlPoints[1]);circlePointerRadius_=pointDistance(rawPoint,circle.controlPoints[0]);dragSnapshot_=documentState();dragRecorded_=false;
                }
                if (hit.kind == 0) {
                    bodyDragSegment_ = hit.index;
                    bodyDragLast_ = rawPoint;
                    bodyDragMoved_ = false;
                    dragSnapshot_ = documentState();
                    dragRecorded_ = false;
                }
            }
            return;
        }
        if (event->modifiers() & Qt::ControlModifier) {
            if (!selectPointWithControl(rawPoint)) selectSketchElement(rawPoint, true);
            return;
        }
        if ((event->modifiers() & Qt::ShiftModifier)
            && (drawingTool_ == DrawingTool::Spline || drawingTool_ == DrawingTool::Nurbs)) {
            addControlPointToCurve(rawPoint);
            return;
        }
        // Con gli strumenti di disegno il clic crea sempre (anche su un
        // punto di un'altra entita', per esempio il centro di un cerchio
        // nell'origine): i punti si trascinano con lo strumento Selezione.
        // Clic su un segmento: lo seleziona, a meno che si stia disegnando
        // (punto in sospeso) o che il punto si agganci a un estremo o a un
        // punto medio (da li' parte il segmento nuovo, collegato).
        const SketchElementSelection hit = findSketchElement(rawPoint);
        if (hit.kind >= 0 && !hasPendingPoint_ && curveControlPoints_.isEmpty()) {
            snapPoint(rawPoint);
            const bool pointSnap = lastSnapKind_ == SnapKind::Endpoint || lastSnapKind_ == SnapKind::Midpoint
                || lastSnapKind_ == SnapKind::Nearest;
            if (!pointSnap) {
                selectSketchElement(rawPoint, false);
                return;
            }
        }
        if (drawingTool_ == DrawingTool::Rectangle || drawingTool_ == DrawingTool::CenterRectangle) {
            QPointF point = snapPoint(rawPoint);
            if (!curveControlPoints_.isEmpty()) point = rectangleCorner(curveControlPoints_.first(), point);
            curveControlPoints_.append(point);
            hasPendingPoint_ = true;
            if (curveControlPoints_.size() >= 2) finalizeRectangle();
            update();
            return;
        }
        if (drawingTool_ == DrawingTool::ThreePointArc || drawingTool_ == DrawingTool::TangentArc) {
            const QPointF point = snapPoint(rawPoint);
            if (drawingTool_ == DrawingTool::TangentArc && curveControlPoints_.isEmpty()) {
                tangentStarts_ = tangentStartsAt(point);
                if (tangentStarts_.isEmpty()) {
                    showStatus(QStringLiteral("Arco tangente: clic sull'estremo di un segmento o di un arco"));
                    return;
                }
            }
            if (!curveControlPoints_.isEmpty()
                && pointDistance(point, curveControlPoints_.last()) <= ForgeCad::kSketchConnectionTolerance) return;
            curveControlPoints_.append(point);
            hasPendingPoint_ = true;
            const int requiredPoints = drawingTool_ == DrawingTool::ThreePointArc ? 3 : 2;
            if (curveControlPoints_.size() >= requiredPoints) finalizeDrawnArc();
            update();
            return;
        }
        if (drawingTool_ == DrawingTool::Circle || drawingTool_ == DrawingTool::Arc
            || drawingTool_ == DrawingTool::Polygon || drawingTool_ == DrawingTool::Ellipse) {
            curveControlPoints_.append(snapPoint(rawPoint));
            hasPendingPoint_ = true;
            const int requiredPoints = drawingTool_ == DrawingTool::Arc || drawingTool_ == DrawingTool::Ellipse ? 3 : 2;
            if (curveControlPoints_.size() >= requiredPoints) finalizePrimitive();
            update();
            return;
        }
        if (drawingTool_ == DrawingTool::Spline || drawingTool_ == DrawingTool::Nurbs) {
            curveControlPoints_.append(snapPoint(rawPoint, false));
            hasPendingPoint_ = true;
            update();
            return;
        }
        const LineInference inference = inferLinePoint(rawPoint);
        if (!hasPendingPoint_) {
            pendingPoint_ = inference.point;
            hasPendingPoint_ = true;
            startReference_ = segmentAt(pendingPoint_);
            return;
        }
        const int appliedConstraint = inference.constraint >= 0 ? inference.constraint : constraintMode_;
        const QPointF constrainedPoint = inference.point;
        if (pointDistance(constrainedPoint, pendingPoint_) <= ForgeCad::kSketchConnectionTolerance) return;
        bool closedOnPoint = false;
        if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) {
            recordUndo();
            SketchObject &sketch = sketches_[activeSketch_];
            sketch.segments.append(qMakePair(pendingPoint_, constrainedPoint));
            sketch.constraints.append(-1);
            sketch.segmentLengths.append(0.0);
            sketch.segmentAngles.append(-1.0);
            const int created = sketch.segments.size() - 1;
            if (drawingTool_ == DrawingTool::ConstructionLine) sketch.constructionSegments.append(created);
            // Vincoli del disegno (orizzontale, verticale, perpendicolare o parallelo al
            // segmento di riferimento, quote) come oggetti.
            const auto add = [&](ConstraintType type, const ConstraintRef &second = {}) {
                QVector<ConstraintRef> refs{{0, created, -1}};
                if (second.kind >= 0) refs.append(second);
                sketch.geometricConstraints.append(ForgeCad::makeConstraint(sketch, type, refs));
            };
            if (appliedConstraint == 1) add(ConstraintType::Horizontal);
            else if (appliedConstraint == 2) add(ConstraintType::Vertical);
            else if ((appliedConstraint == 3 || appliedConstraint == 4) && inference.reference >= 0 && inference.reference < created)
                add(appliedConstraint == 3 ? ConstraintType::Perpendicular : ConstraintType::Parallel, {0, inference.reference, -1});
            if (lineLength_ > 0.0) add(ConstraintType::Distance);
            if (lineAngle_ >= 0.0 && appliedConstraint != 1 && appliedConstraint != 2) add(ConstraintType::Angle, {2, 1, -1});
            const int count = sketch.geometricConstraints.size();
            recordCoincidences(sketch, created);
            // Il secondo estremo e' finito su un punto di un'altra entita' (ora coincidenti)?
            for (int k = count; k < sketch.geometricConstraints.size(); ++k)
                closedOnPoint = closedOnPoint || (sketch.geometricConstraints.at(k).type == ConstraintType::Coincident
                                                  && sketch.geometricConstraints.at(k).first.point == 1);
            startReference_ = sketches_[activeSketch_].segments.size() - 1;  // la polilinea prosegue da qui
            sketchEdited();
        }
        pendingPoint_ = constrainedPoint;
        if (drawingTool_ == DrawingTool::Line || drawingTool_ == DrawingTool::ConstructionLine) {
            hasPendingPoint_ = false;
            // Linea chiusa su un punto esistente: si esce dalla funzione linea
            // (la polilinea invece prosegue da li').
            if (closedOnPoint) setDrawingTool(DrawingTool::Select);
        }
        update();
        return;
    }
}

bool CadViewport::segmentEndpointAt(const QPointF &point, QPointF &endpoint) const {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return false;
    double best = pickTolerance();
    bool found = false;
    for (const SketchSegment &segment : sketches_.at(activeSketch_).segments)
        for (const QPointF &candidate : {segment.first, segment.second})
            if (pointDistance(point, candidate) < best) {
                best = pointDistance(point, candidate);
                endpoint = candidate;
                found = true;
            }
    return found;
}

QPointF CadViewport::dragSnapPoint(const QPointF &raw) {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return raw;
    const SketchObject &sketch = sketches_.at(activeSketch_);
    const double tolerance = ForgeCad::kSketchConnectionTolerance;
    QVector<SketchSegment> segments;
    for (const SketchSegment &segment : sketch.segments)
        if (pointDistance(segment.first, pointDragPosition_) > tolerance && pointDistance(segment.second, pointDragPosition_) > tolerance)
            segments.append(segment);
    QVector<QPointF> points;
    for (const QPointF &p : snapCandidates(sketch))
        if (pointDistance(p, pointDragPosition_) > tolerance) points.append(p);
    if (originSnap_ && pointLength(pointDragPosition_) > tolerance) points.append(QPointF(0.0, 0.0));
    const ForgeCad::SnapResult result = ForgeCad::snapSegments(raw, segments, points, geometrySnap_, gridSnap_, snapSpacing_, pickTolerance(10.0));
    lastSnapKind_ = result.kind;
    lastSnapPoint_ = result.point;
    return result.point;
}

void CadViewport::dragSketchPoint(const QPointF &target) {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    const QPointF delta = target - pointDragPosition_;
    if (pointLength(delta) <= 0.0) return;
    SketchObject &sketch = sketches_[activeSketch_];
    const SketchObject before = sketch;
    moveSketchPoint(sketch, -1, pointDragPosition_, delta, QPointF(qQNaN(), qQNaN()));
    // Il punto segue il cursore o la sua componente ammessa dai vincoli.
    const auto targets=targetsAt(sketch,target);
    if (!solveActiveDrag(targets,before)) {
        showStatus(QStringLiteral("Il punto e' bloccato dai vincoli."));
        return;
    }
    if(!targets.isEmpty())ForgeCad::refPoint(sketch,targets.first().point,pointDragPosition_);
    sceneBoundsDirty_ = true;
}

void CadViewport::dragSketchSegment(const QPointF &raw) {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    SketchObject &sketch = sketches_[activeSketch_];
    if (bodyDragSegment_ >= sketch.segments.size()) return;
    QPointF delta = raw - bodyDragLast_;
    if (!bodyDragMoved_ && pointLength(delta) < pickTolerance(3.0)) return;
    bodyDragMoved_ = true;
    const QPointF first = sketch.segments.at(bodyDragSegment_).first, second = sketch.segments.at(bodyDragSegment_).second;
    const QPointF nowhere(qQNaN(), qQNaN());
    const SketchObject before = sketch;
    moveSketchPoints(sketch,bodyDragSegment_,{first,second},delta,nowhere);
    QVector<ForgeCad::PointTarget> targets = targetsAt(sketch, first + delta);
    targets += targetsAt(sketch, second + delta);
    if (!solveActiveDrag(targets, before)) {
        showStatus(QStringLiteral("Il segmento e' bloccato dai vincoli."));
        return;
    }
    bodyDragLast_ = raw;
    sceneBoundsDirty_ = true;
}

bool CadViewport::isEditTool() const {
    return drawingTool_ == DrawingTool::Trim || drawingTool_ == DrawingTool::Extend || drawingTool_ == DrawingTool::Split
        || drawingTool_ == DrawingTool::Fillet || drawingTool_ == DrawingTool::Chamfer;
}

QString CadViewport::editToolLabel() const {
    switch (drawingTool_) {
    case DrawingTool::Trim: return QStringLiteral("✂ Taglia: clic sul tratto da togliere");
    case DrawingTool::Extend: return QStringLiteral("⇥ Estendi: clic vicino all'estremo");
    case DrawingTool::Split: return QStringLiteral("⌿ Spezza: clic nel punto");
    case DrawingTool::Fillet:
        return blendFirst_ >= 0 ? QStringLiteral("◜ Raccordo R = %1: secondo segmento").arg(sketchFilletRadius_)
                                : QStringLiteral("◜ Raccordo R = %1: spigolo o primo segmento").arg(sketchFilletRadius_);
    case DrawingTool::Chamfer:
        return blendFirst_ >= 0 ? QStringLiteral("◸ Smusso D = %1: secondo segmento").arg(sketchChamferDistance_)
                                : QStringLiteral("◸ Smusso D = %1: spigolo o primo segmento").arg(sketchChamferDistance_);
    default: return {};
    }
}

void CadViewport::applyEditTool(const QPointF &point) {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    SketchObject edited = sketches_.at(activeSketch_);
    const SketchElementSelection hit = findSketchElement(point);
    ForgeCad::SketchEditResult result;
    if (drawingTool_ == DrawingTool::Fillet || drawingTool_ == DrawingTool::Chamfer) {
        const bool chamfer = drawingTool_ == DrawingTool::Chamfer;
        const double size = chamfer ? sketchChamferDistance_ : sketchFilletRadius_;
        int first = -1, second = -1;
        QPointF pickFirst, pickSecond;
        if (blendFirst_ < 0 && ForgeCad::sketchCornerAt(edited, point, pickTolerance(), first, pickFirst, second, pickSecond)) {
            result = ForgeCad::blendSketchSegments(edited, first, pickFirst, second, pickSecond, size, chamfer);
        } else if (hit.kind != 0) {
            showStatus(hit.kind < 0 ? QString() : QStringLiteral("Raccordo e smusso si fanno tra segmenti."));
            return;
        } else if (blendFirst_ < 0 || blendFirst_ == hit.index) {
            blendFirst_ = blendFirst_ == hit.index ? -1 : hit.index;
            blendFirstPick_ = point;
            update();
            return;
        } else {
            result = ForgeCad::blendSketchSegments(edited, blendFirst_, blendFirstPick_, hit.index, point, size, chamfer);
            blendFirst_ = -1;
        }
    } else {
        if (hit.kind < 0) return;
        const ForgeCad::SketchEntity entity{hit.kind, hit.index};
        if (drawingTool_ == DrawingTool::Trim) result = ForgeCad::trimSketchEntity(edited, entity, point);
        else if (drawingTool_ == DrawingTool::Extend) result = ForgeCad::extendSketchEntity(edited, entity, point);
        else result = ForgeCad::splitSketchEntity(edited, entity, point, pickTolerance());
    }
    if (!result.error.isEmpty()) {
        showStatus(result.error);
        update();
        return;
    }
    recordUndo();
    for (CurveObject &curve : edited.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
    sketches_[activeSketch_] = edited;
    if (!result.segmentMap.isEmpty()) remapSketchReferences(activeSketch_, result.segmentMap);
    sketchSelections_.clear();
    selectedPoints_.clear();
    selectedConstraints_.clear();
    constraintHover_ = -1;
    sketchHover_ = findSketchElement(point);
    trimPreview_.clear();
    showStatus(QString());
    sketchEdited();
}

void CadViewport::showStatus(const QString &message) {
    if (statusCallback_) statusCallback_(message);
}

void CadViewport::remapRevolutionAxes(int sketchIndex, const QVector<int> &segmentMap) {
    for (ExtrusionObject &body : extrusions_)
        if (body.feature == BodyFeature::Revolution && body.sketchIndex == sketchIndex && body.revolveAxis >= 0)
            body.revolveAxis = body.revolveAxis < segmentMap.size() ? segmentMap.at(body.revolveAxis) : -3;
}

void CadViewport::remapSketchReferences(int sketchIndex, const QVector<int> &segmentMap, const QVector<int> &curveMap ) {
    if (!segmentMap.isEmpty()) remapRevolutionAxes(sketchIndex, segmentMap);
    const auto remap = [&](GeometryRef &ref) {
        if (!ForgeCad::isSketchRef(ref) || ref.index != sketchIndex || ref.element.element < 0) return;
        const QVector<int> &map = ref.element.kind == 0 ? segmentMap : curveMap;
        if ((ref.element.kind != 0 && ref.element.kind != 1) || map.isEmpty()) return;
        ref.element.element = map.value(ref.element.element, -1);
    };
    for (ExtrusionObject &body : extrusions_) {
        for (QVector<GeometryRef> *refs : {&body.datum.refs, &body.pattern.refs, &body.ruledFirst, &body.ruledSecond, &body.planarRefs, &body.loftStartFaces, &body.loftEndFaces, &body.loftSections, &body.loftEdgeGuides})
            for (GeometryRef &ref : *refs) remap(ref);
        remap(body.extentRef);
        remap(body.move.axis);
        remap(body.revolveAxisRef);
        remap(body.draftNeutral);
        remap(body.thickenDirection);
        remap(body.sweepProfileRef); remap(body.sweepPathRef);
        remap(body.curveExtraction.source);
        remap(body.curveExtraction.tool);
    }
}

int CadViewport::segmentAt(const QPointF &point, int exclude ) const {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return -1;
    const SketchObject &sketch = sketches_.at(activeSketch_);
    for (int index = 0; index < sketch.segments.size(); ++index)
        if (index != exclude && distanceToSegment(point, sketch.segments.at(index).first, sketch.segments.at(index).second) <= 1e-9)
            return index;
    return -1;
}

QString CadViewport::snapLabelAt(SnapKind kind, const QPointF &point) const {
    if (!lastSnapNote_.isEmpty() && point == lastSnapPoint_ && kind == lastSnapKind_) return lastSnapNote_;
    return snapLabel(kind);
}

QString CadViewport::snapLabel(SnapKind kind) {
    switch (kind) {
    case SnapKind::Endpoint: return QStringLiteral("● Coincidente");
    case SnapKind::Midpoint: return QStringLiteral("◐ Punto medio");
    case SnapKind::Nearest: return QStringLiteral("∈ Su segmento");
    default: return {};
    }
}

CadViewport::LineInference CadViewport::inferLinePoint(const QPointF &raw) {
    LineInference result;
    result.point = snapPoint(raw);
    const SnapKind snap = lastSnapKind_;
    const int snapCurve = lastSnapCurve_;
    if (snap != SnapKind::None) result.labels.append(snapLabelAt(snap, result.point));
    if (snap == SnapKind::Nearest) result.reference = segmentAt(result.point);
    if (!hasPendingPoint_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return result;
    const SketchObject &sketch = sketches_.at(activeSketch_);
    const QPointF start = pendingPoint_;
    auto project = [&](const QPointF &direction) {
        const double t = QPointF::dotProduct(result.point - start, direction);
        return start + t * direction;
    };
    if (constraintMode_ == 1 || constraintMode_ == 2) {
        result.point = constraintMode_ == 1 ? QPointF(result.point.x(), start.y()) : QPointF(start.x(), result.point.y());
        result.constraint = constraintMode_;
        result.labels = QStringList{constraintMode_ == 1 ? QStringLiteral("— Orizzontale") : QStringLiteral("| Verticale")};
    } else if (constraintMode_ == 0) {
        const QPointF delta = result.point - start;
        const double length = pointLength(delta);
        // Su un cerchio o un arco la direzione si combina con la curva
        // (punto comune esatto); su una spline il punto resta quello.
        const bool circleCurve = snap == SnapKind::Nearest && snapCurve >= 0 && snapCurve < sketch.curves.size()
            && (sketch.curves.at(snapCurve).tool == DrawingTool::Circle || sketch.curves.at(snapCurve).tool == DrawingTool::Arc);
        const bool pointSnap = snap == SnapKind::Endpoint || snap == SnapKind::Midpoint
            || (snap == SnapKind::Nearest && snapCurve >= 0 && !circleCurve);
        if (length > 0.0) {
            // Direzioni candidate con il loro vincolo e segmento di riferimento.
            struct Candidate { QPointF direction; int constraint; int reference; };
            QVector<Candidate> candidates{{QPointF(1, 0), 1, -1}, {QPointF(0, 1), 2, -1}};
            const int attached = startReference_;
            for (int index = 0; index < sketch.segments.size(); ++index) {
                const QPointF d = sketch.segments.at(index).second - sketch.segments.at(index).first;
                const double l = pointLength(d);
                if (l <= ForgeCad::kSketchConnectionTolerance) continue;
                const QPointF u = d / l;
                if (index == attached) candidates.append({QPointF(-u.y(), u.x()), 3, index});
                candidates.append({u, 4, index});
            }
            const QPointF u = delta / length;
            const double threshold = std::sin(4.0 * M_PI / 180.0);
            int best = -1;
            double bestSine = threshold;
            for (int k = 0; k < candidates.size(); ++k) {
                const double sine = std::abs(u.x() * candidates.at(k).direction.y() - u.y() * candidates.at(k).direction.x());
                // Orizzontale e verticale hanno la precedenza a pari scarto.
                if (sine < bestSine - 1e-12) {
                    bestSine = sine;
                    best = k;
                }
            }
            // Il punto agganciato resta quello: il vincolo di direzione solo se torna esatto.
            if (pointSnap && bestSine > 1e-9) best = -1;
            bool apply = best >= 0;
            QPointF curveTarget;
            if (best >= 0) {
                const Candidate &c = candidates.at(best);
                if (circleCurve) {
                    // Retta start + t d con il cerchio: la radice vicina al punto agganciato.
                    const CurveObject &curve = sketch.curves.at(snapCurve);
                    const QPointF center = curve.controlPoints.at(0);
                    const double r = pointDistance(center, curve.controlPoints.at(1));
                    const QPointF w = start - center;
                    const double b = QPointF::dotProduct(w, c.direction), cc = QPointF::dotProduct(w, w) - r * r;
                    const double disc = b * b - cc, now = QPointF::dotProduct(result.point - start, c.direction);
                    apply = false;
                    if (disc >= 0.0) {
                        const double root = std::sqrt(disc);
                        const double t = std::abs(-b + root - now) < std::abs(-b - root - now) ? -b + root : -b - root;
                        const QPointF p = start + t * c.direction;
                        if (pointDistance(p, result.point) <= pickTolerance(10.0)
                            && (curve.tool != DrawingTool::Arc || onArc(curve, std::atan2(p.y() - center.y(), p.x() - center.x())))) {
                            curveTarget = p;
                            apply = true;
                        }
                    }
                }
            }
            if (apply) {
                const Candidate &c = candidates.at(best);
                QPointF target = circleCurve ? curveTarget : project(c.direction);
                // Su un segmento: il punto comune alla direzione e al segmento.
                if (snap == SnapKind::Nearest && result.reference >= 0) {
                    const SketchSegment &on = sketch.segments.at(result.reference);
                    const QPointF e = on.second - on.first;
                    const double det = c.direction.x() * (-e.y()) - c.direction.y() * (-e.x());
                    if (std::abs(det) > 1e-12) {
                        const QPointF r = on.first - start;
                        const double t = (r.x() * (-e.y()) - r.y() * (-e.x())) / det;
                        const double s = (c.direction.x() * r.y() - c.direction.y() * r.x()) / det;
                        if (s >= 0.0 && s <= 1.0) {
                            target = start + t * c.direction;
                        } else {
                            result.labels.clear();
                            result.reference = -1;
                        }
                    }
                } else if (!pointSnap && !circleCurve) {
                    result.labels.clear();
                    result.reference = -1;
                }
                result.point = target;
                result.constraint = c.constraint;
                if (c.constraint >= 3) result.reference = c.reference;
                static const QStringList names = {QString(), QStringLiteral("— Orizzontale"), QStringLiteral("| Verticale"),
                                                  QStringLiteral("⟂ Perpendicolare"), QStringLiteral("∥ Parallelo")};
                result.labels.append(names.at(c.constraint));
            }
        }
    }
    // Quote: angolo e lunghezza fissati.
    const double currentLength = pointDistance(result.point, start);
    const double targetLength = lineLength_ > 0.0 ? lineLength_ : currentLength;
    if (lineAngle_ >= 0.0 && targetLength > 0.0) {
        const double radians = lineAngle_ * M_PI / 180.0;
        result.point = start + QPointF(std::cos(radians) * targetLength, std::sin(radians) * targetLength);
        result.labels = QStringList{QStringLiteral("∠ A = %1°").arg(lineAngle_)};
    }
    if (lineLength_ > 0.0) {
        QPointF direction = result.point - start;
        if (pointLength(direction) <= 0.0) direction = raw - start;
        if (pointLength(direction) > 0.0) result.point = start + direction / pointLength(direction) * lineLength_;
        result.labels.append(QStringLiteral("↔ L = %1").arg(lineLength_));
    }
    return result;
}

void CadViewport::recordCoincidences(SketchObject &sketch, int segment) {
    recordPointCoincidences(sketch, {0, segment, 0}, sketch.segments.at(segment).first);
    recordPointCoincidences(sketch, {0, segment, 1}, sketch.segments.at(segment).second);
}

void CadViewport::recordPointCoincidences(SketchObject &sketch, const ConstraintRef &here, const QPointF &point) {
    const double tolerance = ForgeCad::kSketchConnectionTolerance;
    const auto add = [&](ConstraintType type, const ConstraintRef &b, double value = 0.0) {
        SketchConstraint c;
        c.type = type;
        c.first = here;
        c.second = b;
        c.value = value;
        for (const auto &existing : sketch.geometricConstraints)
            if (existing.type == type && existing.first == here && existing.second == b) return;
        sketch.geometricConstraints.append(c);
    };
    const auto self = [&](int kind, int element) { return here.kind == kind && here.element == element; };
    bool onPoint = false;
    QSet<int> quadrantCurves;
    if (originSnap_ && geometrySnap_ && pointDistance(point, QPointF()) <= tolerance) {
        add(ConstraintType::Coincident, {2, 0, -1});
        onPoint = true;
    }
    for (int other = 0; other < sketch.segments.size(); ++other) {
        if (self(0, other)) continue;
        const QPointF points[2] = {sketch.segments.at(other).first, sketch.segments.at(other).second};
        const int pointCount = pointDistance(points[0], points[1]) <= tolerance ? 1 : 2;
        for (int k = 0; k < pointCount; ++k)
            if (pointDistance(points[k], point) <= tolerance) {
                add(ConstraintType::Coincident, {0, other, k});
                onPoint = true;
            }
    }
    for (int curve = 0; curve < sketch.curves.size(); ++curve) {
        if (self(1, curve)) continue;
        for (const auto &q : ForgeCad::curveQuadrants(sketch.curves.at(curve))) {
            if (pointDistance(point, q.second) <= tolerance) {
                add(ConstraintType::Quadrant, {1, curve, -1}, q.first);
                quadrantCurves.insert(curve);
                break;
            }
        }
        if (quadrantCurves.contains(curve)) continue;
        for (int k = 0; k < sketch.curves.at(curve).controlPoints.size(); ++k)
            if (isPickableCurvePoint(sketch.curves.at(curve), k)
                && pointDistance(sketch.curves.at(curve).controlPoints.at(k), point) <= tolerance) {
                add(ConstraintType::Coincident, {1, curve, k});
                onPoint = true;
            }
    }
    if (onPoint) return;
    for (int other = 0; other < sketch.segments.size(); ++other) {
        if (self(0, other)) continue;
        const SketchSegment &s = sketch.segments.at(other);
        if (distanceToSegment(point, s.first, s.second) > 1e-9) continue;
        add(pointDistance(point, 0.5 * (s.first + s.second)) <= tolerance ? ConstraintType::Midpoint : ConstraintType::PointOnCurve,
            {0, other, -1});
    }
    for (int curve = 0; curve < sketch.curves.size(); ++curve) {
        if (self(1, curve) || quadrantCurves.contains(curve)) continue;
        const CurveObject &c = sketch.curves.at(curve);
        if (c.tool == DrawingTool::Polygon) continue;
        try {
            for (const auto &piece : ForgeCad::curveGeometry(c)) {
                const auto projection = ForgeCad::Kernel::projectPoint(*piece.curve,
                    ForgeCad::Kernel::Vec2(point.x(),point.y()),piece.range);
                if (projection.distance <= tolerance) {
                    add(ConstraintType::PointOnCurve, {1, curve, -1});
                    break;
                }
            }
        } catch (const std::exception &) {
            // Nessun vincolo su geometria non valida.
        }
    }
}

bool CadViewport::threePointArc(const QPointF &start, const QPointF &end, const QPointF &through, CurveObject &arc) {
    const QPointF b = end - start, c = through - start;
    const double scale = qMax(pointLength(b), pointLength(c));
    const double cross = b.x() * c.y() - b.y() * c.x();
    if (scale <= ForgeCad::kSketchConnectionTolerance || std::abs(cross) <= 1e-9 * scale * scale) return false;
    const double b2 = b.x() * b.x() + b.y() * b.y(), c2 = c.x() * c.x() + c.y() * c.y();
    const QPointF center = start + QPointF(c.y() * b2 - b.y() * c2, b.x() * c2 - c.x() * b2) / (2.0 * cross);
    arc.tool = DrawingTool::Arc;
    arc.controlPoints = cross < 0.0 ? QVector<QPointF>{center, start, end} : QVector<QPointF>{center, end, start};
    return true;
}

bool CadViewport::tangentArc(const QPointF &start, const QPointF &direction, const QPointF &end, CurveObject &arc, int *startPoint ) {
    const QPointF d = end - start, normal(-direction.y(), direction.x());
    const double length = pointLength(d), across = normal.x() * d.x() + normal.y() * d.y();
    if (length <= ForgeCad::kSketchConnectionTolerance || std::abs(across) <= 1e-9 * length) return false;
    const QPointF center = start + normal * (length * length / (2.0 * across));
    arc.tool = DrawingTool::Arc;
    arc.controlPoints = across > 0.0 ? QVector<QPointF>{center, start, end} : QVector<QPointF>{center, end, start};
    if (startPoint) *startPoint = across > 0.0 ? 1 : 2;
    return true;
}

QVector<CadViewport::TangentStart> CadViewport::tangentStartsAt(const QPointF &point) const {
    QVector<TangentStart> result;
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return result;
    const SketchObject &sketch = sketches_.at(activeSketch_);
    const double tolerance = ForgeCad::kSketchConnectionTolerance;
    for (int index = 0; index < sketch.segments.size(); ++index) {
        const SketchSegment &s = sketch.segments.at(index);
        const double length = pointDistance(s.first, s.second);
        if (length <= tolerance) continue;
        if (pointDistance(s.first, point) <= tolerance) result.append({{0, index, -1}, (s.first - s.second) / length});
        else if (pointDistance(s.second, point) <= tolerance) result.append({{0, index, -1}, (s.second - s.first) / length});
    }
    for (int index = 0; index < sketch.curves.size(); ++index) {
        const CurveObject &c = sketch.curves.at(index);
        if (c.tool != DrawingTool::Arc || c.controlPoints.size() < 3) continue;
        const QPointF center = c.controlPoints.at(0);
        const QPointF r0 = c.controlPoints.at(1) - center, r1 = c.controlPoints.at(2) - center;
        const double l0 = pointLength(r0), l1 = pointLength(r1);
        if (l0 <= tolerance || l1 <= tolerance) continue;
        // L'arco gira in senso antiorario: all'inizio prosegue all'indietro (orario).
        if (pointDistance(c.controlPoints.at(1), point) <= tolerance) result.append({{1, index, -1}, QPointF(r0.y(), -r0.x()) / l0});
        else if (pointDistance(c.controlPoints.at(2), point) <= tolerance) result.append({{1, index, -1}, QPointF(-r1.y(), r1.x()) / l1});
    }
    return result;
}

CadViewport::TangentStart CadViewport::tangentStartFor(const QPointF &cursor) const {
    if (tangentStarts_.isEmpty() || curveControlPoints_.isEmpty()) return {};
    const QPointF toward = cursor - curveControlPoints_.first();
    int best = 0;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (int k = 0; k < tangentStarts_.size(); ++k) {
        const QPointF &t = tangentStarts_.at(k).direction;
        const double score = (t.x() * toward.x() + t.y() * toward.y()) / qMax(pointLength(toward), 1e-300);
        if (score > bestScore) {
            bestScore = score;
            best = k;
        }
    }
    return tangentStarts_.at(best);
}

void CadViewport::finalizeDrawnArc() {
    const QVector<QPointF> points = curveControlPoints_;
    const bool tangent = drawingTool_ == DrawingTool::TangentArc;
    const TangentStart start = tangent ? tangentStartFor(points.value(1)) : TangentStart{};
    curveControlPoints_.clear();
    tangentStarts_.clear();
    hasPendingPoint_ = false;
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    CurveObject arc;
    const bool built = tangent ? start.entity.kind >= 0 && tangentArc(points.at(0), start.direction, points.at(1), arc)
                               : threePointArc(points.at(0), points.at(1), points.at(2), arc);
    if (!built) {
        showStatus(tangent ? QStringLiteral("Arco tangente: la fine sta sulla tangente (sarebbe un segmento)")
                           : QStringLiteral("Arco per tre punti: i punti sono allineati"));
        update();
        return;
    }
    ForgeCad::recalculateCurve(arc, tessellationQuality_);
    if (!arc.numericallyValid) {
        update();
        return;
    }
    recordUndo();
    SketchObject &sketch = sketches_[activeSketch_];
    sketch.curves.append(arc);
    const int created = sketch.curves.size() - 1;
    recordPointCoincidences(sketch, {1, created, 1}, arc.controlPoints.at(1));
    recordPointCoincidences(sketch, {1, created, 2}, arc.controlPoints.at(2));
    if (tangent)
        sketch.geometricConstraints.append(ForgeCad::makeConstraint(sketch, ConstraintType::Tangent, {{1, created, -1}, start.entity}));
    sketchEdited();
    update();
}

bool CadViewport::previewCurve(CurveObject &curve) const {
    if (!sketchMode_ || curveControlPoints_.isEmpty()) return false;
    curve.tool = drawingTool_;
    curve.controlPoints = curveControlPoints_;
    const QPointF cursor = cursorSketchPoint_;
    switch (drawingTool_) {
    case DrawingTool::Circle:
    case DrawingTool::Polygon:
        if (curve.controlPoints.size() != 1) return false;
        curve.controlPoints.append(cursor);
        curve.sides = polygonSides_;
        break;
    case DrawingTool::Arc:
        // Dopo il centro il cerchio del raggio, dopo l'inizio l'arco fino al cursore.
        if (curve.controlPoints.size() == 1) curve.tool = DrawingTool::Circle;
        else if (curve.controlPoints.size() != 2) return false;
        curve.controlPoints.append(cursor);
        break;
    case DrawingTool::Spline:
    case DrawingTool::Nurbs:
        if (pointDistance(curve.controlPoints.last(), cursor) > ForgeCad::kSketchConnectionTolerance) curve.controlPoints.append(cursor);
        if (curve.tool == DrawingTool::Spline) {
            ForgeCad::initializeTangentHandles(curve);
            ForgeCad::shapeSpline(curve, splineShapeOptions_);
        }
        break;
    case DrawingTool::ThreePointArc:
    case DrawingTool::TangentArc: {
        // Prima dell'ultimo punto la corda fino al cursore, poi l'arco.
        const QPointF &first = curve.controlPoints.first();
        const bool arc = drawingTool_ == DrawingTool::ThreePointArc
            ? curve.controlPoints.size() == 2 && threePointArc(first, curve.controlPoints.at(1), cursor, curve)
            : tangentArc(first, tangentStartFor(cursor).direction, cursor, curve);
        if (!arc) {
            curve.tool = DrawingTool::Line;
            curve.controlPoints = {first, curve.controlPoints.size() == 2 ? curve.controlPoints.at(1) : cursor};
            curve.samples = curve.controlPoints;
            return pointDistance(curve.samples.at(0), curve.samples.at(1)) > ForgeCad::kSketchConnectionTolerance;
        }
        break;
    }
    case DrawingTool::Rectangle:
    case DrawingTool::CenterRectangle:
        if (curve.controlPoints.size() != 1) return false;
        curve.controlPoints.append(rectangleCorner(curve.controlPoints.first(), cursor));
        break;
    case DrawingTool::Ellipse:
        // Dopo il centro il cerchio del primo semiasse, poi l'ellisse fino al cursore.
        if (curve.controlPoints.size() == 1) {
            curve.tool = DrawingTool::Circle;
            curve.controlPoints.append(cursor);
        } else if (curve.controlPoints.size() == 2) {
            curve.controlPoints.append(ellipseMinorPoint(curve.controlPoints.at(0), curve.controlPoints.at(1), cursor));
        } else {
            return false;
        }
        break;
    default:
        return false;
    }
    ForgeCad::recalculateCurve(curve, tessellationQuality_);
    return curve.samples.size() >= 2;
}

QStringList CadViewport::previewMeasures() const {
    CurveObject curve;
    if (!previewCurve(curve)) return {};
    const QVector<QPointF> &p = curve.controlPoints;
    const auto number = [](double value) { return ForgeCad::formatLength(value, 4); };
    switch (curve.tool) {
    case DrawingTool::Circle: {
        const double r = pointDistance(p.at(0), p.at(1));
        return {QStringLiteral("R = %1   Ø = %2").arg(number(r), number(2.0 * r))};
    }
    case DrawingTool::Polygon: {
        const double r = pointDistance(p.at(0), p.at(1));
        return {QStringLiteral("R = %1   lato = %2   %3 lati").arg(number(r), number(2.0 * r * std::sin(M_PI / curve.sides))).arg(curve.sides)};
    }
    case DrawingTool::Rectangle:
    case DrawingTool::CenterRectangle: {
        const QPointF a = curve.tool == DrawingTool::Rectangle ? p.at(0) : 2.0 * p.at(0) - p.at(1);
        return {QStringLiteral("L = %1   H = %2").arg(number(std::abs(p.at(1).x() - a.x())), number(std::abs(p.at(1).y() - a.y()))),
                QStringLiteral("Maiusc: quadrato")};
    }
    case DrawingTool::Ellipse: {
        const double angle = std::atan2(p.at(1).y() - p.at(0).y(), p.at(1).x() - p.at(0).x()) * 180.0 / M_PI;
        return {QStringLiteral("a = %1   b = %2   A = %3°").arg(number(pointDistance(p.at(0), p.at(1))), number(pointDistance(p.at(0), p.at(2))),
                                                               QString::number(angle, 'f', 2))};
    }
    case DrawingTool::Line:
        return {QStringLiteral("L = %1").arg(number(pointDistance(p.at(0), p.at(1))))};
    case DrawingTool::Arc: {
        const double r = pointDistance(p.at(0), p.at(1));
        const double a0 = std::atan2(p.at(1).y() - p.at(0).y(), p.at(1).x() - p.at(0).x());
        double a1 = std::atan2(p.at(2).y() - p.at(0).y(), p.at(2).x() - p.at(0).x());
        while (a1 <= a0) a1 += 2.0 * M_PI;
        const double sweep = a1 - a0;
        return {QStringLiteral("R = %1   A = %2°   L = %3").arg(number(r), QString::number(sweep * 180.0 / M_PI, 'f', 2), number(r * sweep))};
    }
    default: {
        double length = 0.0;
        try {
            for (const ForgeCad::Kernel::ProfileSegment &piece : ForgeCad::curveGeometry(curve))
                length += ForgeCad::Kernel::arcLength(*piece.curve, piece.range);
        } catch (const std::exception &) {
            return {};
        }
        QStringList result{QStringLiteral("L = %1   %2 punti").arg(number(length)).arg(curve.controlPoints.size())};
        if (curve.tool == DrawingTool::Nurbs && curve.controlPoints.size() < 4) result.append(QStringLiteral("NURBS: almeno 4 punti"));
        result.append(QStringLiteral("Invio o tasto destro per finire"));
        return result;
    }
    }
}

void CadViewport::drawCurvePreview(QPainter &painter, const SketchObject &sketch) const {
    CurveObject curve;
    if (!previewCurve(curve)) return;
    painter.setBrush(Qt::NoBrush);
    const bool centered = curve.tool == DrawingTool::Circle || curve.tool == DrawingTool::Arc || curve.tool == DrawingTool::Polygon
                       || curve.tool == DrawingTool::Ellipse || curve.tool == DrawingTool::CenterRectangle;
    if (centered) {
        painter.setPen(QPen(QColor(255, 170, 90, 170), 1.2, Qt::DotLine));
        painter.drawLine(projectWorldPoint(mapSketchPoint(curve.controlPoints.first(), sketch)),
                         projectWorldPoint(mapSketchPoint(cursorSketchPoint_, sketch)));
    }
    const bool radiusOnly = drawingTool_ == DrawingTool::Arc && curveControlPoints_.size() == 1;
    const bool spline = curve.tool == DrawingTool::Spline;
    painter.setPen(QPen(spline ? QColor(242, 166, 255) : QColor(255, 150, 60, radiusOnly ? 110 : 230),
                        spline ? 3.5 : 2.0, radiusOnly ? Qt::DotLine : Qt::DashLine, Qt::RoundCap));
    const QVector<QPointF> screen = projectSketchPolyline(curve.samples, sketch);
    painter.drawPolyline(screen.data(), int(screen.size()));
}

void CadViewport::drawInferenceTags(QPainter &painter) const {
    if (!sketchMode_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size() || !underMouse()) return;
    QStringList labels = currentInference_.labels;
    const bool lineTool = drawingTool_ == DrawingTool::Line || drawingTool_ == DrawingTool::Polyline
                       || drawingTool_ == DrawingTool::ConstructionLine;
    if (drawingTool_ == DrawingTool::Select) {
        labels.clear();
    } else if (isEditTool()) {
        labels = QStringList{editToolLabel()};
    } else if (!lineTool) {
        labels.clear();
        if (lastSnapKind_ != SnapKind::None && lastSnapKind_ != SnapKind::Nearest) labels.append(snapLabelAt(lastSnapKind_, lastSnapPoint_));
        labels += previewMeasures();
    } else if (hasPendingPoint_) {
        // Quote del segmento in costruzione.
        const QPointF delta = currentInference_.point - pendingPoint_;
        double angle = std::atan2(delta.y(), delta.x()) * 180.0 / M_PI;
        if (angle < 0.0) angle += 360.0;
        labels.prepend(QStringLiteral("L = %1   A = %2°").arg(pointLength(delta), 0, 'f', 4).arg(angle, 0, 'f', 2));
    }
    const SketchObject &sketch = sketches_.at(activeSketch_);
    if (lineTool && currentInference_.reference >= 0 && currentInference_.reference < sketch.segments.size()) {
        const SketchSegment &segment = sketch.segments.at(currentInference_.reference);
        QPen pen(QColor(120, 255, 170), 2.0, Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(projectWorldPoint(mapSketchPoint(segment.first, sketch)),
                         projectWorldPoint(mapSketchPoint(segment.second, sketch)));
    }
    if (labels.isEmpty()) return;
    painter.setFont(uiFont(9, QFont::DemiBold));
    const QFontMetrics metrics(painter.font());
    QPointF position = QPointF(lastMousePosition_) + QPointF(18.0, 22.0);
    // Etichette dentro la vista: vicino al bordo destro o in basso vanno dall'altra parte del cursore.
    double widest = 0.0;
    for (const QString &label : labels) widest = qMax(widest, double(metrics.horizontalAdvance(label)) + 10.0);
    if (position.x() + widest > width() - 4.0) position.setX(qMax(4.0, double(lastMousePosition_.x()) - 18.0 - widest));
    const double total = labels.size() * (metrics.height() + 6.0);
    if (position.y() + total > height() - 4.0) position.setY(qMax(4.0, double(lastMousePosition_.y()) - 12.0 - total));
    for (const QString &label : labels) {
        const QRectF box(position, QSizeF(metrics.horizontalAdvance(label) + 10.0, metrics.height() + 4.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 30, 40, 210));
        painter.drawRoundedRect(box, 4.0, 4.0);
        painter.setPen(QColor(120, 255, 170));
        painter.drawText(box, Qt::AlignCenter, label);
        position.ry() += box.height() + 2.0;
    }
    painter.setBrush(Qt::NoBrush);
}

QString CadViewport::editEllipseDimension(int index) {
    const SketchObject &before = sketches_.at(activeSketch_);
    const CurveObject curve = before.curves.at(index);
    if (curve.controlPoints.size() < 3) return QStringLiteral("Ellisse non valida.");
    const QPointF center = curve.controlPoints.at(0);
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Quota dell'ellisse"));
    auto *form = new QFormLayout(&dialog);
    const auto spin = [&dialog](double value, double minimum, double maximum) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setDecimals(6);
        box->setRange(minimum, maximum);
        box->setValue(value);
        return box;
    };
    auto *first = spin(pointDistance(center, curve.controlPoints.at(1)), 1e-6, 1e6);
    auto *second = spin(pointDistance(center, curve.controlPoints.at(2)), 1e-6, 1e6);
    const QPointF axis = curve.controlPoints.at(1) - center;
    auto *angle = spin(std::atan2(axis.y(), axis.x()) * 180.0 / M_PI, -360.0, 360.0);
    angle->setSuffix(QStringLiteral(" \u00B0"));
    auto *centerX = spin(center.x(), -1e6, 1e6), *centerY = spin(center.y(), -1e6, 1e6);
    form->addRow(QStringLiteral("Primo semiasse:"), first);
    form->addRow(QStringLiteral("Secondo semiasse:"), second);
    form->addRow(QStringLiteral("Angolo del primo semiasse:"), angle);
    form->addRow(QStringLiteral("Centro X:"), centerX);
    form->addRow(QStringLiteral("Centro Y:"), centerY);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return {};
    const QPointF newCenter(centerX->value(), centerY->value());
    const double radians = angle->value() * M_PI / 180.0;
    const QPointF u(std::cos(radians), std::sin(radians)), perpendicular(-u.y(), u.x());
    CurveObject changed = curve;
    changed.controlPoints[0] = newCenter;
    changed.controlPoints[1] = newCenter + first->value() * u;
    changed.controlPoints[2] = newCenter + second->value() * perpendicular;
    ForgeCad::recalculateCurve(changed, tessellationQuality_);
    if (!changed.numericallyValid) return QStringLiteral("Ellisse non valida.");
    recordUndo();
    SketchObject &sketch = sketches_[activeSketch_];
    const SketchObject beforeEdit = sketch;
    // I punti coincidenti con il centro lo seguono.
    if (pointDistance(center, newCenter) > 0.0) moveSketchPoint(sketch, -1, center, newCenter - center, QPointF(qQNaN(), qQNaN()));
    sketch.curves[index] = changed;
    QString failure;
    if (!solveActive(curveTargets(sketch, index), beforeEdit, &failure)) return failure;
    sketchEdited();
    return {};
}

QString CadViewport::editCurveDimension(int index) {
    if (!sketchMode_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return QStringLiteral("Entra in modalita' schizzo.");
    const SketchObject &before = sketches_.at(activeSketch_);
    if (index < 0 || index >= before.curves.size()) return QStringLiteral("Seleziona prima un cerchio, un arco o un poligono.");
    const CurveObject curve = before.curves.at(index);
    if (curve.tool == DrawingTool::Ellipse) return editEllipseDimension(index);
    const bool circle = curve.tool == DrawingTool::Circle, arc = curve.tool == DrawingTool::Arc, polygon = curve.tool == DrawingTool::Polygon;
    if (!(circle || arc || polygon) || curve.controlPoints.size() < (arc ? 3 : 2))
        return QStringLiteral("La quota si modifica su segmenti, cerchi, archi e poligoni.");
    const QPointF center = curve.controlPoints.at(0);
    const double radius = pointDistance(center, curve.controlPoints.at(1));
    QDialog dialog(this);
    dialog.setWindowTitle(circle ? QStringLiteral("Quota del cerchio") : arc ? QStringLiteral("Quota dell'arco") : QStringLiteral("Quota del poligono"));
    auto *form = new QFormLayout(&dialog);
    const auto makeBox = [&dialog](double value, double minimum) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setDecimals(6);
        box->setRange(minimum, 100000.0);
        box->setValue(value);
        return box;
    };
    auto *radiusBox = makeBox(radius, 1e-6);
    auto *diameterBox = makeBox(2.0 * radius, 2e-6);
    auto *centerX = makeBox(center.x(), -100000.0), *centerY = makeBox(center.y(), -100000.0);
    // Raggio e diametro legati.
    connect(radiusBox, &QDoubleSpinBox::valueChanged, &dialog, [diameterBox](double value) {
        const QSignalBlocker blocker(diameterBox);
        diameterBox->setValue(2.0 * value);
    });
    connect(diameterBox, &QDoubleSpinBox::valueChanged, &dialog, [radiusBox](double value) {
        const QSignalBlocker blocker(radiusBox);
        radiusBox->setValue(0.5 * value);
    });
    form->addRow(polygon ? QStringLiteral("Raggio (circoscritto):") : QStringLiteral("Raggio:"), radiusBox);
    form->addRow(QStringLiteral("Diametro:"), diameterBox);
    QSpinBox *sidesBox = nullptr;
    if (polygon) {
        sidesBox = new QSpinBox(&dialog);
        sidesBox->setRange(3, 64);
        sidesBox->setValue(curve.sides);
        form->addRow(QStringLiteral("Lati:"), sidesBox);
    }
    form->addRow(QStringLiteral("Centro X:"), centerX);
    form->addRow(QStringLiteral("Centro Y:"), centerY);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return {};
    const QPointF newCenter(centerX->value(), centerY->value());
    const double newRadius = radiusBox->value();
    recordUndo();
    SketchObject &sketch = sketches_[activeSketch_];
    const SketchObject beforeEdit = sketch;
    const QPointF nowhere(qQNaN(), qQNaN());
    // Prima il centro (con i punti coincidenti), poi i punti sulla curva
    // alla nuova distanza, nella stessa direzione di prima.
    moveSketchPoint(sketch, -1, center, newCenter - center, nowhere);
    for (int k = 1; k < curve.controlPoints.size(); ++k) {
        const QPointF old = curve.controlPoints.at(k);
        const QPointF moved = old + (newCenter - center);  // dove l'ha portato lo spostamento del centro
        const QPointF current = sketch.curves.at(index).controlPoints.at(k);
        const QPointF direction = old - center;
        const double length = pointLength(direction);
        if (length <= 0.0) continue;
        const QPointF target = newCenter + direction * (newRadius / length);
        // Se il punto non e' stato trascinato dal centro, si parte dalla sua posizione.
        const QPointF from = pointDistance(current, moved) <= ForgeCad::kSketchConnectionTolerance ? moved : current;
        moveSketchPoint(sketch, -1, from, target - from, newCenter);
        sketch.curves[index].controlPoints[k] = target;
    }
    if (polygon && sidesBox) sketch.curves[index].sides = sidesBox->value();
    for (CurveObject &c : sketch.curves) ForgeCad::recalculateCurve(c, tessellationQuality_);
    // Le quote del raggio seguono il valore scelto; il resto dello schizzo si adatta.
    syncCurveDimensions(sketch, index);
    QString failure;
    if (!solveActive(curveTargets(sketch, index), beforeEdit, &failure)) return failure;
    sketchEdited();
    return {};
}

void CadViewport::mouseDoubleClickEvent(QMouseEvent *event) {
    if ((interactionLocked_ || documentReadOnly_)) { event->accept(); return; }
    if (sketchMode_ && sketchViewUnlocked_) return;
    // Fuori dallo schizzo, doppio clic su una parte del modello: si modifica
    // la funzione che l'ha creata (come dall'albero).
    if (!sketchMode_ && event->button() == Qt::LeftButton && editBodyCallback_ && !(interactionLocked_ || documentReadOnly_) && !refPicking_ && !edgePicking_ && !trimPartPickFinished_
        && !sketchPickCallback_ && !sketchEntityPickCallback_ && !sketchOffsetSelectionCallback_) {
        const SceneSelection hit = pickSceneObject(event->pos());
        if (hit.kind == SceneObjectKind::Sketch) {
            // Doppio clic su uno schizzo: lo si apre (come dall'albero).
            event->accept();
            selectSketch(hit.index);
            return;
        }
        if (hit.kind == SceneObjectKind::Extrusion && !ForgeCad::isDatumBody(extrusions_.at(hit.index))) {
            FaceHit face;
            const int feature = pickBodyFace(hit.index, event->pos(), face) ? faceOwnerFeature(hit.index, event->pos(), face) : hit.index;
            event->accept();
            editBodyCallback_(feature);
            return;
        }
    }
    if (sketchMode_ && (isEditTool() || drawingTool_ == DrawingTool::Dimension)) {
        mousePressEvent(event);  // con gli strumenti di modifica il secondo clic e' un clic
        return;
    }
    if (sketchMode_ && event->button() == Qt::LeftButton && drawingTool_ == DrawingTool::Select) {
        // Doppio clic sul simbolo di una quota: il suo valore.
        const int glyph = constraintAt(event->position().toPoint());
        if (glyph >= 0) {
            editConstraintValue(glyph);
            return;
        }
    }
    if (sketchMode_ && event->button() == Qt::LeftButton) {
        const SketchElementSelection hit = findSketchElement(screenToSketchPoint(event->position().toPoint()));
        if (hit.kind == 0) {
            hasPendingPoint_ = false;
            editSegmentDimension(hit.index);
            return;
        }
        if (hit.kind == 1 && hit.index < sketches_.at(activeSketch_).curves.size()) {
            const DrawingTool tool = sketches_.at(activeSketch_).curves.at(hit.index).tool;
            if (tool == DrawingTool::Circle || tool == DrawingTool::Arc || tool == DrawingTool::Polygon || tool == DrawingTool::Ellipse) {
                hasPendingPoint_ = false;
                curveControlPoints_.clear();
                const QString error = editCurveDimension(hit.index);
                if (!error.isEmpty()) showStatus(error);
                return;
            }
        }
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void CadViewport::contextMenuEvent(QContextMenuEvent *event) {
    // Nello schizzo il tasto destro trascinato ruota la vista: con il mouse
    // il menu si decide al rilascio (se non c'e' stato trascinamento).
    if (sketchMode_ && event->reason() == QContextMenuEvent::Mouse) {
        if (rightDragged_) {
            event->accept();
            return;
        }
        if (QGuiApplication::mouseButtons() & Qt::RightButton) {
            contextPending_ = true;
            event->accept();
            return;
        }
    }
    if (sketchMode_ && (drawingTool_ == DrawingTool::Spline || drawingTool_ == DrawingTool::Nurbs)) {
        finalizeCurve();
        event->accept();
        return;
    }
    if (sketchMode_ && drawingTool_ == DrawingTool::Select) {
        const QPointF point = screenToSketchPoint(event->pos());
        int curveIndex = -1, control = -1;
        EditablePointKind pointKind = EditablePointKind::Control;
        const bool onPoint = findCurveEditPoint(point, curveIndex, control, pointKind);
        if (!onPoint) curveIndex = findNearestFreeCurve(point);
        if (curveIndex >= 0) {
            CurveObject source = sketches_.at(activeSketch_).curves.at(curveIndex);
            if (source.tool == DrawingTool::Spline && source.tangentHandles.size() != source.controlPoints.size())
                ForgeCad::initializeTangentHandles(source);
            if (source.tool == DrawingTool::Spline) source.tangentLinked.resize(source.controlPoints.size());
            CadMenu menu(this);
            QAction *edit = menu.addAction(QStringLiteral("Modifica punti e maniglie..."));
            QAction *insert = menu.addAction(QStringLiteral("Aggiungi punto di controllo qui"));
            QAction *remove = onPoint && pointKind == EditablePointKind::Control
                ? menu.addAction(QStringLiteral("Elimina punto di controllo")) : nullptr;
            QAction *link = nullptr, *dimension = nullptr;
            if (source.tool == DrawingTool::Spline && onPoint) {
                link = menu.addAction(source.tangentLinked.value(control)
                                          ? QStringLiteral("Scollega le maniglie")
                                          : QStringLiteral("Collega le maniglie (tangenza)"));
                if (pointKind != EditablePointKind::Control) dimension = menu.addAction(QStringLiteral("Quota lunghezza maniglia..."));
            }
            insert->setEnabled(canChangeControlPointCount(source));
            if (remove) remove->setEnabled(canChangeControlPointCount(source) && source.controlPoints.size() > (source.tool == DrawingTool::Nurbs ? 4 : 2));
            const QAction *chosen = menu.exec(event->globalPos());
            // Esc o clic fuori dal menu restituiscono nullptr, come le
            // azioni opzionali non presenti: non sono comandi da eseguire.
            if (!chosen) {
                event->accept();
                return;
            }
            if (chosen == edit) editFreeCurve(curveIndex, onPoint ? control : -1);
            else if (chosen == insert) {
                QVector<int> origins; for (int k = 0; k < source.controlPoints.size(); ++k) origins.append(k);
                int at = source.controlPoints.size(); double best = std::numeric_limits<double>::max();
                for (int k = 1; k < source.controlPoints.size(); ++k) {
                    const double d = distanceToSegment(point, source.controlPoints.at(k - 1), source.controlPoints.at(k));
                    if (d < best) { best = d; at = k; }
                }
                source.controlPoints.insert(at, point); origins.insert(at, -1);
                if (source.tool == DrawingTool::Nurbs) {
                    if (source.weights.size() != source.controlPoints.size() - 1)
                        source.weights.fill(1.0, source.controlPoints.size() - 1);
                    source.weights.insert(at, 1.0);
                }
                else {
                    source.tangentHandles.insert(at, {point - QPointF(1.0, 0.0), point + QPointF(1.0, 0.0)});
                    source.tangentLinked.insert(at, true);
                }
                commitFreeCurveEdit(curveIndex, source, origins);
            } else if (chosen == remove) {
                QVector<int> origins; for (int k = 0; k < source.controlPoints.size(); ++k) if (k != control) origins.append(k);
                source.controlPoints.removeAt(control);
                if (source.tool == DrawingTool::Nurbs && control < source.weights.size()) source.weights.removeAt(control);
                else if (source.tool == DrawingTool::Spline) { source.tangentHandles.removeAt(control); source.tangentLinked.removeAt(control); }
                commitFreeCurveEdit(curveIndex, source, origins);
            } else if (chosen == link) {
                recordUndo();
                CurveObject &curve = sketches_[activeSketch_].curves[curveIndex];
                curve.tangentLinked.resize(curve.controlPoints.size());
                curve.tangentLinked[control] = !curve.tangentLinked.at(control);
                if (curve.tangentLinked.at(control)) {
                    QPointF d = curve.controlPoints.at(control) - curve.tangentHandles.at(control).first;
                    const double l = pointLength(d), out = pointDistance(curve.controlPoints.at(control), curve.tangentHandles.at(control).second);
                    if (l > 0.0) curve.tangentHandles[control].second = curve.controlPoints.at(control) + d * (out / l);
                }
                ForgeCad::recalculateCurve(curve, tessellationQuality_); sketchEdited();
            } else if (chosen == dimension) {
                const int side = pointKind == EditablePointKind::TangentIn ? 0 : 1;
                const double old = pointDistance(source.controlPoints.at(control), side == 0 ? source.tangentHandles.at(control).first
                                                                                             : source.tangentHandles.at(control).second);
                bool ok = false;
                const double value = ForgeCad::getDouble(this, QStringLiteral("Quota maniglia"), QStringLiteral("Lunghezza:"), old,
                                                          1e-9, 1e9, 6, &ok);
                if (ok) {
                    const DocumentState snapshot = documentState();
                    SketchObject &sketch = sketches_[activeSketch_];
                    const SketchObject before = sketch;
                    SketchConstraint constraint = ForgeCad::makeConstraint(sketch, ConstraintType::Distance,
                        {{1, curveIndex, control}, {1, curveIndex, handlePoint(control, side)}});
                    constraint.value = value;
                    sketch.geometricConstraints.append(constraint);
                    QString failure;
                    if (!solveActive({}, before, &failure)) showStatus(failure);
                    else { history_.record(snapshot); sketchEdited(); }
                }
            }
            event->accept();
            return;
        }
    }
    if ((interactionLocked_ || documentReadOnly_) || refPicking_) {
        event->accept();
        return;
    }
    if (!sketchMode_) {
        const SceneSelection hit = pickSceneObject(event->pos());
        if (hit.kind == SceneObjectKind::Plane && planeContextCallback_) {
            planeContextCallback_(hit.index);
            event->accept();
            return;
        }
        if ((hit.kind == SceneObjectKind::Sketch || hit.kind == SceneObjectKind::Extrusion)
            && selectedObjects_.size() > 1 && selectedObjects_.contains(hit)) {
            // Piu' oggetti scelti: le operazioni comuni.
            const QVector<SceneSelection> objects = selectedObjects_;
            CadMenu menu(this);
            QAction *hide = menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")),
                                           QStringLiteral("Nascondi %1 oggetti").arg(objects.size()));
            QAction *remove = menu.addAction(ForgeCad::commandIcon(QStringLiteral("delete")),
                                             QStringLiteral("Elimina %1 oggetti").arg(objects.size()));
            const QAction *chosen = menu.exec(event->globalPos());
            if (chosen && chosen == hide) setObjectsVisible(objects, false);
            else if (chosen && chosen == remove) deleteObjectsOrFeatures(objects);
            event->accept();
            return;
        }
        if (hit.kind == SceneObjectKind::Sketch || hit.kind == SceneObjectKind::Extrusion) {
            CadMenu menu(this);
            const bool datum = hit.kind == SceneObjectKind::Extrusion && ForgeCad::isDatumBody(extrusions_.at(hit.index));
            // Faccia sotto il puntatore: la funzione che l'ha creata (si
            // modifica quella), schizzo sul suo piano, raccordo o smusso dei suoi bordi.
            FaceHit face;
            const bool onFace = hit.kind == SceneObjectKind::Extrusion && !datum && pickBodyFace(hit.index, event->pos(), face);
            const int feature = hit.kind != SceneObjectKind::Extrusion ? -1 : onFace ? faceOwnerFeature(hit.index, event->pos(), face) : hit.index;
            if (feature >= 0 && selectionCallback_) selectionCallback_({SceneObjectKind::Extrusion, feature, -1});
            QAction *editSketch = hit.kind == SceneObjectKind::Sketch
                ? menu.addAction(QStringLiteral("Modifica schizzo")) : nullptr;
            QAction *editBody = feature >= 0 && editBodyCallback_
                ? menu.addAction(QStringLiteral("Modifica \"%1\"...").arg(extrusions_.at(feature).name)) : nullptr;
            if (editSketch) editSketch->setIcon(ForgeCad::commandIcon(QStringLiteral("edit")));
            if (editBody) editBody->setIcon(ForgeCad::commandIcon(featureIconName(extrusions_.at(feature))));
            // La funzione che ha creato la faccia e' fatta da uno schizzo: lo si apre.
            const int featureSketch = feature >= 0 && extrusions_.at(feature).operation < 0 ? extrusions_.at(feature).sketchIndex : -1;
            QAction *editFeatureSketch = featureSketch >= 0 && featureSketch < sketches_.size()
                    && (extrusions_.at(feature).feature == BodyFeature::Extrusion || extrusions_.at(feature).feature == BodyFeature::Revolution
                        || extrusions_.at(feature).feature == BodyFeature::Sweep)
                ? menu.addAction(QStringLiteral("Modifica lo schizzo \"%1\"").arg(sketches_.at(featureSketch).name)) : nullptr;
            QAction *datumSketch = datum ? menu.addAction(QStringLiteral("Nuovo schizzo sul piano")) : nullptr;
            if (editFeatureSketch) editFeatureSketch->setIcon(ForgeCad::commandIcon(QStringLiteral("newSketch")));
            if (datumSketch) datumSketch->setIcon(ForgeCad::commandIcon(QStringLiteral("newSketch")));
            if (datumSketch) datumSketch->setEnabled(extrusions_.at(hit.index).datumValid);
            QAction *faceSketch = nullptr, *faceFillet = nullptr, *faceChamfer = nullptr;
            if (onFace) {
                menu.addSeparator();
                faceSketch = menu.addAction(QStringLiteral("Nuovo schizzo sulla faccia"));
                faceSketch->setIcon(ForgeCad::commandIcon(QStringLiteral("faceSketch")));
                faceSketch->setEnabled(face.planar);
                const bool canBlend = !face.edges.isEmpty() && edgePickFinished_;
                faceFillet = menu.addAction(QStringLiteral("Raccordo dei bordi della faccia..."));
                faceChamfer = menu.addAction(QStringLiteral("Smusso dei bordi della faccia..."));
                faceFillet->setIcon(ForgeCad::commandIcon(QStringLiteral("fillet")));
                faceChamfer->setIcon(ForgeCad::commandIcon(QStringLiteral("chamfer")));
                faceFillet->setEnabled(canBlend);
                faceChamfer->setEnabled(canBlend);
                menu.addSeparator();
            }
            QAction *meshColor = hit.kind == SceneObjectKind::Extrusion && modelBodyIndexForFeature(hit.index) >= 0
                ? menu.addAction(ForgeCad::commandIcon(QStringLiteral("meshColor")), QStringLiteral("Colore mesh...")) : nullptr;
            QAction *hide = menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")), QStringLiteral("Nascondi"));
            QAction *remove = menu.addAction(ForgeCad::commandIcon(QStringLiteral("delete")), QStringLiteral("Elimina"));
            const QAction *chosen = menu.exec(event->globalPos());
            if (chosen && chosen == editSketch) selectSketch(hit.index);
            else if (chosen && chosen == editBody) editBodyCallback_(feature);
            else if (chosen && chosen == editFeatureSketch) selectSketch(featureSketch);
            else if (chosen && chosen == datumSketch) createDatumSketch(hit.index, QStringLiteral("Schizzo %1").arg(sketches_.size() + 1));
            else if (chosen && chosen == faceSketch)
                createFaceSketch(hit.index, face, QStringLiteral("Schizzo %1").arg(sketches_.size() + 1));
            else if (chosen && (chosen == faceFillet || chosen == faceChamfer))
                edgePickFinished_(hit.index, face.edges, chosen == faceChamfer);
            else if (chosen && chosen == meshColor) {
                const int model = modelBodyIndexForFeature(hit.index);
                const QColor color = QColorDialog::getColor(modelBodyMeshColor(model), this, QStringLiteral("Colore mesh del corpo"));
                if (color.isValid()) setModelBodyMeshColor(model, color);
            }
            else if (chosen && chosen == remove) deleteObject(hit.kind, hit.index);
            else if (chosen == hide) setObjectVisible(hit.kind, hit.index, false);
            event->accept();
            return;
        }
    }
    QOpenGLWidget::contextMenuEvent(event);
}

void CadViewport::mouseMoveEvent(QMouseEvent *event) {
    if (featureHandleDragging_) {
        if (!(event->buttons() & Qt::LeftButton)) { featureHandleDragging_ = false; unsetCursor(); update(); return; }
        dragFeatureHandle(event->position());
        event->accept();
        return;
    }
    if (!event->buttons() && featureHandleAt(event->position())) { setCursor(Qt::OpenHandCursor); return; }
    if (selectionPending_ && (event->buttons() & Qt::LeftButton)
        && (event->position().toPoint() - selectionPress_).manhattanLength() >= QApplication::startDragDistance())
        selectionDragged_ = true;
    if (selectionPending_ && (event->buttons() & Qt::LeftButton) && !selectionDragged_) return;
    const QPoint currentPosition = event->position().toPoint();
    if (planeResizing_) {
        const double delta = QPointF::dotProduct(QPointF(currentPosition - planeResizeStart_), planeResizeDirection_)
            / QPointF::dotProduct(planeResizeDirection_, planeResizeDirection_);
        const double half = qBound(1e-4, planeResizeHalf_ + delta, 1e8);
        if (planeResizeTarget_.kind == SceneObjectKind::Plane) {
            const double scale = qBound(0.05, planeResizeOriginal_ * half / planeResizeHalf_, 20.0);
            for (double &planeScale : referencePlaneScales_) planeScale = scale;
        } else extrusions_[planeResizeTarget_.index].datum.size = half;
        sceneBoundsDirty_ = true;
        camera_.autoFit_ = false;
        update();
        return;
    }
    if (!event->buttons() && planeResizeHandleAt(currentPosition) >= 0) {
        setCursor(Qt::SizeFDiagCursor);
        return;
    }
    if (!event->buttons() && !sketchMode_ && !sectionHover_) unsetCursor();
    if (sectionDragging_) {
        const double delta = sectionDragDelta(currentPosition - lastMousePosition_);
        lastMousePosition_ = currentPosition;
        if (delta != 0.0 && sectionDragged_) sectionDragged_(delta);
        return;
    }
    if (!sketchMode_ && !(event->buttons() & Qt::LeftButton) && section_.enabled && sectionDragged_) {
        const bool over = sectionHandleAt(currentPosition);
        if (over != sectionHover_) {
            sectionHover_ = over;
            if (over) setCursor(Qt::SizeAllCursor);
            else unsetCursor();
            update();
        }
    }
    if (panning_) {
        // Un pixel vale 8 * zoom / 8 / altezza unita' della vista.
        const float unit = 8.0f * (camera_.zoom_ / 8.0f) / float(qMax(1, height()));
        const QPoint delta = currentPosition - lastMousePosition_;
        camera_.autoFit_ = false;
        camera_.panX_ += float(delta.x()) * unit;
        camera_.panY_ -= float(delta.y()) * unit;
        lastMousePosition_ = currentPosition;
        update();
        return;
    }
    if(spatialAdd_) {
        if(event->buttons() & (Qt::LeftButton|Qt::RightButton)) {
            if((currentPosition-spatialPress_).manhattanLength()>=QApplication::startDragDistance())spatialDragged_=true;
            if(event->buttons() & Qt::RightButton || spatialDragged_) {
                camera_.autoFit_=false;orbitView(currentPosition-lastMousePosition_);
            }
        }
        lastMousePosition_=currentPosition;
        spatialHoverValid_=spatialPointAt(currentPosition,spatialHover_);
        setCursor(Qt::CrossCursor);update();event->accept();return;
    }
    if ((interactionLocked_ || documentReadOnly_) && !refPicking_ && !edgePicking_ && !trimPartPickFinished_
        && !sketchPickCallback_ && !sketchEntityPickCallback_ && !sketchOffsetSelectionCallback_) {
        if (event->buttons() & Qt::LeftButton) {
            camera_.autoFit_ = false;
            orbitView(currentPosition - lastMousePosition_);
        }
        lastMousePosition_ = currentPosition;
        hover_ = {};
        update();
        return;
    }
    if (boxArmed_ && (event->buttons() & Qt::LeftButton)) {
        // Riquadro di selezione (dopo qualche pixel: un clic resta un clic).
        boxEnd_ = currentPosition;
        if (!boxSelecting_ && (boxEnd_ - boxStart_).manhattanLength() >= 5) boxSelecting_ = true;
        lastMousePosition_ = currentPosition;
        if (boxSelecting_) update();
        return;
    }
    if (sketchMode_ && (event->buttons() & Qt::RightButton)) {
        // Rotazione della vista dello schizzo (dopo qualche pixel, per non confondersi con il clic).
        if (!rightDragged_ && (currentPosition - rightPressPosition_).manhattanLength() < 4) return;
        if (!rightDragged_) {
            rightDragged_ = true;
            showStatus(QStringLiteral("Vista ruotata: \"Vista normale allo schizzo\" (Ctrl+8) la rimette perpendicolare al piano"));
        }
        const QPoint delta = currentPosition - lastMousePosition_;
        camera_.autoFit_ = false;
        orbitView(delta);
        if (!sketchViewUnlocked_) sketchViewRotated_ = true;
        lastMousePosition_ = currentPosition;
        update();
        return;
    }
    if (sketchMode_ && sketchViewUnlocked_) {
        // Estrusione o rivoluzione dallo schizzo: si ruota la vista (codice in fondo).
    } else if (sketchMode_ && drawingTool_ == DrawingTool::ConvertEdges) {
        lastMousePosition_ = currentPosition;
        SketchReferenceHit hit;
        pickSketchReference(currentPosition, hit);
        if (hit.body != convertHover_.body || hit.polyline != convertHover_.polyline || hit.sketch != convertHover_.sketch
            || hit.entityKind != convertHover_.entityKind || hit.entity != convertHover_.entity) {
            convertHover_ = hit;
            update();
        }
        return;
    }
    if (sketchMode_ && !sketchViewUnlocked_) {
        lastMousePosition_ = currentPosition;
        const QPointF rawPoint = screenToSketchPoint(currentPosition);
        if (drawingTool_ == DrawingTool::Dimension) {
            // Strumento Quota: al passaggio il punto o l'entita' che si sceglierebbe, e la quota in anteprima.
            ConstraintRef ref;
            dimensionHover_ = dimensionRefAt(rawPoint, ref) ? ref : ConstraintRef();
            sketchHover_ = dimensionHover_.kind >= 0 && dimensionHover_.point < 0 && dimensionHover_.kind != 2
                               ? SketchElementSelection{dimensionHover_.kind, dimensionHover_.element}
                               : SketchElementSelection();
            refreshDimensionPreview(rawPoint);
            update();
            return;
        }
        cursorSketchPoint_ = snapPoint(rawPoint, drawingTool_ != DrawingTool::Spline
            && drawingTool_ != DrawingTool::Nurbs);
        if (drawingTool_ == DrawingTool::Line || drawingTool_ == DrawingTool::Polyline || drawingTool_ == DrawingTool::ConstructionLine) {
            currentInference_ = inferLinePoint(rawPoint);
            cursorSketchPoint_ = currentInference_.point;
        } else {
            currentInference_ = {};
        }
        if (dimensionDrag_ >= 0 && (event->buttons() & Qt::LeftButton)) {
            SketchObject &sketch = sketches_[activeSketch_];
            if (dimensionDrag_ < sketch.geometricConstraints.size()) {
                if (!dragRecorded_) {
                    history_.record(dragSnapshot_);
                    dragRecorded_ = true;
                }
                // Spostare una quota non ne cambia il tipo: raggio o diametro dall'asse
                // si sceglie mentre la si crea, poi solo nella finestra del valore.
                // Una quota d'angolo passa al settore del puntatore (interno o
                // supplementare: stessa geometria, misura attuale).
                SketchConstraint &dragged = sketch.geometricConstraints[dimensionDrag_];
                dragged.placement = rawPoint;
                dragged.placed = true;
                if (dragged.type == ConstraintType::Angle) ForgeCad::chooseAngleSector(sketch, dragged, rawPoint);
                analysisDirty_ = true;
            }
            update();
            return;
        }
        // Quota dall'asse appena creata: segue il puntatore finche' non si fa clic.
        if (dimensionPlacing_ >= 0) {
            SketchObject &sketch = sketches_[activeSketch_];
            if (dimensionPlacing_ < sketch.geometricConstraints.size()) placeAxisDimension(sketch.geometricConstraints[dimensionPlacing_], rawPoint);
            showStatus(QStringLiteral("Quota dall'asse: dalla parte del punto e' un raggio, oltre l'asse un diametro. Clic per fissarla."));
            update();
            return;
        }
        if(circleRadiusDrag_>=0&&(event->buttons()&Qt::LeftButton)){
            auto &sketch=sketches_[activeSketch_];const auto before=sketch;auto &circle=sketch.curves[circleRadiusDrag_];const auto center=circle.controlPoints[0];
            const double radius=circleInitialRadius_+pointDistance(rawPoint,center)-circlePointerRadius_;const auto d=circle.controlPoints[1]-center;const double length=pointLength(d);
            if(radius>1e-9&&length>1e-15){circle.controlPoints[1]=center+d*(radius/length);solveActiveDrag({{{1,circleRadiusDrag_,1},circle.controlPoints[1]}},before,{{{1,circleRadiusDrag_,0},center}});}
            update();return;
        }
        if (pointDragActive_ && (event->buttons() & Qt::LeftButton)) {
            dragSketchPoint(dragSnapPoint(rawPoint));
            update();
            return;
        }
        if (bodyDragSegment_ >= 0 && (event->buttons() & Qt::LeftButton)) {
            dragSketchSegment(rawPoint);
            update();
            return;
        }
        if (drawingTool_ == DrawingTool::Select && !(event->buttons() & Qt::LeftButton)) {
            const int glyph = constraintAt(currentPosition);
            if (glyph != constraintHover_) constraintHover_ = glyph;
        } else {
            constraintHover_ = -1;
        }
        if (drawingTool_ == DrawingTool::Select && !panKeyHeld_ && !(event->buttons() & Qt::LeftButton)) {
            QPointF unused;
            if (segmentEndpointAt(rawPoint, unused) || findCurveEditPoint(rawPoint, draggingCurveIndex_, draggingControlIndex_, draggingPointKind_))
                setCursor(Qt::SizeAllCursor);
            else {
                const auto hit=findSketchElement(rawPoint);
                if(hit.kind==1&&sketches_[activeSketch_].curves[hit.index].tool==DrawingTool::Circle)setCursor(Qt::SizeHorCursor);
                else unsetCursor();
            }
            draggingCurveIndex_ = -1;
            draggingControlIndex_ = -1;
            draggingPointKind_ = EditablePointKind::Control;
        }
        if (draggingControlPoint_ && draggingCurveIndex_ >= 0) {
            const SketchObject sketchBefore = sketches_[activeSketch_];
            CurveObject &curve = sketches_[activeSketch_].curves[draggingCurveIndex_];
            if (draggingPointKind_ == EditablePointKind::Control) {
                const QPointF before = curve.controlPoints.at(draggingControlIndex_);
                QPointF position = curve.tool == DrawingTool::Spline || curve.tool == DrawingTool::Nurbs ? rawPoint : cursorSketchPoint_;
                // La fine dell'arco sta sul suo cerchio: si trascina solo il suo angolo.
                if (curve.tool == DrawingTool::Arc && draggingControlIndex_ == 2 && curve.controlPoints.size() >= 3) {
                    const QPointF c = curve.controlPoints.at(0), r = position - c;
                    const double radius = pointDistance(c, curve.controlPoints.at(1)), l = pointLength(r);
                    if (l > 0.0) position = c + r * (radius / l);
                }
                if(curve.tool==DrawingTool::Spline||curve.tool==DrawingTool::Nurbs||curve.tool==DrawingTool::Circle)
                    moveSketchPoint(sketches_[activeSketch_],-1,before,position-before,QPointF(qQNaN(),qQNaN()));
                else curve.controlPoints[draggingControlIndex_]=position;
                normalizeEllipse(curve, draggingControlIndex_, before);
                SketchObject &sketch = sketches_[activeSketch_];
                if (!solveActiveDrag(targetsAt(sketch,curve.controlPoints.at(draggingControlIndex_)), sketchBefore)) {
                    showStatus(QStringLiteral("Il punto e' bloccato dai vincoli."));
                    update();
                    return;
                }
                (void)sketch;
            } else {
                // La maniglia passa dal risolutore: i vincoli sulle maniglie (tangenze, quote) restano veri.
                const int side = draggingPointKind_ == EditablePointKind::TangentIn ? 0 : 1;
                moveSplineHandle(curve,draggingControlIndex_,side,rawPoint);
                if (!solveActiveDrag({{{1, draggingCurveIndex_, handlePoint(draggingControlIndex_, side)}, rawPoint}}, sketchBefore)) {
                    showStatus(QStringLiteral("La maniglia e' bloccata dai vincoli."));
                    update();
                    return;
                }
            }
            // solveActive ricampiona gia' le curve. Con gli offset puo'
            // sostituire lo schizzo: il riferimento `curve` non e' piu' valido.
        } else {
            snapPoint(rawPoint, drawingTool_ != DrawingTool::Spline
                && drawingTool_ != DrawingTool::Nurbs);
            if (drawingTool_ == DrawingTool::Select) lastSnapKind_ = SnapKind::None;
            sketchHover_ = findSketchElement(rawPoint);
            trimPreview_.clear();
            if (drawingTool_ == DrawingTool::Trim && sketchHover_.kind >= 0)
                trimPreview_ = ForgeCad::trimPreview(sketches_.at(activeSketch_), {sketchHover_.kind, sketchHover_.index}, rawPoint);
        }
        update();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        // L'evidenziazione al passaggio si calcola una volta per giro del
        // ciclo degli eventi, con l'ultima posizione: i movimenti che
        // arrivano mentre si calcola non si accodano (era il ritardo nel
        // ruotare e zoomare).
        hoverPosition_ = currentPosition;
        if (!hoverScheduled_) {
            hoverScheduled_ = true;
            QTimer::singleShot(0, this, [this] {
                hoverScheduled_ = false;
                updateHover(hoverPosition_);
            });
        }
    }
    if (event->buttons() & Qt::LeftButton) {
        const QPoint delta = currentPosition - lastMousePosition_;
        camera_.autoFit_ = false;
        orbitView(delta);  // la vista inclinata di uno schizzo su faccia si raddrizza ruotando
        update();
    }
    lastMousePosition_ = currentPosition;
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

bool CadViewport::event(QEvent *event) {
    if (event->type() == QEvent::ToolTip && topologyIdsVisible_ && !sketchMode_) {
        const auto *help = static_cast<QHelpEvent *>(event);
        TopologyLabelItem label;
        if (pickTopologyLabel(help->pos(), label) && extrusions_.at(label.body).forgeBody) {
            const QString evolution = ForgeCad::entityEvolutionText(*extrusions_.at(label.body).forgeBody,
                label.edge ? ForgeCad::Kernel::EntityKind::Edge : ForgeCad::Kernel::EntityKind::Face, label.subshape);
            QToolTip::showText(help->globalPos(), evolution, this);
        } else QToolTip::hideText();
        event->accept();
        return true;
    }
    if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent *>(event)->key() == panKey_) {
        event->accept();
        return true;
    }
    // Con lo strumento Quota H, V, O e A danno l'orientamento della quota (non i vincoli della prossima linea).
    if (event->type() == QEvent::ShortcutOverride && sketchMode_ && drawingTool_ == DrawingTool::Dimension) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier
            && (key->key() == Qt::Key_H || key->key() == Qt::Key_V || key->key() == Qt::Key_O || key->key() == Qt::Key_A)) {
            event->accept();
            return true;
        }
    }
    return QOpenGLWidget::event(event);
}

void CadViewport::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == panKey_ && !event->isAutoRepeat()) {
        panKeyHeld_ = false;
        if (!panning_) unsetCursor();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(event);
}

void CadViewport::focusOutEvent(QFocusEvent *event) {
    featureHandleDragging_ = false;
    panKeyHeld_ = false;
    panning_ = false;
    unsetCursor();
    QOpenGLWidget::focusOutEvent(event);
}

void CadViewport::mouseReleaseEvent(QMouseEvent *event) {
    if (featureHandleDragging_ && event->button() == Qt::LeftButton) {
        featureHandleDragging_ = false;
        dragFeatureHandle(event->position());
        unsetCursor();
        update();
        event->accept();
        return;
    }
    camera_.orbitPivotValid_ = false;
    if(spatialAdd_ && !panning_) {
        if(event->button()==Qt::LeftButton && spatialPressed_) {
            spatialPressed_=false;
            if(!spatialDragged_ && (event->position().toPoint()-spatialPress_).manhattanLength()<QApplication::startDragDistance()) {
                SpatialPoint point;
                if(spatialPointAt(event->position().toPoint(),point)){auto add=spatialAdd_;add(point);}
                else showStatus(QStringLiteral("Il piano temporaneo e' visto di taglio: ruota la vista o cambia piano."));
            }
        }
        event->accept();return;
    }
    if (selectionPending_ && event->button() == Qt::LeftButton) {
        selectionPending_ = false;
        if (!selectionDragged_
            && (event->position().toPoint() - selectionPress_).manhattanLength() < QApplication::startDragDistance()) {
            QMouseEvent click(QEvent::MouseButtonPress, event->position(), event->globalPosition(),
                              Qt::LeftButton, Qt::LeftButton, event->modifiers());
            releasingSelection_ = true;
            mousePressEvent(&click);
            releasingSelection_ = false;
        }
        return;
    }
    if (planeResizing_ && event->button() == Qt::LeftButton) { finishPlaneResize(false); return; }
    if (sectionDragging_) {
        sectionDragging_ = false;
        if (!sectionHover_) unsetCursor();
        return;
    }
    if (panning_) {
        panning_ = false;
        if (panKeyHeld_) setCursor(Qt::OpenHandCursor);
        else unsetCursor();
        return;
    }
    if (boxArmed_ && event->button() == Qt::LeftButton) {
        const bool dragged = boxSelecting_;
        boxArmed_ = false;
        boxSelecting_ = false;
        if (dragged) finishBoxSelection();
        else if (!sketchMode_) toggleObjectAt(event->position().toPoint());
        update();
        if (!sketchMode_ || dragged) return;  // nello schizzo un clic nel vuoto prosegue come prima
    }
    if (sketchMode_ && event->button() == Qt::RightButton) {
        // Un clic destro senza trascinamento: quello che faceva il menu (chiude la spline).
        if (contextPending_ && !rightDragged_ && (drawingTool_ == DrawingTool::Spline || drawingTool_ == DrawingTool::Nurbs)) {
            const int before = activeSketch_ >= 0 && activeSketch_ < sketches_.size() ? sketches_.at(activeSketch_).curves.size() : 0;
            finalizeCurve();
            if (activeSketch_ >= 0 && activeSketch_ < sketches_.size() && sketches_.at(activeSketch_).curves.size() > before)
                editFreeCurve(sketches_.at(activeSketch_).curves.size() - 1);
        }
        else if (contextPending_ && !rightDragged_ && drawingTool_ == DrawingTool::Select) {
            contextPending_ = false;
            QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, lastMousePosition_, mapToGlobal(lastMousePosition_));
            contextMenuEvent(&menuEvent);
        }
        contextPending_ = false;
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (dragRecorded_ && (pointDragActive_ || bodyDragSegment_ >= 0) && activeSketch_ >= 0 && activeSketch_ < sketches_.size()) {
            // Le quote di lunghezza che il trascinamento ha cambiato non valgono piu'.
            SketchObject &sketch = sketches_[activeSketch_];
            for (int index = 0; index < sketch.segments.size() && index < sketch.segmentLengths.size(); ++index)
                if (sketch.segmentLengths.at(index) > 0.0
                    && std::abs(pointDistance(sketch.segments.at(index).first, sketch.segments.at(index).second) - sketch.segmentLengths.at(index)) > 1e-9)
                    sketch.segmentLengths[index] = 0.0;
        }
        if (dimensionDrag_ >= 0 && dragRecorded_) {
            dragRecorded_ = false;
            documentChanged();
        }
        dimensionDrag_ = -1;
        pointDragActive_ = false;
        circleRadiusDrag_=-1;
        bodyDragSegment_ = -1;
        if (dragRecorded_) sketchEdited();
        dragRecorded_ = false;
        draggingControlPoint_ = false;
        draggingCurveIndex_ = -1;
        draggingControlIndex_ = -1;
        draggingPointKind_ = EditablePointKind::Control;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void CadViewport::leaveEvent(QEvent *event) {
    if (hover_.kind != SceneObjectKind::None || sketchHover_.kind >= 0 || trimPartHover_ >= 0) {
        hover_ = {};
        sketchHover_ = {};
        trimPartHover_ = -1;
        unsetCursor();
        update();
    }
    QOpenGLWidget::leaveEvent(event);
}

void CadViewport::wheelEvent(QWheelEvent *event) {
    if (!wheelZoomEnabled_) { event->ignore(); return; }
    const QPoint angleDelta = event->angleDelta();
    const QPoint pixelDelta = event->pixelDelta();
    const int delta = angleDelta.y() != 0 ? angleDelta.y() : pixelDelta.y();
    if (delta == 0) { event->ignore(); return; }
    camera_.autoFit_ = false;
    setZoom(camera_.zoom_ * std::pow(0.9985f, float(delta)));
    event->accept();
}

DocumentState CadViewport::documentState(bool normalizeVisibility ) const {
    DocumentState state(documentVersion_, documentBaseCheckinId_);
    state.sketches = sketches_;
    state.extrusions = extrusions_;
    state.modelBodies = modelBodies_;
    state.pendingSketchChanges=deferredSketches_.values();
    // La visibilita' del documento conserva la preferenza del corpo;
    // il filtro dei corpi consumati riguarda soltanto la scena corrente.
    if (normalizeVisibility) for (ExtrusionObject &feature : state.extrusions)
        for (const ModelBody &body : state.modelBodies)
            if (feature.modelBodyId == body.id) feature.visible = feature.featureId == body.tipFeatureId && body.visible;
    state.orientation = camera_.orientation_;
    state.orientationSet = true;
    state.lengthUnit = lengthUnit_;
    state.lengthUnitSet = true;
    return state;
}

void CadViewport::selectionChanged() {
    if (sketchOffsetSelectionCallback_) sketchOffsetSelectionCallback_();
    if (constraintPanelCallback_) constraintPanelCallback_();
}

bool CadViewport::solveActive(const QVector<ForgeCad::PointTarget> &targets, const SketchObject &before, QString *error ) {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return false;
    SketchObject &sketch = sketches_[activeSketch_];
    const ForgeCad::SolveResult result = ForgeCad::solveSketch(sketch, targets);
    analysisDirty_ = true;
    if (!result.ok) {
        sketch = before;
        if (error) *error = result.error;
    }
    for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
    return result.ok;
}

bool CadViewport::solveActiveDrag(const QVector<ForgeCad::PointTarget> &targets,const SketchObject &before,const QVector<ForgeCad::PointTarget> &anchors) {
    auto &sketch=sketches_[activeSketch_];const auto seed=sketch;auto work=seed;
    auto exact=targets;exact+=anchors;auto result=ForgeCad::solveSketch(work,exact);
    if(!result.ok){work=seed;result=ForgeCad::solveSketch(work,anchors);}
    analysisDirty_=true;
    if(!result.ok){sketch=before;return false;}
    const auto changed=[&]{
        for(int i=0;i<work.segments.size();++i)if(pointDistance(work.segments[i].first,before.segments[i].first)>1e-12||pointDistance(work.segments[i].second,before.segments[i].second)>1e-12)return true;
        for(int i=0;i<work.curves.size();++i){const auto &a=work.curves[i],&b=before.curves[i];for(int k=0;k<a.controlPoints.size();++k)if(pointDistance(a.controlPoints[k],b.controlPoints[k])>1e-12)return true;for(int k=0;k<a.tangentHandles.size();++k)if(pointDistance(a.tangentHandles[k].first,b.tangentHandles[k].first)>1e-12||pointDistance(a.tangentHandles[k].second,b.tangentHandles[k].second)>1e-12)return true;}
        return false;
    };
    if(changed()){
        if(!dragRecorded_){history_.record(dragSnapshot_);dragRecorded_=true;}
        if(sketchMode_)deferredSketches_.insert(activeSketch_);
    }
    sketch=std::move(work);
    for(auto &curve:sketch.curves)ForgeCad::recalculateCurve(curve,tessellationQuality_);
    sceneBoundsDirty_=true;return true;
}

QVector<ForgeCad::PointTarget> CadViewport::targetsAt(const SketchObject &sketch, const QPointF &position) {
    QVector<ForgeCad::PointTarget> targets;
    const double tolerance = ForgeCad::kSketchConnectionTolerance;
    for (int i = 0; i < sketch.segments.size(); ++i) {
        if (pointDistance(sketch.segments.at(i).first, position) <= tolerance) targets.append({{0, i, 0}, position});
        if (pointDistance(sketch.segments.at(i).second, position) <= tolerance) targets.append({{0, i, 1}, position});
    }
    for (int c = 0; c < sketch.curves.size(); ++c)
        for (int k = 0; k < sketch.curves.at(c).controlPoints.size(); ++k)
            if ((sketch.curves.at(c).tool!=DrawingTool::Circle||k==0)&&pointDistance(sketch.curves.at(c).controlPoints.at(k), position) <= tolerance) targets.append({{1, c, k}, position});
    return targets;
}

QVector<ForgeCad::PointTarget> CadViewport::curveTargets(const SketchObject &sketch, int curve) {
    QVector<ForgeCad::PointTarget> targets;
    for (int k = 0; k < sketch.curves.at(curve).controlPoints.size(); ++k) targets.append({{1, curve, k}, sketch.curves.at(curve).controlPoints.at(k)});
    return targets;
}

void CadViewport::syncCurveDimensions(SketchObject &sketch, int curve) {
    for (SketchConstraint &c : sketch.geometricConstraints)
        if ((c.type == ConstraintType::Radius || c.type == ConstraintType::Diameter) && c.first.kind == 1 && c.first.element == curve)
            c.value = ForgeCad::currentMeasure(sketch, c);
}

QString CadViewport::dimensionText(const SketchConstraint &c) {
    auto number = [](double v) {
        QString text = QString::number(v, 'f', 3);
        while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) text.chop(1);
        return text;
    };
    switch (c.type) {
    case ConstraintType::Radius:
    case ConstraintType::AxisRadius: return QStringLiteral("R") + ForgeCad::formatLength(c.value, 3);
    case ConstraintType::Diameter:
    case ConstraintType::AxisDiameter: return QStringLiteral("\u2300") + ForgeCad::formatLength(c.value, 3);
    case ConstraintType::Angle: return number(std::fabs(c.value)) + QStringLiteral("\u00B0");
    default: return ForgeCad::formatLength(c.value, 3);
    }
}

QPolygonF CadViewport::arrowHead(const QPointF &tip, const QPointF &from) {
    const QPointF d = from - tip;
    const double l = pointLength(d);
    if (l <= 0.0) return {};
    const QPointF u = d / l, n(-u.y(), u.x());
    const double length = 9.0, half = 3.0;
    return QPolygonF({tip, tip + length * u + half * n, tip + length * u - half * n});
}

bool CadViewport::dimensionGraphic(const SketchObject &sketch, int index, DimensionGraphic &g) const {
    return dimensionGraphic(sketch, sketch.geometricConstraints.at(index), g);
}

bool CadViewport::dimensionGraphic(const SketchObject &sketch, const SketchConstraint &c, DimensionGraphic &g) const {
    if (!ForgeCad::isDimension(c.type)) return false;
    const double px = double(camera_.zoom_) / double(qMax(1, height()));  // unita' dello schizzo per pixel
    const auto screen = [&](const QPointF &p) { return projectWorldPoint(mapSketchPoint(p, sketch)); };
    const QFontMetricsF metrics(uiFont(9, QFont::DemiBold));
    g.text = dimensionText(c);
    const double textWidth = metrics.horizontalAdvance(g.text), textHeight = metrics.height();
    const auto readable = [](double degrees) {
        while (degrees > 90.0) degrees -= 180.0;
        while (degrees <= -90.0) degrees += 180.0;
        return degrees;
    };
    const auto finishText = [&](const QPointF &center, double degrees) {
        g.textCenter = center;
        g.textAngle = degrees;
        const double a = degrees * M_PI / 180.0;
        const QPointF u(std::cos(a), std::sin(a)), n(-u.y(), u.x());
        const double hw = 0.5 * textWidth + 3.0, hh = 0.5 * textHeight + 1.0;
        g.textBox = QPolygonF({center - hw * u - hh * n, center + hw * u - hh * n, center + hw * u + hh * n, center - hw * u + hh * n});
    };
    const bool projected = c.type == ConstraintType::HorizontalDistance || c.type == ConstraintType::VerticalDistance;
    if (c.type == ConstraintType::Offset || c.type == ConstraintType::Distance || c.type == ConstraintType::AxisRadius || c.type == ConstraintType::AxisDiameter || projected) {
        QPointF p, q;
        if (!ForgeCad::dimensionPoints(sketch, c, p, q)) return false;
        // Quote orizzontali e verticali: la linea di misura e' lungo X (o Y)
        // dello schizzo, le linee di riferimento perpendicolari a lei.
        QPointF u, n;
        double length = 0.0;
        if (projected) {
            const bool horizontal = c.type == ConstraintType::HorizontalDistance;
            const double d = horizontal ? q.x() - p.x() : q.y() - p.y();
            length = std::fabs(d);
            if (length <= 1e-12) return false;
            const double s = d >= 0.0 ? 1.0 : -1.0;
            u = horizontal ? QPointF(s, 0.0) : QPointF(0.0, s);
        } else {
            length = pointDistance(p, q);
            if (length <= 1e-12) return false;
            u = (q - p) / length;
        }
        n = QPointF(-u.y(), u.x());
        const auto along = [](const QPointF &a, const QPointF &b) { return a.x() * b.x() + a.y() * b.y(); };
        // Livello della linea di misura lungo n (dall'origine dello schizzo) e posizione del testo.
        const double lp = along(p, n), lq = along(q, n);
        double level = std::max(lp, lq) + 30.0 * px, slide = 0.5;
        if (!c.placed) {
            // Di default la quota sta fuori: dalla parte opposta al centro dello schizzo.
            QPointF center;
            int count = 0;
            for (const SketchSegment &segment : sketch.segments) center += segment.first + segment.second, count += 2;
            for (const CurveObject &curve : sketch.curves)
                for (const QPointF &point : curve.controlPoints) center += point, ++count;
            if (count > 0) center /= double(count);
            const QPointF away = 0.5 * (p + q) - center;
            if (along(away, n) < 0.0) level = std::min(lp, lq) - 30.0 * px;
        } else {
            level = along(c.placement, n);
            slide = along(c.placement - p, u) / length;
        }
        const QPointF p2 = p + (level - lp) * n, q2 = q + (level - lq) * n;
        // Linee di riferimento: dal punto (con un piccolo stacco) a poco oltre la linea di misura.
        const auto extensionLine = [&](const QPointF &from, const QPointF &to, double gap) {
            const double side = gap >= 0.0 ? 1.0 : -1.0;
            if (std::fabs(gap) > 3.0 * px) g.lines.moveTo(screen(from + side * 3.0 * px * n));
            else g.lines.moveTo(screen(from));
            g.lines.lineTo(screen(to + side * 6.0 * px * n));
        };
        extensionLine(p, p2, level - lp);
        extensionLine(q, q2, level - lq);
        const QPointF sp = screen(p2), sq = screen(q2);
        const QPointF textAlong = p2 + slide * (q2 - p2);
        const double screenLength = pointDistance(sp, sq);
        // Linea di misura con le frecce dentro; se non c'e' spazio le frecce stanno fuori.
        if (screenLength >= 26.0) {
            g.lines.moveTo(sp);
            g.lines.lineTo(sq);
            g.arrows << arrowHead(sp, sq) << arrowHead(sq, sp);
        } else {
            const QPointF out = (sq - sp) / std::max(screenLength, 1e-9);
            g.lines.moveTo(sp - 16.0 * out);
            g.lines.lineTo(sq + 16.0 * out);
            g.arrows << arrowHead(sp, sp - out) << arrowHead(sq, sq + out);
        }
        // Testo fuori dagli estremi: la linea di misura arriva fino a li'.
        if (slide < 0.0 || slide > 1.0) {
            g.lines.moveTo(slide < 0.0 ? sp : sq);
            g.lines.lineTo(screen(textAlong));
        }
        const QPointF direction = sq - sp;
        const double degrees = readable(std::atan2(direction.y(), direction.x()) * 180.0 / M_PI);
        const double a = degrees * M_PI / 180.0;
        const QPointF up(std::sin(a), -std::cos(a));  // perpendicolare al testo, verso l'alto del testo
        finishText(screen(textAlong) + up * (0.5 * textHeight + 2.0), degrees);
        return true;
    }
    if (c.type == ConstraintType::Radius || c.type == ConstraintType::Diameter) {
        QPointF center;
        double radius;
        if (!ForgeCad::circleOf(sketch, c.first, center, radius) || radius <= 0.0) return false;
        double angle = M_PI / 4.0, reach = radius + 26.0 * px;
        if (c.placed && pointDistance(c.placement, center) > 0.0) {
            angle = std::atan2(c.placement.y() - center.y(), c.placement.x() - center.x());
            reach = std::max(pointDistance(c.placement, center), radius + 10.0 * px);
        } else {
            const CurveObject &curve = sketch.curves.at(c.first.element);
            if (curve.tool == DrawingTool::Arc && curve.controlPoints.size() >= 3) {
                const QPointF a0 = curve.controlPoints.at(1) - center, a1 = curve.controlPoints.at(2) - center;
                double from = std::atan2(a0.y(), a0.x()), to = std::atan2(a1.y(), a1.x());
                while (to <= from) to += 2.0 * M_PI;
                angle = 0.5 * (from + to);
            }
        }
        const QPointF u(std::cos(angle), std::sin(angle));
        const QPointF onCircle = center + radius * u, end = center + reach * u;
        if (c.type == ConstraintType::Radius) {
            g.lines.moveTo(screen(center));
            g.lines.lineTo(screen(end));
            g.arrows << arrowHead(screen(onCircle), screen(center));
        } else {
            const QPointF opposite = center - radius * u;
            g.lines.moveTo(screen(opposite));
            g.lines.lineTo(screen(end));
            g.arrows << arrowHead(screen(onCircle), screen(center)) << arrowHead(screen(opposite), screen(center));
        }
        // Testo in orizzontale dopo il richiamo, dalla parte in cui va la linea.
        const QPointF tail = screen(end), inside = screen(center);
        const double sideX = tail.x() >= inside.x() ? 1.0 : -1.0;
        g.lines.lineTo(tail + QPointF(sideX * (textWidth + 6.0), 0.0));
        finishText(tail + QPointF(sideX * (0.5 * textWidth + 3.0), -(0.5 * textHeight + 1.0)), 0.0);
        return true;
    }
    // Angolo: arco tra le due rette attorno al loro punto comune, dalla prima
    // direzione (nel settore della quota) per il valore del vincolo (con segno).
    QPointF p0, p1, q0, q1, vertex, d1, d2;
    if (!ForgeCad::constraintLines(sketch, c, p0, p1, q0, q1) || !ForgeCad::angleDirections(sketch, c, vertex, d1, d2)) return false;
    const double a1 = std::atan2(d1.y(), d1.x()), sweep = c.value * M_PI / 180.0;
    double radius = 40.0 * px;
    if (c.placed && pointDistance(c.placement, vertex) > 0.0) radius = pointDistance(c.placement, vertex);
    const int steps = 48;
    QPointF previous;
    for (int k = 0; k <= steps; ++k) {
        const double a = a1 + sweep * k / steps;
        const QPointF point = screen(vertex + radius * QPointF(std::cos(a), std::sin(a)));
        if (k == 0) g.lines.moveTo(point);
        else g.lines.lineTo(point);
        previous = point;
    }
    const QPointF start = screen(vertex + radius * QPointF(std::cos(a1), std::sin(a1)));
    const QPointF startNext = screen(vertex + radius * QPointF(std::cos(a1 + sweep / steps), std::sin(a1 + sweep / steps)));
    const QPointF endPrev = screen(vertex + radius * QPointF(std::cos(a1 + sweep * (steps - 1) / steps), std::sin(a1 + sweep * (steps - 1) / steps)));
    g.arrows << arrowHead(start, startNext) << arrowHead(previous, endPrev);
    // Linee di riferimento lungo le rette, dove l'arco va oltre i segmenti.
    const auto extension = [&](const QPointF &a, const QPointF &b, double direction) {
        const QPointF u(std::cos(direction), std::sin(direction));
        const double ta = (a - vertex).x() * u.x() + (a - vertex).y() * u.y(), tb = (b - vertex).x() * u.x() + (b - vertex).y() * u.y();
        // Settore dalla parte opposta al segmento: la linea parte dal vertice.
        const double near = std::max(0.0, std::min(ta, tb)), far = std::max(0.0, std::max(ta, tb));
        if (radius > far) {
            g.lines.moveTo(screen(vertex + (far + 3.0 * px) * u));
            g.lines.lineTo(screen(vertex + (radius + 6.0 * px) * u));
        } else if (radius < near) {
            g.lines.moveTo(screen(vertex + (near - 3.0 * px) * u));
            g.lines.lineTo(screen(vertex + (radius - 6.0 * px) * u));
        }
    };
    if (c.first.kind != 2) extension(p0, p1, a1);
    if (c.second.kind != 2) extension(q0, q1, a1 + sweep);
    const double middle = a1 + 0.5 * sweep;
    const QPointF mid = screen(vertex + radius * QPointF(std::cos(middle), std::sin(middle)));
    const QPointF outward = mid - screen(vertex);
    const QPointF shift = pointLength(outward) > 0.0 ? outward / pointLength(outward) : QPointF(0.0, -1.0);
    finishText(mid + shift * (0.5 * std::max(textWidth, textHeight) + 4.0), 0.0);
    return true;
}

int CadViewport::dimensionAt(const QPoint &position) const {
    const SketchObject *sketch = activeSketchObject();
    if (!sketch || !constraintsVisible_) return -1;
    for (int index = sketch->geometricConstraints.size() - 1; index >= 0; --index) {
        DimensionGraphic g;
        if (!dimensionGraphic(*sketch, index, g)) continue;
        if (g.textBox.containsPoint(QPointF(position), Qt::OddEvenFill)) return index;
        QPainterPathStroker stroker;
        stroker.setWidth(8.0);
        if (stroker.createStroke(g.lines).contains(QPointF(position))) return index;
    }
    return -1;
}

void CadViewport::drawDimensions(QPainter &painter) const {
    const SketchObject *sketch = activeSketchObject();
    if (!sketch || !constraintsVisible_) return;
    painter.save();
    painter.setFont(uiFont(9, QFont::DemiBold));
    // Strumento Quota: il punto che si sceglierebbe e la quota in anteprima (arancio).
    if (drawingTool_ == DrawingTool::Dimension) {
        QPointF hovered;
        if (dimensionHover_.kind >= 0 && ForgeCad::refPoint(*sketch, dimensionHover_, hovered)) {
            painter.setPen(QPen(kHoverColor, 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(projectWorldPoint(mapSketchPoint(hovered, *sketch)), 6.0, 6.0);
        }
        for (const SelectedPoint &point : selectedPoints_) {
            if (!isValidSelectedPoint(point)) continue;
            painter.setPen(QPen(kSelectionColor, 2.0));
            painter.setBrush(kSelectionColor);
            painter.drawEllipse(projectWorldPoint(mapSketchPoint(selectedPointPosition(point), *sketch)), 4.0, 4.0);
        }
    }
    const int previewIndex = drawingTool_ == DrawingTool::Dimension && dimensionPreviewValid_ ? int(sketch->geometricConstraints.size()) : -1;
    for (int index = 0; index < sketch->geometricConstraints.size() || index == previewIndex; ++index) {
        DimensionGraphic g;
        const bool preview = index == previewIndex;
        if (preview ? !dimensionGraphic(*sketch, dimensionPreview_, g) : !dimensionGraphic(*sketch, index, g)) continue;
        const bool selected = !preview && selectedConstraints_.contains(index), hovered = index == constraintHover_;
        const bool broken = !preview && ForgeCad::constraintError(*sketch, sketch->geometricConstraints.at(index)) > 1e-7;
        const QColor color = preview ? QColor(255, 170, 70) : selected ? kSelectionColor : hovered ? kHoverColor : broken ? QColor(255, 140, 90) : QColor(185, 215, 240);
        painter.setPen(QPen(color, selected || preview ? 1.6 : 1.1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(g.lines);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        for (const QPolygonF &arrow : g.arrows) painter.drawPolygon(arrow);
        painter.save();
        painter.translate(g.textCenter);
        painter.rotate(g.textAngle);
        const QFontMetricsF metrics(painter.font());
        const QRectF box(-0.5 * metrics.horizontalAdvance(g.text) - 2.0, -0.5 * metrics.height(), metrics.horizontalAdvance(g.text) + 4.0, metrics.height());
        painter.setBrush(QColor(8, 14, 22, 190));
        painter.drawRect(box);
        painter.setPen(color);
        painter.drawText(box, Qt::AlignCenter, g.text);
        painter.restore();
    }
    painter.restore();
}

QVector<CadViewport::ConstraintGlyph> CadViewport::constraintGlyphs() const {
    QVector<ConstraintGlyph> glyphs;
    const SketchObject *sketch = activeSketchObject();
    if (!sketch || !constraintsVisible_) return glyphs;
    const QFontMetricsF metrics(uiFont(8, QFont::DemiBold));
    QHash<QString, int> stacked;  // quante etichette ci sono gia' accanto allo stesso punto
    for (int index = 0; index < sketch->geometricConstraints.size(); ++index) {
        const SketchConstraint &c = sketch->geometricConstraints.at(index);
        DimensionGraphic dimension;
        if (dimensionGraphic(*sketch, index, dimension)) continue;  // quota disegnata come nel disegno tecnico
        QString text = ForgeCad::constraintSymbol(c.type);
        if (c.type == ConstraintType::Pattern) text += QLatin1Char(' ') + ForgeCad::patternSummary(c.pattern);
        else if (c.type == ConstraintType::Angle) text += QStringLiteral(" %1\u00B0").arg(c.value, 0, 'f', 2);
        else if (ForgeCad::isDimension(c.type)) text += QLatin1Char(' ') + ForgeCad::formatLength(c.value, 3);
        for (const ForgeCad::ConstraintAnchor &anchor : ForgeCad::constraintAnchors(*sketch, c)) {
            const QPointF screen = projectWorldPoint(mapSketchPoint(anchor.point, *sketch));
            ConstraintGlyph glyph;
            glyph.constraint = index;
            if (c.type == ConstraintType::Coincident) {
                glyph.dot = true;
                glyph.rect = QRectF(screen - QPointF(4.0, 4.0), QSizeF(8.0, 8.0));
                glyphs.append(glyph);
                continue;
            }
            glyph.text = text;
            const QSizeF size(metrics.horizontalAdvance(text) + 8.0, metrics.height() + 2.0);
            QPointF along(1.0, 0.0), side(0.0, -1.0);
            if (!anchor.onPoint && pointLength(anchor.direction) > 0.0) {
                const QPointF ahead = projectWorldPoint(mapSketchPoint(anchor.point + anchor.direction * (1.0 / pointLength(anchor.direction)), *sketch)) - screen;
                if (pointLength(ahead) > 1e-9) {
                    along = ahead / pointLength(ahead);
                    side = QPointF(along.y(), -along.x());
                    if (side.y() > 0.0) side = -side;  // le etichette sopra le entita'
                }
            }
            const QString key = QStringLiteral("%1,%2").arg(qRound(screen.x() / 4.0)).arg(qRound(screen.y() / 4.0));
            const int k = stacked.value(key, 0);
            stacked[key] = k + 1;
            const QPointF center = anchor.onPoint ? screen + QPointF(12.0 + 0.5 * size.width(), -12.0 - k * (size.height() + 2.0))
                                                  : screen + side * (4.0 + 0.5 * size.height()) + along * (k * (size.width() + 4.0));
            glyph.rect = QRectF(center - QPointF(0.5 * size.width(), 0.5 * size.height()), size);
            glyphs.append(glyph);
        }
    }
    return glyphs;
}

int CadViewport::constraintAt(const QPoint &position) const {
    const int dimension = dimensionAt(position);
    if (dimension >= 0) return dimension;
    const QVector<ConstraintGlyph> glyphs = constraintGlyphs();
    for (int k = glyphs.size() - 1; k >= 0; --k)
        if (glyphs.at(k).rect.adjusted(-2.0, -2.0, 2.0, 2.0).contains(QPointF(position))) return glyphs.at(k).constraint;
    return -1;
}

void CadViewport::drawSketchConstraints(QPainter &painter) const {
    painter.save();
    painter.setFont(uiFont(8, QFont::DemiBold));
    for (const ConstraintGlyph &glyph : constraintGlyphs()) {
        const bool selected = selectedConstraints_.contains(glyph.constraint), hovered = glyph.constraint == constraintHover_;
        const QColor color = selected ? kSelectionColor : hovered ? kHoverColor : QColor(150, 200, 235);
        if (glyph.dot) {
            painter.setPen(QPen(selected || hovered ? color : QColor(20, 30, 40), 1.2));
            painter.setBrush(selected || hovered ? color : QColor(120, 225, 150));
            painter.drawEllipse(glyph.rect.center(), 3.2, 3.2);
            continue;
        }
        painter.setPen(QPen(color, selected ? 1.6 : 1.0));
        painter.setBrush(QColor(12, 20, 30, 215));
        painter.drawRoundedRect(glyph.rect, 3.0, 3.0);
        painter.drawText(glyph.rect, Qt::AlignCenter, glyph.text);
    }
    painter.restore();
}

void CadViewport::highlightConstraintEntities(QPainter &painter, const SketchObject &sketch, int index, const QColor &color) const {
    if (index < 0 || index >= sketch.geometricConstraints.size()) return;
    const SketchConstraint &c = sketch.geometricConstraints.at(index);
    if (c.type == ConstraintType::Pattern) {
        for (const QVector<ConstraintRef> *refs : {&c.pattern.sources, &c.pattern.copies})
            for (const ConstraintRef &ref : *refs)
                if (ref.kind == 0 || ref.kind == 1) highlightSketchElement(painter, sketch, {ref.kind, ref.element}, color);
        return;
    }
    for (const ConstraintRef &ref : {c.first, c.second, c.third}) {
        if (ref.kind < 0) continue;
        QPointF point;
        if (ForgeCad::refPoint(sketch, ref, point)) {
            painter.setPen(QPen(color, 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(projectWorldPoint(mapSketchPoint(point, sketch)), 6.0, 6.0);
        } else if (ref.kind == 0 || ref.kind == 1) {
            highlightSketchElement(painter, sketch, {ref.kind, ref.element}, color);
        }
    }
}

void CadViewport::recordUndo() { insertionSourceSize_ = extrusions_.size(); history_.record(documentState()); }

void CadViewport::documentChanged() {

    for (ExtrusionObject &feature : extrusions_)
        if (historyPosition_ >= 0 || (consumedBodies_.contains(feature.modelBodyId) && !feature.visible))
            for (const ModelBody &body : modelBodies_)
                if (feature.modelBodyId == body.id && feature.featureId == body.tipFeatureId) feature.visible = body.visible;
    // Creation paths append their results. Insert that entire batch at the
    // rollback cursor, preserving persistent IDs and remapping vector indices.
    if (historyPosition_ >= 0 && insertionSourceSize_ >= 0 && extrusions_.size() > insertionSourceSize_) {
        const int start = qMin(historyPosition_, insertionSourceSize_);
        const int added = extrusions_.size() - insertionSourceSize_;
        ForgeCad::normalizeModelHistory(extrusions_, modelBodies_);
        QVector<int> order;
        for (int i = 0; i < start; ++i) order.append(i);
        for (int i = insertionSourceSize_; i < extrusions_.size(); ++i) order.append(i);
        for (int i = start; i < insertionSourceSize_; ++i) order.append(i);
        QVector<int> map(extrusions_.size(), -1);
        QVector<ExtrusionObject> reordered;
        for (int old : order) { map[old] = reordered.size(); reordered.append(extrusions_.at(old)); }
        const auto mapped = [&](int old) { return old >= 0 ? map.value(old, -1) : old; };
        const auto remapRef = [&](GeometryRef &ref) {
            if (ForgeCad::isBodyRef(ref)) ref.index = mapped(ref.index);
        };
        for (ExtrusionObject &feature : reordered) {
            feature.firstBody = mapped(feature.firstBody);
            feature.secondBody = mapped(feature.secondBody);
            for (int &tool : feature.booleanTools) tool = mapped(tool);
            for (int &merged : feature.mergeBodies) merged = mapped(merged);
            for (GeometryRef &ref : feature.datum.refs) remapRef(ref);
            for (GeometryRef &ref : feature.pattern.refs) remapRef(ref);
            for (QVector<GeometryRef> *refs : {&feature.ruledFirst, &feature.ruledSecond, &feature.planarRefs, &feature.loftStartFaces, &feature.loftEndFaces, &feature.loftSections, &feature.loftEdgeGuides})
                for (GeometryRef &ref : *refs) remapRef(ref);
            remapRef(feature.extentRef);
            remapRef(feature.move.axis);
            remapRef(feature.revolveAxisRef);
            remapRef(feature.draftNeutral);
            remapRef(feature.thickenDirection);
            remapRef(feature.sweepProfileRef); remapRef(feature.sweepPathRef);
            remapRef(feature.curveExtraction.source);
            remapRef(feature.curveExtraction.tool);
        }
        QSet<quint64> changedBodies;
        for (int i = start; i < start + added; ++i) changedBodies.insert(reordered.at(i).modelBodyId);
        for (quint64 bodyId : changedBodies) {
            if (!bodyId) continue;
            int previous = -1;
            for (int current = 0; current < reordered.size(); ++current) {
                ExtrusionObject &feature = reordered[current];
                if (feature.modelBodyId != bodyId) continue;
                if (previous >= 0) {
                    if (feature.operation >= 0 || feature.feature == BodyFeature::Blend || feature.feature == BodyFeature::SheetTrim
                        || feature.feature == BodyFeature::SheetExtend || feature.feature == BodyFeature::Scale
                        || (feature.feature == BodyFeature::Transform && !feature.move.copy) || feature.feature == BodyFeature::Pattern
                        || feature.feature == BodyFeature::Sew || feature.feature == BodyFeature::DeleteFace
                        || feature.feature == BodyFeature::Draft || feature.feature == BodyFeature::Shell || feature.feature == BodyFeature::Thread
                        || (feature.feature == BodyFeature::Thicken && feature.offsetFaces.isEmpty())) {
                        feature.firstBody = previous;
                    } else if (ForgeCad::mergingFeature(feature) && feature.mergeOperation != 0) {
                        bool replaced = false;
                        for (int &merged : feature.mergeBodies)
                            if (merged >= 0 && reordered.at(merged).modelBodyId == bodyId) { merged = previous; replaced = true; break; }
                        if (!replaced) feature.mergeBodies.prepend(previous);
                    }
                }
                previous = current;
            }
        }
        for (SketchObject &sketch : sketches_)
            if (sketch.datumPlane >= 0) sketch.datumPlane = mapped(sketch.datumPlane);
        if (selection_.kind == SceneObjectKind::Extrusion) selection_.index = mapped(selection_.index);
        extrusions_ = std::move(reordered);
        historyPosition_ = start + added;
        regenerateAll();
    }
    insertionSourceSize_ = -1;
    if (historyPosition_ > extrusions_.size()) historyPosition_ = extrusions_.size();
    ForgeCad::normalizeModelHistory(extrusions_, modelBodies_);
    applyHistoryPosition();
    sceneBoundsDirty_ = true;
    raySelectionCache_.clear();
    projectedEdges_.clear();
    if (sectionRefresh_) sectionRefresh_();
    analysisDirty_ = true;
    if (constraintPanelCallback_) constraintPanelCallback_();
    selectedFace_ = {};  // la geometria (e la numerazione delle facce) puo' essere cambiata
    selectedObjects_.clear();  // gli indici possono essere cambiati
    clearFeatureHighlights();
    warmPickCaches();
    if (documentChangedCallback_) documentChangedCallback_();
    update();
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

void CadViewport::sketchEdited() {
    // I vincoli dello schizzo restano soddisfatti (se l'ultima modifica li
    // ha rotti il risolutore li rimette a posto, se puo').
    if (activeSketch_ >= 0 && activeSketch_ < sketches_.size() && !sketches_.at(activeSketch_).geometricConstraints.isEmpty()) {
        const SketchObject before = sketches_.at(activeSketch_);
        QString failure;
        if (!solveActive({}, before, &failure)) showStatus(failure);
    }
    // I vincoli selezionati possono non esserci piu'.
    const int count = activeSketch_ >= 0 && activeSketch_ < sketches_.size() ? sketches_.at(activeSketch_).geometricConstraints.size() : 0;
    for (int k = selectedConstraints_.size() - 1; k >= 0; --k)
        if (selectedConstraints_.at(k) >= count) selectedConstraints_.removeAt(k);
    if (constraintHover_ >= count) constraintHover_ = -1;
    if(sketchMode_&&activeSketch_>=0)deferredSketches_.insert(activeSketch_);
    else regenerateDependents(activeSketch_);
    documentChanged();
}

void CadViewport::regenerateDependents(int sketchIndex) {
    if (sketchIndex < 0 || sketchIndex >= sketches_.size()) return;
    regenerateFrom(0, QVector<bool>(extrusions_.size(), false), {sketchIndex});
}

bool CadViewport::hasSurfacePierceSource(int index) const {
    for (const auto &sketch : sketches_) for (const auto &c : sketch.geometricConstraints)
        if (c.type==ConstraintType::Pierce && c.pierce && pierceSourceIndex(c.pierce->surface)==index) return true;
    return false;
}

void CadViewport::refreshSurfacePierceSketches(int sourceIndex,QSet<int> &changed,int targetSketch) {
    for (int index=0;index<sketches_.size();++index) {
        if (targetSketch>=0 && index!=targetSketch) continue;
        SketchObject work=sketches_.at(index);
        bool touched=false;
        for (auto &c : work.geometricConstraints) {
            if (c.type!=ConstraintType::Pierce || !c.pierce) continue;
            const int owner=pierceSourceIndex(c.pierce->surface);
            if (sourceIndex>=0 && owner!=sourceIndex) continue;
            if (sourceIndex==-2 && owner>=0) continue;
            touched=true;
            if (owner<0 || !extrusions_.at(owner).forgeBody) {
                auto data=std::make_shared<SketchPierceData>(*c.pierce);
                data->segments.clear(); data->curves.clear();
                data->error=QStringLiteral("La superficie del vincolo Pierce non e' disponibile.");
                c.pierce=data;
                continue;
            }
            GeometryRef ref=c.pierce->surface; ref.index=owner;
            ForgeCad::refreshSurfacePierce(work,c,*extrusions_.at(owner).forgeBody,ref,extrusions_.at(owner).name);
        }
        if (!touched) continue;
        const auto solved=ForgeCad::solveSketch(work);
        if (!solved.ok) showStatus(solved.error);
        sketches_[index]=std::move(work);
        changed.insert(index);
        analysisDirty_=true;
    }
}

void CadViewport::regenerateFrom(int from, QVector<bool> dirty, QSet<int> dirtySketches, bool all ,
                        const std::function<void(int, int, const QString &)> &progress ) {
    dirty.resize(extrusions_.size());
    refreshSurfacePierceSketches(-2,dirtySketches);
    const QSet<int> targets=dirtySketches;
    for (int sketch : targets) refreshSurfacePierceSketches(-1,dirtySketches,sketch);
    int completed = 0;
    const int total = all ? qMax(1, int(extrusions_.size())) : 0;
    const auto advanced = [&](int count, const QString &name) {
        completed += count;
        if (progress) progress(completed, total, name);
    };
    QVector<int> pending;  // corpi da rifare fino al prossimo piano di costruzione
    const auto flush = [&] {
        rebuildParallel(pending, progress ? advanced : std::function<void(int, const QString &)>());
        pending.clear();
    };
    for (int index = qMax(0, from); index < extrusions_.size(); ++index) {
        const ExtrusionObject &body = extrusions_.at(index);
        bool depends = all;
        for (int sketch : ForgeCad::sketchesOf(body)) depends = depends || dirtySketches.contains(sketch);
        for (int operand : ForgeCad::bodyOperands(body)) depends = depends || (operand >= 0 && operand < index && dirty.at(operand));
        if (!depends) continue;
        dirty[index] = true;
        if (!ForgeCad::isDatumBody(body)) {
            pending.append(index);
            if (hasSurfacePierceSource(index)) {
                flush();
                refreshSurfacePierceSketches(index,dirtySketches);
            }
            continue;
        }
        flush();
        rebuildBody(extrusions_[index], index);
        advanced(1, extrusions_.at(index).name);
        followDatum(index, dirtySketches);
        const QSet<int> moved=dirtySketches;
        for (int s : moved) if (s>=0 && s<sketches_.size() && sketches_.at(s).datumPlane==index)
            refreshSurfacePierceSketches(-1,dirtySketches,s);
    }
    flush();
}

void CadViewport::rebuildParallel(const QVector<int> &indices,
                         const std::function<void(int, const QString &)> &advanced ) {
    if (indices.isEmpty()) return;
    DeferredWork work(this, indices.size() > 1 ? QStringLiteral("Rigenerazione di %1 corpi...").arg(indices.size())
                                               : QStringLiteral("Rigenerazione del corpo..."));
    if (indices.size() <= 1) {
        for (int index : indices) {
            rebuildBody(extrusions_[index], index);
            if (advanced) advanced(1, extrusions_.at(index).name);
        }
        return;
    }
    QHash<int, int> level;
    int levels = 0;
    for (int index : indices) {
        int l = 0;
        for (int operand : ForgeCad::bodyOperands(extrusions_.at(index)))
            if (level.contains(operand)) l = qMax(l, level.value(operand) + 1);
        level.insert(index, l);
        levels = qMax(levels, l + 1);
    }
    // Copia di lavoro: i thread scrivono qui (separata qui, prima dei
    // thread), la vista intanto disegna i corpi di prima; alla fine i corpi
    // rifatti tornano nel documento.
    QVector<ExtrusionObject> next = extrusions_;
    ExtrusionObject *bodies = next.data();
    const QVector<SketchObject> sketches = sketches_;
    const QVector<ExtrusionObject> &all = next;
    const int quality = tessellationQuality_;
    for (int l = 0; l < levels; ++l) {
        std::vector<int> batch;
        for (int index : indices)
            if (level.value(index) == l) batch.push_back(index);
        const std::size_t wave = advanced ? std::max<std::size_t>(1, ForgeCad::Kernel::threadCount(0)) : batch.size();
        for (std::size_t offset = 0; offset < batch.size(); offset += wave) {
            const std::size_t count = std::min(wave, batch.size() - offset);
            runWhileResponsive([&] {
                ForgeCad::Kernel::parallelFor(count, ForgeCad::Kernel::threadCount(0), [&](std::size_t k) {
                    const int index = batch[offset + k];
                    ForgeCad::buildBody(bodies[index], index, sketches, all, quality);
                });
            });
            if (advanced) advanced(int(count), next.at(batch[offset + count - 1]).name);
        }
    }
    for (int index : indices) extrusions_[index] = std::move(next[index]);
}

void CadViewport::followDatum(int index, QSet<int> &changed) {
    if (index < 0 || index >= extrusions_.size() || !extrusions_.at(index).datumValid) return;
    const SketchFrame &datum = extrusions_.at(index).datumFrame;
    for (int s = 0; s < sketches_.size(); ++s) {
        SketchObject &sketch = sketches_[s];
        if (sketch.datumPlane != index || sketch.plane != kFacePlane) continue;
        const SketchFrame before = sketch.frame;
        SketchFrame frame = datumSketchFrame(datum, &before);
        bool same = true;
        for (int k = 0; k < 3; ++k)
            same = same && frame.origin[k] == before.origin[k] && frame.xAxis[k] == before.xAxis[k] && frame.normal[k] == before.normal[k];
        if (same) continue;
        sketch.frame = frame;
        changed.insert(s);
    }
}

SketchFrame CadViewport::datumSketchFrame(const SketchFrame &datum, const SketchFrame *previous) {
    SketchFrame frame;
    double n[3], c[3];
    for (int k = 0; k < 3; ++k) n[k] = datum.normal[k], c[k] = datum.origin[k];
    const auto dot3 = [](const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    double x[3] = {datum.xAxis[0], datum.xAxis[1], datum.xAxis[2]};
    double o[3] = {0.0, 0.0, 0.0};
    if (previous) {
        const double along = dot3(previous->xAxis, n);
        double projected[3], length = 0.0;
        for (int k = 0; k < 3; ++k) projected[k] = previous->xAxis[k] - along * n[k], length += projected[k] * projected[k];
        length = std::sqrt(length);
        if (length > 1e-3)
            for (int k = 0; k < 3; ++k) x[k] = projected[k] / length;
        for (int k = 0; k < 3; ++k) o[k] = previous->origin[k];
    }
    // Origine: il punto di partenza (origine precedente o del modello) proiettato sul piano.
    double offset[3];
    for (int k = 0; k < 3; ++k) offset[k] = o[k] - c[k];
    const double h = dot3(offset, n);
    for (int k = 0; k < 3; ++k) {
        frame.origin[k] = o[k] - h * n[k];
        frame.xAxis[k] = x[k];
        frame.normal[k] = n[k];
    }
    return frame;
}

void CadViewport::removeBodies(const QSet<int> &removed) {
    // La rigenerazione puo' ridisegnare la vista prima di documentChanged().
    // Gli indici della scelta spigoli appartengono ancora alla vecchia lista:
    // annullarla prima di eliminare o rinumerare i corpi, anche se la base
    // selezionata sopravvive con un indice diverso.
    if (!removed.isEmpty() && edgePicking_) cancelEdgePick();
    QVector<int> map(extrusions_.size(), -1);
    QVector<ExtrusionObject> kept;
    for (int index = 0; index < extrusions_.size(); ++index) {
        if (removed.contains(index)) {
            const ExtrusionObject &body = extrusions_.at(index);
            QVector<int> hidden{body.firstBody, body.secondBody};
            hidden += body.booleanTools;
            hidden += ForgeCad::hiddenOperands(body);
            for (int operand : hidden)
                if (operand >= 0 && operand < extrusions_.size() && !removed.contains(operand)) extrusions_[operand].visible = true;
            continue;
        }
        map[index] = kept.size();
        kept.append(extrusions_.at(index));
    }
    for (ExtrusionObject &body : kept) {
        if (body.firstBody >= 0) body.firstBody = map.value(body.firstBody, -1);
        if (body.secondBody >= 0) body.secondBody = map.value(body.secondBody, -1);
        for (QVector<GeometryRef> *refs : {&body.datum.refs, &body.pattern.refs, &body.ruledFirst, &body.ruledSecond, &body.planarRefs, &body.loftStartFaces, &body.loftEndFaces, &body.loftSections, &body.loftEdgeGuides})
            for (GeometryRef &ref : *refs)
                if (ForgeCad::isBodyRef(ref)) ref.index = map.value(ref.index, -1);
        if (ForgeCad::isBodyRef(body.extentRef)) body.extentRef.index = map.value(body.extentRef.index, -1);
        if (ForgeCad::isBodyRef(body.move.axis)) body.move.axis.index = map.value(body.move.axis.index, -1);
        if (ForgeCad::isBodyRef(body.revolveAxisRef)) body.revolveAxisRef.index = map.value(body.revolveAxisRef.index, -1);
        if (ForgeCad::isBodyRef(body.draftNeutral)) body.draftNeutral.index = map.value(body.draftNeutral.index, -1);
        for (GeometryRef *ref : {&body.sweepProfileRef, &body.sweepPathRef})
            if (ForgeCad::isBodyRef(*ref)) ref->index = map.value(ref->index, -1);
        if (ForgeCad::isBodyRef(body.thickenDirection)) body.thickenDirection.index = map.value(body.thickenDirection.index, -1);
        if (ForgeCad::isBodyRef(body.curveExtraction.source)) body.curveExtraction.source.index = map.value(body.curveExtraction.source.index, -1);
        if (ForgeCad::isBodyRef(body.curveExtraction.tool)) body.curveExtraction.tool.index = map.value(body.curveExtraction.tool.index, -1);
        // Strumenti e corpi fusi eliminati: escono dall'elenco.
        for (QVector<int> *list : {&body.booleanTools, &body.mergeBodies}) {
            QVector<int> kept2;
            for (int other : *list)
                if (map.value(other, -1) >= 0) kept2.append(map.value(other, -1));
            *list = kept2;
        }
    }
    // Gli schizzi su un piano di costruzione eliminato restano dove sono (piano fisso).
    for (SketchObject &sketch : sketches_)
        if (sketch.datumPlane >= 0) sketch.datumPlane = map.value(sketch.datumPlane, -1);
    extrusions_ = std::move(kept);
    QSet<int> changed;
    refreshSurfacePierceSketches(-1,changed);
    if (!changed.isEmpty()) regenerateFrom(0,QVector<bool>(extrusions_.size(),false),changed);
}

void CadViewport::deleteSketchElements() {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size() || sketchSelections_.isEmpty()) return;
    recordUndo();
    QSet<int> segments, curves;
    for (const SketchElementSelection &element : sketchSelections_) (element.kind == 0 ? segments : curves).insert(element.index);
    const int oldCurves = sketches_.at(activeSketch_).curves.size();
    const QVector<int> segmentMap = ForgeCad::removeSketchEntities(sketches_[activeSketch_], segments, curves);
    QVector<int> curveMap(oldCurves, -1);
    int nextCurve = 0;
    for (int old = 0; old < oldCurves; ++old) if (!curves.contains(old)) curveMap[old] = nextCurve++;
    remapSketchReferences(activeSketch_, segmentMap, curveMap);
    const auto remapList = [](QVector<int> &values, const QVector<int> &map) {
        QVector<int> kept;
        for (int value : values) if (map.value(value, -1) >= 0) kept.append(map.at(value));
        values = std::move(kept);
    };
    for (ExtrusionObject &body : extrusions_) {
        if (body.pathSketch == activeSketch_) {
            remapList(body.pathSegments, segmentMap); remapList(body.pathCurves, curveMap);
        }
        for (SketchPathRef &path : body.loftGuidePaths)
            if (path.sketch == activeSketch_) { remapList(path.segments, segmentMap); remapList(path.curves, curveMap); }
    }
    sketchSelections_.clear();
    selectedPoints_.clear();
    selectedConstraints_.clear();
    constraintHover_ = -1;
    sketchHover_ = {};
    sketchEdited();
}

void CadViewport::moveSketchPoint(SketchObject &sketch,int edited,const QPointF &from,const QPointF &delta,const QPointF &fixed) {
    moveSketchPoints(sketch,edited,{from},delta,fixed);
}

void CadViewport::moveSketchPoints(SketchObject &sketch, int edited, const QVector<QPointF> &origins, const QPointF &delta, const QPointF &fixed) {
    const double tolerance = ForgeCad::kSketchConnectionTolerance;
    struct Move { QPointF from, delta; };
    QVector<Move> pending;for(const auto &from:origins)pending.append({from,delta});
    QVector<QPointF> done;
    QVector<QPair<QPointF *, QPointF>> updates;
    QSet<int> movedCurves;
    for (int guard = 0; guard < 1000 && !pending.isEmpty(); ++guard) {
        const Move move = pending.takeFirst();
        if (pointLength(move.delta) <= 1e-15) continue;
        if (pointDistance(move.from, fixed) <= tolerance) continue;
        bool seen = false;
        for (const QPointF &p : done) seen = seen || pointDistance(p, move.from) <= tolerance;
        if (seen) continue;
        done.append(move.from);
        for (int index = 0; index < sketch.segments.size(); ++index) {
            SketchSegment &segment = sketch.segments[index];
            for (int end = 0; end < 2; ++end) {
                QPointF &point = end == 0 ? segment.first : segment.second;
                if (pointDistance(point, move.from) > tolerance) continue;
                updates.append({&point, point + move.delta});
                if (index == edited) continue;
                const QPointF other = end == 0 ? segment.second : segment.first;
                const int constraint = index < sketch.constraints.size() ? sketch.constraints.at(index) : -1;
                if (constraint == 1 && std::abs(move.delta.y()) > 1e-15) pending.append({other, QPointF(0.0, move.delta.y())});
                if (constraint == 2 && std::abs(move.delta.x()) > 1e-15) pending.append({other, QPointF(move.delta.x(), 0.0)});
            }
        }
        for (int curve = 0; curve < sketch.curves.size(); ++curve)
            for (int control = 0; control < sketch.curves[curve].controlPoints.size(); ++control) {
                // Le curve derivate sono rigide: chi vi e' collegato si adatta a loro.
                if (sketch.curves.at(curve).tool == DrawingTool::Converted) break;
                if(sketch.curves.at(curve).tool==DrawingTool::Circle&&control>0)continue;
                QPointF &point = sketch.curves[curve].controlPoints[control];
                if (pointDistance(point, move.from) <= tolerance) {
                    updates.append({&point, point + move.delta});
                    CurveObject &object = sketch.curves[curve];
                    if(object.tool==DrawingTool::Circle&&control==0&&object.controlPoints.size()>1)updates.append({&object.controlPoints[1],object.controlPoints[1]+move.delta});
                    if (object.tool == DrawingTool::Spline && object.tangentHandles.size() == object.controlPoints.size()) {
                        updates.append({&object.tangentHandles[control].first, object.tangentHandles.at(control).first + move.delta});
                        updates.append({&object.tangentHandles[control].second, object.tangentHandles.at(control).second + move.delta});
                    }
                    movedCurves.insert(curve);
                }
            }
    }
    for (const auto &[point, value] : updates) *point = value;
    (void)movedCurves;
}

void CadViewport::regenerateAfter(int changed) {
    QVector<bool> dirty(extrusions_.size(), false);
    QSet<int> sketches;
    if (changed >= 0 && changed < dirty.size()) {
        dirty[changed] = true;
        if (ForgeCad::isDatumBody(extrusions_.at(changed))) { followDatum(changed, sketches); refreshSurfacePierceSketches(-1,sketches); }
        else refreshSurfacePierceSketches(changed,sketches);
    }
    regenerateFrom(changed + 1, dirty, sketches);
}

void CadViewport::regenerateAll(const std::function<void(int, int, const QString &)> &progress ) {
    regenerateFrom(0, {}, {}, true, progress);
}

bool CadViewport::localMergePreview(const ExtrusionObject &body) {
    return body.operation < 0 && (body.feature == BodyFeature::Extrusion || body.feature == BodyFeature::Revolution);
}

void CadViewport::rebuildBody(ExtrusionObject &body, int index) {
    DeferredWork work(this, body.name.isEmpty() ? QStringLiteral("Calcolo della geometria...")
                                                 : QStringLiteral("Calcolo di %1...").arg(body.name));
    ExtrusionObject result = body;
    const QVector<SketchObject> sketches = sketches_;
    const QVector<ExtrusionObject> bodies = extrusions_;
    const int quality = tessellationQuality_;
    runWhileResponsive([&] { ForgeCad::buildBody(result, index, sketches, bodies, quality); });
    body = std::move(result);
}

void CadViewport::runWhileResponsive(const std::function<void()> &task) {
    if (!qApp || QThread::currentThread() != qApp->thread() || !window() || !window()->isVisible()) {
        task();
        return;
    }
    ++responsiveWork_;
    QEventLoop loop;
    // La finestra di avanzamento compare solo se il calcolo dura: i
    // ricalcoli brevi (rilascio di un trascinamento) non lampeggiano.
    QTimer reveal;
    reveal.setSingleShot(true);
    QObject::connect(&reveal, &QTimer::timeout, &loop, [this] {
        for (DeferredWork *work = deferredWork_; work; work = work->outer)
            if (!work->shown && workCallback_) {
                work->shown = true;
                workCallback_(true, work->message, false);
                break;
            }
    });
    if (deferredWork_ && !deferredWork_->shown) reveal.start(kDeferredWorkDelayMs);
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            task();
        } catch (...) {
            failure = std::current_exception();
        }
        QMetaObject::invokeMethod(&loop, "quit", Qt::QueuedConnection);
    });
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    reveal.stop();
    worker.join();
    --responsiveWork_;
    if (failure) std::rethrow_exception(failure);
}

void CadViewport::tessellateBody(ExtrusionObject &body, int index ) const {
    ForgeCad::tessellateGeometry(body, tessellationQuality_, body.display);
    if (index >= 0) ForgeCad::configurePatternInstances(body, index, sketches_, extrusions_);
}

void CadViewport::restoreDocument(DocumentState state) {
    deferredSketches_.clear();for(int index:state.pendingSketchChanges)if(index>=0&&index<state.sketches.size())deferredSketches_.insert(index);
    adoptDocumentIdentity(state.version, state.baseCheckinId);
    dimensionPlacing_ = -1;
    ForgeCad::normalizeModelHistory(state);
    sketches_ = std::move(state.sketches);
    historyPosition_ = -1;
    insertionSourceSize_ = -1;
    extrusions_ = std::move(state.extrusions);
    modelBodies_ = std::move(state.modelBodies);
    for (SketchObject &sketch : sketches_) {
        for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
    }
    for (int index = 0; index < extrusions_.size(); ++index)
        if (extrusions_.at(index).display.quality != tessellationQuality_) tessellateBody(extrusions_[index], index);
    hasPendingPoint_ = false;
    curveControlPoints_.clear();
    sketchSelections_.clear();
    selectedPoints_.clear();
    selectedConstraints_.clear();
    constraintHover_ = -1;
    sketchHover_ = {};
    hover_ = {};
    draggingControlPoint_ = false;
    circleRadiusDrag_=-1;
    dragRecorded_ = false;
    if (edgePicking_) cancelEdgePick();
    clearBlendPreview();
    if (selection_.kind == SceneObjectKind::Sketch || selection_.kind == SceneObjectKind::Extrusion)
        selection_ = {};
    if (activeSketch_ >= sketches_.size()) {
        activeSketch_ = -1;
        if (sketchMode_) endSketchMode();
    }
    if(!sketchMode_&&!deferredSketches_.isEmpty()){
        const auto changed=deferredSketches_;deferredSketches_.clear();
        regenerateFrom(0,QVector<bool>(extrusions_.size(),false),changed);
    }
    documentChanged();
}

void CadViewport::drawBackgroundGradient() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    const float aspect = float(width()) / float(qMax(1, height()));
    QMatrix4x4 projection;
    projection.ortho(-aspect, aspect, -1.0f, 1.0f, -1.0f, 1.0f);
    overlayRenderer_.setMatrices(projection, QMatrix4x4());
    overlayRenderer_.setClipPlane(QVector4D(), false);
    const QColor start = background_.startColor;
    const QColor end = background_.gradient ? background_.endColor : start;
    const float radians = background_.angle * float(M_PI) / 180.0f;
    const QVector2D direction(std::cos(radians), std::sin(radians));
    const QVector2D across(-direction.y(), direction.x());
    const float extent = qAbs(direction.x()) * aspect + qAbs(direction.y());
    const float middle = -extent + 2.0f * extent * qBound(0.0f, background_.position, 1.0f);
    const float span = 2.0f * (aspect + 1.0f);
    const struct { float offset; float mix; } stops[] = {
        {-extent, 0.0f}, {middle, 0.5f}, {extent, 1.0f}};
    QVector<ForgeCad::OverlayVertex> vertices;
    vertices.reserve(6);
    for (const auto &stop : stops) {
        const QVector4D color(float(start.redF() + (end.redF() - start.redF()) * stop.mix),
                              float(start.greenF() + (end.greenF() - start.greenF()) * stop.mix),
                              float(start.blueF() + (end.blueF() - start.blueF()) * stop.mix), 1.0f);
        const QVector2D centre = direction * stop.offset;
        const QVector2D first = centre + across * span;
        const QVector2D second = centre - across * span;
        vertices.push_back({QVector3D(first.x(), first.y(), 0.0f), color});
        vertices.push_back({QVector3D(second.x(), second.y(), 0.0f), color});
    }
    overlayRenderer_.draw(GL_TRIANGLE_STRIP, vertices);
    glEnable(GL_DEPTH_TEST);
}

double CadViewport::pickTolerance(double pixels ) const {
    return pixels * 8.0 * double(camera_.zoom_) / 8.0 / double(qMax(1, height()));
}

QVector<QPointF> CadViewport::curveEditMarkers(const CurveObject &curve,bool hover) {
    if(curve.tool!=DrawingTool::Circle)return curve.controlPoints;
    if(curve.controlPoints.isEmpty())return {};
    const auto center=curve.controlPoints[0];QVector<QPointF> result{center};
    if(hover&&curve.controlPoints.size()>1){const double radius=pointDistance(center,curve.controlPoints[1]);result<<center+QPointF(radius,0)<<center+QPointF(0,radius)<<center-QPointF(radius,0)<<center-QPointF(0,radius);}
    return result;
}

bool CadViewport::closedSpline(const CurveObject &curve) {
    return curve.tool==DrawingTool::Spline&&curve.controlPoints.size()>2&&pointDistance(curve.controlPoints.first(),curve.controlPoints.last())<=ForgeCad::kSketchConnectionTolerance;
}

void CadViewport::moveSplineHandle(CurveObject &curve,int control,int side,QPointF position) {
    const auto set=[&](int k,int s,QPointF p){
        auto &pair=curve.tangentHandles[k];auto &selected=s?pair.second:pair.first;auto &other=s?pair.first:pair.second;const auto center=curve.controlPoints[k];
        const double otherLength=pointDistance(other,center);selected=p;
        const auto d=p-center;const double length=pointLength(d);
        if(curve.tangentLinked.value(k)&&length>1e-15)other=center-d*(otherLength/length);
    };
    const int last=int(curve.controlPoints.size())-1;
    const bool seam=closedSpline(curve)&&((control==0&&side==1)||(control==last&&side==0))&&curve.tangentLinked.value(0)&&curve.tangentLinked.value(last);
    const int partner=control==0?last:0,partnerSide=1-side;
    const double otherLength=seam?pointDistance(partnerSide?curve.tangentHandles[partner].second:curve.tangentHandles[partner].first,curve.controlPoints[partner]):0;
    set(control,side,position);
    const auto d=position-curve.controlPoints[control];const double length=pointLength(d);
    if(seam&&length>1e-15)set(partner,partnerSide,curve.controlPoints[partner]-d*(otherLength/length));
}

bool CadViewport::visibleSplineHandle(const CurveObject &curve, int control, int side) {
    const int last = int(curve.controlPoints.size()) - 1;
    const bool closed = curve.tool == DrawingTool::Spline && last > 1
        && pointDistance(curve.controlPoints.first(),curve.controlPoints.last()) <= ForgeCad::kSketchConnectionTolerance;
    // Alla chiusura partecipano solo l'uscente del primo punto e
    // l'entrante dell'ultimo: le altre due non influenzano la curva.
    return !closed || !((control == 0 && side == 0) || (control == last && side == 1));
}

bool CadViewport::findCurveEditPoint(const QPointF &point, int &curveIndex,
                            int &controlIndex, EditablePointKind &pointKind) const {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return false;
    double nearestDistance = pickTolerance();
    int nearestCurve = -1;
    int nearestControl = -1;
    EditablePointKind nearestKind = EditablePointKind::Control;
    const QVector<CurveObject> &curves = sketches_.at(activeSketch_).curves;
    for (int curve = 0; curve < curves.size(); ++curve) {
        const CurveObject &candidate = curves.at(curve);
        if (candidate.tool == DrawingTool::Converted) continue;
        const bool hasHandles = candidate.tangentHandles.size() == candidate.controlPoints.size();
        for (int control = 0; control < candidate.controlPoints.size(); ++control) {
            if(!isPickableCurvePoint(candidate,control))continue;
            const QPointF candidates[] = {
                candidate.controlPoints.at(control),
                hasHandles ? candidate.tangentHandles.at(control).first : candidate.controlPoints.at(control),
                hasHandles ? candidate.tangentHandles.at(control).second : candidate.controlPoints.at(control)};
            for (int kind = 0; kind < (hasHandles ? 3 : 1); ++kind) {
                if (kind > 0 && !visibleSplineHandle(candidate,control,kind-1)) continue;
                const double distance = pointDistance(point, candidates[kind]);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearestCurve = curve;
                    nearestControl = control;
                    nearestKind = static_cast<EditablePointKind>(kind);
                }
            }
        }
    }
    if (nearestCurve < 0) return false;
    curveIndex = nearestCurve;
    controlIndex = nearestControl;
    pointKind = nearestKind;
    return true;
}

double CadViewport::distanceToSegment(const QPointF &point, const QPointF &first, const QPointF &second) {
    const QPointF line = second - first;
    const double lengthSquared = QPointF::dotProduct(line, line);
    const double parameter = lengthSquared > 0.0
        ? qBound(0.0, QPointF::dotProduct(point - first, line) / lengthSquared, 1.0) : 0.0;
    return pointDistance(point, first + line * parameter);
}

int CadViewport::findNearestFreeCurve(const QPointF &point) const {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return -1;
    double nearestDistance = pickTolerance(12.0);
    int nearest = -1;
    const QVector<CurveObject> &curves = sketches_.at(activeSketch_).curves;
    for (int curve = 0; curve < curves.size(); ++curve) {
        if (curves.at(curve).tool != DrawingTool::Spline && curves.at(curve).tool != DrawingTool::Nurbs) continue;
        const auto &samples = curves.at(curve).samples;
        for (int sample = 1; sample < samples.size(); ++sample) {
            const double distance = distanceToSegment(point, samples.at(sample - 1), samples.at(sample));
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = curve;
            }
        }
    }
    return nearest;
}

bool CadViewport::canChangeControlPointCount(const CurveObject &curve) {
    return curve.tool != DrawingTool::Nurbs || curve.knots.isEmpty();
}

void CadViewport::addControlPointToCurve(const QPointF &point) {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    const int curveIndex = findNearestFreeCurve(point);
    if (curveIndex >= 0) {
        CurveObject curve = sketches_.at(activeSketch_).curves.at(curveIndex);
        if (!canChangeControlPointCount(curve)) {
            showStatus(QStringLiteral("Su una NURBS ritagliata puoi spostare i punti esistenti; aggiunta e rimozione non sono supportate."));
            return;
        }
        QVector<int> origins;
        for (int k = 0; k < curve.controlPoints.size(); ++k) origins.append(k);
        int insertIndex = curve.controlPoints.size();
        double nearestDistance = std::numeric_limits<double>::max();
        for (int index = 1; index < curve.controlPoints.size(); ++index) {
            const double distance = distanceToSegment(point, curve.controlPoints.at(index - 1), curve.controlPoints.at(index));
            if (distance < nearestDistance) {
                nearestDistance = distance;
                insertIndex = index;
            }
        }
        if (curve.tool == DrawingTool::Spline && curve.tangentHandles.size() != curve.controlPoints.size())
            ForgeCad::initializeTangentHandles(curve);
        if (curve.tool == DrawingTool::Spline) curve.tangentLinked.resize(curve.controlPoints.size());
        curve.controlPoints.insert(insertIndex, point);
        origins.insert(insertIndex, -1);
        if (curve.tool == DrawingTool::Nurbs) {
            if (curve.weights.size() != curve.controlPoints.size() - 1) curve.weights.fill(1.0, curve.controlPoints.size() - 1);
            curve.weights.insert(insertIndex, 1.0);
        } else {
            curve.tangentHandles.insert(insertIndex, {point - QPointF(1, 0), point + QPointF(1, 0)});
            curve.tangentLinked.insert(insertIndex, true);
        }
        commitFreeCurveEdit(curveIndex, curve, origins);
    } else {
        curveControlPoints_.append(point);
    }
    update();
}

void CadViewport::commitFreeCurveEdit(int curveIndex, const CurveObject &working, const QVector<int> &origins) {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    const SketchObject &before = sketches_.at(activeSketch_);
    if (curveIndex < 0 || curveIndex >= before.curves.size()) return;
    const int oldCount = before.curves.at(curveIndex).controlPoints.size();
    CurveObject validated = working;
    ForgeCad::recalculateCurve(validated, tessellationQuality_);
    if (origins.size() != validated.controlPoints.size() || !validated.numericallyValid
        || (!canChangeControlPointCount(before.curves.at(curveIndex)) && oldCount != validated.controlPoints.size())) {
        showStatus(QStringLiteral("Modifica non applicata: la curva risultante non e' valida o cambia i poli di una NURBS ritagliata."));
        return;
    }
    QVector<int> map(oldCount, -1);
    for (int now = 0; now < origins.size(); ++now)
        if (origins.at(now) >= 0 && origins.at(now) < oldCount) map[origins.at(now)] = now;
    const auto remap = [&](ConstraintRef &ref) {
        if (ref.kind != 1 || ref.element != curveIndex || ref.point < 0) return true;
        const bool handle = isHandlePoint(ref.point);
        const int old = handle ? (ref.point - kHandlePoint) / 2 : ref.point;
        if (old < 0 || old >= map.size() || map.at(old) < 0) return false;
        if (handle) ref.point = handlePoint(map.at(old), (ref.point - kHandlePoint) % 2);
        else ref.point = map.at(old);
        return true;
    };
    recordUndo();
    // Acquisire il riferimento scrivibile DOPO lo snapshot: detach del QVector
    // esterno, altrimenti le modifiche alterano anche lo stato di Undo.
    SketchObject &sketch = sketches_[activeSketch_];
    QVector<SketchConstraint> kept;
    for (SketchConstraint constraint : sketch.geometricConstraints)
        if (remap(constraint.first) && remap(constraint.second) && remap(constraint.third)) kept.append(std::move(constraint));
    sketch.geometricConstraints = std::move(kept);
    QVector<CoincidentConstraint> coincidences;
    for (CoincidentConstraint c : sketch.coincidentConstraints) {
        ConstraintRef a{c.firstKind, c.firstElement, c.firstPoint}, b{c.secondKind, c.secondElement, c.secondPoint};
        if (!remap(a) || !remap(b)) continue;
        c = {a.kind, a.element, a.point, b.kind, b.element, b.point};
        coincidences.append(c);
    }
    sketch.coincidentConstraints = std::move(coincidences);
    sketch.curves[curveIndex] = std::move(validated);
    selectedPoints_.clear();
    selectedConstraints_.clear();
    sketchEdited();
}

const QVector<ForgeCad::OverlayVertex> &CadViewport::sketchCurveComb(int index, const CurveObject &curve) {
    if (sketchCombCache_.size() <= index) sketchCombCache_.resize(index+1);
    auto &cache = sketchCombCache_[index];
    const auto &old = cache.source;
    if (!cache.valid || cache.scale != sketchCombScale_ || old.tool != curve.tool
        || old.controlPoints != curve.controlPoints || old.tangentHandles != curve.tangentHandles
        || old.weights != curve.weights || old.knots != curve.knots || old.degree != curve.degree) {
        cache.lines.clear(); cache.source=curve; cache.scale=sketchCombScale_; cache.valid=true;
        try {
            for (const auto &segment : ForgeCad::curveGeometry(curve)) {
                ExtrusionObject object;
                object.curve=std::make_shared<ForgeCad::Kernel::TrimmedCurve<3>>(
                    ForgeCad::Kernel::embedCurve(segment.curve, ForgeCad::Kernel::Frame3{}), segment.range.lo, segment.range.hi);
                cache.lines += ForgeCad::analyzeShape(object,4,sketchCombScale_,.1,.001).lines;
            }
        } catch (const std::exception &) { cache.lines.clear(); }
    }
    return cache.lines;
}

void CadViewport::drawSketchCurvature() {
    if (!sketchMode_ || !sketchCombVisible_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    const auto &sketch = sketches_.at(activeSketch_);
    sketchCombCache_.resize(sketch.curves.size());
    glDisable(GL_DEPTH_TEST);
    for (int i=0; i<sketch.curves.size(); ++i) {
        const auto &curve = i==sketchCombPreviewIndex_ ? sketchCombPreview_ : sketch.curves[i];
        if (curve.tool!=DrawingTool::Spline && curve.tool!=DrawingTool::Nurbs) continue;
        QVector<ForgeCad::OverlayVertex> lines = sketchCurveComb(i,curve);
        for (auto &vertex : lines) vertex.position=mapSketchPoint(QPointF(vertex.position.x(),vertex.position.y()),sketch);
        overlayRenderer_.draw(GL_LINES,lines);
        if (i==sketchCombPreviewIndex_) {
            QVector<QVector3D> path;
            for (const QPointF &p : curve.samples) path.append(mapSketchPoint(p,sketch));
            overlayRenderer_.draw(GL_LINE_STRIP,path,QVector4D(.2f,1,.45f,1));
        }
    }
    glEnable(GL_DEPTH_TEST);
}

void CadViewport::editFreeCurve(int curveIndex, int initialPoint ) {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    const CurveObject &source = sketches_.at(activeSketch_).curves.value(curveIndex);
    if (source.tool != DrawingTool::Spline && source.tool != DrawingTool::Nurbs) return;
    CurveObject work = source;
    if (work.tool == DrawingTool::Spline && work.tangentHandles.size() != work.controlPoints.size())
        ForgeCad::initializeTangentHandles(work);
    work.tangentLinked.resize(work.controlPoints.size());
    QVector<int> origins;
    for (int k = 0; k < work.controlPoints.size(); ++k) origins.append(k);

    QDialog dialog(this);
    dialog.setWindowTitle(work.tool == DrawingTool::Spline ? QStringLiteral("Modifica spline") : QStringLiteral("Modifica NURBS"));
    auto *form = new QFormLayout(&dialog);
    auto *points = new QListWidget(&dialog);
    points->setMinimumHeight(150);
    auto refill = [&] {
        const int row = qBound(0, points->currentRow(), work.controlPoints.size() - 1);
        points->clear();
        for (int k = 0; k < work.controlPoints.size(); ++k)
            points->addItem(QStringLiteral("P%1   (%2, %3)").arg(k + 1)
                .arg(ForgeCad::formatLength(work.controlPoints.at(k).x()), ForgeCad::formatLength(work.controlPoints.at(k).y())));
        if (points->count()) points->setCurrentRow(initialPoint >= 0 ? qBound(0, initialPoint, points->count() - 1) : row);
        initialPoint = -1;
    };
    auto spin = [&](double value) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setRange(-1e9, 1e9); box->setDecimals(6); box->setValue(value);
        return box;
    };
    auto *x = spin(0.0), *y = spin(0.0);
    x->setObjectName(QStringLiteral("freeCurveX")); y->setObjectName(QStringLiteral("freeCurveY"));
    auto *linked = new QCheckBox(QStringLiteral("Maniglie collegate (tangenza)"), &dialog);
    auto *inLength = spin(0.0), *outLength = spin(0.0);
    inLength->setRange(0.0, 1e9); outLength->setRange(0.0, 1e9);
    auto *row = new QWidget(&dialog); auto *rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 0);
    auto *add = new QPushButton(QStringLiteral("Aggiungi dopo"), row), *remove = new QPushButton(QStringLiteral("Elimina punto"), row);
    add->setObjectName(QStringLiteral("freeCurveAdd"));
    remove->setObjectName(QStringLiteral("freeCurveRemove"));
    add->setEnabled(canChangeControlPointCount(work));
    rowLayout->addWidget(add); rowLayout->addWidget(remove);
    if (!canChangeControlPointCount(work)) {
        auto *note = new QLabel(QStringLiteral("NURBS ritagliata: puoi spostare i punti esistenti. Aggiunta e rimozione dei punti non sono supportate."), &dialog);
        note->setWordWrap(true); form->addRow(note);
    }
    form->addRow(QStringLiteral("Punti di controllo:"), points);
    form->addRow(QString(), row);
    form->addRow(QStringLiteral("X:"), x); form->addRow(QStringLiteral("Y:"), y);
    if (work.tool == DrawingTool::Spline) {
        form->addRow(QString(), linked);
        form->addRow(QStringLiteral("Maniglia entrante:"), inLength);
        form->addRow(QStringLiteral("Maniglia uscente:"), outLength);
    } else {
        auto *note = new QLabel(QStringLiteral("Nelle NURBS la tangenza e' determinata dal poligono dei punti di controllo."), &dialog);
        note->setWordWrap(true); form->addRow(note);
    }
    auto *uniform = new QCheckBox(QStringLiteral("Uniforma la curvatura (C2)"), &dialog);
    auto *relaxed = new QCheckBox(QStringLiteral("Rilassa la curva"), &dialog);
    auto *limited = new QCheckBox(QStringLiteral("Limita le oscillazioni"), &dialog);
    if (work.tool == DrawingTool::Spline) {
        form->addRow(uniform); form->addRow(relaxed); form->addRow(limited);
        auto *note = new QLabel(QStringLiteral("Le spunte ricalcolano tutte le maniglie alla conferma, mantenendo i punti di passaggio. "
            "Rilassamento e limitazione mantengono la tangenza, ma non la continuita' C2. "
            "La limitazione evita inversioni di X/Y nei singoli tratti; i flessi necessari restano."), &dialog);
        note->setWordWrap(true); form->addRow(note);
        const auto enableManual = [=] {
            const bool manual = !(uniform->isChecked() || relaxed->isChecked() || limited->isChecked());
            linked->setEnabled(manual); inLength->setEnabled(manual); outLength->setEnabled(manual);
        };
        for (QCheckBox *box : {uniform, relaxed, limited})
            QObject::connect(box, &QCheckBox::toggled, &dialog, enableManual);
    } else {
        uniform->hide(); relaxed->hide(); limited->hide();
    }
    bool loading = false;
    const auto load = [&] {
        const int k = points->currentRow(); if (k < 0 || k >= work.controlPoints.size()) return;
        loading = true;
        x->setValue(work.controlPoints.at(k).x()); y->setValue(work.controlPoints.at(k).y());
        remove->setEnabled(canChangeControlPointCount(work) && work.controlPoints.size() > (work.tool == DrawingTool::Nurbs ? 4 : 2));
        if (work.tool == DrawingTool::Spline) {
            linked->setChecked(work.tangentLinked.value(k));
            inLength->setValue(pointDistance(work.controlPoints.at(k), work.tangentHandles.at(k).first));
            outLength->setValue(pointDistance(work.controlPoints.at(k), work.tangentHandles.at(k).second));
        }
        loading = false;
    };
    QObject::connect(points, &QListWidget::currentRowChanged, &dialog, [&](int) { load(); });
    const auto updatePoint = [&] {
        if (loading) return;
        const int k = points->currentRow();
        if (k < 0) return;
        const QPointF before = work.controlPoints.at(k), now(x->value(), y->value()), delta = now - before;
        work.controlPoints[k] = now;
        if (work.tool == DrawingTool::Spline) {
            work.tangentHandles[k].first += delta; work.tangentHandles[k].second += delta;
        }
        refill();
    };
    QObject::connect(x, &QDoubleSpinBox::valueChanged, &dialog, [&](double) { updatePoint(); });
    QObject::connect(y, &QDoubleSpinBox::valueChanged, &dialog, [&](double) { updatePoint(); });
    if (work.tool == DrawingTool::Spline) {
        QObject::connect(linked, &QCheckBox::toggled, &dialog, [&](bool on) {
            if (loading) return;
            const int k = points->currentRow();
            if (k < 0) return;
            work.tangentLinked[k] = on;
            if (on) {
                QPointF d = work.controlPoints.at(k) - work.tangentHandles.at(k).first;
                const double l = pointLength(d), out = pointDistance(work.controlPoints.at(k), work.tangentHandles.at(k).second);
                if (l > 0.0) work.tangentHandles[k].second = work.controlPoints.at(k) + d * (out / l);
            }
        });
        const auto lengthChanged = [&](int side, double length) {
            if (loading) return;
            const int k = points->currentRow();
            if (k < 0) return;
            QPointF &handle = side == 0 ? work.tangentHandles[k].first : work.tangentHandles[k].second;
            QPointF d = handle - work.controlPoints.at(k); const double old = pointLength(d);
            if (old <= 1e-12) d = QPointF(side == 0 ? -1.0 : 1.0, 0.0); else d /= old;
            handle = work.controlPoints.at(k) + d * length;
            if (work.tangentLinked.value(k)) {
                QPointF &other = side == 0 ? work.tangentHandles[k].second : work.tangentHandles[k].first;
                const double otherLength = pointDistance(other, work.controlPoints.at(k));
                other = work.controlPoints.at(k) - d * otherLength;
            }
        };
        QObject::connect(inLength, &QDoubleSpinBox::valueChanged, &dialog, [&, lengthChanged](double v) { lengthChanged(0, v); });
        QObject::connect(outLength, &QDoubleSpinBox::valueChanged, &dialog, [&, lengthChanged](double v) { lengthChanged(1, v); });
    }
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        const int at = qMax(0, points->currentRow()) + 1;
        const QPointF a = work.controlPoints.at(at - 1), b = work.controlPoints.value(at, a + QPointF(10.0, 0.0));
        work.controlPoints.insert(at, 0.5 * (a + b)); origins.insert(at, -1);
        if (work.tool == DrawingTool::Nurbs) {
            if (work.weights.size() != work.controlPoints.size() - 1)
                work.weights.fill(1.0, work.controlPoints.size() - 1);
            work.weights.insert(at, 1.0);
        }
        else { work.tangentHandles.insert(at, {work.controlPoints.at(at) - QPointF(1.0, 0.0), work.controlPoints.at(at) + QPointF(1.0, 0.0)}); work.tangentLinked.insert(at, true); }
        initialPoint = at; refill(); load();
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        const int at = points->currentRow(), minimum = work.tool == DrawingTool::Nurbs ? 4 : 2;
        if (at < 0 || work.controlPoints.size() <= minimum) return;
        work.controlPoints.removeAt(at); origins.removeAt(at);
        if (work.tool == DrawingTool::Nurbs && at < work.weights.size()) work.weights.removeAt(at);
        if (work.tool == DrawingTool::Spline) { work.tangentHandles.removeAt(at); work.tangentLinked.removeAt(at); }
        initialPoint = qMin(at, work.controlPoints.size() - 1); refill(); load();
    });
    auto *comb = new QCheckBox(QStringLiteral("Mostra pettini di curvatura nello schizzo"), &dialog);
    comb->setObjectName(QStringLiteral("sketchCurvatureComb")); comb->setChecked(sketchCombVisible_);
    auto *automatic = new QCheckBox(QStringLiteral("Scala automatica per curva"), &dialog);
    automatic->setObjectName(QStringLiteral("sketchCurvatureAuto")); automatic->setChecked(sketchCombScale_==0);
    auto *gain = new QDoubleSpinBox(&dialog);
    gain->setDecimals(6); gain->setRange(.000001,1e9);
    gain->setValue(sketchCombScale_>0 ? sketchCombScale_ : 100);
    gain->setObjectName(QStringLiteral("sketchCurvatureScale"));
    form->addRow(comb); form->addRow(automatic); form->addRow(QStringLiteral("Scala pettine (mm²):"),gain);
    auto *combNote = new QLabel(QStringLiteral("Denti blu: più lunghi dove la curva piega di più; inviluppo arancio. "
        "Il pettine resta su spline e NURBS dello schizzo attivo, anche dopo la chiusura. "
        "Verde: anteprima delle modifiche; OK le applica, Annulla conserva la curva originale. "
        "Scala automatica indipendente per curva; usa la scala manuale per confrontarle."), &dialog);
    combNote->setWordWrap(true); combNote->setMaximumWidth(520); form->addRow(combNote);
    const auto refreshComb = [&] {
        if (loading) return;
        sketchCombVisible_=comb->isChecked();
        sketchCombScale_=automatic->isChecked()?0:gain->value();
        automatic->setEnabled(sketchCombVisible_); gain->setEnabled(sketchCombVisible_ && !automatic->isChecked());
        sketchCombPreviewIndex_=sketchCombVisible_?curveIndex:-1;
        if (sketchCombVisible_) {
            sketchCombPreview_=work;
            ForgeCad::shapeSpline(sketchCombPreview_,{uniform->isChecked(),relaxed->isChecked(),limited->isChecked()});
            ForgeCad::recalculateCurve(sketchCombPreview_,tessellationQuality_);
        }
        update();
    };
    // Connessioni dopo quelle di modifica: il pettine usa sempre la copia gia' aggiornata.
    for (QCheckBox *box : {comb,automatic,uniform,relaxed,limited,linked})
        QObject::connect(box,&QCheckBox::toggled,&dialog,[&] { refreshComb(); });
    for (QDoubleSpinBox *box : std::array<QDoubleSpinBox *,5>{x,y,inLength,outLength,gain})
        QObject::connect(box,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&] { refreshComb(); });
    for (QPushButton *button : {add,remove})
        QObject::connect(button,&QPushButton::clicked,&dialog,[&] { refreshComb(); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons); refill(); load(); refreshComb();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    sketchCombPreviewIndex_=-1;
    sketchCombCache_.clear();
    update();
    if (accepted) {
        ForgeCad::shapeSpline(work, {uniform->isChecked(), relaxed->isChecked(), limited->isChecked()});
        commitFreeCurveEdit(curveIndex, work, origins);
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

void CadViewport::finalizeCurve() {
    if (drawingTool_ != DrawingTool::Spline && drawingTool_ != DrawingTool::Nurbs) return;
    const int minimumPoints = drawingTool_ == DrawingTool::Nurbs ? 4 : 2;
    if (curveControlPoints_.size() < minimumPoints
        || activeSketch_ < 0 || activeSketch_ >= sketches_.size()) {
        return;
    }
    CurveObject curve;
    curve.tool = drawingTool_;
    curve.controlPoints = curveControlPoints_;
    ForgeCad::shapeSpline(curve, splineShapeOptions_);
    ForgeCad::recalculateCurve(curve, tessellationQuality_);
    if (curve.numericallyValid) {
        recordUndo();
        SketchObject &sketch = sketches_[activeSketch_];
        sketch.curves.append(curve);
        // Gli estremi che cadono su punti di altre entita' diventano coincidenti (poi la tangenza si da' nel punto).
        const int index = int(sketch.curves.size()) - 1, last = int(curve.controlPoints.size()) - 1;
        for (int k = 0; k <= last; ++k)
            recordPointCoincidences(sketch, {1, index, k}, curve.controlPoints.at(k));
        if (curve.tool == DrawingTool::Spline && last > 1
            && pointDistance(curve.controlPoints.first(),curve.controlPoints.last()) <= ForgeCad::kSketchConnectionTolerance) {
            SketchConstraint closure;
            closure.type = ConstraintType::Coincident;
            closure.first = {1,index,0};
            closure.second = {1,index,last};
            sketch.geometricConstraints.append(closure);
        }
        sketchEdited();
    }
    curveControlPoints_.clear();
    hasPendingPoint_ = false;
    update();
}

void CadViewport::finalizePrimitive() {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    CurveObject primitive;
    primitive.tool = drawingTool_;
    primitive.controlPoints = curveControlPoints_;
    if (drawingTool_ == DrawingTool::Polygon) primitive.sides = polygonSides_;
    if (drawingTool_ == DrawingTool::Ellipse && primitive.controlPoints.size() >= 3)
        primitive.controlPoints[2] = ellipseMinorPoint(primitive.controlPoints.at(0), primitive.controlPoints.at(1), primitive.controlPoints.at(2));
    ForgeCad::recalculateCurve(primitive, tessellationQuality_);
    if (primitive.numericallyValid) {
        recordUndo();
        SketchObject &sketch = sketches_[activeSketch_];
        sketch.curves.append(primitive);
        const int index = int(sketch.curves.size()) - 1;
        for (int k = 0; k < primitive.controlPoints.size(); ++k)
            recordPointCoincidences(sketch, {1,index,k}, curveControlPoints_.at(k));
        sketchEdited();
    }
    curveControlPoints_.clear();
    hasPendingPoint_ = false;
    update();
}

void CadViewport::finalizeRectangle() {
    const bool centered = drawingTool_ == DrawingTool::CenterRectangle;
    const QPointF p = curveControlPoints_.value(0), q = curveControlPoints_.value(1);
    curveControlPoints_.clear();
    hasPendingPoint_ = false;
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    const QPointF a = centered ? 2.0 * p - q : p;
    const double tolerance = ForgeCad::kSketchConnectionTolerance;
    if (std::abs(q.x() - a.x()) <= tolerance || std::abs(q.y() - a.y()) <= tolerance) {
        update();
        return;
    }
    const QPointF corners[4] = {a, QPointF(q.x(), a.y()), q, QPointF(a.x(), q.y())};
    recordUndo();
    SketchObject &sketch = sketches_[activeSketch_];
    for (int side = 0; side < 4; ++side) {
        sketch.segments.append(qMakePair(corners[side], corners[(side + 1) % 4]));
        sketch.constraints.append(-1);
        sketch.segmentLengths.append(0.0);
        sketch.segmentAngles.append(-1.0);
        SketchConstraint direction;
        direction.type = side % 2 == 0 ? ConstraintType::Horizontal : ConstraintType::Vertical;
        direction.first = {0, int(sketch.segments.size()) - 1, -1};
        sketch.geometricConstraints.append(direction);
        recordCoincidences(sketch, sketch.segments.size() - 1);
    }
    sketchEdited();
    update();
}

QPointF CadViewport::rectangleCorner(const QPointF &anchor, const QPointF &cursor) const {
    if (!(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)) return cursor;
    const QPointF d = cursor - anchor;
    const double side = std::max(std::abs(d.x()), std::abs(d.y()));
    return anchor + QPointF(d.x() < 0.0 ? -side : side, d.y() < 0.0 ? -side : side);
}

QPointF CadViewport::ellipseMinorPoint(const QPointF &center, const QPointF &major, const QPointF &through) {
    const double a = pointDistance(center, major);
    if (a <= 0.0) return through;
    const QPointF u = (major - center) / a, perpendicular(-u.y(), u.x());
    const QPointF r = through - center;
    return center + std::abs(r.x() * perpendicular.x() + r.y() * perpendicular.y()) * perpendicular;
}

void CadViewport::normalizeEllipse(CurveObject &curve, int moved, const QPointF &previous) {
    if (curve.tool != DrawingTool::Ellipse || curve.controlPoints.size() < 3) return;
    QVector<QPointF> &p = curve.controlPoints;
    if (moved == 0) {
        const QPointF delta = p.at(0) - previous;
        p[1] += delta;
        p[2] += delta;
        return;
    }
    const double b = pointDistance(p.at(0), p.at(2));
    const double a = pointDistance(p.at(0), p.at(1));
    if (a <= 0.0) return;
    const QPointF u = (p.at(1) - p.at(0)) / a, perpendicular(-u.y(), u.x());
    if (moved == 2) {
        p[2] = ellipseMinorPoint(p.at(0), p.at(1), p.at(2));
    } else {
        // Il primo semiasse cambia: il secondo resta lungo uguale, perpendicolare
        // e dalla stessa parte di prima.
        const QPointF r = previous - p.at(0);
        const QPointF oldPerpendicular(-r.y(), r.x());
        const QPointF w = p.at(2) - p.at(0);
        const double side = w.x() * oldPerpendicular.x() + w.y() * oldPerpendicular.y() < 0.0 ? -1.0 : 1.0;
        p[2] = p.at(0) + side * b * perpendicular;
    }
}

QPointF CadViewport::projectWorldPoint(const QVector3D &point) const {
    return camera_.projectWorldPoint(point, size());
}

void CadViewport::drawReferenceLabels() {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // Lettere degli assi oltre la punta delle frecce.
    painter.setFont(uiFont(12, QFont::Bold));
    const float tipDistance = float(axisDisplayLength()) * 1.1f;
    const QPointF origin = projectWorldPoint(QVector3D());
    for (int axis = 0; axis < 3 && axesVisible_; ++axis) {
        QVector3D tip;
        tip[axis] = tipDistance;
        QPointF position = projectWorldPoint(tip);
        // Asse visto di punta: la lettera accanto all'origine.
        if (pointDistance(position, origin) < 6.0) position = origin + QPointF(8.0, -8.0 - 14.0 * axis);
        painter.setPen(axisColor(axis));
        painter.drawText(QRectF(position - QPointF(10.0, 10.0), QSizeF(20.0, 20.0)), Qt::AlignCenter,
                         QString(QChar(u'X' + axis)));
    }
    painter.setFont(uiFont(10, QFont::DemiBold));
    painter.setPen(QColor(150, 200, 255));
    QSizeF size = referencePlaneExtents(0);
    if (isPlaneShown(0)) painter.drawText(projectWorldPoint(QVector3D(0.75f * float(size.width()), 0.75f * float(size.height()), 0.0f)), QStringLiteral("Piano XY"));
    painter.setPen(QColor(150, 240, 190));
    size = referencePlaneExtents(1);
    if (isPlaneShown(1)) painter.drawText(projectWorldPoint(QVector3D(0.75f * float(size.width()), 0.0f, 0.75f * float(size.height()))), QStringLiteral("Piano XZ"));
    painter.setPen(QColor(255, 170, 140));
    size = referencePlaneExtents(2);
    if (isPlaneShown(2)) painter.drawText(projectWorldPoint(QVector3D(0.0f, 0.75f * float(size.width()), 0.75f * float(size.height()))), QStringLiteral("Piano YZ"));
    drawDatumLabels(painter);
    drawTopologyLabels(painter);
    if (sketchMode_) {
        painter.setPen(QColor(255, 220, 120));
        const ForgeCad::SketchAnalysis &analysis = sketchAnalysis();
        painter.drawText(20, 24, QStringLiteral("MODALITA SCHIZZO - VISTA NORMALE BLOCCATA"));
        painter.setPen(analysis.fullyDefined() ? QColor(235, 240, 250) : QColor(120, 190, 255));
        painter.drawText(20, 42, analysis.fullyDefined() ? QStringLiteral("Schizzo completamente definito")
                                                         : QStringLiteral("Gradi di liberta': %1 (sotto definito)").arg(analysis.degreesOfFreedom));
        drawDimensions(painter);
        drawSketchConstraints(painter);
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
        for (const ExtrusionObject &occluder : extrusions_)
            if (occluder.visible && ForgeCad::isShapeBody(occluder)
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
        if (!body.visible || !ForgeCad::isShapeBody(body) || (onlyBody >= 0 && bodyIndex != onlyBody)) continue;
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

void CadViewport::drawTopologyLabels(QPainter &painter) const {
    const QVector<TopologyLabelItem> labels = topologyLabelItems();
    if (labels.isEmpty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(uiFont(8, QFont::DemiBold));
    for (const TopologyLabelItem &item : labels) {
        const QColor color = item.edge ? QColor(105, 225, 255) : QColor(255, 205, 95);
        const QPointF center = item.box.center();
        const QPointF delta = item.anchor - center;
        const double dx = std::fabs(delta.x()), dy = std::fabs(delta.y());
        const double tx = dx > 1e-9 ? 0.5 * item.box.width() / dx : std::numeric_limits<double>::max();
        const double ty = dy > 1e-9 ? 0.5 * item.box.height() / dy : std::numeric_limits<double>::max();
        const QPointF start = center + qMin(tx, ty) * delta;
        const double length = std::hypot(double(start.x() - item.anchor.x()), double(start.y() - item.anchor.y()));
        painter.setPen(QPen(color, 1.4));
        painter.setBrush(color);
        painter.drawLine(start, item.anchor);
        if (length > 1e-6) {
            const QPointF back((start.x() - item.anchor.x()) / length, (start.y() - item.anchor.y()) / length);
            const QPointF side(-back.y(), back.x());
            QPolygonF arrow;
            arrow << item.anchor << item.anchor + 6.0 * back + 2.8 * side << item.anchor + 6.0 * back - 2.8 * side;
            painter.drawPolygon(arrow);
        }
        painter.setPen(QPen(QColor(10, 15, 22, 220), 2.0));
        painter.setBrush(QColor(20, 29, 40, 215));
        painter.drawRoundedRect(item.box, 3.0, 3.0);
        painter.setPen(color);
        painter.drawText(item.box, Qt::AlignCenter, item.text);
    }
    painter.restore();
}

void CadViewport::drawSketchDimensions(QPainter &painter) const {
    if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    const SketchObject &sketch = sketches_.at(activeSketch_);
    painter.setFont(uiFont(9, QFont::DemiBold));
    for (int index = 0; index < sketch.segments.size(); ++index) {
        const auto &segment = sketch.segments.at(index);
        const QVector3D first = mapSketchPoint(segment.first, sketch);
        const QVector3D second = mapSketchPoint(segment.second, sketch);
        const QPointF firstScreen = projectWorldPoint(first);
        const QPointF secondScreen = projectWorldPoint(second);
        const QPointF midpoint = (firstScreen + secondScreen) * 0.5;
        const float dx = float(secondScreen.x() - firstScreen.x());
        const float dy = float(secondScreen.y() - firstScreen.y());
        const float scale = qMax(1.0f, qMax(qAbs(dx), qAbs(dy)));
        const QPointF labelPosition = midpoint + QPointF(-dy / scale * 14.0, dx / scale * 14.0);
        const double length = pointDistance(segment.first, segment.second);
        bool constrained = false;
        for (const SketchConstraint &c : sketch.geometricConstraints)
            constrained = constrained || (c.type == ConstraintType::Distance && c.first == ConstraintRef{0, index, -1} && c.second.kind < 0);
        painter.setPen(constrained ? QColor(255, 215, 90) : QColor(180, 220, 235));
        painter.drawLine(midpoint, labelPosition);
        painter.drawText(labelPosition + QPointF(4.0, -4.0),
                         QStringLiteral("L = %1").arg(ForgeCad::formatLength(length, 4)));
        if (index < sketch.segmentAngles.size() && sketch.segmentAngles.at(index) >= 0.0) {
            painter.drawText(labelPosition + QPointF(4.0, 10.0),
                             QStringLiteral("A = %1 deg")
                                 .arg(sketch.segmentAngles.at(index), 0, 'f', 4));
        }
    }
}

QVector3D CadViewport::mapSketchPoint(const QPointF &point, const SketchObject &sketch) const {
    return ForgeCad::sketchToDisplay(point, sketch);
}

QVector3D CadViewport::mapActiveSketchPoint(const QPointF &point) const {
    if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) return mapSketchPoint(point, sketches_.at(activeSketch_));
    return ForgeCad::sketchToDisplay(point, activePlane_);
}

QPointF CadViewport::screenToSketchPoint(const QPoint &position) const {
    // Vista ruotata nello schizzo: il punto del piano dello schizzo lungo il raggio del puntatore.
    if (sketchViewRotated_ && activeSketchObject()) {
        QPointF point;
        if (rayToSketchPlane(position, point)) return point;
    }
    const double aspect = double(width()) / double(qMax(1, height()));
    const double viewScale = double(camera_.zoom_) / 8.0;
    return QPointF((double(position.x()) / double(qMax(1, width())) - 0.5) * 8.0 * aspect * viewScale - double(camera_.panX_),
                   (0.5 - double(position.y()) / double(qMax(1, height()))) * 8.0 * viewScale - double(camera_.panY_));
}

bool CadViewport::rayToSketchPlane(const QPoint &position, QPointF &point) const {
    const SketchObject *sketch = activeSketchObject();
    if (!sketch) return false;
    QVector3D origin, direction;
    viewRay(position, origin, direction);
    const ForgeCad::Kernel::Frame3 axes = ForgeCad::sketchAxes(*sketch);
    const ForgeCad::Kernel::Vec3 &o = axes.origin(), &n = axes.zDir();
    const double ox = double(origin.x()), oy = double(origin.y()), oz = double(origin.z());
    const double dx = double(direction.x()), dy = double(direction.y()), dz = double(direction.z());
    const double dn = dx * n.x() + dy * n.y() + dz * n.z();
    if (std::fabs(dn) < 1e-6 * std::sqrt(dx * dx + dy * dy + dz * dz)) return false;
    const double t = ((o.x() - ox) * n.x() + (o.y() - oy) * n.y() + (o.z() - oz) * n.z()) / dn;
    point = ForgeCad::worldToSketch(ForgeCad::Kernel::Vec3(ox + t * dx, oy + t * dy, oz + t * dz), *sketch);
    return true;
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
        if (curve.tool == DrawingTool::Spline || curve.tool == DrawingTool::Nurbs)
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
    if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) {
        segments = sketches_.at(activeSketch_).segments;
        points = snapCandidates(sketches_.at(activeSketch_));
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
        const SketchObject &sketch = sketches_.at(activeSketch_);
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

void CadViewport::drawSnapMarkers() {
    if (!sketchMode_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
    const SketchObject &sketch = sketches_.at(activeSketch_);
    glPointSize(9.0f);
    QVector<ForgeCad::OverlayVertex> points;
    QVector<ForgeCad::OverlayVertex> handlePoints;
    const auto point = [&points](const QVector3D &position, float r, float g, float b) {
        points.push_back({position, QVector4D(r, g, b, 1.0f)});
    };
    for (const CurveObject &curve : sketch.curves) {
        const float red = curve.tool == DrawingTool::Nurbs ? 0.95f : 1.0f;
        if (curve.tool == DrawingTool::Converted) {
            // Curva derivata: i poli non si modificano, si vedono solo gli estremi.
            if (!curve.controlPoints.isEmpty()) {
                point(mapSketchPoint(curve.controlPoints.first(), sketch), 1.0f, 0.85f, 0.15f);
                point(mapSketchPoint(curve.controlPoints.last(), sketch), 1.0f, 0.85f, 0.15f);
            }
            continue;
        }
        const bool hover=sketchHover_.kind==1&&sketchHover_.index==int(&curve-sketch.curves.constData());
        for (const QPointF &control : curveEditMarkers(curve,hover)) {
            const QVector3D world = mapSketchPoint(control, sketch);
            point(world, red, 0.35f, 0.75f);
        }
        for (int k=0; k<curve.tangentHandles.size(); ++k) {
            const auto &handles=curve.tangentHandles.at(k);
            const QVector4D color(0.20f,0.75f,1.0f,1.0f);
            if (visibleSplineHandle(curve,k,0)) handlePoints.push_back({mapSketchPoint(handles.first,sketch),color});
            if (visibleSplineHandle(curve,k,1)) handlePoints.push_back({mapSketchPoint(handles.second,sketch),color});
        }
    }
    for (const QPointF &control : curveControlPoints_) {
        const QVector3D world = mapActiveSketchPoint(control);
        point(world, 1.0f, 0.45f, 0.25f);
    }
    for (const auto &segment : sketch.segments) {
        for (const QPointF &endpoint : {segment.first, segment.second}) {
            const QVector3D world = mapSketchPoint(endpoint, sketch);
            point(world, 1.0f, 0.85f, 0.15f);
        }
        const QVector3D midpoint = mapSketchPoint((segment.first + segment.second) * 0.5, sketch);
        point(midpoint, 0.25f, 1.0f, 0.35f);
    }
    if (lastSnapKind_ != SnapKind::None) {
        const QVector3D world = mapSketchPoint(lastSnapPoint_, sketch);
        if (lastSnapKind_ == SnapKind::Nearest) point(world, 0.15f, 0.85f, 1.0f);
        else if (lastSnapKind_ == SnapKind::Midpoint) point(world, 0.25f, 1.0f, 0.35f);
        else point(world, 1.0f, 0.85f, 0.15f);
    }
    overlayRenderer_.draw(GL_POINTS, points);
    glPointSize(5.0f);
    overlayRenderer_.draw(GL_POINTS, handlePoints);
    glLineWidth(1.0f);
    QVector<ForgeCad::OverlayVertex> tangentLines;
    const QVector4D handleColor(0.35f, 0.55f, 0.65f, 1.0f);
    for (const CurveObject &curve : sketch.curves) {
        for (int index = 0; index < curve.tangentHandles.size(); ++index) {
            const QVector3D control = mapSketchPoint(curve.controlPoints.at(index), sketch);
            const auto &pair = curve.tangentHandles.at(index);
            const QVector3D incoming = mapSketchPoint(pair.first, sketch);
            const QVector3D outgoing = mapSketchPoint(pair.second, sketch);
            if (visibleSplineHandle(curve,index,0)) {
                tangentLines.push_back({incoming, handleColor});
                tangentLines.push_back({control, handleColor});
            }
            if (visibleSplineHandle(curve,index,1)) {
                tangentLines.push_back({control, handleColor});
                tangentLines.push_back({outgoing, handleColor});
            }
        }
    }
    overlayRenderer_.draw(GL_LINES, tangentLines);
}

void CadViewport::viewRay(const QPoint &position, QVector3D &origin, QVector3D &direction) const {
    camera_.viewRay(position, size(), sceneDepth(), origin, direction);
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
            if (!body.visible || !ForgeCad::isShapeBody(body) || body.display.vertices.isEmpty()) continue;
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
        if (!sketch.visible) continue;
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
        if (!body.visible || !ForgeCad::isCurveBody(body)) continue;
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
        if (!extrusion.visible || !ForgeCad::isShapeBody(extrusion)) continue;
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
        if (!sketch.visible) continue;
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

void CadViewport::strokeHighlight(QPainter &painter, const QVector<QPointF> &points,
                                bool closed, const QColor &color) {
    if (points.size() < 2) return;
    QPainterPath path(points.first());
    for (int index = 1; index < points.size(); ++index) path.lineTo(points.at(index));
    if (closed) path.closeSubpath();
    QColor halo = color;
    halo.setAlpha(70);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(halo, 9.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
}

QVector<QPointF> CadViewport::projectSketchPolyline(const QVector<QPointF> &points, const SketchObject &sketch) const {
    QVector<QPointF> projected;
    projected.reserve(points.size());
    for (const QPointF &point : points) projected.append(projectWorldPoint(mapSketchPoint(point, sketch)));
    return projected;
}

void CadViewport::highlightSketchElement(QPainter &painter, const SketchObject &sketch,
                                const SketchElementSelection &element, const QColor &color) const {
    if (element.kind == 0 && element.index >= 0 && element.index < sketch.segments.size()) {
        const auto &segment = sketch.segments.at(element.index);
        const QVector<QPointF> points = projectSketchPolyline({segment.first, segment.second}, sketch);
        strokeHighlight(painter, points, false, color);
        painter.setBrush(color);
        for (const QPointF &point : points) painter.drawEllipse(point, 4.0, 4.0);
    } else if (element.kind == 1 && element.index >= 0 && element.index < sketch.curves.size()) {
        strokeHighlight(painter, projectSketchPolyline(sketch.curves.at(element.index).samples, sketch),
                        false, color);
    }
}

void CadViewport::highlightSceneObject(QPainter &painter, const SceneSelection &target, const QColor &color) const {
    if (target.kind == SceneObjectKind::Plane && isPlaneShown(target.index)) {
        QVector<QPointF> corners;
        for (const QVector3D &corner : planeCorners(target.index)) corners.append(projectWorldPoint(corner));
        strokeHighlight(painter, corners, true, color);
    } else if (target.kind == SceneObjectKind::Sketch && target.index >= 0
               && target.index < sketches_.size() && sketches_.at(target.index).visible) {
        const SketchObject &sketch = sketches_.at(target.index);
        for (int index = 0; index < sketch.segments.size(); ++index)
            highlightSketchElement(painter, sketch, {0, index}, color);
        for (int index = 0; index < sketch.curves.size(); ++index)
            highlightSketchElement(painter, sketch, {1, index}, color);
    }
    if (target.kind == SceneObjectKind::Extrusion && isDatumShown(target.index)) {
        const ExtrusionObject &body = extrusions_.at(target.index);
        QVector<QPointF> corners;
        for (const QVector3D &corner : datumCorners(body.datumFrame, body.datum.size)) corners.append(projectWorldPoint(corner));
        strokeHighlight(painter, corners, true, color);
    }
    // I solidi estrusi vengono contornati in OpenGL (drawExtrusionOutline).
}

void CadViewport::drawSelectionHighlight() {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawSectionHandle(painter);
    drawFeatureHandle(painter);
    drawSelectionBox(painter);
    if (sketchMode_ && activeSketch_ >= 0 && activeSketch_ < sketches_.size()) {
        if (drawingTool_ == DrawingTool::Select || drawingTool_ == DrawingTool::Dimension) {
            int kind = -1, index = -1;
            if (findSketchElement(cursorSketchPoint_).kind < 0 && pickSketchReferencePlaneAxis(lastMousePosition_, kind, index)) {
                GeometryRef ref;
                ref.kind = kind; ref.index = index;
                drawGeometryRef(painter, ref, kHoverColor);
            }
        }
        const SketchObject &sketch = sketches_.at(activeSketch_);
        if (sketchHover_.kind >= 0 && !sketchSelections_.contains(sketchHover_))
            highlightSketchElement(painter, sketch, sketchHover_, kHoverColor);
        for (const SketchElementSelection &selected : sketchSelections_)
            highlightSketchElement(painter, sketch, selected, kSelectionColor);
        // Spigolo o curva di un corpo sotto il puntatore (strumento dei riferimenti).
        if (drawingTool_ == DrawingTool::ConvertEdges && convertHover_.body >= 0 && convertHover_.body < extrusions_.size()) {
            const QVector<QVector<QVector3D>> &edges = extrusions_.at(convertHover_.body).display.edges;
            if (convertHover_.polyline >= 0 && convertHover_.polyline < edges.size()) {
                QVector<QPointF> points;
                for (const QVector3D &p : edges.at(convertHover_.polyline)) points.append(projectWorldPoint(p));
                strokeHighlight(painter, points, false, kHoverColor);
            }
        } else if (drawingTool_ == DrawingTool::ConvertEdges && convertHover_.sketch >= 0
                   && convertHover_.sketch < sketches_.size()) {
            const SketchObject &source = sketches_.at(convertHover_.sketch);
            if (convertHover_.entityKind == 0 && convertHover_.entity >= 0 && convertHover_.entity < source.segments.size()) {
                const SketchSegment &segment = source.segments.at(convertHover_.entity);
                strokeHighlight(painter, projectSketchPolyline({segment.first, segment.second}, source), false, kHoverColor);
            } else if (convertHover_.entityKind == 1 && convertHover_.entity >= 0 && convertHover_.entity < source.curves.size()) {
                strokeHighlight(painter, projectSketchPolyline(source.curves.at(convertHover_.entity).samples, source), false, kHoverColor);
            }
        }
        if (blendFirst_ >= 0 && blendFirst_ < sketch.segments.size())
            highlightSketchElement(painter, sketch, {0, blendFirst_}, kSelectionColor);
        if (drawingTool_ == DrawingTool::Select) {
            const int midpoint = segmentMidpointAt(screenToSketchPoint(lastMousePosition_));
            for (int i = 0; i < sketch.segments.size(); ++i) {
                if (i != midpoint && !sketchSelections_.contains({0, i})) continue;
                const QPointF center = projectWorldPoint(mapSketchPoint(
                    0.5 * (sketch.segments.at(i).first + sketch.segments.at(i).second), sketch));
                painter.setPen(QPen(i == midpoint ? kHoverColor : kSelectionColor, 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawPolygon(QPolygonF({center + QPointF(0,-5), center + QPointF(5,4), center + QPointF(-5,4)}));
                if (i == midpoint) painter.drawText(center + QPointF(9,-7), QStringLiteral("Punto medio"));
            }
        }
        // Punti scelti (Ctrl+clic) e entita' dei vincoli selezionati o sotto il puntatore.
        painter.setPen(QPen(kSelectionColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        for (const SelectedPoint &point : selectedPoints_)
            if (isValidSelectedPoint(point))
                painter.drawEllipse(projectWorldPoint(mapSketchPoint(selectedPointPosition(point), sketch)), 6.0, 6.0);
        if (constraintHover_ >= 0 && !selectedConstraints_.contains(constraintHover_)) highlightConstraintEntities(painter, sketch, constraintHover_, kHoverColor);
        for (int index : selectedConstraints_) highlightConstraintEntities(painter, sketch, index, kSelectionColor);
        drawCurvePreview(painter, sketch);
        if (!sketchPatternPreview_.isEmpty()) {
            painter.setPen(QPen(QColor(255, 170, 60), 1.8, Qt::DashLine, Qt::RoundCap));
            for (const QVector<QPointF> &polyline : sketchPatternPreview_)
                if (polyline.size() >= 2) painter.drawPolyline(projectSketchPolyline(polyline, sketch).data(), int(polyline.size()));
        }
        if (drawingTool_ == DrawingTool::Trim && trimPreview_.size() >= 2) {
            painter.setPen(QPen(QColor(255, 80, 70), 4.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawPolyline(projectSketchPolyline(trimPreview_, sketch).data(), int(trimPreview_.size()));
        }
        drawInferenceTags(painter);
        return;
    }
    if(spatialAdd_) {
        painter.save();painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(130,220,255),2));painter.setBrush(QColor(35,105,145));
        for(const auto &p:spatialNodes_)painter.drawEllipse(projectWorldPoint(QVector3D(p.x,p.y,p.z)),5,5);
        if(spatialHoverValid_) {
            const QPointF cursor=projectWorldPoint(QVector3D(spatialHover_.x,spatialHover_.y,spatialHover_.z));
            painter.setPen(QPen(QColor(255,195,90),2,Qt::DashLine));
            if(!spatialNodes_.isEmpty()){const auto &p=spatialNodes_.last();painter.drawLine(projectWorldPoint(QVector3D(p.x,p.y,p.z)),cursor);}
            painter.drawEllipse(cursor,5,5);
        }
        painter.restore();
    }
    drawMeasureOverlay(painter);
    if (refPicking_ || !refMarks_.isEmpty() || !referenceListMarks_.isEmpty()) {
        // Scelta dei riferimenti di un piano di costruzione: quelli scelti e quello sotto il puntatore.
        for (const GeometryRef &mark : refMarks_) drawGeometryRef(painter, mark, kSelectionColor);
        // Le curve 3D autonome non hanno vertici di un B-rep: quando si
        // cerca un punto rendiamo espliciti i loro due estremi cliccabili.
        if (refPicking_ && (refPickRoles_ & ForgeCad::DatumRolePoint)) {
            painter.setPen(QPen(QColor(130, 215, 255, 210), 1.5));
            painter.setBrush(QColor(35, 105, 145, 210));
            for (int body = 0; body < extrusions_.size(); ++body)
                for(int entity=0;entity<qMax(1,int(extrusions_[body].curves.size()));++entity)
                for (int end = 0; end < 2; ++end) {
                    QVector3D point;
                    if (curveBodyEndpoint(body, end, point,entity)) painter.drawEllipse(projectWorldPoint(point), 4.5, 4.5);
                }
        }
        if (refPicking_ && refHoverValid_) drawGeometryRef(painter, refHover_, kHoverColor, &refHoverFace_, refHoverFaceBody_);
        for (const GeometryRef &mark : referenceListMarks_) drawGeometryRef(painter, mark, kHoverColor);
        return;
    }
    if (hover_ != selection_ && !selectedObjects_.contains(hover_)) highlightSceneObject(painter, hover_, kHoverColor);
    highlightSceneObject(painter, selection_, kSelectionColor);
    for (const SceneSelection &object : selectedObjects_)
        if (object != selection_) highlightSceneObject(painter, object, kSelectionColor);
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

QVector<QVector3D> CadViewport::planeCorners(int plane, float u, float v) {
    if (plane == 0) return {QVector3D(-u, -v, 0), QVector3D(u, -v, 0), QVector3D(u, v, 0), QVector3D(-u, v, 0)};
    if (plane == 1) return {QVector3D(-u, 0, -v), QVector3D(u, 0, -v), QVector3D(u, 0, v), QVector3D(-u, 0, v)};
    return {QVector3D(0, -u, -v), QVector3D(0, u, -v), QVector3D(0, u, v), QVector3D(0, -u, v)};
}

QVector<QVector3D> CadViewport::planeCorners(int plane, float h) { return planeCorners(plane, h, h); }

QSizeF CadViewport::referencePlaneExtents(int plane) const {
    updateSceneBounds();
    return referencePlaneAuto_[plane] * referencePlaneScales_[plane];
}

double CadViewport::referencePlaneHalf(int plane) const {
    const QSizeF size = referencePlaneExtents(plane);
    return qMax(size.width(), size.height());
}

QVector<QVector3D> CadViewport::planeCorners(int plane) const {
    const QSizeF size = referencePlaneExtents(plane);
    return planeCorners(plane, float(size.width()), float(size.height()));
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

void CadViewport::drawPlaneResizeHandles() {
    SceneSelection target;
    QVector<QVector3D> corners;
    QVector3D center;
    double half;
    if (!resizablePlane(target, corners, center, half)) return;
    QPainter painter(this);
    painter.setPen(QPen(kSelectionColor, 1.5));
    painter.setBrush(QColor(35, 45, 60));
    for (const auto &corner : corners) {
        const QPointF p = projectWorldPoint(corner);
        painter.drawRect(QRectF(p - QPointF(4, 4), QSizeF(8, 8)));
    }
}

void CadViewport::drawReferencePlanes() {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const GLfloat colors[][3] = {{0.20f, 0.55f, 0.95f}, {0.25f, 0.90f, 0.70f}, {0.95f, 0.45f, 0.25f}};
    const GLfloat baseAlpha[] = {0.08f, 0.06f, 0.06f};
    const auto emphasis = [this](int plane) {
        float extra = 0.0f;
        if (selectedPlane_ == plane) extra += 1.0f;
        if (hover_ == SceneSelection{SceneObjectKind::Plane, plane, -1}) extra += 0.8f;
        return extra;
    };
    QVector<ForgeCad::OverlayVertex> fills;
    for (int plane = 0; plane < 3; ++plane) {
        if (!isPlaneShown(plane)) continue;
        const float alpha = baseAlpha[plane] + 0.10f * emphasis(plane);
        const QVector4D color(colors[plane][0], colors[plane][1], colors[plane][2], alpha);
        const QVector<QVector3D> corners = planeCorners(plane);
        for (int corner : {0, 1, 2, 0, 2, 3}) fills.push_back({corners.at(corner), color});
    }
    overlayRenderer_.draw(GL_TRIANGLES, fills);
    // Bordo: un alone largo e tenue del colore del piano, schiarito, e sopra
    // una linea sottile piu' luminosa dello stesso colore.
    const struct { GLfloat width, alpha, light; } strokes[] = {{4.0f, 0.10f, 0.25f}, {1.3f, 0.55f, 0.40f}};
    for (const auto &stroke : strokes) {
        glLineWidth(stroke.width);
        for (int plane = 0; plane < 3; ++plane) {
            if (!isPlaneShown(plane)) continue;
            const float alpha = qMin(1.0f, stroke.alpha * (1.0f + 0.6f * emphasis(plane)));
            const GLfloat *c = colors[plane];
            const QVector4D color(c[0] + (1.0f - c[0]) * stroke.light, c[1] + (1.0f - c[1]) * stroke.light,
                                  c[2] + (1.0f - c[2]) * stroke.light, alpha);
            QVector<QVector3D> corners = planeCorners(plane);
            corners.push_back(corners.front());
            overlayRenderer_.draw(GL_LINE_STRIP, corners, color);
        }
    }
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

double CadViewport::datumHalf(double size) const { return size > 0.0 ? size : 0.5 * double(planeHalf()); }

QVector<QVector3D> CadViewport::datumCorners(const SketchFrame &f, double size) const {
    const QVector3D c(float(f.origin[0]), float(f.origin[1]), float(f.origin[2]));
    const QVector3D x(float(f.xAxis[0]), float(f.xAxis[1]), float(f.xAxis[2]));
    const QVector3D n(float(f.normal[0]), float(f.normal[1]), float(f.normal[2]));
    const QVector3D y = QVector3D::crossProduct(n, x);
    const float h = float(datumHalf(size));
    return {c - h * x - h * y, c + h * x - h * y, c + h * x + h * y, c - h * x + h * y};
}

bool CadViewport::isDatumShown(int index) const {
    if (index < 0 || index >= extrusions_.size()) return false;
    const ExtrusionObject &body = extrusions_.at(index);
    return ForgeCad::isDatumBody(body) && body.visible && body.datumValid && index != datumPreviewIndex_;
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

void CadViewport::drawDatumQuad(const QVector<QVector3D> &corners, const GLfloat *color, float emphasis) {
    QVector<QVector3D> fill;
    for (int corner : {0, 1, 2, 0, 2, 3}) fill.push_back(corners.at(corner));
    overlayRenderer_.draw(GL_TRIANGLES, fill, QVector4D(color[0], color[1], color[2], 0.07f + 0.10f * emphasis));
    const struct { GLfloat width, alpha, light; } strokes[] = {{4.0f, 0.12f, 0.25f}, {1.4f, 0.65f, 0.40f}};
    for (const auto &stroke : strokes) {
        glLineWidth(stroke.width);
        const QVector4D strokeColor(color[0] + (1.0f - color[0]) * stroke.light,
                                    color[1] + (1.0f - color[1]) * stroke.light,
                                    color[2] + (1.0f - color[2]) * stroke.light,
                                    qMin(1.0f, stroke.alpha * (1.0f + 0.6f * emphasis)));
        QVector<QVector3D> outline = corners;
        outline.push_back(outline.front());
        overlayRenderer_.draw(GL_LINE_STRIP, outline, strokeColor);
    }
    glLineWidth(1.0f);
}

void CadViewport::drawDatumPlanes() {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    static const GLfloat datumColor[] = {0.72f, 0.52f, 0.98f}, previewColor[] = {0.98f, 0.66f, 0.28f};
    for (int index = 0; index < extrusions_.size(); ++index) {
        if (!isDatumShown(index)) continue;
        const ExtrusionObject &body = extrusions_.at(index);
        const SceneSelection self{SceneObjectKind::Extrusion, index, -1};
        const float emphasis = (selection_ == self ? 1.0f : 0.0f) + (hover_ == self ? 0.8f : 0.0f);
        drawDatumQuad(datumCorners(body.datumFrame, body.datum.size), datumColor, emphasis);
    }
    if (datumPreviewValid_) drawDatumQuad(datumCorners(datumPreviewFrame_, datumPreviewSize_), previewColor, 1.0f);
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void CadViewport::drawDatumLabels(QPainter &painter) const {
    painter.setFont(uiFont(9, QFont::DemiBold));
    painter.setPen(QColor(205, 180, 255));
    for (int index = 0; index < extrusions_.size(); ++index) {
        if (!isDatumShown(index)) continue;
        const ExtrusionObject &body = extrusions_.at(index);
        const QVector<QVector3D> corners = datumCorners(body.datumFrame, body.datum.size);
        const QVector3D c(float(body.datumFrame.origin[0]), float(body.datumFrame.origin[1]), float(body.datumFrame.origin[2]));
        painter.drawText(projectWorldPoint(c + 0.8f * (corners.at(2) - c)), body.name);
    }
}

bool CadViewport::referenceBodyEligible(int index) const {
    const int owner = refPickOwner_ < 0 ? int(extrusions_.size()) : refPickOwner_;
    if (!pickBodies_.isEmpty())
        return pickBodies_.contains(index) && index < owner && index < extrusions_.size() && ForgeCad::isShapeBody(extrusions_.at(index));
    return index >= 0 && index < owner && index < extrusions_.size() && extrusions_.at(index).visible && ForgeCad::isShapeBody(extrusions_.at(index));
}

bool CadViewport::referenceSketchEligible(int index) const {
    if (index < 0 || index >= sketches_.size() || !sketches_.at(index).visible) return false;
    const int datum = sketches_.at(index).datumPlane;
    return datum < 0 || refPickOwner_ < 0 || datum < refPickOwner_;
}

bool CadViewport::curveBodyEndpoint(int index, int end, QVector3D &point, int entity) const {
    const int owner = refPickOwner_ < 0 ? int(extrusions_.size()) : refPickOwner_;
    if (index < 0 || index >= owner || index >= extrusions_.size()) return false;
    const ExtrusionObject &body = extrusions_.at(index);
    if (!body.visible || !ForgeCad::isCurveBody(body) || !body.curve) return false;
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
            } else if (b < owner && body.visible && ForgeCad::isCurveBody(body) && body.curve) {
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

void CadViewport::drawGeometryRef(QPainter &painter, const GeometryRef &ref, const QColor &color, const FaceHit *face , int faceBody ) const {
    const auto dot = [&](const QVector3D &p) {
        const QPointF q = projectWorldPoint(p);
        painter.setPen(QPen(color, 2.0));
        painter.setBrush(color);
        painter.drawEllipse(q, 5.0, 5.0);
    };
    const QVector3D point(float(ref.point.x), float(ref.point.y), float(ref.point.z));
    switch (ref.kind) {
    case 0: dot(QVector3D()); break;
    case 1:
        if (ref.index >= 0 && ref.index < 3) {
            QVector<QPointF> corners;
            for (const QVector3D &corner : planeCorners(ref.index)) corners.append(projectWorldPoint(corner));
            strokeHighlight(painter, corners, true, color);
        }
        break;
    case 2: {
        QVector3D tip;
        tip[qBound(0, ref.index, 2)] = float(axisDisplayLength());
        strokeHighlight(painter, {projectWorldPoint(QVector3D()), projectWorldPoint(tip)}, false, color);
        break;
    }
    case 3: dot(point); break;
    case 4: {
        FaceHit points;
        points.edges = {ref.point};
        for (int e : faceDisplayEdges(ref.index, points)) {
            QVector<QPointF> projected;
            for (const QVector3D &p : extrusions_.at(ref.index).display.edges.at(e)) projected.append(projectWorldPoint(p));
            strokeHighlight(painter, projected, false, color);
        }
        break;
    }
    case 5: {
        // La faccia sotto il puntatore, oppure (riferimento gia' scelto) quella
        // ritrovata come nella risoluzione: l'indice salvato puo' essere vecchio.
        FaceHit resolved;
        const FaceHit *edgesOf = face && faceBody == ref.index ? face : nullptr;
        if (!face && ref.index >= 0 && ref.index < extrusions_.size() && extrusions_.at(ref.index).forgeBody) {
            resolved.face = ForgeCad::resolveFaceReference(*extrusions_.at(ref.index).forgeBody, ref.point,
                                                           std::numeric_limits<double>::max()).index;
            edgesOf = &resolved;
        }
        if (edgesOf)
            for (int e : faceDisplayEdges(ref.index, *edgesOf)) {
                QVector<QPointF> projected;
                for (const QVector3D &p : extrusions_.at(ref.index).display.edges.at(e)) projected.append(projectWorldPoint(p));
                strokeHighlight(painter, projected, false, color);
            }
        dot(point);
        break;
    }
    case 6:
        if (ref.index >= 0 && ref.index < sketches_.size()) {
            const SketchObject &sketch = sketches_.at(ref.index);
            const ConstraintRef &e = ref.element;
            if (e.kind == 0 && e.element >= 0 && e.element < sketch.segments.size())
                dot(mapSketchPoint(e.point == 0 ? sketch.segments.at(e.element).first : sketch.segments.at(e.element).second, sketch));
            else if (e.kind == 1 && e.element >= 0 && e.element < sketch.curves.size() && e.point >= 0 && e.point < sketch.curves.at(e.element).controlPoints.size())
                dot(mapSketchPoint(sketch.curves.at(e.element).controlPoints.at(e.point), sketch));
        }
        break;
    case 7:
        if (ref.index >= 0 && ref.index < sketches_.size()) {
            const SketchObject &sketch = sketches_.at(ref.index);
            if (ref.element.kind < 0) {
                for (int i = 0; i < sketch.segments.size(); ++i) highlightSketchElement(painter, sketch, {0, i}, color);
                for (int i = 0; i < sketch.curves.size(); ++i) highlightSketchElement(painter, sketch, {1, i}, color);
            } else highlightSketchElement(painter, sketch, {ref.element.kind, ref.element.element}, color);
        }
        break;
    case 8:
        if (ref.index >= 0 && ref.index < extrusions_.size() && extrusions_.at(ref.index).datumValid) {
            QVector<QPointF> corners;
            for (const QVector3D &corner : datumCorners(extrusions_.at(ref.index).datumFrame, extrusions_.at(ref.index).datum.size))
                corners.append(projectWorldPoint(corner));
            strokeHighlight(painter, corners, true, color);
        }
        break;
    case 9:
        if (ref.index >= 0 && ref.index < extrusions_.size())
            for (const QVector<QVector3D> &polyline : extrusions_.at(ref.index).display.edges) {
                QVector<QPointF> projected;
                for (const QVector3D &p : polyline) projected.append(projectWorldPoint(p));
                strokeHighlight(painter, projected, false, color);
            }
        break;
    case 10: {
        QVector3D endpoint;
        if (snapRefPosition(ref, endpoint)) dot(endpoint);
        else dot(point);
        break;
    }
    case ForgeCad::kGeometryRefEdgeMidpoint:
    case ForgeCad::kGeometryRefEdgeCenter: {
        QVector3D p = point;
        if (snapRefPosition(ref, p)) {
            const bool center = ref.kind == ForgeCad::kGeometryRefEdgeCenter;
            dot(p);
            const QPointF q = projectWorldPoint(p);
            painter.setPen(color);
            painter.drawText(q + QPointF(9.0, -7.0), center ? QStringLiteral("⊙ Centro") : QStringLiteral("◇ Punto medio"));
        }
        break;
    }
    default: break;
    }
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



void CadViewport::configureLighting() {
    ForgeCad::DisplayCache::Lighting lighting;
    lighting.ambient = QVector4D(0.20f, 0.22f, 0.26f, 1.0f);
    // Luci puntiformi fuori dalla scena, legate alla vista: la principale in
    // alto a destra davanti all'osservatore, quella di riempimento a
    // sinistra; dal centro della geometria visibile a tre volte il raggio
    // della sfera che la contiene (almeno quello dei piani di riferimento).
    // Seguono i modelli grandi, non finiscono mai dentro i solidi e
    // illuminano sempre le facce verso chi guarda, in qualsiasi orientamento.
    updateSceneBounds();
    const QVector3D center = 0.5f * (sceneMin_ + sceneMax_);
    const float reach = 3.0f * qMax(0.5f * (sceneMax_ - sceneMin_).length(), 4.0f);
    const QVector3D centerEye = sceneModelViewMatrix().map(center);
    const QVector3D key = centerEye + reach * QVector3D(0.5f, 0.7f, 1.0f).normalized();
    const QVector3D fill = centerEye + reach * QVector3D(-0.6f, 0.3f, 0.5f).normalized();
    lighting.positions[0] = QVector4D(key, 1.0f);
    lighting.positions[1] = QVector4D(fill, 1.0f);
    // Con la luce ambiente attiva le luci principali cedono parte del loro
    // contributo allo sfondo, per non sovraesporre gli oggetti.
    const float keyScale = background_.affectsLighting
        ? 1.0f - 0.35f * qBound(0.0f, background_.lightingStrength, 1.0f) : 1.0f;
    const QVector4D keySource = lightingPreset_ == 2 ? QVector4D(0.80f, 1.0f, 0.88f, 1.0f)
                                                      : QVector4D(1.0f, 0.92f, 0.80f, 1.0f);
    lighting.colors[0] = QVector4D(keySource.x() * keyScale, keySource.y() * keyScale, keySource.z() * keyScale, 1.0f);
    lighting.colors[1] = QVector4D(0.60f * keyScale, 0.76f * keyScale, 1.0f * keyScale, 1.0f);
    lighting.enabled[0] = 1;
    lighting.enabled[1] = lightingPreset_ == 1 ? 0 : 1;
    lighting.shininess = lightingPreset_ == 1 ? 36.0f : 110.0f;
    configureEnvironmentLighting(lighting);
    displayCache_.setLighting(lighting);
}

void CadViewport::configureEnvironmentLighting(ForgeCad::DisplayCache::Lighting &lighting) {
    if (!background_.affectsLighting) return;
    const float strength = qBound(0.0f, background_.lightingStrength, 1.0f);
    const QColor start = background_.startColor;
    const QColor end = background_.gradient ? background_.endColor : start;
    lighting.ambient = QVector4D(
        lighting.ambient.x() * (1.0f - 0.5f * strength) + float(start.redF() + end.redF()) * 0.25f * strength,
        lighting.ambient.y() * (1.0f - 0.5f * strength) + float(start.greenF() + end.greenF()) * 0.25f * strength,
        lighting.ambient.z() * (1.0f - 0.5f * strength) + float(start.blueF() + end.blueF()) * 0.25f * strength, 1.0f);
    const float radians = background_.angle * float(M_PI) / 180.0f;
    const float dx = std::cos(radians), dy = std::sin(radians);
    const float diffuse = 0.55f * strength;
    lighting.positions[2] = QVector4D(-dx, -dy, 0.6f, 0.0f);
    lighting.positions[3] = QVector4D(dx, dy, 0.6f, 0.0f);
    lighting.colors[2] = QVector4D(float(start.redF()) * diffuse, float(start.greenF()) * diffuse,
                                   float(start.blueF()) * diffuse, 1.0f);
    lighting.colors[3] = QVector4D(float(end.redF()) * diffuse, float(end.greenF()) * diffuse,
                                   float(end.blueF()) * diffuse, 1.0f);
    lighting.enabled[2] = lighting.enabled[3] = 1;
}

bool CadViewport::isSketchDrawn(int index) const {
    return sketches_.at(index).visible || (sketchMode_ && index == activeSketch_);
}

void CadViewport::drawSketch() {
    // In modalita' schizzo gli schizzi stanno sopra i corpi: anche le entita'
    // dentro un solido (sezioni) o sugli spigoli (riferimenti) si vedono.
    if (sketchMode_) glDisable(GL_DEPTH_TEST);
    // Il vecchio stipple dello shader usava x+y del pixel: sulle linee
    // inclinate a -45 gradi la coordinata resta costante e l'intera linea
    // poteva cadere in un intervallo vuoto. Costruiamo invece i trattini
    // lungo la polilinea, in pixel, indipendentemente dall'orientamento.
    const auto drawDashedPolyline = [this](const QVector<QVector3D> &points, const QVector4D &color,
                                           double dashPixels = 7.0, double gapPixels = 5.0) {
        if (points.size() < 2) return;
        QVector<QVector3D> dashes;
        bool drawing = true;
        double remaining = dashPixels;
        for (int k = 1; k < points.size(); ++k) {
            const QVector3D a = points.at(k - 1), b = points.at(k);
            const double pixels = pointDistance(projectWorldPoint(a), projectWorldPoint(b));
            if (!(pixels > 1e-9)) continue;
            double along = 0.0;
            while (along < pixels - 1e-9) {
                const double step = qMin(remaining, pixels - along);
                if (drawing) {
                    dashes.append(a + float(along / pixels) * (b - a));
                    dashes.append(a + float((along + step) / pixels) * (b - a));
                }
                along += step;
                remaining -= step;
                if (remaining <= 1e-9) {
                    drawing = !drawing;
                    remaining = drawing ? dashPixels : gapPixels;
                }
            }
        }
        overlayRenderer_.setStipple(false);
        overlayRenderer_.draw(GL_LINES, dashes, color);
    };
    glLineWidth(2.0f);
    const double markerSize = pickTolerance(7.0);
    QVector<QMatrix4x4> segmentInstances[2];  // definito, normale
    QVector<QVector3D> constructionLines, axisLines;
    QVector<QVector3D> constructionMarkers;
    const auto lineTransform = [](const QVector3D &first, const QVector3D &second) {
        const QVector3D delta = second - first;
        return QMatrix4x4(delta.x(), 0.0f, 0.0f, first.x(),
                          delta.y(), 0.0f, 0.0f, first.y(),
                          delta.z(), 0.0f, 0.0f, first.z(),
                          0.0f,      0.0f, 0.0f, 1.0f);
    };
    // Nello schizzo attivo le entita' completamente definite sono bianche (come in SolidWorks, dove sono nere).
    const ForgeCad::SketchAnalysis *analysis = sketchMode_ && activeSketchObject() ? &sketchAnalysis() : nullptr;
    for (int sketchIndex = 0; sketchIndex < sketches_.size(); ++sketchIndex) {
        if (!isSketchDrawn(sketchIndex)) continue;
        const SketchObject &sketch = sketches_.at(sketchIndex);
        const bool active = analysis && sketchIndex == activeSketch_;
        for (int index = 0; index < sketch.segments.size(); ++index) {
            const auto &segment = sketch.segments.at(index);
            const bool construction = sketch.isConstructionSegment(index);
            const QVector3D first = mapSketchPoint(segment.first, sketch);
            const QVector3D second = mapSketchPoint(segment.second, sketch);
            if (pointDistance(segment.first, segment.second) <= ForgeCad::kSketchConnectionTolerance && construction) {
                for (const QPointF &arm : {QPointF(markerSize, 0.0), QPointF(0.0, markerSize)}) {
                    const QVector3D a = mapSketchPoint(segment.first - arm, sketch), b = mapSketchPoint(segment.first + arm, sketch);
                    constructionMarkers.push_back(a);
                    constructionMarkers.push_back(b);
                }
                continue;
            }
            if (sketch.symmetryAxes.contains(index)) axisLines += QVector<QVector3D>{first, second};
            else if (construction) constructionLines += QVector<QVector3D>{first, second};
            else {
                const int style = active && analysis->segmentDefined.value(index) ? 0 : 1;
                segmentInstances[style].push_back(lineTransform(first, second));
            }
        }
    }
    const QVector<QVector3D> unitLine{QVector3D(0, 0, 0), QVector3D(1, 0, 0)};
    const QVector4D definedColor(0.94f, 0.96f, 1.0f, 1.0f), normalColor(1.0f, 0.75f, 0.15f, 1.0f);
    overlayRenderer_.setStipple(false);
    overlayRenderer_.drawInstanced(GL_LINES, unitLine, definedColor, segmentInstances[0]);
    overlayRenderer_.drawInstanced(GL_LINES, unitLine, normalColor, segmentInstances[1]);
    for (int k = 0; k + 1 < constructionLines.size(); k += 2)
        drawDashedPolyline({constructionLines.at(k), constructionLines.at(k + 1)}, QVector4D(0.55f, 0.78f, 0.92f, 1.0f));
    for (int k = 0; k + 1 < axisLines.size(); k += 2)
        drawDashedPolyline({axisLines.at(k), axisLines.at(k + 1)}, QVector4D(0.35f, 0.92f, 0.82f, 1.0f), 11.0, 4.0);
    overlayRenderer_.setStipple(false);
    overlayRenderer_.draw(GL_LINES, constructionMarkers, QVector4D(0.35f, 0.92f, 0.92f, 1.0f));
    // Lo stato resta pieno per tutti gli overlay disegnati dopo gli schizzi.
    overlayRenderer_.setStipple(false);
    glLineWidth(2.5f);
    for (int sketchIndex = 0; sketchIndex < sketches_.size(); ++sketchIndex) {
        if (!isSketchDrawn(sketchIndex)) continue;
        const SketchObject &sketch = sketches_.at(sketchIndex);
        const bool active = analysis && sketchIndex == activeSketch_;
        for (int curveIndex = 0; curveIndex < sketch.curves.size(); ++curveIndex) {
            const CurveObject &curve = sketch.curves.at(curveIndex);
            QVector4D color(0.55f, 0.78f, 0.92f, 1.0f);
            if (!curve.construction) {
                if (active && analysis->curveDefined.value(curveIndex)) color = QVector4D(0.94f, 0.96f, 1.0f, 1.0f);
                else color = QVector4D(curve.tool == DrawingTool::Nurbs ? 0.85f : 0.95f,
                                       curve.tool == DrawingTool::Nurbs ? 0.35f : 0.65f, 1.0f, 1.0f);
            }
            QVector<QVector3D> samples;
            samples.reserve(curve.samples.size());
            for (const QPointF &sample : curve.samples) {
                const QVector3D world = mapSketchPoint(sample, sketch);
                samples.push_back(world);
            }
            if (curve.construction) drawDashedPolyline(samples, color);
            else if (curve.tool == DrawingTool::Spline) overlayRenderer_.drawWideLineStrip(samples, color, 3.5f);
            else overlayRenderer_.draw(GL_LINE_STRIP, samples, color);
            // Le spline interpolano i nodi: la spezzata fra i nodi non e' una guida tangente.
            if (curve.construction || curve.tool == DrawingTool::Converted || curve.tool == DrawingTool::Spline || curve.tool == DrawingTool::Circle) continue;
            QVector<QVector3D> controls;
            controls.reserve(curve.controlPoints.size());
            for (const QPointF &control : curve.controlPoints) {
                const QVector3D world = mapSketchPoint(control, sketch);
                controls.push_back(world);
            }
            overlayRenderer_.draw(GL_LINE_STRIP, controls, QVector4D(0.75f, 0.75f, 0.80f, 1.0f));
        }
    }
    // Se una guida della loft non attraversa una sezione, la sezione viene
    // ridisegnata sopra il modello in rosso, anche se lo schizzo era
    // nascosto. Scompare appena l'anteprima torna valida o la finestra si chiude.
    if (previewErrorSketch_ >= 0 && previewErrorSketch_ < sketches_.size()) {
        const SketchObject &failed = sketches_.at(previewErrorSketch_);
        glDisable(GL_DEPTH_TEST);
        overlayRenderer_.setStipple(false);
        glLineWidth(6.0f);
        for (const SketchSegment &segment : failed.segments) {
            const QVector3D a = mapSketchPoint(segment.first, failed), b = mapSketchPoint(segment.second, failed);
            overlayRenderer_.draw(GL_LINES, QVector<QVector3D>{a, b}, QVector4D(1.0f, 0.16f, 0.12f, 1.0f));
        }
        for (const CurveObject &curve : failed.curves) {
            QVector<QVector3D> samples;
            for (const QPointF &sample : curve.samples) {
                samples.push_back(mapSketchPoint(sample, failed));
            }
            overlayRenderer_.draw(GL_LINE_STRIP, samples, QVector4D(1.0f, 0.16f, 0.12f, 1.0f));
        }
        glLineWidth(2.0f);
        for (const SketchSegment &segment : failed.segments) {
            const QVector3D a = mapSketchPoint(segment.first, failed), b = mapSketchPoint(segment.second, failed);
            overlayRenderer_.draw(GL_LINES, QVector<QVector3D>{a, b}, QVector4D(1.0f, 0.85f, 0.20f, 1.0f));
        }
        for (const CurveObject &curve : failed.curves) {
            QVector<QVector3D> samples;
            for (const QPointF &sample : curve.samples) {
                samples.push_back(mapSketchPoint(sample, failed));
            }
            overlayRenderer_.draw(GL_LINE_STRIP, samples, QVector4D(1.0f, 0.85f, 0.20f, 1.0f));
        }
    }
    const double cursorMarkerSize = pickTolerance(10.0);
    QVector<ForgeCad::OverlayVertex> cursorLines;
    const auto cursorLine = [&cursorLines](const QVector3D &a, const QVector3D &b, const QVector4D &color) {
        cursorLines.push_back({a, color}); cursorLines.push_back({b, color});
    };
    if (hasPendingPoint_) {
        const QVector3D world = mapActiveSketchPoint(pendingPoint_);
        const QVector3D left = mapActiveSketchPoint(pendingPoint_ - QPointF(cursorMarkerSize, 0.0));
        const QVector3D right = mapActiveSketchPoint(pendingPoint_ + QPointF(cursorMarkerSize, 0.0));
        cursorLine(left, right, QVector4D(1.0f, 0.35f, 0.20f, 1.0f));
        if (drawingTool_ == DrawingTool::Line || drawingTool_ == DrawingTool::Polyline
            || drawingTool_ == DrawingTool::ConstructionLine) {
            const QPointF previewPoint = currentInference_.point;
            const QVector3D previewWorld = mapActiveSketchPoint(previewPoint);
            cursorLine(world, previewWorld, QVector4D(1.0f, 0.45f, 0.20f, 1.0f));
        }
    } else if (sketchMode_ && (drawingTool_ == DrawingTool::Line || drawingTool_ == DrawingTool::ConstructionLine
                               || drawingTool_ == DrawingTool::Polyline)) {
        // Croce del cursore nel piano dello schizzo, di dimensione fissa in pixel.
        for (const QPointF &arm : {QPointF(cursorMarkerSize, 0.0), QPointF(0.0, cursorMarkerSize)}) {
            const QVector3D a = mapActiveSketchPoint(cursorSketchPoint_ - arm);
            const QVector3D b = mapActiveSketchPoint(cursorSketchPoint_ + arm);
            cursorLine(a, b, QVector4D(1.0f, 0.45f, 0.20f, 1.0f));
        }
    }
    overlayRenderer_.draw(GL_LINES, cursorLines);
    glEnable(GL_DEPTH_TEST);
}

void CadViewport::drawExtrusionFaces(const ExtrusionObject &extrusion) { drawDisplayFaces(extrusion.display); }

void CadViewport::drawDisplayFaces(const BodyDisplay &display) {
    displayCache_.displayFaces(display);
}

void CadViewport::drawExtrusionEdges(const ExtrusionObject &extrusion) {
    displayCache_.displayEdges(extrusion.display);
}

void CadViewport::drawThreadProfileEdges(const BodyDisplay &display, const QVector4D &color) {
    sceneRenderer_.drawThreadProfileEdges(overlayRenderer_, display, color);
}

void CadViewport::drawExtrusionOutline(const ExtrusionObject &extrusion, const QColor &color, float width) {
    displayCache_.outline(extrusion.display, ForgeCad::isCurveBody(extrusion), color, width);
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
    if (index < 0 || index >= extrusions_.size()) return false;
    const ExtrusionObject &body = extrusions_.at(index);
    if (!ForgeCad::isShapeBody(body) || (!body.visible && index != edgePickBody_)) return false;
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

void CadViewport::drawFeatureHighlightFaces(int index) {
    const auto faces = [this](const BodyDisplay &display, bool whole) {
        const QColor &color = featureHighlightColor_;
        displayCache_.setLightingEnabled(true);
        displayCache_.setColor(QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f));
        displayCache_.setEmission(QVector4D(float(color.redF()) * 0.25f, float(color.greenF()) * 0.25f, float(color.blueF()) * 0.25f, 1.0f));
        // Le facce dei corpi sono spostate indietro (glPolygonOffset 1, 2):
        // senza spostamento l'evidenziazione sta sempre davanti alle facce
        // da cui viene, e senza scrivere la profondita' gli spigoli restano visibili.
        glDepthMask(GL_FALSE);
        if (whole) drawDisplayFaces(display);  // anche le ripetizioni instanziate
        else displayCache_.faces(display);
        glDepthMask(GL_TRUE);
        displayCache_.setEmission(QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
    };
    if (index == bodyHighlight_) faces(extrusions_.at(index).display, true);
    for (const FeatureHighlight &highlight : featureHighlights_)
        if (highlight.body == index && !highlight.display.vertices.isEmpty()) faces(highlight.display, false);
}

void CadViewport::drawFeatureHighlightEdges(int index) {
    const QColor edge = featureHighlightColor_.lighter(120);
    const auto edges = [&](const auto &draw) {
        displayCache_.setLightingEnabled(false);
        displayCache_.setColor(QVector4D(float(edge.redF()), float(edge.greenF()), float(edge.blueF()), 1.0f));
        glLineWidth(3.0f);
        draw();
        glLineWidth(1.0f);
    };
    if (index == bodyHighlight_) edges([&] { drawExtrusionEdges(extrusions_.at(index)); });
    for (const FeatureHighlight &highlight : featureHighlights_)
        if (highlight.body == index && !highlight.display.edges.isEmpty()) edges([&] { displayCache_.edges(highlight.display); });
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

void CadViewport::drawSelectedFace() {
    if (edgePicking_ || sketchMode_ || selectedFace_.body < 0 || selectedFace_.body >= extrusions_.size()) return;
    const QVector<QVector<QVector3D>> &edges = extrusions_.at(selectedFace_.body).display.edges;
    glDisable(GL_DEPTH_TEST);
    glLineWidth(3.5f);
    for (int index : faceDisplayEdges(selectedFace_.body, selectedFace_.hit)) {
        overlayRenderer_.draw(GL_LINE_STRIP, edges.at(index),
                              QVector4D(float(kSelectionColor.redF()), float(kSelectionColor.greenF()),
                                        float(kSelectionColor.blueF()), 1.0f));
    }
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}

void CadViewport::drawTopologyHighlight() {
    if (!topologyIdsVisible_ || topologyHighlightBody_ < 0 || topologyHighlightBody_ >= extrusions_.size()) return;
    const BodyDisplay &display = extrusions_.at(topologyHighlightBody_).display;
    QVector<int> indices;
    QColor color;
    if (topologyHighlightEdge_ >= 0) {
        const int index = display.edgeIds.indexOf(topologyHighlightEdge_);
        if (index >= 0) indices.append(index);
        color = QColor(90, 245, 255);
    } else if (topologyHighlightFace_ >= 0 && topologyHighlightFace_ < display.faceEdges.size()) {
        indices = display.faceEdges.at(topologyHighlightFace_);
        color = QColor(255, 220, 70);
    }
    if (indices.isEmpty()) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    const QVector4D halo(0.03f, 0.05f, 0.07f, 1.0f);
    const QVector4D bright(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f);
    for (int index : indices)
        if (index >= 0 && index < display.edges.size())
            overlayRenderer_.drawWideLineStrip(display.edges.at(index), halo, 6.0f * float(devicePixelRatioF()));
    for (int index : indices)
        if (index >= 0 && index < display.edges.size())
            overlayRenderer_.drawWideLineStrip(display.edges.at(index), bright, 3.5f * float(devicePixelRatioF()));
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void CadViewport::drawPickedEdges() {
    drawSelectedFace();
    if (!edgePicking_) return;
    // I candidati rispettano la profondita' (a meno della modalita' X-ray),
    // cosi' la scena non si riempie di bordi posteriori. Gli spigoli gia'
    // scelti vengono invece sovrapposti nell'ultimo passaggio: durante il
    // calcolo o il fallimento di un'anteprima la base viene ridisegnata e
    // il suo depth buffer non deve far sparire la conferma della scelta.
    if (hiddenEdgesVisible_) glDisable(GL_DEPTH_TEST);
    else glEnable(GL_DEPTH_TEST);
    auto draw = [&](int body, int index, const QColor &color, float width, bool reliableWidth = false) {
        if (body < 0 || body >= extrusions_.size()) return;
        const QVector<QVector<QVector3D>> &edges = extrusions_.at(body).display.edges;
        if (index < 0 || index >= edges.size()) return;
        const QVector4D rgba(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f);
        if (reliableWidth)
            overlayRenderer_.drawWideLineStrip(edges.at(index), rgba, width * float(devicePixelRatioF()));
        else {
            glLineWidth(width);
            overlayRenderer_.draw(GL_LINE_STRIP, edges.at(index), rgba);
        }
    };
    if (edgePickBody_ >= 0 && edgePickBody_ < extrusions_.size())
        for (int index = 0; index < extrusions_.at(edgePickBody_).display.edges.size(); ++index) draw(edgePickBody_, index, QColor(120, 140, 160), 1.5f);
    for (int index : hoverFaceEdges_) draw(hoverEdgeBody_, index, kHoverColor, 3.0f);

    // Hover e selezione sono conferme dell'interazione, non geometria della
    // scena: si disegnano opachi, senza profondita', con un alone scuro che
    // li separa sia dal corpo sia dall'anteprima semitrasparente. Il picking
    // ha gia' escluso gli spigoli nascosti quando la modalita' X-ray e' spenta.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    const QColor edgeHalo(8, 12, 18);
    const bool hoveredSelected = hoverEdgeBody_ == edgePickBody_ && pickedEdges_.contains(hoverEdge_);
    if (hoverEdge_ >= 0 && !hoveredSelected) {
        draw(hoverEdgeBody_, hoverEdge_, edgeHalo, 4.25f, true);
        draw(hoverEdgeBody_, hoverEdge_, kHoverColor, 2.75f, true);
    }
    for (int index : pickedEdges_) draw(edgePickBody_, index, edgeHalo, 5.0f, true);
    for (int index : pickedEdges_) draw(edgePickBody_, index, kSelectionColor, 3.25f, true);
    glDepthMask(GL_TRUE);
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
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

void CadViewport::drawSectionCaps(const QVector<int> &solids) {
    if (!section_.enabled || !section_.caps || solids.isEmpty()) return;
    const QVector<QVector3D> quad = sectionQuad(1.5f);
    QVector<const BodyDisplay *> displays;
    displays.reserve(solids.size());
    for (int index : solids) displays.append(&extrusions_.at(index).display);
    const QVector3D n = section_.normal;
    const QVector4D plane(-n.x(), -n.y(), -n.z(), QVector3D::dotProduct(n, section_.point));
    sceneRenderer_.drawSectionCaps(displayCache_, overlayRenderer_, displays, quad, plane);
}

void CadViewport::drawSectionPlane() {
    if (!section_.enabled || !section_.showPlane) return;
    sceneRenderer_.drawSectionPlane(overlayRenderer_, sectionQuad(), sectionHover_ || sectionDragging_);
}

void CadViewport::drawExtrusions() {
    glDisable(GL_CULL_FACE);
    overlayRenderer_.setStipple(false);
    enableSectionClip();
    QVector<int> sectionSolids;
    const bool previewing = !preview_.key.isEmpty();
    for (int index = 0; index < extrusions_.size(); ++index) {
        const ExtrusionObject &extrusion = extrusions_.at(index);
        // Con un'anteprima il corpo modificato non si vede. Per raccordi e
        // smussi la base resta invece opaca sotto la sola patch locale,
        // anche quando e' un operando nascosto di una lavorazione esistente.
        const bool replaced = previewing && preview_.replaced.contains(index);
        const bool blendBase = previewing && preview_.definition.operation < 0
            && preview_.definition.feature == BodyFeature::Blend
            && preview_.definition.firstBody == index;
        if (previewing && (index == preview_.index || preview_.later.contains(index) || (replaced && (preview_.valid || previewDragVisible_)))) continue;
        // Scelta dei corpi da una finestra: si vedono solo quelli che si possono scegliere.
        if (!pickBodies_.isEmpty() && !pickBodies_.contains(index)) continue;
        const bool forced = replaced || blendBase || index == edgePickBody_ || (previewing && preview_.restored.contains(index))
                         || pickBodies_.contains(index);
        if ((!extrusion.visible && !forced) || (extrusion.display.vertices.isEmpty() && extrusion.display.edges.isEmpty())) continue;
        const SceneSelection self{SceneObjectKind::Extrusion, index, -1};
        const bool hovered = hover_ == self;
        if (displayMode_ != 0 || index == shapeAnalysisBody_) {
            displayCache_.setLightingEnabled(true);
            QColor color = meshColorForFeature(index);
            QVector4D emission(0.0f, 0.0f, 0.0f, 1.0f);
            if (hovered) {
                // L'oggetto sotto il puntatore si "accende".
                color = QColor::fromRgbF(color.redF() * 0.75 + kHoverColor.redF() * 0.25,
                                         color.greenF() * 0.75 + kHoverColor.greenF() * 0.25,
                                         color.blueF() * 0.75 + kHoverColor.blueF() * 0.25);
                emission = QVector4D(float(kHoverColor.redF()) * 0.14f,
                                     float(kHoverColor.greenF()) * 0.14f,
                                     float(kHoverColor.blueF()) * 0.14f, 1.0f);
            }
            // In modalita' schizzo le facce sono trasparenti (opacita' regolabile)
            // e non scrivono la profondita': lo schizzo dietro i corpi resta leggibile.
            const bool seeThrough = sketchMode_ && sketchBodyOpacity_ < 0.999;
            displayCache_.setColor(QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()),
                                             seeThrough ? float(sketchBodyOpacity_) : 1.0f));
            displayCache_.setEmission(emission);
            if (seeThrough) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            // Le facce vanno un poco indietro nella profondita': spigoli e
            // schizzi che stanno sulla faccia si vedono senza spostarli.
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 2.0f);
            const bool analyzing = index == shapeAnalysisBody_;
            const bool faceOnly = analyzing && shapeAnalysisFace_>=0;
            const bool colorMap = analyzing && (shapeAnalysisMode_==1 || shapeAnalysisMode_==2);
            displayCache_.setZebra(analyzing && !faceOnly && shapeAnalysisMode_==0, zebraFrequency_, zebraAngle_);
            if (!colorMap || faceOnly) drawExtrusionFaces(extrusion);
            displayCache_.setZebra(false,zebraFrequency_,zebraAngle_);
            if (faceOnly) glPolygonOffset(0.0f,0.0f);
            if (colorMap) overlayRenderer_.draw(GL_TRIANGLES,shapeAnalysis_.triangles);
            else if (faceOnly && shapeAnalysisMode_==0) {
                displayCache_.setZebra(true,zebraFrequency_,zebraAngle_);
                drawDisplayFaces(shapeAnalysisFaceDisplay_);
                displayCache_.setZebra(false,zebraFrequency_,zebraAngle_);
            }
            glDisable(GL_POLYGON_OFFSET_FILL);
            if (!seeThrough && !analyzing) drawFeatureHighlightFaces(index);
            if (seeThrough) {
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
            displayCache_.setEmission(QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
        }
        if (section_.enabled && extrusion.solid && displayMode_ != 0 && !extrusion.display.vertices.isEmpty()) sectionSolids.append(index);
        if (ForgeCad::isCurveBody(extrusion)) {
            // Curve (eliche): sempre, in arancio, piu' spesse sotto il puntatore.
            displayCache_.setLightingEnabled(false);
            displayCache_.setColor(hovered
                ? QVector4D(float(kHoverColor.redF()), float(kHoverColor.greenF()), float(kHoverColor.blueF()), 1.0f)
                : QVector4D(1.0f, 0.69f, 0.29f, 1.0f));
            const bool spatial=extrusion.feature==BodyFeature::SpatialSketch || extrusion.feature==BodyFeature::ExtractedCurve;
            glLineWidth(spatial ? (hovered ? 5.0f : 3.5f) : (hovered ? 3.0f : 2.2f));
            if(spatial) {
                const QVector4D color=hovered ? QVector4D(float(kHoverColor.redF()),float(kHoverColor.greenF()),float(kHoverColor.blueF()),1)
                                              : QVector4D(1,.69f,.29f,1);
                for(const auto &line:extrusion.display.edges)overlayRenderer_.drawWideLineStrip(line,color,(hovered?5.0f:3.5f)*float(devicePixelRatioF()));
            } else drawExtrusionEdges(extrusion);
            glLineWidth(1.0f);
        } else if (displayMode_ != 1) {
            displayCache_.setLightingEnabled(false);
            displayCache_.setColor(QVector4D(0.82f, 0.91f, 0.96f, 1.0f));
            if (hiddenEdgesVisible_) glDisable(GL_DEPTH_TEST);
            glLineWidth(1.5f);
            // Il filetto fisico e' costruito con molte facce molto
            // piccole. In "ombreggiato con bordi" mostrarne ogni
            // diagonale produce una falsa trama verticale e nasconde il
            // profilo elicoidale. Il wireframe conserva invece l'intera
            // topologia, utile per l'ispezione.
            if (extrusion.feature == BodyFeature::Thread && displayMode_ != 0)
                drawThreadProfileEdges(extrusion.display, QVector4D(0.82f, 0.91f, 0.96f, 1.0f));
            else
                drawExtrusionEdges(extrusion);
            glLineWidth(1.0f);
            if (hiddenEdgesVisible_) glEnable(GL_DEPTH_TEST);
        }
        if (!ForgeCad::isCurveBody(extrusion) && index != shapeAnalysisBody_) drawFeatureHighlightEdges(index);
    }
    if (shapeAnalysisMode_ >= 0) {
        const bool through = shapeAnalysisThrough_ && (shapeAnalysisMode_ == 4 || shapeAnalysisMode_ == 5);
        const bool depthEnabled = glIsEnabled(GL_DEPTH_TEST);
        if (through) glDisable(GL_DEPTH_TEST);
        overlayRenderer_.draw(GL_LINES, shapeAnalysis_.grid);
        overlayRenderer_.draw(GL_LINES, shapeAnalysis_.lines);
        if (through && depthEnabled) glEnable(GL_DEPTH_TEST);
        for (const auto &border : shapeAnalysis_.borders)
            overlayRenderer_.drawWideLineStrip(border.points, border.color, 4.0f);
    }
    const auto outline = [this](const SceneSelection &target, const QColor &color, float width) {
        if (target.kind != SceneObjectKind::Extrusion || target.index < 0
            || target.index >= extrusions_.size()) return;
        const ExtrusionObject &extrusion = extrusions_.at(target.index);
        if (!extrusion.visible || (extrusion.display.vertices.isEmpty() && extrusion.display.edges.isEmpty())) return;
        drawExtrusionOutline(extrusion, color, width);
    };
    if (previewing && (preview_.valid || previewDragVisible_)) drawPreview();
    drawTrimPartHover();
    disableSectionClip();
    drawSectionCaps(sectionSolids);
    enableSectionClip();
    if (!edgePicking_ && !previewing) {  // nessun contorno di selezione sulla base in scelta
        if (hover_ != selection_ && !selectedObjects_.contains(hover_)) outline(hover_, kHoverColor, 6.0f);
        outline(selection_, kSelectionColor, 7.0f);
        for (const SceneSelection &object : selectedObjects_)
            if (object != selection_) outline(object, kSelectionColor, 7.0f);
    }
    disableSectionClip();
    drawSectionPlane();
}

void CadViewport::drawTrimPartHover() {
    if (trimPartHover_ < 0 || trimPartHover_ >= trimPartPickDisplays_.size()) return;
    sceneRenderer_.drawTrimPreview(displayCache_, trimPartPickDisplays_.at(trimPartHover_), displayMode_);
}

void CadViewport::drawPreview() {
    const BodyDisplay &display = previewDragVisible_ ? previewDragDisplay_ : preview_.display;
    const BodyDisplay &retained = preview_.retainedDisplay;
    const bool loft = preview_.definition.operation < 0 && preview_.definition.feature == BodyFeature::Loft;
    const bool blend = preview_.definition.operation < 0 && preview_.definition.feature == BodyFeature::Blend;
    const bool extrusion = preview_.definition.operation < 0 && preview_.definition.feature == BodyFeature::Extrusion;
    const bool revolution = preview_.definition.operation < 0 && preview_.definition.feature == BodyFeature::Revolution;
    const bool transparent = loft || blend || extrusion || revolution;
    const bool nearestSurface = extrusion || revolution;
    ForgeCad::Gui::SceneRenderer::PreviewStyle style;
    style.displayMode = displayMode_;
    style.transparent = transparent;
    style.nearestSurface = nearestSurface;
    style.xray = blend;
    style.spatial = preview_.definition.feature == BodyFeature::SpatialSketch
                 || preview_.definition.feature == BodyFeature::ExtractedCurve;
    style.thread = preview_.definition.feature == BodyFeature::Thread;
    style.devicePixelRatio = float(devicePixelRatioF());
    if (!retained.vertices.isEmpty() || !retained.edges.isEmpty()) {
        int base = preview_.replaced.isEmpty() ? -1 : preview_.replaced.first();
        if (base < 0 && !preview_.definition.mergeBodies.isEmpty()) base = preview_.definition.mergeBodies.first();
        style.retainedColor = base >= 0 && base < extrusions_.size() ? meshColorForFeature(base) : QColor::fromRgbF(0.25, 0.65, 0.90);
    }
    sceneRenderer_.drawFeaturePreview(displayCache_, overlayRenderer_, display, retained, style);
}

void CadViewport::drawExportMeshPreview() {
    sceneRenderer_.drawExportMeshPreview(displayCache_, exportMeshPreview_, exportMeshPreviewQuadrangular_);
}

QString CadViewport::previewKey(const ExtrusionObject &d, int index) {
    const auto n = [](double v) { return QString::number(v, 'g', 17); };
    QStringList parts{QString::number(index), QString::number(d.operation), QString::number(int(d.feature)), QString::number(d.sketchIndex),
                      n(d.distance), QString::number(d.revolveAxis), n(d.revolveAngle), QString::number(d.firstBody),
                      QString::number(d.secondBody), QString::number(d.deleteComponent), n(d.blendSize), QString::number(d.blendChamfer), QString::number(d.trimPlane),
                      n(d.trimKeep.x), n(d.trimKeep.y), n(d.trimKeep.z), QString::number(d.trimBoth),
                      n(d.trimToolKeep.x), n(d.trimToolKeep.y), n(d.trimToolKeep.z),
                      QString::number(d.extendLinear), n(d.scaleFactor),
                      QString::number(d.scaleCenterMode), n(d.scaleCenter.x), n(d.scaleCenter.y), n(d.scaleCenter.z),
                      QString::number(d.blendChamfer && d.chamferSpec.mode != 0 ? d.chamferSpec.mode : 0),
                      n(d.blendChamfer && d.chamferSpec.mode != 0 ? d.chamferSpec.second : 0.0),
                      QString::number(d.blendChamfer && d.chamferSpec.mode != 0 && d.chamferSpec.flip)};
    const PrimitiveParameters &p = d.primitive;
    parts << QString::number(int(p.kind)) << QString::number(p.plane);
    for (int k = 0; k < 3; ++k) parts << n(p.origin[k]) << n(p.size[k]);
    for (const EdgePoint &e : d.blendEdges)
        parts << n(e.x) << n(e.y) << n(e.z) << QString::number(e.subshape) << QString::number(e.geometry) << QString::number(e.context)
              << QString::number(e.role);
    const HelixParameters &h = d.helix;
    parts << QString::number(h.spiral) << QString::number(h.mode) << n(h.pitch) << n(h.turns) << n(h.height) << n(h.taper) << n(h.startAngle)
          << QString::number(h.leftHanded) << QString::number(h.reverse) << QString::number(h.source) << QString::number(h.curve)
          << n(h.reference.x) << n(h.reference.y) << n(h.reference.z) << QString::number(h.reference.subshape)
          << QString::number(h.reference.geometry) << QString::number(h.reference.context) << QString::number(d.sweepPath) << QString::number(d.pathSketch)
          << QString::number(d.sweepMode) << QString::number(d.loftRuled) << QStringLiteral("|");
    for (int segment : d.pathSegments) parts << QStringLiteral("ps%1").arg(segment);
    for (int curve : d.pathCurves) parts << QStringLiteral("pc%1").arg(curve);
    for (int section : d.loftSketches) parts << QString::number(section);
    parts << QStringLiteral("G");
    for (int guide : d.loftGuides) parts << QString::number(guide);
    for (int index : d.loftGuideOrder) parts << QStringLiteral("GO%1").arg(index);
    for (const SketchPathRef &guide : d.loftGuidePaths) {
        parts << QStringLiteral("gp%1").arg(guide.sketch);
        for (int segment : guide.segments) parts << QStringLiteral("s%1").arg(segment);
        for (int curve : guide.curves) parts << QStringLiteral("c%1").arg(curve);
    }
    for (int value : d.loftGuideContinuities) parts << QStringLiteral("GC%1").arg(value);
    for (double value : d.loftGuideInfluences) parts << QStringLiteral("GI") << n(value);
    parts << QString::number(d.loftStartContinuity) << QString::number(d.loftEndContinuity) << QString::number(d.loftGuideContinuity)
          << n(d.loftGuideInfluence)
          << n(d.loftStartInfluence) << n(d.loftEndInfluence);
    const auto ref = [&](const GeometryRef &r) {
        parts << QString::number(r.kind) << QString::number(r.index) << QString::number(r.element.kind) << QString::number(r.element.element)
              << QString::number(r.element.point) << n(r.point.x) << n(r.point.y) << n(r.point.z) << QString::number(r.featureId)
              << QString::number(r.point.subshape) << QString::number(r.point.geometry) << QString::number(r.point.context);
    };
    parts << QStringLiteral("D") << QString::number(d.datum.mode) << n(d.datum.distance) << n(d.datum.angle) << QString::number(d.datum.flip)
          << QString::number(d.datum.onCurve) << n(d.datum.size);
    for (const GeometryRef &r : d.datum.refs) ref(r);
    const PatternParameters &r = d.pattern;
    parts << QStringLiteral("P") << QString::number(r.kind) << QString::number(r.count) << QString::number(r.count2) << n(r.spacing) << n(r.spacing2)
          << n(r.angle) << QString::number(r.spread) << QString::number(r.flip) << QString::number(r.flip2) << QString::number(r.keepOriginal)
          << QString::number(r.featureOnly);
    for (const GeometryRef &g : r.refs) ref(g);
    parts << QStringLiteral("E") << QString::number(d.extent) << QString::number(d.mergeOperation) << QString::number(d.mergeAuto)
          << QString::number(d.mergeProbe) << QString::number(d.extrudeSides) << n(d.distance2)
          << QString::number(d.extrudeSurface);
    ref(d.extentRef);
    for (int body : d.mergeBodies) parts << QString::number(body);
    parts << QStringLiteral("T");
    for (int body : d.booleanTools) parts << QString::number(body);
    parts << QStringLiteral("M") << n(d.move.translation[0]) << n(d.move.translation[1]) << n(d.move.translation[2]) << n(d.move.angle)
          << QString::number(d.move.copy);
    ref(d.move.axis);
    ref(d.revolveAxisRef);
    ref(d.draftNeutral);
    parts << n(d.draftAngle) << QString::number(d.draftReverse);
    parts << QStringLiteral("TK") << QString::number(d.thickenSide);
    ref(d.thickenDirection);
    ref(d.sweepProfileRef); ref(d.sweepPathRef);
    parts << QStringLiteral("S3D") << QString::number(d.spatialSketch.nextId);
    for(const auto &e:d.spatialSketch.entities){parts << QString::number(e.id) << QString::number(e.spline) << QString::number(e.closed);
        for(const auto &p:e.points)parts << n(p.x) << n(p.y) << n(p.z);}
    ref(d.curveExtraction.source);
    ref(d.curveExtraction.tool);
    parts << QString::number(d.curveExtraction.trimmed) << QString::number(d.curveExtraction.projected)
          << QString::number(d.curveExtraction.keepStart) << n(d.curveExtraction.cutHint);
    for (double v : d.curveExtraction.view.origin) parts << n(v);
    for (double v : d.curveExtraction.view.normal) parts << n(v);
    for (double v : d.curveExtraction.view.xAxis) parts << n(v);
    parts << QStringLiteral("PJ") << QString::number(d.trimProject) << QString::number(d.projectionMode) << QString::number(d.projectionReverse);
    // Offset e cucitura; loft e sweep di superficie, rigata e planare.
    parts << QStringLiteral("O") << QString::number(d.offsetSew);
    for (const EdgePoint &e : d.offsetFaces)
        parts << n(e.x) << n(e.y) << n(e.z) << QString::number(e.subshape) << QString::number(e.geometry) << QString::number(e.context);
    parts << n(d.sewTolerance) << QString::number(d.sewSolid) << QString::number(d.loftSurface) << QString::number(d.sweepSurface);
    parts << QStringLiteral("FS") << QString::number(d.fillContinuity) << n(d.fillInfluence) << n(d.fillGuideWeight);
    for (double value : d.fillFaceInfluences) parts << QStringLiteral("FI") << n(value);
    parts << QStringLiteral("FS") << QString::number(d.fillShapeFace) << n(d.fillShapeWeight);
    for(const auto &source:d.curveExtraction.sources){parts<<QStringLiteral("ES");ref(source);}
    parts << QStringLiteral("LS");
    for (const GeometryRef &g : d.loftSections) ref(g);
    parts << QStringLiteral("LG");
    for (const GeometryRef &g : d.loftEdgeGuides) ref(g);
    parts << QStringLiteral("TF1");
    for (const GeometryRef &g : d.loftStartFaces) ref(g);
    parts << QStringLiteral("TF2");
    for (const GeometryRef &g : d.loftEndFaces) ref(g);
    parts << QStringLiteral("R1");
    for (const GeometryRef &g : d.ruledFirst) ref(g);
    parts << QStringLiteral("R2");
    for (const GeometryRef &g : d.ruledSecond) ref(g);
    parts << QStringLiteral("PL");
    for (const GeometryRef &g : d.planarRefs) ref(g);
    const ThreadParameters &thread = d.thread;
    parts << QStringLiteral("TH") << QString::number(thread.standard) << thread.designation << n(thread.pitch) << n(thread.length)
          << QString::number(thread.leftHanded) << QString::number(thread.reverse)
          << n(thread.face.x) << n(thread.face.y) << n(thread.face.z)
          << QString::number(thread.face.subshape) << QString::number(thread.face.geometry) << QString::number(thread.face.context);
    return parts.join(QLatin1Char(','));
}

bool CadViewport::computePreview(const PreviewInputs &in, BodyDisplay &display, BodyDisplay &retainedDisplay,
                               BodyDisplay &resultDisplay, ForgeCad::ForgeBody &geometry,
                               QString &error, QVector<int> *merged , QString *notice ) {
    ExtrusionObject body = in.definition;
    if (in.reuse) {
        body.forgeBody = in.existing;
        body.mergeBodies = in.existingMerge;
        body.error.clear();
    } else {
        ForgeCad::buildGeometry(body, in.index, in.sketches, in.bodies);
    }
    if (merged && body.mergeProbe) *merged = body.mergeBodies;  // i corpi toccati dalla fusione automatica
    if (notice) *notice = body.notice;
    if (!ForgeCad::hasGeometry(body)) {
        error = body.error;
        return false;
    }
    geometry = body.forgeBody;
    const bool blend = in.definition.operation < 0 && in.definition.feature == BodyFeature::Blend && body.forgeBody
        && in.definition.firstBody >= 0 && in.definition.firstBody < in.bodies.size()
        && in.bodies.at(in.definition.firstBody).forgeBody;
    const bool localExtrusion = localMergePreview(in.definition)
        && body.mergeOperation != 0 && !body.mergeBodies.isEmpty() && body.forgeBody;
    if (blend) {
        if (in.reuse) resultDisplay = in.existingDisplay;
        ForgeCad::forgeBlendPreviewDisplay(*in.bodies.at(in.definition.firstBody).forgeBody, *body.forgeBody, in.quality, display,
                                           in.quality <= 0 ? 3 : in.quality == 1 ? 5 : 7,
                                           in.reuse ? &in.existingDisplay : nullptr);
    } else if (localExtrusion) {
        if (in.reuse) resultDisplay = in.existingDisplay;
        QVector<ForgeCad::ForgeBody> bases;
        for (int index : body.mergeBodies)
            if (index >= 0 && index < in.bodies.size() && in.bodies.at(index).forgeBody)
                bases.append(in.bodies.at(index).forgeBody);
        ForgeCad::forgeExtrusionPreviewDisplay(bases, *body.forgeBody, in.quality, display,
                                               in.quality <= 0 ? 3 : in.quality == 1 ? 5 : 7,
                                               body.mergeOperation == 2 ? &retainedDisplay : nullptr);
    } else if (in.reuse) {
        display = in.existingDisplay;
        display.constructionCurves.clear();
    } else {
        ForgeCad::tessellateGeometry(body, in.quality, display);
    }
    if (in.definition.operation < 0 && body.forgeBody) {
        const int divisions = in.quality <= 0 ? 3 : in.quality == 1 ? 5 : 7;
        if (in.definition.feature == BodyFeature::Loft)
            ForgeCad::forgeSurfaceConstructionCurves(*body.forgeBody, display, divisions);
        else if ((in.definition.feature == BodyFeature::Extrusion && !localExtrusion)
                 || in.definition.feature == BodyFeature::Revolution)
            ForgeCad::forgeSurfaceConstructionCurves(*body.forgeBody, display, divisions, true);
    }
    return true;
}

void CadViewport::startPreviewJob() {
    if (preview_.key.isEmpty()) return;
    if (previewRunning_) {
        previewRerun_ = true;
        return;
    }
    PreviewInputs in;
    in.definition = preview_.definition;
    in.definition.forgeBody.reset();
    in.definition.curve.reset();
    in.definition.display = {};
    in.quality = tessellationQuality_;
    in.index = preview_.index >= 0 ? preview_.index : int(extrusions_.size());
    in.sketches = sketches_;
    in.bodies.resize(in.index);
    if (preview_.index >= 0 && unchangedFeature(preview_.definition, preview_.index)) {
        const ExtrusionObject &existing = extrusions_.at(preview_.index);
        in.reuse = true;
        in.existing = existing.forgeBody;
        in.existingDisplay = existing.display;
        in.existingMerge = existing.mergeBodies;
    }
    const ExtrusionObject &d = in.definition;
    QString invalid;
    // Operandi: i body e le curve del kernel sono immutabili, si condividono.
    QVector<int> operands = ForgeCad::bodyOperands(d);
    // Ripetizione della funzione: servono anche gli operandi della booleana.
    if (d.operation < 0 && d.feature == BodyFeature::Pattern && d.pattern.featureOnly && d.firstBody >= 0 && d.firstBody < extrusions_.size())
        operands += ForgeCad::bodyOperands(extrusions_.at(d.firstBody));
    for (int body : operands) {
        if (body < 0 || body >= in.index || !ForgeCad::hasGeometry(extrusions_.at(body))) {
            invalid = QStringLiteral("operandi non validi");
            break;
        }
        // La definizione intera (la ripetizione di un'estrusione che si fonde la
        // ricostruisce), senza la tassellazione.
        ExtrusionObject &copy = in.bodies[body];
        copy = extrusions_.at(body);
        copy.display = {};
    }
    if (d.operation >= 0 && d.firstBody == d.secondBody) invalid = QStringLiteral("scegli due oggetti diversi");
    if (d.operation < 0 && d.feature == BodyFeature::SheetTrim && d.firstBody == d.secondBody)
        invalid = QStringLiteral("scegli uno strumento diverso dalla superficie");
    for (int sketch : ForgeCad::sketchesOf(d))
        if (sketch < 0 || sketch >= sketches_.size()) invalid = QStringLiteral("nessuno schizzo");
    const quint64 generation = preview_.generation;
    if (!invalid.isEmpty()) {
        preview_.error = invalid;
        if (edgePicking_ && edgePickStatus_) edgePickStatus_(edgePickMessage());
        if (previewCallback_) previewCallback_(preview_.error);
        update();
        return;
    }
    const QPointer<CadViewport> guard(this);
    previewRunning_ = true;
    const auto cancellation = std::make_shared<std::atomic<bool>>(false);
    previewCancellation_ = cancellation;
    const QString workText = in.reuse ? QStringLiteral("Preparazione della visualizzazione...") : QStringLiteral("Calcolo dell'anteprima...");
    if (workCallback_) workCallback_(true, workText, true);
    QThreadPool::globalInstance()->start([this, guard, generation, in, workText, cancellation] {
        ForgeCad::Kernel::CancellationScope cancellationScope(cancellation);
        BodyDisplay display;
        BodyDisplay retainedDisplay;
        BodyDisplay resultDisplay;
        ForgeCad::ForgeBody geometry;
        QString error, notice;
        bool ok = false;
        QVector<int> merged;
        const bool probe = in.definition.mergeProbe;
        try {
            ForgeCad::Kernel::cancellationPoint();
            ok = computePreview(in, display, retainedDisplay, resultDisplay, geometry, error, &merged, &notice);
            ForgeCad::Kernel::cancellationPoint();
        } catch (const ForgeCad::Kernel::OperationCancelled &) {
            ok = false;
        } catch (const std::exception &failure) {
            error = QString::fromUtf8(failure.what());
        } catch (...) {
            error = QStringLiteral("errore imprevisto");
        }
        if (!ok && error.isEmpty()) error = QStringLiteral("costruzione non riuscita");
        deliverResult(guard, [this, generation, ok, error, notice, probe, merged, workText,
                                             geometry = std::move(geometry), display = std::move(display),
                                             retainedDisplay = std::move(retainedDisplay),
                                             resultDisplay = std::move(resultDisplay)]() mutable {
            previewRunning_ = false;
            previewCancellation_.reset();
            if (workCallback_) workCallback_(false, workText, true);
            if (generation == preview_.generation) {
                if (probe) {
                    const bool localExtrusion = localMergePreview(preview_.definition)
                        && preview_.definition.mergeOperation != 0;
                    if (localExtrusion) {
                        if (preview_.definition.mergeOperation == 2) {
                            preview_.replaced = ok ? merged : QVector<int>();
                            for (int body : merged) preview_.restored.remove(body);
                        } else {
                            preview_.replaced.clear();
                            if (ok)
                                for (int body : merged) preview_.restored.insert(body);
                        }
                    } else {
                        preview_.replaced = ok ? merged : QVector<int>();
                    }
                }
                preview_.valid = ok;
                preview_.error = ok ? QString() : preparePreviewError(error);
                preview_.notice = ok ? notice : QString();
                preview_.geometry = std::move(geometry);
                preview_.display = std::move(display);
                previewDragVisible_ = false;
                refreshPreviewDragSource();
                preview_.retainedDisplay = std::move(retainedDisplay);
                preview_.resultDisplay = std::move(resultDisplay);
                rememberPreview();
                if (edgePicking_ && edgePickStatus_) edgePickStatus_(edgePickMessage());
                if (previewCallback_) previewCallback_(preview_.error);
                update();
                previewRerun_ = false;
            } else if (!preview_.key.isEmpty() && !preview_.valid && preview_.error.isEmpty()) {
                previewRerun_ = false;
                startPreviewJob();
            }
            if (previewHandlePending_) {
                const auto pending = previewHandleDefinition_;
                const int index = previewHandleIndex_;
                previewHandlePending_ = false;
                requestPreview(pending, index);
            }
        });
    });
}

QString CadViewport::preparePreviewError(QString error) {
    previewErrorSketch_ = -1;
    const QRegularExpression marker(QStringLiteral("\\s*\\[\\[loft-section=(\\d+)\\]\\]"));
    const QRegularExpressionMatch match = marker.match(error);
    if (match.hasMatch()) {
        const int section = match.captured(1).toInt();
        if (!preview_.definition.loftSections.isEmpty()) {
            if(section>=0 && section<preview_.definition.loftSections.size() && preview_.definition.loftSections.at(section).kind==7)
                previewErrorSketch_=preview_.definition.loftSections.at(section).index;
        } else if (section >= 0 && section < preview_.definition.loftSketches.size())
            previewErrorSketch_ = preview_.definition.loftSketches.at(section);
        error.remove(marker);
    }
    return error.trimmed();
}

QVector<QVector3D> CadViewport::sketchGeometryPoints(const SketchObject &sketch) const {
    QVector<QVector3D> points;
    for (const SketchSegment &segment : sketch.segments) {
        points.append(mapSketchPoint(segment.first, sketch));
        points.append(mapSketchPoint(segment.second, sketch));
    }
    for (const CurveObject &curve : sketch.curves) {
        for (const QPointF &p : curve.samples) points.append(mapSketchPoint(p, sketch));
        if (curve.samples.isEmpty())
            for (const QPointF &p : curve.controlPoints) points.append(mapSketchPoint(p, sketch));
    }
    return points;
}

QVector<QVector3D> CadViewport::sceneGeometryPoints(bool planesIfEmpty , bool datumSizes ) const {
    QVector<QVector3D> points;
    for (const ExtrusionObject &body : extrusions_) {
        if (!body.visible) continue;
        for (const QVector<QVector3D> &edge : body.display.edges) points += edge;
        if (body.display.edges.isEmpty()) points += body.display.vertices;
    }
    for (int index = 0; index < sketches_.size(); ++index)
        if (isSketchDrawn(index)) points += sketchGeometryPoints(sketches_.at(index));
    // Piani di costruzione: il centro (la misura automatica dipende dalla scena), gli angoli se la misura e' data.
    for (const ExtrusionObject &body : extrusions_) {
        if (!ForgeCad::isDatumBody(body) || !body.visible || !body.datumValid) continue;
        points.append(QVector3D(float(body.datumFrame.origin[0]), float(body.datumFrame.origin[1]), float(body.datumFrame.origin[2])));
        if (datumSizes && body.datum.size > 0.0) points += datumCorners(body.datumFrame, body.datum.size);
    }
    if (points.isEmpty() && planesIfEmpty)
        for (int plane = 0; plane < 3; ++plane)
            points += planeCorners(plane, kDefaultPlaneHalf * float(referencePlaneScales_[plane]),
                                   kDefaultPlaneHalf * float(referencePlaneScales_[plane]));
    return points;
}

void CadViewport::updateSceneBounds() const {
    if (!sceneBoundsDirty_) return;
    // Le dimensioni grafiche dei datum non devono ingrandire a cascata i
    // piani standard: per questi conta la geometria e la posizione dei datum.
    const QVector<QVector3D> sizingPoints = sceneGeometryPoints(false, false);
    QVector<QVector3D> points = sceneGeometryPoints(false, true);
    if (sizingPoints.isEmpty()) {
        planeHalf_ = kDefaultPlaneHalf;
        for (int plane = 0; plane < 3; ++plane)
            referencePlaneAuto_[plane] = QSizeF(kDefaultPlaneHalf, kDefaultPlaneHalf);
        if (points.isEmpty())
            for (int plane = 0; plane < 3; ++plane)
                points += planeCorners(plane, kDefaultPlaneHalf * float(referencePlaneScales_[plane]));
    } else {
        float reach[3] = {};
        for (const QVector3D &p : sizingPoints) {
            reach[0] = qMax(reach[0], qAbs(p.x()));
            reach[1] = qMax(reach[1], qAbs(p.y()));
            reach[2] = qMax(reach[2], qAbs(p.z()));
        }
        const float commonExtent = niceCeiling(qMax(1.2f * qMax(reach[0], qMax(reach[1], reach[2])), 1e-3f));
        for (QSizeF &size : referencePlaneAuto_) size = QSizeF(commonExtent, commonExtent);
        planeHalf_ = commonExtent;
    }
    sceneMin_ = sceneMax_ = points.first();
    for (const QVector3D &p : points) {
        sceneMin_ = QVector3D(qMin(sceneMin_.x(), p.x()), qMin(sceneMin_.y(), p.y()), qMin(sceneMin_.z(), p.z()));
        sceneMax_ = QVector3D(qMax(sceneMax_.x(), p.x()), qMax(sceneMax_.y(), p.y()), qMax(sceneMax_.z(), p.z()));
    }
    sceneBoundsDirty_ = false;
}

float CadViewport::niceCeiling(float x) {
    const float power = std::pow(10.0f, std::floor(std::log10(x)));
    for (float step : {1.0f, 2.0f, 2.5f, 5.0f, 10.0f})
        if (step * power >= x * 0.9999f) return step * power;
    return 10.0f * power;
}

float CadViewport::planeHalf() const {
    updateSceneBounds();
    return planeHalf_;
}

double CadViewport::sceneDepth() const {
    updateSceneBounds();
    double reach = qMax(10.0, 2.5 * double(planeHalf_));
    reach = qMax(reach, axisDisplayLength() * 1.2);
    for (int plane = 0; plane < 3; ++plane) {
        const QSizeF size = referencePlaneExtents(plane);
        reach = qMax(reach, 2.5 * qMax(size.width(), size.height()));
    }
    for (const QVector3D &corner : {sceneMin_, sceneMax_})
        reach = qMax(reach, double(qMax(qAbs(corner.x()), qMax(qAbs(corner.y()), qAbs(corner.z())))));
    return 1.8 * reach + 30.0;
}

void CadViewport::zoomLimits(float &minimum, float &maximum) const {
    updateSceneBounds();
    const float aspect = float(width()) / float(qMax(1, height()));
    const float radius = qMax(0.5f * (sceneMax_ - sceneMin_).length(), 1e-3f);
    const float fit = 2.2f * radius / qMin(1.0f, aspect);
    minimum = fit / 5000.0f;
    maximum = 4.0f * fit;
}

void CadViewport::setZoom(float zoom) {
    float minimum = 0.0f, maximum = 0.0f;
    zoomLimits(minimum, maximum);
    camera_.zoom_ = qBound(minimum, zoom, maximum);
    update();
}

void CadViewport::fitView(const QVector<QVector3D> &points) {
    if (points.isEmpty()) return;
    // Prima del primo disegno il widget non ha ancora la sua dimensione:
    // l'inquadratura si ripete al primo paintGL.
    if (!painted_) pendingFit_ = points;
    setZoom(camera_.fitPoints(points, size()));
}



void CadViewport::initializeGlassShader() {
    glassCompositor_.initialize();
}







void CadViewport::drawGpuGlassPanels(const QSize &pixels, QOpenGLFramebufferObject *resolvedScene) {
    glassCompositor_.draw(pixels, size(), devicePixelRatioF(), defaultFramebufferObject(), overlayRenderer_, resolvedScene);
}

const QColor &CadViewport::axisColor(int axis) {
    static const QColor colors[3] = {QColor(235, 70, 70), QColor(80, 215, 95), QColor(70, 135, 255)};
    return colors[qBound(0, axis, 2)];
}

void CadViewport::drawAxes() {
    if (!axesVisible_) return;
    const float length = float(axisDisplayLength());
    const float head = 0.09f * length, radius = 0.032f * length;
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    QVector<ForgeCad::OverlayVertex> shafts, cones;
    for (int axis = 0; axis < 3; ++axis) {
        QVector3D direction, side, up;
        direction[axis] = 1.0f;
        side[(axis + 1) % 3] = 1.0f;
        up[(axis + 2) % 3] = 1.0f;
        const QColor &color = axisColor(axis);
        const QVector4D rgba(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f);
        const QVector3D tip = direction * length, base = direction * (length - head);
        shafts.push_back({QVector3D(), rgba});
        shafts.push_back({base, rgba});
        for (int k = 0; k < 24; ++k) {
            const float a0 = float(k) * 2.0f * float(M_PI) / 24.0f;
            const float a1 = float(k + 1) * 2.0f * float(M_PI) / 24.0f;
            const QVector3D p0 = base + radius * (std::cos(a0) * side + std::sin(a0) * up);
            const QVector3D p1 = base + radius * (std::cos(a1) * side + std::sin(a1) * up);
            cones.push_back({tip, rgba}); cones.push_back({p0, rgba}); cones.push_back({p1, rgba});
            cones.push_back({base, rgba}); cones.push_back({p1, rgba}); cones.push_back({p0, rgba});
        }
    }
    overlayRenderer_.draw(GL_LINES, shafts);
    overlayRenderer_.draw(GL_TRIANGLES, cones);
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}

void CadViewport::drawGrid() {
    if (!gridVisible_) return;
    glDisable(GL_DEPTH_TEST);
    // Griglia nel piano orizzontale dell'orientamento (perpendicolare all'asse in alto), poco sotto l'origine.
    const QVector3D a(float(camera_.orientation_.right[0]), float(camera_.orientation_.right[1]), float(camera_.orientation_.right[2]));
    const QVector3D b(float(camera_.orientation_.toward[0]), float(camera_.orientation_.toward[1]), float(camera_.orientation_.toward[2]));
    // Passo 1 con i piani di default, poi segue la loro dimensione (20 passi per lato).
    const float step = niceCeiling(qMax(0.25f * planeHalf(), 1e-4f)), extent = 10.0f * step;
    const QVector3D below = -0.05f * step * QVector3D(float(camera_.orientation_.up[0]), float(camera_.orientation_.up[1]), float(camera_.orientation_.up[2]));
    QVector<QVector3D> lines;
    lines.reserve(84);
    for (int i = -10; i <= 10; ++i) {
        const float f = float(i) * step;
        for (const QVector3D &p : {below + f * a - extent * b, below + f * a + extent * b,
                                   below - extent * a + f * b, below + extent * a + f * b})
            lines.push_back(p);
    }
    overlayRenderer_.draw(GL_LINES, lines, QVector4D(0.16f, 0.21f, 0.25f, 1.0f));
    glEnable(GL_DEPTH_TEST);
}
