#include "fk_mapping_fit.h"
#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include <algorithm>
#include <array>
#include <map>
#include <tuple>
#include <stdexcept>
namespace ForgeCad::Kernel::detail {
namespace {
void require(bool b, const char *m) {
    if (!b)
        throw std::domain_error(m);
}
}
using Cell = std::array<std::array<Vec3, 6>, 6>;
Vec3 bezierPoint(std::array<Vec3, 6> p, int degree, double t) {
    for (int n = degree; n > 0; --n)
        for (int i = 0; i < n; ++i)
            p[i] = (1 - t) * p[i] + t * p[i + 1];
    return p[0];
}
Vec3 cellPoint(const Cell &p, double u, double v, int degree) {
    std::array<Vec3, 6> rows;
    for (int i = 0; i <= degree; ++i)
        rows[i] = bezierPoint(p[i], degree, v);
    return bezierPoint(rows, degree, u);
}
// P0,D0,P(1/3),P(2/3),D1,P1 -> poli Bezier quintici.
std::array<Vec3, 6> quintic(const std::array<Vec3, 6> &f) {
    // Lavoro rispetto a P0: evita combinazioni di coordinate globali grandi.
    const auto origin = f[0];
    std::array<Vec3, 6> p;
    p[0] = Vec3();
    p[1] = f[1] / 5.;
    p[5] = f[5] - origin;
    p[4] = p[5] - f[4] / 5.;
    const auto a = 243 * (f[2] - origin) - 80 * p[1] - 10 * p[4] - p[5];
    const auto b = 243 * (f[3] - origin) - 10 * p[1] - 80 * p[4] - 32 * p[5];
    p[2] = (2 * a - b) / 120.;
    p[3] = (2 * b - a) / 120.;
    for (auto &q : p)
        q += origin;
    return p;
}
// Fit per celle bicubiche Hermite; i nodi sorgenti restano espliciti.
// La griglia e' raffinata in base allo scarto misurato nello stesso dominio UV.
SurfacePtr fitMappingSurface(std::vector<double> us, std::vector<double> vs,
    const std::function<Vec3(double, double)> &f, const MappingJetFunction &analyticJet, double tolerance,
    double &deviation, double derivativeFraction, int degree) {
    require(us.size() >= 2 && vs.size() >= 2, "Deformazione: dominio senza campate.");
    require(std::isfinite(derivativeFraction) && derivativeFraction > 0 && derivativeFraction <= .05,
        "Deformazione: passo delle derivate non valido.");
    require(degree == 3 || degree == 5, "Deformazione: grado di fit non supportato.");
    using Jet = MappingJet;
    using Key = std::tuple<double, double, int, int>;
    std::map<Key, Jet> jets;
    const auto originalU = us, originalV = vs;
    struct Stencil {
        std::array<double, 5> positions, weights;
    };
    const auto stencil = [derivativeFraction](double t, int side, const std::vector<double> &grid) {
        const auto it = std::lower_bound(grid.begin(), grid.end(), t);
        require(it != grid.end() && *it == t, "Deformazione: nodo del fit assente.");
        const auto k = std::size_t(it - grid.begin());
        const double left = k ? t - grid[k - 1] : 0., right = k + 1 < grid.size() ? grid[k + 1] - t : 0.;
        const double width = side < 0 ? left : side > 0 ? right : std::min(left, right);
        const double h = derivativeFraction * width;
        require(h > 0 && t + h != t && t - h != t, "Deformazione: campata numericamente degenere.");
        Stencil out;
        if (side) {
            const double c[5] = { -25, 48, -36, 16, -3 };
            for (int i = 0; i < 5; ++i) {
                out.positions[i] = t + side * i * h;
                out.weights[i] = side * c[i] / (12 * h);
            }
        } else {
            const double c[5] = { 1, -8, 0, 8, -1 };
            for (int i = 0; i < 5; ++i) {
                out.positions[i] = t + (i - 2) * h;
                out.weights[i] = c[i] / (12 * h);
            }
        }
        return out;
    };
    const auto jet = [&](double u, double v, int su, int sv) {
        if (!std::binary_search(originalU.begin(), originalU.end(), u))
            su = 0;
        if (!std::binary_search(originalV.begin(), originalV.end(), v))
            sv = 0;
        const Key key { u, v, su, sv };
        const auto found = jets.find(key);
        if (found != jets.end())
            return found->second;
        if (analyticJet) {
            auto result = analyticJet(u, v, su, sv);
            jets.emplace(key, result);
            return result;
        }
        const auto a = stencil(u, su, us), b = stencil(v, sv, vs);
        Jet result;
        result.p = f(u, v);
        long double pu[3] = { }, pv[3] = { }, puv[3] = { };
        std::array<Vec3, 5> x, y;
        for (int i = 0; i < 5; ++i) {
            x[i] = f(a.positions[i], v);
            y[i] = f(u, b.positions[i]);
        }
        for (int i = 0; i < 5; ++i) {
            for (int k = 0; k < 3; ++k) {
                pu[k] += static_cast<long double>(a.weights[i]) * (x[i][k] - result.p[k]);
                pv[k] += static_cast<long double>(b.weights[i]) * (y[i][k] - result.p[k]);
            }
            for (int j = 0; j < 5; ++j) {
                const Vec3 q = f(a.positions[i], b.positions[j]);
                for (int k = 0; k < 3; ++k)
                    puv[k] += static_cast<long double>(a.weights[i]) * b.weights[j]
                        * ((static_cast<long double>(q[k]) - x[i][k])
                            - (static_cast<long double>(y[j][k]) - result.p[k]));
            }
        }
        for (int k = 0; k < 3; ++k) {
            result.pu[k] = double(pu[k]);
            result.pv[k] = double(pv[k]);
            result.puv[k] = double(puv[k]);
        }
        require(
            isFinite(result.pu) && isFinite(result.pv) && isFinite(result.puv), "Deformazione: derivate non finite.");
        jets.emplace(key, result);
        return result;
    };
    const std::vector<double> probes = degree == 5 ? std::vector<double> { .1, .2, .3, .4, .5, .6, .7, .8, .9 }
                                                   : std::vector<double> { .125, .25, .5, .75, .875 };
    for (int round = 0; round < 24; ++round) {
        cancellationPoint();
        // Il passo delle derivate segue il raffinamento: un passo fisso sul
        // dominio iniziale attraversava dettagli NURBS anche nelle celle fini.
        jets.clear();
        const std::size_t nu = us.size() - 1, nv = vs.size() - 1;
        require(nu <= 65536 / nv && (degree * nu + 1) <= 200000 / (degree * nv + 1),
            "Deformazione: griglia NURBS oltre il budget.");
        std::vector<Cell> cells(nu * nv);
        std::vector<bool> splitU(nu, false), splitV(nv, false);
        bool done = true;
        double worst = 0;
        for (std::size_t i = 0; i < nu; ++i)
            for (std::size_t j = 0; j < nv; ++j) {
                cancellationPoint();
                Cell &cell = cells[i * nv + j];
                const double hu = us[i + 1] - us[i], hv = vs[j + 1] - vs[j];
                require(hu > 0 && hv > 0, "Deformazione: campata nulla.");
                // Hermite: le celle lisce condividono punti e derivate, evitando
                // pieghe artificiali sulle linee della griglia del fit.
                if (degree == 5) {
                    const double at[6] = { 0, 0, 1. / 3., 2. / 3., 1, 1 };
                    Cell functional, rows;
                    for (int a = 0; a < 6; ++a)
                        for (int b = 0; b < 6; ++b) {
                            const double u = a <= 1 ? us[i] : a >= 4 ? us[i + 1] : us[i] + at[a] * hu;
                            const double v = b <= 1 ? vs[j] : b >= 4 ? vs[j + 1] : vs[j] + at[b] * hv;
                            const bool du = a == 1 || a == 4, dv = b == 1 || b == 4;
                            if (!du && !dv)
                                functional[a][b] = f(u, v);
                            else if (du && dv)
                                functional[a][b] = (hu * hv) * jet(u, v, a == 1 ? 1 : -1, b == 1 ? 1 : -1).puv;
                            else {
                                const bool derivativeU = du;
                                const double t = du ? u : v;
                                const auto &grid = du ? us : vs;
                                const auto &original = du ? originalU : originalV;
                                int side = du ? (a == 1 ? 1 : -1) : (b == 1 ? 1 : -1);
                                if (!std::binary_search(original.begin(), original.end(), t))
                                    side = 0;
                                const auto weights = stencil(t, side, grid);
                                const auto origin = f(u, v);
                                long double sum[3] = { };
                                for (int k = 0; k < 5; ++k) {
                                    const auto p
                                        = derivativeU ? f(weights.positions[k], v) : f(u, weights.positions[k]);
                                    for (int axis = 0; axis < 3; ++axis)
                                        sum[axis]
                                            += static_cast<long double>(weights.weights[k]) * (p[axis] - origin[axis]);
                                }
                                for (int axis = 0; axis < 3; ++axis)
                                    functional[a][b][axis] = (du ? hu : hv) * double(sum[axis]);
                            }
                        }
                    for (int a = 0; a < 6; ++a) {
                        const auto line = quintic(functional[a]);
                        for (int b = 0; b < 6; ++b)
                            rows[a][b] = line[b];
                    }
                    for (int b = 0; b < 6; ++b) {
                        std::array<Vec3, 6> values;
                        for (int a = 0; a < 6; ++a)
                            values[a] = rows[a][b];
                        const auto line = quintic(values);
                        for (int a = 0; a < 6; ++a)
                            cell[a][b] = line[a];
                    }
                } else
                    for (int cu = 0; cu < 2; ++cu)
                        for (int cv = 0; cv < 2; ++cv) {
                            const double u = cu ? us[i + 1] : us[i], v = cv ? vs[j + 1] : vs[j];
                            const int su = cu ? -1 : 1, sv = cv ? -1 : 1;
                            const auto data = jet(u, v, su, sv);
                            const int a = cu ? 3 : 0, b = cv ? 3 : 0, a2 = cu ? 2 : 1, b2 = cv ? 2 : 1;
                            cell[a][b] = data.p;
                            cell[a2][b] = data.p + (su * hu / 3.) * data.pu;
                            cell[a][b2] = data.p + (sv * hv / 3.) * data.pv;
                            cell[a2][b2] = data.p + (su * hu / 3.) * data.pu + (sv * hv / 3.) * data.pv
                                + (su * sv * hu * hv / 9.) * data.puv;
                        }
                double alongU = 0, alongV = 0, inside = 0;
                for (double q : probes) {
                    for (double r : { 0., 1. }) {
                        alongU = std::max(
                            alongU, distance(cellPoint(cell, q, r, degree), f(us[i] + hu * q, vs[j] + hv * r)));
                        alongV = std::max(
                            alongV, distance(cellPoint(cell, r, q, degree), f(us[i] + hu * r, vs[j] + hv * q)));
                    }
                    for (double r : probes)
                        inside = std::max(
                            inside, distance(cellPoint(cell, q, r, degree), f(us[i] + hu * q, vs[j] + hv * r)));
                }
                worst = std::max({ worst, alongU, alongV, inside });
                if (std::max({ alongU, alongV, inside }) > tolerance) {
                    done = false;
                    bool u = alongU > .5 * tolerance, v = alongV > .5 * tolerance;
                    if (!u && !v)
                        u = v = true;
                    splitU[i] = splitU[i] || u;
                    splitV[j] = splitV[j] || v;
                }
            }
        if (done) {
            const auto knots = [degree](const std::vector<double> &grid) {
                std::vector<double> k { grid.front() };
                for (double x : grid)
                    for (int r = 0; r < degree; ++r)
                        k.push_back(x);
                k.push_back(grid.back());
                return k;
            };
            const int npu = int(degree * nu + 1), npv = int(degree * nv + 1);
            std::vector<Vec3> poles(std::size_t(npu) * npv);
            for (std::size_t i = 0; i < nu; ++i)
                for (std::size_t j = 0; j < nv; ++j)
                    for (int a = 0; a <= degree; ++a)
                        for (int b = 0; b <= degree; ++b)
                            poles[(degree * i + a) * npv + degree * j + b] = cells[i * nv + j][a][b];
            deviation = std::max(deviation, worst);
            return std::make_shared<BSplineSurface>(degree, degree, knots(us), knots(vs), npu, npv, poles);
        }
        const auto refine = [](std::vector<double> &grid, const std::vector<bool> &split) {
            std::vector<double> next;
            for (std::size_t i = 0; i < split.size(); ++i) {
                next.push_back(grid[i]);
                if (split[i])
                    next.push_back(.5 * grid[i] + .5 * grid[i + 1]);
            }
            next.push_back(grid.back());
            grid = std::move(next);
        };
        refine(us, splitU);
        refine(vs, splitV);
    }
    throw std::domain_error("Deformazione: fit della superficie fuori tolleranza.");
}

}
