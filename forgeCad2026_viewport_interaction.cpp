#include <QNativeGestureEvent>
#include "forgeCad2026_viewport_internal.h"
#include <QDebug>
#include <QPointingDevice>


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
    stopWheelZoom();
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

bool CadViewport::event(QEvent *event) {
    if (event->type()==QEvent::NativeGesture && wheelZoomEnabled_
        && wheelZoom_.nativeGesture(*static_cast<QNativeGestureEvent *>(event))) {
        event->accept();return true;
    }
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
    stopWheelZoom();
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
    if (wheelZoom_.wheel(*event)) { camera_.autoFit_=false;event->accept(); } else event->ignore();
}
