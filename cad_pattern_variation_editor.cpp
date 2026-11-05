#include "cad_pattern_variation_editor.h"
#include "cad_expression.h"
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <cmath>
namespace ForgeCad {
PatternVariationEditor::PatternVariationEditor(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("patternVariations"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *hint =
        new QLabel(QStringLiteral("Clicca una quota dello schizzo nella vista per aggiungerla, poi imposta l’incremento. "
                                  "Le quote selezionate sono verdi. Le esclusioni "
                                  "mantengono la numerazione; nelle griglie avanza prima la direzione 1."),
                   this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *row = new QHBoxLayout;
    available_ = new QComboBox(this);
    available_->setObjectName("patternVariationParameter");
    available_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    available_->setMinimumContentsLength(20);
    add_ = new QPushButton(QStringLiteral("Aggiungi"), this);
    add_->setObjectName("patternVariationAdd");
    row->addWidget(available_, 1);
    row->addWidget(add_);
    layout->addLayout(row);
    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName("patternVariationTable");
    table_->setHorizontalHeaderLabels({QStringLiteral("Parametro"), QStringLiteral("Iniziale"),
                                       QStringLiteral("Incremento"), QStringLiteral("Ultima")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setMinimumHeight(130);
    layout->addWidget(table_);
    auto *remove = new QPushButton(QStringLiteral("Rimuovi variazione"), this);
    remove->setObjectName("patternVariationRemove");
    layout->addWidget(remove);
    connect(add_, &QPushButton::clicked, this, [this] {
        int i = available_->currentIndex();
        if (i < 0 || i >= parameters_.size() || values_.size() >= 256)
            return;
        selectParameter(parameters_[available_->currentData().toInt()].reference);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        int row = table_->currentRow();
        if (row < 0 || row >= values_.size())
            return;
        values_.removeAt(row);
        rebuild();
        if (changed)
            changed();
    });
}
void PatternVariationEditor::setVariations(const QVector<PatternVariation> &values) {
    values_ = values;
    rebuild();
}
void PatternVariationEditor::setContext(const ExtrusionObject &pattern, const QVector<SketchObject> &s,
                                        const QVector<ExtrusionObject> &b) {
    total_ = pattern.pattern.count * (pattern.pattern.kind == 0 ? pattern.pattern.count2 : 1);
    if (source_ != pattern.firstBody || feature_ != pattern.pattern.featureOnly) {
        source_ = pattern.firstBody;
        feature_ = pattern.pattern.featureOnly;
        parameters_ = patternParameters(pattern, s, b);
        const QSignalBlocker blocker(available_);
        available_->clear();
        for (int i = 0; i < parameters_.size(); ++i) {
            const auto &p = parameters_[i];
            if (p.reference.sketch && p.reference.parameter.endsWith("/value")) continue;
            available_->addItem(p.label + QStringLiteral(" [%1 %2]").arg(p.value, 0, 'g', 10).arg(p.unit), i);
        }
        available_->setToolTip(QStringLiteral("Parametri della sola funzione selezionata; le quote si scelgono nella vista."));
        add_->setEnabled(available_->count() > 0);
        rebuild();
    } else
        updateValues();
}
QVector<PatternVariation> PatternVariationEditor::dimensions() const {
    QVector<PatternVariation> out;
    for (const auto &p : parameters_)
        if (p.reference.sketch && p.reference.parameter.endsWith("/value")) out.append(p.reference);
    return out;
}
void PatternVariationEditor::selectParameter(const PatternVariation &reference) {
    if (!isEnabled()) return;
    if (std::none_of(parameters_.begin(), parameters_.end(), [&](const auto &p) {
        return samePatternParameter(reference, p.reference);
    })) return;
    int row = -1;
    for (int i = 0; i < values_.size(); ++i)
        if (samePatternParameter(values_[i], reference)) { row = i; break; }
    if (row < 0) {
        if (values_.size() >= 256) return;
        row = values_.size(); values_.append(reference); rebuild();
        if (changed) changed();
    }
    table_->setCurrentCell(row, 2);
    table_->scrollToItem(table_->item(row, 0));
    if (auto *input = qobject_cast<QDoubleSpinBox *>(table_->cellWidget(row, 2))) {
        input->setFocus(); input->selectAll();
    }
}
void PatternVariationEditor::rebuild() {
    table_->setRowCount(0);
    for (int row = 0; row < values_.size(); ++row) {
        table_->insertRow(row);
        const auto &v = values_[row];
        const PatternParameter *p = nullptr;
        for (const auto &candidate : parameters_)
            if (samePatternParameter(v, candidate.reference)) {
                p = &candidate;
                break;
            }
        for (int c = 0; c < 4; ++c) {
            auto *item = new QTableWidgetItem;
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, c, item);
        }
        table_->item(row, 0)->setText(p ? p->label : QStringLiteral("Parametro non disponibile: riselezionalo"));
        table_->item(row, 0)->setToolTip(table_->item(row, 0)->text());
        auto *increment = new ExpressionSpinBox(table_);
        increment->setObjectName(QStringLiteral("patternVariationIncrement%1").arg(row));
        increment->setRange(-1e9, 1e9);
        increment->setDecimals(p && p->integer ? 0 : 6);
        increment->setKeyboardTracking(false);
        increment->setLengthMeasurement(p && p->unit == QStringLiteral("mm"));
        if (p && !p->unit.isEmpty() && p->unit != QStringLiteral("mm"))
            increment->setSuffix(" " + p->unit);
        increment->setValue(v.increment);
        table_->setCellWidget(row, 2, increment);
        connect(increment, &QDoubleSpinBox::valueChanged, this, [this, row](double value) {
            values_[row].increment = value;
            updateValues();
            if (changed)
                changed();
        });
    }
    updateValues();
}
void PatternVariationEditor::updateValues() {
    for (int row = 0; row < values_.size(); ++row) {
        const auto &v = values_[row];
        const PatternParameter *p = nullptr;
        for (const auto &candidate : parameters_)
            if (samePatternParameter(v, candidate.reference)) {
                p = &candidate;
                break;
            }
        const double last = p ? p->value + (total_ - 1) * v.increment : 0;
        table_->item(row, 1)->setText(p ? (p->unit == QStringLiteral("mm")
                                               ? formatLength(p->value, 6, true, this)
                                               : QString::number(p->value, 'g', 10) + " " + p->unit)
                                        : QStringLiteral("—"));
        table_->item(row, 3)->setText(p ? (p->unit == QStringLiteral("mm")
                                               ? formatLength(last, 6, true, this)
                                               : QString::number(last, 'g', 10) + " " + p->unit)
                                        : QStringLiteral("—"));
        const bool invalid = !p || !std::isfinite(last) || last < p->minimum || last > p->maximum;
        for (int c : {0, 1, 3})
            table_->item(row, c)->setForeground(invalid ? QBrush(QColor(220, 70, 70)) : QBrush());
    }
}
} // namespace ForgeCad
