#include "forgeCad2026_dialogs.h"
#include "forgeCad2026_operation_illustration.h"
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
#include "forgeCad2026_storyboard.h"
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



BlendDialogResult blendDialog(QWidget *parent, CadViewport *viewport, const QString &title, int base, int hidden,
                                     const QVector<EdgePoint> &edges, double size, bool chamfer, bool allowKindChange, const ChamferSpec &initialSpec,
                                     const std::function<QString(int, const QVector<EdgePoint> &, double, bool, const ChamferSpec &)> &apply,
                                     bool pickInView ) {
    if (!chamfer) return filletDialog(parent, viewport, title, base, hidden, edges, size, initialSpec, apply);
    BlendDialogResult result;
    int selectedBase = base;
    QVector<EdgePoint> selectedEdges = edges;
    bool previewStarted = !pickInView;
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(chamfer ? QStringLiteral("chamfer") : QStringLiteral("fillet"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
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
            secondBox->setSuffix(QStringLiteral(" °"));
            secondBox->setRange(0.01, 179.99);
            secondBox->setValue(angle);
        } else {
            secondLabel->setText(QStringLiteral("Seconda distanza (altra faccia):"));
            secondBox->setLengthMeasurement(true);
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
    auto *edgesLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("blendEdges"));
    form->addRow(QStringLiteral("Spigoli:"), edgesLabel);
    auto *previewLabel = new FunctionMessage(pickInView ? QStringLiteral("seleziona almeno uno spigolo nella vista")
                                               : QStringLiteral("Anteprima in calcolo..."), &dialog);
    previewLabel->setObjectName(QStringLiteral("functionPreviewMessage"));
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
        const QString operation = currentChamfer() ? QStringLiteral("chamfer") : QStringLiteral("fillet");
        dialog.setProperty("operationIllustrationId", operation);
        for (QWidget *widget : dialog.findChildren<QWidget *>())
            if (auto *image = dynamic_cast<ForgeCad::Gui::OperationIllustration *>(widget)) image->setOperation(operation);
        if (auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("functionPanelTabs"))) {
            tabs->setTabText(0, currentChamfer() ? QStringLiteral("Smusso") : QStringLiteral("Costante"));
            tabs->setTabIcon(0, ForgeCad::Gui::operationPanelIcon(operation));
        }
        edgesLabel->setEdges(selectedBase, selectedEdges);
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
    sizeBox->setKeyboardTracking(false);
    secondBox->setKeyboardTracking(false);
    sizeBox->onReturn = refresh;
    secondBox->onReturn = refresh;
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

bool scaleDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &definition,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    if (candidates.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Nella scena non ci sono corpi da scalare."));
        return false;
    }
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("scale"), {QStringLiteral("Scala"), QStringLiteral("Modifica scala")});
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int index : candidates) bodyBox->addItem(logicalBodyLabel(viewport, index));
    bodyBox->setCurrentIndex(qMax(0, int(candidates.indexOf(definition.firstBody))));
    bodyBox->setEnabled(replaced < 0);
    BodyPicker picker(dialog, viewport, replaced, candidates, bodyBox);
    auto *factorBox = new ForgeCad::ExpressionSpinBox(&dialog);
    factorBox->setLengthMeasurement(false);
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
    form->addRow(QStringLiteral("Corpo:"), picker.row(bodyBox));
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
        if (picker.waiting()) {
            scope.label->setText(picker.hint());
            viewport->clearPreview();
            return;
        }
        scope.request(current());
    };
    picker.changed = refresh;
    picker.install();
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(centerBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(factorBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    for (QDoubleSpinBox *box : coordinates) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    // Senza un corpo selezionato prima del comando lo si sceglie nella vista.
    if (replaced < 0 && !candidates.contains(definition.firstBody)) picker.start(true);
    refresh();
    factorBox->selectAll();
    const auto run = [&] {
        if (picker.waiting()) return picker.hint();
        picker.stop();
        return apply(current());
    };
    if (auto *window = qobject_cast<QMainWindow *>(parent)) return runUntilAppliedModeless(window, viewport, dialog, form, buttons, run);
    return runUntilApplied(dialog, form, buttons, run);
}

bool patternDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    ExtrusionObject patternInitial = initial;
    if (initial.patternComponent > 0 && initial.firstBody >= 0 && initial.firstBody < bodies.size()) {
        replaced = initial.firstBody;
        patternInitial = bodies.at(replaced);
    }
    const QVector<int> results = viewport->resultBodiesBefore(replaced);
    QVector<int> candidates = results;
    const int limit = replaced < 0 ? viewport->historyPosition() : std::min(replaced, viewport->historyPosition());
    for (int i = 0; i < limit; ++i)
        if (!bodies.at(i).suppressed && bodies.at(i).error.isEmpty() && bodies.at(i).forgeBody
            && ForgeCad::patternFeatureSupported(bodies.at(i)) && !candidates.contains(i)) candidates.append(i);
    if (replaced >= 0 && patternInitial.firstBody >= 0 && patternInitial.firstBody < limit
        && !candidates.contains(patternInitial.firstBody)) candidates.append(patternInitial.firstBody);
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Nella scena non ci sono corpi da ripetere."));
        return false;
    }
    ExtrusionObject definition = patternInitial;
    definition.feature = BodyFeature::Pattern;
    definition.operation = -1;
    PatternParameters &p = definition.pattern;
    const bool preselected = candidates.contains(definition.firstBody);
    if (!preselected) definition.firstBody = -1;
    if (replaced < 0 && preselected && !results.contains(definition.firstBody)) p.featureOnly = true;
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
    // Il blocco della finestra e' gestito qui per lasciare selezionabile
    // la storyboard. Il lock automatico di showEvent la disabiliterebbe di nuovo.
    FunctionDialogPanel dialog(window, false);
    dialog.setSizeIdentity(QStringLiteral("pattern"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *steps = new QWidget(&dialog);
    auto *stepsLayout = new QHBoxLayout(steps);
    stepsLayout->setContentsMargins(0, 0, 0, 0);
    auto *sourceStep = new QPushButton(QStringLiteral("1. Sorgente"), steps);
    auto *referenceStep = new QPushButton(QStringLiteral("2. Riferimento"), steps);
    auto *parametersStep = new QPushButton(QStringLiteral("3. Parametri"), steps);
    sourceStep->setObjectName(QStringLiteral("patternSourceStep"));
    referenceStep->setObjectName(QStringLiteral("patternReferenceStep"));
    parametersStep->setObjectName(QStringLiteral("patternParametersStep"));
    for (auto *step : {sourceStep, referenceStep, parametersStep}) {
        step->setCheckable(true);
        step->setStyleSheet(QStringLiteral("QPushButton:checked { font-weight: 600; border: 1px solid #e8b65b; }"));
        stepsLayout->addWidget(step);
    }
    auto *stepHint = new QLabel(&dialog);
    stepHint->setObjectName(QStringLiteral("patternStepHint"));
    stepHint->setWordWrap(true);
    form->addRow(steps);
    form->addRow(stepHint);
    auto *bodyBox = new QComboBox(&dialog);
    bodyBox->setObjectName(QStringLiteral("patternBody"));
    for (int index : candidates) {
        QString label = logicalBodyLabel(viewport, index);
        if (ForgeCad::patternFeatureSupported(bodies.at(index)) && !bodies.at(index).name.isEmpty())
            label += QStringLiteral(" — ") + bodies.at(index).name;
        bodyBox->addItem(label, index);
    }
    bodyBox->setCurrentIndex(int(candidates.indexOf(definition.firstBody)));
    bodyBox->setPlaceholderText(QStringLiteral("Scegli dalla vista o dalla storyboard"));
    BodyPicker picker(dialog, viewport, replaced, candidates, bodyBox);
    auto *kindBox = new QComboBox(&dialog);
    kindBox->setObjectName(QStringLiteral("patternKind"));
    kindBox->addItems({QStringLiteral("Lineare"), QStringLiteral("Circolare"), QStringLiteral("Specchio")});
    kindBox->setCurrentIndex(qBound(0, p.kind, 2));
    auto *whatBox = new QComboBox(&dialog);
    whatBox->setObjectName(QStringLiteral("patternWhat"));
    whatBox->setToolTip(QStringLiteral("La funzione ripete il materiale aggiunto o sottratto, mantenendo il corpo di base."));
    whatBox->addItems({QStringLiteral("Il corpo intero"), QStringLiteral("Una funzione")});
    whatBox->setCurrentIndex(p.featureOnly ? 1 : 0);
    auto *sourceRow = picker.row(bodyBox);
    auto *sourcePick = sourceRow->findChild<QPushButton *>();
    sourcePick->setObjectName(QStringLiteral("patternSourcePick"));
    sourcePick->setText(QStringLiteral("Scegli sorgente"));
    sourcePick->setToolTip(QStringLiteral("Seleziona una funzione nella storyboard oppure una faccia nella viewport. Un altro clic termina la scelta."));
    form->addRow(QStringLiteral("Tipo:"), kindBox);
    form->addRow(QStringLiteral("Ripeti:"), whatBox);
    form->addRow(QStringLiteral("Sorgente:"), sourceRow);
    auto *refLabel = new QLabel(&dialog), *refLabel2 = new QLabel(QStringLiteral("Direzione 2:"), &dialog);
    auto *refButton = new QPushButton(&dialog), *refButton2 = new QPushButton(&dialog);
    refButton->setObjectName(QStringLiteral("patternReferencePick"));
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
    countBox->setObjectName(QStringLiteral("patternCount"));
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
    auto *occurrences = new QListWidget(&dialog);
    occurrences->setObjectName(QStringLiteral("patternOccurrences"));
    occurrences->setMaximumHeight(150);
    occurrences->setToolTip(QStringLiteral("Togli la spunta per escludere un'occorrenza; rimettila per ripristinarla."));
    form->addRow(QStringLiteral("Occorrenze attive:"), occurrences);
    auto *status = new FunctionMessage(&dialog);
    status->setMinimumWidth(280);
    status->setObjectName(QStringLiteral("patternError"));
    status->hide();
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
        status->setVisible(error && !text.isEmpty());
    };
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.patternComponent = -1; // anteprima di tutte le occorrenze
        d.firstBody = bodyBox->currentIndex() < 0 ? -1 : candidates.at(bodyBox->currentIndex());
        d.pattern.kind = kindBox->currentIndex();
        d.pattern.featureOnly = whatBox->currentIndex() == 1;
        d.secondBody = -1;
        if (d.pattern.featureOnly && d.firstBody >= 0)
            for (int result : results)
                if (result > d.firstBody && bodies.at(result).modelBodyId == bodies.at(d.firstBody).modelBodyId
                    && bodies.at(result).modelBodyId != 0) d.secondBody = result;
        if (replaced >= 0 && patternInitial.secondBody >= 0 && d.firstBody == patternInitial.firstBody && d.pattern.featureOnly)
            d.secondBody = patternInitial.secondBody;
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
        const int source = bodyBox->currentIndex() < 0 ? -1 : candidates.at(bodyBox->currentIndex());
        const bool feature = source >= 0 && ForgeCad::patternFeatureSupported(bodies.at(source));
        const bool validSource = source >= 0 && (whatBox->currentIndex() == 0 || feature);
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
        {
            const QSignalBlocker blocker(occurrences);
            occurrences->clear();
            const qint64 total = kind == 2 ? 2 : qint64(countBox->value()) * (grid ? countBox2->value() : 1);
            if (total <= 501) for (int i = 0; i < total; ++i) {
                if (kind == 2 && i == 0 && !keepBox->isChecked() && whatBox->currentIndex() == 0) continue;
                auto *item = new QListWidgetItem(i == 0 ? QStringLiteral("1 — originale")
                    : grid ? QStringLiteral("%1 — riga %2, colonna %3").arg(i + 1).arg(i / countBox->value() + 1).arg(i % countBox->value() + 1)
                           : QStringLiteral("%1").arg(i + 1), occurrences);
                item->setData(Qt::UserRole, i);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(p.excluded.contains(i) ? Qt::Unchecked : Qt::Checked);
            }
        }
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
        const bool choosingSource = !validSource || picker.active();
        const int step = choosingSource ? 1 : active >= 0 ? 2 : 3;
        const QString referenceName = kind == 0 ? QStringLiteral("Direzione") : kind == 1 ? QStringLiteral("Asse") : QStringLiteral("Piano");
        sourceStep->setChecked(step == 1);
        referenceStep->setChecked(step == 2);
        parametersStep->setChecked(step == 3);
        sourceStep->setText(validSource ? QStringLiteral("1. Sorgente ✓") : QStringLiteral("1. Sorgente"));
        referenceStep->setText(QStringLiteral("2. %1%2").arg(referenceName, validSource && active < 0 ? QStringLiteral(" ✓") : QString()));
        referenceStep->setEnabled(validSource);
        parametersStep->setEnabled(validSource);
        refButton->setEnabled(validSource);
        refButton2->setEnabled(validSource);
        for (QWidget *w : QList<QWidget *>{flipBox, countBox, spacingBox, angleBox, spreadBox, keepBox,
                          secondBox, flipBox2, countBox2, spacingBox2, occurrences}) w->setEnabled(step == 3);
        sourcePick->setText(picker.active() ? QStringLiteral("Termina scelta") : QStringLiteral("Scegli sorgente"));
        if (step == 1) stepHint->setText(picker.active()
            ? QStringLiteral("Passaggio 1 di 3 — Scegli la sorgente. Clicca una funzione nella storyboard o una faccia nella viewport; puoi usare anche l'elenco Sorgente.")
            : QStringLiteral("Passaggio 1 di 3 — Scegli la sorgente nella storyboard o nell'elenco. Premi Scegli sorgente per selezionarla dalla viewport."));
        else if (step == 2) stepHint->setText(QStringLiteral("Passaggio 2 di 3 — Scegli %1 nella viewport. Esc annulla questa scelta e conserva il riferimento precedente.").arg(referenceName.toLower()));
        else stepHint->setText(QStringLiteral("Passaggio 3 di 3 — Regola istanze e misure, poi premi OK. Sorgente: %1. %2: %3.")
            .arg(bodyBox->currentText(), referenceName, ForgeCad::geometryRefText(p.refs.at(0), viewport->sketches(), viewport->extrusions())));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(step == 3);
        form->setRowVisible(scope->label, step == 3);
        if (step != 3) {
            viewport->clearPreview();
            return;
        }
        scope->request(current());
    };
    QObject::connect(occurrences, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem *item) {
        const int occurrence = item->data(Qt::UserRole).toInt();
        p.excluded.removeAll(occurrence);
        if (item->checkState() == Qt::Unchecked) p.excluded.append(occurrence);
        refresh();
    });
    picker.resolve = [&](const GeometryRef &ref) {
        if (ref.kind != 5) return -1;
        const int source = whatBox->currentIndex() == 1 ? viewport->sourceFeatureForFace(ref)
            : resultBodyForPick(viewport, results, ref.index);
        return candidates.contains(source) ? source : -1;
    };
    picker.changed = refresh;
    picker.message = setStatus;
    picker.starting = [&] { active = -1; setStatus({}, false); };
    const auto pick = [&](int slot) {
        setStatus({}, false);
        picker.stop();
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
        if (picker.handle(picked, ref)) return;
        if (picked && active >= 0) {
            if (ForgeCad::geometryRefRoles(ref, viewport->sketches()) & roles(active)) p.refs[active] = ref;
            else setStatus(QStringLiteral("Questo riferimento non va bene qui."), true);
        }
        active = -1;
        refresh();
    });
    QObject::connect(sourceStep, &QPushButton::clicked, &dialog, [&] { picker.start(); refresh(); });
    QObject::connect(referenceStep, &QPushButton::clicked, &dialog, [&] { pick(0); });
    QObject::connect(parametersStep, &QPushButton::clicked, &dialog, [&] {
        picker.stop(); viewport->cancelReferencePick(); active = -1; setStatus({}, false); refresh();
    });
    QObject::connect(refButton, &QPushButton::clicked, &dialog, [&] { pick(0); });
    QObject::connect(refButton2, &QPushButton::clicked, &dialog, [&] { pick(1); });
    QObject::connect(kindBox, &QComboBox::currentIndexChanged, &dialog, [&](int kind) {
        // Il riferimento che non vale per il tipo nuovo torna quello di partenza.
        if (!(ForgeCad::geometryRefRoles(p.refs.at(0), viewport->sketches()) & (kind == 0 ? int(ForgeCad::DatumRoleLine | ForgeCad::DatumRolePlane) : kind == 1 ? int(ForgeCad::DatumRoleLine) : int(ForgeCad::DatumRolePlane))))
            p.refs[0] = defaultRef(kind);
        if (active >= 0) viewport->cancelReferencePick();  // la scelta del corpo invece continua
        active = -1;
        refresh();
    });
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        picker.stop(); active = -1; setStatus({}, false); refresh();
    });
    QObject::connect(whatBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        setStatus({}, false);
        if (bodyBox->currentIndex() < 0 || (whatBox->currentIndex() == 1
            && !ForgeCad::patternFeatureSupported(bodies.at(candidates.at(bodyBox->currentIndex()))))) picker.start();
        refresh();
    });
    for (QCheckBox *box : {flipBox, spreadBox, keepBox, secondBox, flipBox2}) QObject::connect(box, &QCheckBox::toggled, &dialog, refresh);
    for (QSpinBox *box : {countBox, countBox2}) QObject::connect(box, &QSpinBox::valueChanged, &dialog, refresh);
    for (QDoubleSpinBox *box : {spacingBox, angleBox, spacingBox2}) QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (bodyBox->currentIndex() < 0 || picker.waiting()) return setStatus(picker.hint(), true);
        if (whatBox->currentIndex() == 1 && !ForgeCad::patternFeatureSupported(bodies.at(candidates.at(bodyBox->currentIndex()))))
            return setStatus(QStringLiteral("Scegli una funzione da ripetere."), true);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(current());
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        QDockWidget *dock = window->findChild<QDockWidget *>(QStringLiteral("modelDock"));
        auto *tree = dock ? dynamic_cast<StoryboardTree *>(dock->widget()) : nullptr;
        if (tree) {
            tree->setSourcePickCallback([&](int index, bool whole) {
                if (whole) index = viewport->modelBodyTip(index);
                if (!candidates.contains(index)) return setStatus(QStringLiteral("Questa sorgente non e' disponibile in questo punto della storia."), true);
                picker.stop(); viewport->cancelReferencePick(); active = -1;
                { const QSignalBlocker block(bodyBox); bodyBox->setCurrentIndex(candidates.indexOf(index)); }
                whatBox->setCurrentIndex(!whole && ForgeCad::patternFeatureSupported(bodies.at(index)) ? 1 : 0);
                // Il cambio esplicito sblocca anche la prima scelta obbligatoria.
                QMetaObject::invokeMethod(bodyBox, "currentIndexChanged", Qt::DirectConnection, Q_ARG(int, bodyBox->currentIndex()));
                refresh();
            });
            dock->setEnabled(true);
        }
        dialog.show();
        if (replaced < 0 && !preselected) picker.start(true);
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
        if (tree) { tree->setSourcePickCallback({}); dock->setEnabled(false); }
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    scope.reset();
    return accepted;
}

bool extrusionDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                            const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Extrusion;
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("extrusion"), {QStringLiteral("Estrusione"), QStringLiteral("Modifica estrusione")});
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
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
    // Versi: uno solo, simmetrica (meta' per parte del piano dello schizzo) o
    // due versi con la seconda distanza dall'altra parte.
    auto *sidesBox = new QComboBox(&dialog);
    sidesBox->addItems({QStringLiteral("Un verso"), QStringLiteral("Simmetrica"), QStringLiteral("Due versi")});
    sidesBox->setCurrentIndex(qBound(0, definition.extrudeSides, 2));
    sidesBox->setToolTip(QStringLiteral("Simmetrica: la distanza in tutto, meta' per parte dello schizzo (solo con la fine a distanza).\n"
                                        "Due versi: la fine scelta da una parte e la seconda distanza dall'altra."));
    auto *distance2Box = new ForgeCad::ExpressionSpinBox(&dialog);
    distance2Box->setDecimals(6);
    distance2Box->setRange(0.0, 100000.0);
    distance2Box->setValue(definition.distance2);
    distance2Box->setToolTip(QStringLiteral("Distanza nel verso opposto alla prima (positiva)."));
    // Inverti il verso: la distanza cambia segno (nei due versi le due
    // distanze si scambiano); con la fine "fino a" il verso lo da' il riferimento.
    auto *flipButton = new QPushButton(ForgeCad::commandIcon(QStringLiteral("reverseDirection")), QStringLiteral("Inverti il verso"), &dialog);
    flipButton->setToolTip(QStringLiteral("Estrude dall'altra parte del piano dello schizzo"));
    // Come nei raccordi: l'anteprima si calcola con Invio, non a ogni carattere.
    distanceBox->setKeyboardTracking(false);
    distance2Box->setKeyboardTracking(false);
    auto *refButton = new QPushButton(&dialog);
    refButton->setToolTip(QStringLiteral("Scegli nella vista"));
    auto *operationBox = new QComboBox(&dialog);
    operationBox->addItems({QStringLiteral("Corpo nuovo"), QStringLiteral("Unisci ai solidi"), QStringLiteral("Sottrai dai solidi")});
    operationBox->setCurrentIndex(qBound(0, definition.mergeOperation, 2));
    auto *surfaceBox = new QCheckBox(QStringLiteral("Solo superficie (senza base e coperchio)"), &dialog);
    surfaceBox->setChecked(definition.extrudeSurface);
    surfaceBox->setToolTip(QStringLiteral("Una lamina: un contorno chiuso diventa un tubo aperto alle estremita', un profilo aperto una superficie; "
                                          "non si fonde con i solidi"));
    auto *autoBox = new QCheckBox(QStringLiteral("Automatico: i solidi che hanno punti in comune con l'estrusione"), &dialog);
    autoBox->setChecked(definition.mergeAuto);
    auto *bodyList = new QListWidget(&dialog);
    bodyList->setMinimumHeight(110);
    BodyPicker picker(dialog, viewport, replaced, fillMergeBodyList(bodyList, viewport, replaced, definition.mergeBodies), nullptr, bodyList);
    QWidget *bodyRow = picker.row(bodyList);
    form->addRow(QStringLiteral("Versi:"), sidesBox);
    form->addRow(QStringLiteral("Fine:"), extentBox);
    form->addRow(QStringLiteral("Distanza:"), distanceBox);
    form->addRow(QStringLiteral("Fino a:"), refButton);
    form->addRow(QStringLiteral("Secondo verso:"), distance2Box);
    form->addRow(QString(), flipButton);
    form->addRow(QString(), surfaceBox);
    form->addRow(QStringLiteral("Risultato:"), operationBox);
    form->addRow(QString(), autoBox);
    form->addRow(QStringLiteral("Solidi:"), bodyRow);
    auto *status = new FunctionMessage(&dialog);
    status->setMinimumWidth(280);
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
        d.extrudeSides = sidesBox->currentIndex();
        d.distance2 = distance2Box->value();
        // La superficie non si fonde con i solidi.
        d.extrudeSurface = surfaceBox->isChecked();
        d.mergeOperation = d.extrudeSurface ? 0 : operationBox->currentIndex();
        d.mergeAuto = autoBox->isChecked();
        d.mergeBodies.clear();
        for (int row = 0; row < bodyList->count() && !d.extrudeSurface; ++row)
            if (bodyList->item(row)->checkState() == Qt::Checked) d.mergeBodies.append(bodyList->item(row)->data(Qt::UserRole).toInt());
        return d;
    };
    std::function<void()> refresh;
    refresh = [&] {
        const int extent = extentBox->currentIndex();
        // La simmetrica vale solo con la fine a distanza.
        if (auto *model = qobject_cast<QStandardItemModel *>(sidesBox->model()))
            if (QStandardItem *item = model->item(1)) item->setEnabled(extent == 0);
        if (extent != 0 && sidesBox->currentIndex() == 1) {
            const QSignalBlocker block(sidesBox);
            sidesBox->setCurrentIndex(2);
        }
        const int sides = sidesBox->currentIndex();
        form->setRowVisible(distanceBox, extent == 0);
        form->setRowVisible(refButton, extent != 0);
        form->setRowVisible(distance2Box, sides == 2);
        if (auto *label = qobject_cast<QLabel *>(form->labelForField(distanceBox)))
            label->setText(sides == 1 ? QStringLiteral("Distanza totale:") : QStringLiteral("Distanza:"));
        // La simmetrica non ha verso; con la fine "fino a" lo decide il riferimento.
        flipButton->setEnabled(extent == 0 && sides != 1);
        operationBox->setEnabled(!surfaceBox->isChecked());
        const bool merge = !surfaceBox->isChecked() && operationBox->currentIndex() != 0;
        form->setRowVisible(autoBox, merge);
        form->setRowVisible(bodyRow, merge);
        bodyRow->setEnabled(!autoBox->isChecked());
        if (!merge || autoBox->isChecked()) picker.stop();
        QString text = definition.extentRef.kind >= 0 ? ForgeCad::geometryRefText(definition.extentRef, viewport->sketches(), viewport->extrusions())
                                                      : QStringLiteral("(da scegliere)");
        if (picking) text = QStringLiteral("▶ ") + text + QStringLiteral("  - clicca nella vista");
        refButton->setText(text);
        viewport->setReferenceMarks(extent != 0 && definition.extentRef.kind >= 0 ? QVector<GeometryRef>{definition.extentRef} : QVector<GeometryRef>());
        if (picking) setStatus(QStringLiteral("Clicca nella vista il riferimento (Esc nella vista: annulla la scelta)."), false);
        else if (picker.active()) setStatus(picker.hint(), false);
        else if (extent != 0 && definition.extentRef.kind < 0) setStatus(QStringLiteral("Scegli nella vista dove finisce l'estrusione."), false);
        else setStatus(QString(), false);
        ExtrusionObject d = current();
        if (extent == 0 && !picking && !picker.active()) {
            viewport->setFeatureHandle(d, [&](double value) {
                const QSignalBlocker blocker(distanceBox);
                distanceBox->setValue(value);
                refresh();
            });
        } else viewport->clearFeatureHandle();
        if (extent != 0 && d.extentRef.kind < 0) return;
        scope->request(viewport->withMergeCandidates(d, replaced >= 0 ? replaced : int(bodies.size())));
    };
    picker.changed = refresh;
    picker.message = setStatus;
    picker.starting = [&] { picking = false; viewport->clearFeatureHandle(); };
    const auto pick = [&] {
        picker.stop();
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
        if (picker.handle(picked, ref)) return;
        if (picked && picking) {
            if (ForgeCad::geometryRefRoles(ref, viewport->sketches()) & roles()) definition.extentRef = ref;
            else setStatus(QStringLiteral("Questo riferimento non va bene qui."), true);
        }
        picking = false;
        refresh();
    });
    QObject::connect(refButton, &QPushButton::clicked, &dialog, pick);
    QObject::connect(extentBox, &QComboBox::currentIndexChanged, &dialog, [&](int extent) {
        if (picking) viewport->cancelReferencePick();
        picking = false;
        // Il riferimento che non vale per la fine nuova si sceglie di nuovo.
        if (definition.extentRef.kind >= 0 && !(ForgeCad::geometryRefRoles(definition.extentRef, viewport->sketches()) & roles())) definition.extentRef = GeometryRef();
        refresh();
        if (extent != 0 && definition.extentRef.kind < 0) pick();
    });
    QObject::connect(operationBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(surfaceBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(autoBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(bodyList, &QListWidget::itemChanged, &dialog, [&] {
        if (!picker.handling()) refresh();
    });
    QObject::connect(sidesBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(flipButton, &QPushButton::clicked, &dialog, [&] {
        distanceBox->interpretText();
        distance2Box->interpretText();
        const double d = distanceBox->value();
        if (sidesBox->currentIndex() == 2) {
            const double back = distance2Box->value();
            distanceBox->setValue(d >= 0.0 ? -back : back);
            distance2Box->setValue(std::fabs(d));
        } else {
            distanceBox->setValue(-d);
        }
        refresh();
    });
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(distance2Box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    distanceBox->onReturn = refresh;
    distance2Box->onReturn = refresh;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        // Il valore scritto e non ancora confermato con Invio vale comunque.
        distanceBox->interpretText();
        distance2Box->interpretText();
        const ExtrusionObject d = current();
        if (d.extrudeSides == 2 && !(d.distance2 > 1e-7)) {
            setStatus(QStringLiteral("La distanza nel secondo verso deve essere positiva."), true);
            return;
        }
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
    viewport->clearFeatureHandle();
    scope.reset();
    return accepted;
}

bool transformDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                            const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Nella scena non ci sono corpi da spostare."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Transform;
    const bool preselected = candidates.contains(definition.firstBody);
    if (!preselected) definition.firstBody = candidates.last();
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("transform"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int index : candidates) bodyBox->addItem(logicalBodyLabel(viewport, index));
    bodyBox->setCurrentIndex(int(candidates.indexOf(definition.firstBody)));
    bodyBox->setEnabled(replaced < 0);
    BodyPicker picker(dialog, viewport, replaced, candidates, bodyBox);
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
    form->addRow(QStringLiteral("Corpo:"), picker.row(bodyBox));
    form->addRow(QStringLiteral("Traslazione:"), translation);
    form->addRow(QStringLiteral("Rotazione:"), angleBox);
    form->addRow(QStringLiteral("Asse:"), axisButton);
    form->addRow(QString(), copyBox);
    form->addRow(wrappedNote(QStringLiteral("Prima la rotazione attorno all'asse (verso destrorso), poi la traslazione."), &dialog));
    auto *status = new FunctionMessage(&dialog);
    status->setMinimumWidth(280);
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
        setStatus(picking ? QStringLiteral("Clicca nella vista la retta dell'asse (Esc nella vista: annulla la scelta).")
                          : picker.active() ? picker.hint() : QString(), false);
        if (picker.waiting()) {
            scope->label->setText(picker.hint());
            viewport->clearPreview();
            return;
        }
        scope->request(current());
    };
    picker.changed = refresh;
    picker.message = setStatus;
    picker.starting = [&] { picking = false; };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picker.handle(picked, ref)) return;
        if (picked && picking) {
            if (ForgeCad::geometryRefRoles(ref, viewport->sketches()) & ForgeCad::DatumRoleLine) definition.move.axis = ref;
            else setStatus(QStringLiteral("Serve una retta."), true);
        }
        picking = false;
        refresh();
    });
    QObject::connect(axisButton, &QPushButton::clicked, &dialog, [&] {
        picker.stop();
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
        if (picker.waiting()) return setStatus(picker.hint(), true);
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
        if (replaced < 0 && !preselected) picker.start(true);
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

QVector<ThreadChoice> threadChoices(int standard) {
    switch (standard) {
    case 0: return {{"M3 x 0,5",3,.5},{"M4 x 0,7",4,.7},{"M5 x 0,8",5,.8},{"M6 x 1",6,1},{"M8 x 1,25",8,1.25},{"M10 x 1,5",10,1.5},{"M12 x 1,75",12,1.75},{"M16 x 2",16,2},{"M20 x 2,5",20,2.5},{"M24 x 3",24,3},{"M30 x 3,5",30,3.5},{"M36 x 4",36,4}};
    case 1: return {{"1/4-20 UNC",6.35,25.4/20},{"5/16-18 UNC",7.9375,25.4/18},{"3/8-16 UNC",9.525,25.4/16},{"1/2-13 UNC",12.7,25.4/13},{"5/8-11 UNC",15.875,25.4/11},{"3/4-10 UNC",19.05,2.54},{"1-8 UNC",25.4,3.175}};
    case 2: return {{"1/4-28 UNF",6.35,25.4/28},{"5/16-24 UNF",7.9375,25.4/24},{"3/8-24 UNF",9.525,25.4/24},{"1/2-20 UNF",12.7,1.27},{"5/8-18 UNF",15.875,25.4/18},{"3/4-16 UNF",19.05,25.4/16},{"1-12 UNF",25.4,25.4/12}};
    case 3: case 4: return {{"G/R 1/8 - 28",9.728,25.4/28},{"G/R 1/4 - 19",13.157,25.4/19},{"G/R 3/8 - 19",16.662,25.4/19},{"G/R 1/2 - 14",20.955,25.4/14},{"G/R 3/4 - 14",26.441,25.4/14},{"G/R 1 - 11",33.249,25.4/11}};
    case 5: return {{"NPT 1/8 - 27",10.287,25.4/27},{"NPT 1/4 - 18",13.716,25.4/18},{"NPT 3/8 - 18",17.145,25.4/18},{"NPT 1/2 - 14",21.336,25.4/14},{"NPT 3/4 - 14",26.670,25.4/14},{"NPT 1 - 11,5",33.401,25.4/11.5}};
    case 6: return {{"Tr 8 x 1,5",8,1.5},{"Tr 10 x 2",10,2},{"Tr 12 x 3",12,3},{"Tr 16 x 4",16,4},{"Tr 20 x 4",20,4},{"Tr 24 x 5",24,5},{"Tr 30 x 6",30,6}};
    default: return {{"ACME 1/4-16",6.35,25.4/16},{"ACME 1/2-10",12.7,2.54},{"ACME 3/4-6",19.05,25.4/6},{"ACME 1-5",25.4,25.4/5},{"ACME 1 1/2-4",38.1,6.35}};
    }
}

bool threadDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
                         const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    if (initial.firstBody >= 0 && (replaced < 0 || initial.firstBody < replaced) && !candidates.contains(initial.firstBody)) candidates.append(initial.firstBody);
    std::sort(candidates.begin(), candidates.end());
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Nella scena non ci sono solidi da filettare."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Thread;
    int selected = candidates.indexOf(definition.firstBody);
    if (selected < 0) selected = candidates.size() - 1, definition.firstBody = candidates.at(selected);

    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("thread"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *surfaceDiagram = new ThreadDiagram(ThreadDiagram::Surface, &dialog);
    auto *profileDiagram = new ThreadDiagram(ThreadDiagram::Profile, &dialog);
    auto *extentDiagram = new ThreadDiagram(ThreadDiagram::Extent, &dialog);
    auto *bodyBox = new QComboBox(&dialog);
    for (int index : candidates) bodyBox->addItem(logicalBodyLabel(viewport, index));
    bodyBox->setCurrentIndex(selected);
    bodyBox->setEnabled(replaced < 0);
    auto *faceButton = new QPushButton(QStringLiteral("Scegli faccia nella vista…"), &dialog);
    faceButton->setCheckable(true);
    auto *faceInfoLabel = new QLabel(&dialog); faceInfoLabel->setWordWrap(true);
    auto *standardBox = new QComboBox(&dialog);
    standardBox->addItems({QStringLiteral("ISO metrico M (60°)"), QStringLiteral("Unified UNC (60°)"), QStringLiteral("Unified UNF (60°)"),
                           QStringLiteral("BSPP / G cilindrico (55°)"), QStringLiteral("BSPT / R conico (55°)"), QStringLiteral("NPT conico (60°)"),
                           QStringLiteral("ISO trapezoidale Tr (30°)"), QStringLiteral("ACME (29°)")});
    standardBox->setCurrentIndex(qBound(0, definition.thread.standard, 7));
    auto *sizeBox = new QComboBox(&dialog);
    auto *pitchBox = new ForgeCad::ExpressionSpinBox(&dialog);
    pitchBox->setDecimals(6); pitchBox->setRange(0.01, 100.0); pitchBox->setValue(definition.thread.pitch); pitchBox->setKeyboardTracking(false);
    auto *lengthBox = new ForgeCad::ExpressionSpinBox(&dialog);
    lengthBox->setDecimals(6); lengthBox->setRange(0.001, 1e6); lengthBox->setKeyboardTracking(false);
    auto *handBox = new QComboBox(&dialog); handBox->addItems({QStringLiteral("Destrorsa"), QStringLiteral("Sinistrorsa")}); handBox->setCurrentIndex(definition.thread.leftHanded ? 1 : 0);
    auto *reverseBox = new QCheckBox(QStringLiteral("Parti dall'altra estremita' della faccia"), &dialog); reverseBox->setChecked(definition.thread.reverse);
    auto *status = new FunctionMessage(&dialog); form->addRow(surfaceDiagram);
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Superficie:"), faceButton);
    form->addRow(faceInfoLabel);
    form->addRow(profileDiagram);
    form->addRow(QStringLiteral("Standard:"), standardBox);
    form->addRow(QStringLiteral("Misura nominale:"), sizeBox);
    form->addRow(QStringLiteral("Passo:"), pitchBox);
    form->addRow(extentDiagram);
    form->addRow(QStringLiteral("Lunghezza:"), lengthBox);
    form->addRow(QStringLiteral("Verso:"), handBox);
    form->addRow(reverseBox);
    form->addRow(wrappedNote(QStringLiteral("La geometria usa il profilo nominale. Tolleranze, giochi e maggiorazioni di lavorazione restano a carico del processo produttivo."), &dialog));
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); form->addRow(buttons);
    const PreviewScope scope(viewport, dialog, form, replaced);

    ForgeCad::ThreadFaceInfo faceInfo;
    bool faceValid = false, picking = false, initializing = true;
    const auto angle = [&] { const int s = standardBox->currentIndex(); return (s == 3 || s == 4) ? 55.0 : s == 6 ? 30.0 : s == 7 ? 29.0 : 60.0; };
    const auto setStatus = [status](const QString &text, bool error) { status->setStyleSheet(error ? QStringLiteral("color:#ff7a6a;") : QStringLiteral("color:#9fc6e8;")); status->setText(text); };
    const auto readFace = [&] {
        faceValid = false;
        const int body = candidates.at(bodyBox->currentIndex());
        const ForgeCad::ForgeBody &shape = viewport->extrusions().at(body).forgeBody;
        QString error;
        if (shape) faceValid = ForgeCad::forgeThreadFaceInfo(*shape, definition.thread.face, faceInfo, &error);
        if (faceValid) {
            faceInfoLabel->setText(QStringLiteral("%1 • diametro %2 • lunghezza disponibile %3%4")
                                   .arg(faceInfo.internal ? QStringLiteral("Foro interno") : QStringLiteral("Albero esterno"),
                                        ForgeCad::formatLength(faceInfo.diameter, 6, true, &dialog), ForgeCad::formatLength(faceInfo.length, 6, true, &dialog),
                                        faceInfo.conical ? QStringLiteral(" • conica") : QString()));
            lengthBox->setMaximum(qMax(0.001, faceInfo.length));
            if (initializing || lengthBox->value() > faceInfo.length) lengthBox->setValue(definition.thread.length > 0.0 ? qMin(definition.thread.length, faceInfo.length) : faceInfo.length);
        } else faceInfoLabel->setText(QStringLiteral("Nessuna faccia cilindrica o conica selezionata."));
        return error;
    };
    const auto populateSizes = [&] {
        const QVector<ThreadChoice> choices = threadChoices(standardBox->currentIndex());
        const QSignalBlocker blocker(sizeBox);
        sizeBox->clear();
        int best = 0, exact = -1; double delta = std::numeric_limits<double>::max();
        for (int k = 0; k < choices.size(); ++k) {
            const ThreadChoice &choice = choices.at(k);
            sizeBox->addItem(QString::fromLatin1(choice.name), choice.pitch);
            const double d = faceValid ? std::fabs(choice.diameter - faceInfo.diameter) : double(k);
            if (!definition.thread.designation.isEmpty() && QString::fromLatin1(choice.name) == definition.thread.designation) exact = k;
            if (d < delta) best = k, delta = d;
        }
        if (exact >= 0) best = exact;
        sizeBox->setCurrentIndex(best);
        if (initializing && definition.thread.pitch > 0.0) pitchBox->setValue(definition.thread.pitch);
        else if (!choices.isEmpty()) pitchBox->setValue(choices.at(best).pitch);
    };
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.firstBody = candidates.at(bodyBox->currentIndex());
        d.thread.standard = standardBox->currentIndex();
        d.thread.designation = sizeBox->currentText();
        d.thread.pitch = pitchBox->value();
        d.thread.length = lengthBox->value();
        d.thread.leftHanded = handBox->currentIndex() == 1;
        d.thread.reverse = reverseBox->isChecked();
        return d;
    };
    const auto refresh = [&] {
        const double a = angle(), p = pitchBox->value(), l = lengthBox->value();
        for (ThreadDiagram *diagram : {surfaceDiagram, profileDiagram, extentDiagram}) diagram->setValues(faceInfo.internal, a, p, l, handBox->currentIndex() == 1);
        faceButton->setChecked(picking);
        QVector<GeometryRef> marks;
        if (faceValid) marks.append(GeometryRef{5, candidates.at(bodyBox->currentIndex()), {}, definition.thread.face});
        viewport->setReferenceMarks(marks);
        if (picking) { setStatus(QStringLiteral("Clicca la faccia cilindrica o conica del foro o dell'albero; Esc annulla la scelta."), false); viewport->clearPreview(); return; }
        if (!faceValid) { setStatus(QStringLiteral("Seleziona una faccia cilindrica o conica."), true); viewport->clearPreview(); return; }
        setStatus(QString(), false);
        scope.request(current());
    };
    const auto beginPick = [&] {
        picking = true; refresh();
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
        if (!error.isEmpty()) { picking = false; setStatus(error, true); }
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        picking = false;
        if (picked && ref.kind == 5) {
            const int body = resultBodyForPick(viewport, candidates, ref.index);
            if (body >= 0) {
                bodyBox->setCurrentIndex(candidates.indexOf(body));
                definition.firstBody = body;
                definition.thread.face = ref.point;
                const QString error = readFace();
                populateSizes();
                if (!faceValid) setStatus(error, true);
            }
        }
        refresh();
    });
    QObject::connect(faceButton, &QPushButton::clicked, &dialog, [&] { if (picking) { picking = false; viewport->cancelReferencePick(); refresh(); } else beginPick(); });
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, [&] { definition.thread.face = {}; readFace(); populateSizes(); refresh(); });
    QObject::connect(standardBox, &QComboBox::currentIndexChanged, &dialog, [&] { definition.thread.designation.clear(); populateSizes(); refresh(); });
    QObject::connect(sizeBox, &QComboBox::currentIndexChanged, &dialog, [&] { if (sizeBox->currentIndex() >= 0) pitchBox->setValue(sizeBox->currentData().toDouble()); refresh(); });
    pitchBox->onReturn = refresh;
    lengthBox->onReturn = refresh;
    QObject::connect(handBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(reverseBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!faceValid) { setStatus(QStringLiteral("Seleziona prima una faccia cilindrica o conica."), true); return; }
        QApplication::setOverrideCursor(Qt::WaitCursor); const QString error = apply(current()); QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept(); else setStatus(error, true);
    });
    readFace(); populateSizes(); initializing = false; refresh();
    if (!faceValid && replaced < 0) QTimer::singleShot(0, &dialog, beginPick);
    bool accepted = false;
    { const WindowLock lock(window, viewport, &dialog); dialog.show(); refresh(); QEventLoop loop; QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit); loop.exec(); accepted = dialog.result() == QDialog::Accepted; }
    viewport->cancelReferencePick(); viewport->setReferencePickCallback(nullptr); viewport->setReferenceMarks({});
    return accepted;
}

bool booleanDialog(QMainWindow *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> indices = viewport->resultBodiesBefore(replaced);
    // In modifica gli operandi attuali restano scelti anche se non sono piu' l'ultimo stadio del loro corpo.
    for (int operand : QVector<int>{initial.firstBody, initial.secondBody} + initial.booleanTools)
        if (operand >= 0 && operand < viewport->extrusions().size() && (replaced < 0 || operand < replaced) && !indices.contains(operand))
            indices.append(operand);
    std::sort(indices.begin(), indices.end());
    if (indices.size() < 2) {
        QMessageBox::information(parent, title, QStringLiteral("Servono almeno due corpi nella scena (solidi, o un solido e una superficie)."));
        return false;
    }
    const auto label = [&](int index) {
        const bool used = index == initial.firstBody || index == initial.secondBody || initial.booleanTools.contains(index);
        return resultBodyLabel(viewport, index, !used);
    };
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("boolean"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
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
    toolList->setMinimumHeight(110);
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
    auto *pickFirst = new QPushButton(QStringLiteral("A dalla vista"), &dialog);
    auto *pickTools = new QPushButton(QStringLiteral("Strumenti dalla vista"), &dialog);
    for (QPushButton *button : {pickFirst, pickTools}) {
        button->setCheckable(true);
        button->setToolTip(QStringLiteral("Clicca i corpi nella vista (di nuovo uno strumento per toglierlo); Esc nella vista termina"));
    }
    auto *pickRow = new QWidget(&dialog);
    auto *pickLayout = new QHBoxLayout(pickRow);
    pickLayout->setContentsMargins(0, 0, 0, 0);
    pickLayout->addWidget(pickFirst);
    pickLayout->addWidget(pickTools);
    auto *status = new FunctionMessage(&dialog);
    form->addRow(QStringLiteral("Operazione:"), operationBox);
    form->addRow(QStringLiteral("Oggetto A:"), firstBox);
    form->addRow(QStringLiteral("Strumenti:"), toolList);
    form->addRow(QString(), pickRow);
    form->addRow(wrappedNote(QStringLiteral("Spunta uno o piu' strumenti: l'operazione li applica ad A uno dopo l'altro."), &dialog));
    form->addRow(status);
    const PreviewScope scope(viewport, dialog, form, replaced);
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
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
    // 0: nessuna scelta nella vista, 1: A, 2: strumenti. Durante la scelta
    // l'anteprima si toglie: nella vista restano i corpi da cliccare.
    int picking = 0;
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
        pickFirst->setChecked(picking == 1);
        pickTools->setChecked(picking == 2);
        const ExtrusionObject body = current();
        viewport->setPickBodies(picking ? indices : QVector<int>());
        if (picking) {
            scope.label->setText(QStringLiteral("scelta dei corpi nella vista"));
            viewport->clearPreview();
            return;
        }
        if (body.secondBody < 0) {
            scope.label->setText(QStringLiteral("scegli almeno uno strumento"));
            viewport->clearPreview();
            return;
        }
        scope.request(body);
    };
    const auto startPick = [&](int mode) {
        if (picking == mode) {
            picking = 0;
            viewport->cancelReferencePick();
            setStatus(QString(), false);
            refresh();
            return;
        }
        picking = mode;
        refresh();  // via l'anteprima prima di scegliere
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
        if (!error.isEmpty()) {
            picking = 0;
            setStatus(error, true);
        } else {
            setStatus(mode == 1 ? QStringLiteral("Clicca il corpo A nella vista.")
                                : QStringLiteral("Clicca gli strumenti nella vista (di nuovo per toglierli); Esc nella vista termina."), false);
        }
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (!picked || !picking) {
            picking = 0;
            setStatus(QString(), false);
            refresh();
            return;
        }
        const int body = resultBodyForPick(viewport, indices, ref.index);
        if (body < 0) {
            setStatus(QStringLiteral("Quel corpo non e' tra quelli disponibili in questo punto della storia."), true);
        } else if (picking == 1) {
            const QSignalBlocker blocker(firstBox);
            firstBox->setCurrentIndex(int(indices.indexOf(body)));
            picking = 2;  // poi gli strumenti
            setStatus(QStringLiteral("A: %1. Ora clicca gli strumenti; Esc nella vista termina.").arg(resultBodyLabel(viewport, body, false)), false);
        } else if (body == indices.at(firstBox->currentIndex())) {
            setStatus(QStringLiteral("Quel corpo e' A: non puo' essere anche uno strumento."), true);
        } else {
            for (int row = 0; row < toolList->count(); ++row) {
                QListWidgetItem *item = toolList->item(row);
                if (item->data(Qt::UserRole).toInt() != body) continue;
                const QSignalBlocker blocker(toolList);
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            }
            setStatus(QString(), false);
        }
        if (picking) {
            const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);  // si continua a scegliere
            if (!error.isEmpty()) picking = 0;
        }
        refresh();
    });
    QObject::connect(pickFirst, &QPushButton::clicked, &dialog, [&] { startPick(1); });
    // Di default la scelta nella vista e' attiva: A se manca, poi gli strumenti.
    if (replaced < 0) startPick(indices.contains(initial.firstBody) ? 2 : 1);
    QObject::connect(pickTools, &QPushButton::clicked, &dialog, [&] { startPick(2); });
    QObject::connect(operationBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(firstBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(toolList, &QListWidget::itemChanged, &dialog, refresh);
    refresh();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    const bool accepted = runUntilAppliedModeless(parent, viewport, dialog, form, buttons, [&] {
        if (picking) {
            picking = 0;
            viewport->cancelReferencePick();
            viewport->setPickBodies({});
            refresh();
        }
        const ExtrusionObject body = current();
        if (body.secondBody < 0) return QStringLiteral("Scegli almeno uno strumento.");
        return apply(body);
    });
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setPickBodies({});
    return accepted;
}

bool draftDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
                        const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int i) { return !viewport->extrusions().at(i).forgeBody; }), candidates.end());
    if (initial.firstBody >= 0 && initial.firstBody < viewport->extrusions().size()
        && (replaced < 0 || initial.firstBody < replaced) && !candidates.contains(initial.firstBody)) candidates.append(initial.firstBody);
    if (candidates.isEmpty()) { QMessageBox::information(window, title, QStringLiteral("Crea prima un solido o una superficie da sformare.")); return false; }
    ExtrusionObject definition = initial;
    definition.feature = BodyFeature::Draft;
    definition.operation = -1;
    if (!candidates.contains(definition.firstBody)) definition.firstBody = candidates.last();
    if (definition.draftNeutral.kind < 0) { definition.draftNeutral.kind = 1; definition.draftNeutral.index = 0; }
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("draft"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int i : candidates) bodyBox->addItem(logicalBodyLabel(viewport, i));
    bodyBox->setCurrentIndex(candidates.indexOf(definition.firstBody));
    bodyBox->setEnabled(replaced < 0);
    auto *faces = new QPushButton(QStringLiteral("Scegli facce"), &dialog); faces->setCheckable(true);
    auto *clear = new QPushButton(QStringLiteral("Azzera facce"), &dialog);
    auto *faceCount = new QLabel(&dialog);
    auto *neutralBox = new QComboBox(&dialog);
    neutralBox->addItems({QStringLiteral("XY"), QStringLiteral("XZ"), QStringLiteral("YZ"), QStringLiteral("Riferimento nella vista")});
    neutralBox->setCurrentIndex(definition.draftNeutral.kind == 1 ? qBound(0, definition.draftNeutral.index, 2) : 3);
    auto *neutralPick = new QPushButton(QStringLiteral("Scegli piano / faccia neutra"), &dialog); neutralPick->setCheckable(true);
    auto *neutralLabel = new QLabel(&dialog); neutralLabel->setWordWrap(true);
    auto *angle = new ForgeCad::ExpressionSpinBox(&dialog);
    angle->setDecimals(4); angle->setRange(-88.9999, 88.9999); angle->setSuffix(QStringLiteral("°"));
    angle->setValue(definition.draftAngle); angle->setKeyboardTracking(false);
    auto *reverse = new QCheckBox(QStringLiteral("Inverti direzione di estrazione"), &dialog); reverse->setChecked(definition.draftReverse);
    auto *status = new FunctionMessage(&dialog); form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Facce:"), faces); form->addRow(faceCount); form->addRow(clear);
    form->addRow(QStringLiteral("Piano neutro:"), neutralBox); form->addRow(neutralPick); form->addRow(neutralLabel);
    form->addRow(QStringLiteral("Angolo:"), angle); form->addRow(reverse);
    form->addRow(wrappedNote(QStringLiteral("Il piano neutro resta fisso. L'estrazione segue la sua normale: un angolo positivo restringe il corpo in quella direzione, uno negativo lo allarga. Facce piane anche su solidi concavi con superfici curve; facce cilindriche/coniche con asse parallelo all’estrazione. La rifilatura mantiene le adiacenze: collassi e cambi di topologia non risolvibili vengono segnalati. Sulle superfici aperte sono supportate facce piane e bordi rettilinei."), &dialog));
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); form->addRow(buttons);
    PreviewScope scope(viewport, dialog, form, replaced);
    int picking = 0;  // 1 facce, 2 piano neutro
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.draftAngle = angle->value(); d.draftReverse = reverse->isChecked();
        return d;
    };
    const auto refresh = [&] {
        faceCount->setText(QStringLiteral("%1 facce scelte").arg(definition.offsetFaces.size()));
        neutralLabel->setText(ForgeCad::geometryRefText(definition.draftNeutral, viewport->sketches(), viewport->extrusions()));
        faces->setChecked(picking == 1); neutralPick->setChecked(picking == 2);
        QVector<GeometryRef> marks;
        for (const EdgePoint &point : definition.offsetFaces) { GeometryRef r; r.kind = 5; r.index = definition.firstBody; r.point = point; marks.append(r); }
        if (definition.draftNeutral.kind >= 0) marks.append(definition.draftNeutral);
        viewport->setReferenceMarks(marks);
        if (picking || definition.offsetFaces.isEmpty() || definition.draftNeutral.kind < 0) {
            scope.label->setText(picking ? QStringLiteral("termina la scelta per vedere l'anteprima")
                                        : QStringLiteral("scegli le facce e il piano neutro"));
            viewport->clearPreview();
            return;
        }
        scope.request(current());
    };
    const auto beginPick = [&](int mode) {
        viewport->cancelReferencePick();
        picking = mode;
        const QString error = viewport->beginReferencePick(mode == 1 ? ForgeCad::DatumRoleFace : ForgeCad::DatumRolePlane, replaced);
        if (!error.isEmpty()) { picking = 0; status->setText(error); }
        else status->setText(mode == 1 ? QStringLiteral("Clicca le facce; un secondo clic le toglie. Esc termina la scelta.") : QStringLiteral("Clicca una faccia piana o un piano di costruzione."));
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        const int mode = picking;
        picking = 0;
        if (!picked) { status->clear(); refresh(); return; }
        if (mode == 2) {
            ForgeCad::ResolvedRef resolved;
            QString error;
            if (ForgeCad::resolveGeometryRef(ref, replaced < 0 ? viewport->extrusions().size() : replaced, viewport->sketches(), viewport->extrusions(), resolved, &error) && resolved.hasPlane) {
                definition.draftNeutral = ref;
                const QSignalBlocker blocker(neutralBox); neutralBox->setCurrentIndex(ref.kind == 1 ? ref.index : 3);
                status->clear();
            } else status->setText(error.isEmpty() ? QStringLiteral("Il riferimento deve essere piano.") : error);
        } else if (mode == 1) {
            const int body = ref.kind == 5 ? resultBodyForPick(viewport, candidates, ref.index) : -1;
            if (body != definition.firstBody) status->setText(QStringLiteral("Scegli una faccia del corpo indicato nel pannello."));
            else {
                const auto &shape = *viewport->extrusions().at(body).forgeBody;
                const auto face = ForgeCad::resolveFaceReference(shape, ref.point, 1e-6);
                int existing = -1;
                for (int i = 0; i < definition.offsetFaces.size(); ++i)
                    if (ForgeCad::resolveFaceReference(shape, definition.offsetFaces.at(i), 1e-6) == face) existing = i;
                if (face.valid()) {
                    if (existing >= 0) definition.offsetFaces.remove(existing); else definition.offsetFaces.append(ref.point);
                }
            }
            // Selezione continua: l'anteprima parte alla fine della scelta.
            picking = 1;
            const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
            if (!error.isEmpty()) { picking = 0; status->setText(error); }
        }
        refresh();
    });
    QObject::connect(faces, &QPushButton::clicked, &dialog, [&] {
        if (picking == 1) { viewport->cancelReferencePick(); picking = 0; status->clear(); refresh(); } else beginPick(1);
    });
    QObject::connect(neutralPick, &QPushButton::clicked, &dialog, [&] {
        if (picking == 2) { viewport->cancelReferencePick(); picking = 0; refresh(); } else beginPick(2);
    });
    QObject::connect(clear, &QPushButton::clicked, &dialog, [&] { definition.offsetFaces.clear(); refresh(); });
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        viewport->cancelReferencePick(); picking = 0; definition.firstBody = candidates.at(bodyBox->currentIndex()); definition.offsetFaces.clear(); refresh();
    });
    QObject::connect(neutralBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        if (neutralBox->currentIndex() == 3) { beginPick(2); return; }
        viewport->cancelReferencePick(); picking = 0;
        definition.draftNeutral = {}; definition.draftNeutral.kind = 1; definition.draftNeutral.index = neutralBox->currentIndex(); refresh();
    });
    QObject::connect(angle, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(reverse, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        viewport->cancelReferencePick(); picking = 0;
        if (definition.offsetFaces.isEmpty()) { status->setText(QStringLiteral("Scegli almeno una faccia.")); refresh(); return; }
        const QString error = apply(current());
        if (error.isEmpty()) dialog.accept(); else status->setText(error);
    });
    QTimer::singleShot(0, &dialog, [&] { if (definition.offsetFaces.isEmpty()) beginPick(1); else refresh(); });
    const bool accepted = runModeless(window, viewport, dialog);
    viewport->cancelReferencePick(); viewport->setReferencePickCallback(nullptr); viewport->setReferenceMarks({});
    return accepted;
}

bool revolutionDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                             bool fixedSketch, const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<SketchObject> &sketches = viewport->sketches();
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("revolution"), {QStringLiteral("Rivoluzione"), QStringLiteral("Modifica rivoluzione")});
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    form->addRow(new FeatureOperationDiagram(FeatureOperationDiagram::Revolution, &dialog));
    auto *sketchBox = new QComboBox(&dialog);
    for (const SketchObject &item : sketches) sketchBox->addItem(item.name);
    sketchBox->setCurrentIndex(qBound(0, initial.sketchIndex, int(sketches.size()) - 1));
    sketchBox->setEnabled(!fixedSketch);
    GeometryRef axisRef = initial.revolveAxis == kRevolveAxisReference ? initial.revolveAxisRef : GeometryRef();
    auto *axisBox = new QComboBox(&dialog);
    const auto fillAxes = [&, axisBox](int sketchIndex) {
        const QSignalBlocker blocker(axisBox);
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
        if (axisRef.kind >= 0)
            axisBox->addItem(QStringLiteral("Nella vista: ") + ForgeCad::geometryRefText(axisRef, sketches, bodies), kRevolveAxisReference);
    };
    fillAxes(sketchBox->currentIndex());
    {
        const int initialAxis = axisBox->findData(initial.revolveAxis);
        if (initialAxis >= 0 && (fixedSketch || initial.revolveAxis == kRevolveAxisReference)) axisBox->setCurrentIndex(initialAxis);
    }
    auto *pickButton = new QPushButton(QStringLiteral("Scegli l'asse nella vista"), &dialog);
    pickButton->setCheckable(true);
    pickButton->setToolTip(QStringLiteral("Clicca una retta: un segmento o una linea di costruzione dello schizzo, un asse del modello,\n"
                                          "uno spigolo rettilineo o una faccia cilindrica (il suo asse). Deve stare nel piano dello schizzo."));
    auto *angleBox = new ForgeCad::ExpressionSpinBox(&dialog);
    angleBox->setDecimals(6);
    angleBox->setRange(0.0, 360.0);
    angleBox->setValue(std::abs(initial.revolveAngle));
    angleBox->setSuffix(QStringLiteral(" °"));
    // Inverti il verso: pulsante a due stati (premuto = angolo negativo).
    auto *reverseBox = new QPushButton(ForgeCad::commandIcon(QStringLiteral("reverseDirection")), QStringLiteral("Inverti il verso"), &dialog);
    reverseBox->setCheckable(true);
    reverseBox->setToolTip(QStringLiteral("Ruota nel verso opposto attorno all'asse"));
    reverseBox->setChecked(initial.revolveAngle < 0.0);
    // Come nei raccordi: l'anteprima si calcola con Invio (o con le frecce), non a ogni carattere.
    angleBox->setKeyboardTracking(false);
    auto *operationBox = new QComboBox(&dialog);
    operationBox->addItems({QStringLiteral("Corpo nuovo"), QStringLiteral("Unisci ai solidi"), QStringLiteral("Sottrai dai solidi")});
    operationBox->setCurrentIndex(qBound(0, initial.mergeOperation, 2));
    auto *autoBox = new QCheckBox(QStringLiteral("Automatico: i solidi che hanno punti in comune con la rivoluzione"), &dialog);
    autoBox->setChecked(initial.mergeAuto);
    auto *bodyList = new QListWidget(&dialog);
    bodyList->setMinimumHeight(110);
    BodyPicker picker(dialog, viewport, replaced, fillMergeBodyList(bodyList, viewport, replaced, initial.mergeBodies), nullptr, bodyList);
    QWidget *bodyRow = picker.row(bodyList);
    auto *pickStatus = new FunctionMessage(&dialog);
    pickStatus->setStyleSheet(QStringLiteral("color: #8aa0b4;"));
    form->addRow(QStringLiteral("Schizzo:"), sketchBox);
    form->addRow(QStringLiteral("Asse:"), axisBox);
    form->addRow(QString(), pickButton);
    form->addRow(QString(), pickStatus);
    form->addRow(QStringLiteral("Angolo:"), angleBox);
    form->addRow(QString(), reverseBox);
    form->addRow(QStringLiteral("Risultato:"), operationBox);
    form->addRow(QString(), autoBox);
    form->addRow(QStringLiteral("Solidi:"), bodyRow);
    const PreviewScope scope(viewport, dialog, form, replaced);
    const auto current = [&] {
        ExtrusionObject d = initial;
        d.operation = -1;
        d.feature = BodyFeature::Revolution;
        d.sketchIndex = sketchBox->currentIndex();
        d.plane = sketches.value(d.sketchIndex).plane;
        d.revolveAxis = axisBox->currentIndex() >= 0 ? axisBox->currentData().toInt() : -1;
        d.revolveAxisRef = d.revolveAxis == kRevolveAxisReference ? axisRef : GeometryRef();
        d.revolveAngle = reverseBox->isChecked() ? -angleBox->value() : angleBox->value();
        d.mergeOperation = operationBox->currentIndex();
        d.mergeAuto = autoBox->isChecked();
        d.mergeBodies.clear();
        for (int row = 0; row < bodyList->count(); ++row)
            if (bodyList->item(row)->checkState() == Qt::Checked) d.mergeBodies.append(bodyList->item(row)->data(Qt::UserRole).toInt());
        return d;
    };
    // L'asse scelto, evidenziato nella vista.
    const auto markAxis = [&] {
        const int axis = axisBox->currentIndex() >= 0 ? axisBox->currentData().toInt() : -1;
        GeometryRef mark;
        if (axis >= 0) {
            mark.kind = 7;
            mark.index = sketchBox->currentIndex();
            mark.element = {0, axis, -1};
        } else if (axis == kRevolveAxisReference) {
            mark = axisRef;
        }
        viewport->setReferenceMarks(mark.kind >= 0 ? QVector<GeometryRef>{mark} : QVector<GeometryRef>());
    };
    std::function<void()> refresh;
    refresh = [&] {
        const bool merge = operationBox->currentIndex() != 0;
        form->setRowVisible(autoBox, merge);
        form->setRowVisible(bodyRow, merge);
        bodyRow->setEnabled(!autoBox->isChecked());
        if (!merge || autoBox->isChecked()) picker.stop();
        markAxis();
        if (axisBox->currentIndex() < 0) { viewport->clearFeatureHandle(); return; }
        if (!pickButton->isChecked() && !picker.active()) {
            viewport->setFeatureHandle(current(), [&](double value) {
                const QSignalBlocker angleBlocker(angleBox), reverseBlocker(reverseBox);
                angleBox->setValue(std::abs(value));
                reverseBox->setChecked(value < 0);
                refresh();
            });
        } else viewport->clearFeatureHandle();
        scope.request(viewport->withMergeCandidates(current(), replaced >= 0 ? replaced : int(bodies.size())));
    };
    picker.changed = refresh;
    picker.message = [&](const QString &text, bool) { pickStatus->setText(text); };
    picker.starting = [&] {
        viewport->clearFeatureHandle();
        const QSignalBlocker blocker(pickButton);
        pickButton->setChecked(false);
        pickStatus->setText(picker.hint());
    };
    const auto setPicking = [&](bool on) {
        const QSignalBlocker blocker(pickButton);
        pickButton->setChecked(on);
        if (on) { picker.stop(); viewport->clearFeatureHandle(); }
        if (!on) {
            viewport->cancelReferencePick();
            pickStatus->setText(QString());
            refresh();
            return;
        }
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleLine, replaced);
        if (!error.isEmpty()) {
            pickButton->setChecked(false);
            pickStatus->setText(error);
            return;
        }
        pickStatus->setText(QStringLiteral("Clicca l'asse nella vista (Esc nella vista: annulla la scelta; lontano dagli oggetti il trascinamento ruota la vista)."));
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picker.handle(picked, ref)) return;
        const QSignalBlocker blocker(pickButton);
        pickButton->setChecked(false);
        pickStatus->setText(QString());
        if (!picked) return;
        // Un segmento dello schizzo stesso: l'asse e' quel segmento (segue lo schizzo).
        if (ref.kind == 7 && ref.index == sketchBox->currentIndex() && ref.element.kind == 0) {
            const int row = axisBox->findData(ref.element.element);
            if (row >= 0) {
                axisBox->setCurrentIndex(row);
                return;
            }
        }
        if (!(ForgeCad::geometryRefRoles(ref, sketches) & ForgeCad::DatumRoleLine)) {
            pickStatus->setText(QStringLiteral("Questo riferimento non e' una retta."));
            return;
        }
        axisRef = ref;
        fillAxes(sketchBox->currentIndex());
        axisBox->setCurrentIndex(axisBox->findData(kRevolveAxisReference));
        refresh();
    });
    QObject::connect(pickButton, &QPushButton::toggled, &dialog, setPicking);
    QObject::connect(sketchBox, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        // Un asse dell'elenco vale per lo schizzo; quello della vista resta.
        fillAxes(index);
        if (axisRef.kind >= 0) axisBox->setCurrentIndex(axisBox->findData(kRevolveAxisReference));
        refresh();
    });
    QObject::connect(axisBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(angleBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    angleBox->onReturn = refresh;
    QObject::connect(reverseBox, &QPushButton::toggled, &dialog, refresh);
    QObject::connect(operationBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(autoBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(bodyList, &QListWidget::itemChanged, &dialog, [&] {
        if (!picker.handling()) refresh();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refresh();
    const auto run = [&] {
        angleBox->interpretText();  // il valore scritto vale anche senza Invio
        if (axisBox->currentIndex() < 0) return QStringLiteral("Scegli l'asse della rivoluzione.");
        const ExtrusionObject d = current();
        if (d.mergeOperation != 0 && !d.mergeAuto && d.mergeBodies.isEmpty()) return QStringLiteral("Spunta almeno un solido (o scegli Automatico).");
        return apply(d);
    };
    // La finestra non e' modale: la vista si puo' ruotare per vedere il verso e l'asse si sceglie cliccandolo.
    bool applied = false;
    if (auto *window = qobject_cast<QMainWindow *>(parent)) applied = runUntilAppliedModeless(window, viewport, dialog, form, buttons, run);
    else applied = runUntilApplied(dialog, form, buttons, run);
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback({});
    viewport->setReferenceMarks({});
    viewport->clearFeatureHandle();
    return applied;
}

QString primitiveTitle(PrimitiveKind kind) {
    switch (kind) {
    case PrimitiveKind::Box: return QStringLiteral("Parallelepipedo");
    case PrimitiveKind::Cylinder: return QStringLiteral("Cilindro");
    case PrimitiveKind::Sphere: return QStringLiteral("Sfera");
    case PrimitiveKind::Cone: return QStringLiteral("Cono");
    case PrimitiveKind::Torus: return QStringLiteral("Toro");
    }
    return {};
}

bool primitiveDialog(QWidget *parent, PrimitiveParameters &parameters,
                            const std::function<QString(const PrimitiveParameters &)> &apply ,
                            const PreviewSpec<const PrimitiveParameters &> &preview ) {
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
    dialog.setSizeIdentity(QStringLiteral("primitive-%1").arg(int(parameters.kind)));
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
