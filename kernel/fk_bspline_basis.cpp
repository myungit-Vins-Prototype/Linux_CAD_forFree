#include "fk_bspline_basis.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace ForgeCad::Kernel::detail {

void validateKnotVector(const std::vector<double> &knots, int degree, int poleCount, const char *what) {
    const std::string prefix = std::string(what) + ": ";
    const int p = degree;
    if (p < 1) throw std::invalid_argument(prefix + "grado < 1");
    if (poleCount < p + 1) throw std::invalid_argument(prefix + "servono almeno grado + 1 poli");
    if (int(knots.size()) != poleCount + p + 1) throw std::invalid_argument(prefix + "numero di nodi errato");
    for (std::size_t i = 0; i < knots.size(); ++i) {
        if (!std::isfinite(knots[i])) throw std::invalid_argument(prefix + "nodo non finito");
        if (i > 0 && knots[i] < knots[i - 1]) throw std::invalid_argument(prefix + "nodi non crescenti");
    }
    const double first = knots[p], last = knots[poleCount];
    if (!(first < last)) throw std::invalid_argument(prefix + "dominio vuoto");
    for (std::size_t i = 0; i < knots.size();) {
        std::size_t j = i;
        while (j < knots.size() && knots[j] == knots[i]) ++j;
        const int m = int(j - i);
        if (m > p + 1) throw std::invalid_argument(prefix + "molteplicita' > grado + 1");
        if (knots[i] > first && knots[i] < last && m > p)
            throw std::invalid_argument(prefix + "molteplicita' interna > grado (discontinuita')");
        i = j;
    }
}

int findSpan(const std::vector<double> &knots, int degree, int poleCount, double t) {
    const int p = degree;
    const int n = poleCount - 1;
    if (t >= knots[n + 1]) {
        int span = n;
        while (knots[span] >= knots[n + 1]) --span;
        return span;
    }
    if (t <= knots[p]) {
        int span = p;
        while (knots[span + 1] <= knots[p]) ++span;
        return span;
    }
    const auto begin = knots.begin();
    return int(std::upper_bound(begin + p, begin + n + 2, t) - begin) - 1;
}

int findSpanLeft(const std::vector<double> &knots, int degree, int poleCount, double t) {
    const int p = degree;
    const int n = poleCount - 1;
    if (t <= knots[p] || t > knots[n + 1]) return findSpan(knots, degree, poleCount, t);
    const auto begin = knots.begin();
    return int(std::lower_bound(begin + p, begin + n + 2, t) - begin) - 1;
}

std::vector<double> knotBreakpoints(const std::vector<double> &knots, const Interval &range) {
    std::vector<double> result{range.lo};
    for (double knot : knots)
        if (knot > range.lo && knot < range.hi && knot != result.back()) result.push_back(knot);
    result.push_back(range.hi);
    return result;
}

void basisFunctionDerivatives(const std::vector<double> &knots, int span, double u, int p, int n, double *ders) {
    ScratchBuffer<double> ndu((p + 1) * (p + 1));
    ScratchBuffer<double, 16> left(p + 1), right(p + 1);
    auto NDU = [&](int row, int column) -> double & { return ndu[row * (p + 1) + column]; };

    NDU(0, 0) = 1.0;
    for (int j = 1; j <= p; ++j) {
        left[j] = u - knots[span + 1 - j];
        right[j] = knots[span + j] - u;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            NDU(j, r) = right[r + 1] + left[j - r];
            const double temp = NDU(r, j - 1) / NDU(j, r);
            NDU(r, j) = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        NDU(j, j) = saved;
    }

    std::fill(ders, ders + (n + 1) * (p + 1), 0.0);
    for (int j = 0; j <= p; ++j) ders[j] = NDU(j, p);

    ScratchBuffer<double, 32> a(2 * (p + 1));
    auto A = [&](int row, int column) -> double & { return a[row * (p + 1) + column]; };
    for (int r = 0; r <= p; ++r) {
        int s1 = 0, s2 = 1;
        A(0, 0) = 1.0;
        for (int k = 1; k <= n; ++k) {
            double d = 0.0;
            const int rk = r - k, pk = p - k;
            if (r >= k) {
                A(s2, 0) = A(s1, 0) / NDU(pk + 1, rk);
                d = A(s2, 0) * NDU(rk, pk);
            }
            const int j1 = rk >= -1 ? 1 : -rk;
            const int j2 = r - 1 <= pk ? k - 1 : p - r;
            for (int j = j1; j <= j2; ++j) {
                A(s2, j) = (A(s1, j) - A(s1, j - 1)) / NDU(pk + 1, rk + j);
                d += A(s2, j) * NDU(rk + j, pk);
            }
            if (r <= pk) {
                A(s2, k) = -A(s1, k - 1) / NDU(pk + 1, r);
                d += A(s2, k) * NDU(r, pk);
            }
            ders[k * (p + 1) + r] = d;
            std::swap(s1, s2);
        }
    }
    double factor = p;
    for (int k = 1; k <= n; ++k) {
        for (int j = 0; j <= p; ++j) ders[k * (p + 1) + j] *= factor;
        factor *= p - k;
    }
}

double binomial(int n, int k) {
    double result = 1.0;
    for (int i = 1; i <= k; ++i) result = result * (n - k + i) / i;
    return result;
}


}
