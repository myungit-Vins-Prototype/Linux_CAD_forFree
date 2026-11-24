#include "forgeCad2026_dialogs.h"
#include "forgeCad2026_gui_common.h"
#include "cad_expression.h"
#include "cad_datum.h"
#include "cad_forge.h"
#include "fk_precision.h"
#include <algorithm>
#include <array>
#include <limits>
#include <QScrollArea>
#include <QTreeWidget>

using ForgeCad::Gui::wrappedNote;
using ForgeCad::Gui::runUntilApplied;

bool trimDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, ExtrusionObject definition,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    // Superficie e strumento: i corpi che esistono in quel punto della storia, non le feature.
    const QVector<int> savedBodies = QVector<int>{definition.firstBody, definition.secondBody} + definition.booleanTools;
    const QVector<int> used = savedBodies;
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
    dialog.setObjectName(QStringLiteral("trimDialog"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *modeBox = new QComboBox(&dialog);
    modeBox->setObjectName(QStringLiteral("trimMode"));
    modeBox->addItems({QStringLiteral("Con uno strumento"), QStringLiteral("Tra due superfici (solidi)"),
                       QStringLiteral("Con più superfici")});
    modeBox->setCurrentIndex(!definition.trimRegions.isEmpty() ? 2 :
                             definition.feature == BodyFeature::SurfaceTools && definition.surfaceTool.mode == 5 ? 1 : 0);
    form->addRow(QStringLiteral("Tipo di taglio:"), modeBox);
    const auto betweenSurfaces = [&] { return modeBox->currentIndex() == 1; };
    const auto multipleSurfaces = [&] { return modeBox->currentIndex() == 2; };
    auto *sheetBox = new QComboBox(&dialog), *toolBox = new QComboBox(&dialog), *partBox = new QComboBox(&dialog);
    auto *bothBox = new QCheckBox(QStringLiteral("Rifila entrambi i corpi"), &dialog);
    auto *toolPartBox = new QComboBox(&dialog);
    sheetBox->setObjectName(QStringLiteral("trimSource"));
    toolBox->setObjectName(QStringLiteral("trimTool"));
    partBox->setObjectName(QStringLiteral("trimPart"));
    toolPartBox->setObjectName(QStringLiteral("trimToolPart"));
    bothBox->setObjectName(QStringLiteral("trimBoth"));
    bothBox->setChecked(definition.trimBoth);
    for (auto *box : {partBox, toolPartBox})
        box->setPlaceholderText(QStringLiteral("Scegli una parte"));
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
    sheetPicker.setActiveButtonText(QStringLiteral("Termina selezione"));
    toolPicker.setActiveButtonText(QStringLiteral("Termina selezione"));
    form->addRow(QStringLiteral("Corpo o superficie da tagliare:"), sheetPicker.row(sheetBox, Qt::Vertical));
    auto *toolRow = toolPicker.row(toolBox, Qt::Vertical);
    form->addRow(QStringLiteral("Strumento (corpo, superficie, piano o schizzo):"), toolRow);
    QVector<int> surfaceTools;
    auto *multiTools = new QListWidget(&dialog);
    multiTools->setObjectName(QStringLiteral("trimMultipleTools"));
    multiTools->setMaximumHeight(144);
    for (int index : sheets) {
        if (!viewport->extrusions().at(index).forgeBody) continue;
        surfaceTools.append(index);
        auto *item = new QListWidgetItem(label(index), multiTools);
        item->setData(Qt::UserRole, index);
        item->setCheckState(index != definition.firstBody && savedBodies.contains(index) ? Qt::Checked : Qt::Unchecked);
    }
    BodyPicker multiPicker(dialog, viewport, replaced, surfaceTools, nullptr, multiTools);
    multiPicker.setActiveButtonText(QStringLiteral("Termina selezione"));
    auto *multiRow = multiPicker.row(multiTools, Qt::Vertical);
    form->addRow(QStringLiteral("Superfici di taglio:"), multiRow);
    auto *regionTree = new QTreeWidget(&dialog);
    regionTree->setObjectName(QStringLiteral("trimRegions"));
    regionTree->setHeaderHidden(true);
    regionTree->setMaximumHeight(235);
    form->addRow(QStringLiteral("Regioni da conservare:"), regionTree);
    std::array<GeometryRef, 2> limits;
    if (betweenSurfaces())
        for (int i = 0; i < qMin(2, int(definition.surfaceTool.targets.size())); ++i) limits[i] = definition.surfaceTool.targets[i];
    std::array<GeometryReferenceList *, 2> limitLists;
    std::array<QPushButton *, 2> limitPicks, limitRemoves;
    std::array<QCheckBox *, 2> reverse;
    QVector<QWidget *> limitRows;
    int choosingLimit = -1;
    for (int i = 0; i < 2; ++i) {
        limitLists[i] = new GeometryReferenceList(dialog, viewport, QStringLiteral("solidTrimLimit%1").arg(i));
        limitLists[i]->setMaximumHeight(52);
        limitPicks[i] = new QPushButton(QStringLiteral("Scegli faccia"), &dialog);
        limitPicks[i]->setObjectName(QStringLiteral("solidTrimPick%1").arg(i));
        limitRemoves[i] = new QPushButton(QStringLiteral("Rimuovi"), &dialog);
        limitRemoves[i]->setObjectName(QStringLiteral("solidTrimRemove%1").arg(i));
        auto *controls = new QWidget(&dialog);
        auto *row = new QHBoxLayout(controls);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(limitPicks[i]);
        row->addWidget(limitRemoves[i]);
        reverse[i] = new QCheckBox(QStringLiteral("Inverti lato da conservare"), &dialog);
        reverse[i]->setObjectName(QStringLiteral("solidTrimReverse%1").arg(i));
        reverse[i]->setChecked(i ? definition.surfaceTool.trimReverseSecond : definition.surfaceTool.trimReverseFirst);
        form->addRow(i ? QStringLiteral("Seconda superficie:") : QStringLiteral("Prima superficie:"), limitLists[i]);
        form->addRow(controls);
        form->addRow(reverse[i]);
        limitRows << limitLists[i] << controls << reverse[i];
    }
    auto *limitNote = wrappedNote(QStringLiteral("Conserva tutti i solidi compresi tra le due facce e chiude i tagli sulle superfici originali. "
        "Scegli le facce nella vista o nella storyboard. Le facce devono attraversare completamente i solidi: estendile prima se necessario."), &dialog);
    form->addRow(limitNote);
    limitRows << limitNote;
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
    setFunctionMessageRole(projectResult, FunctionMessageRole::Information);
    form->addRow(QStringLiteral("Metodo dello schizzo:"), methodBox);
    form->addRow(QStringLiteral("Taglio:"), projectModeBox);
    form->addRow(QString(), projectReverse);
    form->addRow(projectNote);
    form->addRow(QStringLiteral("Esito:"), projectResult);
    form->addRow(QStringLiteral("Parte da tenere:"), partBox);
    form->addRow(QString(), bothBox);
    form->addRow(QStringLiteral("Parte del secondo corpo:"), toolPartBox);
    auto *focusBox = new QComboBox(&dialog);
    focusBox->setObjectName(QStringLiteral("trimRegionFocus"));
    form->addRow(QStringLiteral("Mostra regioni di:"), focusBox);
    form->setRowVisible(focusBox, false);
    auto *choiceHelp = new QLabel(&dialog);
    choiceHelp->setWordWrap(true);
    setFunctionMessageRole(choiceHelp, FunctionMessageRole::Information, true);
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
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("trimButtons"));
    auto *ok = buttons->button(QDialogButtonBox::Ok);
    auto *finishToolPick = buttons->addButton(QStringLiteral("Mostra suddivisione"), QDialogButtonBox::ActionRole);
    finishToolPick->setObjectName(QStringLiteral("trimFinishToolPick"));
    bool initialSelectionPending = replaced < 0 && !sheets.contains(definition.firstBody);
    QVector<int> multiBodies;
    QVector<QVector<SheetPiece>> multiPieces;
    bool multiReciprocal = false;
    QString multiError;
    const auto selectedBodies = [&] {
        const int source = sheets.at(sheetBox->currentIndex());
        QVector<int> checked;
        for (int i = 0; i < multiTools->count(); ++i) {
            const auto *item = multiTools->item(i);
            const int index = item->data(Qt::UserRole).toInt();
            if (index != source && item->checkState() == Qt::Checked) checked.append(index);
        }
        QVector<int> result{source};
        for (int index : savedBodies) {
            if (checked.removeOne(index)) result.append(index);
        }
        return result + checked;
    };
    const auto chosenRegions = [&] {
        QVector<QVector<SpatialPoint>> result(multiBodies.size());
        for (int i = 0; i < regionTree->topLevelItemCount(); ++i) {
            const auto *group = regionTree->topLevelItem(i);
            for (int j = 0; j < group->childCount(); ++j) {
                if (group->child(j)->checkState(0) != Qt::Checked) continue;
                const EdgePoint &p = multiPieces.at(i).at(j).point;
                result[i].append({p.x, p.y, p.z});
            }
        }
        return result;
    };
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.operation = -1;
        d.feature = BodyFeature::SheetTrim;
        d.firstBody = sheets.at(sheetBox->currentIndex());
        // Only the active mode contributes operands to regeneration and persistence.
        d.surfaceTool = {};
        d.trimRegions.clear();
        d.booleanTools.clear();
        if (multipleSurfaces()) {
            const auto bodies = selectedBodies();
            d.secondBody = bodies.value(1, -1);
            d.booleanTools = bodies.mid(2);
            d.sketchIndex = -1;
            d.trimPlane = 0;
            d.trimProject = false;
            d.trimBoth = bothBox->isChecked();
            d.trimKeep = {};
            d.trimToolKeep = {};
            d.trimRegions = bodies == multiBodies ? chosenRegions() : QVector<QVector<SpatialPoint>>(bodies.size());
            return d;
        }
        if (betweenSurfaces()) {
            d.feature = BodyFeature::SurfaceTools;
            d.surfaceTool.mode = 5;
            for (const auto &ref : limits) if (ref.kind == 5) d.surfaceTool.targets.append(ref);
            d.surfaceTool.trimReverseFirst = reverse[0]->isChecked();
            d.surfaceTool.trimReverseSecond = reverse[1]->isChecked();
            d.secondBody = d.sketchIndex = -1;
            d.trimPlane = 0;
            d.trimBoth = d.trimProject = false;
            return d;
        }
        const int tool = toolBox->currentIndex();
        d.secondBody = tool < tools.size() ? tools.at(tool) : -1;
        d.sketchIndex = tool >= sketchStart && tool < planeStart ? tool - sketchStart : -1;
        d.trimPlane = tool >= planeStart ? tool - planeStart : 0;
        d.trimBoth = bothBox->isChecked() && d.secondBody >= 0 && viewport->extrusions().at(d.secondBody).forgeBody;
        d.trimProject = d.sketchIndex >= 0 && methodBox->currentIndex() == 0;
        d.projectionMode = projectModeBox->currentIndex();
        d.projectionReverse = projectReverse->isChecked();
        if (partBox->currentIndex() >= 0 && partBox->currentIndex() < pieces.size())
            d.trimKeep = pieces.at(partBox->currentIndex()).point;
        if (toolPartBox->currentIndex() >= 0 && toolPartBox->currentIndex() < toolPieces.size())
            d.trimToolKeep = toolPieces.at(toolPartBox->currentIndex()).point;
        return d;
    };
    const auto updateRegionView = [&] {
        QVector<QVector<int>> kept;
        if (multipleSurfaces()) {
            kept.resize(regionTree->topLevelItemCount());
            for (int i = 0; i < kept.size(); ++i) {
                const auto *group = regionTree->topLevelItem(i);
                for (int j = 0; j < group->childCount(); ++j)
                    if (group->child(j)->checkState(0) == Qt::Checked) kept[i].append(j);
            }
        } else {
            kept = {{partBox->currentIndex()}, {toolPartBox->currentIndex()}};
        }
        viewport->setTrimPartView(focusBox->currentData().isValid() ? focusBox->currentData().toInt() : -1, kept);
    };
    const auto setRegionBodies = [&](const QVector<int> &bodies) {
        const QVariant previous = focusBox->currentData();
        const QSignalBlocker blocker(focusBox);
        focusBox->clear();
        focusBox->addItem(QStringLiteral("Tutti i corpi"), -1);
        for (int body : bodies) focusBox->addItem(label(body), body);
        focusBox->setCurrentIndex(qMax(0, focusBox->findData(previous)));
        form->setRowVisible(focusBox, !bodies.isEmpty());
        updateRegionView();
    };
    const auto refreshPreview = [&] {
        if (betweenSurfaces() || multipleSurfaces()) return;
        updateRegionView();
        const ExtrusionObject d = current();
        const int firstPart = partBox->currentIndex();
        const int secondPart = toolPartBox->currentIndex();
        const bool divided = pieces.size() >= 2 && (!d.trimBoth || toolPieces.size() >= 2);
        const bool selected = firstPart >= 0 && (!d.trimBoth || secondPart >= 0);
        ok->setEnabled(divided && selected);
        if (!divided) return;
        if (!selected) {
            viewport->showTrimPreview(d, {}, {}, replaced);
            scope.label->setText(d.trimBoth ? QStringLiteral("Scegli una parte da conservare per ciascun corpo.")
                                           : QStringLiteral("Scegli la parte da conservare nella vista o nell’elenco."));
            return;
        }
        ForgeCad::ForgeBody geometry = pieces.at(firstPart).geometry;
        BodyDisplay display = pieces.at(firstPart).display;
        if (d.trimBoth) {
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
    const auto refreshMultiplePreview = [&] {
        if (!multipleSurfaces() || sheetPicker.active() || multiPicker.active() || !multiError.isEmpty()) return;
        updateRegionView();
        QVector<ForgeCad::ForgeBody> kept;
        for (int i = 0; i < regionTree->topLevelItemCount(); ++i) {
            const auto *group = regionTree->topLevelItem(i);
            for (int j = 0; j < group->childCount(); ++j)
                if (group->child(j)->checkState(0) == Qt::Checked) kept.append(multiPieces[i][j].geometry);
        }
        ok->setEnabled(!kept.isEmpty());
        if (kept.isEmpty()) {
            viewport->showTrimPreview(current(), {}, {}, replaced);
            scope.label->setText(QStringLiteral("Spunta almeno una regione da conservare."));
            return;
        }
        QString error;
        const auto geometry = kept.size() == 1 ? kept.front() : ForgeCad::forgeSew(kept,
            ForgeCad::Kernel::scaledAccuracy(1e-6), false, &error);
        if (!geometry) {
            viewport->showTrimPreview(current(), {}, {}, replaced);
            scope.label->setText(error);
            return;
        }
        BodyDisplay display;
        ForgeCad::forgeTessellate(*geometry, 0, display);
        viewport->showTrimPreview(current(), geometry, display, replaced);
    };
    const auto refreshMultiple = [&] {
        const auto bodies = selectedBodies();
        const bool reciprocal = bothBox->isChecked();
        {
            const QSignalBlocker block(multiTools);
            for (int i = 0; i < multiTools->count(); ++i) {
                auto *item = multiTools->item(i);
                const bool enabled = item->data(Qt::UserRole).toInt() != bodies.front();
                item->setFlags(enabled ? item->flags() | Qt::ItemIsEnabled : item->flags() & ~Qt::ItemIsEnabled);
                if (!enabled) item->setCheckState(Qt::Unchecked);
            }
        }
        if (multiBodies != bodies || multiReciprocal != reciprocal) {
            const bool restore = multiBodies.isEmpty() && replaced >= 0
                && bodies == savedBodies && reciprocal == definition.trimBoth;
            multiBodies = bodies;
            multiReciprocal = reciprocal;
            multiPieces.clear();
            multiError.clear();
            const QSignalBlocker block(regionTree);
            regionTree->clear();
            if (bodies.size() < 2) {
                multiError = QStringLiteral("Scegli almeno una superficie di taglio.");
            } else {
                QVector<ForgeCad::ForgeBody> operands;
                for (int body : bodies) operands.append(viewport->extrusions().at(body).forgeBody);
                QApplication::setOverrideCursor(Qt::WaitCursor);
                multiPieces = ForgeCad::forgeTrimSurfacePieces(operands, reciprocal, &multiError);
                QApplication::restoreOverrideCursor();
            }
            for (int i = 0; i < multiPieces.size(); ++i) {
                auto *group = new QTreeWidgetItem(regionTree, {label(multiBodies[i])});
                QVector<int> checked;
                const auto &pieces = multiPieces.at(i);
                if (restore && i < definition.trimRegions.size()) {
                    QVector<ForgeCad::ForgeBody> geometries;
                    for (const auto &piece : pieces) geometries.append(piece.geometry);
                    checked = ForgeCad::forgeTrimRegionIndices(geometries, definition.trimRegions.at(i));
                }
                for (int j = 0; j < pieces.size(); ++j) {
                    const auto &piece = pieces.at(j);
                    auto *item = new QTreeWidgetItem(group, {QStringLiteral("Parte %1 (area %2)")
                        .arg(j + 1).arg(piece.area, 0, 'g', 6)});
                    item->setCheckState(0, checked.contains(j) ? Qt::Checked : Qt::Unchecked);
                }
                group->setExpanded(true);
            }
        }
        if (!multiError.isEmpty()) {
            ok->setEnabled(false);
            viewport->showTrimPreview(current(), {}, {}, replaced);
            scope.label->setText(multiError);
            return;
        }
        form->setRowVisible(regionTree, true);
        form->setRowVisible(choiceHelp, true);
        viewport->setTrimPartPickCallback(multiBodies, multiPieces, [&](int body, int part, EdgePoint) {
            if (auto *group = regionTree->topLevelItem(body))
                if (auto *item = group->child(part)) {
                    regionTree->setCurrentItem(item);
                    item->setCheckState(0, item->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
                }
        });
        setRegionBodies(multiBodies.mid(0, multiPieces.size()));
        refreshMultiplePreview();
    };
    const auto pickingOperands = [&] {
        return sheetPicker.active() || toolPicker.active() || multiPicker.active();
    };
    bool chaining = false;  // superficie scelta nella vista, lo strumento segue
    std::function<void()> installPartPicker;
    const auto refreshPieces = [&] {
        if (chaining || sheetPicker.handling() || toolPicker.handling() || multiPicker.handling()) return;
        viewport->setTrimPartPickCallback({}, {}, {});
        form->setRowVisible(focusBox, false);
        const bool between = betweenSurfaces(), multiple = multipleSurfaces();
        form->setRowVisible(toolRow, !between && !multiple);
        form->setRowVisible(multiRow, multiple);
        form->setRowVisible(regionTree, false);
        for (auto *row : limitRows) form->setRowVisible(row, between);
        const ExtrusionObject d = current();
        form->setRowVisible(methodBox, !between && d.sketchIndex >= 0);
        for (QWidget *row : {static_cast<QWidget *>(projectModeBox), static_cast<QWidget *>(projectReverse),
                             static_cast<QWidget *>(projectNote), static_cast<QWidget *>(projectResult)})
            form->setRowVisible(row, !between && d.trimProject);
        const bool picking = pickingOperands();
        const bool choosingRegions = !between && !d.trimProject && !picking;
        form->setRowVisible(partBox, choosingRegions && !multiple);
        form->setRowVisible(toolPartBox, choosingRegions && !multiple && d.trimBoth);
        form->setRowVisible(bothBox, !between && !d.trimProject);
        form->setRowVisible(choiceHelp, choosingRegions && !multiple);
        finishToolPick->setVisible(multiPicker.active());
        ok->setVisible(!multiPicker.active());
        ok->setEnabled(!picking);
        bothBox->setText(multiple ? QStringLiteral("Rifila tutti i corpi") : QStringLiteral("Rifila entrambi i corpi"));
        QVector<GeometryRef> marks;
        for (int i = 0; i < 2; ++i) {
            limitLists[i]->setReferences(limits[i].kind == 5 ? QVector<GeometryRef>{limits[i]} : QVector<GeometryRef>{});
            limitPicks[i]->setText(choosingLimit == i ? QStringLiteral("Termina selezione") : QStringLiteral("Scegli faccia"));
            if (between && limits[i].kind == 5) marks.append(limits[i]);
        }
        if (between) {
            GeometryRef source; source.kind = 11; source.index = d.firstBody;
            marks.append(source);
        }
        viewport->setReferenceMarks(marks);
        QVector<int> pickableTools;
        if (multiPicker.active()) {
            pickableTools = surfaceTools;
            pickableTools.removeAll(d.firstBody);
        }
        viewport->setPickBodies(pickableTools);
        if (picking) {
            viewport->clearPreview();
            scope.label->setText(sheetPicker.active() ? QStringLiteral("Clicca nella vista la superficie da tagliare.")
                : multiPicker.active() ? QStringLiteral("Clicca i corpi di taglio nella vista.") :
                  QStringLiteral("Clicca nella vista il corpo o lo schizzo che taglia (o scegli un piano nell’elenco)."));
            return;
        }
        if (between) {
            const auto body = viewport->extrusions().at(d.firstBody).forgeBody;
            if (!body || body->isSheet()) {
                viewport->clearPreview();
                scope.label->setText(QStringLiteral("Il taglio tra due superfici richiede un corpo contenente solidi."));
            } else if (choosingLimit >= 0 || limits[0].kind != 5 || limits[1].kind != 5) {
                viewport->clearPreview();
                scope.label->setText(QStringLiteral("Scegli le due facce limite."));
            } else scope.request(d);
            return;
        }
        if (multiple) {
            bothBox->setEnabled(true);
            bothBox->setToolTip(QStringLiteral("Taglia ciascun corpo con le altre superfici e scegli quali regioni conservare."));
            choiceHelp->setText(QStringLiteral("Clicca una regione verde per aggiungerla o toglierla. Ambra indica le regioni da conservare. "
                                               "Se si coprono, isola un corpo nell’elenco sopra."));
            refreshMultiple();
            return;
        }
        bothBox->setToolTip(QStringLiteral("Conserva una parte di ciascun corpo e rimuove da entrambi le porzioni oltre l’intersezione."));
        if (d.trimProject) {
            projectResult->clear();
            scope.request(d);
            return;
        }
        const bool shapeTool = d.secondBody >= 0 && d.secondBody < viewport->extrusions().size()
            && viewport->extrusions().at(d.secondBody).forgeBody;
        bothBox->setEnabled(shapeTool);
        if (!shapeTool) {
            const QSignalBlocker blocker(bothBox);
            bothBox->setChecked(false);
        }
        toolPartBox->setEnabled(bothBox->isChecked());
        choiceHelp->setText(QStringLiteral("Clicca la regione verde per conservarla. Ambra indica la scelta corrente. "
                                           "Se le regioni si coprono, isola un corpo nell’elenco sopra."));
        const int previousPart = partBox->currentIndex();
        const int previousToolPart = toolPartBox->currentIndex();
        const QString splitKey = QStringLiteral("%1/%2/%3/%4").arg(d.firstBody).arg(d.secondBody).arg(d.trimPlane).arg(d.sketchIndex);
        const bool sameSplit = piecesKey == splitKey;
        const bool restore = piecesKey.isEmpty() && replaced >= 0 && definition.feature == BodyFeature::SheetTrim
            && definition.trimRegions.isEmpty() && !definition.trimProject
            && d.firstBody == definition.firstBody && d.secondBody == definition.secondBody
            && d.trimPlane == definition.trimPlane && d.sketchIndex == definition.sketchIndex;
        if (!sameSplit) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            piecesError.clear();
            pieces = viewport->sheetPieces(d.firstBody, d.secondBody, d.trimPlane, d.sketchIndex, &piecesError);
            QApplication::restoreOverrideCursor();
            piecesKey = splitKey;
        }
        bool sameToolSplit = false;
        if (d.trimBoth && shapeTool) {
            const QString reverseKey = QStringLiteral("%1/%2").arg(d.secondBody).arg(d.firstBody);
            sameToolSplit = toolPiecesKey == reverseKey;
            if (!sameToolSplit) {
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
        const auto fillParts = [](QComboBox *box, const QVector<SheetPiece> &regions,
                                  int previous, const EdgePoint *saved) {
            const QSignalBlocker block(box);
            box->clear();
            int selected = previous;
            double closest = std::numeric_limits<double>::infinity();
            for (int i = 0; regions.size() >= 2 && i < regions.size(); ++i) {
                const auto &region = regions.at(i);
                box->addItem(QStringLiteral("Parte %1 (area %2)").arg(i + 1).arg(region.area, 0, 'g', 6));
                if (saved) {
                    const auto &p = region.point;
                    const double dx = p.x - saved->x, dy = p.y - saved->y, dz = p.z - saved->z;
                    const double distance = dx * dx + dy * dy + dz * dz;
                    if (distance < closest) {
                        closest = distance;
                        selected = i;
                    }
                }
            }
            box->setCurrentIndex(selected < box->count() ? selected : -1);
        };
        fillParts(partBox, pieces, sameSplit ? previousPart : -1, restore ? &definition.trimKeep : nullptr);
        fillParts(toolPartBox, d.trimBoth ? toolPieces : QVector<SheetPiece>{}, sameToolSplit ? previousToolPart : -1,
                  restore && definition.trimBoth ? &definition.trimToolKeep : nullptr);
        installPartPicker();
        if (!errors.isEmpty()) {
            ok->setEnabled(false);
            viewport->showTrimPreview(current(), {}, {}, replaced);
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
                                          [&](int side, int piece, EdgePoint) {
            const QVector<SheetPiece> &selectedPieces = side == 1 ? toolPieces : pieces;
            QComboBox *selectedBox = side == 1 ? toolPartBox : partBox;
            if (piece < 0 || piece >= selectedPieces.size()) return;
            {
                const QSignalBlocker blocker(selectedBox);
                selectedBox->setCurrentIndex(piece);
            }
            if (pieces.size() < 2 || (bothBox->isChecked() && toolPieces.size() < 2)) {
                updateRegionView();
                return;
            }
            scope.label->setText(QStringLiteral("Parte %1 del %2 corpo scelta nella vista.")
                                 .arg(piece + 1).arg(side == 1 ? QStringLiteral("secondo") : QStringLiteral("primo")));
            refreshPreview();
        });
        QVector<int> bodies;
        if (pieces.size() >= 2) bodies.append(d.firstBody);
        if (both && toolPieces.size() >= 2) bodies.append(d.secondBody);
        setRegionBodies(bodies);
    };
    const std::array<BodyPicker *, 3> pickers{&sheetPicker, &toolPicker, &multiPicker};
    for (BodyPicker *picker : pickers) {
        picker->changed = refreshPieces;
        picker->message = [&](const QString &text, bool) { scope.label->setText(text); };
    }
    multiPicker.changed = [&] {
        refreshPieces();
        if (!multiPicker.active() && regionTree->isVisibleTo(&dialog))
            if (auto *scroll = dialog.findChild<QScrollArea *>(QStringLiteral("functionDialogScroll")))
                scroll->ensureWidgetVisible(regionTree);
    };
    QObject::connect(finishToolPick, &QPushButton::clicked, &dialog, [&] {
        multiPicker.stop();
        multiPicker.changed();
    });
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (sheetPicker.active()) {
            chaining = !betweenSurfaces() && !multipleSurfaces() && initialSelectionPending && sheetPicker.waiting();
            sheetPicker.handle(picked, ref);
            if (chaining && picked && !sheetPicker.active()) toolPicker.start(true);  // poi lo strumento
            chaining = false;
            refreshPieces();
            return;
        }
        if (toolPicker.handle(picked, ref) || multiPicker.handle(picked, ref)) return;
        const int slot = choosingLimit;
        choosingLimit = -1;
        if (betweenSurfaces() && picked && slot >= 0 && ref.kind == 5) limits[slot] = ref;
        refreshPieces();
    });
    for (BodyPicker *picker : pickers)
        picker->starting = [&, picker] {
            for (auto *other : pickers) if (other != picker) other->stop();
            choosingLimit = -1;
            viewport->cancelReferencePick();
        };
    QObject::connect(modeBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        for (auto *picker : pickers) picker->stop();
        choosingLimit = -1;
        initialSelectionPending = false;
        viewport->cancelReferencePick();
        viewport->clearPreview();
        refreshPieces();
    });
    for (int i = 0; i < 2; ++i) {
        QObject::connect(limitPicks[i], &QPushButton::clicked, &dialog, [&, i] {
            for (auto *picker : pickers) picker->stop();
            initialSelectionPending = false;
            viewport->cancelReferencePick();
            if (choosingLimit == i) choosingLimit = -1;
            else {
                choosingLimit = i;
                const QString error = viewport->beginReferencePick(ForgeCad::DatumRoleFace, replaced);
                if (!error.isEmpty()) {
                    choosingLimit = -1;
                    scope.label->setText(error);
                    return;
                }
            }
            refreshPieces();
        });
        QObject::connect(limitRemoves[i], &QPushButton::clicked, &dialog, [&, i] {
            limits[i] = {};
            choosingLimit = -1;
            viewport->cancelReferencePick();
            refreshPieces();
        });
        QObject::connect(reverse[i], &QCheckBox::toggled, &dialog, refreshPieces);
    }
    QObject::connect(sheetBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(toolBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(multiTools, &QListWidget::itemChanged, &dialog, refreshPieces);
    QObject::connect(regionTree, &QTreeWidget::itemChanged, &dialog, refreshMultiplePreview);
    QObject::connect(focusBox, &QComboBox::currentIndexChanged, &dialog, updateRegionView);
    QObject::connect(bothBox, &QCheckBox::toggled, &dialog, refreshPieces);
    QObject::connect(methodBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(projectModeBox, &QComboBox::currentIndexChanged, &dialog, refreshPieces);
    QObject::connect(projectReverse, &QCheckBox::toggled, &dialog, refreshPieces);
    QObject::connect(partBox, &QComboBox::currentIndexChanged, &dialog, refreshPreview);
    QObject::connect(toolPartBox, &QComboBox::currentIndexChanged, &dialog, refreshPreview);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (replaced < 0 && !sheets.contains(definition.firstBody)) sheetPicker.start(true);
    refreshPieces();
    const auto run = [&] {
        if (pickingOperands()) return QStringLiteral("Completa la selezione dei corpi.");
        if (betweenSurfaces()) {
            if (choosingLimit >= 0) return QStringLiteral("Termina la selezione della faccia.");
            if (limits[0].kind != 5 || limits[1].kind != 5) return QStringLiteral("Scegli due facce limite.");
            const auto body = viewport->extrusions().at(current().firstBody).forgeBody;
            if (!body || body->isSheet()) return QStringLiteral("Scegli un corpo contenente solidi da tagliare.");
            return apply(current());
        }
        if (multipleSurfaces()) {
            refreshPieces();
            if (!multiError.isEmpty()) return multiError;
            const auto d = current();
            if (std::all_of(d.trimRegions.cbegin(), d.trimRegions.cend(), [](const auto &group) { return group.isEmpty(); }))
                return QStringLiteral("Spunta almeno una regione da conservare.");
            return apply(d);
        }
        if (current().trimProject) return apply(current());
        if (pieces.size() < 2) return QStringLiteral("Lo strumento non divide la superficie: scegline un altro.");
        if (current().trimBoth && toolPieces.size() < 2)
            return QStringLiteral("L'intersezione non divide il secondo corpo: non si puo' rifilare da entrambi i lati.");
        if (partBox->currentIndex() < 0 || (current().trimBoth && toolPartBox->currentIndex() < 0))
            return QStringLiteral("Scegli le regioni da conservare.");
        return apply(current());
    };
    bool applied = false;
    if (auto *window = qobject_cast<QMainWindow *>(parent)) applied = runUntilAppliedModeless(window, viewport, dialog, form, buttons, run);
    else applied = runUntilApplied(dialog, form, buttons, run);
    viewport->setTrimPartPickCallback({}, {}, {});
    viewport->setPickBodies({});
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    return applied;
}
