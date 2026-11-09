#include "forgeCad2026_dialogs.h"
#include "forgeCad2026_dialogs_common.h"
#include "cad_surface_tools.h"
#include "cad_surface_poles_model.h"
#include "cad_surface_poles_session.h"
#include "cad_surface_poles_interaction.h"
#include "cad_datum.h"
#include "cad_feature_dependencies.h"
#include "cad_topology_ref.h"
#include "cad_expression.h"
#include "fk_bspline_surface.h"
#include "fk_surface_refit.h"
#include "fk_history.h"
#include "fk_precision.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QItemSelectionModel>
#include <QLocale>
#include <stdexcept>
#include <cmath>
#include <array>
#include <QHeaderView>
namespace {
class PoleNumberDelegate final : public QStyledItemDelegate {
  public:
    explicit PoleNumberDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QString displayText(const QVariant &value, const QLocale &locale) const override {
        bool valid = false;
        const double number = locale.toDouble(value.toString(), &valid);
        return valid ? locale.toString(number, 'g', 10) : value.toString();
    }
};
} // namespace
bool surfaceToolDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
    if (initial.surfaceTool.mode == 5) return trimDialog(window,viewport,title,replaced,initial,apply);
    if (initial.surfaceTool.mode >= 3) return surfaceMappingDialog(window,viewport,title,replaced,initial,apply);
    using namespace ForgeCad;
    using namespace Kernel;
    ExtrusionObject definition = initial;
    definition.feature = BodyFeature::SurfaceTools;
    definition.operation = -1;
    const int mode = definition.surfaceTool.mode, owner = replaced < 0 ? viewport->extrusions().size() : replaced;
    if (mode == 0 && replaced < 0) {
        definition.blendEdges.clear();
        definition.extentRef = {};
        definition.surfaceTool.targets.clear();
    }
    if (mode == 0 && definition.surfaceTool.targets.isEmpty() && definition.extentRef.kind >= 0)
        definition.surfaceTool.targets = {definition.extentRef};
    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(mode == 0 ? QStringLiteral("surface-extend-to") : mode == 1 ? QStringLiteral("surface-network") : QStringLiteral("surface-poles"));
    if (viewport) ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setObjectName("surfaceToolDialog");
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    dialog.setMinimumWidth(mode == 2 ? 580 : 440);
    auto *form = dialog.createScrollableForm();
    PreviewScope preview(viewport, dialog, form, replaced);
    auto *status = new FunctionMessage(&dialog);
    status->setObjectName("surfaceToolStatus");
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("surfaceToolButtons");
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *tolerance = new ExpressionSpinBox(&dialog);
    tolerance->setObjectName("surfaceToolTolerance");
    tolerance->setDecimals(9);
    tolerance->setRange(1e-9, .01);
    tolerance->setValue(definition.surfaceTool.tolerance);
    tolerance->setKeyboardTracking(false);
    auto *base = new QComboBox(&dialog), *targets = new QComboBox(&dialog);
    auto *edges = new QListWidget(&dialog);
    edges->setMaximumHeight(64);
    base->setObjectName("surfaceToolBase");
    edges->setObjectName("surfaceToolEdge");
    targets->setObjectName("surfaceToolTarget");
    QVector<int> sources;
    QVector<GeometryRef> destinations;
    int extensionPick = 0;
    std::function<void(int)> beginExtensionPick;
    auto *maximum = new ExpressionSpinBox(&dialog);
    maximum->setObjectName("surfaceToolMaximum");
    maximum->setRange(1e-6, 1e6);
    maximum->setDecimals(6);
    maximum->setValue(definition.surfaceTool.maximum);
    maximum->setKeyboardTracking(false);
    auto *linear = new QCheckBox(QStringLiteral("Prolungamento tangente lineare"), &dialog);
    linear->setChecked(definition.extendLinear);
    auto *poles = new QTableView(&dialog);
    auto *poleModel = new SurfacePolesModel(poles);
    poles->setModel(poleModel);
    QVector<int> displayedRows;
    poles->setObjectName("surfacePolesTable");
    poles->setItemDelegate(new PoleNumberDelegate(poles));
    poles->setSelectionBehavior(QAbstractItemView::SelectRows);
    poles->setSelectionMode(QAbstractItemView::ExtendedSelection);
    poles->setMinimumHeight(210);
    poles->verticalHeader()->hide();
    poles->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    poles->verticalHeader()->setDefaultSectionSize(24);
    for (int c = 0; c < 6; ++c) {
        poles->horizontalHeader()->setSectionResizeMode(c, c >= 2 && c <= 4 ? QHeaderView::Stretch : QHeaderView::Fixed);
        poles->setColumnWidth(c, c < 2 ? 56 : 72);
    }

    auto *poleOrientation = new QComboBox(&dialog);
    poleOrientation->setObjectName("surfacePolesOrientation");
    poleOrientation->addItems({QStringLiteral("Globale (X, Y, Z)"),QStringLiteral("Locale (U, V, normale)")});
    auto *poleBox = new QPushButton(QStringLiteral("Selezione rettangolare"), &dialog);
    poleBox->setObjectName("surfacePolesBoxSelection");
    poleBox->setCheckable(true);
    poleBox->setToolTip(QStringLiteral("Attiva il riquadro oppure tieni premuto Alt e trascina. Sullo sfondo, senza Alt, trascina per ruotare."));
    auto *keepConnected=new QCheckBox(QStringLiteral("Mantieni i bordi connessi (G0)"),&dialog);
    keepConnected->setObjectName("surfacePolesKeepConnected");keepConnected->setChecked(definition.surfaceTool.poleContinuity>=1);
    auto *keepTangent=new QCheckBox(QStringLiteral("Mantieni la tangenza esistente (G1)"),&dialog);
    keepTangent->setObjectName("surfacePolesKeepTangent");keepTangent->setChecked(definition.surfaceTool.poleContinuity==2);
    auto *continuityInfo = new FunctionMessage(&dialog);
    continuityInfo->setObjectName("surfacePolesContinuityInfo");
    continuityInfo->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    continuityInfo->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    continuityInfo->setMinimumHeight(96);
    auto *checkContinuity=new QPushButton(QStringLiteral("Verifica continuità"),&dialog);checkContinuity->setObjectName("surfacePolesCheckContinuity");
    auto *poleSelection = new FunctionMessage(&dialog);
    poleSelection->setObjectName("surfacePolesSelection");
    auto *poleGridInfo = new QLabel(&dialog);
    poleGridInfo->setObjectName("surfacePolesGridInfo");
    poleGridInfo->setWordWrap(true);
    auto *refitEnabled = new QCheckBox(QStringLiteral("Semplifica la griglia (approssima la superficie)"), &dialog);
    refitEnabled->setObjectName("surfacePolesRefitEnabled");
    refitEnabled->setChecked(definition.surfaceTool.refitU > 0);
    auto *refitControls = new QWidget(&dialog);
    auto *refitForm = new QFormLayout(refitControls);
    refitForm->setContentsMargins(0,0,0,0);
    QSpinBox *refitCounts[2];
    for (int direction=0; direction<2; ++direction) {
        auto *row = new QWidget(refitControls); auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0);
        auto *slider = new QSlider(Qt::Horizontal,row); auto *count = new QSpinBox(row);
        slider->setRange(4,surfaceRefitMaxPoles); count->setRange(4,surfaceRefitMaxPoles); count->setKeyboardTracking(false);
        count->setObjectName(direction==0?"surfacePolesRefitU":"surfacePolesRefitV");
        const int initialCount=direction==0?definition.surfaceTool.refitU:definition.surfaceTool.refitV;
        count->setValue(initialCount>0?initialCount:direction==0?8:32); slider->setValue(count->value());
        QObject::connect(slider,&QSlider::valueChanged,count,&QSpinBox::setValue);
        QObject::connect(count,qOverload<int>(&QSpinBox::valueChanged),slider,&QSlider::setValue);
        refitCounts[direction]=count;
        layout->addWidget(slider,1); layout->addWidget(count);
        refitForm->addRow(direction==0?QStringLiteral("Numero poli U:"):QStringLiteral("Numero poli V:"),row);
    }
    auto *refitTolerance = new ExpressionSpinBox(refitControls);
    refitTolerance->setObjectName("surfacePolesRefitTolerance");
    refitTolerance->setDecimals(7); refitTolerance->setRange(1e-7,1000); refitTolerance->setValue(definition.surfaceTool.refitTolerance);
    refitTolerance->setKeyboardTracking(false);
    refitForm->addRow(QStringLiteral("Scostamento ammesso:"),refitTolerance);
    auto *refitApply = new QPushButton(QStringLiteral("Aggiorna griglia (azzera gli spostamenti)"),refitControls);
    refitApply->setObjectName("surfacePolesRefitApply");refitForm->addRow(refitApply);
    auto *refitAuto = new QPushButton(QStringLiteral("Adatta i poli allo scostamento"), refitControls);
    refitAuto->setObjectName("surfacePolesRefitAuto");
    refitForm->addRow(refitAuto);
    refitForm->addRow(ForgeCad::Gui::wrappedNote(QStringLiteral("Lo scostamento è vincolante. Adatta aumenta U/V fino al rispetto del limite (massimo 256 per direzione). Cambiare griglia azzera gli spostamenti."),refitControls));
    refitControls->setVisible(refitEnabled->isChecked());
    QTimer refitTimer;refitTimer.setSingleShot(true);refitTimer.setInterval(180);
    bool refitPending = false, refitSearching = false;
    SurfacePolesRefitSearch refitSearch(&dialog);
    std::function<void()> searchGrid, cancelRefitSearch;
    QTimer searchTimer;
    searchTimer.setSingleShot(true); searchTimer.setInterval(180);
    double refitDeviation = 0;
    QString poleLoadError;
    SurfacePolesSession poleSession;
    auto *faceList=new QListWidget(&dialog);
    faceList->setObjectName("surfacePolesFaces");faceList->setMaximumHeight(112);
    auto *faceControls=new QWidget(&dialog);auto *faceLayout=new QHBoxLayout(faceControls);faceLayout->setContentsMargins(0,0,0,0);
    auto *pickFaces=new QPushButton(QStringLiteral("Scegli facce nella vista"),faceControls);
    pickFaces->setObjectName("surfacePolesPickFaces");
    auto *allFaces=new QPushButton(QStringLiteral("Tutte"),faceControls),*noFaces=new QPushButton(QStringLiteral("Nessuna"),faceControls);
    allFaces->setObjectName("surfacePolesAllFaces");noFaces->setObjectName("surfacePolesNoFaces");
    faceLayout->addWidget(pickFaces,1);faceLayout->addWidget(allFaces);faceLayout->addWidget(noFaces);
    int listedBody=-1;
    bool choosingFaces=false;
    QMap<int,SurfacePoleFace> faceEdits;
    std::function<void()> loadSource;
    bool poleDragging = false;
    std::function<void()> cancelPoleInteraction;
    const BSplineSurface *spline = nullptr;
    bool filling = false;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(60);
    std::function<void()> refresh;
    const auto schedule = [&] { timer.start(); };
    QObject::connect(&timer, &QTimer::timeout, &dialog, [&] {
        if (refresh)
            refresh();
    });
    const auto fail = [&](const QString &text) {
        status->setText(text);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        viewport->clearPreview();
        preview.label->setText(mode == 0 ? QStringLiteral("in attesa delle selezioni")
                                                : QStringLiteral("non disponibile"));
    };
    const auto candidates = operandBodies(viewport, replaced, {definition.firstBody}, [&](const ExtrusionObject &b) {
        if (!b.forgeBody) return false;
        if (mode!=2) return b.forgeBody->isSheet();
        for (const auto face:b.forgeBody->faces()) if (!surfaceBasisHash(b.forgeBody,face).isEmpty()) return true;
        return false;
    });
    if (mode != 1) {
        sources = candidates;
        for (int index : sources)
            base->addItem(resultBodyLabel(viewport, index));
        if (sources.isEmpty()) {
            QMessageBox::information(
                window, title,
                mode == 2
                    ? QStringLiteral("Serve un corpo con almeno una faccia convertibile in NURBS.")
                    : QStringLiteral("Serve una superficie aperta da estendere."));
            return false;
        }
        const int wanted = sources.indexOf(definition.firstBody);
        base->setCurrentIndex(wanted < 0 ? 0 : wanted);
        if (mode != 0)
            form->addRow(QStringLiteral("Superficie:"), base);
    }
    auto *rowList = new GeometryReferenceList(dialog, viewport, "surfaceNetworkU"),
         *columnList = new GeometryReferenceList(dialog, viewport, "surfaceNetworkV");
    CurvePicker picker;
    picker.viewport = viewport;
    picker.owner = owner;
    picker.status = [&](const QString &text, bool) { status->setText(text); };
    const auto familyControls = [&](GeometryReferenceList *list, QVector<GeometryRef> *refs, const QString &name) {
        auto *box = new QWidget(&dialog);
        auto *layout = new QHBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *pick = new QPushButton(QStringLiteral("Scegli %1 nella vista").arg(name), box);
        pick->setObjectName(name == "U" ? "surfaceNetworkPickU" : "surfaceNetworkPickV");
        auto *remove = new QPushButton(QStringLiteral("Togli"), box), *up = new QPushButton(QStringLiteral("Su"), box),
             *down = new QPushButton(QStringLiteral("Giu'"), box);
        for (auto *button : {pick, remove, up, down})
            layout->addWidget(button);
        QObject::connect(pick, &QPushButton::clicked, &dialog, [&, refs] {
            if (picker.active == refs)
                picker.stop();
            else
                picker.start(refs);
        });
        QObject::connect(remove, &QPushButton::clicked, &dialog, [&, refs, list] {
            const int row = list->currentRow();
            if (row >= 0 && row < refs->size())
                refs->removeAt(row);
            schedule();
        });
        const auto move = [&, refs, list](int delta) {
            const int row = list->currentRow();
            if (row >= 0 && row + delta >= 0 && row + delta < refs->size()) {
                refs->swapItemsAt(row, row + delta);
                schedule();
            }
        };
        QObject::connect(up, &QPushButton::clicked, &dialog, [move] { move(-1); });
        QObject::connect(down, &QPushButton::clicked, &dialog, [move] { move(1); });
        return box;
    };
    auto *pickEdge = new QPushButton(QStringLiteral("Scegli il bordo nella vista"), &dialog);
    pickEdge->setObjectName("surfaceToolPickEdge");
    auto *pickReference = new QPushButton(QStringLiteral("Scegli il riferimento nella vista"), &dialog);
    pickReference->setObjectName("surfaceToolPickTarget");
    auto *targetList = new GeometryReferenceList(dialog, viewport, "surfaceToolTargets");
    auto *removeTarget = new QPushButton(QStringLiteral("Togli il riferimento selezionato"), &dialog);
    removeTarget->setObjectName("surfaceToolRemoveTarget");
    if (mode == 1) {
        form->addRow(ForgeCad::Gui::wrappedNote(
            QStringLiteral("Due famiglie ordinate di curve aperte: ogni U deve incrociare ogni V una sola volta. Ogni cella interpola i "
                           "quattro lati; continuita' interna G0. Da 2 a 16 curve per famiglia. Le curve originali restano disponibili."),
            &dialog));
        form->addRow(QStringLiteral("Curve U:"), rowList);
        form->addRow(familyControls(rowList, &definition.ruledFirst, "U"));
        form->addRow(QStringLiteral("Curve V:"), columnList);
        form->addRow(familyControls(columnList, &definition.ruledSecond, "V"));
        picker.changed = schedule;
        picker.install();
    } else if (mode == 0) {
        form->addRow(QStringLiteral("1. Bordo da estendere:"), edges);
        form->addRow(pickEdge);
        form->addRow(QStringLiteral("2. Riferimenti da raggiungere:"), targetList);
        form->addRow(pickReference);
        form->addRow(removeTarget);
        form->addRow(QStringLiteral("Aggiungi dall'elenco:"), targets);
        form->addRow(QStringLiteral("Distanza massima di ricerca:"), maximum);
        form->addRow(linear);
        form->addRow(ForgeCad::Gui::wrappedNote(
            QStringLiteral("Clicca tutte le facce del target nella vista; un secondo clic le toglie. Esc o Termina selezione mostra "
                           "l'anteprima. I riferimenti devono delimitare insieme tutto il prolungamento, senza tagliare la superficie iniziale."),
            &dialog));
        for (int i = 0; i < 3; ++i) {
            GeometryRef ref;
            ref.kind = 1;
            ref.index = i;
            destinations.append(ref);
            targets->addItem(planeNames().value(i));
        }
        for (int b = 0; b < owner; ++b) {
            const auto &body = viewport->extrusions()[b];
            if (body.feature == BodyFeature::DatumPlane) {
                GeometryRef ref;
                ref.kind = 8;
                ref.index = b;
                ref.featureId = body.featureId;
                destinations.append(ref);
                targets->addItem(body.name);
            }
            if (body.forgeBody)
                for (const auto face : body.forgeBody->faces()) {
                    Vec3 point;
                    if (!faceInteriorPoint(*body.forgeBody, face, point))
                        continue;
                    GeometryRef ref;
                    ref.kind = 5;
                    ref.index = b;
                    ref.featureId = body.featureId;
                    ref.point = faceReference(*body.forgeBody, face, point);
                    destinations.append(ref);
                    targets->addItem(QStringLiteral("%1 / F%2").arg(body.name).arg(face.index));
                }
        }
        targets->setPlaceholderText(QStringLiteral("Aggiungi un piano o una faccia"));
        targets->setCurrentIndex(-1);
        beginExtensionPick = [&](int step) {
            timer.stop();
            viewport->cancelReferencePick();
            viewport->clearPreview();
            extensionPick = step;
            viewport->setPickBodies(step == 1 ? sources : QVector<int>());
            const auto error = viewport->beginReferencePick(step == 1 ? DatumRoleCurve | DatumRoleLine : DatumRolePlane | DatumRoleFace, owner);
            if (refresh)
                refresh();
            if (!error.isEmpty()) {
                extensionPick = 0;
                viewport->setPickBodies({});
                fail(error);
            }
        };
        QObject::connect(pickEdge, &QPushButton::clicked, &dialog, [&] { beginExtensionPick(1); });
        QObject::connect(pickReference, &QPushButton::clicked, &dialog, [&] {
            if (extensionPick == 2) {
                extensionPick = 0;
                viewport->cancelReferencePick();
                refresh();
            } else beginExtensionPick(2);
        });
        QObject::connect(removeTarget, &QPushButton::clicked, &dialog, [&] {
            const int row = targetList->currentRow();
            if (row >= 0 && row < definition.surfaceTool.targets.size()) {
                definition.surfaceTool.targets.removeAt(row);
                definition.extentRef = definition.surfaceTool.targets.value(0);
                refresh();
            }
        });
        viewport->setReferencePickCallback([&](bool accepted, GeometryRef ref) {
            const int step = extensionPick;
            extensionPick = 0;
            viewport->cancelReferencePick();
            viewport->setPickBodies({});
            if (!accepted) {
                refresh();
                return;
            }
            if (step == 1) {
                const int body = ref.kind == 4 ? resultBodyForPick(viewport, sources, ref.index) : -1;
                const auto shape = body >= 0 ? viewport->extrusions()[body].forgeBody : ForgeBody{};
                const auto edge = shape ? resolveEdgeReference(*shape, ref.point, 1e-5) : EdgeId{};
                if (!edge.valid() || !shape->isLaminar(edge)) {
                    beginExtensionPick(1);
                    status->setText(QStringLiteral("Scegli un bordo libero di una superficie aperta."));
                    return;
                }
                definition.firstBody = body;
                definition.blendEdges = {ref.point};
                definition.extentRef = {};
                definition.surfaceTool.targets.clear();
                {
                    QSignalBlocker guard(base);
                    base->setCurrentIndex(sources.indexOf(body));
                }
                edges->clear();
                edges->addItem(QStringLiteral("%1 / E%2").arg(resultBodyLabel(viewport, body)).arg(edge.index));
                targets->setCurrentIndex(-1);
                beginExtensionPick(2);
                return;
            }
            if (step != 2)
                return;
            if (ref.kind != 1 && ref.kind != 5 && ref.kind != 8) {
                beginExtensionPick(2);
                status->setText(QStringLiteral("Scegli un piano o una faccia finale."));
                return;
            }
            auto &selected = definition.surfaceTool.targets;
            int found = -1;
            for (int i = 0; i < selected.size(); ++i)
                if (selected[i].kind == ref.kind && selected[i].index == ref.index &&
                    (ref.kind != 5 || selected[i].point.subshape == ref.point.subshape)) found = i;
            if (found >= 0) selected.removeAt(found);
            else if (selected.size() < 256) selected.append(ref);
            definition.extentRef = selected.value(0);
            beginExtensionPick(2);
        });
    } else {
        form->addRow(ForgeCad::Gui::wrappedNote(
            QStringLiteral("Clicca un polo per muoverlo liberamente, oppure trascina una freccia per vincolarlo a un asse. "
                           "Ctrl/Cmd aggiunge o toglie poli, Maiusc aggiunge. Alt + trascinamento oppure Selezione rettangolare selezionano un gruppo di poli. Trascina sullo sfondo per ruotare. "
                           "L'anteprima si calcola al rilascio. Esc durante il trascinamento annulla lo spostamento."),
            &dialog));
        form->addRow(QStringLiteral("Facce da modificare:"),faceList);
        form->addRow(ForgeCad::Gui::wrappedNote(QStringLiteral("Le superfici analitiche vengono convertite automaticamente in NURBS esatte prima della modifica. Bordi e fori vengono conservati."),&dialog));
        form->addRow(faceControls);
        form->addRow(ForgeCad::Gui::wrappedNote(QStringLiteral("Le griglie selezionate si modificano insieme. U/V valgono per tutte le facce. "
            "I bordi condivisi devono restare coincidenti: seleziona insieme i poli corrispondenti delle facce adiacenti."),&dialog));
        form->addRow(refitEnabled);
        form->addRow(refitControls);
        form->addRow(QStringLiteral("Orientamento assi:"), poleOrientation);
        form->addRow(poleBox);
        form->addRow(ForgeCad::Gui::wrappedNote(QStringLiteral("Alt + trascinamento: riquadro. Ctrl: aggiungi/togli; Maiusc: aggiungi. Trascina sullo sfondo per ruotare."), &dialog));
        form->addRow(keepConnected);form->addRow(keepTangent);
        form->addRow(ForgeCad::Gui::wrappedNote(QStringLiteral("Le facce adiacenti vengono incluse. I poli collegati seguono anche se non selezionati (verdi). G1 conserva i giunti già tangenti; gli spigoli restano netti. I giunti con griglie non compatibili sono protetti."),&dialog));
        form->addRow(checkContinuity);form->addRow(continuityInfo);
        form->addRow(poleSelection);
        form->addRow(poleGridInfo);
        form->addRow(poles);
        auto *reload = new QPushButton(QStringLiteral("Ricarica griglia (azzera le modifiche)"), &dialog);
        reload->setObjectName("surfacePolesReload");
        form->addRow(reload);
        QObject::connect(reload, &QPushButton::clicked, &dialog, [&] {
            definition.surfaceTool.basis.clear();
            definition.surfaceTool.edits.clear();
            for (auto &entry:definition.surfaceTool.poleFaces) {entry.basis.clear();entry.edits.clear();}
            // Rigenera esplicitamente i riferimenti dalle facce ancora selezionate.
            definition.surfaceTool.poleFaces.clear();faceEdits.clear();
            loadSource();
        });
    }
    const auto updatePoleOverlay = [&](bool recomputeAxes = true) {
        if(mode!=2 || !spline || !poleModel->rowCount()) return;
        QVector<int> selected;
        for(const auto &index:poles->selectionModel()->selectedRows()) selected.append(index.row());
        if (selected.size() > 4096) {
            poleSelection->setText(QStringLiteral("Seleziona fino a 4096 poli per trascinarli insieme."));
            return;
        }
        int active=poles->currentIndex().row();
        if(!selected.contains(active)) active=selected.value(0,-1);
        QVector<int> visible=selected;for(int row:poleModel->automaticRows())if(!visible.contains(row))visible.append(row);
        const auto display = poleModel->cage(visible);
        displayedRows = display.rows;
        QVector<QVector3D> cage;
        for (int row : displayedRows) {
            const auto values = poleModel->values(row);
            for (int c = 0; c < 4; ++c)
                if (!std::isfinite(values[c]) || (c == 3 && values[c] <= 0)) return;
            cage.append(QVector3D(values[0],values[1],values[2]));
        }
        viewport->setSurfacePoleOverlay(cage,display.u,display.v,displayedRows.indexOf(active),display.grids);
        QSet<int> followers;for(int row:poleModel->automaticRows()){const int i=displayedRows.indexOf(row);if(i>=0)followers.insert(i);}
        viewport->setSurfacePoleFollowers(followers);
        QVector<int> selectedOverlay;
        for (int row : selected) selectedOverlay.append(displayedRows.indexOf(row));
        poleGridInfo->setText(QStringLiteral("%1 facce; %2 poli totali. Gabbia: %3 poli visualizzati.\n%4")
            .arg(faceList->count()==1?1:definition.surfaceTool.poleFaces.size()).arg(poleModel->rowCount()).arg(displayedRows.size()).arg(poleSession.details()));
        if (definition.surfaceTool.refitU > 0)
            poleGridInfo->setText(poleGridInfo->text() + QStringLiteral("\nScostamento campionato iniziale: %1 mm. La verifica campionata non certifica un massimo globale.").arg(refitDeviation,0,'g',6));
        auto axes = std::array<QVector3D,3>{{{1,0,0},{0,1,0},{0,0,1}}};
        if(!recomputeAxes) axes=viewport->surfacePoleAxes();
        // Assi locali al parametro di Greville del polo attivo. La geometria
        // della gabbia e' aggiornata senza rifilare o tassellare la superficie.
        bool singular=false;
        bool local=poleOrientation->currentIndex()==1;
        if(recomputeAxes && local && active>=0) {
            const auto greville=[](const std::vector<double>&knots,int degree,int pole) {
                double t=0;for(int k=1;k<=degree;++k)t+=knots[pole+k];return t/degree;
            };
            try {
                const auto current = poleModel->modifiedSurface(active);
                const auto support=poleModel->surfaceForRow(active);
                const int row=poleModel->faceRow(active),countV=support->vPoleCount();
                const double u=greville(support->uKnots(),support->uDegree(),row/countV),v=greville(support->vKnots(),support->vDegree(),row%countV);
                Vec3 d[4];current->evaluate(u,v,1,d);
                const auto x=normalized(d[Surface::derivativeIndex(1,0,1)]);
                const auto shape=viewport->extrusions()[definition.firstBody].forgeBody;
                const int index=poleModel->faceIndex(active);
                const auto face=index<0?shape->faces().front():resolveSurfacePoleFace(*shape,definition.surfaceTool.poleFaces[index].face);
                const double sense=shape->face(face).sense?1.:-1.;
                const auto z=sense*normalized(cross(d[Surface::derivativeIndex(1,0,1)],d[Surface::derivativeIndex(0,1,1)]));
                const auto y=normalized(cross(z,x));
                for(int k=0;k<3;++k) axes[k]=QVector3D((k==0?x:k==1?y:z).x(),(k==0?x:k==1?y:z).y(),(k==0?x:k==1?y:z).z());
            } catch(const std::exception &) {
                local=false;
                singular=true;
            }
        }
        QString label;
        if (active>=0) {
            const auto support=poleModel->surfaceForRow(active);const int row=poleModel->faceRow(active);
            const int face=poleModel->faceIndex(active);
            label=(face<0?QString():QStringLiteral("F%1 / ").arg(definition.surfaceTool.poleFaces[face].face.subshape))+
                QStringLiteral("P(%1,%2)").arg(row/support->vPoleCount()+1).arg(row%support->vPoleCount()+1);
        }
        viewport->setSurfacePoleSelection(selectedOverlay,axes,local,label);
        poleSelection->setText(QStringLiteral("%1 poli selezionati%2").arg(selected.size()).arg(singular?QStringLiteral(" — normale singolare: assi globali"):local?QStringLiteral(" — assi del polo attivo"):QString()));
    };
    loadSource = [&] {
        if (mode == 1)
            return;
        filling = true;
        if(cancelRefitSearch)cancelRefitSearch();
        if(cancelPoleInteraction)cancelPoleInteraction();
        if (mode == 2) {
            spline = nullptr;poleLoadError.clear();
            poleModel->setSurface({});
            displayedRows.clear();
            viewport->setSurfacePoleOverlay({});
        }
        const auto prior = definition.blendEdges;
        definition.firstBody = sources[base->currentIndex()];
        const auto shape = viewport->extrusions()[definition.firstBody].forgeBody;
        if (mode == 0) {
            edges->clear();
            const auto selected = prior.isEmpty() ? EdgeId{} : resolveEdgeReference(*shape, prior.front(), 1e-5);
            if (selected.valid() && shape->isLaminar(selected))
                edges->addItem(QStringLiteral("%1 / E%2").arg(resultBodyLabel(viewport, definition.firstBody)).arg(selected.index));
            else
                definition.blendEdges.clear();
        } else {
            if (listedBody!=definition.firstBody) {
                listedBody=definition.firstBody;faceEdits.clear();poleSession.reset();
                QSignalBlocker guard(faceList);faceList->clear();
                for (const auto face:shape->faces()) {
                    if (surfaceBasisHash(shape,face).isEmpty()) continue;
                    auto *item=new QListWidgetItem(QStringLiteral("F%1").arg(face.index),faceList);
                    item->setData(Qt::UserRole,face.index);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
                    bool selected=definition.surfaceTool.poleFaces.isEmpty();
                    for (const auto &prior:definition.surfaceTool.poleFaces) {
                        const auto resolved=resolveSurfacePoleFace(*shape,prior.face);
                        if (resolved==face) {selected=true;faceEdits.insert(face.index,prior);}
                    }
                    item->setCheckState(selected?Qt::Checked:Qt::Unchecked);
                }
            }
            QVector<FaceId> selected;
            for (int i=0;i<faceList->count();++i) if (faceList->item(i)->checkState()==Qt::Checked)
                selected.append(FaceId(faceList->item(i)->data(Qt::UserRole).toInt()));
            if(definition.surfaceTool.poleContinuity&&!selected.isEmpty())try {
                const std::vector<FaceId> seeds(selected.cbegin(),selected.cend());
                const auto connected=connectedSurfacePoleFaces(*shape,seeds);selected=QVector<FaceId>(connected.cbegin(),connected.cend());
                for(const auto face:selected)if(faceEdits.contains(face.index)&&
                    std::none_of(definition.surfaceTool.poleFaces.cbegin(),definition.surfaceTool.poleFaces.cend(),[&](const auto &entry){return entry.face.subshape==face.index;}))
                    definition.surfaceTool.poleFaces.append(faceEdits.value(face.index));
                QSignalBlocker guard(faceList);
                for(int i=0;i<faceList->count();++i)faceList->item(i)->setCheckState(selected.contains(FaceId(faceList->item(i)->data(Qt::UserRole).toInt()))?Qt::Checked:Qt::Unchecked);
            }catch(const std::exception &error){filling=false;poleLoadError=QString::fromUtf8(error.what());fail(poleLoadError);return;}
            const auto error=poleSession.load(shape,selected,definition.surfaceTool,*poleModel);
            if (!error.isEmpty()) {filling=false;poleLoadError=error;fail(error);return;}
            refitDeviation=poleSession.deviation();
            const auto support=poleModel->surfaceForRow(0);spline=support.get();
            for (int c=0;c<poleModel->columnCount();++c) {
                poles->horizontalHeader()->setSectionResizeMode(c,c>=poleModel->coordinateColumn()&&c<poleModel->coordinateColumn()+3?QHeaderView::Stretch:QHeaderView::Fixed);
                poles->setColumnWidth(c,c<poleModel->coordinateColumn()?56:72);
            }
        }
        filling = false;
        schedule();
    };
    refresh = [&] {
        if (filling || (mode==2 && poleDragging))
            return;
        buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        status->clear();
        definition.surfaceTool.tolerance = tolerance->value();
        if (mode == 1) {
            rowList->setReferences(definition.ruledFirst);
            columnList->setReferences(definition.ruledSecond);
            viewport->setReferenceMarks(definition.ruledFirst + definition.ruledSecond);
            if (definition.ruledFirst.size() < 2 || definition.ruledSecond.size() < 2) {
                fail(QStringLiteral("Scegli almeno due curve per ciascuna famiglia."));
                return;
            }
        } else if (mode == 0) {
            const bool hasEdge = !definition.blendEdges.isEmpty();
            targetList->setReferences(definition.surfaceTool.targets);
            for (int i = 0; i < definition.surfaceTool.targets.size(); ++i)
                if (definition.surfaceTool.targets[i].kind == 5)
                    targetList->item(i)->setText(targetList->item(i)->text() + QStringLiteral(" / F%1").arg(definition.surfaceTool.targets[i].point.subshape));
            removeTarget->setEnabled(!definition.surfaceTool.targets.isEmpty());
            targets->setEnabled(hasEdge);
            pickReference->setEnabled(hasEdge);
            pickEdge->setText(extensionPick == 1 ? QStringLiteral("Seleziona un bordo nella vista…") : QStringLiteral("Scegli il bordo nella vista"));
            if (!hasEdge) {
                viewport->setReferenceMarks({});
                fail(QStringLiteral("1. Clicca nella vista il bordo libero da estendere."));
                return;
            }
            definition.extentRef = definition.surfaceTool.targets.value(0);
            pickReference->setText(extensionPick == 2 ? QStringLiteral("Termina selezione riferimenti") : QStringLiteral("Scegli i riferimenti nella vista"));
            definition.surfaceTool.maximum = maximum->value();
            definition.extendLinear = linear->isChecked();
            GeometryRef boundary;
            boundary.kind = 4;
            boundary.index = definition.firstBody;
            boundary.featureId = viewport->extrusions()[definition.firstBody].featureId;
            boundary.point = definition.blendEdges.front();
            QVector<GeometryRef> marks{boundary};
            marks += definition.surfaceTool.targets;
            viewport->setReferenceMarks(marks);
            if (extensionPick == 1) {
                fail(QStringLiteral("1. Clicca un altro bordo libero da estendere."));
                return;
            }
            if (definition.surfaceTool.targets.isEmpty()) {
                fail(QStringLiteral("2. Clicca nella vista le facce o i piani da raggiungere."));
                return;
            }
            if (extensionPick == 2) {
                viewport->clearPreview();
                preview.label->setText(QStringLiteral("termina la selezione per vedere l'anteprima"));
                status->setText(QStringLiteral("Clicca altri riferimenti; un secondo clic li toglie. Esc o Termina per l'anteprima, OK per confermare."));
                return;
            }
        } else {
            if (!spline || !poleModel->rowCount()) {
                fail(poleLoadError.isEmpty()?QStringLiteral("Griglia NURBS non disponibile."):poleLoadError);
                return;
            }
            if (refitSearching) { fail(QStringLiteral("Calcolo della griglia entro lo scostamento richiesto…"));return; }
            if (refitPending) { fail(QStringLiteral("Aggiornamento della griglia in attesa del nuovo numero di poli…"));return; }
            definition.surfaceTool.refitTolerance=refitTolerance->value();
            const auto continuityError=poleSession.constrain(*poleModel,definition.surfaceTool);
            if(definition.surfaceTool.poleContinuity)continuityInfo->setText(poleSession.continuityText());
            if(!continuityError.isEmpty()){fail(continuityError);return;}
            const auto error = poleSession.store(*poleModel,definition.surfaceTool);
            if (!error.isEmpty()) {
                fail(error);
                return;
            }
            updatePoleOverlay();
            if (definition.surfaceTool.refitU > 0 && refitDeviation > definition.surfaceTool.refitTolerance) {
                fail(QStringLiteral("Scostamento campionato %1 mm oltre %2 mm: premi Adatta i poli allo scostamento oppure aumenta U/V.")
                    .arg(refitDeviation,0,'g',6).arg(definition.surfaceTool.refitTolerance,0,'g',6));return;
            }
            if (definition.surfaceTool.edits.isEmpty() && definition.surfaceTool.refitU == 0 &&
                std::none_of(definition.surfaceTool.poleFaces.cbegin(),definition.surfaceTool.poleFaces.cend(),[](const SurfacePoleFace &f){return !f.edits.isEmpty();})) {
                fail(QStringLiteral("Sposta un polo o modifica un peso."));
                return;
            }
        }
        preview.request(definition);
    };
    const auto applyRefit = [&] {
        refitTimer.stop();timer.stop();refitPending=false;
        definition.surfaceTool.refitU=refitEnabled->isChecked()?refitCounts[0]->value():0;
        definition.surfaceTool.refitV=refitEnabled->isChecked()?refitCounts[1]->value():0;
        definition.surfaceTool.edits.clear();faceEdits.clear();
        for(auto &face:definition.surfaceTool.poleFaces) face.edits.clear();
        loadSource();refresh();
    };
    QObject::connect(refitEnabled,&QCheckBox::toggled,&dialog,[&](bool enabled){refitControls->setVisible(enabled);applyRefit();if(enabled && searchGrid)searchGrid();});
    QObject::connect(refitApply,&QPushButton::clicked,&dialog,applyRefit);
    QObject::connect(&refitTimer,&QTimer::timeout,&dialog,applyRefit);
    for (auto *count : refitCounts) QObject::connect(count,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&]{
        if(refitEnabled->isChecked()){if(cancelRefitSearch)cancelRefitSearch();refitPending=true;timer.stop();refitTimer.start();refresh();}
    });
    QObject::connect(refitTolerance,&QDoubleSpinBox::valueChanged,&dialog,[&] {
        if(cancelRefitSearch)cancelRefitSearch();
        schedule();
        if(refitEnabled->isChecked())searchTimer.start();
    });
    SurfacePolesInteraction drag(viewport,poles,displayedRows,&dialog);
    cancelPoleInteraction=[&]{drag.cancel();};
    QObject::connect(poleBox, &QPushButton::toggled, &dialog, [&](bool enabled) { drag.setBoxSelection(enabled); });
    drag.message=[&](const QString &text){status->setText(text);};
    drag.started = [&] {
        poleDragging=true;timer.stop();viewport->clearPreview();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        preview.label->setText(QStringLiteral("rilascia il mouse per calcolare l'anteprima"));
    };
    drag.moved = [&] {
        poleSession.setDrivers(drag.draggedRows());
        const auto error=poleSession.constrain(*poleModel,definition.surfaceTool);
        continuityInfo->setText(error.isEmpty()?poleSession.continuityText():error);
        updatePoleOverlay(false);
    };
    drag.finished = [&] { poleDragging=false;if(!filling){updatePoleOverlay();refresh();} };
    cancelRefitSearch = [&] {
        refitSearch.cancel();searchTimer.stop();refitSearching=false;
        poles->setEnabled(true);drag.setEnabled(mode==2);
    };
    searchGrid = [&] {
        if(mode!=2 || !refitEnabled->isChecked() || !spline ||
           (!refitPending && refitDeviation<=refitTolerance->value()))return;
        QVector<FaceId> faces;
        std::vector<BSplineSurfacePtr> surfaces;
        const auto shape=viewport->extrusions()[definition.firstBody].forgeBody;
        for(int i=0;i<faceList->count();++i)if(faceList->item(i)->checkState()==Qt::Checked) {
            const FaceId face(faceList->item(i)->data(Qt::UserRole).toInt());
            faces.append(face);
            surfaces.push_back(poleSession.sourceSurface(face));
        }
        if(faces.isEmpty())return;
        refitTimer.stop();timer.stop();refitPending=false;refitSearching=true;
        drag.setEnabled(false);poles->setEnabled(false);refresh();
        refitSearch.finished = [&,faces,surfaces](const SurfaceRefitGrid &grid,const QString &error) {
            refitSearching=false;poles->setEnabled(true);drag.setEnabled(true);
            if(!error.isEmpty()){fail(error);return;}
            poleSession.cacheRefit(faces,surfaces,grid);
            { QSignalBlocker u(refitCounts[0]),v(refitCounts[1]);
              refitCounts[0]->setValue(grid.u);refitCounts[1]->setValue(grid.v); }
            // Gli slider seguono gli spinner anche quando i segnali del fit
            // sono bloccati: la ricerca non deve avviare un nuovo fit manuale.
            for(auto *slider:refitControls->findChildren<QSlider *>()) {
                const int direction=slider->parentWidget()==refitCounts[0]->parentWidget()?0:1;
                QSignalBlocker guard(slider);slider->setValue(refitCounts[direction]->value());
            }
            applyRefit();
            if(grid.sampledDeviation>refitTolerance->value())
                fail(QStringLiteral("Limite di 256 poli raggiunto: scarto campionato %1 mm oltre %2 mm. Conferma disabilitata.")
                     .arg(grid.sampledDeviation,0,'g',6).arg(refitTolerance->value(),0,'g',6));
        };
        const Kernel::PrecisionScope accuracy(viewport->calculationAccuracy());
        refitSearch.start(std::move(surfaces),refitCounts[0]->value(),refitCounts[1]->value(),refitTolerance->value());
    };
    QObject::connect(refitAuto,&QPushButton::clicked,&dialog,searchGrid);
    QObject::connect(&searchTimer,&QTimer::timeout,&dialog,searchGrid);
    const auto setContinuity=[&](int mode) {
        if(filling)return;
        drag.cancel();if(cancelRefitSearch)cancelRefitSearch();
        const auto error=poleSession.store(*poleModel,definition.surfaceTool);
        if(!error.isEmpty()){
            QSignalBlocker a(keepConnected),b(keepTangent);
            keepConnected->setChecked(definition.surfaceTool.poleContinuity>=1);keepTangent->setChecked(definition.surfaceTool.poleContinuity==2);fail(error);return;
        }
        definition.surfaceTool.poleContinuity=mode;
        {QSignalBlocker a(keepConnected),b(keepTangent);keepConnected->setChecked(mode>=1);keepTangent->setChecked(mode==2);}
        loadSource();
        if(!poleSession.constrain(*poleModel,definition.surfaceTool).isEmpty())poleSession.setDrivers({});
        refresh();
    };
    QObject::connect(keepConnected,&QCheckBox::toggled,&dialog,[&](bool on){setContinuity(on?(keepTangent->isChecked()?2:1):0);});
    QObject::connect(keepTangent,&QCheckBox::toggled,&dialog,[&](bool on){setContinuity(on?2:keepConnected->isChecked()?1:0);});
    QObject::connect(checkContinuity,&QPushButton::clicked,&dialog,[&] {
        try {
            const auto shape=viewport->extrusions()[definition.firstBody].forgeBody;
            std::vector<FaceId> faces;for(int i=0;i<faceList->count();++i)if(faceList->item(i)->checkState()==Qt::Checked)faces.push_back(FaceId(faceList->item(i)->data(Qt::UserRole).toInt()));
            const auto report=checkSurfacePoleContinuity(*shape,*shape,faces,definition.surfaceTool.tolerance);
            refresh();
            continuityInfo->setText(QStringLiteral("Sorgente: %1 giunti, %2 tangenti, %3 spigoli. Apertura campionata %4 mm; scarto G1 %5°. Il controllo è campionato.")
                .arg(report.connected).arg(report.tangent).arg(report.sharp).arg(report.gap,0,'g',6).arg(report.angle*180/3.141592653589793,0,'g',6));
        }catch(const std::exception &error){continuityInfo->setText(QString::fromUtf8(error.what()));}
    });
    if (mode != 2)
        drag.setEnabled(false);
    else viewport->setPreviewCallback([&](const QString &error){
        preview.label->setText(error.isEmpty()?QStringLiteral("pronta (in ambra nella vista)"):QStringLiteral("non riuscita: %1").arg(error));
        if(error.isEmpty()&&!viewport->previewNotice().isEmpty())continuityInfo->setText(poleSession.continuityText()+QStringLiteral("\n")+viewport->previewNotice());
    });
    QObject::connect(poleModel, &QAbstractItemModel::dataChanged, &dialog, [&](const QModelIndex &first,const QModelIndex &last) {
        if(!filling){
            QVector<int> rows;const auto snapshot=poleModel->snapshot();
            for(auto it=snapshot.cbegin();it!=snapshot.cend();++it)if(it.key()>=first.row()&&it.key()<=last.row())rows.append(it.key());
            poleSession.setDrivers(rows);schedule();
        }
    });
    QObject::connect(poles->selectionModel(), &QItemSelectionModel::selectionChanged, &dialog, [&] { if(!filling) updatePoleOverlay(!poleDragging); });
    QObject::connect(poles->selectionModel(), &QItemSelectionModel::currentChanged, &dialog, [&] { if(!filling) updatePoleOverlay(!poleDragging); });
    QObject::connect(poleOrientation, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&] { if(!poleDragging) updatePoleOverlay(); });
    const auto changeFaces=[&] {
        if(filling || mode!=2) return;
        if(poleDragging) {fail(QStringLiteral("Terminare prima il trascinamento dei poli."));return;}
        const auto error=poleSession.store(*poleModel,definition.surfaceTool);
        if(!error.isEmpty()) {
            QSignalBlocker guard(faceList);
            for(int i=0;i<faceList->count();++i) {
                const int id=faceList->item(i)->data(Qt::UserRole).toInt();
                const bool checked=std::any_of(definition.surfaceTool.poleFaces.cbegin(),definition.surfaceTool.poleFaces.cend(),[&](const SurfacePoleFace &f){return f.face.subshape==id;});
                faceList->item(i)->setCheckState(checked?Qt::Checked:Qt::Unchecked);
            }
            fail(error);return;
        }
        for(const auto &face:definition.surfaceTool.poleFaces) faceEdits.insert(face.face.subshape,face);
        QVector<SurfacePoleFace> selected;
        for(int i=0;i<faceList->count();++i) if(faceList->item(i)->checkState()==Qt::Checked) {
            const int id=faceList->item(i)->data(Qt::UserRole).toInt();
            if(faceEdits.contains(id)) selected.append(faceEdits[id]);
        }
        definition.surfaceTool.poleFaces=selected;
        timer.stop();loadSource();refresh();
    };
    QObject::connect(faceList,&QListWidget::itemChanged,&dialog,[&]{changeFaces();});
    const auto checkAll=[&](Qt::CheckState state) {
        {QSignalBlocker guard(faceList);for(int i=0;i<faceList->count();++i)faceList->item(i)->setCheckState(state);}
        changeFaces();
    };
    QObject::connect(allFaces,&QPushButton::clicked,&dialog,[&]{checkAll(Qt::Checked);});
    QObject::connect(noFaces,&QPushButton::clicked,&dialog,[&]{checkAll(Qt::Unchecked);});
    std::function<void()> beginFacePick;
    beginFacePick=[&] {
        choosingFaces=true;viewport->setPickBodies({definition.firstBody});
        const auto error=viewport->beginReferencePick(DatumRoleFace,owner);
        pickFaces->setText(QStringLiteral("Termina selezione facce"));
        if(!error.isEmpty()){choosingFaces=false;fail(error);}
    };
    QObject::connect(pickFaces,&QPushButton::clicked,&dialog,[&] {
        if(choosingFaces) {choosingFaces=false;viewport->cancelReferencePick();viewport->setPickBodies({});pickFaces->setText(QStringLiteral("Scegli facce nella vista"));refresh();}
        else beginFacePick();
    });
    if(mode==2) viewport->setReferencePickCallback([&](bool accepted,GeometryRef ref) {
        viewport->cancelReferencePick();
        if(!accepted){choosingFaces=false;viewport->setPickBodies({});pickFaces->setText(QStringLiteral("Scegli facce nella vista"));refresh();return;}
        const int body=resultBodyForPick(viewport,{definition.firstBody},ref.index);
        const auto shape=viewport->extrusions()[definition.firstBody].forgeBody;
        const auto face=ref.kind==5&&body==definition.firstBody?resolveSurfacePoleFace(*shape,ref.point):FaceId{};
        if(face.valid()) for(int i=0;i<faceList->count();++i) if(faceList->item(i)->data(Qt::UserRole).toInt()==face.index)
            faceList->item(i)->setCheckState(faceList->item(i)->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);
        beginFacePick();
        if(!face.valid() || surfaceBasisHash(shape,face).isEmpty()) status->setText(QStringLiteral("Scegli una faccia del corpo corrente convertibile in NURBS."));
    });
    QObject::connect(base, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&] {
        viewport->cancelReferencePick();viewport->setPickBodies({});choosingFaces=false;pickFaces->setText(QStringLiteral("Scegli facce nella vista"));
        definition.surfaceTool.basis.clear();
        definition.surfaceTool.edits.clear();definition.surfaceTool.poleFaces.clear();faceEdits.clear();
        loadSource();
    });
    QObject::connect(targets, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&] {
        const int row = targets->currentIndex();
        if (mode != 0 || row < 0) return;
        const auto ref = destinations.value(row);
        auto &selected = definition.surfaceTool.targets;
        bool found = false;
        for (const auto &r : selected)
            found = found || (r.kind == ref.kind && r.index == ref.index && (ref.kind != 5 || r.point.subshape == ref.point.subshape));
        if (!found && selected.size() < 256) selected.append(ref);
        definition.extentRef = selected.value(0);
        { QSignalBlocker guard(targets); targets->setCurrentIndex(-1); }
        extensionPick = 0;
        viewport->cancelReferencePick();
        viewport->setPickBodies({});
        timer.stop();
        refresh();
    });
    QObject::connect(maximum, &QDoubleSpinBox::valueChanged, &dialog, schedule);
    QObject::connect(tolerance, &QDoubleSpinBox::valueChanged, &dialog, schedule);
    QObject::connect(linear, &QCheckBox::toggled, &dialog, schedule);
    if (mode != 2)
        base->hide();
    if (mode != 0) {
        edges->hide();
        targets->hide();
        maximum->hide();
        linear->hide();
        pickReference->hide();
        pickEdge->hide();
        targetList->hide();
        removeTarget->hide();
    }
    if (mode != 1) {
        rowList->hide();
        columnList->hide();
    }
    if (mode != 2) {
        faceList->hide();faceControls->hide();
        refitEnabled->hide();refitControls->hide();
        poles->hide();
        poleOrientation->hide();
        poleBox->hide();keepConnected->hide();keepTangent->hide();continuityInfo->hide();checkContinuity->hide();
        poleSelection->hide();
    }
    form->addRow(QStringLiteral("Tolleranza geometrica:"), tolerance);
    dialog.layout()->addWidget(buttons);
    if (mode != 1)
        loadSource();
    refresh();
    if (mode == 0 && definition.blendEdges.isEmpty())
        beginExtensionPick(1);
    const bool accepted = runUntilAppliedModeless(window, viewport, dialog, form, buttons, [&] {
        timer.stop();
        if (mode == 0 || mode==2) {
            extensionPick = 0;choosingFaces=false;
            viewport->cancelReferencePick();
            viewport->setPickBodies({});
        }
        refresh();
        if (!buttons->button(QDialogButtonBox::Ok)->isEnabled())
            return status->text();
        return apply(definition);
    });
    refitSearch.cancel();searchTimer.stop();refitTimer.stop();timer.stop();
    if (mode == 1)
        picker.stop();
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback({});
    if (mode == 0 || mode==2)
        viewport->setPickBodies({});
    viewport->setReferenceMarks({});
    viewport->setSurfacePoleOverlay({});
    viewport->setPreviewCallback({});
    return accepted;
}
