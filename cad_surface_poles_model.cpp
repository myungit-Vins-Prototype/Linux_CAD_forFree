#include "cad_surface_poles_model.h"
#include <QLocale>
#include <QColor>
#include <QSet>
#include <cmath>
#include <limits>
#include <algorithm>

namespace ForgeCad {
SurfacePolesModel::SurfacePolesModel(QObject *parent) : QAbstractTableModel(parent) {}
int SurfacePolesModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    if (!faces_.empty()) {
        const auto &last = faces_.back();
        return last.offset + last.model->rowCount();
    }
    return surface_ ? surface_->uPoleCount() * surface_->vPoleCount() : 0;
}
int SurfacePolesModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : coordinateColumn() + 4;
}
std::array<double, 4> SurfacePolesModel::original(int row) const {
    const int u = row / surface_->vPoleCount(), v = row % surface_->vPoleCount();
    const auto point = surface_->pole(u, v);
    return {point.x(), point.y(), point.z(), surface_->weight(u, v)};
}
std::array<double, 4> SurfacePolesModel::values(int row) const {
    if (!faces_.empty())
        return faces_[faceIndex(row)].model->values(faceRow(row));
    auto result = original(row);
    const auto found = edited_.constFind(row);
    if (found != edited_.cend())
        for (int c = 0; c < 4; ++c) {
            bool valid = false;
            result[c] = QLocale().toDouble(found.value()[c], &valid);
            if (!valid)
                result[c] = std::numeric_limits<double>::quiet_NaN();
        }
    if(found==edited_.cend() && automatic_.contains(row))return automatic_.value(row);
    return result;
}
QVariant SurfacePolesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rowCount() || index.column() >= columnCount() ||
        (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole && role != Qt::BackgroundRole))
        return {};
    if (!faces_.empty()) {
        const auto &face = faces_[faceIndex(index.row())];
        if (index.column() == 0)
            return role==Qt::BackgroundRole?QVariant():QVariant(face.label);
        return face.model->data(face.model->index(faceRow(index.row()), index.column() - 1), role);
    }
    if(role==Qt::BackgroundRole)return ((automatic_.contains(index.row())&&!edited_.contains(index.row()))||automaticManual_.contains(index.row()))?QVariant(QColor(55,115,75,100)):QVariant();
    if(role==Qt::ToolTipRole&&((automatic_.contains(index.row())&&!edited_.contains(index.row()))||automaticManual_.contains(index.row())))return QStringLiteral("Polo spostato automaticamente dal vincolo di continuità.");
    if (index.column() < 2)
        return index.column() == 0 ? index.row() / surface_->vPoleCount() + 1
                                   : index.row() % surface_->vPoleCount() + 1;
    if(!edited_.contains(index.row())&&automatic_.contains(index.row()))
        return QLocale().toString(automatic_.value(index.row())[index.column()-2], 'g', 17);
    const auto found = edited_.constFind(index.row());
    return found == edited_.cend() ? QLocale().toString(original(index.row())[index.column() - 2], 'g', 17)
                                   : found.value()[index.column() - 2];
}
QVariant SurfacePolesModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (!faces_.empty() && orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == 0)
            return QStringLiteral("Faccia");
        return faces_.front().model->headerData(section - 1, orientation, role);
    }
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < 6)
        return QStringList{"U", "V", "X", "Y", "Z", QStringLiteral("Peso")}[section];
    return QAbstractTableModel::headerData(section, orientation, role);
}
Qt::ItemFlags SurfacePolesModel::flags(const QModelIndex &index) const {
    return QAbstractTableModel::flags(index) |
           (index.isValid() && index.column() >= coordinateColumn() ? Qt::ItemIsEditable : Qt::NoItemFlags);
}
bool SurfacePolesModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (role != Qt::EditRole || !index.isValid() || index.row() >= rowCount() || index.column() < coordinateColumn() ||
        index.column() >= columnCount())
        return false;
    if (!faces_.empty()) {
        auto &face = faces_[faceIndex(index.row())];
        const bool changed =
            face.model->setData(face.model->index(faceRow(index.row()), index.column() - 1), value, role);
        if (changed)
            emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
        return changed;
    }
    auto found = edited_.find(index.row());
    if (found == edited_.end()) {
        std::array<QString, 4> text;
        const auto numbers = values(index.row());
        for (int c = 0; c < 4; ++c)
            text[c] = QLocale().toString(numbers[c], 'g', 17);
        found = edited_.insert(index.row(), text);
    }
    found.value()[index.column() - 2] = value.toString();
    const auto source = original(index.row());
    bool unchanged = true;
    for (int c = 0; c < 4; ++c)
        unchanged = unchanged && found.value()[c] == QLocale().toString(source[c], 'g', 17);
    if (unchanged&&!constraintMode_)
        edited_.erase(found);
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
    return true;
}
void SurfacePolesModel::setSurface(std::shared_ptr<const Kernel::BSplineSurface> surface,
                                   const QVector<SurfacePoleEdit> &edits) {
    beginResetModel();
    faces_.clear();
    surface_ = std::move(surface);
    edited_.clear();automatic_.clear();automaticManual_.clear();
    if (surface_)
        for (const auto &edit : edits) {
            if (edit.u < 0 || edit.u >= surface_->uPoleCount() || edit.v < 0 || edit.v >= surface_->vPoleCount())
                continue;
            const int row = edit.u * surface_->vPoleCount() + edit.v;
            auto numbers = original(row);
            numbers[0] += edit.dx;
            numbers[1] += edit.dy;
            numbers[2] += edit.dz;
            numbers[3] *= edit.weightFactor;
            std::array<QString, 4> text;
            for (int c = 0; c < 4; ++c)
                text[c] = QLocale().toString(numbers[c], 'g', 17);
            edited_.insert(row, text);
        }
    endResetModel();
}
QString SurfacePolesModel::edits(QVector<SurfacePoleEdit> &result,bool resolved) const {
    result.clear();
    if (!faces_.empty())
        return QStringLiteral("Usare le modifiche per faccia.");
    QSet<int> keys;for(auto it=edited_.cbegin();it!=edited_.cend();++it)keys.insert(it.key());
    if(resolved)for(auto it=automatic_.cbegin();it!=automatic_.cend();++it)keys.insert(it.key());
    QVector<int> rows(keys.cbegin(),keys.cend());std::sort(rows.begin(),rows.end());
    for(int row:rows) {
        const auto numbers = values(row), source = original(row);
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite(numbers[c]) || (c == 3 && numbers[c] <= 0))
                return QStringLiteral("Coordinate finite e pesi positivi richiesti alla riga %1.").arg(row + 1);
        const Kernel::Vec3 delta(numbers[0] - source[0], numbers[1] - source[1], numbers[2] - source[2]);
        const double factor = numbers[3] / source[3];
        if ((constraintMode_&&edited_.contains(row)) || Kernel::norm(delta) > 1e-11 || std::fabs(factor - 1) > 1e-12)
            result.append({row / surface_->vPoleCount(), row % surface_->vPoleCount(), delta.x(), delta.y(),
                           delta.z(), factor});
        if (result.size() > 4096)
            return QStringLiteral("Modifica fino a 4096 poli per funzione.");
    }
    return {};
}
std::shared_ptr<const Kernel::BSplineSurface> SurfacePolesModel::modifiedSurface(int row) const {
    if (!faces_.empty())
        return faces_[faceIndex(row)].model->modifiedSurface();
    if (edited_.isEmpty()&&automatic_.isEmpty())
        return surface_;
    std::vector<Kernel::Vec3> points;
    std::vector<double> weights;
    points.reserve(rowCount());
    weights.reserve(rowCount());
    for (int row = 0; row < rowCount(); ++row) {
        const auto numbers = values(row);
        points.emplace_back(numbers[0], numbers[1], numbers[2]);
        weights.push_back(numbers[3]);
    }
    return std::make_shared<const Kernel::BSplineSurface>(
        surface_->uDegree(), surface_->vDegree(), surface_->uKnots(), surface_->vKnots(), surface_->uPoleCount(),
        surface_->vPoleCount(), std::move(points), std::move(weights), surface_->isUPeriodic(), surface_->isVPeriodic());
}
SurfacePolesModel::Cage SurfacePolesModel::cage(const QVector<int> &selected, int budget) const {
    Cage result;
    if (!faces_.empty()) {
        for (const auto &face : faces_) {
            QVector<int> local;
            for (int row : selected)
                if (row >= face.offset && row < face.offset + face.model->rowCount())
                    local.append(row - face.offset);
            const auto child = face.model->cage(local, std::max(4, budget / int(faces_.size())));
            result.grids.append({int(result.rows.size()), child.u, child.v});
            for (int row : child.rows)
                result.rows.append(face.offset + row);
        }
        if (!result.grids.isEmpty()) {
            result.u = result.grids[0][1];
            result.v = result.grids[0][2];
        }
        return result;
    }
    if (!surface_)
        return result;
    const int nu = surface_->uPoleCount(), nv = surface_->vPoleCount();
    // Limita il solo display, includendo sempre entrambi i bordi del dominio.
    int step = std::max(1, int(std::ceil(std::sqrt(double(rowCount()) / budget))));
    const auto count = [&](int n) { return (n - 1 + step - 1) / step + 1; };
    while (qint64(count(nu)) * count(nv) > budget)
        ++step;
    result.u = count(nu);
    result.v = count(nv);
    for (int i = 0; i < result.u; ++i)
        for (int j = 0; j < result.v; ++j)
            result.rows.append(std::min(i * step, nu - 1) * nv + std::min(j * step, nv - 1));
    result.grids.append({0, result.u, result.v});
    QSet<int> included(result.rows.cbegin(), result.rows.cend());
    for (int row : selected)
        if (row >= 0 && row < rowCount() && !included.contains(row)) {
            result.rows.append(row);
            included.insert(row);
        }
    return result;
}
void SurfacePolesModel::setFaces(const QVector<FaceGrid> &grids) {
    beginResetModel();
    surface_.reset();
    edited_.clear();
    faces_.clear();
    int offset = 0;
    for (const auto &grid : grids) {
        FaceModel face;
        face.model = std::make_unique<SurfacePolesModel>();
        face.model->setSurface(grid.surface, grid.edits);
        face.label = grid.label;
        face.offset = offset;
        offset += face.model->rowCount();
        faces_.push_back(std::move(face));
    }
    endResetModel();
}
int SurfacePolesModel::faceIndex(int row) const {
    for (int i = int(faces_.size()) - 1; i >= 0; --i)
        if (row >= faces_[i].offset)
            return i;
    return -1;
}
int SurfacePolesModel::faceRow(int row) const { return faces_.empty() ? row : row - faces_[faceIndex(row)].offset; }
QString SurfacePolesModel::faceEdits(int face, QVector<SurfacePoleEdit> &result,bool resolved) const {
    return faces_.empty() ? edits(result,resolved) : faces_.at(face).model->edits(result,resolved);
}
std::shared_ptr<const Kernel::BSplineSurface> SurfacePolesModel::surfaceForRow(int row) const {
    return faces_.empty() ? surface_ : faces_[faceIndex(row)].model->surface_;
}
QVector<QVector<SurfacePoleEdit>> SurfacePolesModel::automaticEdits()const {
    QVector<QVector<SurfacePoleEdit>> result;
    const auto collect=[](const SurfacePolesModel &m){QVector<SurfacePoleEdit> result;
        for(auto it=m.automatic_.cbegin();it!=m.automatic_.cend();++it)if(!m.edited_.contains(it.key())){
            const auto source=m.original(it.key());const auto p=it.value();
            result.append({it.key()/m.surface_->vPoleCount(),it.key()%m.surface_->vPoleCount(),p[0]-source[0],p[1]-source[1],p[2]-source[2],p[3]/source[3]});
        }return result;
    };
    if(faces_.empty()){result.append(collect(*this));}
    else for(const auto &face:faces_)result.append(collect(*face.model));
    return result;
}
SurfacePolesModel::EditSnapshot SurfacePolesModel::snapshot() const {
    if(faces_.empty())return edited_;
    EditSnapshot result;
    for(const auto &face:faces_)for(auto it=face.model->edited_.cbegin();it!=face.model->edited_.cend();++it)
        result.insert(face.offset+it.key(),it.value());
    return result;
}
void SurfacePolesModel::restore(const EditSnapshot &snapshot) {
    edited_.clear();automatic_.clear();automaticManual_.clear();
    for(auto &face:faces_){face.model->edited_.clear();face.model->automatic_.clear();face.model->automaticManual_.clear();}
    for(auto it=snapshot.cbegin();it!=snapshot.cend();++it)if(it.key()>=0&&it.key()<rowCount()) {
        if(faces_.empty())edited_.insert(it.key(),it.value());
        else faces_[faceIndex(it.key())].model->edited_.insert(faceRow(it.key()),it.value());
    }
    if(rowCount())emit dataChanged(index(0,0),index(rowCount()-1,columnCount()-1));
}
void SurfacePolesModel::setConstraintMode(bool enabled) {
    constraintMode_=enabled;if(!enabled)automaticManual_.clear();
    for(auto &face:faces_)face.model->setConstraintMode(enabled);
}
void SurfacePolesModel::setAutomatic(const QVector<QVector<SurfacePoleEdit>> &edits,const QSet<int> &drivers,bool fromAction) {
    const auto fill=[&](SurfacePolesModel &model,const QVector<SurfacePoleEdit> &changes,int offset) {
        model.automatic_.clear();if(!model.constraintMode_)model.automaticManual_.clear();
        for(const auto &e:changes){const int row=e.u*model.surface_->vPoleCount()+e.v;
            auto values=model.original(row);values[0]+=e.dx;values[1]+=e.dy;values[2]+=e.dz;values[3]*=e.weightFactor;
            if(model.edited_.contains(row)){
                const auto previous=model.values(row);
                if(fromAction){
                    if(drivers.contains(offset+row))model.automaticManual_.remove(row);
                    else if(std::hypot(std::hypot(previous[0]-values[0],previous[1]-values[1]),previous[2]-values[2])>1e-11)model.automaticManual_.insert(row);
                }
                for(int k=0;k<4;++k)model.edited_[row][k]=QLocale().toString(values[k],'g',17);
            }else model.automatic_[row]=values;
        }
    };
    if(faces_.empty()){if(surface_)fill(*this,edits.value(0),0);}
    else for(int i=0;i<int(faces_.size());++i)fill(*faces_[i].model,edits.value(i),faces_[i].offset);
    if(rowCount())emit dataChanged(index(0,0),index(rowCount()-1,columnCount()-1));
}
QSet<int> SurfacePolesModel::automaticRows() const {
    QSet<int> result;
    if(faces_.empty()){result=automaticManual_;for(auto it=automatic_.cbegin();it!=automatic_.cend();++it){if(!edited_.contains(it.key()))result.insert(it.key());}}
    else for(const auto &face:faces_)for(int row:face.model->automaticRows())result.insert(face.offset+row);
    return result;
}
} // namespace ForgeCad
