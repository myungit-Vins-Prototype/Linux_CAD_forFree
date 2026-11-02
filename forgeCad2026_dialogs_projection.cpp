#include "forgeCad2026_dialogs.h"
#include "forgeCad2026_gui_common.h"
#include "cad_datum.h"
#include "cad_expression.h"
#include "cad_feature_dependencies.h"

#include <algorithm>
#include <array>

bool projectionDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
    const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply) {
    const auto &sketches = viewport->sketches();
    if (sketches.isEmpty()) {
        QMessageBox::information(window, title, QStringLiteral("Crea prima gli schizzi da proiettare."));
        return false;
    }
    QVector<int> candidates = viewport->resultBodiesBefore(replaced);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int i) { return !viewport->extrusions().at(i).forgeBody; }), candidates.end());
    if (initial.firstBody >= 0 && initial.firstBody < viewport->extrusions().size() && viewport->extrusions().at(initial.firstBody).forgeBody
        && (replaced < 0 || initial.firstBody < replaced) && !candidates.contains(initial.firstBody)) candidates.append(initial.firstBody);
    ExtrusionObject definition = initial;
    definition.operation = -1;
    definition.feature = BodyFeature::ProjectedCurve;
    if (!candidates.contains(definition.firstBody)) definition.firstBody = candidates.isEmpty() ? -1 : candidates.last();
    if (definition.sketchIndex < 0 || definition.sketchIndex >= sketches.size()) definition.sketchIndex = sketches.size() - 1;
    if (candidates.isEmpty() && replaced < 0) definition.projectionMode = 1;

    FunctionDialogPanel dialog(window);
    dialog.setSizeIdentity(QStringLiteral("projection"));
    dialog.setObjectName(QStringLiteral("projectionDialog"));
    ForgeCad::setDisplayLengthUnit(viewport->lengthUnit(), &dialog);
    dialog.setWindowTitle(title);
    auto *form = dialog.createScrollableForm();
    auto *mode = new QComboBox(&dialog);
    mode->setObjectName(QStringLiteral("projectionMode"));
    mode->addItems({QStringLiteral("Schizzo su corpo"), QStringLiteral("Schizzo su schizzo (curva 3D)")});
    mode->setCurrentIndex(definition.projectionMode == 1 ? 1 : 0);
    form->addRow(QStringLiteral("Proiezione:"), mode);

    auto *onBody = new QWidget(&dialog);
    auto *onBodyForm = new QFormLayout(onBody);
    onBodyForm->setContentsMargins(0, 0, 0, 0);
    auto *bodyBox = new QComboBox(onBody);
    for (int i : candidates) bodyBox->addItem(logicalBodyLabel(viewport, i));
    bodyBox->setCurrentIndex(candidates.indexOf(definition.firstBody));
    auto *sketchBox = new QComboBox(onBody);
    for (const auto &sketch : sketches) sketchBox->addItem(sketch.name);
    sketchBox->setCurrentIndex(definition.sketchIndex);
    auto *reverse = new QCheckBox(QStringLiteral("Inverti il verso della proiezione"), onBody);
    reverse->setChecked(definition.projectionReverse);
    onBodyForm->addRow(QStringLiteral("Corpo:"), bodyBox);
    onBodyForm->addRow(QStringLiteral("Schizzo:"), sketchBox);
    onBodyForm->addRow(reverse);
    onBodyForm->addRow(ForgeCad::Gui::wrappedNote(QStringLiteral("Le curve dello schizzo sono proiettate lungo la normale sulla prima faccia incontrata. Il verso iniziale e' rivolto al corpo."), onBody));
    form->addRow(onBody);

    auto *onSketches = new QWidget(&dialog);
    auto *inputs = new QVBoxLayout(onSketches);
    inputs->setContentsMargins(0, 0, 0, 0);
    std::array<QComboBox *, 2> sketchChoices{};
    std::array<QListWidget *, 2> entityLists{};
    std::array<QPushButton *, 2> pickButtons{};
    std::array<QVector<GeometryRef>, 2> entities;
    const QVector<GeometryRef> initialRefs[2] = {definition.ruledFirst, definition.ruledSecond};
    bool filling = false;
    int picking = -1;
    for (int group = 0; group < 2; ++group) {
        auto *box = new QGroupBox(group == 0 ? QStringLiteral("Prima curva / primo schizzo") : QStringLiteral("Seconda curva / secondo schizzo"), onSketches);
        auto *layout = new QVBoxLayout(box);
        sketchChoices[group] = new QComboBox(box);
        sketchChoices[group]->setObjectName(QStringLiteral("projectionSketch%1").arg(group));
        for (const auto &sketch : sketches) sketchChoices[group]->addItem(sketch.name);
        int selected = group == 0 ? definition.sketchIndex : (definition.sketchIndex + 1) % sketches.size();
        if (!initialRefs[group].isEmpty() && initialRefs[group].first().index >= 0 && initialRefs[group].first().index < sketches.size()) selected = initialRefs[group].first().index;
        sketchChoices[group]->setCurrentIndex(selected);
        entityLists[group] = new QListWidget(box);
        entityLists[group]->setObjectName(QStringLiteral("projectionEntities%1").arg(group));
        entityLists[group]->setMaximumHeight(125);
        pickButtons[group] = new QPushButton(QStringLiteral("Scegli curva nella vista"), box);
        pickButtons[group]->setObjectName(QStringLiteral("projectionPick%1").arg(group));
        pickButtons[group]->setCheckable(true);
        auto *all = new QPushButton(QStringLiteral("Tutte"), box);
        auto *none = new QPushButton(QStringLiteral("Nessuna"), box);
        auto *actions = new QHBoxLayout;
        actions->addWidget(pickButtons[group]); actions->addWidget(all); actions->addWidget(none);
        layout->addWidget(sketchChoices[group]); layout->addWidget(entityLists[group]); layout->addLayout(actions);
        inputs->addWidget(box);
        QObject::connect(all, &QPushButton::clicked, &dialog, [&, group] {for (int i = 0; i < entityLists[group]->count(); ++i) entityLists[group]->item(i)->setCheckState(Qt::Checked);});
        QObject::connect(none, &QPushButton::clicked, &dialog, [&, group] {for (int i = 0; i < entityLists[group]->count(); ++i) entityLists[group]->item(i)->setCheckState(Qt::Unchecked);});
    }
    inputs->addWidget(ForgeCad::Gui::wrappedNote(QStringLiteral("Spunta solo le curve che definiscono il risultato. La proiezione usa entrambe le direzioni normali ai piani e conserva tutti i rami nell'intervallo comune. Piani paralleli e coincidenze non univoche producono un errore."), onSketches));
    form->addRow(onSketches);
    auto *result = new FunctionMessage(&dialog);
    form->addRow(QStringLiteral("Esito:"), result);
    auto *status = new FunctionMessage(&dialog);
    form->addRow(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    PreviewScope scope(viewport, dialog, form, replaced);
    viewport->setPreviewCallback([preview = scope.label, result, viewport](const QString &error) {
        preview->setText(error.isEmpty() ? QStringLiteral("pronta (in ambra nella vista)") : QStringLiteral("non riuscita: ") + error);
        result->setText(error.isEmpty() ? viewport->previewNotice() : QString());
    });
    const auto refill = [&](int group, const QVector<GeometryRef> &selected) {
        filling = true;
        auto *list = entityLists[group]; list->clear(); entities[group].clear();
        const int index = sketchChoices[group]->currentIndex();
        const auto &sketch = sketches.at(index);
        const auto add = [&](int kind, int element) {
            GeometryRef ref; ref.kind = 7; ref.index = index; ref.element = {kind, element, -1};
            entities[group].append(ref);
            auto *item = new QListWidgetItem(ForgeCad::geometryRefText(ref, sketches, viewport->extrusions()), list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            bool checked = false;
            for (const auto &old : selected) if (old.kind == 7 && old.index == index && (old.element.kind < 0 || old.element == ref.element)) checked = true;
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        };
        for (int i = 0; i < sketch.segments.size(); ++i) if (!sketch.constructionSegments.contains(i)) add(0, i);
        for (int i = 0; i < sketch.curves.size(); ++i)
            if (!sketch.curves[i].construction && sketch.curves[i].tool != DrawingTool::Point) add(1, i);
        if (selected.isEmpty() && list->count() == 1) list->item(0)->setCheckState(Qt::Checked);
        filling = false;
    };
    for (int group = 0; group < 2; ++group) refill(group, initialRefs[group]);
    const auto current = [&] {
        ExtrusionObject d = definition;
        d.projectionMode = mode->currentIndex();
        d.projectionReverse = reverse->isChecked();
        d.sketchIndex = d.projectionMode == 1 ? -1 : sketchBox->currentIndex();
        d.firstBody = d.projectionMode == 1 ? -1 : candidates.value(bodyBox->currentIndex(), -1);
        if (d.projectionMode == 1) {
            QVector<GeometryRef> *groups[] = {&d.ruledFirst, &d.ruledSecond};
            for (int group = 0; group < 2; ++group) {
                groups[group]->clear();
                for (int i = 0; i < entityLists[group]->count(); ++i)
                    if (entityLists[group]->item(i)->checkState() == Qt::Checked) groups[group]->append(entities[group].at(i));
            }
        }
        return d;
    };
    const auto refresh = [&] {
        if (filling) return;
        const bool twoSketches = mode->currentIndex() == 1;
        onBody->setVisible(!twoSketches); onSketches->setVisible(twoSketches);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(twoSketches ? sketches.size() >= 2 : !candidates.isEmpty());
        const auto d = current();
        QVector<GeometryRef> marks = twoSketches ? d.ruledFirst + d.ruledSecond : QVector<GeometryRef>{};
        viewport->setReferenceMarks(marks);
        scope.request(d);
    };
    const auto arm = [&](int group) {
        viewport->cancelReferencePick(); picking = group;
        for (int k = 0; k < 2; ++k) pickButtons[k]->setChecked(k == group);
        const QString reason = viewport->beginReferencePick(ForgeCad::DatumRoleCurve | ForgeCad::DatumRoleLine, replaced);
        if (!reason.isEmpty()) {picking = -1; pickButtons[group]->setChecked(false);}
        status->setText(reason.isEmpty() ? QStringLiteral("Clicca una curva dello schizzo. Le altre curve dello schizzo restano escluse.") : reason);
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        const int group = picking; picking = -1;
        for (auto *button : pickButtons) button->setChecked(false);
        if (!picked || group < 0) return;
        if (ref.kind != 7 || ref.element.kind < 0 || ref.element.point >= 0) {
            status->setText(QStringLiteral("Scegli una curva o un segmento di uno schizzo piano.")); arm(group); return;
        }
        filling = true; sketchChoices[group]->setCurrentIndex(ref.index); filling = false;
        refill(group, {ref});
        status->setText(QStringLiteral("Curva selezionata. Puoi aggiungere altre entita' spuntandole nella lista."));
        refresh();
    });
    for (int group = 0; group < 2; ++group) {
        QObject::connect(sketchChoices[group], &QComboBox::currentIndexChanged, &dialog, [&, group] {if (!filling) {refill(group, {}); refresh();}});
        QObject::connect(entityLists[group], &QListWidget::itemChanged, &dialog, refresh);
        QObject::connect(pickButtons[group], &QPushButton::clicked, &dialog, [&, group] {arm(group);});
        connectReferenceList(entityLists[group], dialog, viewport, [&, group](QListWidgetItem *item) {
            const int row = entityLists[group]->row(item); return row >= 0 ? QVector<GeometryRef>{entities[group].at(row)} : QVector<GeometryRef>{};
        });
    }
    QObject::connect(mode, &QComboBox::currentIndexChanged, &dialog, [&] {viewport->cancelReferencePick(); picking = -1; refresh();});
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(sketchBox, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(reverse, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString error = apply(current());
        if (error.isEmpty()) dialog.accept(); else status->setText(error);
    });
    QObject::connect(&dialog, &QDialog::finished, viewport, [viewport] {
        viewport->cancelReferencePick(); viewport->setReferencePickCallback({}); viewport->setReferenceMarks({});
    });
    QTimer::singleShot(0, &dialog, refresh);
    return runModeless(window, viewport, dialog);
}
