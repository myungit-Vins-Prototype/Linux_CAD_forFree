#include "fk_bspline.h"

#include <algorithm>
#include <atomic>

#include "fk_bspline_basis.h"
#include "fk_precision.h"

namespace ForgeCad::Kernel {
using detail::binomial;

template <int N>
BSplineCurve<N>::BSplineCurve(int degree, std::vector<double> knots, std::vector<Vec<N>> poles, std::vector<double> weights)
    : degree_(degree), knots_(std::move(knots)), poles_(std::move(poles)), weights_(std::move(weights)) {
    validate();
}

template <int N>
void BSplineCurve<N>::validate() const {
    detail::validateKnotVector(knots_, degree_, poleCount(), "BSplineCurve");
    for (const Vec<N> &pole : poles_)
        if (!isFinite(pole)) throw std::invalid_argument("BSplineCurve: polo non finito");
    if (!weights_.empty()) {
        if (int(weights_.size()) != poleCount()) throw std::invalid_argument("BSplineCurve: numero di pesi errato");
        for (double w : weights_)
            if (!(w > 0.0) || !std::isfinite(w)) throw std::invalid_argument("BSplineCurve: pesi non positivi");
    }
}

template <int N>
Interval BSplineCurve<N>::domain() const {
    return {knots_[degree_], knots_[poleCount()]};
}

template <int N>
bool BSplineCurve<N>::isClamped() const {
    const int p = degree_;
    const int n = poleCount() - 1;
    for (int i = 0; i <= p; ++i) {
        if (knots_[i] != knots_[p]) return false;
        if (knots_[n + 1 + i] != knots_[n + 1]) return false;
    }
    return true;
}

template <int N>
int BSplineCurve<N>::findSpan(double t) const {
    return detail::findSpan(knots_, degree_, poleCount(), t);
}

template <int N>
int BSplineCurve<N>::findSpanLeft(double t) const {
    return detail::findSpanLeft(knots_, degree_, poleCount(), t);
}

template <int N>
int BSplineCurve<N>::multiplicity(double u) const {
    return int(std::count(knots_.begin(), knots_.end(), u));
}

template <int N>
void BSplineCurve<N>::evaluate(double t, int order, Vec<N> *out) const {
    evaluateSpan(findSpan(t), t, order, out);
}

template <int N>
void BSplineCurve<N>::evaluateLeft(double t, int order, Vec<N> *out) const {
    evaluateSpan(findSpanLeft(t), t, order, out);
}

template <int N>
void BSplineCurve<N>::evaluateSpan(int span, double t, int order, Vec<N> *out) const {
    const int p = degree_;
    const int basisOrder = std::min(order, p);
    detail::ScratchBuffer<double> ders((basisOrder + 1) * (p + 1));
    detail::basisFunctionDerivatives(knots_, span, t, p, basisOrder, ders.data());

    if (!isRational()) {
        for (int k = 0; k <= order; ++k) {
            Vec<N> value;
            if (k <= basisOrder)
                for (int j = 0; j <= p; ++j) value += ders[k * (p + 1) + j] * poles_[span - p + j];
            out[k] = value;
        }
        return;
    }

    // Derivate razionali (A4.2): A(t) = sum N w P, w(t) = sum N w,
    // C^(k) = (A^(k) - sum_{i=1..k} C(k,i) w^(i) C^(k-i)) / w.
    detail::ScratchBuffer<Vec<N>, 8> a(order + 1);
    detail::ScratchBuffer<double, 8> w(order + 1);
    for (int k = 0; k <= basisOrder; ++k) {
        for (int j = 0; j <= p; ++j) {
            const int index = span - p + j;
            const double b = ders[k * (p + 1) + j] * weights_[index];
            a[k] += b * poles_[index];
            w[k] += b;
        }
    }
    for (int k = 0; k <= order; ++k) {
        Vec<N> value = a[k];
        for (int i = 1; i <= k; ++i) value -= binomial(k, i) * w[i] * out[k - i];
        out[k] = value / w[0];
    }
}

template <int N>
std::vector<double> BSplineCurve<N>::breakpoints(const Interval &range) const {
    return detail::knotBreakpoints(knots_, range);
}

template <int N>
BSplineCurve<N> BSplineCurve<N>::insertKnot(double u, int times) const {
    const Interval dom = domain();
    if (!(u >= dom.lo && u <= dom.hi)) throw std::invalid_argument("insertKnot: il nodo deve stare nel dominio");
    const int p = degree_;
    const int np = poleCount() - 1;
    // Ultimo indice con knots[k] <= u: coincide con findSpan all'interno,
    // e agli estremi del dominio e' l'ultimo nodo del gruppo uguale a u.
    const int k = int(std::upper_bound(knots_.begin(), knots_.end(), u) - knots_.begin()) - 1;
    const int s = multiplicity(u);
    const int r = std::min(times, p - s);
    if (r <= 0) return *this;

    // Si lavora in coordinate omogenee (w P, w).
    std::vector<Vec<N>> pw(np + 1);
    std::vector<double> pwt(np + 1);
    for (int i = 0; i <= np; ++i) {
        pwt[i] = weight(i);
        pw[i] = poles_[i] * pwt[i];
    }

    const int mp = np + p + 1;
    const int nq = np + r;
    std::vector<double> uq(mp + r + 1);
    for (int i = 0; i <= k; ++i) uq[i] = knots_[i];
    for (int i = 1; i <= r; ++i) uq[k + i] = u;
    for (int i = k + 1; i <= mp; ++i) uq[i + r] = knots_[i];

    std::vector<Vec<N>> qw(nq + 1);
    std::vector<double> qwt(nq + 1);
    for (int i = 0; i <= k - p; ++i) { qw[i] = pw[i]; qwt[i] = pwt[i]; }
    for (int i = k - s; i <= np; ++i) { qw[i + r] = pw[i]; qwt[i + r] = pwt[i]; }

    std::vector<Vec<N>> rw(p - s + 1);
    std::vector<double> rwt(p - s + 1);
    for (int i = 0; i <= p - s; ++i) { rw[i] = pw[k - p + i]; rwt[i] = pwt[k - p + i]; }

    int L = 0;
    for (int j = 1; j <= r; ++j) {
        L = k - p + j;
        for (int i = 0; i <= p - j - s; ++i) {
            const double alpha = (u - knots_[L + i]) / (knots_[i + k + 1] - knots_[L + i]);
            rw[i] = alpha * rw[i + 1] + (1.0 - alpha) * rw[i];
            rwt[i] = alpha * rwt[i + 1] + (1.0 - alpha) * rwt[i];
        }
        qw[L] = rw[0];
        qwt[L] = rwt[0];
        qw[k + r - j - s] = rw[p - j - s];
        qwt[k + r - j - s] = rwt[p - j - s];
    }
    for (int i = L + 1; i < k - s; ++i) { qw[i] = rw[i - L]; qwt[i] = rwt[i - L]; }

    std::vector<Vec<N>> poles(nq + 1);
    for (int i = 0; i <= nq; ++i) poles[i] = qw[i] / qwt[i];
    std::vector<double> weights;
    if (isRational()) weights = qwt;
    return BSplineCurve(p, std::move(uq), std::move(poles), std::move(weights));
}

template <int N>
BSplineCurve<N> BSplineCurve<N>::clamped() const {
    if (isClamped()) return *this;
    const int p = degree_;
    const Interval dom = domain();
    const BSplineCurve refined = insertKnot(dom.lo, p).insertKnot(dom.hi, p);
    const std::vector<double> &u = refined.knots_;
    const int first = int(std::lower_bound(u.begin(), u.end(), dom.lo) - u.begin());
    const int last = int(std::upper_bound(u.begin(), u.end(), dom.hi) - u.begin()) - 1;
    const int missingFirst = p + 1 - refined.multiplicity(dom.lo);
    const int missingLast = p + 1 - refined.multiplicity(dom.hi);

    std::vector<double> knots(missingFirst, dom.lo);
    knots.insert(knots.end(), u.begin() + first, u.begin() + last + 1);
    knots.insert(knots.end(), missingLast, dom.hi);
    const int start = first - missingFirst;
    const int count = int(knots.size()) - p - 1;
    std::vector<Vec<N>> poles(refined.poles_.begin() + start, refined.poles_.begin() + start + count);
    std::vector<double> weights;
    if (isRational()) weights.assign(refined.weights_.begin() + start, refined.weights_.begin() + start + count);
    return BSplineCurve(p, std::move(knots), std::move(poles), std::move(weights));
}

template <int N>
std::vector<BSplineCurve<N>> BSplineCurve<N>::bezierSegments() const {
    // Ogni tratto dipende solo dai suoi p + 1 poli e dai 2p + 2 nodi vicini:
    // la curva locale, resa clamped, e' il tratto di Bezier. Costo lineare nel
    // numero dei tratti (inserire i nodi sulla curva intera costerebbe il quadrato).
    const int p = degree_;
    std::vector<BSplineCurve> result;
    for (int span = p; span < poleCount(); ++span) {
        if (!(knots_[span + 1] > knots_[span])) continue;
        std::vector<double> knots(knots_.begin() + span - p, knots_.begin() + span + p + 2);
        std::vector<Vec<N>> poles(poles_.begin() + span - p, poles_.begin() + span + 1);
        std::vector<double> weights;
        if (isRational()) weights.assign(weights_.begin() + span - p, weights_.begin() + span + 1);
        result.push_back(BSplineCurve(p, std::move(knots), std::move(poles), std::move(weights)).clamped());
    }
    return result;
}

template <int N>
std::shared_ptr<const std::vector<BSplineCurve<N>>> BSplineCurve<N>::cachedBezierSegments() const {
    std::shared_ptr<const std::vector<BSplineCurve>> segments = std::atomic_load(&bezierCache_);
    if (!segments) {
        segments = std::make_shared<const std::vector<BSplineCurve>>(bezierSegments());
        std::atomic_store(&bezierCache_, segments);
    }
    return segments;
}

std::vector<double> expandKnots(const std::vector<double> &distinctKnots, const std::vector<int> &multiplicities) {
    if (distinctKnots.size() != multiplicities.size()) throw std::invalid_argument("expandKnots: dimensioni diverse");
    std::vector<double> result;
    for (std::size_t i = 0; i < distinctKnots.size(); ++i) result.insert(result.end(), multiplicities[i], distinctKnots[i]);
    return result;
}

template <int N>
BSplineCurve<N> conicArcToBSpline(const Vec<N> &center, const Vec<N> &a, const Vec<N> &b, double first, double last) {
    const double sweep = last - first;
    if (!(sweep > 0.0) || sweep > kTwoPi * (1.0 + 1.0e-12))
        throw std::invalid_argument("conicArcToBSpline: ampiezza dell'arco non valida");
    const int arcs = std::max(1, int(std::ceil(sweep / kHalfPi - 1.0e-12)));
    const double step = sweep / arcs;
    const double middleWeight = std::cos(0.5 * step);

    std::vector<Vec<N>> poles(2 * arcs + 1);
    std::vector<double> weights(2 * arcs + 1, 1.0);
    std::vector<double> knots(3, first);
    for (int i = 0; i <= arcs; ++i) {
        const double angle = i == arcs ? last : first + i * step;
        poles[2 * i] = center + std::cos(angle) * a + std::sin(angle) * b;
        if (i == arcs) break;
        // Polo centrale: intersezione delle tangenti agli estremi del tratto.
        const double middle = angle + 0.5 * step;
        poles[2 * i + 1] = center + (std::cos(middle) * a + std::sin(middle) * b) / middleWeight;
        weights[2 * i + 1] = middleWeight;
        if (i > 0) knots.insert(knots.end(), 2, angle);
    }
    knots.insert(knots.end(), 3, last);
    return BSplineCurve<N>(2, std::move(knots), std::move(poles), std::move(weights));
}

template <int N>
BSplineCurve<N> toBSpline(const Circle<N> &circle, double first, double last) {
    return conicArcToBSpline(circle.center(), circle.radius() * circle.xAxis(), circle.radius() * circle.yAxis(), first, last);
}

template <int N>
BSplineCurve<N> toBSpline(const Ellipse<N> &ellipse, double first, double last) {
    return conicArcToBSpline(ellipse.center(), ellipse.xRadius() * ellipse.xAxis(), ellipse.yRadius() * ellipse.yAxis(), first, last);
}

template class BSplineCurve<2>;
template class BSplineCurve<3>;
template BSplineCurve<2> conicArcToBSpline<2>(const Vec2 &, const Vec2 &, const Vec2 &, double, double);
template BSplineCurve<3> conicArcToBSpline<3>(const Vec3 &, const Vec3 &, const Vec3 &, double, double);
template BSplineCurve<2> toBSpline<2>(const Circle<2> &, double, double);
template BSplineCurve<3> toBSpline<3>(const Circle<3> &, double, double);
template BSplineCurve<2> toBSpline<2>(const Ellipse<2> &, double, double);
template BSplineCurve<3> toBSpline<3>(const Ellipse<3> &, double, double);

}
