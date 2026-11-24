#include "fk_quantize.h"
#include "fk_tpms_field.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_cancel.h"
#include <variant>

#include "fk_classify.h"
#include "fk_mass.h"
#include "fk_parallel.h"
#include "fk_primitives.h"
#include "fk_unify.h"

namespace ForgeCad::Kernel {
namespace {

constexpr std::size_t maxCells = 500000;
constexpr std::size_t maxFaces = 40000;
using Index = std::array<int, 3>;
using CellPattern = std::variant<std::monostate, CubicLattice, WovenLattice, SheetGyroid>;

bool validBox(const Box &box, double tolerance) {
    const Vec3 size = box.hi - box.lo;
    return isFinite(box.lo) && isFinite(box.hi) && isFinite(size)
        && size.x() > tolerance && size.y() > tolerance && size.z() > tolerance;
}

Box intersectBoxes(const Box &a, const Box &b) {
    Box result;
    for (int axis = 0; axis < 3; ++axis) {
        result.lo[axis] = std::max(a.lo[axis], b.lo[axis]);
        result.hi[axis] = std::min(a.hi[axis], b.hi[axis]);
    }
    return result;
}

bool onLatticeAxis(double coordinate, double origin, const CubicLattice &lattice) {
    return std::fabs(std::remainder(coordinate - origin, lattice.cellPitch)) <= 0.5 * lattice.strutWidth;
}

Index gridDimensions(const Box &domain, double pitch, double tolerance, std::size_t limit) {
    if (!std::isfinite(pitch) || pitch <= tolerance || !validBox(domain, tolerance))
        throw std::invalid_argument("Quantizza: zona o passo di campionamento non valido");
    Index result{};
    for (int axis = 0; axis < 3; ++axis) {
        const double cells = std::ceil((domain.hi[axis] - domain.lo[axis] - tolerance) / pitch);
        if (!std::isfinite(cells) || cells > double(limit))
            throw std::domain_error("Quantizza: troppe celle, aumentare il passo o ridurre la selezione");
        result[axis] = std::max(1, int(cells));
        limit /= std::size_t(result[axis]);
    }
    return result;
}

bool insideWeave(const Vec3 &point, const WovenLattice &weave) {
    const double period = weave.frame.cellPitch;
    const double halfWidth = 0.5 * weave.filamentWidth;
    const Vec3 local = (point - weave.frame.origin) / period;
    const bool warp = std::fabs(std::remainder(local.y(), 1.0)) * period <= halfWidth;
    const bool weft = std::fabs(std::remainder(local.x() - 0.5, 1.0)) * period <= halfWidth;
    if (!warp && !weft) return false;

    const double waveX = std::sin(kPi * std::remainder(local.x(), 2.0));
    const double waveY = std::cos(kPi * std::remainder(local.y(), 2.0));
    // Al nodo della trama le due famiglie hanno segno opposto. Gli incroci
    // a X semintera tengono i fili lontani dalle colonne del telaio.
    const double height = (local.z() - 0.5) * period;
    const double warpOffset = std::copysign(1.0, waveY) * weave.amplitude * waveX;
    const double weftOffset = -std::copysign(1.0, waveX) * weave.amplitude * waveY;
    return (warp && std::fabs(std::remainder(height - warpOffset, period)) <= halfWidth)
        || (weft && std::fabs(std::remainder(height - weftOffset, period)) <= halfWidth);
}


class CellGrid {
public:
    CellGrid(const Box &domain, double pitch, double tolerance)
        : dimensions(gridDimensions(domain, pitch, tolerance, maxCells)) {
        for (int axis = 0; axis < 3; ++axis) {
            auto &coordinates = coordinates_[axis];
            coordinates.reserve(std::size_t(dimensions[axis]) + 1);
            for (int i = 0; i < dimensions[axis]; ++i) {
                const double coordinate = domain.lo[axis] + i * pitch;
                if (i && coordinate - coordinates.back() <= tolerance)
                    throw std::domain_error("Quantizza: passo non risolvibile alle coordinate del solido");
                coordinates.push_back(coordinate);
            }
            if (domain.hi[axis] - coordinates.back() <= tolerance)
                throw std::domain_error("Quantizza: cella terminale non risolvibile");
            coordinates.push_back(domain.hi[axis]);
        }
        material_.resize(std::size_t(dimensions[0]) * dimensions[1] * dimensions[2]);
    }

    void sample(const SolidClassifier &classifier, const CellPattern &pattern,
                const Body &body, const RayFaceIndex &rayIndex, double tolerance) {
        const auto *weave = std::get_if<WovenLattice>(&pattern);
        const auto *lattice = weave ? &weave->frame : std::get_if<CubicLattice>(&pattern);
        const auto *gyroid = std::get_if<SheetGyroid>(&pattern);
        std::array<std::vector<int>, 3> onStrut;
        if (lattice)
            for (int axis = 0; axis < 3; ++axis)
                for (int i = 0; i < dimensions[axis]; ++i) {
                    onStrut[axis].push_back(onLatticeAxis(center(axis, i), lattice->origin[axis], *lattice));
                }
        const std::size_t rowCount = std::size_t(dimensions[1]) * dimensions[2];
        parallelFor(rowCount, material_.size() >= 2048 ? 4u : 1u, [&](std::size_t row) {
            const int y = int(row % dimensions[1]);
            const int z = int(row / dimensions[1]);
            const int crossSection = lattice ? onStrut[1][y] + onStrut[2][z] : 0;
            if (lattice && !weave && crossSection == 0) return;
            const double cy = center(1, y), cz = center(2, z);
            std::vector<double> crossings;
            // Una riga senza aste o fili non richiede intersezioni B-rep.
            bool rowPending = true;
            bool reliable = false;
            for (int x = 0; x < dimensions[0]; ++x) {
                const Vec3 point{center(0, x), cy, cz};
                if (lattice && crossSection + onStrut[0][x] < 2 && (!weave || !insideWeave(point, *weave)))
                    continue;
                if (gyroid && !sheetGyroidMaterial(point, *gyroid)) continue;
                if (rowPending) {
                    reliable = solidGridCrossings(body, rayIndex, classifier, 0, {0, cy, cz}, tolerance, crossings);
                    rowPending = false;
                }
                const std::size_t cell = row * dimensions[0] + x;
                if (reliable) {
                    const auto hit = std::lower_bound(crossings.begin(), crossings.end(), point.x() - tolerance);
                    if (hit == crossings.end() || *hit > point.x() + tolerance) {
                        material_[cell] = (hit - crossings.begin()) % 2 != 0;
                        continue;
                    }
                }
                material_[cell] = classifier.classify(point) != PointLocation::Outside;
            }
        });
    }

    bool occupied(const Index &cell) const {
        for (int axis = 0; axis < 3; ++axis)
            if (cell[axis] < 0 || cell[axis] >= dimensions[axis]) return false;
        return material_[cellIndex(cell)] != 0;
    }

    // Il clipping puo' lasciare contatti puntuali o lungo uno spigolo
    // fra regioni piene o vuote. Rimuove celle locali senza intervenire
    // sul B-rep conservato.
    void removeAmbiguousContacts() {
        for (int pass = 0; pass < 8; ++pass) {
            bool changed = false;
            for (int z = 0; z < dimensions[2]; ++z) {
                cancellationPoint();
                for (int y = 0; y < dimensions[1]; ++y)
                    for (int x = 0; x < dimensions[0]; ++x) {
                        const Index base{x, y, z};
                        for (int axis = 0; axis < 3; ++axis) {
                            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                            Index right = base, top = base;
                            ++right[u];
                            ++top[v];
                            Index diagonal = right;
                            ++diagonal[v];
                            const bool a = occupied(base), b = occupied(right);
                            const bool c = occupied(top), d = occupied(diagonal);
                            if (a != d || b != c || a == b) continue;
                            const Index &first = a ? base : right;
                            const Index &second = a ? diagonal : top;
                            const Index &remove = faceNeighbours(first) < faceNeighbours(second) ? first : second;
                            material_[cellIndex(remove)] = 0;
                            changed = true;
                        }
                    }
            }
            for (int z = 1; z < dimensions[2]; ++z) {
                cancellationPoint();
                for (int y = 1; y < dimensions[1]; ++y)
                    for (int x = 1; x < dimensions[0]; ++x)
                        changed = separateVertexContacts({x, y, z}) || changed;
            }
            if (!changed) return;
        }
    }

    // Un'asta ritagliata dal contorno puo' restare isolata. Conserva le
    // componenti collegate al B-rep sulla faccia di una zona; se l'intera
    // sorgente e' campionata, conserva la rete principale.
    void retainConnectedStruts(const SolidClassifier &classifier, const Box &bounds, double tolerance) {
        std::vector<int> component(material_.size(), 0);
        std::vector<Index> cells;
        std::vector<unsigned char> anchored(1, 0);
        int largest = 0;
        std::size_t largestSize = 0;
        bool hasAnchor = false;
        for (int z = 0; z < dimensions[2]; ++z) {
            cancellationPoint();
            for (int y = 0; y < dimensions[1]; ++y)
                for (int x = 0; x < dimensions[0]; ++x) {
                    const Index start{x, y, z};
                    const std::size_t startId = cellIndex(start);
                    if (!material_[startId] || component[startId]) continue;
                    const int id = int(anchored.size());
                    anchored.push_back(0);
                    cells.assign(1, start);
                    component[startId] = id;
                    for (std::size_t i = 0; i < cells.size(); ++i) {
                        if (i % 1024 == 0) cancellationPoint();
                        const Index cell = cells[i];
                        for (int axis = 0; axis < 3; ++axis)
                            for (int side : {-1, 1}) {
                                Index neighbour = cell;
                                neighbour[axis] += side;
                                if (neighbour[axis] < 0 || neighbour[axis] == dimensions[axis]) {
                                    if (!anchored[id] && meetsPreservedBody(cell, axis, side, classifier, bounds, tolerance))
                                        anchored[id] = true;
                                    continue;
                                }
                                const std::size_t neighbourId = cellIndex(neighbour);
                                if (material_[neighbourId] && !component[neighbourId]) {
                                    component[neighbourId] = id;
                                    cells.push_back(neighbour);
                                }
                            }
                    }
                    hasAnchor = hasAnchor || anchored[id];
                    if (cells.size() > largestSize) {
                        largest = id;
                        largestSize = cells.size();
                    }
                }
        }
        if (anchored.size() <= 2) return;
        for (std::size_t i = 0; i < material_.size(); ++i)
            if (material_[i] && !(hasAnchor ? anchored[component[i]] : component[i] == largest))
                material_[i] = 0;
    }

    Vec3 point(const Index &node) const {
        return {coordinates_[0][node[0]], coordinates_[1][node[1]], coordinates_[2][node[2]]};
    }

    std::size_t nodeIndex(const Index &node) const {
        return (std::size_t(node[2]) * (dimensions[1] + 1) + node[1]) * (dimensions[0] + 1) + node[0];
    }

    Index dimensions{};

private:
    std::size_t cellIndex(const Index &cell) const {
        return (std::size_t(cell[2]) * dimensions[1] + cell[1]) * dimensions[0] + cell[0];
    }

    bool meetsPreservedBody(const Index &cell, int axis, int side,
                            const SolidClassifier &classifier, const Box &bounds, double tolerance) const {
        const double plane = side < 0 ? coordinates_[axis].front() : coordinates_[axis].back();
        const double outside = plane + side * (8.0 * tolerance);
        if (outside <= bounds.lo[axis] || outside >= bounds.hi[axis]) return false;
        Vec3 point{center(0, cell[0]), center(1, cell[1]), center(2, cell[2])};
        point[axis] = outside;
        return classifier.classify(point) == PointLocation::Inside;
    }

    int faceNeighbours(const Index &cell) const {
        int count = 0;
        for (int axis = 0; axis < 3; ++axis)
            for (int side : {-1, 1}) {
                Index neighbour = cell;
                neighbour[axis] += side;
                count += occupied(neighbour);
            }
        return count;
    }

    bool separateOppositeVoids(const std::array<Index, 8> &incident, unsigned emptyMask) {
        // Con gli spigoli regolari e il materiale connesso, l'unico caso
        // con due regioni vuote al vertice ha due ottanti opposti vuoti.
        // Due celle lungo un cammino a facce comuni aprono il passaggio.
        for (int corner = 0; corner < 4; ++corner) {
            const int opposite = corner ^ 7;
            if (emptyMask != ((1u << corner) | (1u << opposite))) continue;
            int first = corner ^ 1, second = opposite ^ 2;
            int bestScore = faceNeighbours(incident[first]) + faceNeighbours(incident[second]);
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b) {
                    if (a == b) continue;
                    const int left = corner ^ (1 << a), right = opposite ^ (1 << b);
                    const int score = faceNeighbours(incident[left]) + faceNeighbours(incident[right]);
                    if (score >= bestScore) continue;
                    bestScore = score;
                    first = left;
                    second = right;
                }
            material_[cellIndex(incident[first])] = 0;
            material_[cellIndex(incident[second])] = 0;
            return true;
        }
        return false;
    }

    bool separateVertexContacts(const Index &node) {
        std::array<Index, 8> incident{};
        unsigned occupiedMask = 0;
        for (int i = 0; i < 8; ++i) {
            incident[i] = node;
            for (int axis = 0; axis < 3; ++axis)
                if (!(i & (1 << axis))) --incident[i][axis];
            if (occupied(incident[i])) occupiedMask |= 1u << i;
        }
        if (occupiedMask == 0 || (occupiedMask & (occupiedMask - 1u)) == 0 || occupiedMask == 255) return false;
        if (separateOppositeVoids(incident, occupiedMask ^ 255u)) return true;
        unsigned unvisited = occupiedMask;
        unsigned strongest = 0;
        int bestScore = -1;
        while (unvisited) {
            const unsigned first = unvisited & (0u - unvisited);
            unsigned component = first;
            unsigned frontier = first;
            int score = 0;
            do {
                const unsigned current = frontier;
                frontier = 0;
                for (int i = 0; i < 8; ++i) {
                    if (!(current & (1u << i))) continue;
                    score += faceNeighbours(incident[i]) + 1;
                    for (int axis = 0; axis < 3; ++axis)
                        frontier |= 1u << (i ^ (1 << axis));
                }
                frontier &= unvisited & ~component;
                component |= frontier;
            } while (frontier);
            unvisited &= ~component;
            if (score > bestScore) {
                strongest = component;
                bestScore = score;
            }
        }
        const unsigned unsupported = occupiedMask & ~strongest;
        for (int i = 0; i < 8; ++i)
            if (unsupported & (1u << i)) material_[cellIndex(incident[i])] = 0;
        return unsupported != 0;
    }

    double center(int axis, int i) const {
        const auto &coordinates = coordinates_[axis];
        return coordinates[i] + 0.5 * (coordinates[i + 1] - coordinates[i]);
    }

    std::array<std::vector<double>, 3> coordinates_;
    std::vector<unsigned char> material_;
};


Body buildCells(const CellGrid &grid) {
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    std::unordered_map<std::size_t, int> vertexIds;
    std::unordered_map<std::uint64_t, int> edgeIds;
    const auto vertex = [&](const Index &node) {
        const auto entry = vertexIds.try_emplace(grid.nodeIndex(node), int(vertices.size()));
        if (entry.second) vertices.push_back(grid.point(node));
        return entry.first->second;
    };
    const auto face = [&](const Index &cell, int axis, int side) {
        if (faces.size() == maxFaces)
            throw std::domain_error("Quantizza: troppe facce, aumentare il passo o ridurre la selezione");
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        Index origin = cell;
        if (side > 0) ++origin[axis];
        std::array<Index, 4> nodes{origin, origin, origin, origin};
        ++nodes[1][u];
        ++nodes[2][u];
        ++nodes[2][v];
        ++nodes[3][v];
        if (side < 0) std::swap(nodes[1], nodes[3]);
        std::array<int, 4> ids;
        for (int i = 0; i < 4; ++i) ids[i] = vertex(nodes[i]);
        Vec3 normal;
        normal[axis] = side;
        const Frame3 frame(vertices[ids[0]], normal, vertices[ids[1]] - vertices[ids[0]]);
        Body::BuildFace surface{std::make_shared<Plane>(frame), true, {{}}};
        auto &loop = surface.loops.front();
        loop.reserve(4);
        for (int i = 0; i < 4; ++i) {
            const int from = ids[i], to = ids[(i + 1) % 4];
            const int start = std::min(from, to), end = std::max(from, to);
            const auto key = (std::uint64_t(start) << 32) | std::uint64_t(end);
            const auto entry = edgeIds.try_emplace(key, int(edges.size()));
            const Vec3 a = vertices[start], delta = vertices[end] - a;
            if (entry.second)
                edges.push_back({start, end, std::make_shared<Line<3>>(a, delta), {0.0, norm(delta)}});
            const Vec3 localA = frame.toLocal(a), localDelta = frame.directionToLocal(delta);
            loop.push_back({entry.first->second, from == start,
                            std::make_shared<Line<2>>(Vec2(localA.x(), localA.y()), Vec2(localDelta.x(), localDelta.y()))});
        }
        faces.push_back(std::move(surface));
    };

    for (int z = 0; z < grid.dimensions[2]; ++z) {
        cancellationPoint();
        for (int y = 0; y < grid.dimensions[1]; ++y)
            for (int x = 0; x < grid.dimensions[0]; ++x) {
                const Index cell{x, y, z};
                if (!grid.occupied(cell)) continue;
                for (int axis = 0; axis < 3; ++axis)
                    for (const int side : {-1, 1}) {
                        Index next = cell;
                        next[axis] += side;
                        if (!grid.occupied(next)) face(cell, axis, side);
                    }
            }
    }
    if (faces.empty()) throw std::domain_error("Quantizza: nessuna cella piena nella selezione");
    Body result;
    try {
        result = Body::build(vertices, edges, faces);
    } catch (const std::invalid_argument &) {
        throw std::domain_error("Quantizza: contatto solo lungo uno spigolo, ridurre il passo");
    }
    checkSampledVertexFans(result);
    return result;
}

Body sampledSolid(const Body &source, double pitch, const CellPattern &pattern,
                  const std::vector<Box> &zones) {
    const double tolerance = booleanConnectionTolerance();
    if (!std::isfinite(pitch) || pitch <= tolerance)
        throw std::invalid_argument("Quantizza: passo non valido");
    if (source.isSheet() || source.faces().empty())
        throw std::invalid_argument("Quantizza: serve un solido chiuso");
    const RayFaceIndex rayIndex(source);
    const Box &bounds = rayIndex.bounds;
    if (!validBox(bounds, tolerance))
        throw std::invalid_argument("Quantizza: volume del corpo non risolvibile");

    std::vector<Box> domains;
    domains.reserve(zones.size());
    for (std::size_t i = 0; i < zones.size(); ++i) {
        const Box &zone = zones[i];
        if (!validBox(zone, tolerance))
            throw std::invalid_argument("Quantizza: selezione volumetrica non valida");
        for (std::size_t j = 0; j < i; ++j)
            if (validBox(intersectBoxes(zone, zones[j]), 0.0))
                throw std::invalid_argument("Quantizza: le zone selezionate si sovrappongono");
        const Box domain = intersectBoxes(bounds, zone);
        if (!validBox(domain, tolerance))
            throw std::invalid_argument("Quantizza: zona fuori dal volume di lavoro");
        domains.push_back(domain);
    }

    const SolidClassifier classifier(source, tolerance, true);
    const auto quantize = [&](const Box &domain) {
        CellGrid grid(domain, pitch, tolerance);
        grid.sample(classifier, pattern, source, rayIndex, tolerance);
        if (!std::holds_alternative<std::monostate>(pattern)) {
            grid.removeAmbiguousContacts();
            grid.retainConnectedStruts(classifier, bounds, tolerance);
        }
        return unifySameDomain(buildCells(grid), tolerance);
    };

    Body result;
    if (zones.empty()) {
        result = quantize(bounds);
    } else {
        result = source;
        for (const Box &zone : zones) {
            if (result.faces().empty()) break;
            const Vec3 size = zone.hi - zone.lo;
            const Body mask = makeBox(Frame3(zone.lo, {0, 0, 1}, {1, 0, 0}), size.x(), size.y(), size.z());
            result = booleanOperation(result, mask, BooleanOperation::Subtract);
        }
        for (const Box &domain : domains) {
            Body replacement = quantize(domain);
            result = result.faces().empty() ? std::move(replacement)
                : booleanOperation(result, replacement, BooleanOperation::Unite);
        }
    }
    const auto issues = checkBody(result, {true, true});
    if (!issues.empty()) throw std::domain_error("Quantizza: " + issues.front().message);
    checkSampledVertexFans(result);
    const auto shells = result.shells();
    const auto outerShells = std::count_if(shells.begin(), shells.end(),
        [&](ShellId shell) { return detail::shellSignedVolume(result, shell) > 0.0; });
    if (outerShells != 1)
        throw std::domain_error("Quantizza: il risultato deve restare un solo volume connesso");
    return result;
}

void checkLattice(double samplingPitch, const CubicLattice &lattice) {
    if (!isFinite(lattice.origin) || !std::isfinite(lattice.cellPitch) || !std::isfinite(lattice.strutWidth)
        || !(lattice.strutWidth >= samplingPitch)
        || !(lattice.cellPitch - lattice.strutWidth >= samplingPitch))
        throw std::invalid_argument("Lattice: passo troppo grande per risolvere aste e vuoti");
}

}

void validateVoxelization(const Box &domain, double pitch) {
    gridDimensions(domain, pitch, booleanConnectionTolerance(), maxCells);
}

void validateCubicLattice(const Box &domain, double samplingPitch, const CubicLattice &lattice) {
    checkLattice(samplingPitch, lattice);
    validateVoxelization(domain, samplingPitch);
}

Body quantizeSolid(const Body &source, double pitch, const std::vector<Box> &zones) {
    return sampledSolid(source, pitch, std::monostate{}, zones);
}

Body latticeSolid(const Body &source, double samplingPitch, const CubicLattice &lattice,
                  const std::vector<Box> &zones) {
    checkLattice(samplingPitch, lattice);
    return sampledSolid(source, samplingPitch, lattice, zones);
}

double wovenLatticeSamplingLimit(const WovenLattice &lattice) {
    const double pitch = lattice.frame.cellPitch;
    const double width = lattice.filamentWidth;
    const double amplitude = lattice.amplitude;
    const double strut = lattice.frame.strutWidth;
    if (!std::isfinite(pitch) || pitch <= 0 || !std::isfinite(width) || !std::isfinite(amplitude)
        || !std::isfinite(strut)) return 0.0;
    const double crossingGap = 2.0 * amplitude * std::cos(0.5 * kPi * width / pitch) - width;
    return std::min({strut, pitch - strut, width, crossingGap, pitch - 2.0 * amplitude - width,
                     0.5 * (pitch - strut - width)});
}

namespace {
void checkWeave(double samplingPitch, const WovenLattice &lattice) {
    checkLattice(samplingPitch, lattice.frame);
    if (!(wovenLatticeSamplingLimit(lattice) >= samplingPitch))
        throw std::invalid_argument("Lattice intrecciato: passo insufficiente a risolvere fili e varchi");
    if (lattice.frame.cellPitch - 2.0 * lattice.amplitude >= lattice.frame.strutWidth + lattice.filamentWidth)
        throw std::invalid_argument("Lattice intrecciato: i fili non raggiungono le aste del reticolo");
}

}

bool latticeMaterial(const Vec3 &point, const CubicLattice &lattice) {
    int axes = 0;
    for (int axis = 0; axis < 3; ++axis)
        axes += onLatticeAxis(point[axis], lattice.origin[axis], lattice);
    return axes >= 2;
}

bool wovenLatticeMaterial(const Vec3 &point, const WovenLattice &lattice) {
    return latticeMaterial(point, lattice.frame) || insideWeave(point, lattice);
}

void validateWovenLattice(const Box &domain, const WovenLatticeSettings &settings) {
    checkWeave(settings.samplingPitch, settings.pattern);
    validateVoxelization(domain, settings.samplingPitch);
}

WovenLatticeSettings suggestWovenLattice(const Box &domain) {
    const double tolerance = booleanConnectionTolerance();
    if (!validBox(domain, tolerance))
        throw std::invalid_argument("Lattice: volume della zona non risolvibile");
    const Vec3 side = domain.hi - domain.lo;
    constexpr std::size_t target = 80000;
    double sample = std::max({std::min({side.x(), side.y(), side.z()}) / 24.0,
                              std::cbrt(side.x() * side.y() * side.z() / double(target)),
                              16.0 * tolerance});
    if (std::isfinite(sample)) {
        const double increment = 0.5 * std::pow(10.0, std::floor(std::log10(sample)));
        if (increment > 0) sample = std::max(16.0 * tolerance, std::round(sample / increment) * increment);
    }
    for (;;) {
        try {
            gridDimensions(domain, sample, tolerance, target);
            break;
        } catch (const std::domain_error &) {
            sample *= 1.25;
        }
    }
    const double period = 8.0 * sample;
    WovenLatticeSettings settings{{{domain.lo + Vec3(sample, sample, sample) * 0.5,
                                      period, period * 0.325}, period * 0.25, period * 0.2875}, sample};
    validateWovenLattice(domain, settings);
    return settings;
}

Body wovenLatticeSolid(const Body &source, double samplingPitch, const WovenLattice &lattice,
                       const std::vector<Box> &zones) {
    checkWeave(samplingPitch, lattice);
    return sampledSolid(source, samplingPitch, lattice, zones);
}

bool solidGridCrossings(const Body &body, const RayFaceIndex &index, const SolidClassifier &classifier,
                        int axis, const Vec3 &origin, double tolerance, std::vector<double> &crossings) {
    // La parita' lungo l'intera retta vale per tutti i centri della
    // riga. Un contatto con un trim, una tangenza o un conteggio
    // ambiguo lascia la riga al classificatore puntuale.
    Vec3 direction;
    direction[axis] = 1;
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    for (const auto &[face, box] : index.faces) {
        if (origin[u] < box.lo[u] - tolerance || origin[u] > box.hi[u] + tolerance
            || origin[v] < box.lo[v] - tolerance || origin[v] > box.hi[v] + tolerance)
            continue;
        bool grazing = false;
        std::vector<double> hits;
        try {
            hits = intersectLineSurface(origin, direction, *body.face(face).surface, grazing, tolerance);
            if (grazing) return false;
            for (const double t : hits) {
                const Vec3 hit = origin + t * direction;
                if (hit[axis] < box.lo[axis] - tolerance || hit[axis] > box.hi[axis] + tolerance) continue;
                const auto location = classifier.classifyFace(face, hit);
                if (location == PointLocation::Boundary) return false;
                if (location == PointLocation::Inside) crossings.push_back(hit[axis]);
            }
        } catch (const std::domain_error &) {
            return false;
        }
    }
    if (crossings.size() % 2 != 0) return false;
    std::sort(crossings.begin(), crossings.end());
    for (std::size_t i = 1; i < crossings.size(); ++i)
        if (crossings[i] - crossings[i - 1] <= 2.0 * tolerance) return false;
    return true;
}

void checkSampledVertexFans(const Body &body) {
    const auto vertices = body.vertices();
    if (vertices.empty()) return;
    const std::size_t slots = std::size_t(vertices.back().index) + 1;
    std::vector<FinId> first(slots);
    std::vector<int> incident(slots);
    for (FinId fin : body.fins()) {
        const int vertex = body.finStart(fin).index;
        first[vertex] = fin;
        ++incident[vertex];
    }
    for (const VertexId vertex : vertices) {
        const FinId start = first[vertex.index];
        if (!start.valid()) continue;
        FinId fin = start;
        int count = 0;
        do {
            const FinId opposite = body.otherFin(fin);
            if (!opposite.valid()) break;
            fin = body.fin(opposite).next;
            ++count;
        } while (fin != start && count < incident[vertex.index]);
        if (fin != start || count != incident[vertex.index])
            throw std::domain_error("Quantizza: contatto solo in un vertice, ridurre il passo");
    }
}

double sheetGyroidSamplingLimit(const SheetGyroid &pattern) {
    if (!isFinite(pattern.origin) || !std::isfinite(pattern.cellPitch) || pattern.cellPitch <= 0
        || !std::isfinite(pattern.wallThickness) || pattern.wallThickness <= 0
        || !std::isfinite(pattern.wallThickness / pattern.cellPitch))
        return 0.0;
    return std::min(pattern.wallThickness / 2.0, pattern.cellPitch / 8.0);
}

namespace {
void checkSheetGyroid(double samplingPitch, const SheetGyroid &pattern) {
    if (!(samplingPitch > 0 && samplingPitch <= sheetGyroidSamplingLimit(pattern)))
        throw std::invalid_argument("Giroide: il campionamento deve risolvere almeno due celle nello spessore nominale e otto nel periodo");
}
}

void validateSheetGyroid(const Box &domain, double samplingPitch, const SheetGyroid &pattern) {
    checkSheetGyroid(samplingPitch, pattern);
    validateVoxelization(domain, samplingPitch);
}

double sheetGyroidField(const Vec3 &point, const SheetGyroid &pattern) {
    const double period = pattern.cellPitch;
    return tpmsFieldFromPhases(tpmsPhase(point.x(), pattern.origin.x(), period),
                               tpmsPhase(point.y(), pattern.origin.y(), period),
                               tpmsPhase(point.z(), pattern.origin.z(), period),
                               (pattern.wallThickness / period) * kPi, period / (kTwoPi * 1.7));
}

bool sheetGyroidMaterial(const Vec3 &point, const SheetGyroid &pattern) {
    return sheetGyroidField(point, pattern) >= 0.0;
}

Body sheetGyroidSolid(const Body &source, double samplingPitch, const SheetGyroid &pattern,
                     const std::vector<Box> &zones) {
    checkSheetGyroid(samplingPitch, pattern);
    return sampledSolid(source, samplingPitch, pattern, zones);
}

}
