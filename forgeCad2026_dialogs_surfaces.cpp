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



bool sweepDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<SketchObject> &sketches = viewport->sketches();
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    const int limit = replaced >= 0 ? replaced : int(bodies.size());
    QVector<int> curves;
    for (int index = 0; index < limit; ++index)
        if (ForgeCad::isCurveBody(bodies.at(index)) && bodies.at(index).curve) curves.append(index);
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("sweep"), {QStringLiteral("sweepDialog")});
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    dialog.setObjectName(QStringLiteral("sweepDialog"));
    auto *form = dialog.createScrollableForm();
    form->addRow(new FeatureOperationDiagram(FeatureOperationDiagram::Sweep, &dialog));
    GeometryRef profileRef = initial.sweepProfileRef, pathRef = initial.sweepPathRef;
    auto *profileSource = new QComboBox(&dialog);
    profileSource->setObjectName(QStringLiteral("sweepProfileSource"));
    profileSource->addItems({QStringLiteral("Schizzo"), QStringLiteral("Dalla vista (bordo o curva estratta / 3D)")});
    profileSource->setCurrentIndex(replaced < 0 || profileRef.kind >= 0 || sketches.isEmpty() ? 1 : 0);
    auto *pickProfile = new QPushButton(QStringLiteral("Seleziona sezione nella vista"), &dialog);
    pickProfile->setObjectName(QStringLiteral("sweepPickProfile"));
    pickProfile->setCheckable(true);
    auto *profileStatus = new FunctionMessage(&dialog); auto *profileBox = new QComboBox(&dialog), *pathTypeBox = new QComboBox(&dialog), *pathSketchBox = new QComboBox(&dialog),
         *pathChainBox = new QComboBox(&dialog), *curveBox = new QComboBox(&dialog);
    for (const SketchObject &sketch : sketches) {
        profileBox->addItem(sketch.name);
        pathSketchBox->addItem(sketch.name);
    }
    for (int index : curves) curveBox->addItem(bodies.at(index).name);
    pathTypeBox->addItems({QStringLiteral("Schizzo (catena o contorno chiuso di entita' tangenti)"), QStringLiteral("Curva 3D (elica, estratta o schizzo 3D)"), QStringLiteral("Dalla vista (bordo di faccia o curva 3D)")});
    profileBox->setCurrentIndex(sketches.isEmpty() ? -1 : qBound(0, initial.sketchIndex, int(sketches.size()) - 1));
    int pathSketch = initial.pathSketch;
    if (pathSketch < 0 || pathSketch >= sketches.size()) pathSketch = profileBox->currentIndex() == 0 && sketches.size() > 1 ? 1 : 0;
    pathSketchBox->setCurrentIndex(pathSketch);
    pathTypeBox->setCurrentIndex(initial.sweepPath == 1 && !curves.isEmpty() ? 1 : (initial.sweepPath == 0 && sketches.size() > 1 ? 0 : (curves.isEmpty() ? 0 : 1)));
    if (replaced < 0 || pathRef.kind >= 0) pathTypeBox->setCurrentIndex(2);
    pathTypeBox->setObjectName(QStringLiteral("sweepPathSource"));
    curveBox->setCurrentIndex(qMax(0, int(curves.indexOf(initial.firstBody))));
    auto *modeBox = new QComboBox(&dialog);
    modeBox->addItems({QStringLiteral("Torsione minima (segue il percorso senza ruotare attorno)"),
                       QStringLiteral("Frenet (segue la curvatura: molle, filetti)"), QStringLiteral("Orientamento costante (il profilo trasla)")});
    modeBox->setCurrentIndex(qBound(0, initial.sweepMode, 2));
    auto *operationBox = new QComboBox(&dialog);
    operationBox->addItems({QStringLiteral("Corpo nuovo"), QStringLiteral("Unisci ai solidi"), QStringLiteral("Sottrai dai solidi")});
    operationBox->setCurrentIndex(qBound(0, initial.mergeOperation, 2));
    auto *surfaceBox = new QCheckBox(QStringLiteral("Solo superficie (senza coperchi)"), &dialog);
    surfaceBox->setChecked(initial.sweepSurface);
    surfaceBox->setToolTip(QStringLiteral("Una lamina: un profilo chiuso diventa un tubo aperto alle estremita', un profilo aperto una superficie; "
                                          "non si fonde con i solidi"));
    auto *autoBox = new QCheckBox(QStringLiteral("Automatico: i solidi che hanno punti in comune con la sweep"), &dialog);
    autoBox->setChecked(initial.mergeAuto);
    auto *bodyList = new QListWidget(&dialog);
    bodyList->setMinimumHeight(110);
    BodyPicker picker(dialog, viewport, replaced, fillMergeBodyList(bodyList, viewport, replaced, initial.mergeBodies), nullptr, bodyList);
    QWidget *bodyRow = picker.row(bodyList);
    auto *help = new QLabel(QStringLiteral("Il profilo resta dove e' disegnato e si muove con il percorso, che parte dal punto piu' vicino al profilo "
                                           "(di solito lo si disegna sul piano normale al percorso, all'inizio). I tratti del percorso devono essere tangenti."),
                            &dialog);
    help->setWordWrap(true);
    help->setMaximumWidth(360);
    help->setStyleSheet(QStringLiteral("color: #8aa0b4;"));
    auto *pickPath = new QPushButton(QStringLiteral("Seleziona percorso nella vista"), &dialog);
    auto *pathStatus = new FunctionMessage(&dialog); auto *pickStatus = new FunctionMessage(QStringLiteral("Puoi scegliere una singola catena cliccando una sua entità nella scena."), &dialog);
    pickStatus->setStyleSheet(QStringLiteral("color: #8aa0b4;"));
    pickPath->setObjectName(QStringLiteral("sweepPickPath"));
    pickPath->setCheckable(true);
    form->addRow(QStringLiteral("Sezione:"), profileSource);
    form->addRow(QStringLiteral("Schizzo della sezione:"), profileBox);
    form->addRow(QString(), pickProfile);
    form->addRow(QString(), profileStatus);
    form->addRow(QStringLiteral("Percorso:"), pathTypeBox);
    form->addRow(QStringLiteral("Schizzo del percorso:"), pathSketchBox);
    form->addRow(QStringLiteral("Catena nello schizzo:"), pathChainBox);
    form->addRow(QString(), pickPath);
    form->addRow(QString(), pathStatus);
    form->addRow(QString(), pickStatus);
    form->addRow(QStringLiteral("Curva del percorso:"), curveBox);
    form->addRow(QStringLiteral("Orientamento:"), modeBox);
    form->addRow(QString(), surfaceBox);
    form->addRow(QStringLiteral("Risultato:"), operationBox);
    form->addRow(QString(), autoBox);
    form->addRow(QStringLiteral("Solidi:"), bodyRow);
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
        d.sweepProfileRef = profileSource->currentIndex() == 1 ? profileRef : GeometryRef{};
        d.sweepPathRef = pathTypeBox->currentIndex() == 2 ? pathRef : GeometryRef{};
        d.sketchIndex = d.sweepProfileRef.kind >= 0 ? -1 : profileBox->currentIndex();
        d.plane = sketches.value(d.sketchIndex).plane;
        d.sweepPath = pathTypeBox->currentIndex() == 1 ? 1 : 0;
        d.pathSketch = pathSketchBox->currentIndex();
        d.pathSegments.clear(); d.pathCurves.clear();
        if (d.sweepPath == 0 && pathChainBox->currentIndex() >= 0 && pathChainBox->currentIndex() < pathComponents.size()) {
            d.pathSegments = pathComponents.at(pathChainBox->currentIndex()).segments;
            d.pathCurves = pathComponents.at(pathChainBox->currentIndex()).curves;
        }
        d.firstBody = d.sweepPathRef.kind >= 0 && ForgeCad::isBodyRef(d.sweepPathRef) ? d.sweepPathRef.index
            : d.sweepPath == 1 && !curves.isEmpty() ? curves.at(qMax(0, curveBox->currentIndex())) : -1;
        d.sweepMode = modeBox->currentIndex();
        d.sweepSurface = surfaceBox->isChecked();
        d.mergeOperation = d.sweepSurface ? 0 : operationBox->currentIndex();
        d.mergeAuto = autoBox->isChecked();
        d.mergeBodies.clear();
        for (int row = 0; row < bodyList->count() && !d.sweepSurface; ++row)
            if (bodyList->item(row)->checkState() == Qt::Checked)
                d.mergeBodies.append(bodyList->item(row)->data(Qt::UserRole).toInt());
        return d;
    };
    const auto refresh = [&] {
        profileBox->setEnabled(profileSource->currentIndex() == 0 && !sketches.isEmpty());
        profileStatus->setText(profileSource->currentIndex() == 1
            ? profileRef.kind >= 0 ? ForgeCad::geometryRefText(profileRef, sketches, bodies) : QStringLiteral("Scegli una sezione piana nella vista.")
            : sketches.value(profileBox->currentIndex()).name);
        form->setRowVisible(profileBox, profileSource->currentIndex() == 0);
        form->setRowVisible(pathSketchBox, pathTypeBox->currentIndex() == 0);
        form->setRowVisible(pathChainBox, pathTypeBox->currentIndex() == 0);
        form->setRowVisible(curveBox, pathTypeBox->currentIndex() == 1);
        pathSketchBox->setEnabled(pathTypeBox->currentIndex() == 0);
        pathChainBox->setEnabled(pathTypeBox->currentIndex() == 0);
        pickPath->setEnabled(true);
        curveBox->setEnabled(pathTypeBox->currentIndex() == 1 && !curves.isEmpty());
        pathStatus->setText(pathTypeBox->currentIndex() == 2
            ? pathRef.kind >= 0 ? ForgeCad::geometryRefText(pathRef, sketches, bodies) : QStringLiteral("Scegli il percorso nella vista.")
            : pathTypeBox->currentIndex() == 1 ? curveBox->currentText() : pathSketchBox->currentText() + QStringLiteral(" / ") + pathChainBox->currentText());
        QVector<GeometryRef> marks;
        const auto definition = current();
        if (definition.sweepProfileRef.kind >= 0) marks.append(definition.sweepProfileRef);
        if (definition.sweepPathRef.kind >= 0) marks.append(definition.sweepPathRef);
        viewport->setReferenceMarks(marks);
        // La superficie non si fonde con i solidi.
        operationBox->setEnabled(!surfaceBox->isChecked());
        const bool merge = !surfaceBox->isChecked() && operationBox->currentIndex() != 0;
        form->setRowVisible(autoBox, merge);
        form->setRowVisible(bodyRow, merge);
        bodyRow->setEnabled(!autoBox->isChecked());
        if (!merge || autoBox->isChecked()) picker.stop();
        scope.request(viewport->withMergeCandidates(current(), replaced >= 0 ? replaced : int(bodies.size())));
    };
    QObject::connect(pathSketchBox, &QComboBox::currentIndexChanged, &dialog, [&](int) { refillPaths(); refresh(); });
    for (QComboBox *box : {profileSource, profileBox, pathTypeBox, pathChainBox, curveBox, modeBox, operationBox})
        QObject::connect(box, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(autoBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(surfaceBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(bodyList, &QListWidget::itemChanged, &dialog, [&] {
        if (!picker.handling()) refresh();
    });
    picker.changed = refresh;
    picker.message = [&](const QString &text, bool) { scope.label->setText(text); };
    int graphicalTarget = 0;  // 1 sezione, 2 percorso
    const auto armPick = [&](int target) {
        picker.stop();
        viewport->cancelReferencePick();
        graphicalTarget = target;
        pickProfile->setChecked(target == 1); pickPath->setChecked(target == 2);
        const QString reason = viewport->beginReferencePick(ForgeCad::DatumRoleCurve | ForgeCad::DatumRoleLine, replaced);
        if (!reason.isEmpty()) { graphicalTarget = 0; pickProfile->setChecked(false); pickPath->setChecked(false); }
        pickStatus->setText(reason.isEmpty() ? target == 1
            ? QStringLiteral("1. Clicca la sezione: schizzo, bordo o curva piana. Poi scegli il percorso.")
            : QStringLiteral("2. Clicca il percorso: bordo di faccia, curva estratta, curva o entità di schizzo 3D.") : reason);
    };
    picker.starting = [&] { graphicalTarget = 0; pickProfile->setChecked(false); pickPath->setChecked(false); };
    QObject::connect(pickProfile, &QPushButton::clicked, &dialog, [&] { armPick(1); });
    QObject::connect(pickPath, &QPushButton::clicked, &dialog, [&] { armPick(2); });
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picker.handle(picked, ref)) return;
        if (!picked) {
            graphicalTarget = 0; pickProfile->setChecked(false); pickPath->setChecked(false);
            return;
        }
        if (!graphicalTarget) return;
        std::vector<ForgeCad::Kernel::PathSegment> pieces; QString reason;
        if (!ForgeCad::geometryRefPath(ref, limit, sketches, bodies, pieces, &reason)) {
            pickStatus->setText(reason);
            viewport->beginReferencePick(ForgeCad::DatumRoleCurve | ForgeCad::DatumRoleLine, replaced);
            return;
        }
        if (graphicalTarget == 1) {
            if (ref.kind == 7) {
                profileRef = {};
                profileBox->setCurrentIndex(ref.index); profileSource->setCurrentIndex(0);
            } else {
                profileRef = ref; profileSource->setCurrentIndex(1);
            }
            refresh(); armPick(2);
        } else {
            if (ref.kind == 7) {
                pathRef = {}; pathTypeBox->setCurrentIndex(0); pathSketchBox->setCurrentIndex(ref.index);
                for (int k = 0; k < pathComponents.size(); ++k)
                    if ((ref.element.kind == 0 ? pathComponents.at(k).segments : pathComponents.at(k).curves).contains(ref.element.element)) {
                        pathChainBox->setCurrentIndex(k); break;
                    }
            } else { pathRef = ref; pathTypeBox->setCurrentIndex(2); }
            graphicalTarget = 0; pickProfile->setChecked(false); pickPath->setChecked(false);
            pickStatus->setText(QStringLiteral("Sezione e percorso selezionati. Regola l'orientamento e verifica l'anteprima."));
            refresh();
        }
    });
    QObject::connect(&dialog, &QDialog::finished, viewport, [viewport] {
        viewport->cancelReferencePick(); viewport->setReferencePickCallback({}); viewport->setReferenceMarks({});
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    refresh();
    armPick(1);
    const auto validate = [&] {
        const ExtrusionObject d = current();
        if (d.sweepProfileRef.kind < 0 && (profileSource->currentIndex() == 1 || d.sketchIndex < 0)) return QStringLiteral("Seleziona la sezione nella vista o scegli uno schizzo.");
        if (pathTypeBox->currentIndex() == 2 && d.sweepPathRef.kind < 0) return QStringLiteral("Seleziona il percorso nella vista.");
        if (d.sweepPathRef.kind < 0 && d.sweepPath == 1 && d.firstBody < 0) return QStringLiteral("Nella scena non ci sono curve 3D da usare come percorso.");
        if (d.sweepPathRef.kind < 0 && d.sweepPath == 0 && d.pathSegments.isEmpty() && d.pathCurves.isEmpty()) return QStringLiteral("Seleziona una catena valida nello schizzo del percorso.");
        if (d.sweepPathRef.kind < 0 && d.sweepProfileRef.kind < 0 && d.sweepPath == 0 && d.pathSketch == d.sketchIndex) return QStringLiteral("Profilo e percorso devono stare in schizzi diversi.");
        if (d.mergeOperation != 0 && !d.mergeAuto && d.mergeBodies.isEmpty())
            return QStringLiteral("Spunta almeno un solido (o scegli Automatico).");
        return apply(d);
    };
    if (auto *window = qobject_cast<QMainWindow *>(parent)) return runUntilAppliedModeless(window, viewport, dialog, form, buttons, validate);
    return runUntilApplied(dialog, form, buttons, validate);
}

bool loftDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<SketchObject> &sketches = viewport->sketches();
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("loft"), {QStringLiteral("loftDialog")});
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
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
    list->setObjectName(QStringLiteral("loftSections"));
    QVector<GeometryRef> sectionRefs = initial.loftSections;
    QVector<GeometryRef> edgeGuides = initial.loftEdgeGuides;
    connectReferenceList(list, dialog, viewport, [&](QListWidgetItem *item) {
        const int pos = item->data(Qt::UserRole + 1).toInt();
        return pos >= 0 && pos < sectionRefs.size() ? QVector<GeometryRef>{sectionRefs.at(pos)} : QVector<GeometryRef>();
    });
    const auto sameRef = [](const GeometryRef &a,const GeometryRef &b) {
        if(a.kind!=b.kind || a.index!=b.index || a.element!=b.element) return false;
        if(a.kind!=4 && a.kind!=9) return true;
        if(a.point.subshape>=0 || b.point.subshape>=0) return a.point.subshape==b.point.subshape;
        return std::hypot(a.point.x-b.point.x,std::hypot(a.point.y-b.point.y,a.point.z-b.point.z))<1e-9;
    };
    const auto sectionText = [&](const GeometryRef &ref) {
        if(ref.kind==7 && ref.element.kind==-1) return sketches.value(ref.index).name;
        return ForgeCad::geometryRefText(ref,sketches,viewport->extrusions());
    };
    const auto appendSection = [&](const GeometryRef &ref,bool checked) {
        int pos=-1;
        for(int k=0;k<sectionRefs.size();++k) if(sameRef(sectionRefs.at(k),ref)) {pos=k;break;}
        if(pos<0) {pos=sectionRefs.size();sectionRefs.append(ref);}
        auto *item=new QListWidgetItem(sectionText(ref),list);
        item->setData(Qt::UserRole,ref.kind==7 && ref.element.kind==-1?ref.index:-1);
        item->setData(Qt::UserRole+1,pos);
        item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
        item->setCheckState(checked?Qt::Checked:Qt::Unchecked);
        return item;
    };
    for(const GeometryRef &ref : initial.loftSections) appendSection(ref,true);
    QVector<int> order = initial.loftSketches;
    for (int index = 0; index < sketches.size(); ++index)
        if (!order.contains(index)) order.append(index);
    for (int index : order) {
        if (index < 0 || index >= sketches.size()) continue;
        GeometryRef ref; ref.kind=7; ref.index=index; ref.element={-1,-1,-1};
        bool present=false;
        for(const auto &existing: initial.loftSections) if(sameRef(existing,ref)) present=true;
        if(!present) appendSection(ref,initial.loftSections.isEmpty() && initial.loftSketches.contains(index));
    }
    auto *removeSection=new QPushButton(QStringLiteral("Rimuovi sezione"),&dialog);
    removeSection->setObjectName(QStringLiteral("loftRemoveSection"));
    auto *moveRow = new QWidget(&dialog);
    auto *moveLayout = new QHBoxLayout(moveRow);
    moveLayout->setContentsMargins(0, 0, 0, 0);
    auto *up = new QPushButton(QStringLiteral("↑ Su"), moveRow), *down = new QPushButton(QStringLiteral("↓ Giu'"), moveRow);
    auto *pickSections = new QPushButton(QStringLiteral("Scegli sezioni nella vista"), moveRow);
    moveLayout->addWidget(up);
    moveLayout->addWidget(down);
    moveLayout->addWidget(pickSections);
    moveLayout->addWidget(removeSection);
    pickSections->setObjectName(QStringLiteral("loftPickSections"));
    moveLayout->addStretch(1);
    auto *ruledBox = new QCheckBox(QStringLiteral("Rigato (superfici rigate tra sezioni consecutive)"), &dialog);
    ruledBox->setChecked(initial.loftRuled);
    auto *surfaceBox = new QCheckBox(QStringLiteral("Solo superficie (senza coperchi)"), &dialog);
    surfaceBox->setChecked(initial.loftSurface);
    surfaceBox->setToolTip(QStringLiteral("Una lamina: con sezioni chiuse il tubo che passa per le sezioni, aperto alle estremita'"));
    auto *guides = new QListWidget(&dialog);
    guides->setObjectName(QStringLiteral("loftGuides"));
    guides->setMinimumHeight(160);
    QVector<SketchPathRef> partialSelections = initial.loftGuidePaths;
    connectReferenceList(guides, dialog, viewport, [&](QListWidgetItem *item) {
        const int kind = item->data(Qt::UserRole).toInt(), index = item->data(Qt::UserRole + 1).toInt();
        if (kind == 2 && index >= 0 && index < edgeGuides.size()) return QVector<GeometryRef>{edgeGuides.at(index)};
        QVector<GeometryRef> refs;
        if (kind == 0 && index >= 0 && index < sketches.size()) {
            GeometryRef ref; ref.kind = 7; ref.index = index; ref.element = {-1, -1, -1}; refs.append(ref);
        }
        if (kind == 1 && index >= 0 && index < partialSelections.size()) {
            const auto &path = partialSelections.at(index);
            for (int segment : path.segments) { GeometryRef ref; ref.kind = 7; ref.index = path.sketch; ref.element = {0, segment, -1}; refs.append(ref); }
            for (int curve : path.curves) { GeometryRef ref; ref.kind = 7; ref.index = path.sketch; ref.element = {1, curve, -1}; refs.append(ref); }
        }
        return refs;
    });
    const auto appendGuide = [&](int kind, int index, bool checked) {
        const QSignalBlocker block(guides);
        QString text;
        if (kind == 0) text = sketches.value(index).name;
        else if (kind == 1) {
            const auto &path = partialSelections.at(index);
            text = QStringLiteral("%1 — selezione parziale (%2 entità)")
                .arg(sketches.value(path.sketch).name).arg(path.segments.size() + path.curves.size());
        } else text = ForgeCad::geometryRefText(edgeGuides.at(index), sketches, viewport->extrusions());
        auto *item = new QListWidgetItem(text, guides);
        item->setData(Qt::UserRole, kind);
        item->setData(Qt::UserRole + 1, index);
        item->setData(Qt::UserRole + 3, initial.loftGuideContinuity);
        item->setData(Qt::UserRole + 4, initial.loftGuideInfluence);
        item->setData(Qt::UserRole + 5, text);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        item->setText(QStringLiteral("%1 — G%2 / %3 %").arg(text).arg(initial.loftGuideContinuity)
                      .arg(100 * initial.loftGuideInfluence, 0, 'g', 3));
        return item;
    };
    const auto guideLabel = [&](QListWidgetItem *item) {
        const QSignalBlocker blocker(guides);
        item->setText(QStringLiteral("%1 — G%2 / %3 %").arg(item->data(Qt::UserRole + 5).toString())
                      .arg(item->data(Qt::UserRole + 3).toInt()).arg(100 * item->data(Qt::UserRole + 4).toDouble(), 0, 'g', 3));
    };
    for (int index : ForgeCad::loftGuideSequence(initial)) {
        QListWidgetItem *item;
        if (index < initial.loftGuides.size()) item = appendGuide(0, initial.loftGuides.at(index), true);
        else if (index < initial.loftGuides.size() + partialSelections.size())
            item = appendGuide(1, index - initial.loftGuides.size(), true);
        else item = appendGuide(2, index - initial.loftGuides.size() - partialSelections.size(), true);
        item->setData(Qt::UserRole + 3, initial.loftGuideContinuities.value(index, initial.loftGuideContinuity));
        item->setData(Qt::UserRole + 4, initial.loftGuideInfluences.value(index, initial.loftGuideInfluence));
        guideLabel(item);
    }
    for (int index = 0; index < sketches.size(); ++index)
        if (!initial.loftGuides.contains(index)) appendGuide(0, index, false);
    auto *guideButtons = new QWidget(&dialog);
    auto *guideButtonsLayout = new QHBoxLayout(guideButtons);
    guideButtonsLayout->setContentsMargins(0,0,0,0);
    auto *guideUp = new QPushButton(QStringLiteral("↑ Su"), guideButtons);
    auto *guideDown = new QPushButton(QStringLiteral("↓ Giu'"), guideButtons);
    auto *removeGuide = new QPushButton(QStringLiteral("Rimuovi guida"), guideButtons);
    guideUp->setObjectName(QStringLiteral("loftGuideUp"));
    guideDown->setObjectName(QStringLiteral("loftGuideDown"));
    removeGuide->setObjectName(QStringLiteral("loftRemoveGuide"));
    guideButtonsLayout->addWidget(guideUp); guideButtonsLayout->addWidget(guideDown);
    guideButtonsLayout->addWidget(removeGuide); guideButtonsLayout->addStretch(1);
    auto *pickGuides = new QPushButton(QStringLiteral("Scegli guide nella vista"), &dialog);
    pickGuides->setObjectName(QStringLiteral("loftPickGuides"));
    auto *pickStatus = new FunctionMessage(QStringLiteral("Spunta gli schizzi interi oppure scegli bordi e curve nella vista; un secondo clic li toglie."), &dialog);
    const auto influence = [&](double value) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setRange(0.0, 100.0);
        spin->setSuffix(QStringLiteral(" %"));
        spin->setDecimals(0);
        spin->setValue(100.0 * value);
        return spin;
    };
    QDoubleSpinBox *guideInfluence = influence(initial.loftGuideInfluence);
    guideInfluence->setObjectName(QStringLiteral("loftGuideInfluence"));
    auto *guideContinuity = new QComboBox(&dialog);
    guideContinuity->setObjectName(QStringLiteral("loftGuideContinuity"));
    guideContinuity->addItems({QStringLiteral("G0 — solo attraversamento"), QStringLiteral("G1 — segue la tangente (consigliato)"),
                               QStringLiteral("G2 — segue tangente e curvatura")});
    guideContinuity->setCurrentIndex(qBound(0, initial.loftGuideContinuity, 2));
    auto *help = new QLabel(QStringLiteral("Le sezioni vanno ordinate. Ogni guida deve essere una catena aperta e attraversare una sola volta ogni sezione; "
                                           "non può essere usata anche come sezione. Seleziona una guida nell'elenco per regolarne continuita' e influenza. Per le guide, G1 segue la tangente senza trasferire la curvatura che può creare flessi; G2 la trasferisce."),
                            &dialog);
    help->setWordWrap(true);
    help->setMaximumWidth(570);
    form->addRow(new LoftSelectionDiagram(&dialog));
    form->addRow(QStringLiteral("Sezioni:"), list);
    form->addRow(QString(), moveRow);
    form->addRow(QStringLiteral("Curve guida:"), guides);
    form->addRow(QString(), pickGuides);
    form->addRow(QString(), guideButtons);
    form->addRow(QString(), pickStatus);
    form->addRow(QStringLiteral("Continuita' guida selezionata:"), guideContinuity);
    form->addRow(QStringLiteral("Influenza guida selezionata:"), guideInfluence);
    ExtrusionObject endpointDefinition = initial;
    SurfaceEndControls endControls(dialog, *form, viewport, replaced, endpointDefinition);
    form->addRow(QString(), ruledBox);
    form->addRow(QString(), surfaceBox);
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
        d.loftSections.clear();
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->checkState() == Qt::Checked) {
                const GeometryRef &ref=sectionRefs.at(list->item(row)->data(Qt::UserRole+1).toInt());
                d.loftSections.append(ref);
                if(ref.kind==7 && ref.element.kind==-1) d.loftSketches.append(ref.index);
            }
        d.loftEdgeGuides.clear(); d.loftGuides.clear(); d.loftGuidePaths.clear(); d.loftGuideOrder.clear();
        QVector<QPair<int,int>> selected;
        QVector<int> modes; QVector<double> influences;
        for (int row = 0; row < guides->count(); ++row) {
            const auto *item = guides->item(row);
            if (item->checkState() != Qt::Checked) continue;
            modes.append(item->data(Qt::UserRole + 3).toInt());
            influences.append(item->data(Qt::UserRole + 4).toDouble());
            const int kind = item->data(Qt::UserRole).toInt(), index = item->data(Qt::UserRole+1).toInt();
            if (kind == 0) { selected.append({kind,d.loftGuides.size()}); d.loftGuides.append(index); }
            else if (kind == 1) { selected.append({kind,d.loftGuidePaths.size()}); d.loftGuidePaths.append(partialSelections.at(index)); }
            else { selected.append({kind,d.loftEdgeGuides.size()}); d.loftEdgeGuides.append(edgeGuides.at(index)); }
        }
        d.loftGuideContinuities.resize(selected.size()); d.loftGuideInfluences.resize(selected.size());
        for (int i = 0; i < selected.size(); ++i) {
            const auto &entry = selected[i];
            const int index = entry.second + (entry.first > 0 ? d.loftGuides.size() : 0)
                + (entry.first > 1 ? d.loftGuidePaths.size() : 0);
            d.loftGuideOrder.append(index);
            d.loftGuideContinuities[index] = modes[i]; d.loftGuideInfluences[index] = influences[i];
        }
        d.loftRuled = ruledBox->isChecked();
        d.loftSurface = surfaceBox->isChecked();
        endControls.copyTo(d);
        d.loftGuideContinuity = initial.loftGuideContinuity;
        d.loftGuideInfluence = initial.loftGuideInfluence;
        d.sketchIndex = d.loftSketches.value(0, -1);
        d.plane = sketches.value(d.sketchIndex).plane;
        return d;
    };
    const auto refresh = [&] {
        const ExtrusionObject d = current();
        if (d.loftSections.size() >= 2) scope.request(d);
        else viewport->clearPreview();
    };
    const auto syncGuideControls = [&] {
        const auto *item = guides->currentItem();
        const QSignalBlocker blockMode(guideContinuity), blockInfluence(guideInfluence);
        const bool editable = item && item->checkState() == Qt::Checked && !ruledBox->isChecked();
        guideContinuity->setEnabled(editable); guideInfluence->setEnabled(editable);
        if (item) {
            guideContinuity->setCurrentIndex(item->data(Qt::UserRole + 3).toInt());
            guideInfluence->setValue(100 * item->data(Qt::UserRole + 4).toDouble());
        }
    };
    const auto changeGuideCondition = [&] {
        if (auto *item = guides->currentItem()) {
            { const QSignalBlocker block(guides);
              item->setData(Qt::UserRole + 3, guideContinuity->currentIndex());
              item->setData(Qt::UserRole + 4, guideInfluence->value() / 100.0); }
            guideLabel(item); refresh();
        }
    };
    QObject::connect(guides, &QListWidget::currentRowChanged, &dialog, syncGuideControls);
    QObject::connect(guides, &QListWidget::itemChanged, &dialog, syncGuideControls);
    QObject::connect(ruledBox, &QCheckBox::toggled, &dialog, syncGuideControls);
    if (guides->count()) guides->setCurrentRow(0);
    syncGuideControls();
    endControls.changed = refresh;
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
    const auto moveGuide = [&](int step) {
        const int row = guides->currentRow();
        if (row < 0 || row + step < 0 || row + step >= guides->count()) return;
        auto *item = guides->takeItem(row);
        guides->insertItem(row + step, item); guides->setCurrentRow(row + step); refresh();
    };
    QObject::connect(guideUp, &QPushButton::clicked, &dialog, [&] { moveGuide(-1); });
    QObject::connect(guideDown, &QPushButton::clicked, &dialog, [&] { moveGuide(1); });
    QObject::connect(removeGuide, &QPushButton::clicked, &dialog, [&] {
        if (guides->currentItem()) guides->currentItem()->setCheckState(Qt::Unchecked);
    });
    QObject::connect(removeSection,&QPushButton::clicked,&dialog,[&] {
        if(list->currentRow()<0) return;
        delete list->takeItem(list->currentRow()); refresh();
    });
    int graphicalTarget = 0;  // 1 sezioni, 2 guide
    const auto armGraphicalPick = [&](int target) {
        endControls.stop();
        graphicalTarget = target;
        pickSections->setChecked(target == 1); pickGuides->setChecked(target == 2);
        viewport->cancelReferencePick();
        viewport->setReferencePickCallback([&](bool picked,GeometryRef ref) {
            if(!picked) {graphicalTarget=0;pickSections->setChecked(false);pickGuides->setChecked(false);return;}
            std::vector<ForgeCad::Kernel::PathSegment> path; QString reason;
            const int owner=replaced<0?viewport->extrusions().size():replaced;
            if(!ForgeCad::geometryRefPath(ref,owner,sketches,viewport->extrusions(),path,&reason)) {
                pickStatus->setText(reason);
            } else if(graphicalTarget==1) {
                QListWidgetItem *item=nullptr;
                for(int row=0;row<list->count();++row)
                    if(sameRef(sectionRefs.at(list->item(row)->data(Qt::UserRole+1).toInt()),ref)) {item=list->item(row);break;}
                if(item) item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);
                else item=appendSection(ref,true);
                list->setCurrentItem(item);
                refresh();
            } else if(graphicalTarget==2) {
                int existing=-1;
                for(int k=0;k<edgeGuides.size();++k) if(sameRef(edgeGuides.at(k),ref)) {existing=k;break;}
                if(existing<0) { existing=edgeGuides.size(); edgeGuides.append(ref); }
                QListWidgetItem *item=nullptr;
                for(int row=0;row<guides->count();++row)
                    if(guides->item(row)->data(Qt::UserRole).toInt()==2 && guides->item(row)->data(Qt::UserRole+1).toInt()==existing) {item=guides->item(row);break;}
                if(item) item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);
                else item=appendGuide(2,existing,true);
                guides->setCurrentItem(item); refresh();
            }
            if(graphicalTarget) viewport->beginReferencePick(ForgeCad::DatumRoleCurve|ForgeCad::DatumRoleLine,replaced);
        });
        const QString reason=viewport->beginReferencePick(ForgeCad::DatumRoleCurve|ForgeCad::DatumRoleLine,replaced);
        pickStatus->setText(reason.isEmpty()?QStringLiteral("Clicca un bordo o una curva; Esc termina la scelta. Gli schizzi interi si spuntano nell’elenco."):reason);
    };
    for(const QString &name : {QStringLiteral("surfaceStartFacesPick"),QStringLiteral("surfaceEndFacesPick")})
        QObject::connect(dialog.findChild<QPushButton *>(name),&QPushButton::clicked,&dialog,[&] {
            graphicalTarget=0; pickSections->setChecked(false);pickGuides->setChecked(false);
        });
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
            int existing = partialSelections.indexOf(path);
            if (existing < 0) { existing=partialSelections.size(); partialSelections.append(path); }
            QListWidgetItem *item=nullptr;
            for (int row=0;row<guides->count();++row) {
                auto *candidate=guides->item(row);
                if(candidate->data(Qt::UserRole).toInt()==1 && candidate->data(Qt::UserRole+1).toInt()==existing) item=candidate;
            }
            if(item) item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);
            else item=appendGuide(1,existing,true);
            if(item->checkState()==Qt::Checked)
                for(int row=0;row<guides->count();++row)
                    if(guides->item(row)->data(Qt::UserRole).toInt()==0 && guides->item(row)->data(Qt::UserRole+1).toInt()==sketch)
                        guides->item(row)->setCheckState(Qt::Unchecked);
            guides->setCurrentItem(item); refresh();
            pickStatus->setText(QStringLiteral("Elenco delle guide aggiornato."));
            break;
        }
    });
    QObject::connect(&dialog, &QDialog::finished, viewport, [viewport] {
        viewport->cancelReferencePick(); viewport->setReferencePickCallback({});
        viewport->setReferenceMarks({});
        viewport->setSketchPickCallback({}); viewport->setSketchEntityPickCallback({});
    });
    QObject::connect(list, &QListWidget::itemChanged, &dialog, refresh);
    QObject::connect(guides, &QListWidget::itemChanged, &dialog, refresh);
    QObject::connect(ruledBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(surfaceBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(guideContinuity, &QComboBox::currentIndexChanged, &dialog, changeGuideCondition);
    QObject::connect(guideInfluence, &QDoubleSpinBox::valueChanged, &dialog, changeGuideCondition);
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
        if (d.loftSections.size() < 2) return QStringLiteral("Scegli almeno due sezioni (schizzi o bordi).");
        for(const auto &section : d.loftSections)
            for(const auto &guide : d.loftEdgeGuides)
                if(sameRef(section,guide) || (section.kind==7 && section.element.kind==-1 && guide.kind==7 && section.index==guide.index))
                    return QStringLiteral("Una curva non può essere insieme sezione e guida.");
        for (int sketch : d.loftGuides)
            if (d.loftSketches.contains(sketch))
                return QStringLiteral("Uno schizzo non può essere insieme sezione e curva guida.");
        for (const SketchPathRef &path : d.loftGuidePaths)
            if (d.loftSketches.contains(path.sketch)) return QStringLiteral("Uno schizzo non può essere insieme sezione e curva guida.");
        return apply(d);
    };
    if (auto *window = qobject_cast<QMainWindow *>(parent)) return runUntilAppliedModeless(window, viewport, dialog, form, buttons, validate);
    return runUntilApplied(dialog, form, buttons, validate);
}

bool projectionDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
                             const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int i) { return !viewport->extrusions().at(i).forgeBody; }), candidates.end());
    if (initial.firstBody >= 0 && initial.firstBody < viewport->extrusions().size() && viewport->extrusions().at(initial.firstBody).forgeBody
        && (replaced < 0 || initial.firstBody < replaced) && !candidates.contains(initial.firstBody))
        candidates.append(initial.firstBody);
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Crea prima un solido o una superficie su cui proiettare."));
        return false;
    }
    if (viewport->sketches().isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Crea prima lo schizzo da proiettare."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::ProjectedCurve;
    if (!candidates.contains(definition.firstBody)) definition.firstBody = candidates.last();
    if (definition.sketchIndex < 0 || definition.sketchIndex >= viewport->sketches().size()) definition.sketchIndex = viewport->sketches().size() - 1;
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("projection"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int i : candidates) bodyBox->addItem(logicalBodyLabel(viewport, i));
    bodyBox->setCurrentIndex(candidates.indexOf(definition.firstBody));
    auto *sketchBox = new QComboBox(&dialog);
    for (const SketchObject &sketch : viewport->sketches()) sketchBox->addItem(sketch.name);
    sketchBox->setCurrentIndex(definition.sketchIndex);
    auto *reverse = new QCheckBox(QStringLiteral("Inverti il verso della proiezione"), &dialog);
    reverse->setChecked(definition.projectionReverse);
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Schizzo:"), sketchBox);
    form->addRow(reverse);
    form->addRow(wrappedNote(QStringLiteral("Ogni entita' dello schizzo (escluse quelle di costruzione) va lungo la normale del suo piano sulla "
                                            "prima faccia del corpo che incontra: la curva sta sulla superficie esatta entro 1e-8 mm. Le entita' "
                                            "collegate danno una sola curva per catena. Di default si proietta verso il corpo. Per tagliare una "
                                            "superficie con lo schizzo proiettato: Taglia superficie, strumento lo schizzo."), &dialog));
    auto *result = new FunctionMessage(&dialog);
    result->setStyleSheet(QStringLiteral("color: #9fc6e8;"));
    form->addRow(QStringLiteral("Esito:"), result);
    auto *status = new FunctionMessage(&dialog);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    PreviewScope scope(viewport, dialog, form, replaced);
    FunctionMessage *previewLabel = scope.label;
    viewport->setPreviewCallback([previewLabel, result, viewport](const QString &error) {
        previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
        result->setText(error.isEmpty() ? viewport->previewNotice() : QString());
    });
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.firstBody = candidates.at(bodyBox->currentIndex());
        d.sketchIndex = sketchBox->currentIndex();
        d.projectionReverse = reverse->isChecked();
        return d;
    };
    const auto refresh = [&] { scope.request(current()); };
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(sketchBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(reverse, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString error = apply(current());
        if (error.isEmpty()) dialog.accept();
        else status->setText(error);
    });
    QTimer::singleShot(0, &dialog, refresh);
    return runModeless(window, viewport, dialog);
}

bool thickenDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply) {
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int i) { return !viewport->extrusions().at(i).forgeBody; }), candidates.end());
    if (initial.firstBody >= 0 && initial.firstBody < viewport->extrusions().size() && viewport->extrusions().at(initial.firstBody).forgeBody
        && (replaced < 0 || initial.firstBody < replaced) && !candidates.contains(initial.firstBody))
        candidates.append(initial.firstBody);
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Crea prima una superficie da ispessire."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Thicken;
    if (!candidates.contains(definition.firstBody)) {
        // Di partenza l'ultima superficie, altrimenti l'ultimo corpo.
        definition.firstBody = candidates.last();
        for (int i : candidates)
            if (viewport->extrusions().at(i).forgeBody->isSheet()) definition.firstBody = i;
    }
    if (!(definition.distance > 0.0)) definition.distance = 1.0;
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("thicken"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *bodyBox = new QComboBox(&dialog);
    for (int i : candidates) bodyBox->addItem(logicalBodyLabel(viewport, i));
    bodyBox->setCurrentIndex(candidates.indexOf(definition.firstBody));
    auto *faces = new QPushButton(QStringLiteral("Scegli facce"), &dialog);
    faces->setCheckable(true);
    auto *all = new QPushButton(QStringLiteral("Tutta la superficie"), &dialog);
    auto *faceCount = new QLabel(&dialog);
    auto *thickness = new ForgeCad::ExpressionSpinBox(&dialog);
    thickness->setDecimals(6);
    thickness->setRange(1e-6, 1e6);
    thickness->setValue(definition.distance);
    thickness->setKeyboardTracking(false);
    auto *sideBox = new QComboBox(&dialog);
    sideBox->addItems({QStringLiteral("Nel verso della direzione"), QStringLiteral("Nel verso opposto"), QStringLiteral("Meta' per parte")});
    sideBox->setCurrentIndex(qBound(0, definition.thickenSide, 2));
    auto *directionBox = new QComboBox(&dialog);
    directionBox->addItems({QStringLiteral("Normale della superficie"), QStringLiteral("Asse X"), QStringLiteral("Asse Y"), QStringLiteral("Asse Z"),
                            QStringLiteral("Riferimento nella vista")});
    const GeometryRef &startDirection = definition.thickenDirection;
    directionBox->setCurrentIndex(startDirection.kind < 0 ? 0 : startDirection.kind == 2 ? 1 + qBound(0, startDirection.index, 2) : 4);
    auto *directionPick = new QPushButton(QStringLiteral("Scegli retta, piano o faccia"), &dialog);
    directionPick->setCheckable(true);
    auto *directionLabel = new QLabel(&dialog);
    directionLabel->setWordWrap(true);
    directionLabel->setMaximumWidth(360);
    auto *status = new FunctionMessage(&dialog);
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Facce:"), faces);
    form->addRow(faceCount);
    form->addRow(all);
    form->addRow(QStringLiteral("Spessore:"), thickness);
    form->addRow(QStringLiteral("Direzione:"), directionBox);
    form->addRow(directionPick);
    form->addRow(directionLabel);
    form->addRow(QStringLiteral("Lato:"), sideBox);
    form->addRow(wrappedNote(QStringLiteral("Lungo la normale: superficie a distanza e pareti lungo la normale ai bordi; le facce devono essere unite da "
                                            "spigoli tangenti e lo spessore non puo' superare il raggio di curvatura. Lungo una direzione (asse, retta, "
                                            "normale di un piano o di una faccia piana): copia traslata e pareti estruse, esatte, anche con spigoli vivi; "
                                            "ogni retta parallela alla direzione deve incontrare la superficie una volta sola. Tutta la superficie diventa "
                                            "un solido nello stesso corpo; facce scelte di un corpo danno un corpo nuovo."), &dialog));
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    PreviewScope scope(viewport, dialog, form, replaced);
    int picking = 0;  // 1 facce, 2 direzione
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.firstBody = candidates.at(bodyBox->currentIndex());
        d.distance = thickness->value();
        d.thickenSide = sideBox->currentIndex();
        return d;
    };
    const auto refresh = [&] {
        faceCount->setText(definition.offsetFaces.isEmpty() ? QStringLiteral("tutta la superficie")
                                                            : QStringLiteral("%1 facce scelte").arg(definition.offsetFaces.size()));
        faces->setChecked(picking == 1);
        directionPick->setChecked(picking == 2);
        directionPick->setVisible(directionBox->currentIndex() == 4);
        directionLabel->setText(definition.thickenDirection.kind < 0 ? QStringLiteral("normale delle facce")
                                                                     : ForgeCad::geometryRefText(definition.thickenDirection, viewport->sketches(), viewport->extrusions()));
        QVector<GeometryRef> marks;
        for (const EdgePoint &point : definition.offsetFaces) {
            GeometryRef r;
            r.kind = 5;
            r.index = candidates.at(bodyBox->currentIndex());
            r.point = point;
            marks.append(r);
        }
        if (definition.thickenDirection.kind >= 0) marks.append(definition.thickenDirection);
        viewport->setReferenceMarks(marks);
        if (picking) {
            scope.label->setText(QStringLiteral("termina la scelta per vedere l'anteprima"));
            viewport->clearPreview();
            return;
        }
        if (directionBox->currentIndex() == 4 && definition.thickenDirection.kind < 0) {
            scope.label->setText(QStringLiteral("scegli la direzione nella vista"));
            viewport->clearPreview();
            return;
        }
        scope.request(current());
    };
    const auto beginPick = [&](int mode) {
        viewport->cancelReferencePick();
        picking = mode;
        const int roles = mode == 1 ? ForgeCad::DatumRoleFace : ForgeCad::DatumRoleLine | ForgeCad::DatumRolePlane | ForgeCad::DatumRoleFace;
        const QString error = viewport->beginReferencePick(roles, replaced);
        if (!error.isEmpty()) {
            picking = 0;
            status->setText(error);
        } else {
            status->setText(mode == 1 ? QStringLiteral("Clicca le facce; un secondo clic le toglie. Esc termina la scelta.")
                                      : QStringLiteral("Clicca una retta, un asse, un piano o una faccia piana: la direzione e' la retta o la normale."));
        }
        refresh();
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        const int mode = picking;
        picking = 0;
        if (!picked) {
            status->clear();
            refresh();
            return;
        }
        if (mode == 2) {
            ForgeCad::ResolvedRef resolved;
            QString error;
            if (ForgeCad::resolveGeometryRef(ref, replaced < 0 ? viewport->extrusions().size() : replaced, viewport->sketches(), viewport->extrusions(),
                                             resolved, &error)
                && (resolved.hasLine || resolved.hasPlane)) {
                definition.thickenDirection = ref;
                status->clear();
            } else {
                status->setText(error.isEmpty() ? QStringLiteral("Il riferimento deve essere una retta, un piano o una faccia piana.") : error);
            }
        } else if (mode == 1) {
            const int body = ref.kind == 5 ? resultBodyForPick(viewport, candidates, ref.index) : -1;
            if (body != candidates.at(bodyBox->currentIndex())) {
                status->setText(QStringLiteral("Scegli una faccia del corpo indicato nel pannello."));
            } else {
                const auto &shape = *viewport->extrusions().at(body).forgeBody;
                const auto face = ForgeCad::resolveFaceReference(shape, ref.point, 1e-6);
                int existing = -1;
                for (int i = 0; i < definition.offsetFaces.size(); ++i)
                    if (ForgeCad::resolveFaceReference(shape, definition.offsetFaces.at(i), 1e-6) == face) existing = i;
                if (face.valid()) {
                    if (existing >= 0) definition.offsetFaces.remove(existing);
                    else definition.offsetFaces.append(ref.point);
                }
            }
            // Scelta continua: l'anteprima parte alla fine.
            picking = 1;
            const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
            if (!error.isEmpty()) {
                picking = 0;
                status->setText(error);
            }
        }
        refresh();
    });
    QObject::connect(faces, &QPushButton::clicked, &dialog, [&] {
        if (picking == 1) {
            viewport->cancelReferencePick();
            picking = 0;
            status->clear();
            refresh();
        } else {
            beginPick(1);
        }
    });
    QObject::connect(directionPick, &QPushButton::clicked, &dialog, [&] {
        if (picking == 2) {
            viewport->cancelReferencePick();
            picking = 0;
            refresh();
        } else {
            beginPick(2);
        }
    });
    QObject::connect(all, &QPushButton::clicked, &dialog, [&] {
        definition.offsetFaces.clear();
        refresh();
    });
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        viewport->cancelReferencePick();
        picking = 0;
        definition.offsetFaces.clear();
        refresh();
    });
    QObject::connect(directionBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        const int index = directionBox->currentIndex();
        if (index == 4) {
            beginPick(2);
            return;
        }
        viewport->cancelReferencePick();
        picking = 0;
        definition.thickenDirection = {};
        if (index >= 1) {
            definition.thickenDirection.kind = 2;
            definition.thickenDirection.index = index - 1;
        }
        refresh();
    });
    QObject::connect(thickness, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(sideBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        viewport->cancelReferencePick();
        picking = 0;
        if (directionBox->currentIndex() == 4 && definition.thickenDirection.kind < 0) {
            status->setText(QStringLiteral("Scegli la direzione nella vista."));
            return;
        }
        const QString error = apply(current());
        if (error.isEmpty()) dialog.accept();
        else status->setText(error);
    });
    QTimer::singleShot(0, &dialog, refresh);
    const bool accepted = runModeless(window, viewport, dialog);
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    return accepted;
}

bool offsetDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                         const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    const bool shell = initial.feature == BodyFeature::Shell;
    const bool deleteFace = initial.feature == BodyFeature::DeleteFace;
    // Eliminazione e guscio: le facce scelte si tolgono.
    const bool removal = deleteFace || shell;
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);  // solo i corpi esistenti, non gli stadi intermedi
    if (shell) candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int k) { return !bodies.at(k).solid; }), candidates.end());
    if (replaced >= 0 && initial.firstBody >= 0 && initial.firstBody < replaced && !candidates.contains(initial.firstBody)) {
        candidates.append(initial.firstBody);
        std::sort(candidates.begin(), candidates.end());
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(window, title, shell ? QStringLiteral("Nella scena non ci sono solidi da svuotare.")
                                                      : QStringLiteral("Nella scena non ci sono solidi o superfici."));
        return false;
    }
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = shell ? BodyFeature::Shell : removal ? BodyFeature::DeleteFace : BodyFeature::SurfaceOffset;
    // Senza un corpo selezionato prima del comando lo da' la prima faccia
    // cliccata (come per i raccordi): fino ad allora niente anteprima.
    bool waiting = replaced < 0 && !candidates.contains(definition.firstBody);
    if (waiting) definition.firstBody = candidates.last();
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(shell ? QStringLiteral("shell") : removal ? QStringLiteral("delete-face") : QStringLiteral("surface-offset"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    QComboBox *bodyBox = nullptr;
    if (!deleteFace) {
        bodyBox = new QComboBox(&dialog);
        for (int index : candidates) bodyBox->addItem(resultBodyLabel(viewport, index, false));  // i corpi, non le funzioni
        bodyBox->setCurrentIndex(int(candidates.indexOf(definition.firstBody)));
        bodyBox->setEnabled(replaced < 0);
    }
    auto *facesLabel = new QLabel(&dialog);
    facesLabel->setWordWrap(true);
    auto *pickButton = new QPushButton(QStringLiteral("Scegli le facce nella vista"), &dialog);
    pickButton->setCheckable(true);
    pickButton->setToolTip(QStringLiteral("Ogni clic su una faccia del corpo la aggiunge o la toglie; Esc nella vista termina la scelta"));
    auto *clearButton = new QPushButton(QStringLiteral("Tutte le facce"), &dialog);
    clearButton->setToolTip(QStringLiteral("Toglie la scelta: l'offset vale per tutte le facce del corpo"));
    auto *faceRow = new QWidget(&dialog);
    auto *faceLayout = new QHBoxLayout(faceRow);
    faceLayout->setContentsMargins(0, 0, 0, 0);
    faceLayout->addWidget(pickButton, 1);
    faceLayout->addWidget(clearButton);
    auto *distanceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    distanceBox->setDecimals(6);
    distanceBox->setRange(0.0, 100000.0);
    distanceBox->setObjectName(QStringLiteral("offsetDistance"));
    distanceBox->setValue(removal && definition.distance == 0.0 ? 1.0 : std::fabs(definition.distance));
    distanceBox->setKeyboardTracking(false);
    auto *flipButton = new QPushButton(ForgeCad::commandIcon(QStringLiteral("reverseDirection")), QStringLiteral("Inverti il verso"), &dialog);
    flipButton->setCheckable(true);
    flipButton->setChecked(definition.distance < 0.0);
    flipButton->setToolTip(QStringLiteral("Spento: verso l'esterno (lungo la normale uscente); acceso: verso l'interno"));
    if (bodyBox) form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Facce:"), facesLabel);
    form->addRow(QString(), faceRow);
    if (shell) {
        flipButton->hide();
        clearButton->setText(QStringLiteral("Azzera"));
        clearButton->setToolTip(QStringLiteral("Nessuna apertura: il solido resta chiuso con una cavita' interna"));
        form->addRow(QStringLiteral("Spessore:"), distanceBox);
        form->addRow(wrappedNote(QStringLiteral("Il solido si svuota lasciando pareti dello spessore dato verso l'interno; "
                                                "le facce scelte si tolgono e fanno l'apertura (nessuna: cavita' chiusa). "
                                                "Clicca un altro solido nella vista per cambiarlo."), &dialog));
    } else if (removal) {
        distanceBox->hide();
        flipButton->hide();
        clearButton->setText(QStringLiteral("Azzera"));
        clearButton->setToolTip(QStringLiteral("Toglie tutte le facce scelte"));
        form->addRow(wrappedNote(QStringLiteral("Le facce scelte si tolgono dal corpo: un solido diventa una superficie aperta "
                                                "(le facce restanti, cucite lungo gli spigoli comuni). "
                                                "Clicca un altro corpo nella vista per cambiarlo."), &dialog));
    } else {
        form->addRow(QStringLiteral("Distanza (0 = copia):"), distanceBox);
        form->addRow(QString(), flipButton);
        form->addRow(wrappedNote(QStringLiteral("A distanza zero si copiano esattamente le facce, conservando i bordi comuni. "
                                               "Il corpo originale resta disponibile. Per l'offset, con la cucitura attiva "
                                               "i bordi comuni vengono estesi e rifilati; disattivandola gli spigoli vivi si separano."), &dialog));
    }
    auto *sewBox = new QCheckBox(QStringLiteral("Mantieni la cucitura (estendi e rifila)"), &dialog);
    sewBox->setChecked(definition.offsetSew);
    sewBox->setVisible(!removal);
    if (!removal) form->addRow(sewBox);
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
        if (bodyBox) d.firstBody = candidates.at(bodyBox->currentIndex());
        d.offsetSew = sewBox->isChecked();
        d.distance = (flipButton->isChecked() ? -1.0 : 1.0) * distanceBox->value();
        return d;
    };
    // Faccia del corpo di un riferimento (per togliere una faccia gia' scelta).
    const auto faceOf = [&](const EdgePoint &point) {
        const ExtrusionObject &body = viewport->extrusions().at(definition.firstBody);
        if (!body.forgeBody) return ForgeCad::Kernel::FaceId();
        return ForgeCad::resolveFaceReference(*body.forgeBody, point, 1e-6);
    };
    const auto refresh = [&] {
        sewBox->setEnabled(removal || distanceBox->value() != 0.0);
        flipButton->setEnabled(removal || distanceBox->value() != 0.0);
        if (waiting) {
            if (shell) viewport->setPickBodies(picking ? candidates : QVector<int>());
            facesLabel->setText(QStringLiteral("clicca nella vista una faccia del corpo"));
            pickButton->setChecked(picking);
            viewport->setReferenceMarks({});
            if (picking) setStatus(QStringLiteral("Clicca una faccia: sceglie anche il corpo. Esc nella vista termina la scelta."), false);
            scope->label->setText(QStringLiteral("scegli il corpo cliccandone una faccia"));
            viewport->clearPreview();
            return;
        }
        facesLabel->setText(definition.offsetFaces.isEmpty()
                                ? (shell ? QStringLiteral("nessuna apertura") : removal ? QStringLiteral("nessuna faccia scelta") : QStringLiteral("tutte le facce del corpo"))
                                : QStringLiteral("%1 facce scelte").arg(definition.offsetFaces.size()));
        // Il guscio conserva il vecchio isolamento dei candidati. Elimina facce
        // lascia invece invariata la vista: i corpi nascosti restano nascosti e
        // non intercettano il picking di un corpo visibile racchiuso al loro interno.
        if (shell) viewport->setPickBodies(picking ? candidates : QVector<int>());
        pickButton->setChecked(picking);
        QVector<GeometryRef> marks;
        for (const EdgePoint &point : definition.offsetFaces) {
            GeometryRef ref;
            ref.kind = 5;
            ref.index = definition.firstBody;
            ref.point = point;
            marks.append(ref);
        }
        viewport->setReferenceMarks(marks);
        if (picking) setStatus(QStringLiteral("Clicca le facce del corpo (di nuovo per toglierle); Esc nella vista termina la scelta."), false);
        if (removal) {
            if (picking) {
                scope->label->setText(QStringLiteral("scelta delle facce (Esc nella vista per l'anteprima)"));
                viewport->clearPreview();
            } else if (definition.offsetFaces.isEmpty() && !shell) {
                scope->label->setText(QStringLiteral("scegli le facce da eliminare"));
                viewport->clearPreview();
            } else if (shell && !(distanceBox->value() > 0.0)) {
                setStatus(QStringLiteral("Lo spessore deve essere maggiore di zero."), true);
                viewport->clearPreview();
            } else {
                scope->request(current());
            }
            return;
        }
        scope->request(current());
    };
    const auto startPicking = [&] {
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
        picking = error.isEmpty();
        if (!picking) setStatus(error, true);
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (!picked) {
            picking = false;
            setStatus(QString(), false);
            refresh();
            return;
        }
        const int body = ref.kind == 5 ? resultBodyForPick(viewport, candidates, ref.index) : -1;
        if (body < 0) {
            setStatus(QStringLiteral("Serve una faccia di un corpo adatto (in questo punto della storia)."), true);
        } else {
            ref.index = body;
            if (waiting) definition.offsetFaces.clear();
            waiting = false;
            if (ref.index != definition.firstBody && replaced >= 0) {
                setStatus(QStringLiteral("In modifica il corpo resta quello della funzione."), true);
                startPicking();
                refresh();
                return;
            }
            if (ref.index != definition.firstBody) {
                // Le facce di un altro corpo: la scelta riparte da li'.
                definition.offsetFaces.clear();
                definition.firstBody = ref.index;
                if (bodyBox) {
                    const QSignalBlocker blocker(bodyBox);
                    bodyBox->setCurrentIndex(int(candidates.indexOf(ref.index)));
                }
            }
            const ForgeCad::Kernel::FaceId face = faceOf(ref.point);
            int existing = -1;
            for (int k = 0; k < definition.offsetFaces.size() && existing < 0; ++k)
                if (face.valid() && faceOf(definition.offsetFaces.at(k)) == face) existing = k;
            if (existing >= 0) definition.offsetFaces.remove(existing);
            else definition.offsetFaces.append(ref.point);
            setStatus(QString(), false);
        }
        startPicking();  // si continua a scegliere
        refresh();
    });
    QObject::connect(pickButton, &QPushButton::clicked, &dialog, [&] {
        if (picking) {
            viewport->cancelReferencePick();
            picking = false;
            setStatus(QString(), false);
        } else {
            startPicking();
        }
        refresh();
    });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [&] {
        definition.offsetFaces.clear();
        refresh();
    });
    if (bodyBox)
        QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, [&] {
            waiting = false;
            definition.firstBody = candidates.at(bodyBox->currentIndex());
            definition.offsetFaces.clear();
            refresh();
        });
    QObject::connect(sewBox, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(flipButton, &QPushButton::toggled, &dialog, refresh);
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (waiting) {
            setStatus(QStringLiteral("Clicca prima nella vista una faccia del corpo."), true);
            return;
        }
        if (removal && !shell && definition.offsetFaces.isEmpty()) {
            setStatus(QStringLiteral("Scegli almeno una faccia da eliminare."), true);
            return;
        }
        if (removal && picking) {
            viewport->cancelReferencePick();
            picking = false;
            refresh();
        }
        if (shell && !(distanceBox->value() > 0.0)) {
            setStatus(QStringLiteral("Lo spessore deve essere maggiore di zero."), true);
            return;
        }
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
        // Senza facce scelte si parte subito con la scelta nella vista.
        // La scelta nella vista parte subito (corpi e facce).
        if (replaced < 0) startPicking();
        refresh();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
        accepted = dialog.result() == QDialog::Accepted;
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    viewport->setPickBodies({});
    scope.reset();
    return accepted;
}

bool ruledDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::Ruled;
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("ruled"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *firstButton = new QPushButton(QStringLiteral("Scegli nella vista"), &dialog);
    auto *secondButton = new QPushButton(QStringLiteral("Scegli nella vista"), &dialog);
    for (QPushButton *button : {firstButton, secondButton}) button->setCheckable(true);
    auto *firstLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("ruledFirstEdges"));
    auto *secondLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("ruledSecondEdges"));
    auto *firstClear = new QPushButton(QStringLiteral("Azzera"), &dialog), *secondClear = new QPushButton(QStringLiteral("Azzera"), &dialog);
    const auto row = [&](QPushButton *pick, QPushButton *clear) {
        auto *widget = new QWidget(&dialog);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(pick, 1);
        layout->addWidget(clear);
        return widget;
    };
    form->addRow(QStringLiteral("Prima curva:"), firstLabel);
    form->addRow(QString(), row(firstButton, firstClear));
    form->addRow(QStringLiteral("Seconda curva:"), secondLabel);
    form->addRow(QString(), row(secondButton, secondClear));
    SurfaceEndControls endControls(dialog, *form, viewport, replaced, definition);
    auto *loopBox = new QCheckBox(QStringLiteral("Bordo libero intero delle superfici"), &dialog);
    loopBox->setChecked(true);
    loopBox->setToolTip(QStringLiteral("Un clic su un bordo libero di una superficie sceglie (o toglie) tutto il suo loop"));
    form->addRow(QString(), loopBox);
    form->addRow(wrappedNote(QStringLiteral("Ogni curva e' una catena di tratti consecutivi (spigoli dei corpi, entita' degli schizzi, "
                                           "eliche), aperta o chiusa come l'altra. Le rette uniscono i punti alla stessa frazione di lunghezza."), &dialog));
    auto *status = new FunctionMessage(&dialog);
    status->setMinimumWidth(280);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    CurvePicker picker;
    picker.viewport = viewport;
    picker.owner = replaced;
    picker.status = setStatus;
    picker.wholeLoop = loopBox;
    const auto refresh = [&] {
        firstLabel->setReferences(definition.ruledFirst);
        secondLabel->setReferences(definition.ruledSecond);
        firstButton->setChecked(picker.active == &definition.ruledFirst);
        secondButton->setChecked(picker.active == &definition.ruledSecond);
        viewport->setReferenceMarks(definition.ruledFirst + definition.ruledSecond + definition.loftStartFaces + definition.loftEndFaces);
        if (definition.ruledFirst.isEmpty() || definition.ruledSecond.isEmpty()) {
            scope->label->setText(QStringLiteral("scegli le due curve"));
            viewport->clearPreview();
            return;
        }
        scope->request(definition);
    };
    picker.changed = refresh;
    endControls.changed = refresh;
    picker.install();
    for (int k = 0; k < 2; ++k) {
        QVector<GeometryRef> *list = k == 0 ? &definition.ruledFirst : &definition.ruledSecond;
        QObject::connect(k == 0 ? firstButton : secondButton, &QPushButton::clicked, &dialog, [&, list] {
            endControls.stop();
            picker.install();
            if (picker.active == list) picker.stop();
            else picker.start(list);
            refresh();
        });
        QObject::connect(k == 0 ? firstClear : secondClear, &QPushButton::clicked, &dialog, [&, list] {
            list->clear();
            refresh();
        });
    }
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (definition.ruledFirst.isEmpty() || definition.ruledSecond.isEmpty()) {
            setStatus(QStringLiteral("Scegli le due curve."), true);
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(definition);
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        if (definition.ruledFirst.isEmpty() && replaced < 0) picker.start(&definition.ruledFirst);
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

bool boundaryDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                           const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::BoundarySurface;
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("boundary"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *curvesLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("boundaryEdges"));
    auto *pickButton = new QPushButton(QStringLiteral("Scegli nella vista"), &dialog);
    pickButton->setCheckable(true);
    auto *clearButton = new QPushButton(QStringLiteral("Azzera"), &dialog);
    auto *pickRow = new QWidget(&dialog);
    auto *pickLayout = new QHBoxLayout(pickRow);
    pickLayout->setContentsMargins(0, 0, 0, 0);
    pickLayout->addWidget(pickButton, 1);
    pickLayout->addWidget(clearButton);
    auto *loopBox = new QCheckBox(QStringLiteral("Bordo libero intero delle superfici"), &dialog);
    loopBox->setChecked(true);
    loopBox->setToolTip(QStringLiteral("Un clic su un bordo libero di una superficie sceglie (o toglie) tutto il suo loop"));
    form->addRow(QStringLiteral("Contorno:"), curvesLabel);
    form->addRow(QString(), pickRow);
    form->addRow(QString(), loopBox);
    form->addRow(wrappedNote(QStringLiteral("Le curve, in qualsiasi ordine, devono formare un contorno chiuso anche sghembo. Gli angoli vivi "
                                           "dividono il contorno in 3 o 4 lati (un contorno liscio in 4 parti uguali); la superficie li "
                                           "raccorda come una patch di Coons."), &dialog));
    auto *status = new FunctionMessage(&dialog);
    status->setMinimumWidth(280);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    CurvePicker picker;
    picker.viewport = viewport;
    picker.owner = replaced;
    picker.status = setStatus;
    picker.wholeLoop = loopBox;
    const auto refresh = [&] {
        curvesLabel->setReferences(definition.planarRefs);
        pickButton->setChecked(picker.active != nullptr);
        viewport->setReferenceMarks(definition.planarRefs);
        if (definition.planarRefs.isEmpty()) {
            scope->label->setText(QStringLiteral("scegli le curve del contorno"));
            viewport->clearPreview();
            return;
        }
        scope->request(definition);
    };
    picker.changed = refresh;
    picker.install();
    QObject::connect(pickButton, &QPushButton::clicked, &dialog, [&] {
        if (picker.active) picker.stop();
        else picker.start(&definition.planarRefs);
        refresh();
    });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [&] {
        definition.planarRefs.clear();
        refresh();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (definition.planarRefs.isEmpty()) {
            setStatus(QStringLiteral("Scegli le curve del contorno."), true);
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(definition);
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        if (definition.planarRefs.isEmpty() && replaced < 0) picker.start(&definition.planarRefs);
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

bool fillDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::FillSurface;
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("fill"), {QStringLiteral("fillDialog")});
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setObjectName(QStringLiteral("fillDialog"));
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    // Tre liste scelte nella vista: contorno, guide, facce adiacenti.
    struct ListRow {
        QWidget *label;
        QPushButton *pick;
        QPushButton *clear;
        QWidget *row;
    };
    const auto listRow = [&](const QString &pickText, const QString &listName = QString()) {
        QWidget *label;
        if (!listName.isEmpty()) label = new GeometryReferenceList(dialog, viewport, listName);
        else { auto *summary = new QLabel(&dialog); summary->setWordWrap(true); label = summary; }
        ListRow r{label, new QPushButton(pickText, &dialog), new QPushButton(QStringLiteral("Azzera"), &dialog), new QWidget(&dialog)};
        r.pick->setCheckable(true);
        auto *layout = new QHBoxLayout(r.row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(r.pick, 1);
        layout->addWidget(r.clear);
        return r;
    };
    const ListRow boundaryRow = listRow(QStringLiteral("Scegli il contorno"), QStringLiteral("fillBoundaryEdges")),
                  guideRow = listRow(QStringLiteral("Scegli le guide"), QStringLiteral("fillGuideEdges")),
                  faceRow = listRow(QStringLiteral("Scegli le facce"));
    auto *faceList = new QListWidget(&dialog);
    faceList->setObjectName(QStringLiteral("fillContactFaces"));
    faceList->setMinimumHeight(100);
    QVector<GeometryRef> previousFaces = definition.ruledSecond;
    QVector<double> previousInfluences;
    QVector<int> previousContinuities;for(int i=0;i<previousFaces.size();++i)previousContinuities.append(definition.fillFaceContinuities.value(i,-1));
    for (int i = 0; i < previousFaces.size(); ++i)
        previousInfluences.append(definition.fillFaceInfluences.value(i, definition.fillInfluence));
    faceRow.pick->setObjectName(QStringLiteral("fillPickFaces"));
    auto *detectButton = new QPushButton(QStringLiteral("Rileva"), &dialog);
    detectButton->setToolTip(QStringLiteral("Le facce dei corpi che hanno un bordo libero sul contorno"));
    faceRow.row->layout()->addWidget(detectButton);
    auto *loopBox = new QCheckBox(QStringLiteral("Bordo libero intero delle superfici"), &dialog);
    loopBox->setChecked(true);
    loopBox->setToolTip(QStringLiteral("Un clic su un bordo libero di una superficie sceglie (o toglie) tutto il suo loop"));
    auto *continuityBox = new QComboBox(&dialog);
    continuityBox->addItems({QStringLiteral("Contatto (G0)"), QStringLiteral("Tangenza (G1)"), QStringLiteral("Curvatura (G2)")});
    continuityBox->setCurrentIndex(qBound(0, definition.fillContinuity, 2));
    continuityBox->setToolTip(QStringLiteral("Raccordo con le facce adiacenti lungo i tratti del contorno che stanno sul loro bordo"));
    auto *contactContinuity=new QComboBox(&dialog);contactContinuity->setObjectName("fillContactContinuity");contactContinuity->addItems({QStringLiteral("Eredita continuita comune"),QStringLiteral("Contatto G0"),QStringLiteral("Tangenza G1"),QStringLiteral("Curvatura G2")});
    auto *influenceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    influenceBox->setObjectName(QStringLiteral("fillFaceInfluence"));
    influenceBox->setDecimals(3);
    influenceBox->setRange(0.05, 20.0);
    influenceBox->setSingleStep(0.1);
    influenceBox->setValue(definition.fillInfluence);
    influenceBox->setKeyboardTracking(false);
    influenceBox->setToolTip(QStringLiteral("Regola la distribuzione della curvatura e la forma del raccordo (1: naturale), mantenendo il piano tangente sul bordo"));
    auto *weightBox = new ForgeCad::ExpressionSpinBox(&dialog);
    weightBox->setDecimals(3);
    weightBox->setRange(0.0, 100.0);
    weightBox->setSingleStep(0.1);
    weightBox->setValue(definition.fillGuideWeight);
    weightBox->setKeyboardTracking(false);
    weightBox->setToolTip(QStringLiteral("1: la superficie passa per le guide; meno di 1: le guide la attirano soltanto; 0: ignorate"));
    form->addRow(QStringLiteral("Contorno:"), boundaryRow.label);
    form->addRow(QString(), boundaryRow.row);
    form->addRow(QString(), loopBox);
    form->addRow(QStringLiteral("Guide:"), guideRow.label);
    form->addRow(QString(), guideRow.row);
    form->addRow(QStringLiteral("Facce adiacenti:"), faceRow.label);
    form->addRow(QString(), faceRow.row);
    form->addRow(QString(), faceList);
    form->addRow(QStringLiteral("Continuita':"), continuityBox);
    form->addRow(QStringLiteral("Continuita faccia selezionata:"),contactContinuity);
    form->addRow(QStringLiteral("Influenza faccia selezionata:"), influenceBox);
    form->addRow(QStringLiteral("Peso delle guide:"), weightBox);
    auto *shapeBox = new QCheckBox(QStringLiteral("Usa la faccia selezionata per guidare la forma"), &dialog);
    shapeBox->setObjectName(QStringLiteral("fillShapeFace"));
    shapeBox->setToolTip(QStringLiteral("Guida la curvatura interna dalla faccia scelta, mantenendo i contatti con le altre facce."));
    auto *shapeWeightBox = new ForgeCad::ExpressionSpinBox(&dialog);
    shapeWeightBox->setObjectName(QStringLiteral("fillShapeWeight"));shapeWeightBox->setRange(0,10);shapeWeightBox->setDecimals(3);
    shapeWeightBox->setValue(definition.fillShapeWeight);shapeWeightBox->setKeyboardTracking(false);
    form->addRow(shapeBox);form->addRow(QStringLiteral("Influenza sulla forma interna:"),shapeWeightBox);

    form->addRow(wrappedNote(QStringLiteral("Il contorno chiuso (in qualsiasi ordine) delimita la superficie, che passa per le curve guida "
                                           "interne. Dove il contorno sta sul bordo di una faccia adiacente la superficie la continua in "
                                           "tangenza o in curvatura; vicino a quei bordi le guide pesano meno, cosi' una guida che arriva "
                                           "con un'altra pendenza non crea una piega. In G1 la curvatura delle facce suggerisce la forma del "
                                           "raccordo; in G2 viene richiesta anche la continuita' di curvatura. L'influenza regola la forma."), &dialog));
    auto *result = new FunctionMessage(&dialog);
    result->setStyleSheet(QStringLiteral("color: #9fc6e8;"));
    form->addRow(QStringLiteral("Esito:"), result);
    auto *status = new FunctionMessage(&dialog);
    status->setMinimumWidth(280);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    FunctionMessage *previewLabel = scope->label;
    viewport->setPreviewCallback([previewLabel, result, viewport](const QString &error) {
        previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
        result->setText(error.isEmpty() ? viewport->previewNotice() : QString());
    });
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    CurvePicker picker;
    picker.viewport = viewport;
    picker.owner = replaced;
    picker.status = setStatus;
    picker.wholeLoop = loopBox;
    picker.faceList = &definition.ruledSecond;
    // Facce adiacenti proposte finche' non si scelgono a mano.
    bool autoFaces = replaced < 0 && definition.ruledSecond.isEmpty();
    const auto detect = [&] {
        definition.ruledSecond.clear();
        const int limit = replaced < 0 ? int(viewport->extrusions().size()) : replaced;
        std::vector<ForgeCad::Kernel::PathSegment> boundary;
        for (const GeometryRef &ref : definition.planarRefs) {
            std::vector<ForgeCad::Kernel::PathSegment> pieces;
            if (ForgeCad::geometryRefPath(ref, limit, viewport->sketches(), viewport->extrusions(), pieces, nullptr))
                boundary.insert(boundary.end(), pieces.begin(), pieces.end());
        }
        if (boundary.empty()) return;
        for (int b : viewport->resultBodiesBefore(limit)) {
            const ExtrusionObject &body = viewport->extrusions().at(b);
            if (!body.forgeBody) continue;
            for (const EdgePoint &point : ForgeCad::forgeFillContactFaces(boundary, *body.forgeBody)) {
                GeometryRef ref;
                ref.kind = 5;
                ref.index = b;
                ref.featureId = body.featureId;
                ref.point = point;
                definition.ruledSecond.append(ref);
            }
        }
    };
    const auto effective=[&](int row){const int own=definition.fillFaceContinuities.value(row,-1);return own<0?continuityBox->currentIndex():own;};
    const auto syncFaceControls = [&] {
        const QSignalBlocker block(influenceBox), shapeBlock(shapeBox),contactBlock(contactContinuity);
        const int row = faceList->currentRow();
        contactContinuity->setEnabled(row>=0);contactContinuity->setCurrentIndex(definition.fillFaceContinuities.value(row,-1)+1);
        shapeBox->setEnabled(effective(row)>0 && row>=0);
        shapeBox->setChecked(row>=0 && row==definition.fillShapeFace);
        shapeWeightBox->setEnabled(definition.fillShapeFace>=0 && effective(definition.fillShapeFace)>0);
        influenceBox->setEnabled(effective(row) > 0 && row >= 0 && row < definition.fillFaceInfluences.size());
        if (row >= 0 && row < definition.fillFaceInfluences.size()) influenceBox->setValue(definition.fillFaceInfluences[row]);
    };
    const auto refresh = [&] {
        // Rileva/picking possono riordinare o rimuovere facce: conserva il
        // valore per identita' topologica, mai per la vecchia riga dell'elenco.
        const int oldRow = faceList->currentRow();
        const GeometryRef selected = previousFaces.value(oldRow);
        const int oldShape=definition.fillShapeFace;
        const GeometryRef shapeRef=previousFaces.value(oldShape);
        int mappedShape=-1;
        QVector<double> values;QVector<int> continuities; int selectedRow = -1;
        for (int i = 0; i < definition.ruledSecond.size(); ++i) {
            const auto &ref = definition.ruledSecond[i]; double value = definition.fillInfluence;
            int continuity=-1;for (int j = 0; j < previousFaces.size(); ++j) if (picker.sameFace(ref, previousFaces[j])) { value = previousInfluences.value(j, definition.fillInfluence);continuity=previousContinuities.value(j,-1); break; }
            continuities.append(continuity);
            if (oldRow >= 0 && picker.sameFace(ref, selected)) selectedRow = i;
            if(oldShape>=0 && picker.sameFace(ref,shapeRef))mappedShape=i;
            values.append(value);
        }
        definition.fillShapeFace=mappedShape;
        definition.fillFaceContinuities=std::all_of(continuities.cbegin(),continuities.cend(),[](int c){return c<0;})?QVector<int>{}:continuities;
        if(definition.fillShapeFace>=0&&effective(definition.fillShapeFace)==0)definition.fillShapeFace=-1;
        definition.fillShapeWeight=shapeWeightBox->value();
        definition.fillFaceInfluences = values;
        previousFaces = definition.ruledSecond; previousInfluences = values;previousContinuities=continuities;
        {
            const QSignalBlocker block(faceList);
            faceList->clear();
            for (int i = 0; i < previousFaces.size(); ++i) {
                const auto &ref = previousFaces[i];
                faceList->addItem(QStringLiteral("%1 — F%2 / influenza %3")
                    .arg(ForgeCad::geometryRefText(ref, viewport->sketches(), viewport->extrusions())).arg(ref.point.subshape)
                    .arg(values[i], 0, 'g', 3) + QStringLiteral(" / G%1%2").arg(effective(i)).arg(continuities[i]<0?QStringLiteral(" (comune)"):QString()) + (i==definition.fillShapeFace ? QStringLiteral(" — guida della forma") : QString()));
            }
            faceList->setCurrentRow(selectedRow >= 0 ? selectedRow : faceList->count() ? 0 : -1);
        }
        static_cast<GeometryReferenceList *>(boundaryRow.label)->setReferences(definition.planarRefs);
        static_cast<GeometryReferenceList *>(guideRow.label)->setReferences(definition.ruledFirst);
        QStringList faceNames;
        for (const GeometryRef &ref : definition.ruledSecond) faceNames.append(ForgeCad::geometryRefText(ref, viewport->sketches(), viewport->extrusions()));
        static_cast<QLabel *>(faceRow.label)->setText(faceNames.isEmpty() ? QStringLiteral("nessuna (solo contatto)") : faceNames.join(QStringLiteral(", ")));
        boundaryRow.pick->setChecked(picker.active == &definition.planarRefs);
        guideRow.pick->setChecked(picker.active == &definition.ruledFirst);
        faceRow.pick->setChecked(picker.active == &definition.ruledSecond);
        viewport->setReferenceMarks(definition.planarRefs + definition.ruledFirst + definition.ruledSecond);
        definition.fillContinuity = continuityBox->currentIndex();

        definition.fillGuideWeight = weightBox->value();
        syncFaceControls();
        if (definition.planarRefs.isEmpty()) {
            previewLabel->setText(QStringLiteral("scegli le curve del contorno"));
            result->clear();
            viewport->clearPreview();
            return;
        }
        result->setText(QStringLiteral("in calcolo..."));
        scope->request(definition);
    };
    picker.changed = [&] {
        if (picker.active == &definition.planarRefs && autoFaces) detect();
        refresh();
    };
    picker.install();
    const auto toggle = [&](QVector<GeometryRef> *list) {
        if (picker.active == list) picker.stop();
        else {
            if (picker.active) picker.stop();
            picker.start(list);
        }
        refresh();
    };
    QObject::connect(boundaryRow.pick, &QPushButton::clicked, &dialog, [&] { toggle(&definition.planarRefs); });
    QObject::connect(guideRow.pick, &QPushButton::clicked, &dialog, [&] { toggle(&definition.ruledFirst); });
    QObject::connect(faceRow.pick, &QPushButton::clicked, &dialog, [&] {
        autoFaces = false;
        toggle(&definition.ruledSecond);
    });
    QObject::connect(boundaryRow.clear, &QPushButton::clicked, &dialog, [&] {
        definition.planarRefs.clear();
        if (autoFaces) definition.ruledSecond.clear();
        refresh();
    });
    QObject::connect(guideRow.clear, &QPushButton::clicked, &dialog, [&] {
        definition.ruledFirst.clear();
        refresh();
    });
    QObject::connect(faceRow.clear, &QPushButton::clicked, &dialog, [&] {
        autoFaces = false;
        definition.ruledSecond.clear();
        refresh();
    });
    QObject::connect(detectButton, &QPushButton::clicked, &dialog, [&] {
        autoFaces = true;
        detect();
        if (definition.ruledSecond.isEmpty()) setStatus(QStringLiteral("Nessuna faccia ha un bordo libero sul contorno."), false);
        refresh();
    });
    QObject::connect(continuityBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(contactContinuity,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int value){const int row=faceList->currentRow();if(row<0)return;if(previousContinuities.size()!=previousFaces.size())return;previousContinuities[row]=value-1;refresh();});
    const auto changeFaceInfluence = [&] {
        const int row = faceList->currentRow();
        if (row < 0 || row >= definition.fillFaceInfluences.size()) return;
        definition.fillFaceInfluences[row] = influenceBox->value();
        previousInfluences[row] = influenceBox->value();
        refresh();
    };
    QObject::connect(faceList, &QListWidget::currentRowChanged, &dialog, syncFaceControls);
    QObject::connect(influenceBox, &QDoubleSpinBox::valueChanged, &dialog, changeFaceInfluence);
    influenceBox->onReturn = changeFaceInfluence;
    QObject::connect(shapeBox,&QCheckBox::toggled,&dialog,[&](bool checked){
        if(checked)definition.fillShapeFace=faceList->currentRow();
        else if(definition.fillShapeFace==faceList->currentRow())definition.fillShapeFace=-1;
        refresh();
    });
    QObject::connect(shapeWeightBox,&QDoubleSpinBox::valueChanged,&dialog,refresh);
    shapeWeightBox->onReturn=refresh;
    weightBox->onReturn = refresh;
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        influenceBox->interpretText();
        changeFaceInfluence();
        weightBox->interpretText();
        definition.fillContinuity = continuityBox->currentIndex();

        definition.fillGuideWeight = weightBox->value();
        if (definition.planarRefs.isEmpty()) {
            setStatus(QStringLiteral("Scegli le curve del contorno."), true);
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QString error = apply(definition);
        QApplication::restoreOverrideCursor();
        if (error.isEmpty()) dialog.accept();
        else setStatus(error, true);
    });
    bool accepted = false;
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        if (definition.planarRefs.isEmpty() && replaced < 0) picker.start(&definition.planarRefs);
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

bool planarDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                         const std::function<QString(const ExtrusionObject &)> &apply) {
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::PlanarSurface;
    const QVector<SketchObject> &sketches = viewport->sketches();
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("planar"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    auto *sourceBox = new QComboBox(&dialog);
    for (const SketchObject &sketch : sketches) sourceBox->addItem(QStringLiteral("Schizzo: %1").arg(sketch.name));
    sourceBox->addItem(QStringLiteral("Bordi scelti nella vista"));
    const int edgesItem = int(sketches.size());
    sourceBox->setCurrentIndex(!definition.planarRefs.isEmpty() || definition.sketchIndex < 0 || definition.sketchIndex >= sketches.size()
                                   ? edgesItem
                                   : definition.sketchIndex);
    auto *edgesLabel = new GeometryReferenceList(dialog, viewport, QStringLiteral("planarEdges"));
    auto *pickButton = new QPushButton(QStringLiteral("Scegli i bordi nella vista"), &dialog);
    pickButton->setCheckable(true);
    auto *clearButton = new QPushButton(QStringLiteral("Azzera"), &dialog);
    auto *edgeRow = new QWidget(&dialog);
    auto *edgeLayout = new QHBoxLayout(edgeRow);
    edgeLayout->setContentsMargins(0, 0, 0, 0);
    edgeLayout->addWidget(pickButton, 1);
    edgeLayout->addWidget(clearButton);
    form->addRow(QStringLiteral("Sorgente:"), sourceBox);
    form->addRow(QStringLiteral("Bordi:"), edgesLabel);
    form->addRow(QString(), edgeRow);
    auto *loopBox = new QCheckBox(QStringLiteral("Bordo libero intero delle superfici"), &dialog);
    loopBox->setChecked(true);
    loopBox->setToolTip(QStringLiteral("Un clic su un bordo libero di una superficie sceglie (o toglie) tutto il suo loop"));
    form->addRow(QString(), loopBox);
    form->addRow(wrappedNote(QStringLiteral("I contorni chiusi complanari delimitano la superficie; quelli dentro un altro sono fori."), &dialog));
    auto *status = new FunctionMessage(&dialog);
    status->setMinimumWidth(280);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto scope = std::make_unique<PreviewScope>(viewport, dialog, form, replaced);
    const auto setStatus = [status](const QString &text, bool error) {
        status->setStyleSheet(error ? QStringLiteral("color: #ff7a6a;") : QStringLiteral("color: #9fc6e8;"));
        status->setText(text);
    };
    CurvePicker picker;
    picker.viewport = viewport;
    picker.owner = replaced;
    picker.status = setStatus;
    picker.wholeLoop = loopBox;
    const auto current = [&] {
        ExtrusionObject d = definition;
        if (sourceBox->currentIndex() == edgesItem) {
            d.sketchIndex = -1;
        } else {
            d.sketchIndex = sourceBox->currentIndex();
            d.planarRefs.clear();
        }
        return d;
    };
    const auto refresh = [&] {
        const bool edges = sourceBox->currentIndex() == edgesItem;
        for (QWidget *w : {static_cast<QWidget *>(edgesLabel), static_cast<QWidget *>(pickButton), static_cast<QWidget *>(clearButton), static_cast<QWidget *>(loopBox)}) w->setEnabled(edges);
        if (!edges && picker.active) picker.stop();
        edgesLabel->setReferences(definition.planarRefs);
        pickButton->setChecked(picker.active != nullptr);
        viewport->setReferenceMarks(edges ? definition.planarRefs : QVector<GeometryRef>());
        if (edges && definition.planarRefs.isEmpty()) {
            scope->label->setText(QStringLiteral("scegli i bordi"));
            viewport->clearPreview();
            return;
        }
        scope->request(current());
    };
    picker.changed = refresh;
    picker.install();
    QObject::connect(sourceBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(pickButton, &QPushButton::clicked, &dialog, [&] {
        if (picker.active) picker.stop();
        else picker.start(&definition.planarRefs);
        refresh();
    });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [&] {
        definition.planarRefs.clear();
        refresh();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const ExtrusionObject d = current();
        if (d.sketchIndex < 0 && d.planarRefs.isEmpty()) {
            setStatus(QStringLiteral("Scegli uno schizzo o i bordi."), true);
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
        if (sourceBox->currentIndex() == edgesItem && definition.planarRefs.isEmpty() && replaced < 0) picker.start(&definition.planarRefs);
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

bool sewDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                      const std::function<QString(const ExtrusionObject &)> &apply) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    QVector<int> indices = viewport->resultBodiesBefore(replaced);  // solo i corpi esistenti, non gli stadi intermedi
    for (int operand : QVector<int>{initial.firstBody} + initial.booleanTools)
        if (operand >= 0 && operand < bodies.size() && (replaced < 0 || operand < replaced) && !indices.contains(operand)) indices.append(operand);
    std::sort(indices.begin(), indices.end());
    if (indices.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Nella scena non ci sono superfici da cucire."));
        return false;
    }
    QVector<int> chosen;
    if (initial.firstBody >= 0) chosen = QVector<int>{initial.firstBody} + initial.booleanTools;
    if (chosen.isEmpty())
        for (const SceneSelection &selection : viewport->selectedObjects())
            if (selection.kind == SceneObjectKind::Extrusion && indices.contains(selection.index)) chosen.append(selection.index);
    if (chosen.size() < 2) {
        chosen.clear();
        for (int index : indices)
            if (bodies.at(index).visible && !bodies.at(index).solid) chosen.append(index);
    }
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("sew"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *list = new QListWidget(&dialog);
    list->setMinimumHeight(160);
    for (int index : indices) {
        const ExtrusionObject &body = bodies.at(index);
        const QString text = resultBodyLabel(viewport, index, false) + (body.solid ? QStringLiteral(" (solido: le sue facce)") : QString());
        auto *item = new QListWidgetItem(text, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(chosen.contains(index) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, index);
    }
    auto *toleranceBox = new ForgeCad::ExpressionSpinBox(&dialog);
    toleranceBox->setDecimals(8);
    toleranceBox->setRange(1e-8, 1.0);
    toleranceBox->setValue(initial.sewTolerance > 0.0 ? initial.sewTolerance : 1e-5);
    toleranceBox->setKeyboardTracking(false);
    toleranceBox->setToolTip(QStringLiteral("Distanza massima tra i bordi e i vertici da unire"));
    auto *solidBox = new QCheckBox(QStringLiteral("Crea un solido (le superfici devono chiudere un volume)"), &dialog);
    solidBox->setChecked(initial.sewSolid);
    BodyPicker picker(dialog, viewport, replaced, indices, nullptr, list);
    form->addRow(QStringLiteral("Superfici:"), picker.row(list));
    form->addRow(QStringLiteral("Tolleranza:"), toleranceBox);
    form->addRow(QString(), solidBox);
    form->addRow(wrappedNote(QStringLiteral("I bordi comuni (stessi estremi e stessa curva entro la tolleranza) si uniscono; un "
                                           "bordo su cui finisce il vertice di un'altra superficie si divide li'. Creando un solido, "
                                           "una singola superficie cilindrica puo' acquisire i trim dell'apertura che chiude."), &dialog));
    const PreviewScope scope(viewport, dialog, form, replaced);
    const auto current = [&] {
        ExtrusionObject body = initial;
        body.operation = -1;
        body.feature = BodyFeature::Sew;
        QVector<int> inputs;
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->checkState() == Qt::Checked) inputs.append(list->item(row)->data(Qt::UserRole).toInt());
        body.firstBody = inputs.value(0, -1);
        body.booleanTools = inputs.mid(1);
        body.sewTolerance = toleranceBox->value();
        body.sewSolid = solidBox->isChecked();
        return body;
    };
    const auto refresh = [&] {
        const ExtrusionObject body = current();
        if (body.firstBody < 0) {
            scope.label->setText(QStringLiteral("spunta le superfici da cucire"));
            viewport->clearPreview();
            return;
        }
        scope.request(body);
    };
    picker.changed = refresh;
    picker.message = [&](const QString &text, bool) { scope.label->setText(text); };
    picker.install();
    QObject::connect(list, &QListWidget::itemChanged, &dialog, [&] {
        if (!picker.handling()) refresh();
    });
    QObject::connect(toleranceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(solidBox, &QCheckBox::toggled, &dialog, refresh);
    refresh();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    const auto run = [&] {
        picker.stop();
        const ExtrusionObject body = current();
        if (body.firstBody < 0) return QStringLiteral("Spunta le superfici da cucire.");
        return apply(body);
    };
    if (auto *window = qobject_cast<QMainWindow *>(parent)) return runUntilAppliedModeless(window, viewport, dialog, form, buttons, run);
    return runUntilApplied(dialog, form, buttons, run);
}

bool trimDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, ExtrusionObject definition,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    // Superficie e strumento: i corpi che esistono in quel punto della storia, non le feature.
    const QVector<int> used = {definition.firstBody, definition.secondBody};
    const QVector<int> sheets = operandBodies(viewport, replaced, used, [](const ExtrusionObject &) { return true; });
    QVector<int> tools = operandBodies(viewport, replaced, used, [](const ExtrusionObject &) { return true; });
    const int historyEnd = replaced >= 0 ? replaced : int(viewport->extrusions().size());
    for (int index = 0; index < historyEnd; ++index)
        if (viewport->extrusions().at(index).feature == BodyFeature::DatumPlane
            && viewport->extrusions().at(index).datumValid && !tools.contains(index)) tools.append(index);
    if (sheets.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Serve un corpo o una superficie da tagliare."));
        return false;
    }
    const auto label = [&](int index) { return resultBodyLabel(viewport, index); };
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("trim"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *sheetBox = new QComboBox(&dialog), *toolBox = new QComboBox(&dialog), *partBox = new QComboBox(&dialog);
    auto *bothBox = new QCheckBox(QStringLiteral("Rifila entrambi i corpi"), &dialog);
    auto *toolPartBox = new QComboBox(&dialog);
    partBox->setObjectName(QStringLiteral("trimPart"));
    toolPartBox->setObjectName(QStringLiteral("trimToolPart"));
    bothBox->setObjectName(QStringLiteral("trimBoth"));
    bothBox->setChecked(definition.trimBoth);
    bothBox->setToolTip(QStringLiteral("Conserva una parte di ciascun corpo e rimuove da entrambi le porzioni oltre l'intersezione."));
    for (int index : sheets) sheetBox->addItem(label(index));
    for (int index : tools) toolBox->addItem(label(index));
    const int sketchStart = int(tools.size());
    for (const SketchObject &sketch : viewport->sketches()) toolBox->addItem(QStringLiteral("Schizzo: ") + sketch.name);
    const int planeStart = sketchStart + int(viewport->sketches().size());
    for (const QString &plane : planeNames()) toolBox->addItem(plane);
    sheetBox->setCurrentIndex(qMax(0, int(sheets.indexOf(definition.firstBody))));
    if (definition.secondBody >= 0 && tools.contains(definition.secondBody)) toolBox->setCurrentIndex(int(tools.indexOf(definition.secondBody)));
    else if (definition.sketchIndex >= 0 && definition.sketchIndex < viewport->sketches().size())
        toolBox->setCurrentIndex(sketchStart + definition.sketchIndex);
    else if (definition.secondBody < 0 && definition.firstBody >= 0)
        toolBox->setCurrentIndex(planeStart + qBound(0, definition.trimPlane, 2));
    else {
        // Il primo corpo che non e' la superficie.
        for (int k = 0; k < tools.size(); ++k)
            if (tools.at(k) != sheets.at(sheetBox->currentIndex())) {
                toolBox->setCurrentIndex(k);
                break;
            }
    }
    // Superficie e strumento si scelgono anche cliccandoli nella vista: per una
    // funzione nuova senza superficie selezionata prima la superficie, poi lo strumento.
    BodyPicker sheetPicker(dialog, viewport, replaced, sheets, sheetBox), toolPicker(dialog, viewport, replaced, tools, toolBox);
    toolPicker.acceptSketches(sketchStart);
    form->addRow(QStringLiteral("Corpo o superficie da tagliare:"), sheetPicker.row(sheetBox));
    form->addRow(QStringLiteral("Strumento (corpo, superficie, piano o schizzo):"), toolPicker.row(toolBox));
    // Strumento schizzo: proiezione sulla prima faccia incontrata (di default
    // nelle funzioni nuove) o taglio passante lungo la normale (file precedenti).
    auto *methodBox = new QComboBox(&dialog);
    methodBox->addItems({QStringLiteral("Proiezione sulla prima faccia incontrata"), QStringLiteral("Taglio passante lungo la normale (tutte le facce)")});
    methodBox->setCurrentIndex(definition.trimProject || replaced < 0 ? 0 : 1);
    auto *projectModeBox = new QComboBox(&dialog);
    projectModeBox->addItems({QStringLiteral("Togli le parti dentro il profilo"), QStringLiteral("Tieni solo le parti dentro il profilo"),
                              QStringLiteral("Dividi soltanto le facce (linea di divisione)")});
    projectModeBox->setCurrentIndex(qBound(0, definition.projectionMode, 2));
    auto *projectReverse = new QCheckBox(QStringLiteral("Inverti il verso della proiezione"), &dialog);
    projectReverse->setChecked(definition.projectionReverse);
    auto *projectNote = wrappedNote(QStringLiteral("Lo schizzo (senza le entita' di costruzione) va lungo la normale del suo piano, di default verso il corpo, "
                                                   "sulla prima faccia che incontra: le facce sotto restano intere. I tratti che ricalcano bordi "
                                                   "esistenti (anche la corda di un bordo curvo corto, entro 0,001 mm) delimitano la regione senza nuovi tagli."), &dialog);
    auto *projectResult = new FunctionMessage(&dialog);
    projectResult->setStyleSheet(QStringLiteral("color: #9fc6e8;"));
    form->addRow(QStringLiteral("Metodo dello schizzo:"), methodBox);
    form->addRow(QStringLiteral("Taglio:"), projectModeBox);
    form->addRow(QString(), projectReverse);
    form->addRow(projectNote);
    form->addRow(QStringLiteral("Esito:"), projectResult);
    form->addRow(QStringLiteral("Parte da tenere:"), partBox);
    form->addRow(QString(), bothBox);
    form->addRow(QStringLiteral("Parte del secondo corpo:"), toolPartBox);
    auto *choiceHelp = new QLabel(QStringLiteral("Passa il mouse sulle regioni: quella verde verra' mantenuta con un clic."), &dialog);
    choiceHelp->setWordWrap(true);
    choiceHelp->setStyleSheet(QStringLiteral("color: #8ee8a2; font-weight: 600;"));
    choiceHelp->setMaximumWidth(360);
    form->addRow(QStringLiteral("Scelta nella vista:"), choiceHelp);
    const PreviewScope scope(viewport, dialog, form, replaced);
    {
        FunctionMessage *previewLabel = scope.label;
        viewport->setPreviewCallback([previewLabel, projectResult, viewport](const QString &error) {
            previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
            projectResult->setText(error.isEmpty() ? viewport->previewNotice() : QString());
        });
    }
    QVector<SheetPiece> pieces, toolPieces;
    QString piecesKey, toolPiecesKey, piecesError, toolPiecesError;
    EdgePoint clickedKeep, clickedToolKeep;
    bool clickedKeepValid = false, clickedToolKeepValid = false;
    bool first = definition.firstBody >= 0 && replaced >= 0;
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.operation = -1;
        d.feature = BodyFeature::SheetTrim;
        d.firstBody = sheets.at(sheetBox->currentIndex());
        const int tool = toolBox->currentIndex();
        d.secondBody = tool < tools.size() ? tools.at(tool) : -1;
        d.sketchIndex = tool >= sketchStart && tool < planeStart ? tool - sketchStart : -1;
        d.trimPlane = tool >= planeStart ? tool - planeStart : 0;
        d.trimBoth = bothBox->isChecked() && d.secondBody >= 0;
        d.trimProject = d.sketchIndex >= 0 && methodBox->currentIndex() == 0;
        d.projectionMode = projectModeBox->currentIndex();
        d.projectionReverse = projectReverse->isChecked();
        if (clickedKeepValid) d.trimKeep = clickedKeep;
        else if (partBox->currentIndex() >= 0 && partBox->currentIndex() < pieces.size())
            d.trimKeep = pieces.at(partBox->currentIndex()).point;
        if (clickedToolKeepValid) d.trimToolKeep = clickedToolKeep;
        else if (toolPartBox->currentIndex() >= 0 && toolPartBox->currentIndex() < toolPieces.size())
            d.trimToolKeep = toolPieces.at(toolPartBox->currentIndex()).point;
        return d;
    };
    const auto refreshPreview = [&] {
        const ExtrusionObject d = current();
        const int firstPart = partBox->currentIndex();
        if (pieces.size() < 2 || firstPart < 0 || firstPart >= pieces.size()) return;
        ForgeCad::ForgeBody geometry = pieces.at(firstPart).geometry;
        BodyDisplay display = pieces.at(firstPart).display;
        if (d.trimBoth) {
            const int secondPart = toolPartBox->currentIndex();
            if (toolPieces.size() < 2 || secondPart < 0 || secondPart >= toolPieces.size()) return;
            QString error;
            geometry = ForgeCad::forgeSew({geometry, toolPieces.at(secondPart).geometry}, 1e-6, false, &error);
            if (!geometry) {
                scope.label->setText(error);
                return;
            }
            ForgeCad::forgeTessellate(*geometry, 0, display);
        }
        viewport->showTrimPreview(d, geometry, display, replaced);
    };
    bool chaining = false;  // superficie scelta nella vista, lo strumento segue
    std::function<void()> installPartPicker;
    const auto refreshPieces = [&] {
        if (chaining || sheetPicker.handling() || toolPicker.handling()) return;
        viewport->setTrimPartPickCallback(-1, {}, -1, {}, {});
        if (sheetPicker.active() || toolPicker.active() || sheetPicker.waiting() || toolPicker.waiting()) {
            viewport->clearPreview();
            scope.label->setText((sheetPicker.active() || sheetPicker.waiting()) ? QStringLiteral("Clicca nella vista la superficie da tagliare.")
                                                       : QStringLiteral("Clicca nella vista il corpo o lo schizzo che taglia (o scegli un piano nell'elenco)."));
            return;
        }
        const ExtrusionObject d = current();
        // Righe del pannello secondo lo strumento: con lo schizzo proiettato
        // niente parti da scegliere (decide il profilo).
        const bool sketchTool = d.sketchIndex >= 0;
        form->setRowVisible(methodBox, sketchTool);
        for (QWidget *row : {static_cast<QWidget *>(projectModeBox), static_cast<QWidget *>(projectReverse),
                             static_cast<QWidget *>(projectNote), static_cast<QWidget *>(projectResult)})
            form->setRowVisible(row, d.trimProject);
        for (QWidget *row : {static_cast<QWidget *>(partBox), static_cast<QWidget *>(bothBox), static_cast<QWidget *>(toolPartBox),
                             static_cast<QWidget *>(choiceHelp)})
            form->setRowVisible(row, !d.trimProject);
        if (d.trimProject) {
            clickedKeepValid = clickedToolKeepValid = false;
            projectResult->clear();
            scope.request(d);
            return;
        }
        clickedKeepValid = clickedToolKeepValid = false;
        const bool shapeTool = d.secondBody >= 0 && d.secondBody < viewport->extrusions().size()
            && viewport->extrusions().at(d.secondBody).forgeBody;
        bothBox->setEnabled(shapeTool);
        if (!shapeTool) {
            const QSignalBlocker blocker(bothBox);
            bothBox->setChecked(false);
        }
        toolPartBox->setEnabled(bothBox->isChecked());
        choiceHelp->setText(bothBox->isChecked()
            ? QStringLiteral("Passa il mouse sulle regioni di entrambi i corpi: il viewport riconosce automaticamente a quale corpo appartengono. Clicca in verde una parte per ciascun corpo.")
            : QStringLiteral("Passa il mouse sulle regioni del corpo: quella verde verra' mantenuta con un clic; la scelta corrente resta in ambra."));
        const int previousPart = partBox->currentIndex();
        const int previousToolPart = toolPartBox->currentIndex();
        const QString splitKey = QStringLiteral("%1/%2/%3/%4").arg(d.firstBody).arg(d.secondBody).arg(d.trimPlane).arg(d.sketchIndex);
        if (piecesKey != splitKey) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            piecesError.clear();
            pieces = viewport->sheetPieces(d.firstBody, d.secondBody, d.trimPlane, d.sketchIndex, &piecesError);
            QApplication::restoreOverrideCursor();
            piecesKey = splitKey;
        }
        if (bothBox->isChecked() && shapeTool) {
            const QString reverseKey = QStringLiteral("%1/%2").arg(d.secondBody).arg(d.firstBody);
            if (toolPiecesKey != reverseKey) {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                toolPiecesError.clear();
                toolPieces = viewport->sheetPieces(d.secondBody, d.firstBody, 0, -1, &toolPiecesError);
                QApplication::restoreOverrideCursor();
                toolPiecesKey = reverseKey;
            }
        }
        QStringList errors;
        if (pieces.size() < 2)
            errors.append(QStringLiteral("Primo corpo: %1").arg(piecesError.isEmpty()
                ? QStringLiteral("lo strumento non divide la superficie.") : piecesError));
        if (bothBox->isChecked() && toolPieces.size() < 2)
            errors.append(QStringLiteral("Secondo corpo: %1").arg(toolPiecesError.isEmpty()
                ? QStringLiteral("l'intersezione non divide la superficie.") : toolPiecesError));
        const QSignalBlocker blocker(partBox);
        const QSignalBlocker toolBlocker(toolPartBox);
        partBox->clear();
        toolPartBox->clear();
        int chosen = 0;
        double closest = 1e300;
        for (int k = 0; pieces.size() >= 2 && k < pieces.size(); ++k) {
            partBox->addItem(QStringLiteral("Parte %1 (area %2)").arg(k + 1).arg(pieces.at(k).area, 0, 'g', 6));
            // Modifica: la parte piu' vicina al punto di prima.
            const EdgePoint &p = pieces.at(k).point, &q = definition.trimKeep;
            const double d2 = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z);
            if (first && d2 < closest) closest = d2, chosen = k;
        }
        if (!first && previousPart >= 0 && previousPart < pieces.size()) chosen = previousPart;
        first = false;
        partBox->setCurrentIndex(chosen);
        int toolChosen = 0;
        double toolClosest = 1e300;
        for (int k = 0; bothBox->isChecked() && toolPieces.size() >= 2 && k < toolPieces.size(); ++k) {
            toolPartBox->addItem(QStringLiteral("Parte %1 (area %2)").arg(k + 1).arg(toolPieces.at(k).area, 0, 'g', 6));
            const EdgePoint &p = toolPieces.at(k).point, &q = definition.trimToolKeep;
            const double dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (definition.trimBoth && d2 < toolClosest) toolClosest = d2, toolChosen = k;
        }
        if (previousToolPart >= 0 && previousToolPart < toolPieces.size()) toolChosen = previousToolPart;
        toolPartBox->setCurrentIndex(toolChosen);
        installPartPicker();
        if (!errors.isEmpty()) {
            viewport->clearPreview();
            scope.label->setText(errors.join(QLatin1Char('\n')));
            choiceHelp->setText(QStringLiteral("Taglio non pronto: verifica che i corpi si intersechino e che l'intersezione divida ciascuno in almeno due regioni. Le eventuali regioni disponibili restano selezionabili."));
            return;
        }
        scope.label->setText(bothBox->isChecked()
            ? QStringLiteral("Clicca nella vista una parte da tenere per ciascun corpo.")
            : QStringLiteral("Clicca nella vista la parte da tenere, oppure sceglila nell'elenco."));
        refreshPreview();
    };
    installPartPicker = [&] {
        const ExtrusionObject d = current();
        const bool both = bothBox->isChecked();
        viewport->setTrimPartPickCallback(d.firstBody, pieces.size() >= 2 ? pieces : QVector<SheetPiece>{},
                                          both ? d.secondBody : -1, both && toolPieces.size() >= 2 ? toolPieces : QVector<SheetPiece>{},
                                          [&](int side, int piece, EdgePoint point) {
            QVector<SheetPiece> &selectedPieces = side == 1 ? toolPieces : pieces;
            QComboBox *selectedBox = side == 1 ? toolPartBox : partBox;
            if (piece < 0 || piece >= selectedPieces.size()) return;
            if (side == 1) clickedToolKeep = point, clickedToolKeepValid = true;
            else clickedKeep = point, clickedKeepValid = true;
            {
                const QSignalBlocker blocker(selectedBox);
                selectedBox->setCurrentIndex(piece);
            }
            if (pieces.size() < 2 || (bothBox->isChecked() && toolPieces.size() < 2)) return;
            scope.label->setText(QStringLiteral("Parte %1 del %2 corpo scelta nella vista.")
                                 .arg(piece + 1).arg(side == 1 ? QStringLiteral("secondo") : QStringLiteral("primo")));
            if (bothBox->isChecked() && clickedKeepValid && clickedToolKeepValid) {
                choiceHelp->setText(QStringLiteral("Entrambe le parti sono scelte. Le regioni mantenute sono mostrate in ambra; premi OK per applicare."));
            } else if (bothBox->isChecked()) {
                choiceHelp->setText(QStringLiteral("Parte scelta. Passa ora il mouse sulle regioni dell'altro corpo e clicca quella verde da mantenere."));
            }
            refreshPreview();
        });
    };
    for (BodyPicker *picker : {&sheetPicker, &toolPicker}) {
        picker->changed = refreshPieces;
        picker->message = [&](const QString &text, bool) { scope.label->setText(text); };
    }
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (sheetPicker.active()) {
            chaining = sheetPicker.waiting();
            sheetPicker.handle(picked, ref);
            if (chaining && picked && !sheetPicker.active()) toolPicker.start(true);  // poi lo strumento
            chaining = false;
            refreshPieces();
            return;
        }
        toolPicker.handle(picked, ref);
    });
    QObject::connect(sheetBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(toolBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(bothBox, &QCheckBox::toggled, &dialog, refreshPieces);
    QObject::connect(methodBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(projectModeBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(projectReverse, &QCheckBox::toggled, &dialog, refreshPieces);
    QObject::connect(partBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        clickedKeepValid = false;
        refreshPreview();
    });
    QObject::connect(toolPartBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        clickedToolKeepValid = false;
        refreshPreview();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (replaced < 0 && !sheets.contains(definition.firstBody)) sheetPicker.start(true);
    refreshPieces();
    const auto run = [&] {
        sheetPicker.stop();
        toolPicker.stop();
        if (current().trimProject) return apply(current());
        if (pieces.size() < 2) return QStringLiteral("Lo strumento non divide la superficie: scegline un altro.");
        if (current().trimBoth && toolPieces.size() < 2)
            return QStringLiteral("L'intersezione non divide il secondo corpo: non si puo' rifilare da entrambi i lati.");
        return apply(current());
    };
    bool applied = false;
    if (auto *window = qobject_cast<QMainWindow *>(parent)) applied = runUntilAppliedModeless(window, viewport, dialog, form, buttons, run);
    else applied = runUntilApplied(dialog, form, buttons, run);
    viewport->setTrimPartPickCallback(-1, {}, -1, {}, {});
    viewport->setReferencePickCallback(nullptr);
    return applied;
}

QVector<EdgePoint> completeExtensionContours(const ForgeCad::ForgeBody &body, const QVector<EdgePoint> &edges) {
    if (!body) return {};
    ForgeCad::Kernel::Box box;
    for (auto vertex : body->vertices()) box.add(body->vertex(vertex).point);
    const double reach = 1e-3 * std::max(1.0, box.diagonal());
    QVector<EdgePoint> result;
    QSet<int> included;
    for (const auto &reference : edges) {
        const auto edge = ForgeCad::resolveEdgeReference(*body, reference, reach);
        if (!edge.valid()) return {};
        const auto loop = ForgeCad::freeBoundaryLoop(*body, edge);
        if (loop.isEmpty()) return {};
        for (const auto &part : loop) {
            if (included.contains(part.subshape)) continue;
            included.insert(part.subshape);
            result.append(part);
        }
    }
    return result;
}

ExtendDialogResult extendDialog(QWidget *parent, CadViewport *viewport, const QString &title, int base, int hidden, const QVector<EdgePoint> &edges,
                                       double distance, bool linear, bool allowReselect, const std::function<QString(const QVector<EdgePoint> &, double, bool)> &apply) {
    ExtendDialogResult result;
    FunctionDialogPanel dialog(parent);
    dialog.setSizeIdentity(QStringLiteral("surface-extend"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
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
    const QVector<EdgePoint> complete = completeExtensionContours(viewport->extrusions().value(base).forgeBody, edges);
    auto *completeBox = new QCheckBox(QStringLiteral("Estendi i contorni completi"), &dialog);
    completeBox->setEnabled(!complete.isEmpty());
    completeBox->setToolTip(QStringLiteral("Include tutti i bordi collegati ai bordi scelti, anche quelli delle facce molto sottili."));
    form->addRow(QString(), completeBox);
    auto *edgeCount = new GeometryReferenceList(dialog, viewport, QStringLiteral("extendEdges"));
    form->addRow(QStringLiteral("Bordi:"), edgeCount);
    const auto selectedEdges = [&] { return completeBox->isChecked() ? complete : edges; };
    auto *previewLabel = new FunctionMessage(QStringLiteral("in calcolo..."), &dialog);
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
    const auto refresh = [&] {
        const auto selected = selectedEdges();
        edgeCount->setEdges(base, selected);
        previewLabel->setText(QStringLiteral("in calcolo..."));
        viewport->requestExtendPreview(base, selected, distanceBox->value(), typeBox->currentIndex() == 1, hidden);
    };
    viewport->setPreviewCallback([previewLabel](const QString &error) {
        previewLabel->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
    });
    QObject::connect(distanceBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    QObject::connect(typeBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    refresh();
    distanceBox->selectAll();
    QObject::connect(completeBox, &QCheckBox::toggled, &dialog, refresh);
    result.applied = runUntilApplied(dialog, form, buttons, [&] { return apply(selectedEdges(), distanceBox->value(), typeBox->currentIndex() == 1); });
    result.edges = selectedEdges();
    viewport->setPreviewCallback({});
    viewport->clearPreview();
    result.distance = distanceBox->value();
    result.linear = typeBox->currentIndex() == 1;
    return result;
}
