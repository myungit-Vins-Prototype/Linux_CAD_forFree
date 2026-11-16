#include "fk_cancel.h"
#include "fk_curve_ops.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_offset.h"
#include "fk_surface.h"
#include "fk_test.h"
using namespace ForgeCad::Kernel;

FK_TEST(OffsetValidationHiddenSpanAndScale) {
    for (double scale : {1., 1000.}) {
        std::vector<double> knots(5, 0.);
        knots.insert(knots.end(), 4, .1);
        knots.insert(knots.end(), 4, .15);
        knots.insert(knots.end(), 5, 1.);
        std::vector<Vec3> poles;
        for (int i = 0; i < 13; ++i) {
            double x = 0;
            for (int j = 1; j <= 4; ++j)
                x += knots[i + j] / 4.;
            poles.emplace_back(scale * x, 0, i == 6 ? .01 * scale : 0.);
        }
        ExtrusionSurface source(std::make_shared<BSplineCurve<3>>(4, knots, poles), Vec3(0, 1, 0));
        const double d = .001 * scale;
        auto out = offsetSurface(source, d, {0, 1}, {0, scale}, 1e-7);
        double worst = 0.;
        for (int i = 0; i <= 1000; ++i) {
            double u = .1 + .05 * i / 1000.;
            worst = std::max(worst, distance(out->point(u, 0), source.point(u, 0) + d * source.normal(u, 0)));
        }
        FK_CHECK(worst < 1e-7);
    }
}
FK_TEST(OffsetValidationToroidalDomain) {
    ToroidalSurface ring(Frame3(), 5, 1);
    FK_CHECK(offsetSurface(ring, 3, {0, kTwoPi}, {0, kTwoPi}) != nullptr);
    FK_CHECK_THROWS(offsetSurface(ring, 4, {0, kTwoPi}, {0, kTwoPi}));
    FK_CHECK_THROWS(offsetSurface(ring, 5, {0, kTwoPi}, {0, kTwoPi}));
    ToroidalSurface horn(Frame3(), 5, 5);
    FK_CHECK_THROWS(offsetSurface(horn, -.1, {0, kTwoPi}, {0, kTwoPi}));
    for (double shift : {0., -4 * kTwoPi, 7 * kTwoPi}) {
        auto outer = offsetSurface(ring, 5, {0, kTwoPi}, {shift - .2, shift + .2});
        for (double v : {shift - .2, shift, shift + .2})
            FK_CHECK(distance(outer->point(.4, v), ring.point(.4, v) + 5 * ring.normal(.4, v)) < 1e-10);
        FK_CHECK_THROWS(offsetSurface(ring, 5, {0, kTwoPi}, {shift + kPi - .2, shift + kPi + .2}));
    }
    ToroidalSurface spindle(Frame3(), 1, 2);
    auto inner = offsetSurface(spindle, .1, {0, kTwoPi}, {kPi - .2, kPi + .2});
    for (double v : {kPi - .2, kPi, kPi + .2})
        FK_CHECK(distance(inner->point(.3, v), spindle.point(.3, v) + .1 * spindle.normal(.3, v)) < 1e-10);
}
FK_TEST(OffsetValidationCancellationAndReset) {
    auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    int calls = 0;
    double deviation = 123.;
    bool cancelled = false;
    try {
        fitCurve(
            [&](double t) {
                ++calls;
                return Vec3(t, 0, 0);
            },
            {0, 1}, {}, 1e-7, &deviation);
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
    FK_CHECK(calls == 0);
    FK_CHECK(deviation == 123.);
    token->store(false);
    cancelled = false;
    try {
        fitCurve(
            [&](double t) {
                ++calls;
                if (t == .75)
                    token->store(true);
                return Vec3(t, 0, 0);
            },
            {0, 1}, {}, 1e-7, &deviation);
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
    FK_CHECK(deviation == 123.);
    token->store(false);
    calls = 0;
    cancelled = false;
    try {
        fitCurve(
            [&](double t) {
                if (++calls == 20)
                    token->store(true);
                return Vec3(t, std::sin(100 * t), 0);
            },
            {0, 1}, {}, 1e-7, &deviation);
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
    FK_CHECK(calls == 20);
    FK_CHECK(deviation == 123.);
    token->store(false);
    FK_CHECK(fitCurve([](double t) { return Vec3(t, 0, 0); }, {0, 1}, {}, 1e-7, &deviation) != nullptr);
    FK_CHECK(deviation < 1e-7);
}
FK_TEST(OffsetValidationAnalyticExtrusionAndRevolution) {
    auto circle = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 2.);
    ExtrusionSurface cylinder(circle, Vec3(0, 0, 1));
    auto exact = offsetSurface(cylinder, .1, {0, kTwoPi}, {0, 1});
    FK_CHECK(static_cast<const ExtrusionSurface &>(*exact).curve()->type() == CurveType::Circle);
    // A circular meridian of a regular torus, represented as a revolution.
    auto meridian = std::make_shared<Circle<3>>(Vec3(5, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, -1), 1.);
    RevolutionSurface torus(meridian, Vec3(), Vec3(0, 0, 1));
    auto out = offsetSurface(torus, .1, {0, kTwoPi}, {0, kTwoPi}, 1e-7);
    for (int i = 0; i < 100; ++i) {
        double v = kTwoPi * i / 100.;
        FK_CHECK(distance(out->point(.3, v), torus.point(.3, v) + .1 * torus.normal(.3, v)) < 1e-7);
    }
    auto line = std::make_shared<Line<3>>(Vec3(2, 0, 0), Vec3(0, 0, 1));
    RevolutionSurface revLine(line, Vec3(), Vec3(0, 0, 1));
    auto lineOut = offsetSurface(revLine, .1, {0, kTwoPi}, {-1, 1}, 1e-7);
    FK_CHECK(distance(lineOut->point(.2, .4), revLine.point(.2, .4) + .1 * revLine.normal(.2, .4)) < 1e-7);
}

FK_TEST(OffsetValidationRotatedCircularConstructions) {
    for (double scale : {1., 1000.}) {
        for (const Vec3 &axis : {Vec3(0, 0, 1), Vec3(.2, .3, 1), Vec3(1, 1, 1)}) {
            const Frame3 frame(Vec3(), axis, Vec3(1, -.1, 0));
            auto line = [&](const Vec2 &a, const Vec2 &b) {
                return ProfileSegment{std::make_shared<Line<2>>(scale * a, scale * (b - a)),
                                      {0, scale * distance(a, b)}};
            };
            auto arc = [&](const Vec2 &center, double from, double to) {
                return ProfileSegment{std::make_shared<Circle<2>>(makeCircle(scale * center, 1.5 * scale)), {from, to}};
            };
            const std::vector<ProfileSegment> segments{line({1.5, 0}, {6.5, 0}), arc({6.5, 1.5}, -kHalfPi, 0),
                                                       line({8, 1.5}, {8, 3.5}), arc({6.5, 3.5}, 0, kHalfPi),
                                                       line({6.5, 5}, {1.5, 5}), arc({1.5, 3.5}, kHalfPi, kPi),
                                                       line({0, 3.5}, {0, 1.5}), arc({1.5, 1.5}, kPi, 1.5 * kPi)};
            const auto region = buildProfile(segments, 1e-9).regions.front();
            const Body prism = makeExtrusion(frame, region, 3 * scale);
            std::vector<FaceId> sides;
            for (FaceId f : prism.faces())
                if (prism.face(f).surface->type() != SurfaceType::Plane ||
                    std::fabs(dot(prism.face(f).surface->normal(0, 0), frame.zDir())) < .5)
                    sides.push_back(f);
            for (double d : {.4, -.4}) {
                const auto out = offsetFaces(prism, sides, d * scale);
                double area = 0;
                for (FaceId f : out.body.faces())
                    area += faceArea(out.body, f);
                const double expected = (2 * (8 + 5 - 4 * 1.5) + kTwoPi * (1.5 + d)) * 3;
                FK_CHECK_NEAR(area / (scale * scale), expected, 1e-9);
            }
            auto circle = std::make_shared<Circle<3>>(Vec3(), frame.xDir(), frame.yDir(), 2 * scale);
            ExtrusionSurface cylinder(circle, frame.zDir());
            const auto out = offsetSurface(cylinder, .1 * scale, {0, kTwoPi}, {0, 3 * scale}, 1e-7);
            FK_CHECK(static_cast<const ExtrusionSurface &>(*out).curve()->type() == CurveType::Circle);
            for (int i = 0; i < 100; ++i) {
                double u = kTwoPi * i / 100.;
                FK_CHECK(distance(out->point(u, scale),
                                  cylinder.point(u, scale) + .1 * scale * cylinder.normal(u, scale)) < 1e-7);
            }
        }
    }
    auto circle = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 2.);
    ExtrusionSurface oblique(circle, Vec3(.2, .1, 1));
    const auto out = offsetSurface(oblique, .1, {0, kTwoPi}, {0, 1}, 1e-7);
    FK_CHECK(static_cast<const ExtrusionSurface &>(*out).curve()->type() == CurveType::BSpline);
    for (int i = 0; i < 100; ++i) {
        double u = kTwoPi * i / 100.;
        FK_CHECK(distance(out->point(u, .5), oblique.point(u, .5) + .1 * oblique.normal(u, .5)) < 1e-7);
    }
}
