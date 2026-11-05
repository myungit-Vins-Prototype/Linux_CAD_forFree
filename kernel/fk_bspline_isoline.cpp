#include "fk_bspline_isoline.h"

#include "fk_bspline_basis.h"
#include "fk_cancel.h"

namespace ForgeCad::Kernel::detail {

BSplineUIsoline::BSplineUIsoline(const BSplineSurface &surface, double u, const Interval &vRange)
    : placed_(surface), source_(surface.basis() ? *surface.basis() : surface), u_(u), range_(vRange) {
    if (!std::isfinite(u) || !vRange.isFinite() || vRange.hi < vRange.lo)
        throw std::invalid_argument("BSplineUIsoline: parametri non validi");
    const int p = source_.uDegree();
    const int su = findSpan(source_.uKnots(), p, source_.uPoleCount(), u);
    ScratchBuffer<double> basis(2 * (p + 1));
    basisFunctionDerivatives(source_.uKnots(), su, u, p, std::min(1, p), basis.data());
    const int q = source_.vDegree();
    first_ = findSpan(source_.vKnots(), q, source_.vPoleCount(), vRange.lo) - q;
    const int last = findSpan(source_.vKnots(), q, source_.vPoleCount(), vRange.hi);
    columns_.resize(last - first_ + 1);
    for (int j = first_; j <= last; ++j) {
        cancellationPoint();
        auto &column = columns_[j - first_];
        // Un'origine per colonna conserva la stabilita' sui nodi U stretti,
        // anche quando la superficie e' molto lontana dall'origine mondo.
        column.origin = source_.pole(su - p, j);
        for (int i = 0; i <= p; ++i) {
            const double weight = source_.weight(su - p + i, j);
            const double a = basis[i] * weight, b = basis[p + 1 + i] * weight;
            const Vec3 delta = source_.pole(su - p + i, j) - column.origin;
            column.value += a * delta;
            column.du += b * delta;
            column.weight += a;
            column.duWeight += b;
        }
    }
}

void BSplineUIsoline::evaluate(double v, Vec3 *out) const {
    if (!std::isfinite(v)) throw std::invalid_argument("BSplineUIsoline: parametro v non valido");
    if (v < range_.lo || v > range_.hi) {
        placed_.evaluate(u_, v, 1, out);
        return;
    }
    const int q = source_.vDegree();
    const int sv = findSpan(source_.vKnots(), q, source_.vPoleCount(), v);
    ScratchBuffer<double> basis(2 * (q + 1));
    basisFunctionDerivatives(source_.vKnots(), sv, v, q, std::min(1, q), basis.data());
    const Vec3 origin = columns_[sv - q - first_].origin;
    Vec3 a, au, av;
    double w = 0.0, wu = 0.0, wv = 0.0;
    for (int j = 0; j <= q; ++j) {
        const auto &column = columns_[sv - q + j - first_];
        const Vec3 delta = column.origin - origin;
        const Vec3 point = column.value + column.weight * delta;
        const Vec3 du = column.du + column.duWeight * delta;
        a += basis[j] * point;
        au += basis[j] * du;
        av += basis[q + 1 + j] * point;
        w += basis[j] * column.weight;
        wu += basis[j] * column.duWeight;
        wv += basis[q + 1 + j] * column.weight;
    }
    out[0] = source_.isRational() ? a / w : a;
    out[2] = source_.isRational() ? (au - wu * out[0]) / w : au;
    out[1] = source_.isRational() ? (av - wv * out[0]) / w : av;
    out[0] += origin;
    out[3] = Vec3();
    if (placed_.basis()) {
        out[0] = placed_.placement().applyToPoint(out[0]);
        out[1] = placed_.placement().applyToVector(out[1]);
        out[2] = placed_.placement().applyToVector(out[2]);
    }
}

}
