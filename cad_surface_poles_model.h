#pragma once

// Tabella virtuale dei poli NURBS: conserva la superficie immutabile e solo
// le celle modificate. La gabbia campiona indici reali, senza ridurre il B-rep.
#include "cad_types.h"
#include "fk_bspline_surface.h"
#include <QAbstractTableModel>
#include <QSet>
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
    struct FaceGrid {
        std::shared_ptr<const Kernel::BSplineSurface> surface;
        QVector<SurfacePoleEdit> edits;
        QString label;
    };
    void setFaces(const QVector<FaceGrid> &faces);
    int coordinateColumn() const { return faces_.empty() ? 2 : 3; }
    int faceIndex(int row) const;
    int faceRow(int row) const;
    QString faceEdits(int face, QVector<SurfacePoleEdit> &result,bool resolved=false) const;
    std::shared_ptr<const Kernel::BSplineSurface> surfaceForRow(int row) const;
    std::array<double, 4> values(int row) const;
    QString edits(QVector<SurfacePoleEdit> &result,bool resolved=false) const;
    QVector<QVector<SurfacePoleEdit>> automaticEdits() const;
    std::shared_ptr<const Kernel::BSplineSurface> modifiedSurface(int row = 0) const;
    struct Cage {
        QVector<int> rows;
        int u = 0, v = 0;
        QVector<std::array<int, 3>> grids; // primo punto, conteggi U/V di ogni gabbia
    };
    using EditSnapshot = QMap<int, std::array<QString, 4>>;
    EditSnapshot snapshot() const;
    void restore(const EditSnapshot &snapshot);
    void setConstraintMode(bool enabled);
    void setAutomatic(const QVector<QVector<SurfacePoleEdit>> &edits,const QSet<int> &drivers={},bool fromAction=false);
    QSet<int> automaticRows() const;
    Cage cage(const QVector<int> &selected, int budget = 4096) const;

  private:
    struct FaceModel {
        std::unique_ptr<SurfacePolesModel> model;
        QString label;
        int offset = 0;
    };
    std::vector<FaceModel> faces_;
    std::array<double, 4> original(int row) const;
    std::shared_ptr<const Kernel::BSplineSurface> surface_;
    QMap<int, std::array<QString, 4>> edited_;
    QMap<int, std::array<double, 4>> automatic_;
    QSet<int> automaticManual_;
    bool constraintMode_=false;
};
} // namespace ForgeCad
