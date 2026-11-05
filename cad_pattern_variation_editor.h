#pragma once
#include "cad_pattern_variation.h"
#include <QWidget>
#include <functional>
class QComboBox;
class QTableWidget;
class QPushButton;
namespace ForgeCad {
class PatternVariationEditor final : public QWidget {
  public:
    explicit PatternVariationEditor(QWidget *parent = nullptr);
    void setVariations(const QVector<PatternVariation> &values);
    QVector<PatternVariation> variations() const {
        return values_;
    }
    void setContext(const ExtrusionObject &pattern, const QVector<SketchObject> &sketches,
                    const QVector<ExtrusionObject> &bodies);
    std::function<void()> changed;
    QVector<PatternVariation> dimensions() const;
    void selectParameter(const PatternVariation &reference);

  private:
    void rebuild();
    void updateValues();
    QVector<PatternVariation> values_;
    QVector<PatternParameter> parameters_;
    QComboBox *available_;
    QTableWidget *table_;
    QPushButton *add_;
    int source_ = -2, total_ = 1;
    bool feature_ = false;
};
} // namespace ForgeCad
