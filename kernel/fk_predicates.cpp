#include "fk_predicates.h"

#include <algorithm>
#include <array>
#include <cfenv>
#include <limits>
#include <vector>

namespace ForgeCad::Kernel {
namespace {

constexpr double kEpsilon = 0x1p-53;  // meta' ulp di 1.0
constexpr double kOrient2dErrorBound = (3.0 + 16.0 * kEpsilon) * kEpsilon;
constexpr double kOrient3dErrorBound = (7.0 + 56.0 * kEpsilon) * kEpsilon;

// Somma esatta: a + b = x + y con x = fl(a + b).
inline void twoSum(double a, double b, double &x, double &y) {
    x = a + b;
    const double bVirtual = x - a;
    const double aVirtual = x - bVirtual;
    y = (a - aVirtual) + (b - bVirtual);
}

// Prodotto esatto: a * b = x + y (fma e' esatto per definizione).
inline void twoProduct(double a, double b, double &x, double &y) {
    x = a * b;
    y = std::fma(a, b, -x);
}

// Numero reale rappresentato esattamente come somma di double non
// sovrapposti, in ordine di modulo crescente e senza zeri: il segno e'
// quello dell'ultima componente.
class Expansion {
public:
    Expansion() = default;
    explicit Expansion(double value) { add(value); }

    // Grow-Expansion con eliminazione degli zeri.
    void add(double value) {
        std::vector<double> result;
        result.reserve(terms_.size() + 1);
        double q = value;
        for (double term : terms_) {
            double sum, error;
            twoSum(q, term, sum, error);
            q = sum;
            if (error != 0.0) result.push_back(error);
        }
        if (q != 0.0) result.push_back(q);
        terms_.swap(result);
    }

    Expansion operator+(const Expansion &other) const {
        Expansion result = *this;
        for (double term : other.terms_) result.add(term);
        return result;
    }

    Expansion operator-() const {
        Expansion result = *this;
        for (double &term : result.terms_) term = -term;
        return result;
    }

    Expansion operator-(const Expansion &other) const { return *this + (-other); }

    Expansion operator*(const Expansion &other) const {
        Expansion result;
        for (double e : terms_) {
            for (double f : other.terms_) {
                double product, error;
                twoProduct(e, f, product, error);
                result.add(error);
                result.add(product);
            }
        }
        return result;
    }

    double estimate() const {
        double result = 0.;
        for (double term : terms_) result += term;
        return result;
    }

    int sign() const { return terms_.empty() ? 0 : (terms_.back() > 0.0 ? 1 : -1); }

private:
    std::vector<double> terms_;
};

Expansion difference(double a, double b) { return Expansion(a) - Expansion(b); }
// Lower bound on the least significant bit of a nonzero binary64 input.
int lowestBit(double value) {
    return value == 0. ? 0 : std::ilogb(std::abs(value)) - 52;
}

bool boundedExpansionInput(double value) {
    return std::abs(value) <= 0x1p128;
}


}

std::optional<Vec2> detail::exactRationalBezierPoint(const Vec2 *poles, const double *weights,
                                                    std::size_t count, double parameter) {
    if (count < 2 || count > 9 || !(0. < parameter && parameter < 1.) || std::fegetround() != FE_TONEAREST)
        return {};
    double complement, lost;
    twoSum(1., -parameter, complement, lost);
    if (lost != 0.) return {};

    int weightBit = 0, poleBit = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!(weights[i] > 0.) || !boundedExpansionInput(weights[i]) ||
            !boundedExpansionInput(poles[i].x()) || !boundedExpansionInput(poles[i].y()))
            return {};
        weightBit = std::min(weightBit, lowestBit(weights[i]));
        poleBit = std::min({poleBit, lowestBit(poles[i].x()), lowestBit(poles[i].y())});
    }
    // Each de Casteljau level adds at most one factor of parameter or
    // complement. Products and their FMA residuals stay on a normal
    // binary lattice. The input magnitude cap leaves room for all sums.
    constexpr int minBit = std::numeric_limits<double>::min_exponent - 1;
    weightBit += int(count - 1) * std::min(lowestBit(parameter), lowestBit(complement));
    if (weightBit + poleBit < minBit) return {};

    const Expansion left(complement), right(parameter);
    std::array<std::array<Expansion, 3>, 9> values;
    for (std::size_t i = 0; i < count; ++i) {
        const Expansion weight(weights[i]);
        values[i] = {Expansion(poles[i].x()) * weight, Expansion(poles[i].y()) * weight, weight};
    }
    for (std::size_t length = count; length > 1; --length)
        for (std::size_t i = 0; i + 1 < length; ++i)
            for (std::size_t j = 0; j < values[i].size(); ++j)
                values[i][j] = values[i][j] * left + values[i + 1][j] * right;

    const auto &w = values[0][2];
    Vec2 result;
    for (std::size_t j = 0; j < 2; ++j) {
        const auto &numerator = values[0][j];
        const double nearest = numerator.estimate() / w.estimate();
        // Candidate division is rounded. Only the exact product identity
        // can publish a Cartesian coordinate.
        bool found = false;
        for (double candidate : {nearest, std::nextafter(nearest, -INFINITY), std::nextafter(nearest, INFINITY)}) {
            if (!boundedExpansionInput(candidate) || weightBit + lowestBit(candidate) < minBit ||
                (numerator - Expansion(candidate) * w).sign() != 0)
                continue;
            result[j] = candidate;
            found = true;
            break;
        }
        if (!found) return {};
    }
    return result;
}

int orient2d(const Vec2 &a, const Vec2 &b, const Vec2 &c) {
    const double left = (a.x() - c.x()) * (b.y() - c.y());
    const double right = (a.y() - c.y()) * (b.x() - c.x());
    const double determinant = left - right;
    const double bound = kOrient2dErrorBound * (std::fabs(left) + std::fabs(right));
    if (determinant > bound) return 1;
    if (-determinant > bound) return -1;

    const Expansion exact = difference(a.x(), c.x()) * difference(b.y(), c.y())
                          - difference(a.y(), c.y()) * difference(b.x(), c.x());
    return exact.sign();
}

// Formula di Shewchuk con d come riferimento: il suo segno e' opposto alla
// convenzione di orient3d (d dalla parte della normale -> positivo).
int orient3d(const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d) {
    const double adx = a.x() - d.x(), ady = a.y() - d.y(), adz = a.z() - d.z();
    const double bdx = b.x() - d.x(), bdy = b.y() - d.y(), bdz = b.z() - d.z();
    const double cdx = c.x() - d.x(), cdy = c.y() - d.y(), cdz = c.z() - d.z();
    const double bdxcdy = bdx * cdy, cdxbdy = cdx * bdy;
    const double cdxady = cdx * ady, adxcdy = adx * cdy;
    const double adxbdy = adx * bdy, bdxady = bdx * ady;
    const double determinant = adz * (bdxcdy - cdxbdy) + bdz * (cdxady - adxcdy) + cdz * (adxbdy - bdxady);
    const double permanent = (std::fabs(bdxcdy) + std::fabs(cdxbdy)) * std::fabs(adz)
                           + (std::fabs(cdxady) + std::fabs(adxcdy)) * std::fabs(bdz)
                           + (std::fabs(adxbdy) + std::fabs(bdxady)) * std::fabs(cdz);
    const double bound = kOrient3dErrorBound * permanent;
    if (determinant > bound) return -1;
    if (-determinant > bound) return 1;

    const Expansion eadx = difference(a.x(), d.x()), eady = difference(a.y(), d.y()), eadz = difference(a.z(), d.z());
    const Expansion ebdx = difference(b.x(), d.x()), ebdy = difference(b.y(), d.y()), ebdz = difference(b.z(), d.z());
    const Expansion ecdx = difference(c.x(), d.x()), ecdy = difference(c.y(), d.y()), ecdz = difference(c.z(), d.z());
    const Expansion exact = eadz * (ebdx * ecdy - ecdx * ebdy)
                          + ebdz * (ecdx * eady - eadx * ecdy)
                          + ecdz * (eadx * ebdy - ebdx * eady);
    return -exact.sign();
}

}
