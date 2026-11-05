#include "fk_loft_profiles.h"
#include "fk_cancel.h"
#include "fk_nurbs.h"
#include "fk_precision.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ForgeCad::Kernel::detail {
namespace {
using H = std::array<double, 4>;
using Net = std::vector<H>;
using Scalar = std::vector<double>;
constexpr int maximumDegree = 128;
H plus(H a, const H &b, double factor = 1) {
    for (int k = 0; k < 4; ++k)
        a[k] += factor * b[k];
    return a;
}
double choose(int n, int k) {
    double value = 1;
    for (int j = 1; j <= k; ++j)
        value *= double(n - j + 1) / j;
    return value;
}
Net net(const BSplineCurve<3> &c) {
    Net result;
    for (int k = 0; k < c.poleCount(); ++k) {
        const auto p = c.weight(k) * c.poles()[k];
        result.push_back({{p.x(), p.y(), p.z(), c.weight(k)}});
    }
    return result;
}
Net elevate(Net h, int degree) {
    const double constant = h[0][3];
    const bool constantWeight = std::all_of(h.begin(), h.end(), [&](const H &v) { return v[3] == constant; });
    if (degree > maximumDegree)
        throw std::domain_error("loft: grado del denominatore comune eccessivo");
    while (int(h.size()) <= degree) {
        cancellationPoint();
        const int n = int(h.size());
        Net next(n + 1);
        for (int k = 0; k <= n; ++k) {
            if (k)
                next[k] = plus(next[k], h[k - 1], double(k) / n);
            if (k < n)
                next[k] = plus(next[k], h[k], 1 - double(k) / n);
        }
        if (constantWeight)
            for (auto &v : next)
                v[3] = constant;
        h = std::move(next);
    }
    return h;
}
Net multiply(const Net &h, const Scalar &w) {
    const int p = int(h.size()) - 1, q = int(w.size()) - 1;
    if (p + q > maximumDegree)
        throw std::domain_error("loft: grado del denominatore comune eccessivo");
    Net result(p + q + 1);
    for (int i = 0; i <= p; ++i) {
        cancellationPoint();
        for (int j = 0; j <= q; ++j)
            result[i + j] = plus(result[i + j], h[i], w[j] * choose(p, i) * choose(q, j) / choose(p + q, i + j));
    }
    return result;
}
Scalar multiply(const Scalar &a, const Scalar &b) {
    Net h;
    for (double v : a)
        h.push_back({{0, 0, 0, v}});
    h = multiply(h, b);
    Scalar result;
    for (const auto &v : h)
        result.push_back(v[3]);
    return result;
}
Scalar elevate(const Scalar &w, int degree) {
    Net h;
    for (double v : w)
        h.push_back({{0, 0, 0, v}});
    h = elevate(std::move(h), degree);
    Scalar result;
    for (const auto &v : h)
        result.push_back(v[3]);
    return result;
}
BSplineCurve<3> curve(const Net &h, const Interval &d) {
    std::vector<Vec3> poles;
    std::vector<double> weights;
    bool rational = false;
    for (const auto &v : h) {
        for (double x : v)
            if (!std::isfinite(x))
                throw std::domain_error("loft: numeri non finiti nel denominatore comune");
        if (!(v[3] > 0))
            throw std::domain_error("loft: denominatore comune non positivo");
        const Vec3 p = Vec3(v[0], v[1], v[2]) / v[3];
        if (!std::isfinite(norm(p)))
            throw std::domain_error("loft: poli non finiti nel denominatore comune");
        poles.push_back(p);
        weights.push_back(v[3]);
        rational = rational || v[3] != 1;
    }
    std::vector<double> knots(h.size(), d.lo);
    knots.insert(knots.end(), h.size(), d.hi);
    return BSplineCurve<3>(int(h.size()) - 1, knots, poles, rational ? weights : std::vector<double>{});
}
Net compose(const Net &h, double a, double b) {
    const double constant = h[0][3];
    const bool constantWeight = std::all_of(h.begin(), h.end(), [&](const H &v) { return v[3] == constant; });
    const Scalar map{0, a / 3, 1 - b / 3, 1}, complement{1, 1 - a / 3, b / 3, 0};
    std::vector<Net> values;
    for (const auto &v : h)
        values.push_back({v});
    while (values.size() > 1) {
        cancellationPoint();
        std::vector<Net> next;
        for (std::size_t k = 0; k + 1 < values.size(); ++k) {
            auto v = multiply(values[k], complement);
            const auto w = multiply(values[k + 1], map);
            for (std::size_t j = 0; j < v.size(); ++j)
                v[j] = plus(v[j], w[j]);
            next.push_back(std::move(v));
        }
        values = std::move(next);
    }
    if (constantWeight)
        for (auto &v : values.front())
            v[3] = constant;
    return values.front();
}
} // namespace
std::vector<BSplineCurve<3>> loftParameterPieces(const Curve<3> &source, const Interval &range, int degree) {
    std::vector<BSplineCurve<3>> result;
    for (const auto &piece : rationalBezierPieces(source, range))
        result.push_back(curve(elevate(net(piece), std::max(degree, piece.degree())), piece.domain()));
    return result;
}
int normalizeLoftParameterJoins(std::vector<std::vector<BSplineCurve<3>>> &pieces,
                                const std::vector<std::vector<double>> &breaks) {
    int degree = pieces.front().front().degree();
    std::vector<std::vector<std::array<double, 2>>> slopes(pieces.size());
    bool changed = false;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        slopes[i].assign(pieces[i].size(), {{1, 1}});
        for (std::size_t k = 1; k < pieces[i].size(); ++k) {
            cancellationPoint();
            // Una campata di pochi ulp puo' avere geometria distinta, ma non
            // ascisse sufficienti per imporre jet piu' piccoli agli estremi.
            const double beforeSpan = breaks[i][k] - breaks[i][k - 1], afterSpan = breaks[i][k + 1] - breaks[i][k];
            if (std::min(beforeSpan, afterSpan) <= 128 * std::numeric_limits<double>::epsilon())
                continue;
            const auto &a = pieces[i][k - 1], &b = pieces[i][k];
            Vec3 left[2], right[2];
            a.evaluateLeft(a.domain().hi, 1, left);
            b.evaluate(b.domain().lo, 1, right);
            const double before = norm(left[1]) * a.domain().length() / beforeSpan,
                         after = norm(right[1]) * b.domain().length() / afterSpan;
            if (!(std::min(before, after) > 0))
                continue;
            const double coordinateScale = std::max({1.0, norm(a.poles().back()), norm(b.poles().front())});
            const double roundoff = 128 * std::numeric_limits<double>::epsilon() * degree * coordinateScale /
                                    std::min(beforeSpan, afterSpan);
            if (std::fabs(before - after) <= roundoff)
                continue;
            const double speed = std::min(before, after);
            slopes[i][k - 1][1] = speed / before;
            slopes[i][k][0] = speed / after;
            changed = true;
        }
    }
    if (changed) {
        degree *= 3;
        for (std::size_t i = 0; i < pieces.size(); ++i)
            for (std::size_t k = 0; k < pieces[i].size(); ++k)
                pieces[i][k] =
                    curve(compose(net(pieces[i][k]), slopes[i][k][0], slopes[i][k][1]), pieces[i][k].domain());
    }
    return degree;
}
void commonLoftDenominator(std::vector<BSplineCurve<3>> &curves, bool blend, std::vector<double> &leftBlend,
                           std::vector<double> &rightBlend, double blendStart, double blendEnd) {
    std::vector<std::vector<BSplineCurve<3>>> source;
    for (const auto &c : curves)
        source.push_back(c.bezierSegments());
    const std::size_t count = source.front().size(), n = curves.size();
    std::vector<std::vector<Net>> result(n);
    std::vector<Scalar> right, left;
    int degree = 0;
    const auto sameWeights = [&]() {
        for (std::size_t i = 1; i < n; ++i)
            if (curves[i].weights() != curves[0].weights())
                return false;
        return true;
    };
    const bool same = sameWeights();
    std::vector<double> breaks{source.front().front().domain().lo};
    for (const auto &piece : source.front())
        breaks.push_back(piece.domain().hi);
    // Tutte le sezioni ricevono il medesimo fattore: normalizza gli estremi
    // senza la riparametrizzazione di Moebius di ogni Bezier separato.
    double w0 = 1, w1 = 1;
    if (same) {
        w0 = curves[0].weight(0);
        w1 = curves[0].weight(curves[0].poleCount() - 1);
    } else
        for (const auto &c : curves) {
            w0 *= c.weight(0);
            w1 *= c.weight(c.poleCount() - 1);
        }
    if (!(w0 > 0 && w1 > 0 && std::isfinite(w0) && std::isfinite(w1)))
        throw std::domain_error("loft: denominatore comune fuori intervallo numerico");
    for (std::size_t k = 0; k < count; ++k) {
        cancellationPoint();
        std::vector<Scalar> weights;
        for (std::size_t i = 0; i < n; ++i) {
            if (source[i].size() != count)
                throw std::logic_error("loft: pezze non compatibili nel denominatore");
            const auto &c = source[i][k];
            Scalar w;
            if (c.isRational())
                for (int j = 0; j < c.poleCount(); ++j)
                    w.push_back(c.weight(j));
            else
                w = {1};
            weights.push_back(std::move(w));
        }
        Scalar denominator = same ? weights[0] : Scalar{1};
        if (!same)
            for (const auto &w : weights)
                denominator = multiply(denominator, w);
        std::vector<Net> hs;
        int localDegree = 0;
        for (std::size_t i = 0; i < n; ++i) {
            auto h = net(source[i][k]);
            if (!same)
                for (std::size_t j = 0; j < n; ++j)
                    if (j != i)
                        h = multiply(h, weights[j]);
            localDegree = std::max(localDegree, int(h.size()) - 1);
            hs.push_back(std::move(h));
        }
        for (auto &h : hs)
            h = elevate(std::move(h), localDegree);
        denominator = elevate(denominator, localDegree);
        if (w0 != 1 || w1 != 1) {
            const double u0 = breaks[k], u1 = breaks[k + 1];
            const Scalar factor{(1 - u0) / w0 + u0 / w1, (1 - u1) / w0 + u1 / w1};
            for (auto &h : hs)
                h = multiply(h, factor);
            denominator = multiply(denominator, factor);
            ++localDegree;
        }
        Scalar r;
        if (blend) {
            const double a = blendStart + (blendEnd-blendStart)*breaks[k];
            const double b = blendStart + (blendEnd-blendStart)*breaks[k+1], h = b-a;
            const auto smooth = [](double u) { return u * u * (3 - 2 * u); };
            const auto derivative = [](double u) { return 6 * u * (1 - u); };
            r = multiply(denominator, Scalar{smooth(a), smooth(a) + h * derivative(a) / 3,
                                             smooth(b) - h * derivative(b) / 3, smooth(b)});
            localDegree += 3;
            for (auto &h : hs)
                h = elevate(std::move(h), localDegree);
            denominator = elevate(denominator, localDegree);
        }
        for (auto &h : hs)
            for (std::size_t j = 0; j < h.size(); ++j)
                h[j][3] = denominator[j];
        Scalar l = denominator;
        if (blend)
            for (std::size_t j = 0; j < l.size(); ++j)
                l[j] -= r[j];
        for (std::size_t i = 0; i < n; ++i)
            result[i].push_back(std::move(hs[i]));
        right.push_back(std::move(r));
        left.push_back(std::move(l));
        degree = std::max(degree, localDegree);
    }
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<BSplineCurve<3>> pieces;
        for (std::size_t k = 0; k < count; ++k)
            pieces.push_back(curve(elevate(std::move(result[i][k]), degree), {breaks[k], breaks[k + 1]}));
        curves[i] = joinBezierPieces(pieces, breaks);
    }
    leftBlend.clear();
    rightBlend.clear();
    if (blend)
        for (std::size_t k = 0; k < count; ++k) {
            const auto l = elevate(left[k], degree), r = elevate(right[k], degree);
            leftBlend.insert(leftBlend.end(), l.begin() + (k ? 1 : 0), l.end());
            rightBlend.insert(rightBlend.end(), r.begin() + (k ? 1 : 0), r.end());
        }
}
} // namespace ForgeCad::Kernel::detail
