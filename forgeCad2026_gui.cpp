#include "forgeCad2026_gui.h"
#include "cuda_support.h"
#include "cad_curve_solver.h"
#include "cad_history.h"
#include "cad_kernel.h"
#include "cad_kernel_lab.h"
#include "cad_snap.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QWheelEvent>
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
        for (ExtrusionObject &body : extrusions_) ForgeCad::tessellate(body.shape, tessellationQuality_, body.display);
        update();
    }
    void setWheelZoomEnabled(bool enabled) { wheelZoomEnabled_ = enabled; }
    void zoomIn() { zoom_ = qMax(3.0f, zoom_ - 0.8f); update(); }
    void zoomOut() { zoom_ = qMin(18.0f, zoom_ + 0.8f); update(); }
    void resetZoom() { zoom_ = 8.0f; update(); }
    void setReferencePlanesVisible(bool visible) { referencePlanesVisible_ = visible; update(); }
    void setPlaneVisible(int plane, bool visible) {
        if (plane < 0 || plane >= 3) return;
        planeVisible_[plane] = visible;
        update();
    }
    const BackgroundSettings &background() const { return background_; }
    void setBackground(const BackgroundSettings &background) { background_ = background; update(); }
    void setDrawingTool(DrawingTool tool) {
        drawingTool_ = tool;
        hasPendingPoint_ = false;
        curveControlPoints_.clear();
        update();
    }

    void setSelectionCallback(std::function<void(SceneSelection)> callback) {
        selectionCallback_ = std::move(callback);
    }
    void setDocumentChangedCallback(std::function<void()> callback) {
        documentChangedCallback_ = std::move(callback);
    }
    void setPlaneContextCallback(std::function<void(int)> callback) {
        planeContextCallback_ = std::move(callback);
    }
    void setSketchModeCallback(std::function<void(bool)> callback) {
        sketchModeCallback_ = std::move(callback);
    }
    void setRendererCallback(std::function<void(const QString &)> callback) {
        rendererCallback_ = std::move(callback);
    }
    void setKernelLabCallback(std::function<void(const QString &)> callback) {
        kernelLabCallback_ = std::move(callback);
    }

    // Menu Debug: l'estrusione selezionata rifatta con il kernel sperimentale e
    // confrontata con quella OCCT. Restituisce un messaggio d'errore o vuoto.
    QString compareSelectedExtrusionWithKernel() {
        if (selection_.kind != SceneObjectKind::Extrusion || selection_.index < 0 || selection_.index >= extrusions_.size())
            return QStringLiteral("Seleziona prima un'estrusione (nella vista o nell'albero modello).");
        const ExtrusionObject &body = extrusions_.at(selection_.index);
        if (body.operation >= 0) {
            const QString error = kernelLab_.compareDocumentBody(sketches_, extrusions_, selection_.index);
            kernelLabProbe_ = {};
            if (error.isEmpty() && kernelLabCallback_) kernelLabCallback_(kernelLab_.summary());
            update();
            return error;
        }
        if (body.sketchIndex < 0 || body.sketchIndex >= sketches_.size())
            return QStringLiteral("Lo schizzo dell'estrusione non esiste piu'.");
        const QString error = kernelLab_.compareSketchExtrusion(sketches_.at(body.sketchIndex), body.distance, body.shape);
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
        const ExtrusionObject &second = extrusions_.at(secondIndex);
        if (!first.solid || !second.solid)
            return QStringLiteral("Le operazioni booleane richiedono due solidi chiusi.");
        ExtrusionObject result;
        result.name = name;
        result.plane = first.plane;
        result.operation = int(operation);
        result.firstBody = firstIndex;
        result.secondBody = secondIndex;
        QString error;
        result.shape = ForgeCad::booleanOperation(first.shape, second.shape, operation, &error);
        if (result.shape.IsNull()) return error;
        result.solid = ForgeCad::isSolidShape(result.shape);
        ForgeCad::tessellate(result.shape, tessellationQuality_, result.display);
        recordUndo();
        extrusions_[firstIndex].visible = false;
        extrusions_[secondIndex].visible = false;
        extrusions_.append(result);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        hover_ = {};
        documentChanged();
        return {};
    }

    // Estrusione esatta dei profili dello schizzo attivo. Restituisce l'errore.
    QString createExtrusion(double distance, const QString &name) {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size())
            return QStringLiteral("Nessuno schizzo attivo.");
        ExtrusionObject extrusion;
        extrusion.name = name;
        extrusion.sketchIndex = activeSketch_;
        extrusion.plane = sketches_.at(activeSketch_).plane;
        extrusion.distance = distance;
        QString error;
        extrusion.shape = ForgeCad::buildExtrusion(sketches_.at(activeSketch_), distance, extrusion.solid, &error);
        if (extrusion.shape.IsNull()) return error;
        ForgeCad::tessellate(extrusion.shape, tessellationQuality_, extrusion.display);
        recordUndo();
        extrusions_.append(extrusion);
        selection_ = {SceneObjectKind::Extrusion, int(extrusions_.size()) - 1, -1};
        documentChanged();
        return {};
    }

    void selectPlane(int plane) { selectObject(SceneObjectKind::Plane, plane); }

    int createSketch(int plane, const QString &name) {
        recordUndo();
        sketches_.append({name, plane, {}, {}, {}, {}, {}, {}});
        activeSketch_ = sketches_.size() - 1;
        documentChanged();
        beginSketchMode(plane);
        return activeSketch_;
    }

    void selectSketch(int index) {
        if (index < 0 || index >= sketches_.size()) return;
        activeSketch_ = index;
        beginSketchMode(sketches_.at(index).plane);
    }

    void beginSketchMode(int plane) {
        activePlane_ = plane;
        selectedPlane_ = plane;
        sketchMode_ = true;
        sketchCameraLocked_ = true;
        hasPendingPoint_ = false;
        curveControlPoints_.clear();
        sketchSelections_.clear();
        // Le selezioni di punti si riferiscono allo schizzo precedente.
        selectedPoints_.clear();
        hover_ = {};
        sketchHover_ = {};
        setViewNormal(plane);
        if (sketchModeCallback_) sketchModeCallback_(true);
        update();
    }

    void endSketchMode() {
        sketchMode_ = false;
        sketchCameraLocked_ = false;
        hasPendingPoint_ = false;
        lastSnapKind_ = SnapKind::None;
        sketchSelections_.clear();
        selectedPoints_.clear();
        curveControlPoints_.clear();
        sketchHover_ = {};
        if (sketchModeCallback_) sketchModeCallback_(false);
        update();
    }

    void setViewPreset(int preset) {
        switch (preset) {
        case 0: yaw_ = 0.0f; pitch_ = 0.0f; break;
        case 1: yaw_ = 180.0f; pitch_ = 0.0f; break;
        case 2: yaw_ = 90.0f; pitch_ = 0.0f; break;
        case 3: yaw_ = 0.0f; pitch_ = -90.0f; break;
        case 4: yaw_ = -32.0f; pitch_ = 22.0f; break;
        case 5: yaw_ = -45.0f; pitch_ = 12.0f; break;
        default: break;
        }
        update();
    }

    void setViewNormal(int plane) {
        if (plane == 0) setViewPreset(0);       // XY, normal Z
        else if (plane == 1) setViewPreset(3);  // XZ, normal Y
        else setViewPreset(2);                  // YZ, normal X
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
    }

    void resizeGL(int width, int height) override { glViewport(0, 0, width, height); }

    void paintGL() override {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        drawBackgroundGradient();
        const float aspect = float(width()) / float(qMax(1, height()));
        const float viewScale = zoom_ / 8.0f;
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-4.0 * aspect * viewScale, 4.0 * aspect * viewScale,
            -4.0 * viewScale, 4.0 * viewScale, -30.0, 30.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -zoom_);
        glRotatef(pitch_, 1.0f, 0.0f, 0.0f);
        glRotatef(yaw_, 0.0f, 1.0f, 0.0f);
        drawReferencePlanes();
        drawGrid();
        configureLighting();
        drawExtrusions();
        drawKernelLab();
        drawSketch();
        drawSnapMarkers();
        drawReferenceLabels();
        drawSelectionHighlight();
        drawKernelLabOverlay();
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape && sketchMode_) {
            endSketchMode();
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
        if (!sketchMode_ && event->button() == Qt::LeftButton) {
            selection_ = pickSceneObject(lastMousePosition_);
            if (selection_.kind == SceneObjectKind::Plane) selectedPlane_ = selection_.index;
            if (selectionCallback_) selectionCallback_(selection_);
            update();
            return;
        }
        if (sketchMode_ && event->button() == Qt::LeftButton) {
            const QPointF rawPoint = screenToSketchPoint(lastMousePosition_);
            if (event->modifiers() & Qt::ControlModifier) {
                if (!selectPointWithControl(rawPoint)) selectSketchElement(rawPoint, true);
                return;
            }
            if ((event->modifiers() & Qt::ShiftModifier)
                && (drawingTool_ == DrawingTool::Spline || drawingTool_ == DrawingTool::Nurbs)) {
                addControlPointToCurve(rawPoint);
                return;
            }
            if (findCurveEditPoint(rawPoint, draggingCurveIndex_, draggingControlIndex_, draggingPointKind_)) {
                draggingControlPoint_ = true;
                dragSnapshot_ = documentState();
                dragRecorded_ = false;
                return;
            }
            const SketchElementSelection hit = findSketchElement(rawPoint);
            if (hit.kind >= 0) {
                selectSketchElement(rawPoint, false);
                return;
            }
            if (drawingTool_ == DrawingTool::Circle || drawingTool_ == DrawingTool::Arc
                || drawingTool_ == DrawingTool::Polygon) {
                curveControlPoints_.append(snapPoint(rawPoint));
                hasPendingPoint_ = true;
                const int requiredPoints = drawingTool_ == DrawingTool::Arc ? 3 : 2;
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
            const QPointF point = snapPoint(rawPoint);
            if (!hasPendingPoint_) {
                pendingPoint_ = point;
                hasPendingPoint_ = true;
                return;
            }
            int appliedConstraint = constraintMode_;
            const QPointF constrainedPoint = constrainLinePoint(point, &appliedConstraint);
            if (pointDistance(constrainedPoint, pendingPoint_) <= ForgeCad::kSketchConnectionTolerance) return;
            if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) {
                recordUndo();
                sketches_[activeSketch_].segments.append(qMakePair(pendingPoint_, constrainedPoint));
                sketches_[activeSketch_].constraints.append(appliedConstraint);
                sketches_[activeSketch_].segmentLengths.append(lineLength_);
                sketches_[activeSketch_].segmentAngles.append(lineAngle_);
                sketchEdited();
            }
            pendingPoint_ = constrainedPoint;
            if (drawingTool_ == DrawingTool::Line) hasPendingPoint_ = false;
            update();
            return;
        }
    }

    // Applica al secondo estremo di una linea i vincoli attivi: orizzontale /
    // verticale (o automatico), angolo e lunghezza quotati. Calcolo in double.
    QPointF constrainLinePoint(const QPointF &point, int *appliedConstraint = nullptr) const {
        QPointF constrained = point;
        int applied = constraintMode_;
        if (constraintMode_ == 1 || (constraintMode_ == 0
            && std::abs(point.x() - pendingPoint_.x()) >= std::abs(point.y() - pendingPoint_.y()))) {
            constrained.setY(pendingPoint_.y());
            applied = 1;
        } else if (constraintMode_ == 2 || constraintMode_ == 0) {
            constrained.setX(pendingPoint_.x());
            applied = 2;
        }
        const double targetLength = lineLength_ > 0.0 ? lineLength_ : pointDistance(constrained, pendingPoint_);
        if (lineAngle_ >= 0.0 && targetLength > 0.0) {
            const double radians = lineAngle_ * M_PI / 180.0;
            constrained = pendingPoint_ + QPointF(std::cos(radians) * targetLength, std::sin(radians) * targetLength);
        }
        if (lineLength_ > 0.0) {
            QPointF direction = constrained - pendingPoint_;
            if (pointLength(direction) <= 0.0) direction = point - pendingPoint_;
            if (pointLength(direction) > 0.0)
                constrained = pendingPoint_ + direction / pointLength(direction) * lineLength_;
        }
        if (appliedConstraint) *appliedConstraint = applied;
        return constrained;
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
                QAction *hide = menu.addAction(QStringLiteral("Nascondi"));
                const QAction *chosen = menu.exec(event->globalPos());
                if (chosen && chosen == editSketch) selectSketch(hit.index);
                else if (chosen == hide) setObjectVisible(hit.kind, hit.index, false);
                event->accept();
                return;
            }
        }
        QOpenGLWidget::contextMenuEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        const QPoint currentPosition = event->position().toPoint();
        if (sketchMode_) {
            lastMousePosition_ = currentPosition;
            const QPointF rawPoint = screenToSketchPoint(currentPosition);
            cursorSketchPoint_ = snapPoint(rawPoint, drawingTool_ != DrawingTool::Spline
                && drawingTool_ != DrawingTool::Nurbs);
            if (draggingControlPoint_ && draggingCurveIndex_ >= 0) {
                if (!dragRecorded_) {
                    history_.record(dragSnapshot_);
                    dragRecorded_ = true;
                }
                CurveObject &curve = sketches_[activeSketch_].curves[draggingCurveIndex_];
                if (draggingPointKind_ == EditablePointKind::Control) {
                    curve.controlPoints[draggingControlIndex_] = curve.tool == DrawingTool::Spline
                        || curve.tool == DrawingTool::Nurbs ? rawPoint : cursorSketchPoint_;
                } else if (draggingPointKind_ == EditablePointKind::TangentIn) {
                    curve.tangentHandles[draggingControlIndex_].first = rawPoint;
                } else {
                    curve.tangentHandles[draggingControlIndex_].second = rawPoint;
                }
                ForgeCad::recalculateCurve(curve, tessellationQuality_);
            } else {
                snapPoint(rawPoint, drawingTool_ != DrawingTool::Spline
                    && drawingTool_ != DrawingTool::Nurbs);
                sketchHover_ = findSketchElement(rawPoint);
            }
            update();
            return;
        }
        if (!(event->buttons() & Qt::LeftButton) && kernelLab_.shape() != ForgeCad::KernelLabShape::None)
            probeKernelLab(currentPosition);
        if (!(event->buttons() & Qt::LeftButton)) {
            const SceneSelection hover = pickSceneObject(currentPosition);
            if (hover != hover_) {
                hover_ = hover;
                update();
            }
        }
        if (event->buttons() & Qt::LeftButton) {
            const QPoint delta = currentPosition - lastMousePosition_;
            yaw_ += delta.x() * 0.5f;
            pitch_ = qBound(-89.0f, pitch_ + delta.y() * 0.5f, 89.0f);
            update();
        }
        lastMousePosition_ = currentPosition;
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
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
        zoom_ = qBound(3.0f, zoom_ - float(delta) * 0.006f, 18.0f);
        update();
        event->accept();
    }

private:
    DocumentState documentState() const { return {sketches_, extrusions_}; }

    // Salva lo stato corrente nella cronologia: va chiamata subito prima di
    // una modifica al documento, seguita da documentChanged() a modifica fatta.
    void recordUndo() { history_.record(documentState()); }

    void documentChanged() {
        if (documentChangedCallback_) documentChangedCallback_();
        update();
    }

    // Modifica allo schizzo attivo: rigenera i corpi che ne dipendono.
    void sketchEdited() {
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
            if (body.operation < 0) {
                if (body.sketchIndex != sketchIndex) continue;
                body.error.clear();
                body.shape = ForgeCad::buildExtrusion(sketches_.at(sketchIndex), body.distance, body.solid, &body.error);
            } else {
                const bool firstDirty = body.firstBody >= 0 && body.firstBody < index && dirty.at(body.firstBody);
                const bool secondDirty = body.secondBody >= 0 && body.secondBody < index && dirty.at(body.secondBody);
                if (!firstDirty && !secondDirty) continue;
                body.error.clear();
                body.shape = ForgeCad::booleanOperation(extrusions_.at(body.firstBody).shape,
                                                        extrusions_.at(body.secondBody).shape,
                                                        BooleanOperation(body.operation), &body.error);
                body.solid = ForgeCad::isSolidShape(body.shape);
            }
            ForgeCad::tessellate(body.shape, tessellationQuality_, body.display);
            dirty[index] = true;
        }
    }

    void restoreDocument(DocumentState state) {
        sketches_ = std::move(state.sketches);
        extrusions_ = std::move(state.extrusions);
        for (SketchObject &sketch : sketches_) {
            for (CurveObject &curve : sketch.curves) ForgeCad::recalculateCurve(curve, tessellationQuality_);
        }
        for (ExtrusionObject &body : extrusions_) {
            if (body.display.quality != tessellationQuality_)
                ForgeCad::tessellate(body.shape, tessellationQuality_, body.display);
        }
        hasPendingPoint_ = false;
        curveControlPoints_.clear();
        sketchSelections_.clear();
        selectedPoints_.clear();
        sketchHover_ = {};
        hover_ = {};
        draggingControlPoint_ = false;
        dragRecorded_ = false;
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
        if (hit.kind < 0) {
            if (!additive) sketchSelections_.clear();
            update();
            return;
        }
        if (!additive) sketchSelections_.clear();
        for (int index = 0; index < sketchSelections_.size(); ++index) {
            if (sketchSelections_.at(index) == hit) {
                if (additive) sketchSelections_.removeAt(index);
                update();
                return;
            }
        }
        sketchSelections_.append(hit);
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
        if (selected.kind < 0) return false;
        if (selectedPoints_.size() >= 2 || (selectedPoints_.size() == 1 && !isValidSelectedPoint(selectedPoints_.first())))
            selectedPoints_.clear();
        selectedPoints_.append(selected);
        if (selectedPoints_.size() == 2) {
            const SelectedPoint first = selectedPoints_.at(0);
            const SelectedPoint second = selectedPoints_.at(1);
            const QPointF merged = (selectedPointPosition(first) + selectedPointPosition(second)) * 0.5;
            recordUndo();
            setSelectedPointPosition(first, merged);
            setSelectedPointPosition(second, merged);
            if (first.kind == 1) ForgeCad::recalculateCurve(sketches_[activeSketch_].curves[first.element], tessellationQuality_);
            if (second.kind == 1) ForgeCad::recalculateCurve(sketches_[activeSketch_].curves[second.element], tessellationQuality_);
            sketches_[activeSketch_].coincidentConstraints.append({first.kind, first.element, first.point,
                                                                     second.kind, second.element, second.point});
            sketchEdited();
        }
        update();
        return true;
    }

    bool isValidSelectedPoint(const SelectedPoint &point) const {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size() || point.element < 0) return false;
        const SketchObject &sketch = sketches_.at(activeSketch_);
        if (point.kind == 0) return point.element < sketch.segments.size() && (point.point == 0 || point.point == 1);
        return point.kind == 1 && point.element < sketch.curves.size() && point.point >= 0
            && point.point < sketch.curves.at(point.element).controlPoints.size();
    }

    QPointF selectedPointPosition(const SelectedPoint &point) const {
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

    QPointF projectWorldPoint(const QVector3D &point) const {
        const float aspect = float(width()) / float(qMax(1, height()));
        const float viewScale = zoom_ / 8.0f;
        QMatrix4x4 model;
        model.translate(0.0f, 0.0f, -zoom_);
        model.rotate(pitch_, 1.0f, 0.0f, 0.0f);
        model.rotate(yaw_, 0.0f, 1.0f, 0.0f);
        const QVector3D cameraPoint = model.map(point);
        return QPointF((cameraPoint.x() / (4.0f * aspect * viewScale) + 1.0f) * width() * 0.5f,
                   (1.0f - cameraPoint.y() / (4.0f * viewScale)) * height() * 0.5f);
    }

    void drawReferenceLabels() {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(QFont(QStringLiteral("Sans"), 10, QFont::DemiBold));
        painter.setPen(QColor(150, 200, 255));
        painter.drawText(projectWorldPoint(QVector3D(3.9f, 0.0f, 0.0f)), QStringLiteral("X - asse X"));
        painter.setPen(QColor(150, 240, 190));
        painter.drawText(projectWorldPoint(QVector3D(0.0f, 3.9f, 0.0f)), QStringLiteral("Y - asse Y"));
        painter.setPen(QColor(255, 170, 140));
        painter.drawText(projectWorldPoint(QVector3D(0.0f, 0.0f, 3.9f)), QStringLiteral("Z - asse Z"));
        painter.setPen(QColor(150, 200, 255));
        painter.drawText(projectWorldPoint(QVector3D(3.0f, 3.0f, 0.0f)), QStringLiteral("Piano XY"));
        painter.setPen(QColor(150, 240, 190));
        painter.drawText(projectWorldPoint(QVector3D(3.0f, 0.0f, 3.0f)), QStringLiteral("Piano XZ"));
        painter.setPen(QColor(255, 170, 140));
        painter.drawText(projectWorldPoint(QVector3D(0.0f, 3.0f, 3.0f)), QStringLiteral("Piano YZ"));
        if (sketchMode_) {
            painter.setPen(QColor(255, 220, 120));
            painter.drawText(20, 24, QStringLiteral("MODALITA SCHIZZO - VISTA NORMALE BLOCCATA"));
            drawSketchDimensions(painter);
        }
    }

    void drawSketchDimensions(QPainter &painter) const {
        if (activeSketch_ < 0 || activeSketch_ >= sketches_.size()) return;
        const SketchObject &sketch = sketches_.at(activeSketch_);
        painter.setFont(QFont(QStringLiteral("Sans"), 9, QFont::DemiBold));
        for (int index = 0; index < sketch.segments.size(); ++index) {
            const auto &segment = sketch.segments.at(index);
            const QVector3D first = mapSketchPoint(segment.first, sketch.plane);
            const QVector3D second = mapSketchPoint(segment.second, sketch.plane);
            const QPointF firstScreen = projectWorldPoint(first);
            const QPointF secondScreen = projectWorldPoint(second);
            const QPointF midpoint = (firstScreen + secondScreen) * 0.5;
            const float dx = float(secondScreen.x() - firstScreen.x());
            const float dy = float(secondScreen.y() - firstScreen.y());
            const float scale = qMax(1.0f, qMax(qAbs(dx), qAbs(dy)));
            const QPointF labelPosition = midpoint + QPointF(-dy / scale * 14.0, dx / scale * 14.0);
            const double length = pointDistance(segment.first, segment.second);
            const bool constrained = index < sketch.segmentLengths.size()
                && sketch.segmentLengths.at(index) > 0.0;
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

    QVector3D mapSketchPoint(const QPointF &point, int plane) const {
        return ForgeCad::sketchToDisplay(point, plane);
    }

    QPointF screenToSketchPoint(const QPoint &position) const {
        const double aspect = double(width()) / double(qMax(1, height()));
        const double viewScale = double(zoom_) / 8.0;
        return QPointF((double(position.x()) / double(qMax(1, width())) - 0.5) * 8.0 * aspect * viewScale,
                       (0.5 - double(position.y()) / double(qMax(1, height()))) * 8.0 * viewScale);
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
        }
        const ForgeCad::SnapResult result = ForgeCad::snapSegments(
            point, segments, points, snapEnabled_, snapToGrid, snapSpacing_, pickTolerance(10.0));
        lastSnapKind_ = result.kind;
        lastSnapPoint_ = result.point;
        return result.point;
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
                const QVector3D world = mapSketchPoint(control, sketch.plane);
                glVertex3f(world.x(), world.y(), world.z() + 0.07f);
            }
            for (const auto &handles : curve.tangentHandles) {
                const QVector3D first = mapSketchPoint(handles.first, sketch.plane);
                const QVector3D second = mapSketchPoint(handles.second, sketch.plane);
                glVertex3f(first.x(), first.y(), first.z() + 0.08f);
                glVertex3f(second.x(), second.y(), second.z() + 0.08f);
            }
        }
        glColor3f(1.0f, 0.45f, 0.25f);
        for (const QPointF &control : curveControlPoints_) {
            const QVector3D world = mapSketchPoint(control, activePlane_);
            glVertex3f(world.x(), world.y(), world.z() + 0.08f);
        }
        for (const auto &segment : sketch.segments) {
            glColor3f(1.0f, 0.85f, 0.15f);
            for (const QPointF &endpoint : {segment.first, segment.second}) {
                const QVector3D world = mapSketchPoint(endpoint, sketch.plane);
                glVertex3f(world.x(), world.y(), world.z() + 0.03f);
            }
            const QVector3D midpoint = mapSketchPoint((segment.first + segment.second) * 0.5, sketch.plane);
            glColor3f(0.25f, 1.0f, 0.35f);
            glVertex3f(midpoint.x(), midpoint.y(), midpoint.z() + 0.03f);
        }
        if (lastSnapKind_ != SnapKind::None) {
            const QVector3D world = mapSketchPoint(lastSnapPoint_, sketch.plane);
            if (lastSnapKind_ == SnapKind::Nearest) glColor3f(0.15f, 0.85f, 1.0f);
            else if (lastSnapKind_ == SnapKind::Midpoint) glColor3f(0.25f, 1.0f, 0.35f);
            else glColor3f(1.0f, 0.85f, 0.15f);
            glVertex3f(world.x(), world.y(), world.z() + 0.05f);
        }
        glEnd();
        glLineWidth(1.0f);
        glBegin(GL_LINES);
        for (const CurveObject &curve : sketch.curves) {
            for (int index = 0; index < curve.tangentHandles.size(); ++index) {
                const QVector3D control = mapSketchPoint(curve.controlPoints.at(index), sketch.plane);
                const auto &handles = curve.tangentHandles.at(index);
                const QVector3D incoming = mapSketchPoint(handles.first, sketch.plane);
                const QVector3D outgoing = mapSketchPoint(handles.second, sketch.plane);
                glColor3f(0.35f, 0.75f, 1.0f);
                glVertex3f(incoming.x(), incoming.y(), incoming.z() + 0.06f);
                glVertex3f(control.x(), control.y(), control.z() + 0.06f);
                glVertex3f(control.x(), control.y(), control.z() + 0.06f);
                glVertex3f(outgoing.x(), outgoing.y(), outgoing.z() + 0.06f);
            }
        }
        glEnd();
        if (selectedPoints_.size() == 2 && isValidSelectedPoint(selectedPoints_.at(0))
            && isValidSelectedPoint(selectedPoints_.at(1))) {
            const QVector3D first = mapSketchPoint(selectedPointPosition(selectedPoints_.at(0)), sketch.plane);
            const QVector3D second = mapSketchPoint(selectedPointPosition(selectedPoints_.at(1)), sketch.plane);
            glColor3f(0.30f, 1.0f, 0.45f);
            glBegin(GL_LINES);
            glVertex3f(first.x(), first.y(), first.z() + 0.10f);
            glVertex3f(second.x(), second.y(), second.z() + 0.10f);
            glEnd();
        }
    }

    // Raggio di vista (proiezione ortografica) che passa per un pixel.
    void viewRay(const QPoint &position, QVector3D &origin, QVector3D &direction) const {
        const float aspect = float(width()) / float(qMax(1, height()));
        const float viewScale = zoom_ / 8.0f;
        QMatrix4x4 rotation;
        rotation.rotate(pitch_, 1.0f, 0.0f, 0.0f);
        rotation.rotate(yaw_, 0.0f, 1.0f, 0.0f);
        const QMatrix4x4 inverse = rotation.inverted();
        origin = inverse.map(QVector3D(
            (float(position.x()) / float(qMax(1, width())) - 0.5f) * 8.0f * aspect * viewScale,
            (0.5f - float(position.y()) / float(qMax(1, height()))) * 8.0f * viewScale,
            zoom_ + 30.0f));
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
                considerSegment(projectWorldPoint(mapSketchPoint(segment.first, sketch.plane)),
                                projectWorldPoint(mapSketchPoint(segment.second, sketch.plane)), sketchIndex);
            }
            for (const CurveObject &curve : sketch.curves) {
                for (int sample = 1; sample < curve.samples.size(); ++sample) {
                    considerSegment(projectWorldPoint(mapSketchPoint(curve.samples.at(sample - 1), sketch.plane)),
                                    projectWorldPoint(mapSketchPoint(curve.samples.at(sample), sketch.plane)),
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
            if (!extrusion.visible || extrusion.shape.IsNull()) continue;
            double distance = 0.0;
            if (ForgeCad::intersectRay(extrusion.shape, origin, direction, distance) && distance < nearest) {
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

    QVector<QPointF> projectSketchPolyline(const QVector<QPointF> &points, int plane) const {
        QVector<QPointF> projected;
        projected.reserve(points.size());
        for (const QPointF &point : points) projected.append(projectWorldPoint(mapSketchPoint(point, plane)));
        return projected;
    }

    void highlightSketchElement(QPainter &painter, const SketchObject &sketch,
                                const SketchElementSelection &element, const QColor &color) const {
        if (element.kind == 0 && element.index >= 0 && element.index < sketch.segments.size()) {
            const auto &segment = sketch.segments.at(element.index);
            const QVector<QPointF> points = projectSketchPolyline({segment.first, segment.second}, sketch.plane);
            strokeHighlight(painter, points, false, color);
            painter.setBrush(color);
            for (const QPointF &point : points) painter.drawEllipse(point, 4.0, 4.0);
        } else if (element.kind == 1 && element.index >= 0 && element.index < sketch.curves.size()) {
            strokeHighlight(painter, projectSketchPolyline(sketch.curves.at(element.index).samples, sketch.plane),
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
        glColor3f(1.0f, 0.75f, 0.15f);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        for (int sketchIndex = 0; sketchIndex < sketches_.size(); ++sketchIndex) {
            if (!isSketchDrawn(sketchIndex)) continue;
            const SketchObject &sketch = sketches_.at(sketchIndex);
            for (const auto &segment : sketch.segments) {
                const QVector3D first = mapSketchPoint(segment.first, sketch.plane);
                const QVector3D second = mapSketchPoint(segment.second, sketch.plane);
                glVertex3f(first.x(), first.y(), first.z() + 0.02f);
                glVertex3f(second.x(), second.y(), second.z() + 0.02f);
            }
        }
        glEnd();
        glLineWidth(2.5f);
        for (int sketchIndex = 0; sketchIndex < sketches_.size(); ++sketchIndex) {
            if (!isSketchDrawn(sketchIndex)) continue;
            const SketchObject &sketch = sketches_.at(sketchIndex);
            for (const CurveObject &curve : sketch.curves) {
                glColor3f(curve.tool == DrawingTool::Nurbs ? 0.85f : 0.95f,
                          curve.tool == DrawingTool::Nurbs ? 0.35f : 0.65f, 1.0f);
                glBegin(GL_LINE_STRIP);
                for (const QPointF &sample : curve.samples) {
                    const QVector3D world = mapSketchPoint(sample, sketch.plane);
                    glVertex3f(world.x(), world.y(), world.z() + 0.04f);
                }
                glEnd();
                glColor3f(0.75f, 0.75f, 0.80f);
                glBegin(GL_LINE_STRIP);
                for (const QPointF &control : curve.controlPoints) {
                    const QVector3D world = mapSketchPoint(control, sketch.plane);
                    glVertex3f(world.x(), world.y(), world.z() + 0.03f);
                }
                glEnd();
            }
        }
        glBegin(GL_LINES);
        if (hasPendingPoint_) {
            const QVector3D world = mapSketchPoint(pendingPoint_, activePlane_);
            glColor3f(1.0f, 0.35f, 0.20f);
            glVertex3f(world.x() - 0.12f, world.y(), world.z() + 0.05f);
            glVertex3f(world.x() + 0.12f, world.y(), world.z() + 0.05f);
            if (drawingTool_ == DrawingTool::Line || drawingTool_ == DrawingTool::Polyline) {
                const QPointF previewPoint = constrainLinePoint(cursorSketchPoint_);
                const QVector3D previewWorld = mapSketchPoint(previewPoint, activePlane_);
                glColor3f(1.0f, 0.45f, 0.20f);
                glVertex3f(world.x(), world.y(), world.z() + 0.05f);
                glVertex3f(previewWorld.x(), previewWorld.y(), previewWorld.z() + 0.05f);
            }
        } else if (sketchMode_ && (drawingTool_ == DrawingTool::Line
                                   || drawingTool_ == DrawingTool::Polyline)) {
            const QVector3D world = mapSketchPoint(cursorSketchPoint_, activePlane_);
            glColor3f(1.0f, 0.45f, 0.20f);
            glVertex3f(world.x() - 0.10f, world.y(), world.z() + 0.05f);
            glVertex3f(world.x() + 0.10f, world.y(), world.z() + 0.05f);
            glVertex3f(world.x(), world.y() - 0.10f, world.z() + 0.05f);
            glVertex3f(world.x(), world.y() + 0.10f, world.z() + 0.05f);
        }
        glEnd();
    }

    // Solo visualizzazione: triangoli e spigoli ricavati dalla forma esatta.
    void drawExtrusionFaces(const ExtrusionObject &extrusion) {
        const BodyDisplay &display = extrusion.display;
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

    void drawExtrusions() {
        glDisable(GL_CULL_FACE);
        const GLfloat noEmission[] = {0.0f, 0.0f, 0.0f, 1.0f};
        for (int index = 0; index < extrusions_.size(); ++index) {
            const ExtrusionObject &extrusion = extrusions_.at(index);
            if (!extrusion.visible || (extrusion.display.vertices.isEmpty() && extrusion.display.edges.isEmpty())) continue;
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
                drawExtrusionFaces(extrusion);
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
        if (hover_ != selection_) outline(hover_, kHoverColor, 6.0f);
        outline(selection_, kSelectionColor, 7.0f);
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

    void drawGrid() {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glColor3f(0.16f, 0.21f, 0.25f);
        glBegin(GL_LINES);
        for (int i = -10; i <= 10; ++i) {
            glVertex3f(float(i), -0.05f, -10); glVertex3f(float(i), -0.05f, 10);
            glVertex3f(-10, -0.05f, float(i)); glVertex3f(10, -0.05f, float(i));
        }
        glEnd();
        glEnable(GL_DEPTH_TEST);
    }

    int displayMode_ = 2;
    float yaw_ = -32.0f, pitch_ = 22.0f, zoom_ = 8.0f;
    QPoint lastMousePosition_;
    int lightingPreset_ = 0, constraintMode_ = 0, selectedPlane_ = 0, activePlane_ = 0, activeSketch_ = -1;
    double lineLength_ = 0.0;
    double lineAngle_ = -1.0;
    int polygonSides_ = 6;
    int tessellationQuality_ = 1;
    bool sketchMode_ = false, sketchCameraLocked_ = false, snapEnabled_ = true;
    bool wheelZoomEnabled_ = true;
    bool hasPendingPoint_ = false, referencePlanesVisible_ = true;
    DrawingTool drawingTool_ = DrawingTool::Line;
    SnapKind lastSnapKind_ = SnapKind::None;
    const double snapSpacing_ = 0.25;
    QPointF pendingPoint_, cursorSketchPoint_, lastSnapPoint_;
    QVector<QPointF> curveControlPoints_;
    bool draggingControlPoint_ = false;
    int draggingCurveIndex_ = -1;
    int draggingControlIndex_ = -1;
    EditablePointKind draggingPointKind_ = EditablePointKind::Control;
    SceneSelection selection_, hover_;
    SketchElementSelection sketchHover_;
    QVector<SketchElementSelection> sketchSelections_;
    QVector<SelectedPoint> selectedPoints_;
    QVector<SketchObject> sketches_;
    QVector<ExtrusionObject> extrusions_;
    ForgeCad::History history_;
    DocumentState dragSnapshot_;
    bool dragRecorded_ = false;
    bool planeVisible_[3] = {true, true, true};
    BackgroundSettings background_;
    std::function<void(SceneSelection)> selectionCallback_;
    std::function<void()> documentChangedCallback_;
    std::function<void(int)> planeContextCallback_;
    std::function<void(bool)> sketchModeCallback_;
    std::function<void(const QString &)> rendererCallback_;
    std::function<void(const QString &)> kernelLabCallback_;
    ForgeCad::KernelLab kernelLab_;
    ForgeCad::KernelLabProbe kernelLabProbe_;
};

// Tipi delle voci dell'albero modello (Qt::UserRole); Qt::UserRole + 1 e' l'indice.
enum TreeItemType { kTreeInfo = -1, kTreeOrigin = 0, kTreePlane = 1, kTreeSketch = 3, kTreeExtrusion = 4 };

static const QStringList &planeNames() {
    static const QStringList names = {QStringLiteral("Piano XY - Superiore"),
                                      QStringLiteral("Piano XZ - Frontale"),
                                      QStringLiteral("Piano YZ - Destro")};
    return names;
}

PdfWindow::PdfWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ForgeCAD - Qt6"));
    resize(1280, 820);
    setMinimumSize(900, 600);
    viewport_ = new CadViewport(this);
    auto *viewport = viewport_;
    setCentralWidget(viewport);

    auto *modelDock = new QDockWidget(QStringLiteral("Albero modello"), this);
    auto *modelTree = new QTreeWidget(modelDock);
    modelTree_ = modelTree;
    modelTree->setHeaderLabel(QStringLiteral("Oggetti scena"));
    modelTree->setMinimumWidth(220);
    modelTree->setContextMenuPolicy(Qt::CustomContextMenu);
    modelDock->setWidget(modelTree);
    addDockWidget(Qt::LeftDockWidgetArea, modelDock);
    rebuildModelTree();
    auto createSketchOnPlane = std::make_shared<std::function<void(int)>>();
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
            [this, modelTree, viewport, createSketchOnPlane](const QPoint &position) {
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
            QAction *toggle = menu.addAction(visible ? QStringLiteral("Nascondi") : QStringLiteral("Mostra"));
            const QAction *chosen = menu.exec(globalPosition);
            if (chosen && chosen == editSketch) viewport->selectSketch(index);
            else if (chosen == toggle) viewport->setObjectVisible(kind, index, !visible);
        }
    });

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    auto *editMenu = menuBar()->addMenu(QStringLiteral("Modifica"));
    undoAction_ = editMenu->addAction(QStringLiteral("Annulla"));
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = editMenu->addAction(QStringLiteral("Ripeti"));
    redoAction_->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    connect(undoAction_, &QAction::triggered, this, [viewport] { viewport->undo(); });
    connect(redoAction_, &QAction::triggered, this, [viewport] { viewport->redo(); });
    updateUndoActions();
    auto *viewMenu = menuBar()->addMenu(QStringLiteral("Visualizza"));
    auto *functionsMenu = menuBar()->addMenu(QStringLiteral("Funzioni"));
    QAction *extrudeAction = functionsMenu->addAction(QStringLiteral("Estrusione..."));
    extrudeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    auto *booleanMenu = functionsMenu->addMenu(QStringLiteral("Booleane"));
    QAction *unionAction = booleanMenu->addAction(QStringLiteral("Unione..."));
    QAction *intersectionAction = booleanMenu->addAction(QStringLiteral("Intersezione..."));
    QAction *differenceAction = booleanMenu->addAction(QStringLiteral("Differenza A - B..."));
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
    auto *wheelZoomAction = viewMenu->addAction(QStringLiteral("Zoom con rotella"));
    wheelZoomAction->setCheckable(true); wheelZoomAction->setChecked(true);
    auto *zoomInAction = viewMenu->addAction(QStringLiteral("Aumenta zoom"));
    auto *zoomOutAction = viewMenu->addAction(QStringLiteral("Riduci zoom"));
    auto *resetZoomAction = viewMenu->addAction(QStringLiteral("Ripristina zoom"));
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
        bool accepted = false;
        const double distance = QInputDialog::getDouble(
            this, QStringLiteral("Estrusione"),
            QStringLiteral("Distanza di estrusione (positiva o negativa):"),
            1.0, -100000.0, 100000.0, 6, &accepted);
        if (!accepted) return;
        const QString name = QStringLiteral("Estrusione %1").arg(viewport->extrusions().size() + 1);
        const QString error = viewport->createExtrusion(distance, name);
        if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Estrusione"), error);
    });
    const auto runBoolean = [this, viewport](BooleanOperation operation, const QString &title) {
        // Solo i solidi chiusi possono essere operandi.
        QStringList names;
        QVector<int> indices;
        const QVector<ExtrusionObject> &bodies = viewport->extrusions();
        for (int index = 0; index < bodies.size(); ++index) {
            if (!bodies.at(index).solid || bodies.at(index).shape.IsNull()) continue;
            names.append(bodies.at(index).visible ? bodies.at(index).name
                                                  : bodies.at(index).name + QStringLiteral(" (nascosto)"));
            indices.append(index);
        }
        if (names.size() < 2) {
            QMessageBox::information(this, title, QStringLiteral("Servono almeno due solidi chiusi nella scena."));
            return;
        }
        const SceneSelection selection = viewport->selection();
        const int preferred = selection.kind == SceneObjectKind::Extrusion ? indices.indexOf(selection.index) : -1;
        const QString firstLabel = operation == BooleanOperation::Difference
            ? QStringLiteral("Oggetto da cui sottrarre (A):") : QStringLiteral("Primo oggetto:");
        const QString secondLabel = operation == BooleanOperation::Difference
            ? QStringLiteral("Oggetto da sottrarre (B):") : QStringLiteral("Secondo oggetto:");
        bool accepted = false;
        const QString firstName = QInputDialog::getItem(this, title, firstLabel, names,
                                                        qMax(0, preferred), false, &accepted);
        if (!accepted) return;
        const int firstChoice = names.indexOf(firstName);
        QStringList others = names;
        others.removeAt(firstChoice);
        const QString secondName = QInputDialog::getItem(this, title, secondLabel, others, 0, false, &accepted);
        if (!accepted) return;
        const int firstIndex = indices.at(firstChoice);
        const int secondIndex = indices.at(names.indexOf(secondName));
        const QString resultName = title + QStringLiteral(" %1").arg(viewport->extrusions().size() + 1);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = viewport->createBoolean(operation, firstIndex, secondIndex, resultName);
        QApplication::restoreOverrideCursor();
        if (!error.isEmpty()) QMessageBox::warning(this, title, error);
    };
    connect(unionAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Union, QStringLiteral("Unione")); });
    connect(intersectionAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Intersection, QStringLiteral("Intersezione")); });
    connect(differenceAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Difference, QStringLiteral("Differenza")); });

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
    QAction *sketchAction = sketchMenu->addAction(QStringLiteral("Disegna segmenti"));
    auto *toolMenu = sketchMenu->addMenu(QStringLiteral("Strumento geometrico"));
    auto *toolGroup = new QActionGroup(this); toolGroup->setExclusive(true);
    auto addTool = [this, toolMenu, toolGroup, viewport](const QString &text, DrawingTool tool, bool checked) {
        auto *action = toolMenu->addAction(text); action->setCheckable(true); action->setChecked(checked); toolGroup->addAction(action);
        connect(action, &QAction::triggered, this, [viewport, tool] { viewport->setDrawingTool(tool); });
        return action;
    };
    QAction *lineTool = addTool(QStringLiteral("Linea"), DrawingTool::Line, true);
    QAction *polylineTool = addTool(QStringLiteral("Polilinea"), DrawingTool::Polyline, false);
    QAction *splineTool = addTool(QStringLiteral("Spline"), DrawingTool::Spline, false);
    QAction *nurbsTool = addTool(QStringLiteral("NURBS"), DrawingTool::Nurbs, false);
    QAction *circleTool = addTool(QStringLiteral("Cerchio"), DrawingTool::Circle, false);
    QAction *arcTool = addTool(QStringLiteral("Arco (centro, inizio, fine)"), DrawingTool::Arc, false);
    QAction *polygonTool = addTool(QStringLiteral("Poligono"), DrawingTool::Polygon, false);
    QAction *polygonSidesAction = toolMenu->addAction(QStringLiteral("Numero lati poligono..."));
    sketchAction->setCheckable(true); sketchAction->setChecked(true);
    QAction *snapAction = sketchMenu->addAction(QStringLiteral("Snap griglia e geometria")); snapAction->setCheckable(true); snapAction->setChecked(true);
    QAction *automaticConstraint = sketchMenu->addAction(QStringLiteral("Vincolo automatico"));
    QAction *freeConstraint = sketchMenu->addAction(QStringLiteral("Nessun vincolo (linea libera)"));
    QAction *horizontalConstraint = sketchMenu->addAction(QStringLiteral("Vincolo orizzontale"));
    QAction *verticalConstraint = sketchMenu->addAction(QStringLiteral("Vincolo verticale"));
    QAction *lengthConstraint = sketchMenu->addAction(QStringLiteral("Quota lunghezza..."));
    QAction *angleConstraint = sketchMenu->addAction(QStringLiteral("Quota angolare..."));
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

    auto *drawingToolbar = addToolBar(QStringLiteral("Strumenti schizzo")); drawingToolbar->setMovable(false); drawingToolbar->setVisible(false);
    drawingToolbar->addAction(lineTool); drawingToolbar->addAction(polylineTool); drawingToolbar->addAction(splineTool); drawingToolbar->addAction(nurbsTool); drawingToolbar->addAction(circleTool); drawingToolbar->addAction(arcTool); drawingToolbar->addAction(polygonTool); drawingToolbar->addSeparator(); drawingToolbar->addAction(exitSketch);
    auto viewActions = std::make_shared<QList<QAction *>>();
    viewport->setSketchModeCallback([viewMenu, viewActions, drawingToolbar](bool active) {
        viewMenu->setEnabled(!active); for (QAction *action : *viewActions) action->setEnabled(!active); drawingToolbar->setVisible(active);
    });

    auto *quit = fileMenu->addAction(QStringLiteral("Esci")); connect(quit, &QAction::triggered, this, &QWidget::close);
    auto *toolbar = addToolBar(QStringLiteral("Modellazione")); toolbar->setMovable(false); toolbar->addAction(modeMenu->actions().at(2)); toolbar->addAction(modeMenu->actions().at(0)); toolbar->addAction(snapAction); toolbar->addWidget(new QLabel(QStringLiteral("  ForgeCAD / Part Studio  ")));
    const QList<QPair<QString, int>> views = {{QStringLiteral("Front"),0},{QStringLiteral("Rear"),1},{QStringLiteral("Right"),2},{QStringLiteral("Top"),3},{QStringLiteral("Isometric"),4},{QStringLiteral("Trimetric"),5}};
    for (int index = 0; index < views.size(); ++index) {
        auto *action = new QAction(views.at(index).first, this); action->setShortcut(QKeySequence(Qt::Key_1 + index)); addAction(action); viewActions->append(action);
        connect(action, &QAction::triggered, this, [viewport, index] { viewport->setViewPreset(index); });
    }
    modeStatus_ = new QLabel(QStringLiteral("Mesh + linee esterne")); statusBar()->addWidget(modeStatus_);
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
    setTheme(true);
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
        QTreeWidgetItem *item = addObject(sketch.name + QStringLiteral(" [") + planeNames().value(sketch.plane)
                                              + QStringLiteral("]"), kTreeSketch, index, sketch.visible);
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
