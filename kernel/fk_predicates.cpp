#include "fk_predicates.h"

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

    int sign() const { return terms_.empty() ? 0 : (terms_.back() > 0.0 ? 1 : -1); }

private:
    std::vector<double> terms_;
};

Expansion difference(double a, double b) { return Expansion(a) - Expansion(b); }

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
