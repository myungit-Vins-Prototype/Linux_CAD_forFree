#include "forgeCad2026_dialogs.h"
#include "forgeCad2026_gui_common.h"
#include "cad_expression.h"
#include "cad_datum.h"
#include "cad_forge.h"
#include <array>

using ForgeCad::Gui::wrappedNote;
using ForgeCad::Gui::runUntilApplied;

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
    dialog.setObjectName(QStringLiteral("trimDialog"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *modeBox = new QComboBox(&dialog);
    modeBox->setObjectName(QStringLiteral("trimMode"));
    modeBox->addItems({QStringLiteral("Con uno strumento"), QStringLiteral("Tra due superfici (solidi)")});
    modeBox->setCurrentIndex(definition.feature == BodyFeature::SurfaceTools && definition.surfaceTool.mode == 5 ? 1 : 0);
    form->addRow(QStringLiteral("Tipo di taglio:"), modeBox);
    const auto betweenSurfaces = [&] { return modeBox->currentIndex() == 1; };
    auto *sheetBox = new QComboBox(&dialog), *toolBox = new QComboBox(&dialog), *partBox = new QComboBox(&dialog);
    auto *bothBox = new QCheckBox(QStringLiteral("Rifila entrambi i corpi"), &dialog);
    auto *toolPartBox = new QComboBox(&dialog);
    sheetBox->setObjectName(QStringLiteral("trimSource"));
    toolBox->setObjectName(QStringLiteral("trimTool"));
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
    auto *toolRow = toolPicker.row(toolBox);
    form->addRow(QStringLiteral("Strumento (corpo, superficie, piano o schizzo):"), toolRow);
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
    bool initialSelectionPending = replaced < 0 && !sheets.contains(definition.firstBody);
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.operation = -1;
        d.feature = BodyFeature::SheetTrim;
        d.firstBody = sheets.at(sheetBox->currentIndex());
        // Only the active mode contributes operands to regeneration and persistence.
        d.surfaceTool = {};
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
        if (betweenSurfaces()) return;
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
        const bool between = betweenSurfaces();
        form->setRowVisible(toolRow, !between);
        for (auto *row : limitRows) form->setRowVisible(row, between);
        const ExtrusionObject d = current();
        form->setRowVisible(methodBox, !between && d.sketchIndex >= 0);
        for (QWidget *row : {static_cast<QWidget *>(projectModeBox), static_cast<QWidget *>(projectReverse),
                             static_cast<QWidget *>(projectNote), static_cast<QWidget *>(projectResult)})
            form->setRowVisible(row, !between && d.trimProject);
        for (QWidget *row : {static_cast<QWidget *>(partBox), static_cast<QWidget *>(bothBox), static_cast<QWidget *>(toolPartBox),
                             static_cast<QWidget *>(choiceHelp)})
            form->setRowVisible(row, !between && !d.trimProject);
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
        if (sheetPicker.active() || toolPicker.active()
            || (initialSelectionPending && (sheetPicker.waiting() || toolPicker.waiting()))) {
            viewport->clearPreview();
            scope.label->setText((sheetPicker.active() || sheetPicker.waiting()) ? QStringLiteral("Clicca nella vista la superficie da tagliare.")
                                                       : QStringLiteral("Clicca nella vista il corpo o lo schizzo che taglia (o scegli un piano nell'elenco)."));
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
            chaining = !betweenSurfaces() && initialSelectionPending && sheetPicker.waiting();
            sheetPicker.handle(picked, ref);
            if (chaining && picked && !sheetPicker.active()) toolPicker.start(true);  // poi lo strumento
            chaining = false;
            refreshPieces();
            return;
        }
        if (toolPicker.handle(picked, ref)) return;
        const int slot = choosingLimit;
        choosingLimit = -1;
        if (betweenSurfaces() && picked && slot >= 0 && ref.kind == 5) limits[slot] = ref;
        refreshPieces();
    });
    for (BodyPicker *picker : {&sheetPicker, &toolPicker})
        picker->starting = [&] { choosingLimit = -1; viewport->cancelReferencePick(); };
    QObject::connect(modeBox, &QComboBox::currentIndexChanged, &dialog, [&] {
        sheetPicker.stop();
        toolPicker.stop();
        choosingLimit = -1;
        initialSelectionPending = false;
        viewport->cancelReferencePick();
        viewport->clearPreview();
        refreshPieces();
    });
    for (int i = 0; i < 2; ++i) {
        QObject::connect(limitPicks[i], &QPushButton::clicked, &dialog, [&, i] {
            sheetPicker.stop();
            toolPicker.stop();
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
    buttons->setObjectName(QStringLiteral("trimButtons"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (replaced < 0 && !sheets.contains(definition.firstBody)) sheetPicker.start(true);
    refreshPieces();
    const auto run = [&] {
        sheetPicker.stop();
        toolPicker.stop();
        if (betweenSurfaces()) {
            if (choosingLimit >= 0) return QStringLiteral("Termina la selezione della faccia.");
            if (limits[0].kind != 5 || limits[1].kind != 5) return QStringLiteral("Scegli due facce limite.");
            const auto body = viewport->extrusions().at(current().firstBody).forgeBody;
            if (!body || body->isSheet()) return QStringLiteral("Scegli un corpo contenente solidi da tagliare.");
            return apply(current());
        }
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
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    return applied;
}
