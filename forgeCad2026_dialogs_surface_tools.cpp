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
    std::function<void()> changed;
    int active = -1;
    QPoint origin;
    std::array<double, 3> initial{};
    PoleDrag(CadViewport *v, QTableWidget *t, QObject *parent) : QObject(parent), view(v), table(t) { view->installEventFilter(this); }
    ~PoleDrag() override { view->removeEventFilter(this); }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object != view)
            return false;
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton)
                return false;
            double best = 12 * 12;
            int row = -1;
            for (int i = 0; i < table->rowCount(); ++i) {
                const auto d = view->surfacePoleScreen(i) - mouse->position();
                const double distance = d.x() * d.x() + d.y() * d.y();
                if (distance < best) {
                    best = distance;
                    row = i;
                }
            }
            if (row < 0)
                return false;
            active = row;
            origin = mouse->position().toPoint();
            table->selectRow(row);
            initial = {QLocale().toDouble(table->item(row, 2)->text()), QLocale().toDouble(table->item(row, 3)->text()),
                       QLocale().toDouble(table->item(row, 4)->text())};
            return true;
        }
        if (active >= 0 && event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const auto delta = view->surfacePoleDragVector(origin, mouse->position().toPoint());
            {
                QSignalBlocker guard(table);
                for (int k = 0; k < 3; ++k)
                    table->item(active, k + 2)->setText(QLocale().toString(initial[k] + double(delta[k]), 'g', 17));
            }
            if (changed)
                changed();
            return true;
        }
        if (active >= 0 && event->type() == QEvent::MouseButtonRelease) {
            active = -1;
            if (changed)
                changed();
            return true;
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
    FunctionDialogPanel dialog(window);
    dialog.setObjectName("surfaceToolDialog");
    dialog.setWindowTitle(title);
    dialog.setModal(false);
    dialog.setMinimumWidth(mode == 2 ? 580 : 440);
    auto *form = dialog.createScrollableForm();
    PreviewScope preview(viewport, dialog, form, replaced);
    auto *status = new QLabel(&dialog);
    status->setObjectName("surfaceToolStatus");
    status->setWordWrap(true);
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
    auto *base = new QComboBox(&dialog), *edges = new QComboBox(&dialog), *targets = new QComboBox(&dialog);
    base->setObjectName("surfaceToolBase");
    edges->setObjectName("surfaceToolEdge");
    targets->setObjectName("surfaceToolTarget");
    QVector<int> sources;
    QVector<GeometryRef> destinations;
    QVector<EdgePoint> edgeRefs;
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
    poles->setSelectionMode(QAbstractItemView::SingleSelection);
    poles->setMinimumHeight(210);
    poles->verticalHeader()->hide();
    poles->horizontalHeader()->setStyleSheet(QStringLiteral("QHeaderView::section { background: #324251; color: #f2f6fa; padding: 3px; }"));
    for (int c = 0; c < 6; ++c) {
        poles->horizontalHeader()->setSectionResizeMode(c, c >= 2 && c <= 4 ? QHeaderView::Stretch : QHeaderView::Fixed);
        poles->setColumnWidth(c, c < 2 ? 32 : 72);
    }

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
    auto *pickReference = new QPushButton(QStringLiteral("Scegli il riferimento nella vista"), &dialog);
    pickReference->setObjectName("surfaceToolPickTarget");
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
        form->addRow(QStringLiteral("Bordo libero:"), edges);
        form->addRow(QStringLiteral("Fino al riferimento:"), targets);
        form->addRow(pickReference);
        form->addRow(QStringLiteral("Distanza massima di ricerca:"), maximum);
        form->addRow(linear);
        form->addRow(ForgeCad::Gui::wrappedNote(
            QStringLiteral("Il riferimento deve attraversare tutto il prolungamento, senza tagliare la superficie iniziale. Sono "
                           "disponibili piani e facce, anche curve; la distanza massima limita la ricerca."),
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
        int wanted = -1;
        for (int i = 0; i < destinations.size(); ++i)
            if (destinations[i].kind == definition.extentRef.kind && destinations[i].index == definition.extentRef.index &&
                destinations[i].point.name == definition.extentRef.point.name &&
                destinations[i].point.subshape == definition.extentRef.point.subshape)
                wanted = i;
        if (wanted >= 0)
            targets->setCurrentIndex(wanted);
        QObject::connect(pickReference, &QPushButton::clicked, &dialog, [&] {
            const auto error = viewport->beginReferencePick(DatumRolePlane | DatumRoleFace, owner);
            if (!error.isEmpty())
                status->setText(error);
        });
        viewport->setReferencePickCallback([&](bool accepted, GeometryRef ref) {
            if (!accepted)
                return;
            int row = -1;
            for (int i = 0; i < destinations.size(); ++i)
                if (destinations[i].kind == ref.kind && destinations[i].index == ref.index &&
                    (ref.kind != 5 || destinations[i].point.subshape == ref.point.subshape))
                    row = i;
            if (row < 0) {
                destinations.append(ref);
                targets->addItem(geometryRefText(ref, viewport->sketches(), viewport->extrusions()));
                row = targets->count() - 1;
            }
            targets->setCurrentIndex(row);
            viewport->cancelReferencePick();
            schedule();
        });
    } else {
        form->addRow(ForgeCad::Gui::wrappedNote(
            QStringLiteral("Seleziona un polo nella tabella o trascinalo nella vista sul piano dello schermo. XYZ e peso sono "
                           "modificabili; gradi, nodi e rifilature UV restano quelli della sorgente. Le modifiche si applicano come "
                           "spostamenti e fattori relativi, rigenerabili."),
            &dialog));
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
            QSignalBlocker guard(edges);
            edges->clear();
            edgeRefs.clear();
            const auto selected = prior.isEmpty() ? ForgeCad::Kernel::EdgeId{} : resolveEdgeReference(*shape, prior.front(), 1e-5);
            int wanted = prior.isEmpty() ? 0 : -1;
            for (const auto edge : shape->edges())
                if (shape->isLaminar(edge)) {
                    const auto &e = shape->edge(edge);
                    const auto point = e.curve->point((e.range.lo + e.range.hi) / 2);
                    const auto ref = edgeReference(*shape, edge, point);
                    if (edge == selected)
                        wanted = edgeRefs.size();
                    edgeRefs.append(ref);
                    edges->addItem(QStringLiteral("E%1").arg(edge.index));
                }
            if (!edgeRefs.isEmpty())
                edges->setCurrentIndex(wanted);
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
        if (filling)
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
            if (edges->currentIndex() < 0) {
                fail(QStringLiteral("La superficie non ha bordi liberi."));
                return;
            }
            definition.blendEdges = {edgeRefs[edges->currentIndex()]};
            definition.extentRef = destinations.value(targets->currentIndex());
            definition.surfaceTool.maximum = maximum->value();
            definition.extendLinear = linear->isChecked();
            GeometryRef boundary;
            boundary.kind = 4;
            boundary.index = definition.firstBody;
            boundary.featureId = viewport->extrusions()[definition.firstBody].featureId;
            boundary.point = definition.blendEdges.front();
            viewport->setReferenceMarks({boundary, definition.extentRef});
        } else {
            if (!spline || poles->rowCount() != nu * nv || qint64(nu) * nv > 4096) {
                fail(QStringLiteral("Griglia NURBS non disponibile o oltre 4096 poli."));
                return;
            }
            definition.surfaceTool.edits.clear();
            QVector<QVector3D> cage;
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
                    cage.append(QVector3D(values[0], values[1], values[2]));
                }
            viewport->setSurfacePoleOverlay(cage, nu, nv, poles->currentRow());
            if (definition.surfaceTool.edits.isEmpty()) {
                fail(QStringLiteral("Sposta un polo o modifica un peso."));
                return;
            }
        }
        preview.request(definition);
    };
    PoleDrag drag(viewport, poles, &dialog);
    drag.changed = schedule;
    if (mode != 2)
        viewport->removeEventFilter(&drag);
    QObject::connect(poles, &QTableWidget::cellChanged, &dialog, [&] {
        if (!filling)
            schedule();
    });
    QObject::connect(poles, &QTableWidget::itemSelectionChanged, &dialog, schedule);
    QObject::connect(base, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&] {
        definition.surfaceTool.basis.clear();
        definition.surfaceTool.edits.clear();
        loadSource();
    });
    for (auto *combo : {edges, targets})
        QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, schedule);
    QObject::connect(maximum, &QDoubleSpinBox::valueChanged, &dialog, schedule);
    QObject::connect(tolerance, &QDoubleSpinBox::valueChanged, &dialog, schedule);
    QObject::connect(linear, &QCheckBox::toggled, &dialog, schedule);
    if (mode == 1)
        base->hide();
    if (mode != 0) {
        edges->hide();
        targets->hide();
        maximum->hide();
        linear->hide();
        pickReference->hide();
    }
    if (mode != 1) {
        rowList->hide();
        columnList->hide();
    }
    if (mode != 2)
        poles->hide();
    form->addRow(QStringLiteral("Tolleranza geometrica:"), tolerance);
    dialog.layout()->addWidget(buttons);
    if (mode != 1)
        loadSource();
    refresh();
    const bool accepted = runUntilAppliedModeless(window, viewport, dialog, form, buttons, [&] {
        timer.stop();
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
    viewport->setReferenceMarks({});
    viewport->setSurfacePoleOverlay({});
    return accepted;
}
