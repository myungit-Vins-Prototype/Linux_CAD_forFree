#include "fk_offset_support.h"
#include "fk_offset.h"
#include "fk_classify_uv.h"
#include "fk_exchange.h"
#include "fk_surface_bound_math.h"
#include "fk_cancel.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>

namespace ForgeCad::Kernel::detail {
namespace {
BSplineCurve<3> continueCurve(const BSplineCurve<3> &input, double lo, double hi) {
    const auto curve = input.clamped();
    auto knots = curve.knots();
    const int degree = curve.degree();
    const auto domain = curve.domain();
    std::vector<Vec3> homogeneous, weights;
    for (int i = 0; i < curve.poleCount(); ++i) {
        homogeneous.push_back(curve.weight(i) * curve.poles()[i]);
        weights.emplace_back(curve.weight(i), 0, 0);
    }
    const BSplineCurve<3> numerator(degree, knots, homogeneous), denominator(degree, knots, weights);
    const auto section = [&](const BSplineCurve<3> &basis, double at, double length, bool left) {
        Vec3 jet[3];
        basis.evaluate(at, 2, jet);
        std::vector<Vec3> poles;
        for (int i = 0; i <= degree; ++i) {
            const double a = double(i) / degree;
            const double b = degree > 1 ? double(i) * (i - 1) / (degree * (degree - 1)) : 0.0;
            poles.push_back(left ? jet[0] + (a - 1) * length * jet[1] + .5 * (1 - 2 * a + b) * length * length * jet[2]
                                 : jet[0] + a * length * jet[1] + .5 * b * length * length * jet[2]);
        }
        return poles;
    };
    if (hi > domain.hi) {
        const auto p = section(numerator, domain.hi, hi - domain.hi, false);
        const auto w = section(denominator, domain.hi, hi - domain.hi, false);
        homogeneous.insert(homogeneous.end(), p.begin() + 1, p.end());
        weights.insert(weights.end(), w.begin() + 1, w.end());
        knots.pop_back();
        knots.insert(knots.end(), degree + 1, hi);
    }
    if (lo < domain.lo) {
        const auto p = section(numerator, domain.lo, domain.lo - lo, true);
        const auto w = section(denominator, domain.lo, domain.lo - lo, true);
        homogeneous.insert(homogeneous.begin(), p.begin(), p.end() - 1);
        weights.insert(weights.begin(), w.begin(), w.end() - 1);
        knots.erase(knots.begin());
        knots.insert(knots.begin(), degree + 1, lo);
    }
    std::vector<Vec3> poles;
    std::vector<double> resultWeights;
    for (std::size_t i = 0; i < homogeneous.size(); ++i) {
        if (!(weights[i].x() > 0))
            throw std::domain_error("offset: continuazione C2 con peso non positivo");
        poles.push_back(homogeneous[i] / weights[i].x());
        if (curve.isRational())
            resultWeights.push_back(weights[i].x());
    }
    return BSplineCurve<3>(degree, std::move(knots), std::move(poles), std::move(resultWeights));
}
Body trimBody(const RawModel &model, const RawFace &face) {
    std::vector<Vec3> points;
    std::vector<Body::BuildEdge> edges;
    Body::BuildFace result{face.surface, face.sense, {}};
    std::map<int, int> pointMap, edgeMap;
    const auto point = [&](int index) {
        auto entry = pointMap.emplace(index, int(points.size()));
        if (entry.second)
            points.push_back(model.points.at(std::size_t(index)));
        return entry.first->second;
    };
    for (const auto &loop : face.loops) {
        std::vector<Body::BuildFin> fins;
        for (const auto &fin : loop) {
            auto entry = edgeMap.emplace(fin.edge, int(edges.size()));
            if (entry.second) {
                const auto &edge = model.edges.at(std::size_t(fin.edge));
                edges.push_back({point(edge.start), point(edge.end), edge.curve, edge.range});
            }
            fins.push_back({entry.first->second, fin.sense, fin.pcurve, fin.pcurveTolerance});
        }
        result.loops.push_back(std::move(fins));
    }
    return Body::buildSheet(points, edges, {result});
}
} // namespace
BSplineSurface continueOffsetSupport(const BSplineSurface &input, const Interval &u, const Interval &v) {
    BSplineSurface surface = input;
    for (int axis = 0; axis < 2; ++axis) {
        const int count = axis == 0 ? surface.vPoleCount() : surface.uPoleCount();
        const int along = axis == 0 ? surface.uPoleCount() : surface.vPoleCount();
        std::vector<BSplineCurve<3>> curves;
        for (int j = 0; j < count; ++j) {
            std::vector<Vec3> poles;
            std::vector<double> weights;
            for (int i = 0; i < along; ++i) {
                poles.push_back(axis == 0 ? surface.pole(i, j) : surface.pole(j, i));
                if (surface.isRational())
                    weights.push_back(axis == 0 ? surface.weight(i, j) : surface.weight(j, i));
            }
            const BSplineCurve<3> curve(axis == 0 ? surface.uDegree() : surface.vDegree(), axis == 0 ? surface.uKnots() : surface.vKnots(),
                                        std::move(poles), std::move(weights));
            const auto range = axis == 0 ? u : v;
            curves.push_back(continueCurve(curve, range.lo, range.hi));
        }
        const int nu = axis == 0 ? curves.front().poleCount() : count;
        const int nv = axis == 0 ? count : curves.front().poleCount();
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < nu; ++i)
            for (int j = 0; j < nv; ++j) {
                const auto &curve = curves[std::size_t(axis == 0 ? j : i)];
                const int k = axis == 0 ? i : j;
                poles.push_back(curve.poles()[std::size_t(k)]);
                if (surface.isRational())
                    weights.push_back(curve.weight(k));
            }
        surface = BSplineSurface(surface.uDegree(), surface.vDegree(), axis == 0 ? curves.front().knots() : surface.uKnots(),
                                 axis == 1 ? curves.front().knots() : surface.vKnots(), nu, nv, std::move(poles), std::move(weights));
    }
    return surface;
}
static bool regularOffset(const Surface &surface, double distance, const Vec2 &uv, double &radius) {
    if (distance == 0)
        return true;
    Vec3 jet[9];
    surface.evaluate(uv.x(), uv.y(), 2, jet);
    const auto a = jet[3], b = jet[1], n = normalized(cross(a, b));
    const double E = dot(a, a), F = dot(a, b), G = dot(b, b), L = dot(n, jet[6]), M = dot(n, jet[4]), N = dot(n, jet[2]);
    const double det = E * G - F * F;
    if (!(det > 0))
        return false;
    const double H = (E * N - 2 * F * M + G * L) / (2 * det), K = (L * N - M * M) / det;
    const double root = std::sqrt(std::max(0., H * H - K));
    const double ratio = std::max(distance * (H + root), distance * (H - root));
    if (std::isfinite(ratio) && ratio >= 1.0) radius = std::fabs(distance) / ratio;
    return 1 - distance * (H + root) > 0 && 1 - distance * (H - root) > 0;
}

bool regularOffset(const Surface &surface, double distance, const Vec2 &uv) {
    double radius = 0.0;
    return regularOffset(surface, distance, uv, radius);
}
void requireRegularOffsetTrim(const RawModel &model, const RawFace &face, double distance, const Interval &u, const Interval &v,
                              double tolerance) {
    if (distance == 0 || face.surface->type() != SurfaceType::BSpline)
        return;
    auto us = face.surface->uBreakpoints(u), vs = face.surface->vBreakpoints(v);
    for (int k = 0; k <= 64; ++k) {
        us.push_back(u.lo + u.length() * k / 64.);
        vs.push_back(v.lo + v.length() * k / 64.);
    }
    // Preparazione solo se il rettangolo contiene un tratto ripiegato.
    std::optional<Body> trim;
    std::optional<FaceParameterClassifier> classifier;
    for (double a : us)
        for (double b : vs) {
            double radius = 0.0;
            if (!regularOffset(*face.surface, distance, {a, b}, radius)) {
                if (!trim) {
                    trim = trimBody(model, face);
                    classifier.emplace(*trim, trim->faces().front(), tolerance);
                }
                if (classifier->classify({a, b}) != PointLocation::Outside) {
                    const char *message = "offset: la parete a distanza si ripiega dentro la faccia rifilata";
                    if (radius > 0.0) throw OffsetCurvatureError(message, radius, Vec2(a, b));
                    throw std::domain_error(message);
                }
            }
        }
}
namespace {
using surface_bounds::I;
using surface_bounds::finite;
using surface_bounds::bernsteinProductFactor;

struct BlendPolynomial {
    int u, v;
    std::vector<I> poles;
    BlendPolynomial(int uDegree, int vDegree) : u(uDegree), v(vDegree), poles((u + 1) * (v + 1)) {}
    I &at(int i, int j) { return poles[i * (v + 1) + j]; }
    I at(int i, int j) const { return poles[i * (v + 1) + j]; }
};

BlendPolynomial product(const BlendPolynomial &a, const BlendPolynomial &b) {
    BlendPolynomial result(a.u + b.u, a.v + b.v);
    for (int i = 0; i <= a.u; ++i)
        for (int k = 0; k <= b.u; ++k) {
            const I factor = bernsteinProductFactor(a.u, b.u, i, k);
            for (int j = 0; j <= a.v; ++j)
                for (int l = 0; l <= b.v; ++l)
                    result.at(i + k, j + l) = result.at(i + k, j + l)
                        + a.at(i, j) * b.at(k, l) * factor * bernsteinProductFactor(a.v, b.v, j, l);
        }
    return result;
}

BlendPolynomial difference(BlendPolynomial a, const BlendPolynomial &b) {
    for (std::size_t i = 0; i < a.poles.size(); ++i)
        a.poles[i] = a.poles[i] - b.poles[i];
    return a;
}

BlendPolynomial derivative(const BlendPolynomial &p, bool alongU) {
    BlendPolynomial result(p.u - int(alongU), p.v - int(!alongU));
    for (int i = 0; i <= result.u; ++i)
        for (int j = 0; j <= result.v; ++j)
            result.at(i, j) = I(alongU ? p.u : p.v) * (p.at(i + int(alongU), j + int(!alongU)) - p.at(i, j));
    return result;
}

using BlendVector = std::array<BlendPolynomial, 3>;
BlendPolynomial dotProduct(const BlendVector &a, const BlendVector &b) {
    auto result = product(a[0], b[0]);
    for (int k : {1, 2}) {
        const auto term = product(a[k], b[k]);
        for (std::size_t i = 0; i < result.poles.size(); ++i)
            result.poles[i] = result.poles[i] + term.poles[i];
    }
    return result;
}

double absoluteBound(const BlendPolynomial &p) {
    const auto h = surface_bounds::hull(p.poles);
    return finite(h) ? std::max(std::abs(h.lo), std::abs(h.hi)) : surface_bounds::infinity;
}

std::array<BlendPolynomial, 2> splitV(const BlendPolynomial &p) {
    std::array<BlendPolynomial, 2> halves{p, p};
    for (int i = 0; i <= p.u; ++i) {
        std::vector<I> row(p.v + 1);
        for (int j = 0; j <= p.v; ++j)
            row[j] = p.at(i, j);
        for (int level = 1; level <= p.v; ++level) {
            for (int j = 0; j <= p.v - level; ++j)
                row[j] = (row[j] + row[j + 1]) * I(0.5);
            halves[0].at(i, level) = row[0];
            halves[1].at(i, p.v - level) = row[p.v - level];
        }
    }
    return halves;
}

struct BlendBounds {
    BlendPolynomial denominator, projection, radius;
    BlendVector crossResidual;
};

bool boundedResidual(const BlendBounds &bounds, double radius, double normalBound, double budget, int depth) {
    const auto denominator = surface_bounds::hull(bounds.denominator.poles);
    const auto projection = surface_bounds::hull(bounds.projection.poles);
    if (!(denominator.lo > 0) || !(projection.lo > 0)) return false;
    const I radialError = I(absoluteBound(bounds.radius)) / (I(radius) * I(denominator.lo) * I(denominator.lo));
    I crossSquared;
    for (int k = 0; k < 3; ++k) {
        const I component(absoluteBound(bounds.crossResidual[k]));
        crossSquared = crossSquared + component * component;
    }
    const I crossBound = surface_bounds::outward(0, std::sqrt(crossSquared.hi));
    const I tangentError = crossBound * I(normalBound) / (I(projection.lo) * I(denominator.lo));
    // Removing the tangential component changes the radial length by at
    // most t^2 / (R - radialError), in addition to the tangential error t.
    const I error = radialError + tangentError + tangentError * tangentError / (I(radius) - radialError);
    if (error.hi <= budget) return true;
    if (depth == 0) return false;
    const auto ds = splitV(bounds.denominator), ps = splitV(bounds.projection), rs = splitV(bounds.radius);
    const auto xs = splitV(bounds.crossResidual[0]), ys = splitV(bounds.crossResidual[1]),
               zs = splitV(bounds.crossResidual[2]);
    for (int j : {0, 1}) {
        const BlendBounds half{ds[j], ps[j], rs[j], {xs[j], ys[j], zs[j]}};
        if (!boundedResidual(half, radius, normalBound, budget, depth - 1)) return false;
    }
    return true;
}

// On a Bezier cell, both the normal-offset point and the center rail are
// compared in one coordinate system. The center rail keeps the original
// longitudinal degree and weights. All tests below enclose entire cells.
bool certifyCollapsedCell(const BSplineSurface &surface, int first, int rail, Interval uDomain, const Plane &plane,
                          const Vec3 &shift, double signedDistance, double budget) {
    const int u = surface.uDegree(), v = surface.vDegree();
    const int column = rail == 0 ? 0 : v;
    const Vec3 origin = surface.pole(first, 0);
    const auto &frame = plane.frame();
    BlendPolynomial w(u, v), wc(u, 0);
    BlendVector h{w, w, w}, hc{wc, wc, wc};
    for (int i = 0; i <= u; ++i) {
        const auto railPole = surface.pole(first + i, column);
        wc.at(i, 0) = I(surface.weight(first + i, column));
        I planeDistance;
        for (int k = 0; k < 3; ++k) {
            planeDistance = planeDistance + (I(railPole[k]) - I(frame.origin()[k])) * I(frame.zDir()[k]);
            hc[k].at(i, 0) = wc.at(i, 0) * (I(railPole[k]) - I(origin[k]) + I(shift[k]));
        }
        if (!finite(planeDistance) || planeDistance.lo < -budget || planeDistance.hi > budget)
            return false;
        for (int j = 0; j <= v; ++j) {
            w.at(i, j) = I(surface.weight(first + i, j));
            const auto p = surface.pole(first + i, j);
            for (int k = 0; k < 3; ++k)
                h[k].at(i, j) = w.at(i, j) * (I(p[k]) - I(origin[k]));
        }
    }
    const auto denominator = product(w, wc);
    const auto dHull = surface_bounds::hull(denominator.poles);
    if (!(dHull.lo > 0.0)) return false;
    BlendVector radial = h, su = h, sv = h;
    for (int k = 0; k < 3; ++k) {
        radial[k] = difference(product(h[k], wc), product(hc[k], w));
        su[k] = difference(product(derivative(h[k], true), w), product(h[k], derivative(w, true)));
        sv[k] = difference(product(derivative(h[k], false), w), product(h[k], derivative(w, false)));
    }
    auto radiusSquared = product(denominator, denominator);
    for (auto &coefficient : radiusSquared.poles)
        coefficient = coefficient * I(signedDistance) * I(signedDistance);
    auto radiusResidual = difference(dotProduct(radial, radial), radiusSquared);
    if (!(absoluteBound(radiusResidual) <= 4.0 * budget * std::abs(signedDistance) * dHull.hi * dHull.hi))
        return false;
    // The radial vector's tangential component is bounded with the inverse
    // tangent frame. A fixed projection of Su x Sv proves its nonvanishing.
    const auto vDomain = surface.vDomain();
    Vec3 normal;
    try {
        normal = surface.normal(0.5 * (uDomain.lo + uDomain.hi), 0.5 * (vDomain.lo + vDomain.hi));
    } catch (const std::domain_error &) {
        return false;
    }
    BlendPolynomial normalProjection(4 * u - 1, 4 * v - 1);
    for (int k = 0; k < 3; ++k) {
        const int a = (k + 1) % 3, b = (k + 2) % 3;
        const auto component = difference(product(su[a], sv[b]), product(su[b], sv[a]));
        for (std::size_t i = 0; i < component.poles.size(); ++i)
            normalProjection.poles[i] = normalProjection.poles[i] + component.poles[i] * I(normal[k]);
    }
    I normalSquared;
    for (int k = 0; k < 3; ++k)
        normalSquared = normalSquared + I(normal[k]) * I(normal[k]);
    const double normalBound = surface_bounds::outward(0, std::sqrt(normalSquared.hi)).hi;
    const auto dotU = dotProduct(radial, su), dotV = dotProduct(radial, sv);
    BlendVector crossResidual = su;
    for (int k = 0; k < 3; ++k)
        crossResidual[k] = difference(product(su[k], dotV), product(sv[k], dotU));
    const BlendBounds bounds{denominator, normalProjection, std::move(radiusResidual), std::move(crossResidual)};
    if (!(budget < std::abs(signedDistance) * 0.5) ||
        !boundedResidual(bounds, std::abs(signedDistance), normalBound, budget, 4))
        return false;
    // Within this bound the sign cannot switch on the connected cell. Check
    // which of the two radial normals corresponds to the requested offset.
    const double midU = 0.5 * (uDomain.lo + uDomain.hi), midV = 0.5 * (vDomain.lo + vDomain.hi);
    const Vec3 center = surface.point(midU, column == 0 ? vDomain.lo : vDomain.hi) + shift;
    return dot(normal, center - surface.point(midU, midV)) * signedDistance > 0.0;
}

std::optional<CollapsedPlanarBlend> planarBlendCollapse(const Body &body, FaceId id, double distance, double tolerance,
                                                       bool beyondCollapse) {
    const auto &face = body.face(id);
    const auto *spline = dynamic_cast<const BSplineSurface *>(face.surface.get());
    if (!spline || face.loops.size() != 1 || spline->uDegree() > 3 || spline->vDegree() != 2 ||
        spline->isUPeriodic() || spline->isVPeriodic() || !(std::abs(distance) > tolerance))
        return std::nullopt;
    const auto fins = body.loopFins(face.loops.front());
    if (fins.size() != 4) return std::nullopt;
    const auto u = spline->uDomain(), v = spline->vDomain();
    // Work from original controls: endpoints are clamped and interior U
    // knots already have Bezier multiplicity. No knot insertion is needed.
    const int degree = spline->uDegree();
    const auto &uKnots = spline->uKnots(), &vKnots = spline->vKnots();
    if (degree < 1 || spline->vPoleCount() != 3 || vKnots[0] != vKnots[2] ||
        vKnots[3] != vKnots[5] || !(u.length() > 0) || !(v.length() > 0))
        return std::nullopt;
    std::vector<Interval> cells;
    for (auto it = uKnots.begin(); it != uKnots.end();) {
        const double knot = *it;
        const auto next = std::upper_bound(it, uKnots.end(), knot);
        if (next - it != degree + int(knot == u.lo || knot == u.hi))
            return std::nullopt;
        if (next != uKnots.end())
            cells.push_back({knot, *next});
        it = next;
    }
    if (cells.empty() || cells.size() > 4096 || cells.front().lo != u.lo || cells.back().hi != u.hi)
        return std::nullopt;
    const double uv = 1e-10;
    const auto unit = [&](Vec2 p) { return Vec2{(p.x() - u.lo) / u.length(), (p.y() - v.lo) / v.length()}; };
    std::array<EdgeId, 4> edges{};
    std::array<FaceId, 2> supports{};
    for (auto fi : fins) {
        const auto &fin = body.fin(fi);
        const auto other = body.otherFin(fi);
        if (!other.valid() || !dynamic_cast<const Line<2> *>(fin.pcurve.get())) return std::nullopt;
        const auto range = body.edge(fin.edge).range;
        const auto p = unit(fin.pcurve->point(range.lo)), q = unit(fin.pcurve->point(range.hi));
        int side = -1;
        for (int axis : {0, 1})
            for (int end : {0, 1})
                if (std::abs(p[axis] - end) <= uv && std::abs(q[axis] - end) <= uv &&
                    std::min(p[1 - axis], q[1 - axis]) >= -uv && std::max(p[1 - axis], q[1 - axis]) <= 1 + uv &&
                    std::abs(std::abs(p[1 - axis] - q[1 - axis]) - 1) <= 2 * uv)
                    side = 2 * (1 - axis) + end;
        if (side < 0 || edges[side].valid()) return std::nullopt;
        edges[side] = fin.edge;
        if (side < 2) supports[side] = body.finFace(other);
    }
    for (int rail : {0, 1}) {
        const auto &support = body.face(supports[rail]);
        const auto *plane = dynamic_cast<const Plane *>(support.surface.get());
        if (!plane) continue;
        const Vec3 supportNormal = support.sense ? plane->frame().zDir() : -plane->frame().zDir();
        const double midU = 0.5 * (u.lo + u.hi), midV = 0.5 * (v.lo + v.hi);
        const auto candidate = spline->point(midU, rail == 0 ? v.lo : v.hi);
        const auto sample = spline->point(midU, midV);
        double collapseDistance = distance;
        if (beyondCollapse) {
            Vec3 blendNormal;
            try {
                blendNormal = spline->normal(midU, midV);
            } catch (const std::domain_error &) {
                continue;
            }
            if (!face.sense) blendNormal = -blendNormal;
            // At collapse, the sample and the planar rail translate to the
            // same center. Solve for the shared signed displacement.
            const Vec3 normalDifference = supportNormal - blendNormal;
            const double denominator = dot(normalDifference, normalDifference);
            if (!(denominator > 0.0)) continue;
            collapseDistance = dot(sample - candidate, normalDifference) / denominator;
            if (!std::isfinite(collapseDistance) || std::abs(collapseDistance) <= tolerance ||
                std::signbit(collapseDistance) != std::signbit(distance) ||
                !(std::abs(distance) - std::abs(collapseDistance) > tolerance))
                continue;
        }
        const double signedDistance = face.sense ? collapseDistance : -collapseDistance;
        const Vec3 shift = collapseDistance * supportNormal;
        if (std::abs(norm(sample - candidate - shift) - std::abs(collapseDistance)) > tolerance ||
            std::abs(dot(candidate - plane->frame().origin(), plane->frame().zDir())) > tolerance)
            continue;
        bool proved = true;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            cancellationPoint();
            if (!certifyCollapsedCell(*spline, int(i) * degree, rail, cells[i], *plane, shift, signedDistance, tolerance)) {
                proved = false;
                break;
            }
        }
        if (proved) return CollapsedPlanarBlend{edges[rail], edges[1 - rail], {edges[2], edges[3]}, collapseDistance};
    }
    return std::nullopt;
}

} // namespace

std::optional<CollapsedPlanarBlend> collapsedPlanarBlend(const Body &body, FaceId id, double distance, double tolerance) {
    return planarBlendCollapse(body, id, distance, tolerance, false);
}

std::optional<CollapsedPlanarBlend> overrunPlanarBlend(const Body &body, FaceId id, double distance, double tolerance) {
    return planarBlendCollapse(body, id, distance, tolerance, true);
}

} // namespace ForgeCad::Kernel::detail
