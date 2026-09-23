#include "fk_curve_algo.h"

#include <algorithm>

#include "fk_bernstein.h"
#include "fk_bspline.h"
#include "fk_precision.h"
#include "fk_quadrature.h"

namespace ForgeCad::Kernel {
namespace {

constexpr int kSamplesPerSpan = 16;

void requireFinite(const Interval &range) {
    if (!range.isFinite() || !(range.lo <= range.hi)) throw std::invalid_argument("intervallo non finito o vuoto");
}

template <int N>
CurveProjection<N> projectionAt(const Curve<N> &curve, const Vec<N> &p, double t) {
    const Vec<N> point = curve.point(t);
    return {t, point, distance(point, p)};
}

// Newton su f(t) = C'(t) . (C(t) - p) = 0, con passo di Gauss-Newton dove
// f' <= 0 e dimezzamento del passo se la distanza non diminuisce. Resta nel
// tratto liscio `range`: un minimo nel tratto vicino si trova partendo dai
// campioni di quel tratto, uno su uno spigolo (nodo C0) e' un breakpoint.
template <int N>
double refineProjection(const Curve<N> &curve, const Vec<N> &p, double t, const Interval &range) {
    // All'estremo destro del tratto servono le derivate del tratto stesso.
    const auto evaluate = [&](double u, Vec<N> *out) {
        if (u == range.hi) curve.evaluateLeft(u, 2, out);
        else curve.evaluate(u, 2, out);
    };
    Vec<N> d[3];
    evaluate(t, d);
    double current = squaredNorm(d[0] - p);
    for (int iteration = 0; iteration < 100; ++iteration) {
        const Vec<N> diff = d[0] - p;
        const double f = dot(d[1], diff);
        double fp = dot(d[2], diff) + squaredNorm(d[1]);
        if (!(fp > 0.0)) fp = squaredNorm(d[1]);
        if (!(fp > 0.0)) break;
        double step = -f / fp;
        double next = range.clamp(t + step);
        Vec<N> nd[3];
        evaluate(next, nd);
        double candidate = squaredNorm(nd[0] - p);
        // Vicino al minimo la distanza varia meno del suo arrotondamento:
        // si confronta con un margine, altrimenti Newton si fermerebbe presto.
        const double noise = 64.0 * kMachineEpsilon * (current + squaredNorm(d[0]) + squaredNorm(p));
        for (int halving = 0; halving < 40 && candidate > current + noise; ++halving) {
            step *= 0.5;
            next = range.clamp(t + step);
            evaluate(next, nd);
            candidate = squaredNorm(nd[0] - p);
        }
        if (candidate > current + noise) break;
        const double change = std::fabs(next - t);
        t = next;
        current = candidate;
        std::copy(nd, nd + 3, d);
        if (change <= 4.0 * kMachineEpsilon * std::max(1.0, std::fabs(t))) break;
    }
    return t;
}

template <int N>
using Candidates = std::vector<CurveProjection<N>>;

template <int N>
void addCandidate(const Curve<N> &curve, const Vec<N> &p, double t, Candidates<N> &out) {
    out.push_back(projectionAt(curve, p, t));
}

template <int N>
void lineCandidates(const Line<N> &line, const Vec<N> &p, const Interval &range, Candidates<N> &out) {
    addCandidate<N>(line, p, range.clamp(dot(p - line.origin(), line.direction())), out);
}

// Angolo di p nell'arco, altrimenti (fuori dall'arco la distanza cresce
// allontanandosi dall'angolo di p) i due estremi.
template <int N>
void circleCandidates(const Circle<N> &circle, const Vec<N> &p, const Interval &range, Candidates<N> &out) {
    const Vec<N> v = p - circle.center();
    const double x = dot(v, circle.xAxis());
    const double y = dot(v, circle.yAxis());
    if (x * x + y * y > 0.0) {  // al centro tutti i punti sono equidistanti
        double shifted = std::atan2(y, x) - range.lo;
        shifted -= kTwoPi * std::floor(shifted / kTwoPi);
        if (range.lo + shifted <= range.hi) addCandidate<N>(circle, p, range.lo + shifted, out);
    }
    addCandidate<N>(circle, p, range.lo, out);
    addCandidate<N>(circle, p, range.hi, out);
}

template <int N>
void genericCandidates(const Curve<N> &curve, const Vec<N> &p, const Interval &range, Candidates<N> &out) {
    addCandidate(curve, p, range.hi, out);
    const std::vector<double> breaks = curve.breakpoints(range);
    double samples[kSamplesPerSpan + 1];
    double squared[kSamplesPerSpan + 1];
    for (std::size_t span = 0; span + 1 < breaks.size(); ++span) {
        const double a = breaks[span], b = breaks[span + 1];
        addCandidate(curve, p, a, out);  // estremo o possibile spigolo
        if (!(b > a)) continue;
        for (int i = 0; i <= kSamplesPerSpan; ++i) {
            samples[i] = i == kSamplesPerSpan ? b : a + (b - a) * i / kSamplesPerSpan;
            squared[i] = squaredNorm(curve.point(samples[i]) - p);
        }
        for (int i = 0; i <= kSamplesPerSpan; ++i) {
            const bool leftOk = i == 0 || squared[i] <= squared[i - 1];
            const bool rightOk = i == kSamplesPerSpan || squared[i] <= squared[i + 1];
            if (leftOk && rightOk) addCandidate(curve, p, refineProjection(curve, p, samples[i], Interval{a, b}), out);
        }
    }
}

// Su ogni tratto di Bezier la condizione di stazionarieta' C'(t).(C(t) - p) = 0
// ha per numeratore un polinomio: A' . (A - p) di grado 2p - 1 per le curve
// polinomiali, (A' w - A w') . (A - p w) di grado 3p - 1 per le razionali
// (C = A / w in coordinate omogenee). Se ne trovano *tutte* le radici con
// l'isolamento di Bernstein, poi le si rifinisce con Newton sulla curva.
template <int N>
void bsplineCandidates(const BSplineCurve<N> &curve, const Vec<N> &p, const Interval &range, Candidates<N> &out) {
    addCandidate<N>(curve, p, range.hi, out);
    for (const BSplineCurve<N> &segment : curve.bezierSegments()) {
        const Interval span = segment.domain();
        const Interval local{std::max(span.lo, range.lo), std::min(span.hi, range.hi)};
        if (!(local.lo <= local.hi)) continue;
        addCandidate<N>(curve, p, local.lo, out);  // estremo o possibile spigolo

        const int degree = segment.degree();
        std::vector<double> weights(degree + 1);
        std::vector<std::vector<double>> homogeneous(N, std::vector<double>(degree + 1));
        for (int i = 0; i <= degree; ++i) {
            weights[i] = segment.weight(i);
            for (int c = 0; c < N; ++c) homogeneous[c][i] = weights[i] * segment.poles()[i][c];
        }
        const BernsteinPolynomial w(weights);
        BernsteinPolynomial stationary({0.0});
        for (int c = 0; c < N; ++c) {
            const BernsteinPolynomial a(homogeneous[c]);
            if (curve.isRational())
                stationary = stationary + (a.derivative() * w - a * w.derivative()) * (a - w * p[c]);
            else
                stationary = stationary + a.derivative() * (a - BernsteinPolynomial({p[c]}));
        }
        for (double s : stationary.roots()) {
            const double t = span.lo + s * (span.hi - span.lo);
            if (local.contains(t)) addCandidate<N>(curve, p, refineProjection<N>(curve, p, t, local), out);
        }
    }
}

template <int N>
void collectCandidates(const Curve<N> &curve, const Vec<N> &p, const Interval &range, Candidates<N> &out) {
    if (!(range.lo <= range.hi)) throw std::invalid_argument("projectPoint: intervallo vuoto");
    switch (curve.type()) {
    case CurveType::Line:  // anche su intervalli infiniti
        lineCandidates(static_cast<const Line<N> &>(curve), p, range, out);
        return;
    case CurveType::Trimmed: {
        const auto &trimmed = static_cast<const TrimmedCurve<N> &>(curve);
        const Interval domain = trimmed.domain();
        collectCandidates(*trimmed.basis(), p, Interval{std::max(range.lo, domain.lo), std::min(range.hi, domain.hi)}, out);
        return;
    }
    case CurveType::Transformed:
        if constexpr (N == 3) {
            // Una similitudine conserva i rapporti tra distanze: si lavora
            // sulla curva base con il punto riportato indietro.
            const auto &transformed = static_cast<const TransformedCurve &>(curve);
            double scale = 1.0;
            if (transformed.transform().isSimilarity(&scale)) {
                const std::size_t first = out.size();
                collectCandidates(*transformed.basis(), transformed.transform().inverted().applyToPoint(p), range, out);
                for (std::size_t i = first; i < out.size(); ++i) {
                    out[i].point = transformed.transform().applyToPoint(out[i].point);
                    out[i].distance *= scale;
                }
                return;
            }
        }
        break;
    default:
        break;
    }
    requireFinite(range);
    switch (curve.type()) {
    case CurveType::Circle:
        circleCandidates(static_cast<const Circle<N> &>(curve), p, range, out);
        return;
    case CurveType::BSpline:
        bsplineCandidates(static_cast<const BSplineCurve<N> &>(curve), p, range, out);
        return;
    default:
        genericCandidates(curve, p, range, out);
        return;
    }
}

}

template <int N>
std::vector<CurveProjection<N>> projectPointCandidates(const Curve<N> &curve, const Vec<N> &p, const Interval &range) {
    Candidates<N> result;
    collectCandidates(curve, p, range, result);
    return result;
}

template <int N>
CurveProjection<N> projectPoint(const Curve<N> &curve, const Vec<N> &p, const Interval &range) {
    const Candidates<N> candidates = projectPointCandidates(curve, p, range);
    return *std::min_element(candidates.begin(), candidates.end(),
                             [](const CurveProjection<N> &a, const CurveProjection<N> &b) { return a.distance < b.distance; });
}

template <int N>
CurveProjection<N> projectPoint(const Curve<N> &curve, const Vec<N> &p) {
    return projectPoint(curve, p, curve.domain());
}

template <int N>
double arcLength(const Curve<N> &curve, const Interval &range, double tolerance) {
    requireFinite(range);
    const auto speed = [&curve](double t) {
        Vec<N> d[2];
        curve.evaluate(t, 1, d);
        return norm(d[1]);
    };
    const std::vector<double> breaks = curve.breakpoints(range);
    const double spanTolerance = tolerance / std::max<std::size_t>(1, breaks.size() - 1);
    double length = 0.0;
    for (std::size_t span = 0; span + 1 < breaks.size(); ++span)
        if (breaks[span + 1] > breaks[span])
            length += detail::integrate(speed, breaks[span], breaks[span + 1], spanTolerance);
    return length;
}

template <int N>
double arcLength(const Curve<N> &curve, double tolerance) {
    return arcLength(curve, curve.domain(), tolerance);
}

template std::vector<CurveProjection<2>> projectPointCandidates<2>(const Curve<2> &, const Vec2 &, const Interval &);
template std::vector<CurveProjection<3>> projectPointCandidates<3>(const Curve<3> &, const Vec3 &, const Interval &);
template CurveProjection<2> projectPoint<2>(const Curve<2> &, const Vec2 &, const Interval &);
template CurveProjection<3> projectPoint<3>(const Curve<3> &, const Vec3 &, const Interval &);
template CurveProjection<2> projectPoint<2>(const Curve<2> &, const Vec2 &);
template CurveProjection<3> projectPoint<3>(const Curve<3> &, const Vec3 &);
template double arcLength<2>(const Curve<2> &, const Interval &, double);
template double arcLength<3>(const Curve<3> &, const Interval &, double);
template double arcLength<2>(const Curve<2> &, double);
template double arcLength<3>(const Curve<3> &, double);

}
