#include "cad_drawing_browser.h"
#include "cad_drawing_window.h"
#include "cad_toolbar.h"
#include "cad_curve_extract.h"

#include "kernel/fk_cancel.h"
#include "cad_spatial_sketch.h"
#include <QTableWidget>
#include <QHeaderView>
#include "forgeCad2026_gui.h"
#include "forgeCad2026_viewport.h"
#include "forgeCad2026_storyboard.h"
#include "forgeCad2026_dialogs.h"
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
#include "cad_history_graph.h"
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
#include <QThread>
#include <QPointer>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QMap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QTextBrowser>
#include <QTextEdit>
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
#include <QScrollBar>
#include <QVBoxLayout>
#include <QMenu>
#include "cad_menu.h"
#include <QMenuBar>
#include <QMessageBox>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QLinearGradient>
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
#include "toolbar_editor.h"
#include <QToolButton>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QStyleOptionSlider>
#include <QStyleOptionButton>
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
#include <QFontDatabase>
#include <QStandardItemModel>
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

// I report dei file grandi possono contenere migliaia di righe: scorrono
// nell'area di testo senza aumentare la dimensione della finestra.
static void showImportReport(QWidget *parent, const QString &message) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Importa STEP/IGES"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QTextBrowser(&dialog);
    text->setPlainText(message);
    text->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout->addWidget(text, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    QSize size(720, 420);
    if (const QScreen *screen = dialog.screen()) size = size.boundedTo(screen->availableGeometry().size() * 0.85);
    dialog.setFixedSize(size);
    dialog.exec();
}



static AxesOrientation defaultAxesOrientation();
static void saveDefaultAxesOrientation(const AxesOrientation &o);
static LengthUnit defaultLengthUnit() {
    const int value = QSettings().value(QStringLiteral("units/defaultLength"), int(LengthUnit::Millimeter)).toInt();
    return LengthUnit(qBound(int(LengthUnit::Millimeter), value, int(LengthUnit::Foot)));
}

// Stato temporaneo del solo avvio: conserva i collegamenti fra le fasi,
// non viene trattenuto dalla finestra. Callback/azioni copiano gli stessi valori.
struct PdfWindow::WindowSetup {
    CadViewport * viewport{};
    StoryboardTree * modelTree{};
    std::shared_ptr<std::function<void (int)>> createSketchOnPlane{};
    QMenu * fileMenu{};
    QAction * newAction{};
    QAction * openAction{};
    QAction * saveAction{};
    QAction * newDrawingAction{};
    QAction * openDrawingAction{};
    QAction * importAction{};
    QMenu * editMenu{};
    QAction * deleteAction{};
    QAction * rebuildAction{};
    QMenu * viewMenu{};
    QMenu * storyboardCursorMenu{};
    QMenu * functionPanelMenu{};
    QMenu * functionsMenu{};
    QAction * extrudeAction{};
    QAction * unionAction{};
    QAction * intersectionAction{};
    QAction * differenceAction{};
    QAction * revolveAction{};
    QMenu * primitiveMenu{};
    QAction * filletAction{};
    QAction * chamferAction{};
    QAction * draftAction{};
    QAction * shellAction{};
    QAction * threadAction{};
    QAction * datumAction{};
    QAction * linearPatternAction{};
    QAction * circularPatternAction{};
    QAction * mirrorAction{};
    QAction * moveAction{};
    QAction * scaleAction{};
    QAction * helixAction{};
    QAction * sweepAction{};
    QAction * loftAction{};
    QAction * trimSurfaceAction{};
    QAction * extendSurfaceAction{};
    QAction * copyFacesAction{};
    QAction * offsetSurfaceAction{};
    QAction * deleteFaceAction{};
    QAction * ruledSurfaceAction{};
    QAction * boundarySurfaceAction{};
    QAction * fillSurfaceAction{};
    QAction * thickenAction{};
    QAction * extractedCurveAction{};
    QAction * trimCurve3DAction{};
    QAction * projectedCurveAction{};
    QAction * planarSurfaceAction{};
    QAction * loftSurfaceAction{};
    QAction * sweepSurfaceAction{};
    QAction * sewSurfacesAction{};
    QAction * massAction{};
    QAction * measureAction{};
    QMenu * modeMenu{};
    QMenu * qualityMenu{};
    QActionGroup * qualityGroup{};
    QMenu * lightingMenu{};
    QAction * backgroundAction{};
    QAction * historyGraphAction{};
    QMenu * treeColorMenu{};
    QAction * treeColorAction{};
    QAction * treeColorReset{};
    QMenu * highlightColorMenu{};
    QAction * highlightColorAction{};
    QAction * highlightColorReset{};
    QAction * showAllAction{};
    QAction * planesAction{};
    QAction * gridAction{};
    QMenu * axesMenu{};
    QAction * axesVisibleAction{};
    QAction * axesOnTopAction{};
    QAction * axisLengthAction{};
    QAction * sketchOpacityAction{};
    QMenu * antialiasingMenu{};
    QAction * wheelZoomAction{};
    QAction * zoomInAction{};
    QAction * zoomOutAction{};
    QAction * resetZoomAction{};
    QActionGroup * modeGroup{};
    QAction * hiddenEdgesAction{};
    QAction * topologyIdsAction{};
    QAction * studio{};
    QAction * soft{};
    QAction * inspection{};
    QAction * sectionAction{};
    QAction * sectionParameters{};
    QList<QAction *> primitiveActions{};
    QMenu * optionsMenu{};
    QMenu * unitsMenu{};
    QAction * orientationAction{};
    QMenu * sketchMenu{};
    QAction * newSketchAction{};
    QAction * spatialSketchAction{};
    QAction * faceSketchAction{};
    QAction * selectTool{};
    QAction * lineTool{};
    QAction * polylineTool{};
    QAction * splineTool{};
    QAction * nurbsTool{};
    QAction * circleTool{};
    QAction * arcTool{};
    QAction * threePointArcTool{};
    QAction * tangentArcTool{};
    QAction * polygonTool{};
    QAction * rectangleTool{};
    QAction * centerRectangleTool{};
    QAction * ellipseTool{};
    QAction * constructionTool{};
    QAction * polygonSidesAction{};
    QAction * trimTool{};
    QAction * extendTool{};
    QAction * splitTool{};
    QAction * sketchFilletTool{};
    QAction * sketchChamferTool{};
    QAction * snapAction{};
    QAction * gridSnapAction{};
    QAction * originSnapAction{};
    QAction * automaticConstraint{};
    QAction * freeConstraint{};
    QAction * horizontalConstraint{};
    QAction * verticalConstraint{};
    QAction * lengthConstraint{};
    QAction * angleConstraint{};
    QAction * dimensionAction{};
    QAction * sketchOffsetAction{};
    QAction * sketchMoveAction{};
    QAction * sketchLinearPattern{};
    QAction * sketchCircularPattern{};
    QAction * sketchMirror{};
    QAction * convertTool{};
    QAction * sectionReferences{};
    QAction * symmetryAxisAction{};
    QAction * toggleConstruction{};
    QAction * constraintsAction{};
    QAction * exitSketch{};
    QAction * sketchNormalView{};
    std::shared_ptr<QList<QAction *>> viewActions{};
};

PdfWindow::PdfWindow(QWidget *parent) : QMainWindow(parent) {
    WindowSetup setup;
    initializeSceneAndTree(setup);
    initializeFileMenu(setup);
    initializeActionsAndView(setup);
    initializeFeatureCommands(setup);
    initializeOptions(setup);
    initializeSketchCommands(setup);
    initializeToolbarsAndStatus(setup);
}

void PdfWindow::initializeSceneAndTree(WindowSetup &setup) {
    auto &viewport = setup.viewport;
    auto &modelTree = setup.modelTree;
    auto &createSketchOnPlane = setup.createSketchOnPlane;
    setWindowTitle(QGuiApplication::applicationDisplayName());
    qApp->installEventFilter(new DialogMover(this));
    resize(1280, 820);
    setMinimumSize(900, 600);
    viewport_ = new CadViewport(this);
    viewport = viewport_;
    viewport->setOrientation(defaultAxesOrientation());  // assi del documento nuovo (Opzioni, di default Z in alto)
    viewport->setLengthUnit(defaultLengthUnit());
    setCentralWidget(viewport);
    recovery_ = new ForgeCad::DocumentRecovery([this](DocumentState &state,QString &path) {
        if (foregroundProgressDepth_ || loadingDocument_ || !viewport_->recoverySnapshotReady()) return false;
        state=viewport_->recoveryDocument(); path=documentPath_; return true;
    },this);

    auto *modelDock = new QDockWidget(QStringLiteral("Albero modello"), this);
    modelDock->setObjectName(QStringLiteral("modelDock"));  // per saveState/restoreState
    modelTree = new StoryboardTree(modelDock);
    modelTree_ = modelTree;
    modelTree->setItemDelegate(new TreeBadgeDelegate(modelTree));
    modelTree->setMoveFeatureCallback([viewport](int feature, int target) { return viewport->moveFeatureTo(feature, target); });
    modelTree->setHeaderLabel(QStringLiteral("Oggetti scena"));
    modelTree->setMinimumWidth(220);
    modelTree->setContextMenuPolicy(Qt::CustomContextMenu);
    modelTree->setHistoryPositionCallback([viewport](int position) { viewport->setHistoryPosition(position); });
    modelTree->setCursorMode(static_cast<StoryboardTree::CursorMode>(qBound(0,
        QSettings().value(QStringLiteral("view/storyboardCursorMode"), 0).toInt(), 2)));
    modelDock->setWidget(modelTree);
    addDockWidget(Qt::LeftDockWidgetArea, modelDock);
    rebuildModelTree();
    createSketchOnPlane = std::make_shared<std::function<void(int)>>();
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
                // Spigoli scelti di nuovo: sono stati presi sulla base attuale.
                const auto sameReferences = [](const QVector<EdgePoint> &a, const QVector<EdgePoint> &b) {
                    if (a.size() != b.size()) return false;
                    for (int k = 0; k < a.size(); ++k)
                        if (a.at(k).x != b.at(k).x || a.at(k).y != b.at(k).y || a.at(k).z != b.at(k).z || a.at(k).subshape != b.at(k).subshape
                            || a.at(k).role != b.at(k).role)
                            return false;
                    return true;
                };
                if (!sameReferences(selectedEdges, original.blendEdges) && original.firstBody >= 0 && original.firstBody < viewport->extrusions().size())
                    body.blendBaseFeature = viewport->extrusions().at(original.firstBody).featureId;
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
        if (viewport->interactionLocked()||viewport->documentReadOnly()) return;
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
            revolutionDialog(this, viewport, QStringLiteral("Modifica rivoluzione"), index, original, true,
                             [&](const ExtrusionObject &values) { return update(values); });
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
                body.sketchIndex = values.sketchIndex;
                body.trimPlane = values.trimPlane;
                body.trimKeep = values.trimKeep;
                body.trimBoth = values.trimBoth;
                body.trimProject = values.trimProject;
                body.projectionMode = values.projectionMode;
                body.projectionReverse = values.projectionReverse;
                body.trimToolKeep = values.trimToolKeep;
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
        } else if (original.feature == BodyFeature::SurfaceOffset) {
            offsetDialog(this, viewport, original.distance == 0.0 ? QStringLiteral("Modifica copia facce") : QStringLiteral("Modifica offset"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::BoundarySurface) {
            boundaryDialog(this, viewport, QStringLiteral("Modifica superficie tra curve"), index, original,
                           [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::FillSurface) {
            fillDialog(this, viewport, QStringLiteral("Modifica riempimento"), index, original,
                       [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Draft) {
            draftDialog(this, viewport, QStringLiteral("Modifica sformo"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Thicken) {
            thickenDialog(this, viewport, QStringLiteral("Modifica spessore"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::SpatialSketch) {
            spatialSketchDialog(this,viewport,QStringLiteral("Modifica schizzo 3D"),index,original,
                [viewport,index](const ExtrusionObject &d){return viewport->updateBody(index,d);});
        } else if (original.feature == BodyFeature::ExtractedCurve) {
            extractedCurveDialog(this, viewport, QStringLiteral("Modifica curva estratta 3D"), index, original,
                                 [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::ProjectedCurve) {
            projectionDialog(this, viewport, QStringLiteral("Modifica curva proiettata"), index, original,
                             [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Shell) {
            offsetDialog(this, viewport, QStringLiteral("Modifica svuota"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Thread) {
            threadDialog(this, viewport, QStringLiteral("Modifica filettatura"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::DeleteFace) {
            offsetDialog(this, viewport, QStringLiteral("Modifica eliminazione facce"), index, original,
                         [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Ruled) {
            ruledDialog(this, viewport, QStringLiteral("Modifica superficie rigata"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::PlanarSurface) {
            planarDialog(this, viewport, QStringLiteral("Modifica superficie planare"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::Sew) {
            sewDialog(this, viewport, QStringLiteral("Modifica cucitura"), index, original, [&](const ExtrusionObject &values) { return update(values); });
        } else if (original.feature == BodyFeature::SheetExtend) {
            extendDialog(this, viewport, QStringLiteral("Modifica estensione"), original.firstBody, index, original.blendEdges, original.blendSize,
                         original.extendLinear, false, [&](const QVector<EdgePoint> &selected, double distance, bool linear) {
                             ExtrusionObject body = original;
                             body.blendEdges = selected;
                             body.blendSize = distance;
                             body.extendLinear = linear;
                             return update(body);
                         });
        }
    });
    viewport->setEditBodyCallback([editBody](int index) { (*editBody)(index); });
    connect(modelTree, &QTreeWidget::itemDoubleClicked, this, [this, viewport, editBody](QTreeWidgetItem *item, int) {
        if (suppressTreeClick_) return;
        const int type = item->data(0, Qt::UserRole).toInt();
        if (type == kTreeExtrusion) (*editBody)(item->data(0, Qt::UserRole + 1).toInt());
        else if (type == kTreeSketch) viewport->selectSketch(item->data(0, Qt::UserRole + 1).toInt());  // doppio clic: apre lo schizzo
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
    // Selezione multipla nella vista (riquadro, Ctrl/Maiusc+clic): le stesse voci nell'albero.
    viewport->setMultiSelectionCallback([this](const QVector<SceneSelection> &objects) {
        modelTree_->clearSelection();
        for (QTreeWidgetItemIterator it(modelTree_); *it; ++it) {
            QTreeWidgetItem *item = *it;
            const int type = item->data(0, Qt::UserRole).toInt();
            const int itemIndex = item->data(0, Qt::UserRole + 1).toInt();
            const SceneObjectKind kind = type == kTreeSketch ? SceneObjectKind::Sketch
                                       : type == kTreeExtrusion ? SceneObjectKind::Extrusion : SceneObjectKind::None;
            if (kind != SceneObjectKind::None && objects.contains(SceneSelection{kind, itemIndex, -1})) item->setSelected(true);
        }
    });
    viewport->setDocumentChangedCallback([this] {
        if (!loadingDocument_ && !viewport_->documentReadOnly()) recovery_->changed();
        if (!loadingDocument_ && !viewport_->documentReadOnly() && !documentModified_) {
            documentModified_ = true;
            updateWindowTitle();
        }
        scheduleModelTreeRebuild();
        updateUndoActions();
        if (historyGraphDialog_)
            static_cast<ForgeCad::HistoryGraphDialog *>(historyGraphDialog_)->setDocument(viewport_->currentDocument());
    });
    viewport->setPlaneContextCallback([this, viewport, createSketchOnPlane](int plane) {
        CadMenu menu(this);
        QAction *newSketch = menu.addAction(QStringLiteral("Nuovo schizzo su questo piano"));
        QAction *normalView = menu.addAction(QStringLiteral("Vista normale al piano"));
        const QAction *chosen = menu.exec(QCursor::pos());
        if (chosen == newSketch && *createSketchOnPlane) (*createSketchOnPlane)(plane);
        else if (chosen == normalView) viewport->setViewNormal(plane);
    });
    connect(modelTree, &QTreeWidget::itemChanged, this, [this, viewport](QTreeWidgetItem *item, int) {
        if (rebuildingTree_) return;
        if(viewport->documentReadOnly()){scheduleModelTreeRebuild();return;}
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
    connect(modelTree, &QTreeWidget::itemClicked, this, [this, viewport, modelTree](QTreeWidgetItem *item, int) {
        if (suppressTreeClick_) return;
        const int type = item->data(0, Qt::UserRole).toInt();
        const int index = item->data(0, Qt::UserRole + 1).toInt();
        // Piu' voci scelte (Ctrl/Maiusc+clic): schizzi e feature insieme nella vista.
        const QList<QTreeWidgetItem *> chosen = modelTree->selectedItems();
        if (chosen.size() > 1 || (QGuiApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            QVector<SceneSelection> objects;
            for (QTreeWidgetItem *selected : chosen) {
                const int selectedType = selected->data(0, Qt::UserRole).toInt();
                const int selectedIndex = selected->data(0, Qt::UserRole + 1).toInt();
                if (selectedType == kTreeSketch) objects.append({SceneObjectKind::Sketch, selectedIndex, -1});
                else if (selectedType == kTreeExtrusion) objects.append({SceneObjectKind::Extrusion, selectedIndex, -1});
            }
            viewport->setSelectedObjects(objects);
            return;
        }
        if (type == kTreePlane) viewport->selectPlane(index);
        // Un clic seleziona lo schizzo; si apre con il doppio clic.
        if (type == kTreeSketch) viewport->setSelectedObjects({{SceneObjectKind::Sketch, index, -1}});
        if (type == kTreeExtrusion) viewport->selectObject(SceneObjectKind::Extrusion, index);
        if (type == kTreeBody) {
            const int tip = viewport->modelBodyTip(index);
            if (tip >= 0) viewport->selectObject(SceneObjectKind::Extrusion, tip, true);  // tutto il corpo
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
        if(viewport->documentReadOnly())return;
        auto *item = modelTree->itemAt(position);
        if (!item) return;
        const int type = item->data(0, Qt::UserRole).toInt();
        const int index = item->data(0, Qt::UserRole + 1).toInt();
        CadMenu menu(this);
        const QPoint globalPosition = modelTree->viewport()->mapToGlobal(position);
        // Piu' voci scelte: le operazioni comuni a tutte.
        const QList<QTreeWidgetItem *> chosenItems = modelTree->selectedItems();
        if (chosenItems.size() > 1 && item->isSelected()) {
            QVector<SceneSelection> objects;
            QVector<int> features;
            bool anySuppressed = false, anyActive = false, anyVisible = false;
            for (QTreeWidgetItem *selected : chosenItems) {
                const int selectedType = selected->data(0, Qt::UserRole).toInt();
                const int selectedIndex = selected->data(0, Qt::UserRole + 1).toInt();
                if (selectedType == kTreeSketch && selectedIndex >= 0 && selectedIndex < viewport->sketches().size()) {
                    objects.append({SceneObjectKind::Sketch, selectedIndex, -1});
                    anyVisible = anyVisible || viewport->isObjectVisible(SceneObjectKind::Sketch, selectedIndex);
                } else if (selectedType == kTreeExtrusion && selectedIndex >= 0 && selectedIndex < viewport->extrusions().size()) {
                    objects.append({SceneObjectKind::Extrusion, selectedIndex, -1});
                    const ExtrusionObject &feature = viewport->extrusions().at(selectedIndex);
                    if (feature.modelBodyId != 0) {
                        features.append(selectedIndex);
                        anySuppressed = anySuppressed || feature.suppressed;
                        anyActive = anyActive || !feature.suppressed;
                    } else {
                        anyVisible = anyVisible || feature.visible;
                    }
                }
            }
            if (objects.isEmpty()) return;
            viewport->setSelectedObjects(objects);
            QAction *suppress = anyActive ? menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")),
                                                           QStringLiteral("Sopprimi %1 feature").arg(features.size())) : nullptr;
            QAction *activate = anySuppressed ? menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")),
                                                               QStringLiteral("Riattiva %1 feature").arg(features.size())) : nullptr;
            // La visibilita' vale per schizzi e corpi fuori dalla storia (le feature seguono il loro corpo).
            QVector<SceneSelection> visibilityTargets;
            for (const SceneSelection &object : objects)
                if (object.kind == SceneObjectKind::Sketch || !features.contains(object.index)) visibilityTargets.append(object);
            QAction *hide = !visibilityTargets.isEmpty() && anyVisible
                ? menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")), QStringLiteral("Nascondi")) : nullptr;
            QAction *show = !visibilityTargets.isEmpty() && !anyVisible
                ? menu.addAction(ForgeCad::commandIcon(QStringLiteral("visibility")), QStringLiteral("Mostra")) : nullptr;
            menu.addSeparator();
            QAction *remove = menu.addAction(ForgeCad::commandIcon(QStringLiteral("delete")),
                                             QStringLiteral("Elimina %1 elementi").arg(objects.size()));
            const QAction *chosen = menu.exec(globalPosition);
            if (!chosen) return;
            if (chosen == suppress || chosen == activate) {
                const QString error = viewport->setFeaturesSuppressed(features, chosen == suppress);
                if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Storyboard"), error);
            } else if (chosen == hide || chosen == show) {
                viewport->setObjectsVisible(visibilityTargets, chosen == show);
            } else if (chosen == remove) {
                viewport->deleteObjectsOrFeatures(objects);
            }
            return;
        }
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
            QAction *meshColor = menu.addAction(QStringLiteral("Colore mesh..."));
            toggle->setIcon(ForgeCad::commandIcon(QStringLiteral("visibility")));
            rename->setIcon(ForgeCad::commandIcon(QStringLiteral("rename")));
            meshColor->setIcon(ForgeCad::commandIcon(QStringLiteral("meshColor")));
            rename->setShortcut(QKeySequence(Qt::Key_F2));
            menu.addSeparator();
            QAction *remove = menu.addAction(QStringLiteral("Elimina corpo e storyboard"));
            remove->setIcon(ForgeCad::commandIcon(QStringLiteral("delete")));
            const QAction *chosen = menu.exec(globalPosition);
            if (chosen == rename) renameTreeItem(item);
            else if (chosen == toggle) viewport->setModelBodyVisible(index, !visible);
            else if (chosen == meshColor) {
                const QColor color = QColorDialog::getColor(viewport->modelBodyMeshColor(index), this,
                                                            QStringLiteral("Colore mesh del corpo"));
                if (color.isValid()) viewport->setModelBodyMeshColor(index, color);
            }
            else if (chosen == remove) viewport->deleteModelBody(index);
        } else if (type == kTreeSketch || type == kTreeExtrusion) {
            const SceneObjectKind kind = type == kTreeSketch ? SceneObjectKind::Sketch : SceneObjectKind::Extrusion;
            const bool visible = viewport->isObjectVisible(kind, index);
            QAction *editSketch = type == kTreeSketch ? menu.addAction(QStringLiteral("Modifica schizzo")) : nullptr;
            QAction *editParameters = type == kTreeExtrusion ? menu.addAction(QStringLiteral("Modifica parametri...")) : nullptr;
            if (editSketch) editSketch->setIcon(ForgeCad::commandIcon(QStringLiteral("edit")));
            if (editParameters && index >= 0 && index < viewport->extrusions().size())
                editParameters->setIcon(ForgeCad::commandIcon(featureIconName(viewport->extrusions().at(index))));
            const bool storyboardFeature = type == kTreeExtrusion && index >= 0 && index < viewport->extrusions().size()
                                        && viewport->extrusions().at(index).modelBodyId != 0;
            QAction *suppress = storyboardFeature
                ? menu.addAction(viewport->extrusions().at(index).suppressed ? QStringLiteral("Riattiva feature")
                                                                            : QStringLiteral("Sopprimi feature"))
                : nullptr;
            if (suppress) suppress->setIcon(ForgeCad::commandIcon(QStringLiteral("visibility")));
            QAction *moveEarlier = storyboardFeature ? menu.addAction(QStringLiteral("Sposta prima nella storia")) : nullptr;
            QAction *moveLater = storyboardFeature ? menu.addAction(QStringLiteral("Sposta dopo nella storia")) : nullptr;
            if (moveEarlier) moveEarlier->setIcon(ForgeCad::commandIcon(QStringLiteral("move")));
            if (moveLater) moveLater->setIcon(ForgeCad::commandIcon(QStringLiteral("move")));
            const bool datum = type == kTreeExtrusion && index >= 0 && index < viewport->extrusions().size()
                            && viewport->extrusions().at(index).feature == BodyFeature::DatumPlane;
            QAction *datumSketch = datum ? menu.addAction(QStringLiteral("Nuovo schizzo sul piano")) : nullptr;
            if (datumSketch) datumSketch->setIcon(ForgeCad::commandIcon(QStringLiteral("newSketch")));
            if (datumSketch) datumSketch->setEnabled(viewport->extrusions().at(index).datumValid);
            QAction *toggle = storyboardFeature ? nullptr : menu.addAction(visible ? QStringLiteral("Nascondi") : QStringLiteral("Mostra"));
            QAction *rename = menu.addAction(QStringLiteral("Rinomina..."));
            if (toggle) toggle->setIcon(ForgeCad::commandIcon(QStringLiteral("visibility")));
            rename->setIcon(ForgeCad::commandIcon(QStringLiteral("rename")));
            rename->setShortcut(QKeySequence(Qt::Key_F2));
            menu.addSeparator();
            QAction *remove = menu.addAction(QStringLiteral("Elimina"));
            remove->setIcon(ForgeCad::commandIcon(QStringLiteral("delete")));
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

}

void PdfWindow::initializeFileMenu(WindowSetup &setup) {
    auto &viewport = setup.viewport;
    auto &fileMenu = setup.fileMenu;
    auto &newAction = setup.newAction;
    auto &openAction = setup.openAction;
    auto &saveAction = setup.saveAction;
    auto &newDrawingAction = setup.newDrawingAction;
    auto &openDrawingAction = setup.openDrawingAction;
    auto &importAction = setup.importAction;
    fileMenu = addCadMenu(menuBar(), QStringLiteral("File"));
    newAction = fileMenu->addAction(QStringLiteral("Nuovo"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &PdfWindow::newDocument);
    openAction = fileMenu->addAction(QStringLiteral("Apri..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &PdfWindow::openDocument);
    saveAction = fileMenu->addAction(QStringLiteral("Salva"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this] { saveDocument(false); });
    QAction *saveAsAction = fileMenu->addAction(QStringLiteral("Salva con nome..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    saveAsAction->setToolTip(QStringLiteral("Salva una copia indipendente del documento."));
    connect(saveAsAction, &QAction::triggered, this, [this] { saveDocument(true); });
    auto *recoverAction=fileMenu->addAction(QStringLiteral("Recupera documenti..."));
    recoverAction->setObjectName(QStringLiteral("documentRecoveryAction"));
    recoverAction->setIcon(ForgeCad::commandIcon(QStringLiteral("open")));
    recoverAction->setToolTip(QStringLiteral("Apri come copia un backup automatico rimasto dopo una chiusura imprevista"));
    connect(recoverAction,&QAction::triggered,this,[this]{showRecoveryDocuments();});
    QMenu *drawingMenu = addCadMenu(fileMenu, QStringLiteral("Tavole 2D"));
    newDrawingAction = drawingMenu->addAction(QStringLiteral("Nuova tavola dal modello..."));
    newDrawingAction->setObjectName(QStringLiteral("newDrawingAction"));
    newDrawingAction->setIconVisibleInMenu(true);newDrawingAction->setToolTip(QStringLiteral("Crea una tavola 2D associata al modello salvato."));
    connect(newDrawingAction,&QAction::triggered,this,&PdfWindow::newDrawing);
    openDrawingAction = drawingMenu->addAction(QStringLiteral("Apri tavola..."));
    openDrawingAction->setObjectName(QStringLiteral("openDrawingAction"));
    openDrawingAction->setIconVisibleInMenu(true);openDrawingAction->setToolTip(QStringLiteral("Apre le tavole collegate al modello: scelta con anteprima se sono più di una; altrimenti consente di sfogliare i file .drw."));
    connect(openDrawingAction,&QAction::triggered,this,&PdfWindow::openDrawing);
    QMenu *revisionMenu = addCadMenu(fileMenu, QStringLiteral("Revisioni"));
    revisionMenu->setObjectName(QStringLiteral("documentRevisionMenu"));
    const auto reportRevisionError = [this](const QString &error) {
        if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Revisioni"), error);
    };
    QAction *checkoutAction = revisionMenu->addAction(QStringLiteral("Esegui check-out..."));
    checkoutAction->setObjectName(QStringLiteral("revisionCheckoutAction"));
    connect(checkoutAction, &QAction::triggered, this, [this, reportRevisionError] {
        bool accepted = false;
        const auto fallback = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
        const QString owner = QInputDialog::getText(this, QStringLiteral("Check-out"), QStringLiteral("Proprietario della copia di lavoro:"),
            QLineEdit::Normal, QSettings().value(QStringLiteral("revisions/owner"),fallback).toString(), &accepted).trimmed();
        if (!accepted || owner.isEmpty()) return;
        if ((documentPath_.isEmpty()||(documentModified_&&!documentEditAccess_.readOnly))&&!saveDocument(false)) return;
        const auto error = runDocumentRevisionAction(DocumentRevisionAction::Checkout,owner);
        if (error.isEmpty()) QSettings().setValue(QStringLiteral("revisions/owner"),owner);
        reportRevisionError(error);
    });
    QAction *checkinAction = revisionMenu->addAction(QStringLiteral("Esegui check-in..."));
    checkinAction->setObjectName(QStringLiteral("revisionCheckinAction"));
    connect(checkinAction, &QAction::triggered, this, [this, reportRevisionError] {
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Check-in"));
        dialog.setObjectName(QStringLiteral("revisionCheckinDialog"));
        auto *layout = new QFormLayout(&dialog);
        QString proposed;
        const auto proposalError=ForgeCad::nextWorkspaceRevisionLabel(documentPath_,viewport_->currentDocument().version.documentId,proposed);
        if(!proposalError.isEmpty()){reportRevisionError(proposalError);return;}
        auto *revision = new QLineEdit(proposed,&dialog);revision->setReadOnly(true);
        revision->setObjectName(QStringLiteral("revisionLabel")); revision->setMaxLength(128);
        auto *comment = new QTextEdit(&dialog); comment->setAcceptRichText(false); comment->setObjectName(QStringLiteral("revisionComment"));
        layout->addRow(QStringLiteral("Revisione tecnica:"),revision); layout->addRow(QStringLiteral("Commento:"),comment);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Esegui check-in"));
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(revision,&QLineEdit::textChanged,&dialog,[buttons](const QString &value) {
            buttons->button(QDialogButtonBox::Ok)->setEnabled(!value.trimmed().isEmpty());
        });
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!revision->text().trimmed().isEmpty());
        layout->addRow(buttons); dialog.resize(440,250);
        if (dialog.exec()!=QDialog::Accepted || !saveDocument(false)) return;
        const auto error = runDocumentRevisionAction(DocumentRevisionAction::Checkin,{},comment->toPlainText());
        reportRevisionError(error);
    });
    QAction *completeAction = revisionMenu->addAction(QStringLiteral("Completa check-in pendente"));
    completeAction->setObjectName(QStringLiteral("revisionCompleteAction"));
    connect(completeAction,&QAction::triggered,this,[this,reportRevisionError] {
        reportRevisionError(runDocumentRevisionAction(DocumentRevisionAction::CompletePending));
    });
    QAction *cancelCheckoutAction = revisionMenu->addAction(QStringLiteral("Annulla check-out..."));
    cancelCheckoutAction->setObjectName(QStringLiteral("revisionCancelAction"));
    connect(cancelCheckoutAction,&QAction::triggered,this,[this,reportRevisionError] {
        if (QMessageBox::question(this,QStringLiteral("Annulla check-out"),
            QStringLiteral("Liberare la prenotazione del documento? Le modifiche locali vengono mantenute."),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        reportRevisionError(runDocumentRevisionAction(DocumentRevisionAction::CancelCheckout));
    });
    revisionMenu->addSeparator();
    QAction *historyAction = revisionMenu->addAction(QStringLiteral("Storico dei check-in..."));
    historyAction->setObjectName(QStringLiteral("revisionHistoryAction"));
    connect(historyAction,&QAction::triggered,this,[this] { showDocumentRevisions(); });
    recentDocumentsMenu_ = addCadMenu(fileMenu, QStringLiteral("File recenti"));
    recentDocumentsMenu_->setObjectName(QStringLiteral("recentDocumentsMenu"));
    connect(recentDocumentsMenu_, &QMenu::aboutToShow, this, &PdfWindow::refreshRecentDocuments);
    refreshRecentDocuments();
    fileMenu->addSeparator();
    // Importazione da altri CAD (cad_import): STEP e IGES, B-rep esatti in mm, un corpo per solido.
    importAction = fileMenu->addAction(QStringLiteral("Importa STEP/IGES..."));
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
        QString error;
        QStringList failures;
        try {
            error = viewport->readImportFile(path, parts, notes);
            if (error.isEmpty()) failures = viewport->importParts(parts, QFileInfo(path).fileName(), [this](int done, int total) {
                updateForegroundProgress(QStringLiteral("Preparazione della vista: %1 / %2 corpi").arg(done).arg(total), done, total);
            });
        } catch (const std::exception &failure) {
            error = QStringLiteral("Importazione non riuscita: %1").arg(QString::fromUtf8(failure.what()));
        }
        endForegroundProgress();
        if (!error.isEmpty()) {
            showImportReport(this, error);
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
            showImportReport(this, text);
        }
    });
    // Esportazione per altri CAD (cad_export): corpi visibili, B-rep esatti in mm.
    auto *exportMenu = addCadMenu(fileMenu, QStringLiteral("Esporta"));
    const auto exportMesh = [this, viewport](bool quadrangular) {
        const QVector<ForgeCad::ExportBody> bodies = viewport->exportableBodies();
        bool hasBody = false;
        for (const ForgeCad::ExportBody &body : bodies) hasBody = hasBody || bool(body.body);
        if (!hasBody) {
            QMessageBox::information(this, quadrangular ? QStringLiteral("Esporta OBJ") : QStringLiteral("Esporta STL"),
                                     QStringLiteral("Non ci sono solidi o superfici visibili da esportare."));
            return;
        }

        const ForgeCad::StlExportOptions suggested = viewport->suggestedStlOptions();
        QSettings settings;
        QDialog dialog(this);
        dialog.setWindowTitle(quadrangular ? QStringLiteral("Qualita' mesh OBJ quadrangolare")
                                           : QStringLiteral("Qualita' mesh STL"));
        dialog.setMinimumWidth(470);
        auto *layout = new QVBoxLayout(&dialog);
        auto *description = new QLabel(quadrangular
            ? QStringLiteral("OBJ conserva i quadrilateri validi per Blender; vicino a poli, fori e transizioni restano alcuni triangoli.\n"
                             "Dimensione, scarto cordale e angolo controllano la densita' e l'approssimazione delle curve.")
            : QStringLiteral("La dimensione massima controlla la densita' anche sulle facce piane.\n"
                             "Lo scarto cordale e l'angolo controllano l'approssimazione di raggi e superfici curve."), &dialog);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *automaticMesh = new QCheckBox(QStringLiteral("Calcola automaticamente il raffinamento migliore"), &dialog);
        automaticMesh->setChecked(settings.value(QStringLiteral("export/automaticMeshRefinement"), true).toBool());
        automaticMesh->setToolTip(QStringLiteral("Parte dalle dimensioni del modello e cerca parametri che triangolino tutte le facce. I valori riusciti sono mostrati nel risultato."));
        layout->addWidget(automaticMesh);
        auto *form = new QFormLayout;
        const auto distanceBox = [&](double value, bool allowZero) {
            auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
            box->setDecimals(6);
            box->setRange(allowZero ? 0.0 : 0.000001, 1.0e9);
            box->setSingleStep(qMax(0.000001, value / 10.0));
            box->setValue(value);
            if (allowZero) box->setSpecialValueText(QStringLiteral("Nessun limite"));
            return box;
        };
        auto *edgeSize = distanceBox(automaticMesh->isChecked() ? suggested.maxEdgeLength : settings.value(QStringLiteral("export/stlMaxEdge"), suggested.maxEdgeLength).toDouble(), true);
        auto *deflection = distanceBox(automaticMesh->isChecked() ? suggested.deflection : settings.value(QStringLiteral("export/stlDeflection"), suggested.deflection).toDouble(), false);
        auto *angle = new QDoubleSpinBox(&dialog);
        angle->setDecimals(2); angle->setRange(0.1, 90.0); angle->setSingleStep(1.0); angle->setSuffix(QStringLiteral(" °"));
        angle->setValue(automaticMesh->isChecked() ? suggested.angle : settings.value(QStringLiteral("export/stlAngle"), suggested.angle).toDouble());
        edgeSize->setToolTip(QStringLiteral("Lunghezza massima di ogni lato dei triangoli; zero lascia decidere allo scarto e all'angolo"));
        deflection->setToolTip(QStringLiteral("Distanza massima ammessa tra la mesh e la geometria esatta"));
        angle->setToolTip(QStringLiteral("Variazione massima della normale tra punti vicini sulle superfici curve"));
        form->addRow(QStringLiteral("Lato massimo triangolo:"), edgeSize);
        form->addRow(QStringLiteral("Scarto cordale massimo:"), deflection);
        form->addRow(QStringLiteral("Angolo massimo superfici:"), angle);
        edgeSize->setEnabled(!automaticMesh->isChecked());
        deflection->setEnabled(!automaticMesh->isChecked());
        angle->setEnabled(!automaticMesh->isChecked());
        auto *count = new QLabel(QStringLiteral("Non ancora calcolato"), &dialog);
        count->setWordWrap(true);
        form->addRow(QStringLiteral("Risultato:"), count);
        layout->addLayout(form);
        auto *showPreview = new QCheckBox(QStringLiteral("Mostra anteprima della mesh nella vista"), &dialog);
        showPreview->setChecked(settings.value(QStringLiteral("export/showMeshPreview"), true).toBool());
        showPreview->setToolTip(QStringLiteral("Riutilizza la mesh calcolata: non esegue una seconda tassellazione"));
        layout->addWidget(showPreview);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Esporta..."));
        auto *calculate = buttons->addButton(QStringLiteral("Calcola mesh e anteprima"), QDialogButtonBox::ActionRole);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        ForgeCad::StlBuildResult built;
        ForgeCad::ObjBuildResult builtObj;
        ForgeCad::StlExportOptions builtOptions;
        bool cacheValid = false;
        const auto currentOptions = [&] {
            ForgeCad::StlExportOptions options;
            options.maxEdgeLength = edgeSize->value();
            options.deflection = deflection->value();
            options.angle = angle->value();
            options.automaticRefinement = automaticMesh->isChecked();
            return options;
        };
        const auto sameOptions = [](const ForgeCad::StlExportOptions &a, const ForgeCad::StlExportOptions &b) {
            return a.maxEdgeLength == b.maxEdgeLength && a.deflection == b.deflection && a.angle == b.angle
                && a.automaticRefinement == b.automaticRefinement;
        };
        const auto build = [&] {
            builtOptions = currentOptions();
            count->setStyleSheet(QString());
            count->setText(QStringLiteral("Calcolo in corso..."));
            calculate->setEnabled(false);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (quadrangular) builtObj = ForgeCad::buildQuadObj(bodies, builtOptions);
            else built = ForgeCad::buildBinaryStl(bodies, builtOptions);
            QApplication::restoreOverrideCursor();
            calculate->setEnabled(true);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
            const QString buildError = quadrangular ? builtObj.error : built.error;
            cacheValid = buildError.isEmpty();
            if (!cacheValid) {
                viewport->clearExportMeshPreview();
                count->setStyleSheet(QStringLiteral("color:#ff806e"));
                count->setText(buildError);
            } else {
                const bool limited = quadrangular ? builtObj.previewLimited : built.previewLimited;
                if (quadrangular) {
                    count->setText(QStringLiteral("%1 quadrilateri + %2 triangoli — %3%4")
                        .arg(QLocale().toString(qlonglong(builtObj.quadCount)),
                             QLocale().toString(qlonglong(builtObj.triangleCount)),
                             QLocale().formattedDataSize(builtObj.data.size()),
                             limited ? QStringLiteral(" — anteprima alleggerita") : QString()));
                } else {
                    count->setText(QStringLiteral("%1 triangoli — %2%3")
                        .arg(QLocale().toString(qlonglong(built.triangleCount)),
                             QLocale().formattedDataSize(built.data.size()),
                             limited ? QStringLiteral(" — anteprima alleggerita") : QString()));
                }
                const auto &used = quadrangular ? builtObj.usedOptions : built.usedOptions;
                if (automaticMesh->isChecked()) {
                    const QSignalBlocker edgeBlock(edgeSize), deflectionBlock(deflection), angleBlock(angle);
                    edgeSize->setValue(used.maxEdgeLength);
                    deflection->setValue(used.deflection);
                    angle->setValue(used.angle);
                    builtOptions = currentOptions();
                }
                count->setText(count->text() + QStringLiteral("\nLato %1 mm · scarto %2 mm · angolo %3° · %4 tentativi")
                    .arg(QLocale().toString(used.maxEdgeLength, 'g', 6), QLocale().toString(used.deflection, 'g', 6),
                         QLocale().toString(used.angle, 'g', 4))
                    .arg(quadrangular ? builtObj.tessellationAttempts : built.tessellationAttempts));
                if (showPreview->isChecked())
                    viewport->setExportMeshPreview(quadrangular ? builtObj.preview : built.preview, quadrangular, limited);
                else viewport->clearExportMeshPreview();
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return cacheValid;
        };
        connect(calculate, &QPushButton::clicked, &dialog, [&, build] { build(); });
        const auto invalidate = [&] {
            cacheValid = false;
            viewport->clearExportMeshPreview();
            count->setStyleSheet(QString());
            count->setText(QStringLiteral("Parametri modificati — ricalcolare"));
        };
        connect(automaticMesh, &QCheckBox::toggled, &dialog, [&](bool automatic) {
            settings.setValue(QStringLiteral("export/automaticMeshRefinement"), automatic);
            edgeSize->setEnabled(!automatic);
            deflection->setEnabled(!automatic);
            angle->setEnabled(!automatic);
            if (automatic) {
                edgeSize->setValue(suggested.maxEdgeLength);
                deflection->setValue(suggested.deflection);
                angle->setValue(suggested.angle);
            }
            invalidate();
        });
        connect(edgeSize, &QDoubleSpinBox::valueChanged, &dialog, [invalidate](double) { invalidate(); });
        connect(deflection, &QDoubleSpinBox::valueChanged, &dialog, [invalidate](double) { invalidate(); });
        connect(angle, &QDoubleSpinBox::valueChanged, &dialog, [invalidate](double) { invalidate(); });
        connect(showPreview, &QCheckBox::toggled, &dialog, [&](bool visible) {
            settings.setValue(QStringLiteral("export/showMeshPreview"), visible);
            if (!visible || !cacheValid) viewport->clearExportMeshPreview();
            else viewport->setExportMeshPreview(quadrangular ? builtObj.preview : built.preview, quadrangular,
                                                quadrangular ? builtObj.previewLimited : built.previewLimited);
        });
        const bool accepted = runModeless(this, viewport, dialog);
        viewport->clearExportMeshPreview();
        if (!accepted) return;

        const ForgeCad::StlExportOptions requested = currentOptions();
        settings.setValue(QStringLiteral("export/stlMaxEdge"), requested.maxEdgeLength);
        settings.setValue(QStringLiteral("export/stlDeflection"), requested.deflection);
        settings.setValue(QStringLiteral("export/stlAngle"), requested.angle);
        const QString base = documentPath_.isEmpty() ? QStringLiteral("Senza nome") : QFileInfo(documentPath_).completeBaseName();
        const QString directory = documentPath_.isEmpty() ? QDir::homePath() : QFileInfo(documentPath_).absolutePath();
        const QString suffix = quadrangular ? QStringLiteral("obj") : QStringLiteral("stl");
        QString path = QFileDialog::getSaveFileName(this,
                                                    quadrangular ? QStringLiteral("Esporta OBJ quadrangolare") : QStringLiteral("Esporta STL"),
                                                    directory + QStringLiteral("/") + base + QStringLiteral(".") + suffix,
                                                    quadrangular ? QStringLiteral("Wavefront OBJ (*.obj)") : QStringLiteral("STL binario (*.stl)"));
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) != 0) path += QStringLiteral(".") + suffix;
        if (!cacheValid || !sameOptions(requested, builtOptions)) {
            beginForegroundProgress(quadrangular ? QStringLiteral("Tassellazione OBJ quadrangolare...")
                                                 : QStringLiteral("Tassellazione STL..."));
            builtOptions = requested;
            if (quadrangular) builtObj = ForgeCad::buildQuadObj(bodies, requested);
            else built = ForgeCad::buildBinaryStl(bodies, requested);
            cacheValid = quadrangular ? builtObj.error.isEmpty() : built.error.isEmpty();
            endForegroundProgress();
        }
        if (!cacheValid) {
            QMessageBox::warning(this, quadrangular ? QStringLiteral("Esporta OBJ") : QStringLiteral("Esporta STL"),
                                 quadrangular ? builtObj.error : built.error);
            return;
        }
        beginForegroundProgress(quadrangular ? QStringLiteral("Scrittura OBJ...") : QStringLiteral("Scrittura STL..."));
        const QString error = quadrangular ? ForgeCad::saveQuadObj(path, builtObj.data)
                                           : ForgeCad::saveBinaryStl(path, built.data);
        endForegroundProgress();
        if (!error.isEmpty()) QMessageBox::warning(this, quadrangular ? QStringLiteral("Esporta OBJ") : QStringLiteral("Esporta STL"), error);
        else if (quadrangular)
            statusBar()->showMessage(QStringLiteral("Esportati %1 quadrilateri e %2 triangoli in %3")
                .arg(QLocale().toString(qlonglong(builtObj.quadCount)),
                     QLocale().toString(qlonglong(builtObj.triangleCount)), path), 10000);
        else
            statusBar()->showMessage(QStringLiteral("Esportati %1 triangoli in %2")
                                     .arg(QLocale().toString(qlonglong(built.triangleCount)), path), 10000);
    };
    connect(exportMenu->addAction(QStringLiteral("STL mesh triangolare...")), &QAction::triggered, this,
            [exportMesh] { exportMesh(false); });
    connect(exportMenu->addAction(QStringLiteral("OBJ mesh quadrangolare (Blender)...")), &QAction::triggered, this,
            [exportMesh] { exportMesh(true); });
    exportMenu->addSeparator();
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
}

void PdfWindow::initializeActionsAndView(WindowSetup &setup) {
    auto &viewport = setup.viewport;
    auto &modelTree = setup.modelTree;
    auto &editMenu = setup.editMenu;
    auto &deleteAction = setup.deleteAction;
    auto &rebuildAction = setup.rebuildAction;
    auto &viewMenu = setup.viewMenu;
    auto &storyboardCursorMenu = setup.storyboardCursorMenu;
    auto &functionPanelMenu = setup.functionPanelMenu;
    auto &functionsMenu = setup.functionsMenu;
    auto &extrudeAction = setup.extrudeAction;
    auto &unionAction = setup.unionAction;
    auto &intersectionAction = setup.intersectionAction;
    auto &differenceAction = setup.differenceAction;
    auto &revolveAction = setup.revolveAction;
    auto &primitiveMenu = setup.primitiveMenu;
    auto &filletAction = setup.filletAction;
    auto &chamferAction = setup.chamferAction;
    auto &draftAction = setup.draftAction;
    auto &shellAction = setup.shellAction;
    auto &threadAction = setup.threadAction;
    auto &datumAction = setup.datumAction;
    auto &linearPatternAction = setup.linearPatternAction;
    auto &circularPatternAction = setup.circularPatternAction;
    auto &mirrorAction = setup.mirrorAction;
    auto &moveAction = setup.moveAction;
    auto &scaleAction = setup.scaleAction;
    auto &helixAction = setup.helixAction;
    auto &sweepAction = setup.sweepAction;
    auto &loftAction = setup.loftAction;
    auto &trimSurfaceAction = setup.trimSurfaceAction;
    auto &extendSurfaceAction = setup.extendSurfaceAction;
    auto &copyFacesAction = setup.copyFacesAction;
    auto &offsetSurfaceAction = setup.offsetSurfaceAction;
    auto &deleteFaceAction = setup.deleteFaceAction;
    auto &ruledSurfaceAction = setup.ruledSurfaceAction;
    auto &boundarySurfaceAction = setup.boundarySurfaceAction;
    auto &fillSurfaceAction = setup.fillSurfaceAction;
    auto &thickenAction = setup.thickenAction;
    auto &extractedCurveAction = setup.extractedCurveAction;
    auto &trimCurve3DAction = setup.trimCurve3DAction;
    auto &projectedCurveAction = setup.projectedCurveAction;
    auto &planarSurfaceAction = setup.planarSurfaceAction;
    auto &loftSurfaceAction = setup.loftSurfaceAction;
    auto &sweepSurfaceAction = setup.sweepSurfaceAction;
    auto &sewSurfacesAction = setup.sewSurfacesAction;
    auto &massAction = setup.massAction;
    auto &measureAction = setup.measureAction;
    auto &modeMenu = setup.modeMenu;
    auto &qualityMenu = setup.qualityMenu;
    auto &qualityGroup = setup.qualityGroup;
    auto &lightingMenu = setup.lightingMenu;
    auto &backgroundAction = setup.backgroundAction;
    auto &historyGraphAction = setup.historyGraphAction;
    auto &treeColorMenu = setup.treeColorMenu;
    auto &treeColorAction = setup.treeColorAction;
    auto &treeColorReset = setup.treeColorReset;
    auto &highlightColorMenu = setup.highlightColorMenu;
    auto &highlightColorAction = setup.highlightColorAction;
    auto &highlightColorReset = setup.highlightColorReset;
    auto &showAllAction = setup.showAllAction;
    auto &planesAction = setup.planesAction;
    auto &gridAction = setup.gridAction;
    auto &axesMenu = setup.axesMenu;
    auto &axesVisibleAction = setup.axesVisibleAction;
    auto &axesOnTopAction = setup.axesOnTopAction;
    auto &axisLengthAction = setup.axisLengthAction;
    auto &sketchOpacityAction = setup.sketchOpacityAction;
    auto &antialiasingMenu = setup.antialiasingMenu;
    auto &wheelZoomAction = setup.wheelZoomAction;
    auto &zoomInAction = setup.zoomInAction;
    auto &zoomOutAction = setup.zoomOutAction;
    auto &resetZoomAction = setup.resetZoomAction;
    auto &modeGroup = setup.modeGroup;
    auto &hiddenEdgesAction = setup.hiddenEdgesAction;
    auto &topologyIdsAction = setup.topologyIdsAction;
    auto &studio = setup.studio;
    auto &soft = setup.soft;
    auto &inspection = setup.inspection;
    auto &sectionAction = setup.sectionAction;
    auto &sectionParameters = setup.sectionParameters;
    editMenu = addCadMenu(menuBar(), QStringLiteral("Modifica"));
    undoAction_ = editMenu->addAction(QStringLiteral("Annulla"));
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = editMenu->addAction(QStringLiteral("Ripeti"));
    redoAction_->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    connect(undoAction_, &QAction::triggered, this, [viewport] { viewport->undo(); });
    connect(redoAction_, &QAction::triggered, this, [viewport] { viewport->redo(); });
    editMenu->addSeparator();
    // Elimina: il corpo o lo schizzo selezionato (con le funzioni che ne
    // dipendono, dopo conferma); in modalita' schizzo le entita' selezionate.
    deleteAction = editMenu->addAction(QStringLiteral("Elimina"));
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, [viewport] { viewport->deleteSelection(); });
    // Rigenera tutto: l'intera storia ricalcolata con il kernel attuale, senza
    // usare i corpi salvati nel documento.
    rebuildAction = editMenu->addAction(QStringLiteral("Rigenera tutto"));
    rebuildAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    rebuildAction->setToolTip(QStringLiteral("Ricalcola tutte le feature della storia con il kernel attuale (anche quelle lette dal documento)"));
    connect(rebuildAction, &QAction::triggered, this, [this, viewport] {
        const int failed = viewport->rebuildAllFeatures();
        statusBar()->showMessage(failed == 0 ? QStringLiteral("Storia rigenerata.")
                                             : QStringLiteral("Storia rigenerata: %1 feature con errori (⚠ nell'albero).").arg(failed),
                                 8000);
    });
    updateUndoActions();
    viewMenu = addCadMenu(menuBar(), QStringLiteral("Visualizza"));
    storyboardCursorMenu = addCadMenu(viewMenu, QStringLiteral("Barra di posizione storyboard"));
    auto *storyboardCursorGroup = new QActionGroup(this);
    storyboardCursorGroup->setExclusive(true);
    const QStringList cursorModes = {QStringLiteral("Orizzontale nell'albero"), QStringLiteral("Verticale a sinistra"), QStringLiteral("Verticale a destra")};
    for (int mode = 0; mode < cursorModes.size(); ++mode) {
        auto *action = storyboardCursorMenu->addAction(cursorModes.at(mode));
        action->setObjectName(QStringLiteral("storyboardCursorMode%1").arg(mode));
        action->setCheckable(true);
        action->setChecked(int(modelTree->cursorMode()) == mode);
        storyboardCursorGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, modelTree, mode] {
            QSettings().setValue(QStringLiteral("view/storyboardCursorMode"), mode);
            modelTree->setCursorMode(static_cast<StoryboardTree::CursorMode>(mode));
            rebuildModelTree();
        });
    }
    functionPanelMenu = addCadMenu(viewMenu, QStringLiteral("Pannelli delle funzioni"));
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
    functionsMenu = addCadMenu(menuBar(), QStringLiteral("Funzioni"));
    extrudeAction = functionsMenu->addAction(QStringLiteral("Estrusione..."));
    extrudeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    auto *booleanMenu = addCadMenu(functionsMenu, QStringLiteral("Booleane"));
    unionAction = booleanMenu->addAction(QStringLiteral("Unione..."));
    intersectionAction = booleanMenu->addAction(QStringLiteral("Intersezione..."));
    differenceAction = booleanMenu->addAction(QStringLiteral("Differenza A - B..."));
    revolveAction = functionsMenu->addAction(QStringLiteral("Rivoluzione..."));
     revolveAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    primitiveMenu = addCadMenu(functionsMenu, QStringLiteral("Primitive"));
    filletAction = functionsMenu->addAction(QStringLiteral("Raccordo spigoli..."));
    chamferAction = functionsMenu->addAction(QStringLiteral("Smusso spigoli..."));
    draftAction = functionsMenu->addAction(QStringLiteral("Sformo facce..."));
    draftAction->setToolTip(QStringLiteral("Inclina le facce piane di solidi convessi o superfici rispetto a un piano neutro"));
    shellAction = functionsMenu->addAction(QStringLiteral("Svuota..."));
    shellAction->setToolTip(QStringLiteral("Svuota un solido lasciando pareti di spessore costante; le facce scelte si tolgono e fanno l'apertura"));
    threadAction = functionsMenu->addAction(QStringLiteral("Filettatura automatica..."));
    threadAction->setToolTip(QStringLiteral("Crea una filettatura reale scegliendo la faccia cilindrica o conica di un foro o di un albero"));
    datumAction = functionsMenu->addAction(QStringLiteral("Piano di costruzione..."));
    datumAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    datumAction->setToolTip(QStringLiteral("Piano parallelo a distanza, per tre punti, normale a una curva, per una retta e un punto, ad angolo, medio"));
    auto *patternMenu = addCadMenu(functionsMenu, QStringLiteral("Ripetizione"));
    linearPatternAction = patternMenu->addAction(QStringLiteral("Ripetizione lineare..."));
    circularPatternAction = patternMenu->addAction(QStringLiteral("Ripetizione circolare..."));
    mirrorAction = patternMenu->addAction(QStringLiteral("Specchio..."));
    linearPatternAction->setToolTip(QStringLiteral("Copie di un corpo solido, di superficie o di una funzione (foro, sporgenza) lungo una o due direzioni"));
    circularPatternAction->setToolTip(QStringLiteral("Copie di un corpo solido, di superficie o di una funzione attorno a un asse"));
    mirrorAction->setToolTip(QStringLiteral("Immagine speculare di un corpo solido, di superficie o di una funzione rispetto a un piano"));
    moveAction = functionsMenu->addAction(QStringLiteral("Sposta / ruota corpo..."));
    moveAction->setToolTip(QStringLiteral("Traslazione e rotazione esatte del corpo selezionato (anche come copia)"));
    scaleAction = functionsMenu->addAction(QStringLiteral("Scala..."));
    scaleAction->setToolTip(QStringLiteral("Scala uniforme del corpo selezionato attorno all'origine, al baricentro o a un punto"));
    functionsMenu->addSeparator();
    helixAction = functionsMenu->addAction(QStringLiteral("Elica e spirale..."));
    helixAction->setToolTip(QStringLiteral("Elica (cilindrica o conica) o spirale piana da un cerchio di uno schizzo, uno spigolo circolare o una faccia cilindrica"));
    sweepAction = functionsMenu->addAction(QStringLiteral("Sweep (estrusione lungo un percorso)..."));
    sweepAction->setToolTip(QStringLiteral("Sezione piana trascinata lungo un percorso: schizzo, bordo di faccia o curva 3D"));
    loftAction = functionsMenu->addAction(QStringLiteral("Loft..."));
    loftAction->setToolTip(QStringLiteral("Solido o superficie che passa per le sezioni di piu' schizzi"));
    functionsMenu->addSeparator();
    auto *surfaceMenu = addCadMenu(functionsMenu, QStringLiteral("Superfici"));
    trimSurfaceAction = surfaceMenu->addAction(QStringLiteral("Taglia superficie..."));
    trimSurfaceAction->setToolTip(QStringLiteral("Taglia una superficie con un corpo (superficie o solido) o un piano e ne tiene una parte"));
    extendSurfaceAction = surfaceMenu->addAction(QStringLiteral("Estendi superficie..."));
    extendSurfaceAction->setToolTip(QStringLiteral("Estende i bordi scelti di una superficie: clic sui bordi, Invio, poi la distanza"));
    copyFacesAction = surfaceMenu->addAction(QStringLiteral("Copia facce..."));
    copyFacesAction->setToolTip(QStringLiteral("Copia una faccia, piu' facce o tutta una superficie cucita, mantenendo il corpo originale"));
    offsetSurfaceAction = surfaceMenu->addAction(QStringLiteral("Offset superficie..."));
    offsetSurfaceAction->setToolTip(QStringLiteral("Superficie a distanza costante dalle facce scelte di un solido o di una superficie "
                                                   "(con estensione e rifilo opzionali dei bordi comuni)"));
    deleteFaceAction = surfaceMenu->addAction(QStringLiteral("Elimina facce..."));
    deleteFaceAction->setToolTip(QStringLiteral("Toglie le facce scelte da un solido o da una superficie: un solido diventa una superficie aperta"));
    ruledSurfaceAction = surfaceMenu->addAction(QStringLiteral("Superficie rigata..."));
    ruledSurfaceAction->setToolTip(QStringLiteral("Superficie di rette tra due curve scelte nella vista (spigoli, entita' degli schizzi, eliche)"));
    boundarySurfaceAction = surfaceMenu->addAction(QStringLiteral("Superficie tra curve..."));
    boundarySurfaceAction->setToolTip(QStringLiteral("Superficie delimitata da un contorno chiuso di curve, anche sghembo (3 o 4 lati: patch di Coons)"));
    fillSurfaceAction = surfaceMenu->addAction(QStringLiteral("Superficie di riempimento..."));
    fillSurfaceAction->setToolTip(QStringLiteral("Superficie in un contorno chiuso che passa per curve guida e continua in tangenza o "
                                                 "curvatura le facce adiacenti"));
    thickenAction = surfaceMenu->addAction(QStringLiteral("Spessore..."));
    thickenAction->setToolTip(QStringLiteral("Da' spessore a una superficie (o a facce scelte) lungo la normale o lungo una direzione scelta"));
    extractedCurveAction = surfaceMenu->addAction(QStringLiteral("Estrai bordo 3D..."));
    extractedCurveAction->setToolTip(QStringLiteral("Curva esatta associativa dal bordo originale, senza proiezione su un piano"));
    trimCurve3DAction = surfaceMenu->addAction(QStringLiteral("Taglia curva 3D..."));
    trimCurve3DAction->setToolTip(QStringLiteral("Taglia una curva con un segmento mantenendo il legame con la sorgente"));
    projectedCurveAction = surfaceMenu->addAction(QStringLiteral("Curva proiettata..."));
    projectedCurveAction->setToolTip(QStringLiteral("Curve 3D: lo schizzo proiettato lungo la normale del suo piano sulla prima faccia del corpo"));
    planarSurfaceAction = surfaceMenu->addAction(QStringLiteral("Superficie planare..."));
    planarSurfaceAction->setToolTip(QStringLiteral("Superficie piana delimitata dai contorni chiusi di uno schizzo o da bordi complanari"));
    loftSurfaceAction = surfaceMenu->addAction(QStringLiteral("Loft di superfici..."));
    loftSurfaceAction->setToolTip(QStringLiteral("Loft che resta una superficie: con sezioni chiuse un tubo senza coperchi"));
    sweepSurfaceAction = surfaceMenu->addAction(QStringLiteral("Sweep di superfici..."));
    sweepSurfaceAction->setToolTip(QStringLiteral("Sweep che resta una superficie: con un profilo chiuso un tubo senza coperchi"));
    sewSurfacesAction = surfaceMenu->addAction(QStringLiteral("Cuci superfici..."));
    sewSurfacesAction->setToolTip(QStringLiteral("Unisce le superfici lungo i bordi comuni; se chiudono un volume crea un solido"));
    auto *analysisMenu = addCadMenu(menuBar(), QStringLiteral("Analisi"));
    auto *shapeAction = analysisMenu->addAction(QStringLiteral("Curvatura, zebra e continuità..."));
    connect(shapeAction, &QAction::triggered, this, [this, viewport] { shapeAnalysisDialog(this, viewport); });
    massAction = analysisMenu->addAction(QStringLiteral("Proprieta' di massa..."));
    massAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    massAction->setToolTip(QStringLiteral("Volume, massa, baricentro e momenti d'inerzia del corpo selezionato"));
    QAction *microAction = analysisMenu->addAction(QStringLiteral("Micro-geometrie..."));
    microAction->setToolTip(QStringLiteral("Facce sottili, spigoli corti e quasi tangenti del corpo selezionato: il rumore che fa fallire raccordi e offset"));
    connect(microAction, &QAction::triggered, this, [this, viewport] { microFeaturesDialog(this, viewport); });
    measureAction = analysisMenu->addAction(QStringLiteral("Misura..."));
    measureAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    measureAction->setToolTip(QStringLiteral("Misura nella vista: distanze, angoli, lunghezze, raggi e aree di punti, spigoli e facce"));
    modeMenu = addCadMenu(viewMenu, QStringLiteral("Stile visualizzazione"));
    qualityMenu = addCadMenu(viewMenu, QStringLiteral("Qualita tessellazione"));
    qualityGroup = new QActionGroup(this); qualityGroup->setExclusive(true);
    QAction *lowQuality = qualityMenu->addAction(QStringLiteral("Bassa"));
    QAction *mediumQuality = qualityMenu->addAction(QStringLiteral("Media"));
    QAction *highQuality = qualityMenu->addAction(QStringLiteral("Alta"));
    lowQuality->setCheckable(true); mediumQuality->setCheckable(true); highQuality->setCheckable(true);
    mediumQuality->setChecked(true);
    qualityGroup->addAction(lowQuality); qualityGroup->addAction(mediumQuality); qualityGroup->addAction(highQuality);
    lightingMenu = addCadMenu(viewMenu, QStringLiteral("Luci scena"));
    backgroundAction = viewMenu->addAction(QStringLiteral("Sfondo e luce ambiente..."));
    connect(backgroundAction, &QAction::triggered, this, &PdfWindow::editBackground);
    historyGraphAction = viewMenu->addAction(QStringLiteral("Debug history / storyboard..."));
    historyGraphAction->setToolTip(QStringLiteral("Grafo di feature, schizzi, operandi, riferimenti topologici ed errori di rigenerazione"));
    connect(historyGraphAction, &QAction::triggered, this, [this, viewport] {
        auto *dialog = static_cast<ForgeCad::HistoryGraphDialog *>(historyGraphDialog_);
        if (!dialog) {
            dialog = new ForgeCad::HistoryGraphDialog(this);
            historyGraphDialog_ = dialog;
            dialog->setSelectionCallback([viewport](int kind, int index) {
                if (kind == 0) viewport->selectSketch(index);
                else if (kind == 1) viewport->selectObject(SceneObjectKind::Extrusion, index);
            });
            connect(dialog, &QObject::destroyed, this, [this] { historyGraphDialog_ = nullptr; });
        }
        dialog->setDocument(viewport->currentDocument());
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    });
    // Sfondo dell'albero modello (QSettings view/treeBackground; vuoto = quello del tema).
    treeColorMenu = addCadMenu(viewMenu, QStringLiteral("Sfondo dell'albero"));
    treeColorAction = treeColorMenu->addAction(QStringLiteral("Scegli il colore..."));
    treeColorReset = treeColorMenu->addAction(QStringLiteral("Colore del tema"));
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
    // Colore delle facce della feature scelta (QSettings view/featureHighlightColor; di
    // default quello della selezione degli spigoli).
    highlightColorMenu = addCadMenu(viewMenu, QStringLiteral("Colore evidenziazione feature"));
    highlightColorAction = highlightColorMenu->addAction(QStringLiteral("Scegli il colore..."));
    highlightColorReset = highlightColorMenu->addAction(QStringLiteral("Colore della selezione (predefinito)"));
    connect(highlightColorAction, &QAction::triggered, this, [this, viewport] {
        const QColor color = QColorDialog::getColor(viewport->featureHighlightColor(), this,
                                                    QStringLiteral("Colore delle facce della feature scelta"));
        if (!color.isValid()) return;
        QSettings().setValue(QStringLiteral("view/featureHighlightColor"), color);
        viewport->setFeatureHighlightColor(color);
    });
    connect(highlightColorReset, &QAction::triggered, this, [viewport] {
        QSettings().remove(QStringLiteral("view/featureHighlightColor"));
        viewport->setFeatureHighlightColor(QColor());
    });
    viewport->setFeatureHighlightColor(QSettings().value(QStringLiteral("view/featureHighlightColor")).value<QColor>());
    showAllAction = viewMenu->addAction(QStringLiteral("Mostra tutti gli oggetti"));
    showAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    connect(showAllAction, &QAction::triggered, this, [viewport] { viewport->showAllObjects(); });
    planesAction = viewMenu->addAction(QStringLiteral("Piani di riferimento"));
    planesAction->setCheckable(true); planesAction->setChecked(true);
    QSettings viewSettings;
    gridAction = viewMenu->addAction(QStringLiteral("Griglia"));
    gridAction->setCheckable(true); gridAction->setChecked(viewSettings.value(QStringLiteral("view/grid"), true).toBool());
    viewport->setGridVisible(gridAction->isChecked());
    connect(gridAction, &QAction::toggled, this, [viewport](bool visible) {
        viewport->setGridVisible(visible);
        QSettings().setValue(QStringLiteral("view/grid"), visible);
    });
    // Assi: visibili o no, sopra gli oggetti o sotto, lunghezza.
    axesMenu = addCadMenu(viewMenu, QStringLiteral("Assi"));
    axesVisibleAction = axesMenu->addAction(QStringLiteral("Mostra gli assi"));
    axesVisibleAction->setCheckable(true); axesVisibleAction->setChecked(viewSettings.value(QStringLiteral("view/axes"), true).toBool());
    viewport->setAxesVisible(axesVisibleAction->isChecked());
    connect(axesVisibleAction, &QAction::toggled, this, [viewport](bool visible) {
        viewport->setAxesVisible(visible);
        QSettings().setValue(QStringLiteral("view/axes"), visible);
    });
    axesOnTopAction = axesMenu->addAction(QStringLiteral("Sempre in primo piano (sopra solidi, superfici e schizzi)"));
    axesOnTopAction->setCheckable(true); axesOnTopAction->setChecked(viewSettings.value(QStringLiteral("view/axesOnTop"), true).toBool());
    viewport->setAxesOnTop(axesOnTopAction->isChecked());
    connect(axesOnTopAction, &QAction::toggled, this, [viewport](bool onTop) {
        viewport->setAxesOnTop(onTop);
        QSettings().setValue(QStringLiteral("view/axesOnTop"), onTop);
    });
    axisLengthAction = axesMenu->addAction(QStringLiteral("Dimensione degli assi..."));
    viewport->setAxisLength(viewSettings.value(QStringLiteral("view/axisLength"), CadViewport::kDefaultAxisLength).toDouble());
    connect(axisLengthAction, &QAction::triggered, this, [this, viewport] {
        bool accepted = false;
        const double length = ForgeCad::getDouble(this, QStringLiteral("Dimensione degli assi"),
            QStringLiteral("Scala degli assi a video (2 = dimensione standard, adattata allo zoom):"),
            viewport->axisLength(), 0.1, 100.0, 3, &accepted, false);
        if (!accepted) return;
        viewport->setAxisLength(length);
        QSettings().setValue(QStringLiteral("view/axisLength"), viewport->axisLength());
    });
    // Trasparenza dei corpi in modalita' schizzo: lo schizzo dietro (o dentro) i solidi resta leggibile.
    sketchOpacityAction = viewMenu->addAction(QStringLiteral("Trasparenza dei corpi nello schizzo..."));
    viewport->setSketchBodyOpacity(viewSettings.value(QStringLiteral("view/sketchBodyOpacity"), 0.35).toDouble());
    connect(sketchOpacityAction, &QAction::triggered, this, [this, viewport] {
        const double previous = viewport->sketchBodyOpacity();
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Trasparenza dei corpi nello schizzo"));
        auto *form = new QFormLayout(&dialog);
        form->addRow(wrappedNote(QStringLiteral("In modalita' schizzo le facce dei corpi diventano trasparenti\n"
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
    antialiasingMenu = addCadMenu(viewMenu, QStringLiteral("Antialiasing"));
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
    auto *zoomMenu = addCadMenu(viewMenu, QStringLiteral("Zoom"));
    zoomMenu->setObjectName("zoomMenu");
    zoomMenu->menuAction()->setIcon(ForgeCad::commandIcon("zoomAll"));
    zoomInAction = zoomMenu->addAction(QStringLiteral("Aumenta zoom"));
    zoomOutAction = zoomMenu->addAction(QStringLiteral("Riduci zoom"));
    resetZoomAction = zoomMenu->addAction(QStringLiteral("Zoom tutto (inquadra la scena)"));
    zoomMenu->addSeparator();
    wheelZoomAction = zoomMenu->addAction(QStringLiteral("Zoom con rotella"));
    wheelZoomAction->setCheckable(true); wheelZoomAction->setChecked(true);
    auto *zoomPreferencesAction = zoomMenu->addAction(QStringLiteral("Impostazioni zoom…"));
    zoomPreferencesAction->setObjectName("zoomPreferencesAction");
    connect(zoomPreferencesAction, &QAction::triggered, this, [this] { ForgeCad::showZoomPreferences(this); });
    // La barra personalizzata salva i percorsi del menu: conserva le scelte
    // precedenti dopo il raggruppamento sotto Visualizza/Zoom.
    {
        QSettings settings;
        const auto saved = settings.value(QStringLiteral("toolbar/custom")).toStringList();
        auto migrated = saved;
        for (QString &key : migrated)
            for (QAction *action : zoomMenu->actions())
                if (!action->isSeparator() && key == QStringLiteral("Visualizza/") + action->text()) {
                    key = QStringLiteral("Visualizza/Zoom/") + action->text();
                    break;
                }
        if (migrated != saved) settings.setValue(QStringLiteral("toolbar/custom"), migrated);
    }
    zoomInAction->setShortcut(QKeySequence(Qt::Key_Plus));
    zoomOutAction->setShortcut(QKeySequence(Qt::Key_Minus));
    resetZoomAction->setShortcut(QKeySequence(Qt::Key_0));
    modeGroup = new QActionGroup(this); modeGroup->setExclusive(true);
    auto addMode = [this, modeMenu, modeGroup, viewport](const QString &text, int mode) {
        auto *action = modeMenu->addAction(text); action->setCheckable(true); action->setChecked(mode == 2); modeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, viewport, mode] { viewport->setDisplayMode(mode); setDisplayMode(mode); });
    };
    addMode(QStringLiteral("Solo linee esterne"), 0);
    addMode(QStringLiteral("Mesh"), 1);
    addMode(QStringLiteral("Mesh + linee esterne"), 2);
    modeMenu->addSeparator();
    hiddenEdgesAction = modeMenu->addAction(QStringLiteral("Mostra e seleziona i bordi nascosti"));
    hiddenEdgesAction->setCheckable(true);
    hiddenEdgesAction->setToolTip(QStringLiteral("Visualizza gli spigoli attraverso le facce e ne consente la selezione; disattivato facilita la scelta dei contorni esterni"));
    hiddenEdgesAction->setChecked(viewSettings.value(QStringLiteral("view/hiddenEdges"), false).toBool());
    viewport->setHiddenEdgesVisible(hiddenEdgesAction->isChecked());
    connect(hiddenEdgesAction, &QAction::toggled, this, [viewport](bool visible) {
        viewport->setHiddenEdgesVisible(visible);
        QSettings().setValue(QStringLiteral("view/hiddenEdges"), visible);
    });
    topologyIdsAction = modeMenu->addAction(QStringLiteral("Mostra nomi e ID topologici (facce e bordi)"));
    topologyIdsAction->setCheckable(true);
    topologyIdsAction->setToolTip(QStringLiteral("Mostra il nome persistente e l’ID di facce e bordi con una freccia; clicca un’etichetta per illuminarne il bordo o il contorno"));
    topologyIdsAction->setChecked(viewSettings.value(QStringLiteral("view/topologyIds"), false).toBool());
    viewport->setTopologyIdsVisible(topologyIdsAction->isChecked());
    connect(topologyIdsAction, &QAction::toggled, this, [viewport](bool visible) {
        viewport->setTopologyIdsVisible(visible);
        QSettings().setValue(QStringLiteral("view/topologyIds"), visible);
    });
    studio = lightingMenu->addAction(QStringLiteral("Studio"));
    soft = lightingMenu->addAction(QStringLiteral("Morbida"));
    inspection = lightingMenu->addAction(QStringLiteral("Ispezione"));
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
    sectionAction = viewMenu->addAction(QStringLiteral("Vista in sezione"));
    sectionAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    sectionAction->setCheckable(true);
    sectionAction->setToolTip(QStringLiteral("Vista in sezione accesa/spenta (i parametri nella finestra Sezione; il piano si trascina dalla freccia)"));
    sectionParameters = viewMenu->addAction(QStringLiteral("Parametri della sezione..."));
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
        const auto apply = [viewport, basePlane, offsetBox, flipBox, capsBox, planeVisible, info, planeBox, state] {
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
}

void PdfWindow::initializeFeatureCommands(WindowSetup &setup) {
    auto &viewport = setup.viewport;
    auto &extrudeAction = setup.extrudeAction;
    auto &unionAction = setup.unionAction;
    auto &intersectionAction = setup.intersectionAction;
    auto &differenceAction = setup.differenceAction;
    auto &revolveAction = setup.revolveAction;
    auto &primitiveMenu = setup.primitiveMenu;
    auto &filletAction = setup.filletAction;
    auto &chamferAction = setup.chamferAction;
    auto &draftAction = setup.draftAction;
    auto &shellAction = setup.shellAction;
    auto &threadAction = setup.threadAction;
    auto &datumAction = setup.datumAction;
    auto &linearPatternAction = setup.linearPatternAction;
    auto &circularPatternAction = setup.circularPatternAction;
    auto &mirrorAction = setup.mirrorAction;
    auto &moveAction = setup.moveAction;
    auto &scaleAction = setup.scaleAction;
    auto &helixAction = setup.helixAction;
    auto &sweepAction = setup.sweepAction;
    auto &loftAction = setup.loftAction;
    auto &trimSurfaceAction = setup.trimSurfaceAction;
    auto &extendSurfaceAction = setup.extendSurfaceAction;
    auto &copyFacesAction = setup.copyFacesAction;
    auto &offsetSurfaceAction = setup.offsetSurfaceAction;
    auto &deleteFaceAction = setup.deleteFaceAction;
    auto &ruledSurfaceAction = setup.ruledSurfaceAction;
    auto &boundarySurfaceAction = setup.boundarySurfaceAction;
    auto &fillSurfaceAction = setup.fillSurfaceAction;
    auto &thickenAction = setup.thickenAction;
    auto &extractedCurveAction = setup.extractedCurveAction;
    auto &trimCurve3DAction = setup.trimCurve3DAction;
    auto &projectedCurveAction = setup.projectedCurveAction;
    auto &planarSurfaceAction = setup.planarSurfaceAction;
    auto &loftSurfaceAction = setup.loftSurfaceAction;
    auto &sweepSurfaceAction = setup.sweepSurfaceAction;
    auto &sewSurfacesAction = setup.sewSurfacesAction;
    auto &massAction = setup.massAction;
    auto &measureAction = setup.measureAction;
    auto &primitiveActions = setup.primitiveActions;
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
            QMessageBox::information(this, QStringLiteral("Rivoluzione"), QStringLiteral("Disegna prima uno schizzo con un profilo (chiuso: solido, aperto: superficie) e l'asse (linea di costruzione)."));
            return;
        }
        const SceneSelection selection = viewport->selection();
        ExtrusionObject initial;
        initial.feature = BodyFeature::Revolution;
        initial.sketchIndex = viewport->activeSketchIndex();
        if (selection.kind == SceneObjectKind::Sketch) initial.sketchIndex = selection.index;
        initial.revolveAxis = 0;
        initial.revolveAngle = 360.0;
        // Come lo sweep: con un solido visibile nella scena si parte unendo
        // (automatico: solo quelli toccati; senza contatto resta un corpo nuovo).
        for (const ExtrusionObject &body : viewport->extrusions())
            if (body.visible && body.solid && body.forgeBody) {
                initial.mergeOperation = 1;
                break;
            }
        const QString name = QStringLiteral("Rivoluzione %1").arg(viewport->extrusions().size() + 1);
        const SketchViewUnlock unlock(viewport);  // la vista si ruota per vedere il verso della rivoluzione
        revolutionDialog(this, viewport, QStringLiteral("Rivoluzione"), -1, initial, false, [viewport, name](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = name;
            return viewport->createBody(body);
        });
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
    connect(threadAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        definition.feature = BodyFeature::Thread;
        int selectedBody = -1;
        EdgePoint selectedFace;
        if (viewport->selectedFaceReference(selectedBody, selectedFace)) {
            definition.firstBody = selectedBody;
            definition.thread.face = selectedFace;
        } else {
            const SceneSelection selection = viewport->selection();
            if (selection.kind == SceneObjectKind::Extrusion) definition.firstBody = selection.index;
        }
        threadDialog(this, viewport, QStringLiteral("Filettatura automatica"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Thread ? 1 : 0;
            body.name = QStringLiteral("Filettatura %1").arg(count);
            body.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(body, {d.firstBody});
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
            return viewport->createBody(body, d.sweepPathRef.kind < 0 && d.sweepPath == 1 ? QVector<int>{d.firstBody} : QVector<int>{});
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
    connect(measureAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        measureDialog(this, viewport);
    });
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
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        // Di partenza il corpo e lo schizzo selezionati: lo schizzo diventa lo strumento (proiettato).
        QVector<SceneSelection> selected = viewport->selectedObjects();
        selected.prepend(viewport->selection());
        for (const SceneSelection &object : selected) {
            if (object.kind == SceneObjectKind::Extrusion && definition.firstBody < 0) definition.firstBody = object.index;
            if (object.kind == SceneObjectKind::Sketch && definition.sketchIndex < 0) {
                definition.sketchIndex = object.index;
                definition.trimProject = true;
            }
        }
        const QString name = QStringLiteral("Taglio %1").arg(viewport->extrusions().size() + 1);
        trimDialog(this, viewport, QStringLiteral("Taglia superficie"), -1, definition, [viewport, name](const ExtrusionObject &d) {
            return viewport->createSheetTrim(d, name);
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
                                                       *extendLinear, true, [&](const QVector<EdgePoint> &selected, double distance, bool linear) {
                                                           return viewport->createSheetExtend(body, selected, distance, linear, name);
                                                       });
        *extendLinear = result.linear;
        viewport->setEdgePickSize(result.distance);
        if (pickSizeBox_) pickSizeBox_->setValue(result.distance);
        if (result.reselect) viewport->resumeExtendPick(body, result.edges, result.linear);
    });
    for (QAction *action : {copyFacesAction, offsetSurfaceAction}) {
        connect(action, &QAction::triggered, this, [this, viewport, action, copyFacesAction] {
            if (viewport->sketchModeActive()) viewport->endSketchMode();
            ExtrusionObject definition;
            definition.feature = BodyFeature::SurfaceOffset;
            definition.distance = action == copyFacesAction ? 0.0 : 1.0;
            // Di partenza il corpo e la faccia selezionati nella vista.
            int body = -1;
            EdgePoint face;
            if (viewport->selectedFaceReference(body, face)) {
                definition.firstBody = body;
                definition.offsetFaces = {face};
            } else if (viewport->selection().kind == SceneObjectKind::Extrusion) {
                definition.firstBody = viewport->selection().index;
            }
            offsetDialog(this, viewport, action == copyFacesAction ? QStringLiteral("Copia facce") : QStringLiteral("Offset superficie"), -1, definition, [viewport](const ExtrusionObject &d) {
                ExtrusionObject body = d;
                body.name = (d.distance == 0.0 ? QStringLiteral("Copia facce %1") : QStringLiteral("Offset %1")).arg(viewport->extrusions().size() + 1);
                body.plane = viewport->extrusions().value(d.firstBody).plane;
                return viewport->createBody(body);
            });
        });
    }
    connect(draftAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::Draft;
        definition.draftNeutral.kind = 1;
        definition.draftNeutral.index = 0;
        int body = -1;
        EdgePoint face;
        if (viewport->selectedFaceReference(body, face)) { definition.firstBody = body; definition.offsetFaces = {face}; }
        else if (viewport->selection().kind == SceneObjectKind::Extrusion) definition.firstBody = viewport->selection().index;
        draftDialog(this, viewport, QStringLiteral("Sformo facce"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Draft ? 1 : 0;
            result.name = QStringLiteral("Sformo %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(result);
        });
    });
    connect(thickenAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::Thicken;
        definition.distance = 1.0;
        // Di partenza la superficie selezionata (tutta) o il corpo e la faccia selezionati.
        int body = -1;
        EdgePoint face;
        if (viewport->selectedFaceReference(body, face)) {
            definition.firstBody = body;
            const ExtrusionObject &chosen = viewport->extrusions().at(body);
            if (!(chosen.forgeBody && chosen.forgeBody->isSheet())) definition.offsetFaces = {face};
        } else if (viewport->selection().kind == SceneObjectKind::Extrusion) {
            definition.firstBody = viewport->selection().index;
        }
        thickenDialog(this, viewport, QStringLiteral("Spessore"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Thicken ? 1 : 0;
            result.name = QStringLiteral("Spessore %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(result);
        });
    });
    const auto createExtractedCurve = [this, viewport](bool trim) {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition; definition.feature=BodyFeature::ExtractedCurve; definition.sketchIndex=-1;
        definition.curveExtraction.trimmed=trim; definition.curveExtraction.projected=true;
        const auto selection=viewport->selection();
        if(selection.kind==SceneObjectKind::Extrusion && selection.index>=0 && selection.index<viewport->extrusions().size()
            && ForgeCad::isCurveBody(viewport->extrusions().at(selection.index))) {
            auto &source=definition.curveExtraction.source;source.kind=9;source.index=selection.index;
            source.featureId=viewport->extrusions().at(selection.index).featureId;
        }
        extractedCurveDialog(this,viewport,trim?QStringLiteral("Taglia curva 3D"):QStringLiteral("Estrai bordo 3D"),-1,definition,[viewport,trim](const ExtrusionObject &d){
            auto result=d;int count=1;
            for(const auto &body:viewport->extrusions()) count+=body.feature==BodyFeature::ExtractedCurve?1:0;
            result.name=QStringLiteral("%1 %2").arg(trim?QStringLiteral("Curva tagliata 3D"):QStringLiteral("Curva estratta 3D")).arg(count);
            return viewport->createBody(result);
        });
    };
    connect(extractedCurveAction,&QAction::triggered,this,[createExtractedCurve]{createExtractedCurve(false);});
    connect(trimCurve3DAction,&QAction::triggered,this,[createExtractedCurve]{createExtractedCurve(true);});
    connect(projectedCurveAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::ProjectedCurve;
        definition.sketchIndex = -1;
        // Di partenza il corpo e lo schizzo selezionati.
        QVector<SceneSelection> selected = viewport->selectedObjects();
        selected.prepend(viewport->selection());
        for (const SceneSelection &object : selected) {
            if (object.kind == SceneObjectKind::Extrusion) definition.firstBody = object.index;
            if (object.kind == SceneObjectKind::Sketch) definition.sketchIndex = object.index;
        }
        projectionDialog(this, viewport, QStringLiteral("Curva proiettata"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::ProjectedCurve ? 1 : 0;
            result.name = QStringLiteral("Curva proiettata %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(result);
        });
    });
    connect(shellAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::Shell;
        definition.distance = 1.0;
        // Di partenza il solido e la faccia selezionati nella vista (la faccia: l'apertura).
        int body = -1;
        EdgePoint face;
        if (viewport->selectedFaceReference(body, face)) {
            definition.firstBody = body;
            definition.offsetFaces = {face};
        } else if (viewport->selection().kind == SceneObjectKind::Extrusion) {
            definition.firstBody = viewport->selection().index;
        }
        offsetDialog(this, viewport, QStringLiteral("Svuota"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Shell ? 1 : 0;
            result.name = QStringLiteral("Svuota %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(result);
        });
    });
    connect(boundarySurfaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        boundaryDialog(this, viewport, QStringLiteral("Superficie tra curve"), -1, ExtrusionObject(), [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::BoundarySurface ? 1 : 0;
            body.name = QStringLiteral("Superficie tra curve %1").arg(count);
            return viewport->createBody(body);
        });
    });
    connect(fillSurfaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        fillDialog(this, viewport, QStringLiteral("Superficie di riempimento"), -1, ExtrusionObject(), [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::FillSurface ? 1 : 0;
            body.name = QStringLiteral("Riempimento %1").arg(count);
            return viewport->createBody(body);
        });
    });
    connect(deleteFaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        definition.feature = BodyFeature::DeleteFace;
        // Di partenza il corpo e la faccia selezionati nella vista.
        int body = -1;
        EdgePoint face;
        if (viewport->selectedFaceReference(body, face)) {
            definition.firstBody = body;
            definition.offsetFaces = {face};
        } else if (viewport->selection().kind == SceneObjectKind::Extrusion) {
            definition.firstBody = viewport->selection().index;
        }
        offsetDialog(this, viewport, QStringLiteral("Elimina facce"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::DeleteFace ? 1 : 0;
            result.name = QStringLiteral("Elimina facce %1").arg(count);
            result.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createDeleteFaces(result);
        });
    });
    connect(ruledSurfaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ruledDialog(this, viewport, QStringLiteral("Superficie rigata"), -1, ExtrusionObject(), [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::Ruled ? 1 : 0;
            body.name = QStringLiteral("Rigata %1").arg(count);
            return viewport->createBody(body);
        });
    });
    connect(planarSurfaceAction, &QAction::triggered, this, [this, viewport] {
        if (viewport->sketchModeActive()) viewport->endSketchMode();
        ExtrusionObject definition;
        const SceneSelection selection = viewport->selection();
        definition.sketchIndex = selection.kind == SceneObjectKind::Sketch ? selection.index : viewport->activeSketchIndex();
        planarDialog(this, viewport, QStringLiteral("Superficie planare"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            int count = 1;
            for (const ExtrusionObject &other : viewport->extrusions()) count += other.feature == BodyFeature::PlanarSurface ? 1 : 0;
            body.name = QStringLiteral("Superficie planare %1").arg(count);
            return viewport->createBody(body);
        });
    });
    connect(loftSurfaceAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        definition.feature = BodyFeature::Loft;
        definition.loftSurface = true;
        loftDialog(this, viewport, QStringLiteral("Loft di superfici"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = QStringLiteral("Loft %1").arg(viewport->extrusions().size() + 1);
            return viewport->createBody(body);
        });
    });
    connect(sweepSurfaceAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        definition.feature = BodyFeature::Sweep;
        definition.sweepSurface = true;
        const SceneSelection selection = viewport->selection();
        definition.sketchIndex = selection.kind == SceneObjectKind::Sketch ? selection.index : qMax(0, viewport->activeSketchIndex());
        if (selection.kind == SceneObjectKind::Extrusion && selection.index >= 0 && selection.index < viewport->extrusions().size()
            && viewport->extrusions().at(selection.index).feature == BodyFeature::Helix) {
            definition.sweepPath = 1;
            definition.firstBody = selection.index;
        }
        sweepDialog(this, viewport, QStringLiteral("Sweep di superfici"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = QStringLiteral("Sweep %1").arg(viewport->extrusions().size() + 1);
            return viewport->createBody(body, d.sweepPathRef.kind < 0 && d.sweepPath == 1 ? QVector<int>{d.firstBody} : QVector<int>{});
        });
    });
    connect(sewSurfacesAction, &QAction::triggered, this, [this, viewport] {
        ExtrusionObject definition;
        sewDialog(this, viewport, QStringLiteral("Cuci superfici"), -1, definition, [viewport](const ExtrusionObject &d) {
            ExtrusionObject body = d;
            body.name = QStringLiteral("%1 %2").arg(d.sewSolid ? QStringLiteral("Solido cucito") : QStringLiteral("Cucitura"))
                            .arg(viewport->extrusions().size() + 1);
            body.plane = viewport->extrusions().value(d.firstBody).plane;
            return viewport->createBody(body);
        });
    });

    connect(unionAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Union, QStringLiteral("Unione")); });
    connect(intersectionAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Intersection, QStringLiteral("Intersezione")); });
    connect(differenceAction, &QAction::triggered, this, [runBoolean] { runBoolean(BooleanOperation::Difference, QStringLiteral("Differenza")); });

    // Opzioni: kernel geometrico con cui si costruiscono i corpi. La scelta
    // resta per gli avvii successivi (QSettings).
}

void PdfWindow::initializeOptions(WindowSetup &setup) {
    auto &viewport = setup.viewport;
    auto &optionsMenu = setup.optionsMenu;
    auto &unitsMenu = setup.unitsMenu;
    auto &orientationAction = setup.orientationAction;
    optionsMenu = addCadMenu(menuBar(), QStringLiteral("Opzioni"));
    unitsMenu = addCadMenu(optionsMenu, QStringLiteral("Unita' di misura del documento"));
    auto *unitsGroup = new QActionGroup(unitsMenu);
    unitsGroup->setExclusive(true);
    const QList<LengthUnit> units = {LengthUnit::Millimeter, LengthUnit::Centimeter, LengthUnit::Meter,
                                     LengthUnit::Inch, LengthUnit::Foot};
    for (LengthUnit unit : units) {
        QAction *action = unitsMenu->addAction(QStringLiteral("%1 (%2)")
            .arg(ForgeCad::lengthUnitName(unit), ForgeCad::lengthUnitSymbol(unit)));
        action->setCheckable(true);
        action->setData(int(unit));
        unitsGroup->addAction(action);
    }
    connect(unitsMenu, &QMenu::aboutToShow, this, [viewport, unitsGroup] {
        for (QAction *action : unitsGroup->actions()) action->setChecked(action->data().toInt() == int(viewport->lengthUnit()));
    });
    connect(unitsGroup, &QActionGroup::triggered, this, [this, viewport](QAction *action) {
        const LengthUnit unit = LengthUnit(action->data().toInt());
        viewport->setLengthUnit(unit);
        QSettings().setValue(QStringLiteral("units/defaultLength"), int(unit));
        statusBar()->showMessage(QStringLiteral("Unita' del documento: %1. Geometria e storia convertite in visualizzazione senza ricalcolo.")
                                     .arg(ForgeCad::lengthUnitName(unit)), 7000);
    });
    optionsMenu->addSeparator();
    // Orientamento degli assi del documento: quale asse sta in alto e quale guarda
    // l'osservatore nella vista frontale (o la vista corrente come frontale).
    orientationAction = optionsMenu->addAction(QStringLiteral("Orientamento degli assi..."));
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
        form->addRow(wrappedNote(QStringLiteral("Cambia solo come si vedono gli assi (viste standard, orbita, griglia) e gli assi\n"
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
    // Tassellazione di display con CUDA (cad_cuda_tessellation): i punti delle
    // superfici B-spline si valutano sulla GPU; vale dalla prossima tassellazione.
    QAction *cudaTessellationAction = optionsMenu->addAction(QStringLiteral("Tassellazione con CUDA"));
    cudaTessellationAction->setCheckable(true);
    cudaTessellationAction->setEnabled(ForgeCad::cudaTessellationAvailable());
    cudaTessellationAction->setChecked(ForgeCad::cudaTessellationAvailable() &&
                                       QSettings().value(QStringLiteral("view/cudaTessellation"), true).toBool());
    cudaTessellationAction->setToolTip(ForgeCad::cudaTessellationAvailable()
        ? QStringLiteral("Valuta sulla GPU i punti delle superfici B-spline durante la tassellazione (solo visualizzazione)")
        : QStringLiteral("Nessuna GPU CUDA disponibile: la tassellazione resta sulla CPU"));
    ForgeCad::setCudaTessellationEnabled(cudaTessellationAction->isChecked());
    connect(cudaTessellationAction, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("view/cudaTessellation"), enabled);
        ForgeCad::setCudaTessellationEnabled(enabled);
        if (auto *label = findChild<QLabel *>(QStringLiteral("gpuStatus"))) label->setText(gpuStatusText());
    });
    // Tasto per il pan (da tenere premuto trascinando con il sinistro), in QSettings.
    QAction *panKeyAction = optionsMenu->addAction(QStringLiteral("Tasto per il pan..."));
    const auto panKeyName = [](int key) { return QKeySequence(key).toString(QKeySequence::NativeText); };
    viewport->setPanKey(QSettings().value(QStringLiteral("view/panKey"), int(Qt::Key_Space)).toInt());
    connect(panKeyAction, &QAction::triggered, this, [this, viewport, panKeyName] {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Tasto per il pan"));
        auto *form = new QFormLayout(&dialog);
        form->addRow(wrappedNote(QStringLiteral("Tieni premuto il tasto e trascina con il tasto sinistro per spostare la vista\n"
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
}

void PdfWindow::initializeSketchCommands(WindowSetup &setup) {
    auto &viewport = setup.viewport;
    auto &createSketchOnPlane = setup.createSketchOnPlane;
    auto &fileMenu = setup.fileMenu;
    auto &sketchMenu = setup.sketchMenu;
    auto &newSketchAction = setup.newSketchAction;
    auto &spatialSketchAction = setup.spatialSketchAction;
    auto &faceSketchAction = setup.faceSketchAction;
    auto &selectTool = setup.selectTool;
    auto &lineTool = setup.lineTool;
    auto &polylineTool = setup.polylineTool;
    auto &splineTool = setup.splineTool;
    auto &nurbsTool = setup.nurbsTool;
    auto &circleTool = setup.circleTool;
    auto &arcTool = setup.arcTool;
    auto &threePointArcTool = setup.threePointArcTool;
    auto &tangentArcTool = setup.tangentArcTool;
    auto &polygonTool = setup.polygonTool;
    auto &rectangleTool = setup.rectangleTool;
    auto &centerRectangleTool = setup.centerRectangleTool;
    auto &ellipseTool = setup.ellipseTool;
    auto &constructionTool = setup.constructionTool;
    auto &polygonSidesAction = setup.polygonSidesAction;
    auto &trimTool = setup.trimTool;
    auto &extendTool = setup.extendTool;
    auto &splitTool = setup.splitTool;
    auto &sketchFilletTool = setup.sketchFilletTool;
    auto &sketchChamferTool = setup.sketchChamferTool;
    auto &snapAction = setup.snapAction;
    auto &gridSnapAction = setup.gridSnapAction;
    auto &originSnapAction = setup.originSnapAction;
    auto &automaticConstraint = setup.automaticConstraint;
    auto &freeConstraint = setup.freeConstraint;
    auto &horizontalConstraint = setup.horizontalConstraint;
    auto &verticalConstraint = setup.verticalConstraint;
    auto &lengthConstraint = setup.lengthConstraint;
    auto &angleConstraint = setup.angleConstraint;
    auto &dimensionAction = setup.dimensionAction;
    auto &sketchOffsetAction = setup.sketchOffsetAction;
    auto &sketchMoveAction = setup.sketchMoveAction;
    auto &sketchLinearPattern = setup.sketchLinearPattern;
    auto &sketchCircularPattern = setup.sketchCircularPattern;
    auto &sketchMirror = setup.sketchMirror;
    auto &convertTool = setup.convertTool;
    auto &sectionReferences = setup.sectionReferences;
    auto &symmetryAxisAction = setup.symmetryAxisAction;
    auto &toggleConstruction = setup.toggleConstruction;
    auto &constraintsAction = setup.constraintsAction;
    auto &exitSketch = setup.exitSketch;
    auto &sketchNormalView = setup.sketchNormalView;
    auto &viewActions = setup.viewActions;
    sketchMenu = addCadMenu(menuBar(), QStringLiteral("Schizzo"));
    newSketchAction = sketchMenu->addAction(QStringLiteral("Nuovo schizzo..."));
    spatialSketchAction =sketchMenu->addAction(QStringLiteral("Nuovo schizzo 3D..."));
    spatialSketchAction->setToolTip(QStringLiteral("Disegna polilinee e spline nello spazio con punti XYZ liberi"));
    connect(spatialSketchAction,&QAction::triggered,this,[this,viewport]{
        if(viewport->sketchModeActive())viewport->endSketchMode();
        ExtrusionObject definition;definition.feature=BodyFeature::SpatialSketch;
        spatialSketchDialog(this,viewport,QStringLiteral("Nuovo schizzo 3D"),-1,definition,[viewport](const ExtrusionObject &d){
            auto result=d;int count=1;for(const auto &b:viewport->extrusions())count+=b.feature==BodyFeature::SpatialSketch?1:0;
            result.name=QStringLiteral("Schizzo 3D %1").arg(count);return viewport->createBody(result);
        });
    });
    faceSketchAction = sketchMenu->addAction(QStringLiteral("Nuovo schizzo sulla faccia selezionata"));
    faceSketchAction->setToolTip(QStringLiteral("Clic su una faccia piana di un corpo, poi questo comando (anche dal menu contestuale della faccia)"));
    connect(faceSketchAction, &QAction::triggered, this, [this, viewport] {
        const QString error = viewport->createSketchOnSelectedFace(QStringLiteral("Schizzo %1").arg(viewport->sketches().size() + 1));
        if (!error.isEmpty()) QMessageBox::information(this, QStringLiteral("Schizzo sulla faccia"), error);
    });
    QAction *sketchAction = sketchMenu->addAction(QStringLiteral("Disegna segmenti"));
    auto *toolMenu = addCadMenu(sketchMenu, QStringLiteral("Strumento geometrico"));
    auto *toolGroup = new QActionGroup(this); toolGroup->setExclusive(true);
    auto addTool = [this, toolMenu, toolGroup, viewport](const QString &text, DrawingTool tool, bool checked) {
        auto *action = toolMenu->addAction(text); action->setCheckable(true); action->setChecked(checked); toolGroup->addAction(action);
        action->setData(int(tool));
        connect(action, &QAction::triggered, this, [viewport, tool] { viewport->setDrawingTool(tool); });
        return action;
    };
    selectTool = addTool(QStringLiteral("Selezione"), DrawingTool::Select, true);
    selectTool->setToolTip(QStringLiteral("Selezione (Esc): clic seleziona, Maiusc+clic aggiunge, trascina i punti delle curve"));
    viewport->setToolChangedCallback([toolGroup](DrawingTool tool) {
        for (QAction *action : toolGroup->actions())
            if (action->data().isValid() && action->data().toInt() == int(tool)) action->setChecked(true);
    });
    lineTool = addTool(QStringLiteral("Linea"), DrawingTool::Line, false);
    polylineTool = addTool(QStringLiteral("Polilinea"), DrawingTool::Polyline, false);
    splineTool = addTool(QStringLiteral("Spline"), DrawingTool::Spline, false);
    auto *splineOptionsMenu = addCadMenu(toolMenu, QStringLiteral("Opzioni spline"));
    auto *uniformSpline = splineOptionsMenu->addAction(QStringLiteral("Uniforma la curvatura (C2)"));
    auto *relaxedSpline = splineOptionsMenu->addAction(QStringLiteral("Rilassa la curva"));
    auto *limitedSpline = splineOptionsMenu->addAction(QStringLiteral("Limita le oscillazioni"));
    uniformSpline->setToolTip(QStringLiteral("Curvatura continua tra i tratti; i punti di passaggio restano fissi"));
    relaxedSpline->setToolTip(QStringLiteral("Accorcia le tangenti per ridurre le gobbe; combinabile con le altre spunte, senza garanzia C2"));
    limitedSpline->setToolTip(QStringLiteral("Evita inversioni di X/Y nei singoli tratti; non elimina flessi necessari e non garantisce C2"));
    for (QAction *option : {uniformSpline, relaxedSpline, limitedSpline}) {
        option->setCheckable(true);
        connect(option, &QAction::toggled, this, [=] {
            viewport->setSplineShapeOptions({uniformSpline->isChecked(), relaxedSpline->isChecked(), limitedSpline->isChecked()});
        });
    }
    uniformSpline->setChecked(true);
    splineTool->setToolTip(QStringLiteral("Spline per punti, continuita' C2 automatica. Spunte in Schizzo > Strumento geometrico > Opzioni spline; "
                                        "per curve esistenti: Selezione, clic destro > Modifica punti e maniglie"));
    nurbsTool = addTool(QStringLiteral("NURBS"), DrawingTool::Nurbs, false);
    circleTool = addTool(QStringLiteral("Cerchio"), DrawingTool::Circle, false);
    arcTool = addTool(QStringLiteral("Arco (centro, inizio, fine)"), DrawingTool::Arc, false);
    threePointArcTool = addTool(QStringLiteral("Arco per tre punti"), DrawingTool::ThreePointArc, false);
    threePointArcTool->setToolTip(QStringLiteral("Arco per tre punti: inizio, fine, poi un punto dell'arco"));
    tangentArcTool = addTool(QStringLiteral("Arco tangente"), DrawingTool::TangentArc, false);
    tangentArcTool->setToolTip(QStringLiteral("Arco tangente: clic sull'estremo di un segmento o di un arco, poi la fine"));
    polygonTool = addTool(QStringLiteral("Poligono"), DrawingTool::Polygon, false);
    rectangleTool = addTool(QStringLiteral("Rettangolo (due angoli)"), DrawingTool::Rectangle, false);
    rectangleTool->setToolTip(QStringLiteral("Rettangolo: due angoli opposti (Maiusc: quadrato); quattro segmenti orizzontali e verticali"));
    centerRectangleTool = addTool(QStringLiteral("Rettangolo dal centro"), DrawingTool::CenterRectangle, false);
    centerRectangleTool->setToolTip(QStringLiteral("Rettangolo: centro e un angolo (Maiusc: quadrato)"));
    ellipseTool = addTool(QStringLiteral("Ellisse"), DrawingTool::Ellipse, false);
    ellipseTool->setToolTip(QStringLiteral("Ellisse: centro, estremo del primo semiasse, punto per il secondo semiasse"));
    constructionTool = addTool(QStringLiteral("Linea di costruzione"), DrawingTool::ConstructionLine, false);
    polygonSidesAction = toolMenu->addAction(QStringLiteral("Numero lati poligono..."));
    auto *editToolMenu = addCadMenu(sketchMenu, QStringLiteral("Modifica entita'"));
    auto addEditTool = [this, editToolMenu, toolGroup, viewport](const QString &text, DrawingTool tool, Qt::Key key) {
        auto *action = editToolMenu->addAction(text); action->setCheckable(true); toolGroup->addAction(action);
        action->setData(int(tool));
        action->setShortcut(QKeySequence(key));
        connect(action, &QAction::triggered, this, [viewport, tool] { viewport->setDrawingTool(tool); });
        return action;
    };
    trimTool = addEditTool(QStringLiteral("Taglia"), DrawingTool::Trim, Qt::Key_T);
    extendTool = addEditTool(QStringLiteral("Estendi"), DrawingTool::Extend, Qt::Key_E);
    splitTool = addEditTool(QStringLiteral("Spezza"), DrawingTool::Split, Qt::Key_S);
    sketchFilletTool = addEditTool(QStringLiteral("Raccordo..."), DrawingTool::Fillet, Qt::Key_R);
    sketchChamferTool = addEditTool(QStringLiteral("Smusso..."), DrawingTool::Chamfer, Qt::Key_M);
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
    snapAction = sketchMenu->addAction(QStringLiteral("Snap alla geometria"));
    snapAction->setCheckable(true); snapAction->setChecked(QSettings().value(QStringLiteral("sketch/geometrySnap"), true).toBool());
    viewport->setGeometrySnap(snapAction->isChecked());
    gridSnapAction = sketchMenu->addAction(QStringLiteral("Snap alla griglia"));
    gridSnapAction->setCheckable(true); gridSnapAction->setChecked(QSettings().value(QStringLiteral("sketch/gridSnap"), true).toBool());
    viewport->setGridSnap(gridSnapAction->isChecked());
    originSnapAction = sketchMenu->addAction(QStringLiteral("Snap all'origine"));
    originSnapAction->setCheckable(true); originSnapAction->setChecked(QSettings().value(QStringLiteral("sketch/originSnap"), true).toBool());
    viewport->setOriginSnap(originSnapAction->isChecked());
    connect(originSnapAction, &QAction::toggled, this, [viewport](bool enabled) {
        viewport->setOriginSnap(enabled);
        QSettings().setValue(QStringLiteral("sketch/originSnap"), enabled);
    });
    automaticConstraint = sketchMenu->addAction(QStringLiteral("Vincolo automatico"));
    freeConstraint = sketchMenu->addAction(QStringLiteral("Nessun vincolo (linea libera)"));
    horizontalConstraint = sketchMenu->addAction(QStringLiteral("Vincolo orizzontale"));
    verticalConstraint = sketchMenu->addAction(QStringLiteral("Vincolo verticale"));
    lengthConstraint = sketchMenu->addAction(QStringLiteral("Quota lunghezza..."));
    angleConstraint = sketchMenu->addAction(QStringLiteral("Quota angolare..."));
    // Quota: prima si scelgono le entita', poi si mette la quota (orizzontale,
    // verticale o obliqua secondo il puntatore, o con H/V/O) e se ne da' il valore.
    dimensionAction = sketchMenu->addAction(QStringLiteral("Quota (scegli le entita', poi mettila)"));
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
    sketchOffsetAction = sketchMenu->addAction(QStringLiteral("Offset entita'..."));
    sketchOffsetAction->setToolTip(QStringLiteral("Copia a distanza costante le entita' selezionate (le catene restano collegate)"));
    connect(sketchOffsetAction, &QAction::triggered, this, [this, viewport] { sketchOffsetDialog(this, viewport); });
    sketchMoveAction = sketchMenu->addAction(QStringLiteral("Sposta / ruota / copia entita'..."));
    sketchMoveAction->setToolTip(QStringLiteral("Sposta, ruota o copia le entita' selezionate dello schizzo"));
    connect(sketchMoveAction, &QAction::triggered, this, [this, viewport] { sketchMoveDialog(this, viewport); });
    auto *sketchPatternMenu = addCadMenu(sketchMenu, QStringLiteral("Ripetizione"));
    sketchLinearPattern = sketchPatternMenu->addAction(QStringLiteral("Ripetizione lineare..."));
    sketchCircularPattern = sketchPatternMenu->addAction(QStringLiteral("Ripetizione circolare..."));
    sketchMirror = sketchPatternMenu->addAction(QStringLiteral("Specchio..."));
    sketchLinearPattern->setToolTip(QStringLiteral("Copie delle entita' selezionate lungo una o due direzioni"));
    sketchCircularPattern->setToolTip(QStringLiteral("Copie delle entita' selezionate attorno a un centro"));
    sketchMirror->setToolTip(QStringLiteral("Immagine speculare delle entita' selezionate rispetto a un asse o a un segmento"));
    for (int kind = 0; kind < 3; ++kind) {
        QAction *action = kind == 0 ? sketchLinearPattern : kind == 1 ? sketchCircularPattern : sketchMirror;
        connect(action, &QAction::triggered, this, [this, viewport, kind] { sketchPatternDialog(this, viewport, kind); });
    }
    // Riferimenti esterni: spigoli e curve dei corpi, sezioni dei solidi nel piano dello schizzo.
    auto *referenceMenu = addCadMenu(sketchMenu, QStringLiteral("Riferimenti esterni"));
    auto *axisReferences = addCadMenu(referenceMenu, QStringLiteral("Assi del modello"));
    for (int axis = 0; axis < 3; ++axis) {
        QAction *action = axisReferences->addAction(QStringLiteral("Asse %1").arg(QStringLiteral("XYZ").at(axis)));
        connect(action, &QAction::triggered, this, [viewport, axis] { viewport->selectSketchReference(2, axis); });
    }
    auto *planeReferences = addCadMenu(referenceMenu, QStringLiteral("Piani di riferimento"));
    for (int plane = 0; plane < 3; ++plane) {
        QAction *action = planeReferences->addAction(plane == 0 ? QStringLiteral("XY") : plane == 1 ? QStringLiteral("XZ") : QStringLiteral("YZ"));
        connect(action, &QAction::triggered, this, [viewport, plane] { viewport->selectSketchReference(1, plane); });
    }
    referenceMenu->addSeparator();
    convertTool = referenceMenu->addAction(QStringLiteral("Converti spigoli e curve"));
    convertTool->setCheckable(true);
    toolGroup->addAction(convertTool);
    convertTool->setData(int(DrawingTool::ConvertEdges));
    convertTool->setToolTip(QStringLiteral("Clic su uno spigolo, una curva o un'entita' di un altro schizzo visibile: proietta lungo la normale al piano dello schizzo attivo"));
    connect(convertTool, &QAction::triggered, this, [viewport] { viewport->setDrawingTool(DrawingTool::ConvertEdges); });
    sectionReferences = referenceMenu->addAction(QStringLiteral("Sezione di solidi e superfici nel piano dello schizzo"));
    sectionReferences->setToolTip(QStringLiteral("Le curve esatte in cui il piano dello schizzo interseca i solidi e le superfici visibili"));
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
    symmetryAxisAction = sketchMenu->addAction(QStringLiteral("Asse di simmetria on/off per i segmenti selezionati"));
    symmetryAxisAction->setToolTip(QStringLiteral("Linea d'asse per le simmetrie e le quote di raggio e diametro (le linee di costruzione lo diventano anche quando le si usa come asse di una quota)"));
    connect(symmetryAxisAction, &QAction::triggered, this, [this, viewport] {
        const QString error = viewport->toggleSymmetryAxis();
        if (!error.isEmpty()) statusBar()->showMessage(error, 5000);
    });
    toggleConstruction = sketchMenu->addAction(QStringLiteral("Costruzione on/off per le entita' selezionate"));
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
    constraintsAction = sketchMenu->addAction(QStringLiteral("Vincoli..."));
    constraintsAction->setShortcut(QKeySequence(Qt::Key_K));
    constraintsAction->setToolTip(QStringLiteral("Finestra dei vincoli: quelli possibili per le entita' scelte e l'elenco (K)"));
    connect(constraintsAction, &QAction::triggered, this, [constraintPanel, refreshPanel] {
        (*refreshPanel)();
        constraintPanel->show();
        constraintPanel->raise();
    });
    auto *pointConstraints = addCadMenu(sketchMenu, QStringLiteral("Vincoli di punto"));
    auto *pierceAction=pointConstraints->addAction(QStringLiteral("Pierce su superficie..."));
    pierceAction->setToolTip(QStringLiteral("Seleziona un punto con Ctrl+clic, poi scegli la faccia: il punto resta sulla sua intersezione con il piano dello schizzo e segue la superficie."));
    connect(pierceAction,&QAction::triggered,this,[this,viewport] {
        const QString error=viewport->beginSurfacePierce();
        if (!error.isEmpty()) statusBar()->showMessage(error,8000);
    });
    for (ConstraintType type : {ConstraintType::Midpoint, ConstraintType::Quadrant}) {
        auto *action = pointConstraints->addAction(ForgeCad::constraintName(type));
        const QString help = type == ConstraintType::Midpoint
            ? QStringLiteral("Seleziona un punto con Ctrl+clic e il punto medio di un segmento con Ctrl+clic (oppure il segmento con Maiusc+clic), poi applica Punto medio.")
            : QStringLiteral("Seleziona un punto con Ctrl+clic e un cerchio/arco con Maiusc+clic: viene scelto il quadrante presente piu' vicino.");
        action->setToolTip(help);
        connect(action, &QAction::triggered, this, [this, viewport, type, help] {
            const QString error = viewport->addConstraint(type);
            if (!error.isEmpty()) statusBar()->showMessage(error + QLatin1Char(' ') + help, 10000);
        });
    }
    constraintPanel_ = constraintPanel;
    exitSketch = sketchMenu->addAction(QStringLiteral("Esci dalla modalita schizzo"));
    // Nello schizzo il tasto destro trascinato ruota la vista; questo la rimette perpendicolare al piano.
    sketchNormalView = sketchMenu->addAction(QStringLiteral("Vista normale allo schizzo"));
    sketchNormalView->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_8));
    sketchNormalView->setToolTip(QStringLiteral("Rimette la vista perpendicolare al piano dello schizzo (il tasto destro trascinato la ruota)"));
    connect(sketchNormalView, &QAction::triggered, this, [viewport] { viewport->alignViewToSketch(); });
    automaticConstraint->setShortcut(QKeySequence(Qt::Key_A));
    freeConstraint->setShortcut(QKeySequence(Qt::Key_O));
    horizontalConstraint->setShortcut(QKeySequence(Qt::Key_H));
    verticalConstraint->setShortcut(QKeySequence(Qt::Key_V));
    lengthConstraint->setShortcut(QKeySequence(Qt::Key_L));
    angleConstraint->setShortcut(QKeySequence(Qt::Key_G));

    *createSketchOnPlane = [viewport](int plane) {
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
    connect(snapAction, &QAction::toggled, this, [viewport](bool enabled) {
        viewport->setGeometrySnap(enabled);
        QSettings().setValue(QStringLiteral("sketch/geometrySnap"), enabled);
    });
    connect(gridSnapAction, &QAction::toggled, this, [viewport](bool enabled) {
        viewport->setGridSnap(enabled);
        QSettings().setValue(QStringLiteral("sketch/gridSnap"), enabled);
    });
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
            0.0, -360.0, 360.0, 6, &accepted, false);
        if (accepted) viewport->setLineAngle(angle);
    });
    connect(exitSketch, &QAction::triggered, this, [viewport] { viewport->endSketchMode(); });

    viewActions = std::make_shared<QList<QAction *>>();
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
}

void PdfWindow::initializeToolbarsAndStatus(WindowSetup &setup) {
    auto &viewport = setup.viewport;
    auto &newAction = setup.newAction;
    auto &openAction = setup.openAction;
    auto &saveAction = setup.saveAction;
    auto &newDrawingAction = setup.newDrawingAction;
    auto &openDrawingAction = setup.openDrawingAction;
    auto &importAction = setup.importAction;
    auto &editMenu = setup.editMenu;
    auto &deleteAction = setup.deleteAction;
    auto &rebuildAction = setup.rebuildAction;
    auto &viewMenu = setup.viewMenu;
    auto &storyboardCursorMenu = setup.storyboardCursorMenu;
    auto &functionPanelMenu = setup.functionPanelMenu;
    auto &functionsMenu = setup.functionsMenu;
    auto &extrudeAction = setup.extrudeAction;
    auto &unionAction = setup.unionAction;
    auto &intersectionAction = setup.intersectionAction;
    auto &differenceAction = setup.differenceAction;
    auto &revolveAction = setup.revolveAction;
    auto &filletAction = setup.filletAction;
    auto &chamferAction = setup.chamferAction;
    auto &draftAction = setup.draftAction;
    auto &shellAction = setup.shellAction;
    auto &threadAction = setup.threadAction;
    auto &datumAction = setup.datumAction;
    auto &linearPatternAction = setup.linearPatternAction;
    auto &circularPatternAction = setup.circularPatternAction;
    auto &mirrorAction = setup.mirrorAction;
    auto &moveAction = setup.moveAction;
    auto &scaleAction = setup.scaleAction;
    auto &helixAction = setup.helixAction;
    auto &sweepAction = setup.sweepAction;
    auto &loftAction = setup.loftAction;
    auto &trimSurfaceAction = setup.trimSurfaceAction;
    auto &extendSurfaceAction = setup.extendSurfaceAction;
    auto &copyFacesAction = setup.copyFacesAction;
    auto &offsetSurfaceAction = setup.offsetSurfaceAction;
    auto &deleteFaceAction = setup.deleteFaceAction;
    auto &ruledSurfaceAction = setup.ruledSurfaceAction;
    auto &boundarySurfaceAction = setup.boundarySurfaceAction;
    auto &fillSurfaceAction = setup.fillSurfaceAction;
    auto &thickenAction = setup.thickenAction;
    auto &extractedCurveAction = setup.extractedCurveAction;
    auto &trimCurve3DAction = setup.trimCurve3DAction;
    auto &projectedCurveAction = setup.projectedCurveAction;
    auto &planarSurfaceAction = setup.planarSurfaceAction;
    auto &loftSurfaceAction = setup.loftSurfaceAction;
    auto &sweepSurfaceAction = setup.sweepSurfaceAction;
    auto &sewSurfacesAction = setup.sewSurfacesAction;
    auto &massAction = setup.massAction;
    auto &measureAction = setup.measureAction;
    auto &modeMenu = setup.modeMenu;
    auto &qualityMenu = setup.qualityMenu;
    auto &qualityGroup = setup.qualityGroup;
    auto &lightingMenu = setup.lightingMenu;
    auto &backgroundAction = setup.backgroundAction;
    auto &historyGraphAction = setup.historyGraphAction;
    auto &treeColorMenu = setup.treeColorMenu;
    auto &treeColorAction = setup.treeColorAction;
    auto &treeColorReset = setup.treeColorReset;
    auto &highlightColorMenu = setup.highlightColorMenu;
    auto &highlightColorAction = setup.highlightColorAction;
    auto &highlightColorReset = setup.highlightColorReset;
    auto &showAllAction = setup.showAllAction;
    auto &planesAction = setup.planesAction;
    auto &gridAction = setup.gridAction;
    auto &axesMenu = setup.axesMenu;
    auto &axesVisibleAction = setup.axesVisibleAction;
    auto &axesOnTopAction = setup.axesOnTopAction;
    auto &axisLengthAction = setup.axisLengthAction;
    auto &sketchOpacityAction = setup.sketchOpacityAction;
    auto &antialiasingMenu = setup.antialiasingMenu;
    auto &wheelZoomAction = setup.wheelZoomAction;
    auto &zoomInAction = setup.zoomInAction;
    auto &zoomOutAction = setup.zoomOutAction;
    auto &resetZoomAction = setup.resetZoomAction;
    auto &modeGroup = setup.modeGroup;
    auto &hiddenEdgesAction = setup.hiddenEdgesAction;
    auto &topologyIdsAction = setup.topologyIdsAction;
    auto &studio = setup.studio;
    auto &soft = setup.soft;
    auto &inspection = setup.inspection;
    auto &sectionAction = setup.sectionAction;
    auto &sectionParameters = setup.sectionParameters;
    auto &primitiveActions = setup.primitiveActions;
    auto &optionsMenu = setup.optionsMenu;
    auto &unitsMenu = setup.unitsMenu;
    auto &orientationAction = setup.orientationAction;
    auto &sketchMenu = setup.sketchMenu;
    auto &newSketchAction = setup.newSketchAction;
    auto &spatialSketchAction = setup.spatialSketchAction;
    auto &faceSketchAction = setup.faceSketchAction;
    auto &selectTool = setup.selectTool;
    auto &lineTool = setup.lineTool;
    auto &polylineTool = setup.polylineTool;
    auto &splineTool = setup.splineTool;
    auto &nurbsTool = setup.nurbsTool;
    auto &circleTool = setup.circleTool;
    auto &arcTool = setup.arcTool;
    auto &threePointArcTool = setup.threePointArcTool;
    auto &tangentArcTool = setup.tangentArcTool;
    auto &polygonTool = setup.polygonTool;
    auto &rectangleTool = setup.rectangleTool;
    auto &centerRectangleTool = setup.centerRectangleTool;
    auto &ellipseTool = setup.ellipseTool;
    auto &constructionTool = setup.constructionTool;
    auto &polygonSidesAction = setup.polygonSidesAction;
    auto &trimTool = setup.trimTool;
    auto &extendTool = setup.extendTool;
    auto &splitTool = setup.splitTool;
    auto &sketchFilletTool = setup.sketchFilletTool;
    auto &sketchChamferTool = setup.sketchChamferTool;
    auto &snapAction = setup.snapAction;
    auto &gridSnapAction = setup.gridSnapAction;
    auto &originSnapAction = setup.originSnapAction;
    auto &automaticConstraint = setup.automaticConstraint;
    auto &freeConstraint = setup.freeConstraint;
    auto &horizontalConstraint = setup.horizontalConstraint;
    auto &verticalConstraint = setup.verticalConstraint;
    auto &lengthConstraint = setup.lengthConstraint;
    auto &angleConstraint = setup.angleConstraint;
    auto &dimensionAction = setup.dimensionAction;
    auto &sketchOffsetAction = setup.sketchOffsetAction;
    auto &sketchMoveAction = setup.sketchMoveAction;
    auto &sketchLinearPattern = setup.sketchLinearPattern;
    auto &sketchCircularPattern = setup.sketchCircularPattern;
    auto &sketchMirror = setup.sketchMirror;
    auto &convertTool = setup.convertTool;
    auto &sectionReferences = setup.sectionReferences;
    auto &symmetryAxisAction = setup.symmetryAxisAction;
    auto &toggleConstruction = setup.toggleConstruction;
    auto &constraintsAction = setup.constraintsAction;
    auto &exitSketch = setup.exitSketch;
    auto &sketchNormalView = setup.sketchNormalView;
    auto &viewActions = setup.viewActions;
    const auto decorate = [](QAction *action, const QString &icon) {
        action->setIcon(ForgeCad::commandIcon(icon));
        QString tip = action->toolTip();
        const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
        if (!shortcut.isEmpty() && !tip.contains(QStringLiteral("(") + shortcut + QStringLiteral(")"))) tip += QStringLiteral(" (") + shortcut + QStringLiteral(")");
        action->setToolTip(tip);
    };
    const QList<QPair<QAction *, QString>> iconActions = {
        {newAction, QStringLiteral("new")}, {openAction, QStringLiteral("open")}, {saveAction, QStringLiteral("save")},
        {newDrawingAction, QStringLiteral("drawingNew")}, {openDrawingAction, QStringLiteral("drawingOpen")},
        {undoAction_, QStringLiteral("undo")}, {redoAction_, QStringLiteral("redo")}, {deleteAction, QStringLiteral("delete")},
        {rebuildAction, QStringLiteral("rebuild")},
        {newSketchAction, QStringLiteral("newSketch")}, {spatialSketchAction, QStringLiteral("newSketch3D")}, {faceSketchAction, QStringLiteral("faceSketch")},
        {extrudeAction, QStringLiteral("extrude")}, {revolveAction, QStringLiteral("revolve")},
        {filletAction, QStringLiteral("fillet")}, {chamferAction, QStringLiteral("chamfer")}, {draftAction, QStringLiteral("draft")}, {thickenAction, QStringLiteral("thicken")}, {projectedCurveAction, QStringLiteral("projectedCurve")}, {extractedCurveAction, QStringLiteral("extractEdge3D")}, {trimCurve3DAction, QStringLiteral("trimEdge3D")}, {shellAction, QStringLiteral("shell")}, {threadAction, QStringLiteral("thread")},
        {trimSurfaceAction, QStringLiteral("trimSurface")}, {extendSurfaceAction, QStringLiteral("extendSurface")},
        {copyFacesAction, QStringLiteral("copyFaces")}, {offsetSurfaceAction, QStringLiteral("offsetSurface")}, {sewSurfacesAction, QStringLiteral("sewSurfaces")},
        {deleteFaceAction, QStringLiteral("deleteFace")}, {boundarySurfaceAction, QStringLiteral("boundarySurface")}, {fillSurfaceAction, QStringLiteral("fillSurface")},
        {ruledSurfaceAction, QStringLiteral("ruledSurface")}, {planarSurfaceAction, QStringLiteral("planarSurface")},
        {loftSurfaceAction, QStringLiteral("loftSurface")}, {sweepSurfaceAction, QStringLiteral("sweepSurface")}, {scaleAction, QStringLiteral("scale")}, {moveAction, QStringLiteral("move")},
        {datumAction, QStringLiteral("datumPlane")}, {importAction, QStringLiteral("import")},
        {linearPatternAction, QStringLiteral("patternLinear")}, {circularPatternAction, QStringLiteral("patternCircular")},
        {mirrorAction, QStringLiteral("mirror")},
        {helixAction, QStringLiteral("helix")}, {sweepAction, QStringLiteral("sweep")}, {loftAction, QStringLiteral("loft")}, {massAction, QStringLiteral("massProperties")}, {measureAction, QStringLiteral("measure")},
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
        {sketchMirror, QStringLiteral("sketchMirror")}, {convertTool, QStringLiteral("convertEdges")}, {sketchOffsetAction, QStringLiteral("sketchOffset")}, {sketchMoveAction, QStringLiteral("sketchMove")},
        {sectionReferences, QStringLiteral("sectionCurves")},
        {dimensionAction, QStringLiteral("dimension")}, {automaticConstraint, QStringLiteral("constraintAuto")},
        {freeConstraint, QStringLiteral("constraintFree")}, {horizontalConstraint, QStringLiteral("constraintHorizontal")},
        {verticalConstraint, QStringLiteral("constraintVertical")}, {lengthConstraint, QStringLiteral("constraintLength")},
        {angleConstraint, QStringLiteral("constraintAngle")}, {snapAction, QStringLiteral("snap")}, {gridSnapAction, QStringLiteral("gridSnap")}, {originSnapAction, QStringLiteral("originSnap")},
        {exitSketch, QStringLiteral("exitSketch")}, {sketchNormalView, QStringLiteral("viewFront")}};
    for (const auto &entry : iconActions) decorate(entry.first, entry.second);
    const QList<QPair<QAction *, QString>> viewIconActions = {
        {viewMenu->menuAction(), "viewOptions"}, {storyboardCursorMenu->menuAction(), "storyboard"},
        {storyboardCursorMenu->actions().at(0), "storyboardHorizontal"},
        {storyboardCursorMenu->actions().at(1), "storyboardLeft"}, {storyboardCursorMenu->actions().at(2), "storyboardRight"},
        {functionPanelMenu->menuAction(), "panelCorners"}, {modeMenu->menuAction(), "displayShadedEdges"},
        {qualityMenu->menuAction(), "viewQuality"}, {lightingMenu->menuAction(), "viewLight"},
        {studio, "viewLight"}, {soft, "panelBlur"}, {inspection, "viewInspect"},
        {backgroundAction, "viewBackground"}, {historyGraphAction, "storyboard"},
        {treeColorMenu->menuAction(), "viewColor"}, {treeColorAction, "viewColor"}, {treeColorReset, "viewTheme"},
        {highlightColorMenu->menuAction(), "viewHighlight"}, {highlightColorAction, "viewHighlight"}, {highlightColorReset, "viewTheme"},
        {showAllAction, "viewOptions"}, {planesAction, "datumPlane"}, {gridAction, "viewGrid"},
        {axesMenu->menuAction(), "viewAxes"}, {axesVisibleAction, "viewAxes"}, {axesOnTopAction, "viewLayers"}, {axisLengthAction, "dimension"},
        {sketchOpacityAction, "panelOpacity"}, {antialiasingMenu->menuAction(), "viewAntialias"},
        {wheelZoomAction, "viewWheel"}, {zoomInAction, "zoomIn"}, {zoomOutAction, "zoomOut"},
        {hiddenEdgesAction, "displayWireframe"}, {topologyIdsAction, "viewTopology"}, {sectionParameters, "section"}
    };
    for (const auto &entry : viewIconActions) decorate(entry.first, entry.second);
    // Dynamic choices inherit the meaningful icon of their submenu.
    const std::function<void(QMenu *)> completeViewIcons = [&](QMenu *menu) {
        for (QAction *action : menu->actions()) {
            if (action->isSeparator()) continue;
            action->setIconVisibleInMenu(true);
            if (action->icon().isNull()) action->setIcon(menu->menuAction()->icon());
            if (action->menu()) completeViewIcons(action->menu());
        }
    };
    completeViewIcons(viewMenu);
    // Il menu generale resta testuale; le voci interne conservano le icone.
    viewMenu->menuAction()->setIcon(QIcon());
    const QStringList primitiveIcons = {QStringLiteral("box"), QStringLiteral("cylinder"), QStringLiteral("sphere"), QStringLiteral("cone"), QStringLiteral("torus")};
    for (int k = 0; k < primitiveActions.size() && k < primitiveIcons.size(); ++k) decorate(primitiveActions.at(k), primitiveIcons.at(k));
    const QStringList viewIcons = {QStringLiteral("viewFront"), QStringLiteral("viewRear"), QStringLiteral("viewRight"),
                                   QStringLiteral("viewTop"), QStringLiteral("viewIso"), QStringLiteral("viewTrimetric")};
    for (int k = 0; k < viewActions->size(); ++k) decorate(viewActions->at(k), viewIcons.at(k));
    exitSketch->setToolTip(QStringLiteral("Chiudi lo schizzo"));
    const auto flyout = [](QToolBar *bar, const QList<QAction *> &actions, const QString &tip) {
        auto *button = new QToolButton(bar);
        auto *menu = new CadMenu(button);
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
        bar->setProperty("toolbarManagedVisibility", true);
        ForgeCad::configureCommandBar(bar);
        return bar;
    };

    auto *toolbar = iconBar(QStringLiteral("Modellazione"), QStringLiteral("modelingIconBar"));
    toolbar->addAction(newAction); toolbar->addAction(openAction); toolbar->addAction(saveAction); toolbar->addSeparator();
    toolbar->addAction(newDrawingAction);toolbar->addAction(openDrawingAction);toolbar->addSeparator();
    toolbar->addAction(undoAction_); toolbar->addAction(redoAction_); toolbar->addAction(deleteAction); toolbar->addAction(rebuildAction);
    toolbar->addSeparator();
    toolbar->addAction(importAction); toolbar->addSeparator();
    toolbar->addAction(newSketchAction); toolbar->addAction(spatialSketchAction); toolbar->addAction(faceSketchAction); toolbar->addAction(datumAction); toolbar->addSeparator();
    toolbar->addAction(extrudeAction); toolbar->addAction(revolveAction);
    flyout(toolbar, primitiveActions, QStringLiteral("Primitive: la freccia per le altre"));
    toolbar->addAction(filletAction); toolbar->addAction(chamferAction); toolbar->addAction(draftAction); toolbar->addAction(shellAction); toolbar->addAction(threadAction);
    toolbar->addAction(moveAction);
    toolbar->addAction(scaleAction);
    flyout(toolbar, {linearPatternAction, circularPatternAction, mirrorAction}, QStringLiteral("Ripetizioni: la freccia per le altre"));
    toolbar->addSeparator();
    toolbar->addAction(helixAction); toolbar->addAction(sweepAction); toolbar->addAction(loftAction);
    toolbar->addSeparator();
    toolbar->addAction(unionAction); toolbar->addAction(intersectionAction); toolbar->addAction(differenceAction); toolbar->addSeparator();
    toolbar->addAction(massAction); toolbar->addAction(measureAction); toolbar->addSeparator();
    toolbar->addAction(resetZoomAction);
    toolbar->addAction(sectionAction);
    flyout(toolbar, *viewActions, QStringLiteral("Viste standard: la freccia per le altre"));
    flyout(toolbar, {modeMenu->actions().at(2), modeMenu->actions().at(1), modeMenu->actions().at(0)}, QStringLiteral("Stile di visualizzazione"));
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("  %1 / Part Studio  ").arg(QGuiApplication::applicationDisplayName()), this));

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
    // Superfici: tutti i comandi come pulsanti diretti (prima erano dietro la
    // freccia di un solo pulsante nella barra di modellazione).
    auto *surfaceToolbar = iconBar(QStringLiteral("Superfici"), QStringLiteral("surfaceIconBar"));
    for (QAction *action : {trimSurfaceAction, extendSurfaceAction, copyFacesAction, offsetSurfaceAction, thickenAction, deleteFaceAction, sewSurfacesAction})
        surfaceToolbar->addAction(action);
    surfaceToolbar->addSeparator();
    for (QAction *action : {ruledSurfaceAction, boundarySurfaceAction, fillSurfaceAction, planarSurfaceAction, loftSurfaceAction, sweepSurfaceAction})
        surfaceToolbar->addAction(action);
    surfaceToolbar->addSeparator();
    surfaceToolbar->addAction(extractedCurveAction);
    surfaceToolbar->addAction(trimCurve3DAction);
    surfaceToolbar->addAction(projectedCurveAction);
    // Barra personalizzata: i comandi scelti in Opzioni -> Personalizza barra
    // degli strumenti (chiavi "Menu/Sottomenu/Comando" in QSettings).
    auto *customToolbar = iconBar(QStringLiteral("Personalizzata"), QStringLiteral("customIconBar"));
    customToolbar->setProperty("toolbarHideWhenEmpty", true);
    const auto menuCommands = [this] {
        QVector<QPair<QString, QAction *>> commands;
        const std::function<void(QMenu *, const QString &)> visit = [&](QMenu *menu, const QString &path) {
            for (QAction *action : menu->actions()) {
                if (action->isSeparator() || action->property("toolbarConfiguration").toBool()) continue;
                QString text = action->text();
                text.remove(QLatin1Char('&'));
                if (action->menu()) { visit(action->menu(), path + text + QLatin1Char('/')); continue; }
                if (!text.isEmpty()) commands.append({path + text, action});
            }
        };
        for (QAction *top : menuBar()->actions())
            if (top->menu()) visit(top->menu(), top->text().remove(QLatin1Char('&')) + QLatin1Char('/'));
        return commands;
    };
    const auto fillCustomToolbar = [customToolbar, menuCommands, flyout] {
        // clear() removes widget actions but does not destroy them.
        const auto oldActions = customToolbar->actions();
        customToolbar->clear();
        for (QAction *action : oldActions)
            if (action->parent() == customToolbar) delete action;
        const QStringList keys = QSettings().value(QStringLiteral("toolbar/custom")).toStringList();
        const auto commands = menuCommands();
        QList<QAction *> selected;
        for (const QString &key : keys)
            for (const auto &command : commands)
                if (command.first == key) { selected.append(command.second); break; }
        if (!selected.isEmpty() && QSettings().value(QStringLiteral("toolbar/customDropdown"), false).toBool()) {
            auto *button = flyout(customToolbar, selected, QStringLiteral("Comandi personalizzati"));
            button->setObjectName(QStringLiteral("customToolbarDropdown"));
        } else {
            for (QAction *action : selected) customToolbar->addAction(action);
        }
        ToolbarEditor::refreshVisibility(customToolbar);
    };
    auto *toolbarsMenu = addCadMenu(optionsMenu, QStringLiteral("Barre degli strumenti"));
    toolbarsMenu->setObjectName(QStringLiteral("toolbarsMenu"));
    toolbarsMenu->menuAction()->setProperty("toolbarConfiguration", true);
    QAction *customizeAction = toolbarsMenu->addAction(QStringLiteral("Personalizza barra Personalizzata..."));
    customizeAction->setProperty("toolbarConfiguration", true);
    customizeAction->setToolTip(QStringLiteral("Sceglie i comandi dei menu da mettere nella barra Personalizzata, e il loro ordine"));
    connect(customizeAction, &QAction::triggered, this, [this, menuCommands, fillCustomToolbar] {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Personalizza barra degli strumenti"));
        auto *layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(QStringLiteral("Spunta i comandi da mettere nella barra Personalizzata; Su e Giu' cambiano l'ordine."), &dialog));
        auto *dropdown = new QCheckBox(QStringLiteral("Raggruppa i comandi in un menu a discesa"), &dialog);
        dropdown->setChecked(QSettings().value(QStringLiteral("toolbar/customDropdown"), false).toBool());
        layout->addWidget(dropdown);
        auto *hint = new QLabel(QStringLiteral("Per spostare la barra, trascina la maniglia sul bordo.\nIn Opzioni puoi bloccare posizione e personalizzazione delle barre."), &dialog);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *filter = new QLineEdit(&dialog);
        filter->setPlaceholderText(QStringLiteral("Cerca un comando..."));
        layout->addWidget(filter);
        auto *list = new QListWidget(&dialog);
        list->setObjectName(QStringLiteral("customToolbarCommands"));
        list->setIconSize(QSize(20, 20));
        layout->addWidget(list, 1);
        const QStringList chosen = QSettings().value(QStringLiteral("toolbar/custom")).toStringList();
        const auto commands = menuCommands();
        // Prima i comandi scelti nel loro ordine, poi gli altri nell'ordine dei menu.
        for (const QString &key : chosen)
            for (const auto &command : commands)
                if (command.first == key) {
                    auto *item = new QListWidgetItem(command.second->icon(), command.first, list);
                    item->setCheckState(Qt::Checked);
                    break;
                }
        for (const auto &command : commands) {
            if (chosen.contains(command.first)) continue;
            auto *item = new QListWidgetItem(command.second->icon(), command.first, list);
            item->setCheckState(Qt::Unchecked);
        }
        auto *moves = new QHBoxLayout;
        auto *up = new QPushButton(QStringLiteral("Su"), &dialog), *down = new QPushButton(QStringLiteral("Giu'"), &dialog);
        moves->addWidget(up);
        moves->addWidget(down);
        moves->addStretch(1);
        layout->addLayout(moves);
        const auto move = [list](int step) {
            const int row = list->currentRow(), target = row + step;
            if (row < 0 || target < 0 || target >= list->count()) return;
            QListWidgetItem *item = list->takeItem(row);
            list->insertItem(target, item);
            list->setCurrentRow(target);
        };
        connect(up, &QPushButton::clicked, &dialog, [move] { move(-1); });
        connect(down, &QPushButton::clicked, &dialog, [move] { move(1); });
        connect(filter, &QLineEdit::textChanged, &dialog, [list](const QString &text) {
            for (int row = 0; row < list->count(); ++row)
                list->item(row)->setHidden(!text.isEmpty() && !list->item(row)->text().contains(text, Qt::CaseInsensitive));
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(520, 560);
        if (dialog.exec() != QDialog::Accepted) return;
        QStringList keys;
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->checkState() == Qt::Checked) keys.append(list->item(row)->text());
        QSettings().setValue(QStringLiteral("toolbar/custom"), keys);
        QSettings().remove(QStringLiteral("toolbar/layout/customIconBar"));
        QSettings().setValue(QStringLiteral("toolbar/customDropdown"), dropdown->isChecked());
        fillCustomToolbar();
    });
    auto *drawingToolbar = iconBar(QStringLiteral("Strumenti schizzo"), QStringLiteral("sketchIconBar"));
    drawingToolbar->setProperty("toolbarModeHidden", true);
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
    drawingToolbar->addAction(sketchOffsetAction);
    drawingToolbar->addAction(sketchMoveAction);
    flyout(drawingToolbar, {sketchLinearPattern, sketchCircularPattern, sketchMirror}, QStringLiteral("Ripetizioni: la freccia per le altre"));
    flyout(drawingToolbar, {convertTool, sectionReferences}, QStringLiteral("Riferimenti dai corpi: la freccia per la sezione"));
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(constraintsAction); drawingToolbar->addAction(dimensionAction);
    flyout(drawingToolbar, {automaticConstraint, freeConstraint, horizontalConstraint, verticalConstraint},
           QStringLiteral("Vincolo della prossima linea: la freccia per gli altri"));
    flyout(drawingToolbar, {lengthConstraint, angleConstraint}, QStringLiteral("Quote della prossima linea: la freccia per l'altra"));
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(snapAction); drawingToolbar->addAction(gridSnapAction); drawingToolbar->addAction(originSnapAction);
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(extrudeAction); drawingToolbar->addAction(revolveAction);
    // In modalita' schizzo si spengono solo le viste standard (la vista resta normale
    // al piano); il resto del menu Visualizza (trasparenza dei corpi, zoom...) resta attivo.
    viewport->setSketchModeCallback([this, viewActions, drawingToolbar, toolbar, surfaceToolbar](bool active) {
        for (QAction *action : *viewActions) action->setEnabled(!active);
        toolbar->setProperty("toolbarModeHidden", active);
        surfaceToolbar->setProperty("toolbarModeHidden", active);
        drawingToolbar->setProperty("toolbarModeHidden", !active);
        for (auto *bar : {toolbar, surfaceToolbar, drawingToolbar}) ToolbarEditor::refreshVisibility(bar);
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
    auto *gpuStatus = new QLabel(gpuStatusText());
    gpuStatus->setObjectName(QStringLiteral("gpuStatus"));
    auto *sceneCounts = new QLabel(QStringLiteral("Solidi: 0 · Superfici: 0"));
    sceneCounts->setObjectName(QStringLiteral("sceneCounts"));
    sceneCounts->setToolTip(QStringLiteral("Solidi e superfici separati nei corpi visibili: dopo un'unione riuscita i solidi uniti ne fanno uno"));
    statusBar()->addPermanentWidget(sceneCounts);
    statusBar()->addPermanentWidget(gpuStatus);
    viewport->setRendererCallback([this, gpuStatus](const QString &renderer) {
        glRenderer_ = renderer;
        gpuStatus->setText(gpuStatusText());
    });
    installDocumentRevisionPanel(viewMenu);
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
        settings.setValue(QStringLiteral("hiddenEdges"), viewport->hiddenEdgesVisible());
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
                                   QStringLiteral("sketch/geometrySnap"), QStringLiteral("sketch/gridSnap"),
                                   QStringLiteral("view/constraintPanel"), QStringLiteral("document/saveBodies"),
                                   QStringLiteral("view/sketchBodyOpacity"), QStringLiteral("view/hiddenEdges"),
                                   QStringLiteral("view/featureHighlightColor")})
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
        if (settings.contains(QStringLiteral("hiddenEdges"))) hiddenEdgesAction->setChecked(settings.value(QStringLiteral("hiddenEdges")).toBool());
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
        // La visibilita' delle barre dipende dalla modalita', non dallo stato
        // salvato (che puo' provenire da una sessione in schizzo).
        toolbar->setVisible(true);
        surfaceToolbar->setVisible(true);
        drawingToolbar->setVisible(false);  // compare solo in modalita' schizzo
        settings.endGroup();
    }
    auto *lockToolbars = toolbarsMenu->addAction(QStringLiteral("Blocca barre degli strumenti"));
    lockToolbars->setObjectName(QStringLiteral("lockToolbarsAction"));
    lockToolbars->setProperty("toolbarConfiguration", true);
    lockToolbars->setCheckable(true);
    lockToolbars->setToolTip(QStringLiteral("Blocca lo spostamento delle barre e la modifica dei comandi personalizzati"));
    const auto applyToolbarLock = [this, customizeAction](bool locked) {
        for (auto *bar : findChildren<QToolBar *>()) {
            bar->setMovable(!locked);
            bar->setFloatable(!locked);
        }
        customizeAction->setEnabled(!locked);
        for (auto *action : findChildren<QAction *>())
            if (action->property("toolbarEditorAction").toBool()) action->setEnabled(!locked);
    };
    lockToolbars->setChecked(QSettings().value(QStringLiteral("toolbar/locked"), false).toBool());
    applyToolbarLock(lockToolbars->isChecked());
    connect(lockToolbars, &QAction::toggled, this, [this, applyToolbarLock](bool locked) {
        QSettings().setValue(QStringLiteral("toolbar/locked"), locked);
        applyToolbarLock(locked);
        if (locked) QSettings().setValue(QStringLiteral("interface/state"), saveState());
    });
    fillCustomToolbar();  // dopo lo stato salvato: visibile se ha dei comandi
    const auto toolbarCommands = menuCommands();
    auto *editBarsMenu = addCadMenu(toolbarsMenu, QStringLiteral("Personalizza pulsanti delle barre"));
    editBarsMenu->menuAction()->setProperty("toolbarConfiguration", true);
    toolbarsMenu->addSeparator();
    for (auto *bar : {toolbar, surfaceToolbar, customToolbar, drawingToolbar}) {
        const auto saveCustomOrder = [customToolbar, toolbarCommands] {
            if (QSettings().value(QStringLiteral("toolbar/customDropdown"), false).toBool()) return;
            QStringList keys;
            for (auto *action : customToolbar->actions())
                for (const auto &command : toolbarCommands)
                    if (command.second == action) { keys.append(command.first); break; }
            QSettings().setValue(QStringLiteral("toolbar/custom"), keys);
        };
        auto *editor = new ToolbarEditor(bar, toolbarCommands, [lockToolbars] { return lockToolbars->isChecked(); },
                                        bar == customToolbar ? std::function<void()>(saveCustomOrder) : std::function<void()>());
        auto *editAction = editBarsMenu->addAction(bar->windowTitle() + QStringLiteral("..."));
        editAction->setObjectName(bar->objectName() + QStringLiteral("EditorAction"));
        editAction->setProperty("toolbarEditorAction", true);
        editAction->setProperty("toolbarConfiguration", true);
        editAction->setEnabled(!lockToolbars->isChecked());
        connect(editAction, &QAction::triggered, editor, [editor] { editor->edit(); });
        auto *visibleAction = toolbarsMenu->addAction(bar->windowTitle());
        visibleAction->setObjectName(bar->objectName() + QStringLiteral("VisibleAction"));
        visibleAction->setProperty("toolbarConfiguration", true);
        visibleAction->setCheckable(true);
        const QString visibilityKey = QStringLiteral("toolbar/visible/") + bar->objectName();
        visibleAction->setChecked(QSettings().value(visibilityKey, true).toBool());
        if (bar == drawingToolbar) visibleAction->setToolTip(QStringLiteral("Mostra la barra quando è attiva la modalità schizzo"));
        connect(visibleAction, &QAction::toggled, bar, [bar, visibilityKey](bool visible) {
            QSettings().setValue(visibilityKey, visible);
            ToolbarEditor::refreshVisibility(bar);
        });
        ToolbarEditor::refreshVisibility(bar);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(bar, &QWidget::customContextMenuRequested, this, [this, bar, lockToolbars, editAction, toolbarsMenu](const QPoint &point) {
            CadMenu menu(this);
            menu.addAction(editAction);
            menu.addSeparator();
            menu.addAction(lockToolbars);
            menu.addSeparator();
            menu.addMenu(toolbarsMenu);
            menu.exec(bar->mapToGlobal(point));
        });
    }
    const auto markMutation=[this](QAction *action){if(!documentMutationActions_.contains(action))documentMutationActions_.append(action);};
    const std::function<void(QMenu *)> markMenu=[&](QMenu *menu){for(auto *action:menu->actions()){if(action->menu())markMenu(action->menu());else if(!action->isSeparator())markMutation(action);}};
    for(auto *menu:{editMenu,functionsMenu,sketchMenu,unitsMenu})markMenu(menu);
    markMutation(saveAction);markMutation(importAction);markMutation(orientationAction);
    saveAction->setObjectName("documentSaveAction");importAction->setObjectName("documentImportAction");
    documentRevisionStatus_=new QLabel(this);documentRevisionStatus_->setObjectName("documentRevisionStatus");
    statusBar()->addPermanentWidget(documentRevisionStatus_);
    auto *revisionTimer=new QTimer(this);revisionTimer->setInterval(5000);
    connect(revisionTimer,&QTimer::timeout,this,[this]{
        if(foregroundProgressDepth_||viewport_->interactionLocked())return;
        refreshDocumentEditAccess();updateWindowTitle();
    });revisionTimer->start();
    documentRevisionDock_->setVisible(QSettings().value("revisions/modelPanelVisible",true).toBool());
    refreshDocumentEditAccess();updateWindowTitle();
    setTheme(QApplication::palette().color(QPalette::Window).lightness() < 128);
    connect(recovery_,&ForgeCad::DocumentRecovery::checkpointFinished,this,[this](quint64,bool,const QString &error){
        if(!error.isEmpty())statusBar()->showMessage(QStringLiteral("Backup automatico non riuscito: %1").arg(error),12000);
    });
    connect(recovery_,&ForgeCad::DocumentRecovery::cleared,this,[this](quint64 revision,const QString &error){
        if(!error.isEmpty())statusBar()->showMessage(QStringLiteral("Pulizia backup: %1").arg(error),8000);
        if(recoveryClosing_&&revision==recoveryCloseRevision_)recovery_->shutdown();
    });
    connect(recovery_,&ForgeCad::DocumentRecovery::stopped,this,[this]{if(recoveryClosing_){recoveryCloseReady_=true;close();}});
    connect(recovery_,&ForgeCad::DocumentRecovery::discarded,this,[this](const QString &error){
        if(!error.isEmpty())statusBar()->showMessage(error,10000);
    });
    connect(recovery_,&ForgeCad::DocumentRecovery::restored,this,[this](DocumentState state,const ForgeCad::RecoveryEntry &entry,const QString &error){
        if(!error.isEmpty()){QMessageBox::warning(this,QStringLiteral("Recupero documento"),error);return;}
        ForgeCad::forkDocumentIdentity(state);
        auto *window=new PdfWindow;window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();window->beginForegroundProgress(QStringLiteral("Ricostruzione del modello recuperato..."));
        window->loadingDocument_=true;window->viewport_->loadDocument(std::move(state));window->loadingDocument_=false;
        window->endForegroundProgress();
        window->documentModified_=true;window->recoveredSource_=entry;window->recovery_->changed();
        window->refreshDocumentEditAccess();window->updateWindowTitle();window->show();
        window->statusBar()->showMessage(QStringLiteral("Recuperato %1. Salva con nome per conservare la copia.").arg(entry.title),15000);
    });
    connect(recovery_,&ForgeCad::DocumentRecovery::entriesFound,this,[this](const QVector<ForgeCad::RecoveryEntry> &entries,const QString &error){
        if(!recoveryScanPending_)return;
        recoveryScanPending_=false;
        if(recoveryClosing_)return;
        if(foregroundProgressDepth_||loadingDocument_||viewport_->interactionLocked()||QApplication::activeModalWidget()){
            if(!entries.isEmpty())statusBar()->showMessage(QStringLiteral("Backup disponibili in File → Recupera documenti..."),10000);
            return;
        }
        if(!error.isEmpty()){statusBar()->showMessage(error,10000);return;}
        if(entries.isEmpty()){
            if(!recoveryScanAutomatic_)statusBar()->showMessage(QStringLiteral("Nessun documento da recuperare."),6000);
            return;
        }
        QDialog dialog(this);dialog.setObjectName(QStringLiteral("documentRecoveryDialog"));dialog.setWindowTitle(QStringLiteral("Documenti da recuperare"));
        auto *layout=new QVBoxLayout(&dialog);
        auto *hint=new QLabel(QStringLiteral("Sono disponibili copie automatiche di sessioni interrotte. Il recupero apre una nuova copia e lascia intatto il file originale."),&dialog);
        hint->setWordWrap(true);layout->addWidget(hint);
        auto *table=new QTableWidget(entries.size(),3,&dialog);table->setObjectName(QStringLiteral("documentRecoveryTable"));
        table->setHorizontalHeaderLabels({QStringLiteral("Documento"),QStringLiteral("Ultimo backup"),QStringLiteral("Dimensione")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);
        for(int row=0;row<entries.size();++row){const auto &entry=entries[row];
            auto *name=new QTableWidgetItem(entry.originalPath.isEmpty()?entry.title:entry.originalPath);name->setToolTip(entry.originalPath);table->setItem(row,0,name);
            table->setItem(row,1,new QTableWidgetItem(entry.savedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
            table->setItem(row,2,new QTableWidgetItem(QStringLiteral("%1 KiB").arg((entry.bytes+1023)/1024)));
        }
        table->setTextElideMode(Qt::ElideMiddle);table->resizeColumnsToContents();table->setColumnWidth(2,qMax(110,table->columnWidth(2)));
        table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table->selectRow(0);layout->addWidget(table);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
        buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Chiudi"));
        auto *restore=buttons->addButton(QStringLiteral("Recupera come copia"),QDialogButtonBox::AcceptRole);restore->setObjectName(QStringLiteral("restoreRecoveryButton"));
        auto *remove=buttons->addButton(QStringLiteral("Elimina backup"),QDialogButtonBox::DestructiveRole);remove->setObjectName(QStringLiteral("discardRecoveryButton"));
        connect(restore,&QPushButton::clicked,&dialog,[&]{const int row=table->currentRow();if(row>=0){recovery_->restore(entries[row]);dialog.accept();}});
        connect(remove,&QPushButton::clicked,&dialog,[&]{const int row=table->currentRow();if(row>=0){recovery_->discard(entries[row]);dialog.accept();}});
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);dialog.resize(800,340);dialog.exec();
    });
    // Solo la prima finestra dell'app propone il recupero all'avvio. Il menu
    // File resta disponibile anche dopo aver chiuso la proposta.
    static bool startupRecoveryScheduled=false;
    if(!startupRecoveryScheduled){startupRecoveryScheduled=true;QTimer::singleShot(0,this,[this]{if(isVisible())showRecoveryDocuments(true);});}
}


bool PdfWindow::event(QEvent *event) {
    const bool handled = QMainWindow::event(event);
    if (event->type() == QEvent::WindowActivate&&viewport_&&documentRevisionStatus_) {refreshDocumentEditAccess();updateWindowTitle();}
    if (event->type() == QEvent::ApplicationPaletteChange) {
        setTheme(QApplication::palette().color(QPalette::Window).lightness() < 128);
        // Le icone vettoriali leggono la palette corrente al ridisegno.
        for (auto *bar : findChildren<QToolBar *>()) bar->update();
    }
    return handled;
}

void PdfWindow::refreshDocumentEditAccess(){
    if(!viewport_||foregroundProgressDepth_||viewport_->interactionLocked())return;
    ForgeCad::workspaceDocumentEditAccess(documentPath_,viewport_->documentVersion(),viewport_->documentBaseCheckinId(),documentEditAccess_);
    if(documentEditAccess_.readOnly)documentLock_.reset();
    if(!documentPath_.isEmpty()&&!documentEditAccess_.readOnly&&(!documentLock_||!documentLock_->owns(documentPath_))){
        documentLock_.reset();documentEditAccess_.readOnly=true;
        documentEditAccess_.reason=QStringLiteral("Copia aperta in consultazione. Eseguire check-out per passare alla modifica; la copia potrebbe essere in uso da un'altra sessione.");
    }
    viewport_->setDocumentReadOnly(documentEditAccess_.readOnly);
    if(documentEditAccess_.readOnly){
        for(const auto &action:documentMutationActions_)if(action&&action->isEnabled()){
            action->setEnabled(false);if(!revisionDisabledActions_.contains(action))revisionDisabledActions_.append(action);
        }
    }else{
        for(const auto &action:revisionDisabledActions_)if(action)action->setEnabled(true);
        revisionDisabledActions_.clear();
    }
    if(documentRevisionStatus_){documentRevisionStatus_->setText(!viewport_->documentVersion().checkinId.isNull()?QStringLiteral("Revisione storica · Sola lettura"):documentEditAccess_.readOnly?QStringLiteral("Sola lettura · Consultazione"):QString());documentRevisionStatus_->setToolTip(documentEditAccess_.reason);}
    updateUndoActions();
    refreshDocumentRevisionPanel();
}
void PdfWindow::updateWindowTitle() {
    const QString name = documentPath_.isEmpty() ? QStringLiteral("Senza nome") : QFileInfo(documentPath_).fileName();
    setWindowTitle(QStringLiteral("%1%2%3 - %4").arg(name, documentModified_ ? QStringLiteral(" *") : QString(),
        documentEditAccess_.readOnly?QStringLiteral(" [sola lettura]"):QString(),QGuiApplication::applicationDisplayName()));
}

void PdfWindow::rememberRecentDocument(const QString &path) {
    QSettings settings;
    QStringList paths = settings.value(QStringLiteral("document/recentFiles")).toStringList();
    const QString canonical = ForgeCad::canonicalDocumentPath(path);
    for (int i = paths.size() - 1; i >= 0; --i)
        if (ForgeCad::canonicalDocumentPath(paths.at(i)) == canonical) paths.removeAt(i);
    paths.prepend(canonical);
    while (paths.size() > 10) paths.removeLast();
    settings.setValue(QStringLiteral("document/recentFiles"), paths);
    refreshRecentDocuments();
}

void PdfWindow::refreshRecentDocuments() {
    if (!recentDocumentsMenu_) return;
    recentDocumentsMenu_->clear();
    const QStringList paths = QSettings().value(QStringLiteral("document/recentFiles")).toStringList();
    if (paths.isEmpty()) {
        recentDocumentsMenu_->addAction(QStringLiteral("Nessun file recente"))->setEnabled(false);
        return;
    }
    for (int i = 0; i < qMin(10, int(paths.size())); ++i) {
        const QString path = paths.at(i);
        auto *action = recentDocumentsMenu_->addAction(QStringLiteral("&%1 %2").arg(i + 1).arg(QString(path).replace('&', QStringLiteral("&&"))));
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] { openDocumentPath(path); });
    }
    recentDocumentsMenu_->addSeparator();
    auto *clear = recentDocumentsMenu_->addAction(QStringLiteral("Svuota elenco"));
    clear->setObjectName(QStringLiteral("clearRecentDocuments"));
    connect(clear, &QAction::triggered, this, [this] {
        QSettings().remove(QStringLiteral("document/recentFiles"));
        refreshRecentDocuments();
    });
}

// Chiede se salvare le modifiche; false se l'utente annulla (o il salvataggio fallisce).
bool PdfWindow::maybeSaveChanges() {
    if (!documentModified_) return true;
    const auto answer = QMessageBox::question(this, QGuiApplication::applicationDisplayName(),
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
        foregroundProgress_->setWindowTitle(QStringLiteral("%1 - operazione in corso").arg(QGuiApplication::applicationDisplayName()));
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

void PdfWindow::newDrawing() {
    if (foregroundProgressDepth_>0 || viewport_->interactionLocked()) return;
    if ((documentPath_.isEmpty()||documentModified_)&&!saveDocument(false)) return;
    auto *drawing = new ForgeCad::DrawingWindow(this);
    const auto error = drawing->createFromModel(documentPath_);
    if (!error.isEmpty()) { delete drawing; QMessageBox::warning(this,QStringLiteral("Tavola 2D"),error); return; }
    drawing->setAttribute(Qt::WA_DeleteOnClose); drawing->show();
}

void PdfWindow::openDrawing() {
    const auto path=ForgeCad::chooseLinkedDrawing(this,documentPath_,viewport_->currentDocument().version.documentId);
    if (path.isEmpty()) return;
    for (auto *child:children()) if (auto *drawing=dynamic_cast<ForgeCad::DrawingWindow *>(child))
        if (drawing->filePath()==ForgeCad::canonicalDocumentPath(path) && drawing->document().sheet.drawing.version.checkinId.isNull()) { drawing->show(); drawing->raise(); drawing->activateWindow(); return; }
    auto *drawing=new ForgeCad::DrawingWindow(this); const auto error=drawing->openPath(path);
    if (!error.isEmpty()) { delete drawing; QMessageBox::warning(this,QStringLiteral("Apri tavola 2D"),error); return; }
    drawing->setAttribute(Qt::WA_DeleteOnClose); drawing->show();
}

void PdfWindow::newDocument() {
    if (!maybeSaveChanges()) return;
    clearDocumentRecovery();
    loadingDocument_ = true;
    viewport_->setDocumentReadOnly(false);
    DocumentState empty;
    empty.orientation = defaultAxesOrientation();
    empty.orientationSet = true;
    empty.lengthUnit = defaultLengthUnit();
    empty.lengthUnitSet = true;
    viewport_->loadDocument(empty);
    loadingDocument_ = false;
    documentLock_.reset();
    documentDiskFingerprint_.clear();
    documentPath_.clear();
    documentModified_ = false;
    refreshDocumentEditAccess();
    updateWindowTitle();
}

void PdfWindow::openDocument() {
    QFileDialog dialog(this, QStringLiteral("Apri"), QFileInfo(documentPath_).absolutePath(),
                       QStringLiteral("Documenti %1 (*.prt);;Tutti i file (*)").arg(QGuiApplication::applicationDisplayName()));
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

QString PdfWindow::gpuStatusText() const {
    QString cuda;
    if (!forgecad_cuda_available()) cuda = FORGECAD_HAS_CUDA ? QStringLiteral("CUDA compilato, GPU runtime non disponibile")
                                                              : QString::fromUtf8(forgecad_cuda_backend());
    else if (ForgeCad::cudaTessellationEnabled()) cuda = QStringLiteral("CUDA: tassellazione su GPU");
    else cuda = QStringLiteral("CUDA disponibile (tassellazione su CPU)");
    return QStringLiteral("%1 | OpenGL: %2").arg(cuda, glRenderer_);
}

bool PdfWindow::openDocumentPath(const QString &path) {
    const QString resolved = ForgeCad::canonicalDocumentPath(path);
    if (documentLock_ && documentLock_->owns(resolved)) {
        rememberRecentDocument(resolved);
        return true;
    }
    if (!maybeSaveChanges()) return false;
    std::unique_ptr<ForgeCad::DocumentFileLock> candidate;
    const QString fileName = QFileInfo(path).fileName();
    beginForegroundProgress(QStringLiteral("Lettura di %1... 0%").arg(fileName), 100);
    updateForegroundProgress(QStringLiteral("Lettura del file %1... 5%").arg(fileName), 5);
    DocumentState state;
    QString error = ForgeCad::loadDocumentFile(resolved, state);
    QByteArray fingerprint;
    if(error.isEmpty()){
        ForgeCad::WorkspaceEditAccess access;
        ForgeCad::workspaceDocumentEditAccess(resolved,state.version,state.baseCheckinId,access);
        ForgeCad::WorkspaceRevisionStatus status;
        const auto statusError=ForgeCad::workspaceRevisionStatus(resolved,state,status);
        // Un check-out persistente viene ripreso soltanto su comando esplicito.
        if(!access.readOnly&&(statusError.isEmpty()?!status.hasSession:state.baseCheckinId.isNull())){
            candidate=std::make_unique<ForgeCad::DocumentFileLock>(resolved);
            if(!candidate->acquire().isEmpty())candidate.reset();
            else{
                error=ForgeCad::loadDocumentFile(resolved,state);
                if(error.isEmpty())error=ForgeCad::documentFileFingerprint(resolved,fingerprint);
            }
        }
    }
    if (!error.isEmpty()) {
        endForegroundProgress();
        QMessageBox::warning(this, QStringLiteral("Apri"), error);
        return false;
    }
    clearDocumentRecovery();
    updateForegroundProgress(QStringLiteral("Definizione caricata, preparazione della geometria... 15%"), 15);
    // I file senza orientamento (versioni vecchie) prendono quello predefinito.
    if (!state.orientationSet) {
        state.orientation = defaultAxesOrientation();
        state.orientationSet = true;
    }
    loadingDocument_ = true;
    viewport_->setDocumentReadOnly(false);
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
    documentLock_ = std::move(candidate);
    documentDiskFingerprint_=fingerprint;
    documentPath_ = resolved;
    documentModified_ = false;
    refreshDocumentEditAccess();
    rememberRecentDocument(resolved);
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
                                            QStringLiteral("Documenti %1 (*.prt)").arg(QGuiApplication::applicationDisplayName()));
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().compare(QLatin1String(ForgeCad::kDocumentSuffix), Qt::CaseInsensitive) != 0)
            path += QStringLiteral(".") + QLatin1String(ForgeCad::kDocumentSuffix);
    }
    return saveDocumentPath(path);
}

bool PdfWindow::saveDocumentPath(const QString &path) {
    const QString resolved = ForgeCad::canonicalDocumentPath(path);
    refreshDocumentEditAccess();
    if(!documentPath_.isEmpty()&&resolved==documentPath_&&documentEditAccess_.readOnly){
        statusBar()->showMessage(documentEditAccess_.reason,10000);return false;
    }
    std::unique_ptr<ForgeCad::DocumentFileLock> candidate;
    const ForgeCad::DocumentFileLock *lock = documentLock_.get();
    if (!lock || lock->path() != resolved) {
        candidate = std::make_unique<ForgeCad::DocumentFileLock>(resolved);
        const QString error = candidate->acquire();
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Salva — accesso esclusivo"), error);
            return false;
        }
        lock = candidate.get();
    }
    beginForegroundProgress(QStringLiteral("Salvataggio di %1...").arg(QFileInfo(path).fileName()));
    DocumentState saved = viewport_->currentDocument();
    if (!documentPath_.isEmpty() && ForgeCad::canonicalDocumentPath(documentPath_) != resolved)
        ForgeCad::forkDocumentIdentity(saved);
    const QString error = ForgeCad::saveDocumentFile(resolved, saved,
                                                     QSettings().value(QStringLiteral("document/saveBodies"), true).toBool(), lock);
    endForegroundProgress();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Salva"), error);
        return false;
    }
    if (candidate) documentLock_ = std::move(candidate);
    documentDiskFingerprint_.clear();
    ForgeCad::documentFileFingerprint(resolved,documentDiskFingerprint_);
    viewport_->adoptDocumentIdentity(saved.version, saved.baseCheckinId);
    documentPath_ = resolved;
    documentModified_ = false;
    refreshDocumentEditAccess();
    rememberRecentDocument(resolved);
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("Salvato %1 (%2 byte)").arg(path).arg(QFileInfo(path).size()), 6000);
    clearDocumentRecovery();
    return true;
}

QString PdfWindow::acquireDocumentWriteAccess() {
    if(documentLock_&&documentLock_->owns(documentPath_))return {};
    auto candidate=std::make_unique<ForgeCad::DocumentFileLock>(documentPath_);
    auto error=candidate->acquire();if(!error.isEmpty())return error;
    QByteArray fingerprint;error=ForgeCad::documentFileFingerprint(documentPath_,fingerprint);
    if(!error.isEmpty())return error;
    if(documentModified_){
        if(documentDiskFingerprint_.isEmpty()||fingerprint!=documentDiskFingerprint_)
            return QStringLiteral("La copia su disco e' cambiata. Salvare le modifiche locali con un altro nome prima di riprendere il check-out.");
    }else if(documentDiskFingerprint_.isEmpty()||fingerprint!=documentDiskFingerprint_){
        DocumentState current;error=ForgeCad::loadDocumentFile(documentPath_,current);
        if(!error.isEmpty())return error;
        if(current.version.documentId!=viewport_->documentVersion().documentId||!current.version.checkinId.isNull())
            return QStringLiteral("Il documento su disco ha un'altra identita'. Riaprirlo prima di modificarlo.");
        // La rilettura avviene sotto esclusiva, prima di prenotare la base.
        beginForegroundProgress(QStringLiteral("Aggiornamento della copia di lavoro..."));
        loadingDocument_=true;viewport_->setDocumentReadOnly(false);viewport_->loadDocument(std::move(current));loadingDocument_=false;
        endForegroundProgress();
    }
    documentDiskFingerprint_=fingerprint;documentLock_=std::move(candidate);return {};
}

QString PdfWindow::runDocumentRevisionAction(DocumentRevisionAction action, const QString &first, const QString &second) {
    if (foregroundProgressDepth_>0 || viewport_->interactionLocked()) return QStringLiteral("Attendere la conclusione dell'operazione in corso.");
    if (documentPath_.isEmpty()) return QStringLiteral("Salvare prima il documento nel workspace.");
    if(!viewport_->documentVersion().checkinId.isNull())return QStringLiteral("La revisione storica e' in sola lettura.");
    const bool owned=documentLock_&&documentLock_->owns(documentPath_);
    if(!owned&&action==DocumentRevisionAction::Checkin)return QStringLiteral("Riprendere il check-out prima di pubblicare le modifiche.");
    const auto lockError=acquireDocumentWriteAccess();
    if(!lockError.isEmpty()){refreshDocumentEditAccess();return lockError;}
    auto working = viewport_->currentDocument();
    const auto oldBase = working.baseCheckinId;
    const bool bodies = QSettings().value(QStringLiteral("document/saveBodies"),true).toBool();
    ForgeCad::RevisionRecord record; bool matchesWorking = true;
    viewport_->setInteractionLocked(true);
    beginForegroundProgress(QStringLiteral("Aggiornamento delle revisioni..."));
    QString error;
    switch (action) {
    case DocumentRevisionAction::Checkout:
        error = ForgeCad::checkoutWorkspaceDocument(documentPath_,working,first); break;
    case DocumentRevisionAction::Checkin:
        error = ForgeCad::checkinWorkspaceDocument(documentPath_,working,first,second,record,bodies,documentLock_.get()); break;
    case DocumentRevisionAction::CompletePending:
        error = ForgeCad::completeWorkspaceCheckin(documentPath_,working,record,matchesWorking,bodies,documentLock_.get()); break;
    case DocumentRevisionAction::CancelCheckout:
        error = ForgeCad::cancelWorkspaceCheckout(documentPath_,working); break;
    }
    endForegroundProgress(); viewport_->setInteractionLocked(false);
    viewport_->adoptDocumentIdentity(working.version,working.baseCheckinId);
    if (working.baseCheckinId!=oldBase && !error.isEmpty()) documentModified_=true;
    if (error.isEmpty()) {
        if (action==DocumentRevisionAction::Checkin || action==DocumentRevisionAction::CompletePending) {
            documentModified_=false;
            if(matchesWorking)clearDocumentRecovery();
            statusBar()->showMessage(matchesWorking
                ? QStringLiteral("Check-in %1, revisione %2, completato.").arg(record.sequence).arg(record.technicalRevision)
                : QStringLiteral("Check-in pendente completato. Le modifiche successive restano nella copia di lavoro."),10000);
        } else statusBar()->showMessage(action==DocumentRevisionAction::Checkout
                ? QStringLiteral("Check-out attivo: %1.").arg(first)
                : QStringLiteral("Check-out annullato; modifiche locali mantenute."),6000);
    }
    if(error.isEmpty()){
        documentDiskFingerprint_.clear();ForgeCad::documentFileFingerprint(documentPath_,documentDiskFingerprint_);
    }
    if(!owned&&!error.isEmpty())documentLock_.reset();
    refreshDocumentEditAccess();updateWindowTitle(); return error;
}

void PdfWindow::installDocumentRevisionPanel(QMenu *viewMenu) {
    documentRevisionDock_=new QDockWidget(QStringLiteral("Revisioni"),this);documentRevisionDock_->setObjectName("documentRevisionDock");
    documentRevisionDock_->setMinimumWidth(300);auto *body=new QWidget(documentRevisionDock_);auto *layout=new QVBoxLayout(body);
    documentRevisionPanelStatus_=new QLabel(body);documentRevisionPanelStatus_->setObjectName("documentRevisionPanelStatus");documentRevisionPanelStatus_->setWordWrap(true);documentRevisionPanelStatus_->setTextFormat(Qt::PlainText);layout->addWidget(documentRevisionPanelStatus_);
    auto *commands=new QHBoxLayout;
    for(const auto &entry:QList<QPair<QString,QString>>{{"revisionCheckoutAction","open"},{"revisionCheckinAction","save"},{"revisionCompleteAction","rebuild"},{"revisionCancelAction","undo"}}){
        auto *action=findChild<QAction *>(entry.first);action->setIcon(ForgeCad::commandIcon(entry.second));auto *button=new QToolButton(body);button->setDefaultAction(action);button->setToolTip(action->text());commands->addWidget(button);
    }
    layout->addLayout(commands);
    documentRevisionHistory_=new QTableWidget(0,5,body);documentRevisionHistory_->setObjectName("documentRevisionHistory");
    documentRevisionHistory_->setHorizontalHeaderLabels({QStringLiteral("N."),QStringLiteral("Rev."),QStringLiteral("Autore"),QStringLiteral("Data"),QStringLiteral("Commento")});
    documentRevisionHistory_->setEditTriggers(QAbstractItemView::NoEditTriggers);documentRevisionHistory_->setSelectionBehavior(QAbstractItemView::SelectRows);documentRevisionHistory_->setSelectionMode(QAbstractItemView::SingleSelection);documentRevisionHistory_->horizontalHeader()->setStretchLastSection(true);layout->addWidget(documentRevisionHistory_,1);
    documentOpenRevision_=new QPushButton(ForgeCad::commandIcon("open"),QStringLiteral("Apri revisione selezionata"),body);documentOpenRevision_->setObjectName("documentOpenSelectedRevision");documentOpenRevision_->setToolTip(QStringLiteral("Apre il modello archiviato in una finestra separata in sola lettura."));documentOpenRevision_->setEnabled(false);layout->addWidget(documentOpenRevision_);
    connect(documentOpenRevision_,&QPushButton::clicked,this,&PdfWindow::openSelectedDocumentRevision);
    connect(documentRevisionHistory_,&QTableWidget::cellDoubleClicked,this,[this](int,int){openSelectedDocumentRevision();});
    connect(documentRevisionHistory_,&QTableWidget::itemSelectionChanged,this,[this]{documentOpenRevision_->setEnabled(documentRevisionHistory_->currentRow()>=0&&!foregroundProgressDepth_&&!viewport_->interactionLocked());});
    auto *refresh=new QPushButton(QStringLiteral("Aggiorna stato"),body);refresh->setObjectName("documentRefreshRevisions");layout->addWidget(refresh);connect(refresh,&QPushButton::clicked,this,[this]{refreshDocumentEditAccess();updateWindowTitle();});
    documentRevisionDock_->setWidget(body);addDockWidget(Qt::RightDockWidgetArea,documentRevisionDock_);
    auto *toggle=documentRevisionDock_->toggleViewAction();toggle->setObjectName("documentRevisionPanelAction");toggle->setText(QStringLiteral("Pannello revisioni"));
    toggle->setIcon(ForgeCad::commandIcon("storyboard"));toggle->setIconVisibleInMenu(true);viewMenu->addAction(toggle);
    documentRevisionDock_->setVisible(QSettings().value("revisions/modelPanelVisible",true).toBool());
    connect(documentRevisionDock_,&QDockWidget::visibilityChanged,this,[this](bool visible){if(isVisible())QSettings().setValue("revisions/modelPanelVisible",visible);});
}
void PdfWindow::refreshDocumentRevisionPanel() {
    if(!documentRevisionDock_||!viewport_||foregroundProgressDepth_||viewport_->interactionLocked())return;
    DocumentState identity;identity.version=viewport_->documentVersion();identity.baseCheckinId=viewport_->documentBaseCheckinId();
    const bool historical=!identity.version.checkinId.isNull();QVector<ForgeCad::RevisionRecord> records;ForgeCad::WorkspaceRevisionStatus status;
    QString message,error;bool known=false;
    if(documentPath_.isEmpty())message=QStringLiteral("Nuovo modello: salva il documento per gestire le revisioni.");
    else{
        error=ForgeCad::workspaceDocumentHistory(documentPath_,identity,records);
        if(historical)message=QStringLiteral("Copia storica in sola lettura.");
        else{
            const auto statusError=ForgeCad::workspaceRevisionStatus(documentPath_,identity,status);known=statusError.isEmpty();
            if(!known)message=QStringLiteral("Stato non verificato: %1").arg(statusError);
            else if(status.hasPending)message=QStringLiteral("Check-in pendente: completa l'operazione.");
            else if(status.hasSession)message=QStringLiteral("Check-out: %1\n%2").arg(status.session.checkout.owner,status.phase==ForgeCad::WorkspaceCheckoutPhase::Active?QStringLiteral("Copia di lavoro prenotata"):QStringLiteral("Prenotazione da completare"));
            else message=documentEditAccess_.readOnly?documentEditAccess_.reason:QStringLiteral("Nessun check-out attivo.");
            QString next;if(ForgeCad::nextWorkspaceRevisionLabel(documentPath_,identity.version.documentId,next).isEmpty())message+=QStringLiteral("\nProssima revisione automatica: %1").arg(next);
        }
    }
    const auto base=historical?identity.version.checkinId:identity.baseCheckinId;
    for(const auto &record:records)if(record.version.checkinId==base)message+=QStringLiteral("\n%1: %2").arg(historical?QStringLiteral("Revisione"):QStringLiteral("Base"),record.technicalRevision);
    if(documentModified_)message+=QStringLiteral("\nModifiche locali da salvare.");
    documentRevisionPanelStatus_->setText(message);documentRevisionPanelStatus_->setToolTip(error);
    const auto selected=documentRevisionHistory_->currentRow()>=0&&documentRevisionHistory_->item(documentRevisionHistory_->currentRow(),0)?documentRevisionHistory_->item(documentRevisionHistory_->currentRow(),0)->data(Qt::UserRole).toUuid():QUuid();
    QSignalBlocker lock(documentRevisionHistory_);documentRevisionHistory_->setRowCount(records.size());int selection=records.size()-1;
    for(int row=0;row<records.size();++row){const auto &record=records[row];const QStringList cells{QString::number(record.sequence),record.technicalRevision,record.author,record.createdUtc.toLocalTime().toString("yyyy-MM-dd HH:mm"),record.comment};for(int col=0;col<cells.size();++col)documentRevisionHistory_->setItem(row,col,new QTableWidgetItem(cells[col]));auto *item=documentRevisionHistory_->item(row,0);item->setData(Qt::UserRole,record.version.checkinId);item->setData(Qt::UserRole+1,record.version.documentId);if(record.version.checkinId==selected)selection=row;}
    documentRevisionHistory_->resizeColumnsToContents();documentRevisionHistory_->setColumnWidth(4,200);documentRevisionHistory_->resizeRowsToContents();if(selection>=0)documentRevisionHistory_->selectRow(selection);
    documentOpenRevision_->setEnabled(selection>=0);
    const bool writableVersion=!historical&&!documentPath_.isEmpty();
    findChild<QAction *>("revisionCheckoutAction")->setEnabled(writableVersion&&known&&!status.hasPending&&(!status.hasSession||status.phase==ForgeCad::WorkspaceCheckoutPhase::Reserving||!documentLock_));
    findChild<QAction *>("revisionCheckinAction")->setEnabled(writableVersion&&known&&documentLock_&&!documentEditAccess_.readOnly&&status.hasSession&&!status.hasPending&&status.phase==ForgeCad::WorkspaceCheckoutPhase::Active);
    findChild<QAction *>("revisionCompleteAction")->setEnabled(writableVersion&&known&&status.hasPending);
    findChild<QAction *>("revisionCancelAction")->setEnabled(writableVersion&&known&&status.hasSession&&!status.hasPending);
}
void PdfWindow::openSelectedDocumentRevision() {
    if(foregroundProgressDepth_||viewport_->interactionLocked()||!documentRevisionHistory_)return;
    const int row=documentRevisionHistory_->currentRow();auto *item=row>=0?documentRevisionHistory_->item(row,0):nullptr;if(!item)return;
    const ForgeCad::DocumentVersion version{item->data(Qt::UserRole+1).toUuid(),item->data(Qt::UserRole).toUuid()};
    ForgeCad::WorkspacePaths paths;auto error=ForgeCad::locateDocumentWorkspace(documentPath_,paths);DocumentState source;
    if(error.isEmpty()){ForgeCad::RevisionStore store(paths.revisions);error=ForgeCad::readNativeRevision(store,version,source);}
    if(!error.isEmpty()){QMessageBox::warning(this,QStringLiteral("Revisioni"),error);return;}
    auto *window=new PdfWindow;window->loadingDocument_=true;window->viewport_->loadDocument(source);window->documentPath_=documentPath_;window->documentModified_=false;window->loadingDocument_=false;
    window->refreshDocumentEditAccess();window->updateWindowTitle();window->rebuildModelTree();window->setAttribute(Qt::WA_DeleteOnClose);window->show();
}

void PdfWindow::showDocumentRevisions() {
    QVector<ForgeCad::RevisionRecord> records;
    const auto error = ForgeCad::workspaceDocumentHistory(documentPath_,viewport_->currentDocument(),records);
    if (!error.isEmpty()) { QMessageBox::warning(this,QStringLiteral("Revisioni"),error); return; }
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Storico dei check-in"));
    dialog.setObjectName(QStringLiteral("documentRevisionHistoryDialog"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(int(records.size()),5,&dialog);
    table->setObjectName(QStringLiteral("documentRevisionHistoryTable"));
    table->setHorizontalHeaderLabels({QStringLiteral("Check-in"),QStringLiteral("Revisione"),QStringLiteral("Autore"),QStringLiteral("Data"),QStringLiteral("Commento")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    for (int row=0;row<records.size();++row) {
        const auto &record=records[records.size()-1-row];
        const QStringList values{QString::number(record.sequence),record.technicalRevision,record.author,
            record.createdUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),record.comment};
        for (int column=0;column<values.size();++column) table->setItem(row,column,new QTableWidgetItem(values[column]));
    }
    table->resizeColumnsToContents();table->horizontalHeader()->setStretchLastSection(true);layout->addWidget(table);
    if (records.isEmpty()) layout->addWidget(new QLabel(QStringLiteral("Nessun check-in archiviato per questo documento."),&dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);
    dialog.resize(760,360);dialog.exec();
}

void PdfWindow::closeEvent(QCloseEvent *event) {
    if(recoveryCloseReady_){documentLock_.reset();event->accept();return;}
    if(recoveryClosing_){event->ignore();return;}
    // Durante un calcolo il ciclo di eventi gira ancora (runWhileResponsive):
    // la chiusura aspetta la fine.
    if (foregroundProgressDepth_ > 0 || (viewport_ && viewport_->interactionLocked())) {
        event->ignore();
        return;
    }
    if (maybeSaveChanges()) {
        const auto attached=children();
        for (auto *child:attached) if (auto *drawing=dynamic_cast<ForgeCad::DrawingWindow *>(child)) {
            if (!drawing->close()) { event->ignore(); return; }
        }
        // La coda elimina anche una scrittura gia' in corso prima di uscire.
        // Il ciclo eventi continua a rispondere; nessun wait/join sul disco.
        recoveryClosing_=true;viewport_->setInteractionLocked(true);setEnabled(false);
        recoveryCloseRevision_=clearDocumentRecovery();event->ignore();
    }
    else event->ignore();
}

quint64 PdfWindow::clearDocumentRecovery() {
    if(!recoveredSource_.directory.isEmpty()){recovery_->discard(recoveredSource_);recoveredSource_={};}
    return recovery_->reset();
}
void PdfWindow::showRecoveryDocuments(bool automatic) {
    if(recoveryScanPending_||foregroundProgressDepth_||viewport_->interactionLocked())return;
    recoveryScanPending_=true;recoveryScanAutomatic_=automatic;recovery_->scan();
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
    if(viewport_->documentReadOnly())return;
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
    // Ricreare le voci azzera temporaneamente i range e puo' far scorrere
    // l'albero verso la selezione: conserva la posizione prima di cancellarle.
    const int verticalPosition=modelTree_->verticalScrollBar()->value();
    const int horizontalPosition=modelTree_->horizontalScrollBar()->value();
    const int previousHistoryPosition=static_cast<StoryboardTree *>(modelTree_)->historyPosition();
    rebuildingTree_ = true;
    const auto addObject = [this](const QString &name, int type, int index, int checkState, QTreeWidgetItem *parent = nullptr) {
        auto *item = parent ? new QTreeWidgetItem(parent, {name}) : new QTreeWidgetItem(modelTree_, {name});
        item->setData(0, Qt::UserRole, type);
        item->setData(0, Qt::UserRole + 1, index);
        item->setFlags(item->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
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
    // Stato aperto/chiuso delle voci, ripreso dopo la ricostruzione (chiave in UserRole + 2).
    const bool cursorSelected = StoryboardTree::isHistoryCursor(modelTree_->currentItem());
    QHash<QString, bool> expanded;
    const std::function<void(QTreeWidgetItem *)> remember = [&](QTreeWidgetItem *item) {
        const QString key = item->data(0, Qt::UserRole + 2).toString();
        if (!key.isEmpty()) expanded.insert(key, item->isExpanded());
        for (int child = 0; child < item->childCount(); ++child) remember(item->child(child));
    };
    for (int top = fixedItems; top < modelTree_->topLevelItemCount(); ++top) remember(modelTree_->topLevelItem(top));
    const auto restore = [&](QTreeWidgetItem *item, const QString &key, bool open) {
        item->setData(0, Qt::UserRole + 2, key);
        item->setExpanded(expanded.value(key, open));
    };
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
    QTreeWidgetItem *referenceRoot = nullptr;
    int referenceCount = 0;
    for (const ExtrusionObject &feature : extrusions)
        if (feature.modelBodyId == 0) ++referenceCount;
    if (referenceCount > 0) {
        referenceRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Geometria di riferimento")});
        referenceRoot->setData(0, Qt::UserRole, kTreeInfo);
        referenceRoot->setData(0, kTreeBadgeRole, QString::number(referenceCount));
        referenceRoot->setToolTip(0, QStringLiteral("%1 piani di costruzione e curve di riferimento").arg(referenceCount));
        restore(referenceRoot, QStringLiteral("R"), false);  // chiusa di default
    }
    // Storia: tutte le feature dei corpi nell'ordine di calcolo, ognuna
    // preceduta dai suoi schizzi. I corpi stanno in un gruppo a parte.
    QTreeWidgetItem *historyRoot = nullptr, *bodiesRoot = nullptr;
    const QHash<quint64, quint64> roots = viewport_->storyboardRoots();
    QHash<quint64, int> bodyIndex;
    for (int index = 0; index < modelBodies.size(); ++index) bodyIndex.insert(modelBodies.at(index).id, index);
    if (!modelBodies.isEmpty()) {
        // Corpi subito sotto la geometria di riferimento, chiusi di default.
        bodiesRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Corpi")});
        bodiesRoot->setData(0, Qt::UserRole, kTreeInfo);
        restore(bodiesRoot, QStringLiteral("C"), false);
        historyRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Storia delle feature")});
        historyRoot->setData(0, Qt::UserRole, kTreeInfo);
        historyRoot->setToolTip(0, QStringLiteral("Le feature nell'ordine in cui sono calcolate, dall'alto verso il basso"));
        restore(historyRoot, QStringLiteral("H"), true);
        // Solo i corpi che esistono nella posizione corrente: quelli consumati da
        // una booleana, una fusione o una cucitura fanno parte di un altro.
        for (int index = 0; index < modelBodies.size(); ++index) {
            const ModelBody &body = modelBodies.at(index);
            if (roots.value(body.id, body.id) != body.id) continue;
            QString label = body.name;
            const int tip = viewport_->modelBodyTip(index);
            if (viewport_->historyPosition() < extrusions.size() && tip < 0) continue;
            if (tip >= 0) {
                const QPair<int, int> parts = viewport_->bodyComponents(tip);
                if (parts.first > 1) label += QStringLiteral("  (%1 solidi)").arg(parts.first);
                if (parts.second > 1) label += QStringLiteral("  (%1 superfici)").arg(parts.second);
            }
            QTreeWidgetItem *item = addObject(label, kTreeBody, index, body.visible, bodiesRoot);
            item->setIcon(0, ForgeCad::commandIcon(QStringLiteral("box")));
            item->setToolTip(0, tip >= 0 ? QStringLiteral("Risultato di \"%1\" nella storia").arg(extrusions.at(tip).name)
                                         : QStringLiteral("Tutte le feature del corpo sono soppresse"));
            if (tip >= 0 && !extrusions.at(tip).error.isEmpty()) item->setForeground(0, QColor(255, 150, 90));
        }
        bodiesRoot->setData(0, kTreeBadgeRole, QString::number(bodiesRoot->childCount()));
        bodiesRoot->setToolTip(0, QStringLiteral("%1 corpi alla fine della storia").arg(bodiesRoot->childCount()));
    }
    for (int index = 0; index < extrusions.size(); ++index) {
        const ExtrusionObject &body = extrusions.at(index);
        QString label = body.name;
        const bool reference = body.modelBodyId == 0;
        QTreeWidgetItem *parent = reference || !historyRoot ? referenceRoot : historyRoot;
        for (int sketch : viewport_->featureSketches(index)) addSketch(sketch, parent);
        // Il corpo in cui finisce la feature (anche attraverso una booleana).
        const quint64 root = roots.value(body.modelBodyId, body.modelBodyId);
        const ModelBody owner = modelBodies.value(bodyIndex.value(root, -1));
        const bool tip = !reference && body.visible && index < viewport_->historyPosition();
        // Le feature sono passi della storia, non solidi: il numero di solidi
        // o superfici separati sta nel gruppo dei corpi.
        // Con piu' corpi si dice in quale finisce la feature.
        if (!reference && !owner.name.isEmpty() && bodiesRoot && bodiesRoot->childCount() > 1) label += QStringLiteral("   \u2192 %1").arg(owner.name);
        if (tip) label += QStringLiteral("  \u25C0 risultato");
        // Lo stadio precedente dello stesso ramo e' implicito nella storia: non si ripete tra i dettagli.
        const auto previousStage = [&](int operand) {
            return !reference && operand >= 0 && operand < extrusions.size() && extrusions.at(operand).modelBodyId == body.modelBodyId;
        };
        const QString featureKey = QStringLiteral("F%1").arg(body.featureId);
        QTreeWidgetItem *item = addObject(body.error.isEmpty() ? label : label + QStringLiteral("  \u26A0"),
                                          kTreeExtrusion, index, reference ? body.visible : -1, parent);
        bool hasPreviousStage = false;
        for (int previous = 0; previous < index; ++previous)
            if (extrusions.at(previous).modelBodyId == body.modelBodyId) { hasPreviousStage = true; break; }
        if (!reference) {
            item->setFlags(item->flags() | Qt::ItemIsDropEnabled);
            if (hasPreviousStage) item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        }
        const QString icon = featureIconName(body);
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
                QString size = ForgeCad::formatLength(body.blendSize);
                if (body.blendChamfer && body.chamferSpec.mode == 1) size += QStringLiteral(" x %1").arg(ForgeCad::formatLength(body.chamferSpec.second));
                if (body.blendChamfer && body.chamferSpec.mode == 2) size += QStringLiteral(" a %1\u00B0").arg(body.chamferSpec.second);
                item->setToolTip(0, QStringLiteral("%1 %2 su %3 spigoli")
                    .arg(body.blendChamfer ? QStringLiteral("Smusso di") : QStringLiteral("Raccordo di raggio"))
                    .arg(size).arg(body.blendEdges.size()));
            } else if (body.feature == BodyFeature::Scale) {
                static const QStringList centers = {QStringLiteral("all'origine"), QStringLiteral("al baricentro"), QStringLiteral("attorno al punto")};
                item->setToolTip(0, QStringLiteral("Scala %1 %2").arg(body.scaleFactor).arg(centers.value(body.scaleCenterMode)));
            } else if (body.feature == BodyFeature::SheetTrim) {
                const QString tool = body.secondBody >= 0 ? extrusions.value(body.secondBody).name
                    : body.sketchIndex >= 0 ? sketches.value(body.sketchIndex).name : planeNames().value(body.trimPlane);
                item->setToolTip(0, QStringLiteral("Superficie tagliata da %1%2").arg(tool, body.notice.isEmpty() ? QString() : QStringLiteral("\n") + body.notice));
            } else if (body.feature == BodyFeature::SurfaceOffset) {
                const QString operation = body.distance == 0.0 ? QStringLiteral("Copia facce")
                                                               : QStringLiteral("Offset di %1").arg(ForgeCad::formatLength(body.distance));
                item->setToolTip(0, QStringLiteral("%1 da %2 (%3)").arg(operation, extrusions.value(body.firstBody).name)
                    .arg(body.offsetFaces.isEmpty() ? QStringLiteral("tutte le facce") : QStringLiteral("%1 facce").arg(body.offsetFaces.size())));
            } else if (body.feature == BodyFeature::Draft) {
                item->setToolTip(0, QStringLiteral("Sformo di %1° su %2 facce").arg(body.draftAngle).arg(body.offsetFaces.size()));
            } else if (body.feature == BodyFeature::Thicken) {
                static const char *sides[] = {"nel verso", "nel verso opposto", "meta' per parte"};
                item->setToolTip(0, QStringLiteral("Spessore %1 %2 lungo %3").arg(ForgeCad::formatLength(body.distance), QString::fromLatin1(sides[qBound(0, body.thickenSide, 2)]),
                    body.thickenDirection.kind < 0 ? QStringLiteral("la normale") : ForgeCad::geometryRefText(body.thickenDirection, sketches, extrusions)));
            } else if (body.feature == BodyFeature::SpatialSketch) {
                item->setToolTip(0,QStringLiteral("Schizzo 3D libero: %1 entita'").arg(body.spatialSketch.entities.size()));
            } else if (body.feature == BodyFeature::ExtractedCurve) {
                item->setToolTip(0, QStringLiteral("Curva 3D associativa da %1%2").arg(
                    ForgeCad::geometryRefText(body.curveExtraction.source, sketches, extrusions),
                    body.curveExtraction.trimmed ? QStringLiteral("; taglio tramite segmento") : QString()));
            } else if (body.feature == BodyFeature::ProjectedCurve) {
                item->setToolTip(0, QStringLiteral("Proiezione di %1 su %2%3").arg(sketches.value(body.sketchIndex).name, extrusions.value(body.firstBody).name,
                                                                                    body.notice.isEmpty() ? QString() : QStringLiteral("\n") + body.notice));
            } else if (body.feature == BodyFeature::Shell) {
                item->setToolTip(0, QStringLiteral("Guscio di spessore %1, %2").arg(ForgeCad::formatLength(body.distance))
                    .arg(body.offsetFaces.isEmpty() ? QStringLiteral("chiuso") : QStringLiteral("%1 facce aperte").arg(body.offsetFaces.size())));
            } else if (body.feature == BodyFeature::Thread) {
                item->setToolTip(0, QStringLiteral("%1: passo %2, lunghezza %3%4")
                    .arg(body.thread.designation.isEmpty() ? QStringLiteral("Filettatura") : body.thread.designation,
                         ForgeCad::formatLength(body.thread.pitch), ForgeCad::formatLength(body.thread.length),
                         body.thread.leftHanded ? QStringLiteral(", sinistrorsa") : QString()));
            } else if (body.feature == BodyFeature::BoundarySurface) {
                item->setToolTip(0, QStringLiteral("Superficie tra %1 curve").arg(body.planarRefs.size()));
            } else if (body.feature == BodyFeature::FillSurface) {
                item->setToolTip(0, QStringLiteral("Riempimento di %1 curve, %2 guide, %3 facce adiacenti (%4)%5")
                    .arg(body.planarRefs.size()).arg(body.ruledFirst.size()).arg(body.ruledSecond.size())
                    .arg(QStringList{QStringLiteral("contatto"), QStringLiteral("tangenza"), QStringLiteral("curvatura")}.value(body.fillContinuity))
                    .arg(body.notice.isEmpty() ? QString() : QStringLiteral("\n") + body.notice));
            } else if (body.feature == BodyFeature::DeleteFace) {
                item->setToolTip(0, QStringLiteral("%1 facce tolte da %2").arg(body.offsetFaces.size()).arg(extrusions.value(body.firstBody).name));
            } else if (body.feature == BodyFeature::Ruled) {
                item->setToolTip(0, QStringLiteral("Superficie %1 tra %2 e %3 tratti")
                    .arg(body.loftStartContinuity || body.loftEndContinuity ? QStringLiteral("raccordata") : QStringLiteral("rigata"))
                    .arg(body.ruledFirst.size()).arg(body.ruledSecond.size()));
            } else if (body.feature == BodyFeature::PlanarSurface) {
                item->setToolTip(0, body.planarRefs.isEmpty() ? QStringLiteral("Superficie planare dello schizzo %1").arg(sketches.value(body.sketchIndex).name)
                                                              : QStringLiteral("Superficie planare da %1 bordi").arg(body.planarRefs.size()));
            } else if (body.feature == BodyFeature::Sew) {
                item->setToolTip(0, QStringLiteral("%1 superfici cucite (tolleranza %2)%3").arg(1 + body.booleanTools.size()).arg(ForgeCad::formatLength(body.sewTolerance))
                    .arg(body.solid ? QStringLiteral(": solido") : QString()));
            } else if (body.feature == BodyFeature::SheetExtend) {
                item->setToolTip(0, QStringLiteral("%1 bordi estesi di %2 (%3)").arg(body.blendEdges.size()).arg(ForgeCad::formatLength(body.blendSize))
                    .arg(body.extendLinear ? QStringLiteral("lineare") : QStringLiteral("stessa superficie")));
            } else if (body.feature == BodyFeature::Helix) {
                double pitch, turns, height;
                ForgeCad::helixDimensions(body.helix, pitch, turns, height);
                item->setToolTip(0, body.helix.spiral ? QStringLiteral("Spirale: passo %1, %2 giri").arg(ForgeCad::formatLength(pitch)).arg(turns)
                                                      : QStringLiteral("Elica%1: passo %2, %3 giri, altezza %4%5")
                                                            .arg(body.helix.taper != 0.0 ? QStringLiteral(" conica") : QString())
                                                            .arg(ForgeCad::formatLength(pitch)).arg(turns).arg(ForgeCad::formatLength(height))
                                                            .arg(body.helix.leftHanded ? QStringLiteral(", sinistrorsa") : QString()));
            } else if (body.feature == BodyFeature::Sweep) {
                static const QStringList modes = {QStringLiteral("torsione minima"), QStringLiteral("Frenet"), QStringLiteral("orientamento costante")};
                item->setToolTip(0, QStringLiteral("Sweep (%1)").arg(modes.value(body.sweepMode)));
            } else if (body.feature == BodyFeature::Loft) {
                const int guideCount = body.loftGuides.size() + body.loftGuidePaths.size() + body.loftEdgeGuides.size();
                QStringList guideModes;
                for (int index : ForgeCad::loftGuideSequence(body))
                    guideModes << QStringLiteral("G%1 (%2 %)").arg(body.loftGuideContinuities.value(index, body.loftGuideContinuity))
                        .arg(100 * body.loftGuideInfluences.value(index, body.loftGuideInfluence), 0, 'g', 3);
                item->setToolTip(0, QStringLiteral("Loft %1: %2 sezioni, %3 guide, estremità G%4/G%5, guide %6")
                                        .arg(body.loftRuled && !body.loftStartContinuity && !body.loftEndContinuity ? QStringLiteral("rigato") : QStringLiteral("liscio"))
                                        .arg(body.loftSections.isEmpty()?body.loftSketches.size():body.loftSections.size()).arg(guideCount)
                                        .arg(body.loftStartContinuity == 3 ? QStringLiteral("1 (facce)") : QString::number(body.loftStartContinuity))
                                        .arg(body.loftEndContinuity == 3 ? QStringLiteral("1 (facce)") : QString::number(body.loftEndContinuity))
                                        .arg(guideModes.join(QStringLiteral(", "))));
            } else if (body.feature == BodyFeature::Imported) {
                item->setToolTip(0, QStringLiteral("%1 importato da %2").arg(body.solid ? QStringLiteral("Solido") : QStringLiteral("Superficie"), body.importSource));
            } else if (body.feature == BodyFeature::Pattern) {
                const PatternParameters &p = body.pattern;
                QString tip = p.kind == 0 ? QStringLiteral("Ripetizione lineare: %1 x passo %2").arg(p.count).arg(ForgeCad::formatLength(p.spacing))
                              : p.kind == 1 ? QStringLiteral("Ripetizione circolare: %1 istanze, %2 %3\u00B0").arg(p.count)
                                                  .arg(p.spread ? QStringLiteral("angolo totale") : QStringLiteral("passo")).arg(p.angle)
                                            : QStringLiteral("Specchio%1").arg(p.keepOriginal || p.featureOnly ? QString() : QStringLiteral(" (solo l'immagine)"));
                if (p.kind == 0 && p.count2 > 1) tip += QStringLiteral(", %1 x passo %2 nella seconda direzione").arg(p.count2).arg(ForgeCad::formatLength(p.spacing2));
                if (p.featureOnly) tip += QStringLiteral(" (della funzione)");
                item->setToolTip(0, tip);
            } else if (body.feature == BodyFeature::DatumPlane) {
                const ForgeCad::DatumMode mode = ForgeCad::datumModes().value(body.datum.mode);
                QString tip = QStringLiteral("Piano di costruzione: %1").arg(mode.name.toLower());
                if (mode.distance) tip += QStringLiteral(", distanza %1").arg(ForgeCad::formatLength(body.datum.distance));
                if (mode.angle) tip += QStringLiteral(", angolo %1\u00B0").arg(body.datum.angle);
                item->setToolTip(0, tip);
            } else if (body.feature == BodyFeature::Primitive) {
                item->setToolTip(0, QStringLiteral("Origine (%1, %2, %3), %4")
                    .arg(ForgeCad::formatLength(body.primitive.origin[0])).arg(ForgeCad::formatLength(body.primitive.origin[1]))
                    .arg(ForgeCad::formatLength(body.primitive.origin[2]))
                    .arg(planeNames().value(body.primitive.plane)));
            }
        }
        // Estrusione fino a un riferimento, oppure estrusione/sweep fusa con altri solidi.
        if (body.operation < 0
            && ((body.feature == BodyFeature::Extrusion && (body.extent != 0 || body.extrudeSides != 0))
                || (body.feature == BodyFeature::Revolution && body.revolveAxis == kRevolveAxisReference)
                || ((body.feature == BodyFeature::Extrusion || body.feature == BodyFeature::Revolution || body.feature == BodyFeature::Sweep)
                    && body.mergeOperation != 0 && !body.mergeBodies.isEmpty()))) {
            QStringList children;
            if (body.feature == BodyFeature::Extrusion && body.extrudeSides == 1)
                children.append(QStringLiteral("Simmetrica: %1 in tutto").arg(ForgeCad::formatLength(body.distance)));
            if (body.feature == BodyFeature::Extrusion && body.extent != 0)
                children.append(QStringLiteral("Fino a: ") + ForgeCad::geometryRefText(body.extentRef, sketches, extrusions));
            if (body.feature == BodyFeature::Revolution && body.revolveAxis == kRevolveAxisReference)
                children.append(QStringLiteral("Asse: ") + ForgeCad::geometryRefText(body.revolveAxisRef, sketches, extrusions));
            if (body.feature == BodyFeature::Extrusion && body.extrudeSides == 2)
                children.append(QStringLiteral("Secondo verso: %1").arg(ForgeCad::formatLength(body.distance2)));
            if (body.mergeOperation != 0)
                for (int other : body.mergeBodies)
                    if (!previousStage(other))
                        children.append((body.mergeOperation == 1 ? QStringLiteral("Unita a: ") : QStringLiteral("Sottratta da: ")) + extrusions.value(other).name);
            for (const QString &text : children) {
                auto *child = new QTreeWidgetItem(item, {text});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            restore(item, featureKey, false);
        }
        const bool withChildren = body.feature == BodyFeature::Blend || body.feature == BodyFeature::SheetTrim || body.feature == BodyFeature::SheetExtend
                               || body.feature == BodyFeature::Scale || body.feature == BodyFeature::Helix || body.feature == BodyFeature::Sweep
                               || body.feature == BodyFeature::Loft || body.feature == BodyFeature::DatumPlane || body.feature == BodyFeature::Imported
                               || body.feature == BodyFeature::Pattern || body.feature == BodyFeature::Transform
                               || body.feature == BodyFeature::SurfaceOffset || body.feature == BodyFeature::Sew
                               || body.feature == BodyFeature::Ruled || body.feature == BodyFeature::PlanarSurface
                               || body.feature == BodyFeature::DeleteFace || body.feature == BodyFeature::BoundarySurface
                               || body.feature == BodyFeature::FillSurface
                               || body.feature == BodyFeature::Draft || body.feature == BodyFeature::Shell || body.feature == BodyFeature::Thread
                               || body.feature == BodyFeature::ProjectedCurve || body.feature == BodyFeature::Thicken || body.feature == BodyFeature::ExtractedCurve || body.feature == BodyFeature::SpatialSketch;
        if (body.operation < 0 && withChildren) {
            QStringList children;
            if (body.feature == BodyFeature::SurfaceOffset) {
                if (!previousStage(body.firstBody)) children.append(QStringLiteral("Corpo: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Distanza: %1").arg(ForgeCad::formatLength(body.distance)));
            } else if (body.feature == BodyFeature::Thicken) {
                if (!body.offsetFaces.isEmpty() || !previousStage(body.firstBody))
                    children.append(QStringLiteral("Corpo: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Spessore: %1").arg(ForgeCad::formatLength(body.distance)));
                children.append(body.offsetFaces.isEmpty() ? QStringLiteral("Facce: tutta la superficie") : QStringLiteral("Facce: %1").arg(body.offsetFaces.size()));
                children.append(QStringLiteral("Direzione: ") + (body.thickenDirection.kind < 0 ? QStringLiteral("normale della superficie")
                                                                   : ForgeCad::geometryRefText(body.thickenDirection, sketches, extrusions)));
                static const char *sides[] = {"nel verso", "nel verso opposto", "meta' per parte"};
                children.append(QStringLiteral("Lato: ") + QString::fromLatin1(sides[qBound(0, body.thickenSide, 2)]));
            } else if (body.feature == BodyFeature::SpatialSketch) {
                for(const auto &e:body.spatialSketch.entities)children.append(QStringLiteral("%1 %2: %3 punti%4").arg(e.spline?QStringLiteral("Spline"):QStringLiteral("Polilinea")).arg(e.id).arg(e.points.size()).arg(e.closed?QStringLiteral(" (chiusa)"):QString()));
            } else if (body.feature == BodyFeature::ExtractedCurve) {
                children.append(QStringLiteral("Sorgente: ") + ForgeCad::geometryRefText(body.curveExtraction.source, sketches, extrusions));
                if(!body.curveExtraction.sources.isEmpty())for(const auto &ref:body.curveExtraction.sources)children.append(QStringLiteral("Bordo unificato: ")+ForgeCad::geometryRefText(ref,sketches,extrusions));
                if (body.curveExtraction.trimmed) {
                    children.append(QStringLiteral("Segmento: ") + ForgeCad::geometryRefText(body.curveExtraction.tool, sketches, extrusions));
                    children.append(body.curveExtraction.projected ? QStringLiteral("Taglio: vista salvata") : QStringLiteral("Taglio: intersezione 3D"));
                    children.append(body.curveExtraction.keepStart ? QStringLiteral("Parte: inizio") : QStringLiteral("Parte: fine"));
                }
            } else if (body.feature == BodyFeature::ProjectedCurve) {
                children.append(QStringLiteral("Corpo: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Schizzo: ") + sketches.value(body.sketchIndex).name);
                children.append(QStringLiteral("Curve: %1").arg(body.curves.size()));
                if (body.projectionReverse) children.append(QStringLiteral("Verso: invertito"));
            } else if (body.feature == BodyFeature::Draft) {
                children.append(QStringLiteral("Angolo: %1°").arg(body.draftAngle));
                children.append(QStringLiteral("Facce: %1").arg(body.offsetFaces.size()));
                children.append(QStringLiteral("Piano neutro: ") + ForgeCad::geometryRefText(body.draftNeutral, sketches, extrusions));
                children.append(body.draftReverse ? QStringLiteral("Estrazione: verso invertito") : QStringLiteral("Estrazione: normale del piano"));
            } else if (body.feature == BodyFeature::Shell) {
                if (!previousStage(body.firstBody)) children.append(QStringLiteral("Solido: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Spessore: %1").arg(ForgeCad::formatLength(body.distance)));
                children.append(QStringLiteral("Facce aperte: %1").arg(body.offsetFaces.size()));
            } else if (body.feature == BodyFeature::Thread) {
                if (!previousStage(body.firstBody)) children.append(QStringLiteral("Solido: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Standard: ") + body.thread.designation);
                children.append(QStringLiteral("Passo: %1").arg(ForgeCad::formatLength(body.thread.pitch)));
                children.append(QStringLiteral("Lunghezza: %1").arg(ForgeCad::formatLength(body.thread.length)));
            } else if (body.feature == BodyFeature::BoundarySurface) {
                for (const GeometryRef &ref : body.planarRefs) children.append(QStringLiteral("Curva: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
            } else if (body.feature == BodyFeature::FillSurface) {
                for (const GeometryRef &ref : body.planarRefs) children.append(QStringLiteral("Contorno: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
                for (const GeometryRef &ref : body.ruledFirst) children.append(QStringLiteral("Guida: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
                for (int i = 0; i < body.ruledSecond.size(); ++i)
                    children.append(QStringLiteral("Faccia adiacente: %1 — influenza %2")
                        .arg(ForgeCad::geometryRefText(body.ruledSecond[i], sketches, extrusions))
                        .arg(body.fillFaceInfluences.value(i, body.fillInfluence), 0, 'g', 3));
            } else if (body.feature == BodyFeature::DeleteFace) {
                if (!previousStage(body.firstBody)) children.append(QStringLiteral("Corpo: ") + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Facce tolte: %1").arg(body.offsetFaces.size()));
            } else if (body.feature == BodyFeature::Ruled) {
                for (const GeometryRef &ref : body.ruledFirst) children.append(QStringLiteral("Prima curva: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
                for (const GeometryRef &ref : body.ruledSecond) children.append(QStringLiteral("Seconda curva: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
            } else if (body.feature == BodyFeature::PlanarSurface) {
                if (body.planarRefs.isEmpty()) children.append(QStringLiteral("Schizzo: ") + sketches.value(body.sketchIndex).name);
                for (const GeometryRef &ref : body.planarRefs) children.append(QStringLiteral("Bordo: ") + ForgeCad::geometryRefText(ref, sketches, extrusions));
            } else if (body.feature == BodyFeature::Sew) {
                for (int input : QVector<int>{body.firstBody} + body.booleanTools)
                    if (!previousStage(input)) children.append(QStringLiteral("Cucita con: ") + extrusions.value(input).name);
            } else if (body.feature == BodyFeature::DatumPlane) {
                for (const GeometryRef &ref : body.datum.refs) children.append(ForgeCad::geometryRefText(ref, sketches, extrusions));
            } else if (body.feature == BodyFeature::Imported) {
                children.append(QStringLiteral("File: ") + body.importSource);
            } else if (body.feature == BodyFeature::Transform) {
                const TransformParameters &m = body.move;
                if (m.copy || !previousStage(body.firstBody))
                    children.append((m.copy ? QStringLiteral("Copia di: ") : QStringLiteral("Corpo: ")) + extrusions.value(body.firstBody).name);
                children.append(QStringLiteral("Traslazione: (%1, %2, %3)")
                    .arg(ForgeCad::formatLength(m.translation[0]), ForgeCad::formatLength(m.translation[1]), ForgeCad::formatLength(m.translation[2])));
                if (std::fabs(m.angle) > 0.0)
                    children.append(QStringLiteral("Rotazione: %1\u00B0 attorno a %2").arg(m.angle).arg(ForgeCad::geometryRefText(m.axis, sketches, extrusions)));
            } else if (body.feature == BodyFeature::Pattern) {
                if (body.pattern.featureOnly || !previousStage(body.firstBody))
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
                children.append(QStringLiteral("Profilo: ") + (body.sweepProfileRef.kind >= 0
                    ? ForgeCad::geometryRefText(body.sweepProfileRef, sketches, extrusions) : sketches.value(body.sketchIndex).name));
                const int entities = body.pathSegments.size() + body.pathCurves.size();
                children.append(QStringLiteral("Percorso: ") + (body.sweepPathRef.kind >= 0
                    ? ForgeCad::geometryRefText(body.sweepPathRef, sketches, extrusions) : body.sweepPath == 0
                    ? sketches.value(body.pathSketch).name + (entities ? QStringLiteral(" (%1 entità)").arg(entities) : QString())
                    : extrusions.value(body.firstBody).name));
            } else if (body.feature == BodyFeature::Loft) {
                if(body.loftSections.isEmpty()) {
                    for (int k = 0; k < body.loftSketches.size(); ++k)
                        children.append(QStringLiteral("Sezione %1: %2").arg(k + 1).arg(sketches.value(body.loftSketches.at(k)).name));
                } else for(int k=0;k<body.loftSections.size();++k) {
                    const auto &ref=body.loftSections.at(k);
                    children.append(QStringLiteral("Sezione %1: %2").arg(k+1).arg(ref.kind==7 && ref.element.kind==-1?sketches.value(ref.index).name:ForgeCad::geometryRefText(ref,sketches,extrusions)));
                }
                const auto guideOrder = ForgeCad::loftGuideSequence(body);
                for (int k=0;k<guideOrder.size();++k) {
                    int index=guideOrder.at(k); QString text;
                    if(index<body.loftGuides.size()) text=sketches.value(body.loftGuides.at(index)).name;
                    else if((index-=body.loftGuides.size())<body.loftGuidePaths.size()) {
                        const auto &path=body.loftGuidePaths.at(index);
                        text=QStringLiteral("%1 (%2 entità)").arg(sketches.value(path.sketch).name).arg(path.segments.size()+path.curves.size());
                    } else text=ForgeCad::geometryRefText(body.loftEdgeGuides.at(index-body.loftGuidePaths.size()),sketches,extrusions);
                    children.append(QStringLiteral("Guida %1: %2 — G%3 / %4 %").arg(k+1).arg(text)
                        .arg(body.loftGuideContinuities.value(guideOrder[k], body.loftGuideContinuity))
                        .arg(100 * body.loftGuideInfluences.value(guideOrder[k], body.loftGuideInfluence), 0, 'g', 3));
                }
            } else {
                const QString role = body.feature == BodyFeature::Blend ? QStringLiteral("Base: ") : body.feature == BodyFeature::Scale ? QStringLiteral("Corpo: ")
                                                                                                                                      : QStringLiteral("Superficie: ");
                if (!previousStage(body.firstBody)) children.append(role + extrusions.value(body.firstBody).name);
            }
            if (body.feature == BodyFeature::SheetTrim) {
                const QString tool = body.secondBody >= 0 ? extrusions.value(body.secondBody).name
                    : body.sketchIndex >= 0 ? sketches.value(body.sketchIndex).name : planeNames().value(body.trimPlane);
                children.append(QStringLiteral("Strumento: ") + tool);
                if (body.secondBody < 0 && body.sketchIndex >= 0) {
                    static const char *modes[] = {"toglie le parti dentro il profilo", "tiene solo le parti dentro il profilo", "divide le facce"};
                    children.append(body.trimProject ? QStringLiteral("Proiezione sulla prima faccia: ") + QString::fromLatin1(modes[qBound(0, body.projectionMode, 2)])
                                                     : QStringLiteral("Taglio passante lungo la normale"));
                    if (body.trimProject && body.projectionReverse) children.append(QStringLiteral("Verso: invertito"));
                }
            }
            for (const QString &text : children) {
                auto *child = new QTreeWidgetItem(item, {text});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            restore(item, featureKey, false);
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
            // Nella storia il primo operando e' lo stadio precedente; restano gli
            // strumenti (i rami assorbiti, che compaiono sopra nella storia).
            QStringList operands;
            if (!previousStage(body.firstBody)) operands.append(QStringLiteral("Corpo: ") + firstName);
            for (int tool : QVector<int>{body.secondBody} + body.booleanTools)
                operands.append(QStringLiteral("Strumento: ") + extrusions.value(tool).name);
            for (const QString &operand : operands) {
                auto *child = new QTreeWidgetItem(item, {operand});
                child->setData(0, Qt::UserRole, kTreeInfo);
                child->setFlags(Qt::ItemIsEnabled);
                child->setForeground(0, QColor(140, 160, 175));
            }
            restore(item, featureKey, false);
        }
        if (selection.kind == SceneObjectKind::Extrusion && selection.index == index) modelTree_->setCurrentItem(item);
    }
    QTreeWidgetItem *sketchRoot = nullptr;
    if (!historyRoot) {
        historyRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Storia delle feature")});
        historyRoot->setData(0, Qt::UserRole, kTreeInfo);
        restore(historyRoot, QStringLiteral("H"), true);
    }
    const int position = viewport_->historyPosition();
    int cursorRow = historyRoot->childCount();
    for (int row = 0; row < historyRoot->childCount(); ++row) {
        auto *item = historyRoot->child(row);
        if (item->data(0, Qt::UserRole).toInt() != kTreeExtrusion) continue;
        if (item->data(0, Qt::UserRole + 1).toInt() >= position) {
            cursorRow = row;
            // Keep the next feature's profile together with that feature.
            while (cursorRow > 0 && historyRoot->child(cursorRow - 1)->data(0, Qt::UserRole).toInt() == kTreeSketch) --cursorRow;
            break;
        }
    }
    const bool horizontalCursor = static_cast<StoryboardTree *>(modelTree_)->cursorMode() == StoryboardTree::CursorMode::Horizontal;
    auto *cursor = new QTreeWidgetItem(QStringList{(horizontalCursor
        ? QStringLiteral("━━━━  Inserisci qui  %1 / %2  ━━━━")
        : QStringLiteral("Inserisci qui  %1 / %2")).arg(position).arg(extrusions.size())});
    cursor->setData(0, Qt::UserRole, kTreeInfo);
    cursor->setData(0, Qt::UserRole + 2, QStringLiteral("historyCursor"));
    cursor->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    cursor->setForeground(0, kStoryboardPositionColor);
    if (horizontalCursor) cursor->setBackground(0, QColor(25, 65, 85));
    QFont cursorFont = cursor->font(0);
    cursorFont.setBold(true);
    cursor->setFont(0, cursorFont);
    cursor->setToolTip(0, horizontalCursor
        ? QStringLiteral("Trascina la barra tra le feature per scegliere la posizione di calcolo e inserimento. Frecce su/giù: un passo; Home: inizio; End: fine.")
        : QStringLiteral("Trascina la maniglia laterale per scegliere la posizione di calcolo e inserimento. Frecce su/giù: un passo; Home: inizio; End: fine."));
    historyRoot->insertChild(cursorRow, cursor);
    static_cast<StoryboardTree *>(modelTree_)->setHistoryRange(position, extrusions.size());
    for (int row = cursorRow + 1; row < historyRoot->childCount(); ++row) {
        auto *item = historyRoot->child(row);
        item->setForeground(0, QColor(110, 120, 130));
        QFont font = item->font(0);
        font.setItalic(true);
        item->setFont(0, font);
    }
    for (int index = 0; index < sketches.size(); ++index) {
        if (shownSketches.contains(index)) continue;
        if (!sketchRoot) {
            sketchRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("Schizzi non utilizzati")});
            sketchRoot->setData(0, Qt::UserRole, kTreeInfo);
            restore(sketchRoot, QStringLiteral("S"), true);
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
    // Aggiorna prima il layout completo: ripristinare mentre il range e'
    // ancora vuoto troncherebbe lo scorrimento a zero.
    // Gli schizzi non utilizzati sono aggiunti dopo il cursore: ripristina
    // la sua selezione per ultima, cosi' non viene sostituita da uno schizzo.
    if(cursorSelected)modelTree_->setCurrentItem(cursor);
    modelTree_->doItemsLayout();
    if(cursorSelected && position!=previousHistoryPosition)modelTree_->scrollToItem(cursor);
    else modelTree_->verticalScrollBar()->setValue(verticalPosition);
    modelTree_->horizontalScrollBar()->setValue(horizontalPosition);
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
    if (!application) return;
    const QString sheet = dark ? QStringLiteral("QMainWindow { background: #111820; color: #d8e4ea; }QMenuBar,QToolBar,QStatusBar { background: #1b2730; color: #d8e4ea; }QMenu { background: #202d36; color: #d8e4ea; }"
        "QToolBar { spacing: 1px; padding: 2px 4px; border: none; }"
        "QToolBar::separator { background: #34444f; width: 1px; margin: 5px 3px; }"
        "QToolBar QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 2px; }"
        "QToolBar QToolButton:checked { background: #24495f; border-color: #4f9fd0; }"
        "QToolButton:enabled:hover, QToolBar QToolButton:enabled:hover { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QToolButton:enabled:pressed, QToolBar QToolButton:enabled:pressed { background: #1f4258; border-color: #80c8f0; }"
        "QToolBar QToolButton[popupMode=\"1\"] { padding-right: 11px; }"
        "QToolBar QToolButton::menu-button { border: none; width: 10px; }"
        "QToolButton::menu-button:enabled:hover { background: #5089ad; }"
        "QToolButton::menu-button:enabled:pressed { background: #1f4258; }"
        "QPushButton:enabled:hover { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QPushButton:enabled:pressed { background: #1f4258; border-color: #80c8f0; }"
        "QMenuBar::item:selected:enabled { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QMenuBar::item:pressed:enabled { background: #1f4258; border-color: #80c8f0; color: #ffffff; }"
        "QMenu::item:selected:enabled { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QMenu::item:disabled { color: #788995; }") : QStringLiteral(
        "QMainWindow { background: palette(window); color: palette(window-text); }"
        "QMenuBar,QToolBar,QStatusBar,QMenu { background: palette(window); color: palette(window-text); }"
        "QToolBar { spacing: 1px; padding: 2px 4px; border: none; }"
        "QToolBar::separator { background: #c3cbd2; width: 1px; margin: 5px 3px; }"
        "QToolBar QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; }"
        "QToolBar QToolButton:checked { background: #d6eafb; border-color: #659dca; }"
        "QToolButton:enabled:hover, QToolBar QToolButton:enabled:hover { background: #e2eff9; border-color: #659dca; }"
        "QToolButton:enabled:pressed, QToolBar QToolButton:enabled:pressed { background: #c1dcf1; border-color: #427fab; }"
        "QToolBar QToolButton[popupMode=\"1\"] { padding-right: 11px; }"
        "QToolBar QToolButton::menu-button { border: none; width: 10px; }"
        "QToolButton::menu-button:enabled:hover { background: #c1dcf1; }"
        "QToolButton::menu-button:enabled:pressed { background: #a9cee9; }"
        "QMenuBar::item:selected:enabled,QMenu::item:selected:enabled { background: #d6eafb; color: palette(window-text); }"
        "QMenuBar::item:pressed:enabled { background: #c1dcf1; }"
        "QMenu::item:disabled { color: palette(disabled, window-text); }");
    if (application->styleSheet() != sheet) application->setStyleSheet(sheet);
}
