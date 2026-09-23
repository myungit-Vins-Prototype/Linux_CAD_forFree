#include "fk_marching.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "fk_bernstein.h"
#include "fk_bspline.h"
#include "fk_curve_algo.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

constexpr int kU = Surface::derivativeIndex(1, 0, 1), kV = Surface::derivativeIndex(0, 1, 1);
// Seno dell'angolo tra le normali sotto il quale le superfici sono tangenti.
constexpr double kTangentSine = 1e-6;

using Params = std::array<double, 4>;  // (u, v) su A, (s, t) su B

struct Node {
    Params x{};
    Vec3 p, t;    // punto e tangente unitaria (verso di avanzamento)
    Vec2 da, db;  // derivate delle SP-curve rispetto all'ascissa
    // Tangente e derivate in arrivo: diverse da quelle in partenza solo sugli
    // spigoli vivi delle superfici (nodi C0, per esempio la chiusura di una
    // spline chiusa con un angolo), dove la curva d'intersezione fa un angolo.
    Vec3 tIn;
    Vec2 daIn, dbIn;
    double sine = 0.0;

    void setIncoming() {
        tIn = t;
        daIn = da;
        dbIn = db;
    }
    Node reversed() const {
        Node n = *this;
        n.t = -tIn;
        n.da = -daIn;
        n.db = -dbIn;
        n.tIn = -t;
        n.daIn = -da;
        n.dbIn = -db;
        return n;
    }
};

// Vincolo che completa le tre equazioni S_A = S_B: un piano per il punto
// (avanzamento) oppure un parametro fissato (nodi e bordi dei domini).
struct Constraint {
    bool plane = true;
    Vec3 normal, origin;
    int index = 0;
    double value = 0.0;
};

bool solve4(double m[4][5], double x[4]) {
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(m[r][c]) > std::fabs(m[pivot][c])) pivot = r;
        if (!(std::fabs(m[pivot][c]) > 1e-300)) return false;
        if (pivot != c)
            for (int k = 0; k < 5; ++k) std::swap(m[c][k], m[pivot][k]);
        for (int r = c + 1; r < 4; ++r) {
            const double f = m[r][c] / m[c][c];
            for (int k = c; k < 5; ++k) m[r][k] -= f * m[c][k];
        }
    }
    for (int r = 3; r >= 0; --r) {
        double sum = m[r][4];
        for (int k = r + 1; k < 4; ++k) sum -= m[r][k] * x[k];
        x[r] = sum / m[r][r];
        if (!std::isfinite(x[r])) return false;
    }
    return true;
}

// (du, dv) con Su du + Sv dv = t (t tangente alla superficie).
Vec2 tangentParameters(const Vec3 &su, const Vec3 &sv, const Vec3 &t) {
    const double a = dot(su, su), b = dot(su, sv), c = dot(sv, sv), p = dot(su, t), q = dot(sv, t);
    const double det = a * c - b * b;
    return Vec2((c * p - b * q) / det, (a * q - b * p) / det);
}

// Cubica di Hermite sul tratto di lunghezza h, in s in [0, 1].
template <int N>
Vec<N> hermite(const Vec<N> &p0, const Vec<N> &m0, const Vec<N> &p1, const Vec<N> &m1, double h, double s,
               Vec<N> *derivative = nullptr) {
    const double s2 = s * s, s3 = s2 * s;
    if (derivative)
        *derivative = ((6 * s2 - 6 * s) * p0 + (3 * s2 - 4 * s + 1) * h * m0 + (6 * s - 6 * s2) * p1 + (3 * s2 - 2 * s) * h * m1) / h;
    return (2 * s3 - 3 * s2 + 1) * p0 + (s3 - 2 * s2 + s) * h * m0 + (3 * s2 - 2 * s3) * p1 + (s3 - s2) * h * m1;
}

bool inside(const Box &box, const Vec3 &p) {
    for (int i = 0; i < 3; ++i)
        if (p[i] < box.lo[i] || p[i] > box.hi[i]) return false;
    return true;
}

enum class Stop { Box, Limit, Closed, Tangent };

class Marcher {
public:
    Marcher(const Surface &a, const Surface &b, const Box &bounds, const SurfaceIntersectionOptions &options)
        : a_(a), b_(b), bounds_(bounds), tolerance_(options.tolerance), fitTolerance_(options.fitTolerance) {
        scale_ = std::max(bounds.diagonal(), 1.0);
        h0_ = 0.01 * scale_;
        hMax_ = 0.05 * scale_;
        hMin_ = 1e-9 * scale_;
    }

    void traceFrom(const Vec3 &seed, SurfaceIntersection &out);

private:
    void evaluate(const Params &x, Vec3 *ea, Vec3 *eb) const {
        a_.evaluate(x[0], x[1], 1, ea);
        b_.evaluate(x[2], x[3], 1, eb);
    }
    bool correct(Params &x, const Constraint &constraint) const;
    // Falso nei punti singolari (normali parallele).
    bool makeNode(const Params &x, const Vec3 &reference, Node &node) const;
    // Nodo su un nodo della superficie, raggiunto muovendo i parametri lungo
    // `direction`: tangenti in arrivo e in partenza dai due lati.
    bool makeBreakNode(const Params &x, const Vec3 &reference, const Params &direction, int index, Node &node) const;
    bool validate(const Node &a, const Node &b, double &curveDeviation, double &pcurveDeviation) const;
    bool firstCrossing(const Params &from, const Params &to, double &fraction, int &index, double &value, bool &limit) const;
    Stop trace(const Node &seed, std::vector<Node> &out, Vec3 &tangentPoint);
    IntersectionCurve assemble(const std::vector<Node> &nodes, bool closed) const;

    const Surface &a_, &b_;
    Box bounds_;
    double tolerance_, fitTolerance_, scale_, h0_, hMax_, hMin_;
    double curveDeviation_ = 0.0, pcurveDeviation_ = 0.0;
};

bool Marcher::correct(Params &x, const Constraint &constraint) const {
    const double threshold = 1e-13 * scale_;
    for (int iteration = 0; iteration < 30; ++iteration) {
        Vec3 ea[4], eb[4];
        evaluate(x, ea, eb);
        const Vec3 gap = ea[0] - eb[0];
        double m[4][5];
        for (int i = 0; i < 3; ++i) {
            m[i][0] = ea[kU][i];
            m[i][1] = ea[kV][i];
            m[i][2] = -eb[kU][i];
            m[i][3] = -eb[kV][i];
            m[i][4] = -gap[i];
        }
        double constraintError;
        if (constraint.plane) {
            const double f = dot(constraint.normal, ea[0] - constraint.origin);
            m[3][0] = dot(constraint.normal, ea[kU]);
            m[3][1] = dot(constraint.normal, ea[kV]);
            m[3][2] = m[3][3] = 0.0;
            m[3][4] = -f;
            constraintError = std::fabs(f);
        } else {
            for (int k = 0; k < 4; ++k) m[3][k] = k == constraint.index ? 1.0 : 0.0;
            m[3][4] = constraint.value - x[constraint.index];
            constraintError = std::fabs(m[3][4]) * scale_;
        }
        if (norm(gap) <= threshold && constraintError <= threshold) return true;
        double dx[4];
        if (!solve4(m, dx)) return false;
        double largest = 0.0;
        for (int k = 0; k < 4; ++k) {
            x[k] += dx[k];
            largest = std::max(largest, std::fabs(dx[k]) / (1.0 + std::fabs(x[k])));
        }
        if (!constraint.plane) x[constraint.index] = constraint.value;
        if (largest <= 1e-15) break;
    }
    Vec3 ea[4], eb[4];
    evaluate(x, ea, eb);
    return distance(ea[0], eb[0]) <= 1e-10 * scale_;
}

bool Marcher::makeNode(const Params &x, const Vec3 &reference, Node &node) const {
    Vec3 ea[4], eb[4];
    evaluate(x, ea, eb);
    const Vec3 na = cross(ea[kU], ea[kV]), nb = cross(eb[kU], eb[kV]);
    const double la = norm(na), lb = norm(nb);
    if (!(la > 0.0) || !(lb > 0.0)) return false;
    const Vec3 c = cross(na / la, nb / lb);
    node.sine = norm(c);
    if (node.sine < kTangentSine) return false;
    node.t = c / node.sine;
    if (dot(node.t, reference) < 0.0) node.t = -node.t;
    node.x = x;
    node.p = 0.5 * (ea[0] + eb[0]);
    node.da = tangentParameters(ea[kU], ea[kV], node.t);
    node.db = tangentParameters(eb[kU], eb[kV], node.t);
    node.setIncoming();
    return true;
}

bool Marcher::makeBreakNode(const Params &x, const Vec3 &reference, const Params &direction, int index, Node &node) const {
    double largest = 0.0;
    for (int i = 0; i < 4; ++i) largest = std::max(largest, std::fabs(direction[i]) / (1.0 + std::fabs(x[i])));
    if (!(largest > 0.0)) return false;
    Params before = x, after = x;
    for (int i = 0; i < 4; ++i) {
        const double delta = 1e-9 * direction[i] / largest;
        before[i] -= delta;
        after[i] += delta;
    }
    Node in, out;
    if (!makeNode(before, reference, in) || !makeNode(after, in.t, out) || !makeNode(x, reference, node)) return false;
    // Su uno spigolo vivo la curva fa un angolo: si riparte oltre il nodo
    // (nel verso di `direction` lungo il suo parametro), anche se l'angolo
    // supera 90 gradi. Dove la superficie e' liscia vale la continuita' della
    // tangente (la curva puo' anche solo toccare la linea del nodo e tornare indietro).
    const double component = index < 2 ? out.da[index] : out.db[index - 2];
    if (dot(in.t, out.t) < 1.0 - 1e-6 && component * direction[index] < 0.0) out = out.reversed();
    node.t = out.t;
    node.da = out.da;
    node.db = out.db;
    node.tIn = in.t;
    node.daIn = in.da;
    node.dbIn = in.db;
    return true;
}

// Scarto della cubica di Hermite tra due nodi (curva 3D e SP-curve) in tre
// punti interni, misurato contro l'intersezione vera.
bool Marcher::validate(const Node &a, const Node &b, double &curveDeviation, double &pcurveDeviation) const {
    const double h = distance(a.p, b.p);
    curveDeviation = pcurveDeviation = 0.0;
    for (double s : {0.25, 0.5, 0.75}) {
        Vec3 derivative;
        const Vec3 point = hermite(a.p, a.t, b.p, b.tIn, h, s, &derivative);
        const Vec2 pa = hermite(Vec2(a.x[0], a.x[1]), a.da, Vec2(b.x[0], b.x[1]), b.daIn, h, s);
        const Vec2 pb = hermite(Vec2(a.x[2], a.x[3]), a.db, Vec2(b.x[2], b.x[3]), b.dbIn, h, s);
        Params x{pa[0], pa[1], pb[0], pb[1]};
        Constraint plane;
        plane.normal = normalized(derivative);
        plane.origin = point;
        if (!correct(x, plane)) return false;
        const Vec3 exact = a_.point(x[0], x[1]);
        curveDeviation = std::max(curveDeviation, distance(point, exact));
        pcurveDeviation = std::max({pcurveDeviation, distance(a_.point(pa[0], pa[1]), point), distance(b_.point(pb[0], pb[1]), point)});
    }
    return true;
}

// Primo nodo delle superfici o bordo di dominio non periodico attraversato
// andando da `from` a `to` (frazione del passo, indice del parametro, valore).
bool Marcher::firstCrossing(const Params &from, const Params &to, double &fraction, int &index, double &value, bool &limit) const {
    fraction = 2.0;
    for (int i = 0; i < 4; ++i) {
        const double p0 = from[i], p1 = to[i];
        if (p0 == p1) continue;
        const Surface &surface = i < 2 ? a_ : b_;
        const bool isU = i % 2 == 0;
        const double lo = std::min(p0, p1), hi = std::max(p0, p1);
        auto consider = [&](double c, bool isLimit) {
            const double f = (c - p0) / (p1 - p0);
            if (f >= 0.0 && f < fraction) {
                fraction = f;
                index = i;
                value = c;
                limit = isLimit;
            }
        };
        const std::vector<double> breaks = isU ? surface.uBreakpoints({lo, hi}) : surface.vBreakpoints({lo, hi});
        for (double c : breaks) {
            const double margin = 1e-12 * (1.0 + std::fabs(c));
            if (c > lo + margin && c < hi - margin) consider(c, false);
        }
        const bool periodic = isU ? surface.isUPeriodic() : surface.isVPeriodic();
        const Interval domain = isU ? surface.uDomain() : surface.vDomain();
        if (!periodic) {
            if (std::isfinite(domain.hi) && p1 > domain.hi && p0 <= domain.hi) consider(domain.hi, true);
            if (std::isfinite(domain.lo) && p1 < domain.lo && p0 >= domain.lo) consider(domain.lo, true);
        }
    }
    return fraction <= 1.0;
}

// Tracciamento dal seme nel verso della sua tangente, fino al bordo del box,
// al bordo di un dominio, a un punto di tangenza o al ritorno sul seme.
// `out` riceve i nodi dopo il seme.
Stop Marcher::trace(const Node &seed, std::vector<Node> &out, Vec3 &tangentPoint) {
    Node current = seed;
    double h = h0_, travelled = 0.0;
    for (int guard = 0; guard < 200000; ++guard) {
        if (h < hMin_) {
            if (current.sine < 1e-3) {
                tangentPoint = current.p;
                return Stop::Tangent;
            }
            throw std::domain_error("intersectSurfaces: tracciamento dell'intersezione non riuscito");
        }
        Params x = current.x;
        for (int i = 0; i < 2; ++i) {
            x[i] += h * current.da[i];
            x[i + 2] += h * current.db[i];
        }
        // Nodi delle superfici e bordi dei domini: il passo si ferma li'.
        // Di norma si guarda il punto corretto (una curva che tocca soltanto la
        // linea di un nodo non la attraversa); se Newton non converge si
        // guarda la previsione, che magari scavalca uno spigolo vivo (Newton
        // attraverso lo spigolo non converge) e si va direttamente sul nodo.
        Node next;
        double fraction;
        int index = 0;
        double value = 0.0;
        bool limit = false;
        bool onBreak = false;
        Constraint plane;
        plane.normal = current.t;
        plane.origin = current.p + h * current.t;
        const Params predicted = x;
        if (correct(x, plane)) {
            if (!makeNode(x, current.t, next)) {
                if (h > 1e-6 * scale_) {
                    h *= 0.5;
                    continue;
                }
                tangentPoint = 0.5 * (a_.point(x[0], x[1]) + b_.point(x[2], x[3]));
                return Stop::Tangent;
            }
            onBreak = firstCrossing(current.x, x, fraction, index, value, limit);
        } else {
            x = predicted;
            onBreak = firstCrossing(current.x, x, fraction, index, value, limit);
            if (!onBreak) {
                h *= 0.5;
                continue;
            }
        }
        if (onBreak) {
            Params y, direction;
            for (int i = 0; i < 4; ++i) {
                y[i] = current.x[i] + fraction * (x[i] - current.x[i]);
                direction[i] = x[i] - current.x[i];
            }
            y[index] = value;
            Constraint fixed;
            fixed.plane = false;
            fixed.index = index;
            fixed.value = value;
            if (!correct(y, fixed) || !makeBreakNode(y, current.t, direction, index, next)) {
                h *= 0.5;
                continue;
            }
            if (distance(next.p, current.p) <= 1e-9 * scale_) {
                if (limit) return Stop::Limit;
                current.x[index] = value;  // gia' sul nodo
                continue;
            }
        } else {
            limit = false;
        }
        // Un salto lontano o una svolta brusca: forse un altro ramo.
        if (distance(next.p, current.p) > 1.5 * h || dot(next.tIn, current.t) < std::cos(0.3)) {
            h *= 0.5;
            continue;
        }
        // Ritorno sul seme: la curva e' chiusa.
        if (!out.empty() && travelled > 0.0) {
            const Vec3 chord = next.p - current.p;
            const double length2 = squaredNorm(chord);
            const double lambda = dot(seed.p - current.p, chord) / length2;
            if (lambda * std::sqrt(length2) > 1e-9 * scale_ && lambda <= 1.0 + 1e-9 && dot(seed.tIn, current.t) > 0.8
                && distance(seed.p, current.p + lambda * chord) <= 0.05 * std::sqrt(length2) + 10.0 * tolerance_) {
                Node closing = seed;
                bool consistent = true;
                for (int i = 0; i < 4; ++i) {
                    const Surface &surface = i < 2 ? a_ : b_;
                    const bool periodic = i % 2 == 0 ? surface.isUPeriodic() : surface.isVPeriodic();
                    const double expected = current.x[i] + lambda * (next.x[i] - current.x[i]);
                    if (periodic) {
                        const double period = i % 2 == 0 ? surface.uPeriod() : surface.vPeriod();
                        closing.x[i] = seed.x[i] + period * std::round((expected - seed.x[i]) / period);
                    }
                    consistent = consistent && std::fabs(closing.x[i] - expected) <= 0.1 * (1.0 + std::fabs(next.x[i] - current.x[i]));
                }
                if (consistent) {
                    double curveDeviation, pcurveDeviation;
                    if (validate(current, closing, curveDeviation, pcurveDeviation) && curveDeviation <= fitTolerance_
                        && pcurveDeviation <= fitTolerance_) {
                        curveDeviation_ = std::max(curveDeviation_, curveDeviation);
                        pcurveDeviation_ = std::max(pcurveDeviation_, pcurveDeviation);
                        out.push_back(closing);
                        return Stop::Closed;
                    }
                    h = std::max(0.5 * lambda * std::sqrt(length2), 0.5 * hMin_);
                    continue;
                }
            }
        }
        double curveDeviation, pcurveDeviation;
        if (!validate(current, next, curveDeviation, pcurveDeviation)) {
            h *= 0.5;
            continue;
        }
        const double deviation = std::max(curveDeviation, pcurveDeviation);
        if (deviation > fitTolerance_) {
            h *= std::clamp(0.8 * std::pow(fitTolerance_ / deviation, 0.25), 0.1, 0.5);
            continue;
        }
        curveDeviation_ = std::max(curveDeviation_, curveDeviation);
        pcurveDeviation_ = std::max(pcurveDeviation_, pcurveDeviation);
        travelled += distance(current.p, next.p);
        out.push_back(next);
        current = next;
        if (limit) return Stop::Limit;
        if (!inside(bounds_, current.p)) return Stop::Box;
        h = std::min(hMax_, h * std::clamp(0.8 * std::pow(fitTolerance_ / std::max(deviation, 1e-3 * fitTolerance_), 0.25), 0.5, 2.0));
    }
    throw std::domain_error("intersectSurfaces: curva d'intersezione troppo lunga");
}

// B-spline cubiche C1 (Hermite a tratti, nodi interni tripli) per la curva e
// le due SP-curve, parametrizzate con la lunghezza delle corde.
IntersectionCurve Marcher::assemble(const std::vector<Node> &nodes, bool closed) const {
    std::vector<double> knots(4, 0.0);
    std::vector<Vec3> poles{nodes.front().p};
    std::vector<Vec2> polesA{Vec2(nodes.front().x[0], nodes.front().x[1])}, polesB{Vec2(nodes.front().x[2], nodes.front().x[3])};
    double t = 0.0;
    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
        const Node &a = nodes[i], &b = nodes[i + 1];
        const double h = distance(a.p, b.p);
        if (!(h > 1e-12 * scale_)) throw std::logic_error("intersectSurfaces: due punti coincidenti sulla curva");
        const Vec2 a0(a.x[0], a.x[1]), a1(b.x[0], b.x[1]), b0(a.x[2], a.x[3]), b1(b.x[2], b.x[3]);
        poles.push_back(a.p + (h / 3.0) * a.t);
        poles.push_back(b.p - (h / 3.0) * b.tIn);
        poles.push_back(b.p);
        polesA.push_back(a0 + (h / 3.0) * a.da);
        polesA.push_back(a1 - (h / 3.0) * b.daIn);
        polesA.push_back(a1);
        polesB.push_back(b0 + (h / 3.0) * a.db);
        polesB.push_back(b1 - (h / 3.0) * b.dbIn);
        polesB.push_back(b1);
        t += h;
        knots.insert(knots.end(), i + 2 == nodes.size() ? 4 : 3, t);
    }
    IntersectionCurve curve;
    curve.curve = std::make_shared<BSplineCurve<3>>(3, knots, std::move(poles));
    curve.pcurves[0] = std::make_shared<BSplineCurve<2>>(3, knots, std::move(polesA));
    curve.pcurves[1] = std::make_shared<BSplineCurve<2>>(3, knots, std::move(polesB));
    curve.range = {0.0, t};
    curve.deviation = std::max(curveDeviation_, pcurveDeviation_);
    curve.closed = closed;
    return curve;
}

void Marcher::traceFrom(const Vec3 &seedPoint, SurfaceIntersection &out) {
    const SurfaceProjection pa = projectPoint(a_, seedPoint), pb = projectPoint(b_, seedPoint);
    if (pa.distance > 100.0 * tolerance_ || pb.distance > 100.0 * tolerance_) return;
    Params x{pa.u, pa.v, pb.u, pb.v};
    Node seed;
    if (!makeNode(x, Vec3(), seed)) {
        out.tangentPoints.push_back(seedPoint);
        return;
    }
    Constraint plane;
    plane.normal = seed.t;
    plane.origin = seedPoint;
    if (!correct(x, plane)) throw std::domain_error("intersectSurfaces: punto iniziale non convergente");
    if (!makeNode(x, seed.t, seed)) {
        out.tangentPoints.push_back(seedPoint);
        return;
    }
    // Seme su nodi delle superfici (magari spigoli vivi, anche due insieme):
    // le tangenti ai due lati sono diverse. Si riparte da un punto vicino
    // della stessa curva, fuori dai nodi, cercato in ciascun quadrante dei
    // parametri interessati; il tracciamento poi attraversa i nodi come al solito.
    std::vector<int> onBreak;
    for (int i = 0; i < 4; ++i) {
        const Surface &surface = i < 2 ? a_ : b_;
        const double margin = 1e-10 * (1.0 + std::fabs(x[i]));
        const Interval around{x[i] - margin, x[i] + margin};
        const std::vector<double> breaks = i % 2 == 0 ? surface.uBreakpoints(around) : surface.vBreakpoints(around);
        for (double c : breaks)
            if (c > around.lo && c < around.hi) {
                x[i] = c;
                onBreak.push_back(i);
                break;
            }
    }
    if (!onBreak.empty()) {
        const double step = 1e-4 * scale_;
        bool moved = false;
        for (int sides = 0; sides < (1 << onBreak.size()) && !moved; ++sides) {
            Params y = x;
            std::vector<double> sign(onBreak.size());
            for (std::size_t k = 0; k < onBreak.size(); ++k) {
                sign[k] = sides & (1 << k) ? -1.0 : 1.0;
                y[onBreak[k]] += sign[k] * 1e-9 * (1.0 + std::fabs(x[onBreak[k]]));
            }
            Node side;
            if (!makeNode(y, Vec3(), side)) continue;
            for (double orientation : {1.0, -1.0}) {
                const Node n = orientation > 0.0 ? side : side.reversed();
                bool agrees = true;
                for (std::size_t k = 0; k < onBreak.size(); ++k) {
                    const int i = onBreak[k];
                    agrees = agrees && (i < 2 ? n.da[i] : n.db[i - 2]) * sign[k] > 0.0;
                }
                if (!agrees) continue;
                Params z = y;
                for (int i = 0; i < 2; ++i) {
                    z[i] += step * n.da[i];
                    z[i + 2] += step * n.db[i];
                }
                Constraint plane;
                plane.normal = n.t;
                plane.origin = seed.p + step * n.t;
                if (!correct(z, plane)) continue;
                bool sameSides = true;
                for (std::size_t k = 0; k < onBreak.size(); ++k) sameSides = sameSides && (z[onBreak[k]] - x[onBreak[k]]) * sign[k] > 0.0;
                if (sameSides && makeNode(z, n.t, seed)) {
                    x = z;
                    moved = true;
                    break;
                }
            }
        }
        if (!moved) throw std::domain_error("intersectSurfaces: punto iniziale su uno spigolo delle superfici non gestito");
    }
    curveDeviation_ = pcurveDeviation_ = 0.0;
    std::vector<Node> forward, backward;
    Vec3 tangentPoint;
    const Stop stop = trace(seed, forward, tangentPoint);
    if (stop == Stop::Tangent) out.tangentPoints.push_back(tangentPoint);
    if (stop != Stop::Closed) {
        if (trace(seed.reversed(), backward, tangentPoint) == Stop::Tangent) out.tangentPoints.push_back(tangentPoint);
    }
    std::vector<Node> nodes;
    for (auto it = backward.rbegin(); it != backward.rend(); ++it) nodes.push_back(it->reversed());
    nodes.push_back(seed);
    nodes.insert(nodes.end(), forward.begin(), forward.end());
    if (nodes.size() < 2) return;
    out.curves.push_back(assemble(nodes, stop == Stop::Closed));
}

// Parametri in cui n.C(u) e' stazionaria.
void criticalParameters(const Curve<3> &curve, const Interval &range, const Vec3 &n, std::vector<double> &out) {
    switch (curve.type()) {
    case CurveType::Line:
        return;
    case CurveType::Circle:
    case CurveType::Ellipse: {
        Vec3 x, y;
        double rx, ry;
        if (curve.type() == CurveType::Circle) {
            const auto &c = static_cast<const Circle<3> &>(curve);
            x = c.xAxis(), y = c.yAxis(), rx = ry = c.radius();
        } else {
            const auto &e = static_cast<const Ellipse<3> &>(curve);
            x = e.xAxis(), y = e.yAxis(), rx = e.xRadius(), ry = e.yRadius();
        }
        // -rx sin t (n.x) + ry cos t (n.y) = 0
        const double base = std::atan2(ry * dot(n, y), rx * dot(n, x));
        for (double t = base + kPi * std::ceil((range.lo - base) / kPi - 1e-12); t <= range.hi + 1e-12; t += kPi)
            out.push_back(range.clamp(t));
        return;
    }
    case CurveType::BSpline: {
        const auto &spline = static_cast<const BSplineCurve<3> &>(curve);
        const auto segments = spline.cachedBezierSegments();
        for (const BSplineCurve<3> &segment : *segments) {
            const Interval dom = segment.domain();
            if (dom.hi < range.lo || dom.lo > range.hi) continue;
            std::vector<double> numerator, weights;
            double size = 0.0;
            for (int j = 0; j < segment.poleCount(); ++j) {
                numerator.push_back(segment.weight(j) * dot(n, segment.poles()[j]));
                weights.push_back(segment.weight(j));
                size = std::max(size, std::fabs(numerator.back()));
            }
            const BernsteinPolynomial a(numerator), w(weights);
            // (a / w)' = (a' w - a w') / w^2
            const BernsteinPolynomial derivative = spline.isRational() ? a.derivative() * w - a * w.derivative() : a.derivative();
            // Gli estremi dei tratti: nei nodi C0 (angoli della curva) n.C puo'
            // avere un estremo senza che la derivata si annulli; se n.C e'
            // costante sul tratto bastano loro.
            out.push_back(range.clamp(dom.lo));
            out.push_back(range.clamp(dom.hi));
            bool flat = true;
            for (double c : derivative.coefficients()) flat = flat && std::fabs(c) <= 1e-13 * std::max(size, 1.0);
            if (flat) continue;
            for (double s : derivative.roots()) {
                const double t = dom.lo + s * dom.length();
                if (range.contains(t)) out.push_back(t);
            }
        }
        return;
    }
    case CurveType::Trimmed:
        criticalParameters(*static_cast<const TrimmedCurve<3> &>(curve).basis(), range, n, out);
        return;
    default:
        throw std::domain_error("intersectSurfaces: curva base non gestita");
    }
}

// Punti in cui le generatrici critiche di `g` (e quelle agli estremi di un
// dominio non periodico) incontrano l'altra superficie.
void criticalSeeds(const GeneralizedCylinder &g, const Surface &other, const Vec3 &n, const Box &bounds, double tolerance,
                   std::vector<Vec3> &seeds) {
    std::vector<double> us;
    criticalParameters(*g.profile, g.domain, n, us);
    if (!g.periodic) {
        us.push_back(g.domain.lo);
        us.push_back(g.domain.hi);
    }
    for (double u : us) {
        const Vec3 origin = g.profile->point(u);
        bool grazing = false;
        for (double t : intersectLineSurface(origin, g.direction, other, grazing, tolerance)) {
            const Vec3 p = origin + t * g.direction;
            if (inside(bounds, p)) seeds.push_back(p);
        }
        if (grazing && other.type() == SurfaceType::Cylinder) {
            // Generatrice tangente al cilindro: il punto di contatto (tangenza
            // tra le superfici) va comunque segnalato.
            const auto &cylinder = static_cast<const CylindricalSurface &>(other);
            const Vec3 axis = cylinder.frame().zDir(), w = origin - cylinder.frame().origin();
            const Vec3 wp = w - dot(w, axis) * axis, dp = g.direction - dot(g.direction, axis) * axis;
            if (squaredNorm(dp) > 1e-18) {
                const Vec3 p = origin - (dot(wp, dp) / squaredNorm(dp)) * g.direction;
                if (inside(bounds, p)) seeds.push_back(p);
            }
        }
    }
}

// Direzioni parallele: rette per i punti comuni delle due sezioni.
SurfaceIntersection parallelCase(const GeneralizedCylinder &ga, const GeneralizedCylinder &gb, const Box &bounds, double tolerance) {
    SurfaceIntersection out;
    const Frame3 frame = normalFrame(ga.direction, ga.profile->point(ga.domain.lo));
    const PlanarImage ia = planarImage(*ga.profile, ga.domain, frame), ib = planarImage(*gb.profile, gb.domain, frame);
    if (!ia.curve || !ib.curve) throw std::domain_error("intersectSurfaces: sezione degenere");
    const CurveCurveIntersection hits = intersectCurves(*ia.curve, ia.range, *ib.curve, ib.range, tolerance);
    if (hits.overlap) {
        out.coincident = true;
        return out;
    }
    for (const CurveCurvePoint &p : hits.points) {
        const Vec2 ta = ia.curve->derivative(p.s), tb = ib.curve->derivative(p.t);
        const bool tangent = std::fabs(cross(normalized(ta), normalized(tb))) <= 1e-9;
        IntersectionCurve line;
        line.curve = std::make_shared<Line<3>>(frame.toGlobal(Vec3(p.point.x(), p.point.y(), 0.0)), ga.direction);
        if (!clipLineToBox(frame.toGlobal(Vec3(p.point.x(), p.point.y(), 0.0)), ga.direction, bounds, line.range)) continue;
        (tangent ? out.tangentCurves : out.curves).push_back(line);
    }
    return out;
}

}

SurfaceIntersection intersectSurfaces(const Surface &a, const Surface &b, const Box &bounds, const std::vector<Vec3> &seeds,
                                      const SurfaceIntersectionOptions &options) {
    GeneralizedCylinder ga, gb;
    if (!generalizedCylinder(a, ga) || !generalizedCylinder(b, gb))
        throw std::domain_error("intersectSurfaces: coppia di superfici non ancora gestita");
    const Vec3 normal = cross(ga.direction, gb.direction);
    if (norm(normal) <= 1e-12) return parallelCase(ga, gb, bounds, options.tolerance);
    const Vec3 n = normalized(normal);

    std::vector<Vec3> all;
    for (const Vec3 &p : seeds)
        if (inside(bounds, p)) all.push_back(p);
    criticalSeeds(ga, b, n, bounds, options.tolerance, all);
    criticalSeeds(gb, a, n, bounds, options.tolerance, all);

    SurfaceIntersection out;
    Marcher marcher(a, b, bounds, options);
    for (const Vec3 &seed : all) {
        bool known = false;
        for (const IntersectionCurve &curve : out.curves)
            known = known || projectPoint(*curve.curve, seed, curve.range).distance <= 10.0 * options.tolerance;
        for (const Vec3 &p : out.tangentPoints) known = known || distance(p, seed) <= 10.0 * options.tolerance;
        if (!known) marcher.traceFrom(seed, out);
    }
    return out;
}

}
