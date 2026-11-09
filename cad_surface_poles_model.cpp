#include "cad_surface_poles_model.h"
#include <QLocale>
#include <QSet>
#include <cmath>
#include <limits>

namespace ForgeCad {
SurfacePolesModel::SurfacePolesModel(QObject *parent) : QAbstractTableModel(parent) {}
int SurfacePolesModel::rowCount(const QModelIndex &parent) const {
    return !parent.isValid() && surface_ ? surface_->uPoleCount() * surface_->vPoleCount() : 0;
}
int SurfacePolesModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 6; }
std::array<double, 4> SurfacePolesModel::original(int row) const {
    const int u = row / surface_->vPoleCount(), v = row % surface_->vPoleCount();
    const auto point = surface_->pole(u, v);
    return {point.x(), point.y(), point.z(), surface_->weight(u, v)};
}
std::array<double, 4> SurfacePolesModel::values(int row) const {
    auto result = original(row);
    const auto found = edited_.constFind(row);
    if (found != edited_.cend())
        for (int c = 0; c < 4; ++c) {
            bool valid = false;
            result[c] = QLocale().toDouble(found.value()[c], &valid);
            if (!valid)
                result[c] = std::numeric_limits<double>::quiet_NaN();
        }
    return result;
}
QVariant SurfacePolesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rowCount() || index.column() >= 6 ||
        (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole))
        return {};
    if (index.column() < 2)
        return index.column() == 0 ? index.row() / surface_->vPoleCount() + 1
                                   : index.row() % surface_->vPoleCount() + 1;
    const auto found = edited_.constFind(index.row());
    return found == edited_.cend() ? QLocale().toString(original(index.row())[index.column() - 2], 'g', 17)
                                   : found.value()[index.column() - 2];
}
QVariant SurfacePolesModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < 6)
        return QStringList{"U", "V", "X", "Y", "Z", QStringLiteral("Peso")}[section];
    return QAbstractTableModel::headerData(section, orientation, role);
}
Qt::ItemFlags SurfacePolesModel::flags(const QModelIndex &index) const {
    return QAbstractTableModel::flags(index) |
           (index.isValid() && index.column() >= 2 ? Qt::ItemIsEditable : Qt::NoItemFlags);
}
bool SurfacePolesModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (role != Qt::EditRole || !index.isValid() || index.row() >= rowCount() || index.column() < 2 ||
        index.column() >= 6)
        return false;
    auto found = edited_.find(index.row());
    if (found == edited_.end()) {
        std::array<QString, 4> text;
        const auto numbers = original(index.row());
        for (int c = 0; c < 4; ++c)
            text[c] = QLocale().toString(numbers[c], 'g', 17);
        found = edited_.insert(index.row(), text);
    }
    found.value()[index.column() - 2] = value.toString();
    const auto source = original(index.row());
    bool unchanged = true;
    for (int c = 0; c < 4; ++c)
        unchanged = unchanged && found.value()[c] == QLocale().toString(source[c], 'g', 17);
    if (unchanged)
        edited_.erase(found);
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
    return true;
}
void SurfacePolesModel::setSurface(std::shared_ptr<const Kernel::BSplineSurface> surface,
                                   const QVector<SurfacePoleEdit> &edits) {
    beginResetModel();
    surface_ = std::move(surface);
    edited_.clear();
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
QString SurfacePolesModel::edits(QVector<SurfacePoleEdit> &result) const {
    result.clear();
    for (auto it = edited_.cbegin(); it != edited_.cend(); ++it) {
        const auto numbers = values(it.key()), source = original(it.key());
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite(numbers[c]) || (c == 3 && numbers[c] <= 0))
                return QStringLiteral("Coordinate finite e pesi positivi richiesti alla riga %1.").arg(it.key() + 1);
        const Kernel::Vec3 delta(numbers[0] - source[0], numbers[1] - source[1], numbers[2] - source[2]);
        const double factor = numbers[3] / source[3];
        if (Kernel::norm(delta) > 1e-11 || std::fabs(factor - 1) > 1e-12)
            result.append({it.key() / surface_->vPoleCount(), it.key() % surface_->vPoleCount(), delta.x(), delta.y(),
                           delta.z(), factor});
        if (result.size() > 4096)
            return QStringLiteral("Modifica fino a 4096 poli per funzione.");
    }
    return {};
}
std::shared_ptr<const Kernel::BSplineSurface> SurfacePolesModel::modifiedSurface() const {
    if (edited_.isEmpty())
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
        surface_->vPoleCount(), std::move(points), std::move(weights));
}
SurfacePolesModel::Cage SurfacePolesModel::cage(const QVector<int> &selected) const {
    Cage result;
    if (!surface_)
        return result;
    const int nu = surface_->uPoleCount(), nv = surface_->vPoleCount();
    // Limita il solo display, includendo sempre entrambi i bordi del dominio.
    int step = std::max(1, int(std::ceil(std::sqrt(double(rowCount()) / 4096))));
    const auto count = [&](int n) { return (n - 1 + step - 1) / step + 1; };
    while (qint64(count(nu)) * count(nv) > 4096)
        ++step;
    result.u = count(nu);
    result.v = count(nv);
    for (int i = 0; i < result.u; ++i)
        for (int j = 0; j < result.v; ++j)
            result.rows.append(std::min(i * step, nu - 1) * nv + std::min(j * step, nv - 1));
    QSet<int> included(result.rows.cbegin(), result.rows.cend());
    for (int row : selected)
        if (row >= 0 && row < rowCount() && !included.contains(row)) {
            result.rows.append(row);
            included.insert(row);
        }
    return result;
}
} // namespace ForgeCad
