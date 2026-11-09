#pragma once

// Tabella virtuale dei poli NURBS: conserva la superficie immutabile e solo
// le celle modificate. La gabbia campiona indici reali, senza ridurre il B-rep.
#include "cad_types.h"
#include "fk_bspline_surface.h"
#include <QAbstractTableModel>
#include <QMap>
#include <array>

namespace ForgeCad {
class SurfacePolesModel final : public QAbstractTableModel {
  public:
    explicit SurfacePolesModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    void setSurface(std::shared_ptr<const Kernel::BSplineSurface> surface, const QVector<SurfacePoleEdit> &edits = {});
    std::array<double, 4> values(int row) const;
    QString edits(QVector<SurfacePoleEdit> &result) const;
    std::shared_ptr<const Kernel::BSplineSurface> modifiedSurface() const;
    struct Cage {
        QVector<int> rows;
        int u = 0, v = 0;
    };
    Cage cage(const QVector<int> &selected) const;

  private:
    std::array<double, 4> original(int row) const;
    std::shared_ptr<const Kernel::BSplineSurface> surface_;
    QMap<int, std::array<QString, 4>> edited_;
};
} // namespace ForgeCad
