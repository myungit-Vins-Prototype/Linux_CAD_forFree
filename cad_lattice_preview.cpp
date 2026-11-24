#include "cad_lattice_preview.h"

#include "fk_cancel.h"
#include "fk_precision.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ForgeCad::Gui {
namespace {
using Kernel::Box;
using Kernel::Vec3;
using GridIndex = std::array<int, 3>;

struct Vertex {
    QVector3D point, normal;
};

void appendTriangle(BodyDisplay &mesh, const Vertex &a, const Vertex &b, const Vertex &c) {
    mesh.vertices.append(a.point);
    mesh.vertices.append(b.point);
    mesh.vertices.append(c.point);
    mesh.normals.append(a.normal);
    mesh.normals.append(b.normal);
    mesh.normals.append(c.normal);
}

std::vector<Vertex> clip(const std::vector<Vertex> &input, int axis, double limit, int side) {
    std::vector<Vertex> result;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const Vertex &a = input[i], &b = input[(i + 1) % input.size()];
        const double da = side * (double(a.point[axis]) - limit);
        const double db = side * (double(b.point[axis]) - limit);
        if (da >= 0) result.push_back(a);
        if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
            const float fraction = float(da / (da - db));
            result.push_back({a.point + fraction * (b.point - a.point),
                              (a.normal + fraction * (b.normal - a.normal)).normalized()});
        }
    }
    return result;
}

void appendPolygon(BodyDisplay &mesh, const std::vector<Vertex> &polygon) {
    for (std::size_t i = 2; i < polygon.size(); ++i)
        appendTriangle(mesh, polygon[0], polygon[i - 1], polygon[i]);
}

void splitMesh(const BodyDisplay &source, const Box &zone, int axis, BodyDisplay &retained, BodyDisplay *inside) {
    if (axis < 0) return;
    const double from = zone.lo[axis], to = zone.hi[axis];
    const bool normals = source.normals.size() == source.vertices.size();
    for (qsizetype i = 0; i + 2 < source.vertices.size(); i += 3) {
        if (i % 3072 == 0) Kernel::cancellationPoint();
        const auto &a = source.vertices[i], &b = source.vertices[i + 1], &c = source.vertices[i + 2];
        const double low = std::min({a[axis], b[axis], c[axis]});
        const double high = std::max({a[axis], b[axis], c[axis]});
        const QVector3D normal = normals ? QVector3D() : QVector3D::normal(a, b, c);
        const Vertex va{a, normals ? source.normals[i] : normal};
        const Vertex vb{b, normals ? source.normals[i + 1] : normal};
        const Vertex vc{c, normals ? source.normals[i + 2] : normal};
        if (high < from || low > to) {
            appendTriangle(retained, va, vb, vc);
        } else if (low >= from && high <= to) {
            if (inside) appendTriangle(*inside, va, vb, vc);
        } else {
            const std::vector<Vertex> triangle{va, vb, vc};
            if (low < from) appendPolygon(retained, clip(triangle, axis, from, -1));
            if (high > to) appendPolygon(retained, clip(triangle, axis, to, 1));
            if (inside) appendPolygon(*inside, clip(clip(triangle, axis, from, 1), axis, to, -1));
        }
    }
}

class DisplayGrid {
public:
    DisplayGrid(const Box &zone, double step) {
        for (int axis = 0; axis < 3; ++axis) {
            dims[axis] = std::max(1, int(std::ceil((zone.hi[axis] - zone.lo[axis] - Kernel::booleanConnectionTolerance()) / step)));
            for (int i = 0; i < dims[axis]; ++i)
                edges[axis].push_back(zone.lo[axis] + step * i);
            edges[axis].push_back(zone.hi[axis]);
            for (int i = 0; i < dims[axis]; ++i)
                centers[axis].push_back(0.5 * (edges[axis][i] + edges[axis][i + 1]));
        }
        cells.resize(std::size_t(dims[0]) * dims[1] * dims[2]);
    }

    void sample(const BodyDisplay &source, const PreparedVolumeStructure &structure) {
        std::vector<std::vector<double>> rows(std::size_t(dims[1]) * dims[2]);
        for (qsizetype i = 0; i + 2 < source.vertices.size(); i += 3) {
            if (i % 3072 == 0) Kernel::cancellationPoint();
            const auto &a = source.vertices[i], &b = source.vertices[i + 1], &c = source.vertices[i + 2];
            const double uy = double(b.y()) - a.y(), uz = double(b.z()) - a.z();
            const double vy = double(c.y()) - a.y(), vz = double(c.z()) - a.z();
            const double det = uy * vz - uz * vy;
            if (std::abs(det) <= 1e-24) continue;
            const auto &ys = centers[1], &zs = centers[2];
            const auto yEnd = std::upper_bound(ys.begin(), ys.end(), std::max({a.y(), b.y(), c.y()}));
            const auto zEnd = std::upper_bound(zs.begin(), zs.end(), std::max({a.z(), b.z(), c.z()}));
            for (auto z = std::lower_bound(zs.begin(), zs.end(), std::min({a.z(), b.z(), c.z()})); z != zEnd; ++z)
                for (auto y = std::lower_bound(ys.begin(), ys.end(), std::min({a.y(), b.y(), c.y()})); y != yEnd; ++y) {
                    const double dy = *y - a.y(), dz = *z - a.z();
                    const double u = (dy * vz - dz * vy) / det, v = (uy * dz - uz * dy) / det;
                    if (u < -1e-10 || v < -1e-10 || u + v > 1.0 + 1e-10) continue;
                    rows[std::size_t(z - zs.begin()) * dims[1] + (y - ys.begin())].push_back(
                        a.x() + u * (double(b.x()) - a.x()) + v * (double(c.x()) - a.x()));
                }
        }
        for (int z = 0; z < dims[2]; ++z) {
            Kernel::cancellationPoint();
            for (int y = 0; y < dims[1]; ++y) {
                auto &hits = rows[std::size_t(z) * dims[1] + y];
                std::sort(hits.begin(), hits.end());
                hits.erase(std::unique(hits.begin(), hits.end(), [](double a, double b) {
                    return std::abs(a - b) <= 1e-6;
                }), hits.end());
                // Una riga ambigua della tassellazione non fornisce una parita'.
                if (hits.size() % 2 != 0) continue;
                std::size_t crossing = 0;
                for (int x = 0; x < dims[0]; ++x) {
                    const Vec3 p{centers[0][x], centers[1][y], centers[2][z]};
                    while (crossing < hits.size() && hits[crossing] <= p.x()) ++crossing;
                    cells[index({x, y, z})] = (crossing % 2 != 0) && structure.containsMaterial(p);
                }
            }
        }
    }

    BodyDisplay display() const {
        BodyDisplay result;
        for (int z = 0; z < dims[2]; ++z) {
            Kernel::cancellationPoint();
            for (int y = 0; y < dims[1]; ++y)
                for (int x = 0; x < dims[0]; ++x) {
                    const GridIndex cell{x, y, z};
                    if (!occupied(cell)) continue;
                    for (int axis = 0; axis < 3; ++axis)
                        for (int side : {-1, 1}) {
                            GridIndex adjacent = cell;
                            adjacent[axis] += side;
                            if (!occupied(adjacent)) appendFace(result, cell, axis, side);
                        }
                }
        }
        return result;
    }

private:
    std::size_t index(const GridIndex &cell) const {
        return (std::size_t(cell[2]) * dims[1] + cell[1]) * dims[0] + cell[0];
    }

    bool occupied(const GridIndex &cell) const {
        for (int axis = 0; axis < 3; ++axis)
            if (cell[axis] < 0 || cell[axis] >= dims[axis]) return false;
        return cells[index(cell)] != 0;
    }

    void appendFace(BodyDisplay &mesh, GridIndex cell, int axis, int side) const {
        if (side > 0) ++cell[axis];
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        QVector3D normal;
        normal[axis] = float(side);
        const auto corner = [&](int du, int dv) {
            auto node = cell;
            node[u] += du;
            node[v] += dv;
            return Vertex{QVector3D(float(edges[0][node[0]]), float(edges[1][node[1]]), float(edges[2][node[2]])), normal};
        };
        const Vertex a = corner(0, 0), b = corner(1, 0), c = corner(1, 1), d = corner(0, 1);
        if (side > 0) {
            appendTriangle(mesh, a, b, c);
            appendTriangle(mesh, a, c, d);
        } else {
            appendTriangle(mesh, a, c, b);
            appendTriangle(mesh, a, d, c);
        }
    }

    GridIndex dims{};
    std::array<std::vector<double>, 3> edges, centers;
    std::vector<unsigned char> cells;
};

}

LatticePreviewMeshes latticePreviewMeshes(const BodyDisplay &source, const PreparedVolumeStructure &structure, int axis) {
    LatticePreviewMeshes result;
    const auto &zone = structure.region;
    const Vec3 size = zone.hi - zone.lo;
    double step = structure.settings.samplingPitch;
    while (std::ceil(size.x() / step) * std::ceil(size.y() / step) * std::ceil(size.z() / step) > 120000) {
        step *= 1.25;
        result.simplified = true;
    }
    DisplayGrid grid(zone, step);
    grid.sample(source, structure);
    result.lattice = grid.display();
    splitMesh(source, zone, axis, result.retained, nullptr);
    return result;
}

LatticePreviewMeshes latticeResultMeshes(BodyDisplay result, const Box &zone, int axis) {
    LatticePreviewMeshes meshes;
    if (axis < 0) meshes.lattice = std::move(result);
    else splitMesh(result, zone, axis, meshes.retained, &meshes.lattice);
    return meshes;
}

}
