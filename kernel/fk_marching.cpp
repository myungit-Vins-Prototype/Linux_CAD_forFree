#include "fk_marching.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "fk_bernstein.h"
#include "fk_bspline.h"
#include "fk_curve_algo.h"
#include "fk_surface_algo.h"
#include "fk_curve_surface.h"
#include "fk_nurbs.h"
#include <functional>

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
    bool singular = false;  // nodo in un punto di tangenza (vertice)

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

enum class Stop { Box, Limit, Closed, Tangent, Singular };

// Nodi della superficie che contano per il tracciamento (curvatura
// discontinua): i quadranti delle superfici analitiche servono solo alla
// proiezione, la superficie li' e' liscia.
std::vector<double> surfaceBreaks(const Surface &surface, bool isU, const Interval &range) {
    switch (surface.type()) {
    case SurfaceType::Plane:
    case SurfaceType::Cylinder:
    case SurfaceType::Cone:
    case SurfaceType::Sphere:
    case SurfaceType::Torus:
        return {range.lo, range.hi};
    case SurfaceType::Revolution:
        if (isU) return {range.lo, range.hi};  // la rotazione e' liscia; i nodi del meridiano no
        break;
    default:
        break;
    }
    return isU ? surface.uBreakpoints(range) : surface.vBreakpoints(range);
}

class Marcher {
public:
    Marcher(const Surface &a, const Surface &b, const Box &bounds, const SurfaceIntersectionOptions &options)
        : a_(a), b_(b), bounds_(bounds), tolerance_(options.tolerance), fitTolerance_(options.fitTolerance) {
        scale_ = std::max(bounds.diagonal(), 1.0);
        h0_ = 0.01 * scale_;
        hMax_ = 0.05 * scale_;
        hMin_ = 1e-9 * scale_;
    }

    // singularOnly: solo se il seme e' un punto di tangenza (primo passaggio:
    // cosi' i tracciamenti successivi li conoscono gia' e vi si fermano).
    void traceFrom(const Vec3 &seed, SurfaceIntersection &out, bool singularOnly = false);

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
    Stop trace(Node &seed, std::vector<Node> &out, Vec3 &tangentPoint);
    // Curva per il seme (tracciata nei due versi) aggiunta a out.
    void traceCurve(const Node &seed, SurfaceIntersection &out);
    // Punto di tangenza vicino a x (Gauss-Newton su S_A = S_B e normali
    // parallele), facoltativamente su un piano (per le curve di tangenza).
    bool refineSingular(Params &x, const Vec3 *planeOrigin = nullptr, const Vec3 *planeNormal = nullptr) const;
    // Curva lungo la quale le superfici si toccano (tangenti in ogni punto,
    // una sola direzione in cui le curvature coincidono): per esempio una
    // sfera in un cilindro dello stesso raggio. Va in out.tangentCurves.
    bool traceTangentCurve(const Params &x, const Vec3 &direction, SurfaceIntersection &out);
    bool tangentNode(const Params &x, const Vec3 &reference, Node &node) const;
    // Direzioni dei rami in un punto di tangenza: 2 (si incrociano), 0
    // (contatto isolato), -2 (una sola direzione in cui le curvature
    // coincidono, in directions[0]: forse una curva di tangenza), -1 (contatto
    // di ordine superiore).
    int singularBranches(const Params &x, Vec3 directions[2]) const;
    void singularSeed(const Params &x, SurfaceIntersection &out);  // registra il punto
public:
    void traceSingularBranches(SurfaceIntersection &out);  // dopo aver registrato tutti i punti
private:
    struct Singular {
        Params x;
        Vec3 p;
        Vec3 directions[2];
        // Vertice di un cono (superficie `coneSide`) sull'altra superficie: ogni
        // ramo parte lungo una generatrice, con il suo u (`branch`).
        bool apex = false;
        int coneSide = 0;
        Params branch[2];
    };
    // Parametri e derivate delle SP-curve di un nodo nel punto singolare lungo w.
    void singularFrame(const Singular &singular, const Vec3 &w, Node &node) const;
public:
    // Vertici dei coni che stanno sull'altra superficie: punti singolari da
    // cui partono i rami lungo le generatrici nel piano tangente dell'altra.
    void registerApexes(SurfaceIntersection &out);
private:
    // Nodo finale nel punto di tangenza, arrivando da `current`.
    Node singularNode(const Singular &singular, const Node &current) const;
    std::vector<Singular> singular_;
    bool knownPoint(const Vec3 &p, const SurfaceIntersection &out) const;
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
        const std::vector<double> breaks = surfaceBreaks(surface, isU, {lo, hi});
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
Stop Marcher::trace(Node &seed, std::vector<Node> &out, Vec3 &tangentPoint) {
    Node current = seed;
    double h = h0_, travelled = 0.0;
    // Un nodo a una distanza minima dall'ultimo prende il suo posto (se la
    // cubica dal penultimo resta nella tolleranza): tratti cortissimi
    // renderebbero rumorose le derivate delle SP-curve.
    auto replaceLast = [&](const Node &node) {
        if (out.empty()) return false;
        const Node &before = out.size() >= 2 ? out[out.size() - 2] : seed;
        double curveDeviation, pcurveDeviation;
        if (!validate(before, node, curveDeviation, pcurveDeviation) || curveDeviation > fitTolerance_ || pcurveDeviation > fitTolerance_)
            return false;
        curveDeviation_ = std::max(curveDeviation_, curveDeviation);
        pcurveDeviation_ = std::max(pcurveDeviation_, pcurveDeviation);
        out.back() = node;
        return true;
    };
    const double shortest = 1e-5 * scale_;
    for (int guard = 0; guard < 200000; ++guard) {
        if (h < hMin_) {
            if (current.sine < 1e-3) {
                tangentPoint = current.p;
                return Stop::Tangent;
            }
            throw std::domain_error("intersectSurfaces: tracciamento dell'intersezione non riuscito");
        }
        // Un punto di tangenza poco avanti: la curva finisce li' (i rami che
        // vi si incrociano sono curve distinte, il punto e' un vertice).
        bool nearSingular = false;
        for (const Singular &singular : singular_) {
            const Vec3 d = singular.p - current.p;
            const double along = dot(d, current.t);
            if (along <= 1e-6 * scale_ || along > 1.5 * h || norm(d - along * current.t) > 0.2 * along + 10.0 * tolerance_) continue;
            const Node node = singularNode(singular, current);
            double curveDeviation, pcurveDeviation;
            if (validate(current, node, curveDeviation, pcurveDeviation) && curveDeviation <= fitTolerance_ && pcurveDeviation <= fitTolerance_) {
                curveDeviation_ = std::max(curveDeviation_, curveDeviation);
                pcurveDeviation_ = std::max(pcurveDeviation_, pcurveDeviation);
                out.push_back(node);
                return Stop::Singular;
            }
            h = 0.5 * along;
            nearSingular = true;
            break;
        }
        if (nearSingular) continue;
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
                // Gia' sul nodo (a meno dell'arrotondamento): il nodo corrente
                // diventa quello esatto sul nodo della superficie.
                next.tIn = current.tIn;
                next.daIn = current.daIn;
                next.dbIn = current.dbIn;
                next.singular = current.singular;
                if (out.empty()) seed = next;
                else out.back() = next;
                current = next;
                continue;
            }
            if (distance(next.p, current.p) <= shortest && replaceLast(next)) {
                current = next;
                if (limit) return Stop::Limit;
                if (!inside(bounds_, current.p)) return Stop::Box;
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
            if (lambda > 0.0 && lambda <= 1.0 + 1e-9 && dot(seed.tIn, current.t) > 0.8
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
                if (consistent && lambda * std::sqrt(length2) <= shortest && replaceLast(closing)) return Stop::Closed;
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
    IntersectionCurve curve;
    if (nodes.front().singular) curve.splitParameters.push_back(0.0);
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
        if (b.singular) curve.splitParameters.push_back(t);
    }
    curve.curve = std::make_shared<BSplineCurve<3>>(3, knots, std::move(poles));
    curve.pcurves[0] = std::make_shared<BSplineCurve<2>>(3, knots, std::move(polesA));
    curve.pcurves[1] = std::make_shared<BSplineCurve<2>>(3, knots, std::move(polesB));
    curve.range = {0.0, t};
    curve.deviation = std::max(curveDeviation_, pcurveDeviation_);
    curve.closed = closed;
    return curve;
}

void Marcher::traceFrom(const Vec3 &seedPoint, SurfaceIntersection &out, bool singularOnly) {
    const SurfaceProjection pa = projectPoint(a_, seedPoint), pb = projectPoint(b_, seedPoint);
    if (pa.distance > 100.0 * tolerance_ || pb.distance > 100.0 * tolerance_) return;
    Params x{pa.u, pa.v, pb.u, pb.v};
    Node seed;
    // Gia' su una curva di tangenza (o su un punto noto).
    if (knownPoint(seedPoint, out)) return;
    // Vicino a un punto di tangenza: lo si cerca e se ne tracciano i rami.
    const bool regular = makeNode(x, Vec3(), seed);
    if (!regular || seed.sine < 1e-3) {
        Params y = x;
        // Un punto di tangenza lontano dal seme (le superfici quasi tangenti
        // attorno a un contatto di ordine superiore) non ferma un seme regolare.
        if (refineSingular(y) && (!regular || distance(a_.point(y[0], y[1]), seedPoint) <= 1e-3 * scale_)) {
            singularSeed(y, out);
            return;
        }
        if (!makeNode(x, Vec3(), seed)) {
            if (!singularOnly) out.tangentPoints.push_back(seedPoint);
            return;
        }
    }
    if (singularOnly || knownPoint(seedPoint, out)) return;
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
        const std::vector<double> breaks = surfaceBreaks(surface, i % 2 == 0, around);
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
    traceCurve(seed, out);
}

void Marcher::traceCurve(const Node &seed, SurfaceIntersection &out) {
    curveDeviation_ = pcurveDeviation_ = 0.0;
    std::vector<Node> forward, backward;
    Vec3 tangentPoint;
    Node start = seed;  // il tracciamento puo' spostarlo esattamente su un nodo vicino
    const Stop stop = trace(start, forward, tangentPoint);
    if (stop == Stop::Tangent) out.tangentPoints.push_back(tangentPoint);
    if (stop != Stop::Closed) {
        Node reversed = start.reversed();
        if (trace(reversed, backward, tangentPoint) == Stop::Tangent) out.tangentPoints.push_back(tangentPoint);
        start = reversed.reversed();
    }
    std::vector<Node> nodes;
    for (auto it = backward.rbegin(); it != backward.rend(); ++it) nodes.push_back(it->reversed());
    nodes.push_back(start);
    nodes.insert(nodes.end(), forward.begin(), forward.end());
    if (nodes.size() < 2) return;
    out.curves.push_back(assemble(nodes, stop == Stop::Closed));
}

bool Marcher::knownPoint(const Vec3 &p, const SurfaceIntersection &out) const {
    for (const std::vector<IntersectionCurve> *curves : {&out.curves, &out.tangentCurves})
        for (const IntersectionCurve &curve : *curves)
            if (projectPoint(*curve.curve, p, curve.range).distance <= 10.0 * tolerance_) return true;
    for (const std::vector<Vec3> *points : {&out.tangentPoints, &out.isolatedPoints, &out.singularPoints})
        for (const Vec3 &q : *points)
            if (distance(p, q) <= 10.0 * tolerance_) return true;
    return false;
}

bool Marcher::refineSingular(Params &x, const Vec3 *planeOrigin, const Vec3 *planeNormal) const {
    // Residui: S_A - S_B, le componenti di n_B nel piano tangente di A e, se
    // c'e', la distanza dal piano.
    const int count = planeOrigin ? 6 : 5;
    auto residual = [&](const Params &y, double r[6]) {
        Vec3 ea[4], eb[4];
        evaluate(y, ea, eb);
        const Vec3 na = normalized(cross(ea[kU], ea[kV])), nb = normalized(cross(eb[kU], eb[kV]));
        const Vec3 e1 = normalized(ea[kU]), e2 = cross(na, e1);
        const Vec3 gap = ea[0] - eb[0];
        for (int i = 0; i < 3; ++i) r[i] = gap[i];
        // Le componenti angolari pesate con la dimensione del modello.
        r[3] = scale_ * dot(nb, e1);
        r[4] = scale_ * dot(nb, e2);
        if (planeOrigin) r[5] = dot(*planeNormal, ea[0] - *planeOrigin);
    };
    const Params start = x;
    for (int iteration = 0; iteration < 40; ++iteration) {
        double r[6], jac[6][4];
        residual(x, r);
        double size = 0.0;
        for (int i = 0; i < count; ++i) size = std::max(size, std::fabs(r[i]));
        if (size <= 1e-12 * scale_) break;
        for (int k = 0; k < 4; ++k) {
            Params y = x;
            const double step = 1e-7 * (1.0 + std::fabs(x[k]));
            y[k] += step;
            double ry[6];
            residual(y, ry);
            for (int i = 0; i < count; ++i) jac[i][k] = (ry[i] - r[i]) / step;
        }
        // Equazioni normali (sistema coerente: Gauss-Newton converge al punto),
        // con uno smorzamento minimo: lungo una curva di tangenza il sistema
        // senza piano ha una direzione libera (si va al punto piu' vicino).
        double m[4][5];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                m[i][j] = 0.0;
                for (int k = 0; k < count; ++k) m[i][j] += jac[k][i] * jac[k][j];
            }
            m[i][4] = 0.0;
            for (int k = 0; k < count; ++k) m[i][4] -= jac[k][i] * r[k];
        }
        double trace = 0.0;
        for (int i = 0; i < 4; ++i) trace += m[i][i];
        for (int i = 0; i < 4; ++i) m[i][i] += 1e-12 * trace;
        double dx[4];
        if (!solve4(m, dx)) return false;
        double largest = 0.0;
        for (int k = 0; k < 4; ++k) {
            x[k] += dx[k];
            largest = std::max(largest, std::fabs(dx[k]) / (1.0 + std::fabs(x[k])));
        }
        if (largest <= 1e-14) break;
    }
    double r[6];
    residual(x, r);
    Vec3 ea[4], eb[4];
    evaluate(x, ea, eb);
    const Vec3 moved = ea[0] - a_.point(start[0], start[1]);
    return norm(Vec3(r[0], r[1], r[2])) <= 1e-10 * scale_ && std::fabs(r[3]) + std::fabs(r[4]) <= 1e-8 * scale_
        && (!planeOrigin || std::fabs(r[5]) <= 1e-10 * scale_) && norm(moved) <= 1e-3 * scale_ * (planeOrigin ? 100.0 : 1.0);
}

int Marcher::singularBranches(const Params &x, Vec3 directions[2]) const {
    constexpr int kUU = Surface::derivativeIndex(2, 0, 2), kUV = Surface::derivativeIndex(1, 1, 2),
                  kVV = Surface::derivativeIndex(0, 2, 2), kU2 = Surface::derivativeIndex(1, 0, 2),
                  kV2 = Surface::derivativeIndex(0, 1, 2);
    Vec3 ea[9], eb[9];
    a_.evaluate(x[0], x[1], 2, ea);
    b_.evaluate(x[2], x[3], 2, eb);
    const Vec3 n = normalized(cross(ea[kU2], ea[kV2]));
    // Quota di ciascuna superficie sul piano tangente comune: 1/2 II(x).
    auto height = [&](const Vec3 *e, const Vec3 &direction) {
        const Vec2 ab = tangentParameters(e[kU2], e[kV2], direction);
        return 0.5 * (dot(e[kUU], n) * ab[0] * ab[0] + 2.0 * dot(e[kUV], n) * ab[0] * ab[1] + dot(e[kVV], n) * ab[1] * ab[1]);
    };
    const Vec3 e1 = normalized(ea[kU2]), e2 = cross(n, e1);
    auto q = [&](const Vec3 &direction) { return height(ea, direction) - height(eb, direction); };
    const double d11 = q(e1), d22 = q(e2), d12 = 0.5 * (q(e1 + e2) - d11 - d22);
    const double size = std::max({std::fabs(d11), std::fabs(d22), std::fabs(d12)});
    if (!(size > 1e-9 / scale_)) return -1;  // le superfici coincidono al secondo ordine
    const double disc = d12 * d12 - d11 * d22;
    if (disc < -1e-9 * size * size) return 0;
    if (disc <= 1e-9 * size * size) {
        // q semidefinita: la sua direzione nulla (le curvature coincidono).
        if (std::fabs(d11) >= std::fabs(d22)) directions[0] = normalized((-d12 / d11) * e1 + e2);
        else directions[0] = normalized(e1 + (-d12 / d22) * e2);
        return -2;
    }
    const double root = std::sqrt(disc);
    for (int k = 0; k < 2; ++k) {
        const double sign = k == 0 ? 1.0 : -1.0;
        if (std::fabs(d11) >= std::fabs(d22)) directions[k] = normalized(((-d12 + sign * root) / d11) * e1 + e2);
        else directions[k] = normalized(e1 + ((-d12 + sign * root) / d22) * e2);
    }
    return 2;
}

void Marcher::singularSeed(const Params &x, SurfaceIntersection &out) {
    const Vec3 p = a_.point(x[0], x[1]);
    if (knownPoint(p, out)) return;
    for (const std::vector<Vec3> *points : {&out.tangentPoints, &out.isolatedPoints, &out.singularPoints})
        for (const Vec3 &q : *points)
            if (distance(p, q) <= 10.0 * tolerance_) return;
    Vec3 directions[2];
    const int branches = singularBranches(x, directions);
    if (branches == -2 && traceTangentCurve(x, directions[0], out)) return;
    if (branches < 0) {
        out.tangentPoints.push_back(p);
        return;
    }
    if (branches == 0) {
        out.isolatedPoints.push_back(p);
        return;
    }
    out.singularPoints.push_back(p);
    Singular singular;
    singular.x = x;
    singular.p = p;
    singular.directions[0] = directions[0];
    singular.directions[1] = directions[1];
    singular_.push_back(singular);
}

void Marcher::traceSingularBranches(SurfaceIntersection &out) {
    const std::vector<Singular> points = singular_;
    for (const Singular &singular : points) {
        Vec3 ea[4], eb[4];
        evaluate(singular.x, ea, eb);
        // Quattro semirami: ogni curva tracciata da qui ne copre due (avanti e indietro).
        for (const Vec3 &w : singular.directions)
            for (double sign : {1.0, -1.0}) {
                const Vec3 h = sign * w;
                // Gia' coperto: una curva che nel punto (un suo nodo singolare)
                // parte lungo h o arriva lungo -h.
                bool known = false;
                for (const IntersectionCurve &curve : out.curves)
                    for (double t : curve.splitParameters) {
                        if (distance(curve.curve->point(t), singular.p) > 10.0 * tolerance_) continue;
                        Vec3 left[2], right[2];
                        curve.curve->evaluateLeft(t, 1, left);
                        curve.curve->evaluate(t, 1, right);
                        if (t < curve.range.hi && dot(normalized(right[1]), h) > 0.99) known = true;
                        if (t > curve.range.lo && dot(normalized(left[1]), -h) > 0.99) known = true;
                    }
                if (known) continue;
                Node node;
                node.x = singular.x;
                node.p = singular.p;
                singularFrame(singular, h, node);
                node.singular = true;
                traceCurve(node, out);
            }
    }
}

bool Marcher::tangentNode(const Params &x, const Vec3 &reference, Node &node) const {
    Vec3 directions[2];
    if (singularBranches(x, directions) != -2) return false;
    Vec3 ea[4], eb[4];
    evaluate(x, ea, eb);
    node = Node();
    node.x = x;
    node.p = 0.5 * (ea[0] + eb[0]);
    node.t = dot(directions[0], reference) < 0.0 ? -directions[0] : directions[0];
    node.da = tangentParameters(ea[kU], ea[kV], node.t);
    node.db = tangentParameters(eb[kU], eb[kV], node.t);
    node.setIncoming();
    return true;
}

bool Marcher::traceTangentCurve(const Params &x, const Vec3 &direction, SurfaceIntersection &out) {
    Node start;
    if (!tangentNode(x, direction, start)) return false;
    const double tolerance = 100.0 * fitTolerance_;
    const double shortest = 1e-5 * scale_;
    // Cubica di Hermite tra due nodi contro la curva di tangenza vera.
    auto fits = [&](const Node &a, const Node &b) {
        const double h = distance(a.p, b.p);
        for (double s : {0.25, 0.5, 0.75}) {
            Vec3 derivative;
            const Vec3 point = hermite(a.p, a.t, b.p, b.tIn, h, s, &derivative);
            const Vec2 pa = hermite(Vec2(a.x[0], a.x[1]), a.da, Vec2(b.x[0], b.x[1]), b.daIn, h, s);
            const Vec2 pb = hermite(Vec2(a.x[2], a.x[3]), a.db, Vec2(b.x[2], b.x[3]), b.dbIn, h, s);
            Params y{pa[0], pa[1], pb[0], pb[1]};
            const Vec3 normal = normalized(derivative);
            if (!refineSingular(y, &point, &normal)) return false;
            if (distance(a_.point(y[0], y[1]), point) > tolerance || distance(a_.point(pa[0], pa[1]), point) > tolerance
                || distance(b_.point(pb[0], pb[1]), point) > tolerance)
                return false;
        }
        return true;
    };
    std::vector<Node> halves[2];
    bool closed = false;
    for (int side = 0; side < 2 && !closed; ++side) {
        Node current = side == 0 ? start : start.reversed();
        double h = h0_, travelled = 0.0;
        for (int guard = 0; guard < 20000; ++guard) {
            if (h < 1e-6 * scale_) break;  // il contatto finisce (o non si riesce a seguirlo)
            Params y = current.x;
            for (int i = 0; i < 2; ++i) {
                y[i] += h * current.da[i];
                y[i + 2] += h * current.db[i];
            }
            const Vec3 origin = current.p + h * current.t;
            Node next;
            if (!refineSingular(y, &origin, &current.t) || !tangentNode(y, current.t, next)) {
                h *= 0.5;
                continue;
            }
            // Ritorno all'inizio: curva chiusa.
            if (travelled > 2.0 * h && distance(next.p, start.p) <= 1.5 * h && dot(start.t, current.t) > 0.5 * (side == 0 ? 1.0 : -1.0)) {
                Node closing = side == 0 ? start : start.reversed();
                for (int i = 0; i < 4; ++i) {
                    const Surface &surface = i < 2 ? a_ : b_;
                    const bool periodic = i % 2 == 0 ? surface.isUPeriodic() : surface.isVPeriodic();
                    if (!periodic) continue;
                    const double period = i % 2 == 0 ? surface.uPeriod() : surface.vPeriod();
                    closing.x[i] += period * std::round((current.x[i] - closing.x[i]) / period);
                }
                if (distance(closing.p, current.p) > shortest && fits(current, closing)) {
                    halves[side].push_back(closing);
                    closed = side == 0;
                    break;
                }
                if (distance(closing.p, current.p) <= shortest && !halves[side].empty()) {
                    halves[side].back() = closing;
                    closed = side == 0;
                    break;
                }
            }
            if (distance(next.p, current.p) > 1.5 * h || !fits(current, next)) {
                h *= 0.5;
                continue;
            }
            travelled += distance(next.p, current.p);
            halves[side].push_back(next);
            current = next;
            if (!inside(bounds_, current.p)) break;
            h = std::min(hMax_, 1.5 * h);
        }
    }
    std::vector<Node> nodes;
    for (auto it = halves[1].rbegin(); it != halves[1].rend(); ++it) nodes.push_back(it->reversed());
    nodes.push_back(start);
    nodes.insert(nodes.end(), halves[0].begin(), halves[0].end());
    if (nodes.size() < 2) return false;
    double length = 0.0;
    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) length += distance(nodes[i].p, nodes[i + 1].p);
    if (length < 10.0 * shortest) return false;
    out.tangentCurves.push_back(assemble(nodes, closed));
    return true;
}

Node Marcher::singularNode(const Singular &singular, const Node &current) const {
    Node node;
    node.x = singular.x;
    for (int i = 0; i < 4; ++i) {  // periodi come il nodo di arrivo
        const Surface &surface = i < 2 ? a_ : b_;
        const bool periodic = i % 2 == 0 ? surface.isUPeriodic() : surface.isVPeriodic();
        if (!periodic) continue;
        const double period = i % 2 == 0 ? surface.uPeriod() : surface.vPeriod();
        node.x[i] += period * std::round((current.x[i] - node.x[i]) / period);
    }
    node.p = singular.p;
    const Vec3 chord = normalized(singular.p - current.p);
    Vec3 w = std::fabs(dot(singular.directions[0], chord)) >= std::fabs(dot(singular.directions[1], chord)) ? singular.directions[0]
                                                                                                                : singular.directions[1];
    if (dot(w, chord) < 0.0) w = -w;
    singularFrame(singular, w, node);
    if (singular.apex)  // u del cono come il nodo di arrivo (a meno di periodi)
        for (int i = 0; i < 4; ++i) {
            const Surface &surface = i < 2 ? a_ : b_;
            const bool periodic = i % 2 == 0 ? surface.isUPeriodic() : surface.isVPeriodic();
            if (periodic) {
                const double period = i % 2 == 0 ? surface.uPeriod() : surface.vPeriod();
                node.x[i] += period * std::round((current.x[i] - node.x[i]) / period);
            }
        }
    node.singular = true;
    return node;
}

void Marcher::singularFrame(const Singular &singular, const Vec3 &w, Node &node) const {
    node.t = w;
    if (!singular.apex) {
        Vec3 ea[4], eb[4];
        evaluate(node.x, ea, eb);
        node.da = tangentParameters(ea[kU], ea[kV], w);
        node.db = tangentParameters(eb[kU], eb[kV], w);
        node.setIncoming();
        return;
    }
    // La generatrice piu' vicina a w: sul cono u fisso e v = distanza lungo
    // la generatrice (dv/ds = +-1); l'altra superficie e' regolare nel vertice.
    const int k = std::fabs(dot(singular.directions[0], w)) >= std::fabs(dot(singular.directions[1], w)) ? 0 : 1;
    node.x = singular.branch[k];
    const double along = dot(singular.directions[k], w) >= 0.0 ? 1.0 : -1.0;
    const Surface &cone = singular.coneSide == 0 ? a_ : b_, &other = singular.coneSide == 0 ? b_ : a_;
    const int o = singular.coneSide == 0 ? 2 : 0;
    Vec3 d[4];
    cone.evaluate(node.x[2 - o], node.x[3 - o], 1, d);
    const Vec2 onCone(0.0, along * (dot(d[Surface::derivativeIndex(0, 1, 1)], singular.directions[k]) >= 0.0 ? 1.0 : -1.0));
    Vec3 e[4];
    other.evaluate(node.x[o], node.x[o + 1], 1, e);
    const Vec2 onOther = tangentParameters(e[kU], e[kV], w);
    node.da = singular.coneSide == 0 ? onCone : onOther;
    node.db = singular.coneSide == 0 ? onOther : onCone;
    node.setIncoming();
}

void Marcher::registerApexes(SurfaceIntersection &out) {
    for (int side = 0; side < 2; ++side) {
        const Surface &surface = side == 0 ? a_ : b_, &other = side == 0 ? b_ : a_;
        if (surface.type() != SurfaceType::Cone) continue;
        const auto &cone = static_cast<const ConicalSurface &>(surface);
        const Vec3 apex = cone.apex();
        if (!inside(bounds_, apex)) continue;
        const SurfaceProjection onOther = projectPoint(other, apex);
        if (onOther.distance > tolerance_) continue;
        Vec3 n;
        try {
            n = other.normal(onOther.u, onOther.v);
        } catch (const std::exception &) {
            continue;  // anche l'altra e' singolare li'
        }
        const Frame3 &f = cone.frame();
        const double sa = std::sin(cone.semiAngle()), ca = std::cos(cone.semiAngle());
        const double A = sa * dot(n, f.xDir()), B = sa * dot(n, f.yDir()), C = ca * dot(n, f.zDir());
        const double R = std::hypot(A, B);
        if (R <= std::fabs(C) * (1.0 + 1e-9)) {
            out.isolatedPoints.push_back(apex);  // l'altra superficie tocca il cono solo nel vertice
            continue;
        }
        const double phi = std::atan2(B, A), half = std::acos(std::max(-1.0, std::min(1.0, -C / R)));
        const double vApex = -cone.referenceRadius() / sa;
        Singular singular;
        singular.apex = true;
        singular.coneSide = side;
        singular.p = apex;
        for (int k = 0; k < 2; ++k) {
            const double u = phi + (k == 0 ? half : -half);
            singular.directions[k] = sa * (std::cos(u) * f.xDir() + std::sin(u) * f.yDir()) + ca * f.zDir();
            Params x;
            x[side == 0 ? 0 : 2] = u - kTwoPi * std::floor(u / kTwoPi);
            x[side == 0 ? 1 : 3] = vApex;
            x[side == 0 ? 2 : 0] = onOther.u;
            x[side == 0 ? 3 : 1] = onOther.v;
            singular.branch[k] = x;
        }
        singular.x = singular.branch[0];
        out.singularPoints.push_back(apex);
        singular_.push_back(singular);
    }
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
    // Sezioni sovrapposte in parte: le superfici hanno una striscia in comune;
    // le rette agli estremi dei tratti comuni (dove le superfici si separano)
    // sono tagli, anche se li' le sezioni sono tangenti.
    out.coincident = hits.overlap;
    auto onB = [&](double s) { return ia.range.contains(s) && projectPoint(*ib.curve, ia.curve->point(s), ib.range).distance <= tolerance; };
    // Lato della curva B da cui sta il punto (distanza con segno).
    auto side = [&](const Vec2 &q) {
        const CurveProjection<2> projection = projectPoint(*ib.curve, q, ib.range);
        return cross(ib.curve->derivative(projection.parameter), q - projection.point);
    };
    for (const CurveCurvePoint &p : hits.points) {
        const Vec2 ta = ia.curve->derivative(p.s), tb = ib.curve->derivative(p.t);
        bool tangent = std::fabs(cross(normalized(ta), normalized(tb))) <= 1e-9;
        if (hits.overlap) {
            // A meta' strada (al piu') verso il punto vicino: una separazione
            // tangente e' ancora entro la tolleranza troppo vicino al punto.
            double delta = 1e-2 * ia.range.length();
            for (const CurveCurvePoint &q : hits.points)
                if (q.s != p.s) delta = std::min(delta, 0.5 * std::fabs(q.s - p.s));
            const bool before = onB(p.s - delta), after = onB(p.s + delta);
            if (before && after) continue;  // dentro il tratto comune
            if (before || after) tangent = false;
        }
        if (tangent) {
            // Tangenti ma attraversandosi (flesso): e' una retta di taglio.
            const double delta = 1e-5 * ia.range.length();
            if (p.s - delta >= ia.range.lo && p.s + delta <= ia.range.hi
                && side(ia.curve->point(p.s - delta)) * side(ia.curve->point(p.s + delta)) < 0.0)
                tangent = false;
        }
        IntersectionCurve line;
        line.curve = std::make_shared<Line<3>>(frame.toGlobal(Vec3(p.point.x(), p.point.y(), 0.0)), ga.direction);
        if (!clipLineToBox(frame.toGlobal(Vec3(p.point.x(), p.point.y(), 0.0)), ga.direction, bounds, line.range)) continue;
        (tangent ? out.tangentCurves : out.curves).push_back(line);
    }
    return out;
}

}

namespace {

// Finestra dei parametri della superficie che copre il box: un periodo nelle
// direzioni periodiche, il dominio in quelle limitate, altrimenti i valori
// dei parametri negli angoli del box (piano: coordinate; cono e cilindri
// generalizzati: v lineare lungo l'asse).
void parameterWindow(const Surface &surface, const Box &bounds, Interval &u, Interval &v) {
    u = surface.uDomain();
    v = surface.vDomain();
    if (surface.isUPeriodic()) u = {u.lo, u.lo + surface.uPeriod()};
    if (surface.isVPeriodic()) v = {v.lo, v.lo + surface.vPeriod()};
    if (u.isFinite() && v.isFinite()) return;
    std::vector<Vec3> corners;
    for (int i = 0; i < 8; ++i)
        corners.push_back(Vec3(i & 1 ? bounds.hi.x() : bounds.lo.x(), i & 2 ? bounds.hi.y() : bounds.lo.y(), i & 4 ? bounds.hi.z() : bounds.lo.z()));
    Interval pu{1e300, -1e300}, pv{1e300, -1e300};
    auto add = [](Interval &range, double value) {
        range.lo = std::min(range.lo, value);
        range.hi = std::max(range.hi, value);
    };
    for (const Vec3 &c : corners) {
        if (surface.type() == SurfaceType::Plane) {
            const Vec3 local = static_cast<const Plane &>(surface).frame().toLocal(c);
            add(pu, local.x());
            add(pv, local.y());
        } else if (surface.type() == SurfaceType::Cone) {
            const auto &cone = static_cast<const ConicalSurface &>(surface);
            add(pv, dot(c - cone.frame().origin(), cone.frame().zDir()) / std::cos(cone.semiAngle()));
        } else {
            GeneralizedCylinder g;
            if (!generalizedCylinder(surface, g)) throw std::domain_error("intersectSurfaces: superficie illimitata non gestita");
            add(pv, dot(c - g.profile->point(g.domain.lo), g.direction));
        }
    }
    const double margin = 1e-3 * std::max(1.0, bounds.diagonal());
    if (!u.isFinite()) u = {pu.lo - margin, pu.hi + margin};
    if (!v.isFinite()) v = {pv.lo - margin, pv.hi + margin};
}

// Semi per qualsiasi coppia di superfici: le pezze di Bezier delle forme
// NURBS esatte si dividono finche' i box dei poli si toccano e sono piccoli;
// da ogni coppia rimasta Newton (a norma minima, 3 equazioni e 4 incognite)
// sulle superfici vere cerca un punto comune. Ogni componente
// dell'intersezione nel box passa per coppie di pezze i cui box si toccano,
// quindi riceve un seme, tranne anse piu' piccole delle pezze finali.
void subdivisionSeeds(const Surface &a, const Surface &b, const Box &bounds, double tolerance, std::vector<Vec3> &seeds) {
    Interval ua, va, ub, vb;
    parameterWindow(a, bounds, ua, va);
    parameterWindow(b, bounds, ub, vb);
    const std::vector<BSplineSurface> patchesA = toBSplineSurface(a, ua, va).bezierPatches();
    const std::vector<BSplineSurface> patchesB = toBSplineSurface(b, ub, vb).bezierPatches();
    const double scale = std::max(bounds.diagonal(), 1e-9);
    const double leafSize = 0.02 * scale;
    const Box area = bounds.padded(10.0 * tolerance);
    std::size_t visits = 0;
    auto newton = [&](Vec3 pa, Vec3 pb) {
        SurfaceProjection qa = projectPoint(a, pa), qb = projectPoint(b, pb);
        double x[4] = {qa.u, qa.v, qb.u, qb.v};
        for (int iteration = 0; iteration < 40; ++iteration) {
            Vec3 ea[4], eb[4];
            a.evaluate(x[0], x[1], 1, ea);
            b.evaluate(x[2], x[3], 1, eb);
            const Vec3 f = ea[0] - eb[0];
            if (norm(f) <= 1e-12 * scale) break;
            const Vec3 columns[4] = {ea[kU], ea[kV], -eb[kU], -eb[kV]};
            // Passo a norma minima: dx = -J^T (J J^T)^-1 f.
            double m[3][3] = {};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    for (int k = 0; k < 4; ++k) m[i][j] += columns[k][i] * columns[k][j];
            const Vec3 r0(m[0][0], m[0][1], m[0][2]), r1(m[1][0], m[1][1], m[1][2]), r2(m[2][0], m[2][1], m[2][2]);
            const double det = dot(r0, cross(r1, r2));
            if (!(std::fabs(det) > 1e-300)) return;
            const Vec3 rhs = -f;
            const Vec3 y(dot(rhs, cross(r1, r2)) / det, dot(r0, cross(rhs, r2)) / det, dot(r0, cross(r1, rhs)) / det);
            for (int k = 0; k < 4; ++k) x[k] += dot(columns[k], y);
            // Parametri limitati ai domini non periodici.
            const Interval domains[4] = {a.uDomain(), a.vDomain(), b.uDomain(), b.vDomain()};
            const bool periodic[4] = {a.isUPeriodic(), a.isVPeriodic(), b.isUPeriodic(), b.isVPeriodic()};
            for (int k = 0; k < 4; ++k)
                if (!periodic[k]) x[k] = domains[k].clamp(x[k]);
        }
        const Vec3 p = a.point(x[0], x[1]), q = b.point(x[2], x[3]);
        if (distance(p, q) > 1e-9 * scale || !inside(area, p)) return;
        for (const Vec3 &known : seeds)
            if (distance(known, p) <= 1e-3 * leafSize) return;
        seeds.push_back(p);
    };
    std::function<void(const BSplineSurface &, const BSplineSurface &, int)> search = [&](const BSplineSurface &pa, const BSplineSurface &pb,
                                                                                         int depth) {
        if (++visits > 400000) throw std::domain_error("intersectSurfaces: suddivisione per i semi troppo profonda");
        const Box boxA = patchBox(pa), boxB = patchBox(pb);
        if (!boxA.padded(tolerance).overlaps(boxB) || !boxA.overlaps(area) || !boxB.overlaps(area)) return;
        const double da = boxA.diagonal(), db = boxB.diagonal();
        if ((da <= leafSize && db <= leafSize) || depth > 40) {
            const Interval pua = pa.uDomain(), pva = pa.vDomain(), pub = pb.uDomain(), pvb = pb.vDomain();
            newton(pa.point(0.5 * (pua.lo + pua.hi), 0.5 * (pva.lo + pva.hi)), pb.point(0.5 * (pub.lo + pub.hi), 0.5 * (pvb.lo + pvb.hi)));
            return;
        }
        const bool splitA = da >= db;
        const BSplineSurface &patch = splitA ? pa : pb;
        const double uLength = distance(patch.pole(patch.uPoleCount() - 1, 0), patch.pole(0, 0))
                             + distance(patch.pole(patch.uPoleCount() - 1, patch.vPoleCount() - 1), patch.pole(0, patch.vPoleCount() - 1));
        const double vLength = distance(patch.pole(0, patch.vPoleCount() - 1), patch.pole(0, 0))
                             + distance(patch.pole(patch.uPoleCount() - 1, patch.vPoleCount() - 1), patch.pole(patch.uPoleCount() - 1, 0));
        const auto [first, second] = splitPatch(patch, uLength >= vLength);
        if (splitA) {
            search(first, pb, depth + 1);
            search(second, pb, depth + 1);
        } else {
            search(pa, first, depth + 1);
            search(pa, second, depth + 1);
        }
    };
    for (const BSplineSurface &pa : patchesA)
        for (const BSplineSurface &pb : patchesB) search(pa, pb, 0);
}

}

namespace {

// --- Superfici di rivoluzione coassiali --------------------------------------
// Cilindri, coni, sfere, tori e rivoluzioni con il meridiano in un piano per
// l'asse, attorno allo stesso asse: l'intersezione sono i cerchi dei punti
// comuni dei due profili nel semipiano (rho, z), esatti; i tratti di profilo
// in comune sono strisce comuni alle due superfici.

struct RotationalAxis {
    Vec3 point, direction;
    bool free = false;  // sfera: ogni retta per il centro
};

bool rotationalAxis(const Surface &surface, RotationalAxis &axis) {
    switch (surface.type()) {
    case SurfaceType::Cylinder: {
        const Frame3 &f = static_cast<const CylindricalSurface &>(surface).frame();
        axis = {f.origin(), f.zDir(), false};
        return true;
    }
    case SurfaceType::Cone: {
        const Frame3 &f = static_cast<const ConicalSurface &>(surface).frame();
        axis = {f.origin(), f.zDir(), false};
        return true;
    }
    case SurfaceType::Torus: {
        const Frame3 &f = static_cast<const ToroidalSurface &>(surface).frame();
        axis = {f.origin(), f.zDir(), false};
        return true;
    }
    case SurfaceType::Sphere: {
        const Frame3 &f = static_cast<const SphericalSurface &>(surface).frame();
        axis = {f.origin(), f.zDir(), true};
        return true;
    }
    case SurfaceType::Revolution: {
        const auto &r = static_cast<const RevolutionSurface &>(surface);
        axis = {r.axisPoint(), r.axisDirection(), false};
        return true;
    }
    default:
        return false;
    }
}

struct Profile2 {
    CurvePtr<2> curve;
    Interval range;
};

// Profili della superficie nel semipiano (rho >= 0, z) dell'asse (O, d), limitati al box.
bool rotationalProfiles(const Surface &surface, const Vec3 &origin, const Vec3 &d, const Box &bounds, double tolerance,
                        std::vector<Profile2> &out) {
    double zLo = 1e300, zHi = -1e300;
    for (int i = 0; i < 8; ++i) {
        const Vec3 c(i & 1 ? bounds.hi.x() : bounds.lo.x(), i & 2 ? bounds.hi.y() : bounds.lo.y(), i & 4 ? bounds.hi.z() : bounds.lo.z());
        zLo = std::min(zLo, dot(c - origin, d));
        zHi = std::max(zHi, dot(c - origin, d));
    }
    switch (surface.type()) {
    case SurfaceType::Cylinder: {
        const auto &c = static_cast<const CylindricalSurface &>(surface);
        const double s = dot(c.frame().zDir(), d) > 0.0 ? 1.0 : -1.0, z0 = dot(c.frame().origin() - origin, d);
        // v -> z = z0 + s v
        const double v0 = (zLo - z0) * s, v1 = (zHi - z0) * s;
        out.push_back({std::make_shared<Line<2>>(Vec2(c.radius(), z0), Vec2(0.0, s)), {std::min(v0, v1), std::max(v0, v1)}});
        return true;
    }
    case SurfaceType::Cone: {
        const auto &c = static_cast<const ConicalSurface &>(surface);
        const double s = dot(c.frame().zDir(), d) > 0.0 ? 1.0 : -1.0, z0 = dot(c.frame().origin() - origin, d);
        const double sa = std::sin(c.semiAngle()), ca = std::cos(c.semiAngle()), r = c.referenceRadius();
        const double vApex = -r / sa;
        const double v0 = (zLo - z0) * s / ca, v1 = (zHi - z0) * s / ca;
        const Interval all{std::min(v0, v1), std::max(v0, v1)};
        // Due falde: la retta del profilo e la sua immagine riflessa (rho < 0 -> -rho).
        for (double mirror : {1.0, -1.0}) {
            auto line = std::make_shared<Line<2>>(Vec2(mirror * r, z0), Vec2(mirror * sa, s * ca));
            // rho = mirror (r + v sa) >= 0
            Interval range = all;
            if (mirror * sa > 0.0) range.lo = std::max(range.lo, vApex);
            else range.hi = std::min(range.hi, vApex);
            if (range.hi > range.lo) out.push_back({line, range});
        }
        return true;
    }
    case SurfaceType::Sphere: {
        const auto &c = static_cast<const SphericalSurface &>(surface);
        const double zc = dot(c.frame().origin() - origin, d);
        out.push_back({std::make_shared<Circle<2>>(Vec2(0.0, zc), Vec2(1.0, 0.0), Vec2(0.0, 1.0), c.radius()), {-kHalfPi, kHalfPi}});
        return true;
    }
    case SurfaceType::Torus: {
        const auto &c = static_cast<const ToroidalSurface &>(surface);
        const double zc = dot(c.frame().origin() - origin, d);
        out.push_back({std::make_shared<Circle<2>>(Vec2(c.majorRadius(), zc), Vec2(1.0, 0.0), Vec2(0.0, 1.0), c.minorRadius()), {0.0, kTwoPi}});
        return true;
    }
    case SurfaceType::Revolution: {
        const auto &r = static_cast<const RevolutionSurface &>(surface);
        const Curve<3> &meridian = *r.meridian();
        const Interval domain = r.vDomain();
        if (!domain.isFinite()) return false;
        // Piano del meridiano: deve contenere l'asse.
        Vec3 radial;
        for (double f : {0.5, 0.3, 0.7, 0.1, 0.9}) {
            const Vec3 w = meridian.point(domain.lo + f * domain.length()) - origin;
            radial = w - dot(w, d) * d;
            if (norm(radial) > 1e-9) break;
        }
        if (!(norm(radial) > 1e-9)) return false;
        radial = normalized(radial);
        const Vec3 normal = cross(radial, d);
        for (int k = 0; k <= 16; ++k) {
            const Vec3 w = meridian.point(domain.lo + domain.length() * k / 16.0) - origin;
            if (std::fabs(dot(w, normal)) > tolerance || dot(w, radial) < -tolerance) return false;
        }
        const PlanarImage image = planarImage(meridian, domain, Frame3(origin, normal, radial));
        if (!image.curve) return false;
        out.push_back({image.curve, image.range});
        return true;
    }
    default:
        return false;
    }
}

bool coaxialPair(const Surface &a, const Surface &b, double tolerance, Vec3 &origin, Vec3 &direction) {
    RotationalAxis aa, ab;
    if (!rotationalAxis(a, aa) || !rotationalAxis(b, ab)) return false;
    if (aa.free && ab.free) {
        // Due sfere: la retta dei centri (centri coincidenti: nessuna curva da qui).
        if (distance(aa.point, ab.point) <= tolerance) return false;
        origin = aa.point;
        direction = normalized(ab.point - aa.point);
        return true;
    }
    const RotationalAxis &fixed = aa.free ? ab : aa, &other = aa.free ? aa : ab;
    auto offAxis = [&](const Vec3 &p) {
        const Vec3 w = p - fixed.point;
        return norm(w - dot(w, fixed.direction) * fixed.direction);
    };
    if (offAxis(other.point) > tolerance) return false;
    if (!other.free && norm(cross(fixed.direction, other.direction)) > 1e-12) return false;
    origin = fixed.point;
    direction = fixed.direction;
    return true;
}

SurfaceIntersection rotationalCase(const Surface &a, const Surface &b, const Vec3 &origin, const Vec3 &d, const Box &bounds,
                                   double tolerance, bool &handled) {
    SurfaceIntersection out;
    std::vector<Profile2> pa, pb;
    handled = rotationalProfiles(a, origin, d, bounds, tolerance, pa) && rotationalProfiles(b, origin, d, bounds, tolerance, pb);
    if (!handled) return out;
    const Frame3 frame = normalFrame(d, origin);
    std::vector<Vec2> done;
    for (const Profile2 &ca : pa)
        for (const Profile2 &cb : pb) {
            const CurveCurveIntersection hits = intersectCurves(*ca.curve, ca.range, *cb.curve, cb.range, tolerance);
            if (hits.overlap) out.coincident = true;
            auto onB = [&](double s) { return ca.range.contains(s) && projectPoint(*cb.curve, ca.curve->point(s), cb.range).distance <= tolerance; };
            auto side = [&](const Vec2 &q) {
                const CurveProjection<2> projection = projectPoint(*cb.curve, q, cb.range);
                return cross(cb.curve->derivative(projection.parameter), q - projection.point);
            };
            for (const CurveCurvePoint &p : hits.points) {
                const double rho = p.point.x(), z = p.point.y();
                if (rho <= tolerance) continue;  // sull'asse: un punto (polo, vertice), non un cerchio
                bool duplicate = false;
                for (const Vec2 &q : done) duplicate = duplicate || distance(q, p.point) <= tolerance;
                if (duplicate) continue;
                const Vec2 ta = ca.curve->derivative(p.s), tb = cb.curve->derivative(p.t);
                bool tangent = std::fabs(cross(normalized(ta), normalized(tb))) <= 1e-9;
                double delta = 1e-2 * ca.range.length();
                for (const CurveCurvePoint &q : hits.points)
                    if (q.s != p.s) delta = std::min(delta, 0.5 * std::fabs(q.s - p.s));
                if (hits.overlap) {
                    const bool before = onB(p.s - delta), after = onB(p.s + delta);
                    if (before && after) continue;
                    if (before || after) tangent = false;  // estremo di una striscia comune: taglio
                }
                if (tangent && p.s - delta >= ca.range.lo && p.s + delta <= ca.range.hi
                    && side(ca.curve->point(p.s - delta)) * side(ca.curve->point(p.s + delta)) < 0.0)
                    tangent = false;  // tangenti ma attraversandosi (flesso)
                done.push_back(p.point);
                IntersectionCurve circle;
                circle.curve = std::make_shared<Circle<3>>(origin + z * d, frame.xDir(), frame.yDir(), rho);
                circle.range = {0.0, kTwoPi};
                circle.closed = true;
                (tangent ? out.tangentCurves : out.curves).push_back(circle);
            }
        }
    return out;
}

}

SurfaceIntersection intersectSurfaces(const Surface &a, const Surface &b, const Box &bounds, const std::vector<Vec3> &seeds,
                                      const SurfaceIntersectionOptions &options) {
    Vec3 axisPoint, axisDirection;
    if (coaxialPair(a, b, options.tolerance, axisPoint, axisDirection)) {
        bool handled = false;
        SurfaceIntersection out = rotationalCase(a, b, axisPoint, axisDirection, bounds, options.tolerance, handled);
        if (handled) return out;
    }
    GeneralizedCylinder ga, gb;
    if (!generalizedCylinder(a, ga) || !generalizedCylinder(b, gb)) {
        // Coppia qualsiasi: semi dalla suddivisione delle forme NURBS.
        std::vector<Vec3> all;
        for (const Vec3 &p : seeds)
            if (inside(bounds, p)) all.push_back(p);
        subdivisionSeeds(a, b, bounds, options.tolerance, all);
        SurfaceIntersection out;
        Marcher marcher(a, b, bounds, options);
        marcher.registerApexes(out);
        for (const Vec3 &seed : all) marcher.traceFrom(seed, out, true);
        marcher.traceSingularBranches(out);
        for (const Vec3 &seed : all) marcher.traceFrom(seed, out);
        return out;
    }
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
    for (const Vec3 &seed : all) marcher.traceFrom(seed, out, true);
    marcher.traceSingularBranches(out);
    for (const Vec3 &seed : all) marcher.traceFrom(seed, out);
    return out;
}

bool coaxialRotational(const Surface &a, const Surface &b, double tolerance) {
    Vec3 origin, direction;
    return coaxialPair(a, b, tolerance, origin, direction);
}

}
