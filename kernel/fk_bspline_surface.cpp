#include "fk_bspline_surface.h"

#include <algorithm>

#include "fk_bspline_basis.h"

namespace ForgeCad::Kernel {

BSplineSurface::BSplineSurface(int uDegree, int vDegree, std::vector<double> uKnots, std::vector<double> vKnots,
                               int uPoleCount, int vPoleCount, std::vector<Vec3> poles, std::vector<double> weights)
    : uDegree_(uDegree), vDegree_(vDegree), uKnots_(std::move(uKnots)), vKnots_(std::move(vKnots)),
      uPoleCount_(uPoleCount), vPoleCount_(vPoleCount), poles_(std::move(poles)), weights_(std::move(weights)) {
    detail::validateKnotVector(uKnots_, uDegree_, uPoleCount_, "BSplineSurface (u)");
    detail::validateKnotVector(vKnots_, vDegree_, vPoleCount_, "BSplineSurface (v)");
    if (int(poles_.size()) != uPoleCount_ * vPoleCount_) throw std::invalid_argument("BSplineSurface: numero di poli errato");
    for (const Vec3 &pole : poles_)
        if (!isFinite(pole)) throw std::invalid_argument("BSplineSurface: polo non finito");
    if (!weights_.empty()) {
        if (weights_.size() != poles_.size()) throw std::invalid_argument("BSplineSurface: numero di pesi errato");
        for (double w : weights_)
            if (!(w > 0.0) || !std::isfinite(w)) throw std::invalid_argument("BSplineSurface: pesi non positivi");
    }
}

Interval BSplineSurface::uDomain() const { return {uKnots_[uDegree_], uKnots_[uPoleCount_]}; }
Interval BSplineSurface::vDomain() const { return {vKnots_[vDegree_], vKnots_[vPoleCount_]}; }

std::vector<double> BSplineSurface::uBreakpoints(const Interval &range) const {
    return detail::knotBreakpoints(uKnots_, range);
}

std::vector<double> BSplineSurface::vBreakpoints(const Interval &range) const {
    return detail::knotBreakpoints(vKnots_, range);
}

void BSplineSurface::evaluate(double u, double v, int order, Vec3 *out) const {
    const int pu = uDegree_, pv = vDegree_;
    const int du = std::min(order, pu), dv = std::min(order, pv);
    const int su = detail::findSpan(uKnots_, pu, uPoleCount_, u);
    const int sv = detail::findSpan(vKnots_, pv, vPoleCount_, v);
    detail::ScratchBuffer<double> nu((du + 1) * (pu + 1)), nv((dv + 1) * (pv + 1));
    detail::basisFunctionDerivatives(uKnots_, su, u, pu, du, nu.data());
    detail::basisFunctionDerivatives(vKnots_, sv, v, pv, dv, nv.data());

    // Derivate del numeratore omogeneo A = sum N N w P e del denominatore w.
    const int size = order + 1;
    detail::ScratchBuffer<Vec3, 16> a(size * size);
    detail::ScratchBuffer<double, 16> w(size * size);
    for (int k = 0; k <= du; ++k) {
        for (int l = 0; l <= dv && k + l <= order; ++l) {
            Vec3 sum;
            double weightSum = 0.0;
            for (int i = 0; i <= pu; ++i) {
                const double bu = nu[k * (pu + 1) + i];
                if (bu == 0.0) continue;
                for (int j = 0; j <= pv; ++j) {
                    const int row = su - pu + i, column = sv - pv + j;
                    const double b = bu * nv[l * (pv + 1) + j] * weight(row, column);
                    sum += b * pole(row, column);
                    weightSum += b;
                }
            }
            a[k * size + l] = sum;
            w[k * size + l] = weightSum;
        }
    }

    for (int i = 0; i < size * size; ++i) out[i] = Vec3();
    if (!isRational()) {
        for (int k = 0; k <= order; ++k)
            for (int l = 0; k + l <= order; ++l) out[derivativeIndex(k, l, order)] = a[k * size + l];
        return;
    }
    // A4.4: S_kl = (A_kl - sum_j C(l,j) w_0j S_k,l-j - sum_i C(k,i) w_i0 S_k-i,l
    //               - sum_i C(k,i) sum_j C(l,j) w_ij S_k-i,l-j) / w_00.
    auto S = [&](int k, int l) -> Vec3 & { return out[derivativeIndex(k, l, order)]; };
    for (int k = 0; k <= order; ++k) {
        for (int l = 0; k + l <= order; ++l) {
            Vec3 value = a[k * size + l];
            for (int j = 1; j <= l; ++j) value -= detail::binomial(l, j) * w[j] * S(k, l - j);
            for (int i = 1; i <= k; ++i) {
                value -= detail::binomial(k, i) * w[i * size] * S(k - i, l);
                Vec3 inner;
                for (int j = 1; j <= l; ++j) inner += detail::binomial(l, j) * w[i * size + j] * S(k - i, l - j);
                value -= detail::binomial(k, i) * inner;
            }
            S(k, l) = value / w[0];
        }
    }
}

// Poli della curva a u costante: Q_j = sum_i N_i(u) w_ij P_ij / W_j, W_j = sum_i N_i(u) w_ij.
BSplineCurve<3> BSplineSurface::uIsoCurve(double u) const {
    const int pu = uDegree_;
    const int su = detail::findSpan(uKnots_, pu, uPoleCount_, u);
    detail::ScratchBuffer<double> nu(pu + 1);
    detail::basisFunctionDerivatives(uKnots_, su, u, pu, 0, nu.data());
    std::vector<Vec3> poles(vPoleCount_);
    std::vector<double> weights(vPoleCount_, 0.0);
    for (int j = 0; j < vPoleCount_; ++j) {
        Vec3 sum;
        for (int i = 0; i <= pu; ++i) {
            const int row = su - pu + i;
            const double b = nu[i] * weight(row, j);
            sum += b * pole(row, j);
            weights[j] += b;
        }
        poles[j] = sum / weights[j];
    }
    if (!isRational()) weights.clear();
    return BSplineCurve<3>(vDegree_, vKnots_, std::move(poles), std::move(weights));
}

BSplineCurve<3> BSplineSurface::vIsoCurve(double v) const {
    const int pv = vDegree_;
    const int sv = detail::findSpan(vKnots_, pv, vPoleCount_, v);
    detail::ScratchBuffer<double> nv(pv + 1);
    detail::basisFunctionDerivatives(vKnots_, sv, v, pv, 0, nv.data());
    std::vector<Vec3> poles(uPoleCount_);
    std::vector<double> weights(uPoleCount_, 0.0);
    for (int i = 0; i < uPoleCount_; ++i) {
        Vec3 sum;
        for (int j = 0; j <= pv; ++j) {
            const int column = sv - pv + j;
            const double b = nv[j] * weight(i, column);
            sum += b * pole(i, column);
            weights[i] += b;
        }
        poles[i] = sum / weights[i];
    }
    if (!isRational()) weights.clear();
    return BSplineCurve<3>(uDegree_, uKnots_, std::move(poles), std::move(weights));
}

// Prima ogni colonna (curva in u) viene divisa nei tratti di Bezier, poi ogni
// riga di ciascun tratto (curva in v). Le curve razionali lavorano gia' in
// coordinate omogenee, quindi pesi e poli restano coerenti.
std::vector<BSplineSurface> BSplineSurface::bezierPatches() const {
    const int pu = uDegree_, pv = vDegree_;
    std::vector<std::vector<BSplineCurve<3>>> columns;
    for (int j = 0; j < vPoleCount_; ++j) {
        std::vector<Vec3> poles(uPoleCount_);
        std::vector<double> weights;
        for (int i = 0; i < uPoleCount_; ++i) {
            poles[i] = pole(i, j);
            if (isRational()) weights.push_back(weight(i, j));
        }
        columns.push_back(BSplineCurve<3>(pu, uKnots_, std::move(poles), std::move(weights)).bezierSegments());
    }
    const std::size_t uSpans = columns.front().size();

    std::vector<BSplineSurface> result;
    for (std::size_t k = 0; k < uSpans; ++k) {
        // Righe del tratto k: per ogni i, curva in v con i poli i-esimi delle colonne.
        std::vector<std::vector<BSplineCurve<3>>> rows;
        for (int i = 0; i <= pu; ++i) {
            std::vector<Vec3> poles(vPoleCount_);
            std::vector<double> weights;
            for (int j = 0; j < vPoleCount_; ++j) {
                poles[j] = columns[j][k].poles()[i];
                if (isRational()) weights.push_back(columns[j][k].weight(i));
            }
            rows.push_back(BSplineCurve<3>(pv, vKnots_, std::move(poles), std::move(weights)).bezierSegments());
        }
        const Interval uSpan = columns.front()[k].domain();
        for (std::size_t m = 0; m < rows.front().size(); ++m) {
            const Interval vSpan = rows.front()[m].domain();
            std::vector<Vec3> poles;
            std::vector<double> weights;
            for (int i = 0; i <= pu; ++i)
                for (int j = 0; j <= pv; ++j) {
                    poles.push_back(rows[i][m].poles()[j]);
                    if (isRational()) weights.push_back(rows[i][m].weight(j));
                }
            std::vector<double> uKnots(pu + 1, uSpan.lo), vKnots(pv + 1, vSpan.lo);
            uKnots.insert(uKnots.end(), pu + 1, uSpan.hi);
            vKnots.insert(vKnots.end(), pv + 1, vSpan.hi);
            result.emplace_back(pu, pv, std::move(uKnots), std::move(vKnots), pu + 1, pv + 1, std::move(poles),
                                std::move(weights));
        }
    }
    return result;
}

CurvePtr<3> BSplineSurface::uIso(double u) const { return std::make_shared<BSplineCurve<3>>(uIsoCurve(u)); }
CurvePtr<3> BSplineSurface::vIso(double v) const { return std::make_shared<BSplineCurve<3>>(vIsoCurve(v)); }

}
