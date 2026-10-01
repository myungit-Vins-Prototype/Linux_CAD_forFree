#include "forgeCad2026_gui.h"
#include "cuda_support.h"
#include "cad_constraints.h"
#include "cad_curve_solver.h"
#include "cad_document_io.h"
#include "cad_export.h"
#include "cad_expression.h"
#include "cad_datum.h"
#include "cad_features.h"
#include "cad_import.h"
#include "cad_mass.h"
#include "cad_pattern.h"
#include "cad_extrude.h"
#include "cad_sketch_refs.h"
#include "cad_forge.h"
#include "cad_display_cache.h"
#include <QOpenGLContext>
#include <array>
#include "fk_curve_algo.h"
#include "fk_topology.h"
#include "cad_history.h"
#include "cad_model_history.h"
#include "cad_icons.h"
#include "cad_kernel.h"
#include "cad_sketch_edit.h"
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
#include <QPointer>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QMap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QTextBrowser>
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
#include <QVBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
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
#include <QToolButton>
#include <QTimer>
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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <functional>
#include <memory>
#include <optional>
#include <qnamespace.h>
#include <utility>

struct SelectedPoint {
    int kind = 0;
    int element = -1;
    int point = -1;
};

enum class EditablePointKind { Control, TangentIn, TangentOut };

enum class SceneObjectKind { None, Plane, Sketch, Extrusion };

struct SceneSelection {
    SceneObjectKind kind = SceneObjectKind::None;
    int index = -1;
    int subIndex = -1;

    bool operator==(const SceneSelection &other) const {
        return kind == other.kind && index == other.index;
    }
    bool operator!=(const SceneSelection &other) const { return !(*this == other); }
};

struct SketchElementSelection {
    int kind = -1;
    int index = -1;

    bool operator==(const SketchElementSelection &other) const {
        return kind == other.kind && index == other.index;
    }
    bool operator!=(const SketchElementSelection &other) const { return !(*this == other); }
};

static double pointLength(const QPointF &point) { return std::hypot(point.x(), point.y()); }
static double pointDistance(const QPointF &a, const QPointF &b) { return pointLength(b - a); }

// Colori di evidenziazione: giallo per la selezione, azzurro per l'hover.
static const QColor kSelectionColor(255, 225, 70);
static const QColor kHoverColor(90, 205, 255);

// Finestra che si chiude solo se `apply` riesce (definita piu' avanti).
static bool runUntilApplied(QDialog &dialog, QFormLayout *form, QDialogButtonBox *buttons, const std::function<QString()> &apply);

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

class CadViewport final : public QOpenGLWidget, protected QOpenGLFunctions {
    friend class ViewportInteractionTest;
public:
    explicit CadViewport(QWidget *parent = nullptr) : QOpenGLWidget(parent) {
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
    ~CadViewport() override {
        // I framebuffer vanno distrutti con il loro contesto corrente.
        // Il contesto puo' emettere aboutToBeDestroyed dal distruttore della
        // classe base, quando i nostri membri non esistono piu'.
        if (context()) disconnect(context(), nullptr, this, nullptr);
        makeCurrent();
        displayCache_.clear();
        glassShader_.reset();
        glassSource_.reset(); glassPing_.reset(); glassBlur_.reset();
        msaaBuffer_.reset();
        resolveBuffer_.reset();
        doneCurrent();
    }

    void setDisplayMode(int mode) { displayMode_ = mode; update(); }
    void setLightingPreset(int preset) { lightingPreset_ = preset; update(); }
    void setSnapEnabled(bool enabled) { snapEnabled_ = enabled; update(); }
    void setConstraintMode(int mode) { constraintMode_ = mode; }
    void setLineLength(double length) { lineLength_ = qMax(0.0, length); update(); }
    void setLineAngle(double angle) { lineAngle_ = angle; update(); }
    void setPolygonSides(int sides) { polygonSides_ = qBound(3, sides, 64); }
    void setTessellationQuality(int quality) {
        ScopedWork work(workCallback_, QStringLiteral("Aggiornamento della qualita' della scena..."));
        tessellationQuality_ = qBound(0, quality, 2);
        for (SketchObject &sketch : sketches_) {
            for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
        }
        for (ExtrusionObject &body : extrusions_) tessellateBody(body);
        update();
    }
    void setWheelZoomEnabled(bool enabled) { wheelZoomEnabled_ = enabled; }
    quint64 renderedFrameSerial() const { return renderedFrameSerial_; }
    void setGlassPanel(quintptr id, const QRect &rect, int blur, int radius, bool visible) {
        if (!visible) glassPanels_.remove(id);
        else glassPanels_[id] = {rect, blur, radius};
        update();
    }
    void removeGlassPanel(quintptr id) { glassPanels_.remove(id); update(); }
    bool gpuGlassAvailable() const { return gpuGlassReady_; }
    QImage panelBackdropFrame() {
        if (panelBackdropSerial_ != renderedFrameSerial_ || panelBackdropCache_.isNull()) {
            panelBackdropCache_ = grabFramebuffer();
            // grabFramebuffer puo' provocare un paintGL: registra il seriale
            // dopo la lettura per non considerare la cattura un nuovo frame.
            panelBackdropSerial_ = renderedFrameSerial_;
        }
        return panelBackdropCache_;
    }
    // Zoom: zoom_ e' l'altezza visibile in unita' del modello. I limiti
    // dipendono dalla geometria della scena (zoomLimits); "zoom tutto"
    // (resetZoom) inquadra tutta la geometria visibile.
    void zoomIn() { setZoom(zoom_ * 0.85f); }
    void zoomOut() { setZoom(zoom_ / 0.85f); }
    void resetZoom() { fitAll(); }
    // Inquadra tutta la scena; finche' l'utente non muove la vista si ripete
    // quando il widget cambia dimensione (il compositor la fissa dopo il primo disegno).
    void fitAll() {
        fitView(sceneGeometryPoints());
        autoFit_ = true;
    }
    // Pan: tenendo premuto questo tasto (o il tasto centrale del mouse) il
    // trascinamento sposta la vista invece di ruotarla (anche in modalita' schizzo).
    void setPanKey(int key) { panKey_ = key; panKeyHeld_ = false; }
    int panKey() const { return panKey_; }
    void setReferencePlanesVisible(bool visible) { referencePlanesVisible_ = visible; update(); }
    void setPlaneVisible(int plane, bool visible) {
        if (plane < 0 || plane >= 3) return;
        planeVisible_[plane] = visible;
        update();
    }
    const BackgroundSettings &background() const { return background_; }
    void setBackground(const BackgroundSettings &background) { background_ = background; update(); }
    void setDrawingTool(DrawingTool tool) {
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
    // Raggio del raccordo o distanza dello smusso tra segmenti dello schizzo.
    void setSketchBlendSize(bool chamfer, double size) { (chamfer ? sketchChamferDistance_ : sketchFilletRadius_) = size; update(); }
    double sketchBlendSize(bool chamfer) const { return chamfer ? sketchChamferDistance_ : sketchFilletRadius_; }
    void setStatusCallback(std::function<void(const QString &)> callback) { statusCallback_ = std::move(callback); }
    // Campioni dell'antialiasing (0 = spento); limitati al massimo della scheda.
    void setAntialiasing(int samples) {
        antialiasing_ = qMax(0, samples);
        update();
    }
    int antialiasing() const { return maxSamples_ > 0 ? qMin(antialiasing_, maxSamples_) : antialiasing_; }
    int maxAntialiasing() const { return maxSamples_; }
    void setGridVisible(bool visible) { gridVisible_ = visible; update(); }
    // Default: meta' del lato dei piani di riferimento (che vanno da -4 a 4).
    static constexpr double kDefaultAxisLength = 2.0;
    void setAxisLength(double length) { axisLength_ = qBound(0.1, length, 100.0); update(); }
    // Scala scelta dall'utente; la lunghezza a video segue lo zoom.
    double axisLength() const { return axisLength_; }
    double axisDisplayLength() const {
        return double(zoom_) * 0.22 * axisLength_ / kDefaultAxisLength;
    }
    void setAxesVisible(bool visible) { axesVisible_ = visible; update(); }
    bool axesVisible() const { return axesVisible_; }
    void setAxesOnTop(bool onTop) { axesOnTop_ = onTop; update(); }
    bool axesOnTop() const { return axesOnTop_; }
    // Opacita' delle facce dei corpi in modalita' schizzo (0.05-1; 1 = opache).
    void setSketchBodyOpacity(double opacity) { sketchBodyOpacity_ = qBound(0.05, opacity, 1.0); update(); }
    double sketchBodyOpacity() const { return sketchBodyOpacity_; }
    // Estrusione o rivoluzione dallo schizzo: la vista si puo' ruotare per vedere
    // da che parte va la funzione; alla fine torna la vista dello schizzo.
    void setSketchViewUnlocked(bool unlocked) {
        if (unlocked == sketchViewUnlocked_) return;
        if (unlocked) {
            if (!sketchMode_) return;
            sketchCamera_ = {yaw_, pitch_, roll_, zoom_, panX_, panY_, sketchViewRotated_};
            sketchViewUnlocked_ = true;
            hasPendingPoint_ = false;
            curveControlPoints_.clear();
            showStatus(QStringLiteral("Trascina nella vista per ruotarla e vedere da che parte va la funzione; alla chiusura torna la vista dello schizzo"));
        } else {
            sketchViewUnlocked_ = false;
            if (sketchMode_) {
                yaw_ = sketchCamera_.yaw;
                pitch_ = sketchCamera_.pitch;
                roll_ = sketchCamera_.roll;
                zoom_ = sketchCamera_.zoom;
                panX_ = sketchCamera_.panX;
                panY_ = sketchCamera_.panY;
                sketchViewRotated_ = sketchCamera_.rotated;
            }
        }
        update();
    }
    // Vista di nuovo normale al piano dello schizzo (X a destra, Y in alto),
    // con lo stesso zoom e lo stesso punto dello schizzo al centro.
    void alignViewToSketch() {
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
            panX_ = float(-middle.x());
            panY_ = float(-middle.y());
        } else {
            const QVector<QVector3D> points = sketchGeometryPoints(*sketch);
            fitView(points.isEmpty() ? sceneGeometryPoints() : points);
        }
        update();
    }
    bool sketchViewRotated() const { return sketchViewRotated_; }
    void setOriginSnap(bool enabled) { originSnap_ = enabled; }
    bool originSnap() const { return originSnap_; }

    void setSelectionCallback(std::function<void(SceneSelection)> callback) {
        selectionCallback_ = std::move(callback);
    }
    // Osservatore temporaneo usato dalle finestre non modali (per esempio
    // loft) senza sostituire la selezione permanente della finestra principale.
    void setSketchPickCallback(std::function<void(int)> callback) { sketchPickCallback_ = std::move(callback); }
    void setSketchEntityPickCallback(std::function<void(int, int, int)> callback) {
        sketchEntityPickCallback_ = std::move(callback);
    }
    void setDocumentChangedCallback(std::function<void()> callback) {
        documentChangedCallback_ = std::move(callback);
    }
    void setPlaneContextCallback(std::function<void(int)> callback) {
        planeContextCallback_ = std::move(callback);
    }
    // Lo strumento dello schizzo e' cambiato dal viewport (apertura dello
    // schizzo, Esc, linea chiusa su un punto): la finestra aggiorna i menu.
    void setToolChangedCallback(std::function<void(DrawingTool)> callback) { toolChangedCallback_ = std::move(callback); }
    DrawingTool drawingTool() const { return drawingTool_; }
    void setSketchModeCallback(std::function<void(bool)> callback) {
        sketchModeCallback_ = std::move(callback);
    }
    void setRendererCallback(std::function<void(const QString &)> callback) {
        rendererCallback_ = std::move(callback);
    }
    void setWorkCallback(std::function<void(bool, const QString &, bool)> callback) {
        workCallback_ = std::move(callback);
    }
    int activeSketchIndex() const { return activeSketch_; }
    bool sketchModeActive() const { return sketchMode_; }
    // Documento corrente (per il salvataggio) e apertura di un documento: la
    // cronologia riparte da zero e i corpi si rigenerano dalla definizione.
    DocumentState currentDocument() const { return documentState(); }
    void loadDocument(DocumentState state,
                      const std::function<void(int, int, const QString &)> &progress = {}) {
        ScopedWork work(workCallback_, QStringLiteral("Caricamento e rigenerazione del documento..."));
        if (sketchMode_) endSketchMode();
        activeSketch_ = -1;
        selection_ = {};
        history_.clear();
        if (state.orientationSet) orientation_ = state.orientation;
        ForgeCad::normalizeModelHistory(state);
        sketches_ = std::move(state.sketches);
        extrusions_ = std::move(state.extrusions);
        modelBodies_ = std::move(state.modelBodies);
        for (SketchObject &sketch : sketches_)
            for (CurveObject &curve : sketch.curves) {
                if (curve.tool == DrawingTool::Spline && curve.tangentHandles.size() != curve.controlPoints.size())
                    ForgeCad::initializeTangentHandles(curve);
                ForgeCad::recalculateCurve(curve, tessellationQuality_);
            }
        const int totalBodies = qMax(1, int(extrusions_.size()));
        if (progress) progress(0, totalBodies, QStringLiteral("Preparazione degli schizzi..."));
        regenerateAll(progress);
        if (progress && extrusions_.isEmpty()) progress(1, 1, QStringLiteral("Preparazione della scena..."));
        restoreDocument(documentState());
        fitAll();
    }
    // Anteprima nella finestra Apri: usa soltanto le geometrie gia' presenti
    // nella cache del .prt. Non rigenera la cronologia parametrica e non
    // modifica il documento aperto nella finestra principale.
    void loadPreviewDocument(DocumentState state) {
        activeSketch_ = -1;
        selection_ = {};
        if (state.orientationSet) orientation_ = state.orientation;
        ForgeCad::normalizeModelHistory(state);
        sketches_ = std::move(state.sketches);
        extrusions_ = std::move(state.extrusions);
        modelBodies_ = std::move(state.modelBodies);
        for (SketchObject &sketch : sketches_)
            for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, 0);
        for (ExtrusionObject &body : extrusions_) {
            if (body.forgeBody) {
                body.solid = !body.forgeBody->isSheet();
                tessellateGeometry(body, 0, body.display);
            } else {
                body.display = {};
            }
        }
        sceneBoundsDirty_ = true;
        setViewPreset(4);
        fitAll();
        update();
    }
    // Corpi da esportare: quelli visibili con una geometria valida (gli
    // operandi delle booleane e le basi dei raccordi sono nascosti), anche
    // le curve (eliche).
    QVector<ForgeCad::ExportBody> exportableBodies() const {
        QVector<ForgeCad::ExportBody> result;
        for (const ExtrusionObject &body : extrusions_)
            if (body.visible && body.error.isEmpty() && (body.forgeBody || body.curve)) result.append({body.name, body.forgeBody, body.curve});
        return result;
    }

    // Impostazioni dell'interfaccia che la finestra salva.
    int displayMode() const { return displayMode_; }
    int lightingPreset() const { return lightingPreset_; }
    int tessellationQuality() const { return tessellationQuality_; }
    bool wheelZoomEnabled() const { return wheelZoomEnabled_; }
    bool referencePlanesVisible() const { return referencePlanesVisible_; }
    bool gridVisible() const { return gridVisible_; }
    const QVector<SketchObject> &sketches() const { return sketches_; }
    const QVector<ExtrusionObject> &extrusions() const { return extrusions_; }
    const QVector<ModelBody> &modelBodies() const { return modelBodies_; }
    QVector<int> featureSketches(int index) const {
        return index >= 0 && index < extrusions_.size() ? sketchesOf(extrusions_.at(index)) : QVector<int>();
    }

    void setModelBodyVisible(int index, bool visible) {
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

    QString renameModelBody(int index, const QString &name) {
        const QString clean = name.trimmed();
        if (index < 0 || index >= modelBodies_.size()) return QStringLiteral("Corpo non valido.");
        if (clean.isEmpty()) return QStringLiteral("Il nome non puo' essere vuoto.");
        if (modelBodies_.at(index).name == clean) return {};
        recordUndo();
        modelBodies_[index].name = clean;
        documentChanged();
        return {};
    }

    int modelBodyTip(int index) const {
        if (index < 0 || index >= modelBodies_.size()) return -1;
        const quint64 featureId = modelBodies_.at(index).tipFeatureId;
        for (int feature = 0; feature < extrusions_.size(); ++feature)
            if (extrusions_.at(feature).featureId == featureId) return feature;
        return -1;
    }

    void deleteModelBody(int index) {
        if (index < 0 || index >= modelBodies_.size()) return;
        const quint64 id = modelBodies_.at(index).id;
        QVector<SceneSelection> features;
        for (int feature = 0; feature < extrusions_.size(); ++feature)
            if (extrusions_.at(feature).modelBodyId == id) features.append({SceneObjectKind::Extrusion, feature, -1});
        deleteObjects(features);
    }

    QString setFeatureSuppressed(int index, bool suppressed) {
        if (index < 0 || index >= extrusions_.size()) return QStringLiteral("Feature non valida.");
        ExtrusionObject &feature = extrusions_[index];
        if (!feature.modelBodyId) return QStringLiteral("Questa geometria di riferimento non appartiene a una storyboard.");
        if (feature.suppressed == suppressed) return {};
        const quint64 bodyId = feature.modelBodyId;
        bool bodyVisible = true;
        for (const ModelBody &body : modelBodies_)
            if (body.id == bodyId) { bodyVisible = body.visible; break; }
        recordUndo();
        const QVector<int> consumed = hiddenOperands(feature);
        feature.suppressed = suppressed;
        for (int operand : consumed) {
            if (operand < 0 || operand >= extrusions_.size() || extrusions_.at(operand).modelBodyId == bodyId) continue;
            bool consumedElsewhere = false;
            if (suppressed) {
                for (int other = 0; other < extrusions_.size() && !consumedElsewhere; ++other)
                    consumedElsewhere = other != index && !extrusions_.at(other).suppressed
                                     && hiddenOperands(extrusions_.at(other)).contains(operand);
            }
            extrusions_[operand].visible = suppressed && !consumedElsewhere;
        }
        rebuildBody(feature, index);
        regenerateAfter(index);
        int tip = -1;
        for (int candidate = extrusions_.size() - 1; candidate >= 0; --candidate)
            if (extrusions_.at(candidate).modelBodyId == bodyId && !extrusions_.at(candidate).suppressed) { tip = candidate; break; }
        for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
            if (extrusions_.at(candidate).modelBodyId == bodyId) extrusions_[candidate].visible = candidate == tip && bodyVisible;
        selection_ = tip >= 0 ? SceneSelection{SceneObjectKind::Extrusion, tip, -1} : SceneSelection{};
        documentChanged();
        return {};
    }

    void deleteFeature(int index) {
        if (index < 0 || index >= extrusions_.size() || !extrusions_.at(index).modelBodyId) return;
        const quint64 bodyId = extrusions_.at(index).modelBodyId;
        bool bodyVisible = true;
        for (const ModelBody &body : modelBodies_)
            if (body.id == bodyId) { bodyVisible = body.visible; break; }
        int previous = -1;
        for (int candidate = index - 1; candidate >= 0; --candidate)
            if (extrusions_.at(candidate).modelBodyId == bodyId) { previous = candidate; break; }
        recordUndo();
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
        int tip = -1;
        for (int candidate = extrusions_.size() - 1; candidate >= 0; --candidate)
            if (extrusions_.at(candidate).modelBodyId == bodyId && !extrusions_.at(candidate).suppressed) { tip = candidate; break; }
        for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
            if (extrusions_.at(candidate).modelBodyId == bodyId) extrusions_[candidate].visible = candidate == tip && bodyVisible;
        regenerateAll();
        selection_ = {};
        documentChanged();
    }

    QString moveFeature(int index, int direction) {
        if (index < 0 || index >= extrusions_.size() || !extrusions_.at(index).modelBodyId)
            return QStringLiteral("Feature non valida.");
        const quint64 bodyId = extrusions_.at(index).modelBodyId;
        QVector<int> chain;
        for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
            if (extrusions_.at(candidate).modelBodyId == bodyId) chain.append(candidate);
        const int position = chain.indexOf(index), destination = position + (direction < 0 ? -1 : 1);
        if (position <= 0 && direction < 0) return QStringLiteral("La feature iniziale deve restare all'inizio del corpo.");
        if (position < 0 || destination < 0 || destination >= chain.size()) return QStringLiteral("Non ci sono altre feature in quella direzione.");
        if (destination == 0) return QStringLiteral("La feature iniziale del corpo non puo' essere sostituita.");
        return moveFeatureTo(index, chain.at(destination));
    }

    QString moveFeatureTo(int index, int target) {
        if (index < 0 || index >= extrusions_.size() || target < 0 || target >= extrusions_.size()
            || !extrusions_.at(index).modelBodyId || extrusions_.at(index).modelBodyId != extrusions_.at(target).modelBodyId)
            return QStringLiteral("Le feature da riordinare devono appartenere allo stesso corpo.");
        if (index == target) return {};
        const quint64 bodyId = extrusions_.at(index).modelBodyId;
        int first = -1;
        for (int candidate = 0; candidate < extrusions_.size(); ++candidate)
            if (extrusions_.at(candidate).modelBodyId == bodyId) { first = candidate; break; }
        if (index == first || target == first) return QStringLiteral("La feature iniziale deve restare all'inizio del corpo.");
        bool bodyVisible = true;
        for (const ModelBody &body : modelBodies_)
            if (body.id == bodyId) { bodyVisible = body.visible; break; }

        QVector<int> order;
        order.reserve(extrusions_.size());
        for (int old = 0; old < extrusions_.size(); ++old)
            if (old != index) order.append(old);
        order.insert(target, index);
        QVector<int> map(extrusions_.size(), -1);
        QVector<ExtrusionObject> reordered;
        reordered.reserve(extrusions_.size());
        for (int old : order) {
            map[old] = reordered.size();
            reordered.append(extrusions_.at(old));
        }
        const auto mapped = [&](int old) { return old >= 0 ? map.value(old, -1) : old; };
        const auto remapRef = [&](GeometryRef &ref) {
            if (isBodyRef(ref)) ref.index = mapped(ref.index);
        };
        for (ExtrusionObject &feature : reordered) {
            feature.firstBody = mapped(feature.firstBody);
            feature.secondBody = mapped(feature.secondBody);
            for (int &tool : feature.booleanTools) tool = mapped(tool);
            for (int &merged : feature.mergeBodies) merged = mapped(merged);
            for (GeometryRef &ref : feature.datum.refs) remapRef(ref);
            for (GeometryRef &ref : feature.pattern.refs) remapRef(ref);
            remapRef(feature.extentRef);
            remapRef(feature.move.axis);
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
                    || (feature.feature == BodyFeature::Transform && !feature.move.copy) || feature.feature == BodyFeature::Pattern) {
                    feature.firstBody = previous;
                } else if ((feature.feature == BodyFeature::Extrusion || feature.feature == BodyFeature::Sweep) && feature.mergeOperation != 0) {
                    bool replaced = false;
                    for (int &merged : feature.mergeBodies)
                        if (merged >= 0 && reordered.at(merged).modelBodyId == bodyId) { merged = previous; replaced = true; break; }
                    if (!replaced) feature.mergeBodies.prepend(previous);
                }
            }
            previous = current;
        }
        for (int current = 0; current < reordered.size(); ++current)
            for (int operand : bodyOperands(reordered.at(current)))
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

    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void undo() {
        ScopedWork work(workCallback_, QStringLiteral("Ripristino dello stato precedente..."));
        DocumentState state = documentState();
        if (history_.undo(state)) restoreDocument(std::move(state));
    }
    void redo() {
        ScopedWork work(workCallback_, QStringLiteral("Ripristino dello stato successivo..."));
        DocumentState state = documentState();
        if (history_.redo(state)) restoreDocument(std::move(state));
    }

    bool isObjectVisible(SceneObjectKind kind, int index) const {
        if (kind == SceneObjectKind::Sketch && index >= 0 && index < sketches_.size())
            return sketches_.at(index).visible;
        if (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size())
            return extrusions_.at(index).visible;
        return false;
    }
    void setObjectVisible(SceneObjectKind kind, int index, bool visible) {
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
    void showAllObjects() {
        bool anyHidden = false;
        for (const SketchObject &sketch : sketches_) anyHidden = anyHidden || !sketch.visible;
        for (const ExtrusionObject &extrusion : extrusions_) anyHidden = anyHidden || !extrusion.visible;
        if (!anyHidden) return;
        recordUndo();
        for (SketchObject &sketch : sketches_) sketch.visible = true;
        for (ExtrusionObject &extrusion : extrusions_) extrusion.visible = true;
        documentChanged();
    }

    SceneSelection selection() const { return selection_; }
    void selectObject(SceneObjectKind kind, int index) {
        if (activeSketchObject() && (kind == SceneObjectKind::Plane
            || (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size() && isDatumBody(extrusions_.at(index))))) {
            selectSketchReference(kind == SceneObjectKind::Plane ? 1 : 8, index);
            return;
        }
        selection_ = {kind, index, -1};
        if (kind == SceneObjectKind::Plane) selectedPlane_ = index;
        update();
    }

    QStringList extrusionNames() const {
        QStringList names;
        for (const ExtrusionObject &extrusion : extrusions_) names.append(extrusion.name);
        return names;
    }

    // Booleana esatta (B-rep) tra due corpi solidi, anche su piani diversi o
    // risultati di booleane precedenti. Il risultato e' un nuovo corpo che
    // conserva il riferimento agli operandi (rigenerato se cambiano); gli
    // operandi vengono nascosti ma restano nell'albero. Restituisce l'errore.
    QString createBoolean(BooleanOperation operation, int firstIndex, const QVector<int> &tools, const QString &name) {
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
        if (!hasGeometry(result)) return result.error;
        recordUndo();
        extrusions_[firstIndex].visible = false;
        for (int tool : tools) extrusions_[tool].visible = false;
        extrusions_.append(result);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
        return {};
    }

    // Candidati della fusione automatica di un'estrusione o sweep nel posto `index`:
    // i solidi visibili che vengono prima.
    QVector<int> mergeCandidates(int index) const {
        QVector<int> candidates;
        for (int body = 0; body < index && body < extrusions_.size(); ++body) {
            const ExtrusionObject &other = extrusions_.at(body);
            if (other.visible && isShapeBody(other) && other.solid) candidates.append(body);
        }
        return candidates;
    }
    // Definizione pronta per la costruzione: con la fusione automatica i corpi
    // da fondere sono ancora da scegliere tra i candidati.
    ExtrusionObject withMergeCandidates(ExtrusionObject definition, int index) const {
        definition.mergeProbe = false;
        if (definition.operation < 0
            && (definition.feature == BodyFeature::Extrusion || definition.feature == BodyFeature::Sweep)
            && definition.mergeOperation != 0 && definition.mergeAuto) {
            definition.mergeBodies = mergeCandidates(index);
            definition.mergeProbe = true;
        }
        return definition;
    }
    // Corpi che la funzione nasconde (operandi delle booleane, corpi in cui si fonde l'estrusione).
    static QVector<int> hiddenOperands(const ExtrusionObject &body) {
        if (body.operation >= 0) return QVector<int>{body.firstBody, body.secondBody} + body.booleanTools;
        if ((body.feature == BodyFeature::Extrusion || body.feature == BodyFeature::Sweep) && body.mergeOperation != 0)
            return body.mergeBodies;
        if (body.feature == BodyFeature::Transform && !body.move.copy) return {body.firstBody};
        return {};
    }

    // Una superficie (lamina) si puo' intersecare con un solido o tagliare con
    // un solido (la parte fuori); l'unione richiede due solidi.
    static QString booleanOperandsError(BooleanOperation operation, const ExtrusionObject &first, const ExtrusionObject &second) {
        if (first.solid && second.solid) return {};
        if (!first.solid && !second.solid) return QStringLiteral("Serve almeno un solido chiuso: tra due superfici non si fanno booleane.");
        if (operation == BooleanOperation::Union) return QStringLiteral("L'unione richiede due solidi chiusi.");
        if (operation == BooleanOperation::Difference && first.solid)
            return QStringLiteral("Da un solido si puo' sottrarre solo un solido (una superficie non ha volume).");
        return {};
    }

    // Parametri nuovi per il corpo `index` (stessa funzione, stessi
    // riferimenti a schizzi e corpi): si ricostruisce e,
    // se riesce, prende il posto del vecchio e si rigenerano i corpi che ne
    // dipendono (un passo di Undo). Restituisce l'errore.
    QString updateBody(int index, const ExtrusionObject &definition) {
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
        if (sameBlendPreview && (!preview_.valid || !preview_.geometry))
            return preview_.error.isEmpty() ? QStringLiteral("Attendi che l'anteprima sia pronta.") : preview_.error;
        if (sameBlendPreview) {
            candidate.forgeBody = preview_.geometry;
            candidate.solid = true;
            candidate.display = preview_.resultDisplay;
            candidate.display.constructionCurves.clear();
        } else {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            rebuildBody(candidate, index);
            QApplication::restoreOverrideCursor();
        }
        candidate.mergeProbe = false;
        if (!hasGeometry(candidate)) return candidate.error;
        recordUndo();
        // Gli operandi che la funzione non usa piu' tornano visibili, quelli nuovi si nascondono.
        const QVector<int> before = hiddenOperands(extrusions_.at(index)), after = hiddenOperands(candidate);
        for (int body : before)
            if (!after.contains(body) && body >= 0 && body < extrusions_.size()) extrusions_[body].visible = true;
        for (int body : after)
            if (!before.contains(body) && body >= 0 && body < index) extrusions_[body].visible = false;
        extrusions_[index] = candidate;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        regenerateAfter(index);
        QApplication::restoreOverrideCursor();
        selection_ = {SceneObjectKind::Extrusion, index, -1};
        documentChanged();
        return {};
    }

    // --- Quote dei segmenti -----------------------------------------------------

    // Quota del segmento selezionato nello schizzo attivo (o di `index`).
    QString editSegmentDimension(int index = -1) {
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
        form->addRow(new QLabel(QStringLiteral("Il primo estremo resta fermo; la lunghezza diventa una quota (vincolo)\n"
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

    // --- Strumento Quota (D) ---------------------------------------------------
    // Prima si scelgono le entita' da quotare (clic su punti ed entita': un
    // segmento, un cerchio o arco, due punti, punto e retta, due rette...),
    // poi la quota segue il puntatore e un clic nel vuoto la mette e ne chiede
    // il valore. Le quote tra due punti (e la lunghezza di un segmento) sono
    // orizzontali, verticali o allineate (oblique) secondo dove sta il
    // puntatore; H, V, O (obliqua) e A (automatico) fissano l'orientamento.
    QString beginDimensionTool() {
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
    // Orientamento delle quote lineari: 0 automatico (dal puntatore), 1 allineata, 2 orizzontale, 3 verticale.
    void setDimensionOrientation(int orientation) {
        dimensionOrientation_ = qBound(0, orientation, 3);
        refreshDimensionPreview(screenToSketchPoint(mapFromGlobal(QCursor::pos())));
        static const char *names[] = {"automatica", "obliqua (allineata)", "orizzontale", "verticale"};
        showStatus(QStringLiteral("Quota %1").arg(QLatin1String(names[dimensionOrientation_])));
        update();
    }

    // Punto o entita' dello schizzo attivo per la quota sotto il puntatore:
    // prima i punti (estremi, centri, punti delle curve, origine), poi le entita'.
    bool dimensionRefAt(const QPointF &point, ConstraintRef &ref) const {
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

    // Quota per i riferimenti scelti con il puntatore in `cursor` (falso se non ce n'e' una).
    bool buildDimension(const QVector<ConstraintRef> &input, const QPointF &cursor, SketchConstraint &result) {
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
            return make(ConstraintType::Angle);
        }
        return make(ConstraintType::Distance);
    }
    void refreshDimensionPreview(const QPointF &cursor) {
        dimensionPreviewValid_ = drawingTool_ == DrawingTool::Dimension && buildDimension(constraintSelection(), cursor, dimensionPreview_);
    }

    // Clic con lo strumento Quota: su un punto o un'entita' la sceglie (o la
    // toglie); nel vuoto mette la quota in anteprima e ne chiede il valore.
    void dimensionToolClick(const QPointF &point) {
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

    // Finestra del valore della quota nuova (orientamento per le quote tra due
    // punti, raggio o diametro per i cerchi), poi il vincolo con il risolutore.
    QString placeDimension(SketchConstraint constraint) {
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
        form->addRow(new QLabel(ForgeCad::describeConstraint(*active, constraint), &dialog));
        auto *kindBox = new QComboBox(&dialog);
        kindBox->addItems(labels);
        kindBox->setCurrentIndex(qMax(0, int(variants.indexOf(constraint.type))));
        if (!variants.isEmpty()) form->addRow(QStringLiteral("Tipo:"), kindBox);
        auto *valueBox = new ForgeCad::ExpressionSpinBox(&dialog);
        valueBox->setDecimals(6);
        valueBox->setRange(constraint.type == T::Angle ? -360.0 : 1e-9, 1e6);
        if (constraint.type == T::Angle) valueBox->setSuffix(QStringLiteral(" °"));
        valueBox->setValue(constraint.value);
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
            placed.value = valueBox->value();
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

    // --- Eliminazione -----------------------------------------------------------

    // Corpi che dipendono (anche a cascata) dai corpi `removed`, compresi.
    QSet<int> withDependentBodies(QSet<int> removed) const {
        for (int index = 0; index < extrusions_.size(); ++index) {
            const ExtrusionObject &body = extrusions_.at(index);
            bool depends = false;
            for (int operand : bodyOperands(body)) depends = depends || removed.contains(operand);
            if (depends) removed.insert(index);
        }
        return removed;
    }

    // Elimina lo schizzo o il corpo; se altri corpi ne dipendono lo dice e
    // chiede se eliminare anche quelli (tutto in un passo di Undo). In
    // modalita' schizzo elimina le entita' selezionate dello schizzo.
    void deleteSelection() {
        if (sketchMode_) {
            if (!selectedConstraints_.isEmpty()) deleteConstraints(selectedConstraints_);
            else deleteSketchElements();
            return;
        }
        // Piu' oggetti scelti con il riquadro (o Maiusc+clic): tutti insieme.
        if (selectedObjects_.size() > 1) {
            deleteObjects(selectedObjects_);
            return;
        }
        if (selection_.kind == SceneObjectKind::Sketch || selection_.kind == SceneObjectKind::Extrusion)
            deleteObject(selection_.kind, selection_.index);
    }

    void deleteObject(SceneObjectKind kind, int index) {
        if (kind == SceneObjectKind::Extrusion && index >= 0 && index < extrusions_.size()
            && extrusions_.at(index).modelBodyId != 0) {
            deleteFeature(index);
            return;
        }
        deleteObjects({{kind, index, -1}});
    }

    // Elimina schizzi e corpi; i corpi che ne dipendono (e non sono tra quelli
    // scelti) si elencano e si eliminano solo con la conferma. Un passo di Undo.
    void deleteObjects(const QVector<SceneSelection> &objects) {
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
            for (int sketch : sketchesOf(extrusions_.at(body)))
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
    // --- Selezione a riquadro ------------------------------------------------------
    void armBoxSelection(const QPoint &position, bool additive) {
        boxArmed_ = true;
        boxSelecting_ = false;
        boxAdditive_ = additive;
        boxStart_ = boxEnd_ = position;
    }
    QRectF boxRect() const { return QRectF(QPointF(boxStart_), QPointF(boxEnd_)).normalized(); }
    // Da sinistra a destra: solo gli oggetti tutti dentro; da destra a sinistra anche quelli toccati.
    bool boxWindowMode() const { return boxEnd_.x() >= boxStart_.x(); }
    static bool segmentTouchesRect(const QPointF &a, const QPointF &b, const QRectF &r) {
        if (r.contains(a) || r.contains(b)) return true;
        const QPointF corners[] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
        const QLineF segment(a, b);
        for (int k = 0; k < 4; ++k)
            if (segment.intersects(QLineF(corners[k], corners[(k + 1) % 4]), nullptr) == QLineF::BoundedIntersection) return true;
        return false;
    }
    // La polilinea (schermo) e' scelta dal riquadro.
    bool polylineInBox(const QVector<QPointF> &points, const QRectF &r, bool window) const {
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
    // Tutte le polilinee dell'oggetto devono esserci (riquadro da sinistra) o almeno una (da destra).
    bool polylinesInBox(const QVector<QVector<QPointF>> &polylines, const QRectF &r, bool window) const {
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
    QVector<QPointF> projectSketchPoints(const QVector<QPointF> &points, const SketchObject &sketch) const {
        QVector<QPointF> screen;
        screen.reserve(points.size());
        for (const QPointF &p : points) screen.append(projectWorldPoint(mapSketchPoint(p, sketch)));
        return screen;
    }
    QVector<QVector<QPointF>> sketchScreenPolylines(const SketchObject &sketch) const {
        QVector<QVector<QPointF>> polylines;
        for (const SketchSegment &segment : sketch.segments) polylines.append(projectSketchPoints({segment.first, segment.second}, sketch));
        for (const CurveObject &curve : sketch.curves) polylines.append(projectSketchPoints(curve.samples, sketch));
        return polylines;
    }
    void finishBoxSelection() {
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
            if (isDatumBody(body)) {
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
        if (selectionCallback_) selectionCallback_(selection_);
        showStatus(found.size() > 1 ? QStringLiteral("%1 oggetti selezionati (Canc elimina)").arg(found.size())
                                    : found.isEmpty() ? QStringLiteral("Nessun oggetto nel riquadro") : QStringLiteral("1 oggetto selezionato"));
    }
    // Maiusc+clic senza trascinare (fuori dallo schizzo): aggiunge o toglie l'oggetto sotto il puntatore.
    void toggleObjectAt(const QPoint &position) {
        const SceneSelection hit = pickSceneObject(position);
        if (hit.kind != SceneObjectKind::Sketch && hit.kind != SceneObjectKind::Extrusion) return;
        if (selectedObjects_.isEmpty() && (selection_.kind == SceneObjectKind::Sketch || selection_.kind == SceneObjectKind::Extrusion))
            selectedObjects_.append(selection_);
        if (selectedObjects_.contains(hit)) selectedObjects_.removeAll(hit);
        else selectedObjects_.append(hit);
        selection_ = selectedObjects_.isEmpty() ? SceneSelection() : selectedObjects_.last();
        selectedFace_ = {};
        if (selectionCallback_) selectionCallback_(selection_);
        if (selectedObjects_.size() > 1) showStatus(QStringLiteral("%1 oggetti selezionati (Canc elimina)").arg(selectedObjects_.size()));
    }
    // Il riquadro mentre lo si trascina: continuo e azzurro da sinistra (oggetti dentro), tratteggiato e verde da destra (toccati).
    void drawSelectionBox(QPainter &painter) const {
        if (!boxSelecting_) return;
        const bool window = boxWindowMode();
        const QColor color = window ? QColor(90, 170, 255) : QColor(110, 230, 140);
        painter.save();
        painter.setPen(QPen(color, 1.2, window ? Qt::SolidLine : Qt::DashLine));
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 40));
        painter.drawRect(boxRect());
        painter.restore();
    }

    // Toglie lo schizzo e rinumera i riferimenti dei corpi.
    void removeSketchAt(int index) {
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
            for (QVector<GeometryRef> *refs : {&body.datum.refs, &body.pattern.refs})
                for (GeometryRef &ref : *refs)
                    if (isSketchRef(ref)) renumber(ref.index);
            if (isSketchRef(body.extentRef)) renumber(body.extentRef.index);
            if (isSketchRef(body.move.axis)) renumber(body.move.axis.index);
        }
        if (activeSketch_ == index) activeSketch_ = -1;
        else if (activeSketch_ > index) --activeSketch_;
    }

    // Menu contestuale della vista: "Modifica parametri..." di un corpo.
    void setEditBodyCallback(std::function<void(int)> callback) { editBodyCallback_ = std::move(callback); }

    // Estrusione esatta dei profili dello schizzo attivo. Restituisce l'errore.
    QString createExtrusion(double distance, const QString &name) {
        ExtrusionObject extrusion;
        extrusion.distance = distance;
        extrusion.sketchIndex = activeSketch_;
        return createExtrusion(extrusion, name);
    }
    // Estrusione con condizione di fine e fusione (definition.sketchIndex; -1: lo schizzo attivo).
    QString createExtrusion(const ExtrusionObject &definition, const QString &name) {
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
        if (!hasGeometry(extrusion)) return extrusion.error;
        recordUndo();
        for (int body : hiddenOperands(extrusion))
            if (body >= 0 && body < extrusions_.size()) extrusions_[body].visible = false;
        extrusions_.append(extrusion);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        documentChanged();
        return {};
    }

    // Rivoluzione esatta dei profili chiusi dello schizzo `sketchIndex`
    // attorno al suo asse `axis` (segmento dello schizzo, -1 asse X, -2 asse Y
    // del piano) di `angle` gradi. Restituisce l'errore.
    QString createRevolution(int sketchIndex, int axis, double angle, const QString &name) {
        if (sketchIndex < 0 || sketchIndex >= sketches_.size()) return QStringLiteral("Nessuno schizzo scelto.");
        ExtrusionObject revolution;
        revolution.name = name;
        revolution.feature = BodyFeature::Revolution;
        revolution.sketchIndex = sketchIndex;
        revolution.plane = sketches_.at(sketchIndex).plane;
        revolution.revolveAxis = axis;
        revolution.revolveAngle = angle;
        rebuildBody(revolution, int(extrusions_.size()));
        if (!hasGeometry(revolution)) return revolution.error;
        recordUndo();
        extrusions_.append(revolution);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        documentChanged();
        return {};
    }

    // Solido elementare (parallelepipedo, cilindro, sfera, cono, toro).
    QString createPrimitive(const PrimitiveParameters &parameters, const QString &name) {
        ExtrusionObject primitive;
        primitive.name = name;
        primitive.feature = BodyFeature::Primitive;
        primitive.plane = parameters.plane;
        primitive.primitive = parameters;
        rebuildBody(primitive, int(extrusions_.size()));
        if (!hasGeometry(primitive)) return primitive.error;
        recordUndo();
        extrusions_.append(primitive);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        documentChanged();
        return {};
    }

    // Raccordo o smusso: prima si scelgono gli spigoli del corpo selezionato
    // (clic sulla vista, Invio conferma, Esc annulla), poi `edgePickFinished`
    // chiede la misura e crea il corpo con createBlend. Restituisce l'errore.
    QString beginEdgePick(bool chamfer, bool deferPreview = false) {
        edgePickEdit_ = -1;
        return startEdgePick(chamfer, false, false, deferPreview);
    }
    // Base di un'elica: un clic su uno spigolo circolare (source 1) o su una
    // faccia cilindrica o conica (source 2) di un corpo visibile; poi
    // setHelixPickCallback riceve il corpo, il tipo e un punto dello spigolo o della faccia.
    QString beginHelixPick() {
        edgePickEdit_ = -1;
        return startEdgePick(false, false, true);
    }
    void setHelixPickCallback(std::function<void(int, int, EdgePoint)> callback) { helixPickFinished_ = std::move(callback); }
    // Estensione di una superficie: si scelgono i suoi bordi (come gli spigoli
    // dei raccordi), poi setExtendPickCallback chiede la distanza.
    QString beginExtendPick(bool linear) {
        edgePickEdit_ = -1;
        edgePickLinear_ = linear;
        return startEdgePick(false, true);
    }
    void setExtendPickCallback(std::function<void(int, QVector<EdgePoint>)> callback) { extendPickFinished_ = std::move(callback); }
    // Tipo dell'estensione per l'anteprima durante la scelta dei bordi.
    void setEdgePickLinear(bool linear) {
        edgePickLinear_ = linear;
        if (edgePicking_ && edgePickExtend_) edgePicked();
    }
    // Di nuovo alla scelta dei bordi da estendere, con quelli dati gia' scelti.
    void resumeExtendPick(int body, const QVector<EdgePoint> &edges, bool linear) {
        if (body < 0 || body >= extrusions_.size()) return;
        selection_ = {SceneObjectKind::Extrusion, body, -1};
        selectedFace_ = {};
        if (!beginExtendPick(linear).isEmpty()) return;
        FaceHit points;
        points.edges = edges;
        pickedEdges_ = faceDisplayEdges(body, points);
        edgePicked();
    }
    // Modifica degli spigoli del raccordo (o smusso) `blend`: la scelta riparte
    // sulla sua base con gli spigoli attuali; al posto del raccordo si vede
    // l'anteprima. Invio chiama il callback di setEdgeEditCallback.
    QString beginBlendEdit(int blend, double size, bool chamfer) {
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
        FaceHit points;
        points.edges = body.blendEdges;
        pickedEdges_ = faceDisplayEdges(body.firstBody, points);
        edgePicked();
        return {};
    }
    void setEdgeEditCallback(std::function<void(int, QVector<EdgePoint>, double, bool)> callback) { edgeEditFinished_ = std::move(callback); }
    // Misura dell'anteprima durante la scelta degli spigoli.
    void setEdgePickSize(double size) {
        edgePickSize_ = size;
        if (edgePicking_) edgePicked();
    }
    double edgePickSize() const { return edgePickSize_; }
    void setEdgePickChamferSpec(const ChamferSpec &spec) {
        edgePickSpec_ = spec;
        if (edgePicking_ && edgePickChamfer_) edgePicked();
    }
    const ChamferSpec &edgePickChamferSpec() const { return edgePickSpec_; }

    // Anteprima di un corpo definito da `definition` (una funzione nuova, o al
    // posto del corpo `index` se si modifica), calcolata in background; le richieste superate si scartano. Si disegna in ambra; al
    // suo posto spariscono il corpo modificato e gli operandi (booleane, base
    // del raccordo), che si vedono finche' l'anteprima non e' pronta.
    void requestPreview(const ExtrusionObject &definition, int index = -1) {
        const QString key = previewKey(definition, index);
        if (key == preview_.key) {
            if ((preview_.valid || !preview_.error.isEmpty()) && previewCallback_) previewCallback_(preview_.error);
            return;
        }
        preview_.key = key;
        preview_.definition = definition;
        preview_.index = index;
        preview_.replaced.clear();
        // Lo strumento del taglio resta visibile; per il resto spariscono gli operandi.
        if (definition.operation < 0 && definition.feature == BodyFeature::SheetTrim) preview_.replaced = {definition.firstBody};
        else if (isCurveBody(definition) || isDatumBody(definition)) preview_.replaced.clear();  // la base dell'elica resta
        else if (definition.operation < 0 && definition.feature == BodyFeature::Blend) preview_.replaced.clear(); // la base opaca resta sotto la patch
        else if (definition.mergeProbe) preview_.replaced.clear();  // i corpi fusi si sanno a calcolo finito
        else preview_.replaced = bodyOperands(definition);
        // La fine su un altro corpo non lo nasconde (solo quelli che si fondono).
        if (definition.operation < 0
            && (definition.feature == BodyFeature::Extrusion || definition.feature == BodyFeature::Sweep)
            && !definition.mergeProbe)
            preview_.replaced = definition.mergeOperation != 0 ? definition.mergeBodies : QVector<int>();
        preview_.valid = false;
        preview_.error.clear();
        preview_.geometry.reset();
        preview_.resultDisplay = {};
        previewErrorSketch_ = -1;
        ++preview_.generation;
        if (!previewTimer_) {
            previewTimer_ = new QTimer(this);
            previewTimer_->setSingleShot(true);
            previewTimer_->setInterval(120);
            previewTimer_->callOnTimeout([this] { startPreviewJob(); });
        }
        previewTimer_->start();
        update();
    }
    // Raccordo o smusso degli spigoli `edges` del corpo `base` (al posto del raccordo `hidden`, se c'e').
    void requestBlendPreview(int base, const QVector<EdgePoint> &edges, double size, bool chamfer, int hidden = -1, const ChamferSpec &spec = {}) {
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
    void clearPreview() {
        ++preview_.generation;
        preview_.key.clear();
        preview_.index = -1;
        preview_.replaced.clear();
        preview_.valid = false;
        preview_.error.clear();
        preview_.geometry.reset();
        previewErrorSketch_ = -1;
        preview_.display = {};
        preview_.resultDisplay = {};
        update();
    }
    void clearBlendPreview() { clearPreview(); }
    // Errore dell'anteprima (vuoto se riuscita o in calcolo), anche a ogni risultato nuovo.
    QString previewError() const { return preview_.error; }
    void setPreviewCallback(std::function<void(const QString &)> callback) { previewCallback_ = std::move(callback); }

    // Non serve scegliere prima il corpo: se quello selezionato va bene si parte
    // da lui, altrimenti lo decide il primo spigolo cliccato (su qualsiasi
    // solido visibile, o superficie per l'estensione).
    QString startEdgePick(bool chamfer, bool extend = false, bool helix = false, bool deferPreview = false) {
        if (sketchMode_) return QStringLiteral("Esci prima dalla modalita' schizzo.");
        edgePickChamfer_ = chamfer;
        edgePickExtend_ = extend;
        edgePickHelix_ = helix;
        edgePickPreviewEnabled_ = !deferPreview;
        edgePickBody_ = -1;
        // Il corpo selezionato vale anche se nascosto (la base di un raccordo da modificare).
        if (!helix && selection_.kind == SceneObjectKind::Extrusion && selection_.index >= 0 && selection_.index < extrusions_.size()) {
            const ExtrusionObject &selected = extrusions_.at(selection_.index);
            if (isShapeBody(selected) && selected.solid != extend) edgePickBody_ = selection_.index;
        }
        bool any = edgePickBody_ >= 0;
        for (int index = 0; index < extrusions_.size() && !any; ++index) any = edgePickEligible(index);
        if (!any) return helix ? QStringLiteral("Nella scena non ci sono corpi visibili con spigoli circolari o facce cilindriche.")
                     : extend ? QStringLiteral("Nella scena non ci sono superfici (estrusioni di profili aperti) visibili da estendere.")
                              : QStringLiteral("Nella scena non ci sono solidi visibili da raccordare.");
        edgePicking_ = true;
        pickedEdges_.clear();
        hoverEdge_ = -1;
        hoverFaceEdges_.clear();
        // Con una faccia selezionata del corpo si parte dai suoi bordi.
        if (edgePickBody_ >= 0 && selectedFace_.body == edgePickBody_) pickedEdges_ = faceDisplayEdges(edgePickBody_, selectedFace_.hit);
        setFocus();
        edgePicked();
        return {};
    }
    // Di nuovo alla scelta degli spigoli, con quelli dati gia' scelti (dalla
    // finestra della misura, per cambiarli dopo un raccordo non riuscito).
    void resumeEdgePick(int body, const QVector<EdgePoint> &edges, bool chamfer) {
        if (body < 0 || body >= extrusions_.size()) return;
        selection_ = {SceneObjectKind::Extrusion, body, -1};
        selectedFace_ = {};
        if (!beginEdgePick(chamfer).isEmpty()) return;
        FaceHit points;
        points.edges = edges;
        pickedEdges_ = faceDisplayEdges(body, points);
        edgePicked();
    }
    void setEdgePickCallbacks(std::function<void(const QString &)> status,
                              std::function<void(int, QVector<EdgePoint>, bool)> finished) {
        edgePickStatus_ = std::move(status);
        edgePickFinished_ = std::move(finished);
    }
    void setEdgePickChangedCallback(std::function<void(int, QVector<EdgePoint>)> callback) {
        edgePickChanged_ = std::move(callback);
    }
    void setEdgePickPanelKeyCallbacks(std::function<void()> accept, std::function<void()> cancel) {
        edgePickPanelAccept_ = std::move(accept);
        edgePickPanelCancel_ = std::move(cancel);
    }
    void enableEdgePickPreview() {
        if (!edgePicking_ || edgePickPreviewEnabled_) return;
        edgePickPreviewEnabled_ = true;
        edgePicked();
    }
    int edgePickBody() const { return edgePickBody_; }
    QVector<EdgePoint> edgePickPoints() const { return pickedEdgePoints(); }
    bool edgePickPanelActive() const { return bool(edgePickChanged_); }
    void endEdgePick(bool keepPreview = false) { cancelEdgePick(keepPreview); }

    QString createBlend(int baseIndex, const QVector<EdgePoint> &edges, double size, bool chamfer, const QString &name, const ChamferSpec &spec = {}) {
        if (baseIndex < 0 || baseIndex >= extrusions_.size()) return QStringLiteral("Corpo non valido.");
        ExtrusionObject blend;
        blend.name = name;
        blend.feature = BodyFeature::Blend;
        blend.plane = extrusions_.at(baseIndex).plane;
        blend.firstBody = baseIndex;
        blend.blendChamfer = chamfer;
        blend.blendSize = size;
        blend.blendEdges = edges;
        blend.chamferSpec = spec;
        // La stessa definizione e' gia' stata costruita dall'anteprima: il
        // B-rep e la tassellazione sono immutabili e si possono promuovere
        // direttamente a risultato definitivo, senza ripetere il raccordo.
        const QString key = previewKey(blend, -1);
        if (preview_.key == key) {
            if (!preview_.valid || !preview_.geometry)
                return preview_.error.isEmpty() ? QStringLiteral("Attendi che l'anteprima sia pronta.") : preview_.error;
            blend.forgeBody = preview_.geometry;
            blend.solid = true;
            blend.display = preview_.resultDisplay;
            blend.display.constructionCurves.clear();
        } else {
            rebuildBody(blend, int(extrusions_.size()));
            if (!hasGeometry(blend)) return blend.error;
        }
        recordUndo();
        extrusions_[baseIndex].visible = false;
        extrusions_.append(blend);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
        return {};
    }

    // Taglio della superficie `sheet` con il corpo `tool` (-1: il piano di
    // riferimento `plane`): resta la parte che contiene `keep`. La superficie
    // tagliata si nasconde, lo strumento resta. Restituisce l'errore.
    QString createSheetTrim(int sheet, int tool, int plane, const EdgePoint &keep, const QString &name) {
        if (sheet < 0 || sheet >= extrusions_.size() || tool >= extrusions_.size() || tool == sheet)
            return QStringLiteral("Superficie o strumento non validi.");
        ExtrusionObject body;
        body.name = name;
        body.feature = BodyFeature::SheetTrim;
        body.plane = extrusions_.at(sheet).plane;
        body.firstBody = sheet;
        body.secondBody = tool;
        body.trimPlane = plane;
        body.trimKeep = keep;
        rebuildBody(body, int(extrusions_.size()));
        if (!hasGeometry(body)) return body.error;
        recordUndo();
        extrusions_[sheet].visible = false;
        extrusions_.append(body);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
        return {};
    }
    // Le parti in cui lo strumento divide la superficie, per sceglierne una.
    QVector<SheetPiece> sheetPieces(int sheet, int tool, int plane, QString *error) const {
        ScopedWork work(workCallback_, QStringLiteral("Calcolo delle parti della superficie..."));
        if (sheet < 0 || sheet >= extrusions_.size() || tool >= extrusions_.size() || tool == sheet) {
            if (error) *error = QStringLiteral("Superficie o strumento non validi.");
            return {};
        }
        const ExtrusionObject &target = extrusions_.at(sheet);
        const ExtrusionObject *cutter = tool >= 0 ? &extrusions_.at(tool) : nullptr;
        return ForgeCad::forgeSheetPieces(target.forgeBody, cutter ? cutter->forgeBody : nullptr, plane, error);
    }
    // Estensione dei bordi `edges` della superficie `sheet` (nascosta). Restituisce l'errore.
    QString createSheetExtend(int sheet, const QVector<EdgePoint> &edges, double distance, bool linear, const QString &name) {
        if (sheet < 0 || sheet >= extrusions_.size()) return QStringLiteral("Superficie non valida.");
        ExtrusionObject body;
        body.name = name;
        body.feature = BodyFeature::SheetExtend;
        body.plane = extrusions_.at(sheet).plane;
        body.firstBody = sheet;
        body.blendEdges = edges;
        body.blendSize = distance;
        body.extendLinear = linear;
        rebuildBody(body, int(extrusions_.size()));
        if (!hasGeometry(body)) return body.error;
        recordUndo();
        extrusions_[sheet].visible = false;
        extrusions_.append(body);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
        return {};
    }
    // Scala uniforme del corpo `base` (nascosto): fattore e centro (0 origine,
    // 1 baricentro del solido, 2 il punto `center`). Restituisce l'errore.
    QString createScale(int base, double factor, int mode, const EdgePoint &center, const QString &name) {
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
        if (!hasGeometry(body)) return body.error;
        recordUndo();
        extrusions_[base].visible = false;
        extrusions_.append(body);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
        return {};
    }

    // Corpo nuovo dalla definizione (elica, sweep, loft); i corpi `hide` (il
    // percorso di uno sweep) e quelli assorbiti da una fusione si nascondono
    // nello stesso passo di Undo.
    // Restituisce l'errore.
    QString createBody(ExtrusionObject body, const QVector<int> &hide = {}) {
        body = withMergeCandidates(body, int(extrusions_.size()));
        rebuildBody(body, int(extrusions_.size()));
        body.mergeProbe = false;
        if (!hasGeometry(body)) return body.error;
        recordUndo();
        QVector<int> hidden = hide + hiddenOperands(body);
        for (int index : hidden)
            if (index >= 0 && index < extrusions_.size()) extrusions_[index].visible = false;
        extrusions_.append(body);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
        return {};
    }

    // --- Piani di costruzione e riferimenti -------------------------------------

    // Scelta di un riferimento nella vista (punto, retta, curva o piano secondo
    // `roles`, maschera di ForgeCad::DatumRole) per il piano di costruzione
    // `owner` (-1: nuovo; i riferimenti devono venire da corpi precedenti). Il
    // clic chiama setReferencePickCallback(true, riferimento); Esc (false, {}).
    // Senza nulla sotto il puntatore il trascinamento ruota la vista.
    QString beginReferencePick(int roles, int owner) {
        if (sketchMode_) return QStringLiteral("Esci prima dalla modalita' schizzo.");
        if (edgePicking_) cancelEdgePick();
        refPicking_ = true;
        refPickRoles_ = roles;
        refPickOwner_ = owner;
        refHoverValid_ = false;
        setFocus();
        update();
        return {};
    }
    void cancelReferencePick() {
        if (!refPicking_) return;
        refPicking_ = false;
        refHoverValid_ = false;
        update();
    }
    bool referencePicking() const { return refPicking_; }
    void setReferencePickCallback(std::function<void(bool, GeometryRef)> callback) { refPickFinished_ = std::move(callback); }
    // Riferimenti gia' scelti, evidenziati nella vista (finestra del piano).
    void setReferenceMarks(const QVector<GeometryRef> &marks) {
        refMarks_ = marks;
        update();
    }
    // Anteprima del piano della finestra (al posto del piano `replaced`, se lo si modifica).
    void setDatumPreview(bool valid, const SketchFrame &frame, double size, int replaced) {
        datumPreviewValid_ = valid;
        datumPreviewFrame_ = frame;
        datumPreviewSize_ = size;
        datumPreviewIndex_ = replaced;
        update();
    }
    void clearDatumPreview() {
        datumPreviewValid_ = false;
        datumPreviewIndex_ = -1;
        refMarks_.clear();
        update();
    }
    // Distanza proposta per un piano parallelo: un quarto dei piani di riferimento, arrotondata.
    double suggestedDatumOffset() const { return double(niceCeiling(0.25f * planeHalf())); }
    // Finestre modali "leggere" (il piano di costruzione): la vista resta
    // attiva per la scelta dei riferimenti, ma niente menu contestuali.
    void setInteractionLocked(bool locked) { interactionLocked_ = locked; }
    // Il piano per la finestra, con il documento com'e' (il corpo `index`, -1 se nuovo).
    bool previewDatum(const DatumParameters &parameters, int index, SketchFrame &frame, QString *error) const {
        return ForgeCad::computeDatum(parameters, index < 0 ? int(extrusions_.size()) : index, sketches_, extrusions_, frame, error);
    }
    // Nuovo schizzo sul piano di costruzione `body` (lo segue quando si rigenera).
    int createDatumSketch(int body, const QString &name) {
        if (body < 0 || body >= extrusions_.size() || !isDatumBody(extrusions_.at(body)) || !extrusions_.at(body).datumValid) return -1;
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
    // Corpi importati (un passo di Undo); `source` e' il nome del file. Restituisce i corpi che non si rileggono.
    QStringList importParts(const QVector<ForgeCad::ImportedPart> &parts, const QString &source) {
        ScopedWork work(workCallback_, QStringLiteral("Preparazione dei corpi importati..."));
        QStringList failures;
        if (parts.isEmpty()) return failures;
        recordUndo();
        for (const ForgeCad::ImportedPart &part : parts) {
            ExtrusionObject body;
            body.name = part.name;
            body.feature = BodyFeature::Imported;
            body.importData = part.data;
            body.importSource = source;
            // Il body appena letto (lo stesso che importData ridara' alla rigenerazione).
            body.forgeBody = part.body;
            body.solid = part.solid;
            if (!body.forgeBody) body.error = QStringLiteral("Il corpo non ha geometria.");
            tessellateBody(body);
            if (!body.error.isEmpty()) failures.append(QStringLiteral("%1: %2").arg(body.name, body.error));
            extrusions_.append(body);
        }
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
        fitAll();
        return failures;
    }

    // Proprieta' di massa del corpo `index` (densita' 1), sulla geometria esatta.
    ForgeCad::MassReport massReport(int index) const {
        ScopedWork work(workCallback_, QStringLiteral("Calcolo delle proprieta' di massa..."));
        ForgeCad::MassReport report;
        if (index < 0 || index >= extrusions_.size() || !hasGeometry(extrusions_.at(index))) {
            report.error = QStringLiteral("il corpo non ha geometria");
            return report;
        }
        const ExtrusionObject &body = extrusions_.at(index);
        if (isDatumBody(body)) {
            report.error = QStringLiteral("e' un piano di costruzione");
            return report;
        }
        if (body.curve) return ForgeCad::curveMassProperties(*body.curve);
        return ForgeCad::forgeMassProperties(*body.forgeBody);
    }

    // Anteprima dell'estensione dei bordi `edges` di `base` (al posto del corpo `hidden`, se c'e').
    void requestExtendPreview(int base, const QVector<EdgePoint> &edges, double distance, bool linear, int hidden = -1) {
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

    // Le entita' selezionate nello schizzo attivo diventano di costruzione
    // (o tornano normali se lo erano tutte). Restituisce l'errore.
    QString toggleConstruction() {
        if (!sketchMode_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size())
            return QStringLiteral("Entra in modalita' schizzo e seleziona le entita' (clic o Ctrl+clic).");
        if (sketchSelections_.isEmpty()) return QStringLiteral("Seleziona prima le entita' dello schizzo (clic o Ctrl+clic).");
        SketchObject &sketch = sketches_[activeSketch_];
        bool allConstruction = true;
        for (const SketchElementSelection &element : sketchSelections_) {
            if (element.kind == 0 && element.index < sketch.segments.size()) allConstruction &= sketch.isConstructionSegment(element.index);
            if (element.kind == 1 && element.index < sketch.curves.size()) allConstruction &= sketch.curves.at(element.index).construction;
        }
        recordUndo();
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

    // I segmenti selezionati diventano assi di simmetria (e di costruzione), o
    // tornano linee di costruzione se lo erano tutti. Restituisce l'errore.
    QString toggleSymmetryAxis() {
        if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo e seleziona i segmenti.");
        QVector<int> segments;
        for (const SketchElementSelection &element : sketchSelections_)
            if (element.kind == 0 && element.index >= 0 && element.index < sketches_.at(activeSketch_).segments.size()) segments.append(element.index);
        if (segments.isEmpty()) return QStringLiteral("Seleziona prima uno o piu' segmenti dello schizzo.");
        SketchObject &sketch = sketches_[activeSketch_];
        bool all = true;
        for (int index : segments) all = all && sketch.symmetryAxes.contains(index);
        recordUndo();
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
    // Una linea di costruzione usata come asse (quote di raggio o diametro, simmetrie) diventa asse di simmetria.
    static void markSymmetryAxis(SketchObject &sketch, const SketchConstraint &c) {
        const ConstraintRef axis = c.type == ConstraintType::Symmetric ? c.third
                                 : c.type == ConstraintType::AxisRadius || c.type == ConstraintType::AxisDiameter ? c.second : ConstraintRef();
        if (axis.kind == 0 && axis.point < 0 && sketch.isConstructionSegment(axis.element) && !sketch.symmetryAxes.contains(axis.element))
            sketch.symmetryAxes.append(axis.element);
    }
    // Quota dall'asse mentre la si posiziona: dalla parte del punto e' un
    // raggio, dall'altra parte dell'asse un diametro (la misura resta quella).
    void placeAxisDimension(SketchConstraint &c, const QPointF &pointer) {
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

    // Solidi e superfici separati del corpo (in cache per body: il B-rep e' immutabile).
    QPair<int, int> bodyComponents(int index) const {
        if (index < 0 || index >= extrusions_.size() || !isShapeBody(extrusions_.at(index))) return {0, 0};
        const ForgeCad::ForgeBody &body = extrusions_.at(index).forgeBody;
        const auto found = componentCache_.find(body.get());
        if (found != componentCache_.end() && found->second.first.lock() == body) return found->second.second;
        int solids = 0, sheets = 0;
        ForgeCad::forgeComponentCounts(body, solids, sheets);
        if (componentCache_.size() > 512) componentCache_.clear();
        componentCache_[body.get()] = {body, {solids, sheets}};
        return {solids, sheets};
    }
    // Rinomina uno schizzo o un corpo (un passo di Undo).
    QString renameObject(SceneObjectKind kind, int index, const QString &name) {
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

    // --- Riferimenti esterni dello schizzo ---------------------------------------

    // kind come GeometryRef: piano di riferimento 1, asse 2, datum 8.
    void selectSketchReference(int kind, int index) {
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
                plane = ForgeCad::referenceSketchFrame(index, orientation_);
                name = QStringLiteral("Piano %1").arg(index == 0 ? QStringLiteral("XY") : index == 1 ? QStringLiteral("XZ") : QStringLiteral("YZ"));
            } else if (kind == 8 && index >= 0 && index < extrusions_.size() && isDatumBody(extrusions_.at(index)) && extrusions_.at(index).datumValid) {
                plane = extrusions_.at(index).datumFrame;
                name = extrusions_.at(index).name;
            } else return;
            error = ForgeCad::sketchPlaneReference(work, plane, point, direction);
        }
        if (!error.isEmpty()) { showStatus(error); return; }
        const int before = int(work.segments.size());
        const int segment = ForgeCad::appendFixedReferenceLine(work, point, direction, std::max(1e-4, double(zoom_) * 0.35));
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
    bool pickSketchReferencePlaneAxis(const QPoint &position, int &kind, int &index) const {
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


    // Spigolo (o curva: elica, spirale) di un corpo visibile sotto il puntatore:
    // la polilinea di display e il suo campione piu' vicino (sta sulla curva esatta).
    struct SketchReferenceHit {
        int body = -1, polyline = -1;
        int sketch = -1, entityKind = -1, entity = -1;
        QVector3D point;
    };
    bool pickSketchReference(const QPoint &position, SketchReferenceHit &hit) const {
        hit = {};
        double best = 8.0;
        for (int b = 0; b < extrusions_.size(); ++b) {
            const ExtrusionObject &body = extrusions_.at(b);
            if (!body.visible || !(isShapeBody(body) || (isCurveBody(body) && body.curve))) continue;
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
    // Lo spigolo o la curva sotto il puntatore proiettati nello schizzo attivo (un passo di Undo).
    QString convertReferenceAt(const QPoint &position) {
        if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo.");
        SketchReferenceHit hit;
        if (!pickSketchReference(position, hit)) return QStringLiteral("Clicca uno spigolo di un corpo, una curva o un'entita' di un altro schizzo visibile.");
        if (hit.sketch >= 0) {
            SketchObject work = sketches_.at(activeSketch_);
            const QString error = ForgeCad::appendSketchContactReference(work, sketches_.at(hit.sketch),
                                                                         {hit.entityKind, hit.entity});
            if (!error.isEmpty()) return error;
            recordUndo();
            sketches_[activeSketch_] = std::move(work);
            sketchEdited();
            return {};
        }
        const ExtrusionObject &body = extrusions_.at(hit.body);
        ForgeCad::Kernel::CurvePtr<3> curve;
        ForgeCad::Kernel::Interval range;
        if (isCurveBody(body)) {
            curve = body.curve;
            range = curve->domain();
        } else if (!ForgeCad::nearestBodyEdge(*body.forgeBody, ForgeCad::Kernel::Vec3(hit.point.x(), hit.point.y(), hit.point.z()), curve, range)) {
            return QStringLiteral("Lo spigolo non si trova sul corpo.");
        }
        SketchObject work = sketches_.at(activeSketch_);
        const QString error = ForgeCad::appendProjectedCurve(work, curve, range, referencesConstruction_, true);
        if (!error.isEmpty()) return error;
        recordUndo();
        sketches_[activeSketch_] = work;
        sketchEdited();
        return {};
    }
    // Le sezioni dei solidi visibili con il piano dello schizzo attivo (un passo di Undo).
    QString addSectionReferences(int *added = nullptr) {
        ScopedWork progressWork(workCallback_, QStringLiteral("Calcolo delle sezioni sul piano di schizzo..."));
        if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo.");
        const int entitiesBefore = sketches_.at(activeSketch_).segments.size() + sketches_.at(activeSketch_).curves.size();
        SketchObject work = sketches_.at(activeSketch_);
        int cut = 0;
        QString last;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        for (const ExtrusionObject &body : extrusions_) {
            if (!body.visible || !isShapeBody(body) || !body.solid) continue;
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
        if (cut == 0) return last.isEmpty() ? QStringLiteral("Nessun solido visibile.") : last;
        if (added) *added = work.segments.size() + work.curves.size() - entitiesBefore;
        recordUndo();
        sketches_[activeSketch_] = work;
        sketchEdited();
        return {};
    }
    // Punti/curve di contatto tra gli altri schizzi visibili e il piano
    // attivo. I punti sono entita' di costruzione fisse e quindi possono
    // ricevere Coincidente o imporre Punto sull'entita' a segmenti e curve.
    QString addSketchContactReferences(int *added = nullptr) {
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
    void setReferencesConstruction(bool construction) { referencesConstruction_ = construction; }

    // --- Ripetizione delle entita' dello schizzo ---------------------------------

    // Entita' selezionate nello schizzo attivo (per la finestra della ripetizione).
    QVector<SketchElementSelection> sketchSelection() const { return sketchMode_ ? sketchSelections_ : QVector<SketchElementSelection>(); }
    // Posizioni dei punti scelti con Ctrl+clic nello schizzo attivo.
    QVector<QPointF> selectedSketchPoints() const {
        QVector<QPointF> points;
        if (!activeSketchObject()) return points;
        for (const SelectedPoint &point : selectedPoints_)
            if (isValidSelectedPoint(point)) points.append(selectedPointPosition(point));
        return points;
    }
    // I punti scelti con Ctrl+clic come riferimenti dei vincoli (stesso ordine di selectedSketchPoints).
    QVector<ConstraintRef> selectedSketchPointRefs() const {
        QVector<ConstraintRef> refs;
        if (!activeSketchObject()) return refs;
        for (const SelectedPoint &point : selectedPoints_)
            if (isValidSelectedPoint(point)) refs.append({point.kind, point.element, point.kind == 2 ? -1 : point.point});
        return refs;
    }
    // Estremi di un segmento dello schizzo attivo.
    bool activeSegment(int index, SketchSegment &segment) const {
        const SketchObject *sketch = activeSketchObject();
        if (!sketch || index < 0 || index >= sketch->segments.size()) return false;
        segment = sketch->segments.at(index);
        return true;
    }
    // Le entita' da ripetere: la selezione senza il segmento `excluded` (la retta dello specchio).
    QVector<ForgeCad::SketchEntity> patternEntities(int excluded) const {
        QVector<ForgeCad::SketchEntity> entities;
        for (const SketchElementSelection &element : sketchSelections_)
            if (!(element.kind == 0 && element.index == excluded)) entities.append({element.kind, element.index});
        return entities;
    }
    // Anteprima della ripetizione (copie tratteggiate in arancio); restituisce l'errore.
    QString previewSketchPattern(const ForgeCad::SketchPattern &pattern, int excluded) {
        sketchPatternPreview_.clear();
        const SketchObject *active = activeSketchObject();
        if (!active) return QStringLiteral("Entra in modalita' schizzo.");
        SketchObject work = *active;
        QVector<ForgeCad::SketchEntity> created;
        const ForgeCad::SketchEditResult result = ForgeCad::patternSketchEntities(work, patternEntities(excluded), pattern, &created);
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
    void clearSketchPatternPreview() {
        sketchPatternPreview_.clear();
        update();
    }
    // Ripetizione nello schizzo attivo (un passo di Undo): le copie diventano la selezione.
    QString applySketchPattern(const ForgeCad::SketchPattern &pattern, int excluded) {
        if (!activeSketchObject()) return QStringLiteral("Entra in modalita' schizzo.");
        SketchObject work = sketches_.at(activeSketch_);
        QVector<ForgeCad::SketchEntity> created;
        const ForgeCad::SketchEditResult result = ForgeCad::patternSketchEntities(work, patternEntities(excluded), pattern, &created);
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

    // --- Vincoli geometrici (oggetti) -------------------------------------------

    // Lo schizzo attivo in modalita' schizzo (nullptr altrimenti).
    const SketchObject *activeSketchObject() const {
        return sketchMode_ && activeSketch_ >= 0 && activeSketch_ < sketches_.size() ? &sketches_.at(activeSketch_) : nullptr;
    }
    // Riferimenti scelti per un vincolo: i punti (Ctrl+clic, anche l'origine) e le entita' selezionate.
    QVector<ConstraintRef> constraintSelection() const {
        QVector<ConstraintRef> refs;
        for (const SelectedPoint &point : selectedPoints_)
            if (isValidSelectedPoint(point)) refs.append({point.kind, point.element, point.kind == 2 ? -1 : point.point});
        for (const SketchElementSelection &element : sketchSelections_) refs.append({element.kind, element.index, -1});
        return refs;
    }
    // Vincolo del tipo dato sui riferimenti scelti; le quote con `value` (NaN: la misura attuale).
    QString addConstraint(ConstraintType type, double value = qQNaN()) {
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
    QString deleteConstraints(QVector<int> indices) {
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
    // Nuovo valore di una quota: lo schizzo si adatta (se i vincoli lo permettono).
    // `type`: le quote dall'asse possono passare da raggio a diametro (e viceversa).
    QString setConstraintValue(int index, double value, std::optional<ConstraintType> type = std::nullopt) {
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
    // Parametri nuovi di una ripetizione parametrica (un passo di Undo): le
    // entita' di partenza restano ferme se si puo', le copie si spostano (o si
    // rifanno, se cambia il numero di istanze).
    QString setSketchPatternValues(int index, const SketchPatternData &values) {
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
            remapRevolutionAxes(activeSketch_, result.segmentMap);
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
    // Finestra dei parametri di una ripetizione dello schizzo.
    void editSketchPatternValues(int index) {
        const SketchObject *active = activeSketchObject();
        if (!active || index < 0 || index >= active->geometricConstraints.size()) return;
        SketchPatternData values = active->geometricConstraints.at(index).pattern;
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Ripetizione"));
        auto *form = new QFormLayout(&dialog);
        form->addRow(new QLabel(ForgeCad::describeConstraint(*active, active->geometricConstraints.at(index)), &dialog));
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
            form->addRow(new QLabel(QStringLiteral("Lo specchio non ha parametri: la retta e' quella scelta."), &dialog));
        }
        form->addRow(new QLabel(QStringLiteral("Un parametro non quotato resta libero: il risolutore lo cambia\n"
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
    // Finestra del valore di una quota: resta aperta (si riapre) finche' il valore non va bene o si annulla.
    void editConstraintValue(int index) {
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
        valueBox->setValue(constraint.value);
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
            return setConstraintValue(index, valueBox->value(), type);
        });
    }
    // Gradi di liberta' dello schizzo attivo (ricalcolati quando lo schizzo cambia).
    const ForgeCad::SketchAnalysis &sketchAnalysis() const {
        if (analysisDirty_ || analysisSketch_ != activeSketch_) {
            analysis_ = activeSketchObject() ? ForgeCad::analyzeSketch(*activeSketchObject()) : ForgeCad::SketchAnalysis();
            analysisSketch_ = activeSketch_;
            analysisDirty_ = false;
        }
        return analysis_;
    }
    QVector<int> selectedConstraints() const { return selectedConstraints_; }
    void setSelectedConstraints(const QVector<int> &indices) {
        selectedConstraints_ = indices;
        update();
    }
    void setConstraintsVisible(bool visible) {
        constraintsVisible_ = visible;
        update();
    }

    // --- Vista in sezione ------------------------------------------------------
    // Solo visualizzazione: i corpi si tagliano con un piano di clip di OpenGL
    // (resta la parte dalla parte opposta alla normale) e le sezioni dei solidi
    // si chiudono con lo stencil (riempimento tratteggiato). La selezione
    // ignora quello che sta dalla parte tolta.
    void setSection(bool enabled, const QVector3D &point = {}, const QVector3D &normal = QVector3D(0, 0, 1), bool caps = true, bool showPlane = true) {
        section_.enabled = enabled && normal.lengthSquared() > 0.0f;
        section_.point = point;
        section_.normal = normal.lengthSquared() > 0.0f ? normal.normalized() : QVector3D(0, 0, 1);
        section_.caps = caps;
        section_.showPlane = showPlane;
        update();
    }
    bool sectionEnabled() const { return section_.enabled; }
    // Trascinamento del piano di sezione nella vista: `callback(spostamento)`
    // lungo la normale della sezione (nullptr: il piano non si trascina).
    void setSectionDragCallback(std::function<void(double)> callback) { sectionDragged_ = std::move(callback); }
    // Maniglia del piano di sezione sullo schermo: la freccia dal centro del
    // piano lungo la normale (un disco se la normale guarda l'osservatore).
    QPolygonF sectionHandleShape(bool *disc = nullptr) const {
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
    // Il puntatore sta sulla maniglia o sul bordo del piano.
    bool sectionHandleAt(const QPoint &position) const {
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
    // La maniglia disegnata come un oggetto selezionato: arancio pieno con il contorno giallo e l'alone.
    void drawSectionHandle(QPainter &painter) const {
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
    float sectionArrowLength() const { return 90.0f * zoom_ / float(qMax(1, height())); }
    // Spostamento lungo la normale per un movimento del puntatore: la sua
    // componente lungo la normale vista sullo schermo; con la normale verso
    // l'osservatore, il movimento verticale.
    double sectionDragDelta(const QPoint &mouse) const {
        const float length = sectionArrowLength();
        const QPointF a = projectWorldPoint(section_.point), b = projectWorldPoint(section_.point + length * section_.normal);
        const QPointF screen = b - a;
        const double pixels = std::hypot(screen.x(), screen.y());
        const double unit = double(zoom_) / double(qMax(1, height()));  // unita' del modello per pixel
        if (pixels < 12.0) return -double(mouse.y()) * unit;
        return (screen.x() * mouse.x() + screen.y() * mouse.y()) / (pixels * pixels) * double(length);
    }
    // Direzione dal centro della vista verso l'osservatore (coordinate del modello).
    QVector3D viewerDirection() const {
        QVector3D origin, direction;
        viewRay(QPoint(width() / 2, height() / 2), origin, direction);
        return -direction.normalized();
    }
    // Chiamato quando il documento cambia (il piano della sezione puo' essersi spostato).
    void setSectionRefreshCallback(std::function<void()> callback) { sectionRefresh_ = std::move(callback); }
    // Estensione della geometria visibile lungo `normal` dal punto `point` (per il cursore della posizione).
    void sceneRangeAlong(const QVector3D &point, const QVector3D &normal, double &lo, double &hi) const {
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
    bool constraintsVisible() const { return constraintsVisible_; }
    // La finestra dei vincoli si aggiorna (selezione, vincoli, schizzo attivo cambiati).
    void setConstraintPanelCallback(std::function<void()> callback) { constraintPanelCallback_ = std::move(callback); }

    void selectPlane(int plane) { selectObject(SceneObjectKind::Plane, plane); }

    int createSketch(int plane, const QString &name) {
        recordUndo();
        SketchObject sketch;
        sketch.name = name;
        sketch.plane = plane;
        // Gli assi dello schizzo sono quelli dello schermo nella vista normale al piano.
        sketch.frame = ForgeCad::referenceSketchFrame(plane, orientation_);
        sketch.customFrame = true;
        sketches_.append(sketch);
        activeSketch_ = sketches_.size() - 1;
        documentChanged();
        beginSketchMode(plane);
        return activeSketch_;
    }

    // Nuovo schizzo sul piano di una faccia piana del corpo `body` (il piano
    // resta quello della faccia al momento della creazione). -1 se la faccia non e' piana.
    int createFaceSketch(int body, const FaceHit &face, const QString &name) {
        if (!face.planar || body < 0 || body >= extrusions_.size()) return -1;
        if (sketchMode_) endSketchMode();
        if (edgePicking_) cancelEdgePick();
        SketchObject sketch;
        sketch.name = name;
        sketch.plane = kFacePlane;
        sketch.frame = ForgeCad::faceSketchFrame(ForgeCad::Kernel::Vec3(face.point[0], face.point[1], face.point[2]),
                                                 normalized(ForgeCad::Kernel::Vec3(face.normal[0], face.normal[1], face.normal[2])),
                                                 ForgeCad::Kernel::Vec3(orientation_.up[0], orientation_.up[1], orientation_.up[2]));
        sketch.faceSource = extrusions_.at(body).name;
        recordUndo();
        sketches_.append(sketch);
        activeSketch_ = sketches_.size() - 1;
        selection_ = {};
        documentChanged();
        beginSketchMode(kFacePlane);
        return activeSketch_;
    }
    // Menu Schizzo: nuovo schizzo sulla faccia selezionata nella vista. Restituisce l'errore.
    QString createSketchOnSelectedFace(const QString &name) {
        if (selectedFace_.body < 0 || selectedFace_.body >= extrusions_.size())
            return QStringLiteral("Seleziona prima una faccia piana di un corpo (clic sulla faccia nella vista).");
        if (!selectedFace_.hit.planar) return QStringLiteral("La faccia selezionata non e' piana.");
        const SelectedFace face = selectedFace_;
        return createFaceSketch(face.body, face.hit, name) >= 0 ? QString() : QStringLiteral("Schizzo sulla faccia non riuscito.");
    }

    void selectSketch(int index) {
        if (index < 0 || index >= sketches_.size()) return;
        activeSketch_ = index;
        beginSketchMode(sketches_.at(index).plane);
    }

    void beginSketchMode(int plane) {
        activePlane_ = plane;
        selectedPlane_ = plane < 3 ? plane : -1;
        selectedFace_ = {};
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

    void endSketchMode() {
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
        if (sketchModeCallback_) sketchModeCallback_(false);
        selectionChanged();
        update();
    }

    // Rotazione dal modello allo spazio della vista standard: righe destra, alto, verso l'osservatore.
    QMatrix4x4 basisMatrix() const {
        const AxesOrientation &o = orientation_;
        return QMatrix4x4(float(o.right[0]), float(o.right[1]), float(o.right[2]), 0.0f,
                          float(o.up[0]), float(o.up[1]), float(o.up[2]), 0.0f,
                          float(o.toward[0]), float(o.toward[1]), float(o.toward[2]), 0.0f,
                          0.0f, 0.0f, 0.0f, 1.0f);
    }

    void setViewPreset(int preset) {
        roll_ = 0.0f;
        switch (preset) {
        case 0: yaw_ = 0.0f; pitch_ = 0.0f; break;
        case 1: yaw_ = 180.0f; pitch_ = 0.0f; break;
        case 2: yaw_ = -90.0f; pitch_ = 0.0f; break;  // destra: dall'asse a destra della vista frontale
        case 3: yaw_ = 0.0f; pitch_ = 90.0f; break;   // superiore: dall'alto, il fondo in alto
        case 4: yaw_ = -32.0f; pitch_ = 22.0f; break;
        case 5: yaw_ = -45.0f; pitch_ = 12.0f; break;
        default: break;
        }
        update();
    }

    // Vista con gli assi x, y dello schermo lungo `x`, `y` e l'osservatore dalla
    // parte di `normal` (terna destrorsa): la rotazione Rz(roll) Rx(pitch)
    // Ry(yaw) ha per righe x, y, normal.
    void setViewFrame(const QVector3D &worldX, const QVector3D &worldNormal) {
        // Le direzioni del modello nello spazio della vista standard (orientamento degli assi).
        const QMatrix4x4 basis = basisMatrix();
        const QVector3D x = basis.mapVector(worldX), normal = basis.mapVector(worldNormal);
        const double degrees = 180.0 / M_PI;
        const double a = std::asin(qBound(-1.0, double(normal.y()), 1.0));
        const double b = std::atan2(-double(normal.x()), double(normal.z()));
        const QVector3D u1(float(std::cos(b)), 0.0f, float(std::sin(b)));
        const QVector3D u2(float(std::sin(a) * std::sin(b)), float(std::cos(a)), float(-std::sin(a) * std::cos(b)));
        pitch_ = float(a * degrees);
        yaw_ = float(b * degrees);
        roll_ = float(std::atan2(-double(QVector3D::dotProduct(x, u2)), double(QVector3D::dotProduct(x, u1))) * degrees);
        update();
    }

    // Vista normale al piano di riferimento: dalla parte e con l'orientamento
    // degli schizzi nuovi su quel piano (X a destra, Y in alto).
    void setViewNormal(int plane) {
        const SketchFrame frame = ForgeCad::referenceSketchFrame(plane, orientation_);
        setViewFrame(QVector3D(float(frame.xAxis[0]), float(frame.xAxis[1]), float(frame.xAxis[2])),
                     QVector3D(float(frame.normal[0]), float(frame.normal[1]), float(frame.normal[2])));
    }

    // Orbita libera: trascinando in orizzontale o verticale si puo' completare
    // un giro intero. La normalizzazione evita angoli sempre piu' grandi senza
    // introdurre arresti ai poli della vista.
    static float normalizedViewAngle(float degrees) {
        return std::remainder(degrees, 360.0f);
    }
    void orbitView(const QPoint &delta) {
        yaw_ = normalizedViewAngle(yaw_ + delta.x() * 0.5f);
        pitch_ = normalizedViewAngle(pitch_ + delta.y() * 0.5f);
        roll_ *= 0.9f;
    }

    // Orientamento degli assi del documento (non e' una modifica annullabile).
    const AxesOrientation &orientation() const { return orientation_; }
    void setOrientation(const AxesOrientation &orientation) {
        orientation_ = orientation;
        setViewPreset(4);
        fitAll();
        update();
    }
    // La vista corrente diventa la vista frontale (come "Aggiorna vista standard" di SolidWorks).
    AxesOrientation orientationFromCurrentView() const {
        QMatrix4x4 rotation;
        rotation.rotate(roll_, 0.0f, 0.0f, 1.0f);
        rotation.rotate(pitch_, 1.0f, 0.0f, 0.0f);
        rotation.rotate(yaw_, 0.0f, 1.0f, 0.0f);
        rotation *= basisMatrix();
        // Righe della rotazione: le direzioni del modello a destra, in alto e verso l'osservatore.
        AxesOrientation result;
        double *rows[3] = {result.right, result.up, result.toward};
        for (int r = 0; r < 3; ++r) {
            double length = 0.0;
            for (int k = 0; k < 3; ++k) length += double(rotation(r, k)) * double(rotation(r, k));
            length = std::sqrt(length);
            for (int k = 0; k < 3; ++k) rows[r][k] = double(rotation(r, k)) / length;
        }
        return result;
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
            makeCurrent();
            displayCache_.clear();
            msaaBuffer_.reset();
            resolveBuffer_.reset();
            doneCurrent();
        }, Qt::DirectConnection);
        if (rendererCallback_) {
            rendererCallback_(QString::fromLatin1(
                reinterpret_cast<const char *>(glGetString(GL_RENDERER))));
        }
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_NORMALIZE);
        glDepthFunc(GL_LEQUAL);
        glShadeModel(GL_SMOOTH);
        glClearColor(0.025f, 0.035f, 0.050f, 1.0f);
        // 0 se i framebuffer multisample non ci sono (la chiamata fallisce e il valore resta 0).
        GLint samples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &samples);
        while (glGetError() != GL_NO_ERROR) {
        }
        maxSamples_ = samples;
        initializeGlassShader();
    }

    void resizeGL(int width, int height) override {
        glViewport(0, 0, width, height);
        if (autoFit_ && painted_ && !sketchMode_) fitView(sceneGeometryPoints());
    }

    void paintGL() override {
        displayCache_.beginFrame();
        if (!painted_) {
            painted_ = true;
            if (!pendingFit_.isEmpty()) fitView(pendingFit_);
            pendingFit_.clear();
        }
        // Con l'antialiasing la scena OpenGL va nel framebuffer multisample;
        // le sovrapposizioni QPainter si disegnano dopo, direttamente nel widget.
        GLint viewport[4] = {0, 0, width(), height()};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const QSize pixels(qMax(1, viewport[2]), qMax(1, viewport[3]));
        QOpenGLFramebufferObject *scene = sceneBuffer(pixels);
        if (scene) {
            scene->bind();
            glViewport(0, 0, pixels.width(), pixels.height());
            glEnable(GL_MULTISAMPLE);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        drawBackgroundGradient();
        const float aspect = float(width()) / float(qMax(1, height()));
        const float viewScale = zoom_ / 8.0f;
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        const double depth = sceneDepth();
        glOrtho(-4.0 * aspect * viewScale, 4.0 * aspect * viewScale,
            -4.0 * viewScale, 4.0 * viewScale, double(zoom_) - depth, double(zoom_) + depth);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(panX_, panY_, -zoom_);
        glRotatef(roll_, 0.0f, 0.0f, 1.0f);
        glRotatef(pitch_, 1.0f, 0.0f, 0.0f);
        glRotatef(yaw_, 0.0f, 1.0f, 0.0f);
        glMultMatrixf(basisMatrix().constData());
        drawReferencePlanes();
        drawDatumPlanes();
        drawGrid();
        if (!axesOnTop_) drawAxes();
        configureLighting();
        drawExtrusions();
        drawPickedEdges();
        drawSketch();
        drawSnapMarkers();
        if (axesOnTop_) drawAxes();
        makeOpaque();
        if (scene) {
            scene->release();
            QOpenGLFramebufferObject::blitFramebuffer(resolveBuffer_.get(), msaaBuffer_.get());
            QOpenGLFramebufferObject::blitFramebuffer(nullptr, QRect(QPoint(), pixels), resolveBuffer_.get(), QRect(QPoint(), pixels));
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        }
        drawGpuGlassPanels(pixels, scene ? resolveBuffer_.get() : nullptr);
        drawReferenceLabels();
        drawSelectionHighlight();
        drawPlaneResizeHandles();
        ++renderedFrameSerial_;
    }

    void keyPressEvent(QKeyEvent *event) override {
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

    void mousePressEvent(QMouseEvent *event) override {
        lastMousePosition_ = event->position().toPoint();
        if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && panKeyHeld_)) {
            panning_ = true;
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        // Nello schizzo il tasto destro trascinato ruota la vista (un clic resta il menu / la chiusura della spline).
        if (sketchMode_ && event->button() == Qt::RightButton) {
            rightPressPosition_ = lastMousePosition_;
            rightDragged_ = false;
            contextPending_ = false;
            return;
        }
        if (event->button() == Qt::LeftButton && beginPlaneResize(lastMousePosition_)) return;
        // Il piano di sezione si trascina dalla maniglia o dal bordo.
        if (!sketchMode_ && !refPicking_ && !edgePicking_ && event->button() == Qt::LeftButton && sectionHandleAt(lastMousePosition_)) {
            sectionDragging_ = true;
            setCursor(Qt::SizeAllCursor);
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
                    body = edgeBody, source = 1, point = {p.x(), p.y(), p.z()};
                } else if (pickBodyFace(faceBody, lastMousePosition_, face)) {
                    QVector3D origin, direction;
                    viewRay(lastMousePosition_, origin, direction);
                    const QVector3D p = origin + direction.normalized() * float(face.distance);
                    body = faceBody, source = 2, point = {p.x(), p.y(), p.z()};
                    // Il punto esatto lungo il raggio (in double).
                    const double length = double(direction.length());
                    point = {double(origin.x()) + double(direction.x()) / length * face.distance, double(origin.y()) + double(direction.y()) / length * face.distance,
                             double(origin.z()) + double(direction.z()) / length * face.distance};
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
                }
                if (pickedEdges_.contains(edge)) pickedEdges_.removeAll(edge);
                else pickedEdges_.append(edge);
            } else if (pickBodyFace(faceBody, lastMousePosition_, face)) {
                if (faceBody != edgePickBody_) {
                    edgePickBody_ = faceBody;
                    pickedEdges_.clear();
                }
                const QVector<int> border = faceDisplayEdges(edgePickBody_, face);
                bool all = !border.isEmpty();
                for (int index : border) all = all && pickedEdges_.contains(index);
                for (int index : border) {
                    pickedEdges_.removeAll(index);
                    if (!all) pickedEdges_.append(index);
                }
            }
            edgePicked();
            return;
        }
        if (!sketchMode_ && event->button() == Qt::LeftButton) {
            // Maiusc: riquadro trascinando, oppure un clic che aggiunge o toglie l'oggetto.
            if (event->modifiers() & Qt::ShiftModifier) {
                armBoxSelection(lastMousePosition_, true);
                return;
            }
            selectedObjects_.clear();
            selection_ = pickSceneObject(lastMousePosition_);
            if (selection_.kind == SceneObjectKind::Plane) selectedPlane_ = selection_.index;
            // Sul corpo si seleziona anche la faccia colpita (bordi evidenziati).
            selectedFace_ = {};
            if (selection_.kind == SceneObjectKind::Extrusion && pickBodyFace(selection_.index, lastMousePosition_, selectedFace_.hit)) {
                selectedFace_.body = selection_.index;
                showStatus(QStringLiteral("%1: faccia %2%3 (%4 bordi)")
                               .arg(extrusions_.at(selection_.index).name)
                               .arg(selectedFace_.hit.face + 1)
                               .arg(selectedFace_.hit.planar ? QStringLiteral(" piana") : QString())
                               .arg(selectedFace_.hit.edges.size()));
            }
            if (selectionCallback_) selectionCallback_(selection_);
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
                // Ctrl indica in modo esplicito la scelta di un punto per i
                // vincoli. Deve vincere sulle etichette delle quote che
                // possono coprire il centro di cerchi e poligoni.
                if ((event->modifiers() & Qt::ControlModifier) && selectPointWithControl(rawPoint)) return;
                // Un clic sul simbolo di un vincolo lo seleziona (Maiusc aggiunge o toglie).
                const int glyph = constraintAt(lastMousePosition_);
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
                int referenceKind = -1, referenceIndex = -1;
                if (findSketchElement(rawPoint).kind < 0 && pickSketchReferencePlaneAxis(lastMousePosition_, referenceKind, referenceIndex)) {
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
                const bool pointSnap = lastSnapKind_ == SnapKind::Endpoint || lastSnapKind_ == SnapKind::Midpoint;
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

    // Estremo di segmento dello schizzo attivo entro la tolleranza di selezione.
    bool segmentEndpointAt(const QPointF &point, QPointF &endpoint) const {
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

    // Aggancio del punto trascinato: agli altri punti e segmenti (non a se
    // stesso ne' ai segmenti che vi arrivano), all'origine, alla griglia.
    QPointF dragSnapPoint(const QPointF &raw) {
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
        const ForgeCad::SnapResult result = ForgeCad::snapSegments(raw, segments, points, snapEnabled_, true, snapSpacing_, pickTolerance(10.0));
        lastSnapKind_ = result.kind;
        lastSnapPoint_ = result.point;
        return result.point;
    }

    // Sposta il punto trascinato (e i punti coincidenti) in `target`; i
    // segmenti orizzontali/verticali collegati restano tali (moveSketchPoint).
    void dragSketchPoint(const QPointF &target) {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
        const QPointF delta = target - pointDragPosition_;
        if (pointLength(delta) <= 0.0) return;
        if (!dragRecorded_) {
            history_.record(dragSnapshot_);
            dragRecorded_ = true;
        }
        SketchObject &sketch = sketches_[activeSketch_];
        const SketchObject before = sketch;
        moveSketchPoint(sketch, -1, pointDragPosition_, delta, QPointF(qQNaN(), qQNaN()));
        // Il risolutore tiene i punti trascinati sul cursore e adatta il resto;
        // se i vincoli non lo permettono il punto resta dov'era.
        QString failure;
        if (!solveActive(targetsAt(sketch, target), before, &failure)) {
            showStatus(QStringLiteral("Il punto e' bloccato dai vincoli."));
            return;
        }
        pointDragPosition_ = target;
        sceneBoundsDirty_ = true;
    }

    // Trascinamento di un segmento intero: i due estremi si spostano insieme
    // (e con loro i punti collegati). Parte dopo qualche pixel di movimento.
    void dragSketchSegment(const QPointF &raw) {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
        SketchObject &sketch = sketches_[activeSketch_];
        if (bodyDragSegment_ >= sketch.segments.size()) return;
        QPointF delta = raw - bodyDragLast_;
        if (!bodyDragMoved_ && pointLength(delta) < pickTolerance(3.0)) return;
        bodyDragMoved_ = true;
        if (!dragRecorded_) {
            history_.record(dragSnapshot_);
            dragRecorded_ = true;
        }
        const QPointF first = sketch.segments.at(bodyDragSegment_).first, second = sketch.segments.at(bodyDragSegment_).second;
        const QPointF nowhere(qQNaN(), qQNaN());
        const SketchObject before = sketch;
        moveSketchPoint(sketch, bodyDragSegment_, first, delta, nowhere);
        moveSketchPoint(sketch, bodyDragSegment_, second, delta, nowhere);
        QVector<ForgeCad::PointTarget> targets = targetsAt(sketch, first + delta);
        targets += targetsAt(sketch, second + delta);
        if (!solveActive(targets, before)) {
            showStatus(QStringLiteral("Il segmento e' bloccato dai vincoli."));
            return;
        }
        bodyDragLast_ = raw;
        sceneBoundsDirty_ = true;
    }

    bool isEditTool() const {
        return drawingTool_ == DrawingTool::Trim || drawingTool_ == DrawingTool::Extend || drawingTool_ == DrawingTool::Split
            || drawingTool_ == DrawingTool::Fillet || drawingTool_ == DrawingTool::Chamfer;
    }

    QString editToolLabel() const {
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

    // Clic con uno strumento di modifica (taglia, estendi, spezza, raccordo,
    // smusso). La modifica si fa su una copia dello schizzo (cad_sketch_edit):
    // se riesce diventa un passo di Undo, altrimenti il messaggio va nella barra di stato.
    void applyEditTool(const QPointF &point) {
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
        if (!result.segmentMap.isEmpty()) remapRevolutionAxes(activeSketch_, result.segmentMap);
        sketchSelections_.clear();
        selectedPoints_.clear();
        selectedConstraints_.clear();
        constraintHover_ = -1;
        sketchHover_ = findSketchElement(point);
        trimPreview_.clear();
        showStatus(QString());
        sketchEdited();
    }

    void showStatus(const QString &message) {
        if (statusCallback_) statusCallback_(message);
    }

    // Assi delle rivoluzioni dello schizzo dopo l'eliminazione di segmenti.
    void remapRevolutionAxes(int sketchIndex, const QVector<int> &segmentMap) {
        for (ExtrusionObject &body : extrusions_)
            if (body.feature == BodyFeature::Revolution && body.sketchIndex == sketchIndex && body.revolveAxis >= 0)
                body.revolveAxis = body.revolveAxis < segmentMap.size() ? segmentMap.at(body.revolveAxis) : -3;
    }

    // Vincolo che il segmento in costruzione ricevera': dall'aggancio del
    // punto (estremo, punto medio, su un segmento) e, in modalita'
    // automatica, dalla direzione (orizzontale, verticale, perpendicolare al
    // segmento da cui parte, parallelo a un segmento esistente, entro 4
    // gradi). Le quote di lunghezza e angolo e i vincoli espliciti H/V vincono.
    struct LineInference {
        QPointF point;
        int constraint = -1;  // -1 libero, 1 orizzontale, 2 verticale, 3 perpendicolare, 4 parallelo
        int reference = -1;   // segmento di riferimento (perpendicolare, parallelo, su segmento)
        QStringList labels;
    };

    // Segmento su cui sta il punto (estremo, punto medio o interno), -1 se nessuno.
    int segmentAt(const QPointF &point, int exclude = -1) const {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return -1;
        const SketchObject &sketch = sketches_.at(activeSketch_);
        for (int index = 0; index < sketch.segments.size(); ++index)
            if (index != exclude && distanceToSegment(point, sketch.segments.at(index).first, sketch.segments.at(index).second) <= 1e-9)
                return index;
        return -1;
    }

    // Etichetta dell'aggancio; sull'origine del piano "Origine".
    QString snapLabelAt(SnapKind kind, const QPointF &point) const {
        if (!lastSnapNote_.isEmpty() && point == lastSnapPoint_ && kind == lastSnapKind_) return lastSnapNote_;
        return snapLabel(kind);
    }

    static QString snapLabel(SnapKind kind) {
        switch (kind) {
        case SnapKind::Endpoint: return QStringLiteral("● Coincidente");
        case SnapKind::Midpoint: return QStringLiteral("◐ Punto medio");
        case SnapKind::Nearest: return QStringLiteral("∈ Su segmento");
        default: return {};
        }
    }

    LineInference inferLinePoint(const QPointF &raw) {
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

    // Vincoli del segmento appena aggiunto con i punti su cui i suoi estremi si
    // sono agganciati: coincidenze con estremi di altri segmenti e punti delle
    // curve; punto medio o punto su un altro segmento, punto su un cerchio,
    // arco o ellisse.
    void recordCoincidences(SketchObject &sketch, int segment) {
        recordPointCoincidences(sketch, {0, segment, 0}, sketch.segments.at(segment).first);
        recordPointCoincidences(sketch, {0, segment, 1}, sketch.segments.at(segment).second);
    }

    // Vincoli del punto `here` (in `point`) con le altre entita': coincidente
    // con i punti che tocca, altrimenti punto medio o punto su un segmento, o
    // su un cerchio o arco.
    void recordPointCoincidences(SketchObject &sketch, const ConstraintRef &here, const QPointF &point) {
        const double tolerance = ForgeCad::kSketchConnectionTolerance;
        const auto add = [&](ConstraintType type, const ConstraintRef &b) {
            SketchConstraint c;
            c.type = type;
            c.first = here;
            c.second = b;
            sketch.geometricConstraints.append(c);
        };
        const auto self = [&](int kind, int element) { return here.kind == kind && here.element == element; };
        bool onPoint = false;
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
            for (int k = 0; k < sketch.curves.at(curve).controlPoints.size(); ++k)
                if (pointDistance(sketch.curves.at(curve).controlPoints.at(k), point) <= tolerance) {
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
            if (self(1, curve)) continue;
            const CurveObject &c = sketch.curves.at(curve);
            if (c.construction && c.tool == DrawingTool::Polygon) continue;
            if ((c.tool == DrawingTool::Circle || c.tool == DrawingTool::Arc) && c.controlPoints.size() >= 2
                && std::abs(pointDistance(point, c.controlPoints.at(0)) - pointDistance(c.controlPoints.at(1), c.controlPoints.at(0))) <= 1e-9
                && (c.tool != DrawingTool::Arc || onArc(c, std::atan2(point.y() - c.controlPoints.at(0).y(), point.x() - c.controlPoints.at(0).x()))))
                add(ConstraintType::PointOnCurve, {1, curve, -1});
        }
    }

    // Arco per tre punti (inizio, fine, un punto di passaggio) nel formato
    // dell'arco: centro, inizio, fine in senso antiorario. Il centro e' il
    // circocentro dei tre punti; se il passaggio sta a destra della corda
    // inizio -> fine l'arco gira in senso orario e i due estremi si scambiano.
    // Falso se i punti sono allineati.
    static bool threePointArc(const QPointF &start, const QPointF &end, const QPointF &through, CurveObject &arc) {
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

    // Arco che parte da `start` tangente alla direzione `direction` (unitaria)
    // e finisce in `end`: il centro sta sulla normale in `start`, a distanza
    // r = |d|^2 / (2 n.d) con d = end - start (con segno: a sinistra della
    // direzione l'arco gira in senso antiorario). In `startPoint` il punto
    // dell'arco (1 o 2) che sta in `start`. Falso se `end` sta sulla tangente.
    static bool tangentArc(const QPointF &start, const QPointF &direction, const QPointF &end, CurveObject &arc, int *startPoint = nullptr) {
        const QPointF d = end - start, normal(-direction.y(), direction.x());
        const double length = pointLength(d), across = normal.x() * d.x() + normal.y() * d.y();
        if (length <= ForgeCad::kSketchConnectionTolerance || std::abs(across) <= 1e-9 * length) return false;
        const QPointF center = start + normal * (length * length / (2.0 * across));
        arc.tool = DrawingTool::Arc;
        arc.controlPoints = across > 0.0 ? QVector<QPointF>{center, start, end} : QVector<QPointF>{center, end, start};
        if (startPoint) *startPoint = across > 0.0 ? 1 : 2;
        return true;
    }

    // Da dove puo' partire un arco tangente: gli estremi di segmenti e archi
    // in `point`, con la direzione che prosegue l'entita' oltre l'estremo.
    struct TangentStart {
        ConstraintRef entity;  // il segmento o l'arco (point -1)
        QPointF direction;
    };
    QVector<TangentStart> tangentStartsAt(const QPointF &point) const {
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
    // Tra le entita' che finiscono nel punto di partenza, quella la cui
    // direzione va di piu' verso il cursore (in un angolo tra due segmenti
    // l'arco continua quello da cui ci si allontana).
    TangentStart tangentStartFor(const QPointF &cursor) const {
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

    // Crea l'arco per tre punti o l'arco tangente: un Arc (centro, inizio,
    // fine) con le coincidenze degli estremi e, per quello tangente, il
    // vincolo di tangenza con l'entita' da cui parte.
    void finalizeDrawnArc() {
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

    // Anteprima della curva in costruzione (cerchio, arco, poligono, spline,
    // NURBS) con il cursore come punto successivo. Solo per disegnare: la
    // curva vera si crea al clic (finalizePrimitive / finalizeCurve).
    bool previewCurve(CurveObject &curve) const {
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
            if (curve.tool == DrawingTool::Spline) ForgeCad::initializeTangentHandles(curve);
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

    // Quote dell'anteprima, calcolate sulla geometria esatta.
    QStringList previewMeasures() const {
        CurveObject curve;
        if (!previewCurve(curve)) return {};
        const QVector<QPointF> &p = curve.controlPoints;
        const auto number = [](double value) { return QString::number(value, 'f', 4); };
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

    // Anteprima tratteggiata della curva in costruzione e del raggio.
    void drawCurvePreview(QPainter &painter, const SketchObject &sketch) const {
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
        painter.setPen(QPen(QColor(255, 150, 60, radiusOnly ? 110 : 230), 2.0, radiusOnly ? Qt::DotLine : Qt::DashLine, Qt::RoundCap));
        const QVector<QPointF> screen = projectSketchPolyline(curve.samples, sketch);
        painter.drawPolyline(screen.data(), int(screen.size()));
    }

    // Etichette del vincolo vicino al cursore (e il segmento di riferimento tratteggiato).
    void drawInferenceTags(QPainter &painter) const {
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
        painter.setFont(QFont(QStringLiteral("Sans"), 9, QFont::DemiBold));
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

    // Quote di cerchi, archi e poligoni: raggio e centro (poligono: raggio
    // circoscritto e lati). I punti collegati (estremi di segmenti sul centro
    // o sui punti della curva) seguono con moveSketchPoint.
    // Quote dell'ellisse: semiassi, angolo del primo semiasse e centro.
    QString editEllipseDimension(int index) {
        SketchObject &sketch = sketches_[activeSketch_];
        const CurveObject curve = sketch.curves.at(index);
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
        const SketchObject beforeEdit = sketch;
        // I punti coincidenti con il centro lo seguono.
        if (pointDistance(center, newCenter) > 0.0) moveSketchPoint(sketch, -1, center, newCenter - center, QPointF(qQNaN(), qQNaN()));
        sketch.curves[index] = changed;
        QString failure;
        if (!solveActive(curveTargets(sketch, index), beforeEdit, &failure)) return failure;
        sketchEdited();
        return {};
    }

    QString editCurveDimension(int index) {
        if (!sketchMode_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return QStringLiteral("Entra in modalita' schizzo.");
        SketchObject &sketch = sketches_[activeSketch_];
        if (index < 0 || index >= sketch.curves.size()) return QStringLiteral("Seleziona prima un cerchio, un arco o un poligono.");
        const CurveObject curve = sketch.curves.at(index);
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

    // Doppio clic su un segmento dello schizzo: la sua quota.
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (sketchMode_ && sketchViewUnlocked_) return;
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

    void contextMenuEvent(QContextMenuEvent *event) override {
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
                QMenu menu(this);
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
                if (remove) remove->setEnabled(source.controlPoints.size() > (source.tool == DrawingTool::Nurbs ? 4 : 2));
                const QAction *chosen = menu.exec(event->globalPos());
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
                        source.tangentLinked.insert(at, false);
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
                    const double value = QInputDialog::getDouble(this, QStringLiteral("Quota maniglia"), QStringLiteral("Lunghezza:"), old,
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
        if (interactionLocked_ || refPicking_) {
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
            if (hit.kind == SceneObjectKind::Sketch || hit.kind == SceneObjectKind::Extrusion) {
                QMenu menu(this);
                QAction *editSketch = hit.kind == SceneObjectKind::Sketch
                    ? menu.addAction(QStringLiteral("Modifica schizzo")) : nullptr;
                QAction *editBody = hit.kind == SceneObjectKind::Extrusion && editBodyCallback_
                    ? menu.addAction(QStringLiteral("Modifica parametri...")) : nullptr;
                const bool datum = hit.kind == SceneObjectKind::Extrusion && isDatumBody(extrusions_.at(hit.index));
                QAction *datumSketch = datum ? menu.addAction(QStringLiteral("Nuovo schizzo sul piano")) : nullptr;
                if (datumSketch) datumSketch->setEnabled(extrusions_.at(hit.index).datumValid);
                // Faccia sotto il puntatore: schizzo sul suo piano, raccordo o smusso dei suoi bordi.
                FaceHit face;
                const bool onFace = hit.kind == SceneObjectKind::Extrusion && !datum && pickBodyFace(hit.index, event->pos(), face);
                QAction *faceSketch = nullptr, *faceFillet = nullptr, *faceChamfer = nullptr;
                if (onFace) {
                    menu.addSeparator();
                    faceSketch = menu.addAction(QStringLiteral("Nuovo schizzo sulla faccia"));
                    faceSketch->setEnabled(face.planar);
                    const bool solid = extrusions_.at(hit.index).solid && !face.edges.isEmpty() && edgePickFinished_;
                    faceFillet = menu.addAction(QStringLiteral("Raccordo dei bordi della faccia..."));
                    faceChamfer = menu.addAction(QStringLiteral("Smusso dei bordi della faccia..."));
                    faceFillet->setEnabled(solid);
                    faceChamfer->setEnabled(solid);
                    menu.addSeparator();
                }
                QAction *hide = menu.addAction(QStringLiteral("Nascondi"));
                QAction *remove = menu.addAction(QStringLiteral("Elimina"));
                const QAction *chosen = menu.exec(event->globalPos());
                if (chosen && chosen == editSketch) selectSketch(hit.index);
                else if (chosen && chosen == editBody) editBodyCallback_(hit.index);
                else if (chosen && chosen == datumSketch) createDatumSketch(hit.index, QStringLiteral("Schizzo %1").arg(sketches_.size() + 1));
                else if (chosen && chosen == faceSketch)
                    createFaceSketch(hit.index, face, QStringLiteral("Schizzo %1").arg(sketches_.size() + 1));
                else if (chosen && (chosen == faceFillet || chosen == faceChamfer))
                    edgePickFinished_(hit.index, face.edges, chosen == faceChamfer);
                else if (chosen && chosen == remove) deleteObject(hit.kind, hit.index);
                else if (chosen == hide) setObjectVisible(hit.kind, hit.index, false);
                event->accept();
                return;
            }
        }
        QOpenGLWidget::contextMenuEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
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
            autoFit_ = false;
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
            const float unit = 8.0f * (zoom_ / 8.0f) / float(qMax(1, height()));
            const QPoint delta = currentPosition - lastMousePosition_;
            autoFit_ = false;
            panX_ += float(delta.x()) * unit;
            panY_ -= float(delta.y()) * unit;
            lastMousePosition_ = currentPosition;
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
            autoFit_ = false;
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
                    sketch.geometricConstraints[dimensionDrag_].placement = rawPoint;
                    sketch.geometricConstraints[dimensionDrag_].placed = true;
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
                else
                    unsetCursor();
                draggingCurveIndex_ = -1;
                draggingControlIndex_ = -1;
                draggingPointKind_ = EditablePointKind::Control;
            }
            if (draggingControlPoint_ && draggingCurveIndex_ >= 0) {
                if (!dragRecorded_) {
                    history_.record(dragSnapshot_);
                    dragRecorded_ = true;
                }
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
                    curve.controlPoints[draggingControlIndex_] = position;
                    normalizeEllipse(curve, draggingControlIndex_, before);
                    SketchObject &sketch = sketches_[activeSketch_];
                    if (!solveActive({{{1, draggingCurveIndex_, draggingControlIndex_}, curve.controlPoints.at(draggingControlIndex_)}}, sketchBefore)) {
                        showStatus(QStringLiteral("Il punto e' bloccato dai vincoli."));
                        update();
                        return;
                    }
                    (void)sketch;
                } else {
                    // La maniglia passa dal risolutore: i vincoli sulle maniglie (tangenze, quote) restano veri.
                    const int side = draggingPointKind_ == EditablePointKind::TangentIn ? 0 : 1;
                    (side == 0 ? curve.tangentHandles[draggingControlIndex_].first : curve.tangentHandles[draggingControlIndex_].second) = rawPoint;
                    if (curve.tangentLinked.value(draggingControlIndex_)) {
                        const QPointF center = curve.controlPoints.at(draggingControlIndex_);
                        const QPointF direction = rawPoint - center;
                        const double length = pointLength(direction);
                        QPointF &other = side == 0 ? curve.tangentHandles[draggingControlIndex_].second
                                                   : curve.tangentHandles[draggingControlIndex_].first;
                        const double otherLength = pointDistance(center, other);
                        if (length > 0.0) other = center - direction * (otherLength / length);
                    }
                    if (!solveActive({{{1, draggingCurveIndex_, handlePoint(draggingControlIndex_, side)}, rawPoint}}, sketchBefore)) {
                        showStatus(QStringLiteral("La maniglia e' bloccata dai vincoli."));
                        update();
                        return;
                    }
                }
                ForgeCad::recalculateCurve(curve, tessellationQuality_);
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
        if (refPicking_ && !(event->buttons() & Qt::LeftButton)) {
            GeometryRef ref;
            FaceHit face;
            int faceBody = -1;
            const bool valid = pickReference(currentPosition, ref, &face, &faceBody);
            refHoverValid_ = valid;
            refHover_ = ref;
            refHoverFace_ = face;
            refHoverFaceBody_ = valid && ref.kind == 5 ? faceBody : -1;
            update();
        }
        if (edgePicking_ && !(event->buttons() & Qt::LeftButton)) {
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
        if (!(event->buttons() & Qt::LeftButton)) {
            const SceneSelection hover = pickSceneObject(currentPosition);
            if (hover != hover_) {
                hover_ = hover;
                update();
            }
        }
        if (event->buttons() & Qt::LeftButton) {
            const QPoint delta = currentPosition - lastMousePosition_;
            autoFit_ = false;
            orbitView(delta);  // la vista inclinata di uno schizzo su faccia si raddrizza ruotando
            update();
        }
        lastMousePosition_ = currentPosition;
    }

    // Il tasto del pan vince sulle scorciatoie dei menu (per esempio una lettera).
    bool event(QEvent *event) override {
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

    void keyReleaseEvent(QKeyEvent *event) override {
        if (event->key() == panKey_ && !event->isAutoRepeat()) {
            panKeyHeld_ = false;
            if (!panning_) unsetCursor();
            return;
        }
        QOpenGLWidget::keyReleaseEvent(event);
    }

    void focusOutEvent(QFocusEvent *event) override {
        panKeyHeld_ = false;
        panning_ = false;
        unsetCursor();
        QOpenGLWidget::focusOutEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
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

    void leaveEvent(QEvent *event) override {
        if (hover_.kind != SceneObjectKind::None || sketchHover_.kind >= 0) {
            hover_ = {};
            sketchHover_ = {};
            update();
        }
        QOpenGLWidget::leaveEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override {
        if (!wheelZoomEnabled_) { event->ignore(); return; }
        const QPoint angleDelta = event->angleDelta();
        const QPoint pixelDelta = event->pixelDelta();
        const int delta = angleDelta.y() != 0 ? angleDelta.y() : pixelDelta.y();
        if (delta == 0) { event->ignore(); return; }
        autoFit_ = false;
        setZoom(zoom_ * std::pow(0.9985f, float(delta)));
        event->accept();
    }

private:
    DocumentState documentState() const {
        DocumentState state;
        state.sketches = sketches_;
        state.extrusions = extrusions_;
        state.modelBodies = modelBodies_;
        state.orientation = orientation_;
        state.orientationSet = true;
        return state;
    }

    void selectionChanged() {
        if (constraintPanelCallback_) constraintPanelCallback_();
    }

    // Risolve i vincoli dello schizzo attivo con i bersagli dati; se non ci
    // riesce lo schizzo torna a `before`. Le curve si ricampionano.
    bool solveActive(const QVector<ForgeCad::PointTarget> &targets, const SketchObject &before, QString *error = nullptr) {
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
    // Bersagli: tutti i punti dello schizzo che stanno in `position`, fermi li'.
    static QVector<ForgeCad::PointTarget> targetsAt(const SketchObject &sketch, const QPointF &position) {
        QVector<ForgeCad::PointTarget> targets;
        const double tolerance = ForgeCad::kSketchConnectionTolerance;
        for (int i = 0; i < sketch.segments.size(); ++i) {
            if (pointDistance(sketch.segments.at(i).first, position) <= tolerance) targets.append({{0, i, 0}, position});
            if (pointDistance(sketch.segments.at(i).second, position) <= tolerance) targets.append({{0, i, 1}, position});
        }
        for (int c = 0; c < sketch.curves.size(); ++c)
            for (int k = 0; k < sketch.curves.at(c).controlPoints.size(); ++k)
                if (pointDistance(sketch.curves.at(c).controlPoints.at(k), position) <= tolerance) targets.append({{1, c, k}, position});
        return targets;
    }
    // Bersagli: i punti della curva dove stanno ora.
    static QVector<ForgeCad::PointTarget> curveTargets(const SketchObject &sketch, int curve) {
        QVector<ForgeCad::PointTarget> targets;
        for (int k = 0; k < sketch.curves.at(curve).controlPoints.size(); ++k) targets.append({{1, curve, k}, sketch.curves.at(curve).controlPoints.at(k)});
        return targets;
    }
    // Le quote di raggio e diametro della curva prendono il valore attuale.
    static void syncCurveDimensions(SketchObject &sketch, int curve) {
        for (SketchConstraint &c : sketch.geometricConstraints)
            if ((c.type == ConstraintType::Radius || c.type == ConstraintType::Diameter) && c.first.kind == 1 && c.first.element == curve)
                c.value = ForgeCad::currentMeasure(sketch, c);
    }

    // Quote come nel disegno tecnico (coordinate schermo): linee di misura, di
    // riferimento e di richiamo, archi, frecce e testo.
    struct DimensionGraphic {
        QPainterPath lines;
        QVector<QPolygonF> arrows;
        QString text;
        QPointF textCenter;
        double textAngle = 0.0;  // gradi
        QPolygonF textBox;       // per la scelta con il mouse
    };
    static QString dimensionText(const SketchConstraint &c) {
        auto number = [](double v) {
            QString text = QString::number(v, 'f', 3);
            while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) text.chop(1);
            return text;
        };
        switch (c.type) {
        case ConstraintType::Radius:
        case ConstraintType::AxisRadius: return QStringLiteral("R") + number(c.value);
        case ConstraintType::Diameter:
        case ConstraintType::AxisDiameter: return QStringLiteral("\u2300") + number(c.value);
        case ConstraintType::Angle: return number(std::fabs(c.value)) + QStringLiteral("\u00B0");
        default: return number(c.value);
        }
    }
    // Freccia con la punta in `tip` e il corpo dalla parte di `from` (schermo).
    static QPolygonF arrowHead(const QPointF &tip, const QPointF &from) {
        const QPointF d = from - tip;
        const double l = pointLength(d);
        if (l <= 0.0) return {};
        const QPointF u = d / l, n(-u.y(), u.x());
        const double length = 9.0, half = 3.0;
        return QPolygonF({tip, tip + length * u + half * n, tip + length * u - half * n});
    }
    bool dimensionGraphic(const SketchObject &sketch, int index, DimensionGraphic &g) const {
        return dimensionGraphic(sketch, sketch.geometricConstraints.at(index), g);
    }
    // Anche per una quota che non e' (ancora) nello schizzo: l'anteprima dello strumento Quota.
    bool dimensionGraphic(const SketchObject &sketch, const SketchConstraint &c, DimensionGraphic &g) const {
        if (!ForgeCad::isDimension(c.type)) return false;
        const double px = double(zoom_) / double(qMax(1, height()));  // unita' dello schizzo per pixel
        const auto screen = [&](const QPointF &p) { return projectWorldPoint(mapSketchPoint(p, sketch)); };
        const QFontMetricsF metrics(QFont(QStringLiteral("Sans"), 9, QFont::DemiBold));
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
        if (c.type == ConstraintType::Distance || c.type == ConstraintType::AxisRadius || c.type == ConstraintType::AxisDiameter || projected) {
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
        // direzione per il valore del vincolo (con segno).
        QPointF p0, p1, q0, q1;
        if (!ForgeCad::constraintLines(sketch, c, p0, p1, q0, q1)) return false;
        const QPointF d1 = p1 - p0, d2 = q1 - q0;
        const double denominator = d1.x() * d2.y() - d1.y() * d2.x();
        if (std::fabs(denominator) <= 1e-12 * pointLength(d1) * pointLength(d2)) return false;
        const QPointF r = q0 - p0;
        const QPointF vertex = p0 + d1 * ((r.x() * d2.y() - r.y() * d2.x()) / denominator);
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
            const double near = std::max(0.0, std::min(ta, tb)), far = std::max(ta, tb);
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
    // Quota sotto il puntatore (testo o linee), -1 se nessuna.
    int dimensionAt(const QPoint &position) const {
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
    void drawDimensions(QPainter &painter) const {
        const SketchObject *sketch = activeSketchObject();
        if (!sketch || !constraintsVisible_) return;
        painter.save();
        painter.setFont(QFont(QStringLiteral("Sans"), 9, QFont::DemiBold));
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

    // Simboli dei vincoli accanto alle entita' (coordinate schermo), per disegnarli e sceglierli.
    struct ConstraintGlyph {
        QRectF rect;
        int constraint = -1;
        bool dot = false;  // coincidenza: un punto invece di un'etichetta
        QString text;
    };
    QVector<ConstraintGlyph> constraintGlyphs() const {
        QVector<ConstraintGlyph> glyphs;
        const SketchObject *sketch = activeSketchObject();
        if (!sketch || !constraintsVisible_) return glyphs;
        const QFontMetricsF metrics(QFont(QStringLiteral("Sans"), 8, QFont::DemiBold));
        QHash<QString, int> stacked;  // quante etichette ci sono gia' accanto allo stesso punto
        for (int index = 0; index < sketch->geometricConstraints.size(); ++index) {
            const SketchConstraint &c = sketch->geometricConstraints.at(index);
            DimensionGraphic dimension;
            if (dimensionGraphic(*sketch, index, dimension)) continue;  // quota disegnata come nel disegno tecnico
            QString text = ForgeCad::constraintSymbol(c.type);
            if (c.type == ConstraintType::Pattern) text += QLatin1Char(' ') + ForgeCad::patternSummary(c.pattern);
            else if (c.type == ConstraintType::Angle) text += QStringLiteral(" %1\u00B0").arg(c.value, 0, 'f', 2);
            else if (ForgeCad::isDimension(c.type)) text += QStringLiteral(" %1").arg(c.value, 0, 'f', 3);
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
    int constraintAt(const QPoint &position) const {
        const int dimension = dimensionAt(position);
        if (dimension >= 0) return dimension;
        const QVector<ConstraintGlyph> glyphs = constraintGlyphs();
        for (int k = glyphs.size() - 1; k >= 0; --k)
            if (glyphs.at(k).rect.adjusted(-2.0, -2.0, 2.0, 2.0).contains(QPointF(position))) return glyphs.at(k).constraint;
        return -1;
    }
    void drawSketchConstraints(QPainter &painter) const {
        painter.save();
        painter.setFont(QFont(QStringLiteral("Sans"), 8, QFont::DemiBold));
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
    // Entita' e punti del vincolo evidenziati.
    void highlightConstraintEntities(QPainter &painter, const SketchObject &sketch, int index, const QColor &color) const {
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

    // Salva lo stato corrente nella cronologia: va chiamata subito prima di
    // una modifica al documento, seguita da documentChanged() a modifica fatta.
    void recordUndo() { history_.record(documentState()); }

    void documentChanged() {
        ForgeCad::normalizeModelHistory(extrusions_, modelBodies_);
        sceneBoundsDirty_ = true;
        raySelectionCache_.clear();
        projectedEdges_.clear();
        if (sectionRefresh_) sectionRefresh_();
        analysisDirty_ = true;
        if (constraintPanelCallback_) constraintPanelCallback_();
        selectedFace_ = {};  // la geometria (e la numerazione delle facce) puo' essere cambiata
        selectedObjects_.clear();  // gli indici possono essere cambiati
        if (documentChangedCallback_) documentChangedCallback_();
        update();
    }

    // Modifica allo schizzo attivo: rigenera i corpi che ne dipendono.
    void sketchEdited() {
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
        regenerateDependents(activeSketch_);
        documentChanged();
    }

    // Rigenerazione parametrica: le estrusioni dello schizzo indicato e, a
    // cascata, le booleane che usano corpi rigenerati. I corpi dipendono solo
    // da corpi con indice minore, quindi basta una passata in ordine.
    void regenerateDependents(int sketchIndex) {
        if (sketchIndex < 0 || sketchIndex >= sketches_.size()) return;
        regenerateFrom(0, QVector<bool>(extrusions_.size(), false), {sketchIndex});
    }

    // Rigenera da `from` in poi i corpi che dipendono da corpi `dirty` o da
    // schizzi `dirtySketches` (tutti con `all`). Un piano di costruzione
    // rigenerato porta con se' gli schizzi che vi stanno sopra (followDatum),
    // e con loro i corpi che li usano (hanno indice maggiore del piano).
    // I corpi indipendenti tra loro (nessuno e' operando dell'altro) si
    // costruiscono in parallelo, a livelli: un corpo parte quando i suoi
    // operandi da rifare sono pronti. I piani di costruzione fanno da
    // separatori, perche' followDatum cambia gli schizzi che vi stanno sopra.
    void regenerateFrom(int from, QVector<bool> dirty, QSet<int> dirtySketches, bool all = false,
                        const std::function<void(int, int, const QString &)> &progress = {}) {
        dirty.resize(extrusions_.size());
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
            for (int sketch : sketchesOf(body)) depends = depends || dirtySketches.contains(sketch);
            for (int operand : bodyOperands(body)) depends = depends || (operand >= 0 && operand < index && dirty.at(operand));
            if (!depends) continue;
            dirty[index] = true;
            if (!isDatumBody(body)) {
                pending.append(index);
                continue;
            }
            flush();
            rebuildBody(extrusions_[index], index);
            advanced(1, extrusions_.at(index).name);
            followDatum(index, dirtySketches);
        }
        flush();
    }

    // Costruisce i corpi `indices` (crescenti): ogni livello contiene quelli
    // i cui operandi da rifare stanno nei livelli precedenti. buildGeometry
    // legge solo gli schizzi e gli operandi (gia' pronti) e scrive il suo corpo.
    void rebuildParallel(const QVector<int> &indices,
                         const std::function<void(int, const QString &)> &advanced = {}) {
        if (indices.isEmpty()) return;
        ScopedWork work(workCallback_, indices.size() > 1
                                          ? QStringLiteral("Rigenerazione di %1 corpi...").arg(indices.size())
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
            for (int operand : bodyOperands(extrusions_.at(index)))
                if (level.contains(operand)) l = qMax(l, level.value(operand) + 1);
            level.insert(index, l);
            levels = qMax(levels, l + 1);
        }
        ExtrusionObject *bodies = extrusions_.data();  // separato qui, prima dei thread
        const QVector<SketchObject> &sketches = sketches_;
        const QVector<ExtrusionObject> &all = extrusions_;
        const int quality = tessellationQuality_;
        for (int l = 0; l < levels; ++l) {
            std::vector<int> batch;
            for (int index : indices)
                if (level.value(index) == l) batch.push_back(index);
            const std::size_t wave = advanced ? std::max<std::size_t>(1, ForgeCad::Kernel::threadCount(0)) : batch.size();
            for (std::size_t offset = 0; offset < batch.size(); offset += wave) {
                const std::size_t count = std::min(wave, batch.size() - offset);
                ForgeCad::Kernel::parallelFor(count, ForgeCad::Kernel::threadCount(0), [&](std::size_t k) {
                    const int index = batch[offset + k];
                    buildBody(bodies[index], index, sketches, all, quality);
                });
                if (advanced) advanced(int(count), extrusions_.at(batch[offset + count - 1]).name);
            }
        }
    }

    // Gli schizzi sul piano di costruzione `index` prendono il suo piano: la
    // normale del piano, l'asse X precedente proiettato (lo schizzo non ruota
    // se il piano si sposta o si inclina poco) e l'origine precedente proiettata.
    void followDatum(int index, QSet<int> &changed) {
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
    // Sistema di uno schizzo sul piano di costruzione: normale del piano; asse X
    // e origine quelli di `previous` proiettati sul piano (se c'e' e l'asse non
    // e' quasi normale), altrimenti l'asse X del piano e la proiezione
    // dell'origine del modello (come gli schizzi su una faccia).
    static SketchFrame datumSketchFrame(const SketchFrame &datum, const SketchFrame *previous) {
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

    // Toglie i corpi (gli operandi di booleane e raccordi eliminati che restano
    // tornano visibili) e rinumera i riferimenti.
    void removeBodies(const QSet<int> &removed) {
        QVector<int> map(extrusions_.size(), -1);
        QVector<ExtrusionObject> kept;
        for (int index = 0; index < extrusions_.size(); ++index) {
            if (removed.contains(index)) {
                const ExtrusionObject &body = extrusions_.at(index);
                QVector<int> hidden{body.firstBody, body.secondBody};
                hidden += body.booleanTools;
                hidden += hiddenOperands(body);
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
            for (QVector<GeometryRef> *refs : {&body.datum.refs, &body.pattern.refs})
                for (GeometryRef &ref : *refs)
                    if (isBodyRef(ref)) ref.index = map.value(ref.index, -1);
            if (isBodyRef(body.extentRef)) body.extentRef.index = map.value(body.extentRef.index, -1);
            if (isBodyRef(body.move.axis)) body.move.axis.index = map.value(body.move.axis.index, -1);
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
    }

    // Elimina le entita' selezionate dello schizzo attivo, con i loro vincoli;
    // gli indici dei segmenti (vincoli, linee di costruzione, assi delle
    // rivoluzioni) si rinumerano.
    void deleteSketchElements() {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size() || sketchSelections_.isEmpty()) return;
        recordUndo();
        QSet<int> segments, curves;
        for (const SketchElementSelection &element : sketchSelections_) (element.kind == 0 ? segments : curves).insert(element.index);
        const int oldCurves = sketches_.at(activeSketch_).curves.size();
        const QVector<int> segmentMap = ForgeCad::removeSketchEntities(sketches_[activeSketch_], segments, curves);
        remapRevolutionAxes(activeSketch_, segmentMap);
        QVector<int> curveMap(oldCurves, -1);
        int nextCurve = 0;
        for (int old = 0; old < oldCurves; ++old) if (!curves.contains(old)) curveMap[old] = nextCurve++;
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

    // Sposta di `delta` i punti dello schizzo in `from` (estremi dei segmenti,
    // punti delle curve); i segmenti orizzontali e verticali collegati
    // trascinano l'altro estremo quanto serve per restarlo. `fixed` non si
    // muove; il segmento `edited` non propaga all'indietro.
    void moveSketchPoint(SketchObject &sketch, int edited, const QPointF &from, const QPointF &delta, const QPointF &fixed) {
        const double tolerance = ForgeCad::kSketchConnectionTolerance;
        struct Move { QPointF from, delta; };
        QVector<Move> pending{{from, delta}};
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
                    QPointF &point = sketch.curves[curve].controlPoints[control];
                    if (pointDistance(point, move.from) <= tolerance) {
                        updates.append({&point, point + move.delta});
                        CurveObject &object = sketch.curves[curve];
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

    // I corpi che dipendono (anche a cascata) dal corpo `changed`, gia' rigenerato.
    void regenerateAfter(int changed) {
        QVector<bool> dirty(extrusions_.size(), false);
        QSet<int> sketches;
        if (changed >= 0 && changed < dirty.size()) {
            dirty[changed] = true;
            if (isDatumBody(extrusions_.at(changed))) followDatum(changed, sketches);
        }
        regenerateFrom(changed + 1, dirty, sketches);
    }

    // Tutti i corpi, in ordine (dopo un cambio di kernel).
    void regenerateAll(const std::function<void(int, int, const QString &)> &progress = {}) {
        regenerateFrom(0, {}, {}, true, progress);
    }

    static bool hasGeometry(const ExtrusionObject &body) {
        if (isCurveBody(body)) return body.curve != nullptr;
        if (isDatumBody(body)) return body.datumValid;
        return body.forgeBody != nullptr;
    }
    // Funzione curva (elica, spirale): niente solido ne' superficie, solo la curva.
    static bool isCurveBody(const ExtrusionObject &body) { return body.operation < 0 && body.feature == BodyFeature::Helix; }
    // Piano di costruzione: niente solido, solo il piano (datumFrame).
    static bool isDatumBody(const ExtrusionObject &body) { return body.operation < 0 && body.feature == BodyFeature::DatumPlane; }
    // Corpo con una forma (solido o superficie): non una curva ne' un piano di costruzione.
    static bool isShapeBody(const ExtrusionObject &body) { return hasGeometry(body) && !isCurveBody(body) && !isDatumBody(body); }
    // Corpi da cui dipende il corpo: operandi delle booleane, base dei raccordi,
    // superficie (e strumento) dei tagli e delle estensioni. Hanno indice minore.
    static QVector<int> bodyOperands(const ExtrusionObject &body) {
        if (body.operation >= 0) return QVector<int>{body.firstBody, body.secondBody} + body.booleanTools;
        switch (body.feature) {
        case BodyFeature::Extrusion: {
            QVector<int> bodies = body.mergeOperation != 0 ? body.mergeBodies : QVector<int>();
            if (body.extent != 0 && isBodyRef(body.extentRef)) bodies.append(body.extentRef.index);
            return bodies;
        }
        case BodyFeature::Blend:
        case BodyFeature::SheetExtend:
        case BodyFeature::Scale: return {body.firstBody};
        case BodyFeature::SheetTrim: return body.secondBody >= 0 ? QVector<int>{body.firstBody, body.secondBody} : QVector<int>{body.firstBody};
        case BodyFeature::Helix: return body.helix.source != 0 ? QVector<int>{body.firstBody} : QVector<int>{};
        case BodyFeature::Sweep: {
            QVector<int> result = body.sweepPath == 1 ? QVector<int>{body.firstBody} : QVector<int>();
            if (body.mergeOperation != 0) result += body.mergeBodies;
            return result;
        }
        case BodyFeature::DatumPlane: {
            QVector<int> bodies;
            for (const GeometryRef &ref : body.datum.refs)
                if (isBodyRef(ref)) bodies.append(ref.index);
            return bodies;
        }
        case BodyFeature::Pattern: {
            QVector<int> bodies{body.firstBody};
            for (const GeometryRef &ref : body.pattern.refs)
                if (isBodyRef(ref)) bodies.append(ref.index);
            return bodies;
        }
        case BodyFeature::Transform: {
            QVector<int> bodies{body.firstBody};
            if (isBodyRef(body.move.axis)) bodies.append(body.move.axis.index);
            return bodies;
        }
        default: return {};
        }
    }
    // Riferimento a un corpo (vertice, spigolo, faccia, piano di costruzione, curva) o a uno schizzo (punto, entita').
    static bool isBodyRef(const GeometryRef &ref) {
        return ref.kind == 3 || ref.kind == 4 || ref.kind == 5 || ref.kind == 8 || ref.kind == 9 || ref.kind == 10;
    }
    static bool isSketchRef(const GeometryRef &ref) { return ref.kind == 6 || ref.kind == 7; }
    // Schizzi da cui nasce il corpo: estrusione, rivoluzione, base dell'elica,
    // profilo e percorso dello sweep, sezioni e guide del loft.
    static QVector<int> sketchesOf(const ExtrusionObject &body) {
        if (body.operation >= 0) return {};
        switch (body.feature) {
        case BodyFeature::Extrusion:
            if (body.extent != 0 && isSketchRef(body.extentRef) && body.extentRef.index != body.sketchIndex) return {body.sketchIndex, body.extentRef.index};
            return {body.sketchIndex};
        case BodyFeature::Revolution: return {body.sketchIndex};
        case BodyFeature::Transform: return isSketchRef(body.move.axis) ? QVector<int>{body.move.axis.index} : QVector<int>{};
        case BodyFeature::Helix: return body.helix.source == 0 ? QVector<int>{body.sketchIndex} : QVector<int>{};
        case BodyFeature::Sweep: return body.sweepPath == 0 ? QVector<int>{body.sketchIndex, body.pathSketch} : QVector<int>{body.sketchIndex};
        case BodyFeature::Loft: {
            QVector<int> result = body.loftSketches;
            for (int guide : body.loftGuides)
                if (!result.contains(guide)) result.append(guide);
            for (const SketchPathRef &guide : body.loftGuidePaths)
                if (!result.contains(guide.sketch)) result.append(guide.sketch);
            return result;
        }
        case BodyFeature::DatumPlane:
        case BodyFeature::Pattern: {
            QVector<int> sketches;
            for (const GeometryRef &ref : body.feature == BodyFeature::Pattern ? body.pattern.refs : body.datum.refs)
                if (isSketchRef(ref)) sketches.append(ref.index);
            return sketches;
        }
        default: return {};
        }
    }
    static bool usesSketch(const ExtrusionObject &body) { return !sketchesOf(body).isEmpty(); }

    // Geometria esatta del corpo `index` (estrusione o booleana) con il kernel
    // attivo, poi la sua tassellazione. Gli operandi di una booleana hanno
    // indice minore e sono gia' rigenerati. In caso d'errore il corpo resta
    // senza geometria con il messaggio in `error`.
    void rebuildBody(ExtrusionObject &body, int index) {
        ScopedWork work(workCallback_, body.name.isEmpty() ? QStringLiteral("Calcolo della geometria...")
                                                            : QStringLiteral("Calcolo di %1...").arg(body.name));
        buildBody(body, index, sketches_, extrusions_, tessellationQuality_);
    }
    // Geometria e tassellazione di un corpo (anche da un thread: non tocca il viewport).
    static void buildBody(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                          int quality) {
        QElapsedTimer timer;
        timer.start();
        if (body.cachedGeometry && body.forgeBody) {
            // Appena aperto: il body salvato nel documento (stessi sorgenti,
            // stessa definizione) al posto del calcolo.
            body.cachedGeometry = false;
            body.curve.reset();
            body.datumValid = false;
            body.solid = !body.forgeBody->isSheet();
        } else {
            body.cachedGeometry = false;
            buildGeometry(body, index, sketches, bodies);
        }
        const qint64 built = timer.elapsed();
        tessellateGeometry(body, quality, body.display);
        if (qEnvironmentVariableIsSet("FORGECAD_PROFILE"))
            std::fprintf(stderr, "PROFILO corpo %d \"%s\": costruzione %lld ms, tassellazione %lld ms\n", index, qPrintable(body.name),
                         built, timer.elapsed() - built);
    }

    // Geometria esatta del corpo `index` (kernel ForgeCAD): gli schizzi e gli
    // operandi (indici minori, gia' costruiti) vengono da `sketches` e
    // `bodies`. In caso d'errore il corpo resta senza geometria con il
    // messaggio in `error`. Usata dalla rigenerazione e dalle anteprime (in un
    // thread, su copie).
    // FORGECAD_GEOMETRY_HASH_BEGIN
    // Questa sezione entra nell'impronta delle cache B-rep dei documenti.
    // Tenere tra i due marcatori tutta la logica che decide quale geometria
    // esatta costruire; le modifiche alla sola UI fuori da qui non devono
    // invalidare le anteprime salvate.
    static void buildGeometry(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies) {
        body.error.clear();
        body.forgeBody.reset();
        body.curve.reset();
        body.datumValid = false;
        body.solid = false;
        const auto operand = [&](int i) -> const ExtrusionObject * {
            return i >= 0 && i < index && i < bodies.size() ? &bodies.at(i) : nullptr;
        };
        const auto sketch = [&](int i) -> const SketchObject * { return i >= 0 && i < sketches.size() ? &sketches.at(i) : nullptr; };
        if (body.suppressed) {
            // Una feature soppressa e' un passaggio trasparente nella catena:
            // conserva un risultato intermedio valido per le feature seguenti.
            const ExtrusionObject *previous = nullptr;
            for (int candidate = index - 1; candidate >= 0; --candidate)
                if (bodies.at(candidate).modelBodyId == body.modelBodyId) { previous = &bodies.at(candidate); break; }
            if (previous) {
                body.forgeBody = previous->forgeBody;
                body.curve = previous->curve;
                body.solid = previous->solid;
            }
            return;
        }
        try {
            if (body.operation >= 0) {
                const ExtrusionObject *first = operand(body.firstBody), *second = operand(body.secondBody);
                if (!first || !second) {
                    body.error = QStringLiteral("Operandi della booleana non validi.");
                    return;
                }
                // A op B, poi op con gli altri strumenti uno dopo l'altro.
                body.forgeBody = ForgeCad::forgeBoolean(first->forgeBody, second->forgeBody, BooleanOperation(body.operation), &body.error);
                for (int tool : body.booleanTools) {
                    if (!body.forgeBody) break;
                    const ExtrusionObject *other = operand(tool);
                    if (!other) {
                        body.forgeBody.reset();
                        body.error = QStringLiteral("Uno degli strumenti della booleana non esiste piu'.");
                        break;
                    }
                    body.forgeBody = ForgeCad::forgeBoolean(body.forgeBody, other->forgeBody, BooleanOperation(body.operation), &body.error);
                    if (!body.forgeBody) body.error = QStringLiteral("Con \"%1\": %2").arg(other->name, body.error);
                }
                body.solid = body.forgeBody && !body.forgeBody->isSheet();
                return;
            }
            switch (body.feature) {
            case BodyFeature::Transform: {
                const ExtrusionObject *base = operand(body.firstBody);
                if (!base || !isShapeBody(*base)) {
                    body.error = QStringLiteral("Il corpo da spostare non esiste piu' o non ha geometria.");
                    return;
                }
                body.forgeBody = ForgeCad::forgeMoveBody(base->forgeBody, body.move, index, sketches, bodies, &body.error);
                body.solid = base->solid && hasGeometry(body);
                return;
            }
            case BodyFeature::Scale: {
                const ExtrusionObject *base = operand(body.firstBody);
                if (!base) {
                    body.error = QStringLiteral("Il corpo da scalare non esiste piu'.");
                    return;
                }
                body.forgeBody = ForgeCad::forgeScale(base->forgeBody, body.scaleFactor, body.scaleCenterMode, body.scaleCenter, &body.error);
                body.solid = base->solid && hasGeometry(body);
                return;
            }
            case BodyFeature::SheetTrim:
            case BodyFeature::SheetExtend: {
                const bool trim = body.feature == BodyFeature::SheetTrim;
                const ExtrusionObject *sheet = operand(body.firstBody);
                const ExtrusionObject *tool = trim && body.secondBody >= 0 ? operand(body.secondBody) : nullptr;
                if (!sheet) body.error = QStringLiteral("La superficie non esiste piu'.");
                else if (trim && body.secondBody >= 0 && !tool) body.error = QStringLiteral("Lo strumento del taglio non esiste piu'.");
                else if (tool && !hasGeometry(*tool)) body.error = QStringLiteral("Lo strumento del taglio non ha geometria valida.");
                else if (trim) body.forgeBody = ForgeCad::forgeTrimSheet(sheet->forgeBody, tool ? tool->forgeBody : nullptr, body.trimPlane, body.trimKeep, &body.error);
                else body.forgeBody = ForgeCad::forgeExtendSheet(sheet->forgeBody, body.blendEdges, body.blendSize, body.extendLinear, &body.error);
                return;
            }
            case BodyFeature::Blend: {
                const ExtrusionObject *base = operand(body.firstBody);
                if (!base) {
                    body.error = QStringLiteral("Il corpo da raccordare non esiste piu'.");
                    return;
                }
                body.forgeBody = ForgeCad::forgeBlend(base->forgeBody, body.blendEdges, body.blendSize, body.blendChamfer, &body.error, body.chamferSpec);
                // Due raccordi/smussi adiacenti possono intersecare le rispettive
                // zone. Se il secondo e' piu' grande, applicarlo sul raccordo
                // piccolo puo' non avere una superficie offset valida. Rigenera
                // allora dalla base comune in ordine decrescente di misura: la
                // lavorazione grande consuma la zona d'intersezione e quella
                // piccola viene rifatta sugli spigoli rimasti.
                if (!body.forgeBody && base->feature == BodyFeature::Blend
                    && body.blendSize > base->blendSize + 1e-12 * qMax(body.blendSize, base->blendSize)) {
                    const ExtrusionObject *source = operand(base->firstBody);
                    if (source && source->forgeBody) {
                        QString reorderedError;
                        ForgeCad::ForgeBody larger = ForgeCad::forgeBlend(source->forgeBody, body.blendEdges, body.blendSize,
                                                                          body.blendChamfer, &reorderedError, body.chamferSpec);
                        if (larger) {
                            ForgeCad::ForgeBody rebuilt = ForgeCad::forgeBlend(larger, base->blendEdges, base->blendSize,
                                                                               base->blendChamfer, &reorderedError, base->chamferSpec);
                            if (rebuilt) {
                                body.forgeBody = std::move(rebuilt);
                                body.error.clear();
                            }
                        }
                    }
                }
                body.solid = hasGeometry(body);
                return;
            }
            case BodyFeature::Primitive:
                body.forgeBody = ForgeCad::forgePrimitive(body.primitive, &body.error);
                body.solid = hasGeometry(body);
                return;
            case BodyFeature::Helix: {
                ForgeCad::HelixBase base;
                bool ok = false;
                if (body.helix.source == 0) {
                    const SketchObject *s = sketch(body.sketchIndex);
                    if (!s) body.error = QStringLiteral("Lo schizzo della base dell'elica non esiste piu'.");
                    else ok = ForgeCad::helixBaseFromSketch(*s, body.helix.curve, base, &body.error);
                } else {
                    const ExtrusionObject *b = operand(body.firstBody);
                    if (!b || !isShapeBody(*b)) body.error = QStringLiteral("Il corpo della base dell'elica non esiste piu'.");
                    else ok = ForgeCad::forgeHelixBase(*b->forgeBody, body.helix.source, body.helix.reference, base, &body.error);
                }
                if (ok) body.curve = ForgeCad::helixCurve(body.helix, base, &body.error);
                return;
            }
            case BodyFeature::Sweep: {
                const SketchObject *profile = sketch(body.sketchIndex);
                if (!profile) {
                    body.error = QStringLiteral("Lo schizzo del profilo non esiste piu'.");
                    return;
                }
                std::vector<ForgeCad::Kernel::PathSegment> path;
                if (body.sweepPath == 0) {
                    const SketchObject *pathSketch = sketch(body.pathSketch);
                    if (!pathSketch) {
                        body.error = QStringLiteral("Lo schizzo del percorso non esiste piu'.");
                        return;
                    }
                    if (body.pathSketch == body.sketchIndex) {
                        body.error = QStringLiteral("Profilo e percorso devono stare in schizzi diversi.");
                        return;
                    }
                    SketchPathRef selection{body.pathSketch, body.pathSegments, body.pathCurves};
                    const SketchObject selected = ForgeCad::sketchPathSubset(*pathSketch, selection);
                    if (!ForgeCad::sketchPath(selected, path, &body.error)) return;
                } else {
                    const ExtrusionObject *curve = operand(body.firstBody);
                    if (!curve || !curve->curve) {
                        body.error = QStringLiteral("La curva del percorso non esiste piu' o non e' valida.");
                        return;
                    }
                    path = ForgeCad::curvePath(curve->curve);
                }
                path = ForgeCad::alignPath(path, *profile);
                body.forgeBody = ForgeCad::forgeSweep(*profile, path, body.sweepMode, &body.error);
                body.forgeBody = ForgeCad::forgeMergeFeatureResult(body, body.forgeBody, index, bodies, &body.error);
                body.solid = body.forgeBody && !body.forgeBody->isSheet();
                return;
            }
            case BodyFeature::Loft: {
                QVector<SketchObject> sections, guides;
                for (int i : body.loftSketches) {
                    const SketchObject *s = sketch(i);
                    if (!s) {
                        body.error = QStringLiteral("Una sezione del loft non esiste piu'.");
                        return;
                    }
                    sections.append(*s);
                }
                for (int i : body.loftGuides) {
                    const SketchObject *s = sketch(i);
                    if (!s) { body.error = QStringLiteral("Una curva guida del loft non esiste piu'."); return; }
                    guides.append(*s);
                }
                for (const SketchPathRef &path : body.loftGuidePaths) {
                    const SketchObject *s = sketch(path.sketch);
                    if (!s) { body.error = QStringLiteral("Una curva guida del loft non esiste piu'."); return; }
                    guides.append(ForgeCad::sketchPathSubset(*s, path));
                }
                body.forgeBody = ForgeCad::forgeLoft(sections, guides, body.loftRuled, body.loftStartContinuity,
                                                     body.loftEndContinuity, body.loftGuideContinuity, body.loftGuideInfluence,
                                                     body.loftStartInfluence, body.loftEndInfluence, &body.error);
                body.solid = body.forgeBody && !body.forgeBody->isSheet();
                return;
            }
            case BodyFeature::Imported: {
                if (body.importData.isEmpty()) {
                    // Documento del formato 8 (B-rep OpenCASCADE, non piu' letto).
                    body.error = QStringLiteral("Il corpo va importato di nuovo da %1 (documento di una versione precedente).").arg(body.importSource);
                    return;
                }
                body.forgeBody = ForgeCad::forgeImported(body.importData, &body.error);
                body.solid = body.forgeBody && !body.forgeBody->isSheet();
                return;
            }
            case BodyFeature::Pattern: {
                const ExtrusionObject *base = operand(body.firstBody);
                if (!base || !isShapeBody(*base)) {
                    body.error = QStringLiteral("Il corpo da ripetere non esiste piu' o non ha geometria.");
                    return;
                }
                std::vector<ForgeCad::Kernel::Transform3> placements;
                if (!ForgeCad::patternPlacements(body.pattern, index, sketches, bodies, placements, &body.error)) return;
                if (body.pattern.featureOnly) {
                    // Lo strumento della booleana (o l'estrusione che si unisce o
                    // sottrae) ripetuto, l'operazione una volta sola.
                    ForgeCad::ForgeBody target, tool;
                    BooleanOperation operation = BooleanOperation::Union;
                    const auto uniteBodies = [&](QVector<int> list) -> ForgeCad::ForgeBody {
                        ForgeCad::ForgeBody united;
                        for (int i : list) {
                            const ExtrusionObject *b = operand(i);
                            if (!b || !b->forgeBody) return nullptr;
                            united = united ? ForgeCad::forgeBoolean(united, b->forgeBody, BooleanOperation::Union, &body.error) : b->forgeBody;
                            if (!united) return nullptr;
                        }
                        return united;
                    };
                    if (base->operation == 0 || base->operation == 2) {
                        const ExtrusionObject *first = operand(base->firstBody);
                        target = first ? first->forgeBody : nullptr;
                        tool = uniteBodies(QVector<int>{base->secondBody} + base->booleanTools);
                        operation = BooleanOperation(base->operation);
                    } else if (base->operation < 0 && base->feature == BodyFeature::Extrusion && base->mergeOperation != 0 && !base->mergeBodies.isEmpty()) {
                        ExtrusionObject plain = *base;
                        plain.mergeOperation = 0;
                        plain.mergeProbe = false;
                        buildGeometry(plain, body.firstBody, sketches, bodies);
                        tool = plain.forgeBody;
                        target = uniteBodies(base->mergeBodies);
                        operation = base->mergeOperation == 2 ? BooleanOperation::Difference : BooleanOperation::Union;
                    }
                    if (!target || !tool) {
                        if (body.error.isEmpty() || base->operation != 0)
                            body.error = QStringLiteral("La ripetizione della funzione vale per un'unione, una differenza o un'estrusione che si unisce o sottrae.");
                        return;
                    }
                    body.forgeBody = ForgeCad::forgePatternFeature(target, tool, operation, placements, &body.error);
                } else {
                    body.forgeBody = ForgeCad::forgePattern(base->forgeBody, placements, body.pattern.kind != 2 || body.pattern.keepOriginal, &body.error);
                }
                body.solid = body.forgeBody && !body.forgeBody->isSheet();
                return;
            }
            case BodyFeature::DatumPlane:
                // I riferimenti si risolvono sulla geometria esatta dei corpi.
                body.datumValid = ForgeCad::computeDatum(body.datum, index, sketches, bodies, body.datumFrame, &body.error);
                return;
            case BodyFeature::Extrusion:
            case BodyFeature::Revolution: {
                const SketchObject *s = sketch(body.sketchIndex);
                if (!s) {
                    body.error = QStringLiteral("Lo schizzo del corpo non esiste piu'.");
                } else if (body.feature == BodyFeature::Revolution) {
                    body.forgeBody = ForgeCad::forgeRevolution(*s, body.revolveAxis, body.revolveAngle, &body.error);
                    body.solid = hasGeometry(body);
                } else {
                    body.forgeBody = ForgeCad::forgeExtrusionFeature(body, index, sketches, bodies, &body.error);
                    body.solid = body.forgeBody && !body.forgeBody->isSheet();
                }
                return;
            }
            }
        } catch (const std::exception &failure) {
            body.error = QString::fromUtf8(failure.what());
        }
        if (body.error.isEmpty() && !hasGeometry(body)) body.error = QStringLiteral("Costruzione non riuscita.");
    }

    // FORGECAD_GEOMETRY_HASH_END

    void tessellateBody(ExtrusionObject &body) const { tessellateGeometry(body, tessellationQuality_, body.display); }
    static void tessellateGeometry(const ExtrusionObject &body, int quality, BodyDisplay &display) {
        if (isDatumBody(body)) {
            display = {};  // disegnato come piano (drawDatumPlanes)
            display.quality = quality;
        } else if (body.curve) ForgeCad::curveDisplay(*body.curve, quality, display);
        else if (body.forgeBody) ForgeCad::forgeTessellate(*body.forgeBody, quality, display);
        else display = {};
    }

    void restoreDocument(DocumentState state) {
        dimensionPlacing_ = -1;
        ForgeCad::normalizeModelHistory(state);
        sketches_ = std::move(state.sketches);
        extrusions_ = std::move(state.extrusions);
        modelBodies_ = std::move(state.modelBodies);
        for (SketchObject &sketch : sketches_) {
            for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
        }
        for (ExtrusionObject &body : extrusions_)
            if (body.display.quality != tessellationQuality_) tessellateBody(body);
        hasPendingPoint_ = false;
        curveControlPoints_.clear();
        sketchSelections_.clear();
        selectedPoints_.clear();
        selectedConstraints_.clear();
        constraintHover_ = -1;
        sketchHover_ = {};
        hover_ = {};
        draggingControlPoint_ = false;
        dragRecorded_ = false;
        if (edgePicking_) cancelEdgePick();
        clearBlendPreview();
        if (selection_.kind == SceneObjectKind::Sketch || selection_.kind == SceneObjectKind::Extrusion)
            selection_ = {};
        if (activeSketch_ >= sketches_.size()) {
            activeSketch_ = -1;
            if (sketchMode_) endSketchMode();
        }
        documentChanged();
    }

    // Sfondo sfumato disegnato in coordinate schermo: bande perpendicolari alla
    // direzione della sfumatura (angolo), con il punto di mescolanza al 50%
    // spostabile lungo la direzione (posizione).
    void drawBackgroundGradient() {
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        const float aspect = float(width()) / float(qMax(1, height()));
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
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
        glBegin(GL_QUAD_STRIP);
        for (const auto &stop : stops) {
            glColor3f(float(start.redF() + (end.redF() - start.redF()) * stop.mix),
                      float(start.greenF() + (end.greenF() - start.greenF()) * stop.mix),
                      float(start.blueF() + (end.blueF() - start.blueF()) * stop.mix));
            const QVector2D centre = direction * stop.offset;
            const QVector2D first = centre + across * span;
            const QVector2D second = centre - across * span;
            glVertex2f(first.x(), first.y());
            glVertex2f(second.x(), second.y());
        }
        glEnd();
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    // Tolleranza di selezione in unita' di schizzo, equivalente a qualche pixel.
    double pickTolerance(double pixels = 8.0) const {
        return pixels * 8.0 * double(zoom_) / 8.0 / double(qMax(1, height()));
    }

    bool findCurveEditPoint(const QPointF &point, int &curveIndex,
                            int &controlIndex, EditablePointKind &pointKind) const {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return false;
        double nearestDistance = pickTolerance();
        int nearestCurve = -1;
        int nearestControl = -1;
        EditablePointKind nearestKind = EditablePointKind::Control;
        const QVector<CurveObject> &curves = sketches_.at(activeSketch_).curves;
        for (int curve = 0; curve < curves.size(); ++curve) {
            const CurveObject &candidate = curves.at(curve);
            const bool hasHandles = candidate.tangentHandles.size() == candidate.controlPoints.size();
            for (int control = 0; control < candidate.controlPoints.size(); ++control) {
                const QPointF candidates[] = {
                    candidate.controlPoints.at(control),
                    hasHandles ? candidate.tangentHandles.at(control).first : candidate.controlPoints.at(control),
                    hasHandles ? candidate.tangentHandles.at(control).second : candidate.controlPoints.at(control)};
                for (int kind = 0; kind < (hasHandles ? 3 : 1); ++kind) {
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

    static double distanceToSegment(const QPointF &point, const QPointF &first, const QPointF &second) {
        const QPointF line = second - first;
        const double lengthSquared = QPointF::dotProduct(line, line);
        const double parameter = lengthSquared > 0.0
            ? qBound(0.0, QPointF::dotProduct(point - first, line) / lengthSquared, 1.0) : 0.0;
        return pointDistance(point, first + line * parameter);
    }

    // Curva libera (spline/NURBS) piu' vicina al punto, per inserire un punto di controllo.
    int findNearestFreeCurve(const QPointF &point) const {
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

    void addControlPointToCurve(const QPointF &point) {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
        const int curveIndex = findNearestFreeCurve(point);
        if (curveIndex >= 0) {
            recordUndo();
            CurveObject &curve = sketches_[activeSketch_].curves[curveIndex];
            // Inserisce il punto nel tratto del poligono di controllo piu' vicino.
            int insertIndex = curve.controlPoints.size();
            double nearestDistance = std::numeric_limits<double>::max();
            for (int index = 1; index < curve.controlPoints.size(); ++index) {
                const double distance = distanceToSegment(point, curve.controlPoints.at(index - 1),
                                                          curve.controlPoints.at(index));
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    insertIndex = index;
                }
            }
            curve.controlPoints.insert(insertIndex, point);
            if (curve.weights.size() == curve.controlPoints.size() - 1) curve.weights.insert(insertIndex, 1.0);
            ForgeCad::initializeTangentHandles(curve);
            ForgeCad::recalculateCurve(curve, tessellationQuality_);
            sketchEdited();
        } else {
            curveControlPoints_.append(point);
        }
        update();
    }

    // Applica una modifica strutturale a spline/NURBS e rinumera i riferimenti
    // dei vincoli ai punti e alle maniglie. `origins[new]` contiene l'indice
    // precedente, oppure -1 per un punto appena inserito.
    void commitFreeCurveEdit(int curveIndex, const CurveObject &working, const QVector<int> &origins) {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
        SketchObject &sketch = sketches_[activeSketch_];
        if (curveIndex < 0 || curveIndex >= sketch.curves.size()) return;
        const int oldCount = sketch.curves.at(curveIndex).controlPoints.size();
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
        sketch.curves[curveIndex] = working;
        ForgeCad::recalculateCurve(sketch.curves[curveIndex], tessellationQuality_);
        selectedPoints_.clear();
        selectedConstraints_.clear();
        sketchEdited();
    }

    void editFreeCurve(int curveIndex, int initialPoint = -1) {
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
                points->addItem(QStringLiteral("P%1   (%2, %3)").arg(k + 1).arg(work.controlPoints.at(k).x(), 0, 'g', 6).arg(work.controlPoints.at(k).y(), 0, 'g', 6));
            if (points->count()) points->setCurrentRow(initialPoint >= 0 ? qBound(0, initialPoint, points->count() - 1) : row);
            initialPoint = -1;
        };
        auto spin = [&](double value) {
            auto *box = new QDoubleSpinBox(&dialog);
            box->setRange(-1e9, 1e9); box->setDecimals(6); box->setValue(value);
            return box;
        };
        auto *x = spin(0.0), *y = spin(0.0);
        auto *linked = new QCheckBox(QStringLiteral("Maniglie collegate (tangenza)"), &dialog);
        auto *inLength = spin(0.0), *outLength = spin(0.0);
        inLength->setRange(0.0, 1e9); outLength->setRange(0.0, 1e9);
        auto *row = new QWidget(&dialog); auto *rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *add = new QPushButton(QStringLiteral("Aggiungi dopo"), row), *remove = new QPushButton(QStringLiteral("Elimina punto"), row);
        rowLayout->addWidget(add); rowLayout->addWidget(remove);
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
        bool loading = false;
        const auto load = [&] {
            const int k = points->currentRow(); if (k < 0 || k >= work.controlPoints.size()) return;
            loading = true;
            x->setValue(work.controlPoints.at(k).x()); y->setValue(work.controlPoints.at(k).y());
            remove->setEnabled(work.controlPoints.size() > (work.tool == DrawingTool::Nurbs ? 4 : 2));
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
            else { work.tangentHandles.insert(at, {work.controlPoints.at(at) - QPointF(1.0, 0.0), work.controlPoints.at(at) + QPointF(1.0, 0.0)}); work.tangentLinked.insert(at, false); }
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
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons); refill(); load();
        if (dialog.exec() == QDialog::Accepted) commitFreeCurveEdit(curveIndex, work, origins);
    }

    SketchElementSelection findSketchElement(const QPointF &point) const {
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

    void selectSketchElement(const QPointF &point, bool additive) {
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

    // Ctrl+clic su due punti: vincolo di coincidenza (i punti vengono uniti
    // nel punto medio, calcolato in double).
    bool selectPointWithControl(const QPointF &point) {
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

    bool isValidSelectedPoint(const SelectedPoint &point) const {
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
        return point.point < curve.controlPoints.size();
    }

    QPointF selectedPointPosition(const SelectedPoint &point) const {
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

    void setSelectedPointPosition(const SelectedPoint &point, const QPointF &position) {
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

    void finalizeCurve() {
        if (drawingTool_ != DrawingTool::Spline && drawingTool_ != DrawingTool::Nurbs) return;
        const int minimumPoints = drawingTool_ == DrawingTool::Nurbs ? 4 : 2;
        if (curveControlPoints_.size() < minimumPoints
            || activeSketch_ < 0 || activeSketch_ >= sketches_.size()) {
            return;
        }
        CurveObject curve;
        curve.tool = drawingTool_;
        curve.controlPoints = curveControlPoints_;
        ForgeCad::recalculateCurve(curve, tessellationQuality_);
        if (curve.numericallyValid) {
            recordUndo();
            SketchObject &sketch = sketches_[activeSketch_];
            sketch.curves.append(curve);
            // Gli estremi che cadono su punti di altre entita' diventano coincidenti (poi la tangenza si da' nel punto).
            const int index = int(sketch.curves.size()) - 1, last = int(curve.controlPoints.size()) - 1;
            recordPointCoincidences(sketch, {1, index, 0}, curve.controlPoints.first());
            recordPointCoincidences(sketch, {1, index, last}, curve.controlPoints.last());
            sketchEdited();
        }
        curveControlPoints_.clear();
        hasPendingPoint_ = false;
        update();
    }

    // Cerchio, arco e poligono sono memorizzati con i soli parametri esatti
    // (centro, punti, numero di lati); la geometria viene dal kernel.
    void finalizePrimitive() {
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
            sketches_[activeSketch_].curves.append(primitive);
            sketchEdited();
        }
        curveControlPoints_.clear();
        hasPendingPoint_ = false;
        update();
    }

    // Rettangolo (due angoli o centro e angolo): quattro segmenti orizzontali e
    // verticali, collegati dai vincoli di coincidenza (e agganciati agli altri
    // punti su cui cadono gli angoli).
    void finalizeRectangle() {
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

    // Angolo opposto del rettangolo in costruzione: con Maiusc un quadrato.
    QPointF rectangleCorner(const QPointF &anchor, const QPointF &cursor) const {
        if (!(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)) return cursor;
        const QPointF d = cursor - anchor;
        const double side = std::max(std::abs(d.x()), std::abs(d.y()));
        return anchor + QPointF(d.x() < 0.0 ? -side : side, d.y() < 0.0 ? -side : side);
    }
    // Terzo punto dell'ellisse: sulla perpendicolare al primo semiasse, alla
    // distanza del punto `through` dalla retta di quel semiasse.
    static QPointF ellipseMinorPoint(const QPointF &center, const QPointF &major, const QPointF &through) {
        const double a = pointDistance(center, major);
        if (a <= 0.0) return through;
        const QPointF u = (major - center) / a, perpendicular(-u.y(), u.x());
        const QPointF r = through - center;
        return center + std::abs(r.x() * perpendicular.x() + r.y() * perpendicular.y()) * perpendicular;
    }
    // Dopo lo spostamento di un punto dell'ellisse: il centro trascina gli altri,
    // il terzo punto torna sulla perpendicolare al primo semiasse.
    static void normalizeEllipse(CurveObject &curve, int moved, const QPointF &previous) {
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

    QPointF projectWorldPoint(const QVector3D &point) const {
        const std::array<double, 18> key = {double(width()), double(height()), zoom_, panX_, panY_, yaw_, pitch_, roll_,
            orientation_.right[0], orientation_.right[1], orientation_.right[2],
            orientation_.up[0], orientation_.up[1], orientation_.up[2],
            orientation_.toward[0], orientation_.toward[1], orientation_.toward[2], 0.0};
        if (key != projectionKey_) {
            projectionKey_ = key;
            projectionMatrix_.setToIdentity();
            projectionMatrix_.translate(panX_, panY_, -zoom_);
            projectionMatrix_.rotate(roll_, 0.0f, 0.0f, 1.0f);
            projectionMatrix_.rotate(pitch_, 1.0f, 0.0f, 0.0f);
            projectionMatrix_.rotate(yaw_, 0.0f, 1.0f, 0.0f);
            projectionMatrix_ *= basisMatrix();
        }
        const QVector3D cameraPoint = projectionMatrix_.map(point);
        const double pixels = double(qMax(1, height())) / zoom_;
        return QPointF(width() * 0.5 + cameraPoint.x() * pixels, height() * 0.5 - cameraPoint.y() * pixels);
    }

    void drawReferenceLabels() {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        // Lettere degli assi oltre la punta delle frecce.
        painter.setFont(QFont(QStringLiteral("Sans"), 12, QFont::Bold));
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
        painter.setFont(QFont(QStringLiteral("Sans"), 10, QFont::DemiBold));
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

    void drawSketchDimensions(QPainter &painter) const {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
        const SketchObject &sketch = sketches_.at(activeSketch_);
        painter.setFont(QFont(QStringLiteral("Sans"), 9, QFont::DemiBold));
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
                             QStringLiteral("L = %1").arg(length, 0, 'f', 4));
            if (index < sketch.segmentAngles.size() && sketch.segmentAngles.at(index) >= 0.0) {
                painter.drawText(labelPosition + QPointF(4.0, 10.0),
                                 QStringLiteral("A = %1 deg")
                                     .arg(sketch.segmentAngles.at(index), 0, 'f', 4));
            }
        }
    }

    QVector3D mapSketchPoint(const QPointF &point, const SketchObject &sketch) const {
        return ForgeCad::sketchToDisplay(point, sketch);
    }
    // Punto del piano dello schizzo attivo (in costruzione, cursore).
    QVector3D mapActiveSketchPoint(const QPointF &point) const {
        if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) return mapSketchPoint(point, sketches_.at(activeSketch_));
        return ForgeCad::sketchToDisplay(point, activePlane_);
    }

    QPointF screenToSketchPoint(const QPoint &position) const {
        // Vista ruotata nello schizzo: il punto del piano dello schizzo lungo il raggio del puntatore.
        if (sketchViewRotated_ && activeSketchObject()) {
            QPointF point;
            if (rayToSketchPlane(position, point)) return point;
        }
        const double aspect = double(width()) / double(qMax(1, height()));
        const double viewScale = double(zoom_) / 8.0;
        return QPointF((double(position.x()) / double(qMax(1, width())) - 0.5) * 8.0 * aspect * viewScale - double(panX_),
                       (0.5 - double(position.y()) / double(qMax(1, height()))) * 8.0 * viewScale - double(panY_));
    }
    // Intersezione del raggio del puntatore con il piano dello schizzo attivo
    // (falso se il piano si vede di taglio).
    bool rayToSketchPlane(const QPoint &position, QPointF &point) const {
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

    // Punti esatti a cui agganciarsi oltre ai segmenti: estremi delle curve,
    // centri di cerchi/archi/poligoni e vertici dei poligoni.
    QVector<QPointF> snapCandidates(const SketchObject &sketch) const {
        QVector<QPointF> points;
        for (const CurveObject &curve : sketch.curves) {
            if (curve.samples.size() >= 2) {
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

    QPointF snapPoint(const QPointF &point, bool snapToGrid = true) {
        QVector<SketchSegment> segments;
        QVector<QPointF> points;
        if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) {
            segments = sketches_.at(activeSketch_).segments;
            points = snapCandidates(sketches_.at(activeSketch_));
            if (originSnap_) points.append(QPointF(0.0, 0.0));  // origine del piano (dove passano gli assi)
        }
        const double tolerance = pickTolerance(10.0);
        ForgeCad::SnapResult result = ForgeCad::snapSegments(point, segments, points, snapEnabled_, snapToGrid, snapSpacing_, tolerance);
        lastSnapNote_.clear();
        lastSnapCurve_ = -1;
        // Curve (cerchi, archi, poligoni, spline, NURBS): i quadranti di cerchi
        // e archi come punti; poi il punto esatto piu' vicino sulla curva, se
        // e' piu' vicino del punto trovato su un segmento. I punti vincono.
        if (snapEnabled_ && activeSketch_ >= 0 && activeSketch_ < sketches_.size()
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

    // Quadranti (0, 90, 180, 270 gradi, esatti) di cerchi e archi entro la tolleranza.
    bool nearestQuadrant(const SketchObject &sketch, const QPointF &point, double tolerance, QPointF &quadrant) const {
        double best = tolerance;
        bool found = false;
        for (const CurveObject &curve : sketch.curves) {
            if ((curve.tool != DrawingTool::Circle && curve.tool != DrawingTool::Arc) || curve.controlPoints.size() < 2) continue;
            const QPointF center = curve.controlPoints.at(0);
            const double r = pointDistance(center, curve.controlPoints.at(1));
            if (r <= 0.0) continue;
            const QPointF candidates[4] = {center + QPointF(r, 0.0), center + QPointF(0.0, r), center - QPointF(r, 0.0), center - QPointF(0.0, r)};
            for (int k = 0; k < 4; ++k) {
                if (curve.tool == DrawingTool::Arc && !onArc(curve, k * M_PI_2)) continue;
                const double d = pointDistance(point, candidates[k]);
                if (d < best) {
                    best = d;
                    quadrant = candidates[k];
                    found = true;
                }
            }
        }
        return found;
    }

    // L'angolo sta nell'arco (verso antiorario dall'inizio alla fine, come curveGeometry)?
    static bool onArc(const CurveObject &arc, double angle) {
        if (arc.controlPoints.size() < 3) return false;
        const QPointF c = arc.controlPoints.at(0), a = arc.controlPoints.at(1), b = arc.controlPoints.at(2);
        const double start = std::atan2(a.y() - c.y(), a.x() - c.x());
        double sweep = std::atan2(b.y() - c.y(), b.x() - c.x()) - start;
        while (sweep <= 0.0) sweep += 2.0 * M_PI;
        double offset = std::fmod(angle - start, 2.0 * M_PI);
        if (offset < 0.0) offset += 2.0 * M_PI;
        return offset <= sweep + 1e-12;
    }

    // Punto esatto piu' vicino su una curva dello schizzo, entro `tolerance`
    // (cerchi e archi in forma chiusa, il resto proiettando sulla curva
    // esatta). Restituisce l'indice della curva (-1 nessuna).
    int nearestCurvePoint(const SketchObject &sketch, const QPointF &point, double tolerance, QPointF &result) const {
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

    void drawSnapMarkers() {
        if (!sketchMode_ || activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
        const SketchObject &sketch = sketches_.at(activeSketch_);
        glDisable(GL_LIGHTING);
        glPointSize(9.0f);
        glBegin(GL_POINTS);
        for (const CurveObject &curve : sketch.curves) {
            glColor3f(curve.tool == DrawingTool::Nurbs ? 0.95f : 1.0f, 0.35f, 0.75f);
            if (curve.tool == DrawingTool::Converted) continue;  // i poli di un riferimento non si modificano
            for (const QPointF &control : curve.controlPoints) {
                const QVector3D world = mapSketchPoint(control, sketch);
                glVertex3f(world.x(), world.y(), world.z());
            }
            for (const auto &handles : curve.tangentHandles) {
                const QVector3D first = mapSketchPoint(handles.first, sketch);
                const QVector3D second = mapSketchPoint(handles.second, sketch);
                glVertex3f(first.x(), first.y(), first.z());
                glVertex3f(second.x(), second.y(), second.z());
            }
        }
        glColor3f(1.0f, 0.45f, 0.25f);
        for (const QPointF &control : curveControlPoints_) {
            const QVector3D world = mapActiveSketchPoint(control);
            glVertex3f(world.x(), world.y(), world.z());
        }
        for (const auto &segment : sketch.segments) {
            glColor3f(1.0f, 0.85f, 0.15f);
            for (const QPointF &endpoint : {segment.first, segment.second}) {
                const QVector3D world = mapSketchPoint(endpoint, sketch);
                glVertex3f(world.x(), world.y(), world.z());
            }
            const QVector3D midpoint = mapSketchPoint((segment.first + segment.second) * 0.5, sketch);
            glColor3f(0.25f, 1.0f, 0.35f);
            glVertex3f(midpoint.x(), midpoint.y(), midpoint.z());
        }
        if (lastSnapKind_ != SnapKind::None) {
            const QVector3D world = mapSketchPoint(lastSnapPoint_, sketch);
            if (lastSnapKind_ == SnapKind::Nearest) glColor3f(0.15f, 0.85f, 1.0f);
            else if (lastSnapKind_ == SnapKind::Midpoint) glColor3f(0.25f, 1.0f, 0.35f);
            else glColor3f(1.0f, 0.85f, 0.15f);
            glVertex3f(world.x(), world.y(), world.z());
        }
        glEnd();
        glLineWidth(1.0f);
        glBegin(GL_LINES);
        for (const CurveObject &curve : sketch.curves) {
            for (int index = 0; index < curve.tangentHandles.size(); ++index) {
                const QVector3D control = mapSketchPoint(curve.controlPoints.at(index), sketch);
                const auto &handles = curve.tangentHandles.at(index);
                const QVector3D incoming = mapSketchPoint(handles.first, sketch);
                const QVector3D outgoing = mapSketchPoint(handles.second, sketch);
                glColor3f(0.35f, 0.75f, 1.0f);
                glVertex3f(incoming.x(), incoming.y(), incoming.z());
                glVertex3f(control.x(), control.y(), control.z());
                glVertex3f(control.x(), control.y(), control.z());
                glVertex3f(outgoing.x(), outgoing.y(), outgoing.z());
            }
        }
        glEnd();
    }

    // Raggio di vista (proiezione ortografica) che passa per un pixel.
    void viewRay(const QPoint &position, QVector3D &origin, QVector3D &direction) const {
        const float aspect = float(width()) / float(qMax(1, height()));
        const float viewScale = zoom_ / 8.0f;
        QMatrix4x4 rotation;
        rotation.rotate(roll_, 0.0f, 0.0f, 1.0f);
        rotation.rotate(pitch_, 1.0f, 0.0f, 0.0f);
        rotation.rotate(yaw_, 0.0f, 1.0f, 0.0f);
        rotation *= basisMatrix();
        const QMatrix4x4 inverse = rotation.inverted();
        origin = inverse.map(QVector3D(
            (float(position.x()) / float(qMax(1, width())) - 0.5f) * 8.0f * aspect * viewScale - panX_,
            (0.5f - float(position.y()) / float(qMax(1, height()))) * 8.0f * viewScale - panY_,
            float(sceneDepth())));
        direction = inverse.mapVector(QVector3D(0.0f, 0.0f, -1.0f));
    }

    // Oggetto sotto il puntatore, in ordine di priorita': linee degli schizzi,
    // solidi estrusi (il piu' vicino all'osservatore), piani di riferimento.
    SceneSelection pickSceneObject(const QPoint &position) const {
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
            if (!body.visible || !isCurveBody(body)) continue;
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
        double skipped = 0.0;
        const bool reaches = sectionRay(origin, direction, skipped);
        for (int index = 0; index < extrusions_.size() && reaches; ++index) {
            const ExtrusionObject &extrusion = extrusions_.at(index);
            if (!extrusion.visible || !isShapeBody(extrusion)) continue;
            FaceHit face;
            bool hit = cachedRayFace(index, origin, direction, face);
            double distance = hit ? face.distance : 0.0;
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

    bool pickVisibleSketchEntity(const QPoint &position, int &sketchIndex, int &kind, int &entityIndex) const {
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

    static void strokeHighlight(QPainter &painter, const QVector<QPointF> &points,
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

    QVector<QPointF> projectSketchPolyline(const QVector<QPointF> &points, const SketchObject &sketch) const {
        QVector<QPointF> projected;
        projected.reserve(points.size());
        for (const QPointF &point : points) projected.append(projectWorldPoint(mapSketchPoint(point, sketch)));
        return projected;
    }

    void highlightSketchElement(QPainter &painter, const SketchObject &sketch,
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

    void highlightSceneObject(QPainter &painter, const SceneSelection &target, const QColor &color) const {
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

    void drawSelectionHighlight() {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        drawSectionHandle(painter);
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
        if (refPicking_ || !refMarks_.isEmpty()) {
            // Scelta dei riferimenti di un piano di costruzione: quelli scelti e quello sotto il puntatore.
            for (const GeometryRef &mark : refMarks_) drawGeometryRef(painter, mark, kSelectionColor);
            // Le curve 3D autonome non hanno vertici di un B-rep: quando si
            // cerca un punto rendiamo espliciti i loro due estremi cliccabili.
            if (refPicking_ && (refPickRoles_ & ForgeCad::DatumRolePoint)) {
                painter.setPen(QPen(QColor(130, 215, 255, 210), 1.5));
                painter.setBrush(QColor(35, 105, 145, 210));
                for (int body = 0; body < extrusions_.size(); ++body)
                    for (int end = 0; end < 2; ++end) {
                        QVector3D point;
                        if (curveBodyEndpoint(body, end, point)) painter.drawEllipse(projectWorldPoint(point), 4.5, 4.5);
                    }
            }
            if (refPicking_ && refHoverValid_) drawGeometryRef(painter, refHover_, kHoverColor, &refHoverFace_, refHoverFaceBody_);
            return;
        }
        if (hover_ != selection_ && !selectedObjects_.contains(hover_)) highlightSceneObject(painter, hover_, kHoverColor);
        highlightSceneObject(painter, selection_, kSelectionColor);
        for (const SceneSelection &object : selectedObjects_)
            if (object != selection_) highlightSceneObject(painter, object, kSelectionColor);
    }

    int pickReferencePlane(const QPoint &position) const {
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

    bool isPlaneShown(int plane) const {
        return referencePlanesVisible_ && plane >= 0 && plane < 3 && planeVisible_[plane];
    }

    static QVector<QVector3D> planeCorners(int plane, float u, float v) {
        if (plane == 0) return {QVector3D(-u, -v, 0), QVector3D(u, -v, 0), QVector3D(u, v, 0), QVector3D(-u, v, 0)};
        if (plane == 1) return {QVector3D(-u, 0, -v), QVector3D(u, 0, -v), QVector3D(u, 0, v), QVector3D(-u, 0, v)};
        return {QVector3D(0, -u, -v), QVector3D(0, u, -v), QVector3D(0, u, v), QVector3D(0, -u, v)};
    }
    static QVector<QVector3D> planeCorners(int plane, float h) { return planeCorners(plane, h, h); }
    QSizeF referencePlaneExtents(int plane) const {
        updateSceneBounds();
        return referencePlaneAuto_[plane] * referencePlaneScales_[plane];
    }
    double referencePlaneHalf(int plane) const {
        const QSizeF size = referencePlaneExtents(plane);
        return qMax(size.width(), size.height());
    }
    QVector<QVector3D> planeCorners(int plane) const {
        const QSizeF size = referencePlaneExtents(plane);
        return planeCorners(plane, float(size.width()), float(size.height()));
    }

    // Maniglie solo sul piano selezionato: il trascinamento non sposta il piano geometrico.
    bool resizablePlane(SceneSelection &target, QVector<QVector3D> &corners, QVector3D &center, double &half) const {
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
    int planeResizeHandleAt(const QPoint &position) const {
        SceneSelection target;
        QVector<QVector3D> corners;
        QVector3D center;
        double half;
        if (!resizablePlane(target, corners, center, half)) return -1;
        for (int k = 0; k < corners.size(); ++k)
            if (pointDistance(projectWorldPoint(corners[k]), position) <= 9.0) return k;
        return -1;
    }
    bool beginPlaneResize(const QPoint &position) {
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
    void finishPlaneResize(bool cancel) {
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
    void drawPlaneResizeHandles() {
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

    void drawReferencePlanes() {
        glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT);
        glDisable(GL_LIGHTING);
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
        glBegin(GL_QUADS);
        for (int plane = 0; plane < 3; ++plane) {
            if (!isPlaneShown(plane)) continue;
            const float alpha = baseAlpha[plane] + 0.10f * emphasis(plane);
            glColor4f(colors[plane][0], colors[plane][1], colors[plane][2], alpha);
            for (const QVector3D &corner : planeCorners(plane)) glVertex3f(corner.x(), corner.y(), corner.z());
        }
        glEnd();
        // Bordo: un alone largo e tenue del colore del piano, schiarito, e sopra
        // una linea sottile piu' luminosa dello stesso colore.
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        const struct { GLfloat width, alpha, light; } strokes[] = {{4.0f, 0.10f, 0.25f}, {1.3f, 0.55f, 0.40f}};
        for (const auto &stroke : strokes) {
            glLineWidth(stroke.width);
            for (int plane = 0; plane < 3; ++plane) {
                if (!isPlaneShown(plane)) continue;
                const float alpha = qMin(1.0f, stroke.alpha * (1.0f + 0.6f * emphasis(plane)));
                const GLfloat *c = colors[plane];
                glColor4f(c[0] + (1.0f - c[0]) * stroke.light, c[1] + (1.0f - c[1]) * stroke.light,
                          c[2] + (1.0f - c[2]) * stroke.light, alpha);
                glBegin(GL_LINE_LOOP);
                for (const QVector3D &corner : planeCorners(plane)) glVertex3f(corner.x(), corner.y(), corner.z());
                glEnd();
            }
        }
        glPopAttrib();
    }

    // --- Piani di costruzione a video ---------------------------------------------

    // Mezza misura a video di un piano di costruzione: quella data o meta' dei piani di riferimento.
    double datumHalf(double size) const { return size > 0.0 ? size : 0.5 * double(planeHalf()); }
    QVector<QVector3D> datumCorners(const SketchFrame &f, double size) const {
        const QVector3D c(float(f.origin[0]), float(f.origin[1]), float(f.origin[2]));
        const QVector3D x(float(f.xAxis[0]), float(f.xAxis[1]), float(f.xAxis[2]));
        const QVector3D n(float(f.normal[0]), float(f.normal[1]), float(f.normal[2]));
        const QVector3D y = QVector3D::crossProduct(n, x);
        const float h = float(datumHalf(size));
        return {c - h * x - h * y, c + h * x - h * y, c + h * x + h * y, c - h * x + h * y};
    }
    bool isDatumShown(int index) const {
        if (index < 0 || index >= extrusions_.size()) return false;
        const ExtrusionObject &body = extrusions_.at(index);
        return isDatumBody(body) && body.visible && body.datumValid && index != datumPreviewIndex_;
    }
    // Piano di costruzione sotto il puntatore: il bordo entro qualche pixel
    // (`border`) o l'interno lungo il raggio (`distance`); -1 se nessuno.
    int pickDatumPlane(const QPoint &position, bool border, double *distance = nullptr) const {
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
    void drawDatumQuad(const QVector<QVector3D> &corners, const GLfloat *color, float emphasis) {
        glColor4f(color[0], color[1], color[2], 0.07f + 0.10f * emphasis);
        glBegin(GL_QUADS);
        for (const QVector3D &p : corners) glVertex3f(p.x(), p.y(), p.z());
        glEnd();
        const struct { GLfloat width, alpha, light; } strokes[] = {{4.0f, 0.12f, 0.25f}, {1.4f, 0.65f, 0.40f}};
        for (const auto &stroke : strokes) {
            glLineWidth(stroke.width);
            glColor4f(color[0] + (1.0f - color[0]) * stroke.light, color[1] + (1.0f - color[1]) * stroke.light, color[2] + (1.0f - color[2]) * stroke.light,
                      qMin(1.0f, stroke.alpha * (1.0f + 0.6f * emphasis)));
            glBegin(GL_LINE_LOOP);
            for (const QVector3D &p : corners) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
        }
        glLineWidth(1.0f);
    }
    // Come i piani di riferimento: trasparenti, dietro ai corpi, con il bordo schiarito.
    void drawDatumPlanes() {
        glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT);
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_LINE_SMOOTH);
        static const GLfloat datumColor[] = {0.72f, 0.52f, 0.98f}, previewColor[] = {0.98f, 0.66f, 0.28f};
        for (int index = 0; index < extrusions_.size(); ++index) {
            if (!isDatumShown(index)) continue;
            const ExtrusionObject &body = extrusions_.at(index);
            const SceneSelection self{SceneObjectKind::Extrusion, index, -1};
            const float emphasis = (selection_ == self ? 1.0f : 0.0f) + (hover_ == self ? 0.8f : 0.0f);
            drawDatumQuad(datumCorners(body.datumFrame, body.datum.size), datumColor, emphasis);
        }
        if (datumPreviewValid_) drawDatumQuad(datumCorners(datumPreviewFrame_, datumPreviewSize_), previewColor, 1.0f);
        glPopAttrib();
    }
    void drawDatumLabels(QPainter &painter) const {
        painter.setFont(QFont(QStringLiteral("Sans"), 9, QFont::DemiBold));
        painter.setPen(QColor(205, 180, 255));
        for (int index = 0; index < extrusions_.size(); ++index) {
            if (!isDatumShown(index)) continue;
            const ExtrusionObject &body = extrusions_.at(index);
            const QVector<QVector3D> corners = datumCorners(body.datumFrame, body.datum.size);
            const QVector3D c(float(body.datumFrame.origin[0]), float(body.datumFrame.origin[1]), float(body.datumFrame.origin[2]));
            painter.drawText(projectWorldPoint(c + 0.8f * (corners.at(2) - c)), body.name);
        }
    }

    // --- Scelta dei riferimenti -----------------------------------------------------

    // Corpi da cui si puo' prendere un riferimento: visibili, con una forma, prima del piano in modifica.
    bool referenceBodyEligible(int index) const {
        const int owner = refPickOwner_ < 0 ? int(extrusions_.size()) : refPickOwner_;
        return index >= 0 && index < owner && index < extrusions_.size() && extrusions_.at(index).visible && isShapeBody(extrusions_.at(index));
    }
    bool referenceSketchEligible(int index) const {
        if (index < 0 || index >= sketches_.size() || !sketches_.at(index).visible) return false;
        const int datum = sketches_.at(index).datumPlane;
        return datum < 0 || refPickOwner_ < 0 || datum < refPickOwner_;
    }
    bool curveBodyEndpoint(int index, int end, QVector3D &point) const {
        const int owner = refPickOwner_ < 0 ? int(extrusions_.size()) : refPickOwner_;
        if (index < 0 || index >= owner || index >= extrusions_.size()) return false;
        const ExtrusionObject &body = extrusions_.at(index);
        if (!body.visible || !isCurveBody(body) || !body.curve) return false;
        const ForgeCad::Kernel::Interval domain = body.curve->domain();
        const ForgeCad::Kernel::Vec3 p = body.curve->point(end == 1 ? domain.hi : domain.lo);
        point = QVector3D(float(p.x()), float(p.y()), float(p.z()));
        return true;
    }
    // Riferimento sotto il puntatore, con la priorita': punti, poi rette e curve, poi facce e piani.
    bool pickReference(const QPoint &position, GeometryRef &ref, FaceHit *faceHit = nullptr, int *faceBody = nullptr) const {
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
                for (const QVector<QVector3D> &polyline : extrusions_.at(b).display.edges)
                    for (const QVector3D &p : {polyline.first(), polyline.last()}) {
                        r.point = {p.x(), p.y(), p.z()};
                        consider(p, r);
                    }
            }
            for (int b = 0; b < extrusions_.size(); ++b)
                for (int end = 0; end < 2; ++end) {
                    QVector3D point;
                    if (!curveBodyEndpoint(b, end, point)) continue;
                    GeometryRef r;
                    r.kind = 10;
                    r.index = b;
                    r.element = {-1, -1, end};
                    r.point = {point.x(), point.y(), point.z()};
                    consider(point, r);
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
                        candidate.point = {p.x(), p.y(), p.z()};
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
                    for (const QVector<QVector3D> &polyline : body.display.edges) considerPolyline(polyline, r, true);
                } else if (b < owner && body.visible && isCurveBody(body) && body.curve) {
                    r.kind = 9;
                    for (const QVector<QVector3D> &polyline : body.display.edges) considerPolyline(polyline, r, false);
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
                ref.point = {double(origin.x()) + double(direction.x()) / length * hit.distance, double(origin.y()) + double(direction.y()) / length * hit.distance,
                             double(origin.z()) + double(direction.z()) / length * hit.distance};
                if (faceHit) *faceHit = hit;
                if (faceBody) *faceBody = b;
                found = true;
            }
            if (roles & (ForgeCad::DatumRolePlane | ForgeCad::DatumRoleFace)) {
                double distance = 0.0;
                const int datum = pickDatumPlane(position, false, &distance);
                if (datum >= 0 && datum < owner && distance < nearest) {
                    ref = GeometryRef();
                    ref.kind = 8;
                    ref.index = datum;
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
    // Il riferimento evidenziato (QPainter): punto, polilinea, bordo del piano o della faccia.
    void drawGeometryRef(QPainter &painter, const GeometryRef &ref, const QColor &color, const FaceHit *face = nullptr, int faceBody = -1) const {
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
        case 5:
            if (face && faceBody == ref.index) {
                for (int e : faceDisplayEdges(ref.index, *face)) {
                    QVector<QPointF> projected;
                    for (const QVector3D &p : extrusions_.at(ref.index).display.edges.at(e)) projected.append(projectWorldPoint(p));
                    strokeHighlight(painter, projected, false, color);
                }
            }
            dot(point);
            break;
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
            if (ref.index >= 0 && ref.index < sketches_.size()) highlightSketchElement(painter, sketches_.at(ref.index), {ref.element.kind, ref.element.element}, color);
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
            if (curveBodyEndpoint(ref.index, ref.element.point, endpoint)) dot(endpoint);
            else dot(point);
            break;
        }
        default: break;
        }
    }

    // La fusione con GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA mescola anche il
    // canale alfa del framebuffer, che scende sotto 1 dove si disegna qualcosa
    // di trasparente (i piani, le anteprime). Il compositore di Wayland (niri)
    // usa quell'alfa per la finestra e vi fa vedere attraverso il desktop: alla
    // fine della scena l'alfa torna 1 ovunque, senza toccare i colori.
    void makeOpaque() {
        GLfloat clear[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(clear[0], clear[1], clear[2], clear[3]);
    }

    void configureLighting() {
        const GLfloat ambient[] = {0.20f, 0.22f, 0.26f, 1.0f};
        // Luci puntiformi fuori dalla scena, legate alla vista: la principale in
        // alto a destra davanti all'osservatore, quella di riempimento a
        // sinistra; dal centro della geometria visibile a tre volte il raggio
        // della sfera che la contiene (almeno quello dei piani di riferimento).
        // Seguono i modelli grandi, non finiscono mai dentro i solidi e
        // illuminano sempre le facce verso chi guarda, in qualsiasi orientamento.
        updateSceneBounds();
        const QVector3D center = 0.5f * (sceneMin_ + sceneMax_);
        const float reach = 3.0f * qMax(0.5f * (sceneMax_ - sceneMin_).length(), 4.0f);
        GLfloat view[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, view);
        // Direzione della vista -> del modello: la trasposta della rotazione (colonne della matrice OpenGL).
        const auto toModel = [&view](const QVector3D &eye) {
            const QVector3D d = eye.normalized();
            QVector3D w(view[0] * d.x() + view[1] * d.y() + view[2] * d.z(), view[4] * d.x() + view[5] * d.y() + view[6] * d.z(),
                        view[8] * d.x() + view[9] * d.y() + view[10] * d.z());
            return w.normalized();
        };
        const QVector3D key = center + reach * toModel(QVector3D(0.5f, 0.7f, 1.0f)), fill = center + reach * toModel(QVector3D(-0.6f, 0.3f, 0.5f));
        const GLfloat keyPosition[] = {key.x(), key.y(), key.z(), 1};
        const GLfloat fillPosition[] = {fill.x(), fill.y(), fill.z(), 1};
        const GLfloat keyColor[] = {1.0f, 0.92f, 0.80f, 1};
        const GLfloat fillColor[] = {0.60f, 0.76f, 1.0f, 1};
        const GLfloat inspectionColor[] = {0.80f, 1.0f, 0.88f, 1};
        const GLfloat specular[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_LIGHT1);
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
        // Con la luce ambiente attiva le luci principali cedono parte del loro
        // contributo allo sfondo, per non sovraesporre gli oggetti.
        const float keyScale = background_.affectsLighting
            ? 1.0f - 0.35f * qBound(0.0f, background_.lightingStrength, 1.0f) : 1.0f;
        const GLfloat *keySource = lightingPreset_ == 2 ? inspectionColor : keyColor;
        const GLfloat scaledKey[] = {keySource[0] * keyScale, keySource[1] * keyScale, keySource[2] * keyScale, 1};
        const GLfloat scaledFill[] = {fillColor[0] * keyScale, fillColor[1] * keyScale, fillColor[2] * keyScale, 1};
        glLightfv(GL_LIGHT0, GL_POSITION, keyPosition);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, scaledKey);
        glLightfv(GL_LIGHT1, GL_POSITION, fillPosition);
        glLightfv(GL_LIGHT1, GL_DIFFUSE, scaledFill);
        if (lightingPreset_ == 1) glDisable(GL_LIGHT1);
        configureEnvironmentLighting(ambient);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, lightingPreset_ == 1 ? 36.0f : 110.0f);
    }

    // Illuminazione dall'ambiente: lo sfondo agisce come una sorgente emisferica.
    // Due luci direzionali in coordinate vista arrivano dai lati dello schermo
    // dove si trovano i due colori, e la luce ambiente assume il loro colore medio.
    void configureEnvironmentLighting(const GLfloat *baseAmbient) {
        if (!background_.affectsLighting) {
            glDisable(GL_LIGHT2);
            glDisable(GL_LIGHT3);
            return;
        }
        const float strength = qBound(0.0f, background_.lightingStrength, 1.0f);
        const QColor start = background_.startColor;
        const QColor end = background_.gradient ? background_.endColor : start;
        const GLfloat ambient[] = {
            baseAmbient[0] * (1.0f - 0.5f * strength) + float(start.redF() + end.redF()) * 0.25f * strength,
            baseAmbient[1] * (1.0f - 0.5f * strength) + float(start.greenF() + end.greenF()) * 0.25f * strength,
            baseAmbient[2] * (1.0f - 0.5f * strength) + float(start.blueF() + end.blueF()) * 0.25f * strength,
            1.0f};
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
        const float radians = background_.angle * float(M_PI) / 180.0f;
        const float dx = std::cos(radians), dy = std::sin(radians);
        const GLfloat startDirection[] = {-dx, -dy, 0.6f, 0.0f};
        const GLfloat endDirection[] = {dx, dy, 0.6f, 0.0f};
        const float diffuse = 0.55f * strength;
        const GLfloat startColor[] = {float(start.redF()) * diffuse, float(start.greenF()) * diffuse,
                                      float(start.blueF()) * diffuse, 1.0f};
        const GLfloat endColor[] = {float(end.redF()) * diffuse, float(end.greenF()) * diffuse,
                                    float(end.blueF()) * diffuse, 1.0f};
        const GLfloat noSpecular[] = {0.0f, 0.0f, 0.0f, 1.0f};
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glLightfv(GL_LIGHT2, GL_POSITION, startDirection);
        glLightfv(GL_LIGHT3, GL_POSITION, endDirection);
        glPopMatrix();
        glLightfv(GL_LIGHT2, GL_DIFFUSE, startColor);
        glLightfv(GL_LIGHT3, GL_DIFFUSE, endColor);
        glLightfv(GL_LIGHT2, GL_SPECULAR, noSpecular);
        glLightfv(GL_LIGHT3, GL_SPECULAR, noSpecular);
        glEnable(GL_LIGHT2);
        glEnable(GL_LIGHT3);
    }

    bool isSketchDrawn(int index) const {
        return sketches_.at(index).visible || (sketchMode_ && index == activeSketch_);
    }

    void drawSketch() {
        glDisable(GL_LIGHTING);
        // In modalita' schizzo gli schizzi stanno sopra i corpi: anche le entita'
        // dentro un solido (sezioni) o sugli spigoli (riferimenti) si vedono.
        if (sketchMode_) glDisable(GL_DEPTH_TEST);
        // Entita' di costruzione: tratteggiate, in grigio azzurro.
        const auto setConstructionStyle = [this](bool construction) {
            if (construction) {
                glEnable(GL_LINE_STIPPLE);
                glLineStipple(2, 0x00FF);
                glColor3f(0.55f, 0.72f, 0.85f);
            } else {
                glDisable(GL_LINE_STIPPLE);
            }
        };
        glLineWidth(2.0f);
        const double markerSize = pickTolerance(7.0);
        // Nello schizzo attivo le entita' completamente definite sono bianche (come in SolidWorks, dove sono nere).
        const ForgeCad::SketchAnalysis *analysis = sketchMode_ && activeSketchObject() ? &sketchAnalysis() : nullptr;
        for (int sketchIndex = 0; sketchIndex < sketches_.size(); ++sketchIndex) {
            if (!isSketchDrawn(sketchIndex)) continue;
            const SketchObject &sketch = sketches_.at(sketchIndex);
            const bool active = analysis && sketchIndex == activeSketch_;
            for (int index = 0; index < sketch.segments.size(); ++index) {
                const auto &segment = sketch.segments.at(index);
                const bool construction = sketch.isConstructionSegment(index);
                setConstructionStyle(construction);
                if (sketch.symmetryAxes.contains(index)) {
                    // Asse di simmetria: linea d'asse (tratto e punto), in verde acqua.
                    glEnable(GL_LINE_STIPPLE);
                    glLineStipple(2, 0x27FF);
                    glColor3f(0.35f, 0.88f, 0.78f);
                }
                if (!construction) {
                    if (active && analysis->segmentDefined.value(index)) glColor3f(0.94f, 0.96f, 1.0f);
                    else glColor3f(1.0f, 0.75f, 0.15f);
                }
                const QVector3D first = mapSketchPoint(segment.first, sketch);
                const QVector3D second = mapSketchPoint(segment.second, sketch);
                if (pointDistance(segment.first, segment.second) <= ForgeCad::kSketchConnectionTolerance && construction) {
                    glColor3f(0.35f, 0.92f, 0.92f);
                    glBegin(GL_LINES);
                    for (const QPointF &arm : {QPointF(markerSize, 0.0), QPointF(0.0, markerSize)}) {
                        const QVector3D a = mapSketchPoint(segment.first - arm, sketch), b = mapSketchPoint(segment.first + arm, sketch);
                        glVertex3f(a.x(), a.y(), a.z());
                        glVertex3f(b.x(), b.y(), b.z());
                    }
                    glEnd();
                    continue;
                }
                glBegin(GL_LINES);
                glVertex3f(first.x(), first.y(), first.z());
                glVertex3f(second.x(), second.y(), second.z());
                glEnd();
            }
        }
        // Il tratteggio dell'ultimo segmento di costruzione non deve restare acceso
        // (altrimenti al disegno successivo tratteggia anche gli spigoli dei corpi).
        glDisable(GL_LINE_STIPPLE);
        glLineWidth(2.5f);
        for (int sketchIndex = 0; sketchIndex < sketches_.size(); ++sketchIndex) {
            if (!isSketchDrawn(sketchIndex)) continue;
            const SketchObject &sketch = sketches_.at(sketchIndex);
            const bool active = analysis && sketchIndex == activeSketch_;
            for (int curveIndex = 0; curveIndex < sketch.curves.size(); ++curveIndex) {
                const CurveObject &curve = sketch.curves.at(curveIndex);
                setConstructionStyle(curve.construction);
                if (!curve.construction) {
                    if (active && analysis->curveDefined.value(curveIndex)) glColor3f(0.94f, 0.96f, 1.0f);
                    else glColor3f(curve.tool == DrawingTool::Nurbs ? 0.85f : 0.95f, curve.tool == DrawingTool::Nurbs ? 0.35f : 0.65f, 1.0f);
                }
                glBegin(GL_LINE_STRIP);
                for (const QPointF &sample : curve.samples) {
                    const QVector3D world = mapSketchPoint(sample, sketch);
                    glVertex3f(world.x(), world.y(), world.z());
                }
                glEnd();
                setConstructionStyle(false);
                glColor3f(0.75f, 0.75f, 0.80f);
                glBegin(GL_LINE_STRIP);
                for (const QPointF &control : curve.controlPoints) {
                    const QVector3D world = mapSketchPoint(control, sketch);
                    glVertex3f(world.x(), world.y(), world.z());
                }
                glEnd();
            }
        }
        // Se una guida della loft non attraversa una sezione, la sezione viene
        // ridisegnata sopra il modello in rosso, anche se lo schizzo era
        // nascosto. Scompare appena l'anteprima torna valida o la finestra si chiude.
        if (previewErrorSketch_ >= 0 && previewErrorSketch_ < sketches_.size()) {
            const SketchObject &failed = sketches_.at(previewErrorSketch_);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_LINE_STIPPLE);
            glColor3f(1.0f, 0.16f, 0.12f);
            glLineWidth(6.0f);
            for (const SketchSegment &segment : failed.segments) {
                const QVector3D a = mapSketchPoint(segment.first, failed), b = mapSketchPoint(segment.second, failed);
                glBegin(GL_LINES); glVertex3f(a.x(), a.y(), a.z()); glVertex3f(b.x(), b.y(), b.z()); glEnd();
            }
            for (const CurveObject &curve : failed.curves) {
                glBegin(GL_LINE_STRIP);
                for (const QPointF &sample : curve.samples) {
                    const QVector3D p = mapSketchPoint(sample, failed); glVertex3f(p.x(), p.y(), p.z());
                }
                glEnd();
            }
            glLineWidth(2.0f);
            glColor3f(1.0f, 0.85f, 0.20f);
            for (const SketchSegment &segment : failed.segments) {
                const QVector3D a = mapSketchPoint(segment.first, failed), b = mapSketchPoint(segment.second, failed);
                glBegin(GL_LINES); glVertex3f(a.x(), a.y(), a.z()); glVertex3f(b.x(), b.y(), b.z()); glEnd();
            }
            for (const CurveObject &curve : failed.curves) {
                glBegin(GL_LINE_STRIP);
                for (const QPointF &sample : curve.samples) {
                    const QVector3D p = mapSketchPoint(sample, failed); glVertex3f(p.x(), p.y(), p.z());
                }
                glEnd();
            }
        }
        const double cursorMarkerSize = pickTolerance(10.0);
        glBegin(GL_LINES);
        if (hasPendingPoint_) {
            const QVector3D world = mapActiveSketchPoint(pendingPoint_);
            glColor3f(1.0f, 0.35f, 0.20f);
            const QVector3D left = mapActiveSketchPoint(pendingPoint_ - QPointF(cursorMarkerSize, 0.0));
            const QVector3D right = mapActiveSketchPoint(pendingPoint_ + QPointF(cursorMarkerSize, 0.0));
            glVertex3f(left.x(), left.y(), left.z());
            glVertex3f(right.x(), right.y(), right.z());
            if (drawingTool_ == DrawingTool::Line || drawingTool_ == DrawingTool::Polyline
                || drawingTool_ == DrawingTool::ConstructionLine) {
                const QPointF previewPoint = currentInference_.point;
                const QVector3D previewWorld = mapActiveSketchPoint(previewPoint);
                glColor3f(1.0f, 0.45f, 0.20f);
                glVertex3f(world.x(), world.y(), world.z());
                glVertex3f(previewWorld.x(), previewWorld.y(), previewWorld.z());
            }
        } else if (sketchMode_ && (drawingTool_ == DrawingTool::Line || drawingTool_ == DrawingTool::ConstructionLine
                                   || drawingTool_ == DrawingTool::Polyline)) {
            // Croce del cursore nel piano dello schizzo, di dimensione fissa in pixel.
            glColor3f(1.0f, 0.45f, 0.20f);
            for (const QPointF &arm : {QPointF(cursorMarkerSize, 0.0), QPointF(0.0, cursorMarkerSize)}) {
                const QVector3D a = mapActiveSketchPoint(cursorSketchPoint_ - arm);
                const QVector3D b = mapActiveSketchPoint(cursorSketchPoint_ + arm);
                glVertex3f(a.x(), a.y(), a.z());
                glVertex3f(b.x(), b.y(), b.z());
            }
        }
        glEnd();
        glEnable(GL_DEPTH_TEST);
    }

    // Solo visualizzazione: triangoli e spigoli ricavati dalla forma esatta.
    void drawExtrusionFaces(const ExtrusionObject &extrusion) { drawDisplayFaces(extrusion.display); }
    void drawDisplayFaces(const BodyDisplay &display) { displayCache_.faces(display); }
    void drawExtrusionEdges(const ExtrusionObject &extrusion) { displayCache_.edges(extrusion.display); }

    // Bordo di evidenziazione: la sagoma dell'oggetto va nello stencil, poi le
    // sue facce vengono ridisegnate a linee spesse solo fuori dalla sagoma.
    void drawExtrusionOutline(const ExtrusionObject &extrusion, const QColor &color, float width) {
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        // Una curva non ha una sagoma di facce: lo stencil e il doppio passaggio
        // servono solo ai corpi. Evidenziarla direttamente evita lavoro inutile
        // durante la navigazione con un'elica selezionata.
        if (isCurveBody(extrusion)) {
            glLineWidth(width);
            glColor3f(float(color.redF()), float(color.greenF()), float(color.blueF()));
            drawExtrusionEdges(extrusion);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            glLineWidth(1.0f);
            return;
        }
        glEnable(GL_STENCIL_TEST);
        glClear(GL_STENCIL_BUFFER_BIT);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        drawExtrusionFaces(extrusion);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(width);
        glColor3f(float(color.redF()), float(color.greenF()), float(color.blueF()));
        drawExtrusionFaces(extrusion);
        drawExtrusionEdges(extrusion);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_STENCIL_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glLineWidth(1.0f);
    }

    QString edgePickMessage() const {
        if (edgePickHelix_) return QStringLiteral("Elica: clicca uno spigolo circolare o una faccia cilindrica o conica per la base, Esc annulla");
        QString preview;
        if (!pickedEdges_.isEmpty())
            preview = preview_.valid ? QStringLiteral(" - anteprima") : preview_.error.isEmpty() ? QStringLiteral(" - anteprima in calcolo...")
                                                                                          : QStringLiteral(" - anteprima non riuscita: ") + preview_.error;
        if (edgePickBody_ < 0)
            return QStringLiteral("%1: clicca %2, Esc annulla")
                .arg(edgePickExtend_ ? QStringLiteral("Estensione") : edgePickChamfer_ ? QStringLiteral("Smusso") : QStringLiteral("Raccordo"))
                .arg(edgePickExtend_ ? QStringLiteral("i bordi di una superficie") : QStringLiteral("gli spigoli di un solido (o una sua faccia per tutti i suoi bordi)"));
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
    // Un punto per ogni spigolo scelto: un campione interno della polilinea
    // (i campioni stanno sulla curva esatta), il punto medio se e' un segmento.
    QVector<EdgePoint> pickedEdgePoints() const {
        QVector<EdgePoint> points;
        if (edgePickBody_ < 0 || edgePickBody_ >= extrusions_.size()) return points;
        const QVector<QVector<QVector3D>> &edges = extrusions_.at(edgePickBody_).display.edges;
        for (int index : pickedEdges_) {
            if (index < 0 || index >= edges.size() || edges.at(index).size() < 2) continue;
            const QVector<QVector3D> &polyline = edges.at(index);
            const QVector3D p = polyline.size() >= 3 ? polyline.at(polyline.size() / 2) : 0.5f * (polyline.first() + polyline.last());
            points.append({p.x(), p.y(), p.z()});
        }
        return points;
    }
    // Gli spigoli scelti sono cambiati: messaggio e anteprima.
    void edgePicked() {
        if (!edgePickPreviewEnabled_ || pickedEdges_.isEmpty() || edgePickHelix_) clearBlendPreview();
        else if (edgePickExtend_) requestExtendPreview(edgePickBody_, pickedEdgePoints(), edgePickSize_, edgePickLinear_, edgePickEdit_);
        else requestBlendPreview(edgePickBody_, pickedEdgePoints(), edgePickSize_, edgePickChamfer_, edgePickEdit_, edgePickSpec_);
        if (edgePickStatus_) edgePickStatus_(edgePickMessage());
        if (edgePickChanged_) edgePickChanged_(edgePickBody_, pickedEdgePoints());
        update();
    }
    void cancelEdgePick(bool keepPreview = false) {
        if (!keepPreview) clearBlendPreview();
        edgePickEdit_ = -1;
        edgePickBody_ = -1;
        edgePicking_ = false;
        edgePickHelix_ = false;
        hoverEdgeBody_ = -1;
        pickedEdges_.clear();
        hoverEdge_ = -1;
        hoverFaceEdges_.clear();
        if (edgePickStatus_) edgePickStatus_(QString());
        update();
    }
    void finishEdgePick() {
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
    // Il corpo puo' dare gli spigoli della scelta: visibile (o quello gia' scelto),
    // un solido per raccordi e smussi, una superficie per l'estensione.
    bool edgePickEligible(int index) const {
        if (index < 0 || index >= extrusions_.size()) return false;
        const ExtrusionObject &body = extrusions_.at(index);
        if (!isShapeBody(body) || (!body.visible && index != edgePickBody_)) return false;
        if (edgePickHelix_) return true;
        return edgePickExtend_ ? !body.solid : body.solid;
    }
    struct ProjectedEdges {
        BodyDisplay source;
        std::array<double, 18> view{};
        QVector<QVector<QPointF>> lines;
        QVector<QRectF> bounds;
        bool valid = false;
    };
    const ProjectedEdges &projectedBodyEdges(int index) const {
        projectWorldPoint(QVector3D()); // aggiorna la chiave della vista
        if (projectedEdges_.size() != extrusions_.size()) projectedEdges_.resize(extrusions_.size());
        auto &cache = projectedEdges_[index];
        const auto &display = extrusions_.at(index).display;
        if (cache.valid && cache.view == projectionKey_ && cache.source.edges.constData() == display.edges.constData()) return cache;
        cache.source = display;
        cache.view = projectionKey_;
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
    // Spigolo sotto il puntatore (entro 8 pixel), -1 se nessuno, e il suo corpo in
    // `body`: quello in scelta o, finche' non ci sono spigoli scelti, qualsiasi corpo adatto.
    int pickEdge(const QPoint &position, int *body = nullptr) const {
        int best = -1, bestBody = -1;
        double nearest = 8.0;
        for (int candidate = 0; candidate < extrusions_.size(); ++candidate) {
            if (!pickedEdges_.isEmpty() && candidate != edgePickBody_) continue;
            if (!edgePickEligible(candidate)) continue;
            const auto &projected = projectedBodyEdges(candidate);
            const auto &edges = projected.lines;
            for (int index = 0; index < edges.size(); ++index) {
                if (!projected.bounds.at(index).adjusted(-9, -9, 9, 9).contains(position)) continue;
                const auto &polyline = edges.at(index);
                for (int k = 1; k < polyline.size(); ++k) {
                    // A parita' di distanza vince il corpo gia' in scelta.
                    const double d = distanceToSegment(QPointF(position), polyline.at(k - 1), polyline.at(k))
                                   - (candidate == edgePickBody_ ? 0.5 : 0.0);
                    if (d < nearest) {
                        nearest = d;
                        best = index;
                        bestBody = candidate;
                    }
                }
            }
        }
        if (body) *body = bestBody;
        return best;
    }
    // Corpo adatto sotto il puntatore per il clic su una faccia (quello in scelta, se c'e').
    int edgePickFaceBody(const QPoint &position) const {
        if (edgePickBody_ >= 0 && !pickedEdges_.isEmpty()) return edgePickBody_;
        const SceneSelection hit = pickSceneObject(position);
        if (hit.kind == SceneObjectKind::Extrusion && edgePickEligible(hit.index)) return hit.index;
        return edgePickBody_;
    }
    struct RaySelectionCache {
        ForgeCad::ForgeBody body;
        QVector3D origin, direction;
        FaceHit face;
        bool valid = false, hit = false;
    };
    bool cachedRayFace(int index, const QVector3D &origin, const QVector3D &direction, FaceHit &hit) const {
        const ExtrusionObject &body = extrusions_.at(index);
        if (!body.forgeBody) return false;
        if (raySelectionCache_.size() != extrusions_.size()) raySelectionCache_.resize(extrusions_.size());
        auto &cache = raySelectionCache_[index];
        if (!cache.valid || cache.body != body.forgeBody || cache.origin != origin || cache.direction != direction) {
            cache.body = body.forgeBody;
            cache.origin = origin;
            cache.direction = direction;
            cache.hit = ForgeCad::forgePickFace(*body.forgeBody, origin, direction, cache.face, body.display.rayIndex.get());
            cache.valid = true;
        }
        if (cache.hit) hit = cache.face;
        return cache.hit;
    }
    // Faccia del corpo `index` sotto il pixel (geometria esatta).
    bool pickBodyFace(int index, const QPoint &position, FaceHit &hit) const {
        if (index < 0 || index >= extrusions_.size() || !isShapeBody(extrusions_.at(index))) return false;
        QVector3D origin, direction;
        viewRay(position, origin, direction);
        double skipped = 0.0;
        if (!sectionRay(origin, direction, skipped)) return false;
        if (!cachedRayFace(index, origin, direction, hit)) return false;
        if (sectionRemoves(origin + float(hit.distance) * direction.normalized())) return false;
        hit.distance += skipped;
        return true;
    }
    // Spigoli visualizzati (indici in display.edges) dei bordi della faccia:
    // per ogni suo spigolo la polilinea piu' vicina al suo punto.
    QVector<int> faceDisplayEdges(int index, const FaceHit &hit) const {
        QVector<int> result;
        if (index < 0 || index >= extrusions_.size()) return result;
        const BodyDisplay &display = extrusions_.at(index).display;
        if (hit.face >= 0 && hit.face < display.faceEdges.size()) return display.faceEdges.at(hit.face);
        const QVector<QVector<QVector3D>> &edges = display.edges;
        for (const EdgePoint &point : hit.edges) {
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
    // Bordi della faccia selezionata (fuori dalla scelta degli spigoli).
    void drawSelectedFace() {
        if (edgePicking_ || sketchMode_ || selectedFace_.body < 0 || selectedFace_.body >= extrusions_.size()) return;
        const QVector<QVector<QVector3D>> &edges = extrusions_.at(selectedFace_.body).display.edges;
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glColor3f(float(kSelectionColor.redF()), float(kSelectionColor.greenF()), float(kSelectionColor.blueF()));
        glLineWidth(3.5f);
        for (int index : faceDisplayEdges(selectedFace_.body, selectedFace_.hit)) {
            glBegin(GL_LINE_STRIP);
            for (const QVector3D &p : edges.at(index)) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
        }
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
    }
    void drawPickedEdges() {
        drawSelectedFace();
        if (!edgePicking_) return;
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        auto draw = [&](int body, int index, const QColor &color, float width) {
            if (body < 0 || body >= extrusions_.size()) return;
            const QVector<QVector<QVector3D>> &edges = extrusions_.at(body).display.edges;
            if (index < 0 || index >= edges.size()) return;
            glColor3f(float(color.redF()), float(color.greenF()), float(color.blueF()));
            glLineWidth(width);
            glBegin(GL_LINE_STRIP);
            for (const QVector3D &p : edges.at(index)) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
        };
        if (edgePickBody_ >= 0)
            for (int index = 0; index < extrusions_.at(edgePickBody_).display.edges.size(); ++index) draw(edgePickBody_, index, QColor(120, 140, 160), 1.5f);
        for (int index : hoverFaceEdges_) draw(hoverEdgeBody_, index, kHoverColor, 3.0f);
        if (hoverEdgeBody_ != edgePickBody_ || !pickedEdges_.contains(hoverEdge_)) draw(hoverEdgeBody_, hoverEdge_, kHoverColor, 4.0f);
        for (int index : pickedEdges_) draw(edgePickBody_, index, kSelectionColor, 4.0f);
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
    }

    // Raggio di selezione con la sezione: l'origine dalla parte tolta va sul
    // piano (i punti prima non si vedono); `skipped` e' il tratto saltato.
    // Falso se il raggio non arriva alla parte che resta.
    bool sectionRay(QVector3D &origin, const QVector3D &direction, double &skipped) const {
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
    // Il punto sta nella parte tolta dalla sezione.
    bool sectionRemoves(const QVector3D &point) const {
        return section_.enabled && QVector3D::dotProduct(point - section_.point, section_.normal) > 1e-6f * qMax(1.0f, float(sceneDepth()));
    }
    // Quadrato sul piano di sezione che copre la scena.
    QVector<QVector3D> sectionQuad(float scale = 1.0f) const {
        const QVector3D n = section_.normal;
        const QVector3D helper = qAbs(n.x()) < 0.9f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
        const QVector3D u = QVector3D::crossProduct(n, helper).normalized(), v = QVector3D::crossProduct(n, u);
        updateSceneBounds();
        const QVector3D middle = 0.5f * (sceneMin_ + sceneMax_);
        const QVector3D center = middle - QVector3D::dotProduct(middle - section_.point, n) * n;
        const float half = scale * qMax(0.6f * (sceneMax_ - sceneMin_).length(), 1.0f);
        return {center - half * u - half * v, center + half * u - half * v, center + half * u + half * v, center - half * u + half * v};
    }
    void enableSectionClip() {
        if (!section_.enabled) return;
        const QVector3D n = section_.normal;
        const GLdouble equation[4] = {-double(n.x()), -double(n.y()), -double(n.z()), double(QVector3D::dotProduct(n, section_.point))};
        glClipPlane(GL_CLIP_PLANE0, equation);
        glEnable(GL_CLIP_PLANE0);
    }
    // Chiusura delle sezioni dei solidi: per ogni solido la parita' delle sue
    // facce tagliate nello stencil (dispari = dentro il solido), poi il
    // quadrato del piano dove lo stencil e' 1, pieno e tratteggiato.
    void drawSectionCaps(const QVector<int> &solids) {
        if (!section_.enabled || !section_.caps || solids.isEmpty()) return;
        const QVector<QVector3D> quad = sectionQuad(1.5f);
        static const GLubyte hatch[128] = {
            0x80, 0x80, 0x80, 0x80, 0x40, 0x40, 0x40, 0x40, 0x20, 0x20, 0x20, 0x20, 0x10, 0x10, 0x10, 0x10,
            0x08, 0x08, 0x08, 0x08, 0x04, 0x04, 0x04, 0x04, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01,
            0x80, 0x80, 0x80, 0x80, 0x40, 0x40, 0x40, 0x40, 0x20, 0x20, 0x20, 0x20, 0x10, 0x10, 0x10, 0x10,
            0x08, 0x08, 0x08, 0x08, 0x04, 0x04, 0x04, 0x04, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01,
            0x80, 0x80, 0x80, 0x80, 0x40, 0x40, 0x40, 0x40, 0x20, 0x20, 0x20, 0x20, 0x10, 0x10, 0x10, 0x10,
            0x08, 0x08, 0x08, 0x08, 0x04, 0x04, 0x04, 0x04, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01,
            0x80, 0x80, 0x80, 0x80, 0x40, 0x40, 0x40, 0x40, 0x20, 0x20, 0x20, 0x20, 0x10, 0x10, 0x10, 0x10,
            0x08, 0x08, 0x08, 0x08, 0x04, 0x04, 0x04, 0x04, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01};
        glDisable(GL_LIGHTING);
        glEnable(GL_STENCIL_TEST);
        for (int index : solids) {
            const BodyDisplay &display = extrusions_.at(index).display;
            glClear(GL_STENCIL_BUFFER_BIT);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_FALSE);
            glDisable(GL_DEPTH_TEST);
            glStencilFunc(GL_ALWAYS, 0, 1);
            glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
            enableSectionClip();
            drawDisplayFaces(display);
            glDisable(GL_CLIP_PLANE0);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            glStencilFunc(GL_EQUAL, 1, 1);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glColor3f(0.78f, 0.36f, 0.30f);
            glBegin(GL_QUADS);
            for (const QVector3D &p : quad) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_POLYGON_STIPPLE);
            glPolygonStipple(hatch);
            glColor3f(0.45f, 0.14f, 0.12f);
            glBegin(GL_QUADS);
            for (const QVector3D &p : quad) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
            glDisable(GL_POLYGON_STIPPLE);
        }
        // Resta GL_LEQUAL (come in initializeGL): con GL_LESS le linee degli schizzi
        // che stanno sugli spigoli dei corpi non si vedrebbero piu'.
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_STENCIL_TEST);
        glStencilFunc(GL_ALWAYS, 0, 0xff);
    }
    // Il piano di sezione: trasparente con il bordo.
    void drawSectionPlane() {
        if (!section_.enabled || !section_.showPlane) return;
        const QVector<QVector3D> quad = sectionQuad();
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glColor4f(0.95f, 0.45f, 0.35f, 0.10f);
        glBegin(GL_QUADS);
        for (const QVector3D &p : quad) glVertex3f(p.x(), p.y(), p.z());
        glEnd();
        const bool active = sectionHover_ || sectionDragging_;
        glColor4f(1.0f, 0.55f, 0.45f, active ? 1.0f : 0.75f);
        glLineWidth(active ? 2.5f : 1.5f);
        glBegin(GL_LINE_LOOP);
        for (const QVector3D &p : quad) glVertex3f(p.x(), p.y(), p.z());
        glEnd();
        glLineWidth(1.0f);
        glDepthMask(GL_TRUE);
    }

    void drawExtrusions() {
        glDisable(GL_CULL_FACE);
        glDisable(GL_LINE_STIPPLE);  // gli spigoli dei corpi sono sempre continui
        enableSectionClip();
        QVector<int> sectionSolids;
        const GLfloat noEmission[] = {0.0f, 0.0f, 0.0f, 1.0f};
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
            if (previewing && (index == preview_.index || (replaced && preview_.valid))) continue;
            const bool forced = replaced || blendBase || index == edgePickBody_;
            if ((!extrusion.visible && !forced) || (extrusion.display.vertices.isEmpty() && extrusion.display.edges.isEmpty())) continue;
            const SceneSelection self{SceneObjectKind::Extrusion, index, -1};
            const bool hovered = hover_ == self;
            if (displayMode_ != 0) {
                glEnable(GL_LIGHTING);
                glEnable(GL_COLOR_MATERIAL);
                QColor color = extrusion.solid ? QColor::fromRgbF(0.25f, 0.65f, 0.90f)
                                               : QColor::fromRgbF(0.20f, 0.80f, 0.95f);
                if (hovered) {
                    // L'oggetto sotto il puntatore si "accende".
                    color = QColor::fromRgbF(color.redF() * 0.75 + kHoverColor.redF() * 0.25,
                                             color.greenF() * 0.75 + kHoverColor.greenF() * 0.25,
                                             color.blueF() * 0.75 + kHoverColor.blueF() * 0.25);
                    const GLfloat emission[] = {float(kHoverColor.redF()) * 0.14f,
                                                float(kHoverColor.greenF()) * 0.14f,
                                                float(kHoverColor.blueF()) * 0.14f, 1.0f};
                    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, emission);
                }
                // In modalita' schizzo le facce sono trasparenti (opacita' regolabile)
                // e non scrivono la profondita': lo schizzo dietro i corpi resta leggibile.
                const bool seeThrough = sketchMode_ && sketchBodyOpacity_ < 0.999;
                glColor4f(float(color.redF()), float(color.greenF()), float(color.blueF()), seeThrough ? float(sketchBodyOpacity_) : 1.0f);
                if (seeThrough) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glDepthMask(GL_FALSE);
                }
                // Le facce vanno un poco indietro nella profondita': spigoli e
                // schizzi che stanno sulla faccia si vedono senza spostarli.
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1.0f, 2.0f);
                drawExtrusionFaces(extrusion);
                glDisable(GL_POLYGON_OFFSET_FILL);
                if (seeThrough) {
                    glDepthMask(GL_TRUE);
                    glDisable(GL_BLEND);
                }
                glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, noEmission);
            }
            if (section_.enabled && extrusion.solid && displayMode_ != 0 && !extrusion.display.vertices.isEmpty()) sectionSolids.append(index);
            if (isCurveBody(extrusion)) {
                // Curve (eliche): sempre, in arancio, piu' spesse sotto il puntatore.
                glDisable(GL_LIGHTING);
                if (hovered) glColor3f(float(kHoverColor.redF()), float(kHoverColor.greenF()), float(kHoverColor.blueF()));
                else glColor3f(1.0f, 0.69f, 0.29f);
                glLineWidth(hovered ? 3.0f : 2.2f);
                drawExtrusionEdges(extrusion);
                glLineWidth(1.0f);
            } else if (displayMode_ != 1) {
                glDisable(GL_LIGHTING);
                glColor3f(0.82f, 0.91f, 0.96f);
                glLineWidth(1.5f);
                drawExtrusionEdges(extrusion);
                glLineWidth(1.0f);
            }
        }
        const auto outline = [this](const SceneSelection &target, const QColor &color, float width) {
            if (target.kind != SceneObjectKind::Extrusion || target.index < 0
                || target.index >= extrusions_.size()) return;
            const ExtrusionObject &extrusion = extrusions_.at(target.index);
            if (!extrusion.visible || (extrusion.display.vertices.isEmpty() && extrusion.display.edges.isEmpty())) return;
            drawExtrusionOutline(extrusion, color, width);
        };
        if (previewing && preview_.valid) drawPreview();
        glDisable(GL_CLIP_PLANE0);
        drawSectionCaps(sectionSolids);
        enableSectionClip();
        if (!edgePicking_ && !previewing) {  // nessun contorno di selezione sulla base in scelta
            if (hover_ != selection_ && !selectedObjects_.contains(hover_)) outline(hover_, kHoverColor, 6.0f);
            outline(selection_, kSelectionColor, 7.0f);
            for (const SceneSelection &object : selectedObjects_)
                if (object != selection_) outline(object, kSelectionColor, 7.0f);
        }
        glDisable(GL_CLIP_PLANE0);
        drawSectionPlane();
    }

    // Anteprima di loft e raccordi: semitrasparente con isoparametriche U/V.
    // Le altre funzioni mantengono le facce ambra opache.
    void drawPreview() {
        const BodyDisplay &display = preview_.display;
        const bool loft = preview_.definition.operation < 0 && preview_.definition.feature == BodyFeature::Loft;
        const bool blend = preview_.definition.operation < 0 && preview_.definition.feature == BodyFeature::Blend;
        const bool transparent = loft || blend;
        // La superficie del raccordo e' interna al vecchio spigolo convesso:
        // in sovrapposizione alla base opaca la mostriamo come patch X-ray.
        if (blend) glDisable(GL_DEPTH_TEST);
        if (displayMode_ != 0) {
            glEnable(GL_LIGHTING);
            glEnable(GL_COLOR_MATERIAL);
            glColor4f(0.95f, 0.66f, 0.28f, transparent ? 0.42f : 1.0f);
            if (transparent) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 2.0f);
            drawDisplayFaces(display);
            glDisable(GL_POLYGON_OFFSET_FILL);
            if (transparent) {
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
        }
        glDisable(GL_LIGHTING);
        glColor3f(1.0f, 0.88f, 0.62f);
        glLineWidth(1.5f);
        displayCache_.edges(display);
        if (transparent && !display.constructionCurves.isEmpty()) {
            glEnable(GL_LINE_STIPPLE);
            glLineStipple(1, 0x3F3F);
            glColor3f(0.45f, 0.92f, 1.0f);
            glLineWidth(1.0f);
            for (const QVector<QVector3D> &curve : display.constructionCurves) {
                glBegin(GL_LINE_STRIP);
                for (const QVector3D &point : curve) glVertex3f(point.x(), point.y(), point.z());
                glEnd();
            }
            glDisable(GL_LINE_STIPPLE);
        }
        glLineWidth(1.0f);
        if (blend) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
        }
    }

    // Chiave di una richiesta d'anteprima: due richieste uguali non si rifanno.
    static QString previewKey(const ExtrusionObject &d, int index) {
        const auto n = [](double v) { return QString::number(v, 'g', 17); };
        QStringList parts{QString::number(index), QString::number(d.operation), QString::number(int(d.feature)), QString::number(d.sketchIndex),
                          n(d.distance), QString::number(d.revolveAxis), n(d.revolveAngle), QString::number(d.firstBody),
                          QString::number(d.secondBody), n(d.blendSize), QString::number(d.blendChamfer), QString::number(d.trimPlane),
                          n(d.trimKeep.x), n(d.trimKeep.y), n(d.trimKeep.z), QString::number(d.extendLinear), n(d.scaleFactor),
                          QString::number(d.scaleCenterMode), n(d.scaleCenter.x), n(d.scaleCenter.y), n(d.scaleCenter.z),
                          QString::number(d.chamferSpec.mode), n(d.chamferSpec.second), QString::number(d.chamferSpec.flip)};
        const PrimitiveParameters &p = d.primitive;
        parts << QString::number(int(p.kind)) << QString::number(p.plane);
        for (int k = 0; k < 3; ++k) parts << n(p.origin[k]) << n(p.size[k]);
        for (const EdgePoint &e : d.blendEdges) parts << n(e.x) << n(e.y) << n(e.z);
        const HelixParameters &h = d.helix;
        parts << QString::number(h.spiral) << QString::number(h.mode) << n(h.pitch) << n(h.turns) << n(h.height) << n(h.taper) << n(h.startAngle)
              << QString::number(h.leftHanded) << QString::number(h.reverse) << QString::number(h.source) << QString::number(h.curve)
              << n(h.reference.x) << n(h.reference.y) << n(h.reference.z) << QString::number(d.sweepPath) << QString::number(d.pathSketch)
              << QString::number(d.sweepMode) << QString::number(d.loftRuled) << QStringLiteral("|");
        for (int segment : d.pathSegments) parts << QStringLiteral("ps%1").arg(segment);
        for (int curve : d.pathCurves) parts << QStringLiteral("pc%1").arg(curve);
        for (int section : d.loftSketches) parts << QString::number(section);
        parts << QStringLiteral("G");
        for (int guide : d.loftGuides) parts << QString::number(guide);
        for (const SketchPathRef &guide : d.loftGuidePaths) {
            parts << QStringLiteral("gp%1").arg(guide.sketch);
            for (int segment : guide.segments) parts << QStringLiteral("s%1").arg(segment);
            for (int curve : guide.curves) parts << QStringLiteral("c%1").arg(curve);
        }
        parts << QString::number(d.loftStartContinuity) << QString::number(d.loftEndContinuity) << QString::number(d.loftGuideContinuity)
              << n(d.loftGuideInfluence)
              << n(d.loftStartInfluence) << n(d.loftEndInfluence);
        const auto ref = [&](const GeometryRef &r) {
            parts << QString::number(r.kind) << QString::number(r.index) << QString::number(r.element.kind) << QString::number(r.element.element)
                  << QString::number(r.element.point) << n(r.point.x) << n(r.point.y) << n(r.point.z);
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
              << QString::number(d.mergeProbe);
        ref(d.extentRef);
        for (int body : d.mergeBodies) parts << QString::number(body);
        parts << QStringLiteral("T");
        for (int body : d.booleanTools) parts << QString::number(body);
        parts << QStringLiteral("M") << n(d.move.translation[0]) << n(d.move.translation[1]) << n(d.move.translation[2]) << n(d.move.angle)
              << QString::number(d.move.copy);
        ref(d.move.axis);
        return parts.join(QLatin1Char(','));
    }

    // Dati di un'anteprima, copiati nel thread dell'interfaccia: il thread del
    // pool non tocca il documento (i body del kernel sono immutabili e
    // condivisi, gli schizzi sono copie).
    struct PreviewInputs {
        ExtrusionObject definition;
        int index = 0;  // posizione del corpo (gli operandi hanno indice minore)
        QVector<SketchObject> sketches;
        QVector<ExtrusionObject> bodies;  // solo gli operandi, copiati
        int quality = 1;
    };
    // La geometria dell'anteprima, come rebuildBody, e la sua tassellazione.
    static bool computePreview(const PreviewInputs &in, BodyDisplay &display, BodyDisplay &resultDisplay, ForgeCad::ForgeBody &geometry,
                               QString &error, QVector<int> *merged = nullptr) {
        ExtrusionObject body = in.definition;
        buildGeometry(body, in.index, in.sketches, in.bodies);
        if (merged && body.mergeProbe) *merged = body.mergeBodies;  // i corpi toccati dalla fusione automatica
        if (!hasGeometry(body)) {
            error = body.error;
            return false;
        }
        geometry = body.forgeBody;
        const bool blend = in.definition.operation < 0 && in.definition.feature == BodyFeature::Blend && body.forgeBody
            && in.definition.firstBody >= 0 && in.definition.firstBody < in.bodies.size()
            && in.bodies.at(in.definition.firstBody).forgeBody;
        if (blend) {
            tessellateGeometry(body, in.quality, resultDisplay);
            ForgeCad::forgeBlendPreviewDisplay(*in.bodies.at(in.definition.firstBody).forgeBody, *body.forgeBody, in.quality, display,
                                               in.quality <= 0 ? 3 : in.quality == 1 ? 5 : 7);
        } else {
            tessellateGeometry(body, in.quality, display);
        }
        if (in.definition.operation < 0 && in.definition.feature == BodyFeature::Loft && body.forgeBody)
            ForgeCad::forgeSurfaceConstructionCurves(*body.forgeBody, display, in.quality <= 0 ? 3 : in.quality == 1 ? 5 : 7);
        return true;
    }

    // Calcolo dell'anteprima in un thread del pool. Il risultato arriva nel
    // thread dell'interfaccia e vale solo se nel frattempo non e' arrivata
    // un'altra richiesta.
    void startPreviewJob() {
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
        const ExtrusionObject &d = in.definition;
        QString invalid;
        // Operandi: i body e le curve del kernel sono immutabili, si condividono.
        QVector<int> operands = bodyOperands(d);
        // Ripetizione della funzione: servono anche gli operandi della booleana.
        if (d.operation < 0 && d.feature == BodyFeature::Pattern && d.pattern.featureOnly && d.firstBody >= 0 && d.firstBody < extrusions_.size())
            operands += bodyOperands(extrusions_.at(d.firstBody));
        for (int body : operands) {
            if (body < 0 || body >= in.index || !hasGeometry(extrusions_.at(body))) {
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
        for (int sketch : sketchesOf(d))
            if (sketch < 0 || sketch >= sketches_.size()) invalid = QStringLiteral("nessuno schizzo");
        const quint64 generation = preview_.generation;
        if (!invalid.isEmpty()) {
            preview_.error = invalid;
            if (edgePicking_ && edgePickStatus_) edgePickStatus_(edgePickMessage());
            if (previewCallback_) previewCallback_(preview_.error);
            update();
            return;
        }
        if (!previewReceiver_) previewReceiver_ = new QObject(this);
        QObject *receiver = previewReceiver_;
        previewRunning_ = true;
        if (workCallback_) workCallback_(true, QStringLiteral("Calcolo dell'anteprima..."), true);
        QThreadPool::globalInstance()->start([this, receiver, generation, in] {
            BodyDisplay display;
            BodyDisplay resultDisplay;
            ForgeCad::ForgeBody geometry;
            QString error;
            bool ok = false;
            QVector<int> merged;
            const bool probe = in.definition.mergeProbe;
            try {
                ok = computePreview(in, display, resultDisplay, geometry, error, &merged);
            } catch (const std::exception &failure) {
                error = QString::fromUtf8(failure.what());
            } catch (...) {
                error = QStringLiteral("errore imprevisto");
            }
            if (!ok && error.isEmpty()) error = QStringLiteral("costruzione non riuscita");
            // Il ricevitore e' figlio del viewport: se il viewport non c'e' piu', la chiamata non avviene.
            QMetaObject::invokeMethod(receiver, [this, generation, ok, error, probe, merged,
                                                 geometry = std::move(geometry), display = std::move(display),
                                                 resultDisplay = std::move(resultDisplay)]() mutable {
                previewRunning_ = false;
                if (workCallback_) workCallback_(false, QStringLiteral("Calcolo dell'anteprima..."), true);
                if (generation == preview_.generation) {
                    if (probe) preview_.replaced = ok ? merged : QVector<int>();
                    preview_.valid = ok;
                    preview_.error = ok ? QString() : preparePreviewError(error);
                    preview_.geometry = std::move(geometry);
                    preview_.display = std::move(display);
                    preview_.resultDisplay = std::move(resultDisplay);
                    if (edgePicking_ && edgePickStatus_) edgePickStatus_(edgePickMessage());
                    if (previewCallback_) previewCallback_(preview_.error);
                    update();
                    previewRerun_ = false;
                } else if (!preview_.key.isEmpty()) {
                    previewRerun_ = false;
                    startPreviewJob();
                }
            }, Qt::QueuedConnection);
        });
    }

    QString preparePreviewError(QString error) {
        previewErrorSketch_ = -1;
        const QRegularExpression marker(QStringLiteral("\\s*\\[\\[loft-section=(\\d+)\\]\\]"));
        const QRegularExpressionMatch match = marker.match(error);
        if (match.hasMatch()) {
            const int section = match.captured(1).toInt();
            if (section >= 0 && section < preview_.definition.loftSketches.size())
                previewErrorSketch_ = preview_.definition.loftSketches.at(section);
            error.remove(marker);
        }
        return error.trimmed();
    }

    // Punti della geometria di un schizzo (estremi dei segmenti, campioni
    // delle curve) nello spazio del modello: solo per inquadrare la vista.
    QVector<QVector3D> sketchGeometryPoints(const SketchObject &sketch) const {
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

    // Geometria visibile della scena (schizzi, spigoli o triangoli dei corpi);
    // se non c'e' nulla, i piani di riferimento.
    QVector<QVector3D> sceneGeometryPoints(bool planesIfEmpty = true, bool datumSizes = true) const {
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
            if (!isDatumBody(body) || !body.visible || !body.datumValid) continue;
            points.append(QVector3D(float(body.datumFrame.origin[0]), float(body.datumFrame.origin[1]), float(body.datumFrame.origin[2])));
            if (datumSizes && body.datum.size > 0.0) points += datumCorners(body.datumFrame, body.datum.size);
        }
        if (points.isEmpty() && planesIfEmpty)
            for (int plane = 0; plane < 3; ++plane)
                points += planeCorners(plane, kDefaultPlaneHalf * float(referencePlaneScales_[plane]),
                                       kDefaultPlaneHalf * float(referencePlaneScales_[plane]));
        return points;
    }

    // Box della geometria visibile e mezzo lato comune dei piani di riferimento:
    // il piano che richiede la portata maggiore determina un quadrato applicato
    // a XY, XZ e YZ (piu' il 20%, arrotondato a 1, 2, 2.5 o 5 per 10^k).
    void updateSceneBounds() const {
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
    // Il piu' piccolo 1, 2, 2.5 o 5 per una potenza di 10 non minore di x (> 0).
    static float niceCeiling(float x) {
        const float power = std::pow(10.0f, std::floor(std::log10(x)));
        for (float step : {1.0f, 2.0f, 2.5f, 5.0f, 10.0f})
            if (step * power >= x * 0.9999f) return step * power;
        return 10.0f * power;
    }
    // Mezzo lato dei piani di riferimento (segue la dimensione della scena).
    float planeHalf() const {
        updateSceneBounds();
        return planeHalf_;
    }

    // Mezza profondita' del volume di vista: tutta la geometria (e i piani)
    // resta tra i piani di taglio anche nelle scene grandi.
    double sceneDepth() const {
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

    // Zoom che inquadra la sfera attorno al box della scena; i limiti sono
    // 4 volte questo (allontanandosi) e 1/5000 (avvicinandosi).
    void zoomLimits(float &minimum, float &maximum) const {
        updateSceneBounds();
        const float aspect = float(width()) / float(qMax(1, height()));
        const float radius = qMax(0.5f * (sceneMax_ - sceneMin_).length(), 1e-3f);
        const float fit = 2.2f * radius / qMin(1.0f, aspect);
        minimum = fit / 5000.0f;
        maximum = 4.0f * fit;
    }

    void setZoom(float zoom) {
        float minimum = 0.0f, maximum = 0.0f;
        zoomLimits(minimum, maximum);
        zoom_ = qBound(minimum, zoom, maximum);
        update();
    }

    // Inquadra i punti nella vista corrente (orientamento invariato): pan sul
    // centro del loro box in coordinate vista e zoom con un margine del 10%.
    void fitView(const QVector<QVector3D> &points) {
        if (points.isEmpty()) return;
        // Prima del primo disegno il widget non ha ancora la sua dimensione:
        // l'inquadratura si ripete al primo paintGL.
        if (!painted_) pendingFit_ = points;
        QMatrix4x4 rotation;
        rotation.rotate(roll_, 0.0f, 0.0f, 1.0f);
        rotation.rotate(pitch_, 1.0f, 0.0f, 0.0f);
        rotation.rotate(yaw_, 0.0f, 1.0f, 0.0f);
        rotation *= basisMatrix();
        QVector3D low = rotation.map(points.first()), high = low;
        for (const QVector3D &p : points) {
            const QVector3D v = rotation.map(p);
            low = QVector3D(qMin(low.x(), v.x()), qMin(low.y(), v.y()), 0.0f);
            high = QVector3D(qMax(high.x(), v.x()), qMax(high.y(), v.y()), 0.0f);
        }
        const float aspect = float(width()) / float(qMax(1, height()));
        const float halfWidth = 0.5f * (high.x() - low.x()), halfHeight = 0.5f * (high.y() - low.y());
        panX_ = -0.5f * (low.x() + high.x());
        panY_ = -0.5f * (low.y() + high.y());
        // Mezza altezza visibile = zoom_ / 2, mezza larghezza = aspect * zoom_ / 2.
        const float needed = 2.0f * 1.1f * qMax(halfHeight, halfWidth / aspect);
        setZoom(needed > 0.0f ? needed : zoom_);
    }

    // Framebuffer multisample della scena (nullptr: antialiasing spento o non disponibile).
    QOpenGLFramebufferObject *sceneBuffer(const QSize &size) {
        const int samples = antialiasing();
        if (samples <= 0 || maxSamples_ <= 0 || !QOpenGLFramebufferObject::hasOpenGLFramebufferBlit()) {
            msaaBuffer_.reset();
            resolveBuffer_.reset();
            return nullptr;
        }
        if (!msaaBuffer_ || msaaBuffer_->size() != size || bufferSamples_ != samples) {
            QOpenGLFramebufferObjectFormat format;
            format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
            format.setSamples(samples);
            msaaBuffer_ = std::make_unique<QOpenGLFramebufferObject>(size, format);
            resolveBuffer_ = std::make_unique<QOpenGLFramebufferObject>(size);
            bufferSamples_ = samples;
            if (!msaaBuffer_->isValid() || !resolveBuffer_->isValid()) {
                msaaBuffer_.reset();
                resolveBuffer_.reset();
                antialiasing_ = 0;
                return nullptr;
            }
        }
        return msaaBuffer_.get();
    }

    void initializeGlassShader() {
        glassShader_ = std::make_unique<QOpenGLShaderProgram>();
        static const char *vertex =
            "#version 120\n"
            "varying vec2 uv;\n"
            "void main() { gl_Position = gl_Vertex; uv = gl_MultiTexCoord0.xy; }\n";
        static const char *fragment =
            "#version 120\n"
            "uniform sampler2D sourceTexture;\n"
            "uniform vec2 direction;\n"
            "varying vec2 uv;\n"
            "void main() {\n"
            " vec4 c = texture2D(sourceTexture, uv) * 0.227027;\n"
            " c += texture2D(sourceTexture, uv + direction * 1.384615) * 0.316216;\n"
            " c += texture2D(sourceTexture, uv - direction * 1.384615) * 0.316216;\n"
            " c += texture2D(sourceTexture, uv + direction * 3.230769) * 0.070270;\n"
            " c += texture2D(sourceTexture, uv - direction * 3.230769) * 0.070270;\n"
            " gl_FragColor = c;\n"
            "}\n";
        gpuGlassReady_ = glassShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
            && glassShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
            && glassShader_->link() && QOpenGLFramebufferObject::hasOpenGLFramebufferBlit();
        if (!gpuGlassReady_) glassShader_.reset();
    }

    void ensureGlassBuffers(const QSize &fullSize) {
        // Il 75% elimina gran parte della pixelatura visibile sui bordi e sul
        // testo della scena, mantenendo il costo sotto quello del full-size.
        const QSize work(qMax(1, (fullSize.width() * 3 + 3) / 4),
                         qMax(1, (fullSize.height() * 3 + 3) / 4));
        if (!glassSource_ || glassSource_->size() != fullSize)
            glassSource_ = std::make_unique<QOpenGLFramebufferObject>(fullSize);
        if (!glassPing_ || glassPing_->size() != work) {
            glassPing_ = std::make_unique<QOpenGLFramebufferObject>(work);
            glassBlur_ = std::make_unique<QOpenGLFramebufferObject>(work);
        }
        if (!glassSource_->isValid() || !glassPing_->isValid() || !glassBlur_->isValid()) gpuGlassReady_ = false;
    }

    void glassBlurPass(QOpenGLFramebufferObject *target, GLuint texture, const QVector2D &direction) {
        target->bind();
        glViewport(0, 0, target->width(), target->height());
        glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_BLEND);
        glassShader_->bind();
        glassShader_->setUniformValue("sourceTexture", 0);
        glassShader_->setUniformValue("direction", direction);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(-1, -1);
        glTexCoord2f(1, 0); glVertex2f( 1, -1);
        glTexCoord2f(1, 1); glVertex2f( 1,  1);
        glTexCoord2f(0, 1); glVertex2f(-1,  1);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glassShader_->release();
        target->release();
    }

    void drawRoundedGlassRect(const QRect &rect, int radius, const QSize &pixels) {
        const float cx = rect.center().x(), cy = height() - rect.center().y();
        const float left = rect.left(), right = rect.right() + 1.0f;
        const float bottom = height() - (rect.bottom() + 1.0f), top = height() - rect.top();
        const float r = qMin(float(qMax(0, radius)), 0.5f * qMin(rect.width(), rect.height()));
        const auto vertex = [&](float x, float y) {
            glTexCoord2f(x / qMax(1, width()), y / qMax(1, height()));
            glVertex2f(x, y);
        };
        glBegin(GL_TRIANGLE_FAN);
        vertex(cx, cy);
        if (r <= 0.0f) {
            vertex(left, bottom); vertex(right, bottom); vertex(right, top); vertex(left, top); vertex(left, bottom);
        } else {
            const struct { float x, y, start; } corners[] = {
                {right - r, bottom + r, -90}, {right - r, top - r, 0},
                {left + r, top - r, 90}, {left + r, bottom + r, 180}};
            for (const auto &corner : corners)
                for (int step = 0; step <= 5; ++step) {
                    const float a = (corner.start + step * 18.0f) * float(M_PI) / 180.0f;
                    vertex(corner.x + r * std::cos(a), corner.y + r * std::sin(a));
                }
            vertex(right - r, bottom);
        }
        glEnd();
        Q_UNUSED(pixels);
    }

    void drawGpuGlassPanels(const QSize &pixels, QOpenGLFramebufferObject *resolvedScene) {
        if (!gpuGlassReady_ || glassPanels_.isEmpty()) return;
        ensureGlassBuffers(pixels);
        if (!gpuGlassReady_) return;
        if (resolvedScene) {
            QOpenGLFramebufferObject::blitFramebuffer(glassSource_.get(), resolvedScene);
        } else {
            QOpenGLFramebufferObject::blitFramebuffer(
                glassSource_.get(), QRect(QPoint(), pixels), nullptr, QRect(QPoint(), pixels), GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        int blur = 0;
        for (const GlassPanel &panel : glassPanels_) blur = qMax(blur, panel.blur);
        if (blur <= 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
            return;
        }
        // blur_ e' espresso in pixel logici. Conserva la stessa estensione
        // visiva al variare sia del DPR sia della risoluzione di lavoro.
        const float workRatio = float(glassPing_->width()) / qMax(1, pixels.width());
        const float scale = qMax(0.25f, blur * float(devicePixelRatioF()) * workRatio / 2.0f);
        glassBlurPass(glassPing_.get(), glassSource_->texture(),
                      QVector2D(scale / glassPing_->width(), 0));
        glassBlurPass(glassBlur_.get(), glassPing_->texture(),
                      QVector2D(0, scale / glassBlur_->height()));

        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, pixels.width(), pixels.height());
        glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_BLEND);
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, glassBlur_->texture());
        // L'ambiente texture della pipeline compatibility modula il campione
        // con il colore corrente, lasciato dai disegni della scena.
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0, width(), 0, height(), -1, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        for (const GlassPanel &panel : glassPanels_) drawRoundedGlassRect(panel.rect, panel.radius, pixels);
        glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
        glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D); glEnable(GL_DEPTH_TEST);
    }


    static const QColor &axisColor(int axis) {
        static const QColor colors[3] = {QColor(235, 70, 70), QColor(80, 215, 95), QColor(70, 135, 255)};
        return colors[qBound(0, axis, 2)];
    }

    // Assi cartesiani dall'origine: una linea per asse e una freccia conica
    // in punta (sotto gli oggetti, senza test di profondita').
    void drawAxes() {
        if (!axesVisible_) return;
        const float length = float(axisDisplayLength());
        const float head = 0.09f * length, radius = 0.032f * length;
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.0f);
        for (int axis = 0; axis < 3; ++axis) {
            QVector3D direction, side, up;
            direction[axis] = 1.0f;
            side[(axis + 1) % 3] = 1.0f;
            up[(axis + 2) % 3] = 1.0f;
            const QColor &color = axisColor(axis);
            glColor3f(float(color.redF()), float(color.greenF()), float(color.blueF()));
            const QVector3D tip = direction * length, base = direction * (length - head);
            glBegin(GL_LINES);
            glVertex3f(0.0f, 0.0f, 0.0f);
            glVertex3f(base.x(), base.y(), base.z());
            glEnd();
            glBegin(GL_TRIANGLE_FAN);
            glVertex3f(tip.x(), tip.y(), tip.z());
            for (int k = 0; k <= 24; ++k) {
                const float angle = float(k) * 2.0f * float(M_PI) / 24.0f;
                const QVector3D p = base + radius * (std::cos(angle) * side + std::sin(angle) * up);
                glVertex3f(p.x(), p.y(), p.z());
            }
            glEnd();
            glBegin(GL_TRIANGLE_FAN);
            glVertex3f(base.x(), base.y(), base.z());
            for (int k = 0; k <= 24; ++k) {
                const float angle = float(k) * 2.0f * float(M_PI) / 24.0f;
                const QVector3D p = base + radius * (std::cos(angle) * side + std::sin(angle) * up);
                glVertex3f(p.x(), p.y(), p.z());
            }
            glEnd();
        }
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
    }

    void drawGrid() {
        if (!gridVisible_) return;
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glColor3f(0.16f, 0.21f, 0.25f);
        // Griglia nel piano orizzontale dell'orientamento (perpendicolare all'asse in alto), poco sotto l'origine.
        const QVector3D a(float(orientation_.right[0]), float(orientation_.right[1]), float(orientation_.right[2]));
        const QVector3D b(float(orientation_.toward[0]), float(orientation_.toward[1]), float(orientation_.toward[2]));
        // Passo 1 con i piani di default, poi segue la loro dimensione (20 passi per lato).
        const float step = niceCeiling(qMax(0.25f * planeHalf(), 1e-4f)), extent = 10.0f * step;
        const QVector3D below = -0.05f * step * QVector3D(float(orientation_.up[0]), float(orientation_.up[1]), float(orientation_.up[2]));
        glBegin(GL_LINES);
        for (int i = -10; i <= 10; ++i) {
            const float f = float(i) * step;
            for (const QVector3D &p : {below + f * a - extent * b, below + f * a + extent * b,
                                       below - extent * a + f * b, below + extent * a + f * b})
                glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
        glEnable(GL_DEPTH_TEST);
    }

    int displayMode_ = 2;
    float yaw_ = -32.0f, pitch_ = 22.0f, zoom_ = 8.0f;
    float roll_ = 0.0f;  // rotazione attorno all'asse di vista (schizzi su facce inclinate)
    AxesOrientation orientation_;  // orientamento degli assi del documento (vedi basisMatrix)
    bool autoFit_ = false;         // l'ultima inquadratura e' "zoom tutto" e l'utente non ha mosso la vista
    float panX_ = 0.0f, panY_ = 0.0f;  // spostamento della vista (unita' della vista)
    int panKey_ = Qt::Key_Space;
    bool panKeyHeld_ = false, panning_ = false;
    QPoint lastMousePosition_;
    int lightingPreset_ = 0, constraintMode_ = 0, selectedPlane_ = 0, activePlane_ = 0, activeSketch_ = -1;
    double lineLength_ = 0.0;
    double lineAngle_ = -1.0;
    int polygonSides_ = 6;
    int tessellationQuality_ = 1;
    bool sketchMode_ = false, sketchCameraLocked_ = false, snapEnabled_ = true;
    bool wheelZoomEnabled_ = true;
    bool hasPendingPoint_ = false, referencePlanesVisible_ = true;
    DrawingTool drawingTool_ = DrawingTool::Select;
    SnapKind lastSnapKind_ = SnapKind::None;
    QString lastSnapNote_;   // etichetta particolare dell'aggancio (origine, quadrante, su curva)
    int lastSnapCurve_ = -1; // curva su cui si e' agganciato il punto (aggancio "su curva")
    const double snapSpacing_ = 0.25;
    QPointF pendingPoint_, cursorSketchPoint_, lastSnapPoint_;
    LineInference currentInference_;
    int startReference_ = -1;  // segmento su cui parte il segmento in costruzione
    QVector<QPointF> curveControlPoints_;
    QVector<TangentStart> tangentStarts_;  // arco tangente: le entita' da cui puo' partire
    bool draggingControlPoint_ = false;
    int draggingCurveIndex_ = -1;
    int draggingControlIndex_ = -1;
    EditablePointKind draggingPointKind_ = EditablePointKind::Control;
    SceneSelection selection_, hover_;
    SketchElementSelection sketchHover_;
    QVector<SketchElementSelection> sketchSelections_;
    QVector<SelectedPoint> selectedPoints_;
    QVector<int> selectedConstraints_;  // vincoli selezionati (indici in geometricConstraints dello schizzo attivo)
    int constraintHover_ = -1;
    int dimensionDrag_ = -1;  // quota che si sta spostando
    int dimensionPlacing_ = -1;  // quota dall'asse appena creata che segue il puntatore
    // Strumento Quota: orientamento scelto (0 automatico, 1 allineata, 2 orizzontale,
    // 3 verticale) e quota in anteprima per le entita' scelte, che segue il puntatore.
    int dimensionOrientation_ = 0;
    SketchConstraint dimensionPreview_;
    bool dimensionPreviewValid_ = false;
    ConstraintRef dimensionHover_;
    // Durante estrusione e rivoluzione dallo schizzo la vista si puo' ruotare
    // (camera dello schizzo salvata, ripresa alla fine).
    bool sketchViewUnlocked_ = false;
    struct SavedCamera {
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f, zoom = 8.0f, panX = 0.0f, panY = 0.0f;
        bool rotated = false;
    } sketchCamera_;
    // Vista dello schizzo ruotata dall'utente (tasto destro trascinato): i punti
    // del puntatore vanno sul piano lungo il raggio (screenToSketchPoint).
    bool sketchViewRotated_ = false;
    QPoint rightPressPosition_;
    bool rightDragged_ = false, contextPending_ = false;
    // Selezione a riquadro: nello schizzo trascinando dal vuoto con la Selezione
    // (le entita' dello schizzo), fuori con Maiusc+trascinamento (schizzi e corpi).
    // Da sinistra a destra valgono gli oggetti tutti dentro, da destra a sinistra
    // anche quelli toccati. selectedObjects_: gli oggetti scelti (piu' di uno).
    bool boxArmed_ = false, boxSelecting_ = false, boxAdditive_ = false;
    QPoint boxStart_, boxEnd_;
    QVector<SceneSelection> selectedObjects_;
    // Opacita' dei corpi in modalita' schizzo (1 = opachi): lo schizzo resta leggibile dietro di loro.
    double sketchBodyOpacity_ = 0.35;
    mutable ForgeCad::SketchAnalysis analysis_;
    mutable bool analysisDirty_ = true;
    mutable int analysisSketch_ = -1;
    bool constraintsVisible_ = true;
    std::function<void()> constraintPanelCallback_;
    QVector<SketchObject> sketches_;
    QVector<ExtrusionObject> extrusions_;
    QVector<ModelBody> modelBodies_;
    ForgeCad::History history_;
    DocumentState dragSnapshot_;
    bool dragRecorded_ = false;
    int edgePickBody_ = -1, hoverEdge_ = -1, hoverEdgeBody_ = -1;
    bool edgePicking_ = false;  // scelta degli spigoli in corso (edgePickBody_ < 0: corpo non ancora deciso)
    bool edgePickChamfer_ = false;
    bool edgePickExtend_ = false, edgePickLinear_ = false;
    bool edgePickHelix_ = false;  // scelta della base di un'elica (un clic su uno spigolo o una faccia)
    bool edgePickPreviewEnabled_ = true;  // falso finche' il pannello del raccordo non e' apparso
    // Scelta dei riferimenti dei piani di costruzione (beginReferencePick).
    bool refPicking_ = false;
    int refPickRoles_ = 0;
    int refPickOwner_ = -1;
    GeometryRef refHover_;
    bool refHoverValid_ = false;
    FaceHit refHoverFace_;  // faccia sotto il puntatore (per evidenziarne i bordi)
    int refHoverFaceBody_ = -1;
    QVector<GeometryRef> refMarks_;
    std::function<void(bool, GeometryRef)> refPickFinished_;
    bool interactionLocked_ = false;
    // Anteprima del piano di costruzione della finestra.
    SketchFrame datumPreviewFrame_;
    bool datumPreviewValid_ = false;
    double datumPreviewSize_ = 0.0;
    int datumPreviewIndex_ = -1;
    std::function<void(int, int, EdgePoint)> helixPickFinished_;
    ChamferSpec edgePickSpec_;  // smusso: modo delle distanze per l'anteprima durante la scelta  // scelta dei bordi di una superficie da estendere
    std::function<void(int, QVector<EdgePoint>)> extendPickFinished_;
    QVector<int> pickedEdges_, hoverFaceEdges_;
    int edgePickEdit_ = -1;         // raccordo di cui si modificano gli spigoli (-1: raccordo nuovo)
    double edgePickSize_ = 0.5;     // misura dell'anteprima durante la scelta
    std::function<void(int, QVector<EdgePoint>, double, bool)> edgeEditFinished_;
    std::function<void(int, QVector<EdgePoint>)> edgePickChanged_; // pannello aperto: selezione aggiornata nella vista
    std::function<void()> edgePickPanelAccept_, edgePickPanelCancel_;
    // Anteprima di una funzione (vedi requestPreview).
    struct Preview {
        QString key;  // vuota: nessuna anteprima
        ExtrusionObject definition;
        int index = -1;           // corpo che l'anteprima sostituisce (modifica), -1 nuovo
        QVector<int> replaced;    // operandi (booleane) o base (raccordi): al loro posto l'anteprima
        bool valid = false;
        QString error;
        ForgeCad::ForgeBody geometry;
        BodyDisplay display;        // cio' che si disegna (per un raccordo, solo la patch)
        BodyDisplay resultDisplay;  // tassellazione completa promossa con OK
        quint64 generation = 0;
    };
    Preview preview_;
    int previewErrorSketch_ = -1;  // sezione della loft evidenziata quando una guida non la incontra
    QTimer *previewTimer_ = nullptr;
    QObject *previewReceiver_ = nullptr;
    bool previewRunning_ = false, previewRerun_ = false;
    std::function<void(const QString &)> previewCallback_;
    // Faccia selezionata con il clic su un corpo (schizzo su faccia, bordi da raccordare).
    struct SelectedFace {
        int body = -1;
        FaceHit hit;
    };
    SelectedFace selectedFace_;
    std::function<void(const QString &)> edgePickStatus_;
    std::function<void(int)> editBodyCallback_;
    std::function<void(int, QVector<EdgePoint>, bool)> edgePickFinished_;
    bool planeVisible_[3] = {true, true, true};
    BackgroundSettings background_;
    std::function<void(SceneSelection)> selectionCallback_;
    std::function<void(int)> sketchPickCallback_;
    std::function<void(int, int, int)> sketchEntityPickCallback_;
    std::function<void()> documentChangedCallback_;
    std::function<void(int)> planeContextCallback_;
    std::function<void(bool)> sketchModeCallback_;
    std::function<void(DrawingTool)> toolChangedCallback_;
    std::function<void(const QString &)> rendererCallback_;
    std::function<void(bool, const QString &, bool)> workCallback_;
    // Strumenti di modifica dello schizzo: misure di raccordo e smusso, primo
    // segmento scelto (se non si e' cliccato uno spigolo) e tratto che il taglio toglierebbe.
    double sketchFilletRadius_ = 1.0, sketchChamferDistance_ = 1.0;
    int blendFirst_ = -1;
    QPointF blendFirstPick_;
    QVector<QPointF> trimPreview_;
    QVector<QVector<QPointF>> sketchPatternPreview_;  // copie della ripetizione in anteprima
    std::function<void(const QString &)> statusCallback_;
    // Antialiasing: campioni MSAA (0 = spento). La scena OpenGL si disegna in
    // un framebuffer multisample, risolto in uno normale e copiato nel widget.
    int antialiasing_ = 4, maxSamples_ = 0, bufferSamples_ = 0;
    std::unique_ptr<QOpenGLFramebufferObject> msaaBuffer_, resolveBuffer_;
    struct GlassPanel { QRect rect; int blur = 0; int radius = 0; };
    QHash<quintptr, GlassPanel> glassPanels_;
    std::unique_ptr<QOpenGLShaderProgram> glassShader_;
    std::unique_ptr<QOpenGLFramebufferObject> glassSource_, glassPing_, glassBlur_;
    bool gpuGlassReady_ = false;
    bool gridVisible_ = true;
    double referencePlaneScales_[3] = {1.0, 1.0, 1.0};
    mutable QSizeF referencePlaneAuto_[3] = {
        QSizeF(kDefaultPlaneHalf, kDefaultPlaneHalf),
        QSizeF(kDefaultPlaneHalf, kDefaultPlaneHalf),
        QSizeF(kDefaultPlaneHalf, kDefaultPlaneHalf)};
    bool planeResizing_ = false;
    SceneSelection planeResizeTarget_;
    QPoint planeResizeStart_;
    QPointF planeResizeDirection_;
    double planeResizeHalf_ = 0.0, planeResizeOriginal_ = 0.0;
    DocumentState planeResizeSnapshot_;
    double axisLength_ = kDefaultAxisLength;
    ForgeCad::DisplayCache displayCache_;
    mutable QVector<RaySelectionCache> raySelectionCache_;
    mutable QVector<ProjectedEdges> projectedEdges_;
    mutable std::array<double, 18> projectionKey_{};
    mutable QMatrix4x4 projectionMatrix_;
    bool axesVisible_ = true, axesOnTop_ = true;  // assi mostrati; sopra solidi, superfici e schizzi
    bool originSnap_ = true;                      // lo schizzo si aggancia all'origine del piano
    // Box della geometria visibile (coordinate del modello), per zoom e profondita' della vista.
    mutable QVector3D sceneMin_, sceneMax_;
    struct SectionState {
        bool enabled = false;
        QVector3D point, normal{0.0f, 0.0f, 1.0f};
        bool caps = true, showPlane = true;
    };
    SectionState section_;
    SketchReferenceHit convertHover_;
    mutable std::map<const void *, std::pair<std::weak_ptr<const ForgeCad::Kernel::Body>, QPair<int, int>>> componentCache_;
    bool referencesConstruction_ = true;
    std::function<void()> sectionRefresh_;
    std::function<void(double)> sectionDragged_;
    bool sectionDragging_ = false, sectionHover_ = false;
    mutable bool sceneBoundsDirty_ = true;
    static constexpr float kDefaultPlaneHalf = 4.0f;  // mezzo lato dei piani senza geometria
    mutable float planeHalf_ = kDefaultPlaneHalf;
    bool painted_ = false;
    quint64 renderedFrameSerial_ = 0;
    quint64 panelBackdropSerial_ = std::numeric_limits<quint64>::max();
    QImage panelBackdropCache_;
    // Trascinamento con lo strumento Selezione: un punto (estremo di segmento
    // con i punti coincidenti) o un segmento intero.
    bool pointDragActive_ = false, bodyDragMoved_ = false;
    QPointF pointDragPosition_, bodyDragLast_;
    int bodyDragSegment_ = -1;
    QVector<QVector3D> pendingFit_;
};

// Tipi delle voci dell'albero modello (Qt::UserRole); Qt::UserRole + 1 e' l'indice.
enum TreeItemType { kTreeInfo = -1, kTreeOrigin = 0, kTreePlane = 1, kTreeSketch = 3, kTreeExtrusion = 4, kTreeBody = 5 };

class StoryboardTree final : public QTreeWidget {
public:
    explicit StoryboardTree(QWidget *parent = nullptr) : QTreeWidget(parent) {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
    }
    void setMoveFeatureCallback(std::function<QString(int, int)> callback) { moveFeature_ = std::move(callback); }

protected:
    void dropEvent(QDropEvent *event) override {
        QTreeWidgetItem *source = currentItem();
        QTreeWidgetItem *target = itemAt(event->position().toPoint());
        if (!source || !target || source == target || source->data(0, Qt::UserRole).toInt() != kTreeExtrusion
            || target->data(0, Qt::UserRole).toInt() != kTreeExtrusion || source->parent() != target->parent()
            || !source->parent() || source->parent()->data(0, Qt::UserRole).toInt() != kTreeBody || !moveFeature_) {
            event->ignore();
            return;
        }
        const QString error = moveFeature_(source->data(0, Qt::UserRole + 1).toInt(), target->data(0, Qt::UserRole + 1).toInt());
        event->ignore();  // l'albero viene ricostruito dal documento, mai spostato solo graficamente
        if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Riordina storyboard"), error);
    }

private:
    std::function<QString(int, int)> moveFeature_;
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
        message_ = new QLabel(QStringLiteral("Seleziona un documento ForgeCAD"), viewport_);
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
class FunctionDialogPanel : public QDialog {
public:
    explicit FunctionDialogPanel(QWidget *parent = nullptr)
        : QDialog(overlayParent(parent)), panelColor_(palette().color(QPalette::Window)), captureTimer_(this) {
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
        captureTimer_.setSingleShot(true);
        connect(&captureTimer_, &QTimer::timeout, this, [this] { captureSceneBackdrop(); });
        updatePalette();
    }
    ~FunctionDialogPanel() override {
        if (viewport_) viewport_->removeGlassPanel(reinterpret_cast<quintptr>(this));
    }
    QFormLayout *createScrollableForm() {
        if (form_) return form_;
        auto *root = new QVBoxLayout(this);
        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("functionDialogScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        auto *contents = new QWidget(scroll);
        contents->setObjectName(QStringLiteral("functionDialogContents"));
        contents->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        form_ = new QFormLayout(contents);
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
    void showEvent(QShowEvent *event) override {
        QDialog::showEvent(event);
        locateViewport();
        initializePanelSize();
        if (embedded_ && !placed_) placeAtLeft();
        raise();
        syncGpuGlass();
        scheduleBackdropCapture(0);
        QTimer::singleShot(0, this, [this] { focusFirstEditor(); });
    }
    void hideEvent(QHideEvent *event) override {
        if (viewport_) viewport_->removeGlassPanel(reinterpret_cast<quintptr>(this));
        QDialog::hideEvent(event);
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
        if (embedded_ && watched == parentWidget() && event->type() == QEvent::Resize) keepInsideViewport();
        return QDialog::eventFilter(watched, event);
    }
private:
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
        return (layout() ? layout()->sizeHint() : sizeHint()).expandedTo(minimumSize());
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
    QPoint dragOffset_;
    QPoint resizeStartGlobal_;
    QSize resizeStartSize_;
    bool embedded_ = false, dragging_ = false, resizing_ = false, placed_ = false, panelSizeInitialized_ = false;
};

class FeatureOperationDiagram final : public QWidget {
public:
    enum Kind { Extrusion, Revolution };
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
static bool runUntilApplied(QDialog &dialog, QFormLayout *form, QDialogButtonBox *buttons, const std::function<QString()> &apply) {
    auto *errorLabel = new QLabel(&dialog);
    errorLabel->setWordWrap(true);
    errorLabel->setStyleSheet(QStringLiteral("color: #ff7b72; font-weight: 600;"));
    errorLabel->setMaximumWidth(460);
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
    errorLabel->setMaximumWidth(460);
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
        label->setMaximumWidth(460);
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

// Finestra della misura di un raccordo o smusso con l'anteprima dal vivo
// (CadViewport::requestBlendPreview a ogni cambio). `edgesLabel` descrive gli
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
            secondBox->setRange(0.01, 179.99);
            secondBox->setValue(angle);
        } else {
            secondLabel->setText(QStringLiteral("Seconda distanza (altra faccia):"));
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
    auto *edgesLabel = new QLabel(&dialog);
    form->addRow(QStringLiteral("Spigoli:"), edgesLabel);
    auto *previewLabel = new QLabel(pickInView ? QStringLiteral("seleziona almeno uno spigolo nella vista")
                                               : QStringLiteral("Anteprima in calcolo..."), &dialog);
    previewLabel->setWordWrap(true);
    previewLabel->setMaximumWidth(460);
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
        edgesLabel->setText(selectedEdges.isEmpty()
                                ? QStringLiteral("nessuno — clicca nella vista")
                                : QStringLiteral("%1 selezionati — clicca per aggiungere o togliere").arg(selectedEdges.size()));
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
    QObject::connect(sizeBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(secondBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
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

// Finestra della scala: corpo, fattore uniforme e centro (origine, baricentro
// del solido o un punto), con l'anteprima. `definition` porta i valori
// iniziali; `replaced` e' il corpo modificato (-1 nuovo).
static bool scaleDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &definition,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    const int limit = replaced >= 0 ? replaced : int(bodies.size());
    QVector<int> candidates;
    for (int index = 0; index < limit; ++index)
        if (bodies.at(index).forgeBody) candidates.append(index);
    if (candidates.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Nella scena non ci sono corpi da scalare."));
        return false;
    }
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int index : candidates) bodyBox->addItem(bodies.at(index).visible ? bodies.at(index).name : bodies.at(index).name + QStringLiteral(" (nascosto)"));
    bodyBox->setCurrentIndex(qMax(0, int(candidates.indexOf(definition.firstBody))));
    bodyBox->setEnabled(replaced < 0);
    auto *factorBox = new ForgeCad::ExpressionSpinBox(&dialog);
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
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
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
        scope.request(current());
    };
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(centerBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(factorBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    for (QDoubleSpinBox *box : coordinates) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refresh();
    factorBox->selectAll();
    return runUntilApplied(dialog, form, buttons, [&] { return apply(current()); });
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
                              .arg(c + 1).arg(r, 0, 'g', 6).arg(curve.construction ? QStringLiteral(", costruzione") : QString()));
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
    auto *heightBox = spin(initial.helix.height, 1e-6, 1e6, 6);
    auto *taperBox = spin(initial.helix.taper, -89.0, 89.0, 4, QStringLiteral("°"));
    taperBox->setToolTip(QStringLiteral("Elica conica: positivo = il raggio cresce lungo l'asse. Sulla faccia di un cono vale quella della faccia."));
    auto *startBox = spin(initial.helix.startAngle, -360.0, 360.0, 4, QStringLiteral("°"));
    startBox->setToolTip(QStringLiteral("Dal punto del cerchio della base (schizzo) o dall'asse X del modello proiettato (spigolo, faccia)"));
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

// Sweep: profilo (uno schizzo), percorso (un altro schizzo o una curva, come
// un'elica) e orientamento del profilo lungo il percorso, con l'anteprima.
static bool sweepDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<SketchObject> &sketches = viewport->sketches();
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    const int limit = replaced >= 0 ? replaced : int(bodies.size());
    QVector<int> curves;
    for (int index = 0; index < limit; ++index)
        if (bodies.at(index).feature == BodyFeature::Helix && bodies.at(index).operation < 0 && bodies.at(index).curve) curves.append(index);
    if (sketches.isEmpty() || (sketches.size() < 2 && curves.isEmpty())) {
        QMessageBox::information(parent, title, QStringLiteral("Servono uno schizzo con il profilo e un percorso: un altro schizzo o una curva (elica)."));
        return false;
    }
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *profileBox = new QComboBox(&dialog), *pathTypeBox = new QComboBox(&dialog), *pathSketchBox = new QComboBox(&dialog),
         *pathChainBox = new QComboBox(&dialog), *curveBox = new QComboBox(&dialog);
    for (const SketchObject &sketch : sketches) {
        profileBox->addItem(sketch.name);
        pathSketchBox->addItem(sketch.name);
    }
    for (int index : curves) curveBox->addItem(bodies.at(index).name);
    pathTypeBox->addItems({QStringLiteral("Schizzo (catena o contorno chiuso di entita' tangenti)"), QStringLiteral("Curva (elica o spirale)")});
    profileBox->setCurrentIndex(qBound(0, initial.sketchIndex, int(sketches.size()) - 1));
    int pathSketch = initial.pathSketch;
    if (pathSketch < 0 || pathSketch >= sketches.size()) pathSketch = profileBox->currentIndex() == 0 && sketches.size() > 1 ? 1 : 0;
    pathSketchBox->setCurrentIndex(pathSketch);
    pathTypeBox->setCurrentIndex(initial.sweepPath == 1 && !curves.isEmpty() ? 1 : (initial.sweepPath == 0 && sketches.size() > 1 ? 0 : (curves.isEmpty() ? 0 : 1)));
    curveBox->setCurrentIndex(qMax(0, int(curves.indexOf(initial.firstBody))));
    auto *modeBox = new QComboBox(&dialog);
    modeBox->addItems({QStringLiteral("Torsione minima (segue il percorso senza ruotare attorno)"),
                       QStringLiteral("Frenet (segue la curvatura: molle, filetti)"), QStringLiteral("Orientamento costante (il profilo trasla)")});
    modeBox->setCurrentIndex(qBound(0, initial.sweepMode, 2));
    auto *operationBox = new QComboBox(&dialog);
    operationBox->addItems({QStringLiteral("Corpo nuovo"), QStringLiteral("Unisci ai solidi"), QStringLiteral("Sottrai dai solidi")});
    operationBox->setCurrentIndex(qBound(0, initial.mergeOperation, 2));
    auto *autoBox = new QCheckBox(QStringLiteral("Automatico: i solidi che hanno punti in comune con la sweep"), &dialog);
    autoBox->setChecked(initial.mergeAuto);
    auto *bodyList = new QListWidget(&dialog);
    bodyList->setMinimumHeight(110);
    for (int index = 0; index < bodies.size() && (replaced < 0 || index < replaced); ++index) {
        const ExtrusionObject &body = bodies.at(index);
        if (!body.forgeBody || !body.solid || (body.operation < 0 && (body.feature == BodyFeature::DatumPlane || body.feature == BodyFeature::Helix)))
            continue;
        const bool used = initial.mergeBodies.contains(index);
        auto *item = new QListWidgetItem(body.visible || used ? body.name : body.name + QStringLiteral(" (nascosto)"), bodyList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(used ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, index);
    }
    auto *help = new QLabel(QStringLiteral("Il profilo resta dove e' disegnato e si muove con il percorso, che parte dal punto piu' vicino al profilo "
                                           "(di solito lo si disegna sul piano normale al percorso, all'inizio). I tratti del percorso devono essere tangenti."),
                            &dialog);
    help->setWordWrap(true);
    help->setMaximumWidth(460);
    help->setStyleSheet(QStringLiteral("color: #8aa0b4;"));
    auto *pickPath = new QPushButton(QStringLiteral("Seleziona percorso nella vista"), &dialog);
    auto *pickStatus = new QLabel(QStringLiteral("Puoi scegliere una singola catena cliccando una sua entità nella scena."), &dialog);
    pickStatus->setWordWrap(true); pickStatus->setStyleSheet(QStringLiteral("color: #8aa0b4;"));
    form->addRow(QStringLiteral("Profilo:"), profileBox);
    form->addRow(QStringLiteral("Percorso:"), pathTypeBox);
    form->addRow(QStringLiteral("Schizzo del percorso:"), pathSketchBox);
    form->addRow(QStringLiteral("Catena nello schizzo:"), pathChainBox);
    form->addRow(QString(), pickPath);
    form->addRow(QString(), pickStatus);
    form->addRow(QStringLiteral("Curva del percorso:"), curveBox);
    form->addRow(QStringLiteral("Orientamento:"), modeBox);
    form->addRow(QStringLiteral("Risultato:"), operationBox);
    form->addRow(QString(), autoBox);
    form->addRow(QStringLiteral("Solidi:"), bodyList);
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
        d.sketchIndex = profileBox->currentIndex();
        d.plane = sketches.value(d.sketchIndex).plane;
        d.sweepPath = pathTypeBox->currentIndex();
        d.pathSketch = pathSketchBox->currentIndex();
        d.pathSegments.clear(); d.pathCurves.clear();
        if (d.sweepPath == 0 && pathChainBox->currentIndex() >= 0 && pathChainBox->currentIndex() < pathComponents.size()) {
            d.pathSegments = pathComponents.at(pathChainBox->currentIndex()).segments;
            d.pathCurves = pathComponents.at(pathChainBox->currentIndex()).curves;
        }
        d.firstBody = d.sweepPath == 1 && !curves.isEmpty() ? curves.at(qMax(0, curveBox->currentIndex())) : -1;
        d.sweepMode = modeBox->currentIndex();
        d.mergeOperation = operationBox->currentIndex();
        d.mergeAuto = autoBox->isChecked();
        d.mergeBodies.clear();
        for (int row = 0; row < bodyList->count(); ++row)
            if (bodyList->item(row)->checkState() == Qt::Checked)
                d.mergeBodies.append(bodyList->item(row)->data(Qt::UserRole).toInt());
        return d;
    };
    const auto refresh = [&] {
        pathSketchBox->setEnabled(pathTypeBox->currentIndex() == 0);
        pathChainBox->setEnabled(pathTypeBox->currentIndex() == 0);
        pickPath->setEnabled(pathTypeBox->currentIndex() == 0);
        curveBox->setEnabled(pathTypeBox->currentIndex() == 1 && !curves.isEmpty());
        const bool merge = operationBox->currentIndex() != 0;
        form->setRowVisible(autoBox, merge);
        form->setRowVisible(bodyList, merge);
        bodyList->setEnabled(!autoBox->isChecked());
        scope.request(viewport->withMergeCandidates(current(), replaced >= 0 ? replaced : int(bodies.size())));
    };
    QObject::connect(pathSketchBox, &QComboBox::currentIndexChanged, &dialog, [&](int) { refillPaths(); refresh(); });
    for (QComboBox *box : {profileBox, pathTypeBox, pathChainBox, curveBox, modeBox, operationBox})
        QObject::connect(box, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(autoBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(bodyList, &QListWidget::itemChanged, &dialog, refresh);
    bool pathPicking = false;
    QObject::connect(pickPath, &QPushButton::clicked, &dialog, [&] {
        pathPicking = true;
        pickStatus->setText(QStringLiteral("Scelta attiva: clicca un segmento o una curva del percorso nella vista."));
    });
    viewport->setSketchEntityPickCallback([&](int sketchIndex, int kind, int entity) {
        if (!pathPicking || pathTypeBox->currentIndex() != 0) return;
        pathSketchBox->setCurrentIndex(sketchIndex);
        for (int k = 0; k < pathComponents.size(); ++k) {
            const SketchPathRef &path = pathComponents.at(k);
            if ((kind == 0 ? path.segments : path.curves).contains(entity)) {
                pathChainBox->setCurrentIndex(k);
                pickStatus->setText(QStringLiteral("Selezionato %1 / percorso %2.").arg(sketches.at(sketchIndex).name).arg(k + 1));
                pathPicking = false;
                break;
            }
        }
    });
    QObject::connect(&dialog, &QDialog::finished, viewport, [viewport] { viewport->setSketchEntityPickCallback({}); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refresh();
    const auto validate = [&] {
        const ExtrusionObject d = current();
        if (d.sweepPath == 1 && d.firstBody < 0) return QStringLiteral("Nella scena non ci sono curve (eliche) da usare come percorso.");
        if (d.sweepPath == 0 && d.pathSegments.isEmpty() && d.pathCurves.isEmpty()) return QStringLiteral("Seleziona una catena valida nello schizzo del percorso.");
        if (d.sweepPath == 0 && d.pathSketch == d.sketchIndex) return QStringLiteral("Profilo e percorso devono stare in schizzi diversi.");
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
static bool loftDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<SketchObject> &sketches = viewport->sketches();
    if (sketches.size() < 2) {
        QMessageBox::information(parent, title, QStringLiteral("Servono almeno due schizzi, uno per sezione."));
        return false;
    }
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
    QVector<int> order = initial.loftSketches;
    for (int index = 0; index < sketches.size(); ++index)
        if (!order.contains(index)) order.append(index);
    for (int index : order) {
        if (index < 0 || index >= sketches.size()) continue;
        auto *item = new QListWidgetItem(sketches.at(index).name, list);
        item->setData(Qt::UserRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(initial.loftSketches.contains(index) ? Qt::Checked : Qt::Unchecked);
    }
    auto *moveRow = new QWidget(&dialog);
    auto *moveLayout = new QHBoxLayout(moveRow);
    moveLayout->setContentsMargins(0, 0, 0, 0);
    auto *up = new QPushButton(QStringLiteral("↑ Su"), moveRow), *down = new QPushButton(QStringLiteral("↓ Giu'"), moveRow);
    auto *pickSections = new QPushButton(QStringLiteral("Scegli sezioni nella vista"), moveRow);
    moveLayout->addWidget(up);
    moveLayout->addWidget(down);
    moveLayout->addWidget(pickSections);
    moveLayout->addStretch(1);
    auto *ruledBox = new QCheckBox(QStringLiteral("Rigato (superfici rigate tra sezioni consecutive)"), &dialog);
    ruledBox->setChecked(initial.loftRuled);
    auto *guides = new QListWidget(&dialog);
    guides->setMinimumHeight(105);
    for (int index = 0; index < sketches.size(); ++index) {
        auto *item = new QListWidgetItem(sketches.at(index).name, guides);
        item->setData(Qt::UserRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(initial.loftGuides.contains(index) ? Qt::Checked : Qt::Unchecked);
    }
    QVector<SketchPathRef> partialSelections = initial.loftGuidePaths;
    auto *partialGuides = new QListWidget(&dialog);
    partialGuides->setMinimumHeight(85);
    auto *removePartial = new QPushButton(QStringLiteral("Rimuovi selezione parziale"), &dialog);
    const auto refillPartial = [&] {
        const int current = partialGuides->currentRow();
        partialGuides->clear();
        QVector<int> counters(sketches.size(), 0);
        for (const SketchPathRef &path : partialSelections) {
            const int number = path.sketch >= 0 && path.sketch < counters.size() ? ++counters[path.sketch] : 1;
            partialGuides->addItem(QStringLiteral("%1 — selezione %2 (%3 entità)")
                .arg(sketches.value(path.sketch).name).arg(number).arg(path.segments.size() + path.curves.size()));
        }
        if (partialGuides->count()) partialGuides->setCurrentRow(qBound(0, current, partialGuides->count() - 1));
        removePartial->setEnabled(partialGuides->count() > 0);
    };
    refillPartial();
    auto *pickGuides = new QPushButton(QStringLiteral("Scegli guide nella vista"), &dialog);
    auto *pickStatus = new QLabel(QStringLiteral("Premi uno dei pulsanti e clicca gli schizzi direttamente nella scena; un secondo clic li toglie."), &dialog);
    pickStatus->setWordWrap(true);
    auto *startContinuity = new QComboBox(&dialog), *endContinuity = new QComboBox(&dialog);
    const QStringList continuity{QStringLiteral("G0 — posizione"), QStringLiteral("G1 — normale alla sezione"), QStringLiteral("G2 — curvatura continua")};
    startContinuity->addItems(continuity);
    endContinuity->addItems(continuity);
    startContinuity->setCurrentIndex(qBound(0, initial.loftStartContinuity, 2));
    endContinuity->setCurrentIndex(qBound(0, initial.loftEndContinuity, 2));
    const auto influence = [&](double value) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setRange(0.0, 100.0);
        spin->setSuffix(QStringLiteral(" %"));
        spin->setDecimals(0);
        spin->setValue(100.0 * value);
        return spin;
    };
    QDoubleSpinBox *guideInfluence = influence(initial.loftGuideInfluence);
    auto *guideContinuity = new QComboBox(&dialog);
    guideContinuity->addItems({QStringLiteral("G0 — solo attraversamento"), QStringLiteral("G1 — segue la tangente (consigliato)"),
                               QStringLiteral("G2 — segue tangente e curvatura")});
    guideContinuity->setCurrentIndex(qBound(0, initial.loftGuideContinuity, 2));
    QDoubleSpinBox *startInfluence = influence(initial.loftStartInfluence);
    QDoubleSpinBox *endInfluence = influence(initial.loftEndInfluence);
    auto *help = new QLabel(QStringLiteral("Le sezioni vanno ordinate. Ogni guida deve essere una catena aperta e attraversare una sola volta ogni sezione; "
                                           "non può essere usata anche come sezione. Per le guide, G1 segue la tangente senza trasferire la curvatura che può creare flessi; G2 la trasferisce."),
                            &dialog);
    help->setWordWrap(true);
    help->setMaximumWidth(570);
    form->addRow(new LoftSelectionDiagram(&dialog));
    form->addRow(QStringLiteral("Sezioni:"), list);
    form->addRow(QString(), moveRow);
    form->addRow(QStringLiteral("Curve guida:"), guides);
    form->addRow(QString(), pickGuides);
    form->addRow(QStringLiteral("Selezioni parziali:"), partialGuides);
    form->addRow(QString(), removePartial);
    form->addRow(QString(), pickStatus);
    form->addRow(QStringLiteral("Tangenza guide:"), guideContinuity);
    form->addRow(QStringLiteral("Influenza guide:"), guideInfluence);
    form->addRow(QStringLiteral("Inizio:"), startContinuity);
    form->addRow(QStringLiteral("Influenza iniziale:"), startInfluence);
    form->addRow(QStringLiteral("Fine:"), endContinuity);
    form->addRow(QStringLiteral("Influenza finale:"), endInfluence);
    form->addRow(QString(), ruledBox);
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
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->checkState() == Qt::Checked) d.loftSketches.append(list->item(row)->data(Qt::UserRole).toInt());
        d.loftGuides.clear();
        for (int row = 0; row < guides->count(); ++row) {
            const int sketch = guides->item(row)->data(Qt::UserRole).toInt();
            if (guides->item(row)->checkState() == Qt::Checked && !d.loftSketches.contains(sketch)) d.loftGuides.append(sketch);
        }
        d.loftGuidePaths = partialSelections;
        d.loftRuled = ruledBox->isChecked();
        d.loftStartContinuity = d.loftRuled ? 0 : startContinuity->currentIndex();
        d.loftEndContinuity = d.loftRuled ? 0 : endContinuity->currentIndex();
        d.loftGuideContinuity = guideContinuity->currentIndex();
        d.loftGuideInfluence = guideInfluence->value() / 100.0;
        d.loftStartInfluence = startInfluence->value() / 100.0;
        d.loftEndInfluence = endInfluence->value() / 100.0;
        d.sketchIndex = d.loftSketches.value(0, -1);
        d.plane = sketches.value(d.sketchIndex).plane;
        return d;
    };
    const auto refresh = [&] {
        const ExtrusionObject d = current();
        if (d.loftSketches.size() >= 2) scope.request(d);
        else viewport->clearPreview();
    };
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
    QObject::connect(removePartial, &QPushButton::clicked, &dialog, [&] {
        const int row = partialGuides->currentRow();
        if (row < 0 || row >= partialSelections.size()) return;
        partialSelections.removeAt(row); refillPartial(); refresh();
    });
    int graphicalTarget = 0;  // 1 sezioni, 2 guide
    const auto armGraphicalPick = [&](int target) {
        graphicalTarget = target;
        pickSections->setChecked(target == 1); pickGuides->setChecked(target == 2);
        pickStatus->setText(target == 1 ? QStringLiteral("Scelta sezioni attiva: clicca gli schizzi nella vista.")
                                        : QStringLiteral("Scelta guide attiva: clicca gli schizzi nella vista."));
    };
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
            const int existing = partialSelections.indexOf(path);
            if (existing >= 0) partialSelections.removeAt(existing);
            else {
                partialSelections.append(path);
                for (int row = 0; row < guides->count(); ++row)
                    if (guides->item(row)->data(Qt::UserRole).toInt() == sketch) guides->item(row)->setCheckState(Qt::Unchecked);
            }
            refillPartial(); refresh();
            pickStatus->setText(existing >= 0 ? QStringLiteral("Selezione parziale rimossa.")
                                               : QStringLiteral("Selezione parziale aggiunta alle curve guida."));
            break;
        }
    });
    QObject::connect(&dialog, &QDialog::finished, viewport, [viewport] {
        viewport->setSketchPickCallback({}); viewport->setSketchEntityPickCallback({});
    });
    QObject::connect(list, &QListWidget::itemChanged, &dialog, refresh);
    QObject::connect(guides, &QListWidget::itemChanged, &dialog, refresh);
    const auto updateContinuityAvailability = [&](bool ruled) {
        for (QWidget *control : {static_cast<QWidget *>(startContinuity), static_cast<QWidget *>(endContinuity),
                                 static_cast<QWidget *>(startInfluence), static_cast<QWidget *>(endInfluence)})
            control->setEnabled(!ruled);
        refresh();
    };
    QObject::connect(ruledBox, &QCheckBox::toggled, &dialog, updateContinuityAvailability);
    QObject::connect(startContinuity, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(endContinuity, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(guideContinuity, &QComboBox::currentIndexChanged, &dialog, refresh);
    for (QDoubleSpinBox *spin : {guideInfluence, startInfluence, endInfluence})
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    updateContinuityAvailability(ruledBox->isChecked());
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
        if (d.loftSketches.size() < 2) return QStringLiteral("Spunta almeno due schizzi.");
        for (int row = 0; row < guides->count(); ++row)
            if (guides->item(row)->checkState() == Qt::Checked
                && d.loftSketches.contains(guides->item(row)->data(Qt::UserRole).toInt()))
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
struct WindowLock {
    QList<QPointer<QWidget>> widgets;
    QList<QPointer<QAction>> actions;
    CadViewport *viewport = nullptr;
    WindowLock(QMainWindow *window, CadViewport *target, QWidget *keep) : viewport(target) {
        QList<QWidget *> candidates{window->menuBar()};
        for (QToolBar *bar : window->findChildren<QToolBar *>()) candidates.append(bar);
        for (QDockWidget *dock : window->findChildren<QDockWidget *>()) candidates.append(dock);
        for (QWidget *widget : candidates)
            if (widget && widget->isEnabled()) {
                widget->setEnabled(false);
                widgets.append(widget);
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
        viewport->setInteractionLocked(false);
    }
};

static bool runModeless(QMainWindow *window, CadViewport *viewport, QDialog &dialog) {
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
    status->setMinimumWidth(380);
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
    form->addRow(new QLabel(QStringLiteral("%1 entita' selezionate. Un segmento scelto come riferimento non si ripete.").arg(selection.size()), &dialog));
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
    status->setMaximumWidth(460);
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

// Ripetizione di un corpo o di una funzione (nuova, o al posto del corpo
// `replaced`): corpo, tipo (lineare, circolare, specchio), cosa si ripete,
// riferimenti scelti nella vista come per i piani di costruzione (direzione,
// asse o piano; la seconda direzione della griglia), istanze, passo o angolo.
// Anteprima in ambra; la conferma chiama `apply` (l'errore resta nella finestra).
static bool patternDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    QVector<int> candidates;
    for (int index = 0; index < bodies.size() && (replaced < 0 || index < replaced); ++index) {
        const ExtrusionObject &body = bodies.at(index);
        const bool shape = body.forgeBody && !(body.operation < 0 && (body.feature == BodyFeature::DatumPlane || body.feature == BodyFeature::Helix));
        if (shape) candidates.append(index);
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Nella scena non ci sono corpi da ripetere."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.feature = BodyFeature::Pattern;
    definition.operation = -1;
    PatternParameters &p = definition.pattern;
    if (!candidates.contains(definition.firstBody)) definition.firstBody = candidates.last();
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
    for (int index : candidates) bodyBox->addItem(bodies.at(index).visible ? bodies.at(index).name : bodies.at(index).name + QStringLiteral(" (nascosto)"));
    bodyBox->setCurrentIndex(int(candidates.indexOf(definition.firstBody)));
    bodyBox->setEnabled(replaced < 0);
    auto *kindBox = new QComboBox(&dialog);
    kindBox->addItems({QStringLiteral("Lineare"), QStringLiteral("Circolare"), QStringLiteral("Specchio")});
    kindBox->setCurrentIndex(qBound(0, p.kind, 2));
    auto *whatBox = new QComboBox(&dialog);
    whatBox->addItems({QStringLiteral("Il corpo intero"), QStringLiteral("La funzione (lo strumento dell'unione o della differenza)")});
    whatBox->setCurrentIndex(p.featureOnly ? 1 : 0);
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
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
    status->setMinimumWidth(380);
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
            || (base.operation < 0 && base.feature == BodyFeature::Extrusion && base.mergeOperation != 0 && !base.mergeBodies.isEmpty());
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
        else setStatus(QString(), false);
        scope->request(current());
    };
    const auto pick = [&](int slot) {
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
        viewport->cancelReferencePick();
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
    auto *refButton = new QPushButton(&dialog);
    refButton->setToolTip(QStringLiteral("Scegli nella vista"));
    auto *operationBox = new QComboBox(&dialog);
    operationBox->addItems({QStringLiteral("Corpo nuovo"), QStringLiteral("Unisci ai solidi"), QStringLiteral("Sottrai dai solidi")});
    operationBox->setCurrentIndex(qBound(0, definition.mergeOperation, 2));
    auto *autoBox = new QCheckBox(QStringLiteral("Automatico: i solidi che hanno punti in comune con l'estrusione"), &dialog);
    autoBox->setChecked(definition.mergeAuto);
    auto *bodyList = new QListWidget(&dialog);
    bodyList->setMinimumHeight(110);
    for (int index = 0; index < bodies.size() && (replaced < 0 || index < replaced); ++index) {
        const ExtrusionObject &body = bodies.at(index);
        if (!body.forgeBody || !body.solid || (body.operation < 0 && (body.feature == BodyFeature::DatumPlane || body.feature == BodyFeature::Helix))) continue;
        const bool used = definition.mergeBodies.contains(index);
        auto *item = new QListWidgetItem(body.visible || used ? body.name : body.name + QStringLiteral(" (nascosto)"), bodyList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(used ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, index);
    }
    form->addRow(QStringLiteral("Fine:"), extentBox);
    form->addRow(QStringLiteral("Distanza:"), distanceBox);
    form->addRow(QStringLiteral("Fino a:"), refButton);
    form->addRow(QStringLiteral("Risultato:"), operationBox);
    form->addRow(QString(), autoBox);
    form->addRow(QStringLiteral("Solidi:"), bodyList);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(380);
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
        d.mergeOperation = operationBox->currentIndex();
        d.mergeAuto = autoBox->isChecked();
        d.mergeBodies.clear();
        for (int row = 0; row < bodyList->count(); ++row)
            if (bodyList->item(row)->checkState() == Qt::Checked) d.mergeBodies.append(bodyList->item(row)->data(Qt::UserRole).toInt());
        return d;
    };
    const auto refresh = [&] {
        const int extent = extentBox->currentIndex();
        form->setRowVisible(distanceBox, extent == 0);
        form->setRowVisible(refButton, extent != 0);
        const bool merge = operationBox->currentIndex() != 0;
        form->setRowVisible(autoBox, merge);
        form->setRowVisible(bodyList, merge);
        bodyList->setEnabled(!autoBox->isChecked());
        QString text = definition.extentRef.kind >= 0 ? ForgeCad::geometryRefText(definition.extentRef, viewport->sketches(), viewport->extrusions())
                                                      : QStringLiteral("(da scegliere)");
        if (picking) text = QStringLiteral("▶ ") + text + QStringLiteral("  - clicca nella vista");
        refButton->setText(text);
        viewport->setReferenceMarks(extent != 0 && definition.extentRef.kind >= 0 ? QVector<GeometryRef>{definition.extentRef} : QVector<GeometryRef>());
        if (picking) setStatus(QStringLiteral("Clicca nella vista il riferimento (Esc nella vista: annulla la scelta)."), false);
        else if (extent != 0 && definition.extentRef.kind < 0) setStatus(QStringLiteral("Scegli nella vista dove finisce l'estrusione."), false);
        else setStatus(QString(), false);
        ExtrusionObject d = current();
        if (extent != 0 && d.extentRef.kind < 0) return;
        scope->request(viewport->withMergeCandidates(d, replaced >= 0 ? replaced : int(bodies.size())));
    };
    const auto pick = [&] {
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
        if (picked && picking) {
            if (ForgeCad::geometryRefRoles(ref, viewport->sketches()) & roles()) definition.extentRef = ref;
            else setStatus(QStringLiteral("Questo riferimento non va bene qui."), true);
        }
        picking = false;
        refresh();
    });
    QObject::connect(refButton, &QPushButton::clicked, &dialog, pick);
    QObject::connect(extentBox, &QComboBox::currentIndexChanged, &dialog, [&](int extent) {
        viewport->cancelReferencePick();
        picking = false;
        // Il riferimento che non vale per la fine nuova si sceglie di nuovo.
        if (definition.extentRef.kind >= 0 && !(ForgeCad::geometryRefRoles(definition.extentRef, viewport->sketches()) & roles())) definition.extentRef = GeometryRef();
        refresh();
        if (extent != 0 && definition.extentRef.kind < 0) pick();
    });
    QObject::connect(operationBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(autoBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(bodyList, &QListWidget::itemChanged, &dialog, refresh);
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const ExtrusionObject d = current();
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
    scope.reset();
    return accepted;
}

// Spostamento e rotazione di un corpo (nuovo, o al posto del corpo
// `replaced`): corpo, traslazione X/Y/Z, angolo e asse della rotazione
// (assi del modello o una retta scelta nella vista), copia. Finestra non
// modale; anteprima in ambra; la conferma chiama `apply`.
static bool transformDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                            const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    QVector<int> candidates;
    for (int index = 0; index < bodies.size() && (replaced < 0 || index < replaced); ++index) {
        const ExtrusionObject &body = bodies.at(index);
        if (body.forgeBody && !(body.operation < 0 && (body.feature == BodyFeature::DatumPlane || body.feature == BodyFeature::Helix))) candidates.append(index);
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Nella scena non ci sono corpi da spostare."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Transform;
    if (!candidates.contains(definition.firstBody)) definition.firstBody = candidates.last();
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int index : candidates) bodyBox->addItem(bodies.at(index).visible ? bodies.at(index).name : bodies.at(index).name + QStringLiteral(" (nascosto)"));
    bodyBox->setCurrentIndex(int(candidates.indexOf(definition.firstBody)));
    bodyBox->setEnabled(replaced < 0);
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
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Traslazione:"), translation);
    form->addRow(QStringLiteral("Rotazione:"), angleBox);
    form->addRow(QStringLiteral("Asse:"), axisButton);
    form->addRow(QString(), copyBox);
    form->addRow(new QLabel(QStringLiteral("Prima la rotazione attorno all'asse (verso destrorso), poi la traslazione."), &dialog));
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setMinimumWidth(380);
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
        setStatus(picking ? QStringLiteral("Clicca nella vista la retta dell'asse (Esc nella vista: annulla la scelta).") : QString(), false);
        scope->request(current());
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picked && picking) {
            if (ForgeCad::geometryRefRoles(ref, viewport->sketches()) & ForgeCad::DatumRoleLine) definition.move.axis = ref;
            else setStatus(QStringLiteral("Serve una retta."), true);
        }
        picking = false;
        refresh();
    });
    QObject::connect(axisButton, &QPushButton::clicked, &dialog, [&] {
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

// Booleana (nuova, o al posto del corpo `replaced`): operazione, il primo
// oggetto (A) e gli strumenti, anche piu' d'uno (A op B op C...: l'unione di
// tutti, l'intersezione di tutti, A meno tutti gli strumenti). Anteprima in
// ambra; la conferma chiama `apply` (l'errore resta nella finestra).
static bool booleanDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    QVector<int> indices;
    for (int index = 0; index < bodies.size() && (replaced < 0 || index < replaced); ++index) {
        const ExtrusionObject &body = bodies.at(index);
        if (!body.forgeBody || (body.operation < 0 && (body.feature == BodyFeature::DatumPlane || body.feature == BodyFeature::Helix))) continue;
        indices.append(index);
    }
    if (indices.size() < 2) {
        QMessageBox::information(parent, title, QStringLiteral("Servono almeno due corpi nella scena (solidi, o un solido e una superficie)."));
        return false;
    }
    const auto label = [&](int index) {
        const ExtrusionObject &body = bodies.at(index);
        QString text = body.name;
        if (!body.solid) text += QStringLiteral(" (superficie)");
        if (!body.visible && index != initial.firstBody && index != initial.secondBody && !initial.booleanTools.contains(index)) text += QStringLiteral(" (nascosto)");
        return text;
    };
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
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
    toolList->setMinimumHeight(140);
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
    form->addRow(QStringLiteral("Operazione:"), operationBox);
    form->addRow(QStringLiteral("Oggetto A:"), firstBox);
    form->addRow(QStringLiteral("Strumenti:"), toolList);
    form->addRow(new QLabel(QStringLiteral("Spunta uno o piu' strumenti: l'operazione li applica ad A uno dopo l'altro."), &dialog));
    const PreviewScope scope(viewport, dialog, form, replaced);
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
        const ExtrusionObject body = current();
        if (body.secondBody < 0) {
            scope.label->setText(QStringLiteral("scegli almeno uno strumento"));
            viewport->clearPreview();
            return;
        }
        scope.request(body);
    };
    QObject::connect(operationBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(firstBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(toolList, &QListWidget::itemChanged, &dialog, refresh);
    refresh();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    return runUntilApplied(dialog, form, buttons, [&] {
        const ExtrusionObject body = current();
        if (body.secondBody < 0) return QStringLiteral("Scegli almeno uno strumento.");
        return apply(body);
    });
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
        const auto fixed = [](double v) { return QString::number(v, 'f', 6); };
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
            row(QStringLiteral("Lunghezza"), fixed(r.length) + QStringLiteral(" mm"));
            row(QStringLiteral("Baricentro della curva"), QStringLiteral("X %1, Y %2, Z %3 mm").arg(fixed(r.centroid[0]), fixed(r.centroid[1]), fixed(r.centroid[2])));
        } else if (r.kind == ForgeCad::MassReport::Kind::Sheet) {
            row(QStringLiteral("Area della superficie"), fixed(r.area) + QStringLiteral(" mm²"));
            if (r.hasCentroid)
                row(QStringLiteral("Baricentro dell'area"), QStringLiteral("X %1, Y %2, Z %3 mm").arg(fixed(r.centroid[0]), fixed(r.centroid[1]), fixed(r.centroid[2])));
        } else {
            const double mass = rho * r.volume;
            row(QStringLiteral("Volume"), QStringLiteral("%1 mm³ (%2 cm³)").arg(fixed(r.volume), num(r.volume * 1e-3, 9)));
            row(QStringLiteral("Area della superficie"), fixed(r.area) + QStringLiteral(" mm²"));
            row(QStringLiteral("Densita'"), num(densityBox->value()) + QStringLiteral(" g/cm³"));
            row(QStringLiteral("Massa"), QStringLiteral("<b>%1 g</b> (%2 kg)").arg(num(mass, 9), num(mass * 1e-3, 9)));
            if (r.hasCentroid) {
                row(QStringLiteral("Baricentro"), QStringLiteral("X %1, Y %2, Z %3 mm").arg(fixed(r.centroid[0]), fixed(r.centroid[1]), fixed(r.centroid[2])));
                // Momenti (Ixx = \\int (y^2 + z^2) dm) e prodotti d'inerzia (Pxy = \\int x y dm), in g mm^2.
                const auto tensorRows = [&](const double t[3][3], const QString &where) {
                    row(QStringLiteral("Momenti d'inerzia %1").arg(where),
                        QStringLiteral("Ixx %1, Iyy %2, Izz %3 g·mm²").arg(num(rho * t[0][0], 9), num(rho * t[1][1], 9), num(rho * t[2][2], 9)));
                    row(QStringLiteral("Prodotti d'inerzia %1").arg(where),
                        QStringLiteral("Pxy %1, Pxz %2, Pyz %3 g·mm²").arg(num(-rho * t[0][1], 9), num(-rho * t[0][2], 9), num(-rho * t[1][2], 9)));
                };
                tensorRows(r.inertia, QStringLiteral("al baricentro"));
                double moments[3], axes[3][3];
                ForgeCad::principalMoments(r.inertia, moments, axes);
                for (int k = 0; k < 3; ++k)
                    row(QStringLiteral("Momento principale I%1").arg(k + 1),
                        QStringLiteral("%1 g·mm² lungo (%2, %3, %4), raggio d'inerzia %5 mm")
                            .arg(num(rho * moments[k], 9), num(axes[0][k], 6), num(axes[1][k], 6), num(axes[2][k], 6))
                            .arg(num(r.volume > 0.0 ? std::sqrt(std::max(0.0, moments[k]) / r.volume) : 0.0, 6)));
                const double origin[3] = {0.0, 0.0, 0.0};
                double about[3][3];
                ForgeCad::inertiaAbout(r, origin, about);
                tensorRows(about, QStringLiteral("all'origine"));
            }
        }
        html += QStringLiteral("</table><p style='color:#8aa0b4'>Calcolo: %1.<br>Unita': mm, g. Prodotti d'inerzia con il segno positivo (Pxy = ∫ x y dm); "
                               "il tensore d'inerzia ha fuori diagonale -Pxy.</p>")
                    .arg(r.method.toHtmlEscaped());
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

// Finestra del taglio di una superficie: la superficie, lo strumento (un corpo
// o un piano di riferimento) e la parte da tenere, con l'anteprima dal vivo.
// Le parti si ricalcolano a ogni cambio di superficie o di strumento; ognuna
// e' ricordata da un suo punto (trimKeep). `definition` porta i valori
// iniziali; `replaced` e' il corpo modificato (-1 nuovo): si scelgono solo i
// corpi che vengono prima. `apply` fa il comando.
static bool trimDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, ExtrusionObject definition,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    const int limit = replaced >= 0 ? replaced : int(bodies.size());
    QVector<int> sheets, tools;
    for (int index = 0; index < limit; ++index) {
        const ExtrusionObject &body = bodies.at(index);
        if (!body.forgeBody) continue;
        if (!body.solid) sheets.append(index);
        tools.append(index);
    }
    if (sheets.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Serve una superficie (estrusione di un profilo aperto) da tagliare."));
        return false;
    }
    const auto label = [&](int index) { return bodies.at(index).visible ? bodies.at(index).name : bodies.at(index).name + QStringLiteral(" (nascosto)"); };
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *sheetBox = new QComboBox(&dialog), *toolBox = new QComboBox(&dialog), *partBox = new QComboBox(&dialog);
    for (int index : sheets) sheetBox->addItem(label(index));
    for (int index : tools) toolBox->addItem(label(index));
    for (const QString &plane : planeNames()) toolBox->addItem(plane);
    sheetBox->setCurrentIndex(qMax(0, int(sheets.indexOf(definition.firstBody))));
    if (definition.secondBody >= 0 && tools.contains(definition.secondBody)) toolBox->setCurrentIndex(int(tools.indexOf(definition.secondBody)));
    else if (definition.secondBody < 0 && definition.firstBody >= 0) toolBox->setCurrentIndex(int(tools.size()) + qBound(0, definition.trimPlane, 2));
    else {
        // Il primo corpo che non e' la superficie.
        for (int k = 0; k < tools.size(); ++k)
            if (tools.at(k) != sheets.at(sheetBox->currentIndex())) {
                toolBox->setCurrentIndex(k);
                break;
            }
    }
    form->addRow(QStringLiteral("Superficie da tagliare:"), sheetBox);
    form->addRow(QStringLiteral("Strumento (corpo o piano):"), toolBox);
    form->addRow(QStringLiteral("Parte da tenere:"), partBox);
    const PreviewScope scope(viewport, dialog, form, replaced);
    QVector<SheetPiece> pieces;
    bool first = definition.firstBody >= 0 && replaced >= 0;
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.operation = -1;
        d.feature = BodyFeature::SheetTrim;
        d.firstBody = sheets.at(sheetBox->currentIndex());
        const int tool = toolBox->currentIndex();
        d.secondBody = tool < tools.size() ? tools.at(tool) : -1;
        d.trimPlane = tool < tools.size() ? 0 : tool - int(tools.size());
        if (partBox->currentIndex() >= 0 && partBox->currentIndex() < pieces.size()) d.trimKeep = pieces.at(partBox->currentIndex()).point;
        return d;
    };
    const auto refreshPreview = [&] {
        if (pieces.size() >= 2) scope.request(current());
    };
    const auto refreshPieces = [&] {
        const ExtrusionObject d = current();
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        pieces = viewport->sheetPieces(d.firstBody, d.secondBody, d.trimPlane, &error);
        QApplication::restoreOverrideCursor();
        const QSignalBlocker blocker(partBox);
        partBox->clear();
        if (pieces.size() < 2) {
            viewport->clearPreview();
            scope.label->setText(error.isEmpty() ? QStringLiteral("lo strumento non divide la superficie") : error);
            return;
        }
        int chosen = 0;
        double closest = 1e300;
        for (int k = 0; k < pieces.size(); ++k) {
            partBox->addItem(QStringLiteral("Parte %1 (area %2)").arg(k + 1).arg(pieces.at(k).area, 0, 'g', 6));
            // Modifica: la parte piu' vicina al punto di prima.
            const EdgePoint &p = pieces.at(k).point, &q = definition.trimKeep;
            const double d2 = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z);
            if (first && d2 < closest) closest = d2, chosen = k;
        }
        first = false;
        partBox->setCurrentIndex(chosen);
        refreshPreview();
    };
    QObject::connect(sheetBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(toolBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(partBox, &QComboBox::currentIndexChanged, &dialog, refreshPreview);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refreshPieces();
    return runUntilApplied(dialog, form, buttons, [&] {
        if (pieces.size() < 2) return QStringLiteral("Lo strumento non divide la superficie: scegline un altro.");
        return apply(current());
    });
}

// Finestra dell'estensione di una superficie: distanza e tipo (stessa
// superficie o lineare), con l'anteprima; "Bordi..." (se `allowReselect`)
// torna alla scelta dei bordi.
struct ExtendDialogResult {
    bool applied = false, reselect = false;
    double distance = 0.0;
    bool linear = false;
};
static ExtendDialogResult extendDialog(QWidget *parent, CadViewport *viewport, const QString &title, int base, int hidden, const QVector<EdgePoint> &edges,
                                       double distance, bool linear, bool allowReselect, const std::function<QString(double, bool)> &apply) {
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
    form->addRow(QStringLiteral("Bordi: %1").arg(edges.size()), new QLabel(&dialog));
    auto *previewLabel = new QLabel(QStringLiteral("in calcolo..."), &dialog);
    previewLabel->setWordWrap(true);
    previewLabel->setMaximumWidth(460);
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
    const auto refresh = [=] {
        previewLabel->setText(QStringLiteral("in calcolo..."));
        viewport->requestExtendPreview(base, edges, distanceBox->value(), typeBox->currentIndex() == 1, hidden);
    };
    viewport->setPreviewCallback([previewLabel](const QString &error) {
        previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
    });
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(typeBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    refresh();
    distanceBox->selectAll();
    result.applied = runUntilApplied(dialog, form, buttons, [&] { return apply(distanceBox->value(), typeBox->currentIndex() == 1); });
    viewport->setPreviewCallback({});
    viewport->clearPreview();
    result.distance = distanceBox->value();
    result.linear = typeBox->currentIndex() == 1;
    return result;
}

// Finestra dei parametri della rivoluzione (anche per modificarne una: lo
// schizzo resta quello, `fixedSketch`). Valori iniziali e risultato negli argomenti.
// Con `apply` la conferma esegue il comando e la finestra resta aperta se fallisce.
static bool revolutionDialog(QWidget *parent, const QVector<SketchObject> &sketches, bool fixedSketch, int &sketch, int &axis,
                             double &angle, const std::function<QString(int, int, double)> &apply = {},
                             const PreviewSpec<int, int, double> &preview = {}) {
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Rivoluzione"));
    auto *form = dialog.createScrollableForm();
    form->addRow(new FeatureOperationDiagram(FeatureOperationDiagram::Revolution, &dialog));
    auto *sketchBox = new QComboBox(&dialog);
    for (const SketchObject &item : sketches) sketchBox->addItem(item.name);
    sketchBox->setCurrentIndex(qBound(0, sketch, int(sketches.size()) - 1));
    sketchBox->setEnabled(!fixedSketch);
    auto *axisBox = new QComboBox(&dialog);
    const auto fillAxes = [axisBox, &sketches](int sketchIndex) {
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
    };
    fillAxes(sketchBox->currentIndex());
    const int initialAxis = axisBox->findData(axis);
    if (fixedSketch && initialAxis >= 0) axisBox->setCurrentIndex(initialAxis);
    QObject::connect(sketchBox, &QComboBox::currentIndexChanged, &dialog, fillAxes);
    auto *angleBox = new ForgeCad::ExpressionSpinBox(&dialog);
    angleBox->setDecimals(6);
    angleBox->setRange(0.000001, 360.0);
    angleBox->setValue(std::abs(angle));
    angleBox->setSuffix(QStringLiteral(" \u00B0"));
    auto *reverseBox = new QCheckBox(QStringLiteral("Verso opposto"), &dialog);
    reverseBox->setChecked(angle < 0.0);
    form->addRow(QStringLiteral("Schizzo:"), sketchBox);
    form->addRow(QStringLiteral("Asse:"), axisBox);
    form->addRow(QStringLiteral("Angolo:"), angleBox);
    form->addRow(QString(), reverseBox);
    const PreviewScope scope(preview.define ? preview.viewport : nullptr, dialog, form, preview.index);
    if (preview.define) {
        const auto refresh = [&scope, &preview, sketchBox, axisBox, angleBox, reverseBox] {
            if (axisBox->currentIndex() < 0) return;
            scope.request(preview.define(sketchBox->currentIndex(), axisBox->currentData().toInt(),
                                         reverseBox->isChecked() ? -angleBox->value() : angleBox->value()));
        };
        QObject::connect(axisBox, &QComboBox::currentIndexChanged, &dialog, refresh);
        QObject::connect(angleBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
        QObject::connect(reverseBox, &QCheckBox::toggled, &dialog, refresh);
        refresh();
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    const auto read = [&] {
        sketch = sketchBox->currentIndex();
        axis = axisBox->currentData().toInt();
        angle = reverseBox->isChecked() ? -angleBox->value() : angleBox->value();
    };
    if (apply) {
        const auto run = [&] {
            if (axisBox->currentIndex() < 0) return QStringLiteral("Scegli l'asse della rivoluzione.");
            read();
            return apply(sketch, axis, angle);
        };
        // Con l'anteprima la finestra non e' modale: la vista si puo' ruotare per vedere il verso.
        auto *window = qobject_cast<QMainWindow *>(parent);
        if (window && preview.viewport) return runUntilAppliedModeless(window, preview.viewport, dialog, form, buttons, run);
        return runUntilApplied(dialog, form, buttons, run);
    }
    if (dialog.exec() != QDialog::Accepted || axisBox->currentIndex() < 0) return false;
    read();
    return true;
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
        : FunctionDialogPanel(window), settingsKey_(settingsKey), defaultSize_(defaultSize) {
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

PdfWindow::PdfWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ForgeCAD - Qt6"));
    qApp->installEventFilter(new DialogMover(this));
    resize(1280, 820);
    setMinimumSize(900, 600);
    viewport_ = new CadViewport(this);
    auto *viewport = viewport_;
    viewport->setOrientation(defaultAxesOrientation());  // assi del documento nuovo (Opzioni, di default Z in alto)
    setCentralWidget(viewport);

    auto *modelDock = new QDockWidget(QStringLiteral("Albero modello"), this);
    modelDock->setObjectName(QStringLiteral("modelDock"));  // per saveState/restoreState
    auto *modelTree = new StoryboardTree(modelDock);
    modelTree_ = modelTree;
    modelTree->setMoveFeatureCallback([viewport](int feature, int target) { return viewport->moveFeatureTo(feature, target); });
    modelTree->setHeaderLabel(QStringLiteral("Oggetti scena"));
    modelTree->setMinimumWidth(220);
    modelTree->setContextMenuPolicy(Qt::CustomContextMenu);
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
            int sketch = original.sketchIndex, axis = original.revolveAxis;
            double angle = original.revolveAngle;
            const auto define = [&](int, int axisIndex, double degrees) {
                ExtrusionObject body = original;
                body.revolveAxis = axisIndex;
                body.revolveAngle = degrees;
                return body;
            };
            revolutionDialog(this, viewport->sketches(), true, sketch, axis, angle,
                             [&](int s, int a, double d) { return update(define(s, a, d)); }, {viewport, index, define});
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
                body.trimPlane = values.trimPlane;
                body.trimKeep = values.trimKeep;
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
        } else if (original.feature == BodyFeature::SheetExtend) {
            extendDialog(this, viewport, QStringLiteral("Modifica estensione"), original.firstBody, index, original.blendEdges, original.blendSize,
                         original.extendLinear, false, [&](double distance, bool linear) {
                             ExtrusionObject body = original;
                             body.blendSize = distance;
                             body.extendLinear = linear;
                             return update(body);
                         });
        }
    });
    viewport->setEditBodyCallback([editBody](int index) { (*editBody)(index); });
    connect(modelTree, &QTreeWidget::itemDoubleClicked, this, [editBody](QTreeWidgetItem *item, int) {
        if (item->data(0, Qt::UserRole).toInt() == kTreeExtrusion) (*editBody)(item->data(0, Qt::UserRole + 1).toInt());
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
    viewport->setDocumentChangedCallback([this] {
        if (!loadingDocument_ && !documentModified_) {
            documentModified_ = true;
            updateWindowTitle();
        }
        scheduleModelTreeRebuild();
        updateUndoActions();
    });
    viewport->setPlaneContextCallback([this, viewport, createSketchOnPlane](int plane) {
        QMenu menu(this);
        QAction *newSketch = menu.addAction(QStringLiteral("Nuovo schizzo su questo piano"));
        QAction *normalView = menu.addAction(QStringLiteral("Vista normale al piano"));
        const QAction *chosen = menu.exec(QCursor::pos());
        if (chosen == newSketch && *createSketchOnPlane) (*createSketchOnPlane)(plane);
        else if (chosen == normalView) viewport->setViewNormal(plane);
    });
    connect(modelTree, &QTreeWidget::itemChanged, this, [this, viewport](QTreeWidgetItem *item, int) {
        if (rebuildingTree_) return;
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
    connect(modelTree, &QTreeWidget::itemClicked, this, [this, viewport](QTreeWidgetItem *item, int) {
        if (suppressTreeClick_) return;
        const int type = item->data(0, Qt::UserRole).toInt();
        const int index = item->data(0, Qt::UserRole + 1).toInt();
        if (type == kTreePlane) viewport->selectPlane(index);
        if (type == kTreeSketch) viewport->selectSketch(index);
        if (type == kTreeExtrusion) viewport->selectObject(SceneObjectKind::Extrusion, index);
        if (type == kTreeBody) {
            const int tip = viewport->modelBodyTip(index);
            if (tip >= 0) viewport->selectObject(SceneObjectKind::Extrusion, tip);
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
        auto *item = modelTree->itemAt(position);
        if (!item) return;
        const int type = item->data(0, Qt::UserRole).toInt();
        const int index = item->data(0, Qt::UserRole + 1).toInt();
        QMenu menu(this);
        const QPoint globalPosition = modelTree->viewport()->mapToGlobal(position);
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
            rename->setShortcut(QKeySequence(Qt::Key_F2));
            menu.addSeparator();
            QAction *remove = menu.addAction(QStringLiteral("Elimina corpo e storyboard"));
            const QAction *chosen = menu.exec(globalPosition);
            if (chosen == rename) renameTreeItem(item);
            else if (chosen == toggle) viewport->setModelBodyVisible(index, !visible);
            else if (chosen == remove) viewport->deleteModelBody(index);
        } else if (type == kTreeSketch || type == kTreeExtrusion) {
            const SceneObjectKind kind = type == kTreeSketch ? SceneObjectKind::Sketch : SceneObjectKind::Extrusion;
            const bool visible = viewport->isObjectVisible(kind, index);
            QAction *editSketch = type == kTreeSketch ? menu.addAction(QStringLiteral("Modifica schizzo")) : nullptr;
            QAction *editParameters = type == kTreeExtrusion ? menu.addAction(QStringLiteral("Modifica parametri...")) : nullptr;
            const bool storyboardFeature = type == kTreeExtrusion && index >= 0 && index < viewport->extrusions().size()
                                        && viewport->extrusions().at(index).modelBodyId != 0;
            QAction *suppress = storyboardFeature
                ? menu.addAction(viewport->extrusions().at(index).suppressed ? QStringLiteral("Riattiva feature")
                                                                            : QStringLiteral("Sopprimi feature"))
                : nullptr;
            QAction *moveEarlier = storyboardFeature ? menu.addAction(QStringLiteral("Sposta prima nella storia")) : nullptr;
            QAction *moveLater = storyboardFeature ? menu.addAction(QStringLiteral("Sposta dopo nella storia")) : nullptr;
            const bool datum = type == kTreeExtrusion && index >= 0 && index < viewport->extrusions().size()
                            && viewport->extrusions().at(index).feature == BodyFeature::DatumPlane;
            QAction *datumSketch = datum ? menu.addAction(QStringLiteral("Nuovo schizzo sul piano")) : nullptr;
            if (datumSketch) datumSketch->setEnabled(viewport->extrusions().at(index).datumValid);
            QAction *toggle = storyboardFeature ? nullptr : menu.addAction(visible ? QStringLiteral("Nascondi") : QStringLiteral("Mostra"));
            QAction *rename = menu.addAction(QStringLiteral("Rinomina..."));
            rename->setShortcut(QKeySequence(Qt::Key_F2));
            menu.addSeparator();
            QAction *remove = menu.addAction(QStringLiteral("Elimina"));
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

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("File"));
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
    connect(saveAsAction, &QAction::triggered, this, [this] { saveDocument(true); });
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
        const QString error = ForgeCad::importCadFile(path, parts, &notes);
        const QStringList failures = error.isEmpty() ? viewport->importParts(parts, QFileInfo(path).fileName()) : QStringList();
        endForegroundProgress();
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Importa"), error);
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
            QMessageBox::information(this, QStringLiteral("Importa"), text);
        }
    });
    // Esportazione per altri CAD (cad_export): corpi visibili, B-rep esatti in mm.
    auto *exportMenu = fileMenu->addMenu(QStringLiteral("Esporta"));
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
    auto *editMenu = menuBar()->addMenu(QStringLiteral("Modifica"));
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
    updateUndoActions();
    auto *viewMenu = menuBar()->addMenu(QStringLiteral("Visualizza"));
    auto *functionPanelMenu = viewMenu->addMenu(QStringLiteral("Pannelli delle funzioni"));
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
    auto *functionsMenu = menuBar()->addMenu(QStringLiteral("Funzioni"));
    QAction *extrudeAction = functionsMenu->addAction(QStringLiteral("Estrusione..."));
    extrudeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    auto *booleanMenu = functionsMenu->addMenu(QStringLiteral("Booleane"));
    QAction *unionAction = booleanMenu->addAction(QStringLiteral("Unione..."));
    QAction *intersectionAction = booleanMenu->addAction(QStringLiteral("Intersezione..."));
    QAction *differenceAction = booleanMenu->addAction(QStringLiteral("Differenza A - B..."));
    QAction *revolveAction = functionsMenu->addAction(QStringLiteral("Rivoluzione..."));
     revolveAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    auto *primitiveMenu = functionsMenu->addMenu(QStringLiteral("Primitive"));
    QAction *filletAction = functionsMenu->addAction(QStringLiteral("Raccordo spigoli..."));
    QAction *chamferAction = functionsMenu->addAction(QStringLiteral("Smusso spigoli..."));
    QAction *datumAction = functionsMenu->addAction(QStringLiteral("Piano di costruzione..."));
    datumAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    datumAction->setToolTip(QStringLiteral("Piano parallelo a distanza, per tre punti, normale a una curva, per una retta e un punto, ad angolo, medio"));
    auto *patternMenu = functionsMenu->addMenu(QStringLiteral("Ripetizione"));
    QAction *linearPatternAction = patternMenu->addAction(QStringLiteral("Ripetizione lineare..."));
    QAction *circularPatternAction = patternMenu->addAction(QStringLiteral("Ripetizione circolare..."));
    QAction *mirrorAction = patternMenu->addAction(QStringLiteral("Specchio..."));
    linearPatternAction->setToolTip(QStringLiteral("Copie di un corpo o di una funzione (foro, sporgenza) lungo una o due direzioni"));
    circularPatternAction->setToolTip(QStringLiteral("Copie di un corpo o di una funzione attorno a un asse"));
    mirrorAction->setToolTip(QStringLiteral("Immagine speculare di un corpo o di una funzione rispetto a un piano"));
    QAction *moveAction = functionsMenu->addAction(QStringLiteral("Sposta / ruota corpo..."));
    moveAction->setToolTip(QStringLiteral("Traslazione e rotazione esatte del corpo selezionato (anche come copia)"));
    QAction *scaleAction = functionsMenu->addAction(QStringLiteral("Scala..."));
    scaleAction->setToolTip(QStringLiteral("Scala uniforme del corpo selezionato attorno all'origine, al baricentro o a un punto"));
    functionsMenu->addSeparator();
    QAction *helixAction = functionsMenu->addAction(QStringLiteral("Elica e spirale..."));
    helixAction->setToolTip(QStringLiteral("Elica (cilindrica o conica) o spirale piana da un cerchio di uno schizzo, uno spigolo circolare o una faccia cilindrica"));
    QAction *sweepAction = functionsMenu->addAction(QStringLiteral("Sweep (estrusione lungo un percorso)..."));
    sweepAction->setToolTip(QStringLiteral("Profilo di uno schizzo trascinato lungo un percorso: un altro schizzo o un'elica"));
    QAction *loftAction = functionsMenu->addAction(QStringLiteral("Loft..."));
    loftAction->setToolTip(QStringLiteral("Solido o superficie che passa per le sezioni di piu' schizzi"));
    functionsMenu->addSeparator();
    auto *surfaceMenu = functionsMenu->addMenu(QStringLiteral("Superfici"));
    QAction *trimSurfaceAction = surfaceMenu->addAction(QStringLiteral("Taglia superficie..."));
    trimSurfaceAction->setToolTip(QStringLiteral("Taglia una superficie con un corpo (superficie o solido) o un piano e ne tiene una parte"));
    QAction *extendSurfaceAction = surfaceMenu->addAction(QStringLiteral("Estendi superficie..."));
    extendSurfaceAction->setToolTip(QStringLiteral("Estende i bordi scelti di una superficie: clic sui bordi, Invio, poi la distanza"));
    auto *analysisMenu = menuBar()->addMenu(QStringLiteral("Analisi"));
    QAction *massAction = analysisMenu->addAction(QStringLiteral("Proprieta' di massa..."));
    massAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    massAction->setToolTip(QStringLiteral("Volume, massa, baricentro e momenti d'inerzia del corpo selezionato"));
    auto *modeMenu = viewMenu->addMenu(QStringLiteral("Stile visualizzazione"));
    auto *qualityMenu = viewMenu->addMenu(QStringLiteral("Qualita tessellazione"));
    auto *qualityGroup = new QActionGroup(this); qualityGroup->setExclusive(true);
    QAction *lowQuality = qualityMenu->addAction(QStringLiteral("Bassa"));
    QAction *mediumQuality = qualityMenu->addAction(QStringLiteral("Media"));
    QAction *highQuality = qualityMenu->addAction(QStringLiteral("Alta"));
    lowQuality->setCheckable(true); mediumQuality->setCheckable(true); highQuality->setCheckable(true);
    mediumQuality->setChecked(true);
    qualityGroup->addAction(lowQuality); qualityGroup->addAction(mediumQuality); qualityGroup->addAction(highQuality);
    auto *lightingMenu = viewMenu->addMenu(QStringLiteral("Luci scena"));
    QAction *backgroundAction = viewMenu->addAction(QStringLiteral("Sfondo e luce ambiente..."));
    connect(backgroundAction, &QAction::triggered, this, &PdfWindow::editBackground);
    // Sfondo dell'albero modello (QSettings view/treeBackground; vuoto = quello del tema).
    auto *treeColorMenu = viewMenu->addMenu(QStringLiteral("Sfondo dell'albero"));
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
    auto *axesMenu = viewMenu->addMenu(QStringLiteral("Assi"));
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
            viewport->axisLength(), 0.1, 100.0, 3, &accepted);
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
        form->addRow(new QLabel(QStringLiteral("In modalita' schizzo le facce dei corpi diventano trasparenti\n"
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
    auto *antialiasingMenu = viewMenu->addMenu(QStringLiteral("Antialiasing"));
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
        const auto apply = [viewport, panel, basePlane, offsetBox, flipBox, capsBox, planeVisible, info, planeBox, state] {
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
            QMessageBox::information(this, QStringLiteral("Rivoluzione"), QStringLiteral("Disegna prima uno schizzo con un profilo chiuso e l'asse (linea di costruzione)."));
            return;
        }
        const SceneSelection selection = viewport->selection();
        int sketch = viewport->activeSketchIndex(), axis = 0;
        if (selection.kind == SceneObjectKind::Sketch) sketch = selection.index;
        double angle = 360.0;
        const QString name = QStringLiteral("Rivoluzione %1").arg(viewport->extrusions().size() + 1);
        const SketchViewUnlock unlock(viewport);  // la vista si ruota per vedere il verso della rivoluzione
        revolutionDialog(this, sketches, false, sketch, axis, angle, [viewport, name](int sketchIndex, int axisIndex, double degrees) {
            return viewport->createRevolution(sketchIndex, axisIndex, degrees, name);
        }, {viewport, -1, [](int sketchIndex, int axisIndex, double degrees) {
            ExtrusionObject body;
            body.feature = BodyFeature::Revolution;
            body.sketchIndex = sketchIndex;
            body.revolveAxis = axisIndex;
            body.revolveAngle = degrees;
            return body;
        }});
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
            return viewport->createBody(body, d.sweepPath == 1 ? QVector<int>{d.firstBody} : QVector<int>{});
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
        ExtrusionObject definition;
        const SceneSelection selection = viewport->selection();
        if (selection.kind == SceneObjectKind::Extrusion) definition.firstBody = selection.index;
        const QString name = QStringLiteral("Taglio %1").arg(viewport->extrusions().size() + 1);
        trimDialog(this, viewport, QStringLiteral("Taglia superficie"), -1, definition, [viewport, name](const ExtrusionObject &d) {
            return viewport->createSheetTrim(d.firstBody, d.secondBody, d.trimPlane, d.trimKeep, name);
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
                                                       *extendLinear, true, [&](double distance, bool linear) {
                                                           return viewport->createSheetExtend(body, edges, distance, linear, name);
                                                       });
        *extendLinear = result.linear;
        viewport->setEdgePickSize(result.distance);
        if (pickSizeBox_) pickSizeBox_->setValue(result.distance);
        if (result.reselect) viewport->resumeExtendPick(body, edges, result.linear);
    });

    connect(unionAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Union, QStringLiteral("Unione")); });
    connect(intersectionAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Intersection, QStringLiteral("Intersezione")); });
    connect(differenceAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Difference, QStringLiteral("Differenza")); });

    // Opzioni: kernel geometrico con cui si costruiscono i corpi. La scelta
    // resta per gli avvii successivi (QSettings).
    auto *optionsMenu = menuBar()->addMenu(QStringLiteral("Opzioni"));
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
        form->addRow(new QLabel(QStringLiteral("Cambia solo come si vedono gli assi (viste standard, orbita, griglia) e gli assi\n"
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
    // Tasto per il pan (da tenere premuto trascinando con il sinistro), in QSettings.
    QAction *panKeyAction = optionsMenu->addAction(QStringLiteral("Tasto per il pan..."));
    const auto panKeyName = [](int key) { return QKeySequence(key).toString(QKeySequence::NativeText); };
    viewport->setPanKey(QSettings().value(QStringLiteral("view/panKey"), int(Qt::Key_Space)).toInt());
    connect(panKeyAction, &QAction::triggered, this, [this, viewport, panKeyName] {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Tasto per il pan"));
        auto *form = new QFormLayout(&dialog);
        form->addRow(new QLabel(QStringLiteral("Tieni premuto il tasto e trascina con il tasto sinistro per spostare la vista\n"
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
    auto *sketchMenu = menuBar()->addMenu(QStringLiteral("Schizzo"));
    QAction *newSketchAction = sketchMenu->addAction(QStringLiteral("Nuovo schizzo..."));
    QAction *faceSketchAction = sketchMenu->addAction(QStringLiteral("Nuovo schizzo sulla faccia selezionata"));
    faceSketchAction->setToolTip(QStringLiteral("Clic su una faccia piana di un corpo, poi questo comando (anche dal menu contestuale della faccia)"));
    connect(faceSketchAction, &QAction::triggered, this, [this, viewport] {
        const QString error = viewport->createSketchOnSelectedFace(QStringLiteral("Schizzo %1").arg(viewport->sketches().size() + 1));
        if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Schizzo sulla faccia"), error);
    });
    QAction *sketchAction = sketchMenu->addAction(QStringLiteral("Disegna segmenti"));
    auto *toolMenu = sketchMenu->addMenu(QStringLiteral("Strumento geometrico"));
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
    auto *editToolMenu = sketchMenu->addMenu(QStringLiteral("Modifica entita'"));
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
    QAction *snapAction = sketchMenu->addAction(QStringLiteral("Snap griglia e geometria")); snapAction->setCheckable(true); snapAction->setChecked(true);
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
    auto *sketchPatternMenu = sketchMenu->addMenu(QStringLiteral("Ripetizione"));
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
    auto *referenceMenu = sketchMenu->addMenu(QStringLiteral("Riferimenti esterni"));
    auto *axisReferences = referenceMenu->addMenu(QStringLiteral("Assi del modello"));
    for (int axis = 0; axis < 3; ++axis) {
        QAction *action = axisReferences->addAction(QStringLiteral("Asse %1").arg(QStringLiteral("XYZ").at(axis)));
        connect(action, &QAction::triggered, this, [viewport, axis] { viewport->selectSketchReference(2, axis); });
    }
    auto *planeReferences = referenceMenu->addMenu(QStringLiteral("Piani di riferimento"));
    for (int plane = 0; plane < 3; ++plane) {
        QAction *action = planeReferences->addAction(plane == 0 ? QStringLiteral("XY") : plane == 1 ? QStringLiteral("XZ") : QStringLiteral("YZ"));
        connect(action, &QAction::triggered, this, [viewport, plane] { viewport->selectSketchReference(1, plane); });
    }
    referenceMenu->addSeparator();
    QAction *convertTool = referenceMenu->addAction(QStringLiteral("Converti spigoli e curve"));
    convertTool->setCheckable(true);
    toolGroup->addAction(convertTool);
    convertTool->setData(int(DrawingTool::ConvertEdges));
    convertTool->setToolTip(QStringLiteral("Clic su uno spigolo di un corpo o su una curva (elica, spirale): la sua proiezione esatta entra nello schizzo"));
    connect(convertTool, &QAction::triggered, this, [viewport] { viewport->setDrawingTool(DrawingTool::ConvertEdges); });
    QAction *sectionReferences = referenceMenu->addAction(QStringLiteral("Sezione dei solidi nel piano dello schizzo"));
    sectionReferences->setToolTip(QStringLiteral("Le curve esatte in cui il piano dello schizzo taglia i solidi visibili"));
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

    *createSketchOnPlane = [this, viewport](int plane) {
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
    connect(snapAction, &QAction::toggled, this, [viewport](bool enabled) { viewport->setSnapEnabled(enabled); });
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
            0.0, -360.0, 360.0, 6, &accepted);
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
        {undoAction_, QStringLiteral("undo")}, {redoAction_, QStringLiteral("redo")}, {deleteAction, QStringLiteral("delete")},
        {newSketchAction, QStringLiteral("newSketch")}, {faceSketchAction, QStringLiteral("faceSketch")},
        {extrudeAction, QStringLiteral("extrude")}, {revolveAction, QStringLiteral("revolve")},
        {filletAction, QStringLiteral("fillet")}, {chamferAction, QStringLiteral("chamfer")},
        {trimSurfaceAction, QStringLiteral("trimSurface")}, {extendSurfaceAction, QStringLiteral("extendSurface")}, {scaleAction, QStringLiteral("scale")}, {moveAction, QStringLiteral("move")},
        {datumAction, QStringLiteral("datumPlane")}, {importAction, QStringLiteral("import")},
        {linearPatternAction, QStringLiteral("patternLinear")}, {circularPatternAction, QStringLiteral("patternCircular")},
        {mirrorAction, QStringLiteral("mirror")},
        {helixAction, QStringLiteral("helix")}, {sweepAction, QStringLiteral("sweep")}, {loftAction, QStringLiteral("loft")}, {massAction, QStringLiteral("massProperties")},
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
        {sketchMirror, QStringLiteral("sketchMirror")}, {convertTool, QStringLiteral("convertEdges")},
        {sectionReferences, QStringLiteral("sectionCurves")},
        {dimensionAction, QStringLiteral("dimension")}, {automaticConstraint, QStringLiteral("constraintAuto")},
        {freeConstraint, QStringLiteral("constraintFree")}, {horizontalConstraint, QStringLiteral("constraintHorizontal")},
        {verticalConstraint, QStringLiteral("constraintVertical")}, {lengthConstraint, QStringLiteral("constraintLength")},
        {angleConstraint, QStringLiteral("constraintAngle")}, {snapAction, QStringLiteral("snap")}, {originSnapAction, QStringLiteral("originSnap")},
        {exitSketch, QStringLiteral("exitSketch")}, {sketchNormalView, QStringLiteral("viewFront")}};
    for (const auto &entry : iconActions) decorate(entry.first, entry.second);
    const QStringList primitiveIcons = {QStringLiteral("box"), QStringLiteral("cylinder"), QStringLiteral("sphere"), QStringLiteral("cone"), QStringLiteral("torus")};
    for (int k = 0; k < primitiveActions.size() && k < primitiveIcons.size(); ++k) decorate(primitiveActions.at(k), primitiveIcons.at(k));
    const QStringList viewIcons = {QStringLiteral("viewFront"), QStringLiteral("viewRear"), QStringLiteral("viewRight"),
                                   QStringLiteral("viewTop"), QStringLiteral("viewIso"), QStringLiteral("viewTrimetric")};
    for (int k = 0; k < viewActions->size(); ++k) decorate(viewActions->at(k), viewIcons.at(k));
    exitSketch->setToolTip(QStringLiteral("Chiudi lo schizzo"));
    const auto flyout = [](QToolBar *bar, const QList<QAction *> &actions, const QString &tip) {
        auto *button = new QToolButton(bar);
        auto *menu = new QMenu(button);
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
        bar->setMovable(false);
        bar->setIconSize(QSize(24, 24));
        bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        return bar;
    };

    auto *toolbar = iconBar(QStringLiteral("Modellazione"), QStringLiteral("modelingIconBar"));
    toolbar->addAction(newAction); toolbar->addAction(openAction); toolbar->addAction(saveAction); toolbar->addSeparator();
    toolbar->addAction(undoAction_); toolbar->addAction(redoAction_); toolbar->addAction(deleteAction); toolbar->addSeparator();
    toolbar->addAction(importAction); toolbar->addSeparator();
    toolbar->addAction(newSketchAction); toolbar->addAction(faceSketchAction); toolbar->addAction(datumAction); toolbar->addSeparator();
    toolbar->addAction(extrudeAction); toolbar->addAction(revolveAction);
    flyout(toolbar, primitiveActions, QStringLiteral("Primitive: la freccia per le altre"));
    toolbar->addAction(filletAction); toolbar->addAction(chamferAction);
    flyout(toolbar, {trimSurfaceAction, extendSurfaceAction}, QStringLiteral("Superfici: la freccia per l'altro comando"));
    toolbar->addAction(moveAction);
    toolbar->addAction(scaleAction);
    flyout(toolbar, {linearPatternAction, circularPatternAction, mirrorAction}, QStringLiteral("Ripetizioni: la freccia per le altre"));
    toolbar->addSeparator();
    toolbar->addAction(helixAction); toolbar->addAction(sweepAction); toolbar->addAction(loftAction);
    toolbar->addSeparator();
    toolbar->addAction(unionAction); toolbar->addAction(intersectionAction); toolbar->addAction(differenceAction); toolbar->addSeparator();
    toolbar->addAction(massAction); toolbar->addSeparator();
    toolbar->addAction(resetZoomAction);
    toolbar->addAction(sectionAction);
    flyout(toolbar, *viewActions, QStringLiteral("Viste standard: la freccia per le altre"));
    flyout(toolbar, {modeMenu->actions().at(2), modeMenu->actions().at(1), modeMenu->actions().at(0)}, QStringLiteral("Stile di visualizzazione"));
    auto *spacer = new QWidget(toolbar); spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); toolbar->addWidget(spacer);
    toolbar->addWidget(new QLabel(QStringLiteral("  ForgeCAD / Part Studio  ")));

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
    auto *drawingToolbar = iconBar(QStringLiteral("Strumenti schizzo"), QStringLiteral("sketchIconBar"));
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
    flyout(drawingToolbar, {sketchLinearPattern, sketchCircularPattern, sketchMirror}, QStringLiteral("Ripetizioni: la freccia per le altre"));
    flyout(drawingToolbar, {convertTool, sectionReferences}, QStringLiteral("Riferimenti dai corpi: la freccia per la sezione"));
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(constraintsAction); drawingToolbar->addAction(dimensionAction);
    flyout(drawingToolbar, {automaticConstraint, freeConstraint, horizontalConstraint, verticalConstraint},
           QStringLiteral("Vincolo della prossima linea: la freccia per gli altri"));
    flyout(drawingToolbar, {lengthConstraint, angleConstraint}, QStringLiteral("Quote della prossima linea: la freccia per l'altra"));
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(snapAction); drawingToolbar->addAction(originSnapAction);
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(extrudeAction); drawingToolbar->addAction(revolveAction);
    // In modalita' schizzo si spengono solo le viste standard (la vista resta normale
    // al piano); il resto del menu Visualizza (trasparenza dei corpi, zoom...) resta attivo.
    viewport->setSketchModeCallback([this, viewActions, drawingToolbar, toolbar](bool active) {
        for (QAction *action : *viewActions) action->setEnabled(!active);
        toolbar->setVisible(!active);
        drawingToolbar->setVisible(active);
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
    const QString cudaStatus = forgecad_cuda_available()
        ? QString::fromUtf8(forgecad_cuda_backend())
        : QStringLiteral("CUDA compilato, GPU runtime non disponibile");
    auto *gpuStatus = new QLabel(QStringLiteral("%1 | OpenGL: in avvio").arg(cudaStatus));
    auto *sceneCounts = new QLabel(QStringLiteral("Solidi: 0 · Superfici: 0"));
    sceneCounts->setObjectName(QStringLiteral("sceneCounts"));
    sceneCounts->setToolTip(QStringLiteral("Solidi e superfici separati nei corpi visibili: dopo un'unione riuscita i solidi uniti ne fanno uno"));
    statusBar()->addPermanentWidget(sceneCounts);
    statusBar()->addPermanentWidget(gpuStatus);
    viewport->setRendererCallback([gpuStatus, cudaStatus](const QString &renderer) {
        gpuStatus->setText(QStringLiteral("%1 | OpenGL: %2").arg(cudaStatus, renderer));
    });
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
                                   QStringLiteral("view/constraintPanel"), QStringLiteral("document/saveBodies"),
                                   QStringLiteral("view/sketchBodyOpacity")})
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
        drawingToolbar->setVisible(false);  // compare solo in modalita' schizzo
        settings.endGroup();
    }
    updateWindowTitle();
    setTheme(true);
}

void PdfWindow::updateWindowTitle() {
    const QString name = documentPath_.isEmpty() ? QStringLiteral("Senza nome") : QFileInfo(documentPath_).fileName();
    setWindowTitle(QStringLiteral("%1%2 - ForgeCAD").arg(name, documentModified_ ? QStringLiteral(" *") : QString()));
}

// Chiede se salvare le modifiche; false se l'utente annulla (o il salvataggio fallisce).
bool PdfWindow::maybeSaveChanges() {
    if (!documentModified_) return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("ForgeCAD"),
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
        foregroundProgress_->setWindowTitle(QStringLiteral("ForgeCAD - operazione in corso"));
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

void PdfWindow::newDocument() {
    if (!maybeSaveChanges()) return;
    loadingDocument_ = true;
    DocumentState empty;
    empty.orientation = defaultAxesOrientation();
    empty.orientationSet = true;
    viewport_->loadDocument(empty);
    loadingDocument_ = false;
    documentPath_.clear();
    documentModified_ = false;
    updateWindowTitle();
}

void PdfWindow::openDocument() {
    if (!maybeSaveChanges()) return;
    QFileDialog dialog(this, QStringLiteral("Apri"), QFileInfo(documentPath_).absolutePath(),
                       QStringLiteral("Documenti ForgeCAD (*.prt);;Tutti i file (*)"));
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

bool PdfWindow::openDocumentPath(const QString &path) {
    const QString fileName = QFileInfo(path).fileName();
    beginForegroundProgress(QStringLiteral("Lettura di %1... 0%").arg(fileName), 100);
    updateForegroundProgress(QStringLiteral("Lettura del file %1... 5%").arg(fileName), 5);
    DocumentState state;
    const QString error = ForgeCad::loadDocumentFile(path, state);
    if (!error.isEmpty()) {
        endForegroundProgress();
        QMessageBox::warning(this, QStringLiteral("Apri"), error);
        return false;
    }
    updateForegroundProgress(QStringLiteral("Definizione caricata, preparazione della geometria... 15%"), 15);
    // I file senza orientamento (versioni vecchie) prendono quello predefinito.
    if (!state.orientationSet) {
        state.orientation = defaultAxesOrientation();
        state.orientationSet = true;
    }
    loadingDocument_ = true;
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
    documentPath_ = path;
    documentModified_ = false;
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
                                            QStringLiteral("Documenti ForgeCAD (*.prt)"));
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().compare(QLatin1String(ForgeCad::kDocumentSuffix), Qt::CaseInsensitive) != 0)
            path += QStringLiteral(".") + QLatin1String(ForgeCad::kDocumentSuffix);
    }
    beginForegroundProgress(QStringLiteral("Salvataggio di %1...").arg(QFileInfo(path).fileName()));
    const QString error = ForgeCad::saveDocumentFile(path, viewport_->currentDocument(),
                                                     QSettings().value(QStringLiteral("document/saveBodies"), true).toBool());
    endForegroundProgress();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Salva"), error);
        return false;
    }
    documentPath_ = path;
    documentModified_ = false;
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("Salvato %1 (%2 byte)").arg(path).arg(QFileInfo(path).size()), 6000);
    return true;
}

void PdfWindow::closeEvent(QCloseEvent *event) {
    if (maybeSaveChanges()) event->accept();
    else event->ignore();
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
    rebuildingTree_ = true;
    const auto addObject = [this](const QString &name, int type, int index, int checkState, QTreeWidgetItem *parent = nullptr) {
        auto *item = parent ? new QTreeWidgetItem(parent, {name}) : new QTreeWidgetItem(modelTree_, {name});
        item->setData(0, Qt::UserRole, type);
        item->setData(0, Qt::UserRole + 1, index);
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
    QHash<quint64, QTreeWidgetItem *> bodyItems;
    for (int index = 0; index < modelBodies.size(); ++index) {
        const ModelBody &body = modelBodies.at(index);
        QTreeWidgetItem *item = addObject(body.name, kTreeBody, index, body.visible);
        item->setIcon(0, ForgeCad::commandIcon(QStringLiteral("box")));
        item->setExpanded(true);
        item->setToolTip(0, QStringLiteral("Corpo parametrico: le feature sono calcolate dall'alto verso il basso."));
        bodyItems.insert(body.id, item);
    }
    QTreeWidgetItem *referenceRoot = nullptr;
    for (int index = 0; index < extrusions.size(); ++index) {
        const ExtrusionObject &body = extrusions.at(index);
        // Piu' solidi o superfici separati in un corpo (per esempio un'unione di corpi che non si toccano).
        const QPair<int, int> parts = viewport_->bodyComponents(index);
        QString label = body.name;
        if (parts.first > 1) label += QStringLiteral("  (%1 solidi)").arg(parts.first);
        if (parts.second > 1) label += QStringLiteral("  (%1 superfici)").arg(parts.second);
        QTreeWidgetItem *parent = bodyItems.value(body.modelBodyId, nullptr);
        if (!parent) {
            if (!referenceRoot) {
                referenceRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Geometria di riferimento")});
                referenceRoot->setData(0, Qt::UserRole, kTreeInfo);
                referenceRoot->setExpanded(true);
            }
            parent = referenceRoot;
        }
        for (int sketch : viewport_->featureSketches(index)) addSketch(sketch, parent);
        const bool reference = body.modelBodyId == 0;
        const bool tip = !reference && parent && modelBodies.value(parent->data(0, Qt::UserRole + 1).toInt()).tipFeatureId == body.featureId;
        if (tip) label += QStringLiteral("   ◀ risultato");
        QTreeWidgetItem *item = addObject(body.error.isEmpty() ? label : label + QStringLiteral("  \u26A0"),
                                          kTreeExtrusion, index, reference ? body.visible : -1, parent);
        QString icon = body.operation >= 0 ? QStringList{QStringLiteral("union"), QStringLiteral("intersection"), QStringLiteral("difference")}.value(body.operation)
                     : body.feature == BodyFeature::Extrusion ? QStringLiteral("extrude")
                     : body.feature == BodyFeature::Revolution ? QStringLiteral("revolve")
                     : body.feature == BodyFeature::Blend ? (body.blendChamfer ? QStringLiteral("chamfer") : QStringLiteral("fillet"))
                     : body.feature == BodyFeature::SheetTrim ? QStringLiteral("trimSurface")
                     : body.feature == BodyFeature::SheetExtend ? QStringLiteral("extendSurface")
                     : body.feature == BodyFeature::Scale ? QStringLiteral("scale")
                     : body.feature == BodyFeature::Helix ? QStringLiteral("helix")
                     : body.feature == BodyFeature::Sweep ? QStringLiteral("sweep")
                     : body.feature == BodyFeature::Loft ? QStringLiteral("loft")
                     : body.feature == BodyFeature::DatumPlane ? QStringLiteral("datumPlane")
                     : body.feature == BodyFeature::Imported ? QStringLiteral("import")
                     : body.feature == BodyFeature::Transform ? QStringLiteral("move")
                     : body.feature == BodyFeature::Pattern ? QStringLiteral("patternLinear")
                     : body.feature == BodyFeature::Primitive
                           ? QStringList{QStringLiteral("box"), QStringLiteral("cylinder"), QStringLiteral("sphere"),
                                         QStringLiteral("cone"), QStringLiteral("torus")}.value(int(body.primitive.kind))
                           : QString();
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
                QString size = QString::number(body.blendSize);
                if (body.blendChamfer && body.chamferSpec.mode == 1) size += QStringLiteral(" x %1").arg(body.chamferSpec.second);
                if (body.blendChamfer && body.chamferSpec.mode == 2) size += QStringLiteral(" a %1\u00B0").arg(body.chamferSpec.second);
                item->setToolTip(0, QStringLiteral("%1 %2 su %3 spigoli")
                    .arg(body.blendChamfer ? QStringLiteral("Smusso di") : QStringLiteral("Raccordo di raggio"))
                    .arg(size).arg(body.blendEdges.size()));
            } else if (body.feature == BodyFeature::Scale) {
                static const QStringList centers = {QStringLiteral("all'origine"), QStringLiteral("al baricentro"), QStringLiteral("attorno al punto")};
                item->setToolTip(0, QStringLiteral("Scala %1 %2").arg(body.scaleFactor).arg(centers.value(body.scaleCenterMode)));
            } else if (body.feature == BodyFeature::SheetTrim) {
                item->setToolTip(0, QStringLiteral("Superficie tagliata da %1").arg(body.secondBody >= 0 ? extrusions.value(body.secondBody).name
                                                                                                         : planeNames().value(body.trimPlane)));
            } else if (body.feature == BodyFeature::SheetExtend) {
                item->setToolTip(0, QStringLiteral("%1 bordi estesi di %2 (%3)").arg(body.blendEdges.size()).arg(body.blendSize)
                    .arg(body.extendLinear ? QStringLiteral("lineare") : QStringLiteral("stessa superficie")));
            } else if (body.feature == BodyFeature::Helix) {
                double pitch, turns, height;
                ForgeCad::helixDimensions(body.helix, pitch, turns, height);
                item->setToolTip(0, body.helix.spiral ? QStringLiteral("Spirale: passo %1, %2 giri").arg(pitch).arg(turns)
                                                      : QStringLiteral("Elica%1: passo %2, %3 giri, altezza %4%5")
                                                            .arg(body.helix.taper != 0.0 ? QStringLiteral(" conica") : QString())
                                                            .arg(pitch).arg(turns).arg(height)
                                                            .arg(body.helix.leftHanded ? QStringLiteral(", sinistrorsa") : QString()));
            } else if (body.feature == BodyFeature::Sweep) {
                static const QStringList modes = {QStringLiteral("torsione minima"), QStringLiteral("Frenet"), QStringLiteral("orientamento costante")};
                item->setToolTip(0, QStringLiteral("Sweep (%1)").arg(modes.value(body.sweepMode)));
            } else if (body.feature == BodyFeature::Loft) {
                const int guideCount = body.loftGuides.size() + body.loftGuidePaths.size();
                item->setToolTip(0, QStringLiteral("Loft %1: %2 sezioni, %3 guide, estremità G%4/G%5, guide G%6")
                                        .arg(body.loftRuled ? QStringLiteral("rigato") : QStringLiteral("liscio"))
                                        .arg(body.loftSketches.size()).arg(guideCount)
                                        .arg(body.loftRuled ? 0 : body.loftStartContinuity).arg(body.loftRuled ? 0 : body.loftEndContinuity)
                                        .arg(body.loftGuideContinuity));
            } else if (body.feature == BodyFeature::Imported) {
                item->setToolTip(0, QStringLiteral("%1 importato da %2").arg(body.solid ? QStringLiteral("Solido") : QStringLiteral("Superficie"), body.importSource));
            } else if (body.feature == BodyFeature::Pattern) {
                const PatternParameters &p = body.pattern;
                QString tip = p.kind == 0 ? QStringLiteral("Ripetizione lineare: %1 x passo %2").arg(p.count).arg(p.spacing)
                              : p.kind == 1 ? QStringLiteral("Ripetizione circolare: %1 istanze, %2 %3\u00B0").arg(p.count)
                                                  .arg(p.spread ? QStringLiteral("angolo totale") : QStringLiteral("passo")).arg(p.angle)
                                            : QStringLiteral("Specchio%1").arg(p.keepOriginal || p.featureOnly ? QString() : QStringLiteral(" (solo l'immagine)"));
                if (p.kind == 0 && p.count2 > 1) tip += QStringLiteral(", %1 x passo %2 nella seconda direzione").arg(p.count2).arg(p.spacing2);
                if (p.featureOnly) tip += QStringLiteral(" (della funzione)");
                item->setToolTip(0, tip);
            } else if (body.feature == BodyFeature::DatumPlane) {
                const ForgeCad::DatumMode mode = ForgeCad::datumModes().value(body.datum.mode);
                QString tip = QStringLiteral("Piano di costruzione: %1").arg(mode.name.toLower());
                if (mode.distance) tip += QStringLiteral(", distanza %1").arg(body.datum.distance);
                if (mode.angle) tip += QStringLiteral(", angolo %1\u00B0").arg(body.datum.angle);
                item->setToolTip(0, tip);
            } else if (body.feature == BodyFeature::Primitive) {
                item->setToolTip(0, QStringLiteral("Origine (%1, %2, %3), %4")
                    .arg(body.primitive.origin[0]).arg(body.primitive.origin[1]).arg(body.primitive.origin[2])
                    .arg(planeNames().value(body.primitive.plane)));
            }
        }
        // Estrusione fino a un riferimento, oppure estrusione/sweep fusa con altri solidi.
        if (body.operation < 0
            && ((body.feature == BodyFeature::Extrusion && body.extent != 0)
                || ((body.feature == BodyFeature::Extrusion || body.feature == BodyFeature::Sweep)
                    && body.mergeOperation != 0 && !body.mergeBodies.isEmpty()))) {
            QStringList children;
            if (body.extent != 0) children.append(QStringLiteral("Fino a: ") + ForgeCad::geometryRefText(body.extentRef, sketches, extrusions));
            if (body.mergeOperation != 0)
                for (int other : body.mergeBodies)
                    children.append((body.mergeOperation == 1 ? QStringLiteral("Unita a: ") : QStringLiteral("Sottratta da: ")) + extrusions.value(other).name);
            for (const QString &text : children) {
                auto *child = new QTreeWidgetItem(item, {text});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            item->setExpanded(true);
        }
        const bool withChildren = body.feature == BodyFeature::Blend || body.feature == BodyFeature::SheetTrim || body.feature == BodyFeature::SheetExtend
                               || body.feature == BodyFeature::Scale || body.feature == BodyFeature::Helix || body.feature == BodyFeature::Sweep
                               || body.feature == BodyFeature::Loft || body.feature == BodyFeature::DatumPlane || body.feature == BodyFeature::Imported
                               || body.feature == BodyFeature::Pattern || body.feature == BodyFeature::Transform;
        if (body.operation < 0 && withChildren) {
            QStringList children;
            if (body.feature == BodyFeature::DatumPlane) {
                for (const GeometryRef &ref : body.datum.refs) children.append(ForgeCad::geometryRefText(ref, sketches, extrusions));
            } else if (body.feature == BodyFeature::Imported) {
                children.append(QStringLiteral("File: ") + body.importSource);
            } else if (body.feature == BodyFeature::Transform) {
                const TransformParameters &m = body.move;
                children.append((m.copy ? QStringLiteral("Copia di: ") : QStringLiteral("Corpo: ")) + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Traslazione: (%1, %2, %3)").arg(m.translation[0]).arg(m.translation[1]).arg(m.translation[2]));
                if (std::fabs(m.angle) > 0.0)
                    children.append(QStringLiteral("Rotazione: %1\u00B0 attorno a %2").arg(m.angle).arg(ForgeCad::geometryRefText(m.axis, sketches, extrusions)));
            } else if (body.feature == BodyFeature::Pattern) {
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
                children.append(QStringLiteral("Profilo: ") + sketches.value(body.sketchIndex).name);
                const int entities = body.pathSegments.size() + body.pathCurves.size();
                children.append(QStringLiteral("Percorso: ") + (body.sweepPath == 0
                    ? sketches.value(body.pathSketch).name + (entities ? QStringLiteral(" (%1 entità)").arg(entities) : QString())
                    : extrusions.value(body.firstBody).name));
            } else if (body.feature == BodyFeature::Loft) {
                for (int k = 0; k < body.loftSketches.size(); ++k)
                    children.append(QStringLiteral("Sezione %1: %2").arg(k + 1).arg(sketches.value(body.loftSketches.at(k)).name));
                for (int k = 0; k < body.loftGuides.size(); ++k)
                    children.append(QStringLiteral("Guida %1: %2").arg(k + 1).arg(sketches.value(body.loftGuides.at(k)).name));
                for (int k = 0; k < body.loftGuidePaths.size(); ++k) {
                    const SketchPathRef &path = body.loftGuidePaths.at(k);
                    children.append(QStringLiteral("Guida %1: %2 (%3 entità)").arg(body.loftGuides.size() + k + 1)
                        .arg(sketches.value(path.sketch).name).arg(path.segments.size() + path.curves.size()));
                }
            } else {
                const QString role = body.feature == BodyFeature::Blend ? QStringLiteral("Base: ") : body.feature == BodyFeature::Scale ? QStringLiteral("Corpo: ")
                                                                                                                                      : QStringLiteral("Superficie: ");
                children.append(role + extrusions.value(body.firstBody).name);
            }
            if (body.feature == BodyFeature::SheetTrim)
                children.append(QStringLiteral("Strumento: ") + (body.secondBody >= 0 ? extrusions.value(body.secondBody).name : planeNames().value(body.trimPlane)));
            for (const QString &text : children) {
                auto *child = new QTreeWidgetItem(item, {text});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            item->setExpanded(true);
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
            QStringList operands{QStringLiteral("A: ") + firstName, QStringLiteral("B: ") + secondName};
            for (int k = 0; k < body.booleanTools.size(); ++k)
                operands.append(QStringLiteral("%1: ").arg(QChar(u'C' + qMin(k, 23))) + extrusions.value(body.booleanTools.at(k)).name);
            for (const QString &operand : operands) {
                auto *child = new QTreeWidgetItem(item, {operand});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            item->setExpanded(true);
        }
        if (selection.kind == SceneObjectKind::Extrusion && selection.index == index) modelTree_->setCurrentItem(item);
    }
    QTreeWidgetItem *sketchRoot = nullptr;
    for (int index = 0; index < sketches.size(); ++index) {
        if (shownSketches.contains(index)) continue;
        if (!sketchRoot) {
            sketchRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Schizzi non utilizzati")});
            sketchRoot->setData(0, Qt::UserRole, kTreeInfo);
            sketchRoot->setExpanded(true);
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
    if (dark && application) application->setStyleSheet(QStringLiteral("QMainWindow { background: #111820; color: #d8e4ea; }QMenuBar,QToolBar,QStatusBar { background: #1b2730; color: #d8e4ea; }QMenu { background: #202d36; color: #d8e4ea; }"
        "QToolBar { spacing: 1px; padding: 2px 4px; border: none; }"
        "QToolBar::separator { background: #34444f; width: 1px; margin: 5px 3px; }"
        "QToolBar QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 2px; }"
        "QToolBar QToolButton:hover { background: #2a3a46; border-color: #3d5566; }"
        "QToolBar QToolButton:pressed { background: #1f4258; }"
        "QToolBar QToolButton:checked { background: #24495f; border-color: #4f9fd0; }"
        "QToolBar QToolButton[popupMode=\"1\"] { padding-right: 11px; }"
        "QToolBar QToolButton::menu-button { border: none; width: 10px; }"
        "QToolBar QToolButton::menu-button:hover { background: #34505f; border-radius: 3px; }"));
}
