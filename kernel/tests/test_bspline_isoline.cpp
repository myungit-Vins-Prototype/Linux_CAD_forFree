#include "fk_bspline_isoline.h"
#include "fk_body_io.h"
#include "fk_boolean.h"
#include "fk_mass.h"
#include "fk_parallel.h"
#include "fk_primitives.h"
#include "fk_test.h"
#include <future>

using namespace ForgeCad::Kernel;
namespace {
BSplineSurface fixture(int degree, bool rational, double width, bool plane) {
    std::vector<double> u(degree + 1, 1.), v(4, 0.);
    u.insert(u.end(), degree + 1, 1. + width);
    for (double x : {.15, .3, .30000001, .6, .8}) v.insert(v.end(), 3, x);
    v.insert(v.end(), 4, 1.);
    const int nv = int(v.size()) - 4;
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i <= degree; ++i) for (int j = 0; j < nv; ++j) {
        poles.push_back({200000. + (plane ? 0. : .4 * std::sin(double(i + j))),
                         20. * i / degree + .2 * std::sin(double(j)), 6. * j});
        if (rational) weights.push_back(1. + .2 * std::sin(double(i + j)));
    }
    return BSplineSurface(degree, 3, u, v, degree + 1, nv, poles, weights);
}
void compare(const BSplineSurface &surface, double u, const Interval &window, bool plane) {
    const detail::BSplineUIsoline iso(surface, u, window);
    for (double v : {-.01, 0., .12, .15, .3, .300000005, .30000001, .6, .81, 1., 1.01}) {
        Vec3 expected[4], got[4];surface.evaluate(u, v, 1, expected);iso.evaluate(v, got);
        for (int k = 0; k < 3; ++k) {
            FK_CHECK(isFinite(got[k]));
            FK_CHECK(norm(got[k] - expected[k]) <= (k ? 2e-12 * (1. + norm(expected[k])) : 2e-9));
            if (plane) FK_CHECK(got[k].x() == (k ? 0. : 200000.));
        }
    }
    std::vector<std::future<Vec3>> reads;
    for (int i = 0; i < 4; ++i) reads.push_back(std::async(std::launch::async, [&] {
        Vec3 jet[4];iso.evaluate(.4, jet);return jet[0];
    }));
    const Vec3 first = reads.front().get();
    for (int i = 1; i < 4; ++i) FK_CHECK(norm(reads[i].get() - first) == 0.);
}
}
FK_TEST(BSplineIsolineJetsAndInstances) {
    for (int degree : {3, 34}) for (bool rational : {false, true}) for (double width : {1., 1e-8}) {
        const auto surface = fixture(degree, rational, width, false);
        for (double t : {.125, .5, .875}) compare(surface, 1. + t * width, {.2, .8}, false);
        const auto plane = fixture(degree, rational, width, true);
        compare(plane, 1. + .4 * width, {0., 1.}, true);
        const BSplineSurface placed(std::make_shared<const BSplineSurface>(surface),
                                  Transform3::translation(Vec3(-123., 71., 5.)) * Transform3::scaling(Vec3(), -1.));
        compare(placed, 1. + .3 * width, {0., 1.}, false);
    }
}
FK_TEST(BSplineIsolineRejectsInvalidAndCancels) {
    const auto surface = fixture(3, true, 1., false);
    FK_CHECK_THROWS(detail::BSplineUIsoline(surface, 1.5, {1., 0.}));
    FK_CHECK_THROWS(detail::BSplineUIsoline(surface, std::numeric_limits<double>::infinity(), {0., 1.}));
    CancellationScope cancelled(std::make_shared<std::atomic<bool>>(true));
    FK_CHECK_THROWS(detail::BSplineUIsoline(surface, 1.5, {0., 1.}));
}
FK_TEST(BSplineIsolineMassAnalyticAndParallel) {
    const Frame3 frame(Vec3(200000., -17., 31.), normalized(Vec3(.3, .7, .5)), Vec3(1., 0., 0.));
    const Frame3 second(frame.toGlobal(Vec3(500., 0., 0.)), frame.zDir(), frame.xDir());
    Body body = booleanOperation(makeBox(frame, 20., 30., 40.), makeBox(second, 20., 30., 40.),
                                 BooleanOperation::Unite);
    FK_CHECK(body.faces().size() >= 8); // esercita davvero la quadratura parallela
    // Griglia densa che rappresenta esattamente i sei piani del box. Le
    // SP-curve mantengono gli stessi parametri; non si cambiano i contorni.
    for (auto f : body.faces()) {
        const auto support = static_cast<const Plane &>(*body.face(f).surface);
        std::vector<double> u(4, -100.), v(4, -100.);
        u.insert(u.end(), 4, 100.);
        for (int i = 1; i < 40; ++i) v.push_back(-100. + 5. * i);
        v.insert(v.end(), 4, 100.);
        const int nv = int(v.size()) - 4;
        std::vector<Vec3> poles;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < nv; ++j) {
            const double x = (u[i + 1] + u[i + 2] + u[i + 3]) / 3.;
            const double y = (v[j + 1] + v[j + 2] + v[j + 3]) / 3.;
            poles.push_back(support.point(x, y));
        }
        body.face(f).surface = std::make_shared<const BSplineSurface>(3, 3, u, v, 4, nv, poles);
    }
    const auto before = writeBodyBinary(body);
    const auto parallel = massProperties(body, 1e-10);
    ++parallelDepth;const auto serial = massProperties(body, 1e-10);--parallelDepth;
    FK_CHECK_NEAR(parallel.volume, 48000., 4.8e-5);
    FK_CHECK(norm(parallel.centroid - frame.toGlobal(Vec3(260., 15., 20.))) < 1e-7);
    FK_CHECK_NEAR(parallel.area, 10400., 1.04e-5);
    FK_CHECK(parallel.volume == serial.volume);
    FK_CHECK(parallel.area == serial.area);
    FK_CHECK(norm(parallel.centroid - serial.centroid) == 0.);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        FK_CHECK(parallel.inertia[i][j] == serial.inertia[i][j]);
    FK_CHECK_NEAR(parallel.inertia[0][0] + parallel.inertia[1][1] + parallel.inertia[2][2],
                  48000. * (20.*20. + 30.*30. + 40.*40.) / 6. + 48000. * 500.*500. / 2., 6.);
    FK_CHECK(writeBodyBinary(body) == before);
}
