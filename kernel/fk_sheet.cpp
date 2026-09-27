#include "fk_sheet.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "fk_blend_model.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
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
double arcParameter(const Curve<3> &curve, double u0, double length, double sign) {
    double u1 = u0 + sign * length / std::max(norm(curve.derivative(u0)), 1e-300);
    for (int iteration = 0; iteration < 60; ++iteration) {
        const double s = arcLength(curve, {std::min(u0, u1), std::max(u0, u1)}, 1e-13);
        const double speed = norm(curve.derivative(u1));
        if (!(speed > 0.0)) break;
        const double step = sign * (length - s) / speed;
        u1 += step;
        if (std::fabs(step) <= 1e-15 * (1.0 + std::fabs(u1))) break;
    }
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

Body trimSheet(const Body &sheet, const Body &tool, const Vec3 &keep, double tolerance) {
    if (!sheet.isSheet()) throw std::domain_error("trimSheet: si tagliano solo le superfici (lamine)");
    BooleanOptions options;
    options.tolerance = tolerance;
    const std::vector<Body> pieces = splitSheet(sheet, tool, options);
    if (pieces.size() < 2) throw std::domain_error("trimSheet: lo strumento non divide la superficie");
    std::size_t best = 0;
    double closest = 1e300;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const double d = distanceToSheet(pieces[i], keep);
        if (d < closest) closest = d, best = i;
    }
    return pieces[best];
}

Body extendSheet(const Body &input, const std::vector<EdgeId> &selected, double length, bool linear) {
    if (!input.isSheet()) throw std::domain_error("extendSheet: si estendono solo le superfici (lamine)");
    if (!(length > kLinearResolution)) throw std::domain_error("extendSheet: distanza non valida");
    Body body = input;
    computePCurves(body);
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    const double scale = std::max(box.diagonal(), 1.0), tolerance = 1e-9 * scale;

    Model model(body);
    // Superfici prolungate (curva base estesa) per faccia del modello.
    std::map<int, SurfacePtr> extended;
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
        if (!vIso && !uIso) throw std::domain_error("extendSheet: solo bordi isoparametrici (in alto, in basso o agli estremi di una superficie estrusa)");
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
        if (vIso) {
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
                u1 = arcParameter(*reach, u0, length, outward);
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
        model.faces.push_back(std::move(strip));
    }
    Body result = model.build();
    computePCurves(result);
    result = unifySameDomain(result);
    const std::vector<CheckIssue> issues = checkBody(result);
    if (!issues.empty()) throw std::domain_error("extendSheet: superficie estesa non valida (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    return result;
}

}
