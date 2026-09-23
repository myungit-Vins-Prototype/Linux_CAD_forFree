#include "fk_test_util.h"

using namespace fktest;

FK_TEST(BSplineSurfaceMatchesOcct) {
    std::mt19937 rng(60);
    for (int trial = 0; trial < 300; ++trial) {
        const BSplineSurface surface = randomBSplineSurface(rng, trial % 2 == 1);
        const Handle(Geom_BSplineSurface) reference = toOcct(surface);
        const Interval uDomain = surface.uDomain(), vDomain = surface.vDomain();
        constexpr int order = 4;
        Vec3 d[(order + 1) * (order + 1)];
        for (int i = 0; i <= 15; ++i) {
            // Anche esattamente sui nodi.
            const double u = i % 5 == 0 ? surface.uKnots()[uniformInt(rng, surface.uDegree(), surface.uPoleCount())]
                                        : uniform(rng, uDomain.lo, uDomain.hi);
            const double v = i % 3 == 0 ? surface.vKnots()[uniformInt(rng, surface.vDegree(), surface.vPoleCount())]
                                        : uniform(rng, vDomain.lo, vDomain.hi);
            surface.evaluate(u, v, order, d);
            FK_CHECK(near(d[0], fromOcct(reference->Value(u, v)), 1e-13));
            for (int k = 0; k <= order; ++k)
                for (int l = 0; k + l <= order; ++l)
                    if (k + l > 0)
                        FK_CHECK(near(d[Surface::derivativeIndex(k, l, order)], fromOcct(reference->DN(u, v, k, l)),
                                      1e-12 * std::pow(10.0, k + l)));

            // Isoparametriche esatte.
            FK_CHECK(near(surface.uIsoCurve(u).point(v), d[0], 1e-13));
            FK_CHECK(near(surface.vIsoCurve(v).point(u), d[0], 1e-13));
            FK_CHECK(near(surface.uIsoCurve(u).derivative(v, 1), d[Surface::derivativeIndex(0, 1, order)], 1e-11));
            FK_CHECK(near(surface.vIsoCurve(v).derivative(u, 1), d[Surface::derivativeIndex(1, 0, order)], 1e-11));
        }
    }
}

FK_TEST(BSplineSurfaceBezierPatches) {
    std::mt19937 rng(61);
    for (int trial = 0; trial < 100; ++trial) {
        const BSplineSurface surface = randomBSplineSurface(rng, trial % 2 == 0);
        const std::vector<BSplineSurface> patches = surface.bezierPatches();
        const std::size_t expected = (surface.uBreakpoints(surface.uDomain()).size() - 1)
                                   * (surface.vBreakpoints(surface.vDomain()).size() - 1);
        FK_CHECK(patches.size() == expected);
        for (const BSplineSurface &patch : patches) {
            FK_CHECK(patch.uPoleCount() == patch.uDegree() + 1 && patch.vPoleCount() == patch.vDegree() + 1);
            for (int i = 0; i <= 4; ++i)
                for (int j = 0; j <= 4; ++j) {
                    const double u = patch.uDomain().lo + patch.uDomain().length() * i / 4;
                    const double v = patch.vDomain().lo + patch.vDomain().length() * j / 4;
                    FK_CHECK(near(patch.point(u, v), surface.point(u, v), 1e-12));
                }
        }
    }
}

FK_TEST(BSplineSurfaceValidation) {
    const std::vector<Vec3> poles(4, Vec3());
    FK_CHECK_THROWS(BSplineSurface(1, 1, {0, 0, 1, 1}, {0, 0, 1, 1}, 2, 2, std::vector<Vec3>(3)));  // poli mancanti
    FK_CHECK_THROWS(BSplineSurface(1, 1, {0, 0, 1}, {0, 0, 1, 1}, 2, 2, poles));                  // nodi u
    FK_CHECK_THROWS(BSplineSurface(1, 1, {0, 0, 1, 1}, {0, 0, 1, 1}, 2, 2, poles, {1, 1, 0, 1}));   // peso nullo
    // Bilineare: S(u, v) = (u, v, u v).
    const BSplineSurface bilinear(1, 1, {0, 0, 1, 1}, {0, 0, 1, 1}, 2, 2,
                                  {Vec3(0, 0, 0), Vec3(0, 1, 0), Vec3(1, 0, 0), Vec3(1, 1, 1)});
    FK_CHECK(near(bilinear.point(0.25, 0.5), Vec3(0.25, 0.5, 0.125), 1e-16));
    Vec3 d[9];
    bilinear.evaluate(0.25, 0.5, 2, d);
    FK_CHECK(near(d[Surface::derivativeIndex(1, 1, 2)], Vec3(0, 0, 1), 1e-15));
    FK_CHECK(near(bilinear.normal(0.0, 0.0), Vec3(0, 0, 1), 1e-15));
}
