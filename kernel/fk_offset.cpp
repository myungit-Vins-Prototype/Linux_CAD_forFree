#include "fk_offset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

#include "fk_bspline_surface.h"
#include "fk_exchange.h"
#include "fk_intersect.h"
#include "fk_pcurve.h"
#include "fk_surface.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"

namespace ForgeCad::Kernel {

namespace {

// Derivata di f in t, del secondo ordine: centrale (side 0) o da una parte
// (side +1 verso t crescenti, -1 verso t decrescenti: dentro un tratto liscio
// quando dall'altra parte c'e' un angolo o il bordo del dominio).
template <class F>
auto derivative(const F &f, double t, double h, int side) -> decltype(f(t)) {
    if (side > 0) return (-3.0 * f(t) + 4.0 * f(t + h) - f(t + 2.0 * h)) / (2.0 * h);
    if (side < 0) return (3.0 * f(t) - 4.0 * f(t - h) + f(t - 2.0 * h)) / (2.0 * h);
    return (f(t + h) - f(t - h)) / (2.0 * h);
}

Vec3 bezier(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2, const Vec3 &p3, double s) {
    const double r = 1.0 - s;
    return r * r * r * p0 + 3.0 * r * r * s * p1 + 3.0 * r * s * s * p2 + s * s * s * p3;
}

std::vector<double> sortedBreaks(const Interval &range, std::vector<double> breaks, double minimumGap) {
    std::sort(breaks.begin(), breaks.end());
    std::vector<double> cuts{range.lo};
    for (double b : breaks)
        if (b > cuts.back() + minimumGap && b < range.hi - minimumGap) cuts.push_back(b);
    cuts.push_back(range.hi);
    return cuts;
}

// Superficie a distanza di una B-spline: bicubica di Hermite a tratti sulla
// griglia dei nodi (infittita finche' lo scarto da O, nello stesso (u, v), e'
// sotto la tolleranza). In ogni nodo della griglia O, O_u, O_v e O_uv dalle
// differenze finite dalla parte della cella: i poli sulle linee della griglia
// sono comuni alle celle vicine (le derivate lungo la linea sono le stesse),
// quelli interni sono della cella. Nodi interni tripli: C0 garantita, C1 dove
// O lo e'.
SurfacePtr offsetBSpline(const Surface &surface, double d, const Interval &uw, const Interval &vw, double tolerance) {
    const auto O = [&](double u, double v) { return surface.point(u, v) + d * surface.normal(u, v); };
    std::vector<double> us = sortedBreaks(uw, surface.uBreakpoints(uw), 1e-9 * uw.length());
    std::vector<double> vs = sortedBreaks(vw, surface.vBreakpoints(vw), 1e-9 * vw.length());
    const auto halve = [](std::vector<double> &lines) {
        std::vector<double> result;
        for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
            result.push_back(lines[i]);
            result.push_back(0.5 * (lines[i] + lines[i + 1]));
        }
        result.push_back(lines.back());
        lines = std::move(result);
    };
    halve(us);
    halve(vs);
    using Cell = std::array<std::array<Vec3, 4>, 4>;
    const auto cellPoles = [&](std::size_t i, std::size_t j) {
        Cell poles;
        const double u0 = us[i], u1 = us[i + 1], v0 = vs[j], v1 = vs[j + 1];
        // Passi delle differenze: piccoli per le derivate prime, piu' grandi
        // per la torsione (la doppia differenza divide per il quadrato del
        // passo: con 1e-4 l'arrotondamento fermava l'errore a 3e-8).
        const double hu = u1 - u0, hv = v1 - v0, du = 1e-3 * hu, dv = 1e-3 * hv, tu = 1e-2 * hu, tv = 1e-2 * hv;
        for (int cu = 0; cu < 2; ++cu)
            for (int cv = 0; cv < 2; ++cv) {
                const double u = cu ? u1 : u0, v = cv ? v1 : v0;
                const int su = cu ? -1 : 1, sv = cv ? -1 : 1;
                const auto alongU = [&](double vv, double step) { return derivative([&](double uu) { return O(uu, vv); }, u, step, su); };
                const Vec3 P = O(u, v);
                const Vec3 Pu = alongU(v, du);
                const Vec3 Pv = derivative([&](double vv) { return O(u, vv); }, v, dv, sv);
                const Vec3 Puv = derivative([&](double vv) { return alongU(vv, tu); }, v, tv, sv);
                const int a = cu ? 3 : 0, b = cv ? 3 : 0, a2 = cu ? 2 : 1, b2 = cv ? 2 : 1;
                poles[a][b] = P;
                poles[a2][b] = P + (su * hu / 3.0) * Pu;
                poles[a][b2] = P + (sv * hv / 3.0) * Pv;
                poles[a2][b2] = P + (su * hu / 3.0) * Pu + (sv * hv / 3.0) * Pv + (su * sv * hu * hv / 9.0) * Puv;
            }
        return poles;
    };
    const auto evaluateCell = [](const Cell &poles, double s, double t) {
        Vec3 rows[4];
        for (int a = 0; a < 4; ++a) rows[a] = bezier(poles[a][0], poles[a][1], poles[a][2], poles[a][3], t);
        return bezier(rows[0], rows[1], rows[2], rows[3], s);
    };
    for (int round = 0;; ++round) {
        std::set<std::size_t> splitU, splitV;
        for (std::size_t i = 0; i + 1 < us.size(); ++i)
            for (std::size_t j = 0; j + 1 < vs.size(); ++j) {
                const Cell poles = cellPoles(i, j);
                double error = 0.0;
                for (double s : {0.25, 0.5, 0.75})
                    for (double t : {0.25, 0.5, 0.75})
                        error = std::max(error, distance(evaluateCell(poles, s, t), O(us[i] + s * (us[i + 1] - us[i]), vs[j] + t * (vs[j + 1] - vs[j]))));
                if (error > tolerance) {
                    splitU.insert(i);
                    splitV.insert(j);
                }
            }
        if (splitU.empty()) break;
        if (round >= 12 || us.size() > 256 || vs.size() > 256)
            throw std::domain_error("offset: superficie B-spline non approssimabile (spigoli vivi dentro la faccia, "
                                    "o distanza oltre il raggio di curvatura)");
        std::vector<double> nextU, nextV;
        for (std::size_t i = 0; i + 1 < us.size(); ++i) {
            nextU.push_back(us[i]);
            if (splitU.count(i)) nextU.push_back(0.5 * (us[i] + us[i + 1]));
        }
        nextU.push_back(us.back());
        for (std::size_t j = 0; j + 1 < vs.size(); ++j) {
            nextV.push_back(vs[j]);
            if (splitV.count(j)) nextV.push_back(0.5 * (vs[j] + vs[j + 1]));
        }
        nextV.push_back(vs.back());
        us = std::move(nextU);
        vs = std::move(nextV);
    }
    const std::size_t nu = us.size() - 1, nv = vs.size() - 1;
    const int uCount = int(3 * nu + 1), vCount = int(3 * nv + 1);
    std::vector<Vec3> poles(std::size_t(uCount) * std::size_t(vCount));
    for (std::size_t i = 0; i < nu; ++i)
        for (std::size_t j = 0; j < nv; ++j) {
            const Cell cell = cellPoles(i, j);
            for (int a = 0; a < 4; ++a)
                for (int b = 0; b < 4; ++b) poles[(3 * i + std::size_t(a)) * std::size_t(vCount) + 3 * j + std::size_t(b)] = cell[a][b];
        }
    const auto knots = [](const std::vector<double> &lines) {
        std::vector<double> result(4, lines.front());
        for (std::size_t k = 1; k + 1 < lines.size(); ++k) result.insert(result.end(), 3, lines[k]);
        result.insert(result.end(), 4, lines.back());
        return result;
    };
    return std::make_shared<BSplineSurface>(3, 3, knots(us), knots(vs), uCount, vCount, std::move(poles));
}

// Retta, cerchio o curva traslata, se la curva a distanza lo e' (verificato
// sui campioni entro l'arrotondamento); altrimenti nullptr.
CurvePtr<3> exactOffsetCurve(const CurvePtr<3> &curve, const Interval &range, const std::function<Vec3(double)> &f, double scale) {
    const double eps = 1e-11 * std::max(1.0, scale);
    std::vector<double> ts;
    for (int k = 0; k <= 8; ++k) ts.push_back(range.lo + range.length() * k / 8.0);
    std::vector<Vec3> values;
    for (double t : ts) values.push_back(f(t));
    // Traslazione (curve di una faccia piana).
    const Vec3 shift = values.front() - curve->point(ts.front());
    bool translated = true;
    for (std::size_t k = 0; k < ts.size() && translated; ++k) translated = distance(values[k], curve->point(ts[k]) + shift) <= eps;
    if (translated) {
        double parameterScale = 1.0;
        CurvePtr<3> moved = transformCurve(curve, Transform3::translation(shift), &parameterScale);
        if (std::fabs(parameterScale - 1.0) < 1e-15) return moved;
    }
    if (curve->type() == CurveType::Line) {
        const auto &line = static_cast<const Line<3> &>(*curve);
        const Vec3 origin = values.front() - ts.front() * line.direction();
        bool straight = true;
        for (std::size_t k = 0; k < ts.size() && straight; ++k) straight = distance(values[k], origin + ts[k] * line.direction()) <= eps;
        if (straight) return std::make_shared<Line<3>>(origin, line.direction());
    }
    if (curve->type() == CurveType::Circle) {
        const auto &circle = static_cast<const Circle<3> &>(*curve);
        // Cerchio per tre campioni, con l'asse dalla parte di quello di partenza.
        const Vec3 &a = values[0], &b = values[4], &c = values[8];
        const Vec3 ab = b - a, ac = c - a;
        Vec3 n = cross(ab, ac);
        const double nn = squaredNorm(n);
        if (nn > 0.0) {
            const Vec3 center = a + (squaredNorm(ac) * cross(n, ab) + squaredNorm(ab) * cross(ac, n)) / (2.0 * nn);
            n = n / std::sqrt(nn);
            if (dot(n, cross(circle.xAxis(), circle.yAxis())) < 0.0) n = -n;
            const double r = distance(a, center);
            if (r > eps) {
                const Vec3 p = (a - center) / r, q = cross(n, p);
                const double t0 = ts.front();
                const Vec3 x = std::cos(t0) * p - std::sin(t0) * q;
                const Vec3 y = cross(n, x);
                bool round = true;
                for (std::size_t k = 0; k < ts.size() && round; ++k)
                    round = distance(values[k], center + r * (std::cos(ts[k]) * x + std::sin(ts[k]) * y)) <= eps;
                if (round) return std::make_shared<Circle<3>>(center, x, y, r);
            }
        }
    }
    return nullptr;
}

// Una curva chiusa con un angolo nel punto di chiusura (spline chiusa non
// periodica) a distanza non si richiude: la superficie a distanza avrebbe un
// taglio nella faccia, che non ha cucitura.
void requireClosed(const Curve<3> &curve, const Interval &domain, const std::function<Vec3(double)> &f, double tolerance) {
    if (distance(curve.point(domain.lo), curve.point(domain.hi)) > kLinearResolution) return;
    if (distance(f(domain.lo), f(domain.hi)) > 10.0 * tolerance)
        throw std::domain_error("offset: la curva chiusa della superficie ha un angolo nel punto di chiusura "
                                "(spigolo vivo dentro la faccia)");
}

CurvePtr<3> offsetCurve(const CurvePtr<3> &curve, const Interval &range, const std::function<Vec3(double)> &f,
                        std::vector<double> breaks, double scale, double tolerance) {
    if (CurvePtr<3> exact = exactOffsetCurve(curve, range, f, scale)) return exact;
    const std::vector<double> own = curve->breakpoints(range);
    breaks.insert(breaks.end(), own.begin(), own.end());
    return fitCurve(f, range, breaks, tolerance);
}

// Parametro appena fuori dal dominio di una direzione periodica (le SP-curve
// arrivano a -1e-17): riportato sul bordo. Altrimenti la superficie lo porta
// dall'altra parte del periodo, e dove la superficie chiusa ha un angolo (la
// chiusura di una spline chiusa non periodica) la normale sarebbe quella del
// lato sbagliato.
double clampPeriodic(double t, const Interval &domain, bool periodic) {
    if (!periodic || !domain.isFinite()) return t;
    const double slack = 1e-9 * domain.length();
    if (t < domain.lo && t > domain.lo - slack) return domain.lo;
    if (t > domain.hi && t < domain.hi + slack) return domain.hi;
    return t;
}

Vec3 faceNormal(const Body &body, FaceId f, const Vec2 &uv) {
    const Face &face = body.face(f);
    const Surface &surface = *face.surface;
    const double u = clampPeriodic(uv[0], surface.uDomain(), surface.isUPeriodic());
    const double v = clampPeriodic(uv[1], surface.vDomain(), surface.isVPeriodic());
    Vec3 n;
    try {
        n = surface.normal(u, v);
    } catch (const std::domain_error &) {
        n = normalAt(surface, u, v);
    }
    return face.sense ? n : -n;
}

}  // namespace

std::shared_ptr<BSplineCurve<3>> fitCurve(const std::function<Vec3(double)> &f, const Interval &range, const std::vector<double> &breaks,
                                          double tolerance, double *deviation) {
    if (!range.isFinite() || !(range.lo < range.hi)) throw std::domain_error("fitCurve: intervallo non valido");
    const double h = 1e-6 * range.length();
    const std::vector<double> cuts = sortedBreaks(range, breaks, 100.0 * h);
    struct Node {
        double t;
        Vec3 p, d;
    };
    std::vector<Vec3> poles{f(range.lo)};
    std::vector<double> knots(4, range.lo);
    double worst = 0.0;
    const std::function<void(const Node &, const Node &, int)> piece = [&](const Node &a, const Node &b, int depth) {
        const double span = b.t - a.t;
        const Vec3 p1 = a.p + (span / 3.0) * a.d, p2 = b.p - (span / 3.0) * b.d;
        double error = 0.0;
        for (double s : {0.25, 0.5, 0.75}) error = std::max(error, distance(bezier(a.p, p1, p2, b.p, s), f(a.t + s * span)));
        if (error <= tolerance || depth >= 40 || span < 64.0 * h) {
            poles.push_back(p1);
            poles.push_back(p2);
            poles.push_back(b.p);
            knots.insert(knots.end(), 3, b.t);
            worst = std::max(worst, error);
            return;
        }
        const double mid = 0.5 * (a.t + b.t);
        const Node m{mid, f(mid), derivative(f, mid, h, 0)};
        piece(a, m, depth + 1);
        piece(m, b, depth + 1);
    };
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
        const double a = cuts[k], b = cuts[k + 1];
        piece(Node{a, f(a), derivative(f, a, h, 1)}, Node{b, f(b), derivative(f, b, h, -1)}, 0);
    }
    knots.push_back(range.hi);
    if (worst > 100.0 * tolerance) throw std::domain_error("offset: curva a distanza non approssimabile (cuspidi?)");
    if (deviation) *deviation = worst;
    return std::make_shared<BSplineCurve<3>>(3, std::move(knots), std::move(poles));
}

SurfacePtr offsetSurface(const Surface &surface, double d, const Interval &uw, const Interval &vw, double tolerance) {
    if (!(std::fabs(d) > 0.0)) throw std::domain_error("offset: distanza nulla");
    SurfacePtr result;
    switch (surface.type()) {
    case SurfaceType::Plane: {
        const Frame3 &f = static_cast<const Plane &>(surface).frame();
        result = std::make_shared<Plane>(Frame3::fromAxes(f.origin() + d * f.zDir(), f.xDir(), f.yDir(), f.zDir()));
        break;
    }
    case SurfaceType::Cylinder: {
        const auto &c = static_cast<const CylindricalSurface &>(surface);
        if (!(c.radius() + d > tolerance)) throw std::domain_error("offset: il raggio del cilindro diventa nullo o negativo");
        result = std::make_shared<CylindricalSurface>(c.frame(), c.radius() + d);
        break;
    }
    case SurfaceType::Cone: {
        const auto &c = static_cast<const ConicalSurface &>(surface);
        const double a = c.semiAngle();
        const Frame3 &f = c.frame();
        result = std::make_shared<ConicalSurface>(Frame3::fromAxes(f.origin() - d * std::sin(a) * f.zDir(), f.xDir(), f.yDir(), f.zDir()), a,
                                                  c.referenceRadius() + d * std::cos(a));
        break;
    }
    case SurfaceType::Sphere: {
        const auto &s = static_cast<const SphericalSurface &>(surface);
        if (!(s.radius() + d > tolerance)) throw std::domain_error("offset: il raggio della sfera diventa nullo o negativo");
        result = std::make_shared<SphericalSurface>(s.frame(), s.radius() + d);
        break;
    }
    case SurfaceType::Torus: {
        const auto &t = static_cast<const ToroidalSurface &>(surface);
        if (!(t.minorRadius() + d > tolerance)) throw std::domain_error("offset: il raggio minore del toro diventa nullo o negativo");
        result = std::make_shared<ToroidalSurface>(t.frame(), t.majorRadius(), t.minorRadius() + d);
        break;
    }
    case SurfaceType::Extrusion: {
        // La normale non dipende da v: S + d N = (C + d N(u)) + v D.
        const auto &e = static_cast<const ExtrusionSurface &>(surface);
        const Interval domain = e.curve()->domain().isFinite() ? e.curve()->domain() : uw;
        const double v0 = vw.isFinite() ? 0.5 * (vw.lo + vw.hi) : 0.0;
        const auto f = [&](double u) { return surface.point(u, v0) + d * surface.normal(u, v0) - v0 * e.direction(); };
        requireClosed(*e.curve(), domain, f, tolerance);
        result = std::make_shared<ExtrusionSurface>(fitCurve(f, domain, e.curve()->breakpoints(domain), tolerance), e.direction());
        break;
    }
    case SurfaceType::Revolution: {
        // La normale ruota con u: il meridiano a distanza (in u = 0) ruotato.
        const auto &r = static_cast<const RevolutionSurface &>(surface);
        const Interval domain = r.meridian()->domain().isFinite() ? r.meridian()->domain() : vw;
        const auto f = [&](double v) { return surface.point(0.0, v) + d * surface.normal(0.0, v); };
        requireClosed(*r.meridian(), domain, f, tolerance);
        result = std::make_shared<RevolutionSurface>(fitCurve(f, domain, r.meridian()->breakpoints(domain), tolerance), r.axisPoint(),
                                                     r.axisDirection());
        break;
    }
    case SurfaceType::BSpline:
        if (!uw.isFinite() || !vw.isFinite()) throw std::domain_error("offset: finestra della B-spline non limitata");
        result = offsetBSpline(surface, d, uw, vw, tolerance);
        break;
    }
    if (!result) throw std::domain_error("offset: superficie non gestita");
    // Stessa parametrizzazione: S + d N e la superficie nuova coincidono nello stesso (u, v).
    if (uw.isFinite() && vw.isFinite()) {
        for (int i = 0; i <= 4; ++i)
            for (int j = 0; j <= 4; ++j) {
                const double u = uw.lo + uw.length() * (0.05 + 0.9 * i / 4.0), v = vw.lo + vw.length() * (0.05 + 0.9 * j / 4.0);
                Vec3 expected;
                try {
                    expected = surface.point(u, v) + d * surface.normal(u, v);
                } catch (const std::domain_error &) {
                    continue;  // punto singolare (polo)
                }
                const Vec3 got = result->point(u, v);
                if (distance(got, expected) > std::max(20.0 * tolerance, 1e-9 * (1.0 + norm(expected))))
                    throw std::domain_error("offset: la superficie a distanza non segue la superficie di partenza");
            }
    }
    return result;
}

OffsetResult offsetFaces(const Body &input, const std::vector<FaceId> &faces, double distanceValue, double tolerance) {
    if (faces.empty()) throw std::domain_error("offset: nessuna faccia scelta");
    if (!(std::fabs(distanceValue) > 0.0)) throw std::domain_error("offset: distanza nulla");
    Body body = input;
    if (computePCurves(body) > 0) throw std::domain_error("offset: SP-curve non calcolabili");
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    const double scale = std::max(1.0, box.diagonal());
    std::set<int> selected;
    for (FaceId f : faces) selected.insert(f.index);

    // Superficie a distanza di ogni faccia, sulla finestra (u, v) dei suoi loop.
    std::map<int, SurfacePtr> offsets;
    for (int index : selected) {
        const FaceId f{index};
        const Face &face = body.face(f);
        const Surface &surface = *face.surface;
        Interval u{1e300, -1e300}, v{1e300, -1e300};
        for (LoopId l : face.loops)
            for (FinId finId : body.loopFins(l)) {
                const Fin &fin = body.fin(finId);
                const Edge &edge = body.edge(fin.edge);
                for (int k = 0; k <= 16; ++k) {
                    const Vec2 uv = fin.pcurve->point(edge.range.lo + edge.range.length() * k / 16.0);
                    u.lo = std::min(u.lo, uv[0]), u.hi = std::max(u.hi, uv[0]);
                    v.lo = std::min(v.lo, uv[1]), v.hi = std::max(v.hi, uv[1]);
                }
            }
        if (!(u.lo <= u.hi)) u = surface.uDomain(), v = surface.vDomain();  // faccia senza bordo (sfera, toro interi)
        const auto widen = [](Interval range, const Interval &domain, bool periodic) {
            const double margin = 0.005 * std::max(range.length(), 1e-9);
            range.lo -= margin, range.hi += margin;
            if (!periodic && domain.isFinite()) range.lo = std::max(range.lo, domain.lo), range.hi = std::min(range.hi, domain.hi);
            return range;
        };
        u = widen(u, surface.uDomain(), surface.isUPeriodic());
        v = widen(v, surface.vDomain(), surface.isVPeriodic());
        offsets[index] = offsetSurface(surface, face.sense ? distanceValue : -distanceValue, u, v, tolerance);
    }

    // Edge delle facce scelte: tangenti (normali parallele) o spigoli vivi.
    struct EdgeInfo {
        std::vector<FinId> fins;
        bool tangent = false;
    };
    std::map<int, EdgeInfo> edges;
    for (int index : selected)
        for (LoopId l : body.face(FaceId{index}).loops)
            for (FinId finId : body.loopFins(l)) edges[body.fin(finId).edge.index].fins.push_back(finId);
    constexpr double kTangentAngle = 1e-3;
    OffsetResult result;
    for (auto &[edgeIndex, info] : edges) {
        if (info.fins.size() != 2) continue;
        const Edge &edge = body.edge(EdgeId{edgeIndex});
        bool tangent = true;
        for (int k = 0; k < 9 && tangent; ++k) {
            const double t = edge.range.lo + edge.range.length() * (k + 0.5) / 9.0;
            const Vec3 a = faceNormal(body, body.finFace(info.fins[0]), body.fin(info.fins[0]).pcurve->point(t));
            const Vec3 b = faceNormal(body, body.finFace(info.fins[1]), body.fin(info.fins[1]).pcurve->point(t));
            tangent = dot(a, b) > std::cos(kTangentAngle);
        }
        info.tangent = tangent;
        if (!tangent) ++result.sharpEdges;
    }

    // Vertici: un punto per vertice e faccia, uniti attraverso gli edge tangenti.
    std::map<std::pair<int, int>, int> slot;  // (vertice, faccia) -> indice
    std::vector<int> parent;
    std::vector<Vec3> sum;
    std::vector<int> count;
    const auto find = [&](int a) {
        while (parent[std::size_t(a)] != a) a = parent[std::size_t(a)] = parent[std::size_t(parent[std::size_t(a)])];
        return a;
    };
    for (int index : selected)
        for (LoopId l : body.face(FaceId{index}).loops)
            for (FinId finId : body.loopFins(l)) {
                const Fin &fin = body.fin(finId);
                const Edge &edge = body.edge(fin.edge);
                const Vec2 uv = fin.pcurve->point(fin.sense ? edge.range.lo : edge.range.hi);
                const Vec3 p = body.vertex(fin.vertex).point + distanceValue * faceNormal(body, FaceId{index}, uv);
                const auto key = std::make_pair(fin.vertex.index, index);
                auto found = slot.find(key);
                if (found == slot.end()) {
                    found = slot.emplace(key, int(parent.size())).first;
                    parent.push_back(int(parent.size()));
                    sum.push_back(Vec3());
                    count.push_back(0);
                }
                sum[std::size_t(found->second)] += p;
                ++count[std::size_t(found->second)];
            }
    for (const auto &[edgeIndex, info] : edges) {
        if (!info.tangent) continue;
        const EdgeId e{edgeIndex};
        const int f0 = body.finFace(info.fins[0]).index, f1 = body.finFace(info.fins[1]).index;
        for (VertexId v : {body.edgeStart(e), body.edgeEnd(e)}) {
            const int a = find(slot.at({v.index, f0})), b = find(slot.at({v.index, f1}));
            if (a != b) {
                parent[std::size_t(b)] = a;
            }
        }
    }
    detail::RawModel model;
    std::map<int, int> pointOf;  // radice -> punto del modello
    std::vector<Vec3> groupSum(parent.size());
    std::vector<int> groupCount(parent.size(), 0);
    for (std::size_t k = 0; k < parent.size(); ++k) {
        const int root = find(int(k));
        groupSum[std::size_t(root)] += sum[k];
        groupCount[std::size_t(root)] += count[k];
    }
    const auto pointIndex = [&](int vertex, int face) {
        const int root = find(slot.at({vertex, face}));
        auto found = pointOf.find(root);
        if (found != pointOf.end()) return found->second;
        model.points.push_back(groupSum[std::size_t(root)] / double(groupCount[std::size_t(root)]));
        return pointOf[root] = int(model.points.size()) - 1;
    };

    // Edge a distanza: uno solo lungo gli edge tangenti (la normale media
    // delle due facce), uno per faccia altrove.
    std::map<std::pair<int, int>, int> rawEdge;  // (edge, faccia o -1) -> indice
    const auto rawEdgeOf = [&](FinId finId) {
        const Fin &fin = body.fin(finId);
        const EdgeId e = fin.edge;
        const EdgeInfo &info = edges.at(e.index);
        const int face = body.finFace(finId).index;
        const auto key = std::make_pair(e.index, info.tangent ? -1 : face);
        auto found = rawEdge.find(key);
        if (found != rawEdge.end()) return found->second;
        const Edge &edge = body.edge(e);
        std::vector<FinId> used = info.tangent ? info.fins : std::vector<FinId>{finId};
        const auto f = [&](double t) {
            Vec3 n;
            for (FinId u : used) n += faceNormal(body, body.finFace(u), body.fin(u).pcurve->point(t));
            return edge.curve->point(t) + distanceValue * (n / norm(n));
        };
        std::vector<double> breaks;
        for (FinId u : used) {
            const std::vector<double> b = body.fin(u).pcurve->breakpoints(edge.range);
            breaks.insert(breaks.end(), b.begin(), b.end());
        }
        detail::RawEdge raw;
        raw.start = pointIndex(body.edgeStart(e).index, face);
        raw.end = pointIndex(body.edgeEnd(e).index, face);
        raw.curve = offsetCurve(edge.curve, edge.range, f, breaks, scale, tolerance);
        raw.hasRange = true;
        raw.range = edge.range;
        model.edges.push_back(raw);
        return rawEdge[key] = int(model.edges.size()) - 1;
    };
    for (int index : selected) {
        const Face &face = body.face(FaceId{index});
        detail::RawFace raw;
        raw.surface = offsets.at(index);
        raw.sense = face.sense;
        for (LoopId l : face.loops) {
            std::vector<detail::RawFin> loop;
            for (FinId finId : body.loopFins(l)) loop.push_back({rawEdgeOf(finId), body.fin(finId).sense});
            if (!loop.empty()) raw.loops.push_back(std::move(loop));
        }
        model.faces.push_back(std::move(raw));
    }
    result.body = detail::assembleBody(model, false, &result.notes);
    for (ShellId s : result.body.shells()) {
        (void)s;
        ++result.shells;
    }
    return result;
}

}
