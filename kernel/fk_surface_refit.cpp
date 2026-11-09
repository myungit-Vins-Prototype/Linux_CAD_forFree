#include "fk_surface_refit.h"
#include "fk_bspline_basis.h"
#include "fk_cancel.h"
#include "fk_parallel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace ForgeCad::Kernel {
namespace {
std::vector<double> knots(int count) {
    std::vector<double> result(4, 0.);
    for (int i = 1; i < count - 3; ++i)
        result.push_back(double(i) / (count - 3));
    result.insert(result.end(), 4, 1.);
    return result;
}
// Proiezione LS monodimensionale con estremi imposti. Il sistema delle
// basi cubiche ha dimensione limitata dal massimo di poli; si fattorizza una sola volta.
std::vector<double> projector(int count, const std::vector<double> &parameters, const std::vector<double> &k) {
    const int samples = parameters.size();
    const int n = count - 2;
    std::vector<double> basis(samples * count, 0.), gram(n * n, 0.), map(count * samples, 0.);
    for (int s = 0; s < samples; ++s) {
        cancellationPoint();
        const double t = parameters[s];
        const int span = detail::findSpan(k, 3, count, t);
        double values[4];
        detail::basisFunctionDerivatives(k, span, t, 3, 0, values);
        for (int j = 0; j < 4; ++j)
            basis[s * count + span - 3 + j] = values[j];
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                gram[i * n + j] += basis[s * count + i + 1] * basis[s * count + j + 1];
    }
    for (int i = 0; i < n; ++i)
        for (int j = 0; j <= i; ++j) {
            double a = gram[i * n + j];
            for (int r = 0; r < j; ++r)
                a -= gram[i * n + r] * gram[j * n + r];
            if (i == j) {
                if (!(a > 1e-14))
                    throw std::domain_error("Ricostruzione poli: sistema singolare.");
                gram[i * n + j] = std::sqrt(a);
            } else
                gram[i * n + j] = a / gram[j * n + j];
        }
    for (int s = 0; s < samples; ++s) {
        cancellationPoint();
        std::vector<double> x(n);
        for (int i = 0; i < n; ++i) {
            double a = basis[s * count + i + 1];
            for (int j = 0; j < i; ++j)
                a -= gram[i * n + j] * x[j];
            x[i] = a / gram[i * n + i];
        }
        for (int i = n - 1; i >= 0; --i) {
            double a = x[i];
            for (int j = i + 1; j < n; ++j)
                a -= gram[j * n + i] * x[j];
            x[i] = a / gram[i * n + i];
            map[(i + 1) * samples + s] = x[i];
        }
    }
    for (int i = 1; i < count - 1; ++i) {
        double first = 0, last = 0;
        for (int s = 0; s < samples; ++s) {
        cancellationPoint();
            first += map[i * samples + s] * basis[s * count];
            last += map[i * samples + s] * basis[s * count + count - 1];
        }
        map[i * samples] -= first;
        map[i * samples + samples - 1] -= last;
    }
    map[0] = 1;
    map[(count - 1) * samples + samples - 1] = 1;
    return map;
}
std::vector<double> probes(const BSplineSurface &source, bool u, int count) {
    const auto range = u ? source.uDomain() : source.vDomain();
    auto result = u ? source.uBreakpoints(range) : source.vBreakpoints(range);
    const auto breaks = result;
    for (std::size_t i = 1; i < breaks.size(); ++i)
        result.push_back(breaks[i - 1] + (breaks[i] - breaks[i - 1]) * .5);
    for (int i = 0; i <= 8 * count; ++i)
        result.push_back(range.lo + range.length() * double(i) / (8 * count));
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
} // namespace
SurfaceRefitResult refitSurfacePoles(const BSplineSurface &source, int uCount, int vCount) {
    if (uCount < 4 || vCount < 4 || uCount > surfaceRefitMaxPoles || vCount > surfaceRefitMaxPoles)
        throw std::domain_error("Ricostruzione poli: scegliere da 4 a 256 poli per direzione.");
    const auto u = source.uDomain(), v = source.vDomain();
    const int mu = std::max(33, 4 * uCount + 1), mv = std::max(33, 4 * vCount + 1);
    // Densita' dei nodi guidata dalla differenza quarta delle sezioni: e'
    // soltanto una scelta del fit. L'accettazione usa campioni indipendenti.
    const auto distribution = [&](bool alongU) {
        constexpr int steps = 1024;
        std::vector<double> cumulative(steps + 1, 0.), density(steps + 1, 0.);
        std::vector<std::array<Vec3, 5>> points(steps + 1);
        for (int i = 0; i <= steps; ++i)
            for (int j = 0; j <= 4; ++j) {
                cancellationPoint();
                const double t = double(j) / 4.;
                points[i][j] = alongU ? source.point(u.lo + u.length() * double(i) / steps, v.lo + v.length() * t)
                                      : source.point(u.lo + u.length() * t, v.lo + v.length() * double(i) / steps);
            }
        double sum = 0;
        for (int i = 0; i <= steps; ++i) {
            const int k = std::clamp(i, 2, steps - 2);
            double d = 0;
            for (int j = 0; j <= 4; ++j) {
                const auto q = (points[k - 2][j] - points[k][j]) - 4 * (points[k - 1][j] - points[k][j]) -
                               4 * (points[k + 1][j] - points[k][j]) + (points[k + 2][j] - points[k][j]);
                d = std::max(d, norm(q));
            }
            density[i] = std::pow(d, .25);
            sum += density[i];
        }
        const double floor = sum / (steps + 1) * .05;
        for (int i = 1; i <= steps; ++i)
            cumulative[i] = cumulative[i - 1] + std::max(floor, (density[i - 1] + density[i]) * .5);
        return cumulative;
    };
    const auto du = distribution(true), dv = distribution(false);
    const auto at = [](const std::vector<double> &d, double q) {
        if (q == 0 || q == 1 || d.back() <= 0)
            return q;
        const double wanted = q * d.back();
        const auto it = std::lower_bound(d.begin() + 1, d.end(), wanted);
        const int i = it - d.begin();
        return (i - 1 + (wanted - d[i - 1]) / (d[i] - d[i - 1])) / (d.size() - 1);
    };
    auto ku = knots(uCount), kv = knots(vCount);
    for (auto &t : ku)
        t = at(du, t);
    for (auto &t : kv)
        t = at(dv, t);
    std::vector<double> su(mu), sv(mv);
    for (int i = 0; i < mu; ++i)
        su[i] = at(du, double(i) / (mu - 1));
    for (int i = 0; i < mv; ++i)
        sv[i] = at(dv, double(i) / (mv - 1));
    const auto a = projector(uCount, su, ku), b = projector(vCount, sv, kv);
    const auto origin = source.point(u.lo, v.lo);
    std::vector<Vec3> samples(mu * mv), intermediate(uCount * mv), points(uCount * vCount);
    parallelFor(mu, threadCount(0), [&](std::size_t i) {
        for (int j = 0; j < mv; ++j)
            samples[i * mv + j] = source.point(u.lo + u.length() * su[i], v.lo + v.length() * sv[j]) - origin;
    });
    for (int i = 0; i < uCount; ++i)
        for (int j = 0; j < mv; ++j) {
            long double xyz[3] = {};
            for (int s = 0; s < mu; ++s)
                for (int c = 0; c < 3; ++c)
                    xyz[c] += static_cast<long double>(a[i * mu + s]) * samples[s * mv + j][c];
            intermediate[i * mv + j] = Vec3(double(xyz[0]), double(xyz[1]), double(xyz[2]));
        }
    for (int i = 0; i < uCount; ++i)
        for (int j = 0; j < vCount; ++j) {
            long double xyz[3] = {};
            for (int s = 0; s < mv; ++s)
                for (int c = 0; c < 3; ++c)
                    xyz[c] += static_cast<long double>(b[j * mv + s]) * intermediate[i * mv + s][c];
            points[i * vCount + j] = origin + Vec3(double(xyz[0]), double(xyz[1]), double(xyz[2]));
        }
    for (auto &t : ku)
        t = t == 0 ? u.lo : t == 1 ? u.hi : u.lo + u.length() * t;
    for (auto &t : kv)
        t = t == 0 ? v.lo : t == 1 ? v.hi : v.lo + v.length() * t;
    auto surface =
        std::make_shared<const BSplineSurface>(3, 3, std::move(ku), std::move(kv), uCount, vCount, std::move(points));
    const auto us = probes(source, true, uCount), vs = probes(source, false, vCount);
    if (us.size() > 8000000 / vs.size())
        throw std::domain_error("Ricostruzione poli: dominio troppo complesso per verificare lo scarto.");
    std::vector<double> worst(us.size(), 0.);
    parallelFor(us.size(), threadCount(0), [&](std::size_t i) {
        for (double t : vs) {
            cancellationPoint();
            const double d = distance(source.point(us[i], t), surface->point(us[i], t));
            if (!std::isfinite(d))
                throw std::domain_error("Ricostruzione poli: scarto non finito.");
            worst[i] = std::max(worst[i], d);
        }
    });
    return {surface, *std::max_element(worst.begin(), worst.end())};
}
SurfaceRefitGrid refitSurfacePolesWithinTolerance(const std::vector<BSplineSurfacePtr> &sources, int u, int v,
                                                  double tolerance) {
    if (sources.empty() || sources.size() > 256 || !std::isfinite(tolerance) || tolerance < 1e-7 || tolerance > 1000)
        throw std::domain_error("Ricostruzione poli: facce o scostamento non validi.");
    const auto fit = [&](int nu, int nv) {
        SurfaceRefitGrid grid;
        grid.u = nu; grid.v = nv;
        for (const auto &source : sources) {
            cancellationPoint();
            if (!source)
                throw std::domain_error("Ricostruzione poli: faccia non NURBS.");
            auto result = refitSurfacePoles(*source, nu, nv);
            grid.sampledDeviation = std::max(grid.sampledDeviation, result.sampledDeviation);
            grid.faces.push_back(std::move(result));
        }
        return grid;
    };
    auto best = fit(u, v);
    const auto next = [](int count) { return std::min(surfaceRefitMaxPoles, count + std::max(2, count / 2)); };
    while (best.sampledDeviation > tolerance && (u < surfaceRefitMaxPoles || v < surfaceRefitMaxPoles)) {
        cancellationPoint();
        // Valutazioni indipendenti: non moltiplicare inutilmente anche la
        // direzione che rappresenta gia' correttamente la superficie.
        auto alongU = u < surfaceRefitMaxPoles ? fit(next(u), v) : SurfaceRefitGrid{};
        auto alongV = v < surfaceRefitMaxPoles ? fit(u, next(v)) : SurfaceRefitGrid{};
        if (alongU.faces.empty() || (!alongV.faces.empty() && alongV.sampledDeviation < alongU.sampledDeviation))
            best = std::move(alongV);
        else
            best = std::move(alongU);
        u = best.u; v = best.v;
    }
    return best;
}
} // namespace ForgeCad::Kernel
