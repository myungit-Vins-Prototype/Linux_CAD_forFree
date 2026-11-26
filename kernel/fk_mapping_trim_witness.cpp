#include "fk_mapping_trim_witness.h"
#include "fk_bernstein.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include "fk_classify_uv.h"
#include "fk_intersect.h"
#include "fk_predicates.h"
#include "fk_surface_bound_math.h"
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ForgeCad::Kernel::detail {
bool isStraightMappingEdge(const Curve<3> &curve) {
    const Curve<3> *basis = &curve;
    for (;;) {
        cancellationPoint();
        if (const auto *trim = dynamic_cast<const TrimmedCurve<3> *>(basis))
            basis = trim->basis().get();
        else if (const auto *placed = dynamic_cast<const TransformedCurve *>(basis))
            basis = placed->basis().get();
        else
            return basis->type() == CurveType::Line;
    }
}

namespace {
struct TrimWindow {
    Vec2 lo{INFINITY, INFINITY}, hi{-INFINITY, -INFINITY};
    bool finite() const { return isFinite(lo) && isFinite(hi) && lo.x() <= hi.x() && lo.y() <= hi.y(); }
    void add(const Vec2 &p) {
        for (int k = 0; k < 2; ++k) {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
        }
    }
    bool overlaps(const TrimWindow &b, double margin) const {
        return lo.x() <= b.hi.x() + margin && b.lo.x() <= hi.x() + margin && lo.y() <= b.hi.y() + margin &&
               b.lo.y() <= hi.y() + margin;
    }
};
// Round-to-nearest TwoSum. Reject rounded event coordinates rather than
// merging distinct boundaries or claiming a gap introduced by rounding.
static std::optional<double> exactTrimSum(double a, double b) {
    const double sum = a + b;
    const double part = sum - a;
    if (!std::isfinite(sum) || (a - (sum - part)) + (b - part) != 0.)
        return {};
    return sum;
}

template <int N>
static int trimAxis(const Vec<N> &direction) {
    int axis = -1;
    for (int i = 0; i < N; ++i) {
        if (direction[i] == 0.)
            continue;
        if (axis != -1 || std::abs(direction[i]) != 1.)
            return -1;
        axis = i;
    }
    return axis;
}

static bool cartesianTrimFrame(const Frame3 &frame) {
    const int x = trimAxis(frame.xDir()), y = trimAxis(frame.yDir()), z = trimAxis(frame.zDir());
    return isFinite(frame.origin()) && x >= 0 && y >= 0 && z >= 0 && x != y && x != z && y != z &&
           dot(cross(frame.xDir(), frame.yDir()), frame.zDir()) == 1.;
}

// For a normal product of this magnitude, the exact multiplication
// residual is representable even when both inputs have 53 significant bits.
static std::optional<double> exactTrimProduct(double a, double b) {
    if (!std::isfinite(a) || !std::isfinite(b))
        return {};
    if (a == 0. || b == 0.)
        return 0.;
    if (std::abs(a) == 1. || std::abs(b) == 1.)
        return a * b;
    const double product = a * b;
    if (!std::isfinite(product) || std::abs(product) < 0x1p-968 || std::fma(a, b, -product) != 0.)
        return {};
    return product;
}

template <int N>
static std::optional<Vec<N>> exactTrimPoint(const Line<N> &line, double parameter) {
    Vec<N> point;
    for (int i = 0; i < N; ++i) {
        const auto term = exactTrimProduct(parameter, line.direction()[i]);
        if (!term)
            return {};
        const auto coordinate = exactTrimSum(line.origin()[i], *term);
        if (!coordinate)
            return {};
        point[i] = *coordinate;
    }
    return point;
}

static std::optional<Vec2> exactTrimProjection(const Vec3 &point, const Frame3 &frame) {
    Vec2 result;
    for (int i = 0; i < 2; ++i) {
        const auto &direction = i == 0 ? frame.xDir() : frame.yDir();
        const int axis = trimAxis(direction);
        const auto value = exactTrimSum(point[axis], -frame.origin()[axis]);
        if (!value)
            return {};
        result[i] = *value * direction[axis];
    }
    return result;
}

// Verify that both geometric representations of each fin agree exactly
// with the topological endpoints before reasoning about the closed trim.
static bool exactTrimFin(const Body &body, const Fin &fin, const Plane &plane, const Vec3 &start, const Vec3 &end) {
    if (!body.contains(fin.edge))
        return false;
    const auto &edge = body.edge(fin.edge);
    const auto *curve = dynamic_cast<const Line<3> *>(edge.curve.get());
    const auto *pcurve = dynamic_cast<const Line<2> *>(fin.pcurve.get());
    if (!curve || !pcurve || fin.pcurveTolerance != 0. || !edge.range.isFinite() || edge.range.lo >= edge.range.hi)
        return false;
    const auto &frame = plane.frame();
    const int normal = trimAxis(frame.zDir());
    for (bool from : {true, false}) {
        const auto &vertex = from ? start : end;
        const double t = from == fin.sense ? edge.range.lo : edge.range.hi;
        const auto point = exactTrimPoint(*curve, t);
        const auto uv = exactTrimPoint(*pcurve, t);
        const auto projected = exactTrimProjection(vertex, frame);
        if (!point || !uv || !projected || vertex[normal] != frame.origin()[normal] ||
            point->x() != vertex.x() || point->y() != vertex.y() || point->z() != vertex.z() ||
            uv->x() != projected->x() || uv->y() != projected->y())
            return false;
    }
    return true;
}

struct OrthogonalTrimEdge {
    Vec2 start;
    TrimWindow bounds;
    bool vertical() const { return bounds.lo.x() == bounds.hi.x(); }
};

struct OrthogonalTrimLoop {
    std::vector<OrthogonalTrimEdge> edges;
    TrimWindow bounds;

    bool containsInterior(const Vec2 &point) const {
        bool inside = false;
        for (const auto &edge : edges)
            if (edge.vertical() && point.x() < edge.start.x() &&
                edge.bounds.lo.y() <= point.y() && point.y() < edge.bounds.hi.y())
                inside = !inside;
        return inside;
    }

    bool meetsBoundary(const OrthogonalTrimLoop &other) const {
        if (!bounds.overlaps(other.bounds, 0.))
            return false;
        for (const auto &edge : edges) {
            cancellationPoint();
            for (const auto &candidate : other.edges)
                if (edge.bounds.overlaps(candidate.bounds, 0.))
                    return true;
        }
        return false;
    }
};

static bool collectOrthogonalLoop(const Body &body, LoopId loop, const Plane &plane, const Frame3 &chart,
                                  bool counterclockwise, std::size_t &remaining, OrthogonalTrimLoop &out) {
    const FinId first = body.loop(loop).first;
    FinId current = first;
    do {
        cancellationPoint();
        if (remaining == 0 || !body.contains(current))
            return false;
        --remaining;
        const auto &fin = body.fin(current);
        if (fin.loop != loop || !body.contains(fin.next))
            return false;
        const auto &next = body.fin(fin.next);
        if (next.previous != current || !body.contains(fin.vertex) || !body.contains(next.vertex))
            return false;
        const auto &start = body.vertex(fin.vertex).point;
        const auto &end = body.vertex(next.vertex).point;
        const auto a = exactTrimProjection(start, chart), b = exactTrimProjection(end, chart);
        if (!a || !b || (a->x() == b->x()) == (a->y() == b->y()) || !exactTrimFin(body, fin, plane, start, end))
            return false;
        OrthogonalTrimEdge edge{*a, {}};
        edge.bounds.add(*a);
        edge.bounds.add(*b);
        out.bounds.add(*a);
        out.edges.push_back(edge);
        current = fin.next;
    } while (current != first);
    const auto &edges = out.edges;
    if (edges.size() < 4 || out.bounds.lo.x() >= out.bounds.hi.x() || out.bounds.lo.y() >= out.bounds.hi.y())
        return false;
    bool sawBottom = false;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        cancellationPoint();
        const auto &edge = edges[i], &next = edges[(i + 1) % edges.size()];
        // Consecutive perpendicular segments meet only at their common
        // corner. For any other pair, the segment boxes are the segments.
        if (edge.vertical() == next.vertical())
            return false;
        for (std::size_t j = i + 2; j < edges.size(); ++j) {
            if (i == 0 && j == edges.size() - 1)
                continue;
            if (edge.bounds.overlaps(edges[j].bounds, 0.))
                return false;
        }
        if (!edge.vertical() && edge.start.y() == out.bounds.lo.y()) {
            if ((edge.start.x() < next.start.x()) != counterclockwise)
                return false;
            sawBottom = true;
        }
    }
    return sawBottom;
}

using OrthogonalTrim = std::vector<OrthogonalTrimLoop>;

static bool collectOrthogonalTrim(const Body &body, FaceId id, const Frame3 &chart, bool target,
                                 std::size_t &loopBudget, std::size_t &edgeBudget, OrthogonalTrim &out) {
    if (!body.contains(id))
        return false;
    const auto &face = body.face(id);
    const auto *plane = dynamic_cast<const Plane *>(face.surface.get());
    if (!plane || !cartesianTrimFrame(plane->frame()) || face.loops.empty() || face.loops.size() > loopBudget)
        return false;
    const double facing = (face.sense ? 1. : -1.) * dot(plane->frame().zDir(), chart.zDir());
    if ((target ? facing : std::abs(facing)) != 1.)
        return false;
    loopBudget -= face.loops.size();
    for (const auto loop : face.loops) {
        cancellationPoint();
        if (!body.contains(loop) || body.loop(loop).face != id)
            return false;
        OrthogonalTrimLoop boundary;
        if (!collectOrthogonalLoop(body, loop, *plane, chart, (facing > 0.) == out.empty(), edgeBudget, boundary))
            return false;
        if (!out.empty()) {
            const auto &outer = out.front();
            const auto &start = boundary.edges.front().start;
            if (outer.meetsBoundary(boundary) || !outer.containsInterior(start))
                return false;
            for (std::size_t i = 1; i < out.size(); ++i) {
                const auto &hole = out[i];
                if (hole.meetsBoundary(boundary) || hole.containsInterior(start) ||
                    boundary.containsInterior(hole.edges.front().start))
                    return false;
            }
        }
        out.push_back(std::move(boundary));
    }
    return true;
}

static std::optional<std::vector<Interval>> trimSlabUnion(const std::vector<OrthogonalTrim> &faces, double y0, double y1) {
    std::vector<Interval> spans;
    for (const auto &face : faces) {
        cancellationPoint();
        std::vector<double> xs;
        for (const auto &loop : face)
            for (const auto &edge : loop.edges)
                if (edge.vertical() && edge.bounds.lo.y() <= y0 && y1 <= edge.bounds.hi.y())
                    xs.push_back(edge.start.x());
        if (xs.size() % 2 != 0)
            return {};
        std::sort(xs.begin(), xs.end());
        // The simple outer loop and enclosed disjoint holes alternate
        // material and empty space at each crossing along the slab.
        for (std::size_t i = 0; i < xs.size(); i += 2)
            spans.push_back({xs[i], xs[i + 1]});
    }
    std::sort(spans.begin(), spans.end(), [](Interval a, Interval b) { return a.lo < b.lo; });
    std::vector<Interval> joined;
    for (const auto span : spans) {
        // Touching closed intervals meet on a trim boundary.
        if (!joined.empty() && span.lo <= joined.back().hi)
            joined.back().hi = std::max(joined.back().hi, span.hi);
        else
            joined.push_back(span);
    }
    return joined;
}

using surface_bounds::I;

static bool sameTrimPoint(const Vec2 &a, const Vec2 &b) {
    return a.x() == b.x() && a.y() == b.y();
}

struct TrimDisk {
    Vec2 center;
    double radius;
};

struct ConvexTrim {
    std::vector<Vec2> vertices;
    std::optional<TrimDisk> disk;
    std::vector<TrimDisk> holes;
};

static TrimWindow trimOutlineBounds(const ConvexTrim &trim) {
    TrimWindow box;
    if (trim.disk) {
        for (int axis = 0; axis < 2; ++axis) {
            box.lo[axis] = (I(trim.disk->center[axis]) - I(trim.disk->radius)).lo;
            box.hi[axis] = (I(trim.disk->center[axis]) + I(trim.disk->radius)).hi;
        }
    } else {
        for (const auto &point : trim.vertices) box.add(point);
    }
    return box;
}

static std::optional<TrimDisk> collectTrimDisk(const Body &body, const Fin &fin, const Plane &plane,
                                              const Frame3 &chart, bool counterclockwise) {
    if (!body.contains(fin.edge) || !body.contains(fin.vertex) || fin.pcurveTolerance != 0.)
        return {};
    const auto &edge = body.edge(fin.edge);
    const auto *curve = dynamic_cast<const Circle<3> *>(edge.curve.get());
    const auto *pc = dynamic_cast<const Circle<2> *>(fin.pcurve.get());
    if (!curve || !pc || edge.range.lo != 0. || edge.range.hi != curve->period() ||
        edge.tolerance != 0. || pc->radius() != curve->radius())
        return {};
    const auto &frame = plane.frame();
    const auto &vertex = body.vertex(fin.vertex);
    const auto start = curve->point(edge.range.lo);
    if (vertex.tolerance != 0. || !isFinite(start) ||
        start.x() != vertex.point.x() || start.y() != vertex.point.y() || start.z() != vertex.point.z())
        return {};
    const int x = trimAxis(curve->xAxis()), y = trimAxis(curve->yAxis()), normal = trimAxis(frame.zDir());
    if (x < 0 || y < 0 || x == y || x == normal || y == normal ||
        curve->center()[normal] != frame.origin()[normal])
        return {};
    const auto localCenter = exactTrimProjection(curve->center(), frame);
    const auto projectAxis = [&](const Vec3 &d) { return Vec2(dot(d, frame.xDir()), dot(d, frame.yDir())); };
    const bool ccw = (fin.sense ? 1. : -1.) * dot(cross(curve->xAxis(), curve->yAxis()), chart.zDir()) > 0.;
    const auto center = exactTrimProjection(curve->center(), chart);
    if (!center || !localCenter || ccw != counterclockwise || !sameTrimPoint(*localCenter, pc->center()) ||
        !sameTrimPoint(projectAxis(curve->xAxis()), pc->xAxis()) ||
        !sameTrimPoint(projectAxis(curve->yAxis()), pc->yAxis()))
        return {};
    // A single periodic fin traverses the entire analytic circle.
    return TrimDisk{*center, curve->radius()};
}

static I trimSquare(I a) {
    if (!surface_bounds::finite(a))
        return surface_bounds::whole();
    if (a.lo <= 0. && 0. <= a.hi)
        return {0., std::max((I(a.lo) * I(a.lo)).hi, (I(a.hi) * I(a.hi)).hi)};
    return a * a;
}

static I trimDistanceSquared(const Vec2 &a, const Vec2 &b) {
    return trimSquare(I(a.x()) - I(b.x())) + trimSquare(I(a.y()) - I(b.y()));
}

struct TrimHalfPlane {
    I x, y, side;
};

static TrimHalfPlane trimHalfPlane(const Vec2 &a, const Vec2 &b, const Vec2 &p) {
    const I x = I(b.x()) - I(a.x()), y = I(b.y()) - I(a.y());
    const I side = x * (I(p.y()) - I(a.y())) - y * (I(p.x()) - I(a.x()));
    return {x, y, side};
}

static bool diskOnTrimSide(const TrimHalfPlane &plane, I radius, bool strict) {
    if (!(plane.side.lo > 0.) || radius.lo < 0.)
        return false;
    const I limit = trimSquare(radius) * (trimSquare(plane.x) + trimSquare(plane.y));
    const I side = trimSquare(plane.side);
    return strict ? side.lo > limit.hi : side.lo >= limit.hi;
}

static bool diskInTrimDisk(const TrimDisk &inner, const TrimDisk &outer) {
    if (sameTrimPoint(inner.center, outer.center))
        return inner.radius <= outer.radius;
    const I margin = I(outer.radius) - I(inner.radius);
    return margin.lo > 0. && trimDistanceSquared(inner.center, outer.center).hi <= trimSquare(margin).lo;
}

static bool diskInTrimPolygon(const TrimDisk &disk, const std::vector<Vec2> &vertices, bool strict) {
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto side = trimHalfPlane(vertices[i], vertices[(i + 1) % vertices.size()], disk.center);
        if (!diskOnTrimSide(side, I(disk.radius), strict))
            return false;
    }
    return true;
}

static bool strictlyConvexTrimPolygon(const std::vector<Vec2> &vertices) {
    // Every directed side strictly supports all other vertices. This
    // certifies simplicity and convexity together, including orientation.
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        cancellationPoint();
        const std::size_t j = (i + 1) % vertices.size();
        for (std::size_t k = 0; k < vertices.size(); ++k)
            if (k != i && k != j && !(trimHalfPlane(vertices[i], vertices[j], vertices[k]).side.lo > 0.))
                return false;
    }
    return true;
}

static bool collectConvexTrim(const Body &body, FaceId id, const Frame3 &chart, bool target,
                              std::size_t &loopBudget, std::size_t &edgeBudget, ConvexTrim &trim) {
    if (!body.contains(id))
        return false;
    const auto &face = body.face(id);
    const auto *plane = dynamic_cast<const Plane *>(face.surface.get());
    if (!plane || !cartesianTrimFrame(plane->frame()) || face.loops.empty() || face.loops.size() > loopBudget)
        return false;
    const double facing = (face.sense ? 1. : -1.) * dot(plane->frame().zDir(), chart.zDir());
    if ((target ? facing : std::abs(facing)) != 1.)
        return false;
    loopBudget -= face.loops.size();
    for (const auto loop : face.loops) {
        cancellationPoint();
        if (edgeBudget == 0 || !body.contains(loop) || body.loop(loop).face != id)
            return false;
        const auto first = body.loop(loop).first;
        if (!body.contains(first))
            return false;
        const auto &firstFin = body.fin(first);
        const bool outer = loop == face.loops.front();
        if (firstFin.next == first) {
            --edgeBudget;
            if (firstFin.previous != first || firstFin.loop != loop)
                return false;
            const auto disk = collectTrimDisk(body, firstFin, *plane, chart, outer == (facing > 0.));
            if (!disk)
                return false;
            if (outer) {
                if (trim.disk)
                    return false;
                trim.disk = disk;
                continue;
            }
            if (trim.disk) {
                const I margin = I(trim.disk->radius) - I(disk->radius);
                if (!(margin.lo > 0.) ||
                    !(trimDistanceSquared(trim.disk->center, disk->center).hi < trimSquare(margin).lo))
                    return false;
            } else if (!diskInTrimPolygon(*disk, trim.vertices, true)) {
                return false;
            }
            for (const auto &hole : trim.holes) {
                const I gap = trimDistanceSquared(hole.center, disk->center) -
                              trimSquare(I(hole.radius) + I(disk->radius));
                if (!(gap.lo > 0.))
                    return false;
            }
            trim.holes.push_back(*disk);
            continue;
        }
        if (!outer || !trim.vertices.empty())
            return false;
        auto current = first;
        do {
            cancellationPoint();
            if (edgeBudget == 0 || !body.contains(current))
                return false;
            --edgeBudget;
            const auto &fin = body.fin(current);
            if (fin.loop != loop || !body.contains(fin.next))
                return false;
            const auto &next = body.fin(fin.next);
            if (next.previous != current || !body.contains(fin.vertex) || !body.contains(next.vertex))
                return false;
            const auto &start = body.vertex(fin.vertex);
            const auto &end = body.vertex(next.vertex);
            const auto projected = exactTrimProjection(start.point, chart);
            if (!projected || start.tolerance != 0. || end.tolerance != 0. ||
                !exactTrimFin(body, fin, *plane, start.point, end.point) ||
                body.edge(fin.edge).tolerance != 0.)
                return false;
            trim.vertices.push_back(*projected);
            current = fin.next;
        } while (current != first);
        auto &vertices = trim.vertices;
        if (vertices.size() < 3)
            return false;
        if (facing < 0.)
            std::reverse(vertices.begin(), vertices.end());
        if (!strictlyConvexTrimPolygon(vertices))
            return false;
    }
    return true;
}

static bool convexTrimIncludes(const ConvexTrim &outer, const ConvexTrim &inner) {
    for (const auto &hole : outer.holes) {
        cancellationPoint();
        if (!std::any_of(inner.holes.begin(), inner.holes.end(),
                         [&](const TrimDisk &candidate) { return diskInTrimDisk(hole, candidate); }))
            return false;
    }
    if (inner.disk)
        return outer.disk ? diskInTrimDisk(*inner.disk, *outer.disk) :
                            diskInTrimPolygon(*inner.disk, outer.vertices, false);
    for (const auto &point : inner.vertices) {
        cancellationPoint();
        if (outer.disk) {
            if (trimDistanceSquared(outer.disk->center, point).hi > trimSquare(I(outer.disk->radius)).lo)
                return false;
        } else {
            for (std::size_t i = 0; i < outer.vertices.size(); ++i) {
                const auto &a = outer.vertices[i], &b = outer.vertices[(i + 1) % outer.vertices.size()];
                if (!sameTrimPoint(point, a) && !sameTrimPoint(point, b) &&
                    trimHalfPlane(a, b, point).side.lo < 0.)
                    return false;
            }
        }
    }
    return true;
}

static std::vector<ConvexTrim> joinConvexTrims(const std::vector<ConvexTrim> &trims) {
    // Charge each pair and the full side/vertex test for every candidate.
    std::size_t work = 65536;
    std::vector<ConvexTrim> joined;
    for (std::size_t i = 0; i < trims.size() + joined.size(); ++i) {
        // Copy the current loop because a successful join may reallocate joined.
        const auto &current = i < trims.size() ? trims[i] : joined[i - trims.size()];
        if (current.vertices.empty() || !current.holes.empty())
            continue;
        const auto a = current.vertices;
        const std::size_t partners = std::min(i, trims.size());
        for (std::size_t j = 0; j < partners; ++j) {
            cancellationPoint();
            const auto &b = trims[j].vertices;
            if (b.empty() || !trims[j].holes.empty())
                continue;
            if (work < a.size() * b.size())
                return joined;
            work -= a.size() * b.size();
            for (std::size_t k = 0; k < a.size(); ++k)
                for (std::size_t m = 0; m < b.size(); ++m) {
                    if (!sameTrimPoint(a[k], b[(m + 1) % b.size()]) ||
                        !sameTrimPoint(a[(k + 1) % a.size()], b[m]))
                        continue;
                    const auto count = a.size() + b.size() - 2;
                    if (work < count * count)
                        return joined;
                    work -= count * count;
                    ConvexTrim candidate;
                    auto &outer = candidate.vertices;
                    outer.reserve(count);
                    // Each convex interior lies in an opposite half-plane
                    // of the shared side. Remove that seam from both loops.
                    for (std::size_t n = 1; n <= a.size(); ++n)
                        outer.push_back(a[(k + n) % a.size()]);
                    for (std::size_t n = 2; n < b.size(); ++n)
                        outer.push_back(b[(m + n) % b.size()]);
                    if (strictlyConvexTrimPolygon(outer))
                        joined.push_back(std::move(candidate));
                }
        }
    }
    return joined;
}

// Across a strip between consecutive vertex ordinates, each crossing
// side is affine. Ordering two sides at both endpoints orders them
// throughout the strip, including shared boundaries.
struct TrimStripSide {
    Vec2 low, high;

    I at(double y) const {
        if (y == low.y() || low.x() == high.x()) return I(low.x());
        if (y == high.y()) return I(high.x());
        const I t = (I(y) - I(low.y())) / (I(high.y()) - I(low.y()));
        return I(low.x()) + t * (I(high.x()) - I(low.x()));
    }
};

using TrimStrip = std::array<TrimStripSide, 2>;

static std::optional<TrimStrip> trimStrip(const std::vector<Vec2> &loop, Interval y) {
    std::array<std::optional<TrimStripSide>, 2> sides;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto &a = loop[i], &b = loop[(i + 1) % loop.size()];
        const bool ascending = a.y() < b.y();
        const auto &low = ascending ? a : b, &high = ascending ? b : a;
        if (low.y() <= y.lo && high.y() >= y.hi)
            sides[ascending] = TrimStripSide{low, high};
    }
    if (!sides[0] || !sides[1]) return {};
    return TrimStrip{*sides[0], *sides[1]};
}

static bool stripSideBefore(const TrimStripSide &a, const TrimStripSide &b, Interval y) {
    return (sameTrimPoint(a.low, b.low) && sameTrimPoint(a.high, b.high)) ||
        (a.at(y.lo).hi <= b.at(y.lo).lo && a.at(y.hi).hi <= b.at(y.hi).lo);
}

static bool trimCoveredByStrips(const ConvexTrim &source, bool enclosed,
                               const std::vector<ConvexTrim> &targets, std::size_t &work) {
    auto outline = source.vertices;
    if (enclosed || source.disk) {
        const auto box = trimOutlineBounds(source);
        if (!box.finite() || box.lo.x() == box.hi.x() || box.lo.y() == box.hi.y()) return false;
        outline = {box.lo, {box.hi.x(), box.lo.y()}, box.hi, {box.lo.x(), box.hi.y()}};
    }
    std::vector<double> events;
    for (const auto &p : outline) events.push_back(p.y());
    const auto [first, last] = std::minmax_element(events.begin(), events.end());
    const double low = *first, high = *last;
    std::size_t edgeCount = outline.size();
    for (const auto &target : targets) {
        if (!target.holes.empty() || target.vertices.empty()) continue;
        edgeCount += target.vertices.size();
        for (const auto &p : target.vertices)
            if (p.y() > low && p.y() < high) events.push_back(p.y());
    }
    std::sort(events.begin(), events.end());
    events.erase(std::unique(events.begin(), events.end()), events.end());
    for (std::size_t i = 1; i < events.size(); ++i) {
        cancellationPoint();
        if (work < edgeCount) return false;
        work -= edgeCount;
        const Interval y{events[i - 1], events[i]};
        const auto desired = trimStrip(outline, y);
        if (!desired) return false;
        std::vector<TrimStrip> active;
        for (const auto &target : targets)
            if (target.holes.empty())
                if (const auto strip = trimStrip(target.vertices, y)) active.push_back(*strip);

        auto end = (*desired)[0];
        while (!stripSideBefore((*desired)[1], end, y)) {
            if (work < active.size()) return false;
            work -= active.size();
            const auto next = std::find_if(active.begin(), active.end(), [&](const TrimStrip &strip) {
                return stripSideBefore(strip[0], end, y) && stripSideBefore(end, strip[1], y);
            });
            if (next == active.end()) return false;
            end = (*next)[1];
            active.erase(next);
        }
    }
    return events.size() > 1;
}

static bool trimDiskPoint(const TrimDisk &disk, const Vec2 &point, double tolerance, bool inside) {
    const I distance = trimDistanceSquared(disk.center, point);
    const I radius = inside ? I(disk.radius) - I(tolerance) : I(disk.radius) + I(tolerance);
    return radius.lo > 0. && (inside ? distance.hi < trimSquare(radius).lo : distance.lo > trimSquare(radius).hi);
}

static bool convexTrimPoint(const ConvexTrim &trim, const Vec2 &point, double tolerance, bool inside) {
    for (const auto &hole : trim.holes)
        if (trimDiskPoint(hole, point, tolerance, !inside) == !inside)
            return !inside;
    if (trim.disk)
        return trimDiskPoint(*trim.disk, point, tolerance, inside);
    for (std::size_t i = 0; i < trim.vertices.size(); ++i) {
        const auto &a = trim.vertices[i], &b = trim.vertices[(i + 1) % trim.vertices.size()];
        const auto plane = inside ? trimHalfPlane(a, b, point) : trimHalfPlane(b, a, point);
        if (diskOnTrimSide(plane, I(tolerance), true) != inside)
            return !inside;
    }
    return inside;
}

struct RestrictedTrimPole {
    std::array<I, 3> homogeneous;
    std::array<I, 2> cartesian;
};

struct ExactTrimSpan {
    const BSplineCurve<3> *curve = nullptr;
    bool forward = true;
    // Endpoints are exact. Interior restricted poles may be represented
    // by an enclosure, which the geometric tests access via poleSide().
    std::vector<Vec2> poles;
    // First active native pole of a represented knot cell.
    std::optional<std::size_t> bezierPoleOffset;
    Interval range;
    std::vector<RestrictedTrimPole> restriction;
    bool intervalRestriction = false;

    const Vec2 &first() const { return forward ? poles.front() : poles.back(); }
    const Vec2 &last() const { return forward ? poles.back() : poles.front(); }
    I weight(std::size_t i) const {
        return restriction.empty() ? I(curve->weight(int(bezierPoleOffset.value_or(0) + i))) :
                                     restriction[i].homogeneous[2];
    }
    bool exactCartesian(std::size_t i) const {
        if (restriction.empty()) return true;
        const auto &box = restriction[i].cartesian;
        return box[0].lo == box[0].hi && box[1].lo == box[1].hi;
    }
    I poleSide(const Vec2 &a, const Vec2 &b, std::size_t i) const {
        if (restriction.empty()) return trimHalfPlane(a, b, poles[i]).side;
        const auto &box = restriction[i].cartesian;
        return (I(b.x()) - I(a.x())) * (box[1] - I(a.y())) -
               (I(b.y()) - I(a.y())) * (box[0] - I(a.x()));
    }
    template <class Add>
    void addPoleHull(Add add) const {
        if (restriction.empty()) {
            for (const auto &pole : poles) add(pole);
            return;
        }
        for (const auto &pole : restriction) {
            const auto &box = pole.cartesian;
            add(Vec2(box[0].lo, box[1].lo));
            if (box[0].lo != box[0].hi) add(Vec2(box[0].hi, box[1].lo));
            if (box[1].lo != box[1].hi) {
                add(Vec2(box[0].lo, box[1].hi));
                if (box[0].lo != box[0].hi) add(Vec2(box[0].hi, box[1].hi));
            }
        }
    }
    Interval bezierDomain() const {
        const auto end = *bezierPoleOffset + std::size_t(curve->degree());
        const auto &knots = curve->knots();
        return {knots[end], knots[end + 1]};
    }
};

static std::optional<double> exactTrimQuotient(double numerator, double denominator) {
    if (!(denominator > 0.)) return {};
    const double quotient = numerator / denominator;
    const auto restored = exactTrimProduct(quotient, denominator);
    if (!restored || *restored != numerator) return {};
    return quotient;
}

static I trimQuotientBound(I numerator, I denominator) {
    if (numerator.lo == numerator.hi && denominator.lo == denominator.hi)
        if (const auto exact = exactTrimQuotient(numerator.lo, denominator.lo)) return I(*exact);
    return numerator / denominator;
}

static std::optional<double> exactTrimParameter(double t, Interval domain) {
    if (t == domain.lo) return 0.;
    if (t == domain.hi) return 1.;
    const auto numerator = exactTrimSum(t, -domain.lo), denominator = exactTrimSum(domain.hi, -domain.lo);
    if (!numerator || !denominator) return {};
    return exactTrimQuotient(*numerator, *denominator);
}

static I trimSumBound(I a, I b) {
    if (a.lo == a.hi && b.lo == b.hi)
        if (const auto exact = exactTrimSum(a.lo, b.lo)) return I(*exact);
    return a + b;
}

static I trimDifferenceBound(I a, I b) {
    return trimSumBound(a, {-b.hi, -b.lo});
}

static I trimProductBound(I a, I b) {
    if (a.lo == a.hi && b.lo == b.hi)
        if (const auto exact = exactTrimProduct(a.lo, b.lo)) return I(*exact);
    return a * b;
}

using TrimHomogeneousPoint = std::array<I, 3>;

struct TrimEndpointValue {
    TrimHomogeneousPoint numerator;
    double denominator = 1.;
};

static std::optional<TrimEndpointValue> blendExactTrimEndpoint(const TrimEndpointValue &a,
    const TrimEndpointValue &b, double t, double lo, double hi) {
    const auto l = exactTrimSum(hi, -t), r = exactTrimSum(t, -lo);
    if (!l || !r || !(*l > 0.) || !(*r > 0.)) return {};
    const auto width = exactTrimSum(*l, *r);
    if (!width) return {};
    auto denominator = exactTrimProduct(*width, a.denominator);
    const bool common = a.denominator == b.denominator;
    const auto left = common ? l : exactTrimProduct(*l, b.denominator);
    const auto right = common ? r : exactTrimProduct(*r, a.denominator);
    if (denominator && !common) denominator = exactTrimProduct(*denominator, b.denominator);
    if (!denominator || !left || !right) return {};
    TrimEndpointValue value{{}, *denominator};
    for (std::size_t j = 0; j < value.numerator.size(); ++j) {
        const I na = a.numerator[j], nb = b.numerator[j];
        if (na.lo != na.hi || nb.lo != nb.hi) return {};
        const auto x = exactTrimProduct(na.lo, *left), y = exactTrimProduct(nb.lo, *right);
        if (!x || !y) return {};
        const auto sum = exactTrimSum(*x, *y);
        if (!sum) return {};
        value.numerator[j] = I(*sum);
    }
    return value;
}

// Each de Boor entry shares a positive denominator across XW, YW and W.
// Keep the recurrence exact without dividing through knot intervals, then
// cancel the final common denominator in the Cartesian ratios.
static std::optional<Vec2> evaluateExactTrimEnd(const ExactTrimSpan &span, double t,
    const std::array<TrimHomogeneousPoint, 9> &original, std::size_t count, std::size_t &work) {
    const auto offset = *span.bezierPoleOffset;
    const auto &knots = span.curve->knots();
    if (work < count * count) return {};
    work -= count * count;
    std::array<TrimEndpointValue, 9> values;
    for (std::size_t i = 0; i < count; ++i) values[i].numerator = original[i];
    for (std::size_t step = 1; step < count; ++step) {
        for (std::size_t k = count - 1; k >= step; --k) {
            const double lo = knots[offset + k], hi = knots[offset + count + k - step];
            if (t == lo) values[k] = values[k - 1];
            else if (t != hi) {
                const auto value = blendExactTrimEndpoint(values[k - 1], values[k], t, lo, hi);
                if (!value) return {};
                values[k] = *value;
            }
        }
    }
    const auto &pole = values[count - 1].numerator;
    if (std::any_of(pole.begin(), pole.end(), [](I coordinate) { return coordinate.lo != coordinate.hi; })) return {};
    const auto x = exactTrimQuotient(pole[0].lo, pole[2].lo), y = exactTrimQuotient(pole[1].lo, pole[2].lo);
    if (!x || !y) return {};
    return Vec2(*x, *y);
}

static std::optional<Vec2> exactBezierTrimEnd(const ExactTrimSpan &span, const Frame3 &chart, double t,
    const std::array<TrimHomogeneousPoint, 9> &original, std::size_t count, std::size_t &work) {
    const auto offset = *span.bezierPoleOffset;
    const auto &knots = span.curve->knots();
    const auto domain = span.bezierDomain();
    if (t == domain.lo && knots[offset + 1] == domain.lo)
        return exactTrimProjection(span.curve->poles()[offset], chart);
    if (t == domain.hi && knots[offset + 2 * (count - 1)] == domain.hi)
        return exactTrimProjection(span.curve->poles()[offset + count - 1], chart);
    if (const auto direct = evaluateExactTrimEnd(span, t, original, count, work)) return direct;

    const auto normalizedEnd = [&]() -> std::optional<Vec2> {
        if (work < count + count * count || std::none_of(original.begin(), original.begin() + count, [](const auto &p) {
                return p[0].lo != p[0].hi || p[1].lo != p[1].hi;
            })) return {};
        work -= count;
        // A common positive weight factor cancels in the Cartesian ratios.
        // Cancel it before forming XW and YW, only if every quotient and
        // resulting numerator is itself exactly representable.
        double scale = 0.;
        for (std::size_t i = 0; i < count; ++i) scale = std::max(scale, original[i][2].lo);
        auto normalized = original;
        for (std::size_t i = 0; i < count; ++i) {
            const auto weight = exactTrimQuotient(original[i][2].lo, scale);
            const auto point = exactTrimProjection(span.curve->poles()[offset + i], chart);
            if (!weight || !(*weight > 0.) || !point) return {};
            const auto x = exactTrimProduct(point->x(), *weight), y = exactTrimProduct(point->y(), *weight);
            if (!x || !y) return {};
            normalized[i] = {I(*x), I(*y), I(*weight)};
        }
        return evaluateExactTrimEnd(span, t, normalized, count, work);
    }();
    if (normalizedEnd) return normalizedEnd;

    // Exact de Boor in homogeneous coordinates also works when the cell
    // is not saturated or its normalized trim parameter is not binary64.
    if (work < 2 * count * count) return {};
    work -= 2 * count * count;
    std::array<Vec2, 9> poles;
    std::array<double, 9> weights;
    for (std::size_t i = 0; i < count; ++i) {
        const auto projected = exactTrimProjection(span.curve->poles()[offset + i], chart);
        if (!projected) return {};
        poles[i] = *projected;
        weights[i] = span.curve->weight(int(offset + i));
    }
    return exactRationalSplinePoint(poles.data(), weights.data(), count, knots.data() + offset + 1, t);
}

// Polar de Boor directly on the original knot cell. Evaluating the blossom
// at the two requested endpoints both extracts and restricts the Bézier
// piece. The hull may use enclosure coefficients, but the endpoints must
// have an independent proof of exactness.
static bool extractBezierTrim(ExactTrimSpan &span, Interval range, const Frame3 &chart, std::size_t &work) {
    const auto count = std::size_t(span.curve->degree()) + 1;
    if (count > 9 || work < count * count * count) return false;
    work -= count * count * count;
    const auto domain = span.bezierDomain();
    if (!(domain.lo <= range.lo && range.lo < range.hi && range.hi <= domain.hi)) return false;
    const auto offset = *span.bezierPoleOffset;
    const auto &curve = *span.curve;
    const auto &knots = curve.knots();
    std::array<TrimHomogeneousPoint, 9> original{};
    for (std::size_t i = 0; i < count; ++i) {
        const auto pole = exactTrimProjection(curve.poles()[offset + i], chart);
        const auto weight = curve.weight(int(offset + i));
        if (!pole) return false;
        original[i] = {trimProductBound(I(pole->x()), I(weight)), trimProductBound(I(pole->y()), I(weight)), I(weight)};
    }
    span.poles.resize(count);
    span.restriction.resize(count);
    span.intervalRestriction = false;
    for (std::size_t i = 0; i < count; ++i) {
        auto values = original;
        for (std::size_t step = 1; step < count; ++step) {
            const double t = step < count - i ? range.lo : range.hi;
            for (std::size_t k = count - 1; k >= step; --k) {
                const Interval basis{knots[offset + k], knots[offset + count + k - step]};
                const auto exact = exactTrimParameter(t, basis);
                const I alpha = exact ? I(*exact) : (I(t) - I(basis.lo)) / (I(basis.hi) - I(basis.lo));
                if (!surface_bounds::finite(alpha)) return false;
                const I complement = trimSumBound(I(1.), {-alpha.hi, -alpha.lo});
                for (std::size_t j = 0; j < 3; ++j) {
                    const I a = values[k - 1][j], b = values[k][j];
                    if (a.lo == a.hi && surface_bounds::exact(b, a.lo)) continue;
                    values[k][j] = trimSumBound(trimProductBound(a, complement), trimProductBound(b, alpha));
                }
            }
        }
        const auto &pole = values[count - 1];
        if (!surface_bounds::finite(pole[2]) || !(pole[2].lo > 0.)) return false;
        span.intervalRestriction = span.intervalRestriction || std::any_of(pole.begin(), pole.end(), [](I value) {
            return value.lo != value.hi;
        });
        std::array<I, 2> box;
        for (std::size_t j = 0; j < box.size(); ++j)
            box[j] = trimQuotientBound(pole[j], pole[2]);
        if ((box[0].lo != box[0].hi || box[1].lo != box[1].hi) && (i == 0 || i + 1 == count)) {
            const auto end = exactBezierTrimEnd(span, chart, i == 0 ? range.lo : range.hi, original, count, work);
            if (!end) return false;
            box = {I(end->x()), I(end->y())};
        }
        if (!surface_bounds::finite(box[0]) || !surface_bounds::finite(box[1])) return false;
        span.poles[i] = Vec2(box[0].lo, box[1].lo);
        span.restriction[i] = {pole, box};
    }
    span.range = range;
    return true;
}

static bool appendExactTrimCells(const ExactTrimSpan &span, Interval range, const Frame3 &chart,
                                 std::size_t &work, std::vector<ExactTrimSpan> &pieces) {
    const auto &knots = span.curve->knots();
    const auto degree = std::size_t(span.curve->degree()), count = span.poles.size();
    if (work < count) return false;
    work -= count;
    for (std::size_t i = degree; i < count; ++i) {
        cancellationPoint();
        const auto cell = span.forward ? i : count + degree - 1 - i;
        const auto offset = cell - degree;
        const Interval domain{knots[cell], knots[cell + 1]};
        const Interval part{std::max(domain.lo, range.lo), std::min(domain.hi, range.hi)};
        if (!(part.lo < part.hi)) continue;
        ExactTrimSpan piece{span.curve, span.forward, {}, offset, domain, {}};
        if (part.lo == domain.lo && part.hi == domain.hi &&
            knots[offset + 1] == domain.lo && knots[cell + degree] == domain.hi) {
            piece.poles.assign(span.poles.begin() + offset, span.poles.begin() + cell + 1);
        } else if (!extractBezierTrim(piece, part, chart, work)) {
            return false;
        }
        pieces.push_back(std::move(piece));
    }
    return true;
}

static bool collectExactTrimBoundary(const Body &body, FaceId id, LoopId loop, const Frame3 &chart, bool target,
                                     std::size_t &work, std::vector<ExactTrimSpan> &spans) {
    const auto &face = body.face(id);
    const auto *plane = dynamic_cast<const Plane *>(face.surface.get());
    if (!plane || !cartesianTrimFrame(plane->frame()) || !body.contains(loop) || body.loop(loop).face != id)
        return false;
    const double facing = (face.sense ? 1. : -1.) * dot(plane->frame().zDir(), chart.zDir());
    if ((target ? facing : std::abs(facing)) != 1.)
        return false;
    const auto first = body.loop(loop).first;
    auto current = first;
    do {
        cancellationPoint();
        if (!work || !body.contains(current)) return false;
        --work;
        const auto &fin = body.fin(current);
        if (fin.loop != loop || !body.contains(fin.next) || !body.contains(fin.edge) || !body.contains(fin.vertex))
            return false;
        const auto &next = body.fin(fin.next);
        if (next.previous != current || !body.contains(next.vertex)) return false;
        const auto &startVertex = body.vertex(fin.vertex);
        const auto &endVertex = body.vertex(next.vertex);
        const auto &edge = body.edge(fin.edge);
        if (startVertex.tolerance != 0. || endVertex.tolerance != 0. || edge.tolerance != 0.) return false;
        const auto &start = startVertex.point, &end = endVertex.point;
        const auto *spline = dynamic_cast<const BSplineCurve<3> *>(edge.curve.get());
        ExactTrimSpan span;
        if (!spline) {
            const auto a = exactTrimProjection(start, chart), b = exactTrimProjection(end, chart);
            if (!a || !b || !exactTrimFin(body, fin, *plane, start, end)) return false;
            span.poles = {*a, *b};
        } else {
            const auto *pc = dynamic_cast<const BSplineCurve<2> *>(fin.pcurve.get());
            const auto range = spline->domain();
            const auto &knots = spline->knots();
            const auto &poles = spline->poles();
            if (!pc || fin.pcurveTolerance != 0. || spline->isPeriodic() || pc->isPeriodic() ||
                spline->degree() != pc->degree() || knots != pc->knots() || spline->weights() != pc->weights() ||
                !edge.range.isFinite() || edge.range.lo < range.lo || edge.range.hi > range.hi ||
                edge.range.lo >= edge.range.hi || knots.front() != range.lo ||
                knots.back() != range.hi || poles.size() != pc->poles().size() || poles.size() > work)
                return false;
            work -= poles.size();
            span.curve = spline;
            span.forward = fin.sense;
            span.range = range;
            if (poles.size() == std::size_t(spline->degree()) + 1) span.bezierPoleOffset = 0;
            const auto &frame = plane->frame();
            const int axis = trimAxis(frame.zDir());
            for (std::size_t i = 0; i < poles.size(); ++i) {
                if (poles[i][axis] != frame.origin()[axis]) return false;
                const auto uv = exactTrimProjection(poles[i], frame);
                const auto projected = exactTrimProjection(poles[i], chart);
                if (!uv || !projected || !sameTrimPoint(*uv, pc->poles()[i])) return false;
                span.poles.push_back(*projected);
            }
            const auto a = exactTrimProjection(start, chart), b = exactTrimProjection(end, chart);
            if (!a || !b || start[axis] != frame.origin()[axis] || end[axis] != frame.origin()[axis]) return false;
            if (edge.range.lo != range.lo || edge.range.hi != range.hi) {
                const auto begin = spans.size();
                if (!appendExactTrimCells(span, edge.range, chart, work, spans) || begin == spans.size() ||
                    !sameTrimPoint(*a, spans[begin].first()) ||
                    !sameTrimPoint(*b, spans.back().last())) return false;
                current = fin.next;
                continue;
            }
            if (!sameTrimPoint(*a, span.first()) || !sameTrimPoint(*b, span.last())) return false;
        }
        spans.push_back(std::move(span));
        current = fin.next;
    } while (current != first);
    return true;
}

// The source material lies in the convex hull of the outer boundary.
// The hull of the fin poles encloses that boundary for positive weights.
static bool collectSourceTrimPoles(const Body &body, FaceId id, const Frame3 &chart,
                                 std::size_t &work, ConvexTrim &hull) {
    const auto &loops = body.face(id).loops;
    std::vector<ExactTrimSpan> spans;
    if (loops.empty() || !collectExactTrimBoundary(body, id, loops.front(), chart, false, work, spans) ||
        std::none_of(spans.begin(), spans.end(), [](const auto &span) { return span.curve; }))
        return false;
    for (const auto &span : spans)
        span.addPoleHull([&](const Vec2 &pole) { hull.vertices.push_back(pole); });
    return true;
}

// Each curved fin can deform to its chord in the exterior half-plane.
// The boundary retains its winding about every interior polygon point.
static std::optional<ConvexTrim> inscribedTrim(const std::vector<ExactTrimSpan> &spans, std::size_t &work) {
    if (spans.size() < 3 || work < spans.size() * spans.size()) return {};
    work -= spans.size() * spans.size();
    ConvexTrim inner;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (!sameTrimPoint(spans[i].last(), spans[(i + 1) % spans.size()].first())) return {};
        inner.vertices.push_back(spans[i].first());
    }
    if (!strictlyConvexTrimPolygon(inner.vertices)) return {};
    for (const auto &span : spans) {
        for (std::size_t i = 1; i + 1 < span.poles.size(); ++i)
            if (span.poleSide(span.first(), span.last(), i).hi > 0.)
                return {};
    }
    return inner;
}

constexpr std::size_t trimTriangleBezierLimit = 9;

struct TriangleTrimSide {
    std::array<I, trimTriangleBezierLimit> numerator;
    std::array<I, 2> chord;
};

static bool trimHomotopyOutsideTriangle(std::array<TriangleTrimSide, 3> sides, std::size_t count,
                                        unsigned depth, std::size_t &work) {
    cancellationPoint();
    const auto hullCost = sides.size() * (count + 2);
    if (work < hullCost) return false;
    work -= hullCost;
    for (const auto &side : sides)
        if (side.chord.front().hi <= 0. && side.chord.back().hi <= 0. &&
            std::all_of(side.numerator.begin(), side.numerator.begin() + count,
                        [](I value) { return value.hi <= 0.; }))
            return true;

    for (bool atEnd : {false, true}) {
        const auto index = atEnd ? count - 1 : 0;
        if (std::all_of(sides.begin(), sides.end(), [&](const auto &side) { return side.numerator[index].lo > 0.; }) ||
            std::all_of(sides.begin(), sides.end(), [&](const auto &side) { return side.chord[atEnd].lo > 0.; }))
            return false;
    }
    const auto subdivisionCost = sides.size() * count * count;
    if (depth == 0 || work < subdivisionCost) return false;
    work -= subdivisionCost;
    const I half(.5);
    std::array<TriangleTrimSide, 3> left, right;
    for (std::size_t i = 0; i < sides.size(); ++i) {
        const auto &chord = sides[i].chord;
        const I midpoint = half * chord.front() + half * chord.back();
        left[i].chord = {chord.front(), midpoint};
        right[i].chord = {midpoint, chord.back()};
        auto &poles = sides[i].numerator;
        left[i].numerator.front() = poles.front();
        right[i].numerator[count - 1] = poles[count - 1];
        for (std::size_t level = 1; level < count; ++level) {
            const auto last = count - level - 1;
            for (std::size_t j = 0; j <= last; ++j)
                poles[j] = half * poles[j] + half * poles[j + 1];
            left[i].numerator[level] = poles.front();
            right[i].numerator[last] = poles[last];
        }
    }
    return trimHomotopyOutsideTriangle(left, count, depth - 1, work) &&
           trimHomotopyOutsideTriangle(right, count, depth - 1, work);
}

static bool trimHullOutsideTriangle(const ExactTrimSpan &span, const std::vector<Vec2> &triangle,
                                    std::size_t &work) {
    const auto exterior = [&](const Vec2 &a, const Vec2 &b) {
        for (std::size_t i = 0; i < span.poles.size(); ++i)
            if (span.poleSide(a, b, i).hi > 0.) return false;
        return true;
    };
    for (std::size_t i = 0; i < triangle.size(); ++i)
        if (exterior(triangle[i], triangle[(i + 1) % triangle.size()])) return true;

    const auto &a = span.first(), &b = span.last();
    if (!sameTrimPoint(a, b) && exterior(a, b) &&
        std::all_of(triangle.begin(), triangle.end(), [&](const Vec2 &p) {
            return trimHalfPlane(a, b, p).side.lo >= 0.;
        }))
        return true;

    const auto count = span.poles.size();
    if (!span.bezierPoleOffset || count > trimTriangleBezierLimit || work < triangle.size() * count)
        return false;
    work -= triangle.size() * count;
    // For a directed triangle side, the signed affine function on a
    // rational Bézier is a Bernstein numerator over a positive denominator.
    // Bisect its numerator and the original chord at the same parameter.
    // A shared exterior half-plane on each subinterval then contains every
    // straight segment of the homotopy from the curve to the original chord.
    std::array<TriangleTrimSide, 3> sides;
    for (std::size_t i = 0; i < sides.size(); ++i) {
        const auto &start = triangle[i], &end = triangle[(i + 1) % sides.size()];
        auto &side = sides[i];
        for (std::size_t j = 0; j < count; ++j) {
            side.numerator[j] = I(span.weight(j)) * span.poleSide(start, end, j);
        }
        side.chord = {trimHalfPlane(start, end, span.poles.front()).side,
                      trimHalfPlane(start, end, span.poles.back()).side};
    }
    return trimHomotopyOutsideTriangle(sides, count, 8, work);
}

static void appendInscribedSplineTriangles(const std::vector<ExactTrimSpan> &spans,
                                            std::vector<ConvexTrim> &inner, std::size_t &work) {
    std::vector<Vec2> outline;
    std::size_t hullCost = spans.size();
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const auto &span = spans[i];
        if (!sameTrimPoint(span.last(), spans[(i + 1) % spans.size()].first()) ||
            sameTrimPoint(span.first(), span.last()))
            return;
        outline.push_back(span.first());
        if (span.curve) hullCost += 4 * span.poles.size() + 3;
    }

    // The chord loop's winding is the sum of the oriented triangles'
    // winding: every inserted diagonal occurs twice in opposite directions.
    // Complete that decomposition before accepting any of the triangles.
    std::vector<ConvexTrim> triangles;
    while (outline.size() >= 3) {
        const auto count = outline.size();
        bool clipped = false;
        for (std::size_t i = 0; i < count; ++i) {
            cancellationPoint();
            if (work < 3 * count) return;
            work -= 3 * count;
            const auto prev = (i + count - 1) % count, next = (i + 1) % count;
            const auto &a = outline[prev], &b = outline[i], &c = outline[next];
            if (trimHalfPlane(a, b, c).side.lo <= 0.) continue;
            bool empty = true;
            for (std::size_t j = 0; j < count && empty; ++j) {
                if (j == prev || j == i || j == next) continue;
                const auto &p = outline[j];
                empty = trimHalfPlane(a, b, p).side.hi < 0. || trimHalfPlane(b, c, p).side.hi < 0. ||
                        trimHalfPlane(c, a, p).side.hi < 0.;
            }
            if (!empty) continue;
            triangles.push_back({{a, b, c}, {}, {}});
            outline.erase(outline.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) return;
    }
    for (auto &triangle : triangles) {
        cancellationPoint();
        if (work < hullCost) return;
        work -= hullCost;
        // Keep only triangles whose positive winding is preserved under
        // the simultaneous homotopy of all chords back to their arcs.
        // Closure includes the edges for the subsequent strip proof.
        if (std::all_of(spans.begin(), spans.end(), [&](const auto &span) {
                return !span.curve || trimHullOutsideTriangle(span, triangle.vertices, work);
            }))
            inner.push_back(std::move(triangle));
    }
}

// When both ends of a span have multiplicity at least p, its degree-p
// Bezier poles are already present. Ordinary knot insertion could round
// two distinct curves to the same coefficients.
static std::vector<ExactTrimSpan> splitSplineTrim(const std::vector<ExactTrimSpan> &loop, std::size_t &work,
                                                const Frame3 *chart = nullptr) {
    std::vector<ExactTrimSpan> pieces;
    bool split = false;
    for (const auto &span : loop) {
        cancellationPoint();
        const auto degree = span.curve ? std::size_t(span.curve->degree()) : 0;
        const auto count = span.poles.size();
        if (work < 2 * count) return {};
        work -= 2 * count;
        if (degree && count > degree + 1 && (count - 1) % degree == 0) {
            const auto &knots = span.curve->knots();
            bool saturated = true;
            for (std::size_t i = degree; i < count; i += degree) {
                if (!(knots[i] < knots[i + 1]) || knots[i - degree + 1] != knots[i] ||
                    knots[i + degree] != knots[i + 1]) {
                    saturated = false;
                    break;
                }
            }
            if (saturated) {
                split = true;
                for (std::size_t i = 0; i < count - 1; i += degree) {
                    const auto offset = span.forward ? i : count - degree - 1 - i;
                    pieces.push_back({span.curve, span.forward,
                                      {span.poles.begin() + offset, span.poles.begin() + offset + degree + 1},
                                      offset, {knots[offset + degree], knots[offset + degree + 1]}, {}});
                }
                continue;
            }
        }
        if (chart && span.curve && !span.bezierPoleOffset) {
            if (!appendExactTrimCells(span, span.range, *chart, work, pieces)) return {};
            split = true;
            continue;
        }
        pieces.push_back(span);
    }
    if (!split) pieces.clear();
    return pieces;
}

static bool sameLocalTrimParameter(double a, double b, Interval first, Interval second) {
    if (first.lo == second.lo && first.hi == second.hi) return a == b;
    const auto offsetA = exactTrimSum(a, -first.lo), offsetB = exactTrimSum(b, -second.lo);
    const auto widthA = exactTrimSum(first.hi, -first.lo), widthB = exactTrimSum(second.hi, -second.lo);
    if (!offsetA || !offsetB || !widthA || !widthB) return false;
    const auto lhs = exactTrimProduct(*offsetA, *widthB), rhs = exactTrimProduct(*offsetB, *widthA);
    return lhs && rhs && *lhs == *rhs;
}

static bool sameNativeTrimCell(const ExactTrimSpan &a, const ExactTrimSpan &b) {
    if (!a.bezierPoleOffset || !b.bezierPoleOffset || a.poles.size() != b.poles.size()) return false;
    const auto first = *a.bezierPoleOffset, second = *b.bezierPoleOffset;
    const auto degree = a.poles.size() - 1;
    const auto &knotsA = a.curve->knots(), &knotsB = b.curve->knots();
    const auto domainA = a.bezierDomain(), domainB = b.bezierDomain();
    for (std::size_t i = 1; i <= 2 * degree; ++i) {
        if (!sameLocalTrimParameter(knotsA[first + i], knotsB[second + i], domainA, domainB)) return false;
    }
    const double scaleA = a.curve->weight(int(first)), scaleB = b.curve->weight(int(second));
    for (std::size_t i = 0; i < a.poles.size(); ++i) {
        const auto &p = a.curve->poles()[first + i], &q = b.curve->poles()[second + i];
        if (p.x() != q.x() || p.y() != q.y() || p.z() != q.z()) return false;
        const double wa = a.curve->weight(int(first + i)), wb = b.curve->weight(int(second + i));
        if (scaleA == scaleB) {
            if (wa != wb) return false;
        } else if (wa != scaleA || wb != scaleB) {
            // One common positive scale cancels in the rational basis.
            // Compare exact cross products so rounded ratios cannot match.
            const auto ratioA = exactTrimProduct(wa, scaleB), ratioB = exactTrimProduct(wb, scaleA);
            if (!ratioA || !ratioB || *ratioA != *ratioB) return false;
        }
    }
    return true;
}

static TrimHomogeneousPoint homogeneousTrimPole(const ExactTrimSpan &span, std::size_t i) {
    if (!span.restriction.empty()) return span.restriction[i].homogeneous;
    const I weight = span.weight(i);
    return {trimProductBound(I(span.poles[i].x()), weight), trimProductBound(I(span.poles[i].y()), weight), weight};
}

static std::vector<TrimHomogeneousPoint> elevatedTrimBezier(const ExactTrimSpan &span,
                                                          std::size_t count, bool reverse) {
    std::vector<TrimHomogeneousPoint> poles;
    for (std::size_t i = 0; i < span.poles.size(); ++i)
        poles.push_back(homogeneousTrimPole(span, reverse ? span.poles.size() - 1 - i : i));
    while (poles.size() < count) {
        const auto size = poles.size();
        // Elevation in homogeneous coordinates has one common divisor.
        // Keeping the numerator avoids rounding fractional coefficients.
        poles.emplace_back();
        for (std::size_t j = 0; j < poles.back().size(); ++j) {
            for (std::size_t i = size; i > 0; --i) {
                const I left = trimProductBound(I(double(i)), poles[i - 1][j]);
                const I right = i == size ? I() : trimProductBound(I(double(size - i)), poles[i][j]);
                poles[i][j] = trimSumBound(left, right);
            }
            poles.front()[j] = trimProductBound(I(double(size)), poles.front()[j]);
        }
    }
    return poles;
}

// Coefficients relative to t^i (1-t)^(n-i). Omitting the common
// Bernstein denominators preserves exact zeros and coefficient signs.
using TrimPolynomial = std::vector<I>;

static TrimPolynomial trimPolynomialProduct(const TrimPolynomial &a, const TrimPolynomial &b) {
    TrimPolynomial product(a.size() + b.size() - 1);
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            product[i + j] = trimSumBound(product[i + j], trimProductBound(a[i], b[j]));
    return product;
}

static bool equalTrimPolynomials(const TrimPolynomial &a, const TrimPolynomial &b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](I x, I y) {
        return surface_bounds::exact(trimDifferenceBound(x, y), 0.);
    });
}

// A positive projective parameter change multiplies the ith rational
// Bézier weight by a common scale times lambda^i, leaving every pole fixed.
static bool projectiveTrimCoincidence(const ExactTrimSpan &a, const ExactTrimSpan &b, bool reverse,
                                      std::size_t &work) {
    const auto count = a.poles.size();
    if (count < 2 || count != b.poles.size() || work < 4 * count) return false;
    work -= 4 * count;
    const auto opposed = [&](std::size_t i) { return reverse ? count - 1 - i : i; };
    for (std::size_t i = 0; i < count; ++i) {
        const auto j = opposed(i);
        if (!a.exactCartesian(i) || !b.exactCartesian(j) || !sameTrimPoint(a.poles[i], b.poles[j])) return false;
    }
    const I scaleA = a.weight(0), scaleB = b.weight(opposed(0));
    const I from = trimProductBound(a.weight(1), scaleB), to = trimProductBound(b.weight(opposed(1)), scaleA);
    if (!(from.lo > 0. && to.lo > 0.)) return false;
    I fromPower = from, toPower = to;
    for (std::size_t i = 2; i < count; ++i) {
        fromPower = trimProductBound(fromPower, from);
        toPower = trimProductBound(toPower, to);
        const I lhs = trimProductBound(trimProductBound(b.weight(opposed(i)), scaleA), fromPower);
        const I rhs = trimProductBound(trimProductBound(a.weight(i), scaleB), toPower);
        if (!surface_bounds::exact(trimDifferenceBound(lhs, rhs), 0.)) return false;
    }
    return true;
}

static std::array<TrimPolynomial, 3> trimHomogeneousPolynomials(const ExactTrimSpan &span, bool reverse) {
    std::array<TrimPolynomial, 3> result;
    const auto count = span.poles.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto pole = homogeneousTrimPole(span, reverse ? count - 1 - i : i);
        const I binomial(surface_bounds::choose(int(count - 1), int(i)));
        for (std::size_t j = 0; j < result.size(); ++j)
            result[j].push_back(trimProductBound(pole[j], binomial));
    }
    return result;
}

// Equality of Cartesian ratios allows any common positive polynomial
// factor in homogeneous coordinates. Compare both cross products over
// the aligned unit interval, keeping the native degrees.
static bool rationalTrimCoincidence(const ExactTrimSpan &a, const ExactTrimSpan &b, bool reverse,
                                    std::size_t &work) {
    const auto cost = 4 * a.poles.size() * b.poles.size();
    if (work < cost) return false;
    work -= cost;
    const auto left = trimHomogeneousPolynomials(a, false), right = trimHomogeneousPolynomials(b, reverse);
    for (std::size_t axis = 0; axis < 2; ++axis) {
        if (!equalTrimPolynomials(trimPolynomialProduct(left[axis], right[2]),
                                  trimPolynomialProduct(right[axis], left[2]))) return false;
    }
    return true;
}

// If one coordinate of A is affine, it determines the parameter on A.
// Monotone Cartesian poles of B in that coordinate certify orientation
// for positive weights. Check the other coordinate by exact composition.
static bool graphTrimCoincidence(const ExactTrimSpan &a, const ExactTrimSpan &b, bool reverse,
                                 std::size_t &work) {
    const auto degreeA = a.poles.size() - 1, degreeB = b.poles.size() - 1;
    const auto composedDegree = degreeA * degreeB;
    if (composedDegree + degreeB > 20) return false;
    const auto left = trimHomogeneousPolynomials(a, false), right = trimHomogeneousPolynomials(b, reverse);
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const double first = a.poles.front()[axis], last = a.poles.back()[axis];
        if (first == last) continue;
        const I sign(first < last ? 1. : -1.);
        bool monotone = true;
        double previous = sign.lo * first;
        for (std::size_t i = 0; i <= degreeB && monotone; ++i) {
            const auto j = reverse ? degreeB - i : i;
            const double current = sign.lo * b.poles[j][axis];
            monotone = b.exactCartesian(j) && current >= previous;
            previous = current;
        }
        if (!monotone ||
            !equalTrimPolynomials(trimPolynomialProduct(left[axis], {I(1.), I(1.)}),
                                  trimPolynomialProduct(left[2], {I(first), I(last)}))) continue;
        const auto cost = 8 * (degreeA + 1) * (composedDegree + 1) * (degreeB + 1);
        if (work < cost) return false;
        work -= cost;

        // Positive scale times W_B gives the common denominator. The
        // two polynomials below give the factors u and 1-u respectively.
        TrimPolynomial parameter(degreeB + 1), complement(degreeB + 1);
        for (std::size_t i = 0; i <= degreeB; ++i) {
            parameter[i] = trimProductBound(sign, trimDifferenceBound(right[axis][i],
                                                                     trimProductBound(I(first), right[2][i])));
            complement[i] = trimProductBound(sign, trimDifferenceBound(trimProductBound(I(last), right[2][i]),
                                                                      right[axis][i]));
        }
        std::vector<TrimPolynomial> powers{{I(1.)}}, complements{{I(1.)}};
        for (std::size_t i = 1; i <= degreeA; ++i) {
            powers.push_back(trimPolynomialProduct(powers.back(), parameter));
            complements.push_back(trimPolynomialProduct(complements.back(), complement));
        }
        const auto other = 1 - axis;
        TrimPolynomial numerator(composedDegree + 1), weight(composedDegree + 1);
        for (std::size_t i = 0; i <= degreeA; ++i) {
            const auto basis = trimPolynomialProduct(powers[i], complements[degreeA - i]);
            for (std::size_t j = 0; j < basis.size(); ++j) {
                numerator[j] = trimSumBound(numerator[j], trimProductBound(left[other][i], basis[j]));
                weight[j] = trimSumBound(weight[j], trimProductBound(left[2][i], basis[j]));
            }
        }
        if (equalTrimPolynomials(trimPolynomialProduct(numerator, right[2]),
                                 trimPolynomialProduct(weight, right[other]))) return true;
    }
    return false;
}

static bool oppositeSplineTrim(const ExactTrimSpan &a, const ExactTrimSpan &b, std::size_t &work) {
    if (!a.curve || !b.curve || !sameTrimPoint(a.first(), b.last()) || !sameTrimPoint(a.last(), b.first()))
        return false;
    if (!a.bezierPoleOffset || !b.bezierPoleOffset)
        return a.forward != b.forward && a.curve->degree() == b.curve->degree() &&
            a.poles.size() == b.poles.size() && a.curve->knots() == b.curve->knots() &&
            a.curve->weights() == b.curve->weights() &&
            std::equal(a.poles.begin(), a.poles.end(), b.poles.begin(), sameTrimPoint);

    // Identical native data can prove equality even if extracted
    // coefficients are only known through enclosures.
    if (a.intervalRestriction || b.intervalRestriction)
        return a.forward != b.forward && sameNativeTrimCell(a, b) &&
               sameLocalTrimParameter(a.range.lo, b.range.lo, a.bezierDomain(), b.bezierDomain()) &&
               sameLocalTrimParameter(a.range.hi, b.range.hi, a.bezierDomain(), b.bezierDomain());

    const bool reverse = a.forward == b.forward;
    if (a.poles.size() == b.poles.size()) {
        bool identical = true;
        for (std::size_t i = 0; i < a.poles.size() && identical; ++i) {
            const auto j = reverse ? b.poles.size() - 1 - i : i;
            if (!a.restriction.empty() && !b.restriction.empty()) {
                const auto &first = a.restriction[i].homogeneous, &second = b.restriction[j].homogeneous;
                identical = std::equal(first.begin(), first.end(), second.begin(), [](I x, I y) { return x.lo == y.lo; });
            } else {
                identical = a.exactCartesian(i) && b.exactCartesian(j) &&
                            a.weight(i).lo == b.weight(j).lo && sameTrimPoint(a.poles[i], b.poles[j]);
            }
        }
        if (identical) return true;
    }

    const auto count = std::max(a.poles.size(), b.poles.size());
    const auto degreeDifference = count - std::min(a.poles.size(), b.poles.size());
    const auto cost = 3 * count * (1 + degreeDifference);
    if (count > 9 || work < cost) return false;
    work -= cost;
    const auto first = elevatedTrimBezier(a, count, false);
    const auto second = elevatedTrimBezier(b, count, reverse);
    const I wa = first.front()[2], wb = second.front()[2];
    bool proportional = wa.lo > 0. && wb.lo > 0.;
    for (std::size_t i = 0; i < count && proportional; ++i)
        for (std::size_t j = 0; j < first[i].size() && proportional; ++j) {
            const I lhs = trimProductBound(first[i][j], wb);
            const I rhs = trimProductBound(second[i][j], wa);
            proportional = surface_bounds::exact(trimDifferenceBound(lhs, rhs), 0.);
        }
    return proportional || projectiveTrimCoincidence(a, b, reverse, work) ||
           rationalTrimCoincidence(a, b, reverse, work) ||
           graphTrimCoincidence(a, b, reverse, work) || graphTrimCoincidence(b, a, reverse, work);
}

static void appendInscribedSplinePair(const std::vector<ExactTrimSpan> &a, const std::vector<ExactTrimSpan> &b,
                                      bool partial, std::vector<ConvexTrim> &inner,
                                      std::vector<std::vector<ExactTrimSpan>> &residual, std::size_t &work) {
    for (std::size_t k = 0; k < a.size(); ++k) for (std::size_t m = 0; m < b.size(); ++m) {
        cancellationPoint();
        const auto limit = partial ? std::min(a.size(), b.size()) : 1;
        std::size_t shared = 0;
        for (; shared < limit; ++shared) {
            const auto &spanA = a[(k + shared) % a.size()];
            const auto &spanB = b[(m + b.size() - shared) % b.size()];
            const auto cost = spanA.poles.size() + spanB.poles.size();
            if (work < cost) {
                work = 0;
                return;
            }
            work -= cost;
            if (!oppositeSplineTrim(spanA, spanB, work)) break;
        }
        if (!shared) continue;
        const auto count = a.size() + b.size() - 2 * shared;
        if (count < 3 || work < count * count) continue;
        // Removing the shared chain from both loops preserves their total
        // winding. Certify the remaining closed perimeter before using it.
        std::vector<ExactTrimSpan> perimeter;
        perimeter.reserve(count);
        for (std::size_t n = shared; n < a.size(); ++n) perimeter.push_back(a[(k + n) % a.size()]);
        for (std::size_t n = 1; n <= b.size() - shared; ++n) perimeter.push_back(b[(m + n) % b.size()]);
        if (auto region = inscribedTrim(perimeter, work)) {
            inner.push_back(std::move(*region));
        } else {
            appendInscribedSplineTriangles(perimeter, inner, work);
            residual.push_back(std::move(perimeter));
        }
    }
}

// Extract the unsplit counterpart of a declared partial trim. Only attempt
// this after the inexpensive saturated-cell proof has had its budget.
static std::vector<ExactTrimSpan> extractUnsplitTrim(const std::vector<ExactTrimSpan> &loop,
    const std::vector<ExactTrimSpan> &opposite, const Frame3 &chart, std::size_t &work) {
    std::vector<ExactTrimSpan> result;
    bool changed = false;
    for (const auto &span : loop) {
        const auto begin = result.size();
        if (span.curve && !span.bezierPoleOffset) {
            const auto &knots = span.curve->knots();
            for (const auto &other : opposite) {
                cancellationPoint();
                if (!work) return {};
                --work;
                if (!other.bezierPoleOffset || other.restriction.empty() || other.forward == span.forward ||
                    other.curve->degree() != span.curve->degree() || other.curve->knots().size() != knots.size())
                    continue;
                if (work < knots.size()) return {};
                work -= knots.size();
                if (knots != other.curve->knots()) continue;
                if (!appendExactTrimCells(span, span.range, chart, work, result)) return {};
                changed = true;
                break;
            }
        }
        if (result.size() == begin) result.push_back(span);
    }
    if (!changed) result.clear();
    return result;
}

static std::optional<double> correspondingTrimParameter(double parameter, Interval from, Interval to) {
    if (from.lo == to.lo && from.hi == to.hi) return parameter;
    if (parameter == from.lo) return to.lo;
    if (parameter == from.hi) return to.hi;
    const auto normalized = exactTrimParameter(parameter, from);
    const auto width = exactTrimSum(to.hi, -to.lo);
    if (!normalized || !width) return {};
    const auto offset = exactTrimProduct(*normalized, *width);
    return offset ? exactTrimSum(to.lo, *offset) : std::nullopt;
}

// Use declared opposing cuts in the same native basis, or map them
// exactly between the whole parameter ranges. Coefficients still prove
// coincidence independently of this choice of cuts.
static std::vector<ExactTrimSpan> alignBezierTrim(const std::vector<ExactTrimSpan> &loop,
    const std::vector<ExactTrimSpan> &opposite, const Frame3 &chart, std::size_t &work,
    bool requireNativeCell) {
    std::vector<ExactTrimSpan> result;
    bool changed = false;
    for (const auto &span : loop) {
        std::vector<double> cuts;
        if (span.bezierPoleOffset) for (const auto &other : opposite) {
            cancellationPoint();
            if (!work) return {};
            --work;
            if (!other.bezierPoleOffset || other.forward == span.forward) continue;
            double first = other.range.lo, last = other.range.hi;
            if (!requireNativeCell) {
                const auto own = span.curve->domain(), opposing = other.curve->domain();
                if (own.lo != opposing.lo || own.hi != opposing.hi) {
                    const auto &poles = span.curve->poles(), &opposingPoles = other.curve->poles();
                    const auto start = exactTrimProjection(poles.front(), chart);
                    const auto end = exactTrimProjection(poles.back(), chart);
                    const auto opposingStart = exactTrimProjection(opposingPoles.front(), chart);
                    const auto opposingEnd = exactTrimProjection(opposingPoles.back(), chart);
                    if (!start || !end || !opposingStart || !opposingEnd ||
                        !sameTrimPoint(*start, *opposingStart) || !sameTrimPoint(*end, *opposingEnd)) continue;
                }
                const auto mappedFirst = correspondingTrimParameter(first, opposing, own);
                const auto mappedLast = correspondingTrimParameter(last, opposing, own);
                if (!mappedFirst || !mappedLast) continue;
                first = *mappedFirst;
                last = *mappedLast;
            }
            if (last <= span.range.lo || first >= span.range.hi ||
                (first <= span.range.lo && last >= span.range.hi)) continue;
            if (requireNativeCell) {
                const auto a = span.bezierDomain(), b = other.bezierDomain();
                if (a.lo != b.lo || a.hi != b.hi) continue;
                const auto cost = 3 * span.poles.size();
                if (work < cost) return {};
                work -= cost;
                if (!sameNativeTrimCell(span, other)) continue;
            }
            if (first > span.range.lo) cuts.push_back(first);
            if (last < span.range.hi) cuts.push_back(last);
        }
        if (cuts.empty()) {
            result.push_back(span);
            continue;
        }
        changed = true;
        cuts.push_back(span.range.lo);
        cuts.push_back(span.range.hi);
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        for (std::size_t i = 1; i < cuts.size(); ++i) {
            const auto j = span.forward ? i : cuts.size() - i;
            auto piece = span;
            if (!extractBezierTrim(piece, {cuts[j - 1], cuts[j]}, chart, work)) return {};
            result.push_back(std::move(piece));
        }
    }
    if (!changed) result.clear();
    return result;
}

static void appendInscribedSplineUnions(const std::vector<std::vector<ExactTrimSpan>> &loops,
    const Frame3 &chart, std::vector<ConvexTrim> &inner,
    std::vector<std::vector<ExactTrimSpan>> &residual, std::size_t &work) {
    for (std::size_t i = 0; i < loops.size() && work; ++i)
        for (std::size_t j = 0; j < i && work; ++j)
            appendInscribedSplinePair(loops[i], loops[j], false, inner, residual, work);

    if (loops.size() < 2 || !work) return;
    // Reserve part of the budget for the subsequent strip coverage proof.
    const auto allowance = work / 2;
    auto remaining = allowance;
    std::vector<std::vector<ExactTrimSpan>> split;
    for (const auto &loop : loops) split.push_back(splitSplineTrim(loop, remaining));
    for (std::size_t i = 0; i < loops.size() && remaining; ++i)
        for (std::size_t j = 0; j < i && remaining; ++j) {
            if (split[i].empty() && split[j].empty()) continue;
            appendInscribedSplinePair(split[i].empty() ? loops[i] : split[i],
                                      split[j].empty() ? loops[j] : split[j], true, inner, residual, remaining);
        }
    const auto hasInteriorCut = [](const std::vector<ExactTrimSpan> &loop) {
        return std::any_of(loop.begin(), loop.end(), [](const auto &span) { return !span.restriction.empty(); });
    };
    for (std::size_t i = 0; i < loops.size() && remaining; ++i)
        for (std::size_t j = 0; j < i && remaining; ++j) {
            const auto &baseA = split[i].empty() ? loops[i] : split[i];
            const auto &baseB = split[j].empty() ? loops[j] : split[j];
            if (!hasInteriorCut(baseA) && !hasInteriorCut(baseB)) continue;
            const auto cellsA = extractUnsplitTrim(baseA, baseB, chart, remaining);
            const auto cellsB = extractUnsplitTrim(baseB, baseA, chart, remaining);
            const auto &a = cellsA.empty() ? baseA : cellsA;
            const auto &b = cellsB.empty() ? baseB : cellsB;
            const auto alignedA = alignBezierTrim(a, b, chart, remaining, true);
            const auto alignedB = alignBezierTrim(b, a, chart, remaining, true);
            if (!cellsA.empty() || !cellsB.empty() || !alignedA.empty() || !alignedB.empty() ||
                (hasInteriorCut(a) && hasInteriorCut(b)))
                appendInscribedSplinePair(alignedA.empty() ? a : alignedA, alignedB.empty() ? b : alignedB,
                                          true, inner, residual, remaining);
        }
    // Different knot layouts may still describe the same curve. Extract
    // the remaining native cells and align their declared parameter cuts.
    if (remaining) {
        std::vector<std::vector<ExactTrimSpan>> extracted(loops.size());
        for (std::size_t i = 0; i < loops.size() && remaining; ++i)
            extracted[i] = splitSplineTrim(split[i].empty() ? loops[i] : split[i], remaining, &chart);
        const auto cells = [&](std::size_t i) -> const std::vector<ExactTrimSpan> & {
            if (!extracted[i].empty()) return extracted[i];
            return split[i].empty() ? loops[i] : split[i];
        };
        for (std::size_t i = 0; i < loops.size() && remaining; ++i)
            for (std::size_t j = 0; j < i && remaining; ++j) {
                const auto &a = cells(i), &b = cells(j);
                const auto alignedA = alignBezierTrim(a, b, chart, remaining, false);
                const auto alignedB = alignBezierTrim(b, a, chart, remaining, false);
                if (!alignedA.empty() || !alignedB.empty() || !extracted[i].empty() || !extracted[j].empty())
                    appendInscribedSplinePair(alignedA.empty() ? a : alignedA, alignedB.empty() ? b : alignedB,
                                              true, inner, residual, remaining);
            }
    }
    work -= allowance - remaining;
}

using TrimBezier = std::vector<TrimHomogeneousPoint>;

static TrimHomogeneousPoint blendTrimPoles(const TrimHomogeneousPoint &a, const TrimHomogeneousPoint &b, I t) {
    const I complement = trimDifferenceBound(I(1.), t);
    TrimHomogeneousPoint point;
    for (std::size_t i = 0; i < point.size(); ++i)
        point[i] = trimSumBound(trimProductBound(a[i], complement), trimProductBound(b[i], t));
    return point;
}

// Polar de Boor extracts the Bernstein coefficients of a closed knot
// span from the original homogeneous B-spline data with outward rounding.
static bool appendOpeningBezier(const ExactTrimSpan &span, std::vector<TrimBezier> &out, std::size_t &work) {
    if (!span.curve) {
        if (!work) return false;
        --work;
        const auto &a = span.first(), &b = span.last();
        out.push_back({{I(a.x()), I(a.y()), I(1)}, {I(b.x()), I(b.y()), I(1)}});
        return true;
    }
    if (span.bezierPoleOffset) {
        if (span.poles.size() > 9 || work < span.poles.size()) return false;
        work -= span.poles.size();
        auto &bezier = out.emplace_back();
        for (std::size_t i = 0; i < span.poles.size(); ++i) {
            const auto j = span.forward ? i : span.poles.size() - 1 - i;
            bezier.push_back(homogeneousTrimPole(span, j));
        }
        return true;
    }
    const auto &curve = *span.curve;
    const auto &knots = curve.knots();
    const int degree = curve.degree(), count = int(span.poles.size());
    if (degree > 8 || !curve.isClamped()) return false;
    const auto width = std::size_t(degree + 1);
    const auto begin = out.size();
    for (int cell = degree; cell < count; ++cell) {
        cancellationPoint();
        const double lo = knots[std::size_t(cell)], hi = knots[std::size_t(cell + 1)];
        if (lo == hi) continue;
        if (work < width * width * width) return false;
        work -= width * width * width;
        TrimBezier original(width);
        for (int j = 0; j <= degree; ++j) {
            const int index = cell - degree + j;
            original[std::size_t(j)] = homogeneousTrimPole(span, std::size_t(index));
        }
        TrimBezier bezier(width);
        for (int pole = 0; pole <= degree; ++pole) {
            auto values = original;
            for (int r = 1; r <= degree; ++r) {
                const double t = r <= degree - pole ? lo : hi;
                for (int j = degree; j >= r; --j) {
                    const double a = knots[std::size_t(cell - degree + j)];
                    const double b = knots[std::size_t(cell + j - r + 1)];
                    const I alpha = t == a ? I(0) : t == b ? I(1) : (I(t) - I(a)) / (I(b) - I(a));
                    values[std::size_t(j)] = blendTrimPoles(values[std::size_t(j - 1)], values[std::size_t(j)], alpha);
                }
            }
            bezier[std::size_t(pole)] = values.back();
        }
        if (!span.forward) std::reverse(bezier.begin(), bezier.end());
        out.push_back(std::move(bezier));
    }
    if (!span.forward) std::reverse(out.begin() + begin, out.end());
    return out.size() > begin;
}

static std::pair<TrimBezier, TrimBezier> splitTrimBezier(TrimBezier poles) {
    TrimBezier left(poles.size()), right(poles.size());
    left.front() = poles.front();
    right.back() = poles.back();
    for (std::size_t level = 1; level < poles.size(); ++level) {
        const auto last = poles.size() - level - 1;
        for (std::size_t i = 0; i <= last; ++i)
            poles[i] = blendTrimPoles(poles[i], poles[i + 1], I(.5));
        left[level] = poles.front();
        right[last] = poles[last];
    }
    return {std::move(left), std::move(right)};
}

// The rational piece and its endpoint chord both lie in the Cartesian
// control box. When this box misses the rectangle about the witness,
// the straight homotopy to the chord avoids it. Count the chord's signed ray
// crossing using certified endpoint signs, including half-open vertices.
static bool accumulateTrimWinding(TrimBezier poles, const Vec2 &point, const Vec2 &margin, int axis,
                                  int depth, std::size_t &work, int &winding) {
    cancellationPoint();
    if (work < poles.size()) return false;
    work -= poles.size();
    std::array<I, 2> box{{{INFINITY, -INFINITY}, {INFINITY, -INFINITY}}};
    std::array<I, 2> endpoint;
    for (std::size_t i = 0; i < poles.size(); ++i) {
        for (int k = 0; k < 2; ++k) {
            const I offset = poles[i][k] / poles[i][2] - I(point[k]);
            if (!surface_bounds::finite(offset)) return false;
            box[k].lo = std::min(box[k].lo, offset.lo);
            box[k].hi = std::max(box[k].hi, offset.hi);
            if (k != axis && (i == 0 || i + 1 == poles.size())) endpoint[i != 0] = offset;
        }
    }
    const auto across = box[1 - axis];
    if (across.lo > margin[1 - axis] || across.hi < -margin[1 - axis] || box[axis].hi < -margin[axis])
        return true;
    if (box[axis].lo > margin[axis]) {
        for (const I value : endpoint)
            if (value.lo <= 0. && value.hi > 0.) return false;
        winding += int(endpoint[1].lo > 0.) - int(endpoint[0].lo > 0.);
        return true;
    }
    if (depth == 16 || work < poles.size() * poles.size()) return false;
    work -= poles.size() * poles.size();
    auto [left, right] = splitTrimBezier(std::move(poles));
    return accumulateTrimWinding(std::move(left), point, margin, axis, depth + 1, work, winding) &&
           accumulateTrimWinding(std::move(right), point, margin, axis, depth + 1, work, winding);
}

static std::optional<int> certifiedTrimWinding(const std::vector<TrimBezier> &boundary, const Vec2 &point,
                                              const Vec2 &margin, std::size_t &work) {
    if (boundary.empty()) return {};
    for (int axis = 0; axis < 2 && work; ++axis) {
        int winding = 0;
        const bool proved = std::all_of(boundary.begin(), boundary.end(), [&](const auto &piece) {
            return accumulateTrimWinding(piece, point, margin, axis, 0, work, winding);
        });
        if (proved) return axis == 0 ? winding : -winding;
    }
    return {};
}

static std::vector<TrimBezier> prepareTrimWindingBoundary(const std::vector<ExactTrimSpan> &perimeter,
                                                         std::size_t &work) {
    if (work < perimeter.size()) return {};
    work -= perimeter.size();
    std::vector<TrimBezier> boundary;
    for (std::size_t i = 0; i < perimeter.size(); ++i) {
        cancellationPoint();
        if (!sameTrimPoint(perimeter[i].last(), perimeter[(i + 1) % perimeter.size()].first()) ||
            !appendOpeningBezier(perimeter[i], boundary, work))
            return {};
    }
    return boundary;
}

static std::vector<std::vector<TrimBezier>> prepareResidualTrimBoundaries(
    const std::vector<std::vector<ExactTrimSpan>> &residual, std::size_t &work) {
    std::vector<std::vector<TrimBezier>> result;
    for (const auto &perimeter : residual) {
        cancellationPoint();
        if (!work) break;
        auto boundary = prepareTrimWindingBoundary(perimeter, work);
        if (!boundary.empty()) result.push_back(std::move(boundary));
    }
    return result;
}

struct TrimEnclosureSide {
    Vec2 first, last;
    double minimum;
};

struct ResidualTrimSource {
    std::optional<TrimDisk> disk;
    std::vector<TrimEnclosureSide> sides;
    std::vector<TrimBezier> outerBoundary;
};

static I enclosureSideValue(const Vec2 &a, const Vec2 &b, const TrimHomogeneousPoint &pole) {
    const I dx = trimDifferenceBound(I(b.x()), I(a.x()));
    const I dy = trimDifferenceBound(I(b.y()), I(a.y()));
    const I x = trimDifferenceBound(pole[0], trimProductBound(I(a.x()), pole[2]));
    const I y = trimDifferenceBound(pole[1], trimProductBound(I(a.y()), pole[2]));
    return trimDifferenceBound(trimProductBound(dx, y), trimProductBound(dy, x));
}

static std::vector<TrimEnclosureSide> trimPoleEnclosure(const std::vector<Vec2> &poles, std::size_t &work) {
    // Reserve most of the remaining budget for the winding and box tests.
    const auto allowance = work / 4;
    const auto count = poles.size();
    std::size_t cost = 6 * count;
    for (auto width = count; width > 1; width = (width + 1) / 2) cost += count;
    if (count < 3 || cost > allowance) return {};
    work -= cost;
    if (std::any_of(poles.begin(), poles.end(), [](const Vec2 &p) { return !isFinite(p); })) return {};

    auto sorted = poles;
    std::sort(sorted.begin(), sorted.end(), [](const Vec2 &a, const Vec2 &b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    });
    sorted.erase(std::unique(sorted.begin(), sorted.end(), sameTrimPoint), sorted.end());
    if (sorted.size() < 3) return {};
    std::vector<Vec2> outline;
    const auto append = [&](const Vec2 &point, std::size_t base) {
        while (outline.size() > base &&
               trimHalfPlane(outline[outline.size() - 2], outline.back(), point).side.lo <= 0.)
            outline.pop_back();
        outline.push_back(point);
    };
    for (const auto &point : sorted) append(point, 1);
    const auto lowerSize = outline.size();
    for (auto point = sorted.rbegin() + 1; point != sorted.rend(); ++point) append(*point, lowerSize);
    outline.pop_back();

    // The chain only chooses useful directions. Certify each support
    // offset against every pole, including corners of interval poles.
    std::vector<TrimEnclosureSide> result;
    for (std::size_t i = 0; i < outline.size() && allowance - cost >= count; ++i) {
        cancellationPoint();
        cost += count;
        work -= count;
        const auto &a = outline[i], &b = outline[(i + 1) % outline.size()];
        double lower = INFINITY;
        for (const auto &point : poles)
            lower = std::min(lower, enclosureSideValue(a, b, {I(point.x()), I(point.y()), I(1.)}).lo);
        if (std::isfinite(lower)) result.push_back({a, b, lower});
    }
    return result;
}

static bool residualBoxOutsideSource(const TrimWindow &box, const ResidualTrimSource &source) {
    if (source.disk) {
        const auto &disk = *source.disk;
        const Vec2 closest(std::clamp(disk.center.x(), box.lo.x(), box.hi.x()),
                           std::clamp(disk.center.y(), box.lo.y(), box.hi.y()));
        return trimDistanceSquared(disk.center, closest).lo > trimSquare(I(disk.radius)).hi;
    }
    for (const auto &side : source.sides) {
        const auto &a = side.first, &b = side.last;
        // The affine side expression attains its maximum at this corner.
        const Vec2 corner(b.y() <= a.y() ? box.hi.x() : box.lo.x(),
                          b.x() >= a.x() ? box.hi.y() : box.lo.y());
        if (trimHalfPlane(a, b, corner).side.hi < side.minimum) return true;
    }
    return false;
}

static bool residualTrimCovers(const TrimWindow &box, const ResidualTrimSource &source,
                               const std::vector<std::vector<TrimBezier>> &boundaries,
                               unsigned depth, std::size_t &work) {
    cancellationPoint();
    const auto cost = 1 + source.sides.size();
    if (work < cost) return false;
    work -= cost;
    if (residualBoxOutsideSource(box, source)) return true;
    const Vec2 center = .5 * box.lo + .5 * box.hi;
    Vec2 margin;
    for (int i = 0; i < 2; ++i) {
        if (!(box.lo[i] < center[i] && center[i] < box.hi[i])) return false;
        margin[i] = std::max((I(center[i]) - I(box.lo[i])).hi, (I(box.hi[i]) - I(center[i])).hi);
    }
    // A box that misses the residual boundary has constant winding, equal
    // to the sum of the two original windings.
    if (std::any_of(boundaries.begin(), boundaries.end(), [&](const auto &boundary) {
            return certifiedTrimWinding(boundary, center, margin, work).value_or(0) > 0;
        }))
        return true;
    // Zero winding on a box separated from the source outer boundary
    // excludes the entire box, even inside the control-pole enclosure.
    if (!source.outerBoundary.empty() &&
        certifiedTrimWinding(source.outerBoundary, center, margin, work) == 0)
        return true;
    if (depth == 0 || (!source.disk && source.sides.empty() && source.outerBoundary.empty())) return false;
    // The children cover the closed parent using the same split coordinates
    // on both sides. Every child must be covered or disjoint from material.
    const std::array<TrimWindow, 4> children{
        TrimWindow{box.lo, center},
        TrimWindow{{center.x(), box.lo.y()}, {box.hi.x(), center.y()}},
        TrimWindow{{box.lo.x(), center.y()}, {center.x(), box.hi.y()}},
        TrimWindow{center, box.hi}
    };
    return std::all_of(children.begin(), children.end(), [&](const auto &child) {
        return residualTrimCovers(child, source, boundaries, depth - 1, work);
    });
}

static bool quadraticTrimNonnegative(double a, double b, double c) {
    if (a < 0. || c < 0.) return false;
    if (b >= 0.) return true;
    // a*(1-t)^2+b*t*(1-t)+c*t^2 is nonnegative for
    // a,c >= 0 and b >= -2*sqrt(a*c), including an interior zero.
    const I determinant = trimDifferenceBound(trimProductBound(I(4.), trimProductBound(I(a), I(c))),
                                              trimProductBound(I(b), I(b)));
    return determinant.lo >= 0.;
}

using TrimIntegerPolynomial = std::vector<std::int64_t>;
// Products, four-term convolutions, and differences fit in int64_t
// when the coefficients entering each operation satisfy this limit.
constexpr std::int64_t trimIntegerLimit = std::int64_t(1) << 30;

static bool reduceTrimIntegerPolynomial(TrimIntegerPolynomial &remainder, TrimIntegerPolynomial &quotient) {
    std::int64_t common = 0;
    for (auto value : remainder) common = std::gcd(common, value);
    for (auto value : quotient) common = std::gcd(common, value);
    if (common > 1) {
        for (auto &value : remainder) value /= common;
        for (auto &value : quotient) value /= common;
    }
    while (!remainder.empty() && remainder.back() == 0) remainder.pop_back();
    const auto bounded = [](std::int64_t value) { return -trimIntegerLimit <= value && value <= trimIntegerLimit; };
    return std::all_of(remainder.begin(), remainder.end(), bounded) &&
           std::all_of(quotient.begin(), quotient.end(), bounded);
}

static bool pseudoDivideTrimPolynomial(TrimIntegerPolynomial &remainder, const TrimIntegerPolynomial &divisor,
                                       TrimIntegerPolynomial &quotient) {
    const auto lead = divisor.back();
    while (remainder.size() >= divisor.size()) {
        const auto shift = remainder.size() - divisor.size();
        const auto pivot = remainder.back();
        for (auto &value : remainder) value *= lead;
        for (std::size_t i = 0; i < divisor.size(); ++i)
            remainder[shift + i] -= pivot * divisor[i];
        if (!quotient.empty()) {
            for (auto &value : quotient) value *= lead;
            quotient[shift] += pivot;
        }
        if (!reduceTrimIntegerPolynomial(remainder, quotient)) return false;
    }
    return true;
}

// On z=t/(1-t) >= 0 the scaled Bernstein coefficients are power
// coefficients, up to the positive factor (1-t)^degree. A positive
// scale of their lower bounds gives an exact integer lower polynomial.
static std::optional<TrimIntegerPolynomial> trimIntegerLowerPolynomial(const TrimPolynomial &coefficients) {
    if (coefficients.front().lo < 0. || coefficients.back().lo < 0.) return {};
    double largest = 0.;
    for (I value : coefficients) largest = std::max(largest, std::abs(value.lo));
    if (!(largest > 0.)) return {};
    // Recover the dyadic coefficients before removing their common
    // divisor. The primitive polynomial can be much smaller.
    constexpr int digits = std::numeric_limits<double>::digits;
    const int exponent = digits - 1 - std::ilogb(largest);
    const double conversionLimit = std::ldexp(1., digits);
    TrimIntegerPolynomial polynomial;
    for (I value : coefficients) {
        const double scaled = std::scalbn(value.lo, exponent);
        if (std::abs(scaled) >= conversionLimit || scaled != std::trunc(scaled) ||
            std::scalbn(scaled, -exponent) != value.lo) return {};
        polynomial.push_back(std::int64_t(scaled));
    }
    TrimIntegerPolynomial discarded;
    if (!reduceTrimIntegerPolynomial(polynomial, discarded) || polynomial.size() < 3) return {};

    return polynomial;
}

static bool nonnegativeWithTrimFactor(TrimIntegerPolynomial polynomial, const TrimIntegerPolynomial &factor) {
    TrimIntegerPolynomial square(2 * factor.size() - 1), discarded;
    for (std::size_t i = 0; i < factor.size(); ++i)
        for (std::size_t j = 0; j < factor.size(); ++j)
            square[i + j] += factor[i] * factor[j];
    if (!reduceTrimIntegerPolynomial(square, discarded) || square.size() > polynomial.size()) return false;
    TrimIntegerPolynomial quotient(polynomial.size() - square.size() + 1);
    if (!pseudoDivideTrimPolynomial(polynomial, square, quotient) || !polynomial.empty()) return false;
    if (std::all_of(quotient.begin(), quotient.end(), [](auto value) { return value >= 0; })) return true;
    return quotient.size() == 3 &&
           quadraticTrimNonnegative(double(quotient[0]), double(quotient[1]), double(quotient[2]));
}

// The GCD with the derivative can recover rational and irrational
// contacts without calculating the contact parameter.
static bool nonnegativeWithRepeatedTrimFactor(const TrimIntegerPolynomial &polynomial) {
    TrimIntegerPolynomial discarded;
    auto factor = polynomial;
    TrimIntegerPolynomial derivative;
    for (std::size_t i = 1; i < polynomial.size(); ++i)
        derivative.push_back(std::int64_t(i) * polynomial[i]);
    if (!reduceTrimIntegerPolynomial(derivative, discarded)) return false;
    while (derivative.size() > 1) {
        if (!pseudoDivideTrimPolynomial(factor, derivative, discarded)) return false;
        factor.swap(derivative);
    }
    return derivative.empty() && factor.size() >= 2 && factor.size() <= 4 &&
           nonnegativeWithTrimFactor(polynomial, factor);
}

// The Euclidean remainders can exceed the integer cap on sextics.
// Continued fractions of stationary points propose rational factors,
// which still require exact division and a certified quotient sign.
static bool nonnegativeWithRationalTrimFactor(const TrimIntegerPolynomial &polynomial) {
    std::vector<double> bernstein;
    const int degree = int(polynomial.size()) - 1;
    for (int i = 0; i <= degree; ++i)
        bernstein.push_back(double(polynomial[i]) / surface_bounds::choose(degree, i));
    const auto stationary = BernsteinPolynomial(std::move(bernstein)).derivative().roots();
    // For q at this cap the coefficients of (q-p)*z-p squared still
    // fit the arithmetic used by the exact division.
    constexpr std::int64_t denominatorLimit = std::int64_t(1) << 15;
    for (double t : stationary) {
        if (!(t > 0. && t < 1.)) continue;
        std::array<std::int64_t, 2> previous{0, 1}, current{1, 0};
        for (double rest = t; std::isfinite(rest);) {
            const double integerPart = std::floor(rest);
            if (integerPart > double(denominatorLimit)) break;
            const auto digit = std::int64_t(integerPart);
            const auto p = digit * current[0] + previous[0];
            const auto q = digit * current[1] + previous[1];
            if (q > denominatorLimit) break;
            if (p > 0 && p < q && std::abs(t - double(p) / double(q)) <= 1e-11) {
                // t = p/q corresponds to z = p/(q-p).
                if (nonnegativeWithTrimFactor(polynomial, {-p, q - p})) return true;
                break;
            }
            const double fraction = rest - integerPart;
            if (fraction == 0.) break;
            previous = current;
            current = {p, q};
            rest = 1. / fraction;
        }
    }
    return false;
}

constexpr std::int64_t trimFactorPrime = 1000003;

static std::int64_t trimFactorResidue(std::int64_t value) {
    value %= trimFactorPrime;
    return value < 0 ? value + trimFactorPrime : value;
}

static std::int64_t trimFactorInverse(std::int64_t value) {
    std::int64_t inverse = 1;
    for (std::int64_t power = trimFactorPrime - 2; power > 0; power /= 2) {
        if (power % 2) inverse = trimFactorResidue(inverse * value);
        value = trimFactorResidue(value * value);
    }
    return inverse;
}

static std::optional<std::array<std::int64_t, 2>> reconstructTrimRatio(std::int64_t residue) {
    // 2 * limit^2 < prime keeps the reconstructed ratio unique.
    constexpr std::int64_t limit = 707;
    std::int64_t r0 = trimFactorPrime, r1 = residue, d0 = 0, d1 = 1;
    while (r1 > limit) {
        const auto quotient = r0 / r1;
        r0 = std::exchange(r1, r0 - quotient * r1);
        d0 = std::exchange(d1, d0 - quotient * d1);
    }
    if (d1 == 0 || std::abs(d1) > limit) return {};
    return std::array<std::int64_t, 2>{d1 < 0 ? -r1 : r1, std::abs(d1)};
}

// For degree eight the integer Euclidean remainders often outgrow
// the cap. A modular GCD can propose a small linear or quadratic
// factor. Exact division and the quotient sign remain mandatory.
static bool nonnegativeWithModularTrimFactor(const TrimIntegerPolynomial &polynomial) {
    auto factor = polynomial;
    for (auto &coefficient : factor) coefficient = trimFactorResidue(coefficient);
    if (factor.back() == 0) return false;
    TrimIntegerPolynomial derivative;
    for (std::size_t i = 1; i < factor.size(); ++i)
        derivative.push_back(trimFactorResidue(std::int64_t(i) * factor[i]));
    while (derivative.size() > 1) {
        const auto inverse = trimFactorInverse(derivative.back());
        while (factor.size() >= derivative.size()) {
            const auto shift = factor.size() - derivative.size();
            const auto scale = trimFactorResidue(factor.back() * inverse);
            for (std::size_t i = 0; i < derivative.size(); ++i)
                factor[i + shift] = trimFactorResidue(factor[i + shift] - scale * derivative[i]);
            while (!factor.empty() && factor.back() == 0) factor.pop_back();
        }
        factor.swap(derivative);
    }
    if (!derivative.empty() || factor.size() < 2 || factor.size() > 3) return false;
    const auto inverse = trimFactorInverse(factor.back());
    std::array<std::array<std::int64_t, 2>, 2> ratios{};
    std::int64_t denominator = 1;
    for (std::size_t i = 0; i + 1 < factor.size(); ++i) {
        const auto ratio = reconstructTrimRatio(trimFactorResidue(factor[i] * inverse));
        if (!ratio) return false;
        ratios[i] = *ratio;
        denominator = std::lcm(denominator, (*ratio)[1]);
    }
    for (std::size_t i = 0; i + 1 < factor.size(); ++i)
        factor[i] = ratios[i][0] * (denominator / ratios[i][1]);
    factor.back() = denominator;
    return nonnegativeWithTrimFactor(polynomial, factor);
}

static bool repeatedTrimContactNonnegative(const TrimPolynomial &coefficients, std::size_t &work) {
    constexpr std::array<std::size_t, 3> costs{128, 320, 512};
    const auto cost = costs[(coefficients.size() - 5) / 2];
    if (work < cost) return false;
    work -= cost;
    const auto polynomial = trimIntegerLowerPolynomial(coefficients);
    return polynomial &&
           ((coefficients.size() == 7 && nonnegativeWithRationalTrimFactor(*polynomial)) ||
            (coefficients.size() == 9 && nonnegativeWithModularTrimFactor(*polynomial)) ||
            nonnegativeWithRepeatedTrimFactor(*polynomial));
}

static bool trimBezierOutsideDisk(const TrimBezier &poles, const TrimDisk &disk, bool testRepeatedFactor, std::size_t &work) {
    const auto count = poles.size();
    const auto cost = 3 * count * count;
    if (count > 9 || work < cost) return false;
    work -= cost;
    TrimPolynomial x(count), y(count), radius(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto &pole = poles[i];
        const I factor(surface_bounds::choose(int(count - 1), int(i)));
        x[i] = trimProductBound(factor, trimDifferenceBound(pole[0], trimProductBound(I(disk.center.x()), pole[2])));
        y[i] = trimProductBound(factor, trimDifferenceBound(pole[1], trimProductBound(I(disk.center.y()), pole[2])));
        radius[i] = trimProductBound(factor, trimProductBound(I(disk.radius), pole[2]));
    }
    const auto xx = trimPolynomialProduct(x, x), yy = trimPolynomialProduct(y, y);
    const auto rr = trimPolynomialProduct(radius, radius);
    TrimPolynomial distance(xx.size());
    for (std::size_t i = 0; i < distance.size(); ++i) {
        distance[i] = trimDifferenceBound(trimSumBound(xx[i], yy[i]), rr[i]);
        if (!surface_bounds::finite(distance[i])) return false;
    }
    if (std::all_of(distance.begin(), distance.end(), [](I c) { return c.lo >= 0.; })) return true;
    if (distance.size() == 3)
        return quadraticTrimNonnegative(distance[0].lo, distance[1].lo, distance[2].lo);
    return testRepeatedFactor && distance.size() >= 5 && distance.size() <= 9 &&
           repeatedTrimContactNonnegative(distance, work);
}

// A rational Bézier stays in the closed exterior half-plane if all the
// homogeneous side numerators are nonpositive and the weights are positive.
static bool trimBezierOutsideInterior(const TrimBezier &poles, const TrimEnclosureSide &side) {
    return std::all_of(poles.begin(), poles.end(), [&](const auto &pole) {
        const I numerator = trimDifferenceBound(enclosureSideValue(side.first, side.last, pole),
                                               trimProductBound(I(side.minimum), pole[2]));
        return surface_bounds::finite(numerator) && numerator.hi <= 0.;
    });
}

constexpr unsigned trimSeparationDepth = 16;

static bool trimBezierOutsideSource(const TrimBezier &poles, const ResidualTrimSource &source,
                                    bool allowContact, unsigned depth, std::size_t &work) {
    cancellationPoint();
    const auto cost = poles.size() + source.sides.size();
    if (work < cost) return false;
    work -= cost;
    TrimWindow box;
    for (const auto &pole : poles) {
        if (!(pole[2].lo > 0.)) return false;
        const I x = pole[0] / pole[2], y = pole[1] / pole[2];
        if (!surface_bounds::finite(x) || !surface_bounds::finite(y)) return false;
        box.add(Vec2(x.lo, y.lo));
        box.add(Vec2(x.hi, y.hi));
    }
    if (residualBoxOutsideSource(box, source)) return true;
    if (allowContact && source.disk &&
        trimBezierOutsideDisk(poles, *source.disk, depth == trimSeparationDepth, work)) return true;
    if (allowContact) for (const auto &side : source.sides) {
        if (work < poles.size()) return false;
        work -= poles.size();
        if (trimBezierOutsideInterior(poles, side)) return true;
    }
    const auto splitCost = poles.size() * poles.size();
    if (depth == 0 || work < splitCost) return false;
    work -= splitCost;
    const auto [left, right] = splitTrimBezier(poles);
    return trimBezierOutsideSource(left, source, allowContact, depth - 1, work) &&
           trimBezierOutsideSource(right, source, allowContact, depth - 1, work);
}

static bool residualWindingCovers(const ResidualTrimSource &source, const std::vector<Vec2> &vertices,
                                  bool enclosed, const std::vector<std::vector<TrimBezier>> &boundaries,
                                  std::size_t &work) {
    Vec2 witness;
    bool allowContact = source.disk.has_value();
    if (source.disk) {
        witness = source.disk->center;
    } else {
        for (const auto &point : vertices)
            witness += point / double(vertices.size());
        allowContact = !source.sides.empty() && std::all_of(source.sides.begin(), source.sides.end(), [&](const auto &side) {
            return enclosureSideValue(side.first, side.last, {I(witness.x()), I(witness.y()), I(1.)}).lo > side.minimum;
        });
        if (!allowContact) {
            if (enclosed) return false;
            witness = vertices.front();
        }
    }
    // The closed disk or intersection of source half-planes has a
    // connected interior. Prove its winding constant, then take the
    // closure to include points of tangency.
    return std::any_of(boundaries.begin(), boundaries.end(), [&](const auto &boundary) {
        return certifiedTrimWinding(boundary, witness, Vec2(), work).value_or(0) > 0 &&
            std::all_of(boundary.begin(), boundary.end(), [&](const auto &piece) {
                return trimBezierOutsideSource(piece, source, allowContact, trimSeparationDepth, work);
            });
    });
}

static std::vector<Vec2> subdividedSourceHull(const std::vector<TrimBezier> &boundary, std::size_t &work) {
    std::vector<Vec2> corners;
    const auto append = [&](const TrimBezier &poles) {
        for (const auto &pole : poles) {
            const I x = trimQuotientBound(pole[0], pole[2]), y = trimQuotientBound(pole[1], pole[2]);
            if (!surface_bounds::finite(x) || !surface_bounds::finite(y)) return false;
            corners.emplace_back(x.lo, y.lo);
            if (x.lo != x.hi) corners.emplace_back(x.hi, y.lo);
            if (y.lo != y.hi) {
                corners.emplace_back(x.lo, y.hi);
                if (x.lo != x.hi) corners.emplace_back(x.hi, y.hi);
            }
        }
        return true;
    };
    for (const auto &piece : boundary) {
        cancellationPoint();
        const auto cost = piece.size() * (piece.size() + 8);
        if (cost > work) return {};
        work -= cost;
        if (piece.size() <= 2) {
            if (!append(piece)) return {};
        } else {
            const auto [left, right] = splitTrimBezier(piece);
            if (!append(left) || !append(right)) return {};
        }
    }
    return corners;
}

static bool residualTrimCoversSource(const ConvexTrim &trim, bool enclosed,
                                    const std::vector<std::vector<TrimBezier>> &boundaries,
                                    const Body &body, FaceId face, const Frame3 &chart, std::size_t &work) {
    const auto box = trimOutlineBounds(trim);
    if (!box.finite() || boundaries.empty()) return false;
    ResidualTrimSource source{trim.disk, {}, {}};
    if (enclosed) {
        source.sides = trimPoleEnclosure(trim.vertices, work);
    } else {
        for (std::size_t i = 0; i < trim.vertices.size(); ++i)
            source.sides.push_back({trim.vertices[i], trim.vertices[(i + 1) % trim.vertices.size()], 0.});
    }
    constexpr unsigned maxDepth = 6;
    if (residualTrimCovers(box, source, boundaries, maxDepth, work)) return true;
    if (!work) return false;
    if (!enclosed) return residualWindingCovers(source, trim.vertices, false, boundaries, work);
    const auto enclosureAllowance = work / 4;
    auto enclosureWork = enclosureAllowance;
    const bool covered = residualWindingCovers(source, trim.vertices, true, boundaries, enclosureWork);
    work -= enclosureAllowance - enclosureWork;
    if (covered) return true;

    // The first pass uses the inexpensive enclosure. Reuse the remaining
    // winding budget for a retry with the actual spline outer boundary.
    const auto &loops = body.face(face).loops;
    if (loops.empty()) return false;
    std::vector<ExactTrimSpan> spans;
    const auto allowance = std::min<std::size_t>(512, work);
    auto extractionWork = allowance;
    const bool extracted = collectExactTrimBoundary(body, face, loops.front(), chart, false, extractionWork, spans);
    work -= allowance - extractionWork;
    if (!extracted) return false;
    source.outerBoundary = prepareTrimWindingBoundary(spans, work);
    if (source.outerBoundary.empty()) return false;
    // Subdivision can expose the supporting line through an exact contact
    // even when the original control hull extends through that line.
    const auto subdivisionAllowance = work / 3;
    auto subdivisionWork = subdivisionAllowance;
    const auto poles = subdividedSourceHull(source.outerBoundary, subdivisionWork);
    const ResidualTrimSource refined{{}, trimPoleEnclosure(poles, subdivisionWork), {}};
    const bool refinedCoverage = !refined.sides.empty() &&
        residualWindingCovers(refined, poles, true, boundaries, subdivisionWork);
    work -= subdivisionAllowance - subdivisionWork;
    return refinedCoverage || residualTrimCovers(box, source, boundaries, maxDepth, work);
}

struct SplineTrimOpening {
    TrimWindow enclosure;
    std::optional<ConvexTrim> interior;
    std::vector<TrimBezier> boundary;

    bool contains(const Vec2 &point, double tolerance, std::size_t &work) const {
        if (interior) return convexTrimPoint(*interior, point, tolerance, true);
        if (boundary.empty() || point.x() <= enclosure.lo.x() || point.x() >= enclosure.hi.x() ||
            point.y() <= enclosure.lo.y() || point.y() >= enclosure.hi.y())
            return false;
        return certifiedTrimWinding(boundary, point, Vec2(tolerance, tolerance), work).value_or(0) > 0;
    }
};

struct PerforatedSplineTrim {
    std::optional<ConvexTrim> region;
    std::vector<SplineTrimOpening> openings;
};

static std::optional<PerforatedSplineTrim> collectPerforatedSplineTrim(const SurfaceTargetFace &target,
    const Frame3 &chart, std::size_t &splineWork, std::size_t &proofWork, std::size_t &openingWork) {
    const auto &body = *target.body;
    const auto &loops = body.face(target.face).loops;
    if (loops.size() < 2) return {};
    std::vector<ExactTrimSpan> outer;
    if (!collectExactTrimBoundary(body, target.face, loops.front(), chart, true, splineWork, outer))
        return {};
    PerforatedSplineTrim result{inscribedTrim(outer, proofWork), {}};
    bool hasSpline = false;
    for (std::size_t i = 1; i < loops.size(); ++i) {
        std::vector<ExactTrimSpan> hole;
        if (!collectExactTrimBoundary(body, target.face, loops[i], chart, true, splineWork, hole))
            return {};
        auto &opening = result.openings.emplace_back();
        for (const auto &span : hole) {
            hasSpline = hasSpline || span.curve;
            span.addPoleHull([&](const Vec2 &pole) { opening.enclosure.add(pole); });
        }
        // Reverse the inner loop to use the same outward-pole test as for
        // the external boundary. Its inscribed polygon describes void.
        std::reverse(hole.begin(), hole.end());
        for (auto &span : hole) span.forward = !span.forward;
        opening.interior = inscribedTrim(hole, proofWork);
        if (!opening.interior && !std::all_of(hole.begin(), hole.end(), [&](const auto &span) {
                return appendOpeningBezier(span, opening.boundary, openingWork);
            }))
            opening.boundary.clear();
    }
    if (!hasSpline) return {};
    return result;
}

static bool perforatedTrimIncludes(const PerforatedSplineTrim &support, const ConvexTrim &source,
                                   const std::vector<ConvexTrim> &other) {
    if (!support.region || !convexTrimIncludes(*support.region, source))
        return false;
    const auto outline = trimOutlineBounds(source);
    for (const auto &opening : support.openings) {
        cancellationPoint();
        const auto &box = opening.enclosure;
        if (!outline.overlaps(box, 0.)) continue;
        // A closed convex hole containing the box also contains every
        // positive-weight spline segment and all the void it bounds.
        ConvexTrim enclosure;
        enclosure.vertices = {box.lo, {box.hi.x(), box.lo.y()}, box.hi, {box.lo.x(), box.hi.y()}};
        if (std::any_of(source.holes.begin(), source.holes.end(), [&](const TrimDisk &hole) {
                return convexTrimIncludes(ConvexTrim{{}, hole, {}}, enclosure);
            }))
            continue;
        if (!std::any_of(other.begin(), other.end(), [&](const ConvexTrim &plug) {
                return convexTrimIncludes(plug, enclosure);
            }))
            return false;
    }
    return true;
}

static bool splineOpeningExposesMaterial(const ConvexTrim &source, const std::vector<PerforatedSplineTrim> &perforated,
    const std::vector<ConvexTrim> &other, double tolerance, std::size_t &work, Vec2 *gapCenter) {
    const auto outline = trimOutlineBounds(source);
    std::size_t candidateCost = 1 + source.holes.size() + source.vertices.size();
    for (const auto &target : other)
        candidateCost += 1 + target.holes.size() + target.vertices.size();
    for (const auto &target : perforated)
        for (const auto &hole : target.openings)
            candidateCost += 1 + (hole.interior ? hole.interior->vertices.size() : 0);
    const auto provesGap = [&](const Vec2 &point) {
        cancellationPoint();
        if (!isFinite(point) || !convexTrimPoint(source, point, tolerance, true) ||
            !std::all_of(other.begin(), other.end(), [&](const ConvexTrim &target) {
                return convexTrimPoint(target, point, tolerance, false);
            }) ||
            !std::all_of(perforated.begin(), perforated.end(), [&](const PerforatedSplineTrim &target) {
                return std::any_of(target.openings.begin(), target.openings.end(), [&](const SplineTrimOpening &hole) {
                    return hole.contains(point, tolerance, work);
                });
            }))
            return false;
        if (gapCenter) *gapCenter = point;
        return true;
    };
    for (const auto &support : perforated) for (const auto &opening : support.openings) {
        if (opening.interior) {
            const auto &vertices = opening.interior->vertices;
            Vec2 center;
            for (const auto &p : vertices) center += p / double(vertices.size());
            if (provesGap(center) || std::any_of(vertices.begin(), vertices.end(), [&](const Vec2 &vertex) {
                    return provesGap(.875 * vertex + .125 * center);
                }))
                return true;
        } else if (!opening.boundary.empty()) {
            const auto &enclosure = opening.enclosure;
            const Vec2 lo(std::max(outline.lo.x(), enclosure.lo.x()), std::max(outline.lo.y(), enclosure.lo.y()));
            const Vec2 hi(std::min(outline.hi.x(), enclosure.hi.x()), std::min(outline.hi.y(), enclosure.hi.y()));
            if (lo.x() >= hi.x() || lo.y() >= hi.y()) continue;
            // Candidates only locate the uncovered point; winding and
            // clearance are proved for the continuous boundary.
            for (int divisions : {2, 4, 8}) {
                for (int y = 1; y < divisions && work >= candidateCost; ++y)
                    for (int x = 1; x < divisions && work >= candidateCost; ++x) {
                        if (x % 2 == 0 && y % 2 == 0) continue;
                        work -= candidateCost;
                        const double u = double(x) / divisions, v = double(y) / divisions;
                        if (provesGap({(1. - u) * lo.x() + u * hi.x(), (1. - v) * lo.y() + v * hi.y()}))
                            return true;
                    }
            }
        }
    }
    return false;
}

static MappingTrimCoverage certifyConvexTrimCoverage(const Body &source, const std::vector<SurfaceTargetFace> &targets,
                                                     const Frame3 &sourceChart, const Frame3 &targetChart,
                                                     double tolerance, Vec2 *gapCenter) {
    using Coverage = MappingTrimCoverage;
    std::size_t loops = 128, edges = 512;
    std::vector<ConvexTrim> from, to;
    std::vector<bool> enclosed;
    std::size_t splineWork = 512;
    const auto sourceFaces = source.faces();
    for (const auto face : sourceFaces) {
        from.emplace_back();
        bool enclosure = false;
        if (!collectConvexTrim(source, face, sourceChart, false, loops, edges, from.back())) {
            from.back() = {};
            if (!collectSourceTrimPoles(source, face, sourceChart, splineWork, from.back()))
                return Coverage::Inconclusive;
            enclosure = true;
        }
        enclosed.push_back(enclosure);
    }
    if (from.empty())
        return Coverage::Inconclusive;
    std::map<const Body *, std::set<int>> seen;
    std::vector<std::vector<ExactTrimSpan>> splineLoops;
    std::vector<PerforatedSplineTrim> perforated;
    std::size_t stripWork = 65536, openingWork = 65536;
    bool allTargetsExactOrPerforated = true;
    for (const auto &target : targets) {
        if (!target.body || !target.body->contains(target.face))
            return Coverage::Inconclusive;
        if (!seen[target.body].insert(target.face.index).second)
            continue;
        ConvexTrim trim;
        if (collectConvexTrim(*target.body, target.face, targetChart, true, loops, edges, trim)) {
            to.push_back(std::move(trim));
            continue;
        }
        if (auto support = collectPerforatedSplineTrim(target, targetChart, splineWork, stripWork, openingWork)) {
            perforated.push_back(std::move(*support));
            continue;
        }
        allTargetsExactOrPerforated = false;
        const auto &boundaries = target.body->face(target.face).loops;
        std::vector<ExactTrimSpan> spans;
        if (boundaries.size() == 1 &&
            collectExactTrimBoundary(*target.body, target.face, boundaries.front(), targetChart, true, splineWork, spans) &&
            std::any_of(spans.begin(), spans.end(), [](const auto &span) { return span.curve; })) {
            if (auto inner = inscribedTrim(spans, stripWork)) to.push_back(std::move(*inner));
            splineLoops.push_back(std::move(spans));
        }
    }
    std::vector<std::vector<ExactTrimSpan>> residual;
    appendInscribedSplineUnions(splineLoops, targetChart, to, residual, stripWork);
    if (to.empty() && perforated.empty() && residual.empty())
        return Coverage::Inconclusive;
    std::optional<std::vector<ConvexTrim>> joined;
    std::optional<std::vector<std::vector<TrimBezier>>> residualBoundaries;
    bool covered = true;
    for (std::size_t i = 0; i < from.size(); ++i) {
        cancellationPoint();
        const auto &trim = from[i];
        const auto includes = [&](const ConvexTrim &candidate) { return convexTrimIncludes(candidate, trim); };
        if (std::any_of(to.begin(), to.end(), includes) ||
            std::any_of(perforated.begin(), perforated.end(), [&](const PerforatedSplineTrim &candidate) {
                return perforatedTrimIncludes(candidate, trim, to);
            }))
            continue;
        if (!joined)
            joined = joinConvexTrims(to);
        if (std::any_of(joined->begin(), joined->end(), includes))
            continue;
        if (stripWork && trimCoveredByStrips(trim, enclosed[i], to, stripWork))
            continue;
        if (!residual.empty()) {
            if (!residualBoundaries)
                residualBoundaries = prepareResidualTrimBoundaries(residual, openingWork);
            if (residualTrimCoversSource(trim, enclosed[i], *residualBoundaries,
                                         source, sourceFaces[i], sourceChart, openingWork))
                continue;
        }
        covered = false;
        if (enclosed[i] || !allTargetsExactOrPerforated) continue;
        if (!perforated.empty()) {
            if (splineOpeningExposesMaterial(trim, perforated, to, tolerance, openingWork, gapCenter))
                return Coverage::Uncovered;
            continue;
        }
        const auto provesGap = [&](const Vec2 &point) {
            cancellationPoint();
            if (!isFinite(point) || !convexTrimPoint(trim, point, tolerance, true) ||
                !std::all_of(to.begin(), to.end(), [&](const ConvexTrim &candidate) {
                    return convexTrimPoint(candidate, point, tolerance, false);
                }))
                return false;
            if (gapCenter)
                *gapCenter = point;
            return true;
        };
        // Candidates locate a witness. Every reported point has a certified
        // open ball in source material and outside all selected target faces.
        Vec2 center;
        if (trim.disk)
            center = trim.disk->center;
        else
            for (const auto &vertex : trim.vertices)
                center += vertex / double(trim.vertices.size());
        if (provesGap(center))
            return Coverage::Uncovered;
        for (const auto &candidate : to)
            for (const auto &hole : candidate.holes)
                if (provesGap(hole.center))
                    return Coverage::Uncovered;
        if (trim.disk) {
            for (const auto &direction : {Vec2(1., 0.), Vec2(-1., 0.), Vec2(0., 1.), Vec2(0., -1.)}) {
                if (provesGap(center + .875 * trim.disk->radius * direction))
                    return Coverage::Uncovered;
                for (const auto &candidate : to)
                    if (candidate.disk && sameTrimPoint(candidate.disk->center, center) &&
                        candidate.disk->radius < trim.disk->radius &&
                        provesGap(center + (.5 * trim.disk->radius + .5 * candidate.disk->radius) * direction))
                        return Coverage::Uncovered;
            }
        } else {
            for (const auto &vertex : trim.vertices)
                if (provesGap(.875 * vertex + .125 * center))
                    return Coverage::Uncovered;
        }
    }
    return covered ? Coverage::Covered : Coverage::Inconclusive;
}

struct TrimBoundary {
    PlanarImage image;
    TrimWindow bounds;
};
struct PreparedBoundary {
    const Body *body;
    EdgeId edge;
    TrimWindow bounds;
    std::size_t outerUses = 0;
};

// Bounds are only a local work filter; all emitted points remain candidates.
static TrimWindow projectedBoundaryBounds(const Edge &edge, const Frame3 &chart) {
    TrimWindow result;
    if (!edge.range.isFinite())
        return result;
    const auto box = curveBox(*edge.curve, edge.range);
    if (box.isEmpty() || !isFinite(box.lo) || !isFinite(box.hi))
        return result;
    for (int corner = 0; corner < 8; ++corner) {
        const auto local =
            chart.toLocal(Vec3(corner & 1 ? box.hi.x() : box.lo.x(), corner & 2 ? box.hi.y() : box.lo.y(),
                               corner & 4 ? box.hi.z() : box.lo.z()));
        result.add(Vec2(local.x(), local.y()));
    }
    return result;
}
static void addOrdinate(std::vector<double> &events, double value, const TrimWindow &window) {
    if (std::isfinite(value) && window.lo.y() < value && value < window.hi.y())
        events.push_back(value);
}
static void boundaryOrdinates(const TrimBoundary &boundary, const TrimWindow &window, std::vector<double> &events,
                              std::size_t &work) {
    const auto &image = boundary.image;
    if (!image.curve) {
        addOrdinate(events, image.point.y(), window);
        return;
    }
    addOrdinate(events, image.curve->point(image.range.lo).y(), window);
    addOrdinate(events, image.curve->point(image.range.hi).y(), window);
    // Horizontal tangencies delimit slabs where the projected boundary can
    // appear or disappear. Spline knots also delimit slabs at corners.
    Vec2 x, y;
    double a = 0., b = 0.;
    if (const auto *circle = dynamic_cast<const Circle<2> *>(image.curve.get())) {
        x = circle->xAxis();
        y = circle->yAxis();
        a = b = circle->radius();
    } else if (const auto *ellipse = dynamic_cast<const Ellipse<2> *>(image.curve.get())) {
        x = ellipse->xAxis();
        y = ellipse->yAxis();
        a = ellipse->xRadius();
        b = ellipse->yRadius();
    } else {
        const auto *spline = dynamic_cast<const BSplineCurve<2> *>(image.curve.get());
        if (!spline)
            return;
        for (double t : spline->breakpoints(image.range))
            addOrdinate(events, spline->point(t).y(), window);
        const auto segments = spline->cachedBezierSegments();
        for (const auto &segment : *segments) {
            cancellationPoint();
            const auto span = segment.domain();
            if (span.hi <= image.range.lo || image.range.hi <= span.lo)
                continue;
            if (!work)
                return;
            --work;
            // For y = A / W, stationary points are roots of A'W - AW'.
            // Translation and common weight scaling preserve those roots.
            const double origin = segment.poles().front().y();
            std::vector<double> ordinates, weights;
            const double weightScale = segment.isRational()
                ? *std::max_element(segment.weights().begin(), segment.weights().end()) : 1.;
            for (int i = 0; i < segment.poleCount(); ++i) {
                const double weight = segment.weight(i) / weightScale;
                ordinates.push_back((segment.poles()[i].y() - origin) * weight);
                weights.push_back(weight);
            }
            const BernsteinPolynomial a(std::move(ordinates)), w(std::move(weights));
            const auto stationary = segment.isRational() ? a.derivative() * w - a * w.derivative() : a.derivative();
            if (!std::all_of(stationary.coefficients().begin(), stationary.coefficients().end(),
                             [](double value) { return std::isfinite(value); }))
                continue;
            for (double s : stationary.roots()) {
                if (!work)
                    return;
                --work;
                cancellationPoint();
                const double t = span.lo + s * span.length();
                if (image.range.lo < t && t < image.range.hi && span.lo < t && t < span.hi)
                    addOrdinate(events, spline->point(t).y(), window);
            }
        }
        return;
    }
    const double first = std::atan2(b * y.y(), a * x.y());
    const double turn = std::ceil((image.range.lo - first) / kPi);
    if (!std::isfinite(turn) || std::abs(turn) >= 0x1p52)
        return;
    for (int i = 0; i < 64; ++i) {
        cancellationPoint();
        const double t = first + (turn + i) * kPi;
        if (t > image.range.hi)
            break;
        if (t >= image.range.lo)
            addOrdinate(events, image.curve->point(t).y(), window);
    }
}
static TrimWindow optionalBoundaryBounds(const Body &body, EdgeId edge, const Frame3 &chart) {
    try {
        return projectedBoundaryBounds(body.edge(edge), chart);
    } catch (const std::domain_error &) {
        return {};
    } catch (const std::invalid_argument &) {
        return {};
    }
}
// A conic viewed edge-on can become a rational spline on one straight
// segment. With clamped endpoints enclosing its axis-aligned control hull,
// continuity and positive weights prove the same projected point set.
static void useStraightProjection(PlanarImage &image, const BSplineCurve<2> &spline) {
    const auto &knots = spline.knots();
    if (spline.isPeriodic() || image.range.lo != knots.front() || image.range.hi != knots.back()) return;
    const auto first = spline.poles().front(), last = spline.poles().back();
    for (int fixed = 0; fixed < 2; ++fixed) {
        const int moving = 1 - fixed;
        if (first[fixed] != last[fixed]) continue;
        const double lo = std::min(first[moving], last[moving]), hi = std::max(first[moving], last[moving]);
        if (!(lo < hi) || !std::isfinite(hi - lo)) continue;
        const bool fits = std::all_of(spline.poles().begin(), spline.poles().end(), [&](const Vec2 &p) {
            return p[fixed] == first[fixed] && lo <= p[moving] && p[moving] <= hi;
        });
        if (!fits || image.range.lo != spline.domain().lo || image.range.hi != spline.domain().hi) continue;
        Vec2 direction;
        direction[moving] = first[moving] < last[moving] ? 1. : -1.;
        // The scan uses only the projected point set, never its original parameter.
        image = {std::make_shared<Line<2>>(first, direction), {0., hi - lo}, {}, {}};
        return;
    }
}

static bool collectTrimBoundaries(const std::vector<PreparedBoundary> &prepared, const Frame3 &chart,
                                  const TrimWindow &window, double precision, std::vector<TrimBoundary> &out) {
    for (const auto &boundary : prepared) {
        cancellationPoint();
        if (!boundary.bounds.overlaps(window, precision))
            continue;
        if (out.size() >= 256)
            return false;
        const auto &edge = boundary.body->edge(boundary.edge);
        try {
            auto image = planarImage(*edge.curve, edge.range, chart);
            if (!image.range.isFinite())
                continue;
            if (image.curve && image.curve->isPeriodic() && image.range.length() >= image.curve->period())
                image.range = image.curve->domain();
            if (const auto *spline = dynamic_cast<const BSplineCurve<2> *>(image.curve.get())) {
                if (spline->poleCount() > 256 || spline->degree() > 8)
                    continue;
                useStraightProjection(image, *spline);
            }
            out.push_back({std::move(image), boundary.bounds});
        } catch (const std::domain_error &) {
            // Optional geometric conversion only: unsupported curves reduce
            // discovery, never transfer a failure to a previously valid wrap.
        } catch (const std::invalid_argument &) {
        }
    }
    return true;
}
static void scanTrimWindow(const TrimWindow &window, const std::vector<TrimBoundary> &boundaries, double precision,
                           std::size_t &work, std::size_t &points, const std::function<void(const Vec2 &)> &visit) {
    std::vector<double> events{window.lo.y(), window.hi.y()};
    for (const auto &boundary : boundaries) {
        cancellationPoint();
        try {
            boundaryOrdinates(boundary, window, events, work);
            if (!boundary.image.curve)
                continue;
            for (double x : {window.lo.x(), window.hi.x()}) {
                if (x < boundary.bounds.lo.x() || x > boundary.bounds.hi.x())
                    continue;
                if (!work)
                    return;
                --work;
                const auto &image = boundary.image;
                for (double t : planeRoots<2>(*image.curve, image.range, Vec2(1., 0.), x, precision).parameters)
                    addOrdinate(events, image.curve->point(t).y(), window);
            }
        } catch (const std::domain_error &) {
        }
        if (events.size() > 2048)
            return;
    }
    // For polygonal frontiers, vertices plus all intersections partition Y
    // into slabs with fixed segment ordering, including overlaps and holes.
    for (std::size_t i = 0; i < boundaries.size(); ++i)
        for (std::size_t j = 0; j < i; ++j) {
            cancellationPoint();
            const auto &a = boundaries[i], &b = boundaries[j];
            if (!a.image.curve || !b.image.curve || !a.bounds.overlaps(b.bounds, precision))
                continue;
            if (work == 0)
                return;
            --work;
            try {
                const auto intersection =
                    intersectCurves(*a.image.curve, a.image.range, *b.image.curve, b.image.range, precision);
                for (const auto &hit : intersection.points)
                    if (window.lo.x() - precision <= hit.point.x() && hit.point.x() <= window.hi.x() + precision)
                        addOrdinate(events, hit.point.y(), window);
            } catch (const std::domain_error &) {
            }
            if (events.size() > 2048)
                return;
        }
    std::sort(events.begin(), events.end());
    events.erase(std::unique(events.begin(), events.end()), events.end());
    for (std::size_t row = 1; row < events.size(); ++row) {
        cancellationPoint();
        const double y = .5 * events[row - 1] + .5 * events[row];
        if (!(events[row - 1] < y && y < events[row]))
            continue;
        std::vector<double> xs{window.lo.x(), window.hi.x()};
        for (const auto &boundary : boundaries) {
            cancellationPoint();
            if (!boundary.image.curve || y < boundary.bounds.lo.y() - precision ||
                y > boundary.bounds.hi.y() + precision)
                continue;
            if (work == 0)
                return;
            --work;
            try {
                const auto roots =
                    planeRoots<2>(*boundary.image.curve, boundary.image.range, Vec2(0., 1.), y, precision);
                for (double t : roots.parameters) {
                    const double x = boundary.image.curve->point(t).x();
                    if (std::isfinite(x) && window.lo.x() < x && x < window.hi.x())
                        xs.push_back(x);
                }
            } catch (const std::domain_error &) {
            }
        }
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        for (std::size_t column = 1; column < xs.size(); ++column) {
            const double x = .5 * xs[column - 1] + .5 * xs[column];
            if (!(xs[column - 1] < x && x < xs[column]))
                continue;
            if (points == 0)
                return;
            --points;
            cancellationPoint();
            visit(Vec2(x, y));
        }
    }
}
} // namespace

static void visitMappingTrimWitnesses(const Body &source, const std::vector<SurfaceTargetFace> &targets,
                                      const Frame3 &sourceChart, const Frame3 &targetChart, double precision,
                                      const std::function<void(const Vec2 &)> &visit) {
    if (!std::isfinite(precision) || precision < 0.)
        return;
    bool planarQuilt = targets.size() > 1;
    bool hasOpening = false, hasCurvedTrim = false, hasDistinctTrims = false;
    const SurfaceTargetFace *firstTrimmed = nullptr;
    for (const auto &target : targets) {
        cancellationPoint();
        if (!target.body || !target.body->contains(target.face))
            return;
        const auto &face = target.body->face(target.face);
        const bool planar = face.surface && face.surface->type() == SurfaceType::Plane;
        hasOpening = hasOpening || (face.surface && face.loops.size() > 1);
        planarQuilt = planarQuilt && planar && !face.loops.empty();
        if (face.surface && !face.loops.empty()) {
            hasCurvedTrim = hasCurvedTrim || !planar;
            if (!firstTrimmed)
                firstTrimmed = &target;
            else if (target.body != firstTrimmed->body || target.face != firstTrimmed->face)
                hasDistinctTrims = true;
        }
    }
    const bool curvedQuilt = hasCurvedTrim && hasDistinctTrims;
    if (!hasOpening && !planarQuilt && !curvedQuilt)
        return;
    std::vector<PreparedBoundary> targetBoundaries, sourceBoundaries;
    std::vector<TrimWindow> windows;
    std::map<const Body *, std::map<int, std::size_t>> targetIndex;
    std::map<const Body *, std::set<int>> visitedFaces;
    // Prepare each selected edge once, including shared fins. Repeated
    // selections must not change the boundary of the selected faces.
    for (const auto &target : targets) {
        cancellationPoint();
        if (!visitedFaces[target.body].insert(target.face.index).second)
            continue;
        const auto &face = target.body->face(target.face);
        for (std::size_t loop = 0; loop < face.loops.size(); ++loop) {
            TrimWindow window;
            bool valid = true;
            for (auto use : target.body->loopFins(face.loops[loop])) {
                cancellationPoint();
                const auto edge = target.body->fin(use).edge;
                auto &index = targetIndex[target.body];
                auto found = index.find(edge.index);
                if (found == index.end()) {
                    const auto bounds = optionalBoundaryBounds(*target.body, edge, targetChart);
                    found = index.emplace(edge.index, targetBoundaries.size()).first;
                    targetBoundaries.push_back({target.body, edge, bounds});
                }
                auto &boundary = targetBoundaries[found->second];
                if (loop == 0) ++boundary.outerUses;
                const auto &bounds = boundary.bounds;
                if (!bounds.finite()) {
                    valid = false;
                    continue;
                }
                window.add(bounds.lo);
                window.add(bounds.hi);
            }
            if (loop > 0 && face.surface && valid && window.finite())
                windows.push_back(window);
        }
    }
    TrimWindow footprint;
    for (auto edge : source.edges()) {
        cancellationPoint();
        const auto bounds = optionalBoundaryBounds(source, edge, sourceChart);
        if (bounds.finite()) {
            sourceBoundaries.push_back({&source, edge, bounds});
            footprint.add(bounds.lo);
            footprint.add(bounds.hi);
        }
    }
    // Several outer loops can enclose a gap even when no face has a hole.
    // Scan their arrangement inside the projected source bounds. For curved
    // quilts, start the sweep where an exposed outer trim may meet that window.
    // A shared edge still participates in any sweep started by an exposed edge.
    // Individual holes retain priority under the shared work budget.
    if (footprint.finite() && footprint.lo.x() < footprint.hi.x() && footprint.lo.y() < footprint.hi.y()) {
        if (planarQuilt || (curvedQuilt && std::any_of(targetBoundaries.begin(), targetBoundaries.end(),
            [&](const PreparedBoundary &boundary) {
                cancellationPoint();
                return boundary.outerUses == 1 && boundary.bounds.finite() && boundary.bounds.overlaps(footprint, precision);
            })))
            windows.push_back(footprint);
    }
    std::size_t work = 50000, points = 32768, scanned = 0;
    for (const auto &window : windows) {
        cancellationPoint();
        // Every finite opening gets a witness even after the optional sweep has exhausted its budget.
        visit(Vec2(.5 * window.lo.x() + .5 * window.hi.x(), .5 * window.lo.y() + .5 * window.hi.y()));
        if (scanned >= 128 || work == 0 || points == 0)
            continue;
        ++scanned;
        std::vector<TrimBoundary> boundaries;
        if (!collectTrimBoundaries(sourceBoundaries, sourceChart, window, precision, boundaries) ||
            !collectTrimBoundaries(targetBoundaries, targetChart, window, precision, boundaries))
            continue;
        scanTrimWindow(window, boundaries, precision, work, points, visit);
    }
}

static MappingTrimCoverage certifyOrthogonalTrimCoverage(const Body &source,
                                                         const std::vector<SurfaceTargetFace> &targets,
                                                         const Frame3 &sourceChart, const Frame3 &targetChart,
                                                         double boundaryTolerance, Vec2 *gapCenter) {
    using Coverage = MappingTrimCoverage;
    // At most 128 loops and 512 edges in total, including source and target.
    std::size_t loopBudget = 128, edgeBudget = 512;
    std::vector<OrthogonalTrim> sources, supports;
    for (const auto id : source.faces()) {
        sources.emplace_back();
        if (!collectOrthogonalTrim(source, id, sourceChart, false, loopBudget, edgeBudget, sources.back()))
            return Coverage::Inconclusive;
    }
    if (sources.empty())
        return Coverage::Inconclusive;
    std::map<const Body *, std::set<int>> seen;
    for (const auto &target : targets) {
        if (!seen[target.body].insert(target.face.index).second)
            continue;
        supports.emplace_back();
        if (!target.body ||
            !collectOrthogonalTrim(*target.body, target.face, targetChart, true, loopBudget, edgeBudget, supports.back()))
            return Coverage::Inconclusive;
    }
    std::vector<double> ys;
    for (const auto *faces : {&sources, &supports})
        for (const auto &face : *faces)
            for (const auto &loop : face)
                for (const auto &edge : loop.edges)
                    ys.push_back(edge.start.y());
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    // Require an open rectangle with room for the same boundary tolerance
    // on both sides of each axis before rejecting a tolerant wrap.
    const auto exceedsBoundaryBand = [boundaryTolerance](double lo, double hi) {
        return boundaryTolerance == 0. || std::nextafter(hi - lo, 0.) > 2. * boundaryTolerance;
    };
    bool hasGap = false;
    // No edges cross an open slab. Its material intervals are constant,
    // so inclusion there covers their closure as well, including event
    // lines and corners. All actual trim points belong to those closures.
    for (std::size_t i = 1; i < ys.size(); ++i) {
        cancellationPoint();
        const auto material = trimSlabUnion(sources, ys[i - 1], ys[i]);
        if (!material)
            return Coverage::Inconclusive;
        if (material->empty())
            continue;
        const auto covered = trimSlabUnion(supports, ys[i - 1], ys[i]);
        if (!covered)
            return Coverage::Inconclusive;
        auto support = covered->begin();
        const bool slabHasRoom = exceedsBoundaryBand(ys[i - 1], ys[i]);
        const auto provesGap = [&](double lo, double hi) {
            hasGap = true;
            if (!slabHasRoom || !exceedsBoundaryBand(lo, hi)) return false;
            if (gapCenter) *gapCenter = Vec2(.5 * lo + .5 * hi, .5 * ys[i - 1] + .5 * ys[i]);
            return true;
        };
        for (const auto span : *material) {
            double cursor = span.lo;
            while (support != covered->end() && support->hi <= cursor)
                ++support;
            while (support != covered->end() && support->lo < span.hi) {
                if (cursor < support->lo && provesGap(cursor, support->lo))
                    return Coverage::Uncovered;
                cursor = std::max(cursor, support->hi);
                if (cursor >= span.hi)
                    break;
                ++support;
            }
            if (cursor < span.hi && provesGap(cursor, span.hi))
                return Coverage::Uncovered;
        }
    }
    return hasGap ? Coverage::Inconclusive : Coverage::Covered;
}

MappingTrimCoverage certifyMappingTrimCoverage(const Body &source, const std::vector<SurfaceTargetFace> &targets,
                                               const Frame3 &sourceChart, const Frame3 &targetChart,
                                               double boundaryTolerance, Vec2 *gapCenter) {
    using Coverage = MappingTrimCoverage;
    if (std::fegetround() != FE_TONEAREST || !cartesianTrimFrame(sourceChart) || !cartesianTrimFrame(targetChart) ||
        targets.empty() || targets.size() >= 128 ||
        !std::isfinite(boundaryTolerance) || boundaryTolerance < 0.)
        return Coverage::Inconclusive;
    const auto orthogonal = certifyOrthogonalTrimCoverage(source, targets, sourceChart, targetChart,
                                                         boundaryTolerance, gapCenter);
    if (orthogonal != Coverage::Inconclusive)
        return orthogonal;
    return certifyConvexTrimCoverage(source, targets, sourceChart, targetChart, boundaryTolerance, gapCenter);
}

void checkMappingTrimCoverage(const Body &source, const std::vector<SurfaceTargetFace> &targets,
                              const Frame3 &sourceChart, const Frame3 &targetChart, double precision,
                              const std::function<void(const Vec3 &)> &map) {
    std::optional<RayFaceIndex> index;
    visitMappingTrimWitnesses(source, targets, sourceChart, targetChart, precision, [&](const Vec2 &xy) {
        cancellationPoint();
        if (!index)
            index.emplace(source);
        const auto base = sourceChart.toGlobal(Vec3(xy.x(), xy.y(), 0.));
        // The source can be on either side of its sketch plane. Only a hit
        // inside a trimmed source face represents material for this check.
        for (const auto &direction : {sourceChart.zDir(), -sourceChart.zDir()}) {
            double distance;
            FaceId face;
            if (!firstRayHit(source, base, direction, precision, distance, &face, &*index))
                continue;
            const auto point = base + distance * direction;
            if (classifyPointOnFace(source, face, point, precision) == PointLocation::Inside) {
                map(point);
                break;
            }
        }
    });
}

double measureMappingKnotResidual(const Body &source, FaceId face, const std::vector<SurfaceTargetFace> &targets,
                                  const Frame3 &sourceChart, const Frame3 &targetChart, const Surface &image,
                                  Interval u, Interval v, double precision,
                                  const std::function<Vec3(const Vec3 &)> &map) {
    const auto *plane = dynamic_cast<const Plane *>(source.face(face).surface.get());
    if (!plane || targets.empty())
        return 0.;
    const auto &frame = plane->frame();
    const double denominator = dot(frame.zDir(), sourceChart.zDir());
    if (std::abs(denominator) < 1e-8)
        return 0.;
    std::optional<FaceParameterClassifier> sourceTrim;
    double worst = 0.;
    // The wrap accepts at most 255 targets. Divide 256 candidate evaluations
    // among them so a large grid cannot exhaust the work before later targets.
    const std::size_t limit = 256 / targets.size();
    for (const auto &target : targets) {
        cancellationPoint();
        const auto *spline = dynamic_cast<const BSplineSurface *>(target.body->face(target.face).surface.get());
        if (!spline)
            continue;
        const auto us = spline->uBreakpoints(spline->uDomain());
        const auto vs = spline->vBreakpoints(spline->vDomain());
        if (us.size() < 2 || vs.size() < 2 || (us.size() == 2 && vs.size() == 2))
            continue;
        const auto spans = [limit](const std::vector<double> &knots) {
            std::vector<Interval> result;
            result.reserve(knots.size() - 1);
            for (std::size_t i = 1; i < knots.size(); ++i) {
                cancellationPoint();
                result.push_back({knots[i - 1], knots[i]});
            }
            // Prefer localized spans when there are too many rectangles.
            const auto keep = std::min(result.size(), limit);
            std::partial_sort(result.begin(), result.begin() + keep, result.end(), [](Interval a, Interval b) {
                return a.length() == b.length() ? a.lo < b.lo : a.length() < b.length();
            });
            result.resize(keep);
            return result;
        };
        const auto uSpans = spans(us), vSpans = spans(vs);
        std::optional<FaceParameterClassifier> targetTrim;
        std::size_t remaining = limit;
        // Visit diagonals in span rank so both UV directions get sampled.
        for (std::size_t rank = 0; remaining && rank < uSpans.size() + vSpans.size() - 1; ++rank) {
            const std::size_t first = rank >= vSpans.size() ? rank - vSpans.size() + 1 : 0;
            for (std::size_t i = first; remaining && i <= rank && i < uSpans.size(); ++i) {
                cancellationPoint();
                --remaining;
                const auto a = uSpans[i], b = vSpans[rank - i];
                const Vec2 uv(.5 * a.lo + .5 * a.hi, .5 * b.lo + .5 * b.hi);
                if (!(a.lo < uv.x() && uv.x() < a.hi && b.lo < uv.y() && uv.y() < b.hi))
                    continue;
                const auto xy = targetChart.toLocal(spline->point(uv.x(), uv.y()));
                const auto base = sourceChart.toGlobal(Vec3(xy.x(), xy.y(), 0.));
                const auto point =
                    base + dot(frame.zDir(), frame.origin() - base) / denominator * sourceChart.zDir();
                const auto local = frame.toLocal(point);
                if (!isFinite(point) || !u.contains(local.x()) || !v.contains(local.y()) ||
                    !image.uDomain().contains(local.x()) || !image.vDomain().contains(local.y()))
                    continue;
                if (!sourceTrim)
                    sourceTrim.emplace(source, face, precision);
                if (sourceTrim->classify(Vec2(local.x(), local.y())) != PointLocation::Inside)
                    continue;
                if (!targetTrim)
                    targetTrim.emplace(*target.body, target.face, precision);
                if (targetTrim->classify(uv) != PointLocation::Inside)
                    continue;
                // Use public contact at the actual source height, including
                // its normal offset and the selection of the trimmed patch.
                const double residual = distance(image.point(local.x(), local.y()), map(plane->point(local.x(), local.y())));
                if (!std::isfinite(residual))
                    return INFINITY;
                worst = std::max(worst, residual);
            }
        }
    }
    return worst;
}
} // namespace ForgeCad::Kernel::detail
