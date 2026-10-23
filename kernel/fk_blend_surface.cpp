#include "fk_blend_surface.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "fk_blend_model.h"
#include "fk_body_check.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_classify.h"
#include "fk_intersect.h"
#include "fk_curve_algo.h"
#include "fk_pcurve.h"
#include "fk_parallel.h"
#include "fk_offset.h"
#include "fk_nurbs.h"
#include "fk_precision.h"
#include "fk_sheet.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

using Model = detail::BlendModel;

constexpr double kFitTolerance = 1e-9;  // scarto della superficie dagli archi (o dai segmenti) veri
constexpr double kSmooth = 1e-9;        // vertici lisci: 1 - coseno dell'angolo tra le tangenti
constexpr double kCrossSmooth = 5e-5;   // continuazioni separate da un trim/booleano: le curve sono approssimate
constexpr double kSplitPassageTolerance = 1e-6;   // mm: palle dei due lati di un passaggio Split considerate la stessa
constexpr double kFilletSolveTolerance = 1e-2;  // residuo relativo ammesso sulle patch B-spline approssimate

// Curva dello spigolo per le sezioni. Un edge rifilato da una booleana a
// meno della risoluzione da un nodo della curva tracciata comincia (o finisce)
// con un tratto di Hermite lungo qualche 1e-8: li' la derivata seconda non ha
// significato geometrico e, attraverso il piano normale, fa tornare indietro
// le righe dei contatti. Il tratto si sostituisce con il prolungamento del
// tratto vicino (stesso polinomio, extendBSpline); la curva resta quella del
// B-rep altrove e gli estremi si spostano meno della risoluzione.
CurvePtr<3> sectionCurve(const CurvePtr<3> &curve, const Interval &range) {
    const auto *spline = dynamic_cast<const BSplineCurve<3> *>(curve.get());
    if (!spline) return curve;
    double first = range.hi, last = range.lo;
    for (double k : spline->knots())
        if (k > range.lo && k < range.hi) first = std::min(first, k), last = std::max(last, k);
    if (!(first < range.hi)) return curve;
    const bool head = distance(curve->point(range.lo), curve->point(first)) <= kLinearResolution;
    const bool tail = distance(curve->point(last), curve->point(range.hi)) <= kLinearResolution;
    if (!head && !tail) return curve;
    try {
        BSplineCurve<3> c = spline->clamped();
        const int p = c.degree();
        const auto cut = [&](double k, bool keepRight) {
            c = c.insertKnot(k, p - c.multiplicity(k));
            const std::vector<double> &knots = c.knots();
            const std::size_t s = std::size_t(std::lower_bound(knots.begin(), knots.end(), k) - knots.begin());
            std::vector<double> newKnots;
            std::vector<Vec3> poles;
            std::vector<double> weights;
            if (keepRight) {
                newKnots.push_back(k);
                newKnots.insert(newKnots.end(), knots.begin() + std::ptrdiff_t(s), knots.end());
                for (int i = int(s) - 1; i < c.poleCount(); ++i) {
                    poles.push_back(c.poles()[std::size_t(i)]);
                    if (c.isRational()) weights.push_back(c.weight(i));
                }
            } else {
                newKnots.assign(knots.begin(), knots.begin() + std::ptrdiff_t(s + std::size_t(p)));
                newKnots.push_back(k);
                for (int i = 0; i < int(s); ++i) {
                    poles.push_back(c.poles()[std::size_t(i)]);
                    if (c.isRational()) weights.push_back(c.weight(i));
                }
            }
            c = BSplineCurve<3>(p, newKnots, poles, weights);
        };
        if (head) cut(first, true);
        if (tail && last > first) cut(last, false);
        const auto regular = std::make_shared<BSplineCurve<3>>(extendBSpline(c, range.lo, range.hi));
        if (distance(regular->point(range.lo), curve->point(range.lo)) > kLinearResolution
            || distance(regular->point(range.hi), curve->point(range.hi)) > kLinearResolution)
            return curve;
        return regular;
    } catch (const std::exception &) {
        return curve;
    }
}

// Punto di una faccia con le derivate prime, la normale uscente e le sue derivate.
struct FacePoint {
    Vec3 p, su, sv, n, nu, nv;
};

FacePoint facePoint(const Surface &surface, bool sense, double u, double v) {
    Vec3 d[9];
    surface.evaluate(u, v, 2, d);
    const auto at = [&](int k, int l) { return d[Surface::derivativeIndex(k, l, 2)]; };
    FacePoint r;
    r.p = at(0, 0);
    r.su = at(1, 0);
    r.sv = at(0, 1);
    const Vec3 m = cross(r.su, r.sv);
    const double l = norm(m);
    if (!(l > 0.0)) throw std::domain_error("blendEdges: punto singolare della superficie");
    const Vec3 n = m / l;
    const Vec3 mu = cross(at(2, 0), r.sv) + cross(r.su, at(1, 1)), mv = cross(at(1, 1), r.sv) + cross(r.su, at(0, 2));
    r.n = n;
    r.nu = (mu - dot(n, mu) * n) / l;
    r.nv = (mv - dot(n, mv) * n) / l;
    if (!sense) {
        r.n = -r.n;
        r.nu = -r.nu;
        r.nv = -r.nv;
    }
    return r;
}

// Parametri vicino al dominio (le direzioni periodiche restano libere): un
// po' oltre i bordi si prolunga la superficie (i raccordi negli angoli vivi e
// contro le facce d'estremita' continuano oltre il vertice).
void clampToDomain(const Surface &surface, double &u, double &v) {
    const auto clamp = [](double x, const Interval &d) {
        if (!d.isFinite()) return x;
        const double margin = 0.5 * d.length();
        return std::clamp(x, d.lo - margin, d.hi + margin);
    };
    if (!surface.isUPeriodic()) u = clamp(u, surface.uDomain());
    if (!surface.isVPeriodic()) v = clamp(v, surface.vDomain());
}

// Sistema lineare N x N con pivot parziale (falso se singolare).
template <int N>
bool solveLinear(double (&a)[N][N], double (&b)[N]) {
    for (int col = 0; col < N; ++col) {
        int pivot = col;
        for (int row = col + 1; row < N; ++row)
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
        if (!(std::fabs(a[pivot][col]) > 0.0)) return false;
        if (pivot != col) {
            for (int k = 0; k < N; ++k) std::swap(a[col][k], a[pivot][k]);
            std::swap(b[col], b[pivot]);
        }
        for (int row = col + 1; row < N; ++row) {
            const double f = a[row][col] / a[col][col];
            for (int k = col; k < N; ++k) a[row][k] -= f * a[col][k];
            b[row] -= f * b[col];
        }
    }
    for (int row = N - 1; row >= 0; --row) {
        double s = b[row];
        for (int k = row + 1; k < N; ++k) s -= a[row][k] * b[k];
        b[row] = s / a[row][row];
    }
    return true;
}

// Riga della superficie in coordinate omogenee: (w P, w).
struct HPoint {
    Vec3 p;
    double w = 1.0;
};
HPoint operator+(const HPoint &a, const HPoint &b) { return {a.p + b.p, a.w + b.w}; }
HPoint operator-(const HPoint &a, const HPoint &b) { return {a.p - b.p, a.w - b.w}; }
HPoint operator*(double s, const HPoint &a) { return {s * a.p, s * a.w}; }

// Sezione del raccordo (o dello smusso) nel parametro t dello spigolo.
struct Section {
    double t = 0.0;
    double residual = 0.0;               // scarto numerico delle superfici offset
    double noise = 0.0;                  // incertezza dei contatti dovuta al residuo (condizionamento)
    double x[4] = {0.0, 0.0, 0.0, 0.0};   // (uA, vA, uB, vB)
    double dx[4] = {0.0, 0.0, 0.0, 0.0};  // derivate rispetto a t
    Vec3 center, dcenter;                 // centro della palla (raccordo)
    HPoint row[3], drow[3];               // righe: contatto su A, punto di mezzo pesato (raccordo), contatto su B
};

// Punto razionale della sezione nel parametro v in [0, 1] (quadrica o segmento).
Vec3 sectionPoint(const HPoint *rows, bool chamfer, double v) {
    if (chamfer) return (1.0 - v) * rows[0].p + v * rows[2].p;
    const double b0 = (1.0 - v) * (1.0 - v), b1 = 2.0 * v * (1.0 - v), b2 = v * v;
    const HPoint h = b0 * rows[0] + b1 * rows[1] + b2 * rows[2];
    return h.p / h.w;
}

// Le sezioni lungo uno spigolo tra le facce A e B.
class SectionSolver {
public:
    // Smusso: `size` sulla faccia A, `sizeB` sulla B (< 0: `size`).
  SectionSolver(CurvePtr<3> edge, SurfacePtr a, bool senseA, SurfacePtr b, bool senseB, double size, bool chamfer, bool convex, double scale,
                double sizeB = -1.0, CurvePtr<2> guide = {}, double middle = 0.5, Interval guideRange = {0.0, 1.0}, bool accurate = false)
      : edge_(std::move(edge)), a_(std::move(a)), b_(std::move(b)), senseA_(senseA), senseB_(senseB), size_(size), sizeB_(sizeB < 0.0 ? size : sizeB),
        chamfer_(chamfer), convex_(convex), scale_(scale), guide_(std::move(guide)), accurate_(accurate) {
      if (guide_) {
          const Vec2 d = guide_->derivative(middle);
          const double du = b_->uDomain().length(), dv = b_->vDomain().length();
          guideAxis_ = std::fabs(d.x()) / du >= std::fabs(d.y()) / dv ? 0 : 1;
          const Vec2 uv = guide_->point(middle);
          const FacePoint at = facePoint(*b_, senseB_, uv.x(), uv.y());
          guideMetric_ = norm(guideAxis_ == 0 ? at.su : at.sv);
          if (!(guideMetric_ > 0.0) || !std::isfinite(guideMetric_)) throw std::domain_error("blendEdges: coordinata del fianco singolare");
          // Il vincolo deve identificare una sola sezione lungo il bordo.
          // Non usare una coordinata che torna indietro o diventa stazionaria.
          const double direction = guide_->derivative(middle)[guideAxis_];
          for (int i = 0; i <= 32; ++i) {
              const double t = guideRange.lo + guideRange.length() * i / 32.0;
              if (!(guide_->derivative(t)[guideAxis_] * direction > 1e-12 * direction * direction))
                  throw std::domain_error("blendEdges: il bordo non ha una coordinata monotona sul fianco");
          }
      }
  }

    bool chamfer() const { return chamfer_; }
    double size() const { return size_; }
    bool intrinsicGuide() const { return bool(guide_); }

    // Sezione in t a partire dalla stima in s.x (le altre voci si ricalcolano). `left`:
    // derivate dello spigolo da sinistra (nei suoi nodi). Falso se Newton non converge.
    bool solve(double t, bool left, Section &s, double radius = -1.0) const {
        if (radius < 0.0) radius = size_;
        s.t = t;
        Vec3 e[3];
        if (left) edge_->evaluateLeft(t, 2, e);
        else edge_->evaluate(t, 2, e);
        const double speed = norm(e[1]);
        if (!(speed > 0.0)) return false;
        const Vec3 T = e[1] / speed, dT = (e[2] - dot(T, e[2]) * T) / speed;
        const double tolerance = 1e-14 * scale_;
        return chamfer_ ? solveChamfer(e, T, dT, radius, tolerance, s) : solveFillet(e, T, dT, radius, tolerance, s);
    }

    // Sezione in t continuando da quella vicina (predittore lungo le derivate);
    // se Newton non converge, o salta su un altro ramo, a passi piu' corti.
    Section follow(double t, bool left, const Section &from) const {
        Section current = from;
        double step = t - from.t;
        const double smallest = 1e-12 * (1.0 + std::fabs(t) + std::fabs(from.t));
        while (true) {
            const bool last = std::fabs(t - current.t) <= std::fabs(step);
            const double next = last ? t : current.t + step;
            Section s = current;
            for (int k = 0; k < 4; ++k) s.x[k] = current.x[k] + current.dx[k] * (next - current.t);
            if (solve(next, last && left, s) && coherent(current, s)) {
                if (last) return s;
                current = s;
                step *= 2.0;
                continue;
            }
            step *= 0.5;
            if (std::fabs(step) < smallest)
                throw std::domain_error("blendEdges: sezione del raccordo non trovata (raggio troppo grande o facce che si ripiegano)");
        }
    }

private:
    // La soluzione non salta su un altro ramo: i contatti restano vicini a quelli
    // previsti dalle derivate (l'errore del predittore e' del secondo ordine).
    bool coherent(const Section &from, const Section &to) const {
        const double h = to.t - from.t;
        const double jump = std::max(distance(from.row[0].p + h * from.drow[0].p, to.row[0].p), distance(from.row[2].p + h * from.drow[2].p, to.row[2].p));
        return jump <= 0.1 * size_;
    }

    bool solveFillet(const Vec3 *e, const Vec3 &T, const Vec3 &dT, double radius, double tolerance, Section &s) const {
        const double k = convex_ ? -radius : radius;
        double x[4] = {s.x[0], s.x[1], s.x[2], s.x[3]};
        auto residual = [&](const double *y, FacePoint &pa, FacePoint &pb, double *f) {
            pa = facePoint(*a_, senseA_, y[0], y[1]);
            pb = facePoint(*b_, senseB_, y[2], y[3]);
            const Vec3 ca = pa.p + k * pa.n, cb = pb.p + k * pb.n;
            const Vec3 d = ca - cb;
            f[0] = d.x();
            f[1] = d.y();
            f[2] = d.z();
            f[3] = guide_ ? guideMetric_ * (y[2 + guideAxis_] - guide_->point(s.t)[guideAxis_]) : dot(ca - e[0], T);
            return std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2] + f[3] * f[3]);
        };
        FacePoint pa, pb;
        double f[4];
        double error;
        try {
            error = residual(x, pa, pb, f);
        } catch (const std::domain_error &) {
            return false;
        }
        double J[4][4];
        auto jacobian = [&]() {
            const Vec3 c0 = pa.su + k * pa.nu, c1 = pa.sv + k * pa.nv, c2 = -(pb.su + k * pb.nu), c3 = -(pb.sv + k * pb.nv);
            const Vec3 cols[4] = {c0, c1, c2, c3};
            for (int j = 0; j < 4; ++j) {
                J[0][j] = cols[j].x();
                J[1][j] = cols[j].y();
                J[2][j] = cols[j].z();
                J[3][j] = guide_ ? (j == 2 + guideAxis_ ? guideMetric_ : 0.0) : (j < 2 ? dot(cols[j], T) : 0.0);
            }
        };
        bool converged = error <= tolerance;
        for (int iteration = 0; iteration < 60 && !converged; ++iteration) {
            jacobian();
            double step[4] = {-f[0], -f[1], -f[2], -f[3]};
            if (!solveLinear<4>(J, step)) return false;
            double lambda = 1.0;
            bool improved = false;
            for (int attempt = 0; attempt < 12; ++attempt, lambda *= 0.5) {
                double y[4];
                for (int j = 0; j < 4; ++j) y[j] = x[j] + lambda * step[j];
                clampToDomain(*a_, y[0], y[1]);
                clampToDomain(*b_, y[2], y[3]);
                FacePoint qa, qb;
                double g[4];
                double next;
                try {
                    next = residual(y, qa, qb, g);
                } catch (const std::domain_error &) {
                    continue;
                }
                if (next < error || next <= tolerance) {
                    std::copy(y, y + 4, x);
                    std::copy(g, g + 4, f);
                    pa = qa;
                    pb = qb;
                    const double previous = error;
                    error = next;
                    improved = true;
                    // Fermo anche quando l'arrotondamento non permette di scendere oltre.
                    converged = error <= tolerance || (lambda == 1.0 && error > 0.5 * previous && error < 1e-12 * scale_);
                    break;
                }
            }
            if (!improved) {
                // Sulle B-spline dei loft l'intersezione delle due superfici
                // offset puo' fermarsi al limite numerico della pezza: il
                // residuo scende di molti ordini ma non fino alla soglia da
                // aritmetica esatta. Accettalo solo dopo la stagnazione e se
                // resta trascurabile rispetto al raggio; i controlli
                // topologici finali verificano comunque i contatti ottenuti.
                converged = error <= std::max(1e-12 * scale_, kFilletSolveTolerance * radius);
                break;
            }
        }
        // Le nuove continuazioni non possono sfruttare il residuo relativo
        // dei vecchi loft per accettare una palla con raggio diverso.
        if (!converged || (accurate_ && error > 1e-12 * scale_)) return false;
        if (guide_) {
            // Le coordinate intrinseche evitano il ripiegamento dei piani
            // normali al vecchio bordo, ma non consentono di oltrepassare una
            // singolarita' geometrica dell'offset o una soluzione imprecisa.
            if (error > 1e-12 * scale_) return false;
            const auto regular = [k](const FacePoint &p) {
                const double E = dot(p.su, p.su), F = dot(p.su, p.sv), G = dot(p.sv, p.sv);
                const Vec3 u = p.su + k * p.nu, v = p.sv + k * p.nv;
                const double a = dot(p.su, u), b = dot(p.su, v), c = dot(p.sv, u), d = dot(p.sv, v);
                const double det = E * G - F * F;
                return a * d - b * c > 1e-12 * det && G * a + E * d - F * (b + c) > 0.0;
            };
            if (!regular(pa) || !regular(pb)) return false;
        }
        // Derivate rispetto a t: J dx = -dF/dt, solo la quarta equazione dipende da t.
        jacobian();
        // Quanto il residuo sposta i contatti: con le facce quasi tangenti (il
        // raccordo che svanisce dove l'angolo tra le facce va a zero) le due
        // superfici offset si tagliano di striscio e un residuo di 1e-13 sposta
        // la sezione di residuo / sin(angolo / 2). Il fit non puo' scendere sotto.
        double amplification = 1.0;
        for (int i = 0; i < 4; ++i) {
            double copy[4][4], unit[4] = {0.0, 0.0, 0.0, 0.0};
            std::copy(&J[0][0], &J[0][0] + 16, &copy[0][0]);
            unit[i] = 1.0;
            if (!solveLinear<4>(copy, unit)) return false;
            amplification = std::max({amplification, norm(pa.su * unit[0] + pa.sv * unit[1]), norm(pb.su * unit[2] + pb.sv * unit[3])});
        }
        const Vec3 ca = pa.p + k * pa.n, cb = pb.p + k * pb.n, center = 0.5 * (ca + cb);
        double rhs[4] = {0.0, 0.0, 0.0, guide_ ? guideMetric_ * guide_->derivative(s.t)[guideAxis_] : dot(e[1], T) - dot(center - e[0], dT)};
        if (!solveLinear<4>(J, rhs)) return false;
        std::copy(x, x + 4, s.x);
        std::copy(rhs, rhs + 4, s.dx);
        s.residual = error;
        s.noise = error * amplification;
        const Vec3 dpA = pa.su * rhs[0] + pa.sv * rhs[1], dpB = pb.su * rhs[2] + pb.sv * rhs[3];
        const Vec3 dc = (pa.su + k * pa.nu) * rhs[0] + (pa.sv + k * pa.nv) * rhs[1];
        s.center = center;
        s.dcenter = dc;
        // Arco dal contatto su A a quello su B: punto di mezzo e peso.
        const Vec3 av = pa.p - center, bv = pb.p - center, dav = dpA - dc, dbv = dpB - dc;
        const double r2 = radius * radius;
        const double cosine = dot(av, bv) / r2, dcosine = (dot(dav, bv) + dot(av, dbv)) / r2;
        if (!(1.0 + cosine > 1e-3)) throw std::domain_error("blendEdges: le facce si ripiegano (arco del raccordo vicino a 180 gradi)");
        const Vec3 mid = center + (av + bv) / (1.0 + cosine);
        const Vec3 dmid = dc + (dav + dbv) / (1.0 + cosine) - (av + bv) * (dcosine / ((1.0 + cosine) * (1.0 + cosine)));
        const double w = std::sqrt(0.5 * (1.0 + cosine)), dw = dcosine / (4.0 * w);
        s.row[0] = {pa.p, 1.0};
        s.drow[0] = {dpA, 0.0};
        s.row[1] = {w * mid, w};
        s.drow[1] = {dw * mid + w * dmid, dw};
        s.row[2] = {pb.p, 1.0};
        s.drow[2] = {dpB, 0.0};
        return true;
    }

    // Smusso: su ciascuna faccia il punto a distanza d dallo spigolo nel piano normale.
    bool solveChamfer(const Vec3 *e, const Vec3 &T, const Vec3 &dT, double d, double tolerance, Section &s) const {
        s.residual = 0.0;
        for (int side = 0; side < 2; ++side) {
            const Surface &surface = side == 0 ? *a_ : *b_;
            const bool sense = side == 0 ? senseA_ : senseB_;
            // `d` e' la distanza su A; quella su B nella stessa proporzione (continuazione nella misura).
            const double reach = side == 0 ? d : d * sizeB_ / size_;
            double x[2] = {s.x[2 * side], s.x[2 * side + 1]};
            FacePoint p;
            auto residual = [&](const double *y, FacePoint &q, double *f) {
                q = facePoint(surface, sense, y[0], y[1]);
                const Vec3 r = q.p - e[0];
                f[0] = norm(r) - reach;
                f[1] = dot(r, T);
                return std::hypot(f[0], f[1]);
            };
            double f[2], error;
            try {
                error = residual(x, p, f);
            } catch (const std::domain_error &) {
                return false;
            }
            double J[2][2];
            auto jacobian = [&]() {
                const Vec3 r = p.p - e[0];
                const double l = norm(r);
                if (!(l > 0.0)) return false;
                const Vec3 g = r / l;
                J[0][0] = dot(g, p.su);
                J[0][1] = dot(g, p.sv);
                J[1][0] = dot(p.su, T);
                J[1][1] = dot(p.sv, T);
                return true;
            };
            bool converged = error <= tolerance;
            for (int iteration = 0; iteration < 60 && !converged; ++iteration) {
                if (!jacobian()) return false;
                double step[2] = {-f[0], -f[1]};
                if (!solveLinear<2>(J, step)) return false;
                double lambda = 1.0;
                bool improved = false;
                for (int attempt = 0; attempt < 12; ++attempt, lambda *= 0.5) {
                    double y[2] = {x[0] + lambda * step[0], x[1] + lambda * step[1]};
                    clampToDomain(surface, y[0], y[1]);
                    FacePoint q;
                    double g[2], next;
                    try {
                        next = residual(y, q, g);
                    } catch (const std::domain_error &) {
                        continue;
                    }
                    if (next < error || next <= tolerance) {
                        x[0] = y[0];
                        x[1] = y[1];
                        f[0] = g[0];
                        f[1] = g[1];
                        p = q;
                        const double previous = error;
                        error = next;
                        improved = true;
                        converged = error <= tolerance || (lambda == 1.0 && error > 0.5 * previous && error < 1e-12 * scale_);
                        break;
                    }
                }
                if (!improved) {
                    converged = error < 1e-12 * scale_;
                    break;
                }
            }
            if (!converged || !jacobian()) return false;
            s.residual = std::max(s.residual, error);
            const Vec3 r = p.p - e[0];
            double rhs[2] = {dot(r / norm(r), e[1]), dot(e[1], T) - dot(r, dT)};
            if (!solveLinear<2>(J, rhs)) return false;
            s.x[2 * side] = x[0];
            s.x[2 * side + 1] = x[1];
            s.dx[2 * side] = rhs[0];
            s.dx[2 * side + 1] = rhs[1];
            const int row = side == 0 ? 0 : 2;
            s.row[row] = {p.p, 1.0};
            s.drow[row] = {p.su * rhs[0] + p.sv * rhs[1], 0.0};
        }
        s.row[1] = 0.5 * (s.row[0] + s.row[2]);
        s.drow[1] = 0.5 * (s.drow[0] + s.drow[2]);
        s.center = s.row[1].p;
        s.dcenter = s.drow[1].p;
        return true;
    }

    CurvePtr<3> edge_;
    SurfacePtr a_, b_;
    bool senseA_, senseB_;
    double size_, sizeB_;
    bool chamfer_, convex_;
    double scale_;
    CurvePtr<2> guide_;
    int guideAxis_ = 0;
    double guideMetric_ = 1.0;
    bool accurate_ = false;
};

// Righe della superficie lungo lo spigolo: cubiche di Hermite a tratti (omogenee).
struct RowFit {
    std::vector<double> breaks;
    std::vector<std::vector<HPoint>> poles;  // per riga: 3 N + 1 poli omogenei
    double error = 0.0;
};

// Raccordo di uno spigolo: le sezioni (in cache) e la superficie.
class EdgeBlend {
public:
    EdgeBlend(const SectionSolver &solver, CurvePtr<3> curve, const Interval &range) : solver_(solver), curve_(std::move(curve)), range_(range) {}

    void seed(const Section &s) { cache_[s.t] = s; }
    const Section &section(double t, bool left = false) {
        const auto found = cache_.find(t);
        if (found != cache_.end() && !left) return found->second;
        // Dalla sezione in cache piu' vicina.
        auto above = cache_.lower_bound(t);
        const Section *nearest = nullptr;
        if (above != cache_.end()) nearest = &above->second;
        if (above != cache_.begin()) {
            const Section &below = std::prev(above)->second;
            if (!nearest || std::fabs(below.t - t) < std::fabs(nearest->t - t)) nearest = &below;
        }
        if (!nearest) throw std::logic_error("blendEdges: sezioni senza seme");
        const Section s = solver_.follow(t, left, *nearest);
        if (left) {
            leftCache_[t] = s;
            return leftCache_[t];
        }
        return cache_[t] = s;
    }

    RowFit fit() { return fit(range_); }
    RowFit fit(const Interval &range) {
        const Interval range_ = range;
        const bool chamfer = solver_.chamfer();
        const std::vector<int> rows = chamfer ? std::vector<int>{0, 2} : std::vector<int>{0, 1, 2};
        std::vector<double> breaks = curve_->breakpoints(range_);
        // Un nodo a meno della risoluzione da un altro (lo spigolo di una
        // booleana rifilato appena prima di un nodo della curva tracciata)
        // darebbe un tratto di lunghezza nulla, con le sezioni dei due
        // estremi uguali e derivate senza significato: non e' un punto della
        // geometria e si toglie (gli estremi del tratto restano).
        for (std::size_t k = 1; k < breaks.size();) {
            if (distance(curve_->point(breaks[k - 1]), curve_->point(breaks[k])) > kLinearResolution) {
                ++k;
                continue;
            }
            breaks.erase(breaks.begin() + std::ptrdiff_t(k + 1 == breaks.size() && k > 1 ? k - 1 : k));
        }
        const std::set<double> knots(breaks.begin(), breaks.end());
        std::vector<double> start;
        for (std::size_t k = 0; k + 1 < breaks.size(); ++k) {
            const double a = breaks[k], b = breaks[k + 1];
            const int pieces = std::max(1, int(std::ceil(8.0 * (b - a) / range_.length())));
            for (int j = 0; j < pieces; ++j) start.push_back(a + (b - a) * j / pieces);
        }
        start.push_back(range_.hi);
        RowFit result;
        result.breaks.push_back(start.front());
        result.poles.assign(rows.size(), {});
        {
            const Section &first = section(start.front());
            for (std::size_t r = 0; r < rows.size(); ++r) result.poles[r].push_back(first.row[rows[r]]);
        }
        // Intervalli da interpolare con l'errore dell'intervallo padre (0: nessuno).
        struct Span {
            double a, b, parentError;
        };
        std::vector<Span> pending;
        for (std::size_t k = start.size() - 1; k > 0; --k) pending.push_back({start[k - 1], start[k], 0.0});
        int guard = 0;
        while (!pending.empty()) {
            if (++guard > 40000) throw std::domain_error("blendEdges: spigolo troppo complesso da raccordare");
            const auto [a, b, parentError] = pending.back();
            pending.pop_back();
            const Section sa = section(a), sb = section(b, knots.count(b) > 0);
            const double h = b - a;
            double error = 0.0;
            double numericalFloor = std::max({sa.residual, sb.residual, sa.noise, sb.noise});
            for (double f : {0.2, 0.5, 0.8}) {
                const Section &exact = section(a + f * h);
                numericalFloor = std::max({numericalFloor, exact.residual, exact.noise});
                const double h00 = (1 + 2 * f) * (1 - f) * (1 - f), h10 = f * (1 - f) * (1 - f), h01 = f * f * (3 - 2 * f), h11 = f * f * (f - 1);
                HPoint fitted[3];
                for (int r = 0; r < 3; ++r)
                    fitted[r] = h00 * sa.row[r] + (h10 * h) * sa.drow[r] + h01 * sb.row[r] + (h11 * h) * sb.drow[r];
                if (!chamfer && !(fitted[1].w > 0.0)) throw std::domain_error("blendEdges: peso del raccordo non valido");
                for (double v : {0.0, 0.25, 0.5, 0.75, 1.0})
                    error = std::max(error, distance(sectionPoint(fitted, chamfer, v), sectionPoint(exact.row, chamfer, v)));
                // Il percorso classico non deve tornare indietro nei piani
                // normali al vecchio bordo. Il percorso intrinseco controlla
                // invece monotonia del parametro e regolarita' degli offset.
                // Il confronto con la vecchia tangente non vale per quella
                // parametrizzazione della nuova curva dei centri.
                const Vec3 tangent = curve_->derivative(a + f * h);
                if (!solver_.intrinsicGuide() && (!(dot(exact.drow[0].p, tangent) > 0.0) || !(dot(exact.drow[2].p, tangent) > 0.0)))
                    throw std::domain_error("blendEdges: raggio maggiore del raggio di curvatura delle facce lungo lo spigolo");
            }
            // Dimezzando l'intervallo l'errore della cubica cala di circa 16
            // volte; se resta almeno la meta' di quello del padre ed e' vicino
            // alla soglia numerica, lo scarto e' il rumore delle sezioni (la
            // stima di Section::noise puo' essere ottimista di qualche volta).
            const bool noise = parentError > 0.0 && error >= 0.5 * parentError && error <= 16.0 * numericalFloor;
            if (error > std::max(kFitTolerance, numericalFloor) && !noise && h > 1e-9 * range_.length()) {
                const double m = 0.5 * (a + b);
                pending.push_back({m, b, error});
                pending.push_back({a, m, error});
                continue;
            }
            // Un salto dei contatti non diventa rumore solo perche' il
            // tratto e' ormai minuscolo: attraversa una piega interna della
            // superficie. Accettarlo crea pezze con contorni non proiettabili.
            if (h <= 1e-9 * range_.length() && error > std::max(1e-6 * solver_.size(), 16.0 * numericalFloor))
                throw std::domain_error("blendEdges: discontinuita' interna della superficie lungo il raccordo; "
                                        "regolarizza la tangenza della superficie o suddividila prima di raccordare");
            result.error = std::max(result.error, error);
            for (std::size_t r = 0; r < rows.size(); ++r) {
                std::vector<HPoint> &p = result.poles[r];
                const int row = rows[r];
                p.push_back(sa.row[row] + (h / 3.0) * sa.drow[row]);
                p.push_back(sb.row[row] - (h / 3.0) * sb.drow[row]);
                p.push_back(sb.row[row]);
            }
            result.breaks.push_back(b);
        }
        return result;
    }

    static std::shared_ptr<BSplineSurface> surface(const RowFit &fit, bool chamfer) {
        std::vector<double> uKnots;
        for (std::size_t k = 0; k < fit.breaks.size(); ++k) {
            const int multiplicity = (k == 0 || k + 1 == fit.breaks.size()) ? 4 : 3;
            for (int j = 0; j < multiplicity; ++j) uKnots.push_back(fit.breaks[k]);
        }
        const int uCount = int(fit.poles[0].size()), vCount = int(fit.poles.size());
        std::vector<Vec3> poles(std::size_t(uCount * vCount));
        std::vector<double> weights;
        if (!chamfer) weights.resize(poles.size());
        for (int i = 0; i < uCount; ++i)
            for (int j = 0; j < vCount; ++j) {
                const HPoint &h = fit.poles[std::size_t(j)][std::size_t(i)];
                if (!(h.w > 0.0)) throw std::domain_error("blendEdges: peso del raccordo non valido");
                poles[std::size_t(i * vCount + j)] = h.p / h.w;
                if (!chamfer) weights[std::size_t(i * vCount + j)] = h.w;
            }
        const std::vector<double> vKnots = chamfer ? std::vector<double>{0, 0, 1, 1} : std::vector<double>{0, 0, 0, 1, 1, 1};
        return std::make_shared<BSplineSurface>(3, chamfer ? 1 : 2, uKnots, vKnots, uCount, vCount, poles, weights);
    }

private:
    const SectionSolver &solver_;
    CurvePtr<3> curve_;
    Interval range_;
    std::map<double, Section> cache_, leftCache_;
};

std::vector<EdgeId> edgesAtVertex(const Body &body, VertexId v) {
    std::vector<EdgeId> result;
    for (EdgeId e : body.edges())
        if (body.edgeStart(e) == v || body.edgeEnd(e) == v) result.push_back(e);
    return result;
}

// Tangente della fin (nel suo verso) all'inizio o alla fine.
Vec3 finTangent(const Body &body, FinId f, bool atEnd) {
    const Fin &fin = body.fin(f);
    const Edge &edge = body.edge(fin.edge);
    const bool hi = fin.sense == atEnd;
    Vec3 d[2];
    if (hi) edge.curve->evaluateLeft(edge.range.hi, 1, d);
    else edge.curve->evaluate(edge.range.lo, 1, d);
    const Vec3 t = normalized(d[1]);
    return fin.sense ? t : -t;
}

// Normale uscente della faccia nel punto (vicino alla faccia).
Vec3 faceNormal(const Body &body, FaceId f, const Vec3 &p) {
    const Face &face = body.face(f);
    const SurfaceProjection q = projectPoint(*face.surface, p);
    const Vec3 n = normalAt(*face.surface, q.u, q.v);
    return face.sense ? n : -n;
}

// Lo spigolo c tra due facce B e i suoi prolungamenti tangenti tra le stesse
// due facce attraverso vertici con due soli spigoli: una cucitura divisa da
// un'estensione (fk_sheet) e' un solo bordo per il contatto che la attraversa.
std::vector<EdgeId> seamChain(const Body &body, EdgeId c) {
    std::vector<EdgeId> chain{c};
    const Edge &first = body.edge(c);
    if (!first.forward.valid() || !first.backward.valid()) return chain;
    const FaceId f0 = body.finFace(first.forward), f1 = body.finFace(first.backward);
    const auto tangentAt = [&](EdgeId e, VertexId v) {
        const Edge &edge = body.edge(e);
        Vec3 values[2];
        if (body.edgeEnd(e) == v) {
            edge.curve->evaluateLeft(edge.range.hi, 1, values);
            return normalized(values[1]);  // in arrivo
        }
        edge.curve->evaluate(edge.range.lo, 1, values);
        return normalized(-values[1]);     // in arrivo percorrendo l'edge al contrario
    };
    for (bool atEnd : {true, false}) {
        EdgeId current = c;
        VertexId v = atEnd ? body.edgeEnd(c) : body.edgeStart(c);
        for (std::size_t guard = 0; guard < body.edges().size(); ++guard) {
            const std::vector<EdgeId> at = edgesAtVertex(body, v);
            if (at.size() != 2) break;
            const EdgeId next = at[0] == current ? at[1] : at[0];
            if (next == current || std::find(chain.begin(), chain.end(), next) != chain.end()) break;
            const Edge &n = body.edge(next);
            if (!n.forward.valid() || !n.backward.valid()) break;
            const FaceId g0 = body.finFace(n.forward), g1 = body.finFace(n.backward);
            if (!((g0 == f0 && g1 == f1) || (g0 == f1 && g1 == f0))) break;
            // Continuazione tangente: la tangente in arrivo su current e quella in partenza su next.
            if (dot(tangentAt(current, v), -tangentAt(next, v)) < 1.0 - 1e-6) break;
            chain.push_back(next);
            v = body.edgeStart(next) == v ? body.edgeEnd(next) : body.edgeStart(next);
            current = next;
        }
    }
    return chain;
}

// Giunti tra fin consecutive della catena (nel loop della faccia A):
//  - Smooth: tangenti, solo i due spigoli nel vertice, stessa faccia B;
//  - Split: tangenti, un terzo spigolo c tra due facce B tangenti tra loro
//    (una circonferenza divisa dalle pezze di un loft o di uno sweep): il
//    raccordo passa da una faccia all'altra dove il contatto attraversa c;
//  - SplitA: come Split, ma B continua e cambia il supporto A;
//  - Mitre: angolo vivo convesso di A con il terzo spigolo c tra le facce B:
//    i due raccordi si tagliano lungo la loro intersezione;
//  - Cross: lo spigolo prosegue tangente su un'altra coppia di facce (A', B')
//    tangenti ad A e B lungo gli spigoli del vertice (i fianchi di uno sweep
//    dove il percorso passa da un segmento a un arco): la stessa sezione.
enum class Joint { Smooth, Split, SplitA, Mitre, Cross };

struct ChainFin {
    FinId fin;  // fin della faccia A
    EdgeId edge;
    FaceId a, b;
    bool sense = true;
    double start = 0.0, end = 0.0;  // parametri dell'edge all'inizio e alla fine della fin
};

struct LoopChain {
    FaceId a;
    std::vector<ChainFin> fins;
    bool closed = false;
    std::vector<Joint> joints;     // joints[i]: tra fins[i] e fins[i + 1] (chiusa: anche tra l'ultima e la prima)
    std::vector<EdgeId> third;     // lo spigolo c del giunto (Split, Mitre); Cross: lo spigolo tra A e A'
    std::vector<EdgeId> fourth;    // Cross: lo spigolo tra B e B'
};

ChainFin chainFin(const Body &body, FinId f) {
    ChainFin c;
    c.fin = f;
    c.edge = body.fin(f).edge;
    c.a = body.finFace(f);
    c.b = body.finFace(body.otherFin(f));
    c.sense = body.fin(f).sense;
    const Interval &r = body.edge(c.edge).range;
    c.start = c.sense ? r.lo : r.hi;
    c.end = c.sense ? r.hi : r.lo;
    return c;
}

// Il giunto nel vertice tra la fin `in` e la fin `out` di A (eccezione se non gestito).
Joint classifyJoint(const Body &body, FinId in, FinId out, EdgeId &third) {
    third = EdgeId();
    const VertexId v = body.finEnd(in);
    const std::vector<EdgeId> at = edgesAtVertex(body, v);
    const FaceId bIn = body.finFace(body.otherFin(in)), bOut = body.finFace(body.otherFin(out));
    const Vec3 tIn = finTangent(body, in, true), tOut = finTangent(body, out, false);
    const bool tangent = dot(tIn, tOut) >= 1.0 - kSmooth;
    const std::size_t own = body.fin(in).edge == body.fin(out).edge ? 1 : 2;
    if (at.size() == own) {
        if (!tangent) throw std::domain_error("blendEdges: angolo vivo tra i raccordi senza spigolo tra i fianchi (non gestito)");
        if (bIn != bOut) throw std::domain_error("blendEdges: vertice della catena non gestito");
        return Joint::Smooth;
    }
    if (at.size() != own + 1) throw std::domain_error("blendEdges: vertice della catena con piu' di tre spigoli (non gestito)");
    for (EdgeId e : at)
        if (e != body.fin(in).edge && e != body.fin(out).edge) third = e;
    const Edge &c = body.edge(third);
    if (!c.curve || body.isLaminar(third)) throw std::domain_error("blendEdges: vertice della catena non gestito");
    const FaceId f0 = body.finFace(c.forward), f1 = body.finFace(c.backward);
    if (!((f0 == bIn && f1 == bOut) || (f0 == bOut && f1 == bIn)))
        throw std::domain_error("blendEdges: lo spigolo nel vertice della catena non separa i fianchi dei raccordi");
    const Vec3 p = body.vertex(v).point;
    if (tangent) {
        if (dot(faceNormal(body, bIn, p), faceNormal(body, bOut, p)) < 1.0 - 1e-6)
            throw std::domain_error("blendEdges: angolo vivo tra i fianchi in un vertice liscio della catena (non gestito)");
        return Joint::Split;
    }
    const Vec3 nA = faceNormal(body, body.finFace(in), p);
    // La faccia A sta a sinistra delle fin: una svolta a sinistra e' un angolo convesso di A.
    if (dot(cross(tIn, tOut), nA) <= 0.0) throw std::domain_error("blendEdges: angoli vivi concavi della catena non gestiti");
    return Joint::Mitre;
}

// Continuazione tangente su un'altra coppia di facce nel vertice con quattro
// spigoli alla fine (forward) o all'inizio della fin f: la fin di A' che
// continua f e gli spigoli tra A e A' e tra B e B' (tangenti). Non valida se non c'e'.
FinId crossContinuation(const Body &body, FinId f, bool forward, EdgeId &jA, EdgeId &jB, const std::set<int> &selected) {
    const VertexId v = forward ? body.finEnd(f) : body.finStart(f);
    const std::vector<EdgeId> at = edgesAtVertex(body, v);
    if (at.size() != 4) return FinId();
    const FinId junction = forward ? body.fin(f).next : body.fin(f).previous;
    const FinId twin = body.otherFin(junction);
    if (!twin.valid()) return FinId();
    const FinId g = forward ? body.fin(twin).next : body.fin(twin).previous;
    if (g == f || body.fin(g).edge == body.fin(f).edge || !body.otherFin(g).valid()) return FinId();
    const Vec3 tf = finTangent(body, f, forward), tg = finTangent(body, g, !forward);
    const bool explicitlySelected = selected.count(body.fin(g).edge.index);
    if (dot(tf, tg) < 1.0 - (explicitlySelected ? kCrossSmooth : kSmooth)) return FinId();
    const Vec3 p = body.vertex(v).point;
    const FaceId a = body.finFace(f), a2 = body.finFace(g), b = body.finFace(body.otherFin(f)), b2 = body.finFace(body.otherFin(g));
    if (a == a2 || b == b2) return FinId();
    jA = body.fin(junction).edge;
    jB = EdgeId();
    for (EdgeId e : at)
        if (e != jA && e != body.fin(f).edge && e != body.fin(g).edge) jB = e;
    if (!jB.valid()) return FinId();
    const Edge &eb = body.edge(jB);
    const FaceId f0 = body.finFace(eb.forward), f1 = body.finFace(eb.backward);
    if (!((f0 == b && f1 == b2) || (f0 == b2 && f1 == b))) return FinId();
    const double smoothA = dot(faceNormal(body, a, p), faceNormal(body, a2, p));
    const double smoothB = dot(faceNormal(body, b, p), faceNormal(body, b2, p));
    const double normalTolerance = explicitlySelected ? kCrossSmooth : 1e-9;
    if (smoothA < 1.0 - normalTolerance || smoothB < 1.0 - normalTolerance) return FinId();
    return g;
}

// Passaggio tangente con B comune e A divisa: percorri il loop opposto
// al contrario, mantenendo A a sinistra e B a destra della catena.
FinId splitAContinuation(const Body &body, FinId f, bool forward, EdgeId &third) {
    const VertexId v = forward ? body.finEnd(f) : body.finStart(f);
    if (edgesAtVertex(body, v).size() != 3) return FinId();
    const FinId twin = body.otherFin(f);
    if (!twin.valid()) return FinId();
    const FinId alongB = forward ? body.fin(twin).previous : body.fin(twin).next;
    const FinId g = body.otherFin(alongB);
    if (!g.valid() || g == f || body.finFace(g) == body.finFace(f)) return FinId();
    const Vec3 tf = finTangent(body, f, forward), tg = finTangent(body, g, !forward);
    if (dot(tf, tg) < 1.0 - kSmooth) return FinId();
    // Solo cambi di supporto lisci; gli angoli vivi restano al percorso mitra.
    try {
        const Joint j = forward ? classifyJoint(body, alongB, twin, third)
                                : classifyJoint(body, twin, alongB, third);
        return j == Joint::Split ? g : FinId();
    } catch (const std::domain_error &) { return FinId(); }
}

// La catena lungo il loop della faccia della fin `seed` (A), con le fin i cui edge sono scelti.
LoopChain chainAlong(const Body &body, FinId seed, const std::set<int> &selected) {
    LoopChain chain;
    chain.a = body.finFace(seed);
    if (body.finFace(body.otherFin(seed)) == chain.a) throw std::domain_error("blendEdges: spigolo interno a una faccia");
    std::vector<FinId> fins{seed};
    std::vector<Joint> joints;
    std::vector<EdgeId> thirds, fourths;
    const auto continues = [&](FinId from, FinId to, bool forward) {
        // Scelto, o tangente (propagazione: un raccordo non puo' finire in un vertice liscio).
        if (!body.otherFin(to).valid()) return false;
        if (selected.count(body.fin(to).edge.index)) return true;
        const Vec3 a = forward ? finTangent(body, from, true) : finTangent(body, to, true);
        const Vec3 b = forward ? finTangent(body, to, false) : finTangent(body, from, false);
        return dot(a, b) >= 1.0 - kSmooth && body.finFace(body.otherFin(to)) != body.finFace(to);
    };
    FinId f = seed;
    while (true) {
        const FinId next = body.fin(f).next;
        EdgeId third, fourth;
        FinId g;
        Joint j = Joint::Smooth;
        if (continues(f, next, true)) {
            g = next;
            j = classifyJoint(body, f, next, third);
        } else {
            g = splitAContinuation(body, f, true, third);
            j = Joint::SplitA;
            if (!g.valid()) {
                g = crossContinuation(body, f, true, third, fourth, selected);
                if (!g.valid()) break;
                j = Joint::Cross;
            }
        }
        joints.push_back(j);
        thirds.push_back(third);
        fourths.push_back(fourth);
        if (g == seed) {
            chain.closed = true;
            break;
        }
        if (std::find(fins.begin(), fins.end(), g) != fins.end()) throw std::domain_error("blendEdges: catena dei raccordi che si richiude male");
        fins.push_back(g);
        f = g;
    }
    if (!chain.closed) {
        f = seed;
        while (true) {
            const FinId previous = body.fin(f).previous;
            EdgeId third, fourth;
            FinId g;
            Joint j = Joint::Smooth;
            if (previous != fins.back() && continues(f, previous, false)) {
                g = previous;
                j = classifyJoint(body, previous, f, third);
            } else {
                g = splitAContinuation(body, f, false, third);
                j = Joint::SplitA;
                if (!g.valid()) {
                    g = crossContinuation(body, f, false, third, fourth, selected);
                    j = Joint::Cross;
                }
                if (!g.valid() || g == fins.back()) break;
            }
            if (std::find(fins.begin(), fins.end(), g) != fins.end()) break;
            fins.insert(fins.begin(), g);
            joints.insert(joints.begin(), j);
            thirds.insert(thirds.begin(), third);
            fourths.insert(fourths.begin(), fourth);
            f = g;
        }
    }
    for (FinId g : fins) chain.fins.push_back(chainFin(body, g));
    chain.joints = joints;
    chain.third = thirds;
    chain.fourth = fourths;
    return chain;
}

// Catena dello spigolo `seed`: dalla parte di una delle sue facce (la piu' lunga; a parita' la faccia piana).
LoopChain chainOf(const Body &body, EdgeId seed, const std::set<int> &selected) {
    const Edge &edge = body.edge(seed);
    if (!edge.curve || body.isLaminar(seed)) throw std::domain_error("blendEdges: spigolo non valido");
    std::string failure;
    LoopChain best;
    bool found = false;
    int bestSelected = -1;
    for (FinId f : {edge.forward, edge.backward}) {
        try {
            LoopChain chain = chainAlong(body, f, selected);
            int selectedCount = 0;
            for (const ChainFin &fin : chain.fins)
                if (selected.count(fin.edge.index)) ++selectedCount;
            const bool planar = body.face(chain.a).surface->type() == SurfaceType::Plane;
            const bool bestPlanar = found && body.face(best.a).surface->type() == SurfaceType::Plane;
            if (!found || selectedCount > bestSelected
                || (selectedCount == bestSelected && chain.fins.size() > best.fins.size())
                || (selectedCount == bestSelected && chain.fins.size() == best.fins.size() && planar && !bestPlanar)) {
                best = std::move(chain);
                found = true;
                bestSelected = selectedCount;
            }
        } catch (const std::domain_error &e) {
            failure = e.what();
        }
    }
    if (!found) throw std::domain_error(failure);
    return best;
}

// Parametri (u, v) del punto sulla superficie (vicino alla stima, se c'e').
Vec2 surfaceUV(const Surface &surface, const Vec3 &p, double scale) {
    const SurfaceProjection projection = projectPoint(surface, p);
    Vec2 uv(projection.u, projection.v);
    invertPoint(surface, p, uv, 1e-6 * scale, scale);
    return uv;
}

// Raccordo lungo un tratto di edge con i due supporti dati (pezzo della catena).
struct Piece {
    int chainFin = -1;
    FaceId a, b;
    double from = 0.0, to = 0.0;  // parametri dell'edge nel verso della catena
    std::unique_ptr<SectionSolver> solver;
    std::unique_ptr<EdgeBlend> blend;
    RowFit fit;
    std::shared_ptr<BSplineSurface> surface;
    double fitLo = 0.0, fitHi = 0.0;  // tratto della superficie (puo' andare oltre l'edge)
    bool startOnA = false;            // SplitA: attraversamento sul primo supporto
    EdgeId startOnC;                  // il pezzo comincia dove il contatto attraversa c
    VertexId startVertex;             // il vertice del giunto (c vi si accorcia)
    int contactA = -1, contactB = -1, face = -1;
    Interval rangeOf() const { return {std::min(from, to), std::max(from, to)}; }
};

// Estremo (o giunto) della catena: i punti e i bordi che vi chiudono i pezzi.
struct Junction {
    int pointA = -1, pointB = -1;  // contatti su A e su B nella sezione del giunto (Mitre: pointA = punto comune in A, pointB = X)
    int connector = -1;            // arco della sezione (Smooth, Split, estremo normale) o curva di taglio (estremo)
    int gamma = -1, delta = -1;    // Mitre: intersezione dei raccordi, taglio del raccordo profondo con l'altro fianco
    int deepPoint = -1;            // Mitre: punto su c dove finisce il contatto del raccordo profondo
    int deep = -1;                 // Mitre: 0 il pezzo prima, 1 quello dopo, -1 simmetrico
    double toleranceA = 0.0, toleranceB = 0.0;  // giunto Cross attraverso curve di trim approssimate
};

}  // namespace

std::vector<EdgeId> surfaceChainRuns(const Body &body, const std::vector<EdgeId> &selected, const std::vector<EdgeId> &seeds) {
    std::set<int> chosen, result;
    for (EdgeId e : selected) chosen.insert(e.index);
    for (EdgeId seed : seeds) {
        const LoopChain chain = chainOf(body, seed, chosen);
        for (const ChainFin &f : chain.fins) result.insert(f.edge.index);
    }
    std::vector<EdgeId> edges;
    for (int index : result) edges.push_back(EdgeId(index));
    return edges;
}

std::vector<std::vector<EdgeId>> surfaceChainGroups(const Body &body, const std::vector<EdgeId> &selected, bool &touching) {
    std::set<int> chosen, pending, usedVertices;
    for (EdgeId e : selected) chosen.insert(e.index), pending.insert(e.index);
    std::vector<std::vector<EdgeId>> groups;
    touching = false;
    while (!pending.empty()) {
        const LoopChain chain = chainOf(body, EdgeId(*pending.begin()), chosen);
        std::set<int> run;
        for (const ChainFin &fin : chain.fins) run.insert(fin.edge.index);
        std::vector<EdgeId> group;
        for (EdgeId e : selected)
            if (pending.count(e.index) && run.count(e.index)) {
                group.push_back(e);
                pending.erase(e.index);
            }
        if (group.empty()) throw std::logic_error("blendEdges: catena senza spigoli scelti");
        std::set<int> vertices;
        for (const ChainFin &fin : chain.fins) {
            vertices.insert(body.finStart(fin.fin).index);
            vertices.insert(body.finEnd(fin.fin).index);
        }
        for (int vertex : vertices)
            if (!usedVertices.insert(vertex).second) touching = true;
        groups.push_back(std::move(group));
    }
    return groups;
}

namespace {

// Curva d'intersezione tra `a` e `b` da `from` a `to` (punti comuni), dentro il box: curva, tratto, scarto.
struct Cut {
    CurvePtr<3> curve;
    Interval range;
    bool forward = true;  // dal punto `from` a `to` il parametro cresce
    double gap = 0.0;
    Interval sV{0.0, 0.0};  // traceBetween: valori di v su S toccati dai nodi
};

// Curva comune ai raccordi S (t, v) e T (s, w) in un angolo a mitra, dal punto
// in A (v = w = 0: i parametri t0, s0) al punto X (tX, vX, sX, wX). Sui punti
// della curva v + w = g, da 0 a vX + wX: Newton 4 x 4 su (t, v, s, w) con la
// continuazione in g (predittore dalle derivate), poi cubiche di Hermite a
// tratti in g raffinate finche' si scostano meno di kFitTolerance dai punti
// veri. Vicino al punto in A i raccordi sono tangenti tra loro (entrambi
// tangenti ad A) e il sistema degenera: l'ultimo tratto va fino al punto
// esatto e il suo scarto (misurato) diventa la tolleranza della curva.
Cut traceMitre(const Surface &S, const Surface &T, double t0, double s0, const double X[4], double scale, double fitTolerance) {
    // Fra raccordi quasi tangenti il sistema d'intersezione e' mal
    // condizionato. Prima verifica una curva comune entro la risoluzione
    // geometrica, senza aumentare le tolleranze delle facce di appoggio.
    const auto pointOn = [&](const Surface &surface, double start, double end, double v, double f) {
        return surface.point(start + f * (end - start), f * v);
    };
    bool coincident = true;
    for (int i = 0; i <= 64 && coincident; ++i) {
        const double f = i / 64.0;
        const Vec3 a = pointOn(S, t0, X[0], X[1], f), b = pointOn(T, s0, X[2], X[3], f);
        coincident = isFinite(a) && isFinite(b) && distance(a, b) <= kLinearResolution;
        if (coincident) {
            try {
                const Vec3 na = S.normal(t0 + f * (X[0] - t0), f * X[1]);
                const Vec3 nb = T.normal(s0 + f * (X[2] - s0), f * X[3]);
                coincident = norm(cross(na, nb)) <= 1e-5;
            } catch (const std::domain_error &) { coincident = false; }
        }
    }
    if (coincident) {
        try {
            const auto curve = fitCurve([&](double f) {
                return 0.5 * (pointOn(S, t0, X[0], X[1], f) + pointOn(T, s0, X[2], X[3], f));
            }, {0.0, 1.0}, {}, kFitTolerance);
            double gap = 0.0;
            for (int i = 0; i <= 128; ++i) {
                const double f = i / 128.0;
                const Vec3 p = curve->point(f);
                if (!isFinite(p)) { gap = std::numeric_limits<double>::infinity(); break; }
                gap = std::max({gap, distance(p, pointOn(S, t0, X[0], X[1], f)),
                                   distance(p, pointOn(T, s0, X[2], X[3], f))});
            }
            if (gap <= kLinearResolution) {
                Cut cut;
                cut.curve = curve;
                cut.range = {0.0, 1.0};
                cut.forward = true;
                cut.gap = gap;
                return cut;
            }
        } catch (const std::domain_error &) {
            // La stima non e' approssimabile: usa la continuazione esatta.
        }
    }
    struct Node {
        double g = 0.0, x[4] = {0, 0, 0, 0}, dx[4] = {0, 0, 0, 0};
        Vec3 p, dp;
    };
    const auto solve = [&](double g, Node &n) {
        for (int iteration = 0; iteration < 50; ++iteration) {
            Vec3 a[4], b[4];
            S.evaluate(n.x[0], n.x[1], 1, a);
            T.evaluate(n.x[2], n.x[3], 1, b);
            const Vec3 f = a[0] - b[0];
            const double f4 = n.x[1] + n.x[3] - g;
            const Vec3 c0 = a[Surface::derivativeIndex(1, 0, 1)], c1 = a[Surface::derivativeIndex(0, 1, 1)];
            const Vec3 c2 = -b[Surface::derivativeIndex(1, 0, 1)], c3 = -b[Surface::derivativeIndex(0, 1, 1)];
            double J[4][4] = {{c0.x(), c1.x(), c2.x(), c3.x()}, {c0.y(), c1.y(), c2.y(), c3.y()}, {c0.z(), c1.z(), c2.z(), c3.z()}, {0, 1, 0, 1}};
            double r[4] = {-f.x(), -f.y(), -f.z(), -f4};
            const bool done = norm(f) < 1e-13 * scale && std::fabs(f4) < 1e-15;
            if (!solveLinear<4>(J, r)) return false;
            for (int k = 0; k < 4; ++k) n.x[k] += r[k];
            if (done) break;
        }
        Vec3 a[4], b[4];
        S.evaluate(n.x[0], n.x[1], 1, a);
        T.evaluate(n.x[2], n.x[3], 1, b);
        if (distance(a[0], b[0]) > 1e-10 * scale) return false;
        const Vec3 c0 = a[Surface::derivativeIndex(1, 0, 1)], c1 = a[Surface::derivativeIndex(0, 1, 1)];
        const Vec3 c2 = -b[Surface::derivativeIndex(1, 0, 1)], c3 = -b[Surface::derivativeIndex(0, 1, 1)];
        double J[4][4] = {{c0.x(), c1.x(), c2.x(), c3.x()}, {c0.y(), c1.y(), c2.y(), c3.y()}, {c0.z(), c1.z(), c2.z(), c3.z()}, {0, 1, 0, 1}};
        double r[4] = {0, 0, 0, 1};
        if (!solveLinear<4>(J, r)) return false;
        n.g = g;
        std::copy(r, r + 4, n.dx);
        n.p = 0.5 * (a[0] + b[0]);
        n.dp = c0 * r[0] + c1 * r[1];
        return true;
    };
    const double gX = X[1] + X[3];
    if (!(gX > 0.0)) throw std::domain_error("blendEdges: angolo a mitra degenere");
    // Nodo in g da quello vicino (predittore lineare), a passi dimezzati se Newton non converge.
    const auto nodeAt = [&](double g, const Node &from, Node &out) {
        Node current = from;
        double step = g - from.g;
        while (true) {
            const bool last = std::fabs(g - current.g) <= std::fabs(step) * (1.0 + 1e-12);
            const double target = last ? g : current.g + step;
            Node trial = current;
            for (int k = 0; k < 4; ++k) trial.x[k] = current.x[k] + current.dx[k] * (target - current.g);
            if (solve(target, trial)) {
                current = trial;
                if (last) {
                    out = current;
                    return true;
                }
                continue;
            }
            step *= 0.5;
            if (std::fabs(step) < 1e-12 * gX) return false;
        }
    };
    Node top;
    std::copy(X, X + 4, top.x);
    if (!solve(gX, top)) throw std::domain_error("blendEdges: punto dei raccordi sui fianchi non trovato nell'angolo");
    double gMin = 1e-4 * gX;
    Node low;
    bool haveLow = false;
    // In prossimita' di A le due superfici di raccordo sono tangenti e il
    // sistema 4x4 diventa singolare. Sui loft guidati la zona numericamente
    // singolare puo' essere piu' ampia del valore fisso usato in precedenza;
    // arretra il primo nodo e lascia all'ultimo tratto tollerante la parte
    // tangente, gia' delimitata dai due punti esatti.
    for (double fraction : {1e-4, 3e-4, 1e-3, 3e-3, 1e-2, 3e-2, 1e-1}) {
        gMin = fraction * gX;
        if (nodeAt(gMin, top, low)) {
            haveLow = true;
            break;
        }
    }
    if (!haveLow) throw std::domain_error("blendEdges: curva dei raccordi nell'angolo non tracciata vicino alla faccia comune");
    std::map<double, Node> nodes{{gX, top}};
    nodes[gMin] = low;
    const auto hermite = [](const Node &a, const Node &b, double f) {
        const double h = b.g - a.g;
        const double h00 = (1 + 2 * f) * (1 - f) * (1 - f), h10 = f * (1 - f) * (1 - f), h01 = f * f * (3 - 2 * f), h11 = f * f * (f - 1);
        return h00 * a.p + (h10 * h) * a.dp + h01 * b.p + (h11 * h) * b.dp;
    };
    // Tratti tra gMin e gX: suddivisi finche' la cubica sta sulla curva vera.
    std::vector<std::pair<double, double>> pending{{gMin, gX}};
    std::vector<double> breaks;
    double approximationGap = 0.0;
    int guard = 0;
    while (!pending.empty()) {
        // La soglia minima 1e-4 consente fino a 2^14 foglie: il limite
        // deve contenere anche tutti i nodi interni dell'albero di raffinamento.
        if (++guard > 32767) throw std::domain_error("blendEdges: curva dei raccordi nell'angolo troppo complessa");
        const auto [ga, gb] = pending.back();
        pending.pop_back();
        const Node &a = nodes.at(ga), &b = nodes.at(gb);
        double error = 0.0;
        bool ok = true;
        for (double f : {0.25, 0.5, 0.75}) {
            Node exact;
            if (!nodeAt(ga + f * (gb - ga), a, exact)) {
                ok = false;
                break;
            }
            error = std::max(error, distance(hermite(a, b, f), exact.p));
        }
        // Vicino alle singolarita' dei fianchi di un loft Newton puo' non
        // risolvere uno dei campioni pur avendo estremi validi. Non suddividere
        // indefinitamente un intervallo ormai trascurabile.
        if ((!ok || error > fitTolerance) && gb - ga > 1e-4 * gX) {
            const double gm = 0.5 * (ga + gb);
            Node mid;
            // La continuazione puo' attraversare una parametrizzazione quasi
            // singolare in un solo verso. Il nodo superiore e' altrettanto
            // valido come predittore e spesso resta meglio condizionato.
            if (!nodeAt(gm, a, mid) && !nodeAt(gm, b, mid))
                throw std::domain_error("blendEdges: curva dei raccordi nell'angolo non tracciata nel tratto interno");
            nodes[gm] = mid;
            pending.push_back({gm, gb});
            pending.push_back({ga, gm});
            continue;
        }
        if (!ok || error > fitTolerance) {
            for (double f : {0.25, 0.5, 0.75}) {
                const Vec3 p = hermite(a, b, f);
                approximationGap = std::max({approximationGap, projectPoint(S, p).distance, projectPoint(T, p).distance});
            }
        }
    }
    // L'ultimo tratto fino al punto in A: stessa tangente del nodo gMin (la curva vi e' liscia).
    Node start;
    start.g = 0.0;
    start.x[0] = t0;
    start.x[2] = s0;
    start.p = 0.5 * (S.point(t0, 0.0) + T.point(s0, 0.0));
    start.dp = low.dp;
    double gap = 0.0;
    for (double f : {0.5, 0.9}) {
        Node exact;
        if (nodeAt(f * gMin, low, exact)) gap = std::max(gap, distance(hermite(start, low, f), exact.p));
    }
    nodes[0.0] = start;
    // B-spline cubica: nodi tripli nei punti di rottura.
    std::vector<double> knots;
    std::vector<Vec3> poles;
    std::vector<double> gs;
    for (const auto &[g, node] : nodes) gs.push_back(g);
    for (int k = 0; k < 4; ++k) knots.push_back(gs.front());
    poles.push_back(nodes.at(gs.front()).p);
    for (std::size_t k = 0; k + 1 < gs.size(); ++k) {
        const Node &a = nodes.at(gs[k]), &b = nodes.at(gs[k + 1]);
        const double h = b.g - a.g;
        poles.push_back(a.p + (h / 3.0) * a.dp);
        poles.push_back(b.p - (h / 3.0) * b.dp);
        poles.push_back(b.p);
        for (int j = 0; j < (k + 2 == gs.size() ? 4 : 3); ++j) knots.push_back(b.g);
    }
    Cut cut;
    cut.curve = std::make_shared<BSplineCurve<3>>(3, knots, poles);
    cut.range = {0.0, gX};
    cut.forward = true;
    cut.gap = std::max(gap, approximationGap);
    return cut;
}

// Curva comune alle superfici S e T tra i punti comuni `from` e `to`, con
// parametro s lungo la corda: sui punti (P - from) . D = s |D|^2, D = to - from.
// Newton 4 x 4 sui parametri delle due superfici con la continuazione in s,
// cubiche di Hermite a tratti raffinate a kFitTolerance. Funziona anche con le
// superfici che si incontrano sotto un angolo piccolo (dove il tracciamento
// generale si ferma), purche' la curva avanzi lungo la corda.
// Con `vRange` il parametro e' invece la v di S (v = lo + s (hi - lo)): la
// curva di chiusura di un raccordo va dal contatto su A (v = 0) a quello su B
// (v = 1) e attraversa una volta ogni arco della sezione, anche quando non
// avanza lungo la corda (facce d'estremita' quasi tangenti a una delle due
// facce: la curva e' una parabola che prima si allontana da `to`).
Cut traceBetween(const Surface &S, const Surface &T, const Vec3 &from, const Vec3 &to, double scale, double surfaceError, const Interval *vRange = nullptr,
                 bool stableTangencies = false) {
    const Vec3 D = to - from;
    const double L2 = dot(D, D);
    if (!(L2 > 0.0)) throw std::domain_error("blendEdges: curva di taglio degenere");
    const bool alongV = vRange != nullptr;
    const double v0 = alongV ? vRange->lo : 0.0, dv = alongV ? vRange->hi - vRange->lo : 0.0;
    if (alongV && !(std::fabs(dv) > 0.0)) throw std::domain_error("blendEdges: curva di taglio degenere");
    // Le superfici B-spline importate o ottenute da una loft portano un
    // errore di approssimazione proporzionale alle dimensioni del modello.
    // Pretendere sempre un nanometro assoluto rende il fit instabile nei
    // giunti tra pezze, dove Newton oscilla tra soluzioni equivalenti.
    const double fitTolerance = std::max(kFitTolerance, surfaceError);
    struct Node {
        double s = 0.0, y[4] = {0, 0, 0, 0}, dy[4] = {0, 0, 0, 0};
        Vec3 p, dp;
    };
    const auto solve = [&](double target, Node &n) {
        for (int iteration = 0; iteration < 50; ++iteration) {
            Vec3 a[4], b[4];
            S.evaluate(n.y[0], n.y[1], 1, a);
            T.evaluate(n.y[2], n.y[3], 1, b);
            const Vec3 f = a[0] - b[0];
            const double f4 = alongV ? n.y[1] - (v0 + target * dv) : dot(a[0] - from, D) - target * L2;
            const Vec3 c0 = a[Surface::derivativeIndex(1, 0, 1)], c1 = a[Surface::derivativeIndex(0, 1, 1)];
            const Vec3 c2 = -b[Surface::derivativeIndex(1, 0, 1)], c3 = -b[Surface::derivativeIndex(0, 1, 1)];
            double J[4][4] = {{c0.x(), c1.x(), c2.x(), c3.x()}, {c0.y(), c1.y(), c2.y(), c3.y()}, {c0.z(), c1.z(), c2.z(), c3.z()},
                              {alongV ? 0.0 : dot(c0, D), alongV ? 1.0 : dot(c1, D), 0, 0}};
            double r[4] = {-f.x(), -f.y(), -f.z(), -f4};
            const bool done = norm(f) < 1e-13 * scale
                && std::fabs(f4) < (alongV ? 1e-13 * std::fabs(dv) : 1e-13 * scale * std::sqrt(L2));
            if (stableTangencies && done) break;
            if (!solveLinear<4>(J, r)) return false;
            for (int k = 0; k < 4; ++k) n.y[k] += r[k];
            if (done) break;
        }
        Vec3 a[4], b[4];
        S.evaluate(n.y[0], n.y[1], 1, a);
        T.evaluate(n.y[2], n.y[3], 1, b);
        if (distance(a[0], b[0]) > 1e-10 * scale) return false;
        const Vec3 c0 = a[Surface::derivativeIndex(1, 0, 1)], c1 = a[Surface::derivativeIndex(0, 1, 1)];
        const Vec3 c2 = -b[Surface::derivativeIndex(1, 0, 1)], c3 = -b[Surface::derivativeIndex(0, 1, 1)];
        double J[4][4] = {{c0.x(), c1.x(), c2.x(), c3.x()}, {c0.y(), c1.y(), c2.y(), c3.y()}, {c0.z(), c1.z(), c2.z(), c3.z()},
                          {alongV ? 0.0 : dot(c0, D), alongV ? 1.0 : dot(c1, D), 0, 0}};
        double r[4] = {0, 0, 0, alongV ? dv : L2};
        if (!solveLinear<4>(J, r)) return false;
        n.s = target;
        std::copy(r, r + 4, n.dy);
        n.p = 0.5 * (a[0] + b[0]);
        n.dp = c0 * r[0] + c1 * r[1];
        return true;
    };
    const auto nodeAt = [&](double target, const Node &near, Node &out) {
        Node current = near;
        double step = target - near.s;
        while (true) {
            const bool last = std::fabs(target - current.s) <= std::fabs(step) * (1.0 + 1e-12);
            const double goal = last ? target : current.s + step;
            Node trial = current;
            for (int k = 0; k < 4; ++k) trial.y[k] = current.y[k] + current.dy[k] * (goal - current.s);
            if (solve(goal, trial)) {
                current = trial;
                if (last) {
                    out = current;
                    return true;
                }
                continue;
            }
            step *= 0.5;
            if (std::fabs(step) < 1e-12) return false;
        }
    };
    const auto seedNode = [&](const Vec3 &p, double target, Node &n) {
        const SurfaceProjection a = projectPoint(S, p), b = projectPoint(T, p);
        n.y[0] = a.u, n.y[1] = alongV ? v0 + target * dv : a.v, n.y[2] = b.u, n.y[3] = b.v;
        if (stableTangencies && (target == 0. || target == 1.)) {
            n.s = target;
            n.p = p;
            return distance(S.point(n.y[0], n.y[1]), p) <= std::max(kFitTolerance, surfaceError) &&
                   distance(T.point(n.y[2], n.y[3]), p) <= std::max(kFitTolerance, surfaceError);
        }
        return solve(target, n);
    };
    Node first, last;
    if (!seedNode(from, 0.0, first) || !seedNode(to, 1.0, last)) throw std::domain_error("blendEdges: estremi della curva di taglio non sulle due superfici");
    // Agli estremi tangenti il punto e' gia' noto dai bordi terminali:
    // non spostarlo con un ultimo passo Newton mal condizionato. Derivate
    // unilaterali dall'interno, poi verifica geometrica dei tratti degeneri.
    if (stableTangencies) {
        for (bool start : {true, false}) {
            Node near;
            const double target = start ? 1e-3 : 1. - 1e-3;
            for (int k = 0; k < 4; ++k) near.y[k] = first.y[k] + target * (last.y[k] - first.y[k]);
            if (!solve(target, near)) throw std::domain_error("blendEdges: continuazione unilaterale della chiusura tangente non trovata");
            Node &end = start ? first : last;
            std::copy(near.dy, near.dy + 4, end.dy);
            end.dp = near.dp;
        }
    }
    const auto nodeBetween = [&](double target, const Node &a, const Node &b, Node &out) {
        if (nodeAt(target, a, out) || nodeAt(target, b, out)) return true;
        if (stableTangencies) {
            const double f = (target - a.s) / (b.s - a.s);
            Node trial;
            for (int k = 0; k < 4; ++k) trial.y[k] = a.y[k] + f * (b.y[k] - a.y[k]);
            if (solve(target, trial)) {
                out = trial;
                return true;
            }
        }
        return false;
    };
    std::map<double, Node> nodes{{0.0, first}, {1.0, last}};
    const auto hermite = [](const Node &a, const Node &b, double f) {
        const double h = b.s - a.s;
        const double h00 = (1 + 2 * f) * (1 - f) * (1 - f), h10 = f * (1 - f) * (1 - f), h01 = f * f * (3 - 2 * f), h11 = f * f * (f - 1);
        return h00 * a.p + (h10 * h) * a.dp + h01 * b.p + (h11 * h) * b.dp;
    };
    std::vector<std::pair<double, double>> pending{{0.0, 0.5}, {0.5, 1.0}};
    {
        Node mid;
        if (!nodeBetween(0.5, first, last, mid)) throw std::domain_error("blendEdges: curva di taglio non tracciata");
        nodes[0.5] = mid;
    }
    int guard = 0;
    double validatedGap = stableTangencies ? std::max({distance(first.p, S.point(first.y[0], first.y[1])), distance(first.p, T.point(first.y[2], first.y[3])),
                                                       distance(last.p, S.point(last.y[0], last.y[1])), distance(last.p, T.point(last.y[2], last.y[3]))})
                                           : 0.0;
    while (!pending.empty()) {
        if (++guard > 20000) throw std::domain_error("blendEdges: curva di taglio troppo complessa");
        const auto [sa, sb] = pending.back();
        pending.pop_back();
        const Node &a = nodes.at(sa), &b = nodes.at(sb);
        double error = 0.0;
        bool ok = true;
        for (double f : {0.25, 0.5, 0.75}) {
            Node exact;
            if (!nodeBetween(sa + f * (sb - sa), a, b, exact)) {
                ok = false;
                break;
            }
            error = std::max(error, distance(hermite(a, b, f), exact.p));
        }
        // Vicino alla tangenza la posizione dei parametri puo' essere
        // rumorosa anche con un punto preciso. Accetta una piccola campata
        // solo se sta su entrambi gli appoggi a kFitTolerance e attraversa
        // le sezioni richieste: nessuna tolleranza geometrica viene allargata.
        if (stableTangencies && alongV && (!ok || error > fitTolerance) && sb - sa <= 1e-3) {
            double measured = 0.;
            for (double f : {0.125, 0.25, 0.5, 0.75, 0.875}) {
                const Vec3 p = hermite(a, b, f);
                const auto onS = projectPoint(S, p);
                const Vec3 sectionPoint = S.point(onS.u, v0 + (sa + f * (sb - sa)) * dv);
                measured = std::max({measured, distance(sectionPoint, p), projectPoint(T, p).distance});
            }
            if (measured <= fitTolerance) {
                validatedGap = std::max(validatedGap, measured);
                continue;
            }
        }
        if (!ok || (error > fitTolerance && sb - sa > 1e-9)) {
            const double sm = 0.5 * (sa + sb);
            Node mid;
            if (!nodeBetween(sm, a, b, mid)) throw std::domain_error("blendEdges: curva di taglio non tracciata");
            nodes[sm] = mid;
            pending.push_back({sm, sb});
            pending.push_back({sa, sm});
        }
    }
    std::vector<double> knots;
    std::vector<Vec3> poles;
    std::vector<double> ss;
    for (const auto &[key, node] : nodes) ss.push_back(key);
    for (int k = 0; k < 4; ++k) knots.push_back(0.0);
    poles.push_back(nodes.at(0.0).p);
    for (std::size_t k = 0; k + 1 < ss.size(); ++k) {
        const Node &a = nodes.at(ss[k]), &b = nodes.at(ss[k + 1]);
        const double h = b.s - a.s;
        poles.push_back(a.p + (h / 3.0) * a.dp);
        poles.push_back(b.p - (h / 3.0) * b.dp);
        poles.push_back(b.p);
        for (int j = 0; j < (k + 2 == ss.size() ? 4 : 3); ++j) knots.push_back(b.s);
    }
    Cut cut;
    cut.curve = std::make_shared<BSplineCurve<3>>(3, knots, poles);
    cut.range = {0.0, 1.0};
    cut.forward = true;
    cut.gap = std::max({validatedGap, distance(first.p, from), distance(last.p, to)});
    cut.sV = {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto &entry : nodes) {
        cut.sV.lo = std::min(cut.sV.lo, entry.second.y[1]);
        cut.sV.hi = std::max(cut.sV.hi, entry.second.y[1]);
    }
    return cut;
}

// Newton su S(t, v) = T(s, w) con v fisso: (t, s, w).
bool surfaceMeet(const Surface &S, double v, const Surface &T, double &t, double &s, double &w, double scale) {
    for (int iteration = 0; iteration < 60; ++iteration) {
        Vec3 a[4], b[4];
        S.evaluate(t, v, 1, a);
        T.evaluate(s, w, 1, b);
        const Vec3 f = a[0] - b[0];
        if (norm(f) < 1e-13 * scale) return true;
        const Vec3 c0 = a[Surface::derivativeIndex(1, 0, 1)], c1 = -b[Surface::derivativeIndex(1, 0, 1)], c2 = -b[Surface::derivativeIndex(0, 1, 1)];
        const double det = dot(c0, cross(c1, c2));
        if (!(std::fabs(det) > 0.0)) return false;
        t += -dot(f, cross(c1, c2)) / det;
        s += -dot(c0, cross(f, c2)) / det;
        w += -dot(c0, cross(c1, f)) / det;
    }
    Vec3 a = S.point(t, v), b = T.point(s, w);
    return distance(a, b) < 1e-11 * scale;
}

// Un estremo concavo puo' richiedere il prolungamento del bordo terminale.
// Per una B-spline di intersezione non basta estrapolare l'ultimo polinomio:
// il nuovo tratto deve restare su entrambe le superfici. Conserva il tratto
// originale e traccia solo la continuazione, in piani normali alla tangente
// nel vertice (parametro regolare anche quando il vecchio trim ha rumore).
// Una cucitura tangente non e' un'intersezione trasversa: il sistema di
// Newton sui due appoggi e' singolare. Se la SP-curve identifica un'iso con
// parametro affine, quella curva e' gia' definita esattamente dal supporto.
// Si cambia solo il parametro della NURBS; il tratto originale viene
// confrontato prima di usare la guida per la continuazione.
CurvePtr<3> boundaryIsoGuide(const Body &body, EdgeId id, const SurfacePtr (&supports)[2]) {
    const Edge &edge = body.edge(id);
    const FinId fins[2] = {edge.forward, edge.backward};
    for (int side = 0; side < 2; ++side) {
        if (supports[side]->type() != SurfaceType::BSpline) continue;
        const auto pc = body.fin(fins[side]).pcurve;
        const double middle = 0.5 * (edge.range.lo + edge.range.hi);
        const Vec2 uv = pc->point(middle), d = pc->derivative(middle);
        const int varying = std::fabs(d.x()) >= std::fabs(d.y()) ? 0 : 1;
        const int fixed = 1 - varying;
        const double slope = d[varying], offset = uv[varying] - slope * middle;
        if (!(std::fabs(slope) > 0.0)) continue;
        bool affine = true;
        for (int i = 0; i <= 32; ++i) {
            const double t = edge.range.lo + edge.range.length() * i / 32.0;
            const Vec2 p = pc->point(t);
            if (std::fabs(p[fixed] - uv[fixed]) > 1e-10 || std::fabs(p[varying] - (offset + slope * t)) > 1e-10) affine = false;
        }
        if (!affine) continue;
        const auto iso = varying == 0 ? supports[side]->vIso(uv.y()) : supports[side]->uIso(uv.x());
        const auto *spline = dynamic_cast<const BSplineCurve<3> *>(iso.get());
        if (!spline) continue;
        std::vector<double> knots = spline->knots(), weights = spline->weights();
        std::vector<Vec3> poles = spline->poles();
        for (double &k : knots) k = (k - offset) / slope;
        if (slope < 0.0) {
            std::reverse(knots.begin(), knots.end());
            std::reverse(poles.begin(), poles.end());
            std::reverse(weights.begin(), weights.end());
        }
        auto candidate = std::make_shared<BSplineCurve<3>>(spline->degree(), knots, poles, weights);
        bool agrees = true;
        for (int i = 0; i <= 32; ++i) {
            const double t = edge.range.lo + edge.range.length() * i / 32.0;
            if (distance(candidate->point(t), edge.curve->point(t)) > std::max(2 * kFitTolerance, 2 * edge.tolerance)) agrees = false;
        }
        if (agrees) return candidate;
    }
    return {};
}

void extendSplineBoundary(Body &body, Model &model, EdgeId id, const Interval &window, bool tangentSeams = false) {
    const Edge &edge = body.edge(id);
    const CurvePtr<3> original = edge.curve;
    const CurvePtr<3> predictor = sectionCurve(original, edge.range);
    const FinId fins[2] = {edge.forward, edge.backward};
    SurfacePtr supports[2];
    for (int side = 0; side < 2; ++side) {
        const Face &face = body.face(body.finFace(fins[side]));
        const auto pc = body.fin(fins[side]).pcurve;
        if (!pc) throw std::domain_error("blendEdges: SP-curve mancante nel prolungamento del bordo");
        supports[side] = face.surface;
        if (face.surface->type() != SurfaceType::BSpline) continue;
        const Interval oldU = face.surface->uDomain(), oldV = face.surface->vDomain();
        Interval u = oldU, v = oldV;
        for (int sample = 0; sample <= 32; ++sample) {
            const double t = window.lo + window.length() * sample / 32.0;
            const double end = std::clamp(t, edge.range.lo, edge.range.hi);
            const Vec2 uv = pc->point(end) + (t - end) * pc->derivative(end);
            u.lo = std::min(u.lo, uv.x()); u.hi = std::max(u.hi, uv.x());
            v.lo = std::min(v.lo, uv.y()); v.hi = std::max(v.hi, uv.y());
        }
        // Margine per il raffinamento dell'intersezione; le pezze originarie
        // restano identiche, si aggiungono soltanto campate all'esterno.
        if (u.lo < oldU.lo) u.lo -= 0.05 * oldU.length();
        if (u.hi > oldU.hi) u.hi += 0.05 * oldU.length();
        if (v.lo < oldV.lo) v.lo -= 0.05 * oldV.length();
        if (v.hi > oldV.hi) v.hi += 0.05 * oldV.length();
        if (u.lo != oldU.lo || u.hi != oldU.hi || v.lo != oldV.lo || v.hi != oldV.hi)
            supports[side] = std::make_shared<BSplineSurface>(
                extendBSplineSurface(static_cast<const BSplineSurface &>(*face.surface), u, v));
    }
    const CurvePtr<3> isoGuide = tangentSeams ? boundaryIsoGuide(body, id, supports) : CurvePtr<3>();
    double extensionDeviation = 0.0;
    const auto extension = [&](const Interval &range, bool head) -> CurvePtr<3> {
        if (isoGuide) {
            double worst = 0.0;
            const auto breaks = isoGuide->breakpoints(range);
            for (std::size_t k = 1; k < breaks.size(); ++k)
                for (double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                    const Vec3 p = isoGuide->point(breaks[k - 1] + fraction * (breaks[k] - breaks[k - 1]));
                    for (int side = 0; side < 2; ++side) worst = std::max(worst, projectPoint(*supports[side], p).distance);
                }
            if (worst > std::max(2 * kFitTolerance, edge.tolerance))
                throw std::domain_error("blendEdges: il prolungamento della cucitura tangente supera il supporto entro tolleranza");
            extensionDeviation = std::max(extensionDeviation, worst);
            return isoGuide;
        }
        const double endpoint = head ? edge.range.lo : edge.range.hi;
        Vec3 jet[2];
        if (head)
            predictor->evaluate(endpoint, 1, jet);
        else
            predictor->evaluateLeft(endpoint, 1, jet);
        const Vec3 origin = original->point(endpoint), tangent = normalized(jet[1]), velocity = jet[1];
        Vec2 uv[2][2];
        for (int side = 0; side < 2; ++side) {
            const auto pc = body.fin(fins[side]).pcurve;
            if (head)
                pc->evaluate(endpoint, 1, uv[side]);
            else
                pc->evaluateLeft(endpoint, 1, uv[side]);
        }
        const auto continuation = [&](double t) {
            const double dt = t - endpoint;
            const Vec3 anchor = origin + dt * velocity;
            const Vec2 pa = uv[0][0] + dt * uv[0][1], pb = uv[1][0] + dt * uv[1][1];
            double x[4] = {pa.x(), pa.y(), pb.x(), pb.y()};
            for (int iteration = 0; iteration < 30; ++iteration) {
                Vec3 a[4], b[4];
                supports[0]->evaluate(x[0], x[1], 1, a);
                supports[1]->evaluate(x[2], x[3], 1, b);
                const Vec3 f = a[0] - b[0];
                const double plane = dot(a[0] - anchor, tangent);
                if (norm(f) < 1e-11 && std::fabs(plane) < 1e-11) return 0.5 * (a[0] + b[0]);
                const Vec3 c[4] = {a[Surface::derivativeIndex(1, 0, 1)], a[Surface::derivativeIndex(0, 1, 1)],
                                   -b[Surface::derivativeIndex(1, 0, 1)], -b[Surface::derivativeIndex(0, 1, 1)]};
                double J[4][4] = {{c[0].x(), c[1].x(), c[2].x(), c[3].x()},
                                  {c[0].y(), c[1].y(), c[2].y(), c[3].y()},
                                  {c[0].z(), c[1].z(), c[2].z(), c[3].z()},
                                  {dot(c[0], tangent), dot(c[1], tangent), 0.0, 0.0}};
                double r[4] = {-f.x(), -f.y(), -f.z(), -plane};
                if (!solveLinear<4>(J, r)) break;
                for (int k = 0; k < 4; ++k) x[k] += r[k];
            }
            throw std::domain_error("blendEdges: prolungamento dell'intersezione non convergente");
        };
        return fitCurve(continuation, range, {}, kFitTolerance);
    };
    std::vector<CurvePtr<3>> curves;
    std::vector<Interval> ranges;
    if (window.lo < edge.range.lo) {
        ranges.push_back({window.lo, edge.range.lo});
        curves.push_back(extension(ranges.back(), true));
    }
    curves.push_back(original);
    ranges.push_back(edge.range);
    if (window.hi > edge.range.hi) {
        ranges.push_back({edge.range.hi, window.hi});
        curves.push_back(extension(ranges.back(), false));
    }
    int degree = 3;
    for (const auto &curve : curves) degree = std::max(degree, static_cast<const BSplineCurve<3> &>(*curve).degree());
    std::vector<BSplineCurve<3>> pieces;
    std::vector<double> breaks{window.lo};
    for (std::size_t k = 0; k < curves.size(); ++k)
        for (const auto &piece : standardBezierPieces(*curves[k], ranges[k], degree)) {
            pieces.push_back(piece);
            breaks.push_back(piece.domain().hi);
        }
    const auto expanded = std::make_shared<BSplineCurve<3>>(joinBezierPieces(pieces, breaks));
    // Commit solo dopo la costruzione: body e modello devono usare la stessa
    // curva e gli stessi appoggi. Le SP-curve vecchie non valgono nel tratto
    // aggiunto e vengono ricalcolate sul range finale dopo moveEnd.
    body.edge(id).curve = expanded;
    body.edge(id).tolerance = std::max(body.edge(id).tolerance, 2 * extensionDeviation);
    const int modelEdge = model.edgeIndex.at(id.index);
    model.edges[std::size_t(modelEdge)].curve = expanded;
    model.edges[std::size_t(modelEdge)].tolerance = body.edge(id).tolerance;
    for (int side = 0; side < 2; ++side) {
        const FaceId face = body.finFace(fins[side]);
        body.face(face).surface = supports[side];
        model.faces[std::size_t(model.faceIndex.at(face.index))].surface = supports[side];
    }
    for (auto &face : model.faces)
        for (auto &loop : face.loops)
            for (auto &fin : loop)
                if (fin.edge == modelEdge) { fin.pcurve.reset(); fin.pcurveTolerance = 0.0; }
}

// La finestra richiesta e' preventiva (5 volte il raggio), non il tratto
// effettivamente usato dalla chiusura. Un polo razionale o la fine del
// supporto lontano dal contatto non devono far fallire tutto il raccordo.
// Cerca la continuazione valida piu' lunga nella finestra, senza cambiare
// appoggi/tolleranze dopo un tentativo fallito. La chiusura finale deve
// comunque raggiungere il bordo costruito e superare tutti i controlli.
Interval extendBoundaryWindow(Body &body, Model &model, EdgeId id, const Interval &requested) {
    const Interval original = body.edge(id).range;
    const Body beforeBody = body;
    const Model beforeModel = model;
    Body bestBody = body;
    Model bestModel = model;
    Interval best = original;
    const auto tryFraction = [&](double fraction) {
        const Interval proposed{original.lo + fraction * (requested.lo - original.lo), original.hi + fraction * (requested.hi - original.hi)};
        Body trial = beforeBody;
        Model trialModel = beforeModel;
        try {
            extendSplineBoundary(trial, trialModel, id, proposed, true);
            bestBody = std::move(trial);
            bestModel = std::move(trialModel);
            best = proposed;
            return true;
        } catch (const std::domain_error &) {
            return false;
        }
    };
    if (!tryFraction(1.0)) {
        double low = 0.0, high = 1.0;
        for (int attempt = 1; attempt <= 16; ++attempt) {
            const double fraction = std::ldexp(1.0, -attempt);
            if (tryFraction(fraction)) {
                low = fraction;
                high = 2.0 * fraction;
                break;
            }
        }
        if (low == 0.0) return original;
        for (int attempt = 0; attempt < 12; ++attempt) {
            const double middle = 0.5 * (low + high);
            if (tryFraction(middle))
                low = middle;
            else
                high = middle;
        }
    }
    body = std::move(bestBody);
    model = std::move(bestModel);
    return best;
}

// Punto comune tra l'isoparametrica S(t, v) e una curva 3D. Risolve in
// minimi quadrati le tre coordinate rispetto ai due parametri; serve nei
// vertici dei loft, dove la continuazione della sezione a palla rotolante puo'
// fermarsi prima di attraversare numericamente il bordo della pezza.
bool surfaceCurveMeet(const Surface &surface, double v, const Curve<3> &curve, const Interval &range,
                      double &t, double &s, double scale) {
    for (int iteration = 0; iteration < 80; ++iteration) {
        Vec3 d[4];
        surface.evaluate(t, v, 1, d);
        const Vec3 q = curve.point(s), a = d[Surface::derivativeIndex(1, 0, 1)], b = -curve.derivative(s), f = d[0] - q;
        const double aa = dot(a, a), ab = dot(a, b), bb = dot(b, b), det = aa * bb - ab * ab;
        if (!(std::fabs(det) > 1e-30 * std::max(aa * bb, 1.0))) return false;
        const double ra = -dot(a, f), rb = -dot(b, f);
        const double dt = (ra * bb - ab * rb) / det, ds = (aa * rb - ab * ra) / det;
        t += dt;
        s += ds;
        if (std::fabs(dt) + std::fabs(ds) < 1e-14 * (1.0 + std::fabs(t) + std::fabs(s))) break;
    }
    const double margin = 1e-8 * (1.0 + range.length());
    return s >= range.lo - margin && s <= range.hi + margin
        && distance(surface.point(t, v), curve.point(s)) <= 1e-9 * scale;
}

}  // namespace

namespace {

// Controllo del contorno ricostruito dal percorso intrinseco: i fianchi
// possono essere regolari ma contatti lontani dello stesso coperchio possono
// incontrarsi. I box dei poli di Bezier scartano le coppie disgiunte; sulle
// altre si usa l'intersezione delle curve, non dei segmenti campionati.
void checkGuidedCap(const Body &body, FaceId cap, double scale) {
    const Frame3 &frame = static_cast<const Plane &>(*body.face(cap).surface).frame();
    const double tolerance = 1e-10 * scale;
    for (LoopId loop : body.face(cap).loops) {
        struct Span { BSplineCurve<2> curve; Box box; std::size_t order; };
        std::vector<Span> spans;
        const auto append = [&](const BSplineCurve<3> &piece) {
            std::vector<Vec2> poles;
            Box box;
            for (const Vec3 &p : piece.poles()) {
                const Vec3 delta = p - frame.origin();
                const Vec2 q(dot(delta, frame.xDir()), dot(delta, frame.yDir()));
                poles.push_back(q);
                box.add(Vec3(q.x(), q.y(), 0.0));
            }
            BSplineCurve<2> curve(piece.degree(), piece.knots(), poles, piece.weights());
            spans.push_back({std::move(curve), box, spans.size()});
        };
        for (FinId fin : body.loopFins(loop)) {
            const Edge &edge = body.edge(body.fin(fin).edge);
            auto pieces = rationalBezierPieces(*edge.curve, edge.range);
            if (!body.fin(fin).sense) std::reverse(pieces.begin(), pieces.end());
            for (const auto &piece : pieces) append(piece);
        }
        std::vector<std::size_t> sorted;
        for (std::size_t i = 0; i < spans.size(); ++i) sorted.push_back(i);
        std::sort(sorted.begin(), sorted.end(), [&](std::size_t a, std::size_t b) { return spans[a].box.lo.x() < spans[b].box.lo.x(); });
        for (std::size_t a = 0; a < sorted.size(); ++a) {
            cancellationPoint();
            const Span &p = spans[sorted[a]];
            for (std::size_t b = a + 1; b < sorted.size(); ++b) {
                const Span &q = spans[sorted[b]];
                if (q.box.lo.x() > p.box.hi.x() + tolerance) break;
                if (!p.box.padded(tolerance).overlaps(q.box.padded(tolerance))) continue;
                const std::size_t gap = p.order > q.order ? p.order - q.order : q.order - p.order;
                const bool adjacent = gap == 1 || gap + 1 == spans.size();
                if (adjacent) continue; // giunto comune, gia' verificato da regolarita' e checkBody
                const auto hits = intersectCurves(p.curve, p.curve.domain(), q.curve, q.curve.domain(), tolerance, true);
                if (!hits.points.empty())
                    throw std::domain_error("blendEdges: il contorno del coperchio si sovrappone dopo il raccordo");
                if (hits.overlap)
                    throw std::domain_error("blendEdges: tratti sovrapposti sul coperchio raccordato");
            }
        }
    }
}

Body blendSurfaceChainsWithGuide(const Body &input, const std::vector<EdgeId> &selected, double size, bool chamfer, const std::vector<ChamferSides> *sides,
                                 BodyHistory *history, bool useGuide, bool boundaryMode = false) {

    if (history) *history = BodyHistory();
    if (!(size > kLinearResolution)) throw std::domain_error("blendEdges: raggio o distanza non validi");
    Body body = input;
    std::map<int, int> edgeSources;
    std::vector<EdgeId> edges = selected;
    std::map<int, ChamferSides> sideOf;
    if (sides)
        for (std::size_t k = 0; k < selected.size() && k < sides->size(); ++k) sideOf[selected[k].index] = (*sides)[k];
    // Un edge chiuso da solo si divide in due: le facce dei raccordi sono pezze aperte.
    for (std::size_t k = 0, count = edges.size(); k < count; ++k) {
        const EdgeId e = edges[k];
        if (body.edgeStart(e) != body.edgeEnd(e)) continue;
        const Edge &edge = body.edge(e);
        const double middle = 0.5 * (edge.range.lo + edge.range.hi);
        const EdgeId half = body.semv(e, edge.curve->point(middle), middle).edge;
        if (history) edgeSources[half.index] = e.index;
        edges.push_back(half);
        if (sides) sideOf[half.index] = sideOf.at(e.index);
    }
    computePCurves(body);
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    const double scale = std::max(box.diagonal(), 1.0);

    std::set<int> pending;
    for (EdgeId e : edges) pending.insert(e.index);
    std::vector<LoopChain> chains;
    while (!pending.empty()) {
        LoopChain chain = chainOf(body, EdgeId(*pending.begin()), pending);
        for (const ChainFin &f : chain.fins) pending.erase(f.edge.index);
        chains.push_back(std::move(chain));
    }
    Model model(body, history ? &input : nullptr, edgeSources);
    // Modifiche delle facce esistenti: edge tolti, fin nuove (con il verso) e
    // edge nuovi senza verso; alla fine i loop si ricollegano per i vertici.
    struct FaceEdit {
        std::set<int> removed;
        std::vector<Body::BuildFin> fixed;
        std::vector<int> free;
    };
    std::map<int, FaceEdit> edits;
    std::vector<std::pair<int, FaceEdit>> newFaces;
    std::set<int> usedVertices;
    bool approximateCross = false;
    std::set<const Surface *> guidedCaps;

    for (const LoopChain &chain : chains) {
        const int n = int(chain.fins.size());
        // Convessita' dalla sezione a meta' della prima fin, con le facce viste come piani.
        const ChainFin &first = chain.fins.front();
        const Edge &firstEdge = body.edge(first.edge);
        const double tm = 0.5 * (firstEdge.range.lo + firstEdge.range.hi);
        const Vec3 pm = firstEdge.curve->point(tm);
        const Vec3 T = normalized(firstEdge.curve->derivative(tm));
        const Vec3 tA = first.sense ? T : -T;
        const Vec3 nA = faceNormal(body, first.a, pm), nB = faceNormal(body, first.b, pm);
        const Vec3 mA = normalized(cross(nA, tA)), mB = normalized(cross(nB, -tA));  // verso l'interno delle facce
        const double angle = std::acos(std::clamp(dot(mA, mB), -1.0, 1.0));
        if (angle > kPi - 1e-6) throw std::domain_error("blendEdges: spigolo liscio (facce tangenti), niente da raccordare");
        if (angle < 1e-6) throw std::domain_error("blendEdges: facce ripiegate sullo spigolo");
        const bool convex = dot(mB, nA) < 0.0;
        // Distanze dello smusso sulle facce A e B (uguali lungo la catena).
        double sizeOnA = size, sizeOnB = size;
        bool haveSizes = false;
        if (sides) {
            for (std::size_t k = 0; k < chain.fins.size(); ++k) {
                const EdgeId e = chain.fins[k].edge;
                if (!sideOf.count(e.index)) continue;  // spigolo aggiunto per tangenza: le distanze della catena
                const auto [onForward, onBackward] = detail::chamferDistances(body, e, sideOf.at(e.index));
                const bool forward = body.edge(e).forward == chain.fins[k].fin;
                const double a = forward ? onForward : onBackward, b = forward ? onBackward : onForward;
                if (!haveSizes) sizeOnA = a, sizeOnB = b, haveSizes = true;
                else if (std::fabs(a - sizeOnA) > 1e-12 * size || std::fabs(b - sizeOnB) > 1e-12 * size)
                    throw std::domain_error("blendEdges: spigoli consecutivi della catena con distanze diverse dello smusso");
            }
        }
        const double reachA = chamfer ? sizeOnA : size;

        // Un coperchio piano chiuso su fianchi tangenti ammette un'altra
        // parametrizzazione della stessa intersezione degli offset: seguire
        // una coordinata monotona del fianco, anziche' le normali al vecchio
        // bordo (che possono intersecarsi anche con offset regolari).
        const bool guideChain = useGuide && !chamfer && chain.closed
            && body.face(chain.a).surface->type() == SurfaceType::Plane
            && std::all_of(chain.joints.begin(), chain.joints.end(), [](Joint j) {
                return j == Joint::Smooth || j == Joint::Split;
            });

        if (guideChain) guidedCaps.insert(body.face(chain.a).surface.get());

        // Pezzi: uno per fin, divisi nei giunti Split.
        std::vector<Piece> pieces;
        const auto makePiece = [&](int f, FaceId b, double from, double to, FaceId a = FaceId()) {
            Piece piece;
            piece.chainFin = f;
            piece.a = a.valid() ? a : chain.fins[std::size_t(f)].a;
            piece.b = b;
            piece.from = from;
            piece.to = to;
            const Edge &edge = body.edge(chain.fins[std::size_t(f)].edge);
            const Face &faceB = body.face(b), &faceA = body.face(piece.a);
            const CurvePtr<3> curve = sectionCurve(edge.curve, edge.range);
            piece.solver = std::make_unique<SectionSolver>(
                curve, faceA.surface, faceA.sense, faceB.surface, faceB.sense, reachA, chamfer, convex, scale, chamfer ? sizeOnB : -1.0,
                guideChain && faceA.surface->type() == SurfaceType::Plane && faceB.surface->type() == SurfaceType::BSpline &&
                        piece.b == chain.fins[std::size_t(f)].b && piece.a == chain.fins[std::size_t(f)].a
                    ? body.fin(body.otherFin(chain.fins[std::size_t(f)].fin)).pcurve
                    : CurvePtr<2>(),
                0.5 * (edge.range.lo + edge.range.hi), edge.range, boundaryMode);
            piece.blend = std::make_unique<EdgeBlend>(*piece.solver, curve, piece.rangeOf());
            return piece;
        };
        // Seme di una sezione in t: continuazione nel raggio dalla stima piana.
        const auto seedAt = [&](Piece &piece, double t) {
            const Edge &edge = body.edge(chain.fins[std::size_t(piece.chainFin)].edge);
            const Vec3 p = edge.curve->point(t);
            const Face &faceB = body.face(piece.b), &faceA = body.face(piece.a);
            const Vec2 uvA = surfaceUV(*faceA.surface, p, scale), uvB = surfaceUV(*faceB.surface, p, scale);
            const FacePoint pa = facePoint(*faceA.surface, faceA.sense, uvA.x(), uvA.y()), pb = facePoint(*faceB.surface, faceB.sense, uvB.x(), uvB.y());
            const Vec3 tangent = normalized(edge.curve->derivative(t));
            const Vec3 ta = chain.fins[std::size_t(piece.chainFin)].sense ? tangent : -tangent;
            const Vec3 ma = normalized(cross(pa.n, ta)), mb = normalized(cross(pb.n, -ta));
            const double localAngle = std::acos(std::clamp(dot(ma, mb), -1.0, 1.0));
            const auto guess = [&](const FacePoint &q, const Vec3 &direction, double reach, double *uv, const Vec2 &at) {
                const Vec3 d = reach * direction;
                const double a11 = dot(q.su, q.su), a12 = dot(q.su, q.sv), a22 = dot(q.sv, q.sv);
                const double b1 = dot(q.su, d), b2 = dot(q.sv, d), det = a11 * a22 - a12 * a12;
                uv[0] = at.x() + (b1 * a22 - a12 * b2) / det;
                uv[1] = at.y() + (a11 * b2 - a12 * b1) / det;
            };
            Section seed;
            bool found = false;
            for (double fraction : {1.0 / 64.0, 1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0}) {
                const double r = fraction * reachA;
                Section trial = seed;
                bool ok = found && piece.solver->solve(t, false, trial, r);
                if (!ok) {
                    trial = Section();
                    const double reach = chamfer ? r : r / std::tan(0.5 * localAngle);
                    guess(pa, ma, reach, trial.x, uvA);
                    guess(pb, mb, chamfer ? r * sizeOnB / sizeOnA : reach, trial.x + 2, uvB);
                    ok = piece.solver->solve(t, false, trial, r);
                }
                if (ok) {
                    seed = trial;
                    found = true;
                } else if (fraction == 1.0 || found) {
                    throw std::domain_error("blendEdges: sezione del raccordo non trovata (raggio troppo grande?)");
                }
            }
            if (!found) throw std::domain_error("blendEdges: sezione del raccordo non trovata (raggio troppo grande?)");
            piece.blend->seed(seed);
        };
        // Seme dalla stessa palla su un'altra coppia di supporti (Split/SplitA).
        // `t`: il parametro del punto sull'edge del pezzo (nel vertice e' quello del suo edge, non dell'altro).
        const auto seedFrom = [&](Piece &piece, const Section &known, double t) {
            Section s = known;
            const Vec2 uvA = surfaceUV(*body.face(piece.a).surface, known.row[0].p, scale);
            s.x[0] = uvA.x();
            s.x[1] = uvA.y();
            const Vec2 uvB = surfaceUV(*body.face(piece.b).surface, known.row[2].p, scale);
            s.x[2] = uvB.x();
            s.x[3] = uvB.y();
            if (!piece.solver->solve(t, false, s)) throw std::domain_error("blendEdges: sezione del raccordo non trovata nel giunto della catena");
            piece.blend->seed(s);
        };

        // Il contatto (riga 0 su A, 2 su B) attraversa lo spigolo `boundary` della faccia `face`:
        // il parametro, da `inside` (contatto dentro la faccia) nel verso `direction` dell'edge.
        // Con `vertex` (estremi delle catene) si cerca l'attraversamento piu'
        // vicino al vertice: lungo uno spigolo che si avvolge attorno alla
        // faccia (un filetto su un cilindro) il contatto cambia lato rispetto
        // al bordo a ogni mezzo giro, e partendo da meta' spigolo si trovava
        // un attraversamento lontano, nel mezzo della catena.
        const auto crossing = [&](Piece &piece, int row, EdgeId boundary, FaceId face, double inside, double direction,
                                  bool extendBoundary = false, const Interval *endWindow = nullptr,
                                  double vertex = std::numeric_limits<double>::quiet_NaN()) {
            const Edge &g = body.edge(boundary);
            const FinId gFin = g.forward.valid() && body.finFace(g.forward) == face ? g.forward : g.backward;
            const bool gSense = body.fin(gFin).sense;
            const Edge &edge = body.edge(chain.fins[std::size_t(piece.chainFin)].edge);
            // Senza finestre speciali il bordo e' la cucitura intera (seamChain).
            const std::vector<EdgeId> seam = extendBoundary || endWindow ? std::vector<EdgeId>{boundary} : seamChain(body, boundary);
            const auto sideOf = [&](const Vec3 &p) {
                Interval window = g.range;
                if (extendBoundary && g.curve->type() == CurveType::Line) {
                    const double margin = 50.0 * size / norm(g.curve->derivative(0.5 * (g.range.lo + g.range.hi)));
                    window = {g.range.lo - margin, g.range.hi + margin};
                }
                if (endWindow) window = *endWindow;
                CurveProjection q = projectPoint(*g.curve, p, window);
                const Edge *on = &g;
                bool onSense = gSense;
                for (std::size_t k = 1; k < seam.size(); ++k) {
                    const Edge &other = body.edge(seam[k]);
                    const CurveProjection candidate = projectPoint(*other.curve, p, other.range);
                    if (candidate.distance < q.distance) {
                        q = candidate;
                        on = &other;
                        onSense = body.fin(body.finFace(other.forward) == face ? other.forward : other.backward).sense;
                    }
                }
                Vec3 tg = normalized(on->curve->derivative(q.parameter));
                if (!onSense) tg = -tg;
                const Vec3 m = cross(faceNormal(body, face, q.point), tg);  // verso l'interno della faccia
                return dot(p - q.point, m);
            };
            if (std::isfinite(vertex)) {
                // Il lato interno dallo spigolo stesso appena prima del vertice
                // (sta nella faccia), poi dal vertice verso l'interno fino al
                // primo contatto dentro la faccia.
                const double speed = norm(edge.curve->derivative(vertex));
                double reference = 0.0;
                for (double fraction : {1e-3, 1e-2, 1e-1}) {
                    reference = sideOf(edge.curve->point(vertex - direction * fraction * size / speed));
                    if (std::fabs(reference) > 1e-12 * scale) break;
                }
                if (std::fabs(reference) > 1e-12 * scale) {
                    const double sign = reference > 0.0 ? 1.0 : -1.0;
                    double t = vertex, step = 0.25 * size / speed;
                    while (true) {
                        if (direction * (inside - t) >= 0.0) break;  // oltre meta': si parte da li'
                        double value = 0.0;
                        try {
                            value = sign * sideOf(piece.blend->section(t).row[row].p);
                        } catch (const std::domain_error &) {
                            value = 0.0;
                        }
                        if (value > 1e-13 * scale) {
                            inside = t;
                            break;
                        }
                        t -= direction * step;
                        step = std::min(1.5 * step, 0.5 * size / speed);
                    }
                }
            }
            const auto rawSide = [&](double t) { return sideOf(piece.blend->section(t).row[row].p); };
            double t0 = inside;
            const double initialSide = rawSide(t0);
            if (std::fabs(initialSide) <= 1e-13 * scale)
                throw std::domain_error("blendEdges: il contatto del raccordo coincide con il bordo della faccia (raggio troppo grande?)");
            // Il verso topologico della fin e la parametrizzazione di una
            // pezza B-spline possono avere segni opposti. Il punto `inside`
            // e' verificato sulla faccia: il suo segno stabilisce una volta
            // per tutte quale semipiano e' l'interno.
            const double insideSign = initialSide > 0.0 ? 1.0 : -1.0;
            const auto side = [&](double t) { return insideSign * rawSide(t); };
            double g0 = side(t0);
            const double speed = norm(edge.curve->derivative(inside));
            // Passi al piu' di mezza misura (oltre il vertice le superfici si prolungano: non troppo lontano).
            const double largest = 0.5 * size / speed;
            double step = direction * 0.25 * size / speed, t1 = t0, g1 = g0, travelled = 0.0;
            while (true) {
                t1 = t0 + step;
                bool computed = true;
                try {
                    g1 = side(t1);
                } catch (const std::domain_error &) {
                    computed = false;
                }
                if (!computed) {
                    step *= 0.25;
                    if (std::fabs(step) * speed < 1e-9 * size) {
                        // Su un angolo di un loft i due contatti possono
                        // incontrarsi tra loro senza attraversare il bordo
                        // della pezza B-spline. L'ultima sezione valida basta
                        // per estendere le due superfici fino alla mitra; le
                        // intersezioni e il controllo finale del body
                        // stabiliscono poi se il raggio e' davvero possibile.
                        return direction > 0.0 ? edge.range.hi : edge.range.lo;
                    }
                    continue;
                }
                if (!(g1 > 0.0)) break;
                t0 = t1;
                g0 = g1;
                // Il limite vale oltre il vertice (le superfici prolungate): lungo
                // lo spigolo il percorso dipende dalla sua lunghezza, non dal raggio
                // (su un arco lungo con un raggio piccolo si fermava prima del vertice).
                if (direction > 0.0 ? t1 > edge.range.hi : t1 < edge.range.lo) travelled += std::fabs(step) * speed;
                if (travelled > 50.0 * size) {
                    return direction > 0.0 ? edge.range.hi : edge.range.lo;
                }
                step = direction * std::min(1.5 * std::fabs(step), largest);
            }
            // Regula falsi (Illinois) tra t0 (dentro) e t1 (fuori).
            int stale = 0;
            for (int iteration = 0; iteration < 200; ++iteration) {
                const double t = std::fabs(g0 - g1) > 0.0 ? t1 - g1 * (t1 - t0) / (g1 - g0) : 0.5 * (t0 + t1);
                const double gt = side(t);
                if (std::fabs(gt) < 1e-13 * scale || std::fabs(t1 - t0) < 1e-15 * (1.0 + std::fabs(t))) return t;
                if (gt > 0.0) {
                    t0 = t, g0 = gt;
                    if (stale == -1) g1 *= 0.5;
                    stale = -1;
                } else {
                    t1 = t, g1 = gt;
                    if (stale == 1) g0 *= 0.5;
                    stale = 1;
                }
            }
            return 0.5 * (t0 + t1);
        };

        for (int f = 0; f < n; ++f) {
            const ChainFin &cf = chain.fins[std::size_t(f)];
            pieces.push_back(makePiece(f, cf.b, cf.start, cf.end));
        }
        // La ricerca della prima sezione e' uno dei passi piu' costosi sui
        // raccordi tra superfici libere. I pezzi non condividono solver ne'
        // cache: si possono inizializzare in parallelo e si rilanciano gli
        // errori nell'ordine della catena.
        std::vector<std::exception_ptr> seedErrors(pieces.size());
        parallelFor(pieces.size(), threadCount(0), [&](std::size_t p) {
            try {
                const ChainFin &cf = chain.fins[std::size_t(pieces[p].chainFin)];
                seedAt(pieces[p], 0.5 * (cf.start + cf.end));
            } catch (...) {
                seedErrors[p] = std::current_exception();
            }
        });
        for (const std::exception_ptr &error : seedErrors)
            if (error) std::rethrow_exception(error);
        // Giunti Split/SplitA: il contatto sul supporto diviso attraversa c
        // prima o dopo il vertice; il pezzo di passaggio usa i due supporti reali.
        const int jointCount = chain.closed ? n : n - 1;
        std::vector<int> splitAt(static_cast<std::size_t>(jointCount), -1);  // indice del pezzo che comincia sul giunto di passaggio
        for (int j = jointCount - 1; j >= 0; --j) {
            const Joint joint = chain.joints[std::size_t(j)];
            if (joint != Joint::Split && joint != Joint::SplitA) continue;
            const bool onA = joint == Joint::SplitA;
            const int row = onA ? 0 : 2;
            const int i = j, k = (j + 1) % n;
            const ChainFin &fi = chain.fins[std::size_t(i)], &fk = chain.fins[std::size_t(k)];
            const EdgeId c = chain.third[std::size_t(j)];
            // Pezzi (nell'ordine attuale) delle fin i e k.
            int pi = -1;
            for (int q = 0; q < int(pieces.size()); ++q)
                if (pieces[std::size_t(q)].chainFin == i) pi = q;
            Piece &before = pieces[std::size_t(pi)];
            // Ogni coordinata intrinseca raggiunge la stessa cucitura con
            // un parametro proprio. Rifilare entrambi i pezzi sul contatto
            // reale evita i ponti costruiti sui vecchi piani di sezione.
            if (guideChain && !onA && before.a == fk.a) {
                int pk = -1;
                for (int r = 0; r < int(pieces.size()); ++r)
                    if (pieces[std::size_t(r)].chainFin == k && pk < 0) pk = r;
                Piece &rest = pieces[std::size_t(pk)];
                if (before.solver->intrinsicGuide() || rest.solver->intrinsicGuide()) {
                    const double dirI = fi.end > fi.start ? 1.0 : -1.0;
                    const double dirK = fk.end > fk.start ? 1.0 : -1.0;
                    const double end = crossing(before, 2, c, before.b, 0.5*(fi.start+fi.end), dirI);
                    const double start = crossing(rest, 2, c, rest.b, 0.5*(fk.start+fk.end), -dirK);
                    const Section &a = before.blend->section(end), &b = rest.blend->section(start);
                    if (std::max(distance(a.row[0].p, b.row[0].p), distance(a.row[2].p, b.row[2].p)) > 1e-9 * scale)
                        throw std::domain_error("blendEdges: i raccordi del coperchio non coincidono sulla cucitura del fianco");
                    before.to = end;
                    rest.from = start;
                    rest.startOnC = c;
                    rest.startVertex = body.finEnd(fi.fin);
                    continue;
                }
            }
            const Section &atVertex = before.blend->section(fi.end, fi.end == body.edge(fi.edge).range.hi);
            const Vec3 q = atVertex.row[row].p;
            const FaceId support = onA ? fi.a : fi.b;
            EdgeId nearestC = c;
            CurveProjection onC = projectPoint(*body.edge(c).curve, q, body.edge(c).range);
            for (EdgeId other : seamChain(body, c)) {
                const CurveProjection candidate = projectPoint(*body.edge(other).curve, q, body.edge(other).range);
                if (candidate.distance < onC.distance) onC = candidate, nearestC = other;
            }
            Vec3 tg = normalized(body.edge(nearestC).curve->derivative(onC.parameter));
            const FinId cFin = body.finFace(body.edge(nearestC).forward) == support ? body.edge(nearestC).forward : body.edge(nearestC).backward;
            if (!body.fin(cFin).sense) tg = -tg;
            const double sideAtVertex = dot(q - onC.point, cross(faceNormal(body, support, onC.point), tg));
            // Il contatto attraversa c proprio nel vertice (sezione per c): il passaggio e' il vertice.
            if (std::fabs(sideAtVertex) <= 1e-9 * scale) continue;
            const auto nearVertex = [&](const ChainFin &cf, double t, double atVertex) {
                return std::fabs(t - atVertex) * norm(body.edge(cf.edge).curve->derivative(atVertex)) <= 1e-7 * size;
            };
            if (sideAtVertex < 0.0) {
                // Il contatto e' gia' sulla faccia di k nel vertice: si passa sulla fin i.
                const double dir = fi.end > fi.start ? 1.0 : -1.0;
                const double t = crossing(before, row, c, support, 0.5 * (fi.start + fi.end), dir);
                if (nearVertex(fi, t, fi.end)) continue;
                Piece after = makePiece(i, fk.b, t, fi.end, fk.a);
                after.startOnA = onA;
                after.startOnC = c;
                after.startVertex = body.finEnd(fi.fin);
                seedFrom(after, before.blend->section(t), t);
                before.to = t;
                pieces.insert(pieces.begin() + pi + 1, std::move(after));
                splitAt[std::size_t(j)] = pi + 1;
            } else {
                // Sulla fin k, ancora con la faccia di i.
                int pk = -1;
                for (int r = 0; r < int(pieces.size()); ++r)
                    if (pieces[std::size_t(r)].chainFin == k && pk < 0) pk = r;
                Piece bridge = makePiece(k, fi.b, fk.start, fk.end, fi.a);
                seedFrom(bridge, atVertex, fk.start);
                // La sezione nel vertice sull'edge k: lo stesso punto, un'altra curva.
                const double dir = fk.end > fk.start ? 1.0 : -1.0;
                const double t = crossing(bridge, row, c, support, fk.start, dir);
                if (nearVertex(fk, t, fk.start)) continue;
                bridge.to = t;
                Piece &rest = pieces[std::size_t(pk)];
                rest.from = t;
                rest.startOnA = onA;
                rest.startOnC = c;
                rest.startVertex = body.finEnd(fi.fin);
                pieces.insert(pieces.begin() + pk, std::move(bridge));
                splitAt[std::size_t(j)] = pk + 1;
                if (pk == 0) {
                    // Catena chiusa: il pezzo di passaggio sta all'inizio.
                }
            }
        }

        // Se il contatto supera un bordo terminale, passa al supporto
        // tangente adiacente prima della chiusura sullo stesso coperchio.
        // Il bordo consumato scompare; i due contatti terminali possono
        // quindi accorciare bordi con vertici originali distinti.
        struct AdvancedEnd { EdgeId boundary; VertexId vertex; };
        std::map<std::pair<bool, bool>, AdvancedEnd> advancedEnds;
        if (!chain.closed && !chamfer) for (bool atStart : {true, false}) for (bool onA : {false, true}) {
            const ChainFin &cf = chain.fins[std::size_t(atStart ? 0 : n-1)];
            const int index = atStart ? 0 : int(pieces.size())-1;
            Piece &piece = pieces[std::size_t(index)];
            const VertexId vertex = atStart ? body.finStart(cf.fin) : body.finEnd(cf.fin);
            const FinId fin = onA ? cf.fin : body.otherFin(cf.fin);
            const FinId boundaryFin = (atStart == onA) ? body.fin(fin).previous : body.fin(fin).next;
            const EdgeId boundary = body.fin(boundaryFin).edge;
            const Edge &edge = body.edge(boundary);
            if (!edge.curve || body.isLaminar(boundary) || edgesAtVertex(body, vertex).size() != 3) continue;
            const double endpoint = atStart ? cf.start : cf.end;
            const int row = onA ? 0 : 2;
            const FaceId support = onA ? piece.a : piece.b, other = onA ? piece.b : piece.a;
            const double dir = (cf.end > cf.start ? 1.0 : -1.0)*(atStart ? -1.0 : 1.0);
            double test = endpoint;
            try { test = crossing(piece, row, boundary, support, 0.5*(piece.from+piece.to), dir, false, &edge.range, endpoint); }
            catch (const std::domain_error &) { continue; }
            const Vec3 contact = piece.blend->section(test).row[row].p;
            const bool boundaryStart = body.edgeStart(boundary) == vertex;
            const double farParameter = boundaryStart ? edge.range.hi : edge.range.lo;
            const Vec3 farPoint = edge.curve->point(farParameter);
            const auto closest = projectPoint(*edge.curve, contact, edge.range);
            Vec3 jet[2];
            if (boundaryStart) edge.curve->evaluateLeft(farParameter, 1, jet);
            else edge.curve->evaluate(farParameter, 1, jet);
            if (!(norm(jet[1]) > 0.0)) continue;
            const Vec3 beyond = (boundaryStart ? 1.0 : -1.0)*normalized(jet[1]);
            if (distance(closest.point, farPoint) > 1e-7*scale || dot(contact-farPoint, beyond) <= 1e-8*scale) continue;
            const VertexId far = boundaryStart ? body.edgeEnd(boundary) : body.edgeStart(boundary);
            const FaceId cap = body.finFace(body.otherFin(boundaryFin));
            const auto at = edgesAtVertex(body, far);
            if (at.size() != 3) continue;
            EdgeId seam, nextBoundary;
            FaceId nextSupport;
            for (EdgeId candidate : at) {
                if (candidate == boundary || body.isLaminar(candidate)) continue;
                const Edge &g = body.edge(candidate);
                const FaceId a = body.finFace(g.forward), b = body.finFace(g.backward);
                if (a == support || b == support) { seam = candidate; nextSupport = a == support ? b : a; }
                if (a == cap || b == cap) nextBoundary = candidate;
            }
            if (!seam.valid() || !nextBoundary.valid() || nextSupport == cap || nextSupport == other) continue;
            const Edge &nb = body.edge(nextBoundary);
            if (body.finFace(nb.forward) != nextSupport && body.finFace(nb.backward) != nextSupport) continue;
            const Vec3 q = body.vertex(far).point;
            if (dot(faceNormal(body, support, q), faceNormal(body, nextSupport, q)) < 1.0-1e-6) continue;
            const double t = crossing(piece, row, seam, support, 0.5*(piece.from+piece.to), dir, false, nullptr, endpoint);
            const double inner = atStart ? piece.to : piece.from;
            const double margin = 0.25*size/norm(body.edge(cf.edge).curve->derivative(endpoint));
            if (dir*(t-inner) <= 1e-10 || dir*(t-endpoint) > 20.0*margin) continue;
            const double outer = dir*(t-endpoint)>0.0 ? t+dir*margin : endpoint;
            const Section known = piece.blend->section(t);
            Piece extension = makePiece(piece.chainFin, onA ? piece.b : nextSupport,
                                        atStart ? outer : t, atStart ? t : outer, onA ? nextSupport : piece.a);
            seedFrom(extension, known, t);
            if (atStart) {
                piece.from = t;
                piece.startOnA = onA; piece.startOnC = seam; piece.startVertex = far;
                pieces.insert(pieces.begin(), std::move(extension));
            } else {
                piece.to = t;
                extension.startOnA = onA; extension.startOnC = seam; extension.startVertex = far;
                pieces.push_back(std::move(extension));
            }
            const int consumed = model.edgeIndex.at(boundary.index);
            model.edgeAlive[std::size_t(consumed)] = false;
            for (auto &face : model.faces) for (auto &loop : face.loops)
                loop.erase(std::remove_if(loop.begin(), loop.end(), [&](const Body::BuildFin &fin) { return fin.edge == consumed; }), loop.end());
            advancedEnds[{atStart, onA}] = {nextBoundary, far};
        }

        // Estremi delle catene aperte e angoli a mitra: tratti delle superfici oltre il vertice, punti e tagli.
        const int pieceCount = int(pieces.size());
        std::vector<Junction> junctions(static_cast<std::size_t>(chain.closed ? pieceCount : pieceCount + 1));
        // Tratto di superficie di ogni pezzo: di base il suo tratto d'edge.
        for (Piece &piece : pieces) {
            const Interval r = piece.rangeOf();
            piece.fitLo = r.lo;
            piece.fitHi = r.hi;
        }
        const auto fitPiece = [&](Piece &piece) {
            try {
                piece.fit = piece.blend->fit({piece.fitLo, piece.fitHi});
            } catch (const std::domain_error &) {
                // Alcune superfici B-spline di loft non ammettono la
                // continuazione della palla oltre il proprio tratto, anche
                // quando i due raccordi si incontrano correttamente entro il
                // vertice. Riprova sul dominio effettivo; le intersezioni
                // successive rifiuteranno comunque una mitra insufficiente.
                const Interval range = piece.rangeOf();
                piece.fitLo = std::max(piece.fitLo, range.lo);
                piece.fitHi = std::min(piece.fitHi, range.hi);
                piece.fit = piece.blend->fit({piece.fitLo, piece.fitHi});
            }
            piece.surface = EdgeBlend::surface(piece.fit, chamfer);
        };
        // Indici dei pezzi attorno a un giunto (tra i pezzi p e p + 1).
        const auto next = [&](int p) { return (p + 1) % pieceCount; };

        // Giunti tra pezzi (mitre dopo aver esteso i tratti).
        // Giunti a mitra: gli angoli vivi di A (Mitre) e i passaggi da una faccia B
        // all'altra dove le due non sono tangenti (Split con le due palle diverse
        // nell'attraversamento di c: un loft da un cerchio a un quadrato, le cui
        // pezze sono tangenti solo sul cerchio). I due raccordi si tagliano lungo
        // la loro intersezione.
        struct MitreData {
            int before = -1, after = -1;
            EdgeId c;
            VertexId vertex;              // dove c si accorcia
            double tA0 = 0.0, tA1 = 0.0;  // parametri del punto comune in A sui due pezzi
            double q0 = 0.0, q1 = 0.0;    // parametri dove i contatti su B attraversano c
            Vec3 onC0, onC1;              // punti esatti su c (il fit puo' avere un piccolo scarto)
            bool haveOnC0 = false, haveOnC1 = false;
        };
        std::vector<MitreData> mitres;
        for (int p = 0; p < (chain.closed ? pieceCount : pieceCount - 1); ++p) {
            Piece &a = pieces[std::size_t(p)], &b = pieces[std::size_t(next(p))];
            MitreData m;
            m.before = p;
            m.after = next(p);
            if (a.chainFin != b.chainFin && chain.joints[std::size_t(a.chainFin)] == Joint::Cross) {
                continue;
            } else if (a.chainFin != b.chainFin && chain.joints[std::size_t(a.chainFin)] == Joint::Mitre) {
                m.c = chain.third[std::size_t(a.chainFin)];
                m.vertex = body.finEnd(chain.fins[std::size_t(a.chainFin)].fin);
            } else if (a.a != b.a || a.b != b.b) {
                // Passaggio Split: e' una mitra solo se le sezioni dei due pezzi non coincidono.
                if (b.startOnC.valid()) {
                    m.c = b.startOnC;
                    m.vertex = b.startVertex;
                } else {
                    m.c = chain.third[std::size_t(a.chainFin)];
                    m.vertex = body.finEnd(chain.fins[std::size_t(a.chainFin)].fin);
                }
                const Section &sa = a.blend->section(a.to), &sb = b.blend->section(b.from);
                // Facce B tangenti entro 1e-7 rad (pezze B-spline di un offset
                // cucito) danno due palle diverse per meno di un micron: e' la
                // stessa sezione, con lo scarto registrato sui punti del giunto.
                // Una mitra tra raccordi quasi coincidenti sarebbe degenere.
                const double passage = std::max(1e-9 * scale, kSplitPassageTolerance);
                if (distance(sa.row[0].p, sb.row[0].p) <= passage && distance(sa.row[2].p, sb.row[2].p) <= passage) continue;
                if (a.a != b.a)
                    throw std::domain_error("blendEdges: passaggio tangente sul primo supporto non coincidente");
                b.startOnC = EdgeId();  // il giunto non e' piu' una sezione comune
            } else {
                continue;
            }
            // Il punto comune in A: Gauss-Newton sui contatti delle sezioni.
            double ta = a.to, tb = b.from;
            for (int iteration = 0; iteration < 60; ++iteration) {
                const Section &sa = a.blend->section(ta), &sb = b.blend->section(tb);
                const Vec3 f = sa.row[0].p - sb.row[0].p, da = sa.drow[0].p, db = sb.drow[0].p;
                const double a11 = dot(da, da), a12 = -dot(da, db), a22 = dot(db, db);
                const double r1 = -dot(da, f), r2 = dot(db, f), det = a11 * a22 - a12 * a12;
                if (!(std::fabs(det) > 0.0)) break;
                const double du = (r1 * a22 - a12 * r2) / det, dv = (a11 * r2 - a12 * r1) / det;
                ta += du;
                tb += dv;
                if (std::fabs(du) + std::fabs(dv) < 1e-15 * (1.0 + std::fabs(ta) + std::fabs(tb))) break;
            }
            if (distance(a.blend->section(ta).row[0].p, b.blend->section(tb).row[0].p) > 1e-9 * scale)
                throw std::domain_error("blendEdges: i contatti dei raccordi nell'angolo non si incontrano (raggio troppo grande)");
            m.tA0 = ta;
            m.tA1 = tb;
            const double dirA = a.to > a.from ? 1.0 : -1.0, dirB = b.to > b.from ? 1.0 : -1.0;
            m.q0 = crossing(a, 2, m.c, a.b, 0.5 * (a.from + a.to), dirA);
            m.q1 = crossing(b, 2, m.c, b.b, 0.5 * (b.from + b.to), -dirB);
            // Le superfici coprono il punto comune in A e l'attraversamento di c, con un margine,
            // solo dalla parte del giunto (l'altra estremita' resta com'e').
            const CurvePtr<3> &ca = body.edge(chain.fins[std::size_t(a.chainFin)].edge).curve, &cb = body.edge(chain.fins[std::size_t(b.chainFin)].edge).curve;
            const double marginA = 0.25 * size / norm(ca->derivative(a.to)), marginB = 0.25 * size / norm(cb->derivative(b.from));
            if (dirA > 0) a.fitHi = std::max(a.fitHi, std::max({m.q0, a.to, ta}) + marginA);
            else a.fitLo = std::min(a.fitLo, std::min({m.q0, a.to, ta}) - marginA);
            if (dirB > 0) b.fitLo = std::min(b.fitLo, std::min({m.q1, b.from, tb}) - marginB);
            else b.fitHi = std::max(b.fitHi, std::max({m.q1, b.from, tb}) + marginB);
            mitres.push_back(m);
        }
        // Estremi delle catene aperte: la faccia d'estremita' E con gli spigoli su A e su B.
        struct EndData {
            int piece = -1;
            bool atStart = true;
            VertexId vertex, vertexA, vertexB;
            EdgeId onA, onB;
            Interval windowA, windowB;
            FaceId face;
            FaceId secondFace;
            EdgeId seam;
            bool free = false;    // i due bordi terminano sulla frontiera della lamina
            bool normal = false;  // E piana e normale allo spigolo: la sezione nel vertice
            double tA = 0.0, tB = 0.0;
            Vec3 pointA, pointB;   // intersezioni raffinate fra superficie definitiva ed edge
            bool havePointA = false, havePointB = false;
        };
        std::vector<EndData> ends;
        if (!chain.closed)
            for (bool atStart : {true, false}) {
                EndData end;
                end.atStart = atStart;
                end.piece = atStart ? 0 : pieceCount - 1;
                const ChainFin &cf = chain.fins[std::size_t(atStart ? 0 : n - 1)];
                end.vertex = atStart ? body.finStart(cf.fin) : body.finEnd(cf.fin);
                const std::vector<EdgeId> at = edgesAtVertex(body, end.vertex);
                if (at.size() != 3 && at.size() != 4)
                    throw std::domain_error("blendEdges: estremo della catena con piu' di quattro spigoli (caso non gestito)");
                const FinId finB = body.otherFin(cf.fin);
                FinId onA = atStart ? body.fin(cf.fin).previous : body.fin(cf.fin).next;
                FinId onB = atStart ? body.fin(finB).next : body.fin(finB).previous;
                end.vertexA = end.vertexB = end.vertex;
                const auto advancedA = advancedEnds.find({atStart, true});
                const auto advancedB = advancedEnds.find({atStart, false});
                if (advancedA != advancedEnds.end()) {
                    const Edge &next = body.edge(advancedA->second.boundary);
                    end.vertexA = advancedA->second.vertex;
                    const FaceId support = pieces[std::size_t(end.piece)].a;
                    onA = body.finFace(next.forward) == support ? next.forward : next.backward;
                }
                if (advancedB != advancedEnds.end()) {
                    const Edge &next = body.edge(advancedB->second.boundary);
                    end.vertexB = advancedB->second.vertex;
                    const FaceId support = pieces[std::size_t(end.piece)].b;
                    onB = body.finFace(next.forward) == support ? next.forward : next.backward;
                }
                end.onA = body.fin(onA).edge;
                end.onB = body.fin(onB).edge;
                const FinId acrossA = body.otherFin(onA), acrossB = body.otherFin(onB);
                end.free = !acrossA.valid() && !acrossB.valid();
                if (acrossA.valid() != acrossB.valid())
                    throw std::domain_error("blendEdges: estremita' con un solo bordo libero non ancora supportata");
                if (!end.free) {
                    end.face = body.finFace(acrossA);
                    end.secondFace = body.finFace(acrossB);
                }
                if (end.secondFace != end.face) {
                    if (at.size() != 4 || chamfer)
                        throw std::domain_error("blendEdges: pezza d'angolo terminale non gestita per gli smussi");
                    for (EdgeId candidate : at) {
                        if (candidate == cf.edge || candidate == end.onA || candidate == end.onB) continue;
                        const Edge &edge = body.edge(candidate);
                        const FaceId f0 = body.finFace(edge.forward), f1 = body.finFace(edge.backward);
                        if ((f0 == end.face && f1 == end.secondFace) || (f1 == end.face && f0 == end.secondFace)) end.seam = candidate;
                    }
                    if (!end.seam.valid()) throw std::domain_error("blendEdges: cucitura della pezza d'angolo non trovata");
                }
                // Un raccordo convesso puo' terminare contro una parete
                // concava: il contatto prolunga l'arco del bordo terminale.
                // Cerca solo oltre il vertice interessato, senza attraversare
                // l'altro estremo o fare un giro della curva periodica.
                // Anche quando la faccia del contatto gira attorno al vertice
                // (angolo concavo della faccia: un solco che finisce contro una
                // parete, con il cilindro che continua oltre la parete) il
                // contatto esce dalla faccia d'estremita' sul prolungamento del
                // bordo rettilineo, che si allunga.
                const auto reflexCorner = [&](FinId onFace, EdgeId other) {
                    const Vec3 p = body.vertex(end.vertex).point;
                    const bool fromVertex = body.finStart(onFace) == end.vertex;
                    const Vec3 inward = cross(faceNormal(body, body.finFace(onFace), p), finTangent(body, onFace, !fromVertex));
                    const Edge &edge = body.edge(other);
                    const bool start = body.edgeStart(other) == end.vertex;
                    const Vec3 leaving = normalized(edge.curve->derivative(start ? edge.range.lo : edge.range.hi));
                    return dot(start ? leaving : -leaving, inward) < -kSmooth;
                };
                const auto endWindow = [&](EdgeId id, bool reflex) {
                    const Edge &edge = body.edge(id);
                    Interval window = edge.range;
                    const bool circle = edge.curve->type() == CurveType::Circle;
                    const bool spline = edge.curve->type() == CurveType::BSpline;
                    if (body.isLaminar(id)) return window;
                    if (circle || ((!convex || reflex) && (edge.curve->type() == CurveType::Line || spline))) {
                        const VertexId vertex = id == end.onB ? end.vertexB : end.vertexA;
                        const bool start = body.edgeStart(id) == vertex;
                        const double t = start ? edge.range.lo : edge.range.hi;
                        double margin = (circle ? 2.0 : spline ? 5.0 : 50.0) * size / norm(edge.curve->derivative(t));
                        if (circle) margin = std::min(margin, 0.45 * std::max(0.0, kTwoPi - edge.range.length()));
                        if (start) window.lo -= margin;
                        else window.hi += margin;
                    }
                    if (spline && (window.lo < edge.range.lo || window.hi > edge.range.hi)) {
                        if (boundaryMode)
                            window = extendBoundaryWindow(body, model, id, window);
                        else
                            extendSplineBoundary(body, model, id, window);
                    }
                    return window;
                };
                end.windowA = endWindow(end.onA, advancedA != advancedEnds.end() ? false : reflexCorner(cf.fin, end.onA));
                end.windowB = endWindow(end.onB, advancedB != advancedEnds.end() ? false : reflexCorner(finB, end.onB));
                const Vec3 tangent = finTangent(body, cf.fin, !atStart);
                if (end.free) {
                    end.normal = body.edge(end.onA).curve->type() == CurveType::Line
                        && body.edge(end.onB).curve->type() == CurveType::Line
                        && std::fabs(dot(finTangent(body,onA,false),tangent)) < kSmooth
                        && std::fabs(dot(finTangent(body,onB,false),tangent)) < kSmooth;
                } else {
                    const Face &E = body.face(end.face);
                    end.normal = end.face == end.secondFace && E.surface->type() == SurfaceType::Plane
                        && std::fabs(dot(static_cast<const Plane &>(*E.surface).frame().zDir(), tangent)) >= 1.0 - kSmooth;
                }
                Piece &piece = pieces[std::size_t(end.piece)];
                const double vertexParameter = atStart ? cf.start : cf.end;
                if (end.normal) {
                    end.tA = end.tB = vertexParameter;
                } else {
                    // Il raccordo prosegue fino a uscire da E: dove i contatti attraversano gli spigoli di E.
                    const double dir = (cf.end > cf.start ? 1.0 : -1.0) * (atStart ? -1.0 : 1.0);
                    const bool advanced = advancedA != advancedEnds.end() || advancedB != advancedEnds.end();
                    const double inside = advanced ? (atStart ? piece.to : piece.from) : 0.5*(piece.from+piece.to);
                    end.tA = crossing(piece, 0, end.onA, piece.a, inside, dir, !convex, &end.windowA, vertexParameter);
                    end.tB = crossing(piece, 2, end.onB, piece.b, inside, dir, !convex, &end.windowB, vertexParameter);
                    const double speed = norm(body.edge(cf.edge).curve->derivative(vertexParameter)), margin = 0.25 * size / speed;
                    if (dir > 0) piece.fitHi = std::max(piece.fitHi, std::max({end.tA, end.tB, vertexParameter}) + margin);
                    else piece.fitLo = std::min(piece.fitLo, std::min({end.tA, end.tB, vertexParameter}) - margin);
                }
                ends.push_back(end);
            }
        // L'approssimazione B-spline di ogni superficie di raccordo campiona
        // molte sezioni numeriche ed e' indipendente dagli altri pezzi.
        std::vector<std::exception_ptr> fitErrors(pieces.size());
        parallelFor(pieces.size(), threadCount(0), [&](std::size_t p) {
            try {
                fitPiece(pieces[p]);
            } catch (...) {
                fitErrors[p] = std::current_exception();
            }
        });
        for (const std::exception_ptr &error : fitErrors)
            if (error) std::rethrow_exception(error);
        // Raffina i punti in cui i contatti sui fianchi raggiungono lo
        // spigolo comune. La ricerca preliminare lavora sulle sezioni non
        // ancora interpolate e sui loft può arrestarsi al vertice; ora sono
        // disponibili le superfici definitive del raccordo.
        for (MitreData &mitre : mitres) {
            Piece &a = pieces[std::size_t(mitre.before)], &b = pieces[std::size_t(mitre.after)];
            const Edge &c = body.edge(mitre.c);
            const double atVertex = body.edgeStart(mitre.c) == mitre.vertex ? c.range.lo : c.range.hi;
            const auto refine = [&](Piece &piece, double &parameter, Vec3 &boundaryPoint, bool &haveBoundaryPoint) {
                double bestT = parameter, bestScore = std::numeric_limits<double>::infinity();
                double bestOnC = atVertex;
                bool found = false;
                // Il vecchio vertice e' sempre una stima allettante per
                // Newton, ma nei fianchi suddivisi di un loft puo' essere
                // l'intersezione banale sbagliata. Parti anche dall'interno
                // dell'isocurva di contatto e preferisci il punto su c la
                // cui distanza dal vertice e' dell'ordine del raggio.
                for (int i = 0; i <= 12; ++i)
                    for (double seedC : {atVertex, 0.5 * (c.range.lo + c.range.hi)}) {
                        double t = piece.fitLo + (piece.fitHi - piece.fitLo) * double(i) / 12.0;
                        double onC = seedC;
                        if (!surfaceCurveMeet(*piece.surface, 1.0, *c.curve, c.range, t, onC, scale)) continue;
                        const double margin = 1e-8 * (1.0 + piece.fitHi - piece.fitLo);
                        if (t < piece.fitLo - margin || t > piece.fitHi + margin) continue;
                        const double travel = distance(c.curve->point(onC), body.vertex(mitre.vertex).point);
                        const double score = std::fabs(travel - size);
                        if (!found || score < bestScore) {
                            found = true;
                            bestScore = score;
                            bestT = t;
                            bestOnC = onC;
                        }
                    }
                if (found && distance(c.curve->point(bestOnC), body.vertex(mitre.vertex).point) > 0.05 * size) {
                    parameter = bestT;
                    boundaryPoint = c.curve->point(bestOnC);
                    haveBoundaryPoint = true;
                    return;
                }
                if (chamfer) return;

                // Se il fit conserva l'intersezione banale nel vecchio
                // vertice, ricava il contatto sulla cucitura imponendo alla
                // sfera, appoggiata al fianco, anche la distanza firmata dalla
                // faccia A. E' la costruzione geometrica del punto in cui il
                // supporto passa da una pezza laterale alla successiva.
                const Interval pr = piece.rangeOf();
                const Section &known = piece.blend->section(0.5 * (pr.lo + pr.hi));
                const FaceId faceA = chain.fins[std::size_t(piece.chainFin)].a;
                const double signA = dot(known.center - known.row[0].p, faceNormal(body, faceA, known.row[0].p)) >= 0.0 ? 1.0 : -1.0;
                const double signB = dot(known.center - known.row[2].p, faceNormal(body, piece.b, known.row[2].p)) >= 0.0 ? 1.0 : -1.0;
                const auto residual = [&](double s) {
                    const Vec3 q = c.curve->point(s);
                    const Vec3 center = q + signB * size * faceNormal(body, piece.b, q);
                    const SurfaceProjection onA = projectPoint(*body.face(faceA).surface, center);
                    const Vec3 pa = body.face(faceA).surface->point(onA.u, onA.v);
                    return dot(center - pa, faceNormal(body, faceA, pa)) - signA * size;
                };
                const double other = atVertex == c.range.lo ? c.range.hi : c.range.lo;
                double s0 = atVertex, f0 = residual(s0);
                bool bracketed = false;
                double s1 = s0, f1 = f0;
                for (int i = 1; i <= 128; ++i) {
                    s1 = atVertex + (other - atVertex) * double(i) / 128.0;
                    f1 = residual(s1);
                    if ((f0 <= 0.0 && f1 >= 0.0) || (f0 >= 0.0 && f1 <= 0.0)) {
                        bracketed = true;
                        break;
                    }
                    s0 = s1;
                    f0 = f1;
                }
                if (!bracketed) return;
                for (int iteration = 0; iteration < 80; ++iteration) {
                    const double sm = 0.5 * (s0 + s1), fm = residual(sm);
                    if ((f0 <= 0.0 && fm >= 0.0) || (f0 >= 0.0 && fm <= 0.0)) s1 = sm, f1 = fm;
                    else s0 = sm, f0 = fm;
                }
                const double onC = 0.5 * (s0 + s1);
                const Vec3 q = c.curve->point(onC);
                if (distance(q, body.vertex(mitre.vertex).point) <= 1e-6 * size) return;
                const CurvePtr<3> contact = std::make_shared<BSplineCurve<3>>(piece.surface->vIsoCurve(1.0));
                parameter = projectPoint(*contact, q, {piece.fitLo, piece.fitHi}).parameter;
                boundaryPoint = q;
                haveBoundaryPoint = true;
            };
            refine(a, mitre.q0, mitre.onC0, mitre.haveOnC0);
            refine(b, mitre.q1, mitre.onC1, mitre.haveOnC1);
        }
        // Anche agli estremi il primo attraversamento e' calcolato sulle
        // sezioni esatte. Nei loft guidati una pezza laterale puo' essere
        // molto compressa vicino al coperchio e la continuazione numerica
        // della sezione puo' fermarsi proprio sul vecchio vertice. Dopo il
        // fit intersechiamo quindi le due isocurve di contatto con gli edge
        // reali della faccia d'estremita': evita di scambiare quel limite
        // numerico per un raggio che non entra nella faccia.
        for (EndData &end : ends) {
            if (end.normal) continue;
            Piece &piece = pieces[std::size_t(end.piece)];
            const auto refine = [&](double v, EdgeId boundary, const Interval &window, double &parameter,
                                    Vec3 &point, bool &havePoint) {
                const Edge &edge = body.edge(boundary);
                const VertexId vertex = boundary == end.onB ? end.vertexB : end.vertexA;
                const double atVertex = body.edgeStart(boundary) == vertex ? edge.range.lo : edge.range.hi;
                double bestT = parameter, bestS = atVertex, bestScore = std::numeric_limits<double>::infinity();
                bool found = false;
                for (int i = 0; i <= 16; ++i) {
                    const double seedT = piece.fitLo + (piece.fitHi - piece.fitLo) * double(i) / 16.0;
                    for (double seedS : {atVertex, 0.5 * (window.lo + window.hi), window.lo, window.hi}) {
                        double onBlend = seedT, onEdge = seedS;
                        if (!surfaceCurveMeet(*piece.surface, v, *edge.curve, window, onBlend, onEdge, scale)) continue;
                        const double margin = 1e-8 * (1.0 + piece.fitHi - piece.fitLo);
                        if (onBlend < piece.fitLo - margin || onBlend > piece.fitHi + margin) continue;
                        const double score = std::fabs(onBlend - parameter);
                        if (!found || score < bestScore) {
                            found = true;
                            bestScore = score;
                            bestT = onBlend;
                            bestS = onEdge;
                        }
                    }
                }
                if (!found) return;
                parameter = bestT;
                point = edge.curve->point(bestS);
                havePoint = true;
            };
            refine(0.0, end.onA, end.windowA, end.tA, end.pointA, end.havePointA);
            refine(1.0, end.onB, end.windowB, end.tB, end.pointB, end.havePointB);
        }
        {
            // Due catene non devono toccarsi (un vertice con tre spigoli scelti: pezza d'angolo, non gestita).
            std::set<int> own;
            for (const ChainFin &cf : chain.fins) own.insert({body.finStart(cf.fin).index, body.finEnd(cf.fin).index});
            for (int v : own)
                if (!usedVertices.insert(v).second) throw std::domain_error("blendEdges: catene di raccordi che si toccano (pezze d'angolo tra facce curve non gestite)");
        }

        // Estremi dei contatti di ogni pezzo: [lo, hi] nel parametro dell'edge.
        std::vector<double> aLo(pieces.size()), aHi(pieces.size()), bLo(pieces.size()), bHi(pieces.size());
        for (std::size_t p = 0; p < pieces.size(); ++p) {
            const Interval r = pieces[p].rangeOf();
            aLo[p] = bLo[p] = r.lo;
            aHi[p] = bHi[p] = r.hi;
        }
        // Estremo "iniziale" o "finale" del pezzo nel verso della catena.
        const auto setEnd = [&](std::vector<double> &lo, std::vector<double> &hi, int p, bool atChainEnd, double t) {
            const Piece &piece = pieces[std::size_t(p)];
            const bool increasing = piece.to > piece.from;
            // I due estremi si calcolano indipendentemente: entrambi possono
            // avanzare oltre il vecchio tratto di un bordo corto. Confrontarli
            // qui con l'altro estremo, ancora provvisorio, sposta il taglio.
            // Un intervallo consumato si riconosce solo dopo tutti i giunti.
            t = std::clamp(t, piece.fitLo, piece.fitHi);
            if (atChainEnd == increasing) hi[std::size_t(p)] = t;
            else lo[std::size_t(p)] = t;
        };

        // Giunti di sezione (Smooth, Split, e i passaggi Split sulla stessa fin): punti e arco.
        const auto sectionJunction = [&](Junction &junction, const Piece &piece, double t, bool onC, EdgeId c, VertexId vertex, bool onA = false) {
            const bool left = t == piece.rangeOf().hi && t == body.edge(chain.fins[std::size_t(piece.chainFin)].edge).range.hi;
            const Section &s = piece.blend->section(t, left);
            Vec3 pa = s.row[0].p, pb = s.row[2].p;
            if (onC && onA) pa = model.projectOnEdgeChain(model.edgeIndex.at(c.index), model.vertexIndex.at(vertex.index), pa);
            junction.pointA = model.addPoint(pa);
            if (onC && !onA) pb = model.projectOnEdgeChain(model.edgeIndex.at(c.index), model.vertexIndex.at(vertex.index), pb);
            junction.pointB = model.addPoint(pb);
            const double gapA = distance(pa, s.row[0].p), gapB = distance(pb, s.row[2].p);
            if (gapA > 1e-9 * scale) model.pointTolerance[std::size_t(junction.pointA)] = 2.0 * gapA;
            if (onC && gapB > 1e-9 * scale) model.pointTolerance[std::size_t(junction.pointB)] = 2.0 * distance(pb, s.row[2].p);
            junction.connector = model.addEdge(junction.pointA, junction.pointB, std::make_shared<BSplineCurve<3>>(piece.surface->uIsoCurve(t)), {0.0, 1.0},
                                               onC ? 2.0 * std::max(gapA, gapB) : 0.0);
        };

        // Giunti tra i pezzi p e p + 1.
        for (int p = 0; p < (chain.closed ? pieceCount : pieceCount - 1); ++p) {
            Junction &junction = junctions[std::size_t(chain.closed ? next(p) : p + 1)];
            Piece &a = pieces[std::size_t(p)], &b = pieces[std::size_t(next(p))];
            const MitreData *mitre = nullptr;
            for (const MitreData &m : mitres)
                if (m.before == p) mitre = &m;
            if (!mitre) {
                // Sezione comune: passaggio Split (il contatto su B attraversa c) o vertice.
                EdgeId c;
                VertexId v;
                bool onA = false;
                if (b.startOnC.valid()) {
                    onA = b.startOnA;
                    c = b.startOnC;
                    v = b.startVertex;
                } else if (a.chainFin != b.chainFin && (chain.joints[std::size_t(a.chainFin)] == Joint::Split || chain.joints[std::size_t(a.chainFin)] == Joint::SplitA)
                           && (a.a != b.a || a.b != b.b)) {
                    onA = chain.joints[std::size_t(a.chainFin)] == Joint::SplitA;
                    // Il contatto attraversa c proprio nel vertice.
                    c = chain.third[std::size_t(a.chainFin)];
                    v = body.finEnd(chain.fins[std::size_t(a.chainFin)].fin);
                }
                if (a.chainFin != b.chainFin && chain.joints[std::size_t(a.chainFin)] == Joint::Cross) {
                    // Stessa sezione sulle due coppie di facce: gli spigoli tra A e A' e tra B e B' vi si accorciano.
                    sectionJunction(junction, a, a.to, false, EdgeId(), VertexId());
                    const Section &sa = a.blend->section(a.to), &sb = b.blend->section(b.from);
                    junction.toleranceA = distance(sa.row[0].p, sb.row[0].p);
                    junction.toleranceB = distance(sa.row[2].p, sb.row[2].p);
                    approximateCross = approximateCross || junction.toleranceA > 1e-9 * scale
                        || junction.toleranceB > 1e-9 * scale;
                    const double crossTolerance = 0.01 * size;
                    if (junction.toleranceA > crossTolerance || junction.toleranceB > crossTolerance)
                        throw std::domain_error("blendEdges: le facce non sono tangenti dove lo spigolo prosegue (non gestito)");
                    model.pointTolerance[std::size_t(junction.pointA)] = std::max(
                        model.pointTolerance[std::size_t(junction.pointA)], 1.01 * junction.toleranceA);
                    model.pointTolerance[std::size_t(junction.pointB)] = std::max(
                        model.pointTolerance[std::size_t(junction.pointB)], 1.01 * junction.toleranceB);
                    const VertexId v = body.finEnd(chain.fins[std::size_t(a.chainFin)].fin);
                    const int vertex = model.vertexIndex.at(v.index);
                    try { model.moveEnd(model.edgeIndex.at(chain.third[std::size_t(a.chainFin)].index), vertex, junction.pointA); }
                    catch (const std::domain_error &failure) {
                        throw std::domain_error(std::string(failure.what()) + " (giunto Cross A, bordo "
                                                + std::to_string(chain.third[std::size_t(a.chainFin)].index) + ")");
                    }
                    try { model.moveEnd(model.edgeIndex.at(chain.fourth[std::size_t(a.chainFin)].index), vertex, junction.pointB); }
                    catch (const std::domain_error &failure) {
                        throw std::domain_error(std::string(failure.what()) + " (giunto Cross B, bordo "
                                                + std::to_string(chain.fourth[std::size_t(a.chainFin)].index) + ")");
                    }
                    continue;
                }
                sectionJunction(junction, a, a.to, c.valid(), c, v, onA);
                if (a.a != b.a || a.b != b.b) {
                    // Passaggio Split con le due sezioni uguali entro kSplitPassageTolerance.
                    const Section &sa = a.blend->section(a.to), &sb = b.blend->section(b.from);
                    const double gapA = distance(sa.row[0].p, sb.row[0].p), gapB = distance(sa.row[2].p, sb.row[2].p);
                    if (gapA > 1e-9 * scale)
                        model.pointTolerance[std::size_t(junction.pointA)] = std::max(model.pointTolerance[std::size_t(junction.pointA)], 2.0 * gapA);
                    if (gapB > 1e-9 * scale)
                        model.pointTolerance[std::size_t(junction.pointB)] = std::max(model.pointTolerance[std::size_t(junction.pointB)], 2.0 * gapB);
                }
                if (c.valid()) model.moveEndAlongChain(model.edgeIndex.at(c.index), model.vertexIndex.at(v.index), onA ? junction.pointA : junction.pointB);
                continue;
            }
            // Angolo a mitra.
            const Vec3 pA = 0.5 * (a.blend->section(mitre->tA0).row[0].p + b.blend->section(mitre->tA1).row[0].p);
            junction.pointA = model.addPoint(pA);
            setEnd(aLo, aHi, p, true, mitre->tA0);
            setEnd(aLo, aHi, next(p), false, mitre->tA1);
            // Punti tripli: il contatto su B di un raccordo sull'altro raccordo.
            double t0 = a.to, s0 = b.from, w0 = 1.0;
            const bool ok0 = surfaceMeet(*a.surface, 1.0, *b.surface, t0, s0, w0, scale) && w0 <= 1.0 + 1e-9 && w0 >= -1e-9;
            double t1 = b.from, s1 = a.to, w1 = 1.0;
            const bool ok1 = surfaceMeet(*b.surface, 1.0, *a.surface, t1, s1, w1, scale) && w1 <= 1.0 + 1e-9 && w1 >= -1e-9;
            if (!ok0 && !ok1) throw std::domain_error("blendEdges: i raccordi nell'angolo non si incontrano sui fianchi (raggio troppo grande?)");
            const bool symmetric = ok0 && ok1 && w0 >= 1.0 - 1e-7 && w1 >= 1.0 - 1e-7;
            // Il raccordo profondo e' quello il cui contatto su B arriva fino a c.
            junction.deep = symmetric ? -1 : (ok0 && (!ok1 || w0 < w1) ? 1 : 0);
            Vec3 X;
            if (junction.deep == 1) X = a.surface->point(t0, 1.0);        // sul contatto del primo (basso), il secondo va a c
            else if (junction.deep == 0) X = b.surface->point(t1, 1.0);
            else X = 0.5 * (a.surface->point(mitre->q0, 1.0) + b.surface->point(mitre->q1, 1.0));
            junction.pointB = model.addPoint(X);
            // Contatti su B: il basso finisce in X, il profondo sull'attraversamento di c.
            const Vec3 onC0 = mitre->haveOnC0 ? mitre->onC0 : a.surface->point(mitre->q0, 1.0);
            const Vec3 onC1 = mitre->haveOnC1 ? mitre->onC1 : b.surface->point(mitre->q1, 1.0);
            const auto deepPoint = [&](const Vec3 &onC) {
                const double gap = distance(X, onC);
                if (gap <= std::max(1e-8 * scale, 0.01 * size)) {
                    if (gap > 1e-9 * scale)
                        model.pointTolerance[std::size_t(junction.pointB)] = std::max(model.pointTolerance[std::size_t(junction.pointB)], 2.0 * gap);
                    return junction.pointB;
                }
                return model.addPoint(onC);
            };
            if (junction.deep == 1) {
                setEnd(bLo, bHi, p, true, t0);
                setEnd(bLo, bHi, next(p), false, mitre->q1);
                junction.deepPoint = deepPoint(onC1);
            } else if (junction.deep == 0) {
                setEnd(bLo, bHi, p, true, mitre->q0);
                setEnd(bLo, bHi, next(p), false, t1);
                junction.deepPoint = deepPoint(onC0);
            } else {
                setEnd(bLo, bHi, p, true, mitre->q0);
                setEnd(bLo, bHi, next(p), false, mitre->q1);
                junction.deepPoint = junction.pointB;
                const double gap = distance(onC0, onC1);
                if (gap > 1e-7 * scale) model.pointTolerance[std::size_t(junction.pointB)] = gap;
            }
            // Gamma: i due raccordi tra il punto in A e X, tracciata direttamente (i raccordi vi si
            // incontrano anche sotto angoli piccoli, dove il tracciamento generale non basta).
            double Xp[4];
            if (junction.deep == 1) Xp[0] = t0, Xp[1] = 1.0, Xp[2] = s0, Xp[3] = w0;
            else if (junction.deep == 0) Xp[0] = s1, Xp[1] = w1, Xp[2] = t1, Xp[3] = 1.0;
            else Xp[0] = mitre->q0, Xp[1] = 1.0, Xp[2] = mitre->q1, Xp[3] = 1.0;
            Cut gamma;
            if (junction.deep == -1) {
                // Simmetrico: X su c; il punto comune esatto dei due contatti su B.
                Xp[0] = t0, Xp[1] = 1.0, Xp[2] = s0, Xp[3] = w0;
            }
            const double mitreTolerance = std::max({kFitTolerance, a.fit.error, b.fit.error});
            gamma = traceMitre(*a.surface, *b.surface, mitre->tA0, mitre->tA1, Xp, scale, mitreTolerance);
            {
                const Vec3 end = gamma.curve->point(gamma.range.hi), begin = gamma.curve->point(gamma.range.lo);
                gamma.gap = std::max({gamma.gap, distance(end, X), distance(begin, pA)});
            }
            // Lo scarto gia' misurato dalla continuazione non deve diventare
            // una tolleranza arbitraria dell'edge. Una cubica instabile vicino
            // alla tangenza puo' passare checkBody con quella tolleranza e
            // produrre SP-curve fuori dalla pezza e triangoli attraverso il
            // modello. Applica anche qui il limite relativo al raggio usato
            // per gli altri contatti approssimati, prima di costruire il B-rep.
            const double admissibleMitreGap = std::max(kLinearResolution, 0.01 * size);
            if (!std::isfinite(gamma.gap) || gamma.gap > admissibleMitreGap)
                throw std::domain_error("blendEdges: raccordo non valido nell'angolo: scarto "
                    + std::to_string(gamma.gap) + " mm oltre il limite "
                    + std::to_string(admissibleMitreGap) + " mm; scegliere un raggio diverso");
            junction.gamma = model.addEdge(junction.pointA, junction.pointB, gamma.curve, gamma.range, gamma.gap > 1e-7 ? 2.0 * gamma.gap : 0.0);
            if (gamma.gap > 1e-7) model.pointTolerance[std::size_t(junction.pointA)] = std::max(model.pointTolerance[std::size_t(junction.pointA)], 2.0 * gamma.gap);
            if (junction.deep >= 0 && junction.deepPoint != junction.pointB) {
                // Delta: il raccordo profondo con il fianco del basso, da X a c.
                const Piece &deepPiece = junction.deep == 1 ? b : a;
                const FaceId other = junction.deep == 1 ? a.b : b.b;
                const Vec3 to = model.points[std::size_t(junction.deepPoint)];
                Cut delta;
                try { delta = traceBetween(*deepPiece.surface, *body.face(other).surface, X, to, scale, deepPiece.fit.error); }
                catch (const std::domain_error &failure) { throw std::domain_error(std::string("blendEdges: delta della mitra: ") + failure.what()); }
                junction.delta = delta.forward ? model.addEdge(junction.pointB, junction.deepPoint, delta.curve, delta.range, delta.gap > 1e-7 ? 2.0 * delta.gap : 0.0)
                                               : model.addEdge(junction.deepPoint, junction.pointB, delta.curve, delta.range, delta.gap > 1e-7 ? 2.0 * delta.gap : 0.0);
            }
            // c si accorcia fino al punto dove arriva il contatto profondo.
            try { model.moveEnd(model.edgeIndex.at(mitre->c.index), model.vertexIndex.at(mitre->vertex.index), junction.deepPoint); }
            catch (const std::domain_error &failure) {
                throw std::domain_error(std::string(failure.what()) + " (mitra, bordo " + std::to_string(mitre->c.index) + ")");
            }
        }
        std::vector<bool> skipA(pieces.size(), false), skipB(pieces.size(), false);
        std::vector<int> aStartOverride(pieces.size(), -1), aEndOverride(pieces.size(), -1);
        std::vector<int> bStartOverride(pieces.size(), -1), bEndOverride(pieces.size(), -1);
        std::vector<std::vector<int>> extraCaps(pieces.size());
        // Estremi delle catene aperte.
        for (const EndData &end : ends) {
            Junction &junction = junctions[std::size_t(end.atStart ? 0 : pieceCount)];
            Piece &piece = pieces[std::size_t(end.piece)];
            if (end.normal) {
                sectionJunction(junction, piece, end.atStart ? piece.from : piece.to, false, EdgeId(), VertexId());
            } else {
                // Le intersezioni preliminari danno i punti esatti sugli edge
                // terminali. Dopo il fit, riallinea separatamente i due
                // parametri alle isocurve di contatto definitive: presso una
                // singolarita' surfaceCurveMeet puo' convergere sull'altro
                // ramo della stessa curva chiusa.
                const Section &sa = piece.blend->section(end.tA), &sb = piece.blend->section(end.tB);
                Vec3 ra = end.havePointA ? end.pointA : projectPoint(*body.edge(end.onA).curve, sa.row[0].p, end.windowA).point;
                Vec3 rb = end.havePointB ? end.pointB : projectPoint(*body.edge(end.onB).curve, sb.row[2].p, end.windowB).point;
                double tA = end.tA, tB = end.tB;
                const BSplineCurve<3> contactA = piece.surface->vIsoCurve(0.0);
                const BSplineCurve<3> contactB = piece.surface->vIsoCurve(1.0);
                if (!end.havePointA) {
                    const CurveProjection aligned = projectPoint(contactA, ra, {piece.fitLo, piece.fitHi});
                    tA = aligned.parameter;
                }
                if (!end.havePointB) {
                    const CurveProjection aligned = projectPoint(contactB, rb, {piece.fitLo, piece.fitHi});
                    tB = aligned.parameter;
                }
                // Il coperchio puo' tagliare il contatto sul supporto comune
                // nella pezza precedente: la nuova pezza e' allora triangolare.
                int trimmedContact = -1, parentIndex = -1;
                double parentParameter = 0.0, splitParameter = 0.0, cutV = 0.0;
                Vec3 onSection;
                Cut parentCut, terminalCut;
                if (!end.free && !end.seam.valid() && body.face(end.face).surface->type() == SurfaceType::Plane) {
                    const int parent = end.atStart ? end.piece+1 : end.piece-1;
                    if (parent >= 0 && parent < pieceCount && pieces[std::size_t(parent)].chainFin == piece.chainFin) {
                        Piece &previous = pieces[std::size_t(parent)];
                        const bool changedA = previous.a != piece.a, changedB = previous.b != piece.b;
                        if (changedA != changedB) {
                            const int contact = changedB ? 0 : 1;
                            const EdgeId boundary = contact == 0 ? end.onA : end.onB;
                            const Interval window = contact == 0 ? end.windowA : end.windowB;
                            const Edge &edge = body.edge(boundary);
                            double best = 1e300, t = 0.0, s = 0.0;
                            const double approximate = contact == 0 ? tA : tB;
                            for (int k = 0; k <= 16; ++k) for (double seed : {window.lo, window.hi}) {
                                double x = previous.fitLo+(previous.fitHi-previous.fitLo)*k/16.0, y = seed;
                                if (!surfaceCurveMeet(*previous.surface, double(contact), *edge.curve, window, x, y, scale)) continue;
                                const Interval range = previous.rangeOf();
                                if (x < range.lo-1e-9 || x > range.hi+1e-9 || std::fabs(x-approximate) >= best) continue;
                                best = std::fabs(x-approximate); t = x; s = y;
                            }
                            const double split = end.atStart ? piece.to : piece.from;
                            const double outward = (piece.to > piece.from ? 1.0 : -1.0)*(end.atStart ? -1.0 : 1.0);
                            if (best < 1e300 && outward*(t-split) < -1e-9) {
                                const Plane &cap = static_cast<const Plane &>(*body.face(end.face).surface);
                                const auto residual = [&](double v) {
                                    return dot(piece.surface->point(split,v)-cap.frame().origin(),cap.frame().zDir());
                                };
                                double lo = 0.0, hi = 1.0, f = residual(lo);
                                if (f * residual(hi) < 0.0) {
                                    for (int k = 0; k < 60; ++k) {
                                        const double m = 0.5 * (lo + hi), g = residual(m);
                                        if (f * g <= 0.0) hi = m;
                                        else { lo = m; f = g; }
                                    }
                                    const double v = 0.5 * (lo + hi);
                                    const Vec3 x = piece.surface->point(split, v), terminal = edge.curve->point(s);
                                    const Interval alongParent{double(contact), v}, alongEnd{v, double(1-contact)};
                                    parentCut=traceBetween(*previous.surface,*body.face(end.face).surface,terminal,x,scale,
                                                           std::max(previous.fit.error,piece.fit.error),&alongParent);
                                    terminalCut=traceBetween(*piece.surface,*body.face(end.face).surface,x,contact==0?rb:ra,scale,
                                                             piece.fit.error,&alongEnd);
                                    trimmedContact = contact;
                                    parentIndex = parent; parentParameter = t;
                                    splitParameter = split; cutV = v; onSection = x;
                                    if (contact == 0) { ra = terminal; tA = t; }
                                    else { rb = terminal; tB = t; }
                                }
                            }
                        }
                    }
                }
                // I raccordi precedenti possono lasciare, presso il vertice,
                // un micro-edge tollerante con intervallo parametrico quasi
                // nullo. Non e' accorciabile ulteriormente: la nuova pezza
                // riusa il vertice comune e assorbe lo scarto nella propria
                // tolleranza geometrica.
                const auto microEdge = [&](EdgeId id) {
                    const Edge &edge = body.edge(id);
                    return edge.range.length() <= 1e-10 * std::max(1.0, std::fabs(edge.range.lo))
                        || distance(body.vertex(body.edgeStart(id)).point, body.vertex(body.edgeEnd(id)).point) <= 1e-3 * size;
                };
                const bool snapA = end.seam.valid() && microEdge(end.onA);
                const bool snapB = end.seam.valid() && microEdge(end.onB);
                if (snapA) ra = body.vertex(end.vertex).point;
                if (snapB) rb = body.vertex(end.vertex).point;
                const int oldVertex = model.vertexIndex.at(end.vertex.index);
                // Raccordo che svanisce: nel vertice l'angolo tra A e B va a
                // zero (la mitra di due raccordi uguali che nasce sulla faccia a
                // cui entrambi sono tangenti), la sezione si riduce a un punto e
                // i due contatti arrivano nel vertice stesso. Non c'e' una curva
                // di chiusura su E: il raccordo termina nel vertice, con la
                // tolleranza dello scarto misurato (la sezione vi e' mal
                // condizionata, vedi Section::noise).
                const Vec3 &vertexPoint = body.vertex(end.vertex).point;
                const double vanishLimit = std::max(1e-5 * size, 10.0 * piece.fit.error);
                const bool vanishing = !end.seam.valid() && distance(ra, vertexPoint) <= vanishLimit && distance(rb, vertexPoint) <= vanishLimit;
                junction.pointA = snapA || vanishing ? oldVertex : model.addPoint(ra);
                junction.pointB = snapB || vanishing ? oldVertex : model.addPoint(rb);
                if (trimmedContact != 0) setEnd(aLo, aHi, end.piece, !end.atStart, tA);
                if (trimmedContact != 1) setEnd(bLo, bHi, end.piece, !end.atStart, tB);
                // Una frontiera libera termina su una sezione trasversale
                // passante per i due contatti, senza creare una faccia tappo.
                SurfacePtr freeSupport;
                if (end.free && vanishing) freeSupport = piece.surface;
                else if (end.free) {
                    const Vec3 chord = normalized(rb-ra);
                    const Vec3 tangent = normalized(body.edge(chain.fins[std::size_t(piece.chainFin)].edge).curve->derivative(0.5*(tA+tB)));
                    freeSupport = std::make_shared<Plane>(Frame3(ra, tangent-dot(tangent,chord)*chord, chord));
                }
                const Surface &E = end.free ? *freeSupport : *body.face(end.face).surface;
                if (vanishing) {
                    double gap = std::max({distance(ra, vertexPoint), distance(rb, vertexPoint),
                                           distance(piece.surface->point(tA, 0.0), vertexPoint), distance(piece.surface->point(tB, 1.0), vertexPoint)});
                    if (gap > 1e-7 * scale)
                        model.pointTolerance[std::size_t(oldVertex)] = std::max(model.pointTolerance[std::size_t(oldVertex)], 1.01 * gap);
                } else if (trimmedContact >= 0) {
                    const std::size_t terminal = std::size_t(end.piece), parent = std::size_t(parentIndex);
                    Junction &middle = junctions[std::size_t(end.atStart ? end.piece+1 : end.piece)];
                    const int x = model.addPoint(onSection);
                    const int endpoint = trimmedContact == 0 ? junction.pointA : junction.pointB;
                    model.edgeAlive[std::size_t(middle.connector)] = false;
                    if (trimmedContact == 0) {
                        middle.pointA = x;
                        skipA[terminal] = true;
                        setEnd(aLo, aHi, parentIndex, !end.atStart, parentParameter);
                        (end.atStart ? aStartOverride : aEndOverride)[parent] = endpoint;
                    } else {
                        middle.pointB = x;
                        skipB[terminal] = true;
                        setEnd(bLo, bHi, parentIndex, !end.atStart, parentParameter);
                        (end.atStart ? bStartOverride : bEndOverride)[parent] = endpoint;
                    }
                    const double vLo = trimmedContact == 0 ? cutV : 0.0, vHi = trimmedContact == 0 ? 1.0 : cutV;
                    middle.connector = model.addEdge(middle.pointA, middle.pointB,
                        std::make_shared<BSplineCurve<3>>(piece.surface->uIsoCurve(splitParameter)), {vLo, vHi});
                    const double allowed = std::max(kLinearResolution, 0.01 * size);
                    if (std::max(parentCut.gap, terminalCut.gap) > allowed)
                        throw std::domain_error("blendEdges: chiusura sul coperchio oltre la tolleranza del raccordo");
                    const int remaining = trimmedContact == 0 ? junction.pointB : junction.pointA;
                    junction.connector = model.addEdge(x, remaining, terminalCut.curve, terminalCut.range,
                                                        terminalCut.gap > 1e-7 ? 2.0 * terminalCut.gap : 0.0);
                    const int cap = model.addEdge(endpoint, x, parentCut.curve, parentCut.range,
                                                  parentCut.gap > 1e-7 ? 2.0 * parentCut.gap : 0.0);
                    extraCaps[parent].push_back(cap);
                    edits[model.faceIndex.at(end.face.index)].free.push_back(cap);
                } else if (!end.seam.valid()) {
                    Cut kappa;
                    // Da A (v = 0) a B (v = 1), seguendo gli archi delle
                    // sezioni. La proiezione sulla corda puo' avere un estremo
                    // interno: tracciarla per prima produceva micro-campate
                    // rumorose, con SP-curve non ricostruibili. La corda resta
                    // un'alternativa quando la continuazione in v fallisce.
                    std::string failure;
                    bool traced = false;
                    try {
                        const Interval along{0.0, 1.0};
                        kappa = traceBetween(*piece.surface, E, ra, rb, scale, piece.fit.error, &along);
                        traced = kappa.sV.lo >= -1e-6 && kappa.sV.hi <= 1.0 + 1e-6;
                        if (!traced) failure = "blendEdges: curva di taglio fuori dal raccordo";
                    } catch (const std::domain_error &error) {
                        failure = error.what();
                    }
                    if (!traced) {
                        try {
                            kappa = traceBetween(*piece.surface, E, ra, rb, scale, piece.fit.error);
                            traced = kappa.sV.lo >= -1e-6 && kappa.sV.hi <= 1.0 + 1e-6;
                        } catch (const std::domain_error &) {
                        }
                    }
                    if (boundaryMode && (!traced || kappa.gap > std::max(kFitTolerance, piece.fit.error))) {
                        try {
                            const Interval along{0.0, 1.0};
                            Cut stable = traceBetween(*piece.surface, E, ra, rb, scale, piece.fit.error, &along, true);
                            if (stable.sV.lo >= -1e-6 && stable.sV.hi <= 1.0 + 1e-6) {
                                kappa = stable;
                                traced = true;
                            }
                        } catch (const std::domain_error &) {
                        }
                    }
                    if (boundaryMode && traced && kappa.gap > std::max(32 * kFitTolerance, 1e-3 * size))
                        throw std::domain_error("blendEdges: chiusura del raccordo non risolta con precisione sufficiente per questo raggio");
                    if (!traced) throw std::domain_error(std::string("blendEdges: chiusura dell'estremo: ") + failure);
                    junction.connector = kappa.forward ? model.addEdge(junction.pointA, junction.pointB, kappa.curve, kappa.range, kappa.gap > 1e-7 ? 2.0 * kappa.gap : 0.0)
                                                       : model.addEdge(junction.pointB, junction.pointA, kappa.curve, kappa.range, kappa.gap > 1e-7 ? 2.0 * kappa.gap : 0.0);
                } else {
                    // Pezza triangolare di Coons (il lato opposto al nuovo
                    // raccordo collassa nel vertice Q). I tre bordi cubici
                    // seguono le superfici adiacenti con le loro tangenti;
                    // la costruzione vale anche quando i raccordi terminali
                    // hanno un raggio diverso da quello nuovo.
                    const VertexId qVertex = body.edgeStart(end.seam) == end.vertex ? body.edgeEnd(end.seam) : body.edgeStart(end.seam);
                    const Vec3 q = body.vertex(qVertex).point;
                    // Cubiche di Hermite della traccia rettilinea nello spazio
                    // parametrico. Vicino ai poli o alle cuciture di una
                    // superficie di raccordo la parametrizzazione puo'
                    // accelerare molto: otto tratti uniformi trasformavano
                    // allora il bordo in una corda lontana oltre un millimetro
                    // dal supporto. Suddividi solo gli intervalli che non
                    // approssimano il proprio supporto entro la tolleranza.
                    const Surface &E = *body.face(end.face).surface;
                    const Surface &supportB = *body.face(end.secondFace).surface;
                    const SurfaceProjection ea = projectPoint(E, ra), eq = projectPoint(E, q);
                    const SurfaceProjection eb = projectPoint(supportB, rb), eq2 = projectPoint(supportB, q);
                    const double allowed = 0.01 * size;
                    const auto pathSample = [](const Surface &support, double u0, double v0, double u1, double v1,
                                               double f, Vec3 &p, Vec3 &dp) {
                        Vec3 d[4];
                        support.evaluate(u0 + f * (u1 - u0), v0 + f * (v1 - v0), 1, d);
                        p = d[0];
                        dp = (u1 - u0) * d[Surface::derivativeIndex(1, 0, 1)]
                           + (v1 - v0) * d[Surface::derivativeIndex(0, 1, 1)];
                    };
                    const auto segmentGap = [&](const Surface &support, double u0, double v0, double u1, double v1,
                                                double a, double b) {
                        Vec3 pa, pb, da, db;
                        pathSample(support, u0, v0, u1, v1, a, pa, da);
                        pathSample(support, u0, v0, u1, v1, b, pb, db);
                        const double h = b - a;
                        const Vec3 p1 = pa + (h / 3.0) * da, p2 = pb - (h / 3.0) * db;
                        double gap = 0.0;
                        for (double f : {0.25, 0.5, 0.75}) {
                            const double s = 1.0 - f;
                            const Vec3 point = (s * s * s) * pa + (3.0 * s * s * f) * p1
                                             + (3.0 * s * f * f) * p2 + (f * f * f) * pb;
                            gap = std::max(gap, projectPoint(support, point).distance);
                        }
                        return gap;
                    };
                    std::vector<double> breaks;
                    for (int k = 0; k <= 8; ++k) breaks.push_back(double(k) / 8.0);
                    const double pathTolerance = 0.2 * allowed;
                    for (int guard = 0; guard < 128; ++guard) {
                        int split = -1;
                        double worst = pathTolerance;
                        for (int k = 0; k + 1 < int(breaks.size()); ++k) {
                            const double a = breaks[std::size_t(k)], b = breaks[std::size_t(k + 1)];
                            const double gap = std::max({segmentGap(*piece.surface, tA, 0.0, tB, 1.0, a, b),
                                segmentGap(E, ea.u, ea.v, eq.u, eq.v, a, b),
                                segmentGap(supportB, eb.u, eb.v, eq2.u, eq2.v, a, b)});
                            if (gap > worst) worst = gap, split = k;
                        }
                        if (split < 0) break;
                        if (breaks.size() >= 65)
                            throw std::domain_error("blendEdges: bordo della pezza d'angolo troppo singolare");
                        breaks.insert(breaks.begin() + split + 1,
                                      0.5 * (breaks[std::size_t(split)] + breaks[std::size_t(split + 1)]));
                    }
                    std::vector<double> knots(4, 0.0);
                    for (std::size_t k = 1; k + 1 < breaks.size(); ++k)
                        for (int repeat = 0; repeat < 3; ++repeat) knots.push_back(breaks[k]);
                    knots.insert(knots.end(), 4, 1.0);
                    const auto surfacePath = [&](const Surface &support, double u0, double v0, double u1, double v1) {
                        std::vector<Vec3> control;
                        for (std::size_t k = 0; k + 1 < breaks.size(); ++k) {
                            const double a = breaks[k], b = breaks[k + 1];
                            Vec3 pa, pb, da, db;
                            pathSample(support, u0, v0, u1, v1, a, pa, da);
                            pathSample(support, u0, v0, u1, v1, b, pb, db);
                            if (k == 0) control.push_back(pa);
                            const double h = b - a;
                            control.push_back(pa + (h / 3.0) * da);
                            control.push_back(pb - (h / 3.0) * db);
                            control.push_back(pb);
                        }
                        return control;
                    };
                    std::vector<Vec3> c0 = surfacePath(*piece.surface, tA, 0.0, tB, 1.0);
                    std::vector<Vec3> ca = surfacePath(E, ea.u, ea.v, eq.u, eq.v);
                    std::vector<Vec3> cb = surfacePath(supportB, eb.u, eb.v, eq2.u, eq2.v);
                    c0.front() = ca.front() = ra;
                    c0.back() = cb.front() = rb;
                    ca.back() = cb.back() = q;
                    const int count = int(c0.size());
                    std::vector<double> parameters;
                    parameters.reserve(std::size_t(count));
                    for (int i = 0; i < count; ++i)
                        parameters.push_back((knots[std::size_t(i + 1)] + knots[std::size_t(i + 2)] + knots[std::size_t(i + 3)]) / 3.0);
                    std::vector<Vec3> poles;
                    poles.reserve(std::size_t(count * count));
                    for (int i = 0; i < count; ++i)
                        for (int j = 0; j < count; ++j) {
                            const double u = parameters[std::size_t(i)], v = parameters[std::size_t(j)], collapse = 1.0 - u;
                            const Vec3 ruled = (1.0 - v) * ca[std::size_t(i)] + v * cb[std::size_t(i)];
                            const Vec3 linear = (1.0 - v) * ra + v * rb;
                            poles.push_back(ruled + collapse * (c0[std::size_t(j)] - linear));
                        }
                    const auto surface = std::make_shared<BSplineSurface>(3, 3, knots, knots, count, count, poles, std::vector<double>());
                    const auto curve = [&](const std::vector<Vec3> &control) {
                        return std::make_shared<BSplineCurve<3>>(3, knots, control);
                    };
                    const auto curveGap = [&](const std::vector<Vec3> &control, const Surface &support) {
                        const BSplineCurve<3> candidate(3, knots, control);
                        double gap = 0.0;
                        for (int sample = 0; sample <= 32; ++sample)
                            gap = std::max(gap, projectPoint(support, candidate.point(sample / 32.0)).distance);
                        return gap;
                    };
                    const double gapNew = curveGap(c0, *piece.surface), gapA = curveGap(ca, E);
                    const double gapB = curveGap(cb, supportB);
                    // I punti di contatto possono appartenere a edge gia'
                    // tolleranti. Eredita soltanto lo scarto misurato agli
                    // estremi, e solo se coperto dalla tolleranza precedente.
                    const auto inheritedGap = [&](double gap, EdgeId edgeId, VertexId vertex) {
                        const double tolerance = std::max({kLinearResolution, body.edge(edgeId).tolerance,
                                                           body.vertex(vertex).tolerance});
                        return gap <= tolerance ? gap : 0.0;
                    };
                    const double allowedA = std::max({allowed,
                        inheritedGap(ea.distance, end.onA, end.vertex), inheritedGap(eq.distance, end.seam, qVertex)});
                    const double allowedB = std::max({allowed,
                        inheritedGap(eb.distance, end.onB, end.vertex), inheritedGap(eq2.distance, end.seam, qVertex)});
                    if (gapNew > allowed || gapA > allowedA + kLinearResolution || gapB > allowedB + kLinearResolution)
                        throw std::domain_error("blendEdges: la pezza d'angolo si discosta troppo dai raccordi adiacenti ("
                                                + std::to_string(gapNew) + ", " + std::to_string(gapA) + ", "
                                                + std::to_string(gapB) + "; limiti " + std::to_string(allowed) + ", "
                                                + std::to_string(allowedA) + ", " + std::to_string(allowedB) + ")");
                    const int pointQ = model.vertexIndex.at(qVertex.index);
                    junction.connector = model.addEdge(junction.pointA, junction.pointB, curve(c0), {0, 1}, 1.01 * gapNew);
                    const int first = model.addEdge(junction.pointA, pointQ, curve(ca), {0, 1}, 1.01 * gapA);
                    const int second = model.addEdge(junction.pointB, pointQ, curve(cb), {0, 1}, 1.01 * gapB);
                    const int oldSeam = model.edgeIndex.at(end.seam.index);
                    model.edgeAlive[std::size_t(oldSeam)] = false;
                    edits[model.faceIndex.at(end.face.index)].removed.insert(oldSeam);
                    edits[model.faceIndex.at(end.secondFace.index)].removed.insert(oldSeam);
                    edits[model.faceIndex.at(end.face.index)].free.push_back(first);
                    edits[model.faceIndex.at(end.secondFace.index)].free.push_back(second);
                    const Edge &oldSeamEdge = body.edge(end.seam);
                    const FinId seamOnFirst = body.finFace(oldSeamEdge.forward) == end.face ? oldSeamEdge.forward : oldSeamEdge.backward;
                    const bool firstFollowsOld = body.edgeStart(end.seam) == end.vertex;
                    const bool firstSenseInOldFace = firstFollowsOld ? body.fin(seamOnFirst).sense : !body.fin(seamOnFirst).sense;
                    Body::BuildFace corner;
                    corner.surface = surface;
                    // Allinea la normale geometrica al verso del loop
                    // triangolare nel dominio (u,v).
                    corner.sense = !firstSenseInOldFace;
                    const int cornerFace = int(model.faces.size());
                    model.addGeneratedFace(std::move(corner), EntityKind::Vertex, end.vertex.index);
                    FaceEdit cornerEdit;
                    // Il triangolo percorre il primo lato nel verso opposto
                    // alla faccia che prima usava la cucitura rimossa.
                    const auto pcurve = [](const Vec2 &origin, const Vec2 &direction) {
                        return std::make_shared<Line<2>>(origin, direction);
                    };
                    cornerEdit.fixed.push_back({junction.connector, firstSenseInOldFace,
                                                pcurve(Vec2(0, 0), Vec2(0, 1)), 0.0});
                    cornerEdit.fixed.push_back({first, !firstSenseInOldFace,
                                                pcurve(Vec2(0, 0), Vec2(1, 0)), 0.0});
                    cornerEdit.fixed.push_back({second, firstSenseInOldFace,
                                                pcurve(Vec2(0, 1), Vec2(1, 0)), 0.0});
                    newFaces.push_back({cornerFace, std::move(cornerEdit)});
                }
            }
            try {
                const int vertexA = model.vertexIndex.at(end.vertexA.index);
                if (junction.pointA != vertexA) model.moveEnd(model.edgeIndex.at(end.onA.index), vertexA, junction.pointA, true, &end.windowA);
            }
            catch (const std::domain_error &failure) {
                throw std::domain_error(std::string(failure.what()) + " (estremo A, bordo " + std::to_string(end.onA.index)
                                        + ", t=" + std::to_string(end.tA) + ")");
            }
            try {
                const int vertexB = model.vertexIndex.at(end.vertexB.index);
                if (junction.pointB != vertexB) model.moveEnd(model.edgeIndex.at(end.onB.index), vertexB, junction.pointB, true, &end.windowB);
            }
            catch (const std::domain_error &failure) {
                throw std::domain_error(std::string(failure.what()) + " (estremo B, bordo " + std::to_string(end.onB.index)
                                        + ", t=" + std::to_string(end.tB) + ")");
            }
            if (!end.free && !end.seam.valid() && junction.connector >= 0) edits[model.faceIndex.at(end.face.index)].free.push_back(junction.connector);
        }

        // Contatti, facce nuove e modifiche di A e delle facce B.
        for (const ChainFin &cf : chain.fins) {
            const int old = model.edgeIndex.at(cf.edge.index);
            edits[model.faceIndex.at(cf.a.index)].removed.insert(old);
            edits[model.faceIndex.at(cf.b.index)].removed.insert(old);
            model.edgeAlive[std::size_t(old)] = false;
        }
        for (int p = 0; p < pieceCount; ++p) {
            Piece &piece = pieces[std::size_t(p)];
            const ChainFin &cf = chain.fins[std::size_t(piece.chainFin)];
            const bool increasing = piece.to > piece.from;
            const Junction &startJ = junctions[std::size_t(p)];
            const Junction &endJ = junctions[std::size_t(chain.closed ? next(p) : p + 1)];
            // Punti di inizio e fine dei contatti nel verso della catena.
            const bool startMitre = startJ.gamma >= 0, endMitre = endJ.gamma >= 0;
            const int aStart = aStartOverride[std::size_t(p)]>= 0 ? aStartOverride[std::size_t(p)] : startJ.pointA;
            const int aEnd = aEndOverride[std::size_t(p)]>= 0 ? aEndOverride[std::size_t(p)] : endJ.pointA;
            int bStart = bStartOverride[std::size_t(p)]>= 0 ? bStartOverride[std::size_t(p)] : startJ.pointB;
            int bEnd = bEndOverride[std::size_t(p)]>= 0 ? bEndOverride[std::size_t(p)] : endJ.pointB;
            if (startMitre && startJ.deep == 1) bStart = startJ.deepPoint;  // questo pezzo e' il profondo del giunto
            if (endMitre && endJ.deep == 0) bEnd = endJ.deepPoint;
            if (startMitre && startJ.deep == -1) bStart = startJ.deepPoint;
            if (endMitre && endJ.deep == -1) bEnd = endJ.deepPoint;
            const Interval ra{aLo[std::size_t(p)], aHi[std::size_t(p)]}, rb{bLo[std::size_t(p)], bHi[std::size_t(p)]};
            if ((!skipA[std::size_t(p)] && !(ra.hi > ra.lo)) || (!skipB[std::size_t(p)] && !(rb.hi > rb.lo)))
                throw std::domain_error("blendEdges: tratto interamente consumato dai raccordi vicini; "
                                        "serve ricostruire il contatto oltre la piccola faccia (bordo "
                                        + std::to_string(cf.edge.index) + ")");
            const double fitSlack = piece.fit.error > 1e-8 ? 2.0 * piece.fit.error : 0.0;
            const double slackA = std::max({fitSlack, startJ.toleranceA, endJ.toleranceA});
            const double slackB = std::max({fitSlack, startJ.toleranceB, endJ.toleranceB});
            if (!skipA[std::size_t(p)]) piece.contactA = increasing ? model.addEdge(aStart, aEnd, std::make_shared<BSplineCurve<3>>(piece.surface->vIsoCurve(0.0)), ra, slackA)
                                        : model.addEdge(aEnd, aStart, std::make_shared<BSplineCurve<3>>(piece.surface->vIsoCurve(0.0)), ra, slackA);
            if (!skipB[std::size_t(p)]) piece.contactB = increasing ? model.addEdge(bStart, bEnd, std::make_shared<BSplineCurve<3>>(piece.surface->vIsoCurve(1.0)), rb, slackB)
                                        : model.addEdge(bEnd, bStart, std::make_shared<BSplineCurve<3>>(piece.surface->vIsoCurve(1.0)), rb, slackB);
            if (piece.contactA >= 0) edits[model.faceIndex.at(piece.a.index)].fixed.push_back({piece.contactA, cf.sense, nullptr, 0.0});
            FaceEdit &editB = edits[model.faceIndex.at(piece.b.index)];
            if (piece.contactB >= 0) editB.fixed.push_back({piece.contactB, !cf.sense, nullptr, 0.0});
            // Verso: normale uscente dal centro della palla sui convessi (materiale tolto), verso il centro sui concavi.
            const Interval r = piece.rangeOf();
            const double middle = 0.5 * (r.lo + r.hi);
            const Section &s = piece.blend->section(middle);
            Vec3 d[4];
            piece.surface->evaluate(middle, 0.5, 1, d);
            const Vec3 normal = cross(d[Surface::derivativeIndex(1, 0, 1)], d[Surface::derivativeIndex(0, 1, 1)]);
            const Vec3 outward = chamfer ? body.edge(cf.edge).curve->point(middle) - d[0] : (convex ? d[0] - s.center : s.center - d[0]);
            const bool faceSense = (dot(normal, outward) > 0.0) == (chamfer ? convex : true);
            Body::BuildFace face;
            face.surface = piece.surface;
            face.sense = faceSense;
            piece.face = int(model.faces.size());
            model.addGeneratedFace(std::move(face), EntityKind::Edge, edgeSources.count(cf.edge.index) ? edgeSources.at(cf.edge.index) : cf.edge.index);
            FaceEdit edit;
            // I contatti sono isocurve della pezza: il loro parametro UV e'
            // noto esattamente, anche vicino a nodi multipli o tratti corti.
            if (piece.contactA >= 0) edit.fixed.push_back({piece.contactA, faceSense, std::make_shared<Line<2>>(Vec2(0,0),Vec2(1,0)), 0.0});
            if (piece.contactB >= 0) edit.fixed.push_back({piece.contactB, !faceSense, std::make_shared<Line<2>>(Vec2(0,1),Vec2(1,0)), 0.0});
            for (const Junction *j : {&startJ, &endJ}) {
                if (j->gamma >= 0) {
                    edit.free.push_back(j->gamma);
                    const bool thisIsDeep = (j == &startJ && j->deep == 1) || (j == &endJ && j->deep == 0);
                    if (thisIsDeep && j->delta >= 0) edit.free.push_back(j->delta);
                } else if (j->connector >= 0) {
                    edit.free.push_back(j->connector);
                }
            }
            for (int cap : extraCaps[std::size_t(p)]) edit.free.push_back(cap);
            newFaces.push_back({piece.face, std::move(edit)});
        }
        // Mitre: delta sta anche nel fianco del raccordo basso; i connettori dei giunti Split e dei vertici nelle facce B? No: solo nelle facce dei raccordi.
        for (int p = 0; p < (chain.closed ? pieceCount : pieceCount - 1); ++p) {
            const Junction &junction = junctions[std::size_t(chain.closed ? next(p) : p + 1)];
            if (junction.delta < 0) continue;
            const Piece &shallow = junction.deep == 1 ? pieces[std::size_t(p)] : pieces[std::size_t(next(p))];
            edits[model.faceIndex.at(shallow.b.index)].free.push_back(junction.delta);
        }
    }
    // Loop ricollegati per i vertici.
    const auto relink = [&](int face, std::vector<Body::BuildFin> fixed, const std::vector<int> &free) {
        std::vector<bool> fixedUsed(fixed.size(), false), freeUsed(free.size(), false);
        std::vector<std::vector<Body::BuildFin>> loops;
        const auto startOf = [&](const Body::BuildFin &f) { return f.sense ? model.edges[std::size_t(f.edge)].start : model.edges[std::size_t(f.edge)].end; };
        const auto endOf = [&](const Body::BuildFin &f) { return f.sense ? model.edges[std::size_t(f.edge)].end : model.edges[std::size_t(f.edge)].start; };
        while (true) {
            std::size_t first = fixed.size();
            for (std::size_t k = 0; k < fixed.size(); ++k)
                if (!fixedUsed[k]) {
                    first = k;
                    break;
                }
            if (first == fixed.size()) break;
            std::vector<Body::BuildFin> loop{fixed[first]};
            fixedUsed[first] = true;
            const int origin = startOf(fixed[first]);
            int at = endOf(fixed[first]);
            int guard = 0;
            while (at != origin) {
                if (++guard > 100000) throw std::domain_error("blendEdges: loop non ricollegato");
                int found = -1;
                for (std::size_t k = 0; k < fixed.size(); ++k)
                    if (!fixedUsed[k] && startOf(fixed[k]) == at) {
                        if (found >= 0) throw std::domain_error("blendEdges: loop ambiguo dopo il raccordo");
                        found = int(k);
                    }
                if (found >= 0) {
                    fixedUsed[std::size_t(found)] = true;
                    loop.push_back(fixed[std::size_t(found)]);
                    at = endOf(fixed[std::size_t(found)]);
                    continue;
                }
                bool linked = false;
                for (std::size_t k = 0; k < free.size() && !linked; ++k) {
                    if (freeUsed[k]) continue;
                    const Body::BuildEdge &e = model.edges[std::size_t(free[k])];
                    if (e.start == at || e.end == at) {
                        const bool sense = e.start == at;
                        freeUsed[k] = true;
                        loop.push_back({free[k], sense, nullptr, 0.0});
                        at = sense ? e.end : e.start;
                        linked = true;
                    }
                }
                if (!linked) throw std::domain_error("blendEdges: loop aperto dopo il raccordo (caso non gestito)");
            }
            loops.push_back(std::move(loop));
        }
        for (std::size_t k = 0; k < free.size(); ++k)
            if (!freeUsed[k]) throw std::domain_error("blendEdges: bordo del raccordo " + std::to_string(free[k])
                                                      + " non collegato nella faccia " + std::to_string(face));
        model.faces[std::size_t(face)].loops = std::move(loops);
    };
    for (auto &[face, edit] : edits) {
        std::vector<Body::BuildFin> fixed;
        for (const auto &loop : model.faces[std::size_t(face)].loops)
            for (const Body::BuildFin &fin : loop)
                if (!edit.removed.count(fin.edge)) fixed.push_back(fin);
        fixed.insert(fixed.end(), edit.fixed.begin(), edit.fixed.end());
        relink(face, fixed, edit.free);
    }
    for (auto &[face, edit] : newFaces) relink(face, edit.fixed, edit.free);
    BodyHistory declared;
    Body result = model.build(history ? &declared : nullptr);
    computePCurves(result);
    // Le curve di mitra tra due raccordi su pezze B-spline quasi singolari
    // sono approssimate. Registra sul B-rep lo scarto effettivo, come edge
    // tollerante, invece di scartare un raccordo topologicamente valido. Un
    // limite relativo al raggio impedisce di nascondere costruzioni errate.
    for (EdgeId edgeId : result.edges()) {
        Edge &edge = result.edge(edgeId);
        double gap = std::max(distance(result.vertex(result.edgeStart(edgeId)).point, edge.curve->point(edge.range.lo)),
                              distance(result.vertex(result.edgeEnd(edgeId)).point, edge.curve->point(edge.range.hi)));
        for (FinId fin : {edge.forward, edge.backward}) {
            if (!fin.valid()) continue;
            const Surface &surface = *result.face(result.finFace(fin)).surface;
            for (int sample = 0; sample <= 16; ++sample) {
                const Vec3 point = edge.curve->point(edge.range.lo + edge.range.length() * sample / 16.0);
                gap = std::max(gap, projectPoint(surface, point).distance);
            }
        }
        const double admissible = (approximateCross ? 0.05 : 0.01) * size;
        if (gap > std::max(kLinearResolution, edge.tolerance) && gap <= admissible)
            edge.tolerance = 1.01 * gap;
    }
    // Le SP-curve di un contatto approssimato possono essere state rifiutate
    // prima di conoscere lo scarto misurato. Completa quelle mancanti con la
    // tolleranza dell'edge: senza di loro il solido passa checkBody ma alcune
    // facce non sono triangolabili nell'anteprima e nel risultato finale.
    if (computePCurves(result) != 0) {
        // Distingui un contatto oltre il dominio del supporto da una sola
        // difficolta' numerica della SP-curve: quel caso richiede un altro
        // passaggio di faccia, non una tolleranza geometrica piu' larga.
        for (FaceId face : result.faces()) for (LoopId loop : result.face(face).loops)
            for (FinId fin : result.loopFins(loop)) {
                if (result.fin(fin).pcurve) continue;
                const Edge &edge = result.edge(result.fin(fin).edge);
                const double tolerance = std::max(kPCurveTolerance, edge.tolerance);
                const auto breaks = edge.curve->breakpoints(edge.range);
                for (std::size_t k = 1; k < breaks.size(); ++k) for (double f : {0.25, 0.5, 0.75}) {
                    const Vec3 point = edge.curve->point(breaks[k-1] + f*(breaks[k]-breaks[k-1]));
                    if (projectPoint(*result.face(face).surface, point).distance > tolerance)
                        throw std::domain_error("blendEdges: il contatto esce dal dominio del fianco F"
                            + std::to_string(face.index) + "; il passaggio su un altro supporto non e' ancora gestito per questo raggio");
                }
            }
        throw std::domain_error("blendEdges: impossibile ricostruire tutti i contorni del raccordo entro tolleranza; "
                                "il risultato non sarebbe completamente visualizzabile");
    }
    for (FaceId f : result.faces())
        if (guidedCaps.count(result.face(f).surface.get())) checkGuidedCap(result, f, scale);
    CheckOptions checks;
    checks.loopCrossings = true;  // un raccordo che invade un altro contorno della faccia
    const std::vector<CheckIssue> issues = checkBody(result, checks);
    if (!issues.empty()) throw std::domain_error("blendEdges: raccordo non valido (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    if (history) *history = std::move(declared);
    return result;
}

}  // namespace

Body blendSurfaceChains(const Body &input, const std::vector<EdgeId> &selected, double size, bool chamfer,
                        const std::vector<ChamferSides> *sides, BodyHistory *history) {
    try {
        return blendSurfaceChainsWithGuide(input, selected, size, chamfer, sides, history, false);
    } catch (const std::domain_error &) {
        std::exception_ptr failure = std::current_exception();
        bool guided = false, boundary = false;
        if (!chamfer) for (EdgeId e : selected) {
            if (input.isLaminar(e)) continue;
            const auto a = input.face(input.finFace(input.edge(e).forward)).surface->type();
            const auto b = input.face(input.finFace(input.edge(e).backward)).surface->type();
            guided = guided || (a == SurfaceType::Plane && b == SurfaceType::BSpline) || (b == SurfaceType::Plane && a == SurfaceType::BSpline);
            boundary = boundary || input.edge(e).curve->type() == CurveType::BSpline;
        }
        if (guided) {
            try {
                return blendSurfaceChainsWithGuide(input, selected, size, chamfer, sides, history, true);
            } catch (const std::domain_error &) {
                failure = std::current_exception();
            }
        }
        // Cuciture tangenti e bordi terminali oltre il vecchio trim:
        // tentativo separato, mantenendo invariati i risultati precedenti.
        if (boundary) return blendSurfaceChainsWithGuide(input, selected, size, chamfer, sides, history, false, true);
        std::rethrow_exception(failure);
    }
}

}
