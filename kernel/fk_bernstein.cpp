#include "fk_bernstein.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "fk_precision.h"

namespace ForgeCad::Kernel {
namespace {

double binomial(int n, int k) {
    double result = 1.0;
    for (int i = 1; i <= k; ++i) result = result * (n - k + i) / i;
    return result;
}

int sign(double value) { return (value > 0.0) - (value < 0.0); }

int signChanges(const std::vector<double> &coefficients) {
    int changes = 0, last = 0;
    for (double value : coefficients) {
        const int s = sign(value);
        if (s == 0) continue;
        if (last != 0 && s != last) ++changes;
        last = s;
    }
    return changes;
}

// Una sola radice semplice in (lo, hi), con segno `startSign` in lo.
double bisect(const BernsteinPolynomial &polynomial, double lo, double hi, int startSign) {
    for (int iteration = 0; iteration < 200; ++iteration) {
        const double middle = 0.5 * (lo + hi);
        if (middle <= lo || middle >= hi) break;
        const int s = sign(polynomial.evaluate(middle));
        if (s == 0) return middle;
        if (s == startSign) lo = middle;
        else hi = middle;
    }
    return 0.5 * (lo + hi);
}

void isolate(const BernsteinPolynomial &original, const BernsteinPolynomial &local, double lo, double hi,
             double tolerance, int depth, std::vector<double> &out) {
    const std::vector<double> &c = local.coefficients();
    if (std::all_of(c.begin(), c.end(), [](double value) { return value == 0.0; })) return;
    if (c.front() == 0.0) out.push_back(lo);
    if (c.back() == 0.0) out.push_back(hi);
    const int changes = signChanges(c);
    if (changes == 0) return;
    if (changes == 1 && c.front() != 0.0 && c.back() != 0.0) {
        out.push_back(bisect(original, lo, hi, sign(c.front())));
        return;
    }
    const double middle = 0.5 * (lo + hi);
    if (hi - lo <= tolerance || depth >= 200 || middle <= lo || middle >= hi) {
        out.push_back(middle);  // radice multipla o grappolo non separabile
        return;
    }
    BernsteinPolynomial left, right;
    local.split(0.5, left, right);
    isolate(original, left, lo, middle, tolerance, depth + 1, out);
    isolate(original, right, middle, hi, tolerance, depth + 1, out);
}

}

BernsteinPolynomial::BernsteinPolynomial(std::vector<double> coefficients) : coefficients_(std::move(coefficients)) {
    if (coefficients_.empty()) throw std::invalid_argument("BernsteinPolynomial: nessun coefficiente");
}

double BernsteinPolynomial::evaluate(double s) const {
    std::vector<double> work = coefficients_;
    for (int level = degree(); level > 0; --level)
        for (int i = 0; i < level; ++i) work[i] = (1.0 - s) * work[i] + s * work[i + 1];
    return work[0];
}

BernsteinPolynomial BernsteinPolynomial::derivative() const {
    const int n = degree();
    if (n == 0) return BernsteinPolynomial({0.0});
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) result[i] = n * (coefficients_[i + 1] - coefficients_[i]);
    return BernsteinPolynomial(std::move(result));
}

BernsteinPolynomial BernsteinPolynomial::elevated(int target) const {
    if (target < degree()) throw std::invalid_argument("BernsteinPolynomial: grado di destinazione minore");
    std::vector<double> current = coefficients_;
    for (int n = degree(); n < target; ++n) {
        std::vector<double> next(n + 2);
        next[0] = current[0];
        next[n + 1] = current[n];
        for (int i = 1; i <= n; ++i) {
            const double alpha = double(i) / (n + 1);
            next[i] = alpha * current[i - 1] + (1.0 - alpha) * current[i];
        }
        current.swap(next);
    }
    return BernsteinPolynomial(std::move(current));
}

void BernsteinPolynomial::split(double s, BernsteinPolynomial &left, BernsteinPolynomial &right) const {
    const int n = degree();
    std::vector<double> work = coefficients_;
    std::vector<double> l(n + 1), r(n + 1);
    l[0] = work[0];
    r[n] = work[n];
    for (int level = 1; level <= n; ++level) {
        for (int i = 0; i <= n - level; ++i) work[i] = (1.0 - s) * work[i] + s * work[i + 1];
        l[level] = work[0];
        r[n - level] = work[n - level];
    }
    left = BernsteinPolynomial(std::move(l));
    right = BernsteinPolynomial(std::move(r));
}

BernsteinPolynomial BernsteinPolynomial::operator+(const BernsteinPolynomial &other) const {
    const int n = std::max(degree(), other.degree());
    std::vector<double> a = elevated(n).coefficients_;
    const std::vector<double> b = other.elevated(n).coefficients_;
    for (int i = 0; i <= n; ++i) a[i] += b[i];
    return BernsteinPolynomial(std::move(a));
}

BernsteinPolynomial BernsteinPolynomial::operator-(const BernsteinPolynomial &other) const {
    return *this + other * -1.0;
}

// c_k = sum_{i+j=k} C(m,i) C(n,j) / C(m+n,k) a_i b_j.
BernsteinPolynomial BernsteinPolynomial::operator*(const BernsteinPolynomial &other) const {
    const int m = degree(), n = other.degree();
    std::vector<double> result(m + n + 1, 0.0);
    for (int i = 0; i <= m; ++i)
        for (int j = 0; j <= n; ++j)
            result[i + j] += binomial(m, i) * binomial(n, j) * coefficients_[i] * other.coefficients_[j];
    for (int k = 0; k <= m + n; ++k) result[k] /= binomial(m + n, k);
    return BernsteinPolynomial(std::move(result));
}

BernsteinPolynomial BernsteinPolynomial::operator*(double factor) const {
    std::vector<double> result = coefficients_;
    for (double &value : result) value *= factor;
    return BernsteinPolynomial(std::move(result));
}

std::vector<double> BernsteinPolynomial::roots(double tolerance) const {
    std::vector<double> found;
    isolate(*this, *this, 0.0, 1.0, tolerance, 0, found);
    std::sort(found.begin(), found.end());
    std::vector<double> result;
    const double merge = std::max(tolerance, 4.0 * kMachineEpsilon);
    for (double root : found)
        if (result.empty() || root - result.back() > merge) result.push_back(root);
    return result;
}

}
