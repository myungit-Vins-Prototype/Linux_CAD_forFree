#include "fk_predicates.h"
#include "fk_test_util.h"

using namespace fktest;

FK_TEST(Orient2dBasic) {
    FK_CHECK(orient2d(Vec2(0, 0), Vec2(1, 0), Vec2(0, 1)) == 1);
    FK_CHECK(orient2d(Vec2(0, 0), Vec2(0, 1), Vec2(1, 0)) == -1);
    FK_CHECK(orient2d(Vec2(0, 0), Vec2(1, 1), Vec2(2, 2)) == 0);
}

// Esempio di Kettner et al. ("Classroom examples of robustness problems"):
// p vicino alla retta y = x per q = (12, 12), r = (24, 24). Il segno esatto
// e' sign(12 (py - px)), mentre il calcolo ingenuo sbaglia su molti punti.
FK_TEST(Orient2dNearDegenerate) {
    const double ulp = 0x1p-53;  // ulp di 0.5
    const Vec2 q(12.0, 12.0), r(24.0, 24.0);
    int naiveErrors = 0;
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 256; ++j) {
            const Vec2 p(0.5 + i * ulp, 0.5 + j * ulp);
            const int expected = (j > i) - (j < i);
            FK_CHECK(orient2d(p, q, r) == expected);
            FK_CHECK(orient2d(q, r, p) == expected);
            FK_CHECK(orient2d(r, p, q) == expected);
            const double naive = (q.x() - p.x()) * (r.y() - p.y()) - (q.y() - p.y()) * (r.x() - p.x());
            if (((naive > 0) - (naive < 0)) != expected) ++naiveErrors;
        }
    }
    FK_CHECK(naiveErrors > 0);  // il test e' davvero al limite della precisione
}

FK_TEST(Orient3dBasic) {
    const Vec3 a(0, 0, 0), b(1, 0, 0), c(0, 1, 0);
    FK_CHECK(orient3d(a, b, c, Vec3(0, 0, 1)) == 1);
    FK_CHECK(orient3d(a, b, c, Vec3(0, 0, -1)) == -1);
    FK_CHECK(orient3d(a, b, c, Vec3(3, -7, 0)) == 0);
}

// Piano x = y per q, r, s: il segno esatto e' sign(px - py).
FK_TEST(Orient3dNearDegenerate) {
    const double ulp = 0x1p-53;
    const Vec3 q(12.0, 12.0, 0.0), r(24.0, 24.0, 0.0), s(12.0, 12.0, 1.0);
    for (int i = 0; i < 128; ++i) {
        for (int j = 0; j < 128; ++j) {
            const Vec3 p(0.5 + i * ulp, 0.5 + j * ulp, 0.5);
            const int expected = (i > j) - (i < j);
            FK_CHECK(orient3d(q, r, s, p) == expected);
            FK_CHECK(orient3d(r, q, s, p) == -expected);
        }
    }
}

FK_TEST(PredicatesPermutationConsistency) {
    std::mt19937 rng(3);
    for (int trial = 0; trial < 20000; ++trial) {
        const Vec2 a = randomVec<2>(rng, 1.0), b = randomVec<2>(rng, 1.0);
        // c quasi allineato ad a, b.
        const double t = uniform(rng, -2.0, 2.0);
        const Vec2 c = a + t * (b - a) + Vec2(uniform(rng, -1, 1), uniform(rng, -1, 1)) * 1e-17;
        const int o = orient2d(a, b, c);
        FK_CHECK(orient2d(b, c, a) == o);
        FK_CHECK(orient2d(b, a, c) == -o);

        const Vec3 d = randomVec<3>(rng, 1.0), e = randomVec<3>(rng, 1.0), f = randomVec<3>(rng, 1.0);
        const double u = uniform(rng, -1.0, 1.0), v = uniform(rng, -1.0, 1.0);
        const Vec3 g = d + u * (e - d) + v * (f - d) + randomVec<3>(rng, 1e-17);
        const int o3 = orient3d(d, e, f, g);
        FK_CHECK(orient3d(e, f, d, g) == o3);
        FK_CHECK(orient3d(e, d, f, g) == -o3);
    }
}
