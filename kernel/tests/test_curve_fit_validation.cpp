#include "fk_cancel.h"
#include "fk_curve_fit_bounds.h"
#include "fk_nurbs.h"
#include "fk_offset.h"
#include "fk_surface.h"
#include "fk_test.h"
#include <limits>
using namespace ForgeCad::Kernel;
namespace {
// Single clamped degree-27 span from the independently reproduced K10 case.
// The hidden lobes vanish at the old acceptance samples; poles are the original
// finite IEEE coefficients, not a regenerated or sampled approximation.
CurvePtr<3> degree27(double scale, bool rational) {
    const std::vector<double> z = {0,
                                   0,
                                   0,
                                   .0023926594471666389,
                                   -.03158506535957388,
                                   .22274247758415189,
                                   -1.0588644597919092,
                                   3.7068614333740251,
                                   -10.150702528319336,
                                   22.626599248232065,
                                   -42.105538437516088,
                                   66.431608164324672,
                                   -89.673893632054543,
                                   104.07526656293439,
                                   -104.07526656293439,
                                   89.673893632054543,
                                   -66.431608164324672,
                                   42.105538437516088,
                                   -22.626599248232065,
                                   10.150702528319336,
                                   -3.7068614333740251,
                                   1.0588644597919092,
                                   -.22274247758415189,
                                   .03158506535957388,
                                   -.0023926594471666389,
                                   0,
                                   0,
                                   0};
    std::vector<Vec3> p;
    std::vector<double> w;
    for (int i = 0; i <= 27; ++i) {
        p.emplace_back(scale * i / 27., 0, scale * z[i]);
        if (rational)
            w.push_back(1 + .1 * std::sin(.4 * i));
    }
    std::vector<double> k(28, 0);
    k.insert(k.end(), 28, 1);
    return std::make_shared<BSplineCurve<3>>(27, k, p, w);
}
void checkFit(const CurvePtr<3> &curve, const Vec3 &dir, double d, double tol) {
    const Interval range = curve->domain();
    ExtrusionSurface surface(curve, dir);
    const auto bound = detail::extrusionOffsetErrorBound(curve, range, d, dir);
    FK_CHECK(bool(bound));
    const auto f = [&](double t) { return surface.point(t, 0) + d * surface.normal(t, 0); };
    double deviation = -123;
    const auto fitted = fitCurveCertified(f, range, curve->breakpoints(range), tol, bound, &deviation);
    FK_CHECK(fitted && deviation >= 0 && deviation <= tol);
    const auto offset = offsetSurface(surface, d, range, {0, 1}, tol);
    FK_CHECK(bool(offset));
    // Dense sampling is an independent regression check, not the certificate.
    // The whole-interval guarantee comes from the production error-bound oracle.
    double error = 0, surfaceError = 0;
    for (int i = 0; i <= 10000; ++i) {
        double t = range.lo + range.length() * i / 10000.;
        error = std::max(error, distance(f(t), fitted->point(t)));
        surfaceError = std::max(
            surfaceError, distance(surface.point(t, .5) + d * surface.normal(t, .5), offset->point(t, .5)));
    }
    FK_CHECK(error <= tol);
    FK_CHECK(surfaceError <= tol);
}
} // namespace
FK_TEST(CurveFitCertifiedDegree27OffsetPoseScaleRational) {
    for (double scale : {.001, 1., 1000.})
        for (bool rational : {false, true})
            for (bool pose : {false, true}) {
                CurvePtr<3> curve = degree27(scale, rational);
                Vec3 dir(0, 1, 0);
                if (pose) {
                    const auto transform = Transform3::translation(scale * Vec3(11, -7, 3)) *
                                           Transform3::rotation(Vec3(), Vec3(1, 2, 3), .71);
                    curve = std::make_shared<TransformedCurve>(curve, transform);
                    dir = Transform3::rotation(Vec3(), Vec3(1, 2, 3), .71).applyToVector(dir);
                }
                checkFit(curve, dir, .001 * scale, 1e-7 * scale);
            }
}
FK_TEST(CurveFitCertifiedNestedTrims) {
    CurvePtr<3> curve = degree27(1, true);
    curve = std::make_shared<TrimmedCurve<3>>(curve, .03, .97);
    curve = std::make_shared<TrimmedCurve<3>>(curve, .07, .91);
    checkFit(curve, Vec3(0, 1, 0), .001, 1e-7);
}
FK_TEST(CurveFitCertifiedInvalidAndAtomic) {
    const double inf = std::numeric_limits<double>::infinity(),
                 nan = std::numeric_limits<double>::quiet_NaN();
    const CurveFitErrorBound exact = [](const Interval &, const std::array<Vec3, 4> &) { return 0.; };
    for (double tolerance : {0., -1., inf, nan}) {
        int calls = 0;
        double deviation = -123;
        FK_CHECK_THROWS(fitCurveCertified(
            [&](double t) {
                ++calls;
                return Vec3(t, 0, 0);
            },
            {0, 1}, {}, tolerance, exact, &deviation));
        FK_CHECK(calls == 0 && deviation == -123);
    }
    for (Interval range : std::vector<Interval>{{0, 0}, {1, 0}, {0, inf}, {nan, 1}}) {
        int calls = 0;
        double deviation = -123;
        FK_CHECK_THROWS(fitCurveCertified(
            [&](double t) {
                ++calls;
                return Vec3(t, 0, 0);
            },
            range, {}, 1e-7, exact, &deviation));
        FK_CHECK(calls == 0 && deviation == -123);
    }
    for (double invalid : {nan, inf}) {
        double deviation = -123;
        FK_CHECK_THROWS(fitCurveCertified([&](double t) { return Vec3(t, invalid, 0); }, {0, 1}, {}, 1e-7,
                                          exact, &deviation));
        FK_CHECK(deviation == -123);
    }
    double deviation = -123;
    FK_CHECK_THROWS(fitCurveCertified([](double) -> Vec3 { throw std::domain_error("callback"); }, {0, 1}, {},
                                      1e-7, exact, &deviation));
    FK_CHECK(deviation == -123);
}
FK_TEST(CurveFitCertifiedUnresolvedAndCancellation) {
    const auto line = [](double t) { return Vec3(t, 0, 0); };
    for (double error :
         {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), -1.}) {
        double deviation = -123;
        int calls = 0;
        FK_CHECK_THROWS(fitCurveCertified(
            line, {0, 1}, {}, 1e-7,
            [&](const Interval &, const std::array<Vec3, 4> &) {
                ++calls;
                return error;
            },
            &deviation));
        FK_CHECK(calls > 0 && deviation == -123);
    }
    for (int phase : {0, 1, 2}) {
        auto token = std::make_shared<std::atomic<bool>>(phase == 0);
        CancellationScope scope(token);
        int callbacks = 0;
        double deviation = -123;
        bool cancelled = false;
        try {
            fitCurveCertified(
                [&](double t) {
                    ++callbacks;
                    if (phase == 1)
                        token->store(true);
                    return line(t);
                },
                {0, 1}, {}, 1e-7,
                [&](const Interval &, const std::array<Vec3, 4> &) {
                    if (phase == 2)
                        token->store(true);
                    return 0.;
                },
                &deviation);
        } catch (const OperationCancelled &) {
            cancelled = true;
        }
        FK_CHECK(cancelled && deviation == -123);
        if (phase == 0)
            FK_CHECK(callbacks == 0);
    }
}
FK_TEST(CurveFitCertifiedConicParameterNotAssumedPolynomial) {
    const auto circle = std::make_shared<Circle<3>>(Vec3(), Vec3(1, 0, 0), Vec3(0, 0, 1), 2.);
    // A rational conic conversion does not preserve the angular parameter.
    FK_CHECK(!detail::extrusionOffsetErrorBound(circle, {0, 1}, .01, Vec3(0, 1, 0)));
}

FK_TEST(CurveFitCertifiedRevolutionMeridian) {
    for (bool rational : {false, true}) {
        CurvePtr<3> curve =
            std::make_shared<TransformedCurve>(degree27(1, rational), Transform3::translation(Vec3(2, 0, 0)));
        curve = std::make_shared<TrimmedCurve<3>>(curve, .03, .97);
        const Vec3 axis(0, 0, 1);
        RevolutionSurface surface(curve, Vec3(), axis);
        const Interval range = curve->domain();
        const auto bound = detail::revolutionOffsetErrorBound(curve, range, .001, Vec3(), axis);
        FK_CHECK(bool(bound));
        const auto f = [&](double t) { return surface.point(0, t) + .001 * surface.normal(0, t); };
        double deviation = -123;
        const auto fit = fitCurveCertified(f, range, curve->breakpoints(range), 1e-7, bound, &deviation);
        const auto offset = offsetSurface(surface, .001, {.1, .8}, range, 1e-7);
        FK_CHECK(fit && offset && deviation >= 0 && deviation <= 1e-7);
        // Dense sampling is an independent regression check, not the certificate.
        // The whole-interval guarantee comes from the production error-bound oracle.
        double error = 0, surfaceError = 0;
        for (int i = 0; i <= 10000; ++i) {
            double t = range.lo + range.length() * i / 10000.;
            error = std::max(error, distance(f(t), fit->point(t)));
            surfaceError =
                std::max(surfaceError,
                         distance(surface.point(.4, t) + .001 * surface.normal(.4, t), offset->point(.4, t)));
        }
        FK_CHECK(error <= 1e-7);
        FK_CHECK(surfaceError <= 1e-7);
    }
}

FK_TEST(CurveFitLegacyInvalidInputAndAtomic) {
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (double tolerance : {0., -1., inf, nan}) {
        int callbacks = 0;
        double deviation = -123;
        FK_CHECK_THROWS(fitCurve(
            [&](double t) {
                ++callbacks;
                return Vec3(t, 0, 0);
            },
            {0, 1}, {}, tolerance, &deviation));
        FK_CHECK(callbacks == 0);
        FK_CHECK(deviation == -123);
    }
    for (double invalid : {nan, inf}) {
        double deviation = -123;
        FK_CHECK_THROWS(
            fitCurve([&](double t) { return Vec3(t, invalid, 0); }, {0, 1}, {}, 1e-7, &deviation));
        FK_CHECK(deviation == -123);
    }
    const CurveFitErrorBound exact = [](const Interval &, const std::array<Vec3, 4> &) { return 0.; };
    for (double invalid : {nan, inf, -inf}) {
        for (bool certified : {false, true}) {
            int callbacks = 0;
            double deviation = -123;
            const auto line = [&](double t) {
                ++callbacks;
                return Vec3(t, 0, 0);
            };
            if (certified) {
                FK_CHECK_THROWS(fitCurveCertified(line, {0, 1}, {invalid}, 1e-7, exact, &deviation));
            } else {
                FK_CHECK_THROWS(fitCurve(line, {0, 1}, {invalid}, 1e-7, &deviation));
            }
            FK_CHECK(callbacks == 0);
            FK_CHECK(deviation == -123);
        }
    }
}

namespace {
void checkExtractedOffset(const CurvePtr<3> &curve, bool revolution = false) {
    SurfacePtr surface = revolution
                             ? SurfacePtr(std::make_shared<RevolutionSurface>(curve, Vec3(), Vec3(0, 0, 1)))
                             : SurfacePtr(std::make_shared<ExtrusionSurface>(curve, Vec3(0, 1, 0)));
    Interval range = curve->domain();
    auto f = [&](double t) {
        return revolution ? surface->point(0, t) + .001 * surface->normal(0, t)
                          : surface->point(t, 0) + .001 * surface->normal(t, 0);
    };
    auto bound = revolution ? detail::revolutionOffsetErrorBound(curve, range, .001, {}, Vec3(0, 0, 1))
                            : detail::extrusionOffsetErrorBound(curve, range, .001, Vec3(0, 1, 0));
    FK_CHECK(bool(bound));
    double deviation = -1;
    auto result = fitCurveCertified(f, range, curve->breakpoints(range), 1e-7, bound, &deviation);
    FK_CHECK(deviation >= 0 && deviation <= 1e-7);
    double worst = 0;
    for (int i = 0; i <= 1000; ++i) {
        double t = range.lo + range.length() * i / 1000.;
        worst = std::max(worst, distance(result->point(t), f(t)));
    }
    FK_CHECK(worst <= deviation + 1e-12);
}
} // namespace

FK_TEST(CurveFitCertifiedRescaledWeightsTrimTransform) {
    for (double scale : {1., 1e-100, 1e100}) {
        auto curve = std::make_shared<BSplineCurve<3>>(
            2, std::vector<double>{0, 0, 0, 1, 1, 1}, std::vector<Vec3>{{3, 0, 0}, {3, 0, 1}, {2, 0, 1}},
            std::vector<double>{scale, scale * .7071067811865476, scale});
        checkExtractedOffset(curve);
        checkExtractedOffset(curve, true);
        auto trim = std::make_shared<TrimmedCurve<3>>(curve, .13, .87);
        checkExtractedOffset(trim);
        checkExtractedOffset(trim, true);
        auto moved = std::make_shared<TransformedCurve>(
            trim, Transform3::translation(Vec3(.1, .2, .3)) * Transform3::rotation({}, Vec3(0, 1, 0), .3));
        checkExtractedOffset(moved);
        checkExtractedOffset(moved, true);
    }
}
FK_TEST(CurveFitCertifiedMultipleSpansUnclamped) {
    auto curve = std::make_shared<BSplineCurve<3>>(
        3, std::vector<double>{0, 1, 2, 3, 4, 5, 6, 7, 8},
        std::vector<Vec3>{{2, 0, 0}, {2.1, 0, .3}, {2.4, 0, .5}, {2.5, 0, .8}, {2.8, 0, 1}});
    checkExtractedOffset(curve);
    checkExtractedOffset(curve, true);
}

FK_TEST(CurveFitCertifiedProviderDomainAndCancellation) {
    int callbacks = 0;
    double deviation = -123;
    FK_CHECK_THROWS(fitCurveCertified(
        [&](double t) {
            ++callbacks;
            return Vec3(t, 0, 0);
        },
        {0, 1}, {}, 1e-7, {}, &deviation));
    FK_CHECK(callbacks == 0 && deviation == -123);
    const auto curve = std::make_shared<BSplineCurve<3>>(1, std::vector<double>{0, 0, 1, 1},
                                                         std::vector<Vec3>{{0, 0, 0}, {1, 0, 0}});
    const auto bound = detail::extrusionOffsetErrorBound(curve, {0, 1}, .01, Vec3(0, 1, 0));
    FK_CHECK(bool(bound));
    FK_CHECK(!std::isfinite(bound({-1, 1}, {Vec3(), Vec3(), Vec3(), Vec3()})));
    const auto token = std::make_shared<std::atomic<bool>>(true);
    CancellationScope scope(token);
    bool cancelled = false;
    try {
        detail::extrusionOffsetErrorBound(curve, {0, 1}, .01, Vec3(0, 1, 0));
    } catch (const OperationCancelled &) {
        cancelled = true;
    }
    FK_CHECK(cancelled);
}

FK_TEST(CurveFitCertifiedClosedNurbsAcrossSeam) {
    const Circle<3> circle(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 1);
    const auto curve = std::make_shared<BSplineCurve<3>>(
        joinBezierPieces(rationalBezierPieces(circle, {0, kTwoPi}), {0, .5 * kPi, kPi, 1.5 * kPi, kTwoPi}));
    const ExtrusionSurface source(curve, Vec3(0, 0, 1));
    const Interval window{kTwoPi - .2, kTwoPi + .2};
    const auto result = offsetSurface(source, .001, window, {0, 1}, 1e-7);
    FK_CHECK(bool(result));
    // Independent dense check across the natural seam; the oracle supplies
    // the continuous bound, not these samples.
    double error = 0;
    for (int i = 0; i <= 10000; ++i) {
        const double t = window.lo + window.length() * i / 10000.;
        error = std::max(error,
                         distance(result->point(t, .5), source.point(t, .5) + .001 * source.normal(t, .5)));
    }
    FK_CHECK(error <= 1e-7);
}
