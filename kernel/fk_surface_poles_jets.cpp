#include "fk_surface_poles_jets.h"
#include "fk_bspline_basis.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>
#include <stdexcept>
namespace ForgeCad::Kernel {
namespace {
struct Jet {
    std::array<std::vector<SurfacePoleJetTerm>, 3> terms;
    std::array<Vec3, 3> value;
    double w = 0, d = 0, dd = 0;
};
std::vector<Jet> jets(const BSplineSurface &s, SurfacePoleJetSide side) {
    const auto &k = side.axis == 0 ? s.uKnots() : s.vKnots();
    const int p = side.axis == 0 ? s.uDegree() : s.vDegree();
    const int n = side.axis == 0 ? s.uPoleCount() : s.vPoleCount();
    const int count = side.axis == 0 ? s.vPoleCount() : s.uPoleCount();
    const int span =
        side.left ? detail::findSpanLeft(k, p, n, side.parameter) : detail::findSpan(k, p, n, side.parameter);
    std::vector<double> basis(3 * (p + 1));
    detail::basisFunctionDerivatives(k, span, side.parameter, p, std::min(2, p), basis.data());
    std::vector<Jet> result(count);
    for (int i = 0; i < count; ++i) {
        auto &j = result[i];
        const int transverse = side.reversed ? count - 1 - i : i;
        for (int q = 0; q <= p; ++q) {
            const int u = side.axis == 0 ? span - p + q : transverse, v = side.axis == 0 ? transverse : span - p + q;
            const double w = s.weight(u, v);
            j.w += basis[q] * w;
            j.d += basis[p + 1 + q] * w;
            j.dd += basis[2 * (p + 1) + q] * w;
        }
        for (int q = 0; q <= p; ++q) {
            const int u = side.axis == 0 ? span - p + q : transverse, v = side.axis == 0 ? transverse : span - p + q;
            const double w = s.weight(u, v), r = basis[q] * w / j.w;
            const double d = (basis[p + 1 + q] * w - j.d * r) / j.w;
            const double dd = (basis[2 * (p + 1) + q] * w - 2 * j.d * d - j.dd * r) / j.w;
            const double c[3] = {r, d, dd};
            for (int order = 0; order < 3; ++order) {
                j.terms[order].push_back({0, u, v, c[order]});
                j.value[order] += (order == 0 ? s.pole(u, v)
                                              : s.pole(u, v) - s.pole(side.axis == 0 ? 0 : transverse,
                                                                      side.axis == 0 ? transverse : 0)) *
                                  c[order];
            }
        }
    }
    return result;
}
bool near(double a, double b) { return std::abs(a - b) <= 1e-10 * std::max({1., std::abs(a), std::abs(b)}); }
} // namespace
SurfacePoleJetLink surfacePoleJetLink(const BSplineSurface &a, SurfacePoleJetSide sa, const BSplineSurface &b,
                                      SurfacePoleJetSide sb, int order) {
    if(order<1||order>2)throw std::invalid_argument("Jet poli: ordine deve essere G1 o G2.");
    SurfacePoleJetLink link;
    const auto ja = jets(a, sa), jb = jets(b, sb);
    if (ja.size() != jb.size())
        return link;
    for (int side = 0; side < 2; ++side)
        for (const auto &jet : side == 0 ? ja : jb)
            for (auto term : jet.terms[0]) {
                term.side = side;
                link.support.push_back(term);
            }
    link.protectedBand = true;

    // Queste condizioni rendono le relazioni per riga identita' razionali
    // lungo tutto il giunto, non vincoli imposti solo su punti campionati.
    for (std::size_t i = 0; i < ja.size(); ++i) {
        if (!near(ja[i].w / jb[i].w, ja[0].w / jb[0].w) || !near(ja[i].d / ja[i].w, ja[0].d / ja[0].w) ||
            !near(jb[i].d / jb[i].w, jb[0].d / jb[0].w) || !near(ja[i].dd / ja[i].w, ja[0].dd / ja[0].w) ||
            !near(jb[i].dd / jb[i].w, jb[0].dd / jb[0].w))
            return link;
    }
    double numerator = 0, denominator = 0;
    for (std::size_t i = 0; i < ja.size(); ++i) {
        numerator += dot(ja[i].value[1], jb[i].value[1]);
        denominator += squaredNorm(jb[i].value[1]);
    }
    if (denominator < 1e-24)
        return link;
    const double alpha = numerator / denominator;
    if (!std::isfinite(alpha) || std::abs(alpha) < 1e-12)
        return link;
    link.tangent = true;
    double beta = 0;
    for (std::size_t i = 0; i < ja.size(); ++i) {
        const double scale = std::max({1., norm(ja[i].value[1]), norm(jb[i].value[1]) * std::abs(alpha)});
        if (norm(ja[i].value[1] - jb[i].value[1] * alpha) > 1e-8 * scale)
            link.tangent = false;
        beta += dot(ja[i].value[2] - jb[i].value[2] * (alpha * alpha), jb[i].value[1]) / denominator;
    }
    link.curvature = link.tangent;
    for (std::size_t i = 0; i < ja.size(); ++i) {
        const auto expected = jb[i].value[2] * (alpha * alpha) + jb[i].value[1] * beta;
        if (norm(ja[i].value[2] - expected) > 1e-8 * std::max({1., norm(ja[i].value[2]), norm(expected)}))
            link.curvature = false;
    }
    link.compatible = true;
    link.protectedBand = !link.tangent || (order == 2 && !link.curvature);
    for (std::size_t i = 0; i < ja.size(); ++i)
        for (int d = 1; d <= order; ++d) {
            if ((d == 1 && !link.tangent) || (d == 2 && !link.curvature))
                continue;
            SurfacePoleJetRow row{d, {}};
            for (auto term : ja[i].terms[d])
                row.terms.push_back(term);
            for (auto term : jb[i].terms[d]) {
                term.side = 1;
                term.coefficient *= d == 1 ? -alpha : -alpha * alpha;
                row.terms.push_back(term);
            }
            if (d == 2)
                for (auto term : jb[i].terms[1]) {
                    term.side = 1;
                    term.coefficient *= -beta;
                    row.terms.push_back(term);
                }
            link.rows.push_back(std::move(row));
        }
    return link;
}
std::vector<SurfacePoleJetLink> surfacePoleInternalJets(const BSplineSurface &s, int order) {
    if(order<1||order>2)throw std::invalid_argument("Jet poli: ordine deve essere G1 o G2.");
    std::vector<SurfacePoleJetLink> result;
    for (int axis = 0; axis < 2; ++axis) {
        const auto &k = axis == 0 ? s.uKnots() : s.vKnots();
        const int degree = axis == 0 ? s.uDegree() : s.vDegree(), count = axis == 0 ? s.uPoleCount() : s.vPoleCount();
        for (int i = degree + 1; i < count;) {
            int end = i + 1;
            while (end < count && k[end] == k[i])
                ++end;
            if (degree - (end - i) < order)
                result.push_back(
                    surfacePoleJetLink(s, {axis, k[i], true, false}, s, {axis, k[i], false, false}, order));
            i = end;
        }
        if(k.front()!=k[degree]||k.back()!=k[count])continue;
        bool closed = true;
        for (int i = 0; i < (axis == 0 ? s.vPoleCount() : s.uPoleCount()); ++i) {
            const auto a = axis == 0 ? s.pole(0, i) : s.pole(i, 0),
                       b = axis == 0 ? s.pole(count - 1, i) : s.pole(i, count - 1);
            closed = closed && distance(a, b) <= 1e-10;
        }
        if (closed)
            result.push_back(
                surfacePoleJetLink(s, {axis, k[degree], false, false}, s, {axis, k[count], true, false}, order));
        // Ai poli degeneri di sfere/coni non esiste una normale UV unica.
        // Una trasformazione affine comune della fascia mantiene il jet
        // regolare originale, senza fissare il vertice nello spazio.
        for (bool high : {false, true}) {
            const int transverseCount = axis == 0 ? s.vPoleCount() : s.uPoleCount();
            const auto pole = [&](int layer, int i) {
                const int q = high ? count - 1 - layer : layer;
                return SurfacePoleJetTerm{0, axis == 0 ? q : i, axis == 0 ? i : q, 0};
            };
            const auto point = [&](SurfacePoleJetTerm t) { return s.pole(t.u, t.v); };
            const auto origin = pole(0, 0);
            bool collapsed = true;
            for (int i = 1; i < transverseCount; ++i)
                collapsed = collapsed && distance(point(origin), point(pole(0, i))) <= 1e-10;
            if (!collapsed || count < order + 1)
                continue;
            std::vector<SurfacePoleJetTerm> anchors{origin};
            std::vector<Vec3> orthogonal;
            std::vector<std::vector<double>> upper;
            bool regular = true;
            for (int layer = 1; layer <= order; ++layer)
                for (int i = 0; i < transverseCount; ++i) {
                    const auto candidate = pole(layer, i);
                    Vec3 v = point(candidate) - point(origin);
                    std::vector<double> column;
                    for (auto q : orthogonal) {
                        column.push_back(dot(q, v));
                        v -= q * column.back();
                    }
                    const double length = norm(v);
                    if (length > 1e-10 * std::max(1., distance(point(candidate), point(origin))) &&
                        orthogonal.size() < 3) {
                        if (layer == 1 && orthogonal.size() == 2) {
                            regular = false;
                            break;
                        }
                        column.push_back(length);
                        upper.push_back(column);
                        orthogonal.push_back(v / length);
                        anchors.push_back(candidate);
                    }
                }
            if (!regular || orthogonal.size() < 2)
                continue;
            SurfacePoleJetLink link;
            link.compatible = link.tangent = true;
            link.curvature = order == 2;
            for (int layer = 0; layer <= order; ++layer)
                for (int i = 0; i < transverseCount; ++i) {
                    auto t = pole(layer, i);
                    const auto v = point(t) - point(origin);
                    std::vector<double> coefficients(orthogonal.size());
                    for (int j = int(orthogonal.size()) - 1; j >= 0; --j) {
                        double value = dot(orthogonal[j], v);
                        for (int l = j + 1; l < int(orthogonal.size()); ++l)
                            value -= upper[l][j] * coefficients[l];
                        coefficients[j] = value / upper[j][j];
                    }
                    SurfacePoleJetRow row{order, {}};
                    t.coefficient = 1;
                    row.terms.push_back(t);
                    double sum = 0;
                    for (std::size_t j = 0; j < coefficients.size(); ++j) {
                        auto a = anchors[j + 1];
                        a.coefficient = -coefficients[j];
                        row.terms.push_back(a);
                        sum += coefficients[j];
                    }
                    auto a = origin;
                    a.coefficient = sum - 1;
                    row.terms.push_back(a);
                    link.rows.push_back(std::move(row));
                }
            result.push_back(std::move(link));
        }
    }
    return result;
}
double surfacePoleJetResidual(const SurfacePoleJetLink &link, const BSplineSurface &a, const BSplineSurface &b,
                              int order) {
    double result = 0;
    for (const auto &row : link.rows)
        if (row.order == order) {
            Vec3 residual;
            double scale = 0;
            const auto &first = row.terms.front();
            const auto origin = (first.side == 0 ? a : b).pole(first.u, first.v);
            for (const auto &term : row.terms) {
                const auto value = ((term.side == 0 ? a : b).pole(term.u, term.v) - origin) * term.coefficient;
                residual += value;
                scale += norm(value);
            }
            result = std::max(result, norm(residual) / std::max(1., scale));
        }
    return result;
}
} // namespace ForgeCad::Kernel
