#include "forgeCad2026_dialogs.h"
#include "cad_drawing_browser.h"
#include "cad_drawing_window.h"
#include "cad_toolbar.h"
#include "cad_curve_extract.h"

#include "kernel/fk_cancel.h"
#include "cad_spatial_sketch.h"
#include "forgeCad2026_viewport.h"
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
#include "cad_icons.h"
#include "cad_kernel.h"
#include "cad_sketch_edit.h"
#include "cad_sketch_offset.h"
#include "cad_snap.h"
#include "fk_parallel.h"

#include "cad_menu.h"
#include "toolbar_editor.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <thread>
#include <cstdio>
#include <limits>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

using ForgeCad::Gui::uiFont;
using ForgeCad::Gui::pointLength;
using ForgeCad::Gui::pointDistance;
using ForgeCad::Gui::kSelectionColor;
using ForgeCad::Gui::kHoverColor;
using ForgeCad::Gui::featureIconName;
using ForgeCad::Gui::wrappedNote;
using ForgeCad::Gui::runUntilApplied;



HelixDialogResult helixDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
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
                              .arg(c + 1).arg(ForgeCad::formatLength(r, 6, true, viewport)).arg(curve.construction ? QStringLiteral(", costruzione") : QString()));
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
    dialog.setSizeIdentity(QStringLiteral("helix"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
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
    turnsBox->setLengthMeasurement(false);
    auto *heightBox = spin(initial.helix.height, 1e-6, 1e6, 6);
    auto *taperBox = spin(initial.helix.taper, -89.0, 89.0, 4, QStringLiteral("°"));
    taperBox->setToolTip(QStringLiteral("Elica conica: positivo = il raggio cresce lungo l'asse. Sulla faccia di un cono vale quella della faccia."));
    auto *startBox = spin(initial.helix.startAngle, -360.0, 360.0, 4, QStringLiteral("°"));
    startBox->setToolTip(QStringLiteral("Base da schizzo: 0° verso X, 90° verso Y (quadrante superiore), anche invertendo l'asse. Spigolo o faccia: dall'asse X del modello proiettato."));
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

bool datumDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ForgeCad::DatumMode> &modes = ForgeCad::datumModes();
    ExtrusionObject definition = initial;
    definition.feature = BodyFeature::DatumPlane;
    definition.operation = -1;
    DatumParameters &d = definition.datum;
    d.mode = qBound(0, d.mode, int(modes.size()) - 1);
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("datum-plane"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
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
    auto *status = new FunctionMessage(&dialog);
    status->setMinimumWidth(280);
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

bool sketchPatternDialog(QWidget *parent, CadViewport *viewport, int kind) {
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
    dialog.setSizeIdentity(QStringLiteral("sketch-pattern-%1").arg(kind));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(titles[kind]);
    auto *form = dialog.createScrollableForm();
    form->addRow(wrappedNote(QStringLiteral("%1 entita' selezionate. Un segmento scelto come riferimento non si ripete.").arg(selection.size()), &dialog));
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
    auto *status = new FunctionMessage(&dialog);
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

bool sketchOffsetDialog(QWidget *parent, CadViewport *viewport) {
    const QString title = QStringLiteral("Offset entita'");
    if (!viewport->activeSketchObject()) {
        QMessageBox::information(parent, title, QStringLiteral("Entra in modalita' schizzo e seleziona le entita' da copiare a distanza."));
        return false;
    }
    static ForgeCad::SketchOffset last;
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("sketch-offset"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *selectionLabel = wrappedNote(QString(), &dialog);
    form->addRow(selectionLabel);
    auto *distanceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    distanceBox->setDecimals(6);
    distanceBox->setRange(0.0, 100000.0);
    distanceBox->setValue(last.distance);
    distanceBox->setKeyboardTracking(false);
    auto *reverseButton = new QPushButton(ForgeCad::commandIcon(QStringLiteral("reverseDirection")), QStringLiteral("Inverti il verso"), &dialog);
    reverseButton->setCheckable(true);
    reverseButton->setChecked(last.reverse);
    reverseButton->setToolTip(QStringLiteral("Le catene chiuse vanno verso l'esterno, quelle aperte a sinistra: acceso, dall'altra parte"));
    auto *bothBox = new QCheckBox(QStringLiteral("Dalle due parti"), &dialog);
    bothBox->setChecked(last.bothSides);
    auto *roundBox = new QCheckBox(QStringLiteral("Archi negli angoli convessi (altrimenti spigoli vivi)"), &dialog);
    roundBox->setChecked(last.roundCorners);
    auto *constructionBox = new QCheckBox(QStringLiteral("Entita' di partenza di costruzione"), &dialog);
    constructionBox->setChecked(last.construction);
    auto *dimensionBox = new QCheckBox(QStringLiteral("Mantieni la quota di offset"), &dialog);
    dimensionBox->setChecked(true);
    form->addRow(QStringLiteral("Distanza:"), distanceBox);
    form->addRow(dimensionBox);
    form->addRow(QString(), reverseButton);
    form->addRow(QString(), bothBox);
    form->addRow(QString(), roundBox);
    form->addRow(QString(), constructionBox);
    form->addRow(wrappedNote(QStringLiteral("Segmenti e archi restano esatti (paralleli e concentrici); spline ed ellissi diventano "
                                           "curve approssimate entro 1e-7."), &dialog));
    auto *status = new FunctionMessage(&dialog);
    form->addRow(status);
    const auto current = [&] {
        ForgeCad::SketchOffset offset;
        offset.distance = distanceBox->value();
        offset.dimensioned = dimensionBox->isChecked();
        offset.reverse = reverseButton->isChecked();
        offset.bothSides = bothBox->isChecked();
        offset.roundCorners = roundBox->isChecked();
        offset.construction = constructionBox->isChecked();
        return offset;
    };
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    const auto refresh = [&] {
        const int count = viewport->sketchSelection().size();
        selectionLabel->setText(QStringLiteral("%1 entita' selezionate — clic su curve o bordi del modello per aggiungere; clic di nuovo per togliere. Riquadro per le entita' dello schizzo.").arg(count));
        reverseButton->setEnabled(!bothBox->isChecked());
        const QString error = count ? viewport->previewSketchOffset(current()) : QString();
        if (!count) viewport->clearSketchPatternPreview();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(count > 0 && error.isEmpty());
        status->setStyleSheet(error.isEmpty() ? QStringLiteral("color: #9fc6e8;") : QStringLiteral("color: #ff7a6a;"));
        status->setText(!count ? QStringLiteral("Seleziona curve dello schizzo o bordi del modello. I bordi saranno copiati come riferimenti di costruzione.")
                             : error.isEmpty() ? QStringLiteral("le copie in arancio nella vista") : error);
    };
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(reverseButton, &QPushButton::toggled, &dialog, refresh);
    for (QCheckBox *box : {bothBox, roundBox, constructionBox, dimensionBox}) QObject::connect(box, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    viewport->setDrawingTool(DrawingTool::Select);
    viewport->setSketchOffsetSelectionCallback(refresh);
    refresh();
    const bool applied = runUntilAppliedModeless(qobject_cast<QMainWindow *>(parent), viewport, dialog, form, buttons, [&] {
        const ForgeCad::SketchOffset offset = current();
        const QString error = viewport->applySketchOffset(offset);
        if (error.isEmpty()) last = offset;
        return error;
    });
    viewport->setSketchOffsetSelectionCallback({});
    viewport->clearSketchPatternPreview();
    return applied;
}

bool sketchMoveDialog(QWidget *parent, CadViewport *viewport) {
    const QString title = QStringLiteral("Sposta entita'");
    const SketchObject *sketch = viewport->activeSketchObject();
    if (!sketch) {
        QMessageBox::information(parent, title, QStringLiteral("Entra in modalita' schizzo e seleziona le entita' da spostare."));
        return false;
    }
    const QVector<SketchElementSelection> selection = viewport->sketchSelection();
    if (selection.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Seleziona prima le entita' (clic, Maiusc+clic o il riquadro)."));
        return false;
    }
    // Centro del riquadro delle entita' scelte (punti esatti, non i campioni).
    QRectF box;
    const auto add = [&box](const QPointF &p) { box = box.isNull() ? QRectF(p, QSizeF(0, 0)) : box.united(QRectF(p, QSizeF(0, 0))); };
    for (const SketchElementSelection &element : selection) {
        if (element.kind == 0 && element.index >= 0 && element.index < sketch->segments.size()) {
            add(sketch->segments.at(element.index).first);
            add(sketch->segments.at(element.index).second);
        } else if (element.kind == 1 && element.index >= 0 && element.index < sketch->curves.size()) {
            for (const QPointF &p : sketch->curves.at(element.index).controlPoints) add(p);
        }
    }
    const QPointF selectionCenter = box.center();
    const QVector<QPointF> points = viewport->selectedSketchPoints();
    static ForgeCad::SketchMove last;
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("sketch-move"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    form->addRow(wrappedNote(QStringLiteral("%1 entita' selezionate. Prima la rotazione attorno al centro (antiorario), poi la traslazione. "
                                            "Le entita' spostate perdono i vincoli che non valgono piu' (verso le entita' rimaste ferme, fissi).")
                                 .arg(selection.size()), &dialog));
    const auto spin = [&dialog](double value, double lo, double hi, const QString &prefix, bool lengthMeasure = true) {
        auto *box = new ForgeCad::ExpressionSpinBox(&dialog);
        box->setLengthMeasurement(lengthMeasure);
        box->setDecimals(6);
        box->setRange(lo, hi);
        box->setValue(value);
        box->setPrefix(prefix);
        box->setKeyboardTracking(false);
        return box;
    };
    const auto pair = [&dialog](QWidget *a, QWidget *b) {
        auto *row = new QWidget(&dialog);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(a);
        layout->addWidget(b);
        return row;
    };
    QDoubleSpinBox *dx = spin(last.translation.x(), -1e6, 1e6, QStringLiteral("ΔX ")), *dy = spin(last.translation.y(), -1e6, 1e6, QStringLiteral("ΔY "));
    QDoubleSpinBox *angleBox = spin(last.angle, -360.0, 360.0, QString(), false);
    angleBox->setSuffix(QStringLiteral(" °"));
    const QPointF center = points.isEmpty() ? selectionCenter : points.first();
    QDoubleSpinBox *cx = spin(center.x(), -1e6, 1e6, QStringLiteral("X ")), *cy = spin(center.y(), -1e6, 1e6, QStringLiteral("Y "));
    auto *centerBox = new QComboBox(&dialog);
    centerBox->addItem(QStringLiteral("Centro della selezione"));
    centerBox->addItem(QStringLiteral("Origine"));
    if (!points.isEmpty()) centerBox->addItem(QStringLiteral("Punto scelto (Ctrl+clic)"));
    centerBox->addItem(QStringLiteral("Coordinate"));
    centerBox->setCurrentIndex(points.isEmpty() ? 0 : 2);
    auto *copyBox = new QCheckBox(QStringLiteral("Copia (le entita' di partenza restano)"), &dialog);
    copyBox->setChecked(last.copy);
    form->addRow(QStringLiteral("Traslazione:"), pair(dx, dy));
    form->addRow(QStringLiteral("Rotazione:"), angleBox);
    form->addRow(QStringLiteral("Centro:"), centerBox);
    form->addRow(QString(), pair(cx, cy));
    form->addRow(QString(), copyBox);
    auto *status = new FunctionMessage(&dialog);
    form->addRow(status);
    const auto current = [&] {
        ForgeCad::SketchMove move;
        move.translation = QPointF(dx->value(), dy->value());
        move.angle = angleBox->value();
        move.center = QPointF(cx->value(), cy->value());
        move.copy = copyBox->isChecked();
        return move;
    };
    const auto refresh = [&] {
        const QString error = viewport->previewSketchMove(current());
        status->setStyleSheet(error.isEmpty() ? QStringLiteral("color: #9fc6e8;") : QStringLiteral("color: #ff7a6a;"));
        status->setText(error.isEmpty() ? QStringLiteral("la posizione nuova in arancio nella vista") : error);
    };
    QObject::connect(centerBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        const QString choice = centerBox->currentText();
        const bool manual = choice == QStringLiteral("Coordinate");
        QPointF c(cx->value(), cy->value());
        if (choice == QStringLiteral("Centro della selezione")) c = selectionCenter;
        else if (choice == QStringLiteral("Origine")) c = QPointF();
        else if (!manual && !points.isEmpty()) c = points.first();
        {
            const QSignalBlocker bx(cx), by(cy);
            cx->setValue(c.x());
            cy->setValue(c.y());
        }
        cx->setEnabled(manual);
        cy->setEnabled(manual);
        refresh();
    });
    cx->setEnabled(false);
    cy->setEnabled(false);
    for (QDoubleSpinBox *box : {dx, dy, angleBox, cx, cy}) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(copyBox, &QCheckBox::toggled, &dialog, refresh);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refresh();
    const bool applied = runUntilApplied(dialog, form, buttons, [&] {
        for (QDoubleSpinBox *box : {dx, dy, angleBox, cx, cy}) box->interpretText();
        const ForgeCad::SketchMove move = current();
        const QString error = viewport->applySketchMove(move);
        if (error.isEmpty()) last = move;
        return error;
    });
    viewport->clearSketchPatternPreview();
    return applied;
}

bool spatialSketchDialog(QMainWindow *window,CadViewport *viewport,const QString &title,int replaced,
    const ExtrusionObject &initial,const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition=initial;definition.feature=BodyFeature::SpatialSketch;definition.operation=-1;definition.sketchIndex=-1;
    auto &p=definition.spatialSketch;
    if(p.entities.isEmpty()){SpatialCurve3D e;e.id=p.nextId++;p.entities.append(e);}
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("spatial-sketch"), {QStringLiteral("spatialSketchDialog")});
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);dialog.setObjectName(QStringLiteral("spatialSketchDialog"));dialog.setWindowTitle(title);
    auto *form=dialog.createScrollableForm();
    auto *entities=new QComboBox(&dialog);entities->setObjectName(QStringLiteral("spatialEntities"));
    auto *newLine=new QPushButton(QStringLiteral("Nuova polilinea"),&dialog),*newSpline=new QPushButton(QStringLiteral("Nuova spline"),&dialog);
    newLine->setObjectName(QStringLiteral("spatialNewLine"));newSpline->setObjectName(QStringLiteral("spatialNewSpline"));
    auto *removeEntity=new QPushButton(QStringLiteral("Elimina entita'"),&dialog);
    auto *kind=new QComboBox(&dialog);kind->addItems({QStringLiteral("Polilinea"),QStringLiteral("Spline interpolante")});
    auto *closed=new QCheckBox(QStringLiteral("Chiusa"),&dialog);closed->setObjectName(QStringLiteral("spatialClosed"));
    auto *points=new QTableWidget(0,3,&dialog);points->setObjectName(QStringLiteral("spatialPoints"));
    points->setHorizontalHeaderLabels({QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")});
    points->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);points->setSelectionBehavior(QAbstractItemView::SelectRows);
    points->setSelectionMode(QAbstractItemView::SingleSelection);points->setMinimumHeight(150);
    std::array<ForgeCad::ExpressionSpinBox*,3> xyz;
    for(int i=0;i<3;++i){xyz[i]=new ForgeCad::ExpressionSpinBox(&dialog);xyz[i]->setDecimals(9);xyz[i]->setRange(-1e9,1e9);xyz[i]->setObjectName(QStringLiteral("spatial%1").arg(QStringLiteral("XYZ")[i]));}
    auto *addXYZ=new QPushButton(QStringLiteral("Aggiungi punto XYZ"),&dialog);addXYZ->setObjectName(QStringLiteral("spatialAddXYZ"));
    auto *changePoint=new QPushButton(QStringLiteral("Aggiorna punto selezionato"),&dialog);
    auto *removePoint=new QPushButton(QStringLiteral("Elimina punto selezionato"),&dialog);
    auto *plane=new QComboBox(&dialog);plane->setObjectName(QStringLiteral("spatialPlane"));
    plane->addItems({QStringLiteral("Vista, attraverso l'ultimo punto"),QStringLiteral("XY: profondita' Z"),QStringLiteral("XZ: profondita' Y"),QStringLiteral("YZ: profondita' X")});
    auto *depth=new ForgeCad::ExpressionSpinBox(&dialog);depth->setObjectName(QStringLiteral("spatialDepth"));depth->setDecimals(9);depth->setRange(-1e9,1e9);
    auto *snap=new QCheckBox(QStringLiteral("Aggancia ai punti esistenti"),&dialog);snap->setChecked(true);
    auto *draw=new QPushButton(QStringLiteral("Inserisci punti nella vista"),&dialog);draw->setObjectName(QStringLiteral("spatialDraw"));draw->setCheckable(true);
    auto *undo=new QPushButton(QStringLiteral("Annulla modifica"),&dialog),*redo=new QPushButton(QStringLiteral("Ripristina modifica"),&dialog);
    undo->setObjectName(QStringLiteral("spatialUndo"));redo->setObjectName(QStringLiteral("spatialRedo"));
    form->addRow(QStringLiteral("Entita':"),entities);form->addRow(newLine,newSpline);form->addRow(removeEntity);
    form->addRow(QStringLiteral("Tipo:"),kind);form->addRow(closed);form->addRow(points);
    for(int i=0;i<3;++i)form->addRow(QStringLiteral("%1:").arg(QStringLiteral("XYZ")[i]),xyz[i]);
    form->addRow(addXYZ);form->addRow(changePoint);form->addRow(removePoint);form->addRow(undo,redo);
    form->addRow(QStringLiteral("Piano temporaneo:"),plane);form->addRow(QStringLiteral("Profondita':"),depth);form->addRow(snap);form->addRow(draw);
    form->addRow(wrappedNote(QStringLiteral("Clic: aggiungi un punto. Trascina con il destro: ruota; tasto centrale: sposta la vista. "
        "Esc interrompe l'inserimento. Puoi cambiare piano e profondita' per ogni punto, oppure modificare XYZ nella tabella. "
        "Le curve non sono vincolate al piano temporaneo."),&dialog));
    auto *status=new FunctionMessage(&dialog);form->addRow(status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    auto scope=std::make_unique<PreviewScope>(viewport,dialog,form,replaced);
    QVector<SpatialSketchParameters> undos,redos;int active=0;
    std::function<void()> refresh;
    const auto remember=[&]{undos.append(p);redos.clear();};
    const auto configure=[&]{
        const QVector<SpatialPoint> nodes=active>=0 && active<p.entities.size()?p.entities[active].points:QVector<SpatialPoint>{};
        viewport->configureSpatialDrawing(plane->currentIndex(),depth->value(),nodes.isEmpty()?SpatialPoint{}:nodes.last(),nodes,snap->isChecked());
    };
    refresh=[&]{
        active=qBound(0,active,qMax(0,int(p.entities.size())-1));
        const QSignalBlocker be(entities),bk(kind),bc(closed),bp(points);
        entities->clear();
        for(const auto &e:p.entities)entities->addItem(QStringLiteral("%1 %2 (%3 punti)").arg(e.spline?QStringLiteral("Spline"):QStringLiteral("Polilinea")).arg(e.id).arg(e.points.size()));
        entities->setCurrentIndex(p.entities.isEmpty()?-1:active);points->setRowCount(0);
        if(!p.entities.isEmpty()){
            const auto &e=p.entities[active];kind->setCurrentIndex(e.spline?1:0);closed->setChecked(e.closed);points->setRowCount(e.points.size());
            for(int row=0;row<e.points.size();++row){const auto &q=e.points[row];const double v[3]={q.x,q.y,q.z};
                for(int col=0;col<3;++col)points->setItem(row,col,new QTableWidgetItem(ForgeCad::formatLength(v[col],9,false, &dialog)));}
        }
        kind->setEnabled(!p.entities.isEmpty());closed->setEnabled(!p.entities.isEmpty());undo->setEnabled(!undos.isEmpty());redo->setEnabled(!redos.isEmpty());
        configure();
        auto preview=definition;
        // Un punto in corso resta visibile come nodo; non e' ancora una curva.
        for(int i=preview.spatialSketch.entities.size()-1;i>=0;--i)
            if(preview.spatialSketch.entities[i].points.size()<2)preview.spatialSketch.entities.removeAt(i);
        if(preview.spatialSketch.entities.isEmpty()){viewport->clearPreview();scope->label->setText(QStringLiteral("aggiungi almeno due punti"));}
        else scope->request(preview);
    };
    const auto addPoint=[&](SpatialPoint q){
        if(p.entities.isEmpty()){remember();SpatialCurve3D e;e.id=p.nextId++;p.entities.append(e);active=0;}
        else remember();
        const auto &old=p.entities[active].points;
        if(!old.isEmpty()){const auto &last=old.last();const double dx=q.x-last.x,dy=q.y-last.y,dz=q.z-last.z;
            if(dx*dx+dy*dy+dz*dz<1e-18){undos.removeLast();status->setText(QStringLiteral("Il nuovo punto coincide con l'ultimo."));return;}}
        p.entities[active].points.append(q);xyz[0]->setValue(q.x);xyz[1]->setValue(q.y);xyz[2]->setValue(q.z);status->clear();refresh();
    };
    QObject::connect(draw,&QPushButton::toggled,&dialog,[&](bool enabled){
        if(enabled){configure();viewport->setSpatialDrawing(addPoint,[&]{draw->setChecked(false);},replaced);}
        else viewport->setSpatialDrawing({},{});
    });
    const auto newEntity=[&](bool spline){remember();
        if(!p.entities.isEmpty() && p.entities[active].points.isEmpty())p.entities[active].spline=spline;
        else {SpatialCurve3D e;e.id=p.nextId++;e.spline=spline;p.entities.append(e);active=p.entities.size()-1;}
        refresh();draw->setChecked(true);
    };
    QObject::connect(newLine,&QPushButton::clicked,&dialog,[&]{newEntity(false);});
    QObject::connect(newSpline,&QPushButton::clicked,&dialog,[&]{newEntity(true);});
    QObject::connect(entities,&QComboBox::currentIndexChanged,&dialog,[&](int index){if(index>=0){active=index;refresh();}});
    QObject::connect(removeEntity,&QPushButton::clicked,&dialog,[&]{if(!p.entities.isEmpty()){remember();p.entities.removeAt(active);refresh();}});
    QObject::connect(kind,&QComboBox::currentIndexChanged,&dialog,[&](int index){if(!p.entities.isEmpty()){remember();p.entities[active].spline=index==1;refresh();}});
    QObject::connect(closed,&QCheckBox::toggled,&dialog,[&](bool value){if(!p.entities.isEmpty()){remember();p.entities[active].closed=value;refresh();}});
    QObject::connect(addXYZ,&QPushButton::clicked,&dialog,[&]{addPoint({xyz[0]->value(),xyz[1]->value(),xyz[2]->value()});});
    QObject::connect(points,&QTableWidget::itemSelectionChanged,&dialog,[&]{const int row=points->currentRow();
        if(active<p.entities.size() && row>=0 && row<p.entities[active].points.size()){const auto &q=p.entities[active].points[row];xyz[0]->setValue(q.x);xyz[1]->setValue(q.y);xyz[2]->setValue(q.z);}});
    QObject::connect(points,&QTableWidget::cellChanged,&dialog,[&](int row,int col){
        if(active>=p.entities.size() || row<0 || row>=p.entities[active].points.size())return;
        double value;if(!ForgeCad::evaluateExpression(points->item(row,col)->text(),value) || !std::isfinite(value)){
            status->setText(QStringLiteral("Coordinata non valida."));refresh();return;}
        remember();value*=ForgeCad::millimetersPerUnit(viewport->lengthUnit());auto &q=p.entities[active].points[row];
        if(col==0)q.x=value;else if(col==1)q.y=value;else q.z=value;status->clear();refresh();
    });
    QObject::connect(changePoint,&QPushButton::clicked,&dialog,[&]{int row=points->currentRow();if(active<p.entities.size() && row>=0 && row<p.entities[active].points.size()){
        remember();p.entities[active].points[row]={xyz[0]->value(),xyz[1]->value(),xyz[2]->value()};refresh();}});
    QObject::connect(removePoint,&QPushButton::clicked,&dialog,[&]{int row=points->currentRow();if(active<p.entities.size() && row>=0 && row<p.entities[active].points.size()){
        remember();p.entities[active].points.removeAt(row);refresh();}});
    QObject::connect(undo,&QPushButton::clicked,&dialog,[&]{if(!undos.isEmpty()){redos.append(p);p=undos.takeLast();refresh();}});
    QObject::connect(redo,&QPushButton::clicked,&dialog,[&]{if(!redos.isEmpty()){undos.append(p);p=redos.takeLast();refresh();}});
    QObject::connect(plane,&QComboBox::currentIndexChanged,&dialog,[&](int index){
        SpatialPoint anchor;if(active<p.entities.size() && !p.entities[active].points.isEmpty())anchor=p.entities[active].points.last();
        depth->setValue(index==1?anchor.z:index==2?anchor.y:index==3?anchor.x:0);configure();
    });
    QObject::connect(depth,&QDoubleSpinBox::valueChanged,&dialog,[&]{configure();});
    QObject::connect(snap,&QCheckBox::toggled,&dialog,[&]{configure();});
    auto *undoKey=new QShortcut(QKeySequence::Undo,&dialog),*redoKey=new QShortcut(QKeySequence::Redo,&dialog);
    undoKey->setContext(Qt::WidgetWithChildrenShortcut);redoKey->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(undoKey,&QShortcut::activated,undo,&QPushButton::click);QObject::connect(redoKey,&QShortcut::activated,redo,&QPushButton::click);
    viewport->setSpatialEditHistory([&]{undo->click();},[&]{redo->click();});
    refresh();draw->setChecked(true);
    const bool accepted=runUntilAppliedModeless(window,viewport,dialog,form,buttons,[&]{
        QString error;ForgeCad::buildSpatialSketch(p,&error);if(!error.isEmpty())return error;return apply(definition);
    });
    viewport->setSpatialDrawing({},{});viewport->setSpatialEditHistory({},{});scope.reset();return accepted;
}

bool extractedCurveDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
    const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition=initial;
    definition.operation=-1;definition.feature=BodyFeature::ExtractedCurve;definition.sketchIndex=-1;
    auto &p=definition.curveExtraction;
    if(replaced<0) p.view=viewport->curveTrimView();
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("extracted-curve"), {QStringLiteral("extractedCurveDialog")});
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);dialog.setObjectName(QStringLiteral("extractedCurveDialog"));dialog.setWindowTitle(title);dialog.setModal(false);
    auto *form=dialog.createScrollableForm();
    auto *sourceLabel=new QLabel(&dialog),*toolLabel=new QLabel(&dialog);
    sourceLabel->setWordWrap(true);toolLabel->setWordWrap(true);
    auto *sourceButton=new QPushButton(QStringLiteral("Scegli bordo o curva"),&dialog);
    auto *toolButton=new QPushButton(QStringLiteral("Scegli segmento"),&dialog);
    sourceButton->setObjectName(QStringLiteral("extractSourceButton"));toolButton->setObjectName(QStringLiteral("extractToolButton"));
    sourceButton->setCheckable(true);toolButton->setCheckable(true);
    auto *joinBox=new QCheckBox(QStringLiteral("Unisci piu' bordi adiacenti in una curva"),&dialog);
    joinBox->setObjectName(QStringLiteral("extractJoinBox"));joinBox->setChecked(!p.sources.isEmpty());
    auto *sourceList=new GeometryReferenceList(dialog, viewport, QStringLiteral("extractSources"));
    auto *removeSource=new QPushButton(QStringLiteral("Rimuovi bordo selezionato"),&dialog);removeSource->setObjectName(QStringLiteral("extractRemoveSource"));
    auto *trimBox=new QCheckBox(QStringLiteral("Taglia la curva"),&dialog);trimBox->setChecked(p.trimmed);
    auto *modeBox=new QComboBox(&dialog);modeBox->addItems({QStringLiteral("Intersezione reale 3D"),QStringLiteral("Proiezione nella vista salvata")});modeBox->setCurrentIndex(p.projected?1:0);
    auto *viewButton=new QPushButton(QStringLiteral("Usa vista corrente"),&dialog);
    auto *sideBox=new QComboBox(&dialog);sideBox->addItems({QStringLiteral("Mantieni dall'inizio al taglio"),QStringLiteral("Mantieni dal taglio alla fine")});sideBox->setCurrentIndex(p.keepStart?0:1);
    auto *cutBox=new QComboBox(&dialog);cutBox->setObjectName(QStringLiteral("extractCutBox"));
    form->addRow(QStringLiteral("Sorgente:"),sourceLabel);form->addRow(sourceButton);
    form->addRow(joinBox);form->addRow(sourceList);form->addRow(removeSource);
    form->addRow(trimBox);form->addRow(QStringLiteral("Segmento:"),toolLabel);form->addRow(toolButton);
    form->addRow(QStringLiteral("Metodo:"),modeBox);form->addRow(viewButton);
    form->addRow(QStringLiteral("Intersezione:"),cutBox);form->addRow(QStringLiteral("Parte:"),sideBox);
    form->addRow(wrappedNote(QStringLiteral("La curva resta legata alla sorgente e conserva la sua forma 3D. "
        "Il segmento e la vista del taglio vengono conservati per la rigenerazione."),&dialog));
    auto *status=new FunctionMessage(&dialog);form->addRow(status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    auto scope=std::make_unique<PreviewScope>(viewport,dialog,form,replaced);
    int picking=0;
    const auto refresh=[&]{
        sourceLabel->setText(p.source.kind<0 ? QStringLiteral("nessuna curva") : ForgeCad::geometryRefText(p.source,viewport->sketches(),viewport->extrusions()));
        sourceList->setReferences(p.sources);
        sourceList->setVisible(joinBox->isChecked());removeSource->setVisible(joinBox->isChecked());
        sourceButton->setText(joinBox->isChecked()?QStringLiteral("Scegli o togli bordi adiacenti"):QStringLiteral("Scegli bordo o curva"));
        toolLabel->setText(p.tool.kind<0 ? QStringLiteral("nessun segmento") : ForgeCad::geometryRefText(p.tool,viewport->sketches(),viewport->extrusions()));
        sourceButton->setChecked(picking==1);toolButton->setChecked(picking==2);
        for(QWidget *w:QList<QWidget*>{toolButton,toolLabel,modeBox,cutBox,sideBox})w->setEnabled(p.trimmed);
        viewButton->setEnabled(p.trimmed && p.projected);
        auto marks=p.sources.isEmpty()?QVector<GeometryRef>{p.source}:p.sources;if(p.trimmed)marks.append(p.tool);viewport->setReferenceMarks(marks);
        if(p.source.kind<0 || (p.trimmed && p.tool.kind<0)){viewport->clearPreview();scope->label->setText(QStringLiteral("scegli i riferimenti"));return;}
        scope->request(definition);
    };
    const auto updateCuts=[&]{
        QVector<double> cuts;QString reason;
        if(p.trimmed && p.source.kind>=0 && p.tool.kind>=0)
            ForgeCad::extractCurves3D(p,replaced<0?viewport->extrusions().size():replaced,viewport->sketches(),viewport->extrusions(),&reason,&cuts);
        const QSignalBlocker blocked(cutBox);cutBox->clear();
        int selected=-1;double best=2;
        for(int k=0;k<cuts.size();++k){
            cutBox->addItem(QStringLiteral("Taglio %1").arg(k+1),cuts[k]);
            if(std::fabs(cuts[k]-p.cutHint)<best){best=std::fabs(cuts[k]-p.cutHint);selected=k;}
        }
        if(selected>=0){cutBox->setCurrentIndex(selected);p.cutHint=cuts[selected];}
        refresh();
    };
    const auto startPick=[&](int target){
        if(picking==target){viewport->cancelReferencePick();picking=0;refresh();return;}
        const QString error=viewport->beginReferencePick(ForgeCad::DatumRoleCurve|ForgeCad::DatumRoleLine,replaced);
        picking=error.isEmpty()?target:0;status->setText(error);refresh();
    };
    viewport->setReferencePickCallback([&](bool picked,GeometryRef ref){
        if(!picked){picking=0;refresh();return;}
        if(!picking)return;
        std::vector<ForgeCad::Kernel::PathSegment> path;QString reason;
        if(!ForgeCad::geometryRefPath(ref,replaced<0?viewport->extrusions().size():replaced,viewport->sketches(),viewport->extrusions(),path,&reason)){
            status->setText(reason);return;
        }
        if(picking==1&&joinBox->isChecked()){
            int existing=-1;
            for(int i=0;i<p.sources.size();++i){const auto &r=p.sources[i];if(r.kind==ref.kind&&r.index==ref.index&&r.point.subshape==ref.point.subshape&&r.element.kind==ref.element.kind&&r.element.element==ref.element.element){existing=i;break;}}
            if(existing>=0)p.sources.removeAt(existing);else p.sources.append(ref);
            p.source=p.sources.isEmpty()?GeometryRef{}:p.sources.first();
            status->clear();updateCuts();
            const QString nextError=viewport->beginReferencePick(ForgeCad::DatumRoleCurve|ForgeCad::DatumRoleLine,replaced);
            if(!nextError.isEmpty()){picking=0;status->setText(nextError);refresh();}
            return;
        }
        if(picking==1){p.source=ref;p.sources.clear();}else p.tool=ref;
        viewport->cancelReferencePick();picking=0;status->clear();updateCuts();
    });
    QObject::connect(joinBox,&QCheckBox::toggled,&dialog,[&](bool enabled){
        if(enabled&&p.sources.isEmpty()&&p.source.kind>=0)p.sources.append(p.source);
        if(!enabled){if(!p.sources.isEmpty())p.source=p.sources.first();p.sources.clear();}
        updateCuts();
    });
    QObject::connect(removeSource,&QPushButton::clicked,&dialog,[&]{const int row=sourceList->currentRow();if(row>=0&&row<p.sources.size())p.sources.removeAt(row);p.source=p.sources.isEmpty()?GeometryRef{}:p.sources.first();updateCuts();});
    QObject::connect(sourceButton,&QPushButton::clicked,&dialog,[&]{startPick(1);});
    QObject::connect(toolButton,&QPushButton::clicked,&dialog,[&]{startPick(2);});
    QObject::connect(trimBox,&QCheckBox::toggled,&dialog,[&](bool value){p.trimmed=value;updateCuts();});
    QObject::connect(modeBox,&QComboBox::currentIndexChanged,&dialog,[&](int index){p.projected=index==1;if(p.projected)p.view=viewport->curveTrimView();updateCuts();});
    QObject::connect(viewButton,&QPushButton::clicked,&dialog,[&]{p.view=viewport->curveTrimView();updateCuts();});
    QObject::connect(sideBox,&QComboBox::currentIndexChanged,&dialog,[&](int index){p.keepStart=index==0;refresh();});
    QObject::connect(cutBox,&QComboBox::currentIndexChanged,&dialog,[&](int index){if(index>=0)p.cutHint=cutBox->itemData(index).toDouble();refresh();});
    if(p.source.kind<0)QTimer::singleShot(0,&dialog,[&]{startPick(1);});
    updateCuts();
    const bool accepted=runUntilAppliedModeless(window,viewport,dialog,form,buttons,[&]{
        if(p.source.kind<0)return QStringLiteral("Scegli il bordo o la curva sorgente.");
        if(p.trimmed && p.tool.kind<0)return QStringLiteral("Scegli il segmento di taglio.");
        return apply(definition);
    });
    viewport->cancelReferencePick();viewport->setReferencePickCallback(nullptr);viewport->setReferenceMarks({});scope.reset();
    return accepted;
}
