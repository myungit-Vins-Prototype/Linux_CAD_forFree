#include "fk_sheet_gyroid.h"

#include "fk_body_check.h"
#include "fk_cancel.h"
#include "fk_classify.h"
#include "fk_mass.h"
#include "fk_parallel.h"
#include "fk_planar.h"
#include "fk_primitives.h"
#include "fk_sew.h"
#include "fk_sheet.h"
#include "fk_solid_trim.h"
#include "fk_tpms_cells.h"
#include "fk_tpms_field.h"
#include "fk_tpms_spline.h"
#include "fk_unify.h"
#include "fk_precision.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace ForgeCad::Kernel {
namespace {

using Index = TpmsGridIndex;
using Triangle = std::array<int, 3>;
constexpr std::size_t maxTriangles = 500000;

// Estremi associati ai dodici lati usati per connettere il contorno.
constexpr int cubeEdges[12][2] = {
    {0,1}, {2,3}, {4,5}, {6,7}, {0,2}, {1,3},
    {4,6}, {5,7}, {0,4}, {1,5}, {2,6}, {3,7}
};

Index gridSize(const Box &bounds, double step) {
    std::size_t remaining = kMaxTpmsNodes;
    Index dims{};
    for (int a = 0; a < 3; ++a) {
        const double n = std::ceil((bounds.hi[a] - bounds.lo[a]) / step) + 3.0;
        if (!(n >= 4 && n <= double(remaining)))
            throw std::domain_error("TPMS: aumenta il passo di campionamento o riduci il corpo.");
        dims[a] = int(n);
        remaining /= std::size_t(dims[a]);
    }
    return dims;
}

std::uint64_t edgeKey(int a, int b) {
    return (std::uint64_t(std::min(a, b)) << 32) | std::uint64_t(std::max(a, b));
}

struct AxialCrossing {
    int node;
    bool startsInside;
    double coordinate;
};

using AxialCrossings = std::array<std::vector<AxialCrossing>, 3>;

struct Grid {
    Vec3 origin;
    Index size;
    Index stride;
    double step;
    std::vector<double> field;
    std::array<std::vector<double>, 3> planes;

    Grid(const Box &bounds, double pitch)
        : origin(bounds.lo - Vec3(.603, .571, .649) * pitch),
          size(gridSize(bounds, pitch)), stride{1, size[0], size[0] * size[1]},
          step(pitch), field(std::size_t(stride[2]) * size[2], -2.0 * pitch) {
        for (int a = 0; a < 3; ++a)
            if (!(origin[a] + pitch > origin[a] && origin[a] < bounds.lo[a]))
                throw std::domain_error("TPMS: passo non risolvibile alle coordinate del corpo.");
    }

    double coordinate(int axis, int i) const {
        return planes[axis].empty() ? origin[axis] + i * step : planes[axis][i];
    }

    // Allinea uno strato al taglio. L'interpolazione riguarda solo il
    // campo campionato del reticolo. Il taglio del pezzo pieno usa il B-rep.
    int alignPlane(int axis, double value) {
        const int i = int(std::lround((value - origin[axis]) / step));
        if (i <= 0 || i >= size[axis] - 1)
            throw std::domain_error("TPMS: il piano di giunzione è fuori dalla griglia.");
        const double here = coordinate(axis, i);
        if (here == value) return i;
        const int neighbor = value < here ? i - 1 : i + 1;
        const double fraction = (value - here) / (coordinate(axis, neighbor) - here);
        if (!(fraction > 0.0 && fraction < 1.0))
            throw std::domain_error("TPMS: i due piani sono troppo vicini per il passo scelto.");
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        for (int j = 0; j < size[v]; ++j) {
            cancellationPoint();
            for (int k = 0; k < size[u]; ++k) {
                const int id = i * stride[axis] + j * stride[v] + k * stride[u];
                field[id] += fraction * (field[id + (neighbor - i) * stride[axis]] - field[id]);
            }
        }
        if (planes[axis].empty()) {
            auto &values = planes[axis];
            values.reserve(size[axis]);
            for (int j = 0; j < size[axis]; ++j) values.push_back(origin[axis] + j * step);
        }
        planes[axis][i] = value;
        return i;
    }
    Vec3 point(int id) const {
        return {coordinate(0, id % size[0]), coordinate(1, id / stride[1] % size[1]),
                coordinate(2, id / stride[2])};
    }
};

double boxInterior(const Vec3 &point, const Box &box) {
    double depth = std::numeric_limits<double>::infinity();
    for (int a = 0; a < 3; ++a)
        depth = std::min({depth, point[a] - box.lo[a], box.hi[a] - point[a]});
    return depth;
}

void sampleEnvelope(Grid &grid, const Body &body, const RayFaceIndex &index, AxialCrossings &axialCrossings) {
    const double tolerance = booleanConnectionTolerance();
    const SolidClassifier classifier(body, tolerance, true);
    const double truncation = 2 * grid.step;
    // Lungo ciascun asse si conservano le distanze dagli attraversamenti
    // esatti. Il minimo dei moduli riduce l'errore sulle pareti tangenti
    // a una delle direzioni. Le righe ambigue lasciano intatti gli altri assi.
    std::vector<double> depth(grid.field.size(), truncation);
    std::vector<unsigned char> votes(depth.size(), 0);
    for (const int a : {0, 2, 1}) {
        const int u = (a + 1) % 3, v = (a + 2) % 3;
        std::vector<std::vector<AxialCrossing>> rows(std::size_t(grid.size[u]) * grid.size[v]);
        parallelFor(rows.size(), std::min(8u, threadCount(0)), [&](std::size_t row) {
            const int i = int(row % grid.size[u]), j = int(row / grid.size[u]);
            Vec3 ray;
            ray[u] = grid.coordinate(u, i);
            ray[v] = grid.coordinate(v, j);
            if (ray[u] < index.bounds.lo[u] || ray[u] > index.bounds.hi[u]
                || ray[v] < index.bounds.lo[v] || ray[v] > index.bounds.hi[v]) return;
            const int start = i * grid.stride[u] + j * grid.stride[v];
            // Per l'ultimo asse si possono saltare le righe già
            // classificate come esterne da entrambe le altre direzioni.
            if (a == 1) {
                bool needed = false;
                for (int t = 0, id = start; t < grid.size[a]; ++t, id += grid.stride[a])
                    if (grid.coordinate(a, t) >= index.bounds.lo[a]
                        && grid.coordinate(a, t) <= index.bounds.hi[a]
                        && (votes[id] != 8)) {
                        needed = true;
                        break;
                    }
                if (!needed) return;
            }
            std::vector<double> hits;
            if (!solidGridCrossings(body, index, classifier, a, ray, tolerance, hits)) return;
            auto next = hits.begin();
            auto &crossings = rows[row];
            for (int t = 0, id = start; t < grid.size[a]; ++t, id += grid.stride[a]) {
                const double coord = grid.coordinate(a, t);
                while (next != hits.end() && *next < coord) ++next;
                const double after = next == hits.end() ? truncation : *next - coord;
                const double before = next == hits.begin() ? truncation : coord - *std::prev(next);
                depth[id] = std::min({depth[id], after, before});
                const bool startsInside = (next - hits.begin()) % 2 != 0;
                votes[id] += startsInside ? 1 : 4;
                if (t + 1 == grid.size[a] || next == hits.end()) continue;
                const double limit = grid.coordinate(a, t + 1);
                if (*next < limit && (std::next(next) == hits.end() || *std::next(next) >= limit))
                    crossings.push_back({id, startsInside, *next});
            }
        });
        auto &crossings = axialCrossings[a];
        for (const auto &row : rows) {
            cancellationPoint();
            crossings.insert(crossings.end(), row.begin(), row.end());
        }
        std::sort(crossings.begin(), crossings.end(), [](const AxialCrossing &x, const AxialCrossing &y) {
            return x.node < y.node;
        });
    }

    parallelFor(std::size_t(grid.size[2]), 2u, [&](std::size_t slice) {
        const int z = int(slice);
        for (int y = 0; y < grid.size[1]; ++y)
            for (int x = 0; x < grid.size[0]; ++x) {
                const int id = x + y * grid.stride[1] + z * grid.stride[2];
                const Vec3 point{grid.coordinate(0, x), grid.coordinate(1, y), grid.coordinate(2, z)};
                const double bbox = boxInterior(point, index.bounds);
                if (bbox < 0.0) {
                    grid.field[id] = -std::min(truncation, -bbox);
                    continue;
                }
                const int inside = votes[id] % 4, outside = votes[id] / 4;
                double envelope = depth[id];
                if (outside > inside) {
                    envelope = -envelope;
                } else if (inside == outside) {
                    const auto location = classifier.classify(point);
                    if (location == PointLocation::Outside) envelope = -envelope;
                    if (location == PointLocation::Boundary) envelope = 0.0;
                }
                grid.field[id] = envelope;
            }
    });
}

double envelopeClearance(const Grid &grid, const Vec3 &point, int axis, double from, double to,
                         bool joinStart, bool joinEnd) {
    double separation = std::numeric_limits<double>::infinity();
    if (joinStart) separation = std::fabs(point[axis] - from);
    if (joinEnd) separation = std::min(separation, std::fabs(point[axis] - to));
    // Il contorno campionato si innesta all'interno della sezione esatta.
    // Il margine si annulla in tre celle dal taglio.
    return std::max(0.0, 1.5 * grid.step - .5 * separation);
}

std::vector<double> gyroidWallValues(const Grid &grid, const SheetGyroid &pattern) {
    std::array<std::vector<TpmsPhase>, 3> phases;
    const double period = pattern.cellPitch;
    for (int axis = 0; axis < 3; ++axis) {
        auto &values = phases[axis];
        values.reserve(grid.size[axis]);
        for (int i = 0; i < grid.size[axis]; ++i) {
            if (i % 8192 == 0) cancellationPoint();
            values.push_back(tpmsPhase(grid.coordinate(axis, i), pattern.origin[axis], period));
        }
    }
    const double bandScale = (pattern.wallThickness / period) * kPi;
    const double distanceScale = period / (kTwoPi * 1.7);
    std::vector<double> result(grid.field.size());
    parallelFor(std::size_t(grid.size[2]), 2u, [&](std::size_t z) {
        int id = int(z) * grid.stride[2];
        for (const auto &y : phases[1])
            for (const auto &x : phases[0])
                result[id++] = tpmsFieldFromPhases(x, y, phases[2][z], bandScale, distanceScale);
    });
    return result;
}

void applyBoxGyroid(Grid &grid, const SheetGyroid &pattern) {
    const auto values = gyroidWallValues(grid, pattern);
    parallelFor(std::size_t(grid.size[2]), 2u, [&](std::size_t slice) {
        const int begin = int(slice) * grid.stride[2];
        const int end = begin + grid.stride[2];
        for (int id = begin; id < end; ++id) {
            const double wall = std::max(values[id], -2.0 * grid.step);
            grid.field[id] = std::min(grid.field[id], wall) - grid.step * 1e-4;
        }
    });
}

// Si mantengono i vuoti interni. Si scartano solo le isole di materiale
// rispetto alla componente principale, con un limite anche per il fusto.
void keepConnectedMaterial(Grid &grid, int axis, double from, double to) {
    std::vector<unsigned char> visited(grid.field.size());
    std::vector<int> pending, largest;
    std::size_t total = 0, outsideLargest = 0;
    for (std::size_t i = 0; i < grid.field.size(); ++i) {
        if (i % 16384 == 0) cancellationPoint();
        if (!(grid.field[i] > 0) || visited[i]) continue;
        pending.assign(1, int(i));
        visited[i] = 1;
        std::size_t retained = 0;
        for (std::size_t j = 0; j < pending.size(); ++j) {
            if (j % 16384 == 0) cancellationPoint();
            const int id = pending[j];
            if (axis >= 0) {
                const double value = grid.coordinate(axis, id / grid.stride[axis] % grid.size[axis]);
                if (value < from || value > to) ++retained;
            }
            // Il bordo esterno della griglia è vuoto.
            for (int stride : grid.stride)
                for (int other : {id - stride, id + stride})
                    if (grid.field[other] > 0 && !visited[other]) {
                        pending.push_back(other);
                        visited[other] = 1;
                    }
        }
        total += pending.size();
        if (pending.size() > largest.size()) {
            largest.swap(pending);
            outsideLargest = retained;
        }
    }
    if (largest.empty() || largest.size() < 0.98 * total)
        throw std::domain_error("TPMS: la parete non forma un volume connesso con la parte piena.");
    if (largest.size() == total) return;
    for (const int id : largest) visited[id] = 2;
    std::size_t retained = 0;
    for (std::size_t i = 0; i < grid.field.size(); ++i) {
        if (i % 16384 == 0) cancellationPoint();
        if (!visited[i]) continue;
        if (axis >= 0) {
            const double value = grid.coordinate(axis, int(i) / grid.stride[axis] % grid.size[axis]);
            if (value < from || value > to) ++retained;
        }
        if (visited[i] == 1) grid.field[i] = -grid.field[i];
    }
    if (retained > outsideLargest + std::max(std::size_t(4), outsideLargest / 200))
        throw std::domain_error("TPMS: la struttura separa parte del volume fuori intervallo.");
}

struct BoxBoundary {
    Box bounds;
    Index low{}, high{};
    std::array<SurfacePtr, 6> carriers;

    bool inside(int id, const Grid &grid) const {
        for (int axis = 0; axis < 3; ++axis) {
            const int i = id / grid.stride[axis] % grid.size[axis];
            if (i < low[axis] || i > high[axis]) return false;
        }
        return true;
    }
};

// Precondizione geometrica e topologica del ritaglio cartesiano esatto.
std::optional<BoxBoundary> rectangularEnvelope(const Body &source) {
    const auto faces = source.faces();
    const auto vertices = source.vertices();
    if (faces.size() != 6 || vertices.size() != 8 || source.edges().size() != 12
        || source.shells().size() != 1) return {};
    BoxBoundary box;
    for (const VertexId id : vertices) box.bounds.add(source.vertex(id).point);
    unsigned corners = 0;
    for (const VertexId id : vertices) {
        const Vec3 point = source.vertex(id).point;
        int corner = 0;
        for (int axis = 0; axis < 3; ++axis) {
            if (point[axis] == box.bounds.hi[axis]) corner |= 1 << axis;
            else if (point[axis] != box.bounds.lo[axis]) return {};
        }
        corners |= 1u << corner;
    }
    if (corners != 255u) return {};
    for (const FaceId id : faces) {
        const auto &face = source.face(id);
        if (!face.surface || face.surface->type() != SurfaceType::Plane || face.loops.size() != 1) return {};
        const auto fins = source.loopFins(face.loops.front());
        if (fins.size() != 4) return {};
        const auto &frame = static_cast<const Plane &>(*face.surface).frame();
        int side = -1;
        for (int axis = 0; axis < 3; ++axis) {
            const auto &normal = frame.zDir();
            if (std::abs(normal[axis]) != 1.0 || normal[(axis + 1) % 3] != 0.0 || normal[(axis + 2) % 3] != 0.0)
                continue;
            const double coordinate = frame.origin()[axis];
            if (coordinate == box.bounds.lo[axis]) side = 2 * axis;
            if (coordinate == box.bounds.hi[axis]) side = 2 * axis + 1;
        }
        if (side < 0 || box.carriers[side]) return {};
        for (const FinId fin : fins) {
            const auto &edge = source.edge(source.fin(fin).edge);
            const Vec3 point = source.vertex(source.finStart(fin)).point;
            if (!edge.curve || edge.curve->type() != CurveType::Line
                || point[side / 2] != frame.origin()[side / 2]) return {};
        }
        box.carriers[side] = face.surface;
    }
    return box;
}

struct Boundary {
    std::vector<Vec3> points;
    std::vector<Triangle> triangles;
    std::vector<unsigned char> wallPoints;
};

struct Crossing {
    double fraction;
    bool onWall;
};

struct ContourFields {
    const std::vector<double> &envelope;
    const std::vector<double> &wall;
    const std::vector<double> &uncutEnvelope;
    const AxialCrossings &axialCrossings;

    Crossing fromMaterial(const Grid &grid, int inside, int outside) const {
        const double innerWall = wall[inside], outerWall = wall[outside];
        const double wallCut = outerWall <= 0.0 ? innerWall / (innerWall - outerWall) : 1.0;
        const double innerEnvelope = envelope[inside], outerEnvelope = envelope[outside];
        if (outerEnvelope > 0.0) return {wallCut, true};

        // Il primo attraversamento uscendo dal materiale limita il volume.
        // Si usa l'intersezione sul B-rep se la cella è lontana dalla fascia
        // di giunzione dove l'inviluppo è stato ristretto intenzionalmente.
        double envelopeCut = innerEnvelope / (innerEnvelope - outerEnvelope);
        const double bias = grid.step * 1e-4;
        if (innerEnvelope == uncutEnvelope[inside] - bias &&
            outerEnvelope == uncutEnvelope[outside] - bias) {
            const int increment = std::abs(inside - outside);
            const int axis = increment == 1 ? 0 : increment == grid.stride[1] ? 1 : 2;
            const int node = std::min(inside, outside);
            const auto &crossings = axialCrossings[axis];
            const auto hit = std::lower_bound(crossings.begin(), crossings.end(), node,
                [](const AxialCrossing &crossing, int id) { return crossing.node < id; });
            if (hit != crossings.end() && hit->node == node && hit->startsInside == (inside == node)) {
                const double start = grid.coordinate(axis, inside / increment % grid.size[axis]);
                const double end = grid.coordinate(axis, outside / increment % grid.size[axis]);
                const double fraction = (hit->coordinate - start) / (end - start);
                if (fraction > 0.0 && fraction < 1.0) envelopeCut = fraction;
            }
        }
        return {std::min(wallCut, envelopeCut), wallCut < envelopeCut};
    }
};

using WallLevel = std::function<double(const Vec3 &)>;

// La topologia viene dal campo campionato. Il livello serve a trovare
// la radice sullo stesso lato della griglia, senza cambiarne l'incidenza.
double refineRoot(const WallLevel &level, const Vec3 &p, const Vec3 &q, bool startInside) {
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 30; ++i) {
        const double middle = (lo + hi) * .5;
        if ((level(p + middle * (q - p)) > 0.0) == startInside) lo = middle;
        else hi = middle;
    }
    return (lo + hi) * .5;
}

Vec3 refineFanCenter(const WallLevel &level, const Vec3 &initial, double pitch) {
    Vec3 point = initial;
    const double h = pitch * 1e-5;
    for (int i = 0; i < 5; ++i) {
        const double residual = level(point);
        Vec3 gradient;
        for (int axis = 0; axis < 3; ++axis) {
            Vec3 delta;
            delta[axis] = h;
            gradient[axis] = (level(point + delta) - level(point - delta)) / (2 * h);
        }
        const double norm2 = squaredNorm(gradient);
        if (norm2 < 1e-8) break;
        const Vec3 suggested = point - residual / norm2 * gradient;
        if (distance(initial, suggested) > .18 * pitch || std::abs(level(suggested)) >= std::abs(residual)) break;
        point = suggested;
    }
    return point;
}

Boundary contour(const Grid &grid, const Index &first, const Index &last, const BoxBoundary *box = nullptr,
                 const ContourFields *fields = nullptr, bool tagWall = false, const WallLevel *level = nullptr) {
    Boundary result;
    std::unordered_map<std::uint64_t, int> vertices;
    auto gpuFirst = first;
    gpuFirst[2] += std::max(1, (last[2] - first[2]) / 4);
    const auto gpuCount = std::size_t(last[0] - first[0]) * (last[1] - first[1]) * (last[2] - gpuFirst[2]);
    std::future<std::vector<TpmsCell>> pending;
    if (const auto gpu = getTpmsBatchEvaluator(); gpu && gpu->canBatch(gpuCount)) {
        cancellationPoint();
        try {
            // I valori sono immutabili. La CPU emette il primo gruppo di
            // celle mentre CUDA precalcola i collegamenti del gruppo finale.
            pending = std::async(std::launch::async, [&, gpu, token = currentCancellation] {
                const CancellationScope cancellation(token);
                cancellationPoint();
                std::vector<TpmsCell> batch;
                if (!gpu->cells(grid.field, grid.size, gpuFirst, last, batch) || batch.size() != gpuCount)
                    batch.clear();
                return batch;
            });
        } catch (const std::system_error &) {
            // Se non si può avviare il worker, si elabora la griglia sulla CPU.
        }
    }
    std::vector<TpmsCell> batch;
    std::size_t cell = 0;
    for (int z = first[2]; z < last[2]; ++z) {
        cancellationPoint();
        if (pending.valid() && z == gpuFirst[2]) {
            batch = pending.get();
            cell = 0;
            cancellationPoint();
        }
        for (int y = first[1]; y < last[1]; ++y)
            for (int x = first[0]; x < last[0]; ++x, ++cell) {
                const int base = x + y * grid.stride[1] + z * grid.stride[2];
                TpmsCell connections = batch.empty()
                    ? tpmsCellConnections(grid.field.data(), base, grid.stride[1], grid.stride[2])
                    : batch[cell];
                if (connections == 0) continue;
                int node[8];
                double f[8];
                for (int i = 0; i < 8; ++i) {
                    node[i] = base + (i & 1) + ((i >> 1) & 1) * grid.stride[1] + (i >> 2) * grid.stride[2];
                    f[i] = grid.field[node[i]];
                }
                const auto vertex = [&](int edge) {
                    const int a = cubeEdges[edge][0], b = cubeEdges[edge][1];
                    int start = node[a], end = node[b];
                    if (box && box->inside(start, grid) != box->inside(end, grid)) {
                        // La cella esterna si chiude sul nodo del piano
                        // esatto. Più lati esterni condividono lo stesso nodo.
                        if (f[a] > 0.0) end = start;
                        else start = end;
                    }
                    const auto entry = vertices.try_emplace(edgeKey(start, end), int(result.points.size()));
                    if (entry.second) {
                        bool onWall = false;
                        if (start == end) {
                            result.points.push_back(grid.point(start));
                        } else {
                            Crossing crossing{f[a] / (f[a] - f[b]), true};
                            if (fields) {
                                const bool forward = f[a] > 0.0;
                                crossing = forward ? fields->fromMaterial(grid, start, end)
                                                   : fields->fromMaterial(grid, end, start);
                                if (!forward) crossing.fraction = 1.0 - crossing.fraction;
                            }
                            const Vec3 p = grid.point(start), q = grid.point(end);
                            if (level) crossing.fraction = refineRoot(*level, p, q, f[a] > 0.0);
                            const double t = std::clamp(crossing.fraction, .001, .999);
                            result.points.push_back(p + (q - p) * t);
                            onWall = crossing.onWall;
                        }
                        if (tagWall) result.wallPoints.push_back(onWall);
                    }
                    return entry.first->second;
                };
                for (int start = 0; start < 12; ++start) {
                    if (tpmsNextEdge(connections, start) < 0) continue;
                    int loop[12], length = 0;
                    int edge = start;
                    do {
                        if (edge < 0 || length == 12)
                            throw std::domain_error("TPMS: contorno locale aperto.");
                        const int id = vertex(edge);
                        if (!box || std::find(loop, loop + length, id) == loop + length) loop[length++] = id;
                        const int following = tpmsNextEdge(connections, edge);
                        connections &= ~(TpmsCell(15) << (4 * edge));
                        edge = following;
                    } while (edge != start);
                    if (length < 3 && box) continue; // cella esterna collassata sul bordo del box
                    if (length < 3) throw std::domain_error("TPMS: contorno locale degenere.");
                    if (length == 3) {
                        result.triangles.push_back({loop[0], loop[1], loop[2]});
                    } else if (length == 4) {
                        const auto d02 = squaredNorm(result.points[loop[0]] - result.points[loop[2]]);
                        const auto d13 = squaredNorm(result.points[loop[1]] - result.points[loop[3]]);
                        if (d02 <= d13) {
                            result.triangles.push_back({loop[0], loop[1], loop[2]});
                            result.triangles.push_back({loop[0], loop[2], loop[3]});
                        } else {
                            result.triangles.push_back({loop[0], loop[1], loop[3]});
                            result.triangles.push_back({loop[1], loop[2], loop[3]});
                        }
                    } else {
                        const int center = int(result.points.size());
                        Vec3 mean;
                        for (int i = 0; i < length; ++i) mean += result.points[loop[i]];
                        const bool onWall = tagWall && std::all_of(loop, loop + length,
                            [&](int vertex) { return result.wallPoints[vertex] != 0; });
                        const Vec3 initial = mean / length;
                        result.points.push_back(level && onWall ? refineFanCenter(*level, initial, grid.step) : initial);
                        if (tagWall) result.wallPoints.push_back(onWall);
                        for (int i = 0; i < length; ++i)
                            result.triangles.push_back({loop[i], loop[(i + 1) % length], center});
                    }
                    if (result.triangles.size() > maxTriangles)
                        throw std::domain_error("TPMS: troppe facce. Aumenta il passo di campionamento.");
                }
            }
    }
    return result;
}

Body boundaryBody(const Boundary &boundary, bool open, const BoxBoundary *box = nullptr) {
    const auto &points = boundary.points;
    if (boundary.triangles.empty()) throw std::domain_error("TPMS: la parete è vuota.");
    std::vector<Body::BuildFace> faces(boundary.triangles.size());
    constexpr std::size_t facesPerTask = 256;
    const auto jobs = (faces.size() + facesPerTask - 1) / facesPerTask;
    parallelFor(jobs, std::min(4u, threadCount(0)), [&](std::size_t job) {
        const auto first = job * facesPerTask;
        const auto last = std::min(faces.size(), first + facesPerTask);
        for (auto index = first; index < last; ++index) {
            const auto &triangle = boundary.triangles[index];
            const Vec3 &p = points[triangle[0]];
            const Vec3 u = points[triangle[1]] - p, v = points[triangle[2]] - p;
            const Vec3 normal = cross(u, v);
            if (!(norm(normal) > linearResolution() * std::max(norm(u), norm(v))))
                throw std::domain_error("TPMS: faccia degenere, cambia il passo di campionamento.");
            Frame3 frame(p, normalized(normal), u);
            SurfacePtr carrier;
            if (box)
                for (const auto &surface : box->carriers) {
                    const auto &candidate = static_cast<const Plane &>(*surface).frame();
                    if (candidate.toLocal(p).z() == 0.0 && dot(candidate.zDir(), u) == 0.0
                        && dot(candidate.zDir(), v) == 0.0) {
                        carrier = surface;
                        frame = candidate;
                        break;
                    }
                }
            Body::BuildFace face{carrier ? carrier : std::make_shared<Plane>(frame),
                                 dot(normal, frame.zDir()) > 0.0, {{}}};
            auto &loop = face.loops.front();
            loop.reserve(3);
            for (int i = 0; i < 3; ++i) {
                const int a = triangle[i], b = triangle[(i + 1) % 3];
                const int start = std::min(a, b), end = std::max(a, b);
                const Vec3 pos = points[start], delta = points[end] - pos;
                const Vec3 local = frame.toLocal(pos), direction = frame.directionToLocal(delta);
                loop.push_back({-1, a == start,
                                std::make_shared<Line<2>>(Vec2(local.x(), local.y()), Vec2(direction.x(), direction.y()))});
            }
            faces[index] = std::move(face);
        }
    });

    // L'ordine di inserimento degli spigoli è parte della topologia: le
    // facce sono già pronte e ogni bordo condiviso prende un solo indice.
    std::vector<Body::BuildEdge> edges;
    std::unordered_map<std::uint64_t, int> edgeIds;
    edges.reserve(faces.size() * 3 / 2);
    edgeIds.reserve(edges.capacity());
    for (std::size_t index = 0; index < faces.size(); ++index) {
        if (index % 4096 == 0) cancellationPoint();
        const auto &triangle = boundary.triangles[index];
        auto &loop = faces[index].loops.front();
        for (int i = 0; i < 3; ++i) {
            const int a = triangle[i], b = triangle[(i + 1) % 3];
            const auto entry = edgeIds.try_emplace(edgeKey(a, b), int(edges.size()));
            if (entry.second) {
                const int start = std::min(a, b), end = std::max(a, b);
                const Vec3 &pos = points[start];
                const Vec3 delta = points[end] - pos;
                edges.push_back({start, end, std::make_shared<Line<3>>(pos, delta), {0.0, norm(delta)}});
            }
            loop[i].edge = entry.first->second;
        }
    }
    return open ? Body::buildSheet(points, edges, faces) : Body::build(points, edges, faces);
}

void validateGyroidBody(const Body &body) {
    checkSampledVertexFans(body);
    const auto shells = body.shells();
    const auto solidShells = std::count_if(shells.begin(), shells.end(), [&](ShellId shell) {
        return detail::shellSignedVolume(body, shell) > 0.0;
    });
    if (solidShells != 1)
        throw std::domain_error("TPMS: il contorno deve formare un unico solido.");
}

// La cucitura riusa i supporti della lamina. La loro identità trasporta
// la provenienza del contorno campionato anche quando cambiano gli ID.
using WallCarriers = std::unordered_set<const Surface *>;

WallCarriers wallCarriers(const Body &body, const Boundary &boundary,
                          const SheetGyroid &pattern, double pitch, const BoxBoundary *box = nullptr) {
    std::vector<unsigned char> nearWall(boundary.points.size());
    for (std::size_t i = 0; i < nearWall.size(); ++i) {
        if (i % 4096 == 0) cancellationPoint();
        // Esclude anche i punti creati dal taglio assiale o dalla
        // rimozione di un frammento invece che dal livello del giroide.
        nearWall[i] = boundary.wallPoints[i]
            && std::abs(sheetGyroidField(boundary.points[i], pattern)) <= .23 * pitch;
    }
    WallCarriers carriers;
    for (const FaceId face : body.faces()) {
        const auto &triangle = boundary.triangles[face.index];
        if (!std::all_of(triangle.begin(), triangle.end(), [&](int i) { return nearWall[i] != 0; })) continue;
        const auto &surface = body.face(face).surface;
        if (box && std::find(box->carriers.begin(), box->carriers.end(), surface) != box->carriers.end()) continue;
        carriers.insert(surface.get());
    }
    return carriers;
}

void interpolateWall(Body &body, const WallCarriers &carriers, const SheetGyroid &pattern, double pitch) {
    std::vector<FaceId> candidates;
    for (const FaceId f : body.faces())
        if (carriers.count(body.face(f).surface.get())) candidates.push_back(f);
    if (fitGyroidFaces(body, candidates, pattern, pitch) == 0)
        throw std::domain_error("TPMS: nessuna porzione della parete idonea all’interpolazione spline.");
    const auto issues = checkBody(body);
    if (!issues.empty())
        throw std::domain_error("TPMS: la parete spline non supera il controllo B-rep. " + issues.front().message);
}

Body rectangularGyroid(BoxBoundary box, double pitch, const SheetGyroid &pattern, GyroidWall wall) {
    Grid grid(box.bounds, pitch);
    for (int axis = 0; axis < 3; ++axis) {
        box.low[axis] = grid.alignPlane(axis, box.bounds.lo[axis]);
        box.high[axis] = grid.alignPlane(axis, box.bounds.hi[axis]);
    }
    for (std::size_t i = 0; i < grid.field.size(); ++i) {
        if (i % 16384 == 0) cancellationPoint();
        if (box.inside(int(i), grid)) grid.field[i] = 2 * pitch;
    }
    applyBoxGyroid(grid, pattern);
    keepConnectedMaterial(grid, -1, 0, 0);
    const Index first{}, last{grid.size[0] - 1, grid.size[1] - 1, grid.size[2] - 1};
    const bool spline = wall == GyroidWall::Spline;
    const Boundary boundary = contour(grid, first, last, &box, nullptr, spline);
    Body result = boundaryBody(boundary, false, &box);
    validateGyroidBody(result);
    if (spline) interpolateWall(result, wallCarriers(result, boundary, pattern, pitch, &box), pattern, pitch);
    return result;
}

Body splineChannel(const Box &bounds, double pitch, const SheetGyroid &pattern, double sign) {
    const Vec3 size = bounds.hi - bounds.lo;
    const Body container = makeBox(Frame3(bounds.lo, {0, 0, 1}, {1, 0, 0}), size.x(), size.y(), size.z());
    auto box = rectangularEnvelope(container);
    if (!box) throw std::domain_error("TPMS: inviluppo dei taglienti non risolvibile.");
    Grid grid(bounds, pitch);
    for (int axis = 0; axis < 3; ++axis) {
        box->low[axis] = grid.alignPlane(axis, bounds.lo[axis]);
        box->high[axis] = grid.alignPlane(axis, bounds.hi[axis]);
    }
    const WallLevel field = [&](const Vec3 &p) {
        const auto x = tpmsPhase(p.x(), pattern.origin.x(), pattern.cellPitch);
        const auto y = tpmsPhase(p.y(), pattern.origin.y(), pattern.cellPitch);
        const auto z = tpmsPhase(p.z(), pattern.origin.z(), pattern.cellPitch);
        const double value = x.sine * y.cosine + y.sine * z.cosine + z.sine * x.cosine;
        const Vec3 grad{x.cosine * y.cosine - z.sine * x.sine,
                        y.cosine * z.cosine - x.sine * y.sine,
                        z.cosine * x.cosine - y.sine * z.sine};
        return (sign * value - pattern.wallThickness * kPi * norm(grad) / pattern.cellPitch)
            * pattern.cellPitch / (kTwoPi * 1.7) - pitch * 1e-4;
    };
    for (std::size_t i = 0; i < grid.field.size(); ++i) {
        if (i % 16384 == 0) cancellationPoint();
        if (box->inside(int(i), grid)) grid.field[i] = field(grid.point(int(i)));
    }
    const Index first{}, last{grid.size[0] - 1, grid.size[1] - 1, grid.size[2] - 1};
    const Boundary boundary = contour(grid, first, last, &*box, nullptr, true, &field);
    if (boundary.triangles.empty()) return {};
    Body tool = boundaryBody(boundary, false, &*box);
    std::vector<FaceId> candidates;
    for (const FaceId face : tool.faces()) {
        const auto &carrier = tool.face(face).surface;
        if (std::find(box->carriers.begin(), box->carriers.end(), carrier) == box->carriers.end())
            candidates.push_back(face);
    }
    if (fitGyroidFaces(tool, candidates, pattern, pitch) != candidates.size())
        throw std::domain_error("TPMS: interpolazione incompleta dei taglienti spline.");
    const auto issues = checkBody(tool);
    if (!issues.empty()) throw std::domain_error("TPMS: tagliente spline non valido. " + issues.front().message);
    return unifySameDomain(tool);
}

Body connectedGyroidMaterial(Body body, int axis, double from, double to) {
    const auto shells = body.shells();
    if (shells.size() == 1) return body;
    const double tolerance = booleanConnectionTolerance();
    ShellId principal;
    double largest = 0.0;
    std::vector<std::pair<ShellId, double>> volumes;
    for (const ShellId id : shells) {
        cancellationPoint();
        const double volume = detail::shellSignedVolume(body, id);
        volumes.emplace_back(id, volume);
        if (volume > largest) {
            principal = id;
            largest = volume;
        }
    }
    if (!principal.valid()) throw std::domain_error("TPMS: il rifilo non produce materiale connesso.");
    std::vector<FaceId> faces = body.shell(principal).faces;
    const SolidClassifier classifier(body, principal, tolerance, true);
    for (const auto &[id, volume] : volumes) {
        if (id == principal) continue;
        cancellationPoint();
        const auto &component = body.shell(id).faces;
        if (volume > 0.0 && axis >= 0)
            for (const FaceId face : component)
                for (const LoopId loop : body.face(face).loops)
                    for (const FinId fin : body.loopFins(loop)) {
                        const double coordinate = body.vertex(body.finStart(fin)).point[axis];
                        if (coordinate < from - tolerance || coordinate > to + tolerance)
                            throw std::domain_error("TPMS: le zone piene non appartengono allo stesso solido.");
                    }
        const PointLocation location = classifier.classify(shellBoundaryPoint(body, id));
        if (location == PointLocation::Inside && volume < 0.0)
            faces.insert(faces.end(), component.begin(), component.end());
        else if (location != PointLocation::Outside || !(volume > 0.0))
            throw std::domain_error("TPMS: le componenti del rifilo non sono separabili.");
    }
    return closedFacesAsSolid(body, faces);
}

struct GyroidSection {
    Body retained;
    Body plane;
    bool upper;
};

std::optional<GyroidSection> gyroidRetainedSection(const Body &source, const Box &bounds,
                                                   int axis, double limit, bool upper) {
    const double tolerance = booleanConnectionTolerance();
    if (upper ? limit >= bounds.hi[axis] - tolerance : limit <= bounds.lo[axis] + tolerance) return {};
    Vec3 origin = .5 * (bounds.lo + bounds.hi);
    origin[axis] = limit;
    Vec3 normal;
    normal[axis] = 1;
    const Vec3 tangent = axis == 0 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Body plane = makePlaneSheet(Frame3(origin, normal, tangent), std::max(bounds.diagonal(), 1.0));
    Body retained = clipSolidByFace(source, plane, upper, {tolerance, false, 4});
    if (retained.faces().empty()) return {};
    return GyroidSection{std::move(retained), std::move(plane), upper};
}

Body restoreGyroidSection(const Body &wall, const GyroidSection &section) {
    // La spline vicino al piano può sporgere di pochi micron.
    // Rifila sullo stesso piano e rimonta la parte originale.
    const double tolerance = booleanConnectionTolerance();
    const Body porous = clipSolidByFace(wall, section.plane, !section.upper, {tolerance, false, 4});
    if (porous.faces().empty()) throw std::domain_error("TPMS: nessun materiale nella zona porosa.");
    return uniteDisjointBodies(section.retained, porous, {tolerance, true, 4});
}

struct RetainedSide {
    Body body;
    std::vector<FaceId> caps;
    std::vector<FaceId> faces;
    double coordinate;
};

RetainedSide retainSide(const Body &source, const Box &bounds, int axis, double limit, bool upper) {
    RetainedSide result{{}, {}, {}, limit};
    if (upper ? limit >= bounds.hi[axis] : limit <= bounds.lo[axis]) return result;
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    const Box pad = bounds.padded(std::max(bounds.diagonal(), 1.0));
    std::vector<PathSegment> outer;
    for (int i = 0; i < 4; ++i) {
        Vec3 a, b;
        a[axis] = b[axis] = limit;
        a[u] = (i == 1 || i == 2) ? pad.hi[u] : pad.lo[u];
        a[v] = i >= 2 ? pad.hi[v] : pad.lo[v];
        const int next = (i + 1) % 4;
        b[u] = (next == 1 || next == 2) ? pad.hi[u] : pad.lo[u];
        b[v] = next >= 2 ? pad.hi[v] : pad.lo[v];
        outer.push_back({std::make_shared<Line<3>>(a, b - a), {0.0, distance(a, b)}});
    }
    const double tolerance = booleanConnectionTolerance();
    const Body cutter = planarSheet({outer}, tolerance);
    const auto &tool = cutter.face(cutter.faces().front());
    const auto n = static_cast<const Plane &>(*tool.surface).frame().zDir() * (tool.sense ? 1.0 : -1.0);
    BodyHistory history;
    result.body = clipSolidByFace(source, cutter, upper == (n[axis] > 0.0),
                                  {tolerance, false, 2}, &history);
    for (const FaceId id : result.body.faces()) {
        const auto &origins = history.origins(EntityKind::Face, id.index);
        const bool isCap = std::any_of(origins.begin(), origins.end(), [](const HistoryLink &origin) {
            return origin.operand == 1;
        });
        (isCap ? result.caps : result.faces).push_back(id);
    }
    return result;
}

struct ContactLoop {
    Vec3 seed;
    std::vector<PathSegment> segments;
};

std::array<std::vector<ContactLoop>, 2> contactLoops(const Body &wall, int axis,
                                                    const std::array<RetainedSide, 2> &sides) {
    const auto vertices = wall.vertices();
    std::vector<FinId> next(vertices.back().index + 1);
    for (const EdgeId id : wall.edges()) {
        const auto &edge = wall.edge(id);
        if (edge.forward.valid() == edge.backward.valid()) continue;
        const FinId fin = edge.forward.valid() ? edge.forward : edge.backward;
        auto &start = next[wall.finStart(fin).index];
        if (start.valid()) throw std::domain_error("TPMS: contatto puntuale nel piano di giunzione.");
        start = fin;
    }
    std::array<std::vector<ContactLoop>, 2> result;
    for (const VertexId start : vertices) {
        if (!next[start.index].valid()) continue;
        cancellationPoint();
        ContactLoop loop{wall.vertex(start).point, {}};
        const int side = loop.seed[axis] == sides[0].coordinate ? 0 : 1;
        VertexId at = start;
        do {
            const FinId fin = next[at.index];
            if (!fin.valid() || wall.vertex(at).point[axis] != sides[side].coordinate)
                throw std::domain_error("TPMS: contorno di giunzione aperto.");
            next[at.index] = {};
            const auto &edge = wall.edge(wall.fin(fin).edge);
            loop.segments.push_back({edge.curve, edge.range});
            at = wall.finEnd(fin);
        } while (at != start);
        result[side].push_back(std::move(loop));
    }
    return result;
}

bool insideContact(const Vec3 &point, int axis, const std::vector<ContactLoop> &loops,
                   const std::vector<std::size_t> &assigned) {
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    bool inside = false;
    for (const std::size_t i : assigned)
        for (const auto &segment : loops[i].segments) {
            const Vec3 a = segment.curve->point(segment.range.lo);
            const Vec3 b = segment.curve->point(segment.range.hi);
            if ((a[v] > point[v]) == (b[v] > point[v])) continue;
            const double uAt = a[u] + (b[u] - a[u]) * ((point[v] - a[v]) / (b[v] - a[v]));
            if (uAt > point[u]) inside = !inside;
        }
    return inside;
}

Body joinRetainedSides(const Body &wall, int axis, const std::array<RetainedSide, 2> &sides) {
    const auto openings = contactLoops(wall, axis, sides);
    const double tolerance = booleanConnectionTolerance();
    std::array<Body, 2> skirts;
    std::vector<Body> sections;
    std::vector<const Body *> pieces{&wall};
    sections.reserve(sides[0].caps.size() + sides[1].caps.size());
    for (int i = 0; i < 2; ++i) {
        const auto &side = sides[i];
        if (side.body.faces().empty()) continue;
        if (side.faces.empty()) throw std::domain_error("TPMS: il taglio non lascia una parte piena valida.");
        skirts[i] = facesAsSheet(side.body, side.faces);
        pieces.push_back(&skirts[i]);
        std::vector<bool> attached(openings[i].size());
        for (const FaceId cap : side.caps) {
            cancellationPoint();
            std::vector<std::vector<PathSegment>> loops;
            for (const LoopId id : side.body.face(cap).loops) {
                auto &segments = loops.emplace_back();
                for (const FinId fin : side.body.loopFins(id)) {
                    const auto &edge = side.body.edge(side.body.fin(fin).edge);
                    segments.push_back({edge.curve, edge.range});
                }
            }
            FaceClassifier classifier(side.body, cap, tolerance);
            std::vector<std::size_t> assigned;
            for (std::size_t j = 0; j < openings[i].size(); ++j)
                if (!attached[j] && classifier.classify(openings[i][j].seed) == PointLocation::Inside) {
                    attached[j] = true;
                    assigned.push_back(j);
                    loops.push_back(openings[i][j].segments);
                }
            // La sezione del solido può contenere fori racchiusi da un loop
            // TPMS senza che i due contorni si intersechino.
            for (const LoopId id : side.body.face(cap).loops) {
                const auto fins = side.body.loopFins(id);
                const Vec3 seed = side.body.vertex(side.body.finStart(fins.front())).point;
                if (insideContact(seed, axis, openings[i], assigned))
                    throw std::domain_error("TPMS: la parete attraversa un vuoto della sezione esatta.");
            }
            // planarSheet verifica complanarità, intersezioni e annidamento
            // sulle curve. I loop aggiunti tolgono l'impronta occupata
            // dalla parete e lasciano la sezione libera del pezzo pieno.
            sections.push_back(planarSheet(loops, tolerance));
        }
        if (std::find(attached.begin(), attached.end(), false) != attached.end())
            throw std::domain_error("TPMS: la giunzione esce dalla sezione esatta. Riduci il passo o sposta il limite.");
    }
    for (const Body &section : sections) pieces.push_back(&section);
    auto result = sewSheets(pieces, tolerance, true);
    if (!result.solid || !result.closed)
        throw std::domain_error("TPMS: impossibile chiudere la giunzione con la parte piena.");
    return std::move(result.body);
}

}

double curvedGyroidSamplingLimit(const SheetGyroid &pattern) {
    if (!(pattern.wallThickness < pattern.cellPitch / 2.0)) return 0.0;
    return std::min(sheetGyroidSamplingLimit(pattern), pattern.wallThickness / 3.0);
}

void validateCurvedGyroid(const Box &bounds, double pitch, const SheetGyroid &pattern) {
    if (!(pitch > booleanConnectionTolerance() && pitch <= curvedGyroidSamplingLimit(pattern)))
        throw std::invalid_argument("TPMS: lo spessore deve essere minore di metà periodo, con almeno tre campioni nello spessore e otto nel periodo.");
    gridSize(bounds, pitch);
}

class CurvedGyroidEnvelope {
public:
    CurvedGyroidEnvelope(const Box &box, double pitch) : bounds(box), samples(box, pitch) {}
    Box bounds;
    Grid samples;
    AxialCrossings crossings;
};

std::shared_ptr<const CurvedGyroidEnvelope> sampleCurvedGyroidEnvelope(const Body &source, double pitch) {
    if (source.isSheet() || source.faces().empty() || !std::isfinite(pitch) || pitch <= booleanConnectionTolerance())
        throw std::invalid_argument("TPMS: serve un solido chiuso e un passo di campionamento valido.");
    const RayFaceIndex index(source);
    auto envelope = std::make_shared<CurvedGyroidEnvelope>(index.bounds, pitch);
    sampleEnvelope(envelope->samples, source, index, envelope->crossings);
    return envelope;
}

Body curvedGyroidSolid(const Body &source, const CurvedGyroidEnvelope &envelope, const SheetGyroid &pattern,
                       int axis, double from, double to, GyroidWall wall) {
    validateCurvedGyroid(envelope.bounds, envelope.samples.step, pattern);
    if (axis < -1 || axis > 2 || (axis >= 0 && !(std::isfinite(from) && std::isfinite(to) && from < to
        && from < envelope.bounds.hi[axis] && to > envelope.bounds.lo[axis])))
        throw std::invalid_argument("TPMS: l'intervallo deve attraversare il solido.");
    if (auto box = rectangularEnvelope(source)) {
        const Vec3 size = box->bounds.hi - box->bounds.lo;
        const bool coversBox = axis < 0 || (from <= box->bounds.lo[axis] && to >= box->bounds.hi[axis]);
        if (coversBox && std::min({size.x(), size.y(), size.z()}) >= envelope.samples.step)
            return rectangularGyroid(std::move(*box), envelope.samples.step, pattern, wall);
    }
    Grid grid = envelope.samples;
    Index first{}, last{grid.size[0] - 1, grid.size[1] - 1, grid.size[2] - 1};
    std::array<RetainedSide, 2> sides;
    bool joinStart = false, joinEnd = false;
    if (axis >= 0) {
        sides[0] = retainSide(source, envelope.bounds, axis, from, false);
        sides[1] = retainSide(source, envelope.bounds, axis, to, true);
        joinStart = !sides[0].body.faces().empty();
        joinEnd = !sides[1].body.faces().empty();
        if (joinStart) first[axis] = grid.alignPlane(axis, from);
        if (joinEnd) last[axis] = grid.alignPlane(axis, to);
        if (first[axis] >= last[axis])
            throw std::domain_error("TPMS: i due piani sono troppo vicini per il passo scelto.");
    }
    auto envelopeField = grid.field;
    auto wallField = gyroidWallValues(grid, pattern);
    parallelFor(std::size_t(grid.size[2]), 2u, [&](std::size_t slice) {
        const int begin = int(slice) * grid.stride[2];
        const int end = begin + grid.stride[2];
        for (int id = begin; id < end; ++id) {
            const Vec3 point = grid.point(id);
            const double clearance = envelopeClearance(grid, point, axis, from, to, joinStart, joinEnd);
            const double retained = axis < 0 ? -2.0 * grid.step
                : std::max(from - point[axis], point[axis] - to);
            // Il minimo determina i segni e le connessioni della griglia.
            // La posizione degli attraversamenti usa i livelli separati.
            envelopeField[id] -= clearance + grid.step * 1e-4;
            wallField[id] = std::max(wallField[id], retained) - grid.step * 2e-4;
            grid.field[id] = std::min(envelopeField[id], wallField[id]);
        }
    });
    keepConnectedMaterial(grid, axis, from, to);
    for (std::size_t i = 0; i < grid.field.size(); ++i) {
        if (i % 16384 == 0) cancellationPoint();
        if (grid.field[i] < 0.0 && envelopeField[i] > 0.0 && wallField[i] > 0.0)
            wallField[i] = grid.field[i];
    }

    const bool open = joinStart || joinEnd;
    const ContourFields fields{envelopeField, wallField, envelope.samples.field, envelope.crossings};
    const bool spline = wall == GyroidWall::Spline;
    Body result;
    WallCarriers carriers;
    {
        const Boundary boundary = contour(grid, first, last, nullptr, &fields, spline);
        result = boundaryBody(boundary, open);
        if (spline) carriers = wallCarriers(result, boundary, pattern, grid.step);
    }
    if (open) result = joinRetainedSides(result, axis, sides);
    validateGyroidBody(result);
    if (spline) interpolateWall(result, carriers, pattern, grid.step);
    return result;
}

Body curvedGyroidSolid(const Body &source, double pitch, const SheetGyroid &pattern,
                       int axis, double from, double to, GyroidWall wall) {
    if (!(pitch > booleanConnectionTolerance() && pitch <= curvedGyroidSamplingLimit(pattern)))
        throw std::invalid_argument("TPMS: lo spessore deve essere minore di metà periodo, con almeno tre campioni nello spessore e otto nel periodo.");
    return curvedGyroidSolid(source, *sampleCurvedGyroidEnvelope(source, pitch), pattern, axis, from, to, wall);
}

Body trimmedSplineGyroidSolid(const Body &source, double pitch, const SheetGyroid &pattern,
                             int axis, double from, double to) {
    if (source.isSheet() || source.faces().empty())
        throw std::invalid_argument("TPMS: serve un solido di partenza.");
    const Box bounds = RayFaceIndex(source).bounds;
    validateCurvedGyroid(bounds, pitch, pattern);
    if (axis < -1 || axis > 2 || (axis >= 0 && !(std::isfinite(from) && std::isfinite(to) && from < to
        && from < bounds.hi[axis] && to > bounds.lo[axis])))
        throw std::invalid_argument("TPMS: l'intervallo deve attraversare il solido.");

    const double tolerance = booleanConnectionTolerance();
    const auto lower = axis >= 0 ? gyroidRetainedSection(source, bounds, axis, from, false) : std::nullopt;
    const auto upper = axis >= 0 ? gyroidRetainedSection(source, bounds, axis, to, true) : std::nullopt;
    // Le facce di chiusura esterne dei taglienti stanno oltre il sorgente.
    Box toolBounds = bounds.padded(std::max(pitch, .165 * pattern.cellPitch));
    if (lower) toolBounds.lo[axis] = from;
    if (upper) toolBounds.hi[axis] = to;
    const Body positive = splineChannel(toolBounds, pitch, pattern, 1.0);
    const Body negative = splineChannel(toolBounds, pitch, pattern, -1.0);
    std::vector<const Body *> channels;
    for (const Body *channel : {&positive, &negative})
        if (!channel->faces().empty()) channels.push_back(channel);
    if (channels.empty()) throw std::domain_error("TPMS: nell’intervallo non ci sono canali da asportare.");
    const Body tools = Body::collect(channels);
    const BooleanOptions options{tolerance, false, int(std::min(6u, threadCount(0))), true};
    Body result = connectedGyroidMaterial(booleanOperation(source, tools, BooleanOperation::Subtract, options),
                                         axis, from, to);
    if (lower) result = restoreGyroidSection(result, *lower);
    if (upper) result = restoreGyroidSection(result, *upper);
    if (result.shells().size() != 1)
        result = connectedGyroidMaterial(std::move(result), axis, from, to);
    const auto issues = checkBody(result);
    if (!issues.empty()) throw std::domain_error("TPMS: il rifilo non supera il controllo B-rep. " + issues.front().message);
    if (result.isSheet() || result.faces().empty())
        throw std::domain_error("TPMS: il rifilo deve produrre un unico solido.");
    return result;
}

}
