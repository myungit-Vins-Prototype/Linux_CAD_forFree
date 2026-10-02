// CadViewport e' ancora definito nel .cpp della finestra. Questa unita' di
// test lo include per verificare interazioni e rendering senza esportare API di test.
#include "../forgeCad2026_gui.cpp"
#include "fk_helix.h"
#include "fk_primitives.h"
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
class ViewportInteractionTest {
public:
    static void run(bool render) {
        using namespace ForgeCad;
        CadViewport v;
        // La fusione parametrica e' condivisa da estrusione e sweep: il probe
        // conserva il bersaglio realmente intersecato e produce un solo solido.
        ExtrusionObject mergeTarget;
        mergeTarget.forgeBody = std::make_shared<const Kernel::Body>(Kernel::makeBox(
            Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 3.0, 3.0, 3.0));
        mergeTarget.solid = true;
        ExtrusionObject sweepMerge;
        sweepMerge.feature = BodyFeature::Sweep;
        sweepMerge.mergeOperation = 1;
        sweepMerge.mergeProbe = true;
        sweepMerge.mergeBodies = {0};
        const ForgeBody sweepTool = std::make_shared<const Kernel::Body>(Kernel::makeBox(
            Kernel::Frame3(Kernel::Vec3(2, 1, 1), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 2.0, 1.0, 1.0));
        QString sweepMergeError;
        const ForgeBody sweepUnited = forgeMergeFeatureResult(sweepMerge, sweepTool, 1, {mergeTarget}, &sweepMergeError);
        require(sweepUnited && sweepMergeError.isEmpty() && sweepMerge.mergeBodies == QVector<int>{0}
                    && sweepUnited->shells().size() == 1,
                "unione automatica del risultato sweep");
        require(CadViewport::hiddenOperands(sweepMerge).contains(0), "bersaglio della sweep unita nascosto");
        CadViewport storyboard;
        PrimitiveParameters box;
        box.size[0] = 4.0;
        box.size[1] = 3.0;
        box.size[2] = 2.0;
        require(storyboard.createPrimitive(box, QStringLiteral("Blocco")).isEmpty(), "radice della storyboard");
        require(storyboard.modelBodies().size() == 1 && storyboard.extrusions().at(0).featureId != 0
                    && storyboard.extrusions().at(0).modelBodyId == storyboard.modelBodies().at(0).id,
                "identita' persistenti di corpo e feature");
        require(storyboard.createScale(0, 1.1, 0, {}, QStringLiteral("Scala 1")).isEmpty()
                    && storyboard.createScale(1, 1.1, 0, {}, QStringLiteral("Scala 2")).isEmpty(),
                "catena di feature nello stesso corpo logico");
        require(storyboard.modelBodies().size() == 1 && !storyboard.extrusions().at(0).visible
                    && !storyboard.extrusions().at(1).visible && storyboard.extrusions().at(2).visible,
                "solo il tip della storyboard e' visibile");
        const quint64 lastFeature = storyboard.extrusions().at(2).featureId;
        require(storyboard.moveFeature(2, -1).isEmpty() && storyboard.extrusions().at(1).featureId == lastFeature,
                "riordino di due feature compatibili");
        require(storyboard.setFeatureSuppressed(2, true).isEmpty() && storyboard.extrusions().at(2).suppressed
                    && storyboard.extrusions().at(1).visible,
                "soppressione ripristina lo stadio precedente");
        require(storyboard.setFeatureSuppressed(2, false).isEmpty() && storyboard.extrusions().at(2).visible,
                "riattivazione della feature");
        storyboard.deleteFeature(2);
        require(storyboard.extrusions().size() == 2 && storyboard.extrusions().at(1).visible
                    && storyboard.modelBodies().at(0).tipFeatureId == storyboard.extrusions().at(1).featureId,
                "eliminazione del tip ripristina la feature precedente");
        storyboard.undo();
        require(storyboard.extrusions().size() == 3 && storyboard.extrusions().at(2).visible,
                "undo dell'eliminazione nella storyboard");
        // Il proprietario viene ritrovato dall'ID della feature e la
        // sotto-entita' dal suo ID B-rep, anche se indice e punto-cache sono
        // volutamente fuorvianti.
        const int referencedFeature = 1;
        const ForgeBody referencedBody = storyboard.extrusions().at(referencedFeature).forgeBody;
        require(referencedBody && referencedBody->edges().size() > 1, "corpo per i riferimenti topologici");
        const Kernel::EdgeId persistentEdge = referencedBody->edges().front();
        const Kernel::Edge &edgeGeometry = referencedBody->edge(persistentEdge);
        const Kernel::Vec3 edgePoint = edgeGeometry.curve->point(0.5 * (edgeGeometry.range.lo + edgeGeometry.range.hi));
        EdgePoint persistentPoint = edgeReference(*referencedBody, persistentEdge, edgePoint);
        const Kernel::Edge &otherGeometry = referencedBody->edge(referencedBody->edges().back());
        const Kernel::Vec3 wrongPoint = otherGeometry.curve->point(0.5 * (otherGeometry.range.lo + otherGeometry.range.hi));
        persistentPoint.x = wrongPoint.x(); persistentPoint.y = wrongPoint.y(); persistentPoint.z = wrongPoint.z();
        require(resolveEdgeReference(*referencedBody, persistentPoint, 1e-12) == persistentEdge,
                "ID topologico preferito al punto piu' vicino");
        EdgePoint renumberedPoint = edgeReference(*referencedBody, persistentEdge, edgePoint);
        renumberedPoint.subshape = std::numeric_limits<int>::max();
        require(resolveEdgeReference(*referencedBody, renumberedPoint, 1e-12) == persistentEdge,
                "firma geometrico-topologica recupera uno spigolo rinumerato");
        const Kernel::FaceId persistentFace = referencedBody->faces().front();
        const EdgePoint facePoint = faceReference(*referencedBody, persistentFace, edgePoint);
        require(resolveFaceReference(*referencedBody, facePoint, 1e-12) == persistentFace,
                "riferimento persistente a una faccia");
        GeometryRef persistentRef;
        persistentRef.kind = 4;
        persistentRef.index = 0; // indice volutamente errato
        persistentRef.featureId = storyboard.extrusions().at(referencedFeature).featureId;
        persistentRef.point = persistentPoint;
        ResolvedRef resolvedPersistent;
        require(resolveGeometryRef(persistentRef, storyboard.extrusions().size(), {}, storyboard.extrusions(), resolvedPersistent, nullptr)
                    && resolvedPersistent.hasCurve,
                "proprietario del riferimento ritrovato dall'ID della feature");
        GeometryRef deletedOwner = persistentRef;
        deletedOwner.featureId = std::numeric_limits<quint64>::max();
        require(!resolveGeometryRef(deletedOwner, storyboard.extrusions().size(), {}, storyboard.extrusions(), resolvedPersistent, nullptr),
                "un proprietario eliminato non si lega per errore all'indice riutilizzato");
        GeometryRef legacyRef = persistentRef;
        legacyRef.point = {edgePoint.x(), edgePoint.y(), edgePoint.z()};
        storyboard.extrusions_[referencedFeature].extentRef = legacyRef;
        storyboard.extrusions_[referencedFeature].blendEdges = {persistentPoint};
        QTemporaryDir storyboardDir;
        const QString storyboardPath = storyboardDir.filePath(QStringLiteral("storyboard.prt"));
        require(storyboardDir.isValid() && saveDocumentFile(storyboardPath, storyboard.currentDocument(), false).isEmpty(),
                "salvataggio della storyboard");
        DocumentState storyboardLoaded;
        require(loadDocumentFile(storyboardPath, storyboardLoaded).isEmpty() && storyboardLoaded.modelBodies.size() == 1
                    && storyboardLoaded.extrusions.at(1).featureId == lastFeature,
                "persistenza e lettura della storyboard");
        require(storyboardLoaded.extrusions.at(referencedFeature).extentRef.featureId == persistentRef.featureId
                    && storyboardLoaded.extrusions.at(referencedFeature).extentRef.point.subshape == persistentEdge.index
                    && storyboardLoaded.extrusions.at(referencedFeature).blendEdges.first().context != -1,
                "migrazione e persistenza dei riferimenti topologici nel formato 20");
        CadViewport booleanStory;
        PrimitiveParameters firstBox, secondBox;
        firstBox.size[0] = firstBox.size[1] = firstBox.size[2] = 2.0;
        secondBox = firstBox;
        secondBox.origin[0] = 1.0;
        require(booleanStory.createPrimitive(firstBox, QStringLiteral("A")).isEmpty()
                    && booleanStory.createPrimitive(secondBox, QStringLiteral("B")).isEmpty()
                    && booleanStory.createBoolean(BooleanOperation::Union, 0, {1}, QStringLiteral("Unione")).isEmpty(),
                "booleana nella storyboard");
        require(booleanStory.modelBodies().size() == 2 && booleanStory.extrusions().at(2).modelBodyId == booleanStory.extrusions().at(0).modelBodyId
                    && !booleanStory.extrusions().at(1).visible && booleanStory.extrusions().at(2).visible,
                "la booleana appartiene al corpo A e consuma il corpo strumento");
        require(booleanStory.setFeatureSuppressed(2, true).isEmpty() && booleanStory.extrusions().at(0).visible
                    && booleanStory.extrusions().at(1).visible,
                "sopprimere la booleana ripristina entrambi gli operandi");
        require(booleanStory.setFeatureSuppressed(2, false).isEmpty() && !booleanStory.extrusions().at(0).visible
                    && !booleanStory.extrusions().at(1).visible && booleanStory.extrusions().at(2).visible,
                "riattivare la booleana nasconde di nuovo gli operandi");
        CadViewport interleavedStory;
        require(interleavedStory.createPrimitive(firstBox, QStringLiteral("Corpo A")).isEmpty()
                    && interleavedStory.createPrimitive(secondBox, QStringLiteral("Corpo B")).isEmpty()
                    && interleavedStory.createScale(0, 1.1, 0, {}, QStringLiteral("A1")).isEmpty()
                    && interleavedStory.createScale(1, 1.1, 0, {}, QStringLiteral("B1")).isEmpty()
                    && interleavedStory.createScale(2, 1.1, 0, {}, QStringLiteral("A2")).isEmpty(),
                "storyboard con feature di corpi intercalati");
        const quint64 a2Id = interleavedStory.extrusions().at(4).featureId;
        const quint64 b1Id = interleavedStory.extrusions().at(3).featureId;
        require(interleavedStory.moveFeatureTo(4, 2).isEmpty()
                    && interleavedStory.extrusions().at(2).featureId == a2Id
                    && interleavedStory.extrusions().at(4).featureId == b1Id,
                "drag logico tra feature con altri corpi intercalati");
        ForgeCad::HistoryGraphDialog historyGraph;
        historyGraph.setDocument(interleavedStory.currentDocument());
        require(historyGraph.nodeCount() >= interleavedStory.extrusions().size()
                    && historyGraph.edgeCount() == 3 && historyGraph.invalidDependencyCount() == 0,
                "grafo diagnostico della storyboard");
        const double graphZoom = historyGraph.zoomFactor();
        historyGraph.zoomBy(1.25);
        require(historyGraph.zoomFactor() > graphZoom,
                "zoom interattivo del grafo diagnostico");
        historyGraph.zoomBy(0.001);
        require(historyGraph.zoomFactor() >= 0.719,
                "limite leggibile dello zoom del grafo diagnostico");
        historyGraph.show();
        QApplication::processEvents();
        QGraphicsView *graphView = historyGraph.findChild<QGraphicsView *>();
        QGraphicsItem *movableNode = nullptr;
        if (graphView)
            for (QGraphicsItem *item : graphView->scene()->items())
                if ((item->flags() & QGraphicsItem::ItemIsMovable) && item->data(0).toInt() == 1) {
                    movableNode = item;
                    break;
                }
        require(movableNode, "blocchi spostabili nel grafo diagnostico");
        const QPointF movedPosition = movableNode->pos() + QPointF(57.0, 31.0);
        movableNode->setPos(movedPosition);
        QApplication::processEvents();
        require(movableNode->pos() == movedPosition,
                "spostamento di un blocco con aggiornamento dei collegamenti");
        historyGraph.setDocument(interleavedStory.currentDocument());
        QApplication::processEvents();
        bool preservedPosition = false;
        for (QGraphicsItem *item : graphView->scene()->items())
            preservedPosition = preservedPosition
                || ((item->flags() & QGraphicsItem::ItemIsMovable) && item->pos() == movedPosition);
        require(preservedPosition, "posizione manuale conservata dopo la ricostruzione del grafo");
        DocumentState invalidGraph = interleavedStory.currentDocument();
        invalidGraph.extrusions[4].firstBody = 99;
        historyGraph.setDocument(invalidGraph);
        require(historyGraph.invalidDependencyCount() == 1,
                "dipendenza mancante evidenziata dal grafo diagnostico");
        const auto renderedAlpha = [](QWidget &widget) {
            widget.resize(500, 300);
            QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            widget.render(&painter);
            return image.pixelColor(widget.rect().center()).alpha();
        };
        FunctionDialogPanel translucentPanel;
        translucentPanel.setPanelOpacity(25);
        translucentPanel.setBackdropBlur(18);
        const int lowOpacity = renderedAlpha(translucentPanel);
        translucentPanel.setPanelOpacity(100);
        require(lowOpacity > 0 && renderedAlpha(translucentPanel) > lowOpacity,
                "opacita' regolabile dei pannelli funzione");
        require(translucentPanel.backdropBlur() == 18, "sfocatura della scena regolabile sui pannelli funzione");
        translucentPanel.setPanelOpacity(70);
        translucentPanel.show();
        QApplication::processEvents();
        translucentPanel.hide();
        FunctionDialogPanel scrollablePanel;
        scrollablePanel.createScrollableForm()->addRow(new FeatureOperationDiagram(FeatureOperationDiagram::Extrusion, &scrollablePanel));
        require(scrollablePanel.findChild<QScrollArea *>(QStringLiteral("functionDialogScroll")),
                "contenuto scorrevole dei pannelli funzione");
        FunctionDialogPanel embeddedPanel(&v);
        embeddedPanel.setWindowTitle(QStringLiteral("Pannello test"));
        require(embeddedPanel.isEmbedded() && !embeddedPanel.isWindow() && embeddedPanel.parentWidget() == &v,
                "pannello funzione sovrapposto nelle coordinate del viewport");
        v.setGlassPanel(123, QRect(10, 20, 300, 220), 16, 14, true);
        require(v.glassPanels_.contains(123) && v.glassPanels_.value(123).blur == 16
                    && v.glassPanels_.value(123).radius == 14,
                "registrazione del pannello per la composizione GPU");
        v.removeGlassPanel(123);
        require(!v.glassPanels_.contains(123), "rimozione del pannello dalla composizione GPU");
        embeddedPanel.resize(420, 300);
        embeddedPanel.placeAtLeft();
        const QPoint initialPanelPosition = embeddedPanel.pos();
        const QPoint dragStart = embeddedPanel.mapToGlobal(QPoint(20, 20));
        embeddedPanel.beginEmbeddedMove(dragStart);
        embeddedPanel.updateEmbeddedMove(dragStart + QPoint(40, 30));
        embeddedPanel.endEmbeddedMove();
        require(embeddedPanel.pos() == initialPanelPosition + QPoint(40, 30),
                "trascinamento del vetro aggiorna il ritaglio della scena");
        embeddedPanel.setCornerRadius(18);
        require(embeddedPanel.cornerRadius() == 18 && !embeddedPanel.mask().contains(QPoint(0, 0))
                    && embeddedPanel.mask().contains(embeddedPanel.rect().center()),
                "angoli arrotondati configurabili dei pannelli funzione");
        auto *panelGrip = embeddedPanel.findChild<QSizeGrip *>(QStringLiteral("functionPanelResizeGrip"));
        require(panelGrip && !embeddedPanel.isSizeGripEnabled(),
                "grip interno senza ridimensionamento nativo della finestra niri");
        const QSize viewportBeforeGrip = v.size();
        const QSize panelBeforeGrip = embeddedPanel.size();
        const QPointF gripLocal(10, 10);
        const QPointF gripGlobal(panelGrip->mapToGlobal(gripLocal.toPoint()));
        QMouseEvent gripPress(QEvent::MouseButtonPress, gripLocal, gripGlobal,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent gripMove(QEvent::MouseMove, gripLocal + QPointF(30, 20), gripGlobal + QPointF(30, 20),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent gripRelease(QEvent::MouseButtonRelease, gripLocal + QPointF(30, 20), gripGlobal + QPointF(30, 20),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(panelGrip, &gripPress);
        QApplication::sendEvent(panelGrip, &gripMove);
        QApplication::sendEvent(panelGrip, &gripRelease);
        require(embeddedPanel.size() == panelBeforeGrip + QSize(30, 20) && v.size() == viewportBeforeGrip,
                "il grip ridimensiona soltanto il pannello funzione");
        require(QSettings().value(QStringLiteral("view/functionPanelSizes/Pannello_test")).toSize() == embeddedPanel.size(),
                "ridimensionamento manuale del pannello memorizzato");
        const QPalette panelPalette = embeddedPanel.palette();
        require(std::abs(panelPalette.color(QPalette::WindowText).lightness() - embeddedPanel.panelColor().lightness()) > 100,
                "contrasto del testo sul vetro");
        QComboBox panelCombo(&embeddedPanel);
        panelCombo.addItems({QStringLiteral("Prima voce"), QStringLiteral("Seconda voce")});
        panelCombo.ensurePolished();
        panelCombo.view()->ensurePolished();
        require(std::abs(panelCombo.palette().color(QPalette::Text).lightness()
                         - embeddedPanel.panelColor().lighter(112).lightness()) > 100
                    && std::abs(panelCombo.view()->palette().color(QPalette::Text).lightness()
                                - embeddedPanel.panelColor().darker(125).lightness()) > 100,
                "testo leggibile nel campo e nella tendina dei pannelli funzione");
        const QColor selectionText = panelPalette.color(QPalette::HighlightedText);
        const QColor selectionBackground = panelPalette.color(QPalette::Highlight);
        require(selectionBackground.red() > 230 && selectionBackground.green() > 120
                    && selectionText.lightness() < 80,
                "testo leggibile nella selezione dei menu a discesa");
        require(!ForgeCad::commandIcon(QStringLiteral("panelOpacity")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("panelBlur")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("panelCorners")).isNull(),
                "icone delle impostazioni dei pannelli");
        FloatingPanel constraintStylePanel(&v, QStringLiteral("Vincoli"),
                                           QStringLiteral("test/constraintPanelStyle"), QSize(380, 360));
        require(constraintStylePanel.isEmbedded()
                    && constraintStylePanel.objectName() == QStringLiteral("functionDialogPanel")
                    && constraintStylePanel.findChild<QSizeGrip *>(QStringLiteral("functionPanelResizeGrip")),
                "pannello Vincoli con lo stesso vetro e grip dei pannelli funzione");
        LoftSelectionDiagram loftDiagram;
        require(renderedAlpha(loftDiagram) == 255, "grafica Loft sempre opaca");
        FeatureOperationDiagram extrusionDiagram(FeatureOperationDiagram::Extrusion);
        FeatureOperationDiagram revolutionDiagram(FeatureOperationDiagram::Revolution);
        require(renderedAlpha(extrusionDiagram) == 255 && renderedAlpha(revolutionDiagram) == 255,
                "grafiche Estrusione e Rivoluzione sempre opache");
        v.resize(900, 650);
        v.setViewNormal(0);
        v.zoom_ = 20;
        const double ratio = v.axisDisplayLength() / v.zoom_;
        v.zoom_ *= 3;
        require(std::fabs(v.axisDisplayLength() / v.zoom_ - ratio) < 1e-12, "dimensione assi durante zoom");
        v.zoom_ = 20;
        v.yaw_ = 0.0f;
        v.pitch_ = 0.0f;
        v.orbitView(QPoint(360, 360));
        require(std::fabs(v.yaw_) == 180.0f && std::fabs(v.pitch_) == 180.0f,
                "orbita orizzontale e verticale oltre i vecchi limiti");
        v.orbitView(QPoint(360, 360));
        require(std::fabs(v.yaw_) < 1e-6f && std::fabs(v.pitch_) < 1e-6f,
                "giro completo di 360 gradi sui due assi della vista");
        v.setViewNormal(0);
        v.selectPlane(0);
        const auto dragPlane = [&](bool cancel) {
            const auto corners = v.selection_.kind == SceneObjectKind::Plane ? v.planeCorners(0)
                : v.datumCorners(v.extrusions_.at(v.selection_.index).datumFrame, v.extrusions_.at(v.selection_.index).datum.size);
            const QPoint corner = v.projectWorldPoint(corners.at(2)).toPoint();
            require(v.beginPlaneResize(corner), "maniglia del piano");
            const QPoint end = corner + (corner - v.projectWorldPoint((corners.at(0) + corners.at(2)) * 0.5f).toPoint()) / 2;
            QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            v.mouseMoveEvent(&move);
            v.finishPlaneResize(cancel);
        };
        const auto initial = v.referencePlaneHalf(0);
        dragPlane(true);
        require(v.referencePlaneHalf(0) == initial, "Esc ripristina la dimensione automatica");
        require(v.referencePlaneHalf(0) == v.referencePlaneHalf(1)
                && v.referencePlaneHalf(1) == v.referencePlaneHalf(2),
                "Esc mantiene uguali i tre piani standard");
        dragPlane(false);
        require(v.referencePlaneHalf(0) > initial * 1.4, "ridimensionamento piano standard");
        require(v.referencePlaneHalf(0) == v.referencePlaneHalf(1)
                && v.referencePlaneHalf(1) == v.referencePlaneHalf(2),
                "il ridimensionamento manuale aggiorna tutti i piani standard");
        ExtrusionObject datum;
        datum.name = QStringLiteral("Piano test");
        datum.feature = BodyFeature::DatumPlane;
        GeometryRef ref;
        ref.kind = 1; ref.index = 0;
        datum.datum.refs = {ref}; datum.datum.distance = 2; datum.datum.size = 3;
        require(v.createBody(datum).isEmpty(), "creazione datum");
        const int datumIndex = int(v.extrusions_.size()) - 1;
        v.selectObject(SceneObjectKind::Extrusion, datumIndex);
        dragPlane(false);
        const double resized = v.extrusions_.at(datumIndex).datum.size;
        require(resized > 4.2, "ridimensionamento datum");
        v.undo();
        require(v.extrusions_.at(datumIndex).datum.size == 3, "Undo dimensione datum");
        v.redo();
        require(v.extrusions_.at(datumIndex).datum.size == resized, "Redo dimensione datum");

        // Una curva 3D autonoma (elica/spirale) offre sia la curva sia i suoi
        // estremi come riferimenti grafici del piano normale.
        CadViewport curveReferences;
        curveReferences.resize(800, 600);
        ExtrusionObject pathBody;
        pathBody.name = QStringLiteral("Curva percorso");
        pathBody.feature = BodyFeature::Helix;
        const auto pathLine = std::make_shared<Kernel::Line<3>>(Kernel::Vec3(2, 3, 1), normalized(Kernel::Vec3(1, 0.2, 0.4)));
        pathBody.curve = std::make_shared<Kernel::TrimmedCurve<3>>(pathLine, 0.0, 5.0);
        curveDisplay(*pathBody.curve, 1, pathBody.display);
        curveReferences.extrusions_.append(pathBody);
        curveReferences.refPickOwner_ = -1;
        GeometryRef pickedCurve, pickedEnd;
        const Kernel::Vec3 middle3 = pathBody.curve->point(2.5), end3 = pathBody.curve->point(5.0);
        curveReferences.refPickRoles_ = DatumRoleCurve;
        require(curveReferences.pickReference(curveReferences.projectWorldPoint(QVector3D(middle3.x(), middle3.y(), middle3.z())).toPoint(), pickedCurve)
                    && pickedCurve.kind == 9,
                "curva 3D selezionabile come riferimento del piano normale");
        curveReferences.refPickRoles_ = DatumRolePoint;
        require(curveReferences.pickReference(curveReferences.projectWorldPoint(QVector3D(end3.x(), end3.y(), end3.z())).toPoint(), pickedEnd)
                    && pickedEnd.kind == 10 && pickedEnd.element.point == 1,
                "estremo finale della curva 3D selezionabile graficamente");
        ResolvedRef resolvedEnd;
        require(resolveGeometryRef(pickedEnd, 1, {}, curveReferences.extrusions_, resolvedEnd, nullptr)
                    && resolvedEnd.hasPoint && distance(resolvedEnd.point, end3) < 1e-12,
                "estremo della curva risolto sul dominio esatto");
        DatumParameters normalAtEnd;
        normalAtEnd.mode = 2;
        normalAtEnd.refs = {pickedCurve, pickedEnd};
        SketchFrame normalFrame;
        QString normalError;
        require(computeDatum(normalAtEnd, 1, {}, curveReferences.extrusions_, normalFrame, &normalError),
                "piano normale costruito sull'estremo scelto della curva 3D");
        require(distance(Kernel::Vec3(normalFrame.origin[0], normalFrame.origin[1], normalFrame.origin[2]), end3) < 1e-9,
                "origine del piano normale coincidente con l'estremo della curva");

        // La conversione di un'elica crea un riferimento con molti poli. Il
        // vincolo Fix deve essere analizzato direttamente, senza una matrice
        // densa quadrata che rendeva l'operazione apparentemente infinita.
        Kernel::HelixSpec helixSpec;
        helixSpec.frame = Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0));
        helixSpec.radius = 5.0;
        helixSpec.pitch = 2.0;
        helixSpec.turns = 3.0;
        const auto helixCurve = std::make_shared<Kernel::HelixCurve>(helixSpec);
        SketchObject projectedHelix;
        projectedHelix.plane = 0;
        require(appendProjectedCurve(projectedHelix, helixCurve, helixCurve->domain(), true, true).isEmpty(),
                "conversione rapida di una elica nello schizzo");
        require(projectedHelix.curves.size() == 1 && projectedHelix.curves.first().tool == DrawingTool::Converted,
                "elica convertita come B-spline di riferimento");
        const SketchAnalysis projectedAnalysis = analyzeSketch(projectedHelix);
        require(projectedAnalysis.fullyDefined() && projectedAnalysis.curveDefined.value(0),
                "riferimento convertito fissato senza analisi cubica dei poli");

        Kernel::HelixSpec longHelixSpec = helixSpec;
        longHelixSpec.turns = 1000.0;
        const Kernel::HelixCurve longHelix(longHelixSpec);
        BodyDisplay boundedDisplay;
        curveDisplay(longHelix, 2, boundedDisplay);
        require(boundedDisplay.edges.size() == 1 && boundedDisplay.edges.first().size() == 32769,
                "campionamento grafico delle eliche lunghe limitato");

        const int sketch = v.createSketch(0, QStringLiteral("Test riferimenti"));
        require(sketch >= 0, "creazione schizzo");
        v.selectSketch(sketch);
        v.selectSketchReference(2, 0);
        require(v.sketches_.at(sketch).segments.size() == 1, "riferimento asse X");
        require(v.constraintSelection().size() == 1, "asse scelto per i vincoli");
        v.selectPlane(1);
        require(v.sketches_.at(sketch).segments.size() == 1, "riuso di una retta coincidente");
        v.selectSketchReference(2, 1);
        require(v.sketches_.at(sketch).segments.size() == 2, "riferimento asse Y");
        require(analyzeSketch(v.sketches_.at(sketch)).fullyDefined(), "riferimenti fissi senza liberta' residue");
        v.selectSketchReference(2, 2);
        v.selectPlane(0);
        require(v.sketches_.at(sketch).segments.size() == 2, "riferimenti degeneri non modificano lo schizzo");
        v.undo();
        require(v.sketches_.at(sketch).segments.size() == 1, "Undo riferimento");
        v.redo();
        require(v.sketches_.at(sketch).segments.size() == 2, "Redo riferimento");

        SketchObject work = v.sketches_.at(sketch);
        const int line = int(work.segments.size());
        work.segments.append({QPointF(1, 2), QPointF(4, 3)});
        work.constraints.append(-1); work.segmentLengths.append(0); work.segmentAngles.append(-1);
        const auto fixed = work.segments.at(0);
        work.geometricConstraints.append(makeConstraint(work, ConstraintType::Parallel, {{0, line, -1}, {0, 0, -1}}));
        require(solveSketch(work).ok, "vincolo parallelo all'asse");
        require(work.segments.at(0) == fixed, "il riferimento resta fisso");
        require(std::fabs(work.segments.at(line).second.y() - work.segments.at(line).first.y()) < 1e-8, "segmento parallelo al riferimento");
        SketchFrame tilted;
        tilted.origin[0] = 2; tilted.normal[0] = std::sqrt(0.5); tilted.normal[2] = std::sqrt(0.5);
        QPointF point, direction;
        require(sketchPlaneReference(work, tilted, point, direction).isEmpty(), "traccia di un piano obliquo");
        const auto world = sketchToWorld(point, work);
        require(std::fabs((world.x() - 2) + world.z()) < 1e-10, "traccia sul piano esatto");
        require(sketchPlaneReference(work, SketchFrame(), point, direction).size() > 0, "piano coincidente rifiutato");

        SketchObject contactTarget;
        contactTarget.name = QStringLiteral("Sezione loft");
        contactTarget.plane = 0;
        SketchObject crossing;
        crossing.name = QStringLiteral("Guida loft");
        crossing.plane = kFacePlane;
        crossing.customFrame = true;
        crossing.frame.normal[0] = 0.0; crossing.frame.normal[1] = 1.0; crossing.frame.normal[2] = 0.0;
        crossing.frame.xAxis[0] = 1.0; crossing.frame.xAxis[1] = 0.0; crossing.frame.xAxis[2] = 0.0;
        crossing.segments.append({QPointF(0, -1), QPointF(0, 1)});
        crossing.constraints.append(-1); crossing.segmentLengths.append(0); crossing.segmentAngles.append(-1);
        require(appendSketchContactReferences(contactTarget, crossing).isEmpty(), "contatto tra schizzi");
        require(contactTarget.segments.size() == 1 && contactTarget.segments.at(0).first == contactTarget.segments.at(0).second,
                "punto di attraversamento come riferimento");
        SketchObject contactLineTarget;
        contactLineTarget.plane = 0;
        SketchObject touching = crossing;
        touching.segments[0] = {QPointF(-1, 0), QPointF(1, 0)};
        require(appendSketchContactReferences(contactLineTarget, touching).isEmpty(), "segmento comune tra piani di schizzo");
        require(contactLineTarget.segments.size() == 1
                && pointDistance(contactLineTarget.segments.at(0).first, contactLineTarget.segments.at(0).second) > 1.0,
                "segmento di contatto come costruzione");
        SketchObject oneContact;
        oneContact.plane = 0;
        touching.segments.append({QPointF(-2, 0), QPointF(2, 0)});
        touching.constraints.append(-1); touching.segmentLengths.append(0); touching.segmentAngles.append(-1);
        require(appendSketchContactReference(oneContact, touching, {0, 1}).isEmpty(), "singolo bordo di schizzo come riferimento");
        require(oneContact.segments.size() == 1, "la scelta grafica non importa tutto lo schizzo");
        contactTarget.segments.append({QPointF(0.4, 0.3), QPointF(1, 0)});
        contactTarget.constraints.append(-1); contactTarget.segmentLengths.append(0); contactTarget.segmentAngles.append(-1);
        contactTarget.geometricConstraints.append(makeConstraint(contactTarget, ConstraintType::Coincident, {{0, 1, 0}, {0, 0, 0}}));
        require(solveSketch(contactTarget).ok, "coincidenza con contatto esterno");
        require(pointDistance(contactTarget.segments.at(1).first, QPointF()) < 1e-8, "nodo sul contatto esterno");
        contactTarget.segments.append({QPointF(-1, 0.5), QPointF(1, 0.5)});
        contactTarget.constraints.append(-1); contactTarget.segmentLengths.append(0); contactTarget.segmentAngles.append(-1);
        contactTarget.geometricConstraints.append(makeConstraint(contactTarget, ConstraintType::PointOnCurve, {{0, 0, 0}, {0, 2, -1}}));
        require(solveSketch(contactTarget).ok, "contatto esterno su segmento");
        require(std::fabs(contactTarget.segments.at(2).first.y()) < 1e-8 && std::fabs(contactTarget.segments.at(2).second.y()) < 1e-8,
                "segmento passa per il contatto esterno");
        CurveObject contactCircle;
        contactCircle.tool = DrawingTool::Circle;
        contactCircle.controlPoints = {QPointF(2, 0), QPointF(3, 0)};
        recalculateCurve(contactCircle);
        contactTarget.curves.append(contactCircle);
        contactTarget.geometricConstraints.append(makeConstraint(contactTarget, ConstraintType::PointOnCurve, {{0, 0, 0}, {1, 0, -1}}));
        require(solveSketch(contactTarget).ok, "contatto esterno su curva");
        require(std::fabs(pointDistance(contactTarget.curves.at(0).controlPoints.at(0), QPointF())
                          - pointDistance(contactTarget.curves.at(0).controlPoints.at(0), contactTarget.curves.at(0).controlPoints.at(1))) < 1e-8,
                "curva passa per il contatto esterno");

        CurveObject linkedSpline;
        linkedSpline.tool = DrawingTool::Spline;
        linkedSpline.controlPoints = {QPointF(0, 0), QPointF(2, 1), QPointF(4, 0)};
        initializeTangentHandles(linkedSpline);
        linkedSpline.tangentLinked[1] = true;
        work.curves.append(linkedSpline);
        SketchObject handleSketch;
        handleSketch.curves.append(linkedSpline);
        SketchConstraint handleLength = makeConstraint(handleSketch, ConstraintType::Distance,
            {{1, 0, 1}, {1, 0, handlePoint(1, 1)}});
        handleLength.value *= 1.4;
        handleSketch.geometricConstraints.append(handleLength);
        require(solveSketch(handleSketch).ok, "quota della maniglia collegata");
        const CurveObject &solvedSpline = handleSketch.curves.first();
        const QPointF hin = solvedSpline.tangentHandles.at(1).first - solvedSpline.controlPoints.at(1);
        const QPointF hout = solvedSpline.tangentHandles.at(1).second - solvedSpline.controlPoints.at(1);
        require(std::fabs(hin.x() * hout.y() - hin.y() * hout.x()) < 1e-8, "tangenza delle maniglie collegate");

        SketchObject multiplePaths;
        multiplePaths.segments = {{QPointF(0, 0), QPointF(0, 3)}, {QPointF(8, 0), QPointF(8, 3)}};
        multiplePaths.constraints = {-1, -1}; multiplePaths.segmentLengths = {0, 0}; multiplePaths.segmentAngles = {-1, -1};
        QVector<SketchPathRef> components = sketchPathComponents(multiplePaths);
        require(components.size() == 2, "percorsi disconnessi nello stesso schizzo");
        std::vector<Kernel::PathSegment> partialPath;
        require(sketchPath(sketchPathSubset(multiplePaths, components.at(1)), partialPath, nullptr) && partialPath.size() == 1,
                "selezione parziale usabile come percorso");

        QVector<SketchObject> loftSections;
        for (int i = 0; i < 3; ++i) {
            SketchObject section;
            section.name = QStringLiteral("Loft UV %1").arg(i + 1);
            section.plane = kFacePlane;
            section.customFrame = true;
            section.frame.origin[2] = 2.0 * i;
            CurveObject circle;
            circle.tool = DrawingTool::Circle;
            circle.controlPoints = {QPointF(), QPointF(i == 1 ? 1.0 : 2.0, 0.0)};
            recalculateCurve(circle);
            section.curves.append(circle);
            loftSections.append(section);
        }
        QString loftError;
        const ForgeBody loftBody = forgeLoft(loftSections, false, &loftError);
        require(loftBody && loftError.isEmpty(), "loft per griglia UV");
        SketchObject mirroredGuides;
        mirroredGuides.name = QStringLiteral("Guide specchiate");
        mirroredGuides.plane = kFacePlane; mirroredGuides.customFrame = true;
        mirroredGuides.frame.normal[0] = 0; mirroredGuides.frame.normal[1] = 1; mirroredGuides.frame.normal[2] = 0;
        mirroredGuides.frame.xAxis[0] = 1; mirroredGuides.frame.xAxis[1] = 0; mirroredGuides.frame.xAxis[2] = 0;
        for (double sign : {-1.0, 1.0}) {
            CurveObject guide; guide.tool = DrawingTool::Spline;
            guide.controlPoints = {QPointF(2 * sign, 0), QPointF(sign, -2), QPointF(2 * sign, -4)};
            initializeTangentHandles(guide); recalculateCurve(guide); mirroredGuides.curves.append(guide);
        }
        QVector<SketchPathRef> guideComponents = sketchPathComponents(mirroredGuides);
        require(guideComponents.size() == 2, "guide specchiate selezionabili separatamente");
        const ForgeBody partialGuidedLoft = forgeLoft(loftSections,
            {sketchPathSubset(mirroredGuides, guideComponents.first())}, false, 0, 0, 1, 1.0, 1.0, 1.0, &loftError);
        require(partialGuidedLoft && loftError.isEmpty(), "loft con una sola guida scelta dallo schizzo specchiato");
        SketchObject missedGuide = sketchPathSubset(mirroredGuides, guideComponents.first());
        missedGuide.name = QStringLiteral("Guida lontana");
        for (CurveObject &curve : missedGuide.curves) {
            for (QPointF &point : curve.controlPoints) point.rx() += 20.0;
            initializeTangentHandles(curve); recalculateCurve(curve);
        }
        require(!forgeLoft(loftSections, {missedGuide}, false, 0, 0, 1, 1.0, 1.0, 1.0, &loftError),
                "guida che non incontra la sezione rifiutata");
        require(loftError.contains(loftSections.first().name) && loftError.contains(QStringLiteral("[[loft-section=0]]")),
                "errore loft con nome e indice della sezione mancata");
        v.preview_.definition.loftSketches = {sketch};
        const QString visibleLoftError = v.preparePreviewError(loftError);
        require(v.previewErrorSketch_ == sketch && !visibleLoftError.contains(QStringLiteral("[[loft-section=")),
                "sezione dell'errore evidenziabile nella vista");
        BodyDisplay loftDisplay;
        forgeTessellate(*loftBody, 0, loftDisplay);
        forgeSurfaceConstructionCurves(*loftBody, loftDisplay, 4);
        require(!loftDisplay.constructionCurves.isEmpty(), "curve UV dell'anteprima loft");

        DocumentState state = v.documentState();
        state.sketches[sketch] = work;
        state.extrusions[datumIndex].loftGuides = {sketch};
        state.extrusions[datumIndex].loftStartContinuity = 1;
        state.extrusions[datumIndex].loftEndContinuity = 2;
        state.extrusions[datumIndex].loftGuideInfluence = 0.65;
        state.extrusions[datumIndex].loftStartInfluence = 0.4;
        state.extrusions[datumIndex].loftEndInfluence = 0.8;
        state.extrusions[datumIndex].loftGuideContinuity = 1;
        state.extrusions[datumIndex].pathSketch = sketch;
        state.extrusions[datumIndex].pathSegments = {1};
        SketchPathRef savedGuide{sketch, {1}, {}};
        state.extrusions[datumIndex].loftGuidePaths = {savedGuide};
        QTemporaryDir tmp;
        require(tmp.isValid(), "directory temporanea");
        const QString path = tmp.filePath(QStringLiteral("refs.prt"));
        require(saveDocumentFile(path, state, false).isEmpty(), "salvataggio riferimenti e datum");
        DocumentState loaded;
        require(loadDocumentFile(path, loaded).isEmpty(), "lettura riferimenti e datum");
        CadViewport filePreview;
        filePreview.loadPreviewDocument(loaded);
        require(!filePreview.sketches_.isEmpty() && !filePreview.sceneGeometryPoints().isEmpty(),
                "schizzi visibili nell'anteprima della finestra Apri");
        require(loaded.extrusions.at(datumIndex).datum.size == resized, "dimensione datum nel documento");
        require(loaded.sketches.at(sketch).curves.last().tangentLinked.value(1), "collegamento maniglie nel documento");
        require(loaded.extrusions.at(datumIndex).pathSegments == QVector<int>{1}
                && loaded.extrusions.at(datumIndex).loftGuidePaths == QVector<SketchPathRef>{savedGuide},
                "percorsi parziali nel documento");
        const ExtrusionObject &roundTrip = loaded.extrusions.at(datumIndex);
        require(roundTrip.loftGuides == QVector<int>{sketch} && roundTrip.loftStartContinuity == 1 && roundTrip.loftEndContinuity == 2,
                "parametri loft nel documento");
        require(roundTrip.loftGuideInfluence == 0.65 && roundTrip.loftStartInfluence == 0.4 && roundTrip.loftEndInfluence == 0.8,
                "influenze loft nel documento");
        require(roundTrip.loftGuideContinuity == 1, "tangenza delle guide nel documento");
        require(solveSketch(loaded.sketches[sketch]).ok, "vincoli dopo riapertura");

        v.endSketchMode();
        PrimitiveParameters primitive;
        require(v.createPrimitive(primitive, QStringLiteral("Box rendering")).isEmpty(), "primitiva per rendering");
        const int body = int(v.extrusions_.size()) - 1;
        const auto &display = v.extrusions_.at(body).display;
        require(display.rayIndex && !display.faceEdges.isEmpty(), "indici di selezione preparati");
        for (const auto &edges : display.faceEdges)
            for (int e : edges) require(e >= 0 && e < display.edges.size(), "corrispondenza faccia-spigoli");
        v.fitAll();
        const auto &projection = v.projectedBodyEdges(body);
        require(projection.lines.size() == display.edges.size(), "cache proiezione spigoli");
        const auto oldPoint = projection.lines.first().first();
        v.panX_ += 1;
        require(v.projectedBodyEdges(body).lines.first().first() != oldPoint, "invalidazione cache sul pan");

        // La dimensione maggiore del corpo determina un unico quadrato comune:
        // i tre piani devono restare uguali anche per un corpo molto sottile.
        CadViewport thin;
        for (double &scale : thin.referencePlaneScales_) scale = 1.0;
        PrimitiveParameters plate;
        plate.size[0] = 100.0;
        plate.size[1] = 5.0;
        plate.size[2] = 2.0;
        require(thin.createPrimitive(plate, QStringLiteral("Piastra sottile")).isEmpty(), "primitiva sottile");
        const QSizeF xy = thin.referencePlaneExtents(0);
        const QSizeF xz = thin.referencePlaneExtents(1);
        const QSizeF yz = thin.referencePlaneExtents(2);
        require(xy.width() == xy.height(), "il piano XY e' quadrato");
        require(xy == xz && xz == yz, "i tre piani hanno la stessa dimensione");
        require(xy.width() >= 60.0, "la misura comune segue la dimensione maggiore del corpo");

        // Una seconda lavorazione piu' grande, adiacente a una gia' presente,
        // viene rigenerata dalla base comune in ordine di misura. Copre tutte
        // le combinazioni raccordo/smusso sulla zona d'intersezione.
        for (bool firstChamfer : {false, true})
            for (bool secondChamfer : {false, true}) {
                CadViewport sequential;
                PrimitiveParameters block;
                block.size[0] = 4.0; block.size[1] = 3.0; block.size[2] = 2.0;
                require(sequential.createPrimitive(block, QStringLiteral("Base raccordi consecutivi")).isEmpty(),
                        "base dei raccordi consecutivi");
                const QVector<EdgePoint> vertical{{4.0, 0.0, 1.0}};
                require(sequential.createBlend(0, vertical, 0.3, firstChamfer, QStringLiteral("Prima lavorazione")).isEmpty(),
                        "prima lavorazione piccola");
                const QVector<EdgePoint> topFront{{2.0, 0.0, 2.0}};
                require(sequential.createBlend(1, topFront, 0.55, secondChamfer, QStringLiteral("Seconda lavorazione")).isEmpty(),
                        "seconda lavorazione grande sulla zona gia' raccordata");
                require(sequential.extrusions_.back().forgeBody != nullptr, "rigenerazione delle lavorazioni intersecanti");
            }

        if (render) {
            v.show();
            for (int i = 0; i < 8; ++i) QApplication::processEvents();
            require(v.isValid(), "contesto OpenGL valido");
            DocumentFilePreview openPreview;
            openPreview.resize(360, 420);
            openPreview.setPath(path);
            auto *fileProgress = openPreview.findChild<QProgressBar *>(QStringLiteral("documentPreviewProgress"));
            require(fileProgress && !fileProgress->isHidden(), "barra durante il caricamento dell'anteprima file");
            openPreview.show();
            QEventLoop previewWait;
            QTimer::singleShot(500, &previewWait, &QEventLoop::quit);
            previewWait.exec();
            auto *previewViewport = dynamic_cast<CadViewport *>(
                openPreview.findChild<QOpenGLWidget *>(QStringLiteral("documentPreviewViewport")));
            auto *previewDetails = openPreview.findChild<QLabel *>(QStringLiteral("documentPreviewDetails"));
            require(previewViewport && !previewViewport->sketches_.isEmpty()
                        && previewDetails && previewDetails->text().contains(QStringLiteral("schizzi")),
                    "caricamento asincrono del riquadro nella finestra Apri");
            require(fileProgress->isHidden(), "barra nascosta al termine dell'anteprima file");
            openPreview.grab().save(QStringLiteral("/tmp/forgecad-open-preview.png"));
            openPreview.hide();
            PdfWindow progressWindow;
            progressWindow.show();
            QApplication::processEvents();
            require(progressWindow.openDocumentPath(path), "apertura del documento con avanzamento determinato");
            auto *openProgress = progressWindow.findChild<QProgressDialog *>(QStringLiteral("foregroundProgressDialog"));
            require(openProgress && openProgress->maximum() == 100 && openProgress->value() == 100 && openProgress->isHidden(),
                    "finestra di apertura arrivata al 100% e chiusa al termine");
            progressWindow.close();
            FunctionDialogPanel focusPanel(&v);
            focusPanel.setWindowTitle(QStringLiteral("Test focus pannello"));
            auto *focusSpin = new QDoubleSpinBox(&focusPanel);
            focusPanel.createScrollableForm()->addRow(QStringLiteral("Valore:"), focusSpin);
            focusPanel.show();
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            require(focusSpin->hasFocus(), "focus iniziale sul primo dato del pannello");
            require(focusPanel.size().width() <= v.width() - 32 && focusPanel.size().height() <= v.height() - 32,
                    "dimensione automatica limitata al viewport");
            focusPanel.hide();
            require(v.gpuGlassAvailable(), "shader OpenGL per la sfocatura dei pannelli");
            v.setGlassPanel(456, QRect(30, 30, 260, 180), 14, 18, true);
            v.update();
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            const QImage screenshot = v.grabFramebuffer();
            require(!screenshot.isNull(), "rendering framebuffer");
            v.removeGlassPanel(456);
            screenshot.save(QStringLiteral("/tmp/forgecad-viewport-test.png"));
            v.makeCurrent();
            while (glGetError() != GL_NO_ERROR) {}
            v.displayCache_.faces(display);
            v.displayCache_.edges(display);
            require(glGetError() == GL_NO_ERROR, "rendering VBO senza errori GL");
            {
                QOpenGLFramebufferObject framebuffer(160, 160, QOpenGLFramebufferObject::CombinedDepthStencil);
                require(framebuffer.isValid(), "framebuffer confronto VBO");
                const auto draw = [&](bool cached) {
                    framebuffer.bind();
                    glViewport(0, 0, 160, 160);
                    glClearColor(0, 0, 0, 1);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glDisable(GL_LIGHTING); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
                    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-6, 6, -6, 6, -100, 100);
                    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glRotatef(25, 1, 0, 0); glRotatef(30, 0, 1, 0);
                    glColor3f(0.8f, 0.5f, 0.2f);
                    if (cached) v.displayCache_.faces(display);
                    else {
                        glBegin(GL_TRIANGLES);
                        for (const auto &p : display.vertices) glVertex3f(p.x(), p.y(), p.z());
                        glEnd();
                    }
                    glColor3f(0.2f, 0.8f, 0.9f);
                    if (cached) v.displayCache_.edges(display);
                    else for (const auto &edge : display.edges) {
                        glBegin(GL_LINE_STRIP);
                        for (const auto &p : edge) glVertex3f(p.x(), p.y(), p.z());
                        glEnd();
                    }
                    glFinish();
                    return framebuffer.toImage();
                };
                const QImage legacy = draw(false), cached = draw(true);
                require(legacy == cached, "VBO e pipeline immediata producono gli stessi pixel");
                bool colored = false;
                for (int y = 0; y < cached.height(); ++y)
                    for (int x = 0; x < cached.width(); ++x) colored |= (cached.pixel(x, y) & 0xffffff) != 0;
                require(colored, "confronto rendering non vuoto");
                framebuffer.release();
            }
            v.displayCache_.clear();
            v.doneCurrent();
            v.close();
        }
        // L'anteprima del raccordo conserva il B-rep esatto e la
        // tassellazione: OK deve promuoverli senza eseguire di nuovo il kernel.
        {
            CadViewport cachedBlend;
            int foregroundStarts = 0, foregroundEnds = 0, backgroundStarts = 0, backgroundEnds = 0;
            cachedBlend.setWorkCallback([&](bool begin, const QString &, bool background) {
                if (background) (begin ? backgroundStarts : backgroundEnds)++;
                else (begin ? foregroundStarts : foregroundEnds)++;
            });
            PrimitiveParameters cachedBlock;
            cachedBlock.size[0] = 4.0; cachedBlock.size[1] = 3.0; cachedBlock.size[2] = 2.0;
            require(cachedBlend.createPrimitive(cachedBlock, QStringLiteral("Base anteprima raccordo")).isEmpty(),
                    "base dell'anteprima raccordo");
            require(foregroundStarts > 0 && foregroundStarts == foregroundEnds,
                    "notifiche bilanciate per il calcolo sul thread principale");
            CadViewport loadedWithProgress;
            int loadedBodies = -1, totalBodies = -1;
            loadedWithProgress.loadDocument(cachedBlend.currentDocument(), [&](int completed, int total, const QString &) {
                loadedBodies = completed;
                totalBodies = total;
            });
            require(loadedBodies == totalBodies && totalBodies == 1,
                    "avanzamento determinato fino all'ultimo corpo durante l'apertura");

            // Il riquadro Apri puo' disegnare una cache di una build precedente,
            // ma il documento normale continua a rifiutarla e rigenera il B-rep.
            QTemporaryDir staleCacheDir;
            const QString staleCachePath = staleCacheDir.filePath(QStringLiteral("cache-precedente.prt"));
            require(saveDocumentFile(staleCachePath, cachedBlend.currentDocument(), true).isEmpty(),
                    "salvataggio del documento con cache 3D");
            QFile staleFile(staleCachePath);
            require(staleFile.open(QIODevice::ReadOnly), "lettura del documento con cache 3D");
            QByteArray staleData = staleFile.readAll();
            staleFile.close();
            QDataStream staleHeader(staleData);
            staleHeader.skipRawData(4);
            quint16 staleVersion = 0;
            quint8 staleCompression = 0;
            quint32 payloadSize = 0;
            staleHeader >> staleVersion >> staleCompression >> payloadSize;
            const qsizetype cacheOffset = 11 + qsizetype(payloadSize);
            QByteArray staleCache = qUncompress(staleData.mid(cacheOffset));
            require(staleVersion >= 14 && staleCompression == 1 && staleCache.size() > 68,
                    "blocco cache 3D presente nel documento");
            staleCache[4] = staleCache.at(4) == '0' ? '1' : '0'; // altera soltanto l'impronta dei sorgenti
            staleData.replace(cacheOffset, staleData.size() - cacheOffset, qCompress(staleCache, 6));
            require(staleFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
                        && staleFile.write(staleData) == staleData.size(),
                    "scrittura della cache con impronta precedente");
            staleFile.close();
            DocumentState strictCache, previewCache;
            require(loadDocumentFile(staleCachePath, strictCache).isEmpty()
                        && loadDocumentFile(staleCachePath, previewCache, true).isEmpty(),
                    "lettura della cache precedente nei due modi");
            require(!strictCache.extrusions.first().forgeBody && previewCache.extrusions.first().forgeBody,
                    "cache precedente disponibile solo per il riquadro di anteprima");
            const QVector<EdgePoint> cachedEdge{{2.0, 0.0, 2.0}};
            cachedBlend.requestBlendPreview(0, cachedEdge, 0.25, false);
            cachedBlend.startPreviewJob();
            QElapsedTimer previewTimeout;
            previewTimeout.start();
            while (cachedBlend.previewRunning_ && previewTimeout.elapsed() < 10000) QApplication::processEvents();
            require(backgroundStarts == 1 && backgroundEnds == 1,
                    "notifiche bilanciate per il calcolo dell'anteprima in background");
            require(cachedBlend.preview_.valid && cachedBlend.preview_.geometry,
                    "B-rep esatto conservato dall'anteprima raccordo");
            require(!cachedBlend.preview_.display.vertices.isEmpty(),
                    "patch locale presente nell'anteprima raccordo");
            require(cachedBlend.preview_.display.vertices.size() < cachedBlend.preview_.resultDisplay.vertices.size(),
                    "l'anteprima mostra solo la patch e non l'intero corpo");
            require(!cachedBlend.preview_.display.constructionCurves.isEmpty(),
                    "curve U/V presenti nell'anteprima raccordo");
            require(cachedBlend.preview_.replaced.isEmpty(),
                    "la base opaca non viene sostituita dalla patch del raccordo");
            const ForgeBody previewGeometry = cachedBlend.preview_.geometry;
            const int resultTriangles = cachedBlend.preview_.resultDisplay.vertices.size();
            require(cachedBlend.createBlend(0, cachedEdge, 0.25, false, QStringLiteral("Raccordo da anteprima")).isEmpty(),
                    "conferma dell'anteprima raccordo");
            require(cachedBlend.extrusions_.back().forgeBody == previewGeometry,
                    "la conferma riusa il B-rep dell'anteprima");
            require(cachedBlend.extrusions_.back().display.vertices.size() == resultTriangles,
                    "la conferma riusa la tassellazione completa conservata con l'anteprima");
        }
        std::cout << "Viewport: assi, piani, datum, Undo/Redo, riferimenti, vincoli, salvataggio e cache OK" << std::endl;
    }
};
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL); format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile); format.setDepthBufferSize(24); format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForgeCADTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Viewport"));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    try { ViewportInteractionTest::run(app.arguments().contains(QStringLiteral("--gl"))); }
    catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
}
