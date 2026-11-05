#pragma once
#include "cad_loft_correspondence.h"
#include <QWidget>
#include <QMap>
#include <functional>
class CadViewport;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
namespace ForgeCad {
class LoftCorrespondenceOverlay;
class LoftCorrespondenceEditor final : public QWidget {
public:
    LoftCorrespondenceEditor(CadViewport *viewport, int owner, QWidget *parent);
    ~LoftCorrespondenceEditor() override;
    void initialize(const QVector<int> &keys, const QVector<LoftSectionCorrespondence> &rows);
    void synchronize(const ExtrusionObject &definition, const QVector<int> &keys);
    void copyTo(ExtrusionObject &definition, const QVector<int> &keys) const;
    void stop();
    std::function<void()> changed;
private:
    friend class LoftCorrespondenceOverlay;
    bool eventFilter(QObject *object, QEvent *event) override;
    void rebuild();
    void updatePoints();
    void updateWidgets();
    void notifyChange();
    void setPosition(double fraction);
    bool locked(int section, int column) const;
    QPointF screen(const Kernel::Vec3 &point) const;
    CadViewport *viewport_;
    int owner_;
    LoftCorrespondenceOverlay *overlay_;
    QCheckBox *enabled_;
    QComboBox *section_, *connector_;
    QDoubleSpinBox *position_;
    QPushButton *reverse_, *add_, *remove_;
    QLabel *status_;
    ExtrusionObject definition_;
    QVector<int> keys_;
    QMap<int,LoftSectionCorrespondence> values_;
    std::vector<Kernel::LoftSection> sections_;
    std::vector<std::vector<Kernel::PathSegment>> paths_;
    std::vector<std::vector<Kernel::Vec3>> points_;
    std::vector<bool> directions_;
    bool closed_ = false, updating_ = false, dragging_ = false, stopped_ = false;
    int dragSection_ = -1, dragColumn_ = -1;
    double dragOriginal_ = 0;
};
}
