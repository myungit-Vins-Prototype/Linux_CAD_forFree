#include "fk_offset.h"

#include <algorithm>
#include <optional>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>

#include "fk_bspline_basis.h"
#include "fk_boolean.h"
#include "fk_bspline_surface.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_exchange.h"
#include "fk_intersect.h"
#include "fk_parallel.h"
#include "fk_pcurve.h"
#include "fk_surface.h"
#include "fk_sheet.h"
#include "fk_sew.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"
#include "fk_timing.h"

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

// Superficie a distanza O = S + d N in (u, v) con le derivate esatte O_u, O_v
// e O_uv (dalle derivate di S fino al terzo ordine). Le derivate si prendono
// appena dentro la cella (side +1/-1 per direzione): sulle linee di nodo le
// derivate di ordine alto di S cambiano da una parte all'altra.
struct OffsetJet {
    Vec3 p, pu, pv, puv;
};

OffsetJet offsetJet(const Surface &surface, double d, double u, double v, int su, int sv, double eu, double ev) {
    OffsetJet jet;
    Vec3 s[16];
    surface.evaluate(u, v, 0, s);
    const Vec3 point = s[0];
    for (Vec3 &x : s) x = Vec3();
    surface.evaluate(u + su * eu, v + sv * ev, 3, s);
    const auto at = [&](int k, int l) { return s[Surface::derivativeIndex(k, l, 3)]; };
    const Vec3 Su = at(1, 0), Sv = at(0, 1), Suu = at(2, 0), Suv = at(1, 1), Svv = at(0, 2), Suuv = at(2, 1), Suvv = at(1, 2);
    const Vec3 n = cross(Su, Sv), nu = cross(Suu, Sv) + cross(Su, Suv), nv = cross(Suv, Sv) + cross(Su, Svv);
    const Vec3 nuv = cross(Suuv, Sv) + cross(Suu, Svv) + cross(Su, Suvv);
    const double length = norm(n);
    if (!(length > 0.0)) throw std::domain_error("offset: punto singolare della superficie B-spline (normale nulla)");
    const double r = 1.0 / length, r3 = r * r * r;
    const double a = dot(n, nu), b = dot(n, nv);
    const double ru = -a * r3, rv = -b * r3;
    const double ruv = -(dot(nv, nu) + dot(n, nuv)) * r3 + 3.0 * a * b * r3 * r * r;
    const Vec3 N = n * r;
    Vec3 nAt;
    {
        Vec3 t[4];
        surface.evaluate(u, v, 1, t);
        const Vec3 m = cross(t[Surface::derivativeIndex(1, 0, 1)], t[Surface::derivativeIndex(0, 1, 1)]);
        nAt = norm(m) > 0.0 ? m / norm(m) : N;
    }
    jet.p = point + d * nAt;
    jet.pu = Su + d * (nu * r + n * ru);
    jet.pv = Sv + d * (nv * r + n * rv);
    jet.puv = Suv + d * (nuv * r + nu * rv + nv * ru + n * ruv);
    return jet;
}

class InternalNormalDiscontinuity final : public std::domain_error {
public:
    using std::domain_error::domain_error;
};

// Un loft puo' avere nodi C0 dentro una singola faccia. Un salto di normale
// produce due offset distinti: nessuna griglia continua puo' approssimarli.
// Valutiamo i due limiti sulle pezze di Bezier, senza epsilon che confondano
// uno spigolo con una zona liscia molto curva o con una campata stretta.
// Linee di nodo (u = costante se il primo e' vero) dove la normale salta
// dentro la finestra; `first` si ferma alla prima.
std::vector<std::pair<bool, double>> creasedKnotLines(const BSplineSurface &surface, double d, const Interval &uw, const Interval &vw, double tolerance,
                                                      bool first = false) {
    std::vector<std::pair<bool, double>> found;
    const auto sharp = surface.cachedSharpKnotLines();
    if (sharp->u.empty() && sharp->v.empty()) return found;
    const auto patches = surface.cachedBezierPatches();
    const auto us = surface.uBreakpoints(surface.uDomain()), vs = surface.vBreakpoints(surface.vDomain());
    const std::size_t nu = us.size() - 1, nv = vs.size() - 1;
    const auto check = [&](const BSplineSurface &a, const BSplineSurface &b, bool fixedU, double knot, Interval range) {
        const Interval window = fixedU ? uw : vw;
        if (!(knot > window.lo && knot < window.hi)) return;
        if (std::find(found.begin(), found.end(), std::make_pair(fixedU, knot)) != found.end()) return;
        if (first && !found.empty()) return;
        const Interval transverse = fixedU ? vw : uw;
        range.lo = std::max(range.lo, transverse.lo), range.hi = std::min(range.hi, transverse.hi);
        if (!(range.hi > range.lo)) return;
        for (double fraction : {0.125, 0.25, 0.5, 0.75, 0.875}) {
            const double t = range.lo + fraction * range.length();
            Vec3 na, nb;
            try {
                na = a.normal(fixedU ? knot : t, fixedU ? t : knot);
                nb = b.normal(fixedU ? knot : t, fixedU ? t : knot);
            } catch (const std::domain_error &) { continue; } // i poli si trattano nel calcolo della superficie
            const double jump = norm(na - nb);
            if (jump > 1e-7 && std::fabs(d) * jump > 10.0 * tolerance) {
                found.emplace_back(fixedU, knot);
                return;
            }
        }
    };
    for (std::size_t i = 0; i < nu; ++i)
        for (std::size_t j = 0; j < nv; ++j) {
            if (i + 1 < nu && std::find(sharp->u.begin(), sharp->u.end(), us[i + 1]) != sharp->u.end())
                check((*patches)[i * nv + j], (*patches)[(i + 1) * nv + j], true, us[i + 1], {vs[j], vs[j + 1]});
            if (j + 1 < nv && std::find(sharp->v.begin(), sharp->v.end(), vs[j + 1]) != sharp->v.end())
                check((*patches)[i * nv + j], (*patches)[i * nv + j + 1], false, vs[j + 1], {us[i], us[i + 1]});
        }
    return found;
}

void requireContinuousNormals(const BSplineSurface &surface, double d, const Interval &uw, const Interval &vw, double tolerance) {
    const auto found = creasedKnotLines(surface, d, uw, vw, tolerance, true);
    if (!found.empty())
        throw InternalNormalDiscontinuity("offset: discontinuita della normale interna alla faccia lungo "
            + std::string(found.front().first ? "u=" : "v=") + std::to_string(found.front().second)
            + "; dividere la faccia sugli spigoli interni o ricostruire il loft con continuita tangente");
}

// La superficie a distanza si ripiega dove d supera il raggio di curvatura
// dalla parte verso cui ci si sposta: lungo una direzione principale
// O_u = (1 - d k) S_u (k con il segno di S_uu . N). Campioni sulla finestra e
// sulle linee di nodo; senza, il fit convergeva su una superficie con le
// cuspidi e il rifilo della cucitura falliva con "vettore nullo".
void requireOffsetBelowCurvature(const Surface &surface, double d, const Interval &window, const Interval &vWindow) {
    // Solo dentro il dominio: la finestra puo' prolungare la superficie per il
    // rifilo della cucitura, e il polinomio estrapolato si curva di piu'
    // (quel tratto si taglia via).
    const Interval ud = surface.uDomain(), vd = surface.vDomain();
    const Interval uw{std::max(window.lo, ud.lo), std::min(window.hi, ud.hi)}, vw{std::max(vWindow.lo, vd.lo), std::min(vWindow.hi, vd.hi)};
    if (!(uw.lo < uw.hi && vw.lo < vw.hi)) return;
    std::vector<double> us = surface.uBreakpoints(uw), vs = surface.vBreakpoints(vw);
    for (int k = 0; k <= 64; ++k) us.push_back(uw.lo + uw.length() * k / 64.0), vs.push_back(vw.lo + vw.length() * k / 64.0);
    double worst = 0.0;
    for (double u : us)
        for (double v : vs) {
            Vec3 s[9];
            surface.evaluate(u, v, 2, s);
            const auto at = [&](int a, int b) { return s[Surface::derivativeIndex(a, b, 2)]; };
            const Vec3 su = at(1, 0), sv = at(0, 1), m = cross(su, sv);
            const double length = norm(m);
            if (!(length > 0.0)) continue;  // punti singolari: li tratta il fit
            const Vec3 n = m / length;
            const double E = dot(su, su), F = dot(su, sv), G = dot(sv, sv);
            const double L = dot(at(2, 0), n), M = dot(at(1, 1), n), N = dot(at(0, 2), n);
            const double area = E * G - F * F;
            if (!(area > 0.0)) continue;
            const double K = (L * N - M * M) / area, H = (E * N - 2.0 * F * M + G * L) / (2.0 * area);
            const double root = std::sqrt(std::max(0.0, H * H - K));
            for (double k : {H + root, H - root}) worst = std::max(worst, d * k);
        }
    // worst = |d| / raggio minimo dalla parte dello spostamento.
    if (worst >= 1.0) {
        std::ostringstream message;
        message.precision(4);
        message << "offset: distanza " << std::fabs(d) << " oltre il raggio di curvatura della faccia (minimo " << std::fabs(d) / worst
                << " da quella parte): la superficie a distanza si ripiega";
        throw std::domain_error(message.str());
    }
}

// Interpolazione cubica C2 con le derivate agli estremi (de Boor, "clamped"):
// nodi x_0..x_n semplici all'interno, quadrupli agli estremi; n + 3 poli,
// il primo e l'ultimo nei punti, il secondo e il penultimo dalle derivate.
// La matrice di collocazione delle B-spline e' totalmente positiva:
// eliminazione a banda senza pivot (de Boor, A Practical Guide to Splines).
std::vector<double> clampedKnots(const std::vector<double> &x) {
    std::vector<double> knots(4, x.front());
    knots.insert(knots.end(), x.begin() + 1, x.end() - 1);
    knots.insert(knots.end(), 4, x.back());
    return knots;
}

std::vector<Vec3> clampedCubic(const std::vector<double> &x, const std::vector<double> &knots, const std::vector<Vec3> &values,
                               const Vec3 &startSlope, const Vec3 &endSlope) {
    const std::size_t n = x.size() - 1;
    std::vector<Vec3> poles(n + 3);
    poles[0] = values.front();
    poles[1] = values.front() + ((x[1] - x[0]) / 3.0) * startSlope;
    poles[n + 2] = values.back();
    poles[n + 1] = values.back() - ((x[n] - x[n - 1]) / 3.0) * endSlope;
    if (n < 2) return poles;
    // Incognite: poli 2..n (n - 1). Riga r: il nodo x_{r+1}, al piu' tre poli non nulli.
    const std::size_t m = n - 1;
    std::vector<std::array<double, 3>> band(m);  // coefficienti dei poli r+1, r+2, r+3 (colonne r-1, r, r+1)
    std::vector<Vec3> rhs(m);
    const int poleCount = int(n + 3);
    for (std::size_t r = 0; r < m; ++r) {
        const double t = x[r + 1];
        const int span = detail::findSpan(knots, 3, poleCount, t);
        double basis[4];
        detail::basisFunctionDerivatives(knots, span, t, 3, 0, basis);
        band[r] = {0.0, 0.0, 0.0};
        rhs[r] = values[r + 1];
        for (int k = 0; k < 4; ++k) {
            const int pole = span - 3 + k;
            const double b = basis[k];
            if (b == 0.0) continue;
            if (pole < 2 || pole > int(n)) {
                rhs[r] -= b * poles[std::size_t(pole)];
                continue;
            }
            const int column = pole - 2;  // incognita
            const int offset = column - int(r) + 1;
            if (offset < 0 || offset > 2) throw std::logic_error("offset: interpolazione cubica fuori banda");
            band[r][std::size_t(offset)] = b;
        }
    }
    // Thomas (tridiagonale: sotto, diagonale, sopra).
    for (std::size_t r = 1; r < m; ++r) {
        const double factor = band[r][0] / band[r - 1][1];
        band[r][1] -= factor * band[r - 1][2];
        rhs[r] -= factor * rhs[r - 1];
    }
    std::vector<Vec3> unknown(m);
    unknown[m - 1] = rhs[m - 1] / band[m - 1][1];
    for (std::size_t r = m - 1; r-- > 0;) unknown[r] = (rhs[r] - band[r][2] * unknown[r + 1]) / band[r][1];
    for (std::size_t r = 0; r < m; ++r) poles[r + 2] = unknown[r];
    return poles;
}

// Superficie a distanza di una B-spline come bicubica C2: interpolazione
// tensoriale dei punti di O = S + d N sulla griglia (derivate O_u, O_v, O_uv
// esatte solo sui bordi), divisa nei tratti in cui S e' liscia. Sulle linee di
// nodo di S (dove O e' solo G1) i nodi sono tripli e le derivate si prendono
// dalla parte di ciascun tratto, come nella Hermite. Rispetto alla Hermite
// (nodi interni tripli, 3 n + 1 poli per direzione) i poli sono circa un terzo
// per direzione: sulle superfici a distanza dei loft con la cucitura
// mantenuta erano oltre un milione. Griglia infittita finche' lo scarto da O
// nello stesso (u, v) e' sotto la tolleranza; nullptr se non converge (si
// ripiega sulla Hermite).
// Poli della Hermite oltre i quali si tiene la C2 (circa un terzo per direzione).
constexpr std::size_t kDenseOffsetPoles = 200000;

SurfacePtr offsetBSplineC2(const Surface &surface, double d, const Interval &uw, const Interval &vw, double tolerance) {
    requireContinuousNormals(static_cast<const BSplineSurface &>(surface), d, uw, vw, tolerance);
    const std::vector<double> uSegments = sortedBreaks(uw, surface.uBreakpoints(uw), 1e-9 * uw.length());
    const std::vector<double> vSegments = sortedBreaks(vw, surface.vBreakpoints(vw), 1e-9 * vw.length());
    const auto O = [&](double u, double v) { return surface.point(u, v) + d * surface.normal(u, v); };
    std::vector<double> us = uSegments, vs = vSegments;
    // Nodi della griglia per tratto: [inizio, fine) degli indici in us/vs.
    // La prima e l'ultima cella di ogni tratto restano cubiche di Hermite
    // (nodi tripli, derivate esatte ai due estremi): i prolungamenti delle
    // superfici (extendSheet, cucitura dell'offset) estrapolano proprio
    // quel polinomio, e con la sola derivata prima imposta al bordo la
    // seconda derivata dell'interpolante C2 li faceva divergere tra le due
    // meta' di un loft.
    const auto pieces = [](const std::vector<double> &grid, const std::vector<double> &segments) {
        std::vector<std::pair<std::size_t, std::size_t>> result;
        std::size_t start = 0;
        for (std::size_t k = 1; k < segments.size(); ++k) {
            const std::size_t end = std::size_t(std::lower_bound(grid.begin(), grid.end(), segments[k]) - grid.begin());
            if (end - start >= 3) {
                result.push_back({start, start + 1});
                result.push_back({start + 1, end - 1});
                result.push_back({end - 1, end});
            } else {
                result.push_back({start, end});
            }
            start = end;
        }
        return result;
    };
    const unsigned threads = threadCount(0);
    // Derivate di O nei nodi, conservate tra un giro e l'altro (la griglia
    // cresce soltanto): chiave = (u, v, lato in u, lato in v).
    using JetKey = std::tuple<double, double, int, int>;
    std::map<JetKey, OffsetJet> jetCache;
    const auto build = [&]() -> SurfacePtr {
        const auto uPieces = pieces(us, uSegments), vPieces = pieces(vs, vSegments);
        {
            // I nodi di ogni rettangolo, con il lato dei tratti; nuovi in parallelo.
            std::vector<std::pair<JetKey, std::pair<double, double>>> missing;
            std::set<JetKey> queued;
            for (const auto &[ua, ub] : uPieces)
                for (const auto &[va, vb] : vPieces) {
                    const double eu = 1e-9 * (us[ub] - us[ua]), ev = 1e-9 * (vs[vb] - vs[va]);
                    for (std::size_t i = ua; i <= ub; ++i)
                        for (std::size_t j = va; j <= vb; ++j) {
                            const JetKey key{us[i], vs[j], i == ub ? -1 : 1, j == vb ? -1 : 1};
                            if (!jetCache.count(key) && queued.insert(key).second) missing.push_back({key, {eu, ev}});
                        }
                }
            std::vector<OffsetJet> computed(missing.size());
            std::vector<std::string> failure(missing.size());
            parallelFor(missing.size(), threads, [&](std::size_t k) {
                const auto &[key, steps] = missing[k];
                try {
                    computed[k] = offsetJet(surface, d, std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key), steps.first, steps.second);
                } catch (const std::exception &e) {
                    failure[k] = e.what();
                }
            });
            for (const std::string &message : failure)
                if (!message.empty()) throw std::domain_error(message);
            for (std::size_t k = 0; k < missing.size(); ++k) jetCache.emplace(missing[k].first, computed[k]);
        }
        // Poli globali: tratti consecutivi condividono la riga del nodo triplo.
        std::vector<std::size_t> uStart, vStart;
        std::size_t uCount = 1, vCount = 1;
        for (const auto &[a, b] : uPieces) uStart.push_back(uCount - 1), uCount += (b - a) + 2;
        for (const auto &[a, b] : vPieces) vStart.push_back(vCount - 1), vCount += (b - a) + 2;
        std::vector<Vec3> poles(uCount * vCount);
        std::vector<std::string> failure(uPieces.size() * vPieces.size());
        parallelFor(uPieces.size() * vPieces.size(), threads, [&](std::size_t job) {
            const std::size_t pu = job / vPieces.size(), pv = job % vPieces.size();
            try {
                const auto [ua, ub] = uPieces[pu];
                const auto [va, vb] = vPieces[pv];
                const std::vector<double> x(us.begin() + std::ptrdiff_t(ua), us.begin() + std::ptrdiff_t(ub) + 1);
                const std::vector<double> y(vs.begin() + std::ptrdiff_t(va), vs.begin() + std::ptrdiff_t(vb) + 1);
                const std::size_t n = x.size() - 1, m = y.size() - 1;
                const auto jet = [&](std::size_t i, std::size_t j) -> const OffsetJet & {
                    return jetCache.at(JetKey{x[i], y[j], i == n ? -1 : 1, j == m ? -1 : 1});
                };
                const std::vector<double> xKnots = clampedKnots(x), yKnots = clampedKnots(y);
                // Righe in u dei punti, poi dei O_v sui due bordi in v, poi colonne in v.
                std::vector<std::vector<Vec3>> rows(m + 1);
                for (std::size_t j = 0; j <= m; ++j) {
                    std::vector<Vec3> values(n + 1);
                    for (std::size_t i = 0; i <= n; ++i) values[i] = jet(i, j).p;
                    rows[j] = clampedCubic(x, xKnots, values, jet(0, j).pu, jet(n, j).pu);
                }
                std::vector<Vec3> slopes[2];
                for (int side = 0; side < 2; ++side) {
                    const std::size_t j = side ? m : 0;
                    std::vector<Vec3> values(n + 1);
                    for (std::size_t i = 0; i <= n; ++i) values[i] = jet(i, j).pv;
                    slopes[side] = clampedCubic(x, xKnots, values, jet(0, j).puv, jet(n, j).puv);
                }
                for (std::size_t k = 0; k < n + 3; ++k) {
                    std::vector<Vec3> values(m + 1);
                    for (std::size_t j = 0; j <= m; ++j) values[j] = rows[j][k];
                    const std::vector<Vec3> column = clampedCubic(y, yKnots, values, slopes[0][k], slopes[1][k]);
                    // Le righe comuni ai tratti le scrive il primo (stesse curve di bordo).
                    for (std::size_t l = 0; l < m + 3; ++l) {
                        if ((pu > 0 && k == 0) || (pv > 0 && l == 0)) continue;
                        poles[(uStart[pu] + k) * vCount + vStart[pv] + l] = column[l];
                    }
                }
            } catch (const std::exception &e) {
                failure[job] = e.what();
            }
        });
        for (const std::string &message : failure)
            if (!message.empty()) throw std::domain_error(message);
        const auto knots = [](const std::vector<double> &grid, const std::vector<std::pair<std::size_t, std::size_t>> &parts) {
            std::vector<double> result(4, grid.front());
            for (std::size_t p = 0; p < parts.size(); ++p) {
                for (std::size_t k = parts[p].first + 1; k < parts[p].second; ++k) result.push_back(grid[k]);
                if (p + 1 < parts.size()) result.insert(result.end(), 3, grid[parts[p].second]);
            }
            result.insert(result.end(), 4, grid.back());
            return result;
        };
        return std::make_shared<BSplineSurface>(3, 3, knots(us, uPieces), knots(vs, vPieces), int(uCount), int(vCount), std::move(poles));
    };
    // Punti di controllo di una cella (3 per lato interno ai lati e 3 x 3 dentro): i
    // valori di O non cambiano da un giro all'altro, solo l'interpolante.
    static constexpr double kQ[3] = {0.25, 0.5, 0.75};
    struct CellSamples {
        std::array<Vec3, 21> o;
    };
    std::map<std::array<double, 4>, CellSamples> sampleCache;
    const auto samplePoint = [&](std::size_t i, std::size_t j, int k, double &u, double &v) {
        // k: 0..5 lati u (q, 0/1), 6..11 lati v (0/1, q), 12..20 interno.
        double s, t;
        if (k < 6) s = kQ[k / 2], t = double(k % 2);
        else if (k < 12) s = double((k - 6) % 2), t = kQ[(k - 6) / 2];
        else s = kQ[(k - 12) / 3], t = kQ[(k - 12) % 3];
        u = us[i] + s * (us[i + 1] - us[i]);
        v = vs[j] + t * (vs[j + 1] - vs[j]);
    };
    for (int round = 0;; ++round) {
        const SurfacePtr fitted = build();
        const std::size_t nu = us.size() - 1, nv = vs.size() - 1;
        {
            std::vector<std::size_t> missing;
            std::vector<std::array<double, 4>> keys(nu * nv);
            for (std::size_t c = 0; c < nu * nv; ++c) {
                const std::size_t i = c / nv, j = c % nv;
                keys[c] = {us[i], us[i + 1], vs[j], vs[j + 1]};
                if (!sampleCache.count(keys[c])) missing.push_back(c);
            }
            std::vector<CellSamples> computed(missing.size());
            parallelFor(missing.size(), threads, [&](std::size_t m) {
                const std::size_t c = missing[m], i = c / nv, j = c % nv;
                for (int k = 0; k < 21; ++k) {
                    double u, v;
                    samplePoint(i, j, k, u, v);
                    computed[m].o[std::size_t(k)] = O(u, v);
                }
            });
            for (std::size_t m = 0; m < missing.size(); ++m) sampleCache.emplace(keys[missing[m]], computed[m]);
        }
        std::vector<int> split(nu * nv, 0);
        std::vector<std::string> failure(nu * nv);
        parallelFor(nu * nv, threads, [&](std::size_t c) {
            const std::size_t i = c / nv, j = c % nv;
            try {
                const CellSamples &exact = sampleCache.at({us[i], us[i + 1], vs[j], vs[j + 1]});
                const auto error = [&](int k) {
                    double u, v;
                    samplePoint(i, j, k, u, v);
                    return distance(fitted->point(u, v), exact.o[std::size_t(k)]);
                };
                double alongU = 0.0, alongV = 0.0, inside = 0.0;
                for (int k = 0; k < 6; ++k) alongU = std::max(alongU, error(k));
                for (int k = 6; k < 12; ++k) alongV = std::max(alongV, error(k));
                for (int k = 12; k < 21; ++k) inside = std::max(inside, error(k));
                if (inside <= tolerance && alongU <= tolerance && alongV <= tolerance) return;
                const int mask = (alongU > 0.5 * tolerance ? 1 : 0) | (alongV > 0.5 * tolerance ? 2 : 0);
                split[c] = mask ? mask : 3;
            } catch (const std::exception &e) {
                failure[c] = e.what();
            }
        });
        for (const std::string &message : failure)
            if (!message.empty()) throw std::domain_error(message);
        std::set<std::size_t> splitU, splitV;
        for (std::size_t c = 0; c < split.size(); ++c) {
            if (split[c] & 1) splitU.insert(c / nv);
            if (split[c] & 2) splitV.insert(c % nv);
        }
        if (splitU.empty() && splitV.empty()) {
            // Solo le superfici molto fitte: sulle altre la Hermite resta (i
            // prolungamenti e le giunzioni di extendSheet sulle meta' dei loft
            // sono verificati su quella; con la C2 la giunzione quasi tangente
            // di OffsetLoftExtendedSeamsTrimBoth non si approssima piu').
            if ((3 * nu + 1) * (3 * nv + 1) < kDenseOffsetPoles) return nullptr;
            return fitted;
        }
        // L'errore di un'interpolazione globale non dipende solo dalla cella:
        // pochi giri in piu' della Hermite, poi si rinuncia (la Hermite resta).
        if (round >= 20 || us.size() + splitU.size() > 4097 || vs.size() + splitV.size() > 4097
            || (us.size() + splitU.size()) * (vs.size() + splitV.size()) > 400000)
            return nullptr;
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
}

// Superficie a distanza di una B-spline: bicubica di Hermite a tratti sulla
// griglia dei nodi (infittita finche' lo scarto da O, nello stesso (u, v), e'
// sotto la tolleranza). In ogni nodo della griglia O, O_u, O_v e O_uv esatti
// dalla parte della cella: i poli sulle linee della griglia sono comuni alle
// celle vicine (le derivate lungo la linea sono le stesse), quelli interni
// sono della cella. Nodi interni tripli: C0 garantita, C1 dove O lo e'. Le
// celle si controllano in parallelo; si dividono solo nelle direzioni in cui
// sbagliano (lo scarto lungo i bordi della cella dice quale).
SurfacePtr offsetBSpline(const Surface &surface, double d, const Interval &uw, const Interval &vw, double tolerance) {
    requireContinuousNormals(static_cast<const BSplineSurface &>(surface), d, uw, vw, tolerance);
    const auto O = [&](double u, double v) { return surface.point(u, v) + d * surface.normal(u, v); };
    std::vector<double> us = sortedBreaks(uw, surface.uBreakpoints(uw), 1e-9 * uw.length());
    std::vector<double> vs = sortedBreaks(vw, surface.vBreakpoints(vw), 1e-9 * vw.length());
    using Cell = std::array<std::array<Vec3, 4>, 4>;
    const auto cellPoles = [&](std::size_t i, std::size_t j) {
        Cell poles;
        const double u0 = us[i], u1 = us[i + 1], v0 = vs[j], v1 = vs[j + 1];
        const double hu = u1 - u0, hv = v1 - v0;
        for (int cu = 0; cu < 2; ++cu)
            for (int cv = 0; cv < 2; ++cv) {
                const double u = cu ? u1 : u0, v = cv ? v1 : v0;
                const int su = cu ? -1 : 1, sv = cv ? -1 : 1;
                const OffsetJet jet = offsetJet(surface, d, u, v, su, sv, 1e-9 * hu, 1e-9 * hv);
                const int a = cu ? 3 : 0, b = cv ? 3 : 0, a2 = cu ? 2 : 1, b2 = cv ? 2 : 1;
                poles[a][b] = jet.p;
                poles[a2][b] = jet.p + (su * hu / 3.0) * jet.pu;
                poles[a][b2] = jet.p + (sv * hv / 3.0) * jet.pv;
                poles[a2][b2] = jet.p + (su * hu / 3.0) * jet.pu + (sv * hv / 3.0) * jet.pv + (su * sv * hu * hv / 9.0) * jet.puv;
            }
        return poles;
    };
    const auto evaluateCell = [](const Cell &poles, double s, double t) {
        Vec3 rows[4];
        for (int a = 0; a < 4; ++a) rows[a] = bezier(poles[a][0], poles[a][1], poles[a][2], poles[a][3], t);
        return bezier(rows[0], rows[1], rows[2], rows[3], s);
    };
    const unsigned threads = threadCount(0);
    for (int round = 0;; ++round) {
        const std::size_t nu = us.size() - 1, nv = vs.size() - 1;
        // 1: dividere lungo u, 2: lungo v (per cella); eccezioni raccolte per indice.
        std::vector<int> split(nu * nv, 0);
        std::vector<std::string> failure(nu * nv);
        parallelFor(nu * nv, threads, [&](std::size_t c) {
            const std::size_t i = c / nv, j = c % nv;
            try {
                const Cell poles = cellPoles(i, j);
                const auto error = [&](double s, double t) {
                    return distance(evaluateCell(poles, s, t), O(us[i] + s * (us[i + 1] - us[i]), vs[j] + t * (vs[j + 1] - vs[j])));
                };
                double alongU = 0.0, alongV = 0.0, inside = 0.0;
                for (double q : {0.25, 0.5, 0.75}) {
                    alongU = std::max({alongU, error(q, 0.0), error(q, 1.0)});
                    alongV = std::max({alongV, error(0.0, q), error(1.0, q)});
                    for (double t : {0.25, 0.5, 0.75}) inside = std::max(inside, error(q, t));
                }
                if (inside <= tolerance && alongU <= tolerance && alongV <= tolerance) return;
                int mask = (alongU > 0.5 * tolerance ? 1 : 0) | (alongV > 0.5 * tolerance ? 2 : 0);
                split[c] = mask ? mask : 3;
            } catch (const std::exception &e) {
                failure[c] = e.what();
            }
        });
        for (const std::string &message : failure)
            if (!message.empty()) throw std::domain_error(message);
        std::set<std::size_t> splitU, splitV;
        for (std::size_t c = 0; c < split.size(); ++c) {
            if (split[c] & 1) splitU.insert(c / nv);
            if (split[c] & 2) splitV.insert(c % nv);
        }
        if (splitU.empty() && splitV.empty()) break;
        if (round >= 16 || us.size() + splitU.size() > 4097 || vs.size() + splitV.size() > 4097
            || (us.size() + splitU.size()) * (vs.size() + splitV.size()) > 400000)
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
    parallelFor(nu * nv, threads, [&](std::size_t c) {
        const std::size_t i = c / nv, j = c % nv;
        const Cell cell = cellPoles(i, j);
        // Un solo proprietario per i poli sul confine: niente scritture
        // concorrenti e scelta deterministica della derivata al nodo.
        for (int a = 0; a < (i + 1 == nu ? 4 : 3); ++a)
            for (int b = 0; b < (j + 1 == nv ? 4 : 3); ++b) poles[(3 * i + std::size_t(a)) * std::size_t(vCount) + 3 * j + std::size_t(b)] = cell[a][b];
    });
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
    // Curva limitata: conta la curva di base (stesso parametro).
    const Curve<3> *basis = curve.get();
    while (basis->type() == CurveType::Trimmed) basis = static_cast<const TrimmedCurve<3> *>(basis)->basis().get();
    if (basis->type() == CurveType::Line) {
        const auto &line = static_cast<const Line<3> &>(*basis);
        const Vec3 origin = values.front() - ts.front() * line.direction();
        bool straight = true;
        for (std::size_t k = 0; k < ts.size() && straight; ++k) straight = distance(values[k], origin + ts[k] * line.direction()) <= eps;
        if (straight) return std::make_shared<Line<3>>(origin, line.direction());
    }
    if (basis->type() == CurveType::Circle) {
        const auto &circle = static_cast<const Circle<3> &>(*basis);
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

// Valutazione esatta S+dN per l'intersezione: due approssimazioni B-spline
// indipendenti possono spostare molto l'intersezione vicino alla tangenza.
void seamJet(const Surface &surface, double d, double u, double v, Vec3 *out) {
    if (d == 0.0) {
        surface.evaluate(u, v, 1, out);
        return;
    }
    Vec3 s[9];
    surface.evaluate(u, v, 2, s);
    const Vec3 su = s[Surface::derivativeIndex(1, 0, 2)], sv = s[Surface::derivativeIndex(0, 1, 2)];
    const Vec3 suu = s[Surface::derivativeIndex(2, 0, 2)], suv = s[Surface::derivativeIndex(1, 1, 2)], svv = s[Surface::derivativeIndex(0, 2, 2)];
    const Vec3 crossProduct = cross(su, sv);
    const Vec3 n = normalized(crossProduct);
    const double length = norm(crossProduct);
    const Vec3 nu = cross(suu, sv) + cross(su, suv), nv = cross(suv, sv) + cross(su, svv);
    out[0] = s[0] + d * n;
    out[Surface::derivativeIndex(1, 0, 1)] = su + d * (nu - dot(n, nu) * n) / length;
    out[Surface::derivativeIndex(0, 1, 1)] = sv + d * (nv - dot(n, nv) * n) / length;
    out[3] = Vec3();
}

// Proiezione locale sul prolungamento della superficie: conserva il ramo UV
// della faccia originale invece di saltare su un'altra parte del loft.
Vec2 projectLocal(const Surface &surface, const Vec3 &point, Vec2 uv, double offset = 0.0) {
    // Gauss-Newton con ricerca lineare: su una superficie a distanza molto
    // curva (offset verso l'interno vicino al raggio di curvatura) il passo
    // pieno usciva dal ramo e divergeva (u ~ 1e16).
    const auto distanceAt = [&](const Vec2 &at) {
        Vec3 jet[4];
        seamJet(surface, offset, at.x(), at.y(), jet);
        return norm(point - jet[0]);
    };
    double current = distanceAt(uv);
    for (int iteration = 0; iteration < 60; ++iteration) {
        Vec3 jet[4];
        seamJet(surface, offset, uv.x(), uv.y(), jet);
        const Vec3 a = jet[Surface::derivativeIndex(1, 0, 1)], b = jet[Surface::derivativeIndex(0, 1, 1)];
        const Vec3 r = point - jet[0];
        const double aa = dot(a, a), ab = dot(a, b), bb = dot(b, b), det = aa * bb - ab * ab;
        if (!(det > 1e-24 * aa * bb)) throw std::domain_error("offset: cucitura su una superficie singolare");
        const Vec2 step((bb * dot(a, r) - ab * dot(b, r)) / det, (aa * dot(b, r) - ab * dot(a, r)) / det);
        if (norm(step) <= 1e-12 * (1.0 + norm(uv))) break;
        double factor = 1.0;
        Vec2 next = uv + step;
        double trial = isFinite(next) ? distanceAt(next) : std::numeric_limits<double>::infinity();
        while (!(trial < current) && factor > 1e-6) {
            factor *= 0.5;
            next = uv + factor * step;
            trial = distanceAt(next);
        }
        // Nessun miglioramento: il punto e' gia' il piu' vicino (residuo normale).
        if (!(trial < current)) break;
        uv = next;
        current = trial;
        if (!isFinite(uv)) throw std::domain_error("offset: estensione della cucitura divergente");
    }
    return uv;
}

struct SeamSurface { SurfacePtr surface; Vec2 uv; double offset = 0.0; };

// Intersezione locale di due superfici (bordo) o delle facce incidenti a un
// vertice. Gram-Schmidt risolve le correzioni normali anche con facce tangenti.
Vec3 seamPoint(Vec3 point, std::vector<SeamSurface> surfaces, double tolerance, double reach) {
    const Vec3 start = point;
    for (int iteration = 0; iteration < 40; ++iteration) {
        Vec3 basis[3], correction;
        int rank = 0;
        double worst = 0.0;
        for (auto &entry : surfaces) {
            entry.uv = projectLocal(*entry.surface, point, entry.uv, entry.offset);
            Vec3 jet[4];
            seamJet(*entry.surface, entry.offset, entry.uv.x(), entry.uv.y(), jet);
            const Vec3 on = jet[0];
            const Vec3 n = normalized(cross(jet[Surface::derivativeIndex(1, 0, 1)], jet[Surface::derivativeIndex(0, 1, 1)]));
            const double residual = dot(n, on - point);
            worst = std::max(worst, std::fabs(residual));
            Vec3 independent = n;
            for (int k = 0; k < rank; ++k) independent -= dot(independent, basis[k]) * basis[k];
            const double magnitude = norm(independent);
            if (magnitude > 1e-6 && rank < 3) {
                basis[rank++] = independent / magnitude;
                correction += ((residual - dot(n, correction)) / magnitude) * basis[rank - 1];
            }
        }
        if (worst <= 0.001 * tolerance || (norm(correction) <= 0.001 * tolerance && worst <= tolerance)) return point;
        point += correction;
        if (!isFinite(point) || distance(point, start) > reach) break;
    }
    throw std::domain_error("offset: impossibile rifilare e cucire le facce adiacenti; ridurre la distanza o disattivare Mantieni la cucitura");
}

// Nei loft una faccia puo' attraversare nodi con normali discontinue.
// Esponiamo le pezze regolari come facce prima dell'offset, mantenendo la
// geometria originale. Solo facce che coprono il rettangolo UV completo:
// non si devono perdere fori o bordi di trim di una faccia generica.
bool fullSplineRectangle(const Body &body, FaceId id) {
    const Face &face = body.face(id);
    if (face.loops.size() != 1) return false;
    const Interval u = face.surface->uDomain(), v = face.surface->vDomain();
    const double eu = 1e-7 * u.length(), ev = 1e-7 * v.length();
    bool corners[4] = {};
    for (FinId fid : body.loopFins(face.loops.front())) {
        const Fin &fin = body.fin(fid);
        if (!fin.pcurve) return false;
        const Edge &edge = body.edge(fin.edge);
        for (int k = 0; k <= 16; ++k) {
            const Vec2 uv = fin.pcurve->point(edge.range.lo + edge.range.length() * k / 16.0);
            const bool left = std::fabs(uv.x() - u.lo) < eu, right = std::fabs(uv.x() - u.hi) < eu;
            const bool bottom = std::fabs(uv.y() - v.lo) < ev, top = std::fabs(uv.y() - v.hi) < ev;
            if (uv.x() < u.lo-eu || uv.x() > u.hi+eu || uv.y() < v.lo-ev || uv.y() > v.hi+ev
                || !(left || right || bottom || top)) return false;
            if (left && bottom) corners[0] = true;
            if (right && bottom) corners[1] = true;
            if (right && top) corners[2] = true;
            if (left && top) corners[3] = true;
        }
    }
    return corners[0] && corners[1] && corners[2] && corners[3];
}

bool splitOffsetFaces(const Body &body, const std::vector<FaceId> &faces, double d, double tolerance, Body &split) {
    std::set<int> divided;
    for (FaceId f : faces) {
        const auto &surface = body.face(f).surface;
        if (surface->type() != SurfaceType::BSpline || !fullSplineRectangle(body, f)) continue;
        try {
            requireContinuousNormals(static_cast<const BSplineSurface &>(*surface), d, surface->uDomain(), surface->vDomain(), tolerance);
        } catch (const InternalNormalDiscontinuity &) {
            divided.insert(f.index);
        }
    }
    if (divided.empty()) return false;
    std::vector<Body> sheets;
    std::vector<FaceId> unchanged;
    std::set<int> seen;
    for (FaceId f : faces) {
        if (!seen.insert(f.index).second) continue;
        if (!divided.count(f.index)) { unchanged.push_back(f); continue; }
        const auto &source = static_cast<const BSplineSurface &>(*body.face(f).surface);
        for (const BSplineSurface &patch : *source.cachedBezierPatches()) {
            const Interval u = patch.uDomain(), v = patch.vDomain();
            std::vector<Vec3> points;
            for (Vec2 uv : {Vec2(u.lo,v.lo), Vec2(u.hi,v.lo), Vec2(u.hi,v.hi), Vec2(u.lo,v.hi)})
                points.push_back(patch.point(uv.x(),uv.y()));
            const std::vector<Body::BuildEdge> edges{{0,1,patch.vIso(v.lo),u,0}, {1,2,patch.uIso(u.hi),v,0},
                {3,2,patch.vIso(v.hi),u,0}, {0,3,patch.uIso(u.lo),v,0}};
            Body::BuildFace face;
            face.surface = std::make_shared<BSplineSurface>(patch);
            face.sense = body.face(f).sense;
            face.loops = {{{0,true,std::make_shared<Line<2>>(Vec2(0,v.lo),Vec2(1,0)),0},
                {1,true,std::make_shared<Line<2>>(Vec2(u.hi,0),Vec2(0,1)),0},
                {2,false,std::make_shared<Line<2>>(Vec2(0,v.hi),Vec2(1,0)),0},
                {3,false,std::make_shared<Line<2>>(Vec2(u.lo,0),Vec2(0,1)),0}}};
            if (!face.sense) {
                std::reverse(face.loops.front().begin(), face.loops.front().end());
                for (auto &fin : face.loops.front()) fin.sense = !fin.sense;
            }
            sheets.push_back(Body::buildSheet(points, edges, {face}));
        }
    }
    if (!unchanged.empty()) sheets.push_back(facesAsSheet(body, unchanged));
    std::vector<const Body *> parts;
    for (const Body &sheet : sheets) parts.push_back(&sheet);
    split = sewSheets(parts, tolerance, false).body;
    return true;
}

// Tratti di una B-spline tra le linee di nodo `cuts` nella direzione u
// (fixedU) o v, dove la superficie e' C0 (nodo di molteplicita' pari almeno al
// grado): poli e nodi del tratto, stesso parametro. Vuoto se un taglio non
// ha la molteplicita' richiesta.
std::vector<std::pair<Interval, SurfacePtr>> splitAtC0Knots(const BSplineSurface &s, bool fixedU, std::vector<double> cuts) {
    const int p = fixedU ? s.uDegree() : s.vDegree();
    const std::vector<double> &K = fixedU ? s.uKnots() : s.vKnots();
    const Interval domain = fixedU ? s.uDomain() : s.vDomain();
    std::sort(cuts.begin(), cuts.end());
    std::vector<double> bounds{domain.lo};
    for (double c : cuts) {
        if (!(c > domain.lo && c < domain.hi)) continue;
        if (std::count(K.begin(), K.end(), c) < p) return {};
        bounds.push_back(c);
    }
    bounds.push_back(domain.hi);
    std::vector<std::pair<Interval, SurfacePtr>> result;
    const int nu = s.uPoleCount(), nv = s.vPoleCount();
    for (std::size_t b = 0; b + 1 < bounds.size(); ++b) {
        const double a = bounds[b], z = bounds[b + 1];
        std::vector<double> knots(std::size_t(p + 1), a);
        for (double k : K)
            if (k > a && k < z) knots.push_back(k);
        knots.insert(knots.end(), std::size_t(p + 1), z);
        const int count = int(knots.size()) - p - 1;
        int start = 0;
        if (b > 0) {
            int last = -1;
            for (int i = 0; i < int(K.size()); ++i)
                if (K[std::size_t(i)] == a) last = i;
            start = last - p;
        }
        std::vector<Vec3> poles;
        std::vector<double> weights;
        const int su = fixedU ? count : nu, sv = fixedU ? nv : count;
        for (int i = 0; i < su; ++i)
            for (int j = 0; j < sv; ++j) {
                const int pi = fixedU ? start + i : i, pj = fixedU ? j : start + j;
                poles.push_back(s.pole(pi, pj));
                if (s.isRational()) weights.push_back(s.weight(pi, pj));
            }
        auto piece = fixedU ? std::make_shared<BSplineSurface>(s.uDegree(), s.vDegree(), knots, s.vKnots(), su, sv, std::move(poles), std::move(weights))
                            : std::make_shared<BSplineSurface>(s.uDegree(), s.vDegree(), s.uKnots(), knots, su, sv, std::move(poles), std::move(weights));
        result.emplace_back(Interval{a, z}, piece);
    }
    return result;
}

// Facce B-spline rifilate con pieghe interne (una linea di nodo dove la
// normale salta: lo sweep lungo una spline solo C1, lontano dal percorso):
// le isoparametriche esatte delle pieghe, nei tratti dentro la faccia, si
// imprimono (imprintCurves) e la faccia si divide. Le pieghe diventano edge
// tra facce, che l'offset tratta come gli altri spigoli. `split` comprende le
// sole facce scelte; falso se non c'e' niente da dividere.
bool splitTrimmedCreases(const Body &body, const std::vector<FaceId> &faces, double d, double tolerance, Body &split) {
    std::set<int> chosen;
    for (FaceId f : faces) chosen.insert(f.index);
    const bool all = body.isSheet() && chosen.size() == body.faces().size();
    Body sheet = all ? body : facesAsSheet(body, faces);
    computePCurves(sheet);
    std::vector<ImprintCurve> curves;
    std::map<const BSplineSurface *, std::vector<std::pair<bool, double>>> creases;
    for (FaceId f : sheet.faces()) {
        const auto &surface = sheet.face(f).surface;
        if (surface->type() != SurfaceType::BSpline || fullSplineRectangle(sheet, f)) continue;
        // Finestra (u, v) dei loop.
        Interval uw{1e300, -1e300}, vw{1e300, -1e300};
        for (LoopId l : sheet.face(f).loops)
            for (FinId fin : sheet.loopFins(l)) {
                const Fin &data = sheet.fin(fin);
                if (!data.pcurve) continue;
                const Edge &edge = sheet.edge(data.edge);
                for (int k = 0; k <= 32; ++k) {
                    const Vec2 uv = data.pcurve->point(edge.range.lo + edge.range.length() * k / 32.0);
                    uw.lo = std::min(uw.lo, uv.x()), uw.hi = std::max(uw.hi, uv.x()), vw.lo = std::min(vw.lo, uv.y()), vw.hi = std::max(vw.hi, uv.y());
                }
            }
        if (!(uw.lo < uw.hi && vw.lo < vw.hi)) continue;
        const auto &spline = static_cast<const BSplineSurface &>(*surface);
        for (const auto &[fixedU, knot] : creasedKnotLines(spline, d, uw, vw, tolerance)) {
            std::vector<std::pair<bool, double>> &known = creases[&spline];
            if (std::find(known.begin(), known.end(), std::make_pair(fixedU, knot)) == known.end()) known.emplace_back(fixedU, knot);
            const CurvePtr<3> iso = fixedU ? spline.uIso(knot) : spline.vIso(knot);
            if (!iso) continue;
            const Interval range = fixedU ? vw : uw;
            const auto inside = [&](double t) { return classifyPointOnFace(sheet, f, iso->point(t), 1e-7) == PointLocation::Inside; };
            // Estremo sul bordo tra un campione dentro e uno fuori, poi il punto dell'edge piu' vicino.
            const auto boundary = [&](double in, double out, Vec3 &foot) {
                for (int k = 0; k < 60 && std::fabs(in - out) > 1e-13 * std::max(1.0, range.length()); ++k) {
                    const double mid = 0.5 * (in + out);
                    (inside(mid) ? in : out) = mid;
                }
                const double t = 0.5 * (in + out);
                const Vec3 p = iso->point(t);
                double best = std::numeric_limits<double>::infinity();
                for (LoopId l : sheet.face(f).loops)
                    for (FinId fin : sheet.loopFins(l)) {
                        const Edge &edge = sheet.edge(sheet.fin(fin).edge);
                        const CurveProjection<3> projection = projectPoint(*edge.curve, p, edge.range);
                        if (projection.distance < best) best = projection.distance, foot = projection.point;
                    }
                return t;
            };
            constexpr int kSamples = 512;
            std::vector<bool> flags(kSamples + 1);
            for (int k = 0; k <= kSamples; ++k) flags[std::size_t(k)] = inside(range.lo + range.length() * k / kSamples);
            for (int k = 0; k <= kSamples;) {
                if (!flags[std::size_t(k)]) { ++k; continue; }
                int last = k;
                while (last + 1 <= kSamples && flags[std::size_t(last + 1)]) ++last;
                ImprintCurve curve;
                curve.face = f;
                curve.curve = iso;
                const auto param = [&](int i) { return range.lo + range.length() * i / kSamples; };
                double lo = param(k), hi = param(last);
                if (k > 0) lo = boundary(param(k), param(k - 1), curve.loPoint), curve.loOnEdge = true;
                if (last < kSamples) hi = boundary(param(last), param(last + 1), curve.hiPoint), curve.hiOnEdge = true;
                if (hi - lo > 1e-9 * range.length()) {
                    curve.range = {lo, hi};
                    curves.push_back(curve);
                }
                k = last + 1;
            }
        }
    }
    if (curves.empty()) return false;
    BooleanOptions options;
    options.unifySameDomain = false;
    split = imprintCurves(sheet, curves, {}, options);
    // Ogni pezzo prende la sotto-superficie del suo tratto tra le pieghe: le
    // finestre prolungate dell'offset continuano il polinomio del tratto
    // invece di attraversare la piega. Stesso parametro: le SP-curve valgono.
    for (const auto &[surface, lines] : creases) {
        std::vector<double> cutsU, cutsV;
        for (const auto &[fixedU, knot] : lines) (fixedU ? cutsU : cutsV).push_back(knot);
        std::vector<std::pair<Interval, SurfacePtr>> piecesU, piecesV;
        if (!cutsU.empty()) piecesU = splitAtC0Knots(*surface, true, cutsU);
        if (!cutsV.empty()) piecesV = splitAtC0Knots(*surface, false, cutsV);
        if (piecesU.empty() == !cutsU.empty() || piecesV.empty() == !cutsV.empty())
            throw std::domain_error("offset: piega interna su un nodo che non separa la superficie (molteplicita' minore del grado)");
        if (!cutsU.empty() && !cutsV.empty()) throw std::domain_error("offset: pieghe interne nelle due direzioni della stessa faccia non gestite");
        const auto &pieces = cutsU.empty() ? piecesV : piecesU;
        const bool alongU = !cutsU.empty();
        for (FaceId f : split.faces()) {
            if (split.face(f).surface.get() != static_cast<const Surface *>(surface)) continue;
            // Il tratto che contiene il centro della finestra della faccia.
            double lo = 1e300, hi = -1e300;
            for (LoopId l : split.face(f).loops)
                for (FinId fin : split.loopFins(l)) {
                    const Fin &data = split.fin(fin);
                    if (!data.pcurve) continue;
                    const Edge &edge = split.edge(data.edge);
                    for (int k = 0; k <= 16; ++k) {
                        const Vec2 uv = data.pcurve->point(edge.range.lo + edge.range.length() * k / 16.0);
                        const double t = alongU ? uv.x() : uv.y();
                        lo = std::min(lo, t), hi = std::max(hi, t);
                    }
                }
            const double middle = 0.5 * (lo + hi);
            for (const auto &[range, piece] : pieces)
                if (middle >= range.lo && middle <= range.hi) split.face(f).surface = piece;
        }
    }
    return true;
}

}  // namespace

std::shared_ptr<BSplineCurve<3>> fitCurve(const std::function<Vec3(double)> &f, const Interval &range, const std::vector<double> &breaks,
                                          double tolerance, double *deviation) {
    if (!range.isFinite() || !(range.lo < range.hi)) throw std::domain_error("fitCurve: intervallo non valido");
    const double h = 1e-6 * range.length();
    // Il passo delle derivate non e' la risoluzione minima della curva:
    // una campata stretta o molto curva puo' richiedere suddivisioni piu' fini.
    const std::vector<double> cuts = sortedBreaks(range, breaks, 1e-12 * range.length());
    struct Node {
        double t;
        Vec3 p, d;
    };
    std::vector<Vec3> poles{f(range.lo)};
    std::vector<double> knots(4, range.lo);
    double worst = 0.0;
    std::size_t visited = 0;
    const std::function<void(const Node &, const Node &, int)> piece = [&](const Node &a, const Node &b, int depth) {
        if (++visited > 32768) throw std::domain_error("offset: curva a distanza non approssimabile entro il limite di raffinamento");
        const double span = b.t - a.t;
        const Vec3 p1 = a.p + (span / 3.0) * a.d, p2 = b.p - (span / 3.0) * b.d;
        double error = 0.0;
        for (double s : {0.25, 0.5, 0.75}) error = std::max(error, distance(bezier(a.p, p1, p2, b.p, s), f(a.t + s * span)));
        if (error <= tolerance || depth >= 40 || span < 1e-12 * range.length()) {
            poles.push_back(p1);
            poles.push_back(p2);
            poles.push_back(b.p);
            knots.insert(knots.end(), 3, b.t);
            worst = std::max(worst, error);
            return;
        }
        const double mid = 0.5 * (a.t + b.t);
        const Node m{mid, f(mid), derivative(f, mid, std::min(h, 0.01 * span), 0)};
        piece(a, m, depth + 1);
        piece(m, b, depth + 1);
    };
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
        const double a = cuts[k], b = cuts[k + 1];
        const double step = std::min(h, 0.01 * (b - a));
        piece(Node{a, f(a), derivative(f, a, step, 1)}, Node{b, f(b), derivative(f, b, step, -1)}, 0);
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
        // Solo la finestra della faccia (con il margine): fuori la curva a
        // distanza puo' avere cuspidi che alla faccia non interessano.
        const Interval full = e.curve()->domain().isFinite() ? e.curve()->domain() : uw;
        Interval domain = full;
        if (uw.isFinite()) {
            if (surface.isUPeriodic()) domain = uw.length() < 0.98 * surface.uPeriod() ? uw : full;  // f riporta u nel periodo
            else domain = Interval{std::max(full.lo, uw.lo), std::min(full.hi, uw.hi)};
        }
        const double v0 = vw.isFinite() ? 0.5 * (vw.lo + vw.hi) : 0.0;
        const auto f = [&](double u) { return surface.point(u, v0) + d * surface.normal(u, v0) - v0 * e.direction(); };
        requireClosed(*e.curve(), domain, f, tolerance);
        // Retta o cerchio a distanza: esatti (stesso parametro).
        CurvePtr<3> base = exactOffsetCurve(e.curve(), domain, f, std::max(1.0, norm(surface.point(domain.lo, v0))));
        if (!base) base = fitCurve(f, domain, e.curve()->breakpoints(domain), tolerance);
        result = std::make_shared<ExtrusionSurface>(base, e.direction());
        break;
    }
    case SurfaceType::Revolution: {
        // La normale ruota con u: il meridiano a distanza (in u = 0) ruotato.
        const auto &r = static_cast<const RevolutionSurface &>(surface);
        // Solo la finestra della faccia: il meridiano puo' proseguire fino
        // all'asse o in tratti molto curvi che la faccia non usa.
        const Interval full = r.meridian()->domain().isFinite() ? r.meridian()->domain() : vw;
        Interval domain = full;
        if (vw.isFinite()) {
            if (r.meridian()->isPeriodic()) domain = vw.length() < 0.98 * r.meridian()->period() ? vw : full;
            else domain = Interval{std::max(full.lo, vw.lo), std::min(full.hi, vw.hi)};
        }
        const auto f = [&](double v) { return surface.point(0.0, v) + d * surface.normal(0.0, v); };
        requireClosed(*r.meridian(), domain, f, tolerance);
        // Meridiano retta o cerchio (il toro scritto come rivoluzione nei file): esatto.
        CurvePtr<3> meridian = exactOffsetCurve(r.meridian(), domain, f, std::max(1.0, norm(surface.point(0.0, domain.lo))));
        if (!meridian) meridian = fitCurve(f, domain, r.meridian()->breakpoints(domain), tolerance);
        result = std::make_shared<RevolutionSurface>(meridian, r.axisPoint(), r.axisDirection());
        break;
    }
    case SurfaceType::BSpline:
        if (!uw.isFinite() || !vw.isFinite()) throw std::domain_error("offset: finestra della B-spline non limitata");
        requireOffsetBelowCurvature(surface, d, uw, vw);
        result = offsetBSplineC2(surface, d, uw, vw, tolerance);
        if (!result) result = offsetBSpline(surface, d, uw, vw, tolerance);
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

OffsetResult offsetFaces(const Body &input, const std::vector<FaceId> &faces, double distanceValue, double tolerance, bool preserveSeams, double joinAngle) {
    if (faces.empty()) throw std::domain_error("offset: nessuna faccia scelta");
    if (!(std::fabs(distanceValue) > 0.0)) throw std::domain_error("offset: distanza nulla");
    const detail::PhaseTimer total("offset: totale");
    Body body = input;
    {
        const detail::PhaseTimer timer("offset: SP-curve iniziali");
        if (computePCurves(body) > 0) throw std::domain_error("offset: SP-curve non calcolabili");
    }
    Body split;
    bool splitPatches = false, splitCreases = false;
    {
        const detail::PhaseTimer timer("offset: divisione in pezze (facce rettangolari)");
        splitPatches = splitOffsetFaces(body, faces, distanceValue, tolerance, split);
    }
    if (!splitPatches) {
        const detail::PhaseTimer timer("offset: divisione sulle pieghe (facce rifilate)");
        splitCreases = splitTrimmedCreases(body, faces, distanceValue, tolerance, split);
    }
    if (splitPatches) {
        OffsetResult result = offsetFaces(split, split.faces(), distanceValue, tolerance, preserveSeams, joinAngle);
        result.notes.push_back("Facce del loft divise sulle discontinuita interne prima dell'offset");
        return result;
    }
    if (splitCreases) {
        OffsetResult result = offsetFaces(split, split.faces(), distanceValue, tolerance, preserveSeams, joinAngle);
        result.notes.push_back("Facce divise sulle pieghe interne (normale discontinua lungo una linea di nodo) prima dell'offset");
        return result;
    }
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    const double scale = std::max(1.0, box.diagonal());
    std::set<int> selected;
    for (FaceId f : faces) selected.insert(f.index);

    // Superficie a distanza di ogni faccia, sulla finestra (u, v) dei suoi loop.
    std::map<int, SurfacePtr> offsets;
    // Finestre (u, v) delle facce in sequenza, poi le superfici a distanza in
    // parallelo (ognuna dipende solo dalla sua faccia; le B-spline sono la
    // parte lunga).
    struct SurfaceJob {
        int index;
        Interval u, v;
        SurfacePtr result;
        std::string failure;
    };
    std::vector<SurfaceJob> surfaceJobs;
    for (int index : selected) {
        const FaceId f{index};
        const Face &face = body.face(f);
        const Surface &surface = *face.surface;
        // Intervalli (u, v) delle SP-curve, una per fin (continue).
        std::vector<Interval> uRanges, vRanges;
        for (LoopId l : face.loops)
            for (FinId finId : body.loopFins(l)) {
                const Fin &fin = body.fin(finId);
                const Edge &edge = body.edge(fin.edge);
                Interval ur{1e300, -1e300}, vr{1e300, -1e300};
                for (int k = 0; k <= 16; ++k) {
                    const Vec2 uv = fin.pcurve->point(edge.range.lo + edge.range.length() * k / 16.0);
                    ur.lo = std::min(ur.lo, uv[0]), ur.hi = std::max(ur.hi, uv[0]);
                    vr.lo = std::min(vr.lo, uv[1]), vr.hi = std::max(vr.hi, uv[1]);
                }
                uRanges.push_back(ur);
                vRanges.push_back(vr);
            }
        // Finestra di una direzione. Nelle direzioni periodiche le SP-curve di
        // fin diverse possono stare a periodi diversi: si uniscono gli
        // intervalli sul cerchio e la finestra e' il complemento del buco piu'
        // grande (con il minimo e il massimo una faccia a cavallo della
        // cucitura sembrerebbe coprire tutto il periodo).
        const auto window = [](const std::vector<Interval> &ranges, const Interval &domain, bool periodic, double period) {
            if (ranges.empty()) return domain;
            if (!periodic) {
                Interval all{1e300, -1e300};
                for (const Interval &r : ranges) all.lo = std::min(all.lo, r.lo), all.hi = std::max(all.hi, r.hi);
                return all;
            }
            std::vector<std::pair<double, double>> pieces;
            for (const Interval &r : ranges) {
                if (r.length() >= period * (1.0 - 1e-9)) return domain;
                const double lo = r.lo - period * std::floor((r.lo - domain.lo) / period);
                pieces.push_back({lo, lo + r.length()});
                pieces.push_back({lo + period, lo + period + r.length()});
            }
            std::sort(pieces.begin(), pieces.end());
            std::vector<std::pair<double, double>> merged;
            for (const auto &p : pieces) {
                if (!merged.empty() && p.first <= merged.back().second) merged.back().second = std::max(merged.back().second, p.second);
                else merged.push_back(p);
            }
            // Il buco piu' grande tra due tratti coperti consecutivi (entro due periodi).
            double gap = 0.0, end = 0.0;
            for (std::size_t k = 0; k + 1 < merged.size(); ++k)
                if (merged[k + 1].first - merged[k].second > gap) gap = merged[k + 1].first - merged[k].second, end = merged[k + 1].first;
            if (!(gap > 0.0) || merged.front().second - merged.front().first >= period) return domain;
            return Interval{end, end + period - gap};
        };
        Interval u = window(uRanges, surface.uDomain(), surface.isUPeriodic(), surface.isUPeriodic() ? surface.uPeriod() : 0.0);
        Interval v = window(vRanges, surface.vDomain(), surface.isVPeriodic(), surface.isVPeriodic() ? surface.vPeriod() : 0.0);
        // Le B-spline dei file hanno spesso i bordi delle facce un poco fuori dal
        // dominio (dati tolleranti): li' la superficie si prolunga (lo stesso
        // polinomio), cosi' le SP-curve delle facce valgono anche per la
        // superficie a distanza; al piu' il 5% del dominio.
        const bool extend = surface.type() == SurfaceType::BSpline;
        // Il margine e' geometrico: una campata UV molto stretta puo'
        // richiedere un prolungamento ben oltre il 5% del proprio dominio.
        double seamU = 0.0, seamV = 0.0;
        if (preserveSeams && extend)
            for (LoopId l : face.loops)
                for (FinId finId : body.loopFins(l)) {
                    const Fin &fin = body.fin(finId);
                    const Edge &edge = body.edge(fin.edge);
                    const FinId other = edge.forward == finId ? edge.backward : edge.forward;
                    if (!other.valid() || !selected.count(body.finFace(other).index)) continue;
                    for (int k = 0; k <= 8; ++k) {
                        const double t = edge.range.lo + edge.range.length() * k / 8.0;
                        const Vec2 uv = fin.pcurve->point(t);
                        const Vec3 a = faceNormal(body, f, uv), b = faceNormal(body, body.finFace(other), body.fin(other).pcurve->point(t));
                        const double cosine = dot(a, b);
                        if (cosine < -0.99) continue;
                        const Vec3 shift = distanceValue * ((a + b) / (1.0 + cosine) - a);
                        Vec3 jet[4];
                        surface.evaluate(uv.x(), uv.y(), 1, jet);
                        const Vec3 su = jet[Surface::derivativeIndex(1,0,1)], sv = jet[Surface::derivativeIndex(0,1,1)];
                        const double aa = dot(su,su), ab = dot(su,sv), bb = dot(sv,sv), det = aa*bb-ab*ab;
                        if (!(det > 1e-24*aa*bb)) continue;
                        seamU = std::max(seamU, 2.0 * std::fabs((bb*dot(su,shift)-ab*dot(sv,shift))/det));
                        seamV = std::max(seamV, 2.0 * std::fabs((aa*dot(sv,shift)-ab*dot(su,shift))/det));
                    }
                }
        const auto widen = [extend](Interval range, const Interval &domain, bool periodic, double seamReach) {
            const double margin = std::max(0.005 * std::max(range.length(), 1e-9), seamReach);
            range.lo -= margin, range.hi += margin;
            if (!periodic && domain.isFinite()) {
                const double reach = extend ? std::max(0.05 * domain.length(), seamReach) : 0.0;
                const double lo = domain.lo - reach, hi = domain.hi + reach;
                range.lo = std::max(range.lo, lo), range.hi = std::min(range.hi, hi);
            }
            // Quasi tutto il periodo: il periodo intero (la superficie resta chiusa).
            if (periodic && domain.isFinite() && range.length() >= 0.98 * domain.length()) range = domain;
            return range;
        };
        u = widen(u, surface.uDomain(), surface.isUPeriodic(), seamU);
        v = widen(v, surface.vDomain(), surface.isVPeriodic(), seamV);
        surfaceJobs.push_back({index, u, v, nullptr, {}});
    }
    std::optional<detail::PhaseTimer> surfaceTimer;
    surfaceTimer.emplace("offset: superfici a distanza");
    parallelFor(surfaceJobs.size(), threadCount(0), [&](std::size_t k) {
        SurfaceJob &job = surfaceJobs[k];
        const Face &face = body.face(FaceId{job.index});
        try {
            job.result = offsetSurface(*face.surface, face.sense ? distanceValue : -distanceValue, job.u, job.v, tolerance);
        } catch (const std::exception &e) {
            job.failure = e.what();
        }
    });
    for (SurfaceJob &job : surfaceJobs) {
        if (!job.failure.empty()) throw std::domain_error("faccia " + std::to_string(job.index + 1) + ": " + job.failure);
        offsets[job.index] = job.result;
    }

    surfaceTimer.reset();
    std::optional<detail::PhaseTimer> edgeTimer;
    edgeTimer.emplace("offset: adiacenze, vertici ed edge a distanza");
    // Edge delle facce scelte: tangenti (normali parallele) o spigoli vivi.
    struct EdgeInfo {
        std::vector<FinId> fins;
        bool joined = false;
    };
    std::map<int, EdgeInfo> edges;
    for (int index : selected)
        for (LoopId l : body.face(FaceId{index}).loops)
            for (FinId finId : body.loopFins(l)) edges[body.fin(finId).edge.index].fins.push_back(finId);
    constexpr double kTangentAngle = 1e-3;
    OffsetResult result;
    // Ogni edge con due fin e' una coppia di facce adiacenti indipendente.
    // La classificazione puo' richiedere proiezioni/derivate costose sulle
    // superfici importate: la calcoliamo per coppia in parallelo, ma applichiamo
    // i risultati alla mappa e ai contatori in ordine stabile dopo il join.
    struct AdjacencyJob {
        int edgeIndex = -1;
        bool tangent = false;
        std::string failure;
    };
    std::vector<AdjacencyJob> adjacencyJobs;
    for (const auto &[edgeIndex, info] : edges)
        if (info.fins.size() == 2) adjacencyJobs.push_back({edgeIndex, false, {}});
    const auto &edgeLookup = edges;
    parallelFor(adjacencyJobs.size(), threadCount(0), [&](std::size_t jobIndex) {
        AdjacencyJob &job = adjacencyJobs[jobIndex];
        try {
            const EdgeInfo &info = edgeLookup.at(job.edgeIndex);
            const Edge &edge = body.edge(EdgeId{job.edgeIndex});
            job.tangent = true;
            for (int k = 0; k < 9 && job.tangent; ++k) {
                const double t = edge.range.lo + edge.range.length() * (k + 0.5) / 9.0;
                const Vec3 a = faceNormal(body, body.finFace(info.fins[0]), body.fin(info.fins[0]).pcurve->point(t));
                const Vec3 b = faceNormal(body, body.finFace(info.fins[1]), body.fin(info.fins[1]).pcurve->point(t));
                job.tangent = joinAngle > 0.0 ? dot(a, b) > std::cos(joinAngle)
                                              : dot(a, b) > std::cos(kTangentAngle) && std::fabs(distanceValue) * norm(a - b) <= 2.0 * tolerance;
            }
        } catch (const std::exception &e) {
            job.failure = e.what();
        }
    });
    for (const AdjacencyJob &job : adjacencyJobs) {
        if (!job.failure.empty()) throw std::domain_error(job.failure);
        edges.at(job.edgeIndex).joined = job.tangent || preserveSeams;
        if (!job.tangent) ++result.sharpEdges;
    }

    const bool trimSeams = preserveSeams && result.sharpEdges > 0;
    const double seamTolerance = std::max(4.0 * tolerance, 1e-10 * scale);
    const double seamReach = std::max(100.0 * std::fabs(distanceValue), 1e-6 * scale);

    // Vertici: un punto per vertice e faccia, uniti attraverso gli edge tangenti.
    std::map<std::pair<int, int>, int> slot;  // (vertice, faccia) -> indice
    std::vector<int> parent;
    std::vector<Vec3> sum;
    std::vector<int> count;
    std::vector<SeamSurface> vertexSurfaces;
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
                    vertexSurfaces.push_back({body.face(FaceId{index}).surface, uv, body.face(FaceId{index}).sense ? distanceValue : -distanceValue});
                }
                sum[std::size_t(found->second)] += p;
                ++count[std::size_t(found->second)];
            }
    for (const auto &[edgeIndex, info] : edges) {
        if (!info.joined) continue;
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
    std::vector<std::vector<SeamSurface>> constraints(parent.size());
    for (std::size_t k = 0; k < parent.size(); ++k) constraints[std::size_t(find(int(k)))].push_back(vertexSurfaces[k]);
    const auto pointIndex = [&](int vertex, int face) {
        const int root = find(slot.at({vertex, face}));
        auto found = pointOf.find(root);
        if (found != pointOf.end()) return found->second;
        Vec3 point = groupSum[std::size_t(root)] / double(groupCount[std::size_t(root)]);
        if (trimSeams) point = seamPoint(point, constraints[std::size_t(root)], seamTolerance, seamReach);
        model.points.push_back(point);
        return pointOf[root] = int(model.points.size()) - 1;
    };

    // Edge a distanza: uno solo lungo gli edge tangenti (la normale media
    // delle due facce), uno per faccia altrove.
    std::map<std::pair<int, int>, int> rawEdge;  // (edge, faccia o -1) -> indice
    struct CurveJob {
        int edge;
        CurvePtr<3> curve;
        Interval range;
        std::function<Vec3(double)> offset;
        std::vector<double> breaks;
        std::string failure;
    };
    std::vector<CurveJob> curveJobs;
    const auto rawEdgeOf = [&](FinId finId) {
        const Fin &fin = body.fin(finId);
        const EdgeId e = fin.edge;
        const EdgeInfo &info = edges.at(e.index);
        const int face = body.finFace(finId).index;
        const auto key = std::make_pair(e.index, info.joined ? -1 : face);
        auto found = rawEdge.find(key);
        if (found != rawEdge.end()) return found->second;
        const Edge &edge = body.edge(e);
        std::vector<FinId> used = info.joined ? info.fins : std::vector<FinId>{finId};
        const Body *source = &body;
        std::function<Vec3(double)> f = [source, used, curve = edge.curve, distanceValue](double t) {
            Vec3 n;
            for (FinId u : used) n += faceNormal(*source, source->finFace(u), source->fin(u).pcurve->point(t));
            if (!(norm(n) > 1e-12)) throw std::domain_error("offset: normali opposte lungo il bordo da cucire");
            return curve->point(t) + distanceValue * (n / norm(n));
        };
        std::vector<double> breaks;
        for (FinId u : used) {
            const std::vector<double> b = body.fin(u).pcurve->breakpoints(edge.range);
            breaks.insert(breaks.end(), b.begin(), b.end());
        }
        detail::RawEdge raw;
        raw.start = pointIndex(body.edgeStart(e).index, face);
        raw.end = pointIndex(body.edgeEnd(e).index, face);
        if (trimSeams) {
            const Vec3 startDelta = model.points[std::size_t(raw.start)] - f(edge.range.lo);
            const Vec3 endDelta = model.points[std::size_t(raw.end)] - f(edge.range.hi);
            f = [&, original = f, used, range = edge.range, startDelta, endDelta](double t) {
                const double fraction = (t - range.lo) / range.length();
                Vec3 point = original(t) + (1.0 - fraction) * startDelta + fraction * endDelta;
                std::vector<SeamSurface> incident;
                for (FinId u : used) {
                    const Face &sourceFace = body.face(body.finFace(u));
                    incident.push_back({sourceFace.surface, body.fin(u).pcurve->point(t), sourceFace.sense ? distanceValue : -distanceValue});
                }
                if (used.size() == 2) {
                    // Fissa la sezione trasversale durante Newton: vicino
                    // alla tangenza l'intersezione non deve scorrere sul bordo.
                    const Vec3 tangent = normalized(body.edge(body.fin(used.front()).edge).curve->derivative(t));
                    const Vec3 axis = std::fabs(tangent.x()) < 0.8 ? Vec3(1,0,0) : Vec3(0,1,0);
                    incident.push_back({std::make_shared<Plane>(Frame3(point, tangent, axis)), Vec2()});
                }
                return seamPoint(point, std::move(incident), seamTolerance, seamReach);
            };
        }
        raw.hasRange = true;
        raw.range = edge.range;
        model.edges.push_back(raw);
        // La curva si calcola dopo, in parallelo con le altre.
        curveJobs.push_back({int(model.edges.size()) - 1, edge.curve, edge.range, f, breaks, {}});
        return rawEdge[key] = int(model.edges.size()) - 1;
    };
    for (int index : selected) {
        const Face &face = body.face(FaceId{index});
        detail::RawFace raw;
        raw.surface = offsets.at(index);
        raw.sense = face.sense;
        for (LoopId l : face.loops) {
            std::vector<detail::RawFin> loop;
            for (FinId finId : body.loopFins(l)) {
                // L'SP-curve della faccia di partenza: la superficie a distanza
                // ha gli stessi parametri (u, v), cosi' assembleBody non proietta.
                const Fin &fin = body.fin(finId);
                // Solo se i suoi (u, v) stanno nel dominio della superficie nuova
                // (che puo' coprire solo la finestra della faccia, senza periodo).
                const Surface &target = *raw.surface;
                const Edge &edge = body.edge(fin.edge);
                bool inside = true;
                for (int k = 0; k <= 8 && inside; ++k) {
                    const Vec2 uv = fin.pcurve->point(edge.range.lo + edge.range.length() * k / 8.0);
                    for (int d = 0; d < 2 && inside; ++d) {
                        const bool periodic = d == 0 ? target.isUPeriodic() : target.isVPeriodic();
                        const Interval domain = d == 0 ? target.uDomain() : target.vDomain();
                        if (periodic || !domain.isFinite()) continue;
                        // Le SP-curve dei file escono un poco dal dominio (dati tolleranti):
                        // li' la superficie nuova si prolunga, e lo scarto misurato va nella
                        // tolleranza dell'edge.
                        const double slack = 1e-6 * domain.length();
                        inside = uv[d] >= domain.lo - slack && uv[d] <= domain.hi + slack;
                    }
                }
                if (inside && !trimSeams) loop.push_back({rawEdgeOf(finId), fin.sense, fin.pcurve, fin.pcurveTolerance + 2.0 * tolerance});
                else loop.push_back({rawEdgeOf(finId), fin.sense});
            }
            if (!loop.empty()) raw.loops.push_back(std::move(loop));
        }
        model.faces.push_back(std::move(raw));
    }
    parallelFor(curveJobs.size(), threadCount(0), [&](std::size_t k) {
        CurveJob &job = curveJobs[k];
        try {
            model.edges[std::size_t(job.edge)].curve = offsetCurve(job.curve, job.range, job.offset, job.breaks, scale, tolerance);
        } catch (const std::exception &e) {
            job.failure = e.what();
        }
    });
    for (const CurveJob &job : curveJobs)
        if (!job.failure.empty())
            throw std::domain_error("bordo " + std::to_string(job.edge + 1) + ": " + job.failure
                + (trimSeams ? "; ridurre la distanza o disattivare Mantieni la cucitura" : ""));
    if (trimSeams) {
        // Le nuove SP-curve seguono il ramo UV originale anche sulle spline
        // prolungate: la proiezione globale puo' scegliere un minimo sul bordo
        // del vecchio dominio o un ramo diverso del loft.
        std::size_t faceIndex = 0;
        for (int index : selected) {
            auto &raw = model.faces[faceIndex++];
            Interval ur = raw.surface->uDomain(), vr = raw.surface->vDomain();
            std::size_t loopIndex = 0;
            for (LoopId l : body.face(FaceId{index}).loops) {
                std::size_t finIndex = 0;
                for (FinId finId : body.loopFins(l)) {
                    const Fin &fin = body.fin(finId);
                    auto &rawFin = raw.loops[loopIndex][finIndex++];
                    const auto &edge = model.edges[std::size_t(rawFin.edge)];
                    const auto uvAt = [&](double t) {
                        const Vec2 uv = projectLocal(*raw.surface, edge.curve->point(t), fin.pcurve->point(t));
                        return Vec3(uv.x(), uv.y(), 0);
                    };
                    double rate = 1.0;
                    for (int k = 0; k <= 16; ++k) {
                        const double t = edge.range.lo + edge.range.length() * k / 16.0;
                        const Vec3 uv = uvAt(t);
                        Vec3 jet[4];
                        raw.surface->evaluate(uv.x(), uv.y(), 1, jet);
                        rate = std::max({rate, norm(jet[Surface::derivativeIndex(1, 0, 1)]), norm(jet[Surface::derivativeIndex(0, 1, 1)])});
                        ur.lo = std::min(ur.lo, uv.x()), ur.hi = std::max(ur.hi, uv.x());
                        vr.lo = std::min(vr.lo, uv.y()), vr.hi = std::max(vr.hi, uv.y());
                    }
                    const auto fitted = fitCurve(uvAt, edge.range, fin.pcurve->breakpoints(edge.range), 0.1 * tolerance / rate);
                    std::vector<Vec2> poles;
                    for (const Vec3 &p : fitted->poles()) poles.emplace_back(p.x(), p.y());
                    rawFin.pcurve = std::make_shared<BSplineCurve<2>>(fitted->degree(), fitted->knots(), poles);
                    rawFin.pcurveTolerance = fin.pcurveTolerance + 10.0 * tolerance;
                    for (int k = 0; k <= 64; ++k) {
                        const double t = edge.range.lo + edge.range.length() * k / 64.0;
                        const Vec2 uv = rawFin.pcurve->point(t);
                        if (distance(raw.surface->point(uv.x(), uv.y()), edge.curve->point(t)) > rawFin.pcurveTolerance)
                            throw std::domain_error("offset: bordo rifilato fuori tolleranza; ridurre la distanza o disattivare Mantieni la cucitura");
                    }
                }
                ++loopIndex;
            }
            if (raw.surface->type() == SurfaceType::BSpline)
                raw.surface = std::make_shared<BSplineSurface>(extendBSplineSurface(
                    static_cast<const BSplineSurface &>(*raw.surface), ur, vr));
        }
        result.sharpEdges = 0;
    }
    edgeTimer.reset();
    {
        const detail::PhaseTimer timer("offset: assemblaggio");
        result.body = detail::assembleBody(model, false, &result.notes);
    }
    for (ShellId s : result.body.shells()) {
        (void)s;
        ++result.shells;
    }
    return result;
}

}
