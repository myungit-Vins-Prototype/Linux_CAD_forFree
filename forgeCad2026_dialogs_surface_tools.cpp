#include "forgeCad2026_dialogs.h"
#include "forgeCad2026_dialogs_common.h"
#include "cad_surface_tools.h"
#include "cad_datum.h"
#include "cad_feature_dependencies.h"
#include "cad_topology_ref.h"
#include "cad_expression.h"
#include "fk_bspline_surface.h"
#include "fk_history.h"
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
class PoleDrag final : public QObject {
  public:
    CadViewport *view;
    QTableWidget *table;
    std::function<void()> started, moved, finished;
    bool active = false;
    int axis = -1;
    QPoint origin;
    QVector<int> rows;
    QVector<std::array<double,3>> initial;
    PoleDrag(CadViewport *v, QTableWidget *t, QObject *parent) : QObject(parent), view(v), table(t) { view->installEventFilter(this); }
    ~PoleDrag() override { view->removeEventFilter(this); if(active) view->releaseMouse(); }
    void finish(bool cancel) {
        if (cancel) {
            QSignalBlocker guard(table);
            for (int i=0; i<rows.size(); ++i)
                for (int k=0; k<3; ++k) table->item(rows[i],k+2)->setText(QLocale().toString(initial[i][k],'g',17));
        }
        active=false;
        view->releaseMouse();
        if (finished) finished();
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object != view) return false;
        if (active && event->type()==QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key()==Qt::Key_Escape) {
            finish(true); return true;
        }
        if (event->type()==QEvent::MouseButtonPress) {
            auto *mouse=static_cast<QMouseEvent *>(event);
            if(mouse->button()!=Qt::LeftButton) return false;
            int handle=view->surfacePoleHandleAt(mouse->position());
            int row=-1; double nearest=10*10;
            for(int i=0;i<table->rowCount();++i) {
                const auto d=view->surfacePoleScreen(i)-mouse->position();
                const double distance=QPointF::dotProduct(d,d);
                if(distance<nearest) { nearest=distance;row=i; }
            }
            const bool toggle=mouse->modifiers() & (Qt::ControlModifier|Qt::MetaModifier);
            const bool add=mouse->modifiers() & Qt::ShiftModifier;
            if(row>=0 && (toggle||add||handle<0||handle==3 ||
                QLineF(mouse->position(),view->surfacePoleAxisScreen(handle)).length()>12)) handle=-1;
            if(handle<0 || handle==3) {
                if(row>=0) {
                    const auto index=table->model()->index(row,0);
                    const bool selected=table->selectionModel()->isRowSelected(row,{});
                    if(toggle && selected) {
                        table->selectionModel()->select(index,QItemSelectionModel::Deselect|QItemSelectionModel::Rows);
                        return true;
                    }
                    if(!selected) table->selectionModel()->select(index,
                        (toggle||add ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect)|QItemSelectionModel::Rows);
                    table->setCurrentCell(row,2,QItemSelectionModel::NoUpdate);
                } else if(handle!=3) {
                    if(!toggle&&!add) table->clearSelection();
                    return false;
                }
                // I modificatori servono alla selezione; trascinare il gruppo
                // richiede un normale clic su uno dei poli gia' selezionati.
                if(toggle||add) return true;
            }
            rows.clear(); initial.clear();
            for(const auto &index:table->selectionModel()->selectedRows()) {
                const int r=index.row(); std::array<double,3> values;
                for(int k=0;k<3;++k) {
                    bool valid=false; values[k]=QLocale().toDouble(table->item(r,k+2)->text(),&valid);
                    if(!valid||!std::isfinite(values[k])) return true;
                }
                rows.append(r); initial.append(values);
            }
            if(rows.isEmpty()) return true;
            axis=handle>=0&&handle<3?handle:-1;
            origin=mouse->position().toPoint(); active=true;
            if(started) started();
            view->setFocus(); view->grabMouse();
            return true;
        }
        if(active && event->type()==QEvent::MouseMove) {
            auto *mouse=static_cast<QMouseEvent *>(event);
            const auto delta=view->surfacePoleAxisDragVector(axis,origin,mouse->position().toPoint());
            {
                QSignalBlocker guard(table);
                for(int i=0;i<rows.size();++i)
                    for(int k=0;k<3;++k) table->item(rows[i],k+2)->setText(QLocale().toString(initial[i][k]+double(delta[k]),'g',17));
            }
            if(moved) moved();
            return true;
        }
        if(active && event->type()==QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(event)->button()==Qt::LeftButton) {
            finish(false);return true;
        }
        return false;
    }
};
} // namespace
bool surfaceToolDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject &)> &apply) {
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
    auto *poles = new QTableWidget(0, 6, &dialog);
    poles->setObjectName("surfacePolesTable");
    poles->setItemDelegate(new PoleNumberDelegate(poles));
    poles->setHorizontalHeaderLabels({"U", "V", "X", "Y", "Z", QStringLiteral("Peso")});
    poles->setSelectionBehavior(QAbstractItemView::SelectRows);
    poles->setSelectionMode(QAbstractItemView::ExtendedSelection);
    poles->setMinimumHeight(210);
    poles->verticalHeader()->hide();
    poles->horizontalHeader()->setStyleSheet(QStringLiteral("QHeaderView::section { background: #324251; color: #f2f6fa; padding: 3px; }"));
    for (int c = 0; c < 6; ++c) {
        poles->horizontalHeader()->setSectionResizeMode(c, c >= 2 && c <= 4 ? QHeaderView::Stretch : QHeaderView::Fixed);
        poles->setColumnWidth(c, c < 2 ? 32 : 72);
    }

    auto *poleOrientation = new QComboBox(&dialog);
    poleOrientation->setObjectName("surfacePolesOrientation");
    poleOrientation->addItems({QStringLiteral("Globale (X, Y, Z)"),QStringLiteral("Locale (U, V, normale)")});
    auto *poleSelection = new FunctionMessage(&dialog);
    poleSelection->setObjectName("surfacePolesSelection");
    bool poleDragging = false;
    const BSplineSurface *spline = nullptr;
    int nu = 0, nv = 0;
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
        if (mode == 0)
            preview.label->setText(QStringLiteral("in attesa delle selezioni"));
    };
    const auto candidates = operandBodies(viewport, replaced, {definition.firstBody}, [&](const ExtrusionObject &b) {
        if (!b.forgeBody || !b.forgeBody->isSheet())
            return false;
        return mode != 2 || !surfaceBasisHash(b.forgeBody).isEmpty();
    });
    if (mode != 1) {
        sources = candidates;
        for (int index : sources)
            base->addItem(resultBodyLabel(viewport, index));
        if (sources.isEmpty()) {
            QMessageBox::information(
                window, title,
                mode == 2
                    ? QStringLiteral("Serve una superficie a una faccia NURBS. Per una faccia di un corpo cucito usare prima Copia facce.")
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
                           "Ctrl/Cmd aggiunge o toglie poli, Maiusc aggiunge; la tabella permette selezioni multiple. "
                           "L'anteprima si calcola al rilascio. Esc durante il trascinamento annulla lo spostamento."),
            &dialog));
        form->addRow(QStringLiteral("Orientamento assi:"), poleOrientation);
        form->addRow(poleSelection);
        form->addRow(poles);
        auto *reload = new QPushButton(QStringLiteral("Ricarica griglia (azzera le modifiche)"), &dialog);
        reload->setObjectName("surfacePolesReload");
        form->addRow(reload);
        QObject::connect(reload, &QPushButton::clicked, &dialog, [&] {
            definition.surfaceTool.basis.clear();
            definition.surfaceTool.edits.clear();
            base->currentIndexChanged(base->currentIndex());
        });
    }
    const auto updatePoleOverlay = [&](bool recomputeAxes = true) {
        if(mode!=2 || !spline || poles->rowCount()!=nu*nv) return;
        QVector<QVector3D> cage; std::vector<Vec3> points; std::vector<double> weights;
        for(int i=0;i<poles->rowCount();++i) {
            double values[4];
            for(int c=0;c<4;++c) {
                bool valid=false;values[c]=QLocale().toDouble(poles->item(i,c+2)->text(),&valid);
                if(!valid||!std::isfinite(values[c])||(c==3&&values[c]<=0)) return;
            }
            cage.append(QVector3D(values[0],values[1],values[2]));
            points.emplace_back(values[0],values[1],values[2]);weights.push_back(values[3]);
        }
        QVector<int> selected;
        for(const auto &index:poles->selectionModel()->selectedRows()) selected.append(index.row());
        int active=poles->currentRow();
        if(!selected.contains(active)) active=selected.value(0,-1);
        viewport->setSurfacePoleOverlay(cage,nu,nv,active);
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
                const BSplineSurface current(spline->uDegree(),spline->vDegree(),spline->uKnots(),spline->vKnots(),nu,nv,points,weights);
                const double u=greville(spline->uKnots(),spline->uDegree(),active/nv),v=greville(spline->vKnots(),spline->vDegree(),active%nv);
                Vec3 d[4];current.evaluate(u,v,1,d);
                const auto x=normalized(d[Surface::derivativeIndex(1,0,1)]);
                const auto shape=viewport->extrusions()[definition.firstBody].forgeBody;
                const double sense=shape->face(shape->faces().front()).sense?1.:-1.;
                const auto z=sense*normalized(cross(d[Surface::derivativeIndex(1,0,1)],d[Surface::derivativeIndex(0,1,1)]));
                const auto y=normalized(cross(z,x));
                for(int k=0;k<3;++k) axes[k]=QVector3D((k==0?x:k==1?y:z).x(),(k==0?x:k==1?y:z).y(),(k==0?x:k==1?y:z).z());
            } catch(const std::exception &) {
                local=false;
                singular=true;
            }
        }
        viewport->setSurfacePoleSelection(selected,axes,local);
        poleSelection->setText(QStringLiteral("%1 poli selezionati%2").arg(selected.size()).arg(singular?QStringLiteral(" — normale singolare: assi globali"):local?QStringLiteral(" — assi del polo attivo"):QString()));
    };
    const auto loadSource = [&] {
        if (mode == 1)
            return;
        filling = true;
        if (mode == 2) {
            spline = nullptr;
            poles->setRowCount(0);
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
            spline = dynamic_cast<const BSplineSurface *>(shape->face(shape->faces().front()).surface.get());
            nu = spline->uPoleCount();
            nv = spline->vPoleCount();
            if (qint64(nu) * nv > 4096) {
                filling = false;
                fail(QStringLiteral("La griglia supera 4096 poli: scegliere una patch piu' piccola."));
                return;
            }
            const bool same = definition.surfaceTool.basis == surfaceBasisHash(shape);
            if (!same && !definition.surfaceTool.basis.isEmpty()) {
                spline = nullptr;
                filling = false;
                fail(QStringLiteral("La griglia a monte e cambiata: ricaricare la griglia e riselezionare i poli."));
                return;
            }
            if (!same)
                definition.surfaceTool.edits.clear();
            definition.surfaceTool.basis = surfaceBasisHash(shape);
            poles->setRowCount(nu * nv);
            QSignalBlocker guard(poles);
            for (int i = 0; i < nu; ++i)
                for (int j = 0; j < nv; ++j) {
                    Vec3 point = spline->pole(i, j);
                    double weight = spline->weight(i, j);
                    for (const auto &edit : definition.surfaceTool.edits)
                        if (edit.u == i && edit.v == j) {
                            point += Vec3(edit.dx, edit.dy, edit.dz);
                            weight *= edit.weightFactor;
                        }
                    const QStringList values{QString::number(i + 1),
                                             QString::number(j + 1),
                                             QLocale().toString(point.x(), 'g', 17),
                                             QLocale().toString(point.y(), 'g', 17),
                                             QLocale().toString(point.z(), 'g', 17),
                                             QLocale().toString(weight, 'g', 17)};
                    for (int c = 0; c < 6; ++c) {
                        auto *item = new QTableWidgetItem(values[c]);
                        item->setToolTip(values[c]);
                        if (c < 2)
                            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                        poles->setItem(i * nv + j, c, item);
                    }
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
            if (!spline || poles->rowCount() != nu * nv || qint64(nu) * nv > 4096) {
                fail(QStringLiteral("Griglia NURBS non disponibile o oltre 4096 poli."));
                return;
            }
            definition.surfaceTool.edits.clear();
            for (int i = 0; i < nu; ++i)
                for (int j = 0; j < nv; ++j) {
                    double values[4];
                    for (int c = 0; c < 4; ++c) {
                        bool valid = false;
                        values[c] = QLocale().toDouble(poles->item(i * nv + j, c + 2)->text(), &valid);
                        if (!valid || !std::isfinite(values[c]) || (c == 3 && values[c] <= 0)) {
                            fail(QStringLiteral("Coordinate finite e pesi positivi richiesti alla riga %1.").arg(i * nv + j + 1));
                            return;
                        }
                    }
                    const auto original = spline->pole(i, j);
                    const Vec3 point(values[0], values[1], values[2]);
                    const auto delta = point - original;
                    const double factor = values[3] / spline->weight(i, j);
                    if (norm(delta) > 1e-11 || std::fabs(factor - 1) > 1e-12)
                        definition.surfaceTool.edits.append({i, j, delta.x(), delta.y(), delta.z(), factor});
                }
            updatePoleOverlay();
            if (definition.surfaceTool.edits.isEmpty()) {
                fail(QStringLiteral("Sposta un polo o modifica un peso."));
                return;
            }
        }
        preview.request(definition);
    };
    PoleDrag drag(viewport, poles, &dialog);
    drag.started = [&] {
        poleDragging=true;timer.stop();viewport->clearPreview();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        preview.label->setText(QStringLiteral("rilascia il mouse per calcolare l'anteprima"));
    };
    drag.moved = [&] { updatePoleOverlay(false); };
    drag.finished = [&] { poleDragging=false;updatePoleOverlay();refresh(); };
    if (mode != 2)
        viewport->removeEventFilter(&drag);
    QObject::connect(poles, &QTableWidget::cellChanged, &dialog, [&] {
        if (!filling)
            schedule();
    });
    QObject::connect(poles, &QTableWidget::itemSelectionChanged, &dialog, [&] { if(!filling) updatePoleOverlay(!poleDragging); });
    QObject::connect(poles, &QTableWidget::currentCellChanged, &dialog, [&](int,int,int,int) { if(!filling) updatePoleOverlay(!poleDragging); });
    QObject::connect(poleOrientation, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&] { if(!poleDragging) updatePoleOverlay(); });
    QObject::connect(base, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&] {
        definition.surfaceTool.basis.clear();
        definition.surfaceTool.edits.clear();
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
        poles->hide();
        poleOrientation->hide();
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
        if (mode == 0) {
            extensionPick = 0;
            viewport->cancelReferencePick();
            viewport->setPickBodies({});
        }
        refresh();
        if (!buttons->button(QDialogButtonBox::Ok)->isEnabled())
            return status->text();
        return apply(definition);
    });
    timer.stop();
    if (mode == 1)
        picker.stop();
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback({});
    if (mode == 0)
        viewport->setPickBodies({});
    viewport->setReferenceMarks({});
    viewport->setSurfacePoleOverlay({});
    return accepted;
}
