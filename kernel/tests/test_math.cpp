#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>

#include "fk_test_util.h"

using namespace fktest;

FK_TEST(VecBasics) {
    const Vec3 a(1.0, 2.0, 3.0), b(-4.0, 5.0, 0.5);
    FK_CHECK_NEAR(dot(a, b), 7.5, 0.0);
    const Vec3 c = cross(a, b);
    FK_CHECK_NEAR(dot(c, a), 0.0, 1e-14);
    FK_CHECK_NEAR(dot(c, b), 0.0, 1e-14);
    FK_CHECK_NEAR(norm(Vec2(3.0, 4.0)), 5.0, 0.0);
    FK_CHECK_NEAR(cross(Vec2(1.0, 0.0), Vec2(0.0, 1.0)), 1.0, 0.0);
    FK_CHECK_THROWS(normalized(Vec3()));
}

FK_TEST(FrameMatchesGpAx3) {
    std::mt19937 rng(1);
    for (int trial = 0; trial < 200; ++trial) {
        const Vec3 origin = randomVec<3>(rng, 100.0);
        const Vec3 z = randomDirection(rng);
        const Vec3 xRef = randomDirection(rng);
        const Frame3 frame(origin, z, xRef);
        FK_CHECK_NEAR(dot(frame.xDir(), frame.yDir()), 0.0, 1e-15);
        FK_CHECK_NEAR(dot(frame.xDir(), frame.zDir()), 0.0, 1e-15);
        FK_CHECK_NEAR(dot(cross(frame.xDir(), frame.yDir()), frame.zDir()), 1.0, 1e-15);

        const gp_Ax3 axes(toPnt(origin), toDir(z), toDir(xRef));
        FK_CHECK(near(frame.xDir(), Vec3(axes.XDirection().X(), axes.XDirection().Y(), axes.XDirection().Z()), 1e-14));
        FK_CHECK(near(frame.yDir(), Vec3(axes.YDirection().X(), axes.YDirection().Y(), axes.YDirection().Z()), 1e-14));

        // gp_Trsf::SetTransformation(ax3) porta le coordinate globali in quelle locali.
        gp_Trsf toLocal;
        toLocal.SetTransformation(axes);
        const Vec3 p = randomVec<3>(rng, 200.0);
        FK_CHECK(near(frame.toLocal(p), fromOcct(toPnt(p).Transformed(toLocal)), 1e-13));
        FK_CHECK(near(frame.toGlobal(frame.toLocal(p)), p, 1e-14));
        FK_CHECK(near(Transform3::fromFrame(frame).applyToPoint(frame.toLocal(p)), p, 1e-14));
    }
    FK_CHECK_THROWS(Frame3(Vec3(), Vec3(0, 0, 1), Vec3(0, 0, 2)));
}

FK_TEST(TransformMatchesGpTrsf) {
    std::mt19937 rng(2);
    for (int trial = 0; trial < 200; ++trial) {
        const Vec3 axisPoint = randomVec<3>(rng, 50.0);
        const Vec3 axis = randomDirection(rng);
        const double angle = uniform(rng, -10.0, 10.0);
        const Transform3 rotation = Transform3::rotation(axisPoint, axis, angle);
        gp_Trsf reference;
        reference.SetRotation(gp_Ax1(toPnt(axisPoint), toDir(axis)), angle);

        const Vec3 p = randomVec<3>(rng, 100.0);
        FK_CHECK(near(rotation.applyToPoint(p), fromOcct(toPnt(p).Transformed(reference)), 1e-13));

        const Transform3 composed = Transform3::translation(randomVec<3>(rng, 10.0)) * rotation
                                  * Transform3::scaling(randomVec<3>(rng, 10.0), uniform(rng, 0.5, 2.0));
        FK_CHECK(near((composed.inverted() * composed).applyToPoint(p), p, 1e-12));
        FK_CHECK(near(composed.inverted().applyToPoint(composed.applyToPoint(p)), p, 1e-12));
    }
}
