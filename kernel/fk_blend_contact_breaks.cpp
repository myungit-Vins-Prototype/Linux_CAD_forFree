#include "fk_blend_contact_breaks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include "fk_bspline_surface.h"

namespace ForgeCad::Kernel::detail {
namespace {

std::vector<std::pair<int, double>> contactKnots(const Surface &a, const Surface &b) {
    std::vector<std::pair<int, double>> result;
    for (int side = 0; side < 2; ++side) {
        const auto &surface = side ? b : a;
        if (surface.type() != SurfaceType::BSpline) continue;
        const auto &s = static_cast<const BSplineSurface &>(surface);
        for (int axis = 0; axis < 2; ++axis) {
            const auto &knots = axis ? s.vKnots() : s.uKnots();
            const auto domain = axis ? s.vDomain() : s.uDomain();
            const int degree = axis ? s.vDegree() : s.uDegree();
            for (std::size_t i = 0; i < knots.size();) {
                std::size_t end = i + 1;
                while (end < knots.size() && knots[end] == knots[i]) ++end;
                if (knots[i] > domain.lo && knots[i] < domain.hi && int(end - i) >= degree - 1)
                    result.emplace_back(2 * side + axis, knots[i]);
                i = end;
            }
        }
    }
    return result;
}

// Una cucitura G1 puo' cambiare velocita' parametrica: il fit va diviso,
// non levigato. Una vera discontinuita' della normale resta un errore.
// Confrontare i due pezzi Bezier esattamente sul nodo evita differenze
// finite che attraverserebbero altre linee di nodo molto vicine.
bool continuousContact(const Surface &surface, const BlendContactSample &at, int coordinate, double knot) {
    const auto &s = static_cast<const BSplineSurface &>(surface);
    const auto us = s.uBreakpoints(s.uDomain()), vs = s.vBreakpoints(s.vDomain());
    const bool fixedU = coordinate % 2 == 0;
    const auto &lines = fixedU ? us : vs;
    const auto found = std::find(lines.begin(), lines.end(), knot);
    if (found == lines.end() || found == lines.begin() || found + 1 == lines.end()) return false;
    const std::size_t index = std::size_t(found - lines.begin());
    const auto &transverse = fixedU ? vs : us;
    const double value = at.uv[std::size_t(coordinate ^ 1)];
    const auto span = std::size_t(std::clamp(
        std::upper_bound(transverse.begin(), transverse.end(), value) - transverse.begin() - 1,
        std::ptrdiff_t(0), std::ptrdiff_t(transverse.size() - 2)));
    const auto pieces = s.cachedBezierPatches();
    const std::size_t nv = vs.size() - 1;
    const auto &left = (*pieces)[fixedU ? (index - 1) * nv + span : span * nv + index - 1];
    const auto &right = (*pieces)[fixedU ? index * nv + span : span * nv + index];
    const auto na = left.normal(fixedU ? knot : value, fixedU ? value : knot);
    const auto nb = right.normal(fixedU ? knot : value, fixedU ? value : knot);
    // Filtro per il tentativo con eventi, non un budget geometrico del fit.
    return norm(na - nb) <= 1e-7;
}

}

std::vector<double> blendContactBreaks(const Surface &a, const Surface &b,
    const Interval &range, const std::vector<double> &edgeBreaks,
    const std::function<BlendContactSample(double)> &sample) {
    const auto knots = contactKnots(a, b);
    if (knots.empty()) return {};
    std::vector<double> probes;
    for (std::size_t k = 1; k < edgeBreaks.size(); ++k) {
        const int count = std::max(1, int(std::ceil(16 * (edgeBreaks[k] - edgeBreaks[k - 1]) / range.length())));
        for (int j = 0; j < count; ++j)
            probes.push_back(edgeBreaks[k - 1] + (edgeBreaks[k] - edgeBreaks[k - 1]) * j / count);
    }
    probes.push_back(range.hi);
    std::vector<double> result;
    const auto add = [&](double t, int coordinate, double knot) {
        if (!(t > range.lo && t < range.hi)) return;
        if (!continuousContact(coordinate < 2 ? a : b, sample(t), coordinate, knot))
            throw std::domain_error("blendEdges: contatto su una discontinuita' interna della normale");
        result.push_back(t);
    };
    for (std::size_t k = 1; k < probes.size(); ++k) {
        const auto first = sample(probes[k - 1]), last = sample(probes[k]);
        for (const auto &[coordinate, knot] : knots) {
            const auto c = std::size_t(coordinate);
            double lo = probes[k - 1], hi = probes[k];
            double fl = first.uv[c] - knot, fh = last.uv[c] - knot;
            if (fl == 0.0 && fh == 0.0) continue; // contatto lungo il nodo, senza attraversamento
            if (fl == 0.0) add(lo, coordinate, knot);
            if (fh == 0.0) add(hi, coordinate, knot);
            if (!(fl * fh < 0.0)) continue;
            double t = lo + (hi - lo) * (-fl) / (fh - fl);
            for (int iteration = 0; iteration < 48; ++iteration) {
                const auto at = sample(t);
                const double f = at.uv[c] - knot;
                if (std::fabs(f) <= 2e-15 * (1 + std::fabs(knot)) || hi - lo < 1e-14 * range.length()) break;
                if (f * fl > 0) { lo = t; fl = f; }
                else { hi = t; fh = f; }
                const double next = t - f / at.derivative[c];
                t = std::isfinite(next) && next > lo && next < hi ? next : 0.5 * (lo + hi);
            }
            add(t, coordinate, knot);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

}
