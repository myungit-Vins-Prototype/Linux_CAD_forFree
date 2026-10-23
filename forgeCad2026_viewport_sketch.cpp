#include "forgeCad2026_viewport_internal.h"


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
        if (!isBodyDrawn(b, false) || !(ForgeCad::isShapeBody(body) || (ForgeCad::isCurveBody(body) && body.curve))) continue;
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
        if (s == activeSketch_ || !isSketchDrawn(s)) continue;
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
    for (int index=0;index<extrusions_.size();++index) {
        const auto &body=extrusions_.at(index);
        if (!isBodyDrawn(index, false) || !ForgeCad::isShapeBody(body)) continue;
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
        if (index == activeSketch_ || !isSketchDrawn(index)) continue;
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
    if (!face.planar || body < 0 || body >= historyPosition() || body >= extrusions_.size()) return -1;
    if (sketchMode_) endSketchMode();
    if (edgePicking_) cancelEdgePick();
    SketchObject sketch;
    sketch.name = name;
    sketch.plane = kFacePlane;
    sketch.frame = ForgeCad::faceSketchFrame(ForgeCad::Kernel::Vec3(face.point[0], face.point[1], face.point[2]),
                                             normalized(ForgeCad::Kernel::Vec3(face.normal[0], face.normal[1], face.normal[2])),
                                             ForgeCad::Kernel::Vec3(camera_.orientation_.up[0], camera_.orientation_.up[1], camera_.orientation_.up[2]));
    sketch.faceSource = extrusions_.at(body).name;
    if (extrusions_.at(body).forgeBody && face.face >= 0) {
        auto ref = std::make_shared<GeometryRef>();
        ref->kind = 5; ref->index = body; ref->featureId = extrusions_.at(body).featureId;
        ref->point = ForgeCad::faceReference(*extrusions_.at(body).forgeBody, ForgeCad::Kernel::FaceId(face.face),
            ForgeCad::Kernel::Vec3(face.point[0], face.point[1], face.point[2]));
        sketch.support = std::move(ref);
    }
    recordUndo();
    sketches_.append(sketch);
    activeSketch_ = sketches_.size() - 1;
    selection_ = {};
    documentChanged();
    beginSketchMode(kFacePlane);
    return activeSketch_;
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
    sketchSessionFeatures_.clear();
    refreshSketchContext();
    raySelectionCache_.clear();projectedEdges_.clear();
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
    sketchSessionFeatures_.clear();
    refreshSketchContext();
    raySelectionCache_.clear();projectedEdges_.clear();
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
    return sketchCombCache_.lines(index, curve, sketchCombScale_);
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
