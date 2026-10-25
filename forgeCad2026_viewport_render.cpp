#include "forgeCad2026_viewport_internal.h"


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

void CadViewport::drawBackgroundGradient() {
    sceneRenderer_.drawBackgroundGradient(overlayRenderer_, size(), background_);
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
    if(surfacePoleU_>0&&surfacePoleV_>0&&surfacePoleU_*surfacePoleV_==surfacePoleOverlay_.size()){
        painter.save();
        painter.setPen(QPen(QColor(70,205,245,180),1,Qt::DashLine));
        for(int i=0;i<surfacePoleU_;++i)for(int j=0;j<surfacePoleV_;++j){const int k=i*surfacePoleV_+j;const auto p=surfacePoleScreen(k);if(i+1<surfacePoleU_)painter.drawLine(p,surfacePoleScreen(k+surfacePoleV_));if(j+1<surfacePoleV_)painter.drawLine(p,surfacePoleScreen(k+1));}
        for(int k=0;k<surfacePoleOverlay_.size();++k){const bool selected=surfacePoleSelection_.contains(k);painter.setPen(Qt::black);painter.setBrush(selected?QColor(255,195,50):QColor(65,220,255));painter.drawEllipse(surfacePoleScreen(k),selected?5:3,selected?5:3);}
        if (!surfacePoleSelection_.isEmpty()) {
            const auto center = surfacePoleGizmoCenter();
            const std::array<QColor,3> colors{{QColor(245,85,85),QColor(80,215,115),QColor(85,155,255)}};
            for (int axis=0; axis<3; ++axis) {
                const auto tip=surfacePoleAxisScreen(axis);
                const auto d=(tip-center)/std::hypot(tip.x()-center.x(),tip.y()-center.y());
                const QPointF n(-d.y(),d.x());
                painter.setPen(QPen(QColor(20,25,30,190),5)); painter.drawLine(center,tip);
                painter.setPen(QPen(colors[axis],2.5)); painter.drawLine(center,tip);
                painter.setBrush(colors[axis]); painter.setPen(Qt::NoPen);
                painter.drawPolygon(QPolygonF{tip,tip-11*d+5*n,tip-11*d-5*n});
                painter.setPen(colors[axis]);
                painter.drawText(tip+QPointF(6,-4),surfacePoleLocal_ ? QStringList{"U","V","N"}[axis] : QString(QChar(u'X'+axis)));
            }
            painter.setPen(QPen(Qt::white,2)); painter.setBrush(QColor(45,50,60)); painter.drawEllipse(center,6,6);
            painter.setPen(QColor(255,210,80));
            const auto label = surfacePoleSelection_.size()>1 ? QStringLiteral("%1 poli").arg(surfacePoleSelection_.size())
                : QStringLiteral("P(%1,%2)").arg(surfacePoleSelected_/surfacePoleV_+1).arg(surfacePoleSelected_%surfacePoleV_+1);
            painter.drawText(center+QPointF(10,20),label);
        }
        painter.restore();
    }
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
                         QStringLiteral("L = %1").arg(ForgeCad::formatLength(length, 4, true, this)));
        if (index < sketch.segmentAngles.size() && sketch.segmentAngles.at(index) >= 0.0) {
            painter.drawText(labelPosition + QPointF(4.0, 10.0),
                             QStringLiteral("A = %1 deg")
                                 .arg(sketch.segmentAngles.at(index), 0, 'f', 4));
        }
    }
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
    std::array<ForgeCad::Gui::SceneRenderer::ReferencePlane, 3> planes;
    for (int plane = 0; plane < 3; ++plane) {
        auto &display = planes[plane];
        display.visible = isPlaneShown(plane);
        if (!display.visible) continue;
        display.corners = planeCorners(plane);
        if (selectedPlane_ == plane) display.emphasis += 1.0f;
        if (hover_ == SceneSelection{SceneObjectKind::Plane, plane, -1}) display.emphasis += 0.8f;
    }
    sceneRenderer_.drawReferencePlanes(overlayRenderer_, planes);
}

void CadViewport::drawDatumQuad(const QVector<QVector3D> &corners, const GLfloat *color, float emphasis) {
    sceneRenderer_.drawDatumQuad(overlayRenderer_, corners, color, emphasis);
}

void CadViewport::drawDatumPlanes() {
    sceneRenderer_.drawPlanePass([this] {
        static const GLfloat datumColor[] = {0.72f, 0.52f, 0.98f}, previewColor[] = {0.98f, 0.66f, 0.28f};
        for (int index = 0; index < extrusions_.size(); ++index) {
            if (!isDatumShown(index)) continue;
            const ExtrusionObject &body = extrusions_.at(index);
            const SceneSelection self{SceneObjectKind::Extrusion, index, -1};
            const float emphasis = (selection_ == self ? 1.0f : 0.0f) + (hover_ == self ? 0.8f : 0.0f);
            drawDatumQuad(datumCorners(body.datumFrame, body.datum.size), datumColor, emphasis);
        }
        if (datumPreviewValid_) drawDatumQuad(datumCorners(datumPreviewFrame_, datumPreviewSize_), previewColor, 1.0f);
    });
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
            if(curve.tool==DrawingTool::Point && curve.controlPoints.size()==1) {
                QVector<QVector3D> marker;const auto p=curve.controlPoints[0];
                for(const QPointF &arm:{QPointF(markerSize*0.6,0),QPointF(0,markerSize*0.6)})
                    marker<<mapSketchPoint(p-arm,sketch)<<mapSketchPoint(p+arm,sketch);
                overlayRenderer_.draw(GL_LINES,marker,color);continue;
            }
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

void CadViewport::drawExtrusionOutline(const ExtrusionObject &extrusion, const QColor &color, float width) {
    displayCache_.outline(extrusion.display, ForgeCad::isCurveBody(extrusion), color, width);
}

void CadViewport::drawFeatureHighlightFaces(int index) {
    if (index == bodyHighlight_)
        sceneRenderer_.drawHighlightFaces(displayCache_, extrusions_.at(index).display, true, featureHighlightColor_);
    for (const FeatureHighlight &highlight : featureHighlights_)
        if (highlight.body == index && !highlight.display.vertices.isEmpty())
            sceneRenderer_.drawHighlightFaces(displayCache_, highlight.display, false, featureHighlightColor_);
}

void CadViewport::drawFeatureHighlightEdges(int index) {
    const QColor edge = featureHighlightColor_.lighter(120);
    if (index == bodyHighlight_)
        sceneRenderer_.drawHighlightEdges(displayCache_, extrusions_.at(index).display, true, edge);
    for (const FeatureHighlight &highlight : featureHighlights_)
        if (highlight.body == index && !highlight.display.edges.isEmpty())
            sceneRenderer_.drawHighlightEdges(displayCache_, highlight.display, false, edge);
}

void CadViewport::drawSelectedFace() {
    if (edgePicking_ || sketchMode_ || selectedFace_.body < 0 || selectedFace_.body >= extrusions_.size()) return;
    sceneRenderer_.drawSelectedFace(overlayRenderer_, extrusions_.at(selectedFace_.body).display,
                                     faceDisplayEdges(selectedFace_.body, selectedFace_.hit), kSelectionColor);
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
    sceneRenderer_.drawTopologyHighlight(overlayRenderer_, display, indices, color, float(devicePixelRatioF()));
}

void CadViewport::drawPickedEdges() {
    drawSelectedFace();
    if (!edgePicking_) return;
    const BodyDisplay *body = edgePickBody_ >= 0 && edgePickBody_ < extrusions_.size()
        ? &extrusions_.at(edgePickBody_).display : nullptr;
    const BodyDisplay *hoverBody = hoverEdgeBody_ >= 0 && hoverEdgeBody_ < extrusions_.size()
        ? &extrusions_.at(hoverEdgeBody_).display : nullptr;
    ForgeCad::Gui::SceneRenderer::EdgePickStyle style;
    style.hiddenEdgesVisible = hiddenEdgesVisible_;
    style.hoveredSelected = hoverEdgeBody_ == edgePickBody_ && pickedEdges_.contains(hoverEdge_);
    style.hoverEdge = hoverEdge_;
    style.hoverColor = kHoverColor;
    style.selectionColor = kSelectionColor;
    style.devicePixelRatio = float(devicePixelRatioF());
    sceneRenderer_.drawPickedEdges(overlayRenderer_, body, hoverBody, hoverFaceEdges_, pickedEdges_, style);
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
    // Preparare prima di prendere riferimenti: una cache mancante puo' causare
    // il detach del QVector condiviso con le istantanee Undo.
    for (int index=0;index<extrusions_.size();++index)
        if (isBodyDrawn(index) || index==edgePickBody_ || pickBodies_.contains(index)
            || preview_.replaced.contains(index) || preview_.restored.contains(index)
            || (!preview_.key.isEmpty() && index==preview_.definition.firstBody)) ensureBodyDisplay(index);
    glDisable(GL_CULL_FACE);
    overlayRenderer_.setStipple(false);
    enableSectionClip();
    QVector<int> sectionSolids;
    // I comandi di feature possono partire dallo schizzo: l'editing resta
    // attivo, ma SketchViewUnlock permette la vista 3D della lavorazione.
    const bool previewing = (!sketchMode_ || sketchViewUnlocked_) && !preview_.key.isEmpty();
    QVector<int> drawn;
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
        if ((sketchMode_ && !isBodyDrawn(index)) || (!isBodyDrawn(index) && !forced)
            || (extrusion.display.vertices.isEmpty() && extrusion.display.edges.isEmpty())) continue;
        drawn.append(index);
    }
    // Un solo strato trasparente per pixel: prima trova la superficie piu
    // vicina fra tutti i corpi, poi applica l'opacita soltanto su quella.
    if(sketchMode_ && sketchBodyOpacity_<0.999 && displayMode_!=0) {
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1.0f,2.0f);
        for(int index:drawn)displayCache_.displayFaces(extrusions_.at(index).display);
        glDisable(GL_POLYGON_OFFSET_FILL);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }
    for(int index:drawn) {
        const auto &extrusion=extrusions_.at(index);
        const SceneSelection self{SceneObjectKind::Extrusion, index, -1};
        const bool hovered = hover_ == self;
        if (displayMode_ != 0 || index == shapeAnalysisBody_) {
            ForgeCad::Gui::SceneRenderer::BodyFaceStyle style;
            style.color = meshColorForFeature(index);
            style.hoverColor = kHoverColor;
            style.hovered = hovered;
            style.seeThrough = sketchMode_ && sketchBodyOpacity_ < 0.999 && displayMode_ != 0;
            style.opacity = float(sketchBodyOpacity_);
            style.analyzing = index == shapeAnalysisBody_;
            style.faceOnly = style.analyzing && shapeAnalysisFace_ >= 0;
            style.analysisMode = shapeAnalysisMode_;
            style.zebraFrequency = zebraFrequency_;
            style.zebraAngle = zebraAngle_;
            sceneRenderer_.drawBodyFaces(displayCache_, overlayRenderer_, extrusion.display, style,
                                         shapeAnalysisFaceDisplay_, shapeAnalysis_.triangles,
                                         [this, index] { drawFeatureHighlightFaces(index); });
        }
        if (section_.enabled && extrusion.solid && displayMode_ != 0 && !extrusion.display.vertices.isEmpty()) sectionSolids.append(index);
        ForgeCad::Gui::SceneRenderer::BodyEdgeStyle edgeStyle;
        edgeStyle.curve = ForgeCad::isCurveBody(extrusion);
        edgeStyle.spatial = extrusion.feature == BodyFeature::SpatialSketch || extrusion.feature == BodyFeature::ExtractedCurve;
        edgeStyle.hovered = hovered;
        edgeStyle.hoverColor = kHoverColor;
        edgeStyle.displayMode = displayMode_;
        edgeStyle.hiddenEdgesVisible = hiddenEdgesVisible_;
        edgeStyle.thread = extrusion.feature == BodyFeature::Thread;
        edgeStyle.devicePixelRatio = float(devicePixelRatioF());
        sceneRenderer_.drawBodyEdges(displayCache_, overlayRenderer_, extrusion.display, edgeStyle);
        if (!ForgeCad::isCurveBody(extrusion) && index != shapeAnalysisBody_) drawFeatureHighlightEdges(index);
    }
    if (shapeAnalysisMode_ >= 0 && (!sketchMode_ || isBodyDrawn(shapeAnalysisBody_))) {
        const bool through = shapeAnalysisThrough_ && (shapeAnalysisMode_ == 4 || shapeAnalysisMode_ == 5);
        sceneRenderer_.drawAnalysisLines(overlayRenderer_, shapeAnalysis_.grid, shapeAnalysis_.lines, through);
        for (const auto &border : shapeAnalysis_.borders)
            overlayRenderer_.drawWideLineStrip(border.points, border.color, 4.0f);
    }
    const auto outline = [this](const SceneSelection &target, const QColor &color, float width) {
        if (target.kind != SceneObjectKind::Extrusion || target.index < 0
            || target.index >= extrusions_.size()) return;
        const ExtrusionObject &extrusion = extrusions_.at(target.index);
        if (!isBodyDrawn(target.index) || (extrusion.display.vertices.isEmpty() && extrusion.display.edges.isEmpty())) return;
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



void CadViewport::initializeGlassShader() {
    glassCompositor_.initialize();
}







void CadViewport::drawGpuGlassPanels(const QSize &pixels, QOpenGLFramebufferObject *resolvedScene) {
    glassCompositor_.draw(pixels, size(), devicePixelRatioF(), defaultFramebufferObject(), overlayRenderer_, resolvedScene);
}

void CadViewport::drawAxes() {
    if (!axesVisible_) return;
    const float length = float(axisDisplayLength());
    const std::array<QColor, 3> colors{axisColor(0), axisColor(1), axisColor(2)};
    sceneRenderer_.drawAxes(overlayRenderer_, length, colors);
}

void CadViewport::drawGrid() {
    if (!gridVisible_) return;
    const float step = niceCeiling(qMax(0.25f * planeHalf(), 1e-4f));
    sceneRenderer_.drawGrid(overlayRenderer_, camera_.orientation_, step);
}
