#include "fk_bernstein.h"
#include "fk_test_util.h"

using namespace fktest;

namespace {

// Prodotto di fattori lineari (s - r): in Bernstein (s - r) = [-r, 1 - r].
BernsteinPolynomial fromRoots(const std::vector<double> &roots, double scale = 1.0) {
    BernsteinPolynomial result({scale});
    for (double r : roots) result = result * BernsteinPolynomial({-r, 1.0 - r});
    return result;
}

}

FK_TEST(BernsteinArithmetic) {
    std::mt19937 rng(40);
    for (int trial = 0; trial < 200; ++trial) {
        std::vector<double> a(uniformInt(rng, 1, 7)), b(uniformInt(rng, 1, 7));
        for (double &v : a) v = uniform(rng, -5, 5);
        for (double &v : b) v = uniform(rng, -5, 5);
        const BernsteinPolynomial p(a), q(b);
        const BernsteinPolynomial sum = p + q, product = p * q, elevated = p.elevated(p.degree() + 3);
        BernsteinPolynomial left, right;
        p.split(0.3, left, right);
        for (int i = 0; i <= 20; ++i) {
            const double s = i / 20.0;
            FK_CHECK_NEAR(sum.evaluate(s), p.evaluate(s) + q.evaluate(s), 1e-12);
            FK_CHECK_NEAR(product.evaluate(s), p.evaluate(s) * q.evaluate(s), 1e-11);
            FK_CHECK_NEAR(elevated.evaluate(s), p.evaluate(s), 1e-12);
            FK_CHECK_NEAR(left.evaluate(s), p.evaluate(0.3 * s), 1e-12);
            FK_CHECK_NEAR(right.evaluate(s), p.evaluate(0.3 + 0.7 * s), 1e-12);
            // Derivata confrontata con differenze centrali.
            const double h = 1e-6, x = 0.1 + 0.8 * s;
            FK_CHECK_NEAR(p.derivative().evaluate(x), (p.evaluate(x + h) - p.evaluate(x - h)) / (2 * h), 1e-6);
        }
    }
}

FK_TEST(BernsteinRoots) {
    std::mt19937 rng(41);
    for (int trial = 0; trial < 500; ++trial) {
        std::vector<double> expected(uniformInt(rng, 1, 9));
        for (double &r : expected) r = uniform(rng, 0.0, 1.0);
        std::sort(expected.begin(), expected.end());
        // Radici distinte di almeno 1e-3, qualcuna fuori da [0, 1].
        bool separated = true;
        for (std::size_t i = 1; i < expected.size(); ++i) separated &= expected[i] - expected[i - 1] > 1e-3;
        if (!separated) continue;
        std::vector<double> all = expected;
        all.push_back(uniform(rng, 1.1, 3.0));
        all.push_back(uniform(rng, -3.0, -0.1));
        const std::vector<double> found = fromRoots(all, uniform(rng, 0.1, 100.0)).roots();
        FK_CHECK(found.size() == expected.size());
        if (found.size() != expected.size()) continue;
        for (std::size_t i = 0; i < found.size(); ++i) FK_CHECK_NEAR(found[i], expected[i], 1e-9);
    }
}

FK_TEST(BernsteinRootsDegenerate) {
    // Radici agli estremi.
    std::vector<double> r = fromRoots({0.0, 1.0, 0.5}).roots();
    FK_CHECK(r.size() == 3);
    // Radice doppia: un solo valore, vicino a 0.3.
    r = fromRoots({0.3, 0.3}).roots();
    FK_CHECK(!r.empty());
    for (double v : r) FK_CHECK_NEAR(v, 0.3, 1e-7);
    // Radici vicinissime ma distinte.
    r = fromRoots({0.5, 0.5 + 1e-6}).roots();
    FK_CHECK(r.size() == 2);
    // Nessuna radice reale: (s - 0.5)^2 + 0.01.
    r = (fromRoots({0.5, 0.5}) + BernsteinPolynomial({0.01})).roots();
    FK_CHECK(r.empty());
    // Identicamente nullo.
    FK_CHECK(BernsteinPolynomial({0.0, 0.0, 0.0}).roots().empty());
}
