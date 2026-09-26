#include "forgeCad2026_gui.h"
#include "cuda_support.h"
#include "cad_constraints.h"
#include "cad_curve_solver.h"
#include "cad_document_io.h"
#include "cad_export.h"
#include "cad_forge.h"
#include "fk_topology.h"
#include "cad_history.h"
#include "cad_kernel.h"
#include "cad_kernel_lab.h"
#include "cad_sketch_edit.h"
#include "cad_snap.h"

#include <BRepBuilderAPI_Copy.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QThreadPool>
#include <QWheelEvent>
#include <QWindow>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>
#include <memory>
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

class CadViewport final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit CadViewport(QWidget *parent = nullptr) : QOpenGLWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setMinimumSize(640, 420);
    }
    ~CadViewport() override {
        // I framebuffer vanno distrutti con il loro contesto corrente.
        makeCurrent();
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
        tessellationQuality_ = qBound(0, quality, 2);
        for (SketchObject &sketch : sketches_) {
            for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
        }
        for (ExtrusionObject &body : extrusions_) tessellateBody(body);
        update();
    }
    void setWheelZoomEnabled(bool enabled) { wheelZoomEnabled_ = enabled; }
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
        blendFirst_ = -1;
        trimPreview_.clear();
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
    // Lunghezza degli assi in unita' del modello (menu Visualizza): fissa,
    // quindi seguono lo zoom come il resto della scena e non dipendono dagli oggetti.
    double axisLength() const { return axisLength_; }
    void setAxesVisible(bool visible) { axesVisible_ = visible; update(); }
    bool axesVisible() const { return axesVisible_; }
    void setAxesOnTop(bool onTop) { axesOnTop_ = onTop; update(); }
    bool axesOnTop() const { return axesOnTop_; }
    void setOriginSnap(bool enabled) { originSnap_ = enabled; }
    bool originSnap() const { return originSnap_; }

    void setSelectionCallback(std::function<void(SceneSelection)> callback) {
        selectionCallback_ = std::move(callback);
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
    void setKernelLabCallback(std::function<void(const QString &)> callback) {
        kernelLabCallback_ = std::move(callback);
    }

    // Kernel geometrico dei corpi (menu Opzioni). Cambiandolo si rigenerano
    // tutti i corpi dalla loro definizione: non e' una modifica del documento
    // (niente Undo), ma i corpi che il kernel scelto non sa costruire restano
    // con l'errore nell'albero.
    GeometryKernel geometryKernel() const { return geometryKernel_; }
    void setGeometryKernel(GeometryKernel kernel) {
        if (kernel == geometryKernel_) return;
        geometryKernel_ = kernel;
        regenerateAll();
        documentChanged();
    }

    // Menu Debug: l'estrusione selezionata rifatta con il kernel sperimentale e
    // confrontata con quella OCCT. Restituisce un messaggio d'errore o vuoto.
    QString compareSelectedExtrusionWithKernel() {
        if (selection_.kind != SceneObjectKind::Extrusion || selection_.index < 0 || selection_.index >= extrusions_.size())
            return QStringLiteral("Seleziona prima un'estrusione (nella vista o nell'albero modello).");
        const ExtrusionObject &body = extrusions_.at(selection_.index);
        if (body.operation >= 0 || body.feature != BodyFeature::Extrusion) {
            const QString error = kernelLab_.compareDocumentBody(sketches_, occtBodies(), selection_.index);
            kernelLabProbe_ = {};
            if (error.isEmpty() && kernelLabCallback_) kernelLabCallback_(kernelLab_.summary());
            update();
            return error;
        }
        if (body.sketchIndex < 0 || body.sketchIndex >= sketches_.size())
            return QStringLiteral("Lo schizzo dell'estrusione non esiste piu'.");
        const QString error = kernelLab_.compareSketchExtrusion(sketches_.at(body.sketchIndex), body.distance,
                                                                occtBodies().at(selection_.index).shape);
        kernelLabProbe_ = {};
        if (error.isEmpty() && kernelLabCallback_) kernelLabCallback_(kernelLab_.summary());
        update();
        return error;
    }

    // Menu Debug: superficie del kernel sperimentale confrontata con OCCT.
    void setKernelLabShape(ForgeCad::KernelLabShape shape) {
        kernelLab_.setShape(shape);
        kernelLabProbe_ = {};
        if (kernelLabCallback_) {
            if (shape == ForgeCad::KernelLabShape::None) kernelLabCallback_(QString());
            else if (!kernelLab_.summary().isEmpty()) kernelLabCallback_(kernelLab_.summary());
            else kernelLabCallback_(QStringLiteral("Kernel sperimentale: %1 - muovi il mouse sulla vista").arg(kernelLab_.shapeName()));
        }
        update();
    }

    int activeSketchIndex() const { return activeSketch_; }
    // Documento corrente (per il salvataggio) e apertura di un documento: la
    // cronologia riparte da zero e i corpi si rigenerano dalla definizione.
    DocumentState currentDocument() const { return documentState(); }
    void loadDocument(DocumentState state) {
        if (sketchMode_) endSketchMode();
        activeSketch_ = -1;
        selection_ = {};
        history_.clear();
        if (state.orientationSet) orientation_ = state.orientation;
        sketches_ = std::move(state.sketches);
        extrusions_ = std::move(state.extrusions);
        for (SketchObject &sketch : sketches_)
            for (CurveObject &curve : sketch.curves) {
                if (curve.tool == DrawingTool::Spline && curve.tangentHandles.size() != curve.controlPoints.size())
                    ForgeCad::initializeTangentHandles(curve);
                ForgeCad::recalculateCurve(curve, tessellationQuality_);
            }
        regenerateAll();
        restoreDocument(documentState());
        fitAll();
    }
    // Corpi da esportare: quelli visibili con una geometria valida (gli
    // operandi delle booleane e le basi dei raccordi sono nascosti). Se il
    // kernel attivo e' quello proprio, le forme OCCT si rigenerano dalla
    // definizione (i traduttori STEP/IGES sono di OpenCASCADE) e il documento
    // torna poi com'era.
    QVector<ForgeCad::ExportBody> exportableBodies() {
        QVector<ForgeCad::ExportBody> result;
        const auto collect = [this, &result] {
            for (const ExtrusionObject &body : extrusions_)
                if (body.visible && body.error.isEmpty() && !body.shape.IsNull()) result.append({body.name, body.shape});
        };
        if (geometryKernel_ == GeometryKernel::OpenCascade) {
            collect();
            return result;
        }
        const QVector<ExtrusionObject> saved = extrusions_;
        geometryKernel_ = GeometryKernel::OpenCascade;
        regenerateAll();
        collect();
        geometryKernel_ = GeometryKernel::Forge;
        extrusions_ = saved;
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

    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void undo() {
        DocumentState state = documentState();
        if (history_.undo(state)) restoreDocument(std::move(state));
    }
    void redo() {
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
    QString createBoolean(BooleanOperation operation, int firstIndex, int secondIndex, const QString &name) {
        if (firstIndex < 0 || secondIndex < 0 || firstIndex >= extrusions_.size()
            || secondIndex >= extrusions_.size())
            return QStringLiteral("Oggetti non validi.");
        if (firstIndex == secondIndex) return QStringLiteral("Scegli due oggetti diversi.");
        const ExtrusionObject &first = extrusions_.at(firstIndex);
        const QString invalid = booleanOperandsError(operation, first, extrusions_.at(secondIndex));
        if (!invalid.isEmpty()) return invalid;
        ExtrusionObject result;
        result.name = name;
        result.plane = first.plane;
        result.operation = int(operation);
        result.firstBody = firstIndex;
        result.secondBody = secondIndex;
        rebuildBody(result, int(extrusions_.size()));
        if (!hasGeometry(result)) return result.error;
        recordUndo();
        extrusions_[firstIndex].visible = false;
        extrusions_[secondIndex].visible = false;
        extrusions_.append(result);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
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
    // riferimenti a schizzi e corpi): si ricostruisce con il kernel attivo e,
    // se riesce, prende il posto del vecchio e si rigenerano i corpi che ne
    // dipendono (un passo di Undo). Restituisce l'errore.
    QString updateBody(int index, const ExtrusionObject &definition) {
        if (index < 0 || index >= extrusions_.size()) return QStringLiteral("Corpo non valido.");
        if (definition.operation >= 0) {
            if (definition.firstBody < 0 || definition.secondBody < 0 || definition.firstBody >= index || definition.secondBody >= index)
                return QStringLiteral("Operandi della booleana non validi.");
            const QString invalid = booleanOperandsError(BooleanOperation(definition.operation), extrusions_.at(definition.firstBody),
                                                         extrusions_.at(definition.secondBody));
            if (!invalid.isEmpty()) return invalid;
        }
        ExtrusionObject candidate = definition;
        candidate.visible = extrusions_.at(index).visible;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        rebuildBody(candidate, index);
        QApplication::restoreOverrideCursor();
        if (!hasGeometry(candidate)) return candidate.error;
        recordUndo();
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
        auto *lengthBox = new QDoubleSpinBox(&dialog);
        lengthBox->setDecimals(6);
        lengthBox->setRange(1e-6, 100000.0);
        lengthBox->setValue(pointLength(delta));
        auto *angleBox = new QDoubleSpinBox(&dialog);
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

    // --- Eliminazione -----------------------------------------------------------

    // Corpi che dipendono (anche a cascata) dai corpi `removed`, compresi.
    QSet<int> withDependentBodies(QSet<int> removed) const {
        for (int index = 0; index < extrusions_.size(); ++index) {
            const ExtrusionObject &body = extrusions_.at(index);
            const bool depends = body.operation >= 0 ? removed.contains(body.firstBody) || removed.contains(body.secondBody)
                                                     : body.feature == BodyFeature::Blend && removed.contains(body.firstBody);
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
        if (selection_.kind == SceneObjectKind::Sketch || selection_.kind == SceneObjectKind::Extrusion)
            deleteObject(selection_.kind, selection_.index);
    }

    void deleteObject(SceneObjectKind kind, int index) {
        QSet<int> bodies;
        QString name;
        if (kind == SceneObjectKind::Sketch) {
            if (index < 0 || index >= sketches_.size()) return;
            name = sketches_.at(index).name;
            for (int body = 0; body < extrusions_.size(); ++body)
                if (extrusions_.at(body).operation < 0 && extrusions_.at(body).feature != BodyFeature::Blend
                    && extrusions_.at(body).feature != BodyFeature::Primitive && extrusions_.at(body).sketchIndex == index)
                    bodies.insert(body);
            bodies = withDependentBodies(bodies);
        } else if (kind == SceneObjectKind::Extrusion) {
            if (index < 0 || index >= extrusions_.size()) return;
            name = extrusions_.at(index).name;
            bodies = withDependentBodies({index});
            bodies.remove(index);
        } else {
            return;
        }
        if (!bodies.isEmpty()) {
            QList<int> sorted(bodies.begin(), bodies.end());
            std::sort(sorted.begin(), sorted.end());
            QStringList names;
            for (int body : sorted) names.append(QStringLiteral("  \u2022 ") + extrusions_.at(body).name);
            const auto answer = QMessageBox::question(this, QStringLiteral("Elimina"),
                QStringLiteral("Da \"%1\" dipendono:\n%2\n\nEliminare anche questi?").arg(name, names.join(QLatin1Char('\n'))),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) return;
        }
        if (kind == SceneObjectKind::Sketch && sketchMode_ && activeSketch_ == index) endSketchMode();
        recordUndo();
        if (kind == SceneObjectKind::Extrusion) bodies.insert(index);
        removeBodies(bodies);
        if (kind == SceneObjectKind::Sketch) {
            sketches_.removeAt(index);
            for (ExtrusionObject &body : extrusions_)
                if (body.sketchIndex > index) --body.sketchIndex;
                else if (body.sketchIndex == index) body.sketchIndex = -1;
            if (activeSketch_ == index) activeSketch_ = -1;
            else if (activeSketch_ > index) --activeSketch_;
        }
        selection_ = {};
        hover_ = {};
        documentChanged();
    }

    // Menu contestuale della vista: "Modifica parametri..." di un corpo.
    void setEditBodyCallback(std::function<void(int)> callback) { editBodyCallback_ = std::move(callback); }

    // Estrusione esatta dei profili dello schizzo attivo. Restituisce l'errore.
    QString createExtrusion(double distance, const QString &name) {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size())
            return QStringLiteral("Nessuno schizzo attivo.");
        ExtrusionObject extrusion;
        extrusion.name = name;
        extrusion.sketchIndex = activeSketch_;
        extrusion.plane = sketches_.at(activeSketch_).plane;
        extrusion.distance = distance;
        rebuildBody(extrusion, int(extrusions_.size()));
        if (!hasGeometry(extrusion)) return extrusion.error;
        recordUndo();
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
    QString beginEdgePick(bool chamfer) {
        edgePickEdit_ = -1;
        return startEdgePick(chamfer);
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
        if (edgePickBody_ >= 0) edgePicked();
    }
    double edgePickSize() const { return edgePickSize_; }

    // Anteprima di un corpo definito da `definition` (una funzione nuova, o al
    // posto del corpo `index` se si modifica), calcolata in background con il
    // kernel attivo; le richieste superate si scartano. Si disegna in ambra; al
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
        if (definition.operation >= 0) preview_.replaced = {definition.firstBody, definition.secondBody};
        else if (definition.feature == BodyFeature::Blend) preview_.replaced = {definition.firstBody};
        preview_.valid = false;
        preview_.error.clear();
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
    void requestBlendPreview(int base, const QVector<EdgePoint> &edges, double size, bool chamfer, int hidden = -1) {
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
        requestPreview(blend, hidden);
    }
    void clearPreview() {
        ++preview_.generation;
        preview_.key.clear();
        preview_.index = -1;
        preview_.replaced.clear();
        preview_.valid = false;
        preview_.error.clear();
        preview_.display = {};
        update();
    }
    void clearBlendPreview() { clearPreview(); }
    // Errore dell'anteprima (vuoto se riuscita o in calcolo), anche a ogni risultato nuovo.
    QString previewError() const { return preview_.error; }
    void setPreviewCallback(std::function<void(const QString &)> callback) { previewCallback_ = std::move(callback); }

    QString startEdgePick(bool chamfer) {
        if (sketchMode_) return QStringLiteral("Esci prima dalla modalita' schizzo.");
        if (selection_.kind != SceneObjectKind::Extrusion || selection_.index < 0 || selection_.index >= extrusions_.size())
            return QStringLiteral("Seleziona prima il corpo (nella vista o nell'albero modello).");
        const ExtrusionObject &body = extrusions_.at(selection_.index);
        if (!hasGeometry(body) || !body.solid) return QStringLiteral("Raccordi e smussi si fanno sui solidi.");
        edgePickBody_ = selection_.index;
        edgePickChamfer_ = chamfer;
        pickedEdges_.clear();
        hoverEdge_ = -1;
        hoverFaceEdges_.clear();
        // Con una faccia selezionata del corpo si parte dai suoi bordi.
        if (selectedFace_.body == edgePickBody_) pickedEdges_ = faceDisplayEdges(edgePickBody_, selectedFace_.hit);
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

    QString createBlend(int baseIndex, const QVector<EdgePoint> &edges, double size, bool chamfer, const QString &name) {
        if (baseIndex < 0 || baseIndex >= extrusions_.size()) return QStringLiteral("Corpo non valido.");
        ExtrusionObject blend;
        blend.name = name;
        blend.feature = BodyFeature::Blend;
        blend.plane = extrusions_.at(baseIndex).plane;
        blend.firstBody = baseIndex;
        blend.blendChamfer = chamfer;
        blend.blendSize = size;
        blend.blendEdges = edges;
        rebuildBody(blend, int(extrusions_.size()));
        if (!hasGeometry(blend)) return blend.error;
        recordUndo();
        extrusions_[baseIndex].visible = false;
        extrusions_.append(blend);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
        return {};
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
            } else if (element.kind == 1 && element.index >= 0 && element.index < sketch.curves.size()) {
                sketch.curves[element.index].construction = !allConstruction;
            }
        }
        std::sort(sketch.constructionSegments.begin(), sketch.constructionSegments.end());
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
        QString failure;
        if (!solveActive({}, before, &failure)) return failure;
        history_.record(snapshot);
        selectedConstraints_ = {int(sketch.geometricConstraints.size()) - 1};
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
        for (int k = indices.size() - 1; k >= 0; --k)
            if (indices.at(k) >= 0 && indices.at(k) < sketch.geometricConstraints.size()) sketch.geometricConstraints.removeAt(indices.at(k));
        selectedConstraints_.clear();
        constraintHover_ = -1;
        sketchEdited();
        selectionChanged();
        return {};
    }
    // Nuovo valore di una quota: lo schizzo si adatta (se i vincoli lo permettono).
    QString setConstraintValue(int index, double value) {
        if (!activeSketchObject() || index < 0 || index >= activeSketchObject()->geometricConstraints.size()) return QStringLiteral("Vincolo non valido.");
        if (!ForgeCad::isDimension(activeSketchObject()->geometricConstraints.at(index).type)) return QStringLiteral("Il vincolo non ha un valore.");
        const DocumentState snapshot = documentState();
        SketchObject &sketch = sketches_[activeSketch_];
        const SketchObject before = sketch;
        sketch.geometricConstraints[index].value = value;
        QString failure;
        if (!solveActive({}, before, &failure)) return failure;
        history_.record(snapshot);
        sketchEdited();
        selectionChanged();
        return {};
    }
    // Finestra del valore di una quota: resta aperta (si riapre) finche' il valore non va bene o si annulla.
    void editConstraintValue(int index) {
        const SketchObject *active = activeSketchObject();
        if (!active || index < 0 || index >= active->geometricConstraints.size()) return;
        const SketchConstraint constraint = active->geometricConstraints.at(index);
        if (!ForgeCad::isDimension(constraint.type)) return;
        double value = constraint.value;
        for (;;) {
            bool accepted = false;
            value = QInputDialog::getDouble(this, ForgeCad::constraintName(constraint.type), ForgeCad::describeConstraint(*activeSketchObject(), constraint),
                                            value, constraint.type == ConstraintType::Angle ? -360.0 : 1e-9, 1e6, 6, &accepted);
            if (!accepted) return;
            const QString error = setConstraintValue(index, value);
            if (error.isEmpty()) return;
            QMessageBox::warning(this, ForgeCad::constraintName(constraint.type), error);
        }
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
        if (edgePickBody_ >= 0) cancelEdgePick();
        SketchObject sketch;
        sketch.name = name;
        sketch.plane = kFacePlane;
        sketch.frame = ForgeCad::faceSketchFrame(gp_Pnt(face.point[0], face.point[1], face.point[2]),
                                                 gp_Dir(face.normal[0], face.normal[1], face.normal[2]),
                                                 gp_Dir(orientation_.up[0], orientation_.up[1], orientation_.up[2]));
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
            const gp_Ax3 axes = ForgeCad::sketchAxes(sketches_.at(activeSketch_));
            setViewFrame(QVector3D(float(axes.XDirection().X()), float(axes.XDirection().Y()), float(axes.XDirection().Z())),
                         QVector3D(float(axes.Direction().X()), float(axes.Direction().Y()), float(axes.Direction().Z())));
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
    }

    void resizeGL(int width, int height) override {
        glViewport(0, 0, width, height);
        if (autoFit_ && painted_ && !sketchMode_) fitView(sceneGeometryPoints());
    }

    void paintGL() override {
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
        drawGrid();
        if (!axesOnTop_) drawAxes();
        configureLighting();
        drawExtrusions();
        drawPickedEdges();
        drawKernelLab();
        drawSketch();
        drawSnapMarkers();
        if (axesOnTop_) drawAxes();
        if (scene) {
            scene->release();
            QOpenGLFramebufferObject::blitFramebuffer(resolveBuffer_.get(), msaaBuffer_.get());
            QOpenGLFramebufferObject::blitFramebuffer(nullptr, QRect(QPoint(), pixels), resolveBuffer_.get(), QRect(QPoint(), pixels));
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        }
        drawReferenceLabels();
        drawSelectionHighlight();
        drawKernelLabOverlay();
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == panKey_) {
            panKeyHeld_ = true;
            setCursor(Qt::OpenHandCursor);
            return;
        }
        if (edgePickBody_ >= 0) {
            if (event->key() == Qt::Key_Escape) {
                cancelEdgePick();
                return;
            }
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                finishEdgePick();
                return;
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
        if (!sketchMode_ && edgePickBody_ >= 0 && event->button() == Qt::LeftButton) {
            // Scelta degli spigoli per raccordo/smusso: il clic li accende o li spegne.
            // Clic su una faccia: tutti i suoi bordi (o nessuno, se c'erano gia' tutti).
            const int edge = pickEdge(lastMousePosition_);
            FaceHit face;
            if (edge >= 0) {
                if (pickedEdges_.contains(edge)) pickedEdges_.removeAll(edge);
                else pickedEdges_.append(edge);
            } else if (pickBodyFace(edgePickBody_, lastMousePosition_, face)) {
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
            update();
            return;
        }
        if (sketchMode_ && event->button() == Qt::LeftButton) {
            const QPointF rawPoint = screenToSketchPoint(lastMousePosition_);
            if (isEditTool()) {
                applyEditTool(rawPoint);
                return;
            }
            if (drawingTool_ == DrawingTool::Select) {
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
                // Selezione: Ctrl+clic su punti ed elementi come prima, il
                // trascinamento muove i punti delle curve, il clic seleziona
                // (Maiusc aggiunge o toglie).
                // Un estremo di segmento (anche comune a piu' entita') si
                // trascina con tutti i punti coincidenti; l'interno di un
                // segmento lo seleziona e, trascinando, lo sposta.
                if (event->modifiers() & Qt::ControlModifier) {
                    if (!selectPointWithControl(rawPoint)) selectSketchElement(rawPoint, true);
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
        const QPointF ends[2] = {sketch.segments.at(segment).first, sketch.segments.at(segment).second};
        const double tolerance = ForgeCad::kSketchConnectionTolerance;
        const auto add = [&](ConstraintType type, const ConstraintRef &a, const ConstraintRef &b) {
            SketchConstraint c;
            c.type = type;
            c.first = a;
            c.second = b;
            sketch.geometricConstraints.append(c);
        };
        for (int end = 0; end < 2; ++end) {
            const ConstraintRef here{0, segment, end};
            bool onPoint = false;
            for (int other = 0; other < sketch.segments.size(); ++other) {
                if (other == segment) continue;
                const QPointF points[2] = {sketch.segments.at(other).first, sketch.segments.at(other).second};
                for (int k = 0; k < 2; ++k)
                    if (pointDistance(points[k], ends[end]) <= tolerance) {
                        add(ConstraintType::Coincident, here, {0, other, k});
                        onPoint = true;
                    }
            }
            for (int curve = 0; curve < sketch.curves.size(); ++curve)
                for (int k = 0; k < sketch.curves.at(curve).controlPoints.size(); ++k)
                    if (pointDistance(sketch.curves.at(curve).controlPoints.at(k), ends[end]) <= tolerance) {
                        add(ConstraintType::Coincident, here, {1, curve, k});
                        onPoint = true;
                    }
            if (onPoint) continue;
            for (int other = 0; other < sketch.segments.size(); ++other) {
                if (other == segment) continue;
                const SketchSegment &s = sketch.segments.at(other);
                if (distanceToSegment(ends[end], s.first, s.second) > 1e-9) continue;
                add(pointDistance(ends[end], 0.5 * (s.first + s.second)) <= tolerance ? ConstraintType::Midpoint : ConstraintType::PointOnCurve,
                    here, {0, other, -1});
            }
            for (int curve = 0; curve < sketch.curves.size(); ++curve) {
                const CurveObject &c = sketch.curves.at(curve);
                if (c.construction && c.tool == DrawingTool::Polygon) continue;
                if ((c.tool == DrawingTool::Circle || c.tool == DrawingTool::Arc) && c.controlPoints.size() >= 2
                    && std::abs(pointDistance(ends[end], c.controlPoints.at(0)) - pointDistance(c.controlPoints.at(1), c.controlPoints.at(0))) <= 1e-9)
                    add(ConstraintType::PointOnCurve, here, {1, curve, -1});
            }
        }
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
                for (const Handle(Geom2d_Curve) &piece : ForgeCad::curveGeometry(curve))
                    length += GCPnts_AbscissaPoint::Length(Geom2dAdaptor_Curve(piece));
            } catch (const Standard_Failure &) {
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
            auto *box = new QDoubleSpinBox(&dialog);
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
            auto *box = new QDoubleSpinBox(&dialog);
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
        if (sketchMode_ && isEditTool()) {
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
        if (sketchMode_ && (drawingTool_ == DrawingTool::Spline || drawingTool_ == DrawingTool::Nurbs)) {
            finalizeCurve();
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
                // Faccia sotto il puntatore: schizzo sul suo piano, raccordo o smusso dei suoi bordi.
                FaceHit face;
                const bool onFace = hit.kind == SceneObjectKind::Extrusion && pickBodyFace(hit.index, event->pos(), face);
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
        if (sketchMode_) {
            lastMousePosition_ = currentPosition;
            const QPointF rawPoint = screenToSketchPoint(currentPosition);
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
                    sketch.geometricConstraints[dimensionDrag_].placement = rawPoint;
                    sketch.geometricConstraints[dimensionDrag_].placed = true;
                }
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
                } else if (draggingPointKind_ == EditablePointKind::TangentIn) {
                    curve.tangentHandles[draggingControlIndex_].first = rawPoint;
                } else {
                    curve.tangentHandles[draggingControlIndex_].second = rawPoint;
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
        if (!(event->buttons() & Qt::LeftButton) && kernelLab_.shape() != ForgeCad::KernelLabShape::None)
            probeKernelLab(currentPosition);
        if (edgePickBody_ >= 0 && !(event->buttons() & Qt::LeftButton)) {
            const int edge = pickEdge(currentPosition);
            // Senza uno spigolo vicino si evidenziano i bordi della faccia sotto il puntatore.
            QVector<int> faceEdges;
            FaceHit face;
            if (edge < 0 && pickBodyFace(edgePickBody_, currentPosition, face)) faceEdges = faceDisplayEdges(edgePickBody_, face);
            if (edge != hoverEdge_ || faceEdges != hoverFaceEdges_) {
                hoverEdge_ = edge;
                hoverFaceEdges_ = faceEdges;
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
            yaw_ += delta.x() * 0.5f;
            pitch_ = qBound(-89.0f, pitch_ + delta.y() * 0.5f, 89.0f);
            roll_ *= 0.9f;  // la vista inclinata di uno schizzo su faccia si raddrizza ruotando
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
        if (panning_) {
            panning_ = false;
            if (panKeyHeld_) setCursor(Qt::OpenHandCursor);
            else unsetCursor();
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
    DocumentState documentState() const { return {sketches_, extrusions_, orientation_, true}; }

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
        case ConstraintType::Radius: return QStringLiteral("R") + number(c.value);
        case ConstraintType::Diameter: return QStringLiteral("\u2300") + number(c.value);
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
        const SketchConstraint &c = sketch.geometricConstraints.at(index);
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
        if (c.type == ConstraintType::Distance) {
            QPointF p, q;
            if (!ForgeCad::dimensionPoints(sketch, c, p, q)) return false;
            const double length = pointDistance(p, q);
            if (length <= 1e-12) return false;
            const QPointF u = (q - p) / length;
            QPointF n(-u.y(), u.x());
            double offset = 30.0 * px, slide = 0.5;
            if (!c.placed) {
                // Di default la quota sta fuori: dalla parte opposta al centro dello schizzo.
                QPointF center;
                int count = 0;
                for (const SketchSegment &segment : sketch.segments) center += segment.first + segment.second, count += 2;
                for (const CurveObject &curve : sketch.curves)
                    for (const QPointF &point : curve.controlPoints) center += point, ++count;
                if (count > 0) center /= double(count);
                const QPointF away = 0.5 * (p + q) - center;
                if (away.x() * n.x() + away.y() * n.y() < 0.0) offset = -offset;
            }
            if (c.placed) {
                const QPointF r = c.placement - p;
                offset = r.x() * n.x() + r.y() * n.y();
                slide = (r.x() * u.x() + r.y() * u.y()) / length;
            }
            const double side = offset >= 0.0 ? 1.0 : -1.0;
            const QPointF p2 = p + offset * n, q2 = q + offset * n;
            // Linee di riferimento: dal punto (con un piccolo stacco) a poco oltre la linea di misura.
            g.lines.moveTo(screen(p + side * 3.0 * px * n));
            g.lines.lineTo(screen(p2 + side * 6.0 * px * n));
            g.lines.moveTo(screen(q + side * 3.0 * px * n));
            g.lines.lineTo(screen(q2 + side * 6.0 * px * n));
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
        for (int index = 0; index < sketch->geometricConstraints.size(); ++index) {
            DimensionGraphic g;
            if (!dimensionGraphic(*sketch, index, g)) continue;
            const bool selected = selectedConstraints_.contains(index), hovered = index == constraintHover_;
            const bool broken = ForgeCad::constraintError(*sketch, sketch->geometricConstraints.at(index)) > 1e-7;
            const QColor color = selected ? kSelectionColor : hovered ? kHoverColor : broken ? QColor(255, 140, 90) : QColor(185, 215, 240);
            painter.setPen(QPen(color, selected ? 1.6 : 1.1));
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
            if (c.type == ConstraintType::Angle) text += QStringLiteral(" %1\u00B0").arg(c.value, 0, 'f', 2);
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
        for (const ConstraintRef &ref : {c.first, c.second}) {
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
        sceneBoundsDirty_ = true;
        analysisDirty_ = true;
        if (constraintPanelCallback_) constraintPanelCallback_();
        selectedFace_ = {};  // la geometria (e la numerazione delle facce) puo' essere cambiata
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
        QVector<bool> dirty(extrusions_.size(), false);
        for (int index = 0; index < extrusions_.size(); ++index) {
            ExtrusionObject &body = extrusions_[index];
            if (body.operation < 0 && body.feature == BodyFeature::Blend) {
                if (!(body.firstBody >= 0 && body.firstBody < index && dirty.at(body.firstBody))) continue;
            } else if (body.operation < 0) {
                if (body.sketchIndex != sketchIndex) continue;
            } else {
                const bool firstDirty = body.firstBody >= 0 && body.firstBody < index && dirty.at(body.firstBody);
                const bool secondDirty = body.secondBody >= 0 && body.secondBody < index && dirty.at(body.secondBody);
                if (!firstDirty && !secondDirty) continue;
            }
            rebuildBody(body, index);
            dirty[index] = true;
        }
    }

    // Toglie i corpi (gli operandi di booleane e raccordi eliminati che restano
    // tornano visibili) e rinumera i riferimenti.
    void removeBodies(const QSet<int> &removed) {
        QVector<int> map(extrusions_.size(), -1);
        QVector<ExtrusionObject> kept;
        for (int index = 0; index < extrusions_.size(); ++index) {
            if (removed.contains(index)) {
                const ExtrusionObject &body = extrusions_.at(index);
                for (int operand : {body.firstBody, body.secondBody})
                    if (operand >= 0 && operand < extrusions_.size() && !removed.contains(operand)) extrusions_[operand].visible = true;
                continue;
            }
            map[index] = kept.size();
            kept.append(extrusions_.at(index));
        }
        for (ExtrusionObject &body : kept) {
            if (body.firstBody >= 0) body.firstBody = map.value(body.firstBody, -1);
            if (body.secondBody >= 0) body.secondBody = map.value(body.secondBody, -1);
        }
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
        remapRevolutionAxes(activeSketch_, ForgeCad::removeSketchEntities(sketches_[activeSketch_], segments, curves));
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
                for (QPointF &point : sketch.curves[curve].controlPoints)
                    if (pointDistance(point, move.from) <= tolerance) {
                        updates.append({&point, point + move.delta});
                        movedCurves.insert(curve);
                    }
        }
        for (const auto &[point, value] : updates) *point = value;
        // Solo le spline che si sono mosse ricalcolano le maniglie (le altre restano esatte).
        for (int curve : movedCurves)
            if (sketch.curves.at(curve).tool == DrawingTool::Spline) ForgeCad::initializeTangentHandles(sketch.curves[curve]);
    }

    // I corpi che dipendono (anche a cascata) dal corpo `changed`, gia' rigenerato.
    void regenerateAfter(int changed) {
        QVector<bool> dirty(extrusions_.size(), false);
        if (changed >= 0 && changed < dirty.size()) dirty[changed] = true;
        for (int index = changed + 1; index < extrusions_.size(); ++index) {
            ExtrusionObject &body = extrusions_[index];
            const auto isDirty = [&](int operand) { return operand >= 0 && operand < index && dirty.at(operand); };
            const bool depends = body.operation >= 0 ? isDirty(body.firstBody) || isDirty(body.secondBody)
                                                     : body.feature == BodyFeature::Blend && isDirty(body.firstBody);
            if (!depends) continue;
            rebuildBody(body, index);
            dirty[index] = true;
        }
    }

    // Tutti i corpi, in ordine (dopo un cambio di kernel).
    void regenerateAll() {
        for (int index = 0; index < extrusions_.size(); ++index) rebuildBody(extrusions_[index], index);
    }

    static bool hasGeometry(const ExtrusionObject &body) {
        return body.kernel == GeometryKernel::Forge ? body.forgeBody != nullptr : !body.shape.IsNull();
    }

    // Geometria esatta del corpo `index` (estrusione o booleana) con il kernel
    // attivo, poi la sua tassellazione. Gli operandi di una booleana hanno
    // indice minore e sono gia' rigenerati. In caso d'errore il corpo resta
    // senza geometria con il messaggio in `error`.
    void rebuildBody(ExtrusionObject &body, int index) {
        body.error.clear();
        body.shape.Nullify();
        body.forgeBody.reset();
        body.solid = false;
        body.kernel = geometryKernel_;
        const bool forge = geometryKernel_ == GeometryKernel::Forge;
        if (body.operation < 0 && body.feature == BodyFeature::Blend) {
            if (body.firstBody < 0 || body.firstBody >= index) {
                body.error = QStringLiteral("Il corpo da raccordare non esiste piu'.");
            } else {
                const ExtrusionObject &base = extrusions_.at(body.firstBody);
                if (forge) body.forgeBody = ForgeCad::forgeBlend(base.forgeBody, body.blendEdges, body.blendSize, body.blendChamfer, &body.error);
                else body.shape = ForgeCad::buildBlend(base.shape, body.blendEdges, body.blendSize, body.blendChamfer, &body.error);
                body.solid = hasGeometry(body);
            }
        } else if (body.operation < 0 && body.feature == BodyFeature::Primitive) {
            if (forge) body.forgeBody = ForgeCad::forgePrimitive(body.primitive, &body.error);
            else body.shape = ForgeCad::buildPrimitive(body.primitive, &body.error);
            body.solid = hasGeometry(body);
        } else if (body.operation < 0) {
            if (body.sketchIndex < 0 || body.sketchIndex >= sketches_.size()) {
                body.error = QStringLiteral("Lo schizzo del corpo non esiste piu'.");
            } else if (body.feature == BodyFeature::Revolution) {
                const SketchObject &sketch = sketches_.at(body.sketchIndex);
                if (forge) body.forgeBody = ForgeCad::forgeRevolution(sketch, body.revolveAxis, body.revolveAngle, &body.error);
                else body.shape = ForgeCad::buildRevolution(sketch, body.revolveAxis, body.revolveAngle, &body.error);
                body.solid = hasGeometry(body);
            } else if (forge) {
                body.forgeBody = ForgeCad::forgeExtrusion(sketches_.at(body.sketchIndex), body.distance, &body.error);
                body.solid = body.forgeBody && !body.forgeBody->isSheet();
            } else {
                body.shape = ForgeCad::buildExtrusion(sketches_.at(body.sketchIndex), body.distance, body.solid, &body.error);
            }
        } else if (body.firstBody < 0 || body.secondBody < 0 || body.firstBody >= index || body.secondBody >= index) {
            body.error = QStringLiteral("Operandi della booleana non validi.");
        } else {
            const ExtrusionObject &first = extrusions_.at(body.firstBody), &second = extrusions_.at(body.secondBody);
            if (forge) {
                body.forgeBody = ForgeCad::forgeBoolean(first.forgeBody, second.forgeBody, BooleanOperation(body.operation), &body.error);
                body.solid = body.forgeBody && !body.forgeBody->isSheet();
            } else {
                body.shape = ForgeCad::booleanOperation(first.shape, second.shape, BooleanOperation(body.operation), &body.error);
                body.solid = ForgeCad::isSolidShape(body.shape);
            }
        }
        tessellateBody(body);
    }

    void tessellateBody(ExtrusionObject &body) const {
        if (body.forgeBody) ForgeCad::forgeTessellate(*body.forgeBody, tessellationQuality_, body.display);
        else ForgeCad::tessellate(body.shape, tessellationQuality_, body.display);
    }

    // I corpi del documento con la forma OCCT, rifatta se il kernel attivo e'
    // l'altro (per i confronti del menu Debug).
    QVector<ExtrusionObject> occtBodies() const {
        QVector<ExtrusionObject> bodies = extrusions_;
        if (geometryKernel_ == GeometryKernel::OpenCascade) return bodies;
        for (int index = 0; index < bodies.size(); ++index) {
            ExtrusionObject &body = bodies[index];
            body.shape.Nullify();
            if (body.operation < 0 && body.feature == BodyFeature::Blend) {
                if (body.firstBody >= 0 && body.firstBody < index)
                    body.shape = ForgeCad::buildBlend(bodies.at(body.firstBody).shape, body.blendEdges, body.blendSize, body.blendChamfer, nullptr);
            } else if (body.operation < 0 && body.feature == BodyFeature::Primitive) {
                body.shape = ForgeCad::buildPrimitive(body.primitive, nullptr);
            } else if (body.operation < 0) {
                if (body.sketchIndex >= 0 && body.sketchIndex < sketches_.size()) {
                    const SketchObject &sketch = sketches_.at(body.sketchIndex);
                    if (body.feature == BodyFeature::Revolution)
                        body.shape = ForgeCad::buildRevolution(sketch, body.revolveAxis, body.revolveAngle, nullptr);
                    else
                        body.shape = ForgeCad::buildExtrusion(sketch, body.distance, body.solid, nullptr);
                }
            } else if (body.firstBody >= 0 && body.secondBody >= 0 && body.firstBody < index && body.secondBody < index) {
                body.shape = ForgeCad::booleanOperation(bodies.at(body.firstBody).shape, bodies.at(body.secondBody).shape,
                                                        BooleanOperation(body.operation), nullptr);
            }
        }
        return bodies;
    }

    void restoreDocument(DocumentState state) {
        sketches_ = std::move(state.sketches);
        extrusions_ = std::move(state.extrusions);
        for (SketchObject &sketch : sketches_) {
            for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
        }
        // Le istantanee costruite con l'altro kernel si rigenerano.
        bool otherKernel = false;
        for (const ExtrusionObject &body : extrusions_) otherKernel = otherKernel || body.kernel != geometryKernel_;
        if (otherKernel) {
            regenerateAll();
        } else {
            for (ExtrusionObject &body : extrusions_)
                if (body.display.quality != tessellationQuality_) tessellateBody(body);
        }
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
        if (edgePickBody_ >= 0) cancelEdgePick();
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
        if (selectedPoints_.size() + sketchSelections_.size() >= 2) selectedPoints_.clear();
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
        return point.kind == 1 && point.element < sketch.curves.size() && point.point >= 0
            && point.point < sketch.curves.at(point.element).controlPoints.size();
    }

    QPointF selectedPointPosition(const SelectedPoint &point) const {
        if (point.kind == 2) return QPointF(0.0, 0.0);
        if (point.kind == 0) {
            const auto &segment = sketches_.at(activeSketch_).segments.at(point.element);
            return point.point == 0 ? segment.first : segment.second;
        }
        return sketches_.at(activeSketch_).curves.at(point.element).controlPoints.at(point.point);
    }

    void setSelectedPointPosition(const SelectedPoint &point, const QPointF &position) {
        if (point.kind == 0) {
            auto &segment = sketches_[activeSketch_].segments[point.element];
            if (point.point == 0) segment.first = position;
            else segment.second = position;
        } else {
            sketches_[activeSketch_].curves[point.element].controlPoints[point.point] = position;
            ForgeCad::initializeTangentHandles(sketches_[activeSketch_].curves[point.element]);
        }
    }

    void finalizeCurve() {
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
            sketches_[activeSketch_].curves.append(curve);
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
        const float aspect = float(width()) / float(qMax(1, height()));
        const float viewScale = zoom_ / 8.0f;
        QMatrix4x4 model;
        model.translate(panX_, panY_, -zoom_);
        model.rotate(roll_, 0.0f, 0.0f, 1.0f);
        model.rotate(pitch_, 1.0f, 0.0f, 0.0f);
        model.rotate(yaw_, 0.0f, 1.0f, 0.0f);
        model *= basisMatrix();
        const QVector3D cameraPoint = model.map(point);
        return QPointF((cameraPoint.x() / (4.0f * aspect * viewScale) + 1.0f) * width() * 0.5f,
                   (1.0f - cameraPoint.y() / (4.0f * viewScale)) * height() * 0.5f);
    }

    void drawReferenceLabels() {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        // Lettere degli assi oltre la punta delle frecce.
        painter.setFont(QFont(QStringLiteral("Sans"), 12, QFont::Bold));
        const float tipDistance = float(axisLength()) * 1.1f;
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
        painter.drawText(projectWorldPoint(QVector3D(3.0f, 3.0f, 0.0f)), QStringLiteral("Piano XY"));
        painter.setPen(QColor(150, 240, 190));
        painter.drawText(projectWorldPoint(QVector3D(3.0f, 0.0f, 3.0f)), QStringLiteral("Piano XZ"));
        painter.setPen(QColor(255, 170, 140));
        painter.drawText(projectWorldPoint(QVector3D(0.0f, 3.0f, 3.0f)), QStringLiteral("Piano YZ"));
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
        const double aspect = double(width()) / double(qMax(1, height()));
        const double viewScale = double(zoom_) / 8.0;
        return QPointF((double(position.x()) / double(qMax(1, width())) - 0.5) * 8.0 * aspect * viewScale - double(panX_),
                       (0.5 - double(position.y()) / double(qMax(1, height()))) * 8.0 * viewScale - double(panY_));
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
            for (const Handle(Geom2d_Curve) &side : ForgeCad::curveGeometry(curve)) {
                const gp_Pnt2d start = side->Value(side->FirstParameter());
                points.append(QPointF(start.X(), start.Y()));
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
                for (const Handle(Geom2d_Curve) &piece : ForgeCad::curveGeometry(curve)) {
                    Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(point.x(), point.y()), piece);
                    if (projection.NbPoints() > 0 && projection.LowerDistance() < best) {
                        best = projection.LowerDistance();
                        const gp_Pnt2d p = projection.NearestPoint();
                        result = QPointF(p.X(), p.Y());
                        found = index;
                    }
                }
            } catch (const Standard_Failure &) {
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
        if (result.kind != SceneObjectKind::None) return result;

        // Selezione sulla forma esatta: intersezione del raggio con le facce B-rep.
        QVector3D origin, direction;
        viewRay(position, origin, direction);
        double nearest = std::numeric_limits<double>::max();
        for (int index = 0; index < extrusions_.size(); ++index) {
            const ExtrusionObject &extrusion = extrusions_.at(index);
            if (!extrusion.visible || !hasGeometry(extrusion)) continue;
            double distance = 0.0;
            const bool hit = extrusion.forgeBody ? ForgeCad::forgeIntersectRay(*extrusion.forgeBody, origin, direction, distance)
                                                 : ForgeCad::intersectRay(extrusion.shape, origin, direction, distance);
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
        // I solidi estrusi vengono contornati in OpenGL (drawExtrusionOutline).
    }

    void drawSelectionHighlight() {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (sketchMode_ && activeSketch_ >= 0 && activeSketch_ < sketches_.size()) {
            const SketchObject &sketch = sketches_.at(activeSketch_);
            if (sketchHover_.kind >= 0 && !sketchSelections_.contains(sketchHover_))
                highlightSketchElement(painter, sketch, sketchHover_, kHoverColor);
            for (const SketchElementSelection &selected : sketchSelections_)
                highlightSketchElement(painter, sketch, selected, kSelectionColor);
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
            if (drawingTool_ == DrawingTool::Trim && trimPreview_.size() >= 2) {
                painter.setPen(QPen(QColor(255, 80, 70), 4.0, Qt::SolidLine, Qt::RoundCap));
                painter.drawPolyline(projectSketchPolyline(trimPreview_, sketch).data(), int(trimPreview_.size()));
            }
            drawInferenceTags(painter);
            return;
        }
        if (hover_ != selection_) highlightSceneObject(painter, hover_, kHoverColor);
        highlightSceneObject(painter, selection_, kSelectionColor);
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
            if (distance > 0.0f && qAbs(hit.x()) <= 4.001f && qAbs(hit.y()) <= 4.001f
                && qAbs(hit.z()) <= 4.001f && distance < nearestDistance) {
                nearestDistance = distance;
                selected = plane;
            }
        }
        return selected;
    }

    bool isPlaneShown(int plane) const {
        return referencePlanesVisible_ && plane >= 0 && plane < 3 && planeVisible_[plane];
    }

    static QVector<QVector3D> planeCorners(int plane) {
        if (plane == 0) return {QVector3D(-4, -4, 0), QVector3D(4, -4, 0), QVector3D(4, 4, 0), QVector3D(-4, 4, 0)};
        if (plane == 1) return {QVector3D(-4, 0, -4), QVector3D(4, 0, -4), QVector3D(4, 0, 4), QVector3D(-4, 0, 4)};
        return {QVector3D(0, -4, -4), QVector3D(0, 4, -4), QVector3D(0, 4, 4), QVector3D(0, -4, 4)};
    }

    void drawReferencePlanes() {
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const GLfloat colors[][3] = {{0.20f, 0.55f, 0.95f}, {0.25f, 0.90f, 0.70f}, {0.95f, 0.45f, 0.25f}};
        const GLfloat baseAlpha[] = {0.08f, 0.06f, 0.06f};
        glBegin(GL_QUADS);
        for (int plane = 0; plane < 3; ++plane) {
            if (!isPlaneShown(plane)) continue;
            const bool hovered = hover_ == SceneSelection{SceneObjectKind::Plane, plane, -1};
            float alpha = baseAlpha[plane];
            if (selectedPlane_ == plane) alpha += 0.10f;
            if (hovered) alpha += 0.08f;
            glColor4f(colors[plane][0], colors[plane][1], colors[plane][2], alpha);
            for (const QVector3D &corner : planeCorners(plane)) glVertex3f(corner.x(), corner.y(), corner.z());
        }
        glEnd();
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    void configureLighting() {
        const GLfloat ambient[] = {0.20f, 0.22f, 0.26f, 1.0f};
        const GLfloat keyPosition[] = {5, 7, 10, 1};
        const GLfloat fillPosition[] = {-6, 3, 5, 1};
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
                if (!construction) {
                    if (active && analysis->segmentDefined.value(index)) glColor3f(0.94f, 0.96f, 1.0f);
                    else glColor3f(1.0f, 0.75f, 0.15f);
                }
                const QVector3D first = mapSketchPoint(segment.first, sketch);
                const QVector3D second = mapSketchPoint(segment.second, sketch);
                glBegin(GL_LINES);
                glVertex3f(first.x(), first.y(), first.z());
                glVertex3f(second.x(), second.y(), second.z());
                glEnd();
            }
        }
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
        const double markerSize = pickTolerance(10.0);
        glBegin(GL_LINES);
        if (hasPendingPoint_) {
            const QVector3D world = mapActiveSketchPoint(pendingPoint_);
            glColor3f(1.0f, 0.35f, 0.20f);
            const QVector3D left = mapActiveSketchPoint(pendingPoint_ - QPointF(markerSize, 0.0));
            const QVector3D right = mapActiveSketchPoint(pendingPoint_ + QPointF(markerSize, 0.0));
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
            for (const QPointF &arm : {QPointF(markerSize, 0.0), QPointF(0.0, markerSize)}) {
                const QVector3D a = mapActiveSketchPoint(cursorSketchPoint_ - arm);
                const QVector3D b = mapActiveSketchPoint(cursorSketchPoint_ + arm);
                glVertex3f(a.x(), a.y(), a.z());
                glVertex3f(b.x(), b.y(), b.z());
            }
        }
        glEnd();
    }

    // Solo visualizzazione: triangoli e spigoli ricavati dalla forma esatta.
    void drawExtrusionFaces(const ExtrusionObject &extrusion) { drawDisplayFaces(extrusion.display); }
    static void drawDisplayFaces(const BodyDisplay &display) {
        glBegin(GL_TRIANGLES);
        for (int index = 0; index < display.vertices.size(); ++index) {
            const QVector3D &normal = display.normals.at(index);
            const QVector3D &vertex = display.vertices.at(index);
            glNormal3f(normal.x(), normal.y(), normal.z());
            glVertex3f(vertex.x(), vertex.y(), vertex.z());
        }
        glEnd();
    }

    void drawExtrusionEdges(const ExtrusionObject &extrusion) {
        for (const QVector<QVector3D> &polyline : extrusion.display.edges) {
            glBegin(GL_LINE_STRIP);
            for (const QVector3D &point : polyline) glVertex3f(point.x(), point.y(), point.z());
            glEnd();
        }
    }

    // Bordo di evidenziazione: la sagoma dell'oggetto va nello stencil, poi le
    // sue facce vengono ridisegnate a linee spesse solo fuori dalla sagoma.
    void drawExtrusionOutline(const ExtrusionObject &extrusion, const QColor &color, float width) {
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
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
        QString preview;
        if (!pickedEdges_.isEmpty())
            preview = preview_.valid ? QStringLiteral(" - anteprima") : preview_.error.isEmpty() ? QStringLiteral(" - anteprima in calcolo...")
                                                                                          : QStringLiteral(" - anteprima non riuscita: ") + preview_.error;
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
        if (pickedEdges_.isEmpty()) clearBlendPreview();
        else requestBlendPreview(edgePickBody_, pickedEdgePoints(), edgePickSize_, edgePickChamfer_, edgePickEdit_);
        if (edgePickStatus_) edgePickStatus_(edgePickMessage());
        update();
    }
    void cancelEdgePick(bool keepPreview = false) {
        if (!keepPreview) clearBlendPreview();
        edgePickEdit_ = -1;
        edgePickBody_ = -1;
        pickedEdges_.clear();
        hoverEdge_ = -1;
        hoverFaceEdges_.clear();
        if (edgePickStatus_) edgePickStatus_(QString());
        update();
    }
    void finishEdgePick() {
        const int body = edgePickBody_;
        if (body < 0 || body >= extrusions_.size() || pickedEdges_.isEmpty()) {
            cancelEdgePick();
            return;
        }
        const QVector<EdgePoint> points = pickedEdgePoints();
        const bool chamfer = edgePickChamfer_;
        const int edited = edgePickEdit_;
        // L'anteprima resta per la finestra della misura (che la aggiorna e poi la toglie).
        cancelEdgePick(true);
        if (points.isEmpty()) {
            clearBlendPreview();
            return;
        }
        if (edited >= 0) {
            if (edgeEditFinished_) edgeEditFinished_(edited, points, edgePickSize_, chamfer);
        } else if (edgePickFinished_) {
            edgePickFinished_(body, points, chamfer);
        }
    }
    // Spigolo del corpo in scelta sotto il puntatore (entro 8 pixel), -1 se nessuno.
    int pickEdge(const QPoint &position) const {
        if (edgePickBody_ < 0 || edgePickBody_ >= extrusions_.size()) return -1;
        const QVector<QVector<QVector3D>> &edges = extrusions_.at(edgePickBody_).display.edges;
        int best = -1;
        double nearest = 8.0;
        for (int index = 0; index < edges.size(); ++index) {
            const QVector<QVector3D> &polyline = edges.at(index);
            for (int k = 1; k < polyline.size(); ++k) {
                const double d = distanceToSegment(QPointF(position), projectWorldPoint(polyline.at(k - 1)), projectWorldPoint(polyline.at(k)));
                if (d < nearest) {
                    nearest = d;
                    best = index;
                }
            }
        }
        return best;
    }
    // Faccia del corpo `index` sotto il pixel (geometria esatta).
    bool pickBodyFace(int index, const QPoint &position, FaceHit &hit) const {
        if (index < 0 || index >= extrusions_.size() || !hasGeometry(extrusions_.at(index))) return false;
        QVector3D origin, direction;
        viewRay(position, origin, direction);
        const ExtrusionObject &body = extrusions_.at(index);
        return body.forgeBody ? ForgeCad::forgePickFace(*body.forgeBody, origin, direction, hit)
                              : ForgeCad::pickFace(body.shape, origin, direction, hit);
    }
    // Spigoli visualizzati (indici in display.edges) dei bordi della faccia:
    // per ogni suo spigolo la polilinea piu' vicina al suo punto.
    QVector<int> faceDisplayEdges(int index, const FaceHit &hit) const {
        QVector<int> result;
        if (index < 0 || index >= extrusions_.size()) return result;
        const QVector<QVector<QVector3D>> &edges = extrusions_.at(index).display.edges;
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
        if (edgePickBody_ >= 0 || sketchMode_ || selectedFace_.body < 0 || selectedFace_.body >= extrusions_.size()) return;
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
        if (edgePickBody_ < 0 || edgePickBody_ >= extrusions_.size()) return;
        const QVector<QVector<QVector3D>> &edges = extrusions_.at(edgePickBody_).display.edges;
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        auto draw = [&](int index, const QColor &color, float width) {
            if (index < 0 || index >= edges.size()) return;
            glColor3f(float(color.redF()), float(color.greenF()), float(color.blueF()));
            glLineWidth(width);
            glBegin(GL_LINE_STRIP);
            for (const QVector3D &p : edges.at(index)) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
        };
        for (int index = 0; index < edges.size(); ++index) draw(index, QColor(120, 140, 160), 1.5f);
        for (int index : hoverFaceEdges_) draw(index, kHoverColor, 3.0f);
        if (!pickedEdges_.contains(hoverEdge_)) draw(hoverEdge_, kHoverColor, 4.0f);
        for (int index : pickedEdges_) draw(index, kSelectionColor, 4.0f);
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
    }

    void drawExtrusions() {
        glDisable(GL_CULL_FACE);
        const GLfloat noEmission[] = {0.0f, 0.0f, 0.0f, 1.0f};
        const bool previewing = !preview_.key.isEmpty();
        for (int index = 0; index < extrusions_.size(); ++index) {
            const ExtrusionObject &extrusion = extrusions_.at(index);
            // Con un'anteprima il corpo modificato non si vede; gli operandi (e la base
            // di un raccordo, anche se nascosta) si vedono finche' l'anteprima non e' pronta.
            const bool replaced = previewing && preview_.replaced.contains(index);
            if (previewing && (index == preview_.index || (replaced && preview_.valid))) continue;
            const bool forced = replaced || index == edgePickBody_;
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
                glColor3f(float(color.redF()), float(color.greenF()), float(color.blueF()));
                // Le facce vanno un poco indietro nella profondita': spigoli e
                // schizzi che stanno sulla faccia si vedono senza spostarli.
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1.0f, 2.0f);
                drawExtrusionFaces(extrusion);
                glDisable(GL_POLYGON_OFFSET_FILL);
                glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, noEmission);
            }
            if (displayMode_ != 1) {
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
        if (edgePickBody_ >= 0 || previewing) return;  // nessun contorno di selezione sulla base in scelta
        if (hover_ != selection_) outline(hover_, kHoverColor, 6.0f);
        outline(selection_, kSelectionColor, 7.0f);
    }

    // Anteprima: facce color ambra e spigoli chiari.
    void drawPreview() {
        const BodyDisplay &display = preview_.display;
        if (displayMode_ != 0) {
            glEnable(GL_LIGHTING);
            glEnable(GL_COLOR_MATERIAL);
            glColor3f(0.95f, 0.66f, 0.28f);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 2.0f);
            drawDisplayFaces(display);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        glDisable(GL_LIGHTING);
        glColor3f(1.0f, 0.88f, 0.62f);
        glLineWidth(1.5f);
        for (const QVector<QVector3D> &polyline : display.edges) {
            glBegin(GL_LINE_STRIP);
            for (const QVector3D &point : polyline) glVertex3f(point.x(), point.y(), point.z());
            glEnd();
        }
        glLineWidth(1.0f);
    }

    // Chiave di una richiesta d'anteprima: due richieste uguali non si rifanno.
    static QString previewKey(const ExtrusionObject &d, int index) {
        const auto n = [](double v) { return QString::number(v, 'g', 17); };
        QStringList parts{QString::number(index), QString::number(d.operation), QString::number(int(d.feature)), QString::number(d.sketchIndex),
                          n(d.distance), QString::number(d.revolveAxis), n(d.revolveAngle), QString::number(d.firstBody),
                          QString::number(d.secondBody), n(d.blendSize), QString::number(d.blendChamfer)};
        const PrimitiveParameters &p = d.primitive;
        parts << QString::number(int(p.kind)) << QString::number(p.plane);
        for (int k = 0; k < 3; ++k) parts << n(p.origin[k]) << n(p.size[k]);
        for (const EdgePoint &e : d.blendEdges) parts << n(e.x) << n(e.y) << n(e.z);
        return parts.join(QLatin1Char(','));
    }

    // Dati di un'anteprima, copiati nel thread dell'interfaccia: il thread del
    // pool non tocca il documento (le forme OCCT si copiano, i body del kernel
    // proprio sono immutabili, gli schizzi sono copie).
    struct PreviewInputs {
        ExtrusionObject definition;
        bool forge = false;
        SketchObject sketch;
        TopoDS_Shape first, second;
        ForgeCad::ForgeBody firstBody, secondBody;
        int quality = 1;
    };
    // La geometria dell'anteprima, come rebuildBody, e la sua tassellazione.
    static bool computePreview(const PreviewInputs &in, BodyDisplay &display, QString &error) {
        const ExtrusionObject &d = in.definition;
        if (d.operation >= 0) {
            if (in.forge) {
                const ForgeCad::ForgeBody result = ForgeCad::forgeBoolean(in.firstBody, in.secondBody, BooleanOperation(d.operation), &error);
                if (!result) return false;
                ForgeCad::forgeTessellate(*result, in.quality, display);
                return true;
            }
            const TopoDS_Shape result = ForgeCad::booleanOperation(in.first, in.second, BooleanOperation(d.operation), &error);
            if (result.IsNull()) return false;
            ForgeCad::tessellate(result, in.quality, display);
            return true;
        }
        if (in.forge) {
            ForgeCad::ForgeBody result;
            switch (d.feature) {
            case BodyFeature::Blend: result = ForgeCad::forgeBlend(in.firstBody, d.blendEdges, d.blendSize, d.blendChamfer, &error); break;
            case BodyFeature::Primitive: result = ForgeCad::forgePrimitive(d.primitive, &error); break;
            case BodyFeature::Revolution: result = ForgeCad::forgeRevolution(in.sketch, d.revolveAxis, d.revolveAngle, &error); break;
            case BodyFeature::Extrusion: result = ForgeCad::forgeExtrusion(in.sketch, d.distance, &error); break;
            }
            if (!result) return false;
            ForgeCad::forgeTessellate(*result, in.quality, display);
            return true;
        }
        TopoDS_Shape result;
        bool solid = false;
        switch (d.feature) {
        case BodyFeature::Blend: result = ForgeCad::buildBlend(in.first, d.blendEdges, d.blendSize, d.blendChamfer, &error); break;
        case BodyFeature::Primitive: result = ForgeCad::buildPrimitive(d.primitive, &error); break;
        case BodyFeature::Revolution: result = ForgeCad::buildRevolution(in.sketch, d.revolveAxis, d.revolveAngle, &error); break;
        case BodyFeature::Extrusion: result = ForgeCad::buildExtrusion(in.sketch, d.distance, solid, &error); break;
        }
        if (result.IsNull()) return false;
        ForgeCad::tessellate(result, in.quality, display);
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
        in.definition.shape.Nullify();
        in.definition.forgeBody.reset();
        in.definition.display = {};
        in.forge = geometryKernel_ == GeometryKernel::Forge;
        in.quality = tessellationQuality_;
        const ExtrusionObject &d = in.definition;
        QString invalid;
        const auto operand = [&](int body, TopoDS_Shape &shape, ForgeCad::ForgeBody &forgeBody) {
            if (body < 0 || body >= extrusions_.size() || (preview_.index >= 0 && body >= preview_.index) || !hasGeometry(extrusions_.at(body))) {
                invalid = QStringLiteral("operandi non validi");
                return;
            }
            const ExtrusionObject &source = extrusions_.at(body);
            if (in.forge) forgeBody = source.forgeBody;
            else shape = BRepBuilderAPI_Copy(source.shape, true, false).Shape();
        };
        if (d.operation >= 0) {
            if (d.firstBody == d.secondBody) invalid = QStringLiteral("scegli due oggetti diversi");
            else {
                operand(d.firstBody, in.first, in.firstBody);
                operand(d.secondBody, in.second, in.secondBody);
            }
        } else if (d.feature == BodyFeature::Blend) {
            operand(d.firstBody, in.first, in.firstBody);
        } else if (d.feature == BodyFeature::Extrusion || d.feature == BodyFeature::Revolution) {
            if (d.sketchIndex < 0 || d.sketchIndex >= sketches_.size()) invalid = QStringLiteral("nessuno schizzo");
            else in.sketch = sketches_.at(d.sketchIndex);
        }
        const quint64 generation = preview_.generation;
        if (!invalid.isEmpty()) {
            preview_.error = invalid;
            if (edgePickBody_ >= 0 && edgePickStatus_) edgePickStatus_(edgePickMessage());
            if (previewCallback_) previewCallback_(preview_.error);
            update();
            return;
        }
        if (!previewReceiver_) previewReceiver_ = new QObject(this);
        QObject *receiver = previewReceiver_;
        previewRunning_ = true;
        QThreadPool::globalInstance()->start([this, receiver, generation, in] {
            BodyDisplay display;
            QString error;
            bool ok = false;
            try {
                ok = computePreview(in, display, error);
            } catch (const std::exception &failure) {
                error = QString::fromUtf8(failure.what());
            } catch (const Standard_Failure &failure) {
                error = QString::fromUtf8(failure.GetMessageString());
            } catch (...) {
                error = QStringLiteral("errore imprevisto");
            }
            if (!ok && error.isEmpty()) error = QStringLiteral("costruzione non riuscita");
            // Il ricevitore e' figlio del viewport: se il viewport non c'e' piu', la chiamata non avviene.
            QMetaObject::invokeMethod(receiver, [this, generation, ok, error, display = std::move(display)]() mutable {
                previewRunning_ = false;
                if (generation == preview_.generation) {
                    preview_.valid = ok;
                    preview_.error = ok ? QString() : error;
                    preview_.display = std::move(display);
                    if (edgePickBody_ >= 0 && edgePickStatus_) edgePickStatus_(edgePickMessage());
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

    // Il punto sotto il mouse sta sul piano parallelo allo schermo che passa per
    // il centro della superficie di prova.
    void probeKernelLab(const QPoint &position) {
        QVector3D origin, direction;
        viewRay(position, origin, direction);
        const float t = QVector3D::dotProduct(kernelLab_.center() - origin, direction) / direction.lengthSquared();
        const QVector3D cursor = origin + t * direction;
        kernelLabProbe_ = kernelLab_.probe(cursor.x(), cursor.y(), cursor.z());
        if (kernelLabCallback_ && kernelLabProbe_.valid) {
            const ForgeCad::KernelLabProbe &probe = kernelLabProbe_;
            QString comparison;
            if (!probe.occtFound) comparison = QStringLiteral("OCCT non trova proiezioni ortogonali (il nuovo kernel considera anche i bordi)");
            else if (probe.tie) comparison = QStringLiteral("pareggio: stessa distanza, piu' punti equidistanti");
            else if (probe.oursDistance < probe.occtDistance - 1e-9 * qMax(1.0, probe.occtDistance))
                comparison = QStringLiteral("il nuovo kernel trova un punto piu' vicino di %1").arg(probe.occtDistance - probe.oursDistance, 0, 'e', 2);
            else comparison = QStringLiteral("scarto tra i piedi %1").arg(probe.pointGap, 0, 'e', 2);
            const QString occtText = probe.occtFound
                ? QStringLiteral("OCCT d = %1 (%2 us) | %3").arg(probe.occtDistance, 0, 'f', 12)
                      .arg(probe.occtMicroseconds, 0, 'f', 1).arg(comparison)
                : QStringLiteral("OCCT (%1 us) | %2").arg(probe.occtMicroseconds, 0, 'f', 1).arg(comparison);
            kernelLabCallback_(QStringLiteral("Nuovo kernel d = %1 (%2 us) | %3")
                                   .arg(probe.oursDistance, 0, 'f', 12).arg(probe.oursMicroseconds, 0, 'f', 1)
                                   .arg(occtText));
        }
        update();
    }

    void drawKernelLab() {
        if (kernelLab_.shape() == ForgeCad::KernelLabShape::None) return;
        glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(1.2f);
        glColor4f(0.45f, 0.75f, 1.0f, 0.75f);
        for (const QVector<QVector3D> &polyline : kernelLab_.wireframe()) {
            glBegin(GL_LINE_STRIP);
            for (const QVector3D &point : polyline) glVertex3f(point.x(), point.y(), point.z());
            glEnd();
        }
        glPopAttrib();
    }

    // Marcatori sopra la scena: cursore (bianco), piede del nuovo kernel
    // (verde) con la normale (giallo), piede di OCCT (anello magenta).
    void drawKernelLabOverlay() {
        if (kernelLab_.shape() == ForgeCad::KernelLabShape::None) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(QFont(QStringLiteral("Sans"), 9));
        QStringList legend = {QStringLiteral("Kernel sperimentale: %1").arg(kernelLab_.shapeName())};
        if (kernelLab_.summary().isEmpty())
            legend << QStringLiteral("bianco = punto del mouse, verde = nuovo kernel")
                   << QStringLiteral("anello magenta = OCCT, giallo = normale");
        else
            legend << kernelLab_.summary();
        painter.setPen(QColor(220, 230, 240));
        for (int line = 0; line < legend.size(); ++line) painter.drawText(QPointF(12, 22 + 16 * line), legend.at(line));
        const ForgeCad::KernelLabProbe &probe = kernelLabProbe_;
        if (!probe.valid) return;
        const QPointF cursor = projectWorldPoint(probe.cursor);
        const QPointF ours = projectWorldPoint(probe.ours);
        painter.setPen(QPen(QColor(90, 230, 120), 1.5, Qt::DashLine));
        painter.drawLine(cursor, ours);
        if (probe.hasNormal) {
            painter.setPen(QPen(QColor(255, 220, 80), 2.0));
            painter.drawLine(ours, projectWorldPoint(probe.ours + 0.6f * probe.normal));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(245, 245, 245));
        painter.drawEllipse(cursor, 4.0, 4.0);
        painter.setBrush(QColor(90, 230, 120));
        painter.drawEllipse(ours, 5.0, 5.0);
        if (probe.occtFound) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(240, 90, 220), 2.0));
            painter.drawEllipse(projectWorldPoint(probe.occt), 9.0, 9.0);
        }
        painter.setPen(QColor(90, 230, 120));
        painter.drawText(ours + QPointF(12, -8), QStringLiteral("d = %1").arg(probe.oursDistance, 0, 'f', 6));
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
    QVector<QVector3D> sceneGeometryPoints() const {
        QVector<QVector3D> points;
        for (const ExtrusionObject &body : extrusions_) {
            if (!body.visible) continue;
            for (const QVector<QVector3D> &edge : body.display.edges) points += edge;
            if (body.display.edges.isEmpty()) points += body.display.vertices;
        }
        for (int index = 0; index < sketches_.size(); ++index)
            if (isSketchDrawn(index)) points += sketchGeometryPoints(sketches_.at(index));
        if (points.isEmpty())
            for (int plane = 0; plane < 3; ++plane) points += planeCorners(plane);
        return points;
    }

    void updateSceneBounds() const {
        if (!sceneBoundsDirty_) return;
        const QVector<QVector3D> points = sceneGeometryPoints();
        sceneMin_ = sceneMax_ = points.isEmpty() ? QVector3D() : points.first();
        for (const QVector3D &p : points) {
            sceneMin_ = QVector3D(qMin(sceneMin_.x(), p.x()), qMin(sceneMin_.y(), p.y()), qMin(sceneMin_.z(), p.z()));
            sceneMax_ = QVector3D(qMax(sceneMax_.x(), p.x()), qMax(sceneMax_.y(), p.y()), qMax(sceneMax_.z(), p.z()));
        }
        sceneBoundsDirty_ = false;
    }

    // Mezza profondita' del volume di vista: tutta la geometria (e i piani)
    // resta tra i piani di taglio anche nelle scene grandi.
    double sceneDepth() const {
        updateSceneBounds();
        double reach = 10.0;  // griglia e piani di riferimento
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


    static const QColor &axisColor(int axis) {
        static const QColor colors[3] = {QColor(235, 70, 70), QColor(80, 215, 95), QColor(70, 135, 255)};
        return colors[qBound(0, axis, 2)];
    }

    // Assi cartesiani dall'origine: una linea per asse e una freccia conica
    // in punta (sotto gli oggetti, senza test di profondita').
    void drawAxes() {
        if (!axesVisible_) return;
        const float length = float(axisLength());
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
        const QVector3D below = -0.05f * QVector3D(float(orientation_.up[0]), float(orientation_.up[1]), float(orientation_.up[2]));
        glBegin(GL_LINES);
        for (int i = -10; i <= 10; ++i) {
            for (const QVector3D &p : {below + float(i) * a - 10.0f * b, below + float(i) * a + 10.0f * b,
                                       below - 10.0f * a + float(i) * b, below + 10.0f * a + float(i) * b})
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
    mutable ForgeCad::SketchAnalysis analysis_;
    mutable bool analysisDirty_ = true;
    mutable int analysisSketch_ = -1;
    bool constraintsVisible_ = true;
    std::function<void()> constraintPanelCallback_;
    QVector<SketchObject> sketches_;
    QVector<ExtrusionObject> extrusions_;
    ForgeCad::History history_;
    DocumentState dragSnapshot_;
    bool dragRecorded_ = false;
    int edgePickBody_ = -1, hoverEdge_ = -1;
    bool edgePickChamfer_ = false;
    QVector<int> pickedEdges_, hoverFaceEdges_;
    int edgePickEdit_ = -1;         // raccordo di cui si modificano gli spigoli (-1: raccordo nuovo)
    double edgePickSize_ = 0.5;     // misura dell'anteprima durante la scelta
    std::function<void(int, QVector<EdgePoint>, double, bool)> edgeEditFinished_;
    // Anteprima di una funzione (vedi requestPreview).
    struct Preview {
        QString key;  // vuota: nessuna anteprima
        ExtrusionObject definition;
        int index = -1;           // corpo che l'anteprima sostituisce (modifica), -1 nuovo
        QVector<int> replaced;    // operandi (booleane) o base (raccordi): al loro posto l'anteprima
        bool valid = false;
        QString error;
        BodyDisplay display;
        quint64 generation = 0;
    };
    Preview preview_;
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
    std::function<void()> documentChangedCallback_;
    std::function<void(int)> planeContextCallback_;
    std::function<void(bool)> sketchModeCallback_;
    std::function<void(DrawingTool)> toolChangedCallback_;
    std::function<void(const QString &)> rendererCallback_;
    std::function<void(const QString &)> kernelLabCallback_;
    ForgeCad::KernelLab kernelLab_;
    ForgeCad::KernelLabProbe kernelLabProbe_;
    GeometryKernel geometryKernel_ = GeometryKernel::OpenCascade;
    // Strumenti di modifica dello schizzo: misure di raccordo e smusso, primo
    // segmento scelto (se non si e' cliccato uno spigolo) e tratto che il taglio toglierebbe.
    double sketchFilletRadius_ = 1.0, sketchChamferDistance_ = 1.0;
    int blendFirst_ = -1;
    QPointF blendFirstPick_;
    QVector<QPointF> trimPreview_;
    std::function<void(const QString &)> statusCallback_;
    // Antialiasing: campioni MSAA (0 = spento). La scena OpenGL si disegna in
    // un framebuffer multisample, risolto in uno normale e copiato nel widget.
    int antialiasing_ = 4, maxSamples_ = 0, bufferSamples_ = 0;
    std::unique_ptr<QOpenGLFramebufferObject> msaaBuffer_, resolveBuffer_;
    bool gridVisible_ = true;
    double axisLength_ = kDefaultAxisLength;
    bool axesVisible_ = true, axesOnTop_ = true;  // assi mostrati; sopra solidi, superfici e schizzi
    bool originSnap_ = true;                      // lo schizzo si aggancia all'origine del piano
    // Box della geometria visibile (coordinate del modello), per zoom e profondita' della vista.
    mutable QVector3D sceneMin_, sceneMax_;
    mutable bool sceneBoundsDirty_ = true;
    bool painted_ = false;
    // Trascinamento con lo strumento Selezione: un punto (estremo di segmento
    // con i punti coincidenti) o un segmento intero.
    bool pointDragActive_ = false, bodyDragMoved_ = false;
    QPointF pointDragPosition_, bodyDragLast_;
    int bodyDragSegment_ = -1;
    QVector<QVector3D> pendingFit_;
};

// Tipi delle voci dell'albero modello (Qt::UserRole); Qt::UserRole + 1 e' l'indice.
enum TreeItemType { kTreeInfo = -1, kTreeOrigin = 0, kTreePlane = 1, kTreeSketch = 3, kTreeExtrusion = 4 };

static const QStringList &planeNames() {
    static const QStringList names = {QStringLiteral("Piano XY - Superiore"),
                                      QStringLiteral("Piano XZ - Frontale"),
                                      QStringLiteral("Piano YZ - Destro")};
    return names;
}

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
    int row = form->rowCount();
    QFormLayout::ItemRole role;
    form->getWidgetPosition(buttons, &row, &role);
    form->insertRow(qMax(0, row), errorLabel);
    QObject::disconnect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, errorLabel, apply] {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply();
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) {
            dialog.accept();
            return;
        }
        errorLabel->setText(error + QStringLiteral("\nCorreggi i valori e riprova, oppure Annulla."));
        errorLabel->show();
        dialog.adjustSize();
    });
    return dialog.exec() == QDialog::Accepted;
}

// Anteprima dal vivo in una finestra di funzione: una riga "Anteprima" con lo
// stato, `request` a ogni cambio dei valori; alla chiusura l'anteprima sparisce.
struct PreviewScope {
    CadViewport *viewport = nullptr;
    QLabel *label = nullptr;
    int index = -1;  // corpo che l'anteprima sostituisce (modifica), -1 nuovo
    PreviewScope(CadViewport *target, QDialog &dialog, QFormLayout *form, int replacedIndex) : viewport(target), index(replacedIndex) {
        if (!viewport) return;
        label = new QLabel(QStringLiteral("in calcolo..."), &dialog);
        label->setWordWrap(true);
        label->setMaximumWidth(460);
        form->addRow(QStringLiteral("Anteprima:"), label);
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

// Un valore numerico per una funzione (distanza, raggio...), con la finestra
// che resta aperta finche' `apply` non riesce o si annulla.
static bool askValueUntilApplied(QWidget *parent, const QString &title, const QString &label, double value, double minimum,
                                 double maximum, const std::function<QString(double)> &apply, const PreviewSpec<double> &preview = {}) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = new QFormLayout(&dialog);
    auto *spin = new QDoubleSpinBox(&dialog);
    spin->setDecimals(6);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    form->addRow(label, spin);
    const PreviewScope scope(preview.define ? preview.viewport : nullptr, dialog, form, preview.index);
    if (preview.define) {
        const auto refresh = [&scope, &preview, spin] { scope.request(preview.define(spin->value())); };
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, refresh);
        refresh();
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    spin->selectAll();
    return runUntilApplied(dialog, form, buttons, [spin, &apply] { return apply(spin->value()); });
}

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
};
static BlendDialogResult blendDialog(QWidget *parent, CadViewport *viewport, const QString &title, int base, int hidden,
                                     const QVector<EdgePoint> &edges, double size, bool chamfer, bool allowKindChange,
                                     const std::function<QString(double, bool)> &apply) {
    BlendDialogResult result;
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = new QFormLayout(&dialog);
    auto *sizeBox = new QDoubleSpinBox(&dialog);
    sizeBox->setDecimals(6);
    sizeBox->setRange(0.000001, 100000.0);
    sizeBox->setValue(size);
    QCheckBox *chamferBox = nullptr;
    auto *sizeLabel = new QLabel(&dialog);
    const auto describe = [sizeLabel](bool isChamfer) {
        sizeLabel->setText(isChamfer ? QStringLiteral("Distanza dello smusso dallo spigolo:") : QStringLiteral("Raggio del raccordo:"));
    };
    describe(chamfer);
    form->addRow(sizeLabel, sizeBox);
    if (allowKindChange) {
        chamferBox = new QCheckBox(QStringLiteral("Smusso (distanza) invece del raccordo (raggio)"), &dialog);
        chamferBox->setChecked(chamfer);
        form->addRow(QString(), chamferBox);
        QObject::connect(chamferBox, &QCheckBox::toggled, &dialog, describe);
    }
    form->addRow(QStringLiteral("Spigoli: %1").arg(edges.size()), new QLabel(&dialog));
    auto *previewLabel = new QLabel(QStringLiteral("Anteprima in calcolo..."), &dialog);
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
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    const auto currentChamfer = [chamferBox, chamfer] { return chamferBox ? chamferBox->isChecked() : chamfer; };
    const auto refresh = [=] {
        previewLabel->setText(QStringLiteral("in calcolo..."));
        viewport->requestBlendPreview(base, edges, sizeBox->value(), currentChamfer(), hidden);
    };
    viewport->setPreviewCallback([previewLabel](const QString &error) {
        previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
    });
    QObject::connect(sizeBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    if (chamferBox) QObject::connect(chamferBox, &QCheckBox::toggled, &dialog, refresh);
    refresh();
    sizeBox->selectAll();
    result.applied = runUntilApplied(dialog, form, buttons, [&] { return apply(sizeBox->value(), currentChamfer()); });
    viewport->setPreviewCallback({});
    viewport->clearBlendPreview();
    result.size = sizeBox->value();
    result.chamfer = currentChamfer();
    return result;
}

// Finestra dei parametri della rivoluzione (anche per modificarne una: lo
// schizzo resta quello, `fixedSketch`). Valori iniziali e risultato negli argomenti.
// Con `apply` la conferma esegue il comando e la finestra resta aperta se fallisce.
static bool revolutionDialog(QWidget *parent, const QVector<SketchObject> &sketches, bool fixedSketch, int &sketch, int &axis,
                             double &angle, const std::function<QString(int, int, double)> &apply = {},
                             const PreviewSpec<int, int, double> &preview = {}) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Rivoluzione"));
    auto *form = new QFormLayout(&dialog);
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
    auto *angleBox = new QDoubleSpinBox(&dialog);
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
    if (apply)
        return runUntilApplied(dialog, form, buttons, [&] {
            if (axisBox->currentIndex() < 0) return QStringLiteral("Scegli l'asse della rivoluzione.");
            read();
            return apply(sketch, axis, angle);
        });
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
    QDialog dialog(parent);
    dialog.setWindowTitle(primitiveTitle(parameters.kind));
    auto *form = new QFormLayout(&dialog);
    auto *planeBox = new QComboBox(&dialog);
    planeBox->addItems(planeNames());
    planeBox->setCurrentIndex(qBound(0, parameters.plane, 2));
    form->addRow(QStringLiteral("Piano di base (asse Z = normale):"), planeBox);
    const auto makeSpin = [&dialog](double value, double minimum) {
        auto *spin = new QDoubleSpinBox(&dialog);
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
            auto *dialog = qobject_cast<QDialog *>(watched);
            if (dialog && dialog->isWindow() && dialog->windowHandle() && dialog->windowHandle()->startSystemMove()) return true;
        }
        return QObject::eventFilter(watched, event);
    }
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
    auto *modelTree = new QTreeWidget(modelDock);
    modelTree_ = modelTree;
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
            const BlendDialogResult result = blendDialog(this, viewport, title, original.firstBody, index, edges, size, chamfer, true,
                                                         [&](double newSize, bool newChamfer) {
                ExtrusionObject body = original;
                const QString oldPrefix = body.blendChamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo");
                const QString newPrefix = newChamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo");
                if (body.name.startsWith(oldPrefix)) body.name.replace(0, oldPrefix.size(), newPrefix);
                body.blendSize = newSize;
                body.blendChamfer = newChamfer;
                body.blendEdges = edges;
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
        const auto standardButtons = [](QDialog &dialog, QFormLayout *form) {
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            return buttons;
        };
        if (original.operation >= 0) {
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("Modifica booleana"));
            auto *form = new QFormLayout(&dialog);
            auto *operationBox = new QComboBox(&dialog);
            operationBox->addItems({QStringLiteral("Unione"), QStringLiteral("Intersezione"), QStringLiteral("Differenza A - B")});
            operationBox->setCurrentIndex(original.operation);
            auto *swapBox = new QCheckBox(QStringLiteral("Scambia A e B"), &dialog);
            form->addRow(QStringLiteral("A: ") + bodies.value(original.firstBody).name + QStringLiteral("   B: ") + bodies.value(original.secondBody).name, new QLabel(&dialog));
            form->addRow(QStringLiteral("Operazione:"), operationBox);
            form->addRow(QString(), swapBox);
            const PreviewScope scope(viewport, dialog, form, index);
            const auto refresh = [&] {
                ExtrusionObject body = original;
                body.operation = operationBox->currentIndex();
                if (swapBox->isChecked()) std::swap(body.firstBody, body.secondBody);
                scope.request(body);
            };
            connect(operationBox, &QComboBox::currentIndexChanged, &dialog, refresh);
            connect(swapBox, &QCheckBox::toggled, &dialog, refresh);
            refresh();
            QDialogButtonBox *buttons = standardButtons(dialog, form);
            runUntilApplied(dialog, form, buttons, [&] {
                static const QStringList names = {QStringLiteral("Unione"), QStringLiteral("Intersezione"), QStringLiteral("Differenza")};
                ExtrusionObject body = original;
                // Il nome segue l'operazione se era quello automatico.
                if (body.name.startsWith(names.value(body.operation)))
                    body.name.replace(0, names.value(body.operation).size(), names.value(operationBox->currentIndex()));
                body.operation = operationBox->currentIndex();
                if (swapBox->isChecked()) std::swap(body.firstBody, body.secondBody);
                return update(body);
            });
        } else if (original.feature == BodyFeature::Extrusion) {
            askValueUntilApplied(this, QStringLiteral("Estrusione"), QStringLiteral("Distanza di estrusione (positiva o negativa):"),
                                 original.distance, -100000.0, 100000.0, [&](double distance) {
                                     ExtrusionObject body = original;
                                     body.distance = distance;
                                     return update(body);
                                 },
                                 {viewport, index, [&](double distance) {
                                      ExtrusionObject body = original;
                                      body.distance = distance;
                                      return body;
                                  }});
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
        }
    });
    viewport->setEditBodyCallback([editBody](int index) { (*editBody)(index); });
    connect(modelTree, &QTreeWidget::itemDoubleClicked, this, [editBody](QTreeWidgetItem *item, int) {
        if (item->data(0, Qt::UserRole).toInt() == kTreeExtrusion) (*editBody)(item->data(0, Qt::UserRole + 1).toInt());
    });
    viewport->setSelectionCallback([this](SceneSelection selection) {
        for (int index = 0; index < modelTree_->topLevelItemCount(); ++index) {
            QTreeWidgetItem *item = modelTree_->topLevelItem(index);
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
    });
    connect(modelTree, &QTreeWidget::itemClicked, this, [this, viewport](QTreeWidgetItem *item, int) {
        if (suppressTreeClick_) return;
        const int type = item->data(0, Qt::UserRole).toInt();
        const int index = item->data(0, Qt::UserRole + 1).toInt();
        if (type == kTreePlane) viewport->selectPlane(index);
        if (type == kTreeSketch) viewport->selectSketch(index);
        if (type == kTreeExtrusion) viewport->selectObject(SceneObjectKind::Extrusion, index);
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
        } else if (type == kTreeSketch || type == kTreeExtrusion) {
            const SceneObjectKind kind = type == kTreeSketch ? SceneObjectKind::Sketch : SceneObjectKind::Extrusion;
            const bool visible = viewport->isObjectVisible(kind, index);
            QAction *editSketch = type == kTreeSketch ? menu.addAction(QStringLiteral("Modifica schizzo")) : nullptr;
            QAction *editParameters = type == kTreeExtrusion ? menu.addAction(QStringLiteral("Modifica parametri...")) : nullptr;
            QAction *toggle = menu.addAction(visible ? QStringLiteral("Nascondi") : QStringLiteral("Mostra"));
            menu.addSeparator();
            QAction *remove = menu.addAction(QStringLiteral("Elimina"));
            const QAction *chosen = menu.exec(globalPosition);
            if (chosen && chosen == editSketch) viewport->selectSketch(index);
            else if (chosen && chosen == editParameters) (*editBody)(index);
            else if (chosen && chosen == remove) viewport->deleteObject(kind, index);
            else if (chosen == toggle) viewport->setObjectVisible(kind, index, !visible);
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
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const QString error = ForgeCad::exportBodies(path, bodies, format);
            QApplication::restoreOverrideCursor();
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
    auto *functionsMenu = menuBar()->addMenu(QStringLiteral("Funzioni"));
    QAction *extrudeAction = functionsMenu->addAction(QStringLiteral("Estrusione..."));
    extrudeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    auto *booleanMenu = functionsMenu->addMenu(QStringLiteral("Booleane"));
    QAction *unionAction = booleanMenu->addAction(QStringLiteral("Unione..."));
    QAction *intersectionAction = booleanMenu->addAction(QStringLiteral("Intersezione..."));
    QAction *differenceAction = booleanMenu->addAction(QStringLiteral("Differenza A - B..."));
    QAction *revolveAction = functionsMenu->addAction(QStringLiteral("Rivoluzione..."));
    auto *primitiveMenu = functionsMenu->addMenu(QStringLiteral("Primitive"));
    QAction *filletAction = functionsMenu->addAction(QStringLiteral("Raccordo spigoli..."));
    QAction *chamferAction = functionsMenu->addAction(QStringLiteral("Smusso spigoli..."));
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
        const double length = QInputDialog::getDouble(this, QStringLiteral("Dimensione degli assi"),
            QStringLiteral("Lunghezza degli assi (unita' del modello; i piani di riferimento vanno da -4 a 4):"),
            viewport->axisLength(), 0.1, 100.0, 3, &accepted);
        if (!accepted) return;
        viewport->setAxisLength(length);
        QSettings().setValue(QStringLiteral("view/axisLength"), viewport->axisLength());
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
    connect(lowQuality, &QAction::triggered, this, [viewport] { viewport->setTessellationQuality(0); });
    connect(mediumQuality, &QAction::triggered, this, [viewport] { viewport->setTessellationQuality(1); });
    connect(highQuality, &QAction::triggered, this, [viewport] { viewport->setTessellationQuality(2); });
    connect(extrudeAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->activeSketchIndex() < 0) return;
        const QString name = QStringLiteral("Estrusione %1").arg(viewport->extrusions().size() + 1);
        const int sketch = viewport->activeSketchIndex();
        askValueUntilApplied(this, QStringLiteral("Estrusione"), QStringLiteral("Distanza di estrusione (positiva o negativa):"), 1.0,
                             -100000.0, 100000.0, [viewport, name](double distance) { return viewport->createExtrusion(distance, name); },
                             {viewport, -1, [sketch](double distance) {
                                  ExtrusionObject body;
                                  body.sketchIndex = sketch;
                                  body.distance = distance;
                                  return body;
                              }});
    });
    const auto runBoolean = [this, viewport](BooleanOperation operation, const QString &title) {
        // Operandi: i solidi chiusi; per intersezione e differenza anche le
        // superfici (la parte dentro o fuori dall'altro solido).
        QStringList names;
        QVector<int> indices;
        const QVector<ExtrusionObject> &bodies = viewport->extrusions();
        for (int index = 0; index < bodies.size(); ++index) {
            if (bodies.at(index).shape.IsNull() && !bodies.at(index).forgeBody) continue;
            if (!bodies.at(index).solid && operation == BooleanOperation::Union) continue;
            names.append(bodies.at(index).visible ? bodies.at(index).name
                                                  : bodies.at(index).name + QStringLiteral(" (nascosto)"));
            indices.append(index);
        }
        if (names.size() < 2) {
            QMessageBox::information(this, title, operation == BooleanOperation::Union
                                                      ? QStringLiteral("Servono almeno due solidi chiusi nella scena.")
                                                      : QStringLiteral("Servono almeno due corpi nella scena (un solido e un solido o una superficie)."));
            return;
        }
        const SceneSelection selection = viewport->selection();
        const int preferred = selection.kind == SceneObjectKind::Extrusion ? indices.indexOf(selection.index) : -1;
        const QString firstLabel = operation == BooleanOperation::Difference
            ? QStringLiteral("Oggetto da cui sottrarre (A):") : QStringLiteral("Primo oggetto:");
        const QString secondLabel = operation == BooleanOperation::Difference
            ? QStringLiteral("Oggetto da sottrarre (B):") : QStringLiteral("Secondo oggetto:");
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        auto *form = new QFormLayout(&dialog);
        auto *firstBox = new QComboBox(&dialog), *secondBox = new QComboBox(&dialog);
        firstBox->addItems(names);
        secondBox->addItems(names);
        firstBox->setCurrentIndex(qMax(0, preferred));
        secondBox->setCurrentIndex(firstBox->currentIndex() == 0 ? 1 : 0);
        form->addRow(firstLabel, firstBox);
        form->addRow(secondLabel, secondBox);
        const PreviewScope scope(viewport, dialog, form, -1);
        const auto refresh = [&] {
            ExtrusionObject body;
            body.operation = int(operation);
            body.firstBody = indices.at(firstBox->currentIndex());
            body.secondBody = indices.at(secondBox->currentIndex());
            scope.request(body);
        };
        connect(firstBox, &QComboBox::currentIndexChanged, &dialog, refresh);
        connect(secondBox, &QComboBox::currentIndexChanged, &dialog, refresh);
        refresh();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons);
        const QString resultName = title + QStringLiteral(" %1").arg(viewport->extrusions().size() + 1);
        runUntilApplied(dialog, form, buttons, [&] {
            if (firstBox->currentIndex() == secondBox->currentIndex()) return QStringLiteral("Scegli due oggetti diversi.");
            return viewport->createBoolean(operation, indices.at(firstBox->currentIndex()), indices.at(secondBox->currentIndex()), resultName);
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
    for (const auto &entry : primitives) {
        QAction *action = primitiveMenu->addAction(entry.first + QStringLiteral("..."));
        const QString title = entry.first;
        const PrimitiveKind kind = entry.second;
        connect(action, &QAction::triggered, this, [runPrimitive, kind, title] { runPrimitive(kind, title); });
    }

    // Raccordi e smussi: gli spigoli si scelgono nella vista, poi la misura.
    // Durante la scelta: la misura dell'anteprima nella barra di stato.
    auto *edgePickStatus = new QLabel(this);
    auto *pickSizeLabel = new QLabel(QStringLiteral("Misura:"), this);
    pickSizeBox_ = new QDoubleSpinBox(this);
    pickSizeBox_->setDecimals(4);
    pickSizeBox_->setRange(0.0001, 100000.0);
    pickSizeBox_->setValue(blendSize_);
    pickSizeBox_->setToolTip(QStringLiteral("Raggio del raccordo o distanza dello smusso per l'anteprima"));
    pickSizeLabel->hide();
    pickSizeBox_->hide();
    connect(pickSizeBox_, &QDoubleSpinBox::valueChanged, this, [viewport](double value) { viewport->setEdgePickSize(value); });
    viewport->setEdgePickCallbacks(
        [edgePickStatus, pickSizeLabel, this](const QString &text) {
            edgePickStatus->setText(text);
            pickSizeLabel->setVisible(!text.isEmpty());
            pickSizeBox_->setVisible(!text.isEmpty());
        },
        [this, viewport](int body, QVector<EdgePoint> edges, bool chamfer) {
            // La misura con l'anteprima: la finestra resta aperta finche' il raccordo
            // non riesce; "Spigoli..." torna alla scelta degli spigoli (gli stessi, da cambiare).
            const QString title = chamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo");
            const QString name = title + QStringLiteral(" %1").arg(viewport->extrusions().size() + 1);
            const BlendDialogResult result = blendDialog(this, viewport, title, body, -1, edges, viewport->edgePickSize(), chamfer, false,
                                                         [&](double size, bool isChamfer) { return viewport->createBlend(body, edges, size, isChamfer, name); });
            blendSize_ = result.size;
            viewport->setEdgePickSize(result.size);
            if (pickSizeBox_) pickSizeBox_->setValue(result.size);
            if (result.reselect) viewport->resumeEdgePick(body, edges, chamfer);
        });
    for (const auto &[action, chamfer] : {std::pair<QAction *, bool>{filletAction, false}, {chamferAction, true}}) {
        const bool isChamfer = chamfer;
        connect(action, &QAction::triggered, this, [this, viewport, isChamfer] {
            viewport->setEdgePickSize(pickSizeBox_->value());
            const QString error = viewport->beginEdgePick(isChamfer);
            if (!error.isEmpty()) QMessageBox::information(this, isChamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo"), error);
        });
    }
    statusBar()->addWidget(edgePickStatus);
    statusBar()->addWidget(pickSizeLabel);
    statusBar()->addWidget(pickSizeBox_);

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
    auto *kernelMenu = optionsMenu->addMenu(QStringLiteral("Kernel geometrico"));
    auto *kernelGroup = new QActionGroup(this); kernelGroup->setExclusive(true);
    QAction *occtKernel = kernelMenu->addAction(QStringLiteral("OpenCASCADE"));
    QAction *forgeKernel = kernelMenu->addAction(QStringLiteral("ForgeCAD (kernel proprio)"));
    occtKernel->setCheckable(true); forgeKernel->setCheckable(true);
    kernelGroup->addAction(occtKernel); kernelGroup->addAction(forgeKernel);
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
    auto *kernelStatus = new QLabel(this);
    const auto applyKernel = [this, viewport, kernelStatus](GeometryKernel kernel, bool remember) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        viewport->setGeometryKernel(kernel);
        QApplication::restoreOverrideCursor();
        kernelStatus->setText(kernel == GeometryKernel::Forge ? QStringLiteral("Kernel: ForgeCAD")
                                                              : QStringLiteral("Kernel: OpenCASCADE"));
        if (remember) QSettings().setValue(QStringLiteral("kernel/geometry"), int(kernel));
        int failed = 0;
        for (const ExtrusionObject &body : viewport->extrusions()) failed += !body.error.isEmpty();
        if (remember && failed > 0)
            QMessageBox::warning(this, QStringLiteral("Kernel geometrico"),
                                 QStringLiteral("%1 corpi non sono stati ricostruiti con il kernel scelto: "
                                                "sono segnati con \u26A0 nell'albero modello.").arg(failed));
    };
    connect(occtKernel, &QAction::triggered, this, [applyKernel] { applyKernel(GeometryKernel::OpenCascade, true); });
    connect(forgeKernel, &QAction::triggered, this, [applyKernel] { applyKernel(GeometryKernel::Forge, true); });
    const GeometryKernel startKernel =
        QSettings().value(QStringLiteral("kernel/geometry"), 0).toInt() == int(GeometryKernel::Forge) ? GeometryKernel::Forge
                                                                                                      : GeometryKernel::OpenCascade;
    (startKernel == GeometryKernel::Forge ? forgeKernel : occtKernel)->setChecked(true);
    applyKernel(startKernel, false);

    auto *debugMenu = menuBar()->addMenu(QStringLiteral("Debug"));
    auto *labMenu = debugMenu->addMenu(QStringLiteral("Kernel sperimentale"));
    auto *labGroup = new QActionGroup(this); labGroup->setExclusive(true);
    const QList<QPair<QString, ForgeCad::KernelLabShape>> labShapes = {
        {QStringLiteral("Spento"), ForgeCad::KernelLabShape::None},
        {QStringLiteral("Sfera"), ForgeCad::KernelLabShape::Sphere},
        {QStringLiteral("Toro"), ForgeCad::KernelLabShape::Torus},
        {QStringLiteral("Cono"), ForgeCad::KernelLabShape::Cone},
        {QStringLiteral("Superficie NURBS"), ForgeCad::KernelLabShape::BSpline},
        {QStringLiteral("Superficie di rivoluzione"), ForgeCad::KernelLabShape::Revolution},
        {QStringLiteral("Superficie estrusa"), ForgeCad::KernelLabShape::Extrusion},
        {QStringLiteral("Solido B-rep: prisma con fori"), ForgeCad::KernelLabShape::PrismBody},
        {QStringLiteral("Solido B-rep: cilindro"), ForgeCad::KernelLabShape::CylinderBody}};
    QAction *labOff = nullptr;
    for (const auto &entry : labShapes) {
        auto *action = labMenu->addAction(entry.first); action->setCheckable(true);
        action->setChecked(entry.second == ForgeCad::KernelLabShape::None); labGroup->addAction(action);
        if (entry.second == ForgeCad::KernelLabShape::None) labOff = action;
        const ForgeCad::KernelLabShape shape = entry.second;
        connect(action, &QAction::triggered, this, [viewport, shape] { viewport->setKernelLabShape(shape); });
    }
    QAction *compareExtrusion = debugMenu->addAction(QStringLiteral("Confronta il corpo selezionato col nuovo kernel (estrusioni e booleane)"));
    connect(compareExtrusion, &QAction::triggered, this, [this, viewport, labOff] {
        const QString error = viewport->compareSelectedExtrusionWithKernel();
        if (!error.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Kernel sperimentale"), error);
            return;
        }
        // Nessuna voce del sottomenu corrisponde: "Spento" torna selezionabile per chiudere.
        labOff->setChecked(false);
        if (labOff->actionGroup()) labOff->actionGroup()->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
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
        const double size = QInputDialog::getDouble(this, chamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo"),
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
    QAction *dimensionAction = sketchMenu->addAction(QStringLiteral("Modifica quota (segmento, cerchio, arco, poligono)... (doppio clic)"));
    dimensionAction->setShortcut(QKeySequence(Qt::Key_D));
    connect(dimensionAction, &QAction::triggered, this, [this, viewport] {
        const QString error = viewport->editSegmentDimension();
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
    auto *constraintPanel = new QDialog(this, Qt::Tool);
    constraintPanel->setObjectName(QStringLiteral("constraintPanel"));
    constraintPanel->setWindowTitle(QStringLiteral("Vincoli"));
    constraintPanel->setModal(false);
    constraintPanel->resize(380, 520);
    auto *panelLayout = new QVBoxLayout(constraintPanel);
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
    auto *deleteShortcut = new QAction(constraintList);
    deleteShortcut->setShortcut(QKeySequence::Delete);
    deleteShortcut->setShortcutContext(Qt::WidgetShortcut);
    constraintList->addAction(deleteShortcut);
    connect(deleteShortcut, &QAction::triggered, constraintPanel, [=] { viewport->deleteConstraints(listSelection()); });
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
    connect(newSketchAction, &QAction::triggered, this, [this, createSketchOnPlane] {
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
        const double length = QInputDialog::getDouble(
            this, QStringLiteral("Quota lunghezza"), QStringLiteral("Lunghezza del prossimo segmento:"),
            1.0, 0.000001, 100000.0, 6, &accepted);
        if (accepted) viewport->setLineLength(length);
    });
    connect(angleConstraint, &QAction::triggered, this, [this, viewport] {
        bool accepted = false;
        const double angle = QInputDialog::getDouble(
            this, QStringLiteral("Quota angolare"),
            QStringLiteral("Angolo rispetto all'asse X del piano (gradi):"),
            0.0, -360.0, 360.0, 6, &accepted);
        if (accepted) viewport->setLineAngle(angle);
    });
    connect(exitSketch, &QAction::triggered, this, [viewport] { viewport->endSketchMode(); });

    auto *drawingToolbar = addToolBar(QStringLiteral("Strumenti schizzo")); drawingToolbar->setObjectName(QStringLiteral("sketchToolbar")); drawingToolbar->setMovable(false); drawingToolbar->setVisible(false);
    drawingToolbar->addAction(selectTool); drawingToolbar->addAction(lineTool); drawingToolbar->addAction(polylineTool); drawingToolbar->addAction(splineTool); drawingToolbar->addAction(nurbsTool); drawingToolbar->addAction(circleTool); drawingToolbar->addAction(arcTool); drawingToolbar->addAction(polygonTool); drawingToolbar->addAction(rectangleTool); drawingToolbar->addAction(centerRectangleTool); drawingToolbar->addAction(ellipseTool); drawingToolbar->addAction(constructionTool); drawingToolbar->addAction(toggleConstruction); drawingToolbar->addSeparator();
    drawingToolbar->addAction(trimTool); drawingToolbar->addAction(extendTool); drawingToolbar->addAction(splitTool); drawingToolbar->addAction(sketchFilletTool); drawingToolbar->addAction(sketchChamferTool); drawingToolbar->addSeparator(); drawingToolbar->addAction(exitSketch);
    auto viewActions = std::make_shared<QList<QAction *>>();
    viewport->setSketchModeCallback([this, viewMenu, viewActions, drawingToolbar](bool active) {
        viewMenu->setEnabled(!active); for (QAction *action : *viewActions) action->setEnabled(!active); drawingToolbar->setVisible(active);
        // La finestra dei vincoli accompagna la modalita' schizzo.
        if (constraintPanel_) constraintPanel_->setVisible(active);
    });

    auto *quit = fileMenu->addAction(QStringLiteral("Esci")); connect(quit, &QAction::triggered, this, &QWidget::close);
    auto *toolbar = addToolBar(QStringLiteral("Modellazione")); toolbar->setObjectName(QStringLiteral("modelingToolbar")); toolbar->setMovable(false); toolbar->addAction(modeMenu->actions().at(2)); toolbar->addAction(modeMenu->actions().at(0)); toolbar->addAction(snapAction); toolbar->addWidget(new QLabel(QStringLiteral("  ForgeCAD / Part Studio  ")));
    const QList<QPair<QString, int>> views = {{QStringLiteral("Front"),0},{QStringLiteral("Rear"),1},{QStringLiteral("Right"),2},{QStringLiteral("Top"),3},{QStringLiteral("Isometric"),4},{QStringLiteral("Trimetric"),5}};
    for (int index = 0; index < views.size(); ++index) {
        auto *action = new QAction(views.at(index).first, this); action->setShortcut(QKeySequence(Qt::Key_1 + index)); addAction(action); viewActions->append(action);
        connect(action, &QAction::triggered, this, [viewport, index] { viewport->setViewPreset(index); });
    }
    modeStatus_ = new QLabel(QStringLiteral("Mesh + linee esterne")); statusBar()->addWidget(modeStatus_);
    statusBar()->addWidget(kernelStatus);
    auto *labStatus = new QLabel(this); statusBar()->addWidget(labStatus);
    viewport->setKernelLabCallback([labStatus](const QString &text) { labStatus->setText(text); });
    const QString cudaStatus = forgecad_cuda_available()
        ? QString::fromUtf8(forgecad_cuda_backend())
        : QStringLiteral("CUDA compilato, GPU runtime non disponibile");
    auto *gpuStatus = new QLabel(QStringLiteral("%1 | OpenGL: in avvio").arg(cudaStatus));
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
                                   QStringLiteral("view/antialiasing"), QStringLiteral("view/panKey"), QStringLiteral("sketch/originSnap")})
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
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Apri"), QFileInfo(documentPath_).absolutePath(),
                                                      QStringLiteral("Documenti ForgeCAD (*.prt);;Tutti i file (*)"));
    if (path.isEmpty()) return;
    openDocumentPath(path);
}

bool PdfWindow::openDocumentPath(const QString &path) {
    DocumentState state;
    const QString error = ForgeCad::loadDocumentFile(path, state);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Apri"), error);
        return false;
    }
    // I file senza orientamento (versioni vecchie) prendono quello predefinito.
    if (!state.orientationSet) {
        state.orientation = defaultAxesOrientation();
        state.orientationSet = true;
    }
    loadingDocument_ = true;
    viewport_->loadDocument(std::move(state));
    loadingDocument_ = false;
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
    const QString error = ForgeCad::saveDocumentFile(path, viewport_->currentDocument());
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
void PdfWindow::rebuildModelTree() {
    rebuildingTree_ = true;
    const auto addObject = [this](const QString &name, int type, int index, int checkState) {
        auto *item = new QTreeWidgetItem(modelTree_, {name});
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
    for (int index = 0; index < sketches.size(); ++index) {
        const SketchObject &sketch = sketches.at(index);
        const QString plane = sketch.plane == kFacePlane ? QStringLiteral("Faccia di %1").arg(sketch.faceSource)
                                                         : planeNames().value(sketch.plane);
        QTreeWidgetItem *item = addObject(sketch.name + QStringLiteral(" [") + plane + QStringLiteral("]"), kTreeSketch, index, sketch.visible);
        if ((selection.kind == SceneObjectKind::Sketch && selection.index == index)
            || viewport_->activeSketchIndex() == index) modelTree_->setCurrentItem(item);
    }
    const QVector<ExtrusionObject> &extrusions = viewport_->extrusions();
    for (int index = 0; index < extrusions.size(); ++index) {
        const ExtrusionObject &body = extrusions.at(index);
        QTreeWidgetItem *item = addObject(body.error.isEmpty() ? body.name : body.name + QStringLiteral("  \u26A0"),
                                          kTreeExtrusion, index, body.visible);
        if (!body.error.isEmpty()) {
            item->setForeground(0, QColor(255, 150, 90));
            item->setToolTip(0, QStringLiteral("Rigenerazione non riuscita: ") + body.error);
        }
        if (body.operation < 0 && body.error.isEmpty()) {
            if (body.feature == BodyFeature::Revolution) {
                item->setToolTip(0, QStringLiteral("Rivoluzione di %1 di %2\u00B0").arg(sketches.value(body.sketchIndex).name).arg(body.revolveAngle));
            } else if (body.feature == BodyFeature::Blend) {
                item->setToolTip(0, QStringLiteral("%1 %2 su %3 spigoli")
                    .arg(body.blendChamfer ? QStringLiteral("Smusso di") : QStringLiteral("Raccordo di raggio"))
                    .arg(body.blendSize).arg(body.blendEdges.size()));
            } else if (body.feature == BodyFeature::Primitive) {
                item->setToolTip(0, QStringLiteral("Origine (%1, %2, %3), %4")
                    .arg(body.primitive.origin[0]).arg(body.primitive.origin[1]).arg(body.primitive.origin[2])
                    .arg(planeNames().value(body.primitive.plane)));
            }
        }
        if (body.operation < 0 && body.feature == BodyFeature::Blend) {
            auto *child = new QTreeWidgetItem(item, {QStringLiteral("Base: ") + extrusions.value(body.firstBody).name});
            child->setData(0, Qt::UserRole, kTreeInfo);
            child->setFlags(Qt::ItemIsEnabled);
            child->setForeground(0, QColor(140, 160, 175));
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
            for (const QString &operand : {QStringLiteral("A: ") + firstName, QStringLiteral("B: ") + secondName}) {
                auto *child = new QTreeWidgetItem(item, {operand});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            item->setExpanded(true);
        }
        if (selection.kind == SceneObjectKind::Extrusion && selection.index == index) modelTree_->setCurrentItem(item);
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
    if (dark && application) application->setStyleSheet(QStringLiteral("QMainWindow { background: #111820; color: #d8e4ea; }QMenuBar,QToolBar,QStatusBar { background: #1b2730; color: #d8e4ea; }QMenu { background: #202d36; color: #d8e4ea; }"));
}
