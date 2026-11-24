#include "fk_sheet.h"
#include "fk_sheet_join_validation.h"
#include "fk_history_tools.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>

#include "fk_blend_model.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_cancel.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_offset.h"
#include "fk_curve_fit_bounds.h"
#include "fk_planar.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"
#include "fk_unify.h"

namespace ForgeCad::Kernel {
namespace {

using Model = detail::BlendModel;

struct HPoint {
    Vec3 p;
    double w = 1.0;
};
HPoint mix(const HPoint &a, const HPoint &b, double s) { return {(1.0 - s) * a.p + s * b.p, (1.0 - s) * a.w + s * b.w}; }

// Distanza del punto dalla lamina (dalle sue facce, bordi compresi).
double distanceToSheet(const Body &body, const Vec3 &p) {
    double best = 1e300;
    for (FaceId f : body.faces()) {
        const SurfaceProjection projection = projectPoint(*body.face(f).surface, p);
        const double d = classifyPointOnFace(body, f, projection.point, 1e-7) != PointLocation::Outside ? projection.distance
                                                                                                        : distanceToFaceBoundary(body, f, p);
        best = std::min(best, d);
    }
    return best;
}

// Parametro u1 con lunghezza d'arco |u1 - u0| = length sulla curva (verso `sign`).
double arcParameter(const Curve<3> &curve, double u0, double length, double sign, double tolerance) {
    cancellationPoint();
    if (!std::isfinite(u0) || !std::isfinite(length) || !(length > 0.0)
        || (sign != -1.0 && sign != 1.0) || !std::isfinite(tolerance) || !(tolerance > 0.0))
        throw std::domain_error("extendSheet: dati della lunghezza d'arco non validi");
    const double initialSpeed = norm(curve.derivative(u0));
    if (!std::isfinite(initialSpeed) || !(initialSpeed > 0.0))
        throw std::domain_error("extendSheet: velocita' iniziale non valida");
    double u1 = u0 + sign * length / std::max(initialSpeed, 1e-300);
    if (!std::isfinite(u1)) throw std::domain_error("extendSheet: parametro d'arco non finito");
    for (int iteration = 0; iteration < 60; ++iteration) {
        cancellationPoint();
        if (!std::isfinite(u1 - u0)) throw std::domain_error("extendSheet: intervallo d'arco non finito");
        const double s = arcLength(curve, {std::min(u0, u1), std::max(u0, u1)}, 1e-13);
        const double speed = norm(curve.derivative(u1));
        if (!std::isfinite(s) || !std::isfinite(speed))
            throw std::domain_error("extendSheet: lunghezza o velocita' d'arco non finita");
        if (!(speed > 0.0)) break;
        const double step = sign * (length - s) / speed;
        if (!std::isfinite(step)) throw std::domain_error("extendSheet: passo d'arco non finito");
        u1 += step;
        if (!std::isfinite(u1)) throw std::domain_error("extendSheet: parametro d'arco non finito");
        if (std::fabs(step) <= 1e-15 * (1.0 + std::fabs(u1))) break;
    }
    // Verifica il parametro effettivamente restituito, dopo l'ultimo passo.
    cancellationPoint();
    const double delta = u1 - u0;
    if (!std::isfinite(delta) || !(sign * delta > 0.0))
        throw std::domain_error("extendSheet: direzione o intervallo d'arco non valido");
    const double achieved = arcLength(curve, {std::min(u0, u1), std::max(u0, u1)}, 1e-13);
    if (!std::isfinite(achieved) || std::fabs(achieved - length) > tolerance)
        throw std::domain_error("extendSheet: lunghezza d'arco richiesta non raggiunta");
    cancellationPoint();
    return u1;
}

}

BSplineCurve<3> extendBSpline(const BSplineCurve<3> &input, double lo, double hi) {
    BSplineCurve<3> curve = input.clamped();
    const int p = curve.degree();
    const Interval domain = curve.domain();
    auto homogeneous = [](const BSplineCurve<3> &c) {
        std::vector<HPoint> h;
        for (int i = 0; i < c.poleCount(); ++i) h.push_back({c.weight(i) * c.poles()[std::size_t(i)], c.weight(i)});
        return h;
    };
    auto rebuild = [&](const std::vector<double> &knots, const std::vector<HPoint> &h) {
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (const HPoint &q : h) {
            if (!(q.w > 0.0)) throw std::domain_error("extendSheet: la curva razionale non si prolunga fin li' (peso non positivo)");
            poles.push_back(q.p / q.w);
            weights.push_back(q.w);
        }
        if (!curve.isRational()) weights.clear();
        return BSplineCurve<3>(p, knots, poles, weights);
    };
    if (hi > domain.hi) {
        // Ultimo tratto di Bezier [a, b] con il nodo a di molteplicita' p, poi de Casteljau in s > 1 (parte sinistra).
        double a = domain.lo;
        for (double k : curve.knots())
            if (k < domain.hi) a = std::max(a, k);
        if (a > domain.lo) curve = curve.insertKnot(a, p - curve.multiplicity(a));
        std::vector<HPoint> h = homogeneous(curve);
        const int n = curve.poleCount();
        const double s = (hi - a) / (domain.hi - a);
        std::vector<HPoint> q(h.begin() + (n - p - 1), h.end());
        std::vector<HPoint> left{q[0]};
        for (int r = 1; r <= p; ++r) {
            for (int i = 0; i + r <= p; ++i) q[std::size_t(i)] = mix(q[std::size_t(i)], q[std::size_t(i + 1)], s);
            left.push_back(q[0]);
        }
        for (int j = 0; j <= p; ++j) h[std::size_t(n - p - 1 + j)] = left[std::size_t(j)];
        std::vector<double> knots = curve.knots();
        for (int j = 0; j <= p; ++j) knots[knots.size() - 1 - std::size_t(j)] = hi;
        curve = rebuild(knots, h);
    }
    if (lo < domain.lo) {
        // Primo tratto [a, b] con il nodo b di molteplicita' p, poi de Casteljau in s < 0 (parte destra).
        const Interval now = curve.domain();
        double b = now.hi;
        for (double k : curve.knots())
            if (k > now.lo) b = std::min(b, k);
        if (b < now.hi) curve = curve.insertKnot(b, p - curve.multiplicity(b));
        std::vector<HPoint> h = homogeneous(curve);
        const double s = (lo - now.lo) / (b - now.lo);
        std::vector<HPoint> q(h.begin(), h.begin() + (p + 1));
        std::vector<HPoint> right(std::size_t(p + 1));
        right[std::size_t(p)] = q[std::size_t(p)];
        for (int r = 1; r <= p; ++r) {
            for (int i = 0; i + r <= p; ++i) q[std::size_t(i)] = mix(q[std::size_t(i)], q[std::size_t(i + 1)], s);
            right[std::size_t(p - r)] = q[std::size_t(p - r)];
        }
        for (int j = 0; j <= p; ++j) h[std::size_t(j)] = right[std::size_t(j)];
        std::vector<double> knots = curve.knots();
        for (int j = 0; j <= p; ++j) knots[std::size_t(j)] = lo;
        curve = rebuild(knots, h);
    }
    return curve;
}

// Prolungamento tensoriale: ogni riga/colonna conserva il polinomio terminale.
BSplineSurface extendBSplineSurface(const BSplineSurface &input, const Interval &u, const Interval &v) {
    BSplineSurface surface = input;
    for (int axis = 0; axis < 2; ++axis) {
        const int count = axis == 0 ? surface.vPoleCount() : surface.uPoleCount();
        std::vector<BSplineCurve<3>> curves;
        for (int j = 0; j < count; ++j) {
            std::vector<Vec3> poles;
            std::vector<double> weights;
            const int along = axis == 0 ? surface.uPoleCount() : surface.vPoleCount();
            for (int i = 0; i < along; ++i) {
                poles.push_back(axis == 0 ? surface.pole(i, j) : surface.pole(j, i));
                if (surface.isRational()) weights.push_back(axis == 0 ? surface.weight(i, j) : surface.weight(j, i));
            }
            const BSplineCurve<3> curve(axis == 0 ? surface.uDegree() : surface.vDegree(),
                                       axis == 0 ? surface.uKnots() : surface.vKnots(), poles, weights);
            const Interval range = axis == 0 ? u : v;
            curves.push_back(extendBSpline(curve, range.lo, range.hi));
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
                if (surface.isRational()) weights.push_back(curve.weight(k));
            }
        surface = BSplineSurface(surface.uDegree(), surface.vDegree(),
                                 axis == 0 ? curves.front().knots() : surface.uKnots(),
                                 axis == 1 ? curves.front().knots() : surface.vKnots(), nu, nv, poles, weights);
    }
    return surface;
}

Body makePlaneSheet(const Frame3 &frame, double halfSize) {
    const double h = halfSize;
    const std::vector<Vec3> corners{frame.toGlobal(Vec3(-h, -h, 0)), frame.toGlobal(Vec3(h, -h, 0)), frame.toGlobal(Vec3(h, h, 0)), frame.toGlobal(Vec3(-h, h, 0))};
    std::vector<Body::BuildEdge> edges;
    for (int i = 0; i < 4; ++i) {
        const Vec3 &a = corners[std::size_t(i)], &b = corners[std::size_t((i + 1) % 4)];
        edges.push_back({i, (i + 1) % 4, std::make_shared<Line<3>>(a, normalized(b - a)), {0.0, distance(a, b)}, 0.0});
    }
    Body::BuildFace face;
    face.surface = std::make_shared<Plane>(frame);
    face.loops.push_back({{0, true, nullptr, 0.0}, {1, true, nullptr, 0.0}, {2, true, nullptr, 0.0}, {3, true, nullptr, 0.0}});
    Body body = Body::buildSheet(corners, edges, {face});
    computePCurves(body);
    return body;
}

// Estensione di una singola faccia piana. Ricostruire il contorno evita le
// strisce sovrapposte quando una seconda estensione raggiunge o percorre i
// connettori creati dalla prima.
Body extendPlanarFace(const Body &body, const std::vector<EdgeId> &selected, double length,
                      double scale, double tolerance) {
    const FaceId faceId = body.faces().front();
    const Face &face = body.face(faceId);
    const Vec3 planeNormal = static_cast<const Plane &>(*face.surface).frame().zDir();
    const auto chosen = [&](EdgeId edge) {
        return std::find(selected.begin(), selected.end(), edge) != selected.end();
    };
    struct Piece {
        EdgeId edge;
        CurvePtr<3> curve;
        Interval range;
        bool sense = true;
        bool selected = false;
        Vec3 start, end;
    };
    std::vector<std::vector<PathSegment>> output;
    for (LoopId loop : face.loops) {
        std::vector<Piece> pieces;
        for (FinId finId : body.loopFins(loop)) {
            const Fin &fin = body.fin(finId);
            const Edge &edge = body.edge(fin.edge);
            Piece piece{fin.edge, edge.curve, edge.range, fin.sense, chosen(fin.edge), {}, {}};
            const double t0 = fin.sense ? edge.range.lo : edge.range.hi;
            const double t1 = fin.sense ? edge.range.hi : edge.range.lo;
            if (piece.selected) {
                const double traversal = fin.sense ? 1.0 : -1.0;
                const double orientation = face.sense ? 1.0 : -1.0;
                const auto parallelPoint = [&](double t) {
                    const Vec3 tangent = traversal * edge.curve->derivative(t);
                    return edge.curve->point(t) + length * normalized(orientation * cross(tangent, planeNormal));
                };
                const double fitTolerance = std::max(tolerance, 1e-8 * scale);
                const auto bound = detail::extrusionOffsetErrorBound(edge.curve, edge.range,
                    length * traversal * orientation, planeNormal);
                piece.curve = bound
                    ? fitCurveCertified(parallelPoint, edge.range, edge.curve->breakpoints(edge.range), fitTolerance, bound)
                    : fitCurve(parallelPoint, edge.range, edge.curve->breakpoints(edge.range), fitTolerance);
            }
            piece.start = piece.curve->point(t0);
            piece.end = piece.curve->point(t1);
            pieces.push_back(std::move(piece));
        }
        if (pieces.empty()) continue;
        // Se l'estremo spostato cade sul bordo adiacente non selezionato, quel
        // bordo si rifila fino al nuovo estremo invece di aggiungere un tratto
        // coincidente percorso due volte.
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            const std::size_t j = (i + 1) % pieces.size();
            Piece &a = pieces[i], &b = pieces[j];
            if (a.selected == b.selected) continue;
            Piece &plain = a.selected ? b : a;
            const Vec3 target = a.selected ? a.end : b.start;
            const CurveProjection<3> projection = projectPoint(*plain.curve, target, plain.range);
            if (projection.distance > 1e3 * tolerance) continue;
            const bool trimStart = &plain == &b;
            const bool canonicalStart = trimStart ? plain.sense : !plain.sense;
            if (canonicalStart) plain.range.lo = projection.parameter;
            else plain.range.hi = projection.parameter;
            if (plain.range.length() > linearResolution()) {
                if (trimStart) plain.start = target;
                else plain.end = target;
            }
        }
        std::vector<std::size_t> active;
        for (std::size_t i = 0; i < pieces.size(); ++i)
            if (pieces[i].range.length() > linearResolution()) active.push_back(i);
        if (active.empty()) throw std::domain_error("extendSheet: l'estensione annulla il contorno planare");
        std::vector<PathSegment> boundary;
        for (std::size_t k = 0; k < active.size(); ++k) {
            Piece &piece = pieces[active[k]];
            if (piece.sense) boundary.push_back({piece.curve, piece.range});
            else boundary.push_back({reversedCurve<3>(piece.curve), {-piece.range.hi, -piece.range.lo}});
            const Piece &next = pieces[active[(k + 1) % active.size()]];
            const Vec3 a = piece.end;
            const Vec3 b = next.start;
            const double gap = distance(a, b);
            if (gap > 1e3 * tolerance)
                boundary.push_back({std::make_shared<Line<3>>(a, (b - a) / gap), {0.0, gap}});
        }
        output.push_back(std::move(boundary));
    }
    Body result = planarSheet(output, std::max(1e-6 * scale, 1e3 * tolerance));
    const Vec3 resultNormal = static_cast<const Plane &>(*result.face(result.faces().front()).surface).frame().zDir();
    if (dot(resultNormal, planeNormal) * (face.sense ? 1.0 : -1.0) < 0.0) {
        // planarSheet orienta dal primo loop; la geometria e' la stessa e il
        // verso viene corretto ricostruendo i loop nell'ordine opposto.
        for (auto &loop : output) std::reverse(loop.begin(), loop.end());
        result = planarSheet(output, std::max(1e-6 * scale, 1e3 * tolerance));
    }
    return result;
}

std::size_t detail::closestSheetPiece(const std::vector<const Body *> &pieces, const Vec3 &keep) {
    if (pieces.empty()) throw std::invalid_argument("closestSheetPiece: nessun pezzo");
    std::size_t best = 0;
    double closest = 1e300;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const double distance = distanceToSheet(*pieces[i], keep);
        if (distance < closest) {
            closest = distance;
            best = i;
        }
    }
    return best;
}

std::size_t detail::closestSheetPiece(const std::vector<Body> &pieces, const Vec3 &keep) {
    std::vector<const Body *> regions;
    regions.reserve(pieces.size());
    for (const auto &piece : pieces) regions.push_back(&piece);
    return closestSheetPiece(regions, keep);
}

Body trimSheet(const Body &sheet, const Body &tool, const Vec3 &keep, double tolerance, BodyHistory *history) {
    if(history)*history={};
    if (!sheet.isSheet()) throw std::domain_error("trimSheet: si tagliano solo le superfici (lamine)");
    BooleanOptions options;
    options.tolerance = tolerance;
    std::vector<BodyHistory> emitted;
    const std::vector<Body> pieces = splitSheet(sheet, tool, options, history?&emitted:nullptr);
    if (pieces.size() < 2) throw std::domain_error("trimSheet: lo strumento non divide la superficie");
    const std::size_t best = detail::closestSheetPiece(pieces, keep);
    if(history)*history=std::move(emitted[best]);
    return pieces[best];
}

namespace {

// Lati delle strisce spline: due facce adiacenti devono proseguire lungo
// un'unica intersezione, anche quando le distanze d'arco danno estremi diversi.
struct SplineExtensionSide {
    int vertex, farVertex, edge, farEdge, face;
    bool vIso;
    Vec2 start;
    double end;
};

// Near-equal normals do not establish an ISO intersection. Inspect the
// ORIGINAL shared fin map before choosing that family: a UV seam whose
// non-ISO variation exceeds its physical source budget requires the implicit
// continuation even at an almost tangent endpoint.
// This selector grants no geometric acceptance; recognized ISO constructions
// must still prove the actual emitted curve on both carriers below.
struct SourceUVInterval { double lo, hi; };
SourceUVInterval sourceUVSum(double a, double b) {
    const double sum = a + b;
    if (!std::isfinite(sum)) return {-INFINITY, INFINITY};
    const double tail = sum - a;
    const double error = (a - (sum - tail)) + (b - tail);
    if (error == 0.0) return {sum, sum};
    return {std::nextafter(sum, -INFINITY), std::nextafter(sum, INFINITY)};
}
SourceUVInterval sourceUVLine(double origin, double direction, double parameter) {
    const double product = direction * parameter;
    if (!std::isfinite(product)) return {-INFINITY, INFINITY};
    const double error = std::fma(direction, parameter, -product);
    // An FMA residual can itself underflow. Simple zero/unit products are
    // exact; otherwise enclose tiny products even when their residual is zero.
    const bool simple = direction == 0.0 || parameter == 0.0 || std::fabs(parameter) == 1.0;
    // Two 53-bit significands can leave a product residual below half the
    // smallest subnormal. Reserve the full 106-bit product range here.
    const bool tiny = !simple && std::fabs(product) < std::ldexp(std::numeric_limits<double>::min(), 106);
    const double lo = error < 0.0 || tiny ? std::nextafter(product, -INFINITY) : product;
    const double hi = error > 0.0 || tiny ? std::nextafter(product, INFINITY) : product;
    return {sourceUVSum(origin, lo).lo, sourceUVSum(origin, hi).hi};
}
// This source-family selector uses a physical, preallocated fin budget.
// Positive NURBS controls bound the full original UV image; Line endpoints
// are enclosed with outward arithmetic. The carrier metric converts only
// the constant-coordinate variation to millimetres, over its natural box.
// It does not certify the original edge or the generated continuation.
bool originalIsoSeed(const CurvePtr<2> &input, const SurfacePtr &surface, const Interval &range,
                     double endpoint, int varying, double budget, Vec2 &seed) {
    const Curve<2> *curve = input.get();
    while (curve) {
        const Interval natural = curve->domain();
        if (range.lo < natural.lo || range.hi > natural.hi) return false;
        if (curve->type() != CurveType::Trimmed) break;
        curve = static_cast<const TrimmedCurve<2> *>(curve)->basis().get();
    }
    if (!curve) return false;
    const int constant = 1 - varying;
    SourceUVInterval hull[2] = {{INFINITY, -INFINITY}, {INFINITY, -INFINITY}};
    seed = input->point(endpoint);
    if (curve->type() == CurveType::Line) {
        const auto &line = static_cast<const Line<2> &>(*curve);
        if (line.direction()[varying] == 0.0) return false;
        for (int axis = 0; axis < 2; ++axis) for (double t : {range.lo, range.hi}) {
            const auto value = sourceUVLine(line.origin()[axis], line.direction()[axis], t);
            hull[axis].lo = std::min(hull[axis].lo, value.lo);
            hull[axis].hi = std::max(hull[axis].hi, value.hi);
        }
        if (line.direction()[constant] == 0.0) seed[constant] = line.origin()[constant];
    } else {
        const auto *spline = dynamic_cast<const BSplineCurve<2> *>(curve);
        if (!spline) return false;
        const auto &poles = spline->poles();
        for (const Vec2 &pole : poles) {
            cancellationPoint();
            for (int axis = 0; axis < 2; ++axis) {
                hull[axis].lo = std::min(hull[axis].lo, pole[axis]);
                hull[axis].hi = std::max(hull[axis].hi, pole[axis]);
            }
        }
        if (hull[varying].lo == hull[varying].hi) return false;
        if (spline->isClamped() && endpoint == spline->domain().lo) seed = poles.front();
        if (spline->isClamped() && endpoint == spline->domain().hi) seed = poles.back();
        if (hull[constant].lo == hull[constant].hi) seed[constant] = poles.front()[constant];
    }
    if (!isFinite(seed)) return false;
    double spread = 0.0;
    for (double value : {hull[constant].lo, hull[constant].hi}) {
        const auto residual = sourceUVSum(value, -seed[constant]);
        spread = std::max({spread, std::fabs(residual.lo), std::fabs(residual.hi)});
    }
    if (spread == 0.0) return true;
    // The straight segment between source and canonical UV must remain in
    // the rectangle for which the ORIGINAL-control derivative bound holds.
    const Interval domains[2] = {surface->uDomain(), surface->vDomain()};
    for (int axis = 0; axis < 2; ++axis)
        if (hull[axis].lo < domains[axis].lo || hull[axis].hi > domains[axis].hi
            || seed[axis] < domains[axis].lo || seed[axis] > domains[axis].hi) return false;
    const auto metric = detail::surfaceParameterBounds(surface);
    if (!metric) return false;
    const double gain = constant == 0 ? metric->u : metric->v;
    const double residual = spread == 0.0 || gain == 0.0 ? 0.0 : std::nextafter(spread * gain, INFINITY);
    return std::isfinite(residual) && residual <= budget;
}

// A planned seam keeps the original cubic coefficients and affine UV map.
// The same pointers identify its emitted fins; no geometric proximity lookup.
struct SplineJoinCertificate {
    CurvePtr<3> curve;
    CurvePtr<2> pcurve;
    double budget;
    int expectedUses;
};

void requireSplineJoin(const CurvePtr<3> &curve, const SurfacePtr &surface,
                       const CurvePtr<2> &pcurve, const Interval &range, double budget) {
    cancellationPoint();
    const auto *spline = dynamic_cast<const BSplineCurve<3> *>(curve.get());
    if (!spline || spline->degree() != 3 || spline->isRational())
        throw std::domain_error("extendSheet: rappresentazione della giunzione certificata modificata");
    const auto data = detail::surfaceCompositionErrorBound(surface, pcurve, range);
    if (!data.bound)
        throw std::domain_error("extendSheet: supporto della giunzione certificata non riconosciuto");
    const auto &knots = spline->knots();
    const auto &poles = spline->poles();
    if (poles.size() < 4 || (poles.size() - 1) % 3 || knots.size() != poles.size() + 4)
        throw std::domain_error("extendSheet: campate della giunzione non riconosciute");
    double covered = range.lo;
    for (std::size_t k = 0; k + 3 < poles.size(); k += 3) {
        cancellationPoint();
        const Interval span{knots[k + 3], knots[k + 4]};
        for (int j = 0; j < 3; ++j)
            if (knots[k + j + 1] != span.lo || knots[k + j + 4] != span.hi)
                throw std::domain_error("extendSheet: nodi della giunzione non riconosciuti");
        if (span.hi <= range.lo || span.lo >= range.hi) continue;
        // Trims must coincide with the planned breaks. Do not certify rounded
        // Bezier extraction as though its controls were the original edge.
        if (span.lo != covered || span.hi > range.hi || !(span.lo < span.hi))
            throw std::domain_error("extendSheet: parametro della giunzione certificata modificato");
        const double error = data.bound(span, {poles[k], poles[k + 1], poles[k + 2], poles[k + 3]});
        if (!std::isfinite(error) || !(error >= 0) || error > budget)
            throw std::domain_error("extendSheet: giunzione fuori dal budget continuo");
        covered = span.hi;
    }
    if (covered != range.hi)
        throw std::domain_error("extendSheet: copertura della giunzione certificata incompleta");
}

struct EmittedSplineJoin {
    FaceId face;
    EdgeId edge;
    CurvePtr<3> curve;
    CurvePtr<2> pcurve;
    Interval range;
    double budget;
};

std::vector<EmittedSplineJoin> emittedSplineJoins(const Body &body,
                                                const std::vector<SplineJoinCertificate> &plans) {
    std::vector<EmittedSplineJoin> result;
    for (const auto &plan : plans) {
        int uses = 0;
        for (FaceId face : body.faces())
            for (LoopId loop : body.face(face).loops)
                for (FinId finId : body.loopFins(loop)) {
                    const Fin &fin = body.fin(finId);
                    if (fin.pcurve != plan.pcurve) continue;
                    const Edge &edge = body.edge(fin.edge);
                    if (edge.curve != plan.curve || edge.tolerance != plan.budget
                        || fin.pcurveTolerance != plan.budget)
                        throw std::domain_error("extendSheet: contratto della giunzione emessa modificato");
                    requireSplineJoin(edge.curve, body.face(face).surface, fin.pcurve, edge.range, plan.budget);
                    result.push_back({face, fin.edge, edge.curve, fin.pcurve, edge.range, plan.budget});
                    ++uses;
                }
        if (uses != plan.expectedUses || uses == 0)
            throw std::domain_error("extendSheet: copertura delle fin pianificate modificata");
    }
    return result;
}

void requireUnifiedSplineJoins(const Body &body, const BodyHistory &history,
                              const std::vector<EmittedSplineJoin> &plans) {
    for (const auto &plan : plans) {
        cancellationPoint();
        const auto faces = history.descendants(EntityKind::Face, 0, plan.face.index);
        const auto edges = history.descendants(EntityKind::Edge, 0, plan.edge.index);
        int uses = 0;
        for (int faceIndex : faces) {
            const FaceId face(faceIndex);
            for (LoopId loop : body.face(face).loops)
                for (FinId finId : body.loopFins(loop)) {
                    const Fin &fin = body.fin(finId);
                    if (std::find(edges.begin(), edges.end(), fin.edge.index) == edges.end()) continue;
                    const Edge &edge = body.edge(fin.edge);
                    if (edge.curve != plan.curve || fin.pcurve != plan.pcurve
                        || edge.range.lo != plan.range.lo || edge.range.hi != plan.range.hi
                        || edge.tolerance != plan.budget || fin.pcurveTolerance != plan.budget)
                        throw std::domain_error("extendSheet: unificazione della giunzione certificata non supportata");
                    requireSplineJoin(edge.curve, body.face(face).surface, fin.pcurve, edge.range, plan.budget);
                    ++uses;
                }
        }
        if (uses != 1)
            throw std::domain_error("extendSheet: genealogia della giunzione certificata ambigua");
    }
}

void joinSplineExtensions(Model &model, const Body &body, const std::vector<SplineExtensionSide> &sides,
                          double scale, std::vector<SplineJoinCertificate> &certificates,
                          std::vector<detail::SheetJoinPlan> &newtonPlans) {
    const double tolerance = std::max(1e-7, 1e-9 * scale);
    for (std::size_t i = 0; i < sides.size(); ++i)
        for (std::size_t j = i + 1; j < sides.size(); ++j) {
            cancellationPoint();
            auto a = sides[i], b = sides[j];
            if (a.vertex != b.vertex || a.face == b.face || a.edge == b.edge) continue;
            EdgeId shared;
            bool ambiguous = false;
            for (EdgeId e : body.edges()) {
                const auto &edge = body.edge(e);
                if (!edge.forward.valid() || !edge.backward.valid()) continue;
                const int f = model.faceIndex.at(body.finFace(edge.forward).index);
                const int g = model.faceIndex.at(body.finFace(edge.backward).index);
                if (!((f == a.face && g == b.face) || (f == b.face && g == a.face))) continue;
                if (model.vertexIndex.at(body.edgeStart(e).index) != a.vertex
                    && model.vertexIndex.at(body.edgeEnd(e).index) != a.vertex) continue;
                ambiguous = ambiguous || shared.valid();
                shared = e;
            }
            if (!shared.valid()) continue;
            const SurfacePtr sa = model.faces[a.face].surface, sb = model.faces[b.face].surface;
            const int da = a.vIso ? 1 : 0, db = b.vIso ? 1 : 0;
            bool sourceIso = !ambiguous;
            const Edge &sourceEdge = body.edge(shared);
            const double sourceEndpoint = model.vertexIndex.at(body.edgeStart(shared).index) == a.vertex
                ? sourceEdge.range.lo : sourceEdge.range.hi;
            Vec2 sourceA, sourceB;
            for (FinId fin : {sourceEdge.forward, sourceEdge.backward}) {
                const int face = model.faceIndex.at(body.finFace(fin).index);
                const bool first = face == a.face;
                const int varying = (first ? a.vIso : b.vIso) ? 1 : 0;
                if (!std::isfinite(sourceEdge.tolerance) || sourceEdge.tolerance < 0.0
                    || !std::isfinite(body.fin(fin).pcurveTolerance) || body.fin(fin).pcurveTolerance < 0.0)
                    throw std::domain_error("extendSheet: budget della giunzione sorgente non valido");
                const double sourceBudget = std::min(tolerance,
                    std::max(linearResolution(), sourceEdge.tolerance) + body.fin(fin).pcurveTolerance);
                const bool iso = originalIsoSeed(body.fin(fin).pcurve, first ? sa : sb, sourceEdge.range,
                                                sourceEndpoint, varying, sourceBudget, first ? sourceA : sourceB);
                sourceIso = sourceIso && iso;
            }
            if (sourceIso) {
                a.start = sourceA;
                b.start = sourceB;
            }
            const double sign = a.end > a.start[da] ? 1.0 : -1.0;
            const double signB = b.end > b.start[db] ? 1.0 : -1.0;
            const double endA = std::fabs(a.end - a.start[da]);
            Vec3 startA[4], startB[4];
            sa->evaluate(a.start.x(), a.start.y(), 1, startA);
            sb->evaluate(b.start.x(), b.start.y(), 1, startB);
            const double rate = norm(startA[da == 1 ? 1 : 2]) / norm(startB[db == 1 ? 1 : 2]);
            if (!(rate > 0.0) || !std::isfinite(rate))
                throw std::domain_error("extendSheet: giunzione su una superficie singolare");
            struct Sample { Vec2 a, b; Vec3 point; };
            // followSeam: le due facce proseguono lungo le loro isoparametriche
            // della cucitura (vedi sotto, giunzioni quasi tangenti).
            bool followSeam = false;
            CurvePtr<2> affineA, affineB;
            SurfacePtr certifiedA, certifiedB;
            const auto sample = [&](double t) {
                cancellationPoint();
                Sample q{a.start, b.start, {}};
                q.a[da] += sign * t;
                q.b[db] += signB * rate * t;
                if (followSeam) {
                    q.a = affineA->point(t); q.b = affineB->point(t);
                    q.point = certifiedA->point(q.a.x(), q.a.y());
                    return q;
                }
                for (int iteration = 0; iteration < 32; ++iteration) {
                    cancellationPoint();
                    Vec3 ja[4], jb[4];
                    sa->evaluate(q.a.x(), q.a.y(), 1, ja);
                    sb->evaluate(q.b.x(), q.b.y(), 1, jb);
                    const Vec3 r = jb[0] - ja[0];
                    const Vec3 x = ja[da == 1 ? 2 : 1], y = -jb[2], z = -jb[1];
                    const double nx = norm(x), ny = norm(y), nz = norm(z);
                    if (!(nx > 0 && ny > 0 && nz > 0)) break;
                    const Vec3 columns[3] = {x / nx, y / ny, z / nz};
                    // Regolarizzazione vicino alla tangenza: l'intersezione
                    // delle approssimazioni non deve amplificare il rumore UV.
                    Vec3 m[3];
                    for (int row = 0; row < 3; ++row)
                        for (int col = 0; col < 3; ++col) {
                            m[col][row] = row == col ? (iteration < 16 ? 1e-8 : 1e-12) : 0.0;
                            for (const auto &c : columns) m[col][row] += c[row] * c[col];
                        }
                    const double det = dot(m[0], cross(m[1], m[2]));
                    if (!(std::fabs(det) > 1e-24)) break;
                    const Vec3 correction(dot(r, cross(m[1], m[2])) / det,
                                          dot(m[0], cross(r, m[2])) / det,
                                          dot(m[0], cross(m[1], r)) / det);
                    q.a[1 - da] += dot(columns[0], correction) / nx;
                    q.b[0] += dot(columns[1], correction) / ny;
                    q.b[1] += dot(columns[2], correction) / nz;
                    if (!isFinite(q.a) || !isFinite(q.b)) break;
                    if (iteration == 31) {
                        const Vec3 pa = sa->point(q.a.x(), q.a.y()), pb = sb->point(q.b.x(), q.b.y());
                        if (distance(pa, pb) <= tolerance) { q.point = 0.5 * (pa + pb); return q; }
                    }
                }
                throw std::domain_error("extendSheet: impossibile prolungare la giunzione tra le facce "
                                        + std::to_string(a.face) + " e " + std::to_string(b.face));
            };
            // Estremo della seconda faccia sulla stessa curva. Il parametro
            // cresce sempre verso l'esterno, anche all'estremita' iniziale.
            double endB = endA, end = endA;
            CurvePtr<3> curve;
            const auto join = [&]() {
                double lo = 0.0, hi = endA;
                const auto beyondB = [&](double t) { return signB * (sample(t).b[db] - b.end) >= 0.0; };
                for (int k = 0; !beyondB(hi); ++k) {
                    if (k == 12) throw std::domain_error("extendSheet: giunzione fuori dalla regione di estensione");
                    hi *= 1.25;
                }
                for (int k = 0; k < 45; ++k) {
                    const double mid = 0.5 * (lo + hi);
                    if (beyondB(mid)) hi = mid; else lo = mid;
                }
                endB = 0.5 * (lo + hi);
                if (distance(sample(endA).point, sample(endB).point) <= tolerance) endB = endA;
                end = std::max(endA, endB);
                curve = fitCurve([&](double t) { return sample(t).point; }, {0.0, end},
                                 {0.0, std::min(endA, endB), end}, tolerance);
            };
            const auto affineJoin = [&]() {
                endB = std::fabs(b.end - b.start[db]) / rate;
                if (!(endA > 0 && endB > 0) || !std::isfinite(endB))
                    throw std::domain_error("extendSheet: intervallo affine della giunzione non valido");
                Vec2 farA = a.start, farB = b.start;
                farA[da] = a.end; farB[db] = b.end;
                if (distance(sa->point(farA.x(), farA.y()), sb->point(farB.x(), farB.y())) <= tolerance)
                    endB = endA;
                end = std::max(endA, endB);
                std::vector<double> cuts{0.0, std::min(endA, endB), end};
                cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
                const auto uvCurve = [&](const SplineExtensionSide &side, int axis, double speed, double finish) {
                    std::vector<Vec2> poles;
                    for (double t : cuts) {
                        Vec2 uv = side.start;
                        uv[axis] = t == finish ? side.end : side.start[axis] + speed * t;
                        if (!isFinite(uv)) throw std::domain_error("extendSheet: dominio affine non finito");
                        poles.push_back(uv);
                    }
                    auto knots = cuts;
                    knots.insert(knots.begin(), cuts.front()); knots.push_back(cuts.back());
                    return std::make_shared<BSplineCurve<2>>(1, knots, poles);
                };
                affineA = uvCurve(a, da, sign, endA);
                affineB = uvCurve(b, db, signB * rate, endB);
                const auto extendedSupport = [&](const SurfacePtr &surface, const CurvePtr<2> &pc) -> SurfacePtr {
                    Interval u = surface->uDomain(), v = surface->vDomain();
                    // An affine span stays in the convex hull of its ORIGINAL
                    // UV endpoints. No sampling is needed to bound its domain.
                    const auto &uv = static_cast<const BSplineCurve<2> &>(*pc);
                    for (const Vec2 &p : uv.poles()) {
                        u.lo = std::min(u.lo, p.x()); u.hi = std::max(u.hi, p.x());
                        v.lo = std::min(v.lo, p.y()); v.hi = std::max(v.hi, p.y());
                    }
                    return std::make_shared<BSplineSurface>(extendBSplineSurface(
                        static_cast<const BSplineSurface &>(*surface), u, v));
                };
                certifiedA = extendedSupport(sa, affineA);
                certifiedB = extendedSupport(sb, affineB);
                const auto boundA = detail::surfaceCompositionErrorBound(certifiedA, affineA, {0.0, end});
                const auto boundB = detail::surfaceCompositionErrorBound(certifiedB, affineB, {0.0, end});
                if (!boundA.bound || !boundB.bound)
                    throw std::domain_error("extendSheet: giunzione affine non certificabile");
                cuts.insert(cuts.end(), boundA.breaks.begin(), boundA.breaks.end());
                cuts.insert(cuts.end(), boundB.breaks.begin(), boundB.breaks.end());
                // Fit and connection allocations are fixed before construction.
                // The independent B residual must fit the same connection cap.
                curve = fitCurveCertified([&](double t) {
                    const Vec2 uv = affineA->point(t);
                    return certifiedA->point(uv.x(), uv.y());
                }, {0.0, end}, cuts, 0.25 * tolerance, boundA.bound);
                requireSplineJoin(curve, certifiedA, affineA, {0.0, end}, tolerance);
                requireSplineJoin(curve, certifiedB, affineB, {0.0, end}, tolerance);
                followSeam = true;
            };
            const Vec3 normalA = normalized(cross(startA[1], startA[2]));
            const Vec3 normalB = normalized(cross(startB[1], startB[2]));
            const double sine = norm(cross(normalA, normalB));
            if (!std::isfinite(sine))
                throw std::domain_error("extendSheet: normale della giunzione non valida");
            if (sine < 1e-3 && sourceIso) {
                // Only an ORIGINAL ISO image selects this affine continuation.
                // The two-carrier proof verifies its affine correspondence; the
                // old edge parameter need not itself be affine in UV.
                // Recognized failures remain terminal: sampled Newton must
                // never re-admit a failed continuous ISO construction.
                affineJoin();
            } else {
                // General source UV maps require the implicit continuation,
                // including almost tangent endpoints. This existing Newton
                // path remains explicitly sampled, outside the ISO certificate.
                join();
            }
            const double joinTolerance = tolerance;
            // Le superfici restano identiche sul dominio originale. Espandiamo
            // solo il loro dominio per includere il nuovo bordo rifilato.
            Interval au = sa->uDomain(), av = sa->vDomain(), bu = sb->uDomain(), bv = sb->vDomain();
            if (!followSeam) {
                // Explicitly sampled legacy domain envelope for Newton joins.
                for (int k = 0; k <= 64; ++k) {
                    const auto q = sample(end * k / 64.0);
                    au.lo = std::min(au.lo, q.a.x()); au.hi = std::max(au.hi, q.a.x());
                    av.lo = std::min(av.lo, q.a.y()); av.hi = std::max(av.hi, q.a.y());
                    bu.lo = std::min(bu.lo, q.b.x()); bu.hi = std::max(bu.hi, q.b.x());
                    bv.lo = std::min(bv.lo, q.b.y()); bv.hi = std::max(bv.hi, q.b.y());
                }
            }
            const SurfacePtr newA = followSeam ? certifiedA : std::make_shared<BSplineSurface>(
                extendBSplineSurface(static_cast<const BSplineSurface &>(*sa), au, av));
            const SurfacePtr newB = followSeam ? certifiedB : std::make_shared<BSplineSurface>(
                extendBSplineSurface(static_cast<const BSplineSurface &>(*sb), bu, bv));
            for (auto &face : model.faces) {
                if (face.surface == sa) face.surface = newA;
                else if (face.surface == sb) face.surface = newB;
            }
            const auto setFar = [&](const SplineExtensionSide &side, const Sample &q, double t, bool first) {
                model.points[side.farVertex] = curve->point(t);
                auto &far = model.edges[side.farEdge];
                const Vec2 uv = first ? q.a : q.b;
                const double parameter = uv[side.vIso ? 0 : 1];
                if (far.start == side.farVertex) far.range.lo = parameter;
                else far.range.hi = parameter;
                const auto &surface = model.faces[side.face].surface;
                far.curve = side.vIso ? surface->vIso(side.end) : surface->uIso(side.end);
            };
            setFar(a, sample(endA), endA, true);
            setFar(b, sample(endB), endB, false);
            const double startError = distance(curve->point(0.0), model.points[a.vertex]);
            if (startError > (followSeam ? std::max(tolerance, model.pointTolerance[a.vertex])
                                        : std::max(10.0 * tolerance, 1.01 * model.pointTolerance[a.vertex])))
                throw std::domain_error("extendSheet: il prolungamento non incontra la giunzione originale");
            if (!followSeam) model.pointTolerance[a.vertex] = std::max(model.pointTolerance[a.vertex], 1.01 * startError);
            const auto &shorter = endA <= endB ? a : b;
            const auto &longer = endA <= endB ? b : a;
            const double shortEnd = std::min(endA, endB);
            const int common = model.addEdge(a.vertex, shorter.farVertex, curve, {0.0, shortEnd}, joinTolerance);
            const int extra = endA == endB ? -1 : model.addEdge(shorter.farVertex, longer.farVertex, curve, {shortEnd, end}, joinTolerance);
            if (followSeam) {
                for (int vertex : {shorter.farVertex, longer.farVertex, a.vertex})
                    model.pointTolerance[std::size_t(vertex)] = std::max(model.pointTolerance[std::size_t(vertex)], joinTolerance);
                for (const auto *side : {&a, &b})
                    model.edges[std::size_t(side->farEdge)].tolerance = std::max(model.edges[std::size_t(side->farEdge)].tolerance, joinTolerance);
            }
            if (extra < 0 && a.farVertex != b.farVertex) {
                for (auto &edge : model.edges) {
                    if (edge.start == b.farVertex) edge.start = a.farVertex;
                    if (edge.end == b.farVertex) edge.end = a.farVertex;
                }
            }
            int usesA = 0, usesB = 0;
            for (const auto *side : {&a, &b}) {
                const bool fromBase = model.edges[side->edge].start == a.vertex;
                for (std::size_t faceIndex = 0; faceIndex < model.faces.size(); ++faceIndex)
                    for (auto &loop : model.faces[faceIndex].loops) {
                        std::vector<Body::BuildFin> rebuilt;
                        for (const auto &fin : loop) {
                            if (fin.edge != side->edge) { rebuilt.push_back(fin); continue; }
                            const bool forward = fin.sense == fromBase;
                            if (!followSeam) {
                                newtonPlans.push_back({int(faceIndex), common, joinTolerance, curve, {0.0, shortEnd}});
                                if (side == &longer && extra >= 0)
                                    newtonPlans.push_back({int(faceIndex), extra, joinTolerance, curve, {shortEnd, end}});
                            }
                            const CurvePtr<2> pc = followSeam ? (side == &a ? affineA : affineB) : nullptr;
                            const double pcBudget = followSeam ? tolerance : 0.0;
                            if (followSeam) (side == &a ? usesA : usesB) += side == &longer && extra >= 0 ? 2 : 1;
                            if (side == &longer && extra >= 0 && !forward) rebuilt.push_back({extra, false, pc, pcBudget});
                            rebuilt.push_back({common, forward, pc, pcBudget});
                            if (side == &longer && extra >= 0 && forward) rebuilt.push_back({extra, true, pc, pcBudget});
                        }
                        loop = std::move(rebuilt);
                    }
            }
            if (followSeam) {
                certificates.push_back({curve, affineA, tolerance, usesA});
                certificates.push_back({curve, affineB, tolerance, usesB});
            }
        }
}

}

Body extendSheet(const Body &input, const std::vector<EdgeId> &selected, double length, bool linear, BodyHistory *history) {
    if(history)*history={};
    cancellationPoint();
    if (!input.isSheet()) throw std::domain_error("extendSheet: si estendono solo le superfici (lamine)");
    if (!(length > linearResolution())) throw std::domain_error("extendSheet: distanza non valida");
    Body body = input;
    computePCurves(body);
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    const double scale = std::max(box.diagonal(), 1.0), tolerance = 1e-9 * scale;

    if (body.faces().size() == 1 && body.face(body.faces().front()).surface->type() == SurfaceType::Plane) {
        Body result=extendPlanarFace(body, selected, length, scale, tolerance);
        if(history)*history=detail::generatedFaces(input,result,{{0,EntityKind::Face,body.faces().front().index,HistoryRelation::Modified}});
        return result;
    }

    Model model(body,history?&input:nullptr);
    // Superfici prolungate (curva base estesa) per faccia del modello.
    std::map<int, SurfacePtr> extended;
    std::vector<SplineExtensionSide> splineSides;
    std::vector<int> newPoints;
    auto pointAt = [&](const Vec3 &p) {
        for (int index : newPoints)
            if (distance(model.points[std::size_t(index)], p) <= 1e3 * tolerance) return index;
        const int index = model.addPoint(p);
        newPoints.push_back(index);
        return index;
    };
    // Edge nuovi tra due punti (condivisi tra strisce vicine).
    std::map<std::pair<int, int>, int> newEdges;
    auto edgeBetween = [&](int a, int b, const CurvePtr<3> &curve, const Interval &range) {
        const auto key = std::make_pair(std::min(a, b), std::max(a, b));
        const auto found = newEdges.find(key);
        if (found != newEdges.end()) return found->second;
        const bool forward = distance(curve->point(range.lo), model.points[std::size_t(a)]) <= distance(curve->point(range.lo), model.points[std::size_t(b)]);
        const int e = model.addEdge(forward ? a : b, forward ? b : a, curve, range);
        newEdges[key] = e;
        return e;
    };
    auto finFrom = [&](int edge, int from) { return Body::BuildFin{edge, model.edges[std::size_t(edge)].start == from, nullptr, 0.0}; };

    for (EdgeId e : selected) {
        if (!body.isLaminar(e)) throw std::domain_error("extendSheet: si estendono solo i bordi della superficie");
        const Edge &edge = body.edge(e);
        const FinId fin = edge.forward.valid() ? edge.forward : edge.backward;
        const Fin &finData = body.fin(fin);
        const FaceId f = body.finFace(fin);
        const Face &face = body.face(f);
        const int modelFace = model.faceIndex.at(f.index);
        if (!finData.pcurve) throw std::domain_error("extendSheet: bordo senza curva nello spazio (u, v)");
        const Vec2 uv0 = finData.pcurve->point(edge.range.lo), uv1 = finData.pcurve->point(edge.range.hi),
                   uvm = finData.pcurve->point(0.5 * (edge.range.lo + edge.range.hi));
        const double du = std::fabs(uv1.x() - uv0.x()) + std::fabs(uvm.x() - uv0.x()), dv = std::fabs(uv1.y() - uv0.y()) + std::fabs(uvm.y() - uv0.y());
        const double flat = 1e-9 * (1.0 + std::fabs(uv0.x()) + std::fabs(uv0.y()));
        const bool vIso = dv <= flat && du > flat, uIso = du <= flat && dv > flat;
        const bool generalPlanar = !vIso && !uIso && face.surface->type() == SurfaceType::Plane;
        if (!vIso && !uIso && !generalPlanar)
            throw std::domain_error("extendSheet: solo bordi isoparametrici (in alto, in basso o agli estremi di una superficie estrusa)");
        // Lato della faccia nello spazio (u, v): a sinistra della fin se la faccia ha il verso della superficie.
        const Vec2 d = finData.sense ? uv1 - uv0 : uv0 - uv1;
        const Vec2 left(-d.y(), d.x());
        const Vec2 side = face.sense ? left : -left;
        const double outward = -((vIso ? side.y() : side.x()) > 0.0 ? 1.0 : -1.0);
        SurfacePtr surface = face.surface;
        const auto found = extended.find(modelFace);
        if (found != extended.end()) surface = found->second;
        const SurfaceType type = surface->type();
        const int A = model.vertexIndex.at(body.finStart(fin).index), B = model.vertexIndex.at(body.finEnd(fin).index);
        const Vec2 uvA = finData.sense ? uv0 : uv1, uvB = finData.sense ? uv1 : uv0;
        Body::BuildFace strip;
        strip.sense = face.sense;
        int sideA = -1, sideB = -1, far = -1, farA = -1, farB = -1;
        double splineEnd = 0.0;
        if (generalPlanar) {
            // Un piano puo' essere prolungato anche da un bordo rifilato
            // curvo: la nuova frontiera e' la parallela complanare del bordo.
            // Si usa il verso della fin per scegliere il lato esterno, ma la
            // curva conserva il parametro e il verso canonico dell'edge.
            const Vec3 planeNormal = static_cast<const Plane &>(*surface).frame().zDir();
            const double traversal = finData.sense ? 1.0 : -1.0;
            const double orientation = face.sense ? 1.0 : -1.0;
            const auto parallelPoint = [&](double t) {
                const Vec3 tangent = traversal * edge.curve->derivative(t);
                const Vec3 direction = normalized(orientation * cross(tangent, planeNormal));
                return edge.curve->point(t) + length * direction;
            };
            const std::vector<double> breaks = edge.curve->breakpoints(edge.range);
            const double fitTolerance = std::max(tolerance, 1e-8 * scale);
            const auto bound = detail::extrusionOffsetErrorBound(edge.curve, edge.range,
                length * traversal * orientation, planeNormal);
            const CurvePtr<3> parallel = bound ? fitCurveCertified(parallelPoint, edge.range, breaks, fitTolerance, bound)
                                             : fitCurve(parallelPoint, edge.range, breaks, fitTolerance);
            const double parameterA = finData.sense ? edge.range.lo : edge.range.hi;
            const double parameterB = finData.sense ? edge.range.hi : edge.range.lo;
            farA = pointAt(parallel->point(parameterA));
            farB = pointAt(parallel->point(parameterB));
            const Vec3 pA = model.points[std::size_t(A)], pB = model.points[std::size_t(B)];
            const Vec3 qA = model.points[std::size_t(farA)], qB = model.points[std::size_t(farB)];
            sideA = edgeBetween(A, farA, std::make_shared<Line<3>>(pA, normalized(qA - pA)), {0.0, distance(pA, qA)});
            sideB = edgeBetween(B, farB, std::make_shared<Line<3>>(pB, normalized(qB - pB)), {0.0, distance(pB, qB)});
            far = edgeBetween(farA, farB, parallel, edge.range);
            strip.surface = surface;
        } else if (type == SurfaceType::BSpline && !linear) {
            const auto &spline = static_cast<const BSplineSurface &>(*surface);
            const double start = vIso ? uv0.y() : uv0.x();
            CurvePtr<3> crossCurve = vIso ? surface->uIso(uvm.x()) : surface->vIso(uvm.y());
            const double speed = norm(crossCurve->derivative(start));
            if (!(speed > linearResolution())) throw std::domain_error("extendSheet: direzione di estensione singolare");
            const double guess = start + outward * 2.0 * length / speed;
            const auto &crossSpline = static_cast<const BSplineCurve<3> &>(*crossCurve);
            const auto reach = extendBSpline(crossSpline, std::min(crossSpline.domain().lo, guess), std::max(crossSpline.domain().hi, guess));
            const double end = arcParameter(reach, start, length, outward, tolerance);
            splineEnd = end;
            Interval ur = surface->uDomain(), vr = surface->vDomain();
            Interval &range = vIso ? vr : ur;
            range.lo = std::min(range.lo, end);
            range.hi = std::max(range.hi, end);
            surface = std::make_shared<BSplineSurface>(extendBSplineSurface(spline, ur, vr));
            extended[modelFace] = surface;
            model.faces[std::size_t(modelFace)].surface = surface;
            strip.surface = surface;
            const Interval extension{std::min(start, end), std::max(start, end)};
            farA = pointAt(vIso ? surface->point(uvA.x(), end) : surface->point(end, uvA.y()));
            farB = pointAt(vIso ? surface->point(uvB.x(), end) : surface->point(end, uvB.y()));
            sideA = edgeBetween(A, farA, vIso ? surface->uIso(uvA.x()) : surface->vIso(uvA.y()), extension);
            sideB = edgeBetween(B, farB, vIso ? surface->uIso(uvB.x()) : surface->vIso(uvB.y()), extension);
            far = edgeBetween(farA, farB, vIso ? surface->vIso(end) : surface->uIso(end),
                              vIso ? Interval{std::min(uvA.x(), uvB.x()), std::max(uvA.x(), uvB.x())}
                                   : Interval{std::min(uvA.y(), uvB.y()), std::max(uvA.y(), uvB.y())});
        } else if (vIso) {
            // Lungo v le isoparametriche sono rette con velocita' costante.
            if (type != SurfaceType::Plane && type != SurfaceType::Cylinder && type != SurfaceType::Cone && type != SurfaceType::Extrusion)
                throw std::domain_error("extendSheet: estensione non gestita per questo tipo di superficie");
            Vec3 derivatives[4];
            surface->evaluate(uvm.x(), uvm.y(), 1, derivatives);
            const double rate = norm(derivatives[Surface::derivativeIndex(0, 1, 1)]);
            const double v0 = uv0.y(), v1 = v0 + outward * length / rate;
            if (!surface->isVPeriodic() && !surface->vDomain().contains(v1))
                throw std::domain_error("extendSheet: l'estensione esce dalla superficie (vertice del cono)");
            strip.surface = surface;
            farA = pointAt(surface->point(uvA.x(), v1));
            farB = pointAt(surface->point(uvB.x(), v1));
            const Interval vRange{std::min(v0, v1), std::max(v0, v1)};
            sideA = edgeBetween(A, farA, surface->uIso(uvA.x()), vRange);
            sideB = edgeBetween(B, farB, surface->uIso(uvB.x()), vRange);
            far = edgeBetween(farA, farB, surface->vIso(v1), {std::min(uvA.x(), uvB.x()), std::max(uvA.x(), uvB.x())});
        } else if (linear && type != SurfaceType::Plane) {
            // Striscia piana tangente: la direzione S_u (costante lungo l'edge rettilineo) verso l'esterno.
            const Vec3 pA = model.points[std::size_t(A)], pB = model.points[std::size_t(B)];
            Vec3 da[4], db[4];
            surface->evaluate(uvA.x(), uvA.y(), 1, da);
            surface->evaluate(uvB.x(), uvB.y(), 1, db);
            const Vec3 m = outward * normalized(da[Surface::derivativeIndex(1, 0, 1)]);
            if (norm(m - outward * normalized(db[Surface::derivativeIndex(1, 0, 1)])) > 1e-9 || norm(cross(normalized(pB - pA), normalized(edge.curve->derivative(edge.range.lo)))) > 1e-9)
                throw std::domain_error("extendSheet: estensione lineare solo lungo bordi rettilinei con la tangente costante");
            Vec3 n = cross(da[Surface::derivativeIndex(1, 0, 1)], da[Surface::derivativeIndex(0, 1, 1)]);
            n = normalized(face.sense ? n : -n);
            strip.surface = std::make_shared<Plane>(Frame3(pA, n, m));
            strip.sense = true;
            farA = pointAt(pA + length * m);
            farB = pointAt(pB + length * m);
            sideA = edgeBetween(A, farA, std::make_shared<Line<3>>(pA, m), {0.0, length});
            sideB = edgeBetween(B, farB, std::make_shared<Line<3>>(pB, m), {0.0, length});
            const Vec3 qA = model.points[std::size_t(farA)], qB = model.points[std::size_t(farB)];
            far = edgeBetween(farA, farB, std::make_shared<Line<3>>(qA, normalized(qB - qA)), {0.0, distance(qA, qB)});
        } else {
            // Stessa superficie lungo u: piano, cilindro (arco), superficie estrusa (curva base prolungata).
            const double u0 = uv0.x();
            double u1 = 0.0;
            if (type == SurfaceType::Plane) {
                u1 = u0 + outward * length;
            } else if (type == SurfaceType::Cylinder) {
                u1 = u0 + outward * length / static_cast<const CylindricalSurface &>(*surface).radius();
            } else if (type == SurfaceType::Extrusion) {
                const auto &extrusion = static_cast<const ExtrusionSurface &>(*surface);
                CurvePtr<3> curve = extrusion.curve();
                const Interval domain = curve->domain();
                // Oltre la fine della curva base si prolunga il suo polinomio.
                CurvePtr<3> reach = curve;
                if (curve->type() == CurveType::BSpline) {
                    const auto &spline = static_cast<const BSplineCurve<3> &>(*curve);
                    const double guess = u0 + outward * 2.0 * length / std::max(norm(curve->derivative(u0)), 1e-300);
                    reach = std::make_shared<BSplineCurve<3>>(extendBSpline(spline, std::min(domain.lo, guess), std::max(domain.hi, guess)));
                }
                u1 = arcParameter(*reach, u0, length, outward, tolerance);
                if (!extrusion.isUPeriodic() && !domain.contains(u1)) {
                    if (curve->type() != CurveType::BSpline) throw std::domain_error("extendSheet: la curva base non si prolunga");
                    const auto &spline = static_cast<const BSplineCurve<3> &>(*curve);
                    surface = std::make_shared<ExtrusionSurface>(std::make_shared<BSplineCurve<3>>(extendBSpline(spline, std::min(domain.lo, u1), std::max(domain.hi, u1))),
                                                                 extrusion.direction());
                    extended[modelFace] = surface;
                    model.faces[std::size_t(modelFace)].surface = surface;
                }
            } else {
                throw std::domain_error("extendSheet: estensione lungo questa superficie non gestita");
            }
            strip.surface = surface;
            farA = pointAt(surface->point(u1, uvA.y()));
            farB = pointAt(surface->point(u1, uvB.y()));
            const Interval uRange{std::min(u0, u1), std::max(u0, u1)};
            sideA = edgeBetween(A, farA, surface->vIso(uvA.y()), uRange);
            sideB = edgeBetween(B, farB, surface->vIso(uvB.y()), uRange);
            far = edgeBetween(farA, farB, surface->uIso(u1), {std::min(uvA.y(), uvB.y()), std::max(uvA.y(), uvB.y())});
        }
        if (!strip.surface) throw std::logic_error("extendSheet: striscia senza superficie");
        // Loop: l'edge al contrario (da B ad A), poi il lato in A, il bordo nuovo, il lato in B.
        const int oldEdge = model.edgeIndex.at(e.index);
        strip.loops.push_back({{oldEdge, !finData.sense, nullptr, 0.0}, finFrom(sideA, A), finFrom(far, farA), finFrom(sideB, farB)});
        model.addGeneratedFace(std::move(strip),EntityKind::Edge,e.index);
        if (type == SurfaceType::BSpline && !linear) {
            splineSides.push_back({A, farA, sideA, far, modelFace, vIso, uvA, splineEnd});
            splineSides.push_back({B, farB, sideB, far, modelFace, vIso, uvB, splineEnd});
        }
    }
    std::vector<SplineJoinCertificate> certificates;
    std::vector<detail::SheetJoinPlan> newtonPlans;
    joinSplineExtensions(model, body, splineSides, scale, certificates, newtonPlans);
    BodyHistory built,unified;
    std::vector<EdgeId> modelEdges;
    Body result = model.build(history?&built:nullptr, &modelEdges);
    computePCurves(result);
    const auto newtonEmitted = detail::certifySheetJoins(result, newtonPlans, modelEdges);
    const auto emitted = emittedSplineJoins(result, certificates);
    result = unifySameDomain(result,1e-6,(history || !emitted.empty() || !newtonEmitted.empty())?&unified:nullptr);
    requireUnifiedSplineJoins(result, unified, emitted);
    detail::requireUnifiedSheetJoins(result, unified, newtonEmitted);
    const std::vector<CheckIssue> issues = checkBody(result);
    if (!issues.empty()) throw std::domain_error("extendSheet: superficie estesa non valida (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    cancellationPoint();
    if(history)*history=BodyHistory::compose(built,unified,0);
    return result;
}

}
